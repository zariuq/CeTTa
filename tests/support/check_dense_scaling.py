#!/usr/bin/env python3
"""Check that a dense relation kept as bits grows by its bits, not by rows.

usage: check_dense_scaling.py PROGRAM NODES

The program takes its number of nodes as its first argument and derives a
dense relation over them.  It runs at NODES and at twice as many, with
relations as bits off and on, and must answer alike either way.  From one
size to the other, its peak memory with bits on may grow by at most a third
of what it grows with bits off, which must be 8 MiB or more, or the test
measures nothing.  The binary is $CETTA_BIN."""
import os
import resource
import subprocess
import sys

FLOOR_KIB = 8 * 1024


def peak_kib(program, nodes, mode):
    env = dict(os.environ, CETTA_SPACE_DENSE=mode)
    done = subprocess.run(
        [os.environ['CETTA_BIN'], '--profile', 'extended', '--lang', 'petta',
         program, str(nodes)],
        env=env, capture_output=True, text=True, timeout=900)
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    return done.returncode, done.stdout, peak


def main():
    program, nodes = sys.argv[1], int(sys.argv[2])
    growth = {}
    answers = {}
    for mode in ('on', 'off'):
        peaks = []
        for n in (nodes, 2 * nodes):
            # Each run in its own child, so its peak is its own.
            child = subprocess.run(
                [sys.executable, __file__, '--one', program, str(n), mode],
                capture_output=True, text=True, timeout=1200)
            rc, _, rest = child.stdout.partition('\n')
            out, _, peak = rest.rstrip('\n').rpartition('\n')
            if child.returncode != 0 or rc != '0':
                print(f"FAIL: {program} at {n} nodes, bits {mode}: rc {rc}\n"
                      f"{child.stdout}{child.stderr}")
                return 1
            if answers.setdefault(n, out) != out:
                print(f"FAIL: {program} at {n} nodes answers differently "
                      f"with bits on and off:\n{answers[n]}\n---\n{out}")
                return 1
            peaks.append(int(peak))
        growth[mode] = peaks[1] - peaks[0]
    if growth['off'] < FLOOR_KIB:
        print(f"FAIL: as rows the peak grew only {growth['off']} KiB: the test "
              f"measures nothing")
        return 1
    if 3 * growth['on'] > growth['off']:
        print(f"FAIL: as bits the peak grew {growth['on']} KiB from {nodes} to "
              f"{2 * nodes} nodes, against {growth['off']} KiB as rows")
        return 1
    print(f"PASS: a dense relation grows by its bits: +{growth['on']} KiB as "
          f"bits, +{growth['off']} KiB as rows, from {nodes} to {2 * nodes} "
          f"nodes")
    return 0


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--one':
        rc, out, peak = peak_kib(sys.argv[2], int(sys.argv[3]), sys.argv[4])
        print(f"{rc}\n{out}{peak}")
        sys.exit(0)
    sys.exit(main())
