#!/usr/bin/env python3
"""Differential test of the compiled route against CeTTa's interpreter.

Each program runs twice, in separate processes: once with the compiled route
off (the reference) and once with it forced on wherever it applies.  HE results
must agree in order; PeTTa results must agree as multisets (answer order is not
PeTTa semantics).

Covers duplicate definitions, choice, partial and undefined calls, builtins,
recursion, data reads and joins, let, unique, reducible arguments to the
structural list builtins, effects inside recursion, definitions added and
removed while running, and once / existence / count wrappers.
"""

import collections
import os
import random
import subprocess
import sys
import tempfile

REFERENCE_ENV = {"CETTA_TCALC_EVAL": "0"}
CANDIDATE_ENV = {"CETTA_TCALC_EVAL": "always"}


def run_cetta(cetta, lang, code, env_extra, profile=None):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False,
                                     encoding="utf-8") as tf:
        tf.write(code)
        path = tf.name
    env = dict(os.environ)
    env.update(env_extra)
    argv = [cetta, "--lang", lang, "--quiet"]
    if profile:
        argv += ["--profile", profile]
    try:
        proc = subprocess.run(argv + [path], capture_output=True, text=True,
                              timeout=60, env=env)
        if proc.returncode != 0:
            raise AssertionError(
                f"cetta --lang {lang} failed (rc={proc.returncode}, env={env_extra}):\n"
                f"CODE:\n{code}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
        lines = []
        for raw in proc.stdout.splitlines():
            s = raw.strip()
            if not s:
                continue
            if lang == "he" and s.startswith("[") and s.endswith("]"):
                s = s[1:-1].strip()
            lines.append(s)
        return lines
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def parse_sexp_items(text):
    s = text.strip()
    if not (s.startswith("(") and s.endswith(")")):
        return [s]
    inner = s[1:-1].strip()
    if not inner:
        return []
    items, depth, in_str, esc, start = [], 0, False, False, None
    for i, ch in enumerate(inner):
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            if start is None:
                start = i
            in_str = True
        elif ch == "(":
            if start is None:
                start = i
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0 and start is not None:
                items.append(inner[start:i + 1])
                start = None
        elif ch.isspace():
            if depth == 0 and start is not None:
                items.append(inner[start:i])
                start = None
        elif start is None:
            start = i
    if start is not None:
        items.append(inner[start:])
    return items


def compare_lines(lang, label, ref, got, code):
    if len(ref) != len(got):
        raise AssertionError(f"{lang} {label}: {len(ref)} reference lines, {len(got)} compiled lines\n"
                             f"reference: {ref}\ncompiled:  {got}\nCODE:\n{code}")
    for i, (r, g) in enumerate(zip(ref, got)):
        if lang == "he":
            same = r == g
        else:
            same = collections.Counter(parse_sexp_items(r)) == collections.Counter(parse_sexp_items(g))
        if not same:
            raise AssertionError(f"{lang} {label} line {i}:\n  reference: {r}\n  compiled:  {g}\nCODE:\n{code}")


def check_program(cetta, lang, label, code, profile=None):
    ref = run_cetta(cetta, lang, code, REFERENCE_ENV, profile)
    got = run_cetta(cetta, lang, code, CANDIDATE_ENV, profile)
    compare_lines(lang, label, ref, got, code)


def check_batch(cetta, lang, label, defs, queries):
    lines = [defs]
    for expr, is_unique in queries:
        if is_unique:
            lines.append(f"!(Ans (collapse (unique {expr})))")
        else:
            lines.append(f"!(Ans (collapse {expr}))")
    check_program(cetta, lang, label, "\n".join(lines) + "\n")


def generate_program(rng: random.Random, seed_id: int) -> tuple[str, list[tuple[str, bool]]]:
    """Generate a terminating MeTTa program exercising duplicates, choice,
    recursion, data reads, undefined heads, builtins, let bindings, and unique."""
    defs = []
    queries: list[tuple[str, bool]] = []
    atoms = ["A", "B", "C", "1", "2", '"hi"']

    # 1. Data facts with intentional duplicates
    n_edges = rng.randint(4, 10)
    nodes = ["n0", "n1", "n2", "n3", "n4"]
    for _ in range(n_edges):
        # DAG edges i -> j with i < j so ordinary multiset recursion terminates
        i = rng.randrange(0, len(nodes) - 1)
        j = rng.randrange(i + 1, len(nodes))
        defs.append(f"(rel {nodes[i]} {nodes[j]})")
        if rng.random() < 0.35:
            defs.append(f"(rel {nodes[i]} {nodes[j]})")

    # 2. Nondeterministic leaf function f with duplicate clauses
    n_f = rng.randint(2, 5)
    for _ in range(n_f):
        arg = rng.choice(["A", "B", "C"])
        val = rng.choice(atoms)
        defs.append(f"(= (f {arg}) {val})")
    if rng.random() < 0.5:
        # Duplicate a clause explicitly
        defs.append(f"(= (f A) {rng.choice(['A', '1'])})")
        defs.append(f"(= (f A) {rng.choice(['A', '1'])})")

    # 3. Choice-combining function g (calls f multiple times or drops a let binding)
    mode = seed_id % 4
    if mode == 0:
        defs.append("(= (g $x) (Pair (f $x) (f $x)))")
    elif mode == 1:
        defs.append("(= (g $x) (let $a (f $x) (Tag $x)))")
        defs.append("(= (g $x) (Tag $x))")
    elif mode == 2:
        defs.append("(= (g $x) (let $a (f $x) (let $b (f A) (Mix $a $b))))")
    else:
        # Partial inner call to test HE unmatched inner call vs PeTTa failure
        defs.append("(= (onlyA A) Hit)")
        defs.append("(= (g $x) (Wrap (onlyA $x)))")
        defs.append("(= (g $x) (f $x))")

    # 4. Data-reading DAG reachability (exact multiplicity of paths in multiset mode)
    defs.append("(= (dpath $x) $x)")
    defs.append("(= (dpath $x) (match &self (rel $x $y) (dpath $y)))")

    # 5. Data-reading 2-hop join with duplicate data atoms
    defs.append("(= (hop2 $x) (match &self (rel $x $y) (match &self (rel $y $z) (Hop $x $z))))")

    # 6. Recursive Peano/list function + duplicate base case
    defs.append("(= (rev Nil) Nil)")
    defs.append("(= (rev (Cons $h $t)) (app (rev $t) (Cons $h Nil)))")
    defs.append("(= (app Nil $ys) $ys)")
    defs.append("(= (app (Cons $h $t) $ys) (Cons $h (app $t $ys)))")

    # 7. Builtin-using function (falls back to interpreter seamlessly)
    k = rng.randint(1, 9)
    defs.append(f"(= (bump $n) (+ $n {k}))")
    defs.append("(= (bump $n) (* $n 2))")

    # 8. Boolean-returning predicate
    defs.append("(= (isA A) True)")
    defs.append("(= (isA B) False)")

    # Queries covering every feature
    queries.append(("(f A)", False))
    queries.append(("(f B)", False))
    queries.append(("(f D)", False))  # unmatched partial call
    queries.append(("(g A)", False))
    queries.append(("(g B)", False))
    queries.append(("(g D)", False))  # unmatched inner call
    queries.append(("(dpath n0)", False))
    queries.append(("(dpath n0)", True))  # presence-only set store via unique
    queries.append(("(hop2 n0)", False))
    queries.append(("(hop2 n0)", True))
    queries.append(("(rev (Cons 1 (Cons 2 (Cons 3 Nil))))", False))
    queries.append(("(bump 5)", False))  # arithmetic builtin fallback
    queries.append(("(undef-head 1 2)", False))  # undefined head stays data
    queries.append(("(isA A)", False))
    queries.append(("(isA B)", False))

    return "\n".join(defs), queries


def test_focused_edge_cases(cetta: str, lang: str) -> None:
    defs = """
; Duplicate zero-arg definitions
(= (z) 1)
(= (z) 1)
(= (z) 2)

; Diamond recursion with branching multiplicity
(= (bd Z) Leaf)
(= (bd (S $n)) (bd $n))
(= (bd (S $n)) (bd $n))

; Nested let pattern destructuring
(= (mkpair) (Pair 1 2))
(= (mkpair) (Pair 1 2))
(= (mkpair) (Pair 3 4))
(= (unpack) (let (Pair $a $b) (mkpair) (Sum $b $a)))
(= (filter1) (let (Pair 1 $b) (mkpair) $b))

; Repeated LHS variable (non-linear pattern)
(= (same $x $x) Eq)
(= (same $x $y) Any)

; Empty expression as argument/result
(= (keep-nil ()) OkNil)

; Nested non-inert call argument passed via quote
(= (my-id $x) $x)

; Arithmetic, comparisons, floats, let*, if, empty, destructuring let
(= (fib $n) (if (<= $n 1) $n (+ (fib (- $n 1)) (fib (- $n 2)))))
(= (safe-div $a $b) (/ $a $b))
(= (use-let-star $x) (let* (($a (+ $x 1)) ($b (* $a 2))) (- $b $x)))
(= (if-nonbool $c) (if $c Yes No))
(= (filter-even $n) (if (== (% $n 2) 0) $n (empty)))
(= (len $l) (if (== $l Nil) 0 (let (Cons $h $t) $l (+ 1 (len $t)))))
(= (upto $n) (if (== $n 0) Nil (Cons $n (upto (- $n 1)))))
(= (pick (Cons $x $xs)) $x)
(= (pick (Cons $x (Cons $y $ys))) (pick (Cons $y $ys)))
(= (safe $c $placed $dist)
   (if (== $placed Nil) True
       (let (Cons $h $t) $placed
         (if (== $c $h) False
             (if (== (- $c $h) $dist) False
                 (if (== (- $h $c) $dist) False
                     (safe $c $t (+ $dist 1))))))))
(= (place $n $cols $placed)
   (if (== $n 0) $placed
       (let $c (pick $cols)
         (if (safe $c $placed 1)
             (place (- $n 1) $cols (Cons $c $placed))
             (empty)))))

; Lists, case, superpose, higher-order calls, and map/filter/foldl
(= (dbl $x) (* $x 2))
(= (apply-fn $f $x) ($f $x))
(= (list-ops $xs)
   (let ($h $t) (decons-atom $xs)
     (ListInfo $h (car-atom $xs) (cdr-atom $xs) (size-atom $xs) (index-atom $xs 1) (cons-atom 0 $t))))
(= (case-fn $x)
   (case $x
     (((Pair $a $b) (+ $a $b))
      (0 Zero)
      ($other (Other $other)))))
(= (sup-fn $x) (superpose ((+ $x 1) (* $x 10))))
(= (map-fn $k $xs) (map-atom $xs $x (+ (dbl $x) $k)))
(= (filt-fn $lo $xs) (filter-atom $xs $x (> $x $lo)))
(= (fold-fn $init $xs) (foldl-atom $xs $init $acc $x (+ $acc (dbl $x))))

; Conjunctive joins (acyclic cousin + cyclic triangles + duplicates)
(jp p0 p1) (jp p0 p2) (jp p1 p3) (jp p1 p4) (jp p2 p5) (jp p2 p6)
(jp p2 p6)
(tri a b) (tri b c) (tri c a) (tri b a)
(= (jcousin $a)
   (match &self (, (jp $x $a) (jp $y $b) (jp $g $x) (jp $g $y)) ($a $b)))
(= (jtri $a)
   (match &self (, (tri $a $b) (tri $b $c) (tri $c $a)) ($a $b $c)))
"""
    queries = [
        ("(z)", False),
        ("(P (z) (z))", False),
        ("(bd (S (S (S Z))))", False),
        ("(bd (S (S (S (S Z)))))", True),
        ("(unpack)", False),
        ("(filter1)", False),
        ("(same 7 7)", False),
        ("(same 7 8)", False),
        ("(keep-nil ())", False),
        ("(my-id (z))", False),
        ("(my-id (+ 2 3))", False),
        ("(unknown-call (z))", False),
        ("(fib 10)", False),
        ("(safe-div 4 2)", False),
        ("(safe-div 3 2)", False),
        ("(use-let-star 7)", False),
        ("(if-nonbool Maybe)", False),
        ("(filter-even 4)", False),
        ("(filter-even 5)", False),
        ("(len (upto 6))", False),
        ("(place 5 (upto 5) Nil)", False),
        ("(apply-fn dbl 9)", False),
        ("(apply-fn MkTag 9)", False),
        ("(list-ops (10 20 30))", False),
        ("(case-fn (Pair 3 4))", False),
        ("(case-fn 0)", False),
        ("(case-fn 9)", False),
        ("(sup-fn 5)", False),
        ("(map-fn 1 (1 2 3))", False),
        ("(map-fn 1 ())", False),
        ("(filt-fn 2 (1 2 3 4 5))", False),
        ("(filt-fn 2 ())", False),
        ("(fold-fn 10 (1 2 3))", False),
        ("(fold-fn 10 ())", False),
        ("(jcousin p3)", False),
        ("(jcousin p3)", True),
        ("(jtri a)", False),
    ]
    check_batch(cetta, lang, "edge-cases", defs, queries)




STRUCTURAL_BUILTINS = """
(= (choose) 1)
(= (choose) 2)
(= (pairs $n) (if (> $n 0) (cons-atom (choose) (pairs (- $n 1))) ()))
(= (cr $n) (if (> $n 0) (cr (- $n 1)) (cons-atom (choose) ())))
(= (car-r $n) (if (> $n 0) (car-r (- $n 1)) (car-atom ((choose) b))))
(= (cdr-r $n) (if (> $n 0) (cdr-r (- $n 1)) (cdr-atom (a (choose)))))
(= (dec-r $n) (if (> $n 0) (dec-r (- $n 1)) (decons-atom ((choose) b))))
(= (size-r $n) (if (> $n 0) (size-r (- $n 1)) (size-atom ((choose) (choose)))))
(= (idx-r $n) (if (> $n 0) (idx-r (- $n 1)) (index-atom ((choose) b) 0)))
!(Ans (collapse (pairs 2)))
!(Ans (collapse (cr 2)))
!(Ans (collapse (car-r 2)))
!(Ans (collapse (cdr-r 2)))
!(Ans (collapse (dec-r 2)))
!(Ans (collapse (size-r 2)))
!(Ans (collapse (idx-r 2)))
"""

EFFECTS_AND_LIVE_EDITS = """
(= (tick $n) (if (== $n 0) 0 (let $u (add-atom &self (t $n)) (+ 1 (tick (- $n 1))))))
!(Ans (collapse (tick 3)))
!(Ans (collapse (tick 3)))
!(Ans (collapse (match &self (t $x) $x)))
(= (rh 0) a)
(= (rh $n) (if (> $n 0) (rh (- $n 1)) none))
!(Ans (collapse (rh 3)))
!(Ans (collapse (let $u (add-atom &self (= (rh 0) b)) (rh 3))))
!(Ans (collapse (let $u (remove-atom &self (= (rh 0) a)) (rh 3))))
(= (d 0) x)
(= (d 0) x)
(= (d $n) (if (> $n 0) (d (- $n 1)) none))
!(Ans (collapse (d 2)))
"""

WRAPPERS = """
(rel n0 n1)
(rel n1 n2)
(rel n0 n2)
(rel n2 n3)
(= (dpath $x) $x)
(= (dpath $x) (match &self (rel $x $y) (dpath $y)))
!(Ans (collapse (once (dpath n0))))
!(Ans (== (collapse (dpath n0)) ()))
!(Ans (== (collapse (dpath n9)) ()))
!(Ans (size-atom (collapse (dpath n0))))
!(Ans (let $c (collapse (dpath n0)) (size-atom $c)))
"""


def main():
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    n_programs = 24
    for lang in ("he", "petta"):
        test_focused_edge_cases(cetta, lang)
        for profile in (None, "extended"):
            check_program(cetta, lang, f"structural-builtins/{profile}", STRUCTURAL_BUILTINS, profile)
            check_program(cetta, lang, f"effects-live-edits/{profile}", EFFECTS_AND_LIVE_EDITS, profile)
        check_program(cetta, lang, "wrappers/extended", WRAPPERS, "extended")
        rng = random.Random(20261124)
        for i in range(n_programs):
            defs, queries = generate_program(rng, i)
            check_batch(cetta, lang, f"gen-{i}", defs, queries)
    print(f"PASS: compiled route agrees with the interpreter (HE in order, PeTTa as multisets): "
          f"edge cases, structural builtins, effects and live edits, wrappers, {n_programs} generated programs, both dialects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
