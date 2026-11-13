#!/usr/bin/env python3
"""A transform's let code answers the same however it is evaluated.

The transform evaluates a let's code as written, under the firing's
bindings, and applies a single arithmetic operation or comparison of numbers
directly.  CETTA_TRANSFORM_LET_REFERENCE=1 instead writes the values into
the code and hands the new term to the evaluator, as the library does.  Both
must give the same answers: for every arithmetic operation and comparison on
integers (the extremes among them) and floats; for code that is no single
operation; for values that are symbols, strings, expressions, and
expressions a program's equations would reduce if they were evaluated, which
a bound value never is; and for an operation the program gives equations of
its own.

A let that is a test of integers after reads alone, such as
(let True (> $x $y)) or (let 0 (% $x $y)), is made by the join as it joins,
and no code runs for it.  Those are compared with the same reference: every
comparison, holding and failing, with operands that are variables, integers
and single operations; over small integers, where the join answers; over
integers at the ends of their range, where a sum or a product leaves it and
the join must decline; over negative numbers and zero divisors, whose
division is the dialect's; and over rows holding a float or a symbol.

The two evaluations are siblings and could be wrong together, so each of
these tests is also put to `match`, which knows nothing of transforms: the
same reads with the lets nested in its template must give the same answers
as a bag, and where a let's code raises an error (a comparison of a symbol,
a division the dialect does not evaluate inside a comparison) the transform
must raise that same error.

After a take, where the order of the candidates decides which firings
succeed and no join may reorder them, the same tests are put to each row of
the take as it is stored, before it is matched: a row a test certainly
refuses is passed over.  Rules that take under tests are compared with the
reference by their answers and by the store they leave: a sieve, takes
under each comparison, a test only some of whose integers the row gives,
and stores holding floats, symbols and atoms with variables.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

INTS = [0, 1, -1, 2, -2, 3, 7, -7, 10, 100, 9223372036854775807,
        -9223372036854775807, 4611686018427387904]
FLOATS = ["0.0", "1.0", "-1.5", "2.5", "7.0", "1e10"]
NUMBERS = [str(i) for i in INTS] + FLOATS
OPERATIONS = ["+", "-", "*", "/", "//", "%", "<", ">", "<=", ">=", "=="]


def run(lines, reference):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write("\n".join(lines) + "\n")
        path = f.name
    try:
        env = dict(os.environ)
        env.pop("CETTA_TRANSFORM_LET_REFERENCE", None)
        if reference:
            env["CETTA_TRANSFORM_LET_REFERENCE"] = "1"
        p = subprocess.run([BINARY, "--lang", "petta", "--profile", "extended", path],
                           cwd=ROOT, env=env, capture_output=True, text=True, timeout=600)
    finally:
        os.unlink(path)
    return p.returncode, [l for l in p.stdout.splitlines() if l.startswith("(g ")], \
        p.stderr.strip().splitlines()[-1:]


def same(name, lines, answers_wanted=1):
    new = run(lines, False)
    old = run(lines, True)
    if new != old:
        print("FAIL: %s: the two evaluations of a let's code differ" % name)
        for a, b in zip(new[1] + [""], old[1] + [""]):
            if a != b:
                print("  as written  :", a[:300])
                print("  values in   :", b[:300])
                break
        print("  exits:", new[0], old[0], new[2], old[2])
        return False
    if len(new[1]) < answers_wanted:
        print("FAIL: %s: answered %d lines, wanted %d: %s"
              % (name, len(new[1]), answers_wanted, new[2]))
        return False
    return True


def top_level(text):
    """The top-level parenthesised parts of TEXT."""
    parts, depth, start = [], 0, None
    for i, ch in enumerate(text):
        if ch == "(":
            if depth == 0:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                parts.append(text[start:i + 1])
    return parts


def as_match(tests, template):
    """The lets of TESTS nested around TEMPLATE, as a match's template."""
    body = template
    for let in reversed(top_level(tests)):
        inner = top_level(let[1:-1])
        pattern = let[len("(let "):].split(" ", 1)[0] if not inner or \
            not let[len("(let "):].startswith("(") else inner[0]
        code = let[len("(let ") + len(pattern):].strip()[:-1].strip()
        body = "(let %s %s %s)" % (pattern, code, body)
    return body


def bag(lines):
    out = []
    for line in lines:
        inner = line[len("(g ("):-2] if line.startswith("(g (") else line
        out.append(sorted(top_level(inner)) if "(" in inner else sorted(inner.split()))
    return out


