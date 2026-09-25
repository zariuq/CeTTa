#!/usr/bin/env python3
"""The compiled open equation tier against canonical equation search.

Usage: check_open_equations_differential.py CETTA_BINARY [--stats]
                                            [--programs N]

Each seeded program has a few relations whose first argument is an integer
budget: every call passes a smaller budget under a positive guard, so every
query terminates.  Derivations grow doubly exponentially in the budget, so
queries start at budget 2 or less; the fixed programs keep their open
queries small for the same reason.  Heads carry constructors, integers and repeated
variables; bodies use `if` over integer comparisons, `let` and `let*` with
structured patterns, `+ - * % min max`, `(empty)`, constructors and calls.
Queries leave variables open, share them between arguments, and set
occurs-check traps.  Each query is its own document.  The output of the
default route must equal the output under CETTA_OPEN_EQUATIONS_REFERENCE=1
(canonical equation search) exactly, exit status included; documents on
which canonical search does not terminate are checked against the outputs
of the PeTTa reference implementation.  With --stats
the binary must be a runtime-stats build, and the open tier must have taken
and answered calls.
"""
import os
import random
import subprocess
import sys
import tempfile

CONSTANTS = ["a", "b", "nil"]
UNARY = ["k1", "s"]
BINARY = ["k2", "pair"]
VARS = ["$x", "$y", "$z", "$w"]
TESTS = ["<", ">", "<=", ">=", "=="]


def render(term):
    if isinstance(term, tuple):
        return "(" + " ".join(render(part) for part in term) + ")"
    return str(term)


def gen_pattern(rng, depth, variables):
    """A head or let pattern: constructors, integers, variables."""
    roll = rng.random()
    if depth == 0 or roll < 0.45:
        if rng.random() < 0.65:
            return rng.choice(variables)
        return rng.choice(CONSTANTS + [rng.randint(-2, 3)])
    if roll < 0.75:
        return (rng.choice(UNARY), gen_pattern(rng, depth - 1, variables))
    return (rng.choice(BINARY), gen_pattern(rng, depth - 1, variables),
            gen_pattern(rng, depth - 1, variables))


def vars_of(term, out):
    if isinstance(term, tuple):
        for part in term[1:]:
            vars_of(part, out)
    elif isinstance(term, str) and term.startswith("$"):
        if term not in out:
            out.append(term)
    return out


def gen_int(rng, depth):
    """Integer arithmetic over the budget and literals."""
    if depth == 0 or rng.random() < 0.4:
        return rng.choice(["$n", "$n", rng.randint(-3, 5)])
    op = rng.choice(["+", "-", "*", "%", "min", "max"])
    left = gen_int(rng, depth - 1)
    if op == "%":
        right = rng.choice([2, 3, 5, -3])
    else:
        right = gen_int(rng, depth - 1)
    return (op, left, right)


class Relation:
    def __init__(self, name, arity):
        self.name = name
        self.arity = arity


def gen_value(rng, depth, bound):
    """A value: data over bound variables, with integer arithmetic."""
    roll = rng.random()
    if depth == 0 or roll < 0.35:
        if bound and rng.random() < 0.7:
            return rng.choice(bound)
        return rng.choice(CONSTANTS + [rng.randint(-2, 4)])
    if roll < 0.5:
        return gen_int(rng, 2)
    if roll < 0.75:
        return (rng.choice(UNARY), gen_value(rng, depth - 1, bound))
    return (rng.choice(BINARY), gen_value(rng, depth - 1, bound),
            gen_value(rng, depth - 1, bound))


def gen_call(rng, relations, bound):
    target = rng.choice(relations)
    args = [("-", "$n", 1)]
    for _ in range(target.arity - 1):
        args.append(gen_value(rng, 2, bound))
    return tuple([target.name] + args)


