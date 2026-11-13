#!/usr/bin/env python3
"""Whole PeTTa programs rewritten to transform (lib/petta/transform_only.metta),
judged by the programs' own tests.

Usage: check_petta_transform_only.py CETTA_BINARY --corpus DIR_OR_FILE ...
           [--lane native|specs] [--leave GROUP ...] [--also GROUP ...] [--timeout SECONDS]
           [--jobs N] [--report FILE] [--require-all]

Each program of the corpus holds `!(test GOT WANT)` queries; a run prints one
check mark for each that holds.  A program is rewritten form by form by
lib/petta/transform_only.metta and run again; it passes when it prints as many
check marks as the program as written and no failed test.  A program whose
written run prints no check mark, or does not finish, is not counted.

    --lane native   transform is the native operation
    --lane specs    transform is lib_transform_specs, written in PeTTa
    --leave GROUP   leave a group as written: space, calls
    --also GROUP    rewrite a group that is left to the evaluator otherwise:
                    choice, binding, branching

The specs lane imports a copy of the specification with is-alpha-member written
out while that operation evaluates a value again (check_petta_by_transform
describes the defect and probes for it).
"""
import argparse
import concurrent.futures
import glob
import os
import resource
import signal
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_petta_by_transform as forms  # noqa: E402
import check_transform_swi_differential as swi_lane  # noqa: E402

ROOT = forms.ROOT
HEADER = [forms.TRANSFORM, forms.FORMS, "!(import! &self (library transform_only))"]
QUIET = ("true", "done", "known", "skipped", "()", "[()]", "")


def run(binary, path, timeout, cwd=None):
    """Run one program.  TIMEOUT bounds the processor time the program may use,
    so that the verdict does not depend on what else the machine is doing; a
    wall-clock bound several times as long stops a program that waits."""
    def limit():
        resource.setrlimit(resource.RLIMIT_CPU, (timeout, timeout + 5))
    code, out, err = swi_lane.run_group([binary, "--lang", "petta", path], cwd or ROOT, timeout * 6,
                                        env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1"),
                                        limit=limit)
    if code in (-signal.SIGXCPU, -signal.SIGKILL):
        return 124, "", "timeout"
    return code, out, err


def options(leave, also):
    return [f"!(to:leave {g})" for g in leave] + [f"!(to:also {g})" for g in also]


def imported_file(form, base):
    """The file an `(import! &self TARGET)` form names, when this check can find
    it: a library of this tree, or a path beside the program.  None otherwise."""
    if not (isinstance(form, list) and len(form) == 3 and form[0] == "import!"
            and form[1] == "&self"):
        return None
    target = form[2]
    if isinstance(target, list) and len(target) == 2 and target[0] == "library":
        candidates = [os.path.join(ROOT, "lib", "petta", str(target[1]) + ".metta")]
    elif isinstance(target, str):
        name = target.strip('"')
        candidates = [os.path.join(base, name), os.path.join(base, name + ".metta")]
    else:
        return None
    return next((os.path.abspath(c) for c in candidates if os.path.isfile(c)), None)


def with_imports(source, base, seen):
    """The forms of SOURCE with each import this check can find replaced by the
    forms of the file it names, once for each file: the program with its
    dependencies, as one list of forms."""
    out, bang = [], False
    for form in swi_lane.read_forms(source):
        if form == "!":
            bang = True
            continue
        path = imported_file(form, base) if bang else None
        if path is None:
            out.append((bang, form))
        elif path not in seen:
            seen.add(path)
            with open(path) as stream:
                out += with_imports(stream.read(), os.path.dirname(path), seen)
        bang = False
    return out