def as_match_does(name, head, rows, tests):
    """The transform's answers, or the error it raises, are match's."""
    lines = list(head) + ["!(add-atom &s %s)" % row for row in rows]
    by_transform = run(lines + ["!(g (collapse (transform &s (, (p $k $x $y) %s) (v $k))))"
                                % tests], False)
    by_match = run(lines + ["!(g (collapse (match &s (p $k $x $y) %s)))"
                            % as_match(tests, "(v $k)")], False)
    if bag(by_transform[1]) != bag(by_match[1]) or by_transform[2] != by_match[2]:
        print("FAIL: %s: the transform and match differ" % name)
        print("  transform:", by_transform[1][:1], by_transform[2])
        print("  match    :", by_match[1][:1], by_match[2])
        return False
    return True


def main():
    global BINARY
    BINARY = os.path.abspath(sys.argv[1])
    head = ["!(import! &self (library lib_transform))", "!(bind! &s (new-space))"]
    checked = 0
    for op in OPERATIONS:
        # A zero divisor raises, which ends the program on either route; a
        # float operand of % is an error of the operation itself.
        operands = NUMBERS if op != "%" else [str(i) for i in INTS]
        pairs = [(a, b) for a in operands for b in operands
                 if not (op in ("/", "//", "%") and float(b) == 0.0)]
        lines = list(head)
        for k, (a, b) in enumerate(pairs):
            lines.append("!(add-atom &s (p %d %s %s))" % (k, a, b))
        lines.append("!(g (collapse (transform &s (, (p $k $x $y) (let $r (%s $x $y))) "
                     "(v $k $r))))" % op)
        if not same("(%s $x $y) on %d pairs of numbers" % (op, len(pairs)), lines):
            return 1
        checked += len(pairs)
    # Guards that pass or fail, as a rule writes them.
    lines = list(head) + ["!(add-atom &s (p %d %d %d))" % (k, a, b)
                          for k, (a, b) in enumerate((a, b) for a in range(1, 13)
                                                     for b in range(1, 13))]
    lines.append("!(g (collapse (transform &s (, (p $k $x $y) (let True (> $x $y)) "
                 "(let 0 (% $x $y))) (v $x $y))))")
    lines.append("!(g (collapse (transform &s (, (p $k $x $y) (let $z (+ (* $x 2) $y)) "
                 "(let True (< $z 9))) (v $x $y $z))))")
    if not same("guards that pass or fail, and code that is no single operation", lines, 2):
        return 1
    checked += 2
    # Tests of integers, which a join of the reads makes as it joins.
    small = [(a, b) for a in range(0, 9) for b in range(1, 9)]
    wide = [(a, b) for a in INTS for b in INTS]
    tables = (
        ("small integers", ["(p %d %d %d)" % (k, a, b) for k, (a, b) in enumerate(small)], 1),
        ("integers to the ends of their range",
         ["(p %d %d %d)" % (k, a, b) for k, (a, b) in enumerate(wide)], 0),
        ("a row holding a float",
         ["(p %d %d %d)" % (k, a, b) for k, (a, b) in enumerate(small[:20])]
         + ["(p 900 2.5 3)"], 0),
        ("a row holding a symbol",
         ["(p %d %d %d)" % (k, a, b) for k, (a, b) in enumerate(small[:20])]
         + ["(p 901 z 3)"], 0),
    )
    operands = (("$x", "$y"), ("$x", "3"), ("4", "$y"), ("(+ $x 1)", "$y"),
                ("$x", "(* $y 2)"), ("(% $x 5)", "(// $y 2)"), ("(- $x $y)", "0"))
    tests = []
    for cmp in ("<", ">", "<=", ">=", "=="):
        for truth in ("True", "False"):
            for left, right in operands:
                tests.append("(let %s (%s %s %s))" % (truth, cmp, left, right))
    for op in ("+", "-", "*", "//", "%"):
        for wanted in (0, 1, 5):
            tests.append("(let %d (%s $x $y))" % (wanted, op))
    tests.append("(let True (> $x $y)) (let 0 (% $x $y))")
    tests.append("(let True (< $x $y)) (let False (== $x 2)) (let 1 (% $y 2))")
    for name, rows, wanted in tables:
        for test in tests:
            lines = list(head) + ["!(add-atom &s %s)" % row for row in rows]
            lines.append("!(g (collapse (transform &s (, (p $k $x $y) %s) (v $k))))" % test)
            # A division by zero, and a comparison of a symbol, raise on
            # either route: then neither answers.
            if not same("%s over %s" % (test, name), lines, 0):
                return 1
            if not as_match_does("%s over %s" % (test, name), head, rows, test):
                return 1
            checked += 2
    # Two reads joined under a test, and a test over a variable bound before.
    lines = list(head) + ["!(add-atom &s (n %d))" % i for i in range(2, 60)]
    lines.append("!(g (size-atom (collapse (transform &s (, (n $y) (n $x) (let True (> $x $y)) "
                 "(let 0 (% $x $y))) (d $y $x)))))")
    lines.append("!(g (collapse (transform &s (, (n $y) (let True (< $y 6)) ) (small $y))))")
    if not same("a join of two reads under tests", lines, 2):
        return 1
    checked += 2
    # Takes under tests: the answers and the store left.
    left = "!(g (collapse (get-atoms &s)))"
    lines = list(head) + ["!(add-atom &s (prime %d))" % i for i in range(2, 80)]
    lines.append("!(g (collapse (transform &s (, (prime $y) (take (prime $x)) "
                 "(let True (> $x $y)) (let 0 (% $x $y))) (removed $x))))")
    lines.append(left)
    lines.append("!(g (collapse (transform &s (, (prime $y) (take (prime $x)) "
                 "(let True (> $x $y)) (let 0 (% $x $y))) (removed $x))))")
    if not same("a sieve by takes under two tests", lines, 3):
        return 1
    checked += 3
    stores = (
        ("integers", ["(q %d %d)" % (k, v) for k, v in enumerate(
            [5, 3, 9, 0, -4, 12, 7, 7, 1, 30, -1, 8])]),
        ("integers to the ends of their range",
         ["(q %d %d)" % (k, v) for k, v in enumerate(INTS)]),
        ("a float, a symbol and an atom with a variable among them",
         ["(q 0 5)", "(q 1 2.5)", "(q 2 9)", "(q 3 zed)", "(q $open 4)", "(q 5 11)",
          "(q 6 (inc 1))", "(q 7 3)"]),
    )
    take_tests = []
    for cmp in ("<", ">", "<=", ">=", "=="):
        take_tests.append("(let True (%s $v 6))" % cmp)
        take_tests.append("(let False (%s (+ $v 1) 9))" % cmp)
    take_tests += ["(let 1 (% $v 2))", "(let 0 (- $v 7))",
                   "(let True (> $v 2)) (let $w (* $v 2)) (let True (< $w 20))",
                   "(let True (> $v $k))"]
    for name, rows in stores:
        for test in take_tests:
            for first_only in (False, True):
                lines = list(head) + ["!(add-atom &s %s)" % row for row in rows]
                call = "(transform &s (, (take (q $k $v)) %s) (took $v))" % test
                lines.append("!(g (collapse %s))"
                             % ("(once %s)" % call if first_only else call))
                lines.append(left)
                # A symbol or an unbound variable under a comparison raises
                # on either route: then neither answers.
                if not same("take under %s over %s%s"
                            % (test, name, ", the first alone" if first_only else ""),
                            lines, 0):
                    return 1
                checked += 1
    # Values that are not numbers; values a program's equations would reduce.
    lines = list(head) + [
        "(= (inc $n) (+ $n 1))",
        "(= (big $x) (> $x 5))",
        "(= (wrap $x) (wrapped $x))",
        "!(add-atom &s (q 1 a b))",
        "!(add-atom &s (q 2 a a))",
        '!(add-atom &s (q 3 "s" "s"))',
        "!(add-atom &s (q 4 (inc 1) (inc 1)))",
        "!(add-atom &s (q 5 (inc 1) 2))",
        "!(add-atom &s (q 6 7 3))",
        "!(add-atom &s (q 7 (f $u) (f c)))",
        "!(g (collapse (transform &s (, (q $k $x $y) (let $r (== $x $y))) (v $k $r))))",
        "!(g (collapse (transform &s (, (q $k $x $y) (let $r (wrap $x))) (v $k $r))))",
        "!(g (collapse (transform &s (, (q 4 $x $y) (let $r (cons-atom $x ()))) (v $r))))",
        "!(g (collapse (transform &s (, (q 6 $x $y) (let True (big $x))) (v $x))))",
        "!(g (collapse (transform &s (, (q 6 $x $y) (let $r (inc $x))) (v $r))))",
        "!(g (collapse (transform &s (, (q 5 $x $y) (let $r (+ $y 1))) (v $x $r))))",
    ]
    if not same("values that are symbols, strings and expressions", lines, 6):
        return 1
    checked += 6
    # An operation the program gives equations of its own.
    lines = list(head) + [
        "(= (> 100 $y) mine)",
        "!(add-atom &s (q 1 7 3))",
        "!(add-atom &s (q 2 100 3))",
        "!(g (collapse (transform &s (, (q $k $x $y) (let $r (> $x $y))) (v $k $r))))",
    ]
    if not same("an operation with equations of the program's own", lines, 1):
        return 1
    checked += 1
    print("PASS: a transform's let code answers alike as written under its bindings and "
          "with the values written in: %d operand pairs and guard programs" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