def gen_body(rng, depth, relations, bound, fresh, may_call):
    """A tail expression.  Calls occur only where `may_call` holds, which
    is inside a positive budget guard."""
    roll = rng.random()
    if depth == 0 or roll < 0.25:
        return gen_value(rng, 2, bound)
    if roll < 0.35:
        return ("empty",)
    if roll < 0.5:
        test = rng.choice(TESTS)
        return ("if", (test, gen_int(rng, 1), gen_int(rng, 1)),
                gen_body(rng, depth - 1, relations, bound, fresh, may_call),
                gen_body(rng, depth - 1, relations, bound, fresh, may_call))
    if roll < 0.72 and may_call:
        pattern = gen_pattern(rng, 2, bound + fresh)
        vars_of(pattern, bound)
        return ("let", pattern, gen_call(rng, relations, bound),
                gen_body(rng, depth - 1, relations, bound, fresh, may_call))
    if roll < 0.85:
        pairs = []
        for _ in range(rng.randint(1, 3)):
            if may_call and rng.random() < 0.6:
                value = gen_call(rng, relations, bound)
            else:
                value = gen_value(rng, 2, bound)
            pattern = gen_pattern(rng, 2, bound + fresh)
            pairs.append((pattern, value))
            vars_of(pattern, bound)
        return ("let*", tuple(pairs),
                gen_body(rng, depth - 1, relations, bound, fresh, may_call))
    if may_call:
        return gen_call(rng, relations, bound)
    return gen_value(rng, 2, bound)


def gen_program(rng, index):
    relations = [Relation(f"r{index}x{i}", rng.randint(1, 3))
                 for i in range(rng.randint(2, 4))]
    lines = []
    for relation in relations:
        for _ in range(rng.randint(1, 4)):
            params = [gen_pattern(rng, 2, VARS) for _ in range(relation.arity - 1)]
            head = tuple([relation.name, "$n"] + params)
            bound = vars_of(head, [])
            fresh = [v for v in ["$p", "$q", "$r"]]
            body = ("if", (">", "$n", 0),
                    gen_body(rng, 3, relations, list(bound), fresh, True),
                    gen_body(rng, 2, relations, list(bound), fresh, False))
            if rng.random() < 0.25:
                body = gen_body(rng, 2, relations, list(bound), fresh, False)
            lines.append(f"(= {render(head)} {render(body)})")
    queries = []
    for _ in range(rng.randint(4, 7)):
        relation = rng.choice(relations)
        qvars = ["$A", "$B", "$C"]
        args = [rng.randint(0, 2)]
        for _ in range(relation.arity - 1):
            roll = rng.random()
            if roll < 0.4:
                args.append(rng.choice(qvars))
            elif roll < 0.55 and len(args) > 1:
                # An occurs-check trap: the argument contains a variable
                # that another argument is.
                args.append(("k1", rng.choice(qvars)))
            else:
                args.append(gen_pattern(rng, 2, qvars))
        queries.append("!" + render(tuple([relation.name] + args)))
    base = "\n".join(lines) + "\n"
    return [base + query + "\n" for query in queries]


def gen_productive_pattern(rng, variables, depth=1):
    roll = rng.random()
    if depth == 0 or roll < 0.6:
        return rng.choice(variables)
    if roll < 0.75:
        return ("s", gen_productive_pattern(rng, variables, depth - 1))
    if roll < 0.92:
        return ("k2", gen_productive_pattern(rng, variables, depth - 1),
                gen_productive_pattern(rng, variables, depth - 1))
    return rng.choice(["a", "b", "nil"])


def gen_productive_term(rng, bound, depth=2):
    roll = rng.random()
    if depth == 0 or roll < 0.4:
        return rng.choice(bound + ["a", "b"]) if bound else rng.choice(["a", "b"])
    if roll < 0.6:
        return ("s", gen_productive_term(rng, bound, depth - 1))
    return ("k2", gen_productive_term(rng, bound, depth - 1),
            gen_productive_term(rng, bound, depth - 1))


