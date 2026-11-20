#!/usr/bin/env python3
"""The native image of a prepared pure program answers what its register
code answers (src/prepared_pure_native.c).

Random programs of two kinds.  Integer programs: functions of one to three
parameters whose bodies are arithmetic (with remainders by divisors that
include zero and the word's edges), comparisons, conditionals and calls of
earlier functions; a counting loop by tail calls; a tree recursion; heads
of literal equations beside one guarded variable equation; and a head whose
pattern has a literal beside a variable.  Queries call them on small
arguments and on arguments near the edges of a 64-bit word, where an exact
result leaves the word and the native code declines to the register code;
every fourth runs in the PeTTa lane, whose `%` is the floor modulo where
HE's is the truncating remainder.  Term programs: lists built, appended,
reversed, mapped, filtered and summed; search trees; pairs taken apart by
patterns and by let; symbols compared; literal lists as arguments; integers
in terms near the edge of what a term word holds; in the HE lane.  Last,
fixed programs over long lists, whose heaps are collected many times, which
the native code must answer alone, never declining.  Each program runs twice, natively and with
CETTA_PREPARED_PURE_NATIVE=0, and the two outputs must be equal; the run
also counts how many programs each lane lowered, so a lowering that quietly
refuses everything fails.

usage: check_prepared_pure_native.py CETTA [--programs N] [--seed S]"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

EDGES = ["0", "1", "-1", "2", "7", "-13", "100",
         "3037000499", "3037000500", "-3037000500",
         "4611686018427387903", "4611686018427387904",
         "9223372036854775806", "-9223372036854775807"]
DIVISORS = ["0", "1", "-1", "2", "-3", "7", "-10", "9223372036854775807",
            "-9223372036854775807"]


def expression(rng, params, functions, depth):
    """An integer-valued expression over the parameters."""
    if depth <= 0 or rng.random() < 0.25:
        return rng.choice(params + [str(rng.randint(-9, 9))])
    choice = rng.random()
    if choice < 0.35:
        op = rng.choice(["+", "-", "*"])
        return "(%s %s %s)" % (op, expression(rng, params, functions, depth - 1),
                               expression(rng, params, functions, depth - 1))
    if choice < 0.45:
        divisor = (rng.choice(DIVISORS) if rng.random() < 0.5
                   else expression(rng, params, functions, depth - 1))
        return "(%% %s %s)" % (expression(rng, params, functions, depth - 1),
                               divisor)
    if choice < 0.7:
        return "(if %s %s %s)" % (condition(rng, params, functions, depth - 1),
                                  expression(rng, params, functions, depth - 1),
                                  expression(rng, params, functions, depth - 1))
    if functions:
        name, arity = rng.choice(functions)
        args = " ".join(expression(rng, params, functions, depth - 1)
                        for _ in range(arity))
        return "(%s %s)" % (name, args)
    return rng.choice(params)


def condition(rng, params, functions, depth):
    op = rng.choice(["<", ">", "<=", ">=", "=="])
    return "(%s %s %s)" % (op, expression(rng, params, functions, depth),
                           expression(rng, params, functions, depth))


def program(rng):
    lines = []
    functions = []
    for k in range(rng.randint(1, 4)):
        arity = rng.randint(1, 3)
        params = ["$a", "$b", "$c"][:arity]
        name = "f%d" % k
        body = expression(rng, params, functions, 3)
        lines.append("(= (%s %s) %s)" % (name, " ".join(params), body))
        functions.append((name, arity))
    # A loop by tail calls, adding to its accumulator a step of $n alone, so
    # that its values stay small enough to compute exactly either way.
    step = expression(rng, ["$n"], [], 2)
    lines.append("(= (loop $n $acc) (if (<= $n 0) $acc (loop (- $n 1) (+ $acc %s))))" % step)
    # A tree recursion.
    lines.append("(= (tree $n) (if (< $n 2) $n (+ (tree (- $n 1)) (tree (- $n 2)))))")
    # Literal equations beside a guarded variable equation.
    lines.append("(= (lit 0) %d)" % rng.randint(-5, 5))
    lines.append("(= (lit 1) %d)" % rng.randint(-5, 5))
    lines.append("(= (lit $n) (if (> $n 1) (+ (lit (- $n 1)) (lit (- $n 2))) (empty)))")
    # A literal beside a variable in one pattern.
    lines.append("(= (mix 0 $b) $b)")
    lines.append("(= (mix $a $b) (if (> $a 0) (mix (- $a 1) (+ $b %s)) (empty)))"
                 % expression(rng, ["$a"], [], 2))
    # Truth values through mutual recursion.
    lines.append("(= (ev $n) (if (== $n 0) True (od (- $n 1))))")
    lines.append("(= (od $n) (if (== $n 0) False (ev (- $n 1))))")
    queries = []
    for name, arity in functions:
        for _ in range(3):
            pool = EDGES if rng.random() < 0.4 else [str(rng.randint(-20, 20))]
            args = " ".join(rng.choice(pool) for _ in range(arity))
            queries.append("!(%s %s)" % (name, args))
    queries.append("!(loop %d %s)" % (rng.randint(0, 3000), rng.choice(EDGES[:7])))
    queries.append("!(tree %d)" % rng.randint(-2, 18))
    queries.append("!(lit %d)" % rng.randint(-2, 25))
    queries.append("!(ev %d)" % rng.randint(0, 400))
    queries.append("!(mix %d %s)" % (rng.randint(-2, 300), rng.choice(EDGES[:7])))
    return "\n".join(lines + queries) + "\n"


def term_program(rng):
    """Lists, trees, pairs and symbols over integers."""
    x_expr = expression(rng, ["$x"], [], 2)
    n_expr = expression(rng, ["$n"], [], 2)
    cond = condition(rng, ["$x"], [], 1)
    lines = [
        "(= (mk $n) (if (> $n 0) (Cons %s (mk (- $n 1))) Nil))" % n_expr,
        "(= (app Nil $ys) $ys)",
        "(= (app (Cons $x $xs) $ys) (Cons $x (app $xs $ys)))",
        "(= (rev Nil) Nil)",
        "(= (rev (Cons $x $xs)) (app (rev $xs) (Cons $x Nil)))",
        "(= (len Nil) 0)",
        "(= (len (Cons $x $xs)) (+ 1 (len $xs)))",
        "(= (sum Nil) 0)",
        "(= (sum (Cons $x $xs)) (+ $x (sum $xs)))",
        "(= (mapf Nil) Nil)",
        "(= (mapf (Cons $x $xs)) (Cons %s (mapf $xs)))" % x_expr,
        "(= (filt Nil) Nil)",
        "(= (filt (Cons $x $xs)) (if %s (Cons $x (filt $xs)) (filt $xs)))" % cond,
        "(= (ins $x Leaf) (Node Leaf $x Leaf))",
        "(= (ins $x (Node $l $y $r)) (if (< $x $y) (Node (ins $x $l) $y $r)"
        " (if (> $x $y) (Node $l $y (ins $x $r)) (Node $l $y $r))))",
        "(= (tolist Leaf $acc) $acc)",
        "(= (tolist (Node $l $y $r) $acc) (tolist $l (Cons $y (tolist $r $acc))))",
        "(= (tree Nil $t) $t)",
        "(= (tree (Cons $x $xs) $t) (tree $xs (ins $x $t)))",
        "(= (first (Pair $a $b)) $a)",
        "(= (swap (Pair $a $b)) (Pair $b $a))",
        "(= (tag $x) (if (< $x 0) Neg (if (== $x 0) Zero Pos)))",
        "(= (same-tag $x $y) (== (tag $x) (tag $y)))",
        "(= (unpair $p) (let (Pair $a $b) $p (+ $a $b)))",
        "(= (nth Nil $n) (empty))",
        "(= (nth (Cons $x $xs) $n) (if (== $n 0) $x (nth $xs (- $n 1))))",
    ]
    small = lambda: rng.randint(-20, 20)
    edge = lambda: rng.choice(EDGES + [str(small())])
    queries = [
        "!(len (mk %d))" % rng.randint(0, 300),
        "!(sum (mapf (mk %d)))" % rng.randint(0, 300),
        "!(rev (mk %d))" % rng.randint(0, 12),
        "!(len (rev (mk %d)))" % rng.randint(0, 250),
        "!(filt (mk %d))" % rng.randint(0, 15),
        "!(tolist (tree (mk %d) Leaf) Nil)" % rng.randint(0, 15),
        "!(sum (tolist (tree (mapf (mk %d)) Leaf) Nil))" % rng.randint(0, 200),
        "!(swap (Pair %s %s))" % (rng.choice(["A", "B", "Nil"]), small()),
        "!(first (swap (Pair %d (Cons %d Nil))))" % (small(), small()),
        "!(same-tag %s %s)" % (edge(), edge()),
        "!(unpair (Pair %s %s))" % (edge(), edge()),
        "!(len (Cons 1 (Cons 2 (Cons 3 Nil))))",
        "!(nth (mk %d) %d)" % (rng.randint(0, 20), rng.randint(-1, 22)),
        "!(== (mk %d) (mk %d))" % (rng.randint(0, 3), rng.randint(0, 3)),
        "!(== (tag %d) Pos)" % small(),
        "!(mapf (Cons %s (Cons %s Nil)))" % (edge(), edge()),
    ]
    rng.shuffle(queries)
    return "\n".join(lines + queries) + "\n"


# Programs the native code answers itself, without declining to the
# register code anywhere: lists long enough that the heap is collected many
# times, answers copied out whole.  A decline here is a fault of the native
# tier (a collection that lost a value reads as a mismatch and declines).
ANSWERED = [
    """(= (app Nil $ys) $ys)
