#!/usr/bin/env python3
"""Removing an atom a space does not hold costs the same in a space eight
times larger.

A space holds N rows (r i), and a second space the rest of 512,000 rows, so
that both timed runs hold the same live data.  128,000 removals of
(r i absent), which no row holds and none unifies with, must cost about the
same for N = 512,000 as for N = 64,000: at most twice (a scan of the space
per removal costs about eight times).  Their cost is a run's less the same
run without them.  A separate small run checks the answers: the removals
leave the space as it was, a row (r $y absent) added after them is still
removed by (remove-atom &s (r 7 absent)), which unifies with it, and (r 5)
still goes.

Usage: check_remove_absent_scaling.py CETTA
"""
import os
import resource
import subprocess
import sys
import tempfile

SETUP = """!(bind! &s (new-space))
!(bind! &pad (new-space))
(= (fill $i $n) (if (>= $i $n) done (let $_ (add-atom &s (r $i)) (fill (+ $i 1) $n))))
(= (pad $i $n) (if (>= $i $n) done (let $_ (add-atom &pad (p $i)) (pad (+ $i 1) $n))))
(= (drop $i $n) (if (>= $i $n) done
  (let $_ (remove-atom &s (r $i absent)) (drop (+ $i 1) $n))))
!(fill 0 {n})
!(pad 0 {padding})
"""

DROP = """!(drop 0 {m})
"""

PROBE = """!(match &s (r 5) present)
"""

ANSWERS = """!(size-atom (collapse (get-atoms &s)))
!(add-atom &s (r $y absent))
!(size-atom (collapse (get-atoms &s)))
!(remove-atom &s (r 7 absent))
!(size-atom (collapse (get-atoms &s)))
!(remove-atom &s (r 5))
!(size-atom (collapse (get-atoms &s)))
"""

REMOVALS = 128000
# Rows held in all, the target space and a second one together: both runs
# hold the same live data, so only the size of the space removed from differs.
LIVE = 512000


def run(cetta, program, expected, timed=True):
    with tempfile.NamedTemporaryFile("w", suffix=".metta", delete=False) as f:
        f.write(program)
        path = f.name
    try:
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = subprocess.run([cetta, "--lang", "petta", path],
                                capture_output=True, text=True, timeout=600)
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
    finally:
        os.unlink(path)
    lines = result.stdout.split()
    if result.returncode != 0 or lines != expected:
        print(f"FAIL: exit {result.returncode}, answers {lines[:10]} against "
              f"{expected}; {result.stderr[-200:]}")
        sys.exit(1)
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def removal_cost(cetta, n):
    setup = SETUP.format(n=n, padding=LIVE - n)
    base = min(run(cetta, setup + PROBE, ["done", "done", "present"])
               for _ in range(3))
    full = min(run(cetta, setup + DROP.format(m=REMOVALS) + PROBE,
                   ["done", "done", "done", "present"])
               for _ in range(3))
    return max(full - base, 0.0)


def main():
    cetta = sys.argv[1]
    n = 1000
    run(cetta, SETUP.format(n=n, padding=0) + DROP.format(m=n) + ANSWERS,
        ["done", "done", "done", str(n), "true", str(n + 1), "true", str(n),
         "true", str(n - 1)])
    small = removal_cost(cetta, 64000)
    large = removal_cost(cetta, 512000)
    ratio = large / max(small, 0.05)
    if ratio > 2.0:
        print(f"FAIL: 128,000 absent removals took {large:.2f} s in 512,000 rows "
              f"against {small:.2f} s in 64,000 ({ratio:.1f}x)")
        sys.exit(1)
    print(f"PASS: removing an absent atom costs the same in a larger space: "
          f"128,000 removals {large:.2f} s in 512,000 rows, {small:.2f} s in "
          f"64,000 ({ratio:.1f}x)")


if __name__ == "__main__":
    main()
