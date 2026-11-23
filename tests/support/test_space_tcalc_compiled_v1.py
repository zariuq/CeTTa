#!/usr/bin/env python3
"""Permanent test: live MeTTa space definitions compiled to demand-driven tcalc
rules under per-head version counters, verified against CeTTa's interpreter in
both HE and PeTTa (ordinary and forced GC).
"""

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


def test_version_guard_exact(cetta: str, lang: str, extra_env=None) -> None:
    code = """
!(import! &self tcalc)
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

!(== (tcalc:space-eval &self (quote (pfib (S (S (S (S (S Z))))))))
     ((pfib (S (S (S (S (S Z))))))))
!(== (tcalc:space-eval &self (quote (seval (Add (Val (S (S Z))) (Add (Val (S Z)) (Val (S (S (S Z)))))))))
     ((seval (Add (Val (S (S Z))) (Add (Val (S Z)) (Val (S (S (S Z)))))))))
!(let $_ (add-atom &self (= (unrelated-fn $x) $x))
   (let $_ (add-atom &self (: unrelated-fn (-> Number Number)))
     (let $_ (add-atom &self (unrelated-fact 1 2))
       (tcalc:space-eval &self (quote (pfib (S (S (S (S (S (S Z))))))))))))
!(tcalc:space-eval &self (quote (pfib (S (S (S (S Z)))))))
!(tcalc:space-cache-stats &self pfib)
!(let $_ (add-atom &self (= (pfib Z) (S Z)))
   (tcalc:space-eval &self (quote (pfib Z))))
!(tcalc:space-cache-stats &self pfib)
!(let $prog (tcalc:space-program &self padd) (car-atom $prog))

(edge a b)
(edge b c)
(edge c a)
(edge c d)
(= (reach $x) (match &self (edge $x $y) $y))
(= (reach $x) (match &self (edge $x $y) (reach $y)))

!(tcalc:space-eval &self (quote (reach a)))
!(let $_ (add-atom &self (note keep))
   (tcalc:space-eval &self (quote (reach a))))
!(let $_ (add-atom &self (edge d e))
   (tcalc:space-eval &self (quote (reach a))))
!(tcalc:space-cache-stats &self reach)
"""
    out = run_cetta(cetta, lang, code, extra_env=extra_env)
    # PeTTa prints `true` for !(import! &self tcalc); strip that leading line.
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert len(out) == 12, f"{lang} unexpected line count ({len(out)}): {out}"
    assert out[0] in ("True", "true"), f"{lang} pfib mismatch vs interpreter: {out[0]}"
    assert out[1] in ("True", "true"), f"{lang} seval mismatch vs interpreter: {out[1]}"
    assert out[2] == "((S (S (S (S (S (S (S (S Z)))))))))", f"{lang} fib(6) wrong: {out[2]}"
    assert out[3] == "((S (S (S Z))))", f"{lang} fib(4) wrong: {out[3]}"
    assert "(compiles 1)" in out[4] and "(hits 2)" in out[4], f"{lang} pfib cache stats wrong: {out[4]}"
    assert "padd" in out[4] and "pfib" in out[4] and "S" in out[4], f"{lang} pfib deps missing: {out[4]}"
    assert "Z" in out[5] and "(S Z)" in out[5], f"{lang} pfib post-edit wrong: {out[5]}"
    assert "(compiles 2)" in out[6] and "(hits 2)" in out[6], f"{lang} pfib post-edit stats wrong: {out[6]}"
    assert out[7] == "(store set)", f"{lang} space-program header wrong: {out[7]}"
    for node in ("a", "b", "c", "d"):
        assert node in out[8], f"{lang} reach(a) missing {node}: {out[8]}"
    assert "e" not in out[8], f"{lang} reach(a) had e too early: {out[8]}"
    assert out[9] == out[8], f"{lang} reach(a) changed on unrelated note: {out[9]}"
    assert "e" in out[10], f"{lang} reach(a) missed new edge e: {out[10]}"
    # Data update on edge kept compiled rules intact: compiles == 1, hits == 2!
    assert "(compiles 1)" in out[11] and "(hits 2)" in out[11], f"{lang} reach recompiled on data edit: {out[11]}"


def test_invalidation_and_refusals(cetta: str, lang: str) -> None:
    """Wildcard rules, =e equations, constructor redefinition, non-strict type
    assignments, grounded ops, higher-order calls, and unbound vars are refused."""
    code = """
!(import! &self tcalc)
(= (box (Mk $x)) (Wrap $x))
(= (uses-arith $x) (+ $x 1))
(= (uses-ho $f $x) ($f $x))
(= (uses-unbound $x) (Pair $x $y))

; Valid first
!(tcalc:space-eval &self (quote (box (Mk a))))

; Refusals caught via case
!(case (tcalc:space-eval &self (quote (uses-arith 1))) (((Error $c $m) (refused-arith)) ($x $x)))
!(case (tcalc:space-eval &self (quote (uses-ho box a))) (((Error $c $m) (refused-ho)) ($x $x)))
!(case (tcalc:space-eval &self (quote (uses-unbound a))) (((Error $c $m) (refused-unbound)) ($x $x)))
!(case (tcalc:space-eval &self (quote (box $open))) (((Error $c $m) (refused-open)) ($x $x)))

; Adding a non-strict type assignment invalidates cached box and refuses
!(let $_ (add-atom &self (: box (-> Atom Atom)))
   (case (tcalc:space-eval &self (quote (box (Mk a)))) (((Error $c $m) (refused-nonstrict)) ($x $x))))
"""
    out = run_cetta(cetta, lang, code)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert out == [
        "((Wrap a))",
        "(refused-arith)",
        "(refused-ho)",
        "(refused-unbound)",
        "(refused-open)",
        "(refused-nonstrict)",
    ], f"{lang} refusals mismatch: {out}"

    # Adding =e equation invalidates cached program and refuses
    eq_code = """
!(import! &self tcalc)
(= (g A) B)
!(tcalc:space-eval &self (quote (g A)))
!(let $_ (add-atom &self (=e A C))
   (case (tcalc:space-eval &self (quote (g A))) (((Error $c $m) (refused-eq)) ($x $x))))
"""
    out = run_cetta(cetta, lang, eq_code)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert out == ["(B)", "(refused-eq)"], f"{lang} =e invalidation mismatch: {out}"

    # Adding wildcard function definition invalidates cached program and refuses
    wild_code = """
!(import! &self tcalc)
(= (h A) B)
!(tcalc:space-eval &self (quote (h A)))
!(let $_ (add-atom &self (= ($any A) Z))
   (case (tcalc:space-eval &self (quote (h A))) (((Error $c $m) (refused-wild)) ($x $x))))
"""
    out = run_cetta(cetta, lang, wild_code)
    if lang == "petta" and out and out[0] == "true":
        out = out[1:]
    assert out == ["(B)", "(refused-wild)"], f"{lang} wildcard invalidation mismatch: {out}"


def main() -> int:
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    for lang in ("he", "petta"):
        test_version_guard_exact(cetta, lang)
        test_version_guard_exact(
            cetta, lang, extra_env={"CETTA_GC": "1", "CETTA_GC_BUDGET_MB": "1"}
        )
        test_invalidation_and_refusals(cetta, lang)
    print(
        "PASS: test_space_tcalc_compiled_v1 (HE + PeTTa, ordinary & forced GC, "
        "differential agreement, per-head version-counter survival & invalidation, refusals)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
