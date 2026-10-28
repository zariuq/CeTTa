#!/usr/bin/env python3
"""Check that a churn program's peak memory plateaus while the universe
reclaims, and grows with collection off.

usage: check_plateau.py PROGRAM EXPECTED TURNS

The program takes its number of turns as its first argument.  It runs at
TURNS and at four times as many, reclaiming and with collection off, and
must answer EXPECTED each time.  Reclaiming, its peak may grow by 1 MiB at
most; with collection off it must grow by more, or the test measures
nothing.  The binary is $CETTA_BIN."""
import os
import resource
import subprocess
import sys

BOUND_KIB = 1024


def peak_kib(program, turns, mode):
    env = dict(os.environ, CETTA_TERM_UNIVERSE_COLLECTION=mode)
    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    done = subprocess.run(
        [os.environ['CETTA_BIN'], '--profile', 'extended', '--lang', 'petta',
         program, str(turns)],
        env=env, capture_output=True, text=True, timeout=900)
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    if peak < before:
        peak = before
    return done.returncode, done.stdout, peak


def main():
    program, expected_file, turns = sys.argv[1], sys.argv[2], int(sys.argv[3])
    expected = open(expected_file).read()
    growth = {}
    for mode in ('reclaim', 'off'):
        peaks = []
        for n in (turns, 4 * turns):
            # Each run in its own child, so its peak is its own.
            child = subprocess.run(
                [sys.executable, __file__, '--one', program, str(n), mode],
                capture_output=True, text=True, timeout=1200)
            rc, _, rest = child.stdout.partition('\n')
            if child.returncode != 0 or rc != '0' or not rest.startswith(expected):
                print(f"FAIL: {program} at {n} turns, {mode}: rc {rc}\n{child.stdout}{child.stderr}")
                return 1
            peaks.append(int(rest[len(expected):].strip()))
        growth[mode] = peaks[1] - peaks[0]
    if growth['reclaim'] > BOUND_KIB:
        print(f"FAIL: reclaiming, the peak grew {growth['reclaim']} KiB from {turns} to {4 * turns} turns")
        return 1
    if growth['off'] <= BOUND_KIB:
        print(f"FAIL: with collection off the peak grew only {growth['off']} KiB: the test measures nothing")
        return 1
    print(f"PASS: churned terms plateau: +{growth['reclaim']} KiB reclaiming, "
          f"+{growth['off']} KiB with collection off, from {turns} to {4 * turns} turns")
    return 0


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--one':
        rc, out, peak = peak_kib(sys.argv[2], int(sys.argv[3]), sys.argv[4])
        print(f"{rc}\n{out}{peak}")
        sys.exit(0)
    sys.exit(main())