def gen_productive_program(rng, index):
    """Programs that answer: variable-heavy patterns, calls with fresh open
    arguments inside bodies, constructor results."""
    relations = [Relation(f"q{index}x{i}", rng.randint(2, 3))
                 for i in range(rng.randint(2, 3))]
    lines = []
    for relation in relations:
        for _ in range(rng.randint(1, 3)):
            params = [gen_productive_pattern(rng, VARS)
                      for _ in range(relation.arity - 1)]
            head = tuple([relation.name, "$n"] + params)
            bound = vars_of(head, [])
            base = ("empty",) if rng.random() < 0.15 else \
                gen_productive_term(rng, bound)
            pairs = []
            local = list(bound)
            for step in range(rng.randint(1, 2)):
                target = rng.choice(relations)
                args = [("-", "$n", 1)]
                for _ in range(target.arity - 1):
                    roll = rng.random()
                    if roll < 0.3:
                        args.append(rng.choice(["$p", "$q", "$r"]))
                    else:
                        args.append(gen_productive_term(rng, local, 1))
                pattern = gen_productive_pattern(
                    rng, ["$u", "$v", "$p", "$q"] + local, 1)
                pairs.append((pattern, tuple([target.name] + args)))
                vars_of(pattern, local)
                vars_of(tuple(["_"] + args), local)
            result = gen_productive_term(rng, local)
            recursive = ("let*", tuple(pairs), result)
            if rng.random() < 0.3:
                test = rng.choice(TESTS)
                recursive = ("if", (test, gen_int(rng, 1), gen_int(rng, 1)),
                             recursive, gen_productive_term(rng, local))
            body = ("if", (">", "$n", 0), recursive, base)
            lines.append(f"(= {render(head)} {render(body)})")
    queries = []
    for _ in range(rng.randint(3, 6)):
        relation = rng.choice(relations)
        args = [rng.randint(1, 2)]
        for _ in range(relation.arity - 1):
            roll = rng.random()
            if roll < 0.5:
                args.append(rng.choice(["$A", "$B"]))
            elif roll < 0.6:
                args.append(("s", rng.choice(["$A", "$B"])))
            else:
                args.append(gen_productive_term(rng, ["$A", "$B"], 1))
        queries.append("!" + render(tuple([relation.name] + args)))
    base = "\n".join(lines) + "\n"
    return [base + query + "\n" for query in queries]


