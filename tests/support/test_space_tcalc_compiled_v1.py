#!/usr/bin/env python3
"""Permanent test: exact compiled tcalc evaluation of live MeTTa space
definitions under per-head version counters, verified against CeTTa's
interpreter in both HE and PeTTa (ordinary and forced GC).

Verifies:
- exact answer multiplicities (duplicate definitions, choice products,
  duplicate data atoms, unused bindings);
- direct answer streaming (no packed outer expression);
- undefined heads stay data and uncovered definitions run on the interpreter
  with zero Error refusals;
- presence-only set store used inside `(unique ...)` on duplicating DAGs and
  cyclic graphs;
- per-head version-counter survival across unrelated writes and invalidation
  on dependency edits, non-strict type assignments, =e equations, and wildcard
  definitions.
"""

import collections
import os
import subprocess
import sys
import tempfile


def run_cetta(cetta: str, lang: str, code: str, extra_env=None) -> list[str]:
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
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
            env=env,
        )
        if proc.returncode != 0:
            raise AssertionError(
                f"cetta --lang {lang} failed (rc={proc.returncode}):\n"
                f"STDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
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
    """Split a top-level MeTTa expression `(a b (c d))` into item strings."""
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


def test_exact_multiplicities_and_answers(
    cetta: str, lang: str, extra_env=None
) -> None:
    code = """
!(import! &self tcalc)
(= (dup) 1)
(= (dup) 1)
(= (choice) a)
(= (choice) b)
(= (choice) a)
(= (pair-dup) (P (dup) (dup)))
(= (drop-choice) (let $x (choice) ok))

(item x)
(item x)
(item y)
(= (items) (match &self (item $v) $v))
(= (items-const) (match &self (item $v) hit))

(= (padd Z $y) $y)
(= (padd (S $x) $y) (S (padd $x $y)))
(= (pfib Z) Z)
(= (pfib (S Z)) (S Z))
(= (pfib (S (S $n))) (padd (pfib (S $n)) (pfib $n)))

(= (seval (Val $n)) $n)
(= (seval (Add $a $b))
   (let $va (seval $a)
     (let $vb (seval $b)
       (padd $va $vb))))

; Direct answers streamed into outer constructors and collapse (no outer packing)
!(collapse (tcalc:space-eval &self (quote (dup))))
!(collapse (dup))
!(collapse (tcalc:space-eval &self (quote (pair-dup))))
!(collapse (pair-dup))
!(collapse (tcalc:space-eval &self (quote (drop-choice))))
!(collapse (drop-choice))
!(collapse (tcalc:space-eval &self (quote (items))))
!(collapse (items))
!(collapse (tcalc:space-eval &self (quote (items-const))))
!(collapse (items-const))
!(collapse (Wrap (tcalc:space-eval &self (quote (dup)))))
!(collapse (Wrap (dup)))

; Exact agreement on Peano Fibonacci and expression evaluator + version-counter survival
!(== (tcalc:space-eval &self (quote (pfib (S (S (S (S (S Z))))))))
     (pfib (S (S (S (S (S Z)))))))
!(== (tcalc:space-eval &self (quote (seval (Add (Val (S (S Z))) (Add (Val (S Z)) (Val (S (S (S Z)))))))))
     (seval (Add (Val (S (S Z))) (Add (Val (S Z)) (Val (S (S (S Z))))))))
!(let $_ (add-atom &self (= (unrelated-fn $x) $x))
   (let $_ (add-atom &self (: unrelated-fn (-> Number Number)))
     (let $_ (add-atom &self (unrelated-fact 1 2))
       (tcalc:space-eval &self (quote (pfib (S (S (S (S (S (S Z))))))))))))
!(tcalc:space-eval &self (quote (pfib (S (S (S (S Z)))))))
!(pfib (S (S (S (S Z)))))
!(tcalc:space-cache-stats &self pfib)
!(let $_ (add-atom &self (= (pfib Z) (S Z)))
   (collapse (tcalc:space-eval &self (quote (pfib Z)))))
!(tcalc:space-cache-stats &self pfib)
!(let $prog (tcalc:space-program &self padd) (car-atom $prog))

; Presence-only set store inside `(unique ...)` handles cyclic graphs and deduplication
(edge a b)
(edge b c)
(edge c a)
(edge c d)
(= (reach $x) (match &self (edge $x $y) $y))
(= (reach $x) (match &self (edge $x $y) (reach $y)))

!(collapse (unique (reach a)))
!(let $_ (add-atom &self (note keep))
   (collapse (unique (reach a))))
!(let $_ (add-atom &self (edge d e))
   (collapse (unique (reach a))))
!(tcalc:space-cache-stats &self reach)
"""
    out = run_cetta(cetta, lang, code, extra_env=extra_env)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert len(out) == 25, f"{lang} unexpected line count ({len(out)}): {out}"

    # Pairs 0..11 compare compiled vs interpreter as exact multisets
    expected_multisets = [
        ("dup", ["1", "1"]),
        ("pair-dup", ["(P 1 1)"] * 4),
        ("drop-choice", ["ok", "ok", "ok"]),
        ("items", ["x", "x", "y"]),
        ("items-const", ["hit", "hit", "hit"]),
        ("wrap-dup", ["(Wrap 1)", "(Wrap 1)"]),
    ]
    for idx, (label, want) in enumerate(expected_multisets):
        got_tcalc = collections.Counter(parse_sexp_items(out[2 * idx]))
        got_interp = collections.Counter(parse_sexp_items(out[2 * idx + 1]))
        assert got_tcalc == got_interp == collections.Counter(want), (
            f"{lang} {label} multiset mismatch: tcalc={out[2*idx]} "
            f"interp={out[2*idx+1]} want={want}"
        )

    assert out[12] in ("True", "true"), f"{lang} pfib mismatch: {out[12]}"
    assert out[13] in ("True", "true"), f"{lang} seval mismatch: {out[13]}"
    assert out[14] == "(S (S (S (S (S (S (S (S Z))))))))", f"{lang} fib(6) wrong: {out[14]}"
    assert out[15] == "(S (S (S Z)))", f"{lang} fib(4) wrong: {out[15]}"
    assert out[16] == "(S (S (S Z)))", f"{lang} direct fib(4) wrong: {out[16]}"
    # Direct `(pfib ...)` call ALSO routes through the cached compiled entry
    # automatically, surviving all 3 unrelated writes without recompiling!
    assert "(compiles 1)" in out[17] and "(hits " in out[17], (
        f"{lang} pfib cache stats wrong: {out[17]}"
    )
    hits_before = int(out[17].split("(hits ")[1].split(")")[0])
    assert hits_before >= 3, f"{lang} pfib hits too low ({hits_before}): {out[17]}"
    assert "padd" in out[17] and "pfib" in out[17] and "S" in out[17], f"{lang} pfib deps missing: {out[17]}"
    assert collections.Counter(parse_sexp_items(out[18])) == collections.Counter(["Z", "(S Z)"]), (
        f"{lang} pfib post-edit wrong: {out[18]}"
    )
    assert "(compiles 2)" in out[19] and f"(hits {hits_before})" in out[19], (
        f"{lang} pfib post-edit stats wrong: {out[19]}"
    )
    assert out[20] == "(store multiset)", f"{lang} space-program store header wrong: {out[20]}"

    reach1 = set(parse_sexp_items(out[21]))
    reach2 = set(parse_sexp_items(out[22]))
    reach3 = set(parse_sexp_items(out[23]))
    assert reach1 == {"a", "b", "c", "d"}, f"{lang} unique reach(a) wrong: {out[21]}"
    assert reach2 == reach1, f"{lang} unique reach(a) changed on unrelated note: {out[22]}"
    assert reach3 == {"a", "b", "c", "d", "e"}, f"{lang} unique reach(a) missed e: {out[23]}"
    # Data update on edge kept compiled rules intact: compiles == 1, hits == 2!
    assert "(compiles 1)" in out[24] and "(hits 2)" in out[24], (
        f"{lang} reach recompiled on data edit: {out[24]}"
    )


def test_undefined_and_uncovered_fallback(cetta: str, lang: str) -> None:
    """Undefined heads stay data and uncovered definitions run seamlessly on
    CeTTa's interpreter with zero Error refusals, while `tcalc:space-program`
    reports `(tcalc-not-compiled HEAD REASON)` as data."""
    code = """
!(import! &self tcalc)
(= (box (Mk $x)) (Wrap $x))
(= (uses-arith $x) (+ $x 1))
(= (uses-ho $f $x) ($f $x))
(= (uses-effect $x) (let $s (new-space) $x))
(= (partial A) Hit)

; Undefined head stays data (no Error)
!(tcalc:space-eval &self (quote (nope 1)))
!(nope 1)

; Covered call
!(tcalc:space-eval &self (quote (box (Mk a))))

; Partial function: HE leaves unmatched call as data, PeTTa yields empty
!(collapse (tcalc:space-eval &self (quote (partial B))))
!(collapse (partial B))

; Compiled arithmetic & higher-order calls, plus uncovered effectful fallback (no Error)
!(tcalc:space-eval &self (quote (uses-arith 41)))
!(tcalc:space-eval &self (quote (uses-ho box (Mk a))))
!(tcalc:space-eval &self (quote (uses-effect 7)))

; Inspection reports compiled heads as `(store multiset)` and uncovered heads as data (not Error)
!(let $p (tcalc:space-program &self nope) (car-atom $p))
!(let $p (tcalc:space-program &self uses-arith) (car-atom $p))
!(let $p (tcalc:space-program &self uses-ho) (car-atom $p))
!(let $p (tcalc:space-program &self uses-effect) (car-atom $p))

; Non-strict type assignment invalidates compiled box and falls back cleanly
!(let $_ (add-atom &self (: box (-> Atom Atom)))
   (tcalc:space-eval &self (quote (box (Mk a)))))
!(let $p (tcalc:space-program &self box) (car-atom $p))
"""
    out = run_cetta(cetta, lang, code)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert out == [
        "(nope 1)",
        "(nope 1)",
        "(Wrap a)",
        "((partial B))" if lang == "he" else "()",
        "((partial B))" if lang == "he" else "()",
        "42",
        "(Wrap a)",
        "7",
        "tcalc-not-compiled",
        "(store multiset)",
        "(store multiset)",
        "tcalc-not-compiled",
        "(Wrap a)",
        "tcalc-not-compiled",
    ], f"{lang} fallback mismatch: {out}"

    # Adding =e equation invalidates cached program and falls back seamlessly
    eq_code = """
!(import! &self tcalc)
(= (g A) B)
!(tcalc:space-eval &self (quote (g A)))
!(let $_ (add-atom &self (=e A C))
   (tcalc:space-eval &self (quote (g A))))
!(let $p (tcalc:space-program &self g) (car-atom $p))
"""
    out = run_cetta(cetta, lang, eq_code)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert out == ["B", "B", "tcalc-not-compiled"], f"{lang} =e fallback mismatch: {out}"

    # Adding wildcard function definition invalidates cached program and falls back seamlessly
    wild_code = """
!(import! &self tcalc)
(= (h A) B)
!(tcalc:space-eval &self (quote (h A)))
!(let $_ (add-atom &self (= ($any A) Z))
   (collapse (tcalc:space-eval &self (quote (h A)))))
!(collapse (h A))
!(let $p (tcalc:space-program &self h) (car-atom $p))
"""
    out = run_cetta(cetta, lang, wild_code)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert out[0] == "B", f"{lang} pre-wildcard mismatch: {out}"
    assert collections.Counter(parse_sexp_items(out[1])) == collections.Counter(parse_sexp_items(out[2])), (
        f"{lang} post-wildcard fallback mismatch vs interpreter: {out}"
    )
    assert out[3] == "tcalc-not-compiled", f"{lang} post-wildcard program inspection mismatch: {out}"


def main() -> int:
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    for lang in ("he", "petta"):
        test_exact_multiplicities_and_answers(cetta, lang)
        test_exact_multiplicities_and_answers(
            cetta, lang, extra_env={"CETTA_GC": "1", "CETTA_GC_BUDGET_MB": "1"}
        )
        test_undefined_and_uncovered_fallback(cetta, lang)
    print(
        "PASS: test_space_tcalc_compiled_v1 (HE + PeTTa, ordinary & forced GC, "
        "exact multiplicities, direct answers, interpreter fallback, unique set-store, "
        "per-head version-counter survival & invalidation)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
