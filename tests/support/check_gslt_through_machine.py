#!/usr/bin/env python3
"""GSLT -> PeTTa -> transform, measured by running it.

    check_gslt_through_machine.py CETTA --corpus DIR [--keep DIR] [--report FILE]
                                  [--timeout SECONDS] [--turns STEPS COUNT]
                                  [--fire transform|direct] [--only NAME ...]

DIR holds invocation manifests made by record_petta_direct_run.sh. Replays
preserve the recorded command/profile, cwd, environment, input order and file
origins. Generated inputs removed by a gate are restored from checked bytes;
live inputs, executables and library contexts must still have their recorded
identities. The machine receives one transformed document per input document.

Comparison parses printed terms and retains string contents, variable sharing
and occurrence order. It is not a native value-kind/effect certificate. Pending,
failed and stale runs never qualify by agreeing with another failure.
"""
import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import resource
import tempfile

import gslt_invocation as invocation
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_petta_machine_corpus as corpus  # noqa: E402

whole, forms, swi_lane = corpus.whole, corpus.forms, corpus.swi_lane
ROOT = forms.ROOT
GENERATED = os.path.join("langdef", "petta", "generated")
QUIET = "(= (pm:quiet $done) (empty))\n(= (pm:marker) @@START)"


def with_compiled_programs(source, origin, imported):
    """Install definition-only generated imports in the guest machine too.

    Executable module imports keep the host's module protocol; they are not
    flattened into a document with a different declaration/effect order.
    """
    result, bang = [], False
    for form in swi_lane.read_forms(source):
        if form == "!":
            bang = True
            continue
        inline = None
        if bang and isinstance(form, list) and len(form) == 3 and form[:2] == ["import!", "&self"]:
            target = form[2]
            if isinstance(target, str) and not target.startswith(("$", '"', "r#")):
                for base in (os.path.dirname(origin), ROOT):
                    for suffix in ("", ".metta"):
                        path = os.path.realpath(os.path.join(base, target + suffix))
                        if os.path.isfile(path) and GENERATED in os.path.relpath(path, ROOT):
                            with open(path) as stream:
                                definitions = swi_lane.read_forms(stream.read())
                            if "!" not in definitions:
                                inline = [] if path in imported else definitions
                                imported.add(path)
                            break
                    if inline is not None:
                        break
        if inline is not None:
            result.extend(inline)
            result.extend(["!", ["quote", "true"]])
        else:
            if bang:
                result.append("!")
            result.append(form)
        bang = False
    return swi_lane.write_forms(result)


def through_the_machine(source, lane_import, initialize=True):
    """SOURCE with its equations and declarations given to the machine and its
    queries asked of the machine."""
    lines = [lane_import, corpus.MACHINE, corpus.ASK, QUIET, "!(pm:hide-loaded!)"] if initialize else []
    if initialize and corpus.FIRE == "direct":
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
    if initialize:
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


def structural_text(lines):
    """Parse displayed terms, preserving string text and variable sharing.

    This is a printed-term observation, not a native value-kind certificate:
    opaque values and effects still require an evaluator observation channel.
    """
    names, result = {}, []
    for line in lines:
        try:
            parsed = swi_lane.read_forms(line)
        except (ValueError, IndexError):
            result.append((("text", line),))
            continue
        if len(parsed) != 1:
            result.append((("text", line),))
            continue
        events, todo = [], [parsed[0]]
        while todo:
            item = todo.pop()
            if item is None:
                events.append(("close", ""))
            elif isinstance(item, list):
                events.append(("open", ""))
                todo.append(None)
                todo.extend(reversed(item))
            elif item.startswith("$"):
                events.append(("variable", names.setdefault(item, len(names))))
            else:
                events.append(("literal", item))
        result.append(tuple(events))
    return result


