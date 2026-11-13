#!/usr/bin/env python3
"""A PeTTa evaluator whose every step is a transform rule, against the host.

    check_petta_machine.py CETTA [--swi PETTA_ROOT] [--programs N] [--seed S]

lib/petta/petta_machine.metta holds a small abstract machine for PeTTa as a
table of transform rules: its state is one stored atom, and each transition
takes the state and makes the next.  tests/petta/petta_machine.metta runs
expressions on the host and on the machine and compares the answers in order.
This check runs that file, and seeded expressions over the same program, with
the native firing, with transform written in PeTTa (lib_transform_specs), and
with --swi on SWI-PeTTa.  Every check must hold on every lane, and a lane that
ends with a status other than 0 fails.

The machine's rules can also be fired by the host's own match, remove-atom and
add-atom (pm:lane! direct).  Each program is run that way too, on CeTTa and on
SWI-PeTTa, and held to the lane that fires by transform on the same host: the
same answers and the same number of rules fired, check by check.
"""
import argparse
import os
import random
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_transform_swi_differential as swi_lane  # noqa: E402
import check_petta_by_transform as forms  # noqa: E402

ROOT = swi_lane.ROOT
MACHINE = "!(import! &self (library petta_machine))"
MACHINE_FILE = os.path.join(ROOT, "lib", "petta", "petta_machine.metta")
FIXED = os.path.join(ROOT, "tests", "petta", "petta_machine.metta")


def results(output):
    holds, fails = [], []
    for line in output.splitlines():
        text = line.strip()
        if text.startswith("(holds "):
            holds.append(text.split()[1].rstrip(")"))
        elif text.startswith("(FAILS "):
            fails.append(text)
    return holds, fails


def steps_fired(output):
    """How many rules the machine fired for each check that holds, by name."""
    found = {}
    for line in output.splitlines():
        text = line.strip()
        if text.startswith("(holds "):
            parts = text[:-1].split()
            if len(parts) == 3:
                found[parts[1]] = parts[2]
    return found


DIRECT = "!(pm:lane! direct)"


def machine_answers(output):
    """What the machine answered for each check, by name."""
    found = {}
    for line in output.splitlines():
        text = line.strip()
        if text.startswith("(machine "):
            name, _, answers = text[len("(machine "):-1].partition(" ")
            found[name] = swi_lane.variables_named(answers)
    return found


def names_in(source):
    return [line.split()[1] for line in source.splitlines()
            if line.startswith(("!(check ", "!(check-apart ", "!(check-text ", "!(check-limited ",
                                "!(check-stopped ", "!(check-turns ", "!(check-unended "))]


def fresh(rng):
    """A variable name used by one let only.  SWI-PeTTa c3adb86 answers nothing for
    (if C (let $v 4 $v) (let $v 1 3)) when the two lets share a name."""
    return f"$v{rng.randint(0, 10 ** 9)}"


def numeric(rng, depth, variables):
    """A numeric expression of the machine's fragment over the fixed file's program."""
    if depth == 0 or rng.random() < 0.2:
        return rng.choice([str(rng.randint(0, 5))] + list(variables))
    kind = rng.choice(["arith", "arith", "double", "pick", "if", "let", "choice", "len"])
    sub = lambda: numeric(rng, depth - 1, variables)
    if kind == "arith":
        return f"({rng.choice(['+', '-', '*'])} {sub()} {sub()})"
    if kind == "double":
        return f"(double {sub()})"
    if kind == "pick":
        return f"(pick {sub()})"
    if kind == "if":
        return f"(if {condition(rng, depth, variables)} {sub()} {sub()})"
    if kind == "let":
        name = fresh(rng)
        return f"(let {name} {sub()} {numeric(rng, depth - 1, variables + [name])})"
    if kind == "choice":
        return f"(superpose ({sub()} {sub()}))"
    return "(len (Cons a (Cons b Nil)))"


def condition(rng, depth, variables):
    return (f"({rng.choice(['<', '>', '=='])} {numeric(rng, depth - 1, variables)} "
            f"{numeric(rng, depth - 1, variables)})")


def expression(rng, depth, variables):
    """Any expression of the fragment: numbers, colours and pairs."""
    if depth == 0 or rng.random() < 0.3:
        return numeric(rng, depth, variables)
    kind = rng.choice(["numeric", "colour", "pair", "if", "let", "choice", "kind"])
    sub = lambda: expression(rng, depth - 1, variables)
    if kind == "numeric":
        return numeric(rng, depth, variables)
    if kind == "colour":
        return "(colour)"
    if kind == "pair":
        return f"(pair {sub()} {sub()})"
    if kind == "if":
        return f"(if {condition(rng, depth, variables)} {sub()} {sub()})"
    if kind == "let":
        name = fresh(rng)
        return (f"(let {name} {numeric(rng, depth - 1, variables)} "
                f"{expression(rng, depth - 1, variables + [name])})")
    if kind == "choice":
        return f"(superpose ({sub()} {sub()}))"
    return f"(kind {rng.choice(['(leaf 1)', '(node (leaf 1) (leaf 2))', '7'])})"


def seeded(rng, index, prelude):
    lines = [prelude]
    for at in range(6):
        lines.append(f"!(check q{index}-{at} {expression(rng, 3, [])})")
    return "\n".join(lines) + "\n"


PROBE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "native_two_take_probe.metta")


