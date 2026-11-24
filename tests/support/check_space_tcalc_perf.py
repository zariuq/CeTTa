#!/usr/bin/env python3
"""Standalone timing benchmark comparing live-space definitions compiled to
demand-driven tcalc (guarded by per-head version counters) against CeTTa's
interpreter in both HE and PeTTa.

Kept out of test-all so CI stays deterministic on wall-clock timing.
"""

import statistics
import subprocess
import sys
import tempfile
import time


def peano(n: int) -> str:
    s = "Z"
    for _ in range(n):
        s = f"(S {s})"
    return s


def bin_add_fib_defs() -> str:
    """Binary natural numbers (Zero, B0 x, B1 x) + Fibonacci.
    Avoids exponential term size so both interpreter and tcalc compute exact
    binary values while Fibonacci's overlapping subcalls exercise demand-driven
    sharing in tcalc vs tree recursion in the interpreter."""
    return """
(= (inc Zero) (B1 Zero))
(= (inc (B0 $x)) (B1 $x))
(= (inc (B1 $x)) (B0 (inc $x)))

(= (badd Zero $y) $y)
(= (badd (B0 $x) Zero) (B0 $x))
(= (badd (B1 $x) Zero) (B1 $x))
(= (badd (B0 $x) (B0 $y)) (B0 (badd $x $y)))
(= (badd (B0 $x) (B1 $y)) (B1 (badd $x $y)))
(= (badd (B1 $x) (B0 $y)) (B1 (badd $x $y)))
(= (badd (B1 $x) (B1 $y)) (B0 (inc (badd $x $y))))

(= (bfib Z) Zero)
(= (bfib (S Z)) (B1 Zero))
(= (bfib (S (S $n))) (badd (bfib (S $n)) (bfib $n)))
"""


def measure(cetta: str, lang: str, code: str, trials: int = 5) -> tuple[float, str]:
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as tf:
        tf.write(code)
        path = tf.name
    times = []
    last_out = ""
    try:
        for _ in range(trials):
            t0 = time.perf_counter()
            proc = subprocess.run(
                [cetta, "--lang", lang, "--quiet", path],
                capture_output=True,
                text=True,
                timeout=30,
            )
            dt = (time.perf_counter() - t0) * 1000.0
            if proc.returncode != 0:
                raise RuntimeError(
                    f"cetta --lang {lang} failed:\n{proc.stdout}\n{proc.stderr}"
                )
            times.append(dt)
            last_out = proc.stdout.strip()
    finally:
        import os
        try:
            os.unlink(path)
        except OSError:
            pass
    return statistics.median(times), last_out


def main() -> int:
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    defs = bin_add_fib_defs()
    ns = [14, 18, 22, 26]

    print(f"{'dialect':<8} {'N':>3} {'interp_ms':>10} {'tcalc_ms':>10} {'speedup':>9} {'warm_10x_ms':>12} {'agree':>6}")
    print("-" * 66)

    for lang in ("he", "petta"):
        for n in ns:
            arg = peano(n)
            interp_code = defs + f"\n!(bfib {arg})\n"
            tcalc_code = "!(import! &self tcalc)\n" + defs + f"\n!(tcalc:space-eval &self (quote (bfib {arg})))\n"
            # Warm reuse across 10 calls interleaved with unrelated definition edits
            warm_calls = []
            for k in range(10):
                warm_calls.append(f"!(add-atom &self (= (unrelated-{k} $x) $x))")
                warm_calls.append(f"!(tcalc:space-eval &self (quote (bfib {arg})))")
            warm_calls.append("!(tcalc:space-cache-stats &self bfib)")
            warm_code = "!(import! &self tcalc)\n" + defs + "\n" + "\n".join(warm_calls) + "\n"

            t_interp, out_interp = measure(cetta, lang, interp_code, trials=3)
            t_tcalc, out_tcalc = measure(cetta, lang, tcalc_code, trials=5)
            t_warm, out_warm = measure(cetta, lang, warm_code, trials=3)

            def norm(s: str) -> str:
                s = s.strip()
                if lang == "he" and s.startswith("[") and s.endswith("]"):
                    s = s[1:-1].strip()
                return s

            tcalc_lines = [norm(l) for l in out_tcalc.splitlines() if l.strip()]
            interp_lines = [norm(l) for l in out_interp.splitlines() if l.strip()]
            agree = tcalc_lines[-1] == interp_lines[-1]
            warm_lines = [norm(l) for l in out_warm.splitlines() if l.strip()]
            stats_line = warm_lines[-1]
            if not agree:
                raise AssertionError(
                    f"disagreement at {lang} N={n}: interp={out_interp!r} tcalc={out_tcalc!r}"
                )
            if "(compiles 1)" not in stats_line or "(hits 9)" not in stats_line:
                raise AssertionError(
                    f"expected 1 compile & 9 hits across 10 warm calls with unrelated edits, got: {stats_line}"
                )
            speedup = t_interp / t_tcalc if t_tcalc > 0 else 0.0
            print(
                f"{lang:<8} {n:>3} {t_interp:>10.2f} {t_tcalc:>10.2f} {speedup:>8.1f}x {t_warm:>12.2f} {'OK':>6}"
            )

    # Measure presence-only set store under `(unique ...)` on a diamond DAG with
    # 2^14 = 16,384 duplicate paths versus explicit multiset collapse + unique-atom.
    print("\nPresence-only set store under `(unique ...)` (14-layer diamond DAG, 2^14 paths):")
    print(f"{'dialect':<8} {'interp_ms':>10} {'unique_ms':>10} {'speedup':>9} {'agree':>6}")
    print("-" * 47)
    dag_defs = """
(= (dag-dup Z) Leaf)
(= (dag-dup (S $n)) (dag-dup $n))
(= (dag-dup (S $n)) (dag-dup $n))
"""
    d_arg = peano(14)
    for lang in ("he", "petta"):
        interp_dag = dag_defs + f"\n!(let $c (collapse (dag-dup {d_arg})) (unique-atom $c))\n"
        unique_dag = dag_defs + f"\n!(collapse (unique (dag-dup {d_arg})))\n"
        t_interp, out_i = measure(cetta, lang, interp_dag, trials=3)
        t_uniq, out_u = measure(cetta, lang, unique_dag, trials=5)

        def norm_u(s: str) -> str:
            s = s.strip()
            if lang == "he" and s.startswith("[") and s.endswith("]"):
                s = s[1:-1].strip()
            return s

        oi = [norm_u(l) for l in out_i.splitlines() if l.strip()][-1]
        ou = [norm_u(l) for l in out_u.splitlines() if l.strip()][-1]
        if oi != "(Leaf)" or ou != "(Leaf)":
            raise AssertionError(f"unique DAG mismatch ({lang}): interp={oi!r} unique={ou!r}")
        speedup = t_interp / t_uniq if t_uniq > 0 else 0.0
        print(f"{lang:<8} {t_interp:>10.2f} {t_uniq:>10.2f} {speedup:>8.1f}x {'OK':>6}")
    print("PASS: check_space_tcalc_perf")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