CLASSICS = [
    # Peano numbers and budgeted append over open arguments.
    """(= (nat $n Z) Z)
(= (nat $n (S $m)) (if (> $n 0) (let $k (nat (- $n 1) $m) (S $k)) (empty)))
(= (ap $n nl $ys) $ys)
(= (ap $n (cns $h $t) $ys) (if (> $n 0) (let $r (ap (- $n 1) $t $ys) (cns $h $r)) (empty)))
(= (plus $n Z $y) $y)
(= (plus $n (S $x) $y) (if (> $n 0) (let $z (plus (- $n 1) $x $y) (S $z)) (empty)))
!(nat 5 $x)
!(ap 4 $x $y)
!(ap 5 (cns 1 (cns 2 nl)) (cns 3 $t))
!(ap 5 $x (cns 3 nl))
!(plus 4 $a $b)
!(plus 4 (S $a) (S (S Z)))
""",
    # A small type inferencer: answers keep type variables open.
    """(= (lkp $n (bnd $v $t $rest) $v) $t)
(= (lkp $n (bnd $w $t $rest) $v) (if (> $n 0) (lkp (- $n 1) $rest $v) (empty)))
(= (tyof $n $env (lit $k)) int)
(= (tyof $n $env (vr $v)) (lkp $n $env $v))
(= (tyof $n $env (lm $v $b)) (if (> $n 0) (let $t (tyof (- $n 1) (bnd $v $a $env) $b) (arr $a $t)) (empty)))
(= (tyof $n $env (ap $f $x)) (if (> $n 0) (let* (((arr $a $r) (tyof (- $n 1) $env $f)) ($a (tyof (- $n 1) $env $x))) $r) (empty)))
!(tyof 6 emp (lm x (lm y (ap (vr x) (vr y)))))
!(tyof 6 emp (lm f (lm x (ap (vr f) (ap (vr f) (vr x))))))
!(tyof 6 emp (lm x (ap (vr x) (vr x))))
!(tyof 6 (bnd k int emp) (ap (lm x (vr x)) (vr k)))
!(tyof 3 emp $e)
""",
    # Selection with open tails, counted by a budget; repeated variables.
    """(= (sel $n (cns $x $t)) (pr $x $t))
(= (sel $n (cns $y $t)) (if (> $n 0) (let (pr $x $r) (sel (- $n 1) $t) (pr $x (cns $y $r))) (empty)))
(= (same $x $x) yes)
(= (same $x (f $x)) loop)
!(sel 4 (cns 1 (cns 2 (cns 3 nl))))
!(sel 3 (cns $a (cns 2 $rest)))
!(same $u $u)
!(same $u (f $u))
!(same (g $u) $u)
!(same $u $v)
""",
    # Relational occurrences in heads: a callable subterm runs after the
    # structural match, in source order, its value unified with the query
    # subterm; a list whose head element is a term is data.
    """(= (identity $x) $x)
(= (twice $x) (pair $x $x))
(= (view (identity $x)) (pair $x $x))
(= (view2 (identity $x) (twice $y)) (triple $x $y $x))
(= (nested (box (identity $x)) $x) ok)
(= (lookup ((pair $n $t) $rest) $n) $t)
(= (lookup ((pair $m $t) $rest) $n) (lookup $rest $n))
(= (extend $env $k $v) ((pair $k $v) $env))
!(view (identity value))
!(view $q)
!(view2 value (pair a a))
!(view2 $u (pair $w b))
!(nested (box v) v)
!(nested $z v)
!(lookup ((pair a 1) ((pair b 2) nil)) b)
!(lookup ((pair a 1) ((pair b 2) nil)) $k)
!(lookup (extend (extend nil a 1) b 2) $k)
""",
    # An authority change between the answers of a suspended open call: a
    # translator rule for a relation outside the program is added and removed
    # while the call's remaining alternatives wait; they still run.
    """(= (lane one) ok)
(= (lane two) (empty))
(= (lane three) (empty))
(= (walk $n (one $p)) (let $_ (lane one) (if (== $n 0) (done $p) (walk (- $n 1) $next))))
(= (walk $n (two $p)) (let $_ (lane two) (if (== $n 0) never (walk (- $n 1) $next))))
(= (walk $n (three $p)) (let $_ (lane three) (if (== $n 0) never (walk (- $n 1) $next))))
(= (other x) y)
!(let $before (walk 2 $first) (let $_ (add-translator-rule! other) (let $during (walk 2 $second) (let $_ (remove-translator-rule! other) (walk 2 $third)))))
!(let $before (walk 1 $first) (let $_ (add-translator-rule! other) (pair $before (walk 1 $second))))
""",
    # Equations added or removed between the answers of an open call: calls
    # already entered keep their equations, later calls see the change.
    """(= (p a) 1)
(= (p b) 2)
(= (p c) 3)
(= (chain $n Z) Z)
(= (chain $n (S $m)) (if (> $n 0) (let $k (chain (- $n 1) $m) (S $k)) (empty)))
(= (grow) (let $_ (add-atom &self (= (chain $n (T $m)) (if (> $n 0) (let $k (chain (- $n 1) $m) (T $k)) (empty)))) done))
!(let $v (p $x) (if (== $v 1) (let $_ (add-atom &self (= (p d) 4)) (r $x $v)) (r $x $v)))
!(let $v (p $x) (if (== $v 1) (let $_ (add-atom &self (= (q z) 9)) (r $x $v)) (r $x $v)))
!(let $v (p $x) (if (== $v 2) (let $_ (remove-atom &self (= (p c) 3)) (r $x $v)) (r $x $v)))
!(let $v (chain 3 $x) (if (== $v (S Z)) (let $_ (grow) (pair $x $v)) (pair $x $v)))
""",
    # Destinations: a demanded constructor meets an equation's exposed
    # output before its body runs, bounding a recursive inverse search.
    """(= (plus Z $right) $right)
(= (plus (S $left) $right) (S (plus $left $right)))
!(let (plus $left (S Z)) (S (S (S (S Z)))) $left)
!(collapse (let (plus $left $right) (S (S Z)) ($left $right)))
!(let (S (S (S (S Z)))) (plus $left (S Z)) $left)
!(collapse (let (plus (S Z) Z) impossible present))
""",
    # Grounded arithmetic past the integer path, after earlier answers:
    # floats, bigints, int64 overflow, zero divisors and raised errors, and
    # `==` over symbols, structures and unbound variables.
    """(= (p a) 1)
(= (p b) (+ 1.5 2.5))
(= (p c) (+ 9223372036854775807 1))
(= (p d) (* 3 4611686018427387904))
(= (p e) (- 0.0 2.5))
(= (q a) 1)
(= (q b) (+ c 1))
(= (q c) (/ 7 0))
(= (q d) 7)
(= (t 1) one)
(= (t $x) (if (== $x foo) yes no))
(= (w 1) one)
(= (w $x) (if (== $x $x) same diff))
(= (u 1) one)
(= (u $x) (if (< $x 2.5) small big))
(= (s (pt 1 2)) one)
(= (s $x) (if (== $x (pt 1 2)) same diff))
!(p $x)
!(collapse (p $x))
!(q $x)
!(collapse (q $x))
!(t $q)
!(t foo)
!(w $q)
!(u $q)
!(u 7)
!(s $q)
!(s (pt 1 2))
""",
    # Builtin names: an equation for a machine-named builtin does not
    # replace the builtin, inside a compiled body or at the root.
    """(= (reverse $x) (mine $x))
(= (last $x) (mylast $x))
(= (usea $x) (let $r (reverse $x) (got $r)))
(= (usel $x) (let $r (last $x) (got $r)))
!(usea (1 2 3))
!(usel (1 2 3))
!(reverse (1 2 3))
""",
    # Program and authority changes between answers: pending alternatives
    # keep the equations their calls entered with; later calls enter the
    # current definitions, or leave the tier when those are outside it.
    """(= (p a) 1)
(= (p b) 2)
(= (q 1) one)
(= (q 2) two)
(= (callq $y) (q $y))
(= (outer $z) (let $v (q $z) (let $w (callq $u) ($z $v $u $w))))
!(let $v (p $x) (if (== $v 1) (let $_ (add-atom &self (= (p c) (superpose (3 4)))) (pair $x $v)) (pair $x $v)))
!(let $v (p $x) (if (== $v 1) (let $_ (remove-atom &self (= (p a) 1)) (let $_ (remove-atom &self (= (p b) 2)) (pair $x $v))) (pair $x $v)))
!(let $v (p $x) (if (== $v 1) (let $_ (add-atom &self (: p (-> Atom Number))) (pair $x $v)) (pair $x $v)))
!(let $r (outer $z) (let $_ (add-atom &self (= (q 3) (superpose (x y)))) $r))
""",
    # Long deterministic loops and searches: the region is collected
    # between steps, cells are compacted, and backtracking across
    # collections restores exactly.
    """(= (cnt $n $x) (if (> $n 0) (cnt (- $n 1) $x) (done $x)))
(= (acc $n $l) (if (> $n 0) (acc (- $n 1) (cons $n $l)) (len $l)))
(= (len ()) 0)
(= (len (cons $h $t)) (+ 1 (len $t)))
(= (digit) 0)
(= (digit) 1)
(= (digit) 2)
(= (digit) 3)
(= (digit) 4)
(= (digit) 5)
(= (digit) 6)
(= (digit) 7)
(= (digit) 8)
(= (digit) 9)
(= (sum3 $s) (let $a (digit) (let $b (digit) (let $c (digit) (if (== (+ $a (+ $b $c)) $s) (t $a $b $c) (empty))))))
(= (app Nil $l $l) done)
(= (app (Cons $h $t) $l (Cons $h $r)) (app $t $l $r))
(= (mk $n) (if (> $n 0) (Cons $n (mk (- $n 1))) Nil))
(= (splits $n) (let $l (mk $n) (let $d (app $x $y $l) (pair $x $y))))
(= (burn $n $k) (if (> $n 0) (burn (- $n 1) (+ $k 1)) $k))
(= (spread $x) (let $b (burn 30000 0) (sp $x $b)))
!(cnt 300000 $q)
!(acc 100000 ())
!(collapse (sum3 25))
!(collapse (splits 8))
!(collapse (let $d (digit) (spread $d)))
""",
    # Selection: a call whose other equations cannot match leaves no
    # alternative behind, while an equation selection cannot refute (a
    # repeated variable) stays one.
    """(= (rank (sen ($x (stv $f $c)) $e)) $c)
(= (rank (sen ($x (stv $f $c)) $x)) -1.0)
(= (rank ()) -99999.0)
(= (best $b ()) $b)
(= (best $b (cons $h $t)) (if (> (rank $h) (rank $b)) (best $h $t) (best $b $t)))
!(best () (cons (sen (a (stv 1.0 0.5)) (1)) (cons (sen (b (stv 1.0 0.9)) (2)) (cons (sen (c (stv 1.0 0.7)) (3)) ()))))
!(collapse (rank (sen (a (stv 1.0 0.5)) (1))))
!(rank ())
""",
    # Arithmetic in heads, guards and results.
    """(= (fib $n) (if (< $n 2) $n (+ (fib (- $n 1)) (fib (- $n 2)))))
(= (walk $n $x) (pr $n $x))
(= (walk $n $x) (if (> $n 0) (walk (- $n 1) (st (% (* $n 7) 5) $x)) (empty)))
(= (mm $n $k) (if (>= $n 0) (pr (min $n $k) (max $n $k)) (empty)))
!(fib 15)
!(walk 5 $z)
!(walk 3 (st $a $b))
!(mm 3 $k)
""",
]


