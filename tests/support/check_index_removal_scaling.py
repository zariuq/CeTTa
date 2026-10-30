#!/usr/bin/env python3
"""Removing rows under a column index costs each removal a constant.

A space holds rows (p i k z) with k = i mod 2.  Lookups of (p $x k z) build
the column index of k, whose two postings each hold half the rows.  The
rows are then removed in ascending order, which takes the first position of
a long posting each time: half of them, a lookup that must not meet the
removed rows, then the rest.  The answers must be exact, and 8N rows must
cost at most sixteen times N rows (linear work gives about eight, a posting
shifted at each removal about sixty-four).

Usage: check_index_removal_scaling.py CETTA
"""
import os
import resource
import subprocess
import sys
import tempfile

PROGRAM = """!(bind! &s (new-space))
(= (fill $i $n) (if (>= $i $n) done
  (let $k (% $i 2) (let $_ (add-atom &s (p $i $k z)) (fill (+ $i 1) $n)))))
!(fill 0 {n})
(= (look $k $j) (if (== $j 0) done
  (let $c (size-atom (collapse (match &s (p $x $k z) $x))) (look $k (- $j 1)))))
!(look 0 8)
!(look 1 8)
(= (step $i $n) (if (>= $i $n) done
  (let $k (% $i 2) (let $u (remove-atom &s (p $i $k z)) (step (+ $i 1) $n)))))
!(step 0 {half})
!(size-atom (collapse (match &s (p $x 1 z) $x)))
!(step {half} {n})
!(size-atom (collapse (match &s (p $x 1 z) $x)))
"""


def run(cetta, n):
    half = n // 2
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(PROGRAM.format(n=n, half=half))
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=300)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    lines = result.stdout.split()
    expected = ["done", "done", "done", "done", str(n // 4), "done", "0"]
    if result.returncode != 0 or lines != expected:
        print(f"FAIL: {n} rows: exit {result.returncode}, answers {lines[:8]} "
              f"against {expected}; {result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def main():
    cetta = sys.argv[1]
    small = min(run(cetta, 32000) for _ in range(2))
    large = min(run(cetta, 256000) for _ in range(2))
    ratio = large / max(small, 0.01)
    if ratio > 16.0:
        print(f"FAIL: 256,000 rows removed in {large:.2f} s against {small:.2f} s "
              f"for 32,000 ({ratio:.1f}x)")
        sys.exit(1)
    print(f"PASS: rows leave a column index in constant time: 256,000 removals "
          f"{large:.2f} s, 32,000 {small:.2f} s ({ratio:.1f}x)")


if __name__ == "__main__":
    main()