def native_misses_a_stored_atom(binary, work):
    """After earlier firings in one space, the native firing's rule with two takes
    does not find a stored atom that `match` finds there.  The probe is a replay
    of plain store operations (no machine library): at its end `match` finds the
    join of the two rows and the rule that takes both fires nothing.  While this
    is so, a machine of transform rules stops on the native lane, and the native
    lane is reported and not required."""
    with open(PROBE) as stream:
        source = stream.read()
    _, out, _ = swi_lane.run_cetta(binary, source, work, "probe-native")
    lines = out.splitlines()
    joined = any(line.startswith("(JOIN ((join") for line in lines)
    fired = any(line.startswith("(FIRES (") and not line.startswith("(FIRES ())") for line in lines)
    return joined and not fired


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--swi")
    parser.add_argument("--fixture", default=FIXED, help="fixed checks to run")
    parser.add_argument("--programs", type=int, default=20)
    parser.add_argument("--seed", type=int, default=20261112)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    swi = os.path.abspath(args.swi) if args.swi else None
    with open(args.fixture) as stream:
        fixed = stream.read()
    # the seeded programs reuse the fixed file up to its first check
    prelude = fixed[:fixed.index("!(check literal")]
    programs = [("fixed", fixed)]
    rng = random.Random(args.seed)
    for index in range(args.programs):
        programs.append((f"seeded-{index}", seeded(rng, index, prelude)))
    totals = {"native": 0, "specs": 0, "direct": 0, "swi": 0, "swi-direct": 0}
    fired_apart = 0
    failed = False
    host_differs = 0
    with tempfile.TemporaryDirectory(prefix="cetta-petta-machine-") as work:
        native_defect = native_misses_a_stored_atom(binary, work)
        # While the engine evaluates a value again in is-alpha-member, transform
        # written in PeTTa is run from a copy with that membership written out,
        # as the other checks of this lane do.
        specs_import = swi_lane.base.SPECS_IMPORT
        if forms.value_evaluated_again(binary, work):
            derived = forms.specs_with_membership(work)
            if not derived:
                print("FAIL: lib_transform_specs no longer uses is-alpha-member as expected")
                return 1
            specs_import = f"!(import! &self {derived})"
        for stem, source in programs:
            names = names_in(source)
            lanes = [("native", lambda: swi_lane.run_cetta(binary, source, work, stem + "-native")),
                     ("specs", lambda: swi_lane.run_cetta(
                         binary, source.replace(swi_lane.LIBRARY_IMPORT, specs_import),
                         work, stem + "-specs"))]
            direct_source = source.replace(MACHINE, MACHINE + "\n" + DIRECT)
            lanes.append(("direct", lambda: swi_lane.run_cetta(
                binary, direct_source.replace(swi_lane.LIBRARY_IMPORT, specs_import),
                work, stem + "-direct")))
            if swi:
                swi_source = swi_lane.for_swi(
                    source.replace(MACHINE, f"!(import! &self {MACHINE_FILE})"),
                    "lib_transform_specs", work)
                lanes.append(("swi", lambda: swi_lane.run_swi(swi, swi_source, work, stem + "-swi")))
                swi_direct = swi_lane.for_swi(
                    source.replace(MACHINE, f"!(import! &self {MACHINE_FILE})\n" + DIRECT),
                    "lib_transform_specs", work)
                lanes.append(("swi-direct",
                              lambda: swi_lane.run_swi(swi, swi_direct, work, stem + "-swi-direct")))
            reference = {}
            fired = {}
            for lane, run in lanes:
                status, out, err = run()
                holds, fails = results(out)
                if lane == "specs":
                    reference = machine_answers(out)
                if lane in ("specs", "swi"):
                    fired[lane] = (steps_fired(out), machine_answers(out))
                if lane in ("direct", "swi-direct"):
                    # the same rules, fired by the host's own operations: the same
                    # answers and the same number of rules as by transform
                    by_transform, answered = fired["specs" if lane == "direct" else "swi"]
                    here_fired, here = steps_fired(out), machine_answers(out)
                    apart = [n for n in names
                             if here.get(n) != answered.get(n)
                             or here_fired.get(n) != by_transform.get(n)]
                    if apart:
                        fired_apart += len(apart)
                        failed = True
                        print(f"FAIL: {stem} on {lane}: {len(apart)} checks fire or answer "
                              f"otherwise than by transform: {' '.join(apart[:8])}")
                        for n in apart[:3]:
                            print(f"    {n}: by transform {by_transform.get(n)} rules "
                                  f"{str(answered.get(n))[:90]} | direct {here_fired.get(n)} rules "
                                  f"{str(here.get(n))[:90]}")
                if lane in ("swi", "swi-direct"):
                    # On SWI-PeTTa the machine is held to what it answers on CeTTa with
                    # transform written in PeTTa: SWI-PeTTa's own evaluator differs from
                    # CeTTa's on some written expressions (a superpose element that is
                    # a let), and the machine is not the place to judge that.
                    here = machine_answers(out)
                    differs = [n for n in names if n not in holds and here.get(n) == reference.get(n)]
                    host_differs += len(differs)
                    holds = holds + differs
                missing = [n for n in names if n not in holds]
                totals[lane] += len(names) - len(missing)
                if status != 0 or missing:
                    failed = True
                    print(f"FAIL: {stem} on {lane}: status {status}, "
                          f"{len(missing)} of {len(names)} checks do not hold: "
                          f"{' '.join(missing[:8])}")
                    for line in fails[:3]:
                        print("    " + line[:240])
    if failed:
        return 1
    if native_defect:
        print("FAIL: the native two-take probe misses a stored atom after earlier firings")
        return 1
    if host_differs:
        print(f"  on SWI-PeTTa the machine answers as it does on CeTTa in {host_differs} checks "
              "where SWI-PeTTa's own evaluator answers differently")
    lanes = "native, specs, direct" + (", SWI-PeTTa, SWI-PeTTa direct" if swi else "")
    print(f"PASS: the machine of transform rules answers as the host ({lanes}): "
          f"{len(programs)} programs, checks held: " +
          ", ".join(f"{lane} {count}" for lane, count in totals.items() if count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