def rewritten(binary, source, leave, also, work, stem, base=None):
    """SOURCE rewritten by transform_only: (text, error).  With BASE, the
    directory of the program, its imports are rewritten with it."""
    lines = [forms.FORMS, "!(import! &self (library transform_only))"] + options(leave, also)
    listed = with_imports(source, base, set()) if base else None
    stream_of_forms = []
    if listed is None:
        for form in swi_lane.read_forms(source):
            stream_of_forms.append(form)
    else:
        for bang, form in listed:
            if bang:
                stream_of_forms.append("!")
            stream_of_forms.append(form)
    bang = False
    for form in stream_of_forms:
        if form == "!":
            bang = True
            continue
        # as text: a form written into the driver as code has its lambdas named
        # by the driver before the rewriting sees it
        text = swi_lane.write_form(form).replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'!(to:query-text "{text}")' if bang else f'!(to:statement-text "{text}")')
        bang = False
    driver = os.path.join(work, stem + ".driver.metta")
    with open(driver, "w") as stream:
        stream.write("\n".join(lines) + "\n")
    code, out, err = run(binary, driver, 120)
    if code != 0:
        return None, f"the rewriting stopped with status {code}: {err.strip()[:200]}"
    body = []
    for line in out.splitlines():
        line = line.strip()
        if line in QUIET:
            continue
        body.append("!" + line[len("(to:bang "):-1] if line.startswith("(to:bang ") else line)
    header = HEADER + options(leave, also)
    return "\n".join(header + body) + "\n", None


EAGER_PROBE = (forms.TRANSFORM + "\n!(bind! &r (new-space))\n!(bind! &w (new-space))\n"
               "!(add-atom &r (r 1))\n!(add-atom &r (r 2))\n"
               "!(probe (collapse (let $k (transform &r (, (r $x) (absent &w (stop))) $x)"
               " (let $_ (add-atom &w (stop)) $k))))\n")


def later_branch_fires_unguarded(binary, work):
    """The native firing runs every branch at the call: a later branch answers
    although the caller has stored the atom whose absence it requires."""
    _, out, _ = forms.cetta(binary, EAGER_PROBE, work, "probe-eager")
    return forms.probe_line(out) != "(probe (1))"


ROW_PROBE = (forms.TRANSFORM + "\n!(bind! &r (new-space))\n!(add-atom &r (kept (+ 41 1)))\n"
             "(= (rows-of $space) (transform $space (read $atom) $atom))\n"
             "!(probe (collapse (rows-of &r)))\n")


def stored_row_evaluated_in_a_function(binary, work):
    """A transform that reads a row answers the row as stored when it is called
    at the top level; called in a function's body, the native firing evaluates
    the row it matched.  A program's own store is read by such functions."""
    _, out, _ = forms.cetta(binary, ROW_PROBE, work, "probe-row")
    return forms.probe_line(out) != "(probe ((kept (+ 41 1))))"


# Fixture programs the native lane is known not to pass, each with the engine
# defect that explains it.  A program is excused only while the probe of its
# defect finds the defect; with the defect gone it must pass like the others.
NATIVE_DEFECTS = {
    "control_and_choice": ("stored code is evaluated in a branch", forms.stored_code_evaluated),
    "cut": ("a later branch fires after its guard stopped holding", later_branch_fires_unguarded),
    "own_space": ("a row read by a transform in a function's body is evaluated",
                  stored_row_evaluated_in_a_function),
    "own_space_whole": ("a row read by a transform in a function's body is evaluated",
                        stored_row_evaluated_in_a_function),
    "own_space_later_code": ("a row read by a transform in a function's body is evaluated",
                             stored_row_evaluated_in_a_function),
}


def results(output):
    """A program's tests in the order they ran: each line a test prints, with
    what it observed and what it expected, variables named in order."""
    return [swi_lane.variables_named(line.strip()) for line in output.splitlines()
            if "✅" in line or "❌" in line]


def held(written, got):
    """How many tests of the program as written hold again, test for test: the
    same line (the same observation and expectation) at the same place."""
    return sum(1 for a, b in zip(written, got) if a == b and "✅" in a)