(= (app (Cons $x $xs) $ys) (Cons $x (app $xs $ys)))
(= (rvs Nil) Nil)
(= (rvs (Cons $x $xs)) (app (rvs $xs) (Cons $x Nil)))
(= (count Nil $acc) $acc)
(= (count (Cons $x $xs) $acc) (count $xs (+ $acc 1)))
(= (mk $n) (if (> $n 0) (Cons $n (mk (- $n 1))) Nil))
!(count (rvs (mk 400)) 0)
""",
    """(= (mk $n) (if (> $n 0) (Cons (% (* $n 7919) 1000) (mk (- $n 1))) Nil))
(= (total Nil $acc) $acc)
(= (total (Cons $x $xs) $acc) (total $xs (+ $acc $x)))
(= (mapf Nil) Nil)
(= (mapf (Cons $x $xs)) (Cons (- (* 3 $x) 1) (mapf $xs)))
(= (filt Nil) Nil)
(= (filt (Cons $x $xs)) (if (> $x 500) (Cons $x (filt $xs)) (filt $xs)))
!(total (filt (mapf (mk 50000))) 0)
""",
    """(= (mk $n) (if (> $n 0) (Cons (% (* $n 7919) 1009) (mk (- $n 1))) Nil))
(= (ins $x Leaf) (Node Leaf $x Leaf))
(= (ins $x (Node $l $y $r)) (if (< $x $y) (Node (ins $x $l) $y $r) (if (> $x $y) (Node $l $y (ins $x $r)) (Node $l $y $r))))
(= (tree Nil $t) $t)
(= (tree (Cons $x $xs) $t) (tree $xs (ins $x $t)))
(= (tolist Leaf $acc) $acc)
(= (tolist (Node $l $y $r) $acc) (tolist $l (Cons $y (tolist $r $acc))))
(= (rvs-acc Nil $acc) $acc)
(= (rvs-acc (Cons $x $xs) $acc) (rvs-acc $xs (Cons $x $acc)))
!(rvs-acc (tolist (tree (mk 3000) Leaf) Nil) Nil)
""",
    """(= (swap (Pair $a $b)) (Pair $b $a))
