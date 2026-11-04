#!/usr/bin/env python3
"""Churning a set space costs time linear in the churn: removing a row and
adding another, N times over N rows of 65 variables each, costs at most
sixteen times as much for 8N as for N (rebuilding the presence bits after
every removal costs about sixty-four times).

Usage: check_set_churn_scaling.py CETTA
"""
import os
import resource
import subprocess
import sys
import tempfile


def program(n):
    vs = " ".join(f"$v{i}" for i in range(65))
    ws = " ".join(f"$w{i}" for i in range(65))
    zs = " ".join(f"$z{i}" for i in range(65))
    return (
        "!(bind! &s (new-space set))\n"
        f"(= (fill $n) (if (== $n 0) done (let $u (add-atom &s (r $n {vs})) "
        "(fill (- $n 1)))))\n"
        f"!(fill {n})\n"
        f"(= (churn $n) (if (== $n 0) done (let $u (remove-atom &s (r $n {ws})) "
        f"(let $v (add-atom &s (r $n {zs})) (churn (- $n 1))))))\n"
        f"!(churn {n})\n"
        "!(size-atom (collapse (get-atoms &s)))\n")


def cpu(cetta, n):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(program(n))
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--profile", "extended", "--lang", "petta", path],
                                capture_output=True, text=True, timeout=600)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    answers = result.stdout.split()
    if result.returncode != 0 or answers != ["done", "done", str(n)]:
        print(f"FAIL: exit {result.returncode}, answers {answers[:6]} for "
              f"{n} rows; {result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def main():
    cetta = sys.argv[1]
    small = min(cpu(cetta, 400) for _ in range(3))
    large = min(cpu(cetta, 3200) for _ in range(2))
    ratio = large / max(small, 0.01)
    if ratio > 16.0:
        print(f"FAIL: set churn grows {ratio:.1f}x for 8x the rows "
              f"({small:.2f} s at 400, {large:.2f} s at 3200)")
        sys.exit(1)
    print(f"PASS: set churn grows {ratio:.1f}x for 8x the rows "
          f"({small:.2f} s at 400, {large:.2f} s at 3200)")


if __name__ == "__main__":
    main()
