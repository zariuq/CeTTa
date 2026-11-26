#!/usr/bin/env python3
"""Check that loops rewriting stored state keep their memory flat.

usage: check_decoded_copy_flat.py REWRITES PROGRAM... [--answers-only]

Each program rewrites one stored state atom as many times as its first
argument says, reading the state each time as a decoded copy of the space's
row; its live data does not grow with the number of rewrites.  Each runs at
REWRITES and at four times as many, with decoded-copy epochs off and on, and
must answer the same in all four runs.  From one size to the other its peak
memory with epochs off grows by what the decoded copies of the extra
rewrites keep, which must be 8 MiB or more, or the test measures nothing;
with epochs on it may grow by at most an eighth of that.  The binary is
$CETTA_BIN."""
import os
import resource
import subprocess
import sys

FLOOR_KIB = 8 * 1024


def peak_kib(program, rewrites, epochs):
    env = dict(os.environ)
    if epochs:
        env.pop('CETTA_DECODE_EPOCHS', None)
    else:
        env['CETTA_DECODE_EPOCHS'] = '0'
    done = subprocess.run(
        [os.environ['CETTA_BIN'], '--lang', 'petta', program, str(rewrites)],
        env=env, capture_output=True, text=True, timeout=900)
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    return done.returncode, done.stdout, peak


def measure(program, rewrites):
    """Each program's answers and growth (KiB) with epochs off and on, or a
    failure message."""
    growth = {}
    answers = None
    for epochs in (False, True):
        peaks = []
        for n in (rewrites, 4 * rewrites):
            # Each run in its own child, so its peak is its own.
            child = subprocess.run(
                [sys.executable, __file__, '--one', program, str(n),
                 '1' if epochs else '0'],
                capture_output=True, text=True, timeout=1200)
            rc, _, rest = child.stdout.partition('\n')
            out, _, peak = rest.rstrip('\n').rpartition('\n')
            mode = 'on' if epochs else 'off'
            if child.returncode != 0 or rc != '0' or not out.strip() or \
                    'Error' in out:
                return None, (f"{program} at {n} rewrites, epochs {mode}: "
                              f"rc {rc}\n{child.stdout}{child.stderr}")
            if answers is None:
                answers = out
            elif out != answers:
                return None, (f"{program} at {n} rewrites, epochs {mode}, "
                              f"answers:\n{out}\nagainst:\n{answers}")
            peaks.append(int(peak))
        growth[epochs] = peaks[1] - peaks[0]
    return growth, None


def main():
    args = sys.argv[1:]
    answers_only = '--answers-only' in args
    args = [arg for arg in args if arg != '--answers-only']
    if len(args) < 2:
        sys.exit(__doc__.split('\n\n')[1])
    rewrites, programs = int(args[0]), args[1:]
    failed = False
    for program in programs:
        growth, failure = measure(program, rewrites)
        if failure:
            print(f"FAIL: {failure}")
            failed = True
            continue
        if answers_only:
            print(f"PASS: {program} answers alike with decoded-copy epochs on "
                  f"and off; memory is qualified on the native executable")
            continue
        if growth[False] < FLOOR_KIB:
            print(f"FAIL: {program}: with epochs off the peak grew only "
                  f"{growth[False]} KiB: the test measures nothing")
            failed = True
        elif 8 * growth[True] > growth[False]:
            print(f"FAIL: {program}: with epochs the peak grew "
                  f"{growth[True]} KiB from {rewrites} to {4 * rewrites} "
                  f"rewrites, against {growth[False]} KiB with epochs off")
            failed = True
        else:
            print(f"PASS: {program} keeps memory flat: {growth[True]:+d} KiB "
                  f"with decoded-copy epochs, {growth[False]:+d} KiB without, "
                  f"from {rewrites} to {4 * rewrites} rewrites")
    return 1 if failed else 0


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--one':
        rc, out, peak = peak_kib(sys.argv[2], int(sys.argv[3]),
                                 sys.argv[4] == '1')
        print(f"{rc}\n{out}{peak}")
        sys.exit(0)
    sys.exit(main())
