#!/usr/bin/env python3
"""GSLT -> PeTTa -> transform, measured by running it.

    check_gslt_through_machine.py CETTA --corpus DIR [--keep DIR] [--report FILE]
                                  [--timeout SECONDS] [--turns STEPS COUNT]
                                  [--fire transform|direct] [--only NAME ...]

DIR holds PeTTa programs that the direct GSLT compiler's gates run, each with
its queries, one program a file (tests/support/record_petta_direct_run.sh keeps
them while the gates run).  Each program runs as written on the PeTTa lane and
again through lib/petta/petta_machine.metta, a PeTTa evaluator whose every step
is a transform rule: the program's equations and declarations are given to the
machine as text and every `!E` is asked of the machine.  The printed answers
are compared, as lists and as bags, variables renamed in order of first
appearance.

A program that imports a compiled program is run with that program's text in
the import's place, so that the compiled equations are the machine's and not
the host's.

Verdicts: same, same-bag (the same answers in another order), same-error (both
runs end with the same uncaught error), not-ended (the machine did not end
within its turns or its time; a budget that runs out is an outcome of its own,
never an answer), DIFFER.  The check fails on DIFFER and on not-ended.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_petta_machine_corpus as corpus  # noqa: E402

whole, forms, swi_lane = corpus.whole, corpus.forms, corpus.swi_lane
ROOT = forms.ROOT
GENERATED = os.path.join("langdef", "petta", "generated")
QUIET = "(= (pm:quiet $done) (empty))\n(= (pm:marker) @@START)"
VAR = re.compile(r"\$[^\s()]+")
IMPORT = re.compile(r"^!\(import!\s+&self\s+(\S+?)\)\s*$", re.M)


STOOD = re.compile(r"^; recorded-from (.+)$", re.M)


def with_compiled_programs(source, origin):
    """SOURCE with each import of a compiled program replaced by that program,
    and each import of a file named relative to where a recorded file stood
    made a name that holds from anywhere."""
    def stood_at(position):
        places = [m for m in STOOD.finditer(source, 0, position)]
        return places[-1].group(1).strip() if places else None

    def inline(match):
        target = match.group(1)
        place = stood_at(match.start())
        if place and GENERATED not in target:
            for suffix in ("", ".metta"):
                path = os.path.normpath(os.path.join(place, target + suffix))
                if ("/" in target or target.startswith(".")) and os.path.isfile(path):
                    return f"!(import! &self {path})"
        # A gate's query names the compiled program relative to its own place in
        # the tree; the kept copy is elsewhere, so the name is read from the
        # compiled programs' directory on.
        at = target.find(GENERATED)
        candidates = [os.path.join(ROOT, target[at:])] if at >= 0 else []
        candidates += [os.path.join(base, target) for base in (ROOT, os.path.dirname(origin))]
        for candidate in candidates:
            for suffix in ("", ".metta"):
                path = os.path.normpath(candidate + suffix)
                if os.path.isfile(path) and GENERATED in os.path.relpath(path, ROOT):
                    with open(path) as stream:
                        return stream.read()
        return match.group(0)
    return IMPORT.sub(inline, source)


def through_the_machine(source, lane_import):
    """SOURCE with its equations and declarations given to the machine and its
    queries asked of the machine."""
    lines = [lane_import, corpus.MACHINE, corpus.ASK, QUIET, "!(pm:hide-loaded!)"]
    if corpus.FIRE == "direct":
        lines.append("!(pm:quiet (pm:lane! direct))")
    read = swi_lane.read_forms(source)
    bang = False
    for form in read:
        if form == "!":
            bang = True
            continue
        if not bang and corpus.is_arrow(form):
            lines.append("!(pm:quiet (pm:type-text %s))" % corpus.as_text(form))
        bang = False
    lines.append("!(pm:marker)")
    bang = False
    for form in read:
        if form == "!":
            bang = True
            continue
        if bang:
            before, answers = corpus.asked(corpus.as_text(form))
            lines.extend(before)
            lines.append("!" + answers)
        else:
            lines.append(swi_lane.write_form(form))
            if corpus.is_equation(form):
                lines.append("!(pm:quiet (pm:program-text %s))" % corpus.as_text(form))
            elif corpus.is_declaration(form):
                lines.append("!(pm:quiet (pm:type-text %s))" % corpus.as_text(form))
        bang = False
    return "\n".join(lines) + "\n"


def renamed(lines):
    names = {}

    def name(match):
        return names.setdefault(match.group(0), "$v%d" % len(names))
    return [VAR.sub(name, line) for line in lines]


def answers(out, marked):
    lines = out.splitlines()
    if marked:
        at = [i for i, line in enumerate(lines) if line.strip() == "@@START"]
        lines = lines[at[0] + 1:] if at else lines
    return [line.rstrip() for line in lines if line.strip() != ""]


def uncaught(err):
    return next((line.split("uncaught PeTTa error: ", 1)[1].strip() for line in err.splitlines()
                 if "uncaught PeTTa error: " in line), None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--keep", required=True, help="where the runs' files are written")
    parser.add_argument("--report")
    parser.add_argument("--timeout", type=int, default=900,
                        help="processor seconds for each run (the longest program of the "
                             "corpus fires a million rules in about 300)")
    parser.add_argument("--turns", nargs=2, type=int, metavar=("STEPS", "COUNT"),
                        default=[100000, 60],
                        help="drive each query in COUNT turns of STEPS rules")
    parser.add_argument("--fire", choices=["transform", "direct"], default="direct",
                        help="how the machine's rules are fired")
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    corpus.FIRE = args.fire
    corpus.TURNS = tuple(args.turns)
    keep = os.path.abspath(args.keep)
    os.makedirs(keep, exist_ok=True)
    lane_import = swi_lane.base.SPECS_IMPORT
    if forms.value_evaluated_again(binary, keep):
        derived = forms.specs_with_membership(keep)
        if not derived:
            print("FAIL: lib_transform_specs no longer uses is-alpha-member as expected")
            return 1
        lane_import = f"!(import! &self {derived})"
    files = sorted(name for name in os.listdir(args.corpus)
                   if name.endswith(".metta") and not name.startswith("."))
    if args.only:
        files = [name for name in files if any(name.startswith(prefix) for prefix in args.only)]
    if not files:
        print(f"FAIL: no program in {args.corpus}")
        return 1
    rows = []
    for name in files:
        stem = name[:-len(".metta")]
        origin = os.path.abspath(os.path.join(args.corpus, name))
        with open(origin) as stream:
            source = with_compiled_programs(stream.read(), origin)
        written_path = os.path.join(keep, f"{stem}.written.metta")
        with open(written_path, "w") as stream:
            stream.write(source)
        code, out, err = whole.run(binary, written_path, args.timeout, cwd=ROOT)
        written = answers(out, False)
        machine_path = os.path.join(keep, f"{stem}.machine.metta")
        with open(machine_path, "w") as stream:
            stream.write(through_the_machine(source, lane_import))
        mcode, mout, merr = whole.run(binary, machine_path, args.timeout, cwd=ROOT)
        got = answers(mout, True)
        for suffix, text in (("written.out", out), ("written.err", err),
                             ("machine.out", mout), ("machine.err", merr)):
            with open(os.path.join(keep, f"{stem}.{suffix}"), "w") as stream:
                stream.write(text)
        a, b = renamed(written), renamed(got)
        host_error = uncaught(err)
        held = next((line[len("(pm:uncaught "):-1] for line in got
                     if line.startswith("(pm:uncaught ")), None)
        unended = (mcode == 124 or mcode < 0
                   or any(line.startswith("(pm:no-end pm:turn-over)") for line in got))
        if code == 124:
            verdict = "host-not-ended"
        elif unended:
            verdict = "not-ended"
        elif host_error is not None or held is not None:
            same_error = (host_error is not None and held is not None
                          and renamed([host_error]) == renamed([held]))
            verdict = "same-error" if same_error else "DIFFER"
        elif code == mcode and a == b:
            verdict = "same"
        elif code == mcode and sorted(a) == sorted(b):
            verdict = "same-bag"
        else:
            verdict = "DIFFER"
        note = ""
        if verdict in ("DIFFER", "not-ended"):
            first = lambda text: next((line for line in text.splitlines() if line.strip()), "")[:110]
            note = (f"written exit {code}, {len(written)} lines {first(err)!r} | "
                    f"machine exit {mcode}, {len(got)} lines {first(merr)!r}")
        rows.append((stem, verdict, len(written), note))
        print(f"{verdict:14s} {stem:62s} {len(written):4d} lines  {note}", flush=True)
    by = {}
    for row in rows:
        by[row[1]] = by.get(row[1], 0) + 1
    summary = (f"programs {len(rows)}: " + ", ".join(f"{kind} {count}" for kind, count in sorted(by.items()))
               + f" (rules fired by {'the host' if args.fire == 'direct' else 'transform'}, "
               f"turns of {args.turns[0]} x {args.turns[1]}, {args.timeout} s)")
    if args.report:
        with open(args.report, "w") as stream:
            for stem, verdict, count, note in rows:
                stream.write(f"{verdict:14s} {stem:62s} {count:4d} lines  {note}\n")
            stream.write(summary + "\n")
    failed = [row for row in rows if row[1] in ("DIFFER", "not-ended", "host-not-ended")]
    if failed:
        print("FAIL: " + summary)
        return 1
    print("PASS: every program gives through the machine of transform rules the answers "
          "it gives on the PeTTa lane: " + summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
