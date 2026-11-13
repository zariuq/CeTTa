#!/usr/bin/env python3
"""Churning a set space costs work linear in the churn: removing a row and
adding another, N times over N rows of 65 variables each, costs at most ten
times as much for 8N as for N.  Rebuilding the presence bits after every
removal costs about sixty-four times; so did the compiled tier when it
looked up the cells host goals took by scanning every one taken before
(12.7 times).  Work is counted in user-space instructions (perf stat),
start-up taken off, so the machine's load does not decide the verdict.

Usage: check_set_churn_scaling.py CETTA
"""
import os
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


def instructions(cetta, text, expected):
    """The user-space instructions CETTA runs TEXT in, its answers checked."""
    with tempfile.TemporaryDirectory(prefix="set-churn-") as directory:
        path = os.path.join(directory, "churn.metta")
        counts = path + ".perf"
        with open(path, "w") as handle:
            handle.write(text)
        result = subprocess.run(
            ["perf", "stat", "-x,", "-e", "instructions:u", "-o", counts, "--",
             cetta, "--profile", "extended", "--lang", "petta", path],
            capture_output=True, text=True, timeout=600)
        answers = result.stdout.split()
        if result.returncode != 0 or answers != expected:
            print(f"FAIL: exit {result.returncode}, answers {answers[:6]}, "
                  f"expected {expected}; {result.stderr[-200:]}")
            sys.exit(1)
        with open(counts) as handle:
            for line in handle:
                fields = line.split(",")
                if len(fields) > 2 and fields[2].startswith("instructions"):
                    return int(fields[0])
    print("FAIL: perf gave no instruction count")
    sys.exit(1)


def main():
    cetta = sys.argv[1]
    startup = instructions(cetta, "!(+ 1 1)\n", ["2"])
    small = instructions(cetta, program(400), ["done", "done", "400"]) - startup
    large = instructions(cetta, program(3200), ["done", "done", "3200"]) - startup
    ratio = large / max(small, 1)
    if ratio > 10.0:
        print(f"FAIL: set churn grows {ratio:.1f}x for 8x the rows "
              f"({small / 1e6:.1f}M instructions at 400, {large / 1e6:.1f}M at 3200)")
        sys.exit(1)
    print(f"PASS: set churn grows {ratio:.1f}x for 8x the rows "
          f"({small / 1e6:.1f}M instructions at 400, {large / 1e6:.1f}M at 3200)")


if __name__ == "__main__":
    main()