def run_invocation(record, arguments, timeout):
    def limit():
        resource.setrlimit(resource.RLIMIT_CPU, (timeout, timeout + 5))
    return swi_lane.run_group(record["command"] + arguments, record["cwd"], timeout * 6,
                              env=invocation.replay_environment(record), limit=limit)


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
    parser.add_argument("--fire", choices=["transform", "direct"], default="transform",
                        help="how the machine's rules are fired")
    parser.add_argument("--lane", choices=["native", "specs"], default="native",
                        help="production transform library or its MeTTa specification")
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    corpus.FIRE = args.fire
    corpus.TURNS = tuple(args.turns)
    keep = os.path.abspath(args.keep)
    os.makedirs(keep, exist_ok=True)
    lane_import = swi_lane.base.LIBRARY_IMPORT if args.lane == "native" else swi_lane.base.SPECS_IMPORT
    files = sorted(name for name in os.listdir(args.corpus) if name.endswith(".invocation.json"))
    if args.only:
        files = [name for name in files if any(name.startswith(prefix) for prefix in args.only)]
    if not files:
        print(f"FAIL: no invocation records in {args.corpus}")
        return 1
    rows = []
    implementation = invocation.digest(json.dumps([
        (path.name, invocation.digest(path.read_bytes()))
        for path in sorted(Path(__file__).parent.glob("*.py"))]).encode())
    for name in files:
        stem = name[:-len(".invocation.json")]
        origin = os.path.abspath(os.path.join(args.corpus, name))
        checkpoint = Path(keep) / (stem + ".qualification.json")
        fingerprint = invocation.digest(json.dumps([
            invocation.digest(Path(origin).read_bytes()), implementation,
            args.fire, args.lane, args.turns, args.timeout]).encode())
        try:
            with invocation.restored_inputs(origin) as record, ExitStack() as cleanup:
                if record["process"].get("state") != "exited" or record["process"].get("returncode") != 0:
                    raise ValueError("source invocation did not complete successfully")
                if record.get("source_gate") and record.get("source_gate_status") != 0:
                    raise ValueError("source gate did not complete successfully")
                if not record["sources"]:
                    raise ValueError("invocation has no recorded source documents")
                if checkpoint.exists():
                    prior = json.loads(checkpoint.read_text())
                    if prior.get("fingerprint") == fingerprint and prior["row"][1] == "same-printed-structure":
                        rows.append(tuple(prior["row"]))
                        print(f"REUSE: {stem}", flush=True)
                        continue
                code, out, err = run_invocation(record, record["arguments"], args.timeout)
                written = answers(out, False)
                machine_arguments = list(record["arguments"])
                imported = set()
                for index, item in enumerate(record["sources"]):
                    source_path = item["path"]
                    with open(source_path) as stream:
                        source = with_compiled_programs(stream.read(), source_path, imported)
                    transformed = through_the_machine(source, lane_import, initialize=index == 0)
                    with tempfile.NamedTemporaryFile(mode="w", prefix=".gslt-machine-", suffix=".metta",
                            dir=os.path.dirname(source_path), delete=False) as stream:
                        stream.write(transformed)
                        machine_path = stream.name
                    cleanup.callback(os.unlink, machine_path)
                    machine_arguments[item["argument"]] = machine_path
                    with open(os.path.join(keep, f"{stem}.{index}.machine.metta"), "w") as stream:
                        stream.write(transformed)
                mcode, mout, merr = run_invocation(record, machine_arguments, args.timeout)
                got = answers(mout, True)
        except (OSError, ValueError) as error:
            rows.append((stem, "unqualified-invocation", 0, str(error)))
            print(f"unqualified-invocation {stem}: {error}", flush=True)
            continue
        for suffix, text in (("written.out", out), ("written.err", err),
                             ("machine.out", mout), ("machine.err", merr)):
            with open(os.path.join(keep, f"{stem}.{suffix}"), "w") as stream:
                stream.write(text)
        a, b = structural_text(written), structural_text(got)
        host_error = uncaught(err)
        held = next((line[len("(pm:uncaught "):-1] for line in got
                     if line.startswith("(pm:uncaught ")), None)
        unended = (mcode == 124 or mcode < 0
                   or any(line.startswith("(pm:no-end ") for line in got))
        if code == 124 or code < 0:
            verdict = "host-not-ended"
        elif unended:
            verdict = "not-ended"
        elif code != 0 or host_error is not None:
            verdict = "host-failed"
        elif mcode != 0 or held is not None:
            verdict = "machine-failed"
        elif a == b:
            verdict = "same-printed-structure"
        else:
            verdict = "DIFFER"
        note = ""
        if verdict in ("DIFFER", "not-ended"):
            first = lambda text: next((line for line in text.splitlines() if line.strip()), "")[:110]
            note = (f"written exit {code}, {len(written)} lines {first(err)!r} | "
                    f"machine exit {mcode}, {len(got)} lines {first(merr)!r}")
        rows.append((stem, verdict, len(written), note))
        invocation.save_json(checkpoint, dict(fingerprint=fingerprint, row=rows[-1]))
        print(f"{verdict:14s} {stem:62s} {len(written):4d} lines  {note}", flush=True)
    by = {}
    for row in rows:
        by[row[1]] = by.get(row[1], 0) + 1
    summary = (f"programs {len(rows)}: " + ", ".join(f"{kind} {count}" for kind, count in sorted(by.items()))
               + f" (rules fired by {'the host' if args.fire == 'direct' else args.lane + ' transform'}, "
               f"turns of {args.turns[0]} x {args.turns[1]}, {args.timeout} s)")
    if args.report:
        with open(args.report, "w") as stream:
            for stem, verdict, count, note in rows:
                stream.write(f"{verdict:14s} {stem:62s} {count:4d} lines  {note}\n")
            stream.write(summary + "\n")
    failed = [row for row in rows if row[1] != "same-printed-structure"]
    if failed:
        print("FAIL: " + summary)
        return 1
    print("PASS: recorded invocations agree as ordered printed structures: " + summary)
    print("Native value kinds and separate effect observations remain unqualified by this checker.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
