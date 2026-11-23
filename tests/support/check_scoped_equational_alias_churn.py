#!/usr/bin/env python3
"""A write to a space with `=e` aliases costs eq:match nothing extra.

The control: N rows (fact i (item i)), a row (Probe Cold) and (=e Cold Warm);
then K steps, each adding an unrelated row and asking a ground eq:match for
(Probe Warm), and the same with a plain match as the reference.  The cost of a
step is measured as the time of K steps less the time of none, at two sizes.
eq:match must stay within a fixed factor of plain match, and its cost per step
must not grow with N.

Run: check_scoped_equational_alias_churn.py ./cetta [--report]
"""
import subprocess
import sys
import time


def program(n, k, mode):
    q = "(eq:match &self (Probe Warm) hit)" if mode != "match" else "(match &self (Probe Cold) hit)"
    lines = ["(=e Cold Warm)", "(Probe Cold)"]
    lines += [f"(fact {i} (item {i}))" for i in range(n)]
    if mode == "eq_edit":
        lines.append(
            f"(= (churn $k) (if (== $k 0) done (let $a (add-atom &self (=e Cold Warm)) "
            f"(let $r (collapse {q}) (churn (- $k 1))))))")
    else:
        lines.append(
            f"(= (churn $k) (if (== $k 0) done (let $a (add-atom &self (noise $k)) "
            f"(let $r (collapse {q}) (churn (- $k 1))))))")
    lines.append(f"!(churn {k})")
    lines.append(f"!(collapse {q})")
    return "\n".join(lines) + "\n"


def run(cetta, dialect, n, k, mode, path):
    with open(path, "w") as f:
        f.write(program(n, k, mode))
    best = None
    for _ in range(3):
        t0 = time.perf_counter()
        proc = subprocess.run([cetta, "--lang", dialect, "--profile", "extended", path],
                              capture_output=True, text=True, timeout=600)
        dt = time.perf_counter() - t0
        if proc.returncode != 0 or "hit" not in proc.stdout:
            raise AssertionError(f"{dialect} {mode} N={n} K={k} failed:\n{proc.stdout}\n{proc.stderr}")
        best = dt if best is None else min(best, dt)
    return best


def main():
    cetta = sys.argv[1] if len(sys.argv) > 1 else "./cetta"
    report = "--report" in sys.argv
    import tempfile, os
    tmp = tempfile.mkdtemp(prefix="eq-alias-churn-")
    path = os.path.join(tmp, "churn.metta")
    k = 2000
    k_edit = 400
    failures = []
    try:
        for dialect in ("petta", "he"):
            per = {}
            for n in (4000, 64000):
                for mode in ("match", "eq", "eq_edit"):
                    steps = k_edit if mode == "eq_edit" else k
                    base = run(cetta, dialect, n, 0, mode, path)
                    full = run(cetta, dialect, n, steps, mode, path)
                    per[(n, mode)] = max(full - base, 0.0) / steps
            for n in (4000, 64000):
                line = (f"{dialect} N={n}: match {per[(n, 'match')] * 1e6:8.1f} us/step, "
                        f"eq:match {per[(n, 'eq')] * 1e6:8.1f} us/step, "
                        f"=e edit {per[(n, 'eq_edit')] * 1e6:8.1f} us/step")
                if report:
                    print(line)
            # eq:match within a fixed factor of plain match, plus a small constant.
            for n in (4000, 64000):
                if per[(n, "eq")] > 5.0 * per[(n, "match")] + 50e-6:
                    failures.append(f"{dialect} N={n}: eq:match {per[(n, 'eq')] * 1e6:.1f} us/step "
                                    f"against match {per[(n, 'match')] * 1e6:.1f} us/step")
            # No growth with N: sixteen times the rows, at most three times the step.
            if per[(64000, "eq")] > 3.0 * per[(4000, "eq")] + 50e-6:
                failures.append(f"{dialect}: eq:match per step grows with N: "
                                f"{per[(4000, 'eq')] * 1e6:.1f} -> {per[(64000, 'eq')] * 1e6:.1f} us")
            if per[(64000, "eq_edit")] > 3.0 * per[(4000, "eq_edit")] + 50e-6:
                failures.append(f"{dialect}: =e edit per step grows with N: "
                                f"{per[(4000, 'eq_edit')] * 1e6:.1f} -> {per[(64000, 'eq_edit')] * 1e6:.1f} us")
    finally:
        try:
            os.remove(path)
            os.rmdir(tmp)
        except OSError:
            pass
    if failures:
        raise AssertionError("\n".join(failures))
    print("PASS: scoped equational alias churn: eq:match and =e edits flat in N and within "
          "a fixed factor of plain match")


if __name__ == "__main__":
    main()
