#!/usr/bin/env python3
"""PeTTa's forms written with transform, against the forms themselves.

Usage: check_petta_by_transform.py CETTA_BINARY [--swi PETTA_ROOT]
           [--programs N] [--seed S]

A law program runs a PeTTa form and the same form from lib_petta_by_transform
and prints (holds NAME) or (FAILS NAME written by-transform).  The fixed laws
are tests/petta/petta_by_transform.metta; seeded law programs compare the forms
over random stores, patterns, expressions and equations.  Each program runs on

    native   CeTTa with (library lib_transform)
    specs    CeTTa with lib_transform_specs, transform written in PeTTa
    swi      SWI-PeTTa with lib_transform_specs (with --swi)

and every law must hold on every lane.  Two engine defects are probed first,
each by its smallest program, and while one is present the lane it breaks runs
as follows:

    stored code evaluated in a branch (native): a matched atom that holds an
        evaluable form is evaluated before the branch reads it, so a call,
        which reads a stored rule, finds nothing.  The laws named call-* and
        stored-code-* are not required on the native lane.
    is-alpha-member evaluates a value again (CeTTa): the specification's commit
        uses it, so the specs lane imports a copy with the membership written
        out with =alpha.

When a probe finds its defect gone the lane runs unchanged, so nothing here has
to be edited when the engine is repaired.  On SWI-PeTTa the caller destination
is written out, as check_transform_swi_differential explains.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_transform_swi_differential as swi_lane  # noqa: E402

ROOT = swi_lane.ROOT
TRANSFORM = swi_lane.LIBRARY_IMPORT
FORMS = "!(import! &self (library lib_petta_by_transform))"
SPECS = os.path.join(ROOT, "lib", "petta", "lib_transform_specs.metta")
FORMS_FILE = os.path.join(ROOT, "lib", "petta", "lib_petta_by_transform.metta")

PRELUDE = (TRANSFORM + "\n" + FORMS + "\n"
           "(: law (-> Atom Atom Atom %Undefined%))\n"
           "(= (law $name $written $by-transform)\n"
           "   (let $a (sort-atom (collapse (eval $written)))\n"
           "     (let $b (sort-atom (collapse (eval $by-transform)))\n"
           "       (if (=alpha $a $b) (holds $name) (FAILS $name $a $b)))))\n"
           "(= (rows $s) (sort-atom (collapse (match $s $row $row))))\n")

STORED_CODE_PROBE = (TRANSFORM + "\n(= (seen1 $x) (saw $x))\n!(bind! &r (new-space))\n"
                     "!(add-atom &r (arith (made a (+ 41 1))))\n"
                     "!(probe (collapse (transform &r (arith $b) (seen1 $b))))\n")
VALUE_PROBE = "!(probe (let $e (quote (k (+ 1 2))) (is-alpha-member $e ((k 3)))))\n"
CHOICE_PROBE = (TRANSFORM + "\n!(bind! &r (new-space))\n"
                "!(probe (collapse (transform &r (let $x (superpose (1 2 3))) $x)))\n")
MEMBERSHIP = ("\n(= (tfs:alpha-member $x $xs)\n"
              "   (if (== $xs ()) False\n"
              "     (let ($y $more) (decons-atom $xs)\n"
              "       (if (=alpha $x $y) True (tfs:alpha-member $x $more)))))\n")


def cetta(binary, source, work, name):
    path = os.path.join(work, name + ".metta")
    with open(path, "w") as stream:
        stream.write(source)
    try:
        p = subprocess.run([binary, "--lang", "petta", path], cwd=ROOT, capture_output=True,
                           text=True, timeout=300,
                           env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1"))
    except subprocess.TimeoutExpired:
        return 124, "", "timeout"
    return p.returncode, p.stdout, p.stderr


def probe_line(output):
    return next((line.strip() for line in output.splitlines()
                 if line.strip().startswith("(probe")), "")


def stored_code_evaluated(binary, work):
    _, out, _ = cetta(binary, STORED_CODE_PROBE, work, "probe-stored-code")
    return probe_line(out) != "(probe ((saw (made a (+ 41 1)))))"


def first_answer_for_every_branch(binary, work):
    """The native firing answers the first answer of a kernel let once for each of
    its answers.  The laws named kernel-let-choice-* are not required on the
    native lane while it does."""
    _, out, _ = cetta(binary, CHOICE_PROBE, work, "probe-choice")
    return probe_line(out) != "(probe (1 2 3))"


def value_evaluated_again(binary, work):
    _, out, _ = cetta(binary, VALUE_PROBE, work, "probe-value")
    return probe_line(out) != "(probe false)"


def specs_with_membership(work):
    """lib_transform_specs with is-alpha-member written out."""
    with open(SPECS) as stream:
        text = stream.read()
    if text.count("is-alpha-member") != 2:
        return None
    path = os.path.join(work, "lib_transform_specs_membership.metta")
    with open(path, "w") as stream:
        stream.write(text.replace("(is-alpha-member ", "(tfs:alpha-member ") + MEMBERSHIP)
    return path


def laws_of(output):
    holds, fails = [], []
    for line in output.splitlines():
        text = line.strip()
        if text.startswith("(holds "):
            holds.append(text[len("(holds "):-1])
        elif text.startswith("(FAILS "):
            fails.append(text)
    return holds, fails


def names_in(source):
    return [line.split()[1] for line in source.splitlines()
            if line.startswith(("!(law ", "!(law-in-order "))]


# ---- seeded law programs ----------------------------------------------------------

CONSTANTS = ["a", "b", "c", "d"]
RELATIONS = {"r": 2, "s": 1, "q": 2}


def fact(rng):
    relation = rng.choice(sorted(RELATIONS))
    return f"({relation} {' '.join(rng.choice(CONSTANTS) for _ in range(RELATIONS[relation]))})"


def pattern(rng):
    relation = rng.choice(sorted(RELATIONS))
    names = ["$x", "$y", "$z"]
    args = [rng.choice(names) if rng.random() < 0.6 else rng.choice(CONSTANTS)
            for _ in range(RELATIONS[relation])]
    return f"({relation} {' '.join(args)})", sorted({a for a in args if a.startswith("$")})


def store_program(rng, number):
    """Reads and writes on two spaces filled alike."""
    lines = [PRELUDE, "!(bind! &a (new-space))\n!(bind! &b (new-space))\n"]
    for _ in range(rng.randint(4, 10)):
        row = fact(rng)
        lines.append(f"!(add-atom &a {row})\n!(add-atom &b {row})\n")
    for index in range(rng.randint(5, 9)):
        kind = rng.choice(["match", "match", "join", "add", "add", "remove", "remove", "atoms"])
        name = f"s{number}-{index}-{kind}"
        if kind == "match":
            p, names = pattern(rng)
            t = f"(out {' '.join(names)})" if names else "(out)"
            lines.append(f"!(law {name} (match &a {p} {t}) (pt:match &b {p} {t}))\n")
        elif kind == "join":
            p, n1 = pattern(rng)
            q, n2 = pattern(rng)
            names = sorted(set(n1) | set(n2))
            t = f"(out {' '.join(names)})" if names else "(out)"
            lines.append(f"!(law {name} (match &a (, {p} {q}) {t}) (pt:match &b (, {p} {q}) {t}))\n")
        elif kind == "add":
            row = fact(rng) if rng.random() < 0.8 else f"(, {fact(rng)} {fact(rng)})"
            lines.append(f"!(law {name} (add-atom &a {row}) (pt:add-atom &b {row}))\n")
        elif kind == "remove":
            row = fact(rng) if rng.random() < 0.5 else pattern(rng)[0]
            lines.append(f"!(law {name} (remove-atom &a {row}) (pt:remove-atom &b {row}))\n")
        else:
            lines.append(f"!(law {name} (get-atoms &a) (pt:get-atoms &b))\n")
        lines.append(f"!(law {name}-store (rows &a) (rows &b))\n")
    return "".join(lines)


def number(rng):
    return str(rng.randint(0, 5))


def numbers(rng):
    return "(superpose (" + " ".join(number(rng) for _ in range(rng.randint(1, 3))) + "))"


def arithmetic(rng, variables):
    left = rng.choice(variables + [number(rng)])
    right = rng.choice(variables + [number(rng)])
    return f"({rng.choice(['+', '*', '-'])} {left} {right})"


def expression_program(rng, index):
    """Choice, binding and branching over small expressions."""
    lines = [PRELUDE]
    for at in range(rng.randint(6, 10)):
        kind = rng.choice(["superpose", "once", "collapse", "let", "let*", "if", "case"])
        name = f"e{index}-{at}-{kind}"
        if kind == "superpose":
            items = " ".join(rng.choice([number(rng), arithmetic(rng, []), rng.choice(CONSTANTS)])
                             for _ in range(rng.randint(0, 4)))
            lines.append(f"!(law {name} (superpose ({items})) (pt:superpose ({items})))\n")
        elif kind == "once":
            e = numbers(rng)
            lines.append(f"!(law {name} (once {e}) (pt:once {e}))\n")
        elif kind == "collapse":
            e = rng.choice([numbers(rng), "(empty)", arithmetic(rng, [])])
            lines.append(f"!(law {name} (collapse {e}) (pt:collapse {e}))\n")
        elif kind == "let":
            e = rng.choice([numbers(rng), number(rng), arithmetic(rng, [])])
            body = rng.choice(["(got $u)", arithmetic(rng, ["$u"])])
            lines.append(f"!(law {name} (let $u {e} {body}) (pt:let $u {e} {body}))\n")
        elif kind == "let*":
            e1 = rng.choice([numbers(rng), number(rng)])
            e2 = rng.choice([numbers(rng), arithmetic(rng, ["$u"])])
            body = rng.choice(["(got $u $v)", arithmetic(rng, ["$u", "$v"])])
            pairs = f"(($u {e1}) ($v {e2}))"
            lines.append(f"!(law {name} (let* {pairs} {body}) (pt:let* {pairs} {body}))\n")
        elif kind == "if":
            test = f"({rng.choice(['>', '<', '=='])} {numbers(rng)} {number(rng)})"
            yes, no = arithmetic(rng, []), rng.choice(["(no)", numbers(rng)])
            lines.append(f"!(law {name} (if {test} {yes} {no}) (pt:if {test} {yes} {no}))\n")
        else:
            value = rng.choice([numbers(rng), f"(pair {number(rng)} {number(rng)})"])
            rows = [f"({number(rng)} (lit {at}))", "((pair $l $r) (+ $l $r))"]
            rng.shuffle(rows)
            if rng.random() < 0.7:
                rows.append("($other (other $other))")
            table = "(" + " ".join(rows) + ")"
            lines.append(f"!(law {name} (case {value} {table}) (pt:case {value} {table}))\n")
    return "".join(lines)


def equation_program(rng, index):
    """Equations and calls: each equation is defined both ways."""
    lines = [PRELUDE]
    equations = [
        ("(count 0)", "(done)", "(done)"),
        ("(count $n)", "(if (> $n 0) (count (- $n 1)) (empty))",
         "(pt:if (> $n 0) (pt:call (count (- $n 1))) (pt:empty))"),
        ("(pick)", f"(superpose ({number(rng)} {number(rng)}))", None),
        ("(pick)", number(rng), None),
        ("(twice $x)", "(* 2 $x)", None),
        ("(swap (pair $l $r))", "(pair $r $l)", None),
        ("(sum-to $n)", "(if (> $n 0) (+ $n (sum-to (- $n 1))) 0)",
         "(pt:if (> $n 0) (+ $n (pt:call (sum-to (- $n 1)))) 0)"),
        ("(both)", "(pair (pick) (pick))", "(pair (pt:call (pick)) (pt:call (pick)))"),
    ]
    for head, body, by_transform in equations:
        lines.append(f"(= {head} {body})\n")
        stored = by_transform or body.replace("(superpose ", "(pt:superpose ")
        lines.append(f"!(pt:define (= {head} {stored}))\n")
    calls = [f"(count {number(rng)})", "(pick)", f"(twice {number(rng)})",
             f"(twice {numbers(rng)})", f"(swap (pair {number(rng)} {number(rng)}))",
             f"(swap {number(rng)})", f"(sum-to {number(rng)})", "(both)",
             f"(twice (twice {number(rng)}))"]
    rng.shuffle(calls)
    for at, call in enumerate(calls[:rng.randint(5, 9)]):
        by_transform = f"(pt:call {call})"
        if call.startswith("(twice (twice"):
            by_transform = "(pt:call (twice (pt:call " + call[len("(twice "):-1] + ")))"
        lines.append(f"!(law call-q{index}-{at} {call} {by_transform})\n")
    return "".join(lines)


GENERATORS = [("stores", store_program), ("expressions", expression_program),
              ("equations", equation_program)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--swi")
    parser.add_argument("--programs", type=int, default=12)
    parser.add_argument("--seed", type=int, default=20261111)
    args = parser.parse_args()
    binary = os.path.abspath(args.binary)
    swi = os.path.abspath(args.swi) if args.swi else None
    with tempfile.TemporaryDirectory(prefix="cetta-petta-by-transform-") as work:
        value_again = value_evaluated_again(binary, work)
        specs_import = swi_lane.base.SPECS_IMPORT
        if value_again:
            derived = specs_with_membership(work)
            if not derived:
                print("FAIL: lib_transform_specs no longer uses is-alpha-member as expected")
                return 1
            specs_import = f"!(import! &self {derived})"
        # a law that does not hold must be reported: a form given other elements
        _, out, _ = cetta(binary, PRELUDE + "!(law control (superpose (1 2)) (pt:superpose (1 3)))\n"
                          "!(law control-store (rows &self) (rows &pt-work))\n",
                          work, "control")
        holds, fails = laws_of(out)
        if holds or len(fails) != 2:
            print("FAIL: laws that do not hold were not reported")
            return 1
        programs = []
        with open(os.path.join(ROOT, "tests", "petta", "petta_by_transform.metta")) as stream:
            programs.append(("fixed", stream.read()))
        for name, generator in GENERATORS:
            rng = random.Random(f"{args.seed}:{name}")
            for number_ in range(args.programs):
                programs.append((f"{name}-{number_}", generator(rng, number_)))
        totals = {"native": 0, "specs": 0, "swi": 0}
        failed = False
        for stem, source in programs:
            names = names_in(source)
            lanes = [("native", lambda: cetta(binary, source, work, stem + "-native")),
                     ("specs", lambda: cetta(binary, source.replace(TRANSFORM, specs_import),
                                             work, stem + "-specs"))]
            if swi:
                swi_source = swi_lane.for_swi(
                    source.replace(FORMS, f"!(import! &self {FORMS_FILE})\n!(pt:variant! swi)"),
                    "lib_transform_specs", work)
                lanes.append(("swi", lambda: swi_lane.run_swi(swi, swi_source, work,
                                                              stem + "-swi")))
            for lane, run in lanes:
                status, out, err = run()
                holds, fails = laws_of(out)
                if status != 0:
                    # a lane that stops early has not answered the laws after the stop
                    failed = True
                    stop = [l for l in (out + "\n" + err).splitlines()
                            if "rror" in l and "janus" not in l]
                    print(f"FAIL: {stem} on {lane}: the run ended with status {status}"
                          + (f": {stop[-1][:200]}" if stop else ""))
                required = names
                missing = [n for n in required if n not in holds]
                totals[lane] += len([n for n in required if n in holds])
                if missing:
                    failed = True
                    print(f"FAIL: {stem} on {lane}: {len(missing)} of {len(required)} laws "
                          f"do not hold: {' '.join(missing[:8])}")
                    for line in fails[:4]:
                        print("    " + line[:260])
                    tail = [l for l in err.splitlines() if "rror" in l and "janus" not in l]
                    for line in tail[:2]:
                        print("    " + line[:260])
        if failed:
            return 1
    notes = []
    if value_again:
        notes.append("the specs lane writes is-alpha-member out: it evaluates a value again")
    lanes = "native, specs" + (", SWI-PeTTa" if swi else "")
    print(f"PASS: PeTTa's forms written with transform answer as the forms ({lanes}): "
          f"{len(programs)} programs, laws held: " +
          ", ".join(f"{lane} {count}" for lane, count in totals.items() if count))
    for note in notes:
        print("  engine defect present: " + note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