def run_on_swi(swi, source, timeout, work, name):
    path = os.path.join(work, name + ".metta")
    with open(path, "w") as stream:
        stream.write(source)
    def limit():
        resource.setrlimit(resource.RLIMIT_CPU, (timeout, timeout + 5))
    code, out, err = swi_lane.run_group(["sh", "run.sh", path, "--silent"], swi, timeout * 6,
                                        limit=limit)
    if code in (-signal.SIGXCPU, -signal.SIGKILL, 128 + signal.SIGXCPU, 128 + signal.SIGKILL):
        return 124, "", "timeout"
    return code, out, err


def judge(binary, path, lane_import, leave, also, timeout, work, swi=None, imports=False):
    stem = os.path.basename(path)[:-len(".metta")]
    if swi:
        with open(path) as stream:
            code, out, _ = run_on_swi(swi, stream.read(), timeout, work, stem + ".written")
    else:
        code, out, _ = run(binary, path, timeout, cwd=os.path.dirname(path))
    written = results(out)
    passed = sum(1 for line in written if "✅" in line)
    if code != 0 or passed == 0:
        return stem, "not-counted", passed, 0, 0, "written run: status %d, %d tests" % (code, passed)
    with open(path) as stream:
        source = stream.read()
    text, error = rewritten(binary, source, leave, also, work, stem,
                            base=os.path.dirname(path) if imports else None)
    if text is None:
        return stem, "not-rewritten", passed, 0, 0, error
    if swi:
        by_path = (text.replace(forms.TRANSFORM, lane_import)
                   .replace(forms.FORMS, f"!(import! &self {forms.FORMS_FILE})\n!(pt:variant! swi)")
                   .replace("!(import! &self (library transform_only))",
                            f"!(import! &self {os.path.join(ROOT, 'lib', 'petta', 'transform_only.metta')})"))
        code, out, err = run_on_swi(swi, by_path, timeout, work, stem + ".rewritten")
    else:
        target = os.path.join(os.path.dirname(path), f".transform-only-{os.getpid()}-{stem}.metta")
        try:
            with open(target, "w") as stream:
                stream.write(text.replace(forms.TRANSFORM, lane_import))
            code, out, err = run(binary, target, timeout, cwd=os.path.dirname(path))
        finally:
            if os.path.exists(target):
                os.remove(target)
    got = results(out)
    same = held(written, got)
    bad = sum(1 for line in got if "❌" in line)
    if code == 124:
        verdict = "timeout"
    elif code == 0 and got == written:
        verdict = "all-pass"
    elif same > 0:
        verdict = "some"
    else:
        verdict = "none"
    note = ""
    if verdict != "all-pass":
        first = next((f"test {at + 1}: {b[:150]}" for at, (a, b) in enumerate(zip(written, got))
                      if a != b), None)
        errors = [l for l in (err + "\n" + out).splitlines() if "rror" in l]
        if first is None and len(got) != len(written):
            first = f"{len(got)} tests ran of {len(written)}"
        note = first or (errors[0][:200] if errors else f"status {code}")
    return stem, verdict, passed, same, bad, note


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--corpus", nargs="+", required=True)
    parser.add_argument("--lane", choices=["native", "specs", "swi"], default="native")
    parser.add_argument("--swi", help="for --lane swi: a SWI-PeTTa tree with run.sh; the program as "
                                      "written and the rewritten program both run there, with "
                                      "transform written in PeTTa")
    parser.add_argument("--leave", nargs="*", default=[])
    parser.add_argument("--also", nargs="*", default=[])
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report")
    parser.add_argument("--imports", action="store_true",
                        help="rewrite the libraries and files a program imports with it, where "
                             "this check can find them, instead of importing them as written")
    parser.add_argument("--require-all", action="store_true")
    parser.add_argument("--known", help="a file of programs known not to pass, each with its "
                                        "reason; with --require-all only these may fail")
    parser.add_argument("--excuse-native-defects", action="store_true",
                        help="with --require-all on the native lane: a program listed in "
                             "NATIVE_DEFECTS may fail while its defect's probe finds the defect")
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    files = []
    for entry in args.corpus:
        files += sorted(glob.glob(os.path.join(os.path.abspath(entry), "*.metta"))) \
            if os.path.isdir(entry) else [os.path.abspath(entry)]
    files = [f for f in files if not os.path.basename(f).startswith(".transform-only-")]
    excused = {}
    if args.known:
        with open(args.known) as stream:
            for line in stream:
                if line.strip() and not line.startswith("#"):
                    stem, _, reason = line.rstrip("\n").partition("\t")
                    excused[stem] = reason
    listed = dict(excused)
    with tempfile.TemporaryDirectory(prefix="cetta-transform-only-") as work:
        if args.excuse_native_defects and args.lane == "native":
            for stem, (defect, probe) in NATIVE_DEFECTS.items():
                if probe(binary, work):
                    excused[stem] = defect
        lane_import = forms.TRANSFORM
        swi = os.path.abspath(args.swi) if args.lane == "swi" else None
        if args.lane == "swi" and not (args.swi and os.access(os.path.join(swi, "run.sh"), os.X_OK)):
            print("FAIL: --lane swi needs --swi naming a SWI-PeTTa tree with an executable run.sh")
            return 2
        if args.lane == "native":
            pass
        elif args.lane == "swi":
            lane_import = f"!(import! &self {swi_lane.one_declaration(work, 'lib_transform_specs')})"
        else:
            lane_import = swi_lane.base.SPECS_IMPORT
            if forms.value_evaluated_again(binary, work):
                derived = forms.specs_with_membership(work)
                if not derived:
                    print("FAIL: lib_transform_specs no longer uses is-alpha-member as expected")
                    return 1
                lane_import = f"!(import! &self {derived})"
        # SWI-PeTTa's hyperpose runs its branches in threads: definitions made
        # while the program runs are critical sections there.
        also = args.also + (["parallel"] if args.lane == "swi" else [])
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            rows = list(pool.map(
                lambda path: judge(binary, path, lane_import, args.leave, also, args.timeout,
                                   work, swi, args.imports),
                files))
    counted = [row for row in rows if row[1] not in ("not-counted",)]
    by = {}
    for row in counted:
        by[row[1]] = by.get(row[1], 0) + 1
    tests = sum(row[2] for row in counted)
    held = sum(min(row[3], row[2]) for row in counted)
    lines = [f"{row[1]:13s} {row[0]:44s} tests {row[2]:3d} held {row[3]:3d} failed {row[4]:2d}  {row[5]}"
             for row in rows]
    summary = (f"programs {len(files)}, counted {len(counted)}: " +
               ", ".join(f"{k} {v}" for k, v in sorted(by.items())) +
               f"; tests held {held} of {tests} (lane {args.lane}"
               + (f", left as written: {' '.join(args.leave)}" if args.leave else "")
               + (f", also rewritten: {' '.join(args.also)}" if args.also else "") + ")")
    if args.report:
        with open(args.report, "w") as stream:
            stream.write("\n".join(lines) + "\n" + summary + "\n")
    for line in lines:
        if not line.startswith(("all-pass", "not-counted")):
            print(line)
    print(summary)
    failing = [row[0] for row in counted if row[1] != "all-pass"]
    for stem in failing:
        if stem in listed:
            print(f"  known: {stem}: {listed[stem]}")
        elif stem in excused:
            print(f"  engine defect present: {stem} is not required on the native lane: {excused[stem]}")
    for stem in sorted(listed):
        if stem not in failing and any(row[0] == stem for row in counted):
            print(f"  listed as known and passing in this run: {stem}")
    if len(counted) != len(rows) and args.require_all and not args.known:
        print("FAIL: a program does not run as written")
        return 1
    if args.require_all and any(stem not in excused for stem in failing):
        print("FAIL: a rewritten program does not pass the tests it passes as written")
        return 1
    if args.require_all:
        print(f"PASS: {len(counted) - len(failing)} of {len(counted)} programs pass, rewritten, "
              f"every test they pass as written (lane {args.lane})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
