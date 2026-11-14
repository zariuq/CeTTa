#!/usr/bin/env python3
"""Example programs through the machine of transform rules.

    check_petta_machine_corpus.py CETTA --corpus DIR... [--lane specs|native]
                                  [--timeout S] [--jobs N] [--report FILE]

lib/petta/petta_machine.metta is a PeTTa evaluator whose every step is a
transform rule.  This check measures how much of a directory of PeTTa programs
it evaluates as the host does.  Each program runs twice:

  as written;
  with its equations also loaded as the machine's clause rows, as they are
  met, its function declarations given to the machine first, and each
  `!(test E EXPECTED)` asked of the machine:
  `!(test (pm:of-text "E") EXPECTED)`, which gives the machine's answers of E
  one after another.  The machine is given equations, declarations and
  expressions as text, read by `parse`.  Every other form runs as written, on
  the host.

A test counts when the same line (the same observation and expectation) stands
at the same place as in the program as written and holds there.  A program
that does not run as written, or passes none of its tests as written, is not
counted.  Nothing is required: the check reports what the machine covers and,
for each program it does not, the first test that differs.  An equation the
program adds to &self or removes from it at its top level is given to the
machine as well.
"""
import argparse
import concurrent.futures
import glob
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_petta_transform_only as whole  # noqa: E402

forms, swi_lane = whole.forms, whole.swi_lane
MACHINE = "!(import! &self (library petta_machine))"
# The machine is given the program as text.  A form written into the driver as
# code would have its lambdas named by the host before the machine sees it.
ASK = """(= (pm:of-text $text)
   (let $expression (parse $text)
     (let $answers (pm:answers $expression) (superpose $answers))))
(= (pm:program-text $text)
   (let $equation (parse $text)
     (let $one (cons-atom $equation ()) (pm:program $one))))
(= (pm:type-text $text)
   (let $declaration (parse $text) (pm:type! $declaration)))
(= (pm:unprogram-text $text)
   (let $equation (parse $text) (pm:unprogram $equation)))
(= (pm:begin-text $text) (let $expression (parse $text) (pm:begin $expression)))
(= (pm:advance-quietly $steps) (let $_outcome (pm:advance $steps) (empty)))
(= (pm:begin-quietly $text) (let $_begun (pm:begin-text $text) (empty)))
(= (pm:end-answers)
   (let $outcome (pm:advance 0)
     (let $answers (pm:end)
       (if (== $outcome pm:halted) (superpose $answers)
           (pm:list3 pm:no-end $outcome $answers)))))"""


def is_test(form):
    return isinstance(form, list) and len(form) == 3 and form[0] == "test"


def is_equation(form):
    return (isinstance(form, list) and len(form) == 3 and form[0] == "="
            and isinstance(form[1], list) and form[1] and isinstance(form[1][0], str))


def is_declaration(form):
    return (isinstance(form, list) and len(form) == 3 and form[0] == ":"
            and isinstance(form[1], str) and not form[1].startswith("$"))


def as_text(form):
    text = swi_lane.write_form(form).replace("\\", "\\\\").replace('"', '\\"')
    return '"' + text + '"'


def own_space_change(form):
    """'program' or 'unprogram' for (add-atom &self EQUATION) / (remove-atom &self EQUATION)."""
    if (isinstance(form, list) and len(form) == 3 and form[1] == "&self"
            and form[0] in ("add-atom", "remove-atom") and is_equation(form[2])):
        return "program" if form[0] == "add-atom" else "unprogram"
    return None


def is_arrow(form):
    return is_declaration(form) and isinstance(form[2], list) and form[2][:1] == ["->"]


# "direct": the machine's rules fired by the host's own operations (pm:lane!).
FIRE = "transform"
# (STEPS, COUNT): a query is driven in COUNT turns of STEPS rules, each a
# statement of its own, so that what one statement's evaluation leaves behind is
# released before the next.  A run that has not ended after them answers
# (pm:no-end pm:turn-over).  None: one statement for each query.
TURNS = None


def asked(text):
    """The statements that ask the machine for the answers of the expression TEXT
    (already quoted), and the expression whose answers they are."""
    if TURNS is None:
        return [], "(pm:of-text %s)" % text
    steps, count = TURNS
    return (["!(pm:begin-quietly %s)" % text] + ["!(pm:advance-quietly %d)" % steps] * count,
            "(pm:end-answers)")


