#!/usr/bin/env python3
"""Differential test comparing exact compiled tcalc evaluation against CeTTa's
interpreter in both HE and PeTTa across generated programs, compared as exact
multisets.

Covers:
- duplicate function definitions and nested choice products;
- partial functions (unmatched calls at root and inside nested calls);
- undefined head symbols (stay data);
- builtin operations (+, *, -, if) falling back seamlessly;
- recursion (Peano arithmetic, lists, binary trees, diamond DAGs);
- data reads (`match &self ...`) with duplicate data atoms and multi-joins;
- `let` destructuring and unused bindings;
- grounded integers, strings, and dialect Booleans;
- presence-only `(unique ...)` over duplicating DAGs.
"""

import collections
import os
import random
import subprocess
import sys
import tempfile


def run_cetta(cetta: str, lang: str, code: str) -> list[str]:
    with tempfile.NamedTemporaryFile(
        "w", suffix=".metta", delete=False, encoding="utf-8"
    ) as tf:
        tf.write(code)
        path = tf.name
    try:
        proc = subprocess.run(
            [cetta, "--lang", lang, "--quiet", path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if proc.returncode != 0:
            raise AssertionError(
                f"cetta --lang {lang} failed (rc={proc.returncode}):\n"
                f"CODE:\n{code}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
            )
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


def parse_sexp_items(text: str) -> list[str]:
    s = text.strip()
    assert s.startswith("(") and s.endswith(")"), f"expected expression, got {s!r}"
    inner = s[1:-1].strip()
    if not inner:
        return []
    items = []
    depth = 0
    in_str = False
    esc = False
    start = None
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
                items.append(inner[start : i + 1])
                start = None
        elif ch.isspace():
            if depth == 0 and start is not None:
                items.append(inner[start:i])
                start = None
        else:
            if start is None:
                start = i
    if start is not None:
        items.append(inner[start:])
    return items


def check_batch(
    cetta: str, lang: str, label: str, defs: str, queries: list[tuple[str, bool]]
) -> None:
    """Run every query in QUERIES via both direct interpreter and compiled route.
    Each item is (expr, is_unique_query).
    """
    lines = ["!(import! &self tcalc)", defs]
    for expr, is_unique in queries:
        if is_unique:
            # Direct `(unique EXPR)` routes through the presence-only set store;
            # compare against explicit `(let $c (collapse EXPR) (unique-atom $c))`.
            lines.append(f"!(Ans (collapse (unique {expr})))")
            lines.append(f"!(Ans (let $c (collapse {expr}) (unique-atom $c)))")
        else:
            lines.append(f"!(Ans (collapse (tcalc:space-eval &self (quote {expr}))))")
            lines.append(f"!(Ans (collapse {expr}))")
    out = run_cetta(cetta, lang, "\n".join(lines) + "\n")
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert len(out) == 2 * len(queries), (
        f"{lang} {label}: expected {2 * len(queries)} lines, got {len(out)}:\n{out}"
    )
    for idx, (expr, is_unique) in enumerate(queries):
        got_wrap = parse_sexp_items(out[2 * idx])
        want_wrap = parse_sexp_items(out[2 * idx + 1])
        assert len(got_wrap) == 2 and got_wrap[0] == "Ans", f"unexpected wrap: {out[2*idx]}"
        assert len(want_wrap) == 2 and want_wrap[0] == "Ans", f"unexpected wrap: {out[2*idx+1]}"
        got = collections.Counter(parse_sexp_items(got_wrap[1]))
        want = collections.Counter(parse_sexp_items(want_wrap[1]))
        if got != want:
            raise AssertionError(
                f"{lang} {label} query {expr!r} (unique={is_unique}) mismatch:\n"
                f"  compiled: {out[2*idx]}\n"
                f"  interp:   {out[2*idx+1]}\n"
                f"  defs:\n{defs}"
            )


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


def test_interleaved_data_writes(cetta: str, lang: str) -> None:
    """Verify exact multiset agreement AND zero rule recompilation when data
    reads are interleaved with add-atom / remove-atom on both queried and
    unrelated data heads."""
    script = """
!(import! &self tcalc)
(edge a b)
(edge b c)
(edge b d)
(edge a c)
(tag b ok)
(tag c ok)
(other z 1)

(= (reach2 $u)
   (match &self (, (edge $u $mid) (edge $mid $v) (tag $mid ok)) ($u $v)))
(= (out-of $u)
   (match &self (edge $u $v) $v))

; Step 1: initial reads
!(Ans (collapse (tcalc:space-eval &self (quote (reach2 a)))))
!(Ans (collapse (reach2 a)))
!(Ans (collapse (tcalc:space-eval &self (quote (out-of a)))))
!(Ans (collapse (out-of a)))

; Step 2: write to unrelated head `other` -> zero recompile, answers unchanged
!(let $_ (add-atom &self (other z 2))
   (Ans (collapse (tcalc:space-eval &self (quote (reach2 a))))))
!(Ans (collapse (reach2 a)))

; Step 3: add duplicate edge + new edge -> data index refreshes without recompiling rules
!(let $_ (add-atom &self (edge b d))
   (let $_2 (add-atom &self (edge c e))
     (Ans (collapse (tcalc:space-eval &self (quote (reach2 a)))))))
!(Ans (collapse (reach2 a)))

; Step 4: remove an edge and a tag -> index reflects removal immediately
!(let $_ (remove-atom &self (edge b c))
   (let $_2 (remove-atom &self (tag c ok))
     (Ans (collapse (tcalc:space-eval &self (quote (reach2 a)))))))
!(Ans (collapse (reach2 a)))

; Step 5: add back tag + new branch -> verify exact multiplicity again
!(let $_ (add-atom &self (tag c ok))
   (let $_2 (add-atom &self (edge c f))
     (Ans (collapse (tcalc:space-eval &self (quote (reach2 a)))))))
!(Ans (collapse (reach2 a)))

; Verify rules were compiled ONCE (data writes refreshed indexes without recompiling rules)
!(tcalc:space-cache-stats &self reach2)
"""
    out = run_cetta(cetta, lang, script)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    # 6 pairs of (compiled, interp) + 1 cache stats line = 13 lines
    assert len(out) == 13, f"{lang} interleaved-writes expected 13 lines, got {len(out)}: {out}"
    for idx in range(6):
        got_wrap = parse_sexp_items(out[2 * idx])
        want_wrap = parse_sexp_items(out[2 * idx + 1])
        assert len(got_wrap) == 2 and got_wrap[0] == "Ans", f"unexpected wrap: {out[2*idx]}"
        assert len(want_wrap) == 2 and want_wrap[0] == "Ans", f"unexpected wrap: {out[2*idx+1]}"
        got = collections.Counter(parse_sexp_items(got_wrap[1]))
        want = collections.Counter(parse_sexp_items(want_wrap[1]))
        if got != want:
            raise AssertionError(
                f"{lang} interleaved-writes step {idx} mismatch:\n"
                f"  compiled: {out[2*idx]}\n"
                f"  interp:   {out[2*idx+1]}"
            )
    stats = out[-1]
    assert "(compiles 1)" in stats, (
        f"{lang} interleaved data writes triggered rule recompilation: {stats}"
    )


def main() -> int:
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    n_programs = 24
    for lang in ("he", "petta"):
        test_focused_edge_cases(cetta, lang)
        test_interleaved_data_writes(cetta, lang)
        rng = random.Random(20261124)
        for i in range(n_programs):
            defs, queries = generate_program(rng, i)
            check_batch(cetta, lang, f"gen-{i}", defs, queries)
    print(
        f"PASS: test_space_tcalc_differential_v1 (HE + PeTTa, edge cases + "
        f"interleaved data writes + {n_programs} generated programs x 15 queries each, exact multiset match)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
