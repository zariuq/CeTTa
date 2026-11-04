#!/usr/bin/env python3
"""A loop run inside one statement keeps what it reaches, not what it made.

Usage: check_engine_loop_reclaim.py CETTA_RUNTIME_STATS_BINARY

A recursive loop parses a fresh term each iteration and drops it.  The open
equation machine runs it, exporting each host goal's ground structure to
the outer machine's tenured arena.  At N and 8 N iterations, inside one
statement, the outer machine's arenas (heap and tenured) peak within 1.25
times of each other (plus a megabyte): what the loop drops is reclaimed
while it runs, whatever its length.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZES = (80000, 640000)
GROWTH = 1.25
SLACK = 1 << 20


def run(binary, work, count):
    path = os.path.join(work, f"loop_{count}.metta")
    with open(path, "w") as f:
        f.write("(= (loop $n) (if (== $n 0) done "
                "(let $s (parse (repr (w $n))) (loop (- $n 1)))))\n"
                f"!(loop {count})\n")
    p = subprocess.run([binary, "--emit-runtime-stats", "--lang", "petta", path],
                       cwd=ROOT, capture_output=True, text=True, timeout=1200)
    printed = [l for l in p.stdout.splitlines() if not l.startswith("runtime-")]
    if p.returncode != 0 or printed != ["done"]:
        print(f"FAIL: the loop of {count} answered {printed[:3]}")
        return None
    for line in (p.stdout + p.stderr).splitlines():
        fields = line.split()
        if (len(fields) >= 3 and fields[0] == "runtime-counter" and
                fields[1] == "query-episode-survivor-arena-live-bytes-peak"):
            return int(fields[2])
    return 0


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cetta-engine-loop-") as work:
        peaks = [run(binary, work, n) for n in SIZES]
    if None in peaks:
        return 1
    small, large = peaks
    if large > GROWTH * small + SLACK:
        print(f"FAIL: the outer machine's arenas peaked at {small} bytes over "
              f"{SIZES[0]} iterations and {large} over {SIZES[1]}")
        return 1
    print(f"PASS: a loop inside one statement keeps what it reaches: arenas "
          f"peak at {small} and {large} bytes over {SIZES[0]} and {SIZES[1]} "
          f"iterations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