# Outputs fixed by the PeTTa reference implementation, where canonical
# equation search does not terminate: a closed call whose destination is
# demanded is bounded by it.
PETTA_FIXED = [
    ("""(= (nat) Z)
(= (nat) (S (nat)))
(= (nat2 $u) Z)
(= (nat2 $u) (S (nat2 $u)))
(= (natp $x) (pair $x (nat)))
(= (wrap $n) (let $m (nat2 $n) (box $m)))
!(let (S (S Z)) (nat) found)
!(let (nat2 u) (S (S Z)) found)
!(let (pair a (S Z)) (natp a) found2)
!(let (box (S Z)) (wrap 0) found3)
""", "found\nfound\nfound2\nfound3\n"),
]


def run(binary, path, reference, stats):
    env = dict(os.environ)
    if reference:
        env["CETTA_OPEN_EQUATIONS_REFERENCE"] = "1"
    else:
        env.pop("CETTA_OPEN_EQUATIONS_REFERENCE", None)
    command = [binary, "--lang", "petta"]
    if stats:
        command.append("--emit-runtime-stats")
    command.append(path)
    result = subprocess.run(command, env=env, capture_output=True, text=True,
                            timeout=120)
    return result.returncode, result.stdout, result.stderr


def main():
    args = sys.argv[1:]
    stats = "--stats" in args
    if stats:
        args.remove("--stats")
    programs = 40
    if "--programs" in args:
        position = args.index("--programs")
        programs = int(args[position + 1])
        del args[position:position + 2]
    binary = os.path.abspath(args[0])
    rng = random.Random(20260924)
    counters = {}
    answers = 0
    documents = []
    for classic in CLASSICS:
        lines = classic.strip().splitlines()
        base = [line for line in lines if not line.startswith("!")]
        for query in (line for line in lines if line.startswith("!")):
            documents.append("\n".join(base + [query]) + "\n")
    for index in range(programs):
        documents += gen_program(rng, index)
        documents += gen_productive_program(rng, index)
    with tempfile.TemporaryDirectory(prefix="open-equations-") as directory:
        queries = 0
        for index, text in enumerate(documents):
            query_index = 0
            if True:
                path = os.path.join(directory, f"d{index}.metta")
                with open(path, "w") as handle:
                    handle.write(text)
                tier = run(binary, path, False, stats)
                reference = run(binary, path, True, False)
                queries += 1
                if tier[0] != reference[0] or tier[1] != reference[1]:
                    sys.stderr.write(
                        f"FAIL document {index}: exit "
                        f"{tier[0]} / {reference[0]}\n{text}\n"
                        f"open tier:\n{tier[1][:1500]}\n{tier[2][-800:]}\n"
                        f"reference:\n{reference[1][:1500]}\n"
                        f"{reference[2][-800:]}\n")
                    return 1
                answers += len(tier[1].splitlines())
                for line in tier[2].splitlines():
                    parts = line.split()
                    if len(parts) == 3 and parts[0] == "runtime-counter" and \
                            parts[1].startswith("open-equation-"):
                        counters[parts[1]] = counters.get(parts[1], 0) + \
                            int(parts[2])
        for index, (text, expected) in enumerate(PETTA_FIXED):
            path = os.path.join(directory, f"fixed{index}.metta")
            with open(path, "w") as handle:
                handle.write(text)
            tier = run(binary, path, False, False)
            queries += 1
            if tier[0] != 0 or tier[1] != expected:
                sys.stderr.write(
                    f"FAIL fixed document {index}: exit {tier[0]}\n{text}\n"
                    f"open tier:\n{tier[1][:1500]}\n{tier[2][-800:]}\n"
                    f"expected:\n{expected}\n")
                return 1
    if stats:
        for counter in ("open-equation-choice", "open-equation-answer"):
            if counters.get(counter, 0) == 0:
                sys.stderr.write(
                    f"FAIL: {counter} never counted ({counters})\n")
                return 1
    print(f"PASS: {queries} queries ({answers} output lines) equal "
          "canonical equation search"
          + (f" ({counters})" if stats else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