(= (unpair $p) (let (Pair $a $b) $p (+ $a $b)))
(= (tag $x) (if (< $x 0) Neg (if (== $x 0) Zero Pos)))
(= (pairs $n) (if (> $n 0) (Cons (Pair $n (tag (- $n 5))) (pairs (- $n 1))) Nil))
(= (firsts Nil) Nil)
(= (firsts (Cons (Pair $a $b) $rest)) (Cons (swap (Pair $b $a)) (firsts $rest)))
!(unpair (Pair 20 22))
!(firsts (pairs 8))
!(swap (Pair A (Cons 1 Nil)))
""",
]


def run(cetta, path, lang, native):
    env = dict(os.environ)
    env["CETTA_PREPARED_PURE_DEBUG"] = "1"
    if not native:
        env["CETTA_PREPARED_PURE_NATIVE"] = "0"
    result = subprocess.run([cetta, "--lang", lang, path], env=env,
                            capture_output=True, text=True, timeout=120)
    lowered = result.stderr.count("register code lowered to native code")
    return result.returncode, result.stdout, lowered


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("cetta")
    parser.add_argument("--programs", type=int, default=200)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    lowered = {"he": 0, "petta": 0, "terms": 0}
    with tempfile.TemporaryDirectory() as work:
        for index in range(args.programs):
            terms = index % 2 == 1
            source = term_program(rng) if terms else program(rng)
            lang = "petta" if not terms and index % 8 == 6 else "he"
            path = os.path.join(work, "p%d.metta" % index)
            with open(path, "w") as handle:
                handle.write(source)
            native = run(args.cetta, path, lang, True)
            reference = run(args.cetta, path, lang, False)
            if native[:2] != reference[:2]:
                print("FAIL: program %d (--lang %s): the native image and the register "
                      "code differ" % (index, lang))
                print(source)
                print("--- register code\n%s--- native\n%s" % (reference[1], native[1]))
                return 1
            lowered["terms" if terms else lang] += native[2]
        for index, source in enumerate(ANSWERED):
            path = os.path.join(work, "answered%d.metta" % index)
            with open(path, "w") as handle:
                handle.write(source)
            env = dict(os.environ, CETTA_PREPARED_PURE_DEBUG="1")
            result = subprocess.run([args.cetta, "--lang", "he", path], env=env,
                                    capture_output=True, text=True, timeout=120)
            reference = run(args.cetta, path, "he", False)
            if (result.returncode, result.stdout) != reference[:2] or \
                    "run declined" in result.stderr or \
                    "register code lowered to native code" not in result.stderr:
                print("FAIL: answered program %d: the native code did not answer it alone"
                      % index)
                print(source)
                print("--- register code\n%s--- native\n%s" % (reference[1], result.stdout))
                print("\n".join(l for l in result.stderr.splitlines() if "native" in l))
                return 1
    for kind, count in lowered.items():
        if count == 0:
            print("FAIL: no %s program was lowered to native code" % kind)
            return 1
    print("PASS: %d random programs answer alike natively and on the register code "
          "(lowered to native code: integers %d in HE, %d in PeTTa; terms %d)"
          % (args.programs, lowered["he"], lowered["petta"], lowered["terms"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