def through_the_machine(source, lane_import):
    """SOURCE with its tests asked of the machine."""
    # The harness's own helpers are not the program's atoms either.
    lines = [lane_import, MACHINE, ASK, "!(pm:hide-loaded!)"]
    if FIRE == "direct":
        lines.append("(= (pm:lane-set $lane) (let $_set (pm:lane! $lane) (empty)))")
        lines.append("!(pm:lane-set direct)")
    read = swi_lane.read_forms(source)
    # The host's reader takes a file's function declarations before its
    # equations; the machine is given them first as well.
    bang = False
    for form in read:
        if form == "!":
            bang = True
            continue
        if not bang and is_arrow(form):
            lines.append("!(pm:type-text %s)" % as_text(form))
        bang = False
    bang = False
    for form in read:
        if form == "!":
            bang = True
            continue
        text = swi_lane.write_form(form)
        if bang and is_test(form):
            before, answers = asked(as_text(form[1]))
            lines.extend(before)
            lines.append("!(test %s %s)" % (answers, swi_lane.write_form(form[2])))
        elif bang:
            lines.append("!" + text)
            # An equation the program adds to its own space or removes from it at
            # the top level is added to the machine's clause rows or removed too.
            change = own_space_change(form)
            if change:
                lines.append("!(pm:%s-text %s)" % (change, as_text(form[2])))
        else:
            lines.append(text)
            if is_equation(form):
                lines.append("!(pm:program-text %s)" % as_text(form))
            elif is_declaration(form):
                lines.append("!(pm:type-text %s)" % as_text(form))
        bang = False
    return "\n".join(lines) + "\n"


def judge(binary, path, lane_import, timeout):
    stem = os.path.basename(path)[:-len(".metta")]
    code, out, _ = whole.run(binary, path, timeout, cwd=os.path.dirname(path))
    written = whole.results(out)
    passed = sum(1 for line in written if "✅" in line)
    if code != 0 or passed == 0:
        return stem, "not-counted", passed, 0, "written run: status %d, %d tests" % (code, passed)
    with open(path) as stream:
        text = through_the_machine(stream.read(), lane_import)
    target = os.path.join(os.path.dirname(path), f".machine-{os.getpid()}-{stem}.metta")
    try:
        with open(target, "w") as stream:
            stream.write(text)
        code, out, err = whole.run(binary, target, timeout, cwd=os.path.dirname(path))
    finally:
        if os.path.exists(target):
            os.remove(target)
    got = whole.results(out)
    same = whole.held(written, got)
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
    return stem, verdict, passed, same, note


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--corpus", nargs="+", required=True)
    parser.add_argument("--lane", choices=["native", "specs"], default="specs")
    parser.add_argument("--fire", choices=["transform", "direct"], default="transform",
                        help="how the machine's rules are fired: by transform, or by the "
                             "host's own match, remove-atom and add-atom")
    parser.add_argument("--turns", nargs=2, type=int, metavar=("STEPS", "COUNT"),
                        help="drive each query in COUNT turns of STEPS rules")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--report")
    parser.add_argument("--own-libraries", action="store_true",
                        help="import this tree's machine and transform libraries by path, "
                             "for a binary built in another tree")
    args = parser.parse_args()
    global FIRE, TURNS
    FIRE = args.fire
    TURNS = tuple(args.turns) if args.turns else None
    binary = os.path.abspath(args.binary)
    if args.own_libraries:
        global MACHINE
        MACHINE = "!(import! &self %s)" % os.path.join(forms.ROOT, "lib", "petta", "petta_machine.metta")
    files = []
    for entry in args.corpus:
        files += sorted(glob.glob(os.path.join(entry, "*.metta"))) if os.path.isdir(entry) else [entry]
    files = [os.path.abspath(f) for f in files if not os.path.basename(f).startswith(".")]
    with tempfile.TemporaryDirectory(prefix="cetta-machine-corpus-") as work:
        lane_import = forms.TRANSFORM
        if args.lane == "specs":
            lane_import = swi_lane.base.SPECS_IMPORT
            if forms.value_evaluated_again(binary, work):
                derived = forms.specs_with_membership(work)
                if not derived:
                    print("FAIL: lib_transform_specs no longer uses is-alpha-member as expected")
                    return 1
                lane_import = f"!(import! &self {derived})"
            elif args.own_libraries:
                lane_import = "!(import! &self %s)" % os.path.join(
                    forms.ROOT, "lib", "petta", "lib_transform_specs.metta")
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            rows = list(pool.map(lambda path: judge(binary, path, lane_import, args.timeout), files))
    counted = [row for row in rows if row[1] != "not-counted"]
    by = {}
    for row in counted:
        by[row[1]] = by.get(row[1], 0) + 1
    tests = sum(row[2] for row in counted)
    held = sum(min(row[3], row[2]) for row in counted)
    lines = [f"{row[1]:13s} {row[0]:44s} tests {row[2]:3d} held {row[3]:3d}  {row[4]}" for row in rows]
    summary = (f"programs {len(files)}, counted {len(counted)}: " +
               ", ".join(f"{k} {v}" for k, v in sorted(by.items())) +
               f"; tests held {held} of {tests} (the machine of transform rules, lane {args.lane}"
               + (", rules fired by the host's own operations" if FIRE == "direct" else "") + ")")
    if args.report:
        with open(args.report, "w") as stream:
            stream.write("\n".join(lines) + "\n" + summary + "\n")
    for line in lines:
        if not line.startswith(("all-pass", "not-counted")):
            print(line)
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
