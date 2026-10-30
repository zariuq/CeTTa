#!/usr/bin/env python3
"""Check that a count asked once builds no index.

usage: check_count_footprint.py FACTS

A document of FACTS facts (rel i j k), and one of four times as many, is
loaded and then counted three ways: not at all, once with its first column
bound, and sixteen times with its first column bound to sixteen values.
Counts that keep coming buy the column's index, as a trie or any index is
bought once its reads would cost more than building it; one count only
rents a scan.  From one size to the other, the peak memory of the run that
counts once may grow by at most a third of what the bought index adds over
the run that does not count, which must be 8 MiB or more, or the test
measures nothing.  The binary is $CETTA_BIN."""
import os
import resource
import subprocess
import sys
import tempfile

FLOOR_KIB = 8 * 1024
TAILS = {
    'none': '',
    'once': '!(size-atom (collapse (match &self (rel 77 $b $c) t)))\n',
    'repeated': '(= (counts $k $n $acc)\n'
                '   (if (>= $k $n) $acc\n'
                '       (counts (+ $k 1) $n\n'
                '               (+ $acc (size-atom (collapse '
                '(match &self (rel $k $b $c) t)))))))\n'
                '!(counts 0 16 0)\n',
}
ANSWERS = {'none': None, 'once': '1', 'repeated': '16'}


def peak_kib(program):
    child = subprocess.run(
        [sys.executable, __file__, '--one', program],
        capture_output=True, text=True, timeout=1200)
    rc, _, rest = child.stdout.strip().partition(' ')
    peak, _, answer = rest.partition(' ')
    if child.returncode != 0 or rc != '0':
        raise SystemExit(f"FAIL: {program}: rc {rc}\n{child.stdout}{child.stderr}")
    return int(peak), answer


def document(path, facts, tail):
    with open(path, 'w') as out:
        for i in range(facts):
            out.write(f'(rel {i} {i * 7 % 1000} {i * 13 % 1000})\n')
        out.write(tail)


def main():
    facts = int(sys.argv[1])
    growth = {}
    with tempfile.TemporaryDirectory() as directory:
        for observation, tail in TAILS.items():
            peaks = []
            for size in (facts, 4 * facts):
                path = os.path.join(directory, f'{observation}_{size}.metta')
                document(path, size, tail)
                peak, answer = peak_kib(path)
                os.unlink(path)
                if ANSWERS[observation] is not None and answer != ANSWERS[observation]:
                    print(f"FAIL: counting {observation} over {size} facts answered "
                          f"{answer!r}, expected {ANSWERS[observation]}")
                    return 1
                peaks.append(peak)
            growth[observation] = peaks[1] - peaks[0]
    bought = growth['repeated'] - growth['none']
    once = growth['once'] - growth['none']
    if bought < FLOOR_KIB:
        print(f"FAIL: counts that keep coming added only {bought} KiB: "
              f"the test measures nothing")
        return 1
    if 3 * once > bought:
        print(f"FAIL: one count added {once} KiB from {facts} to {4 * facts} facts, "
              f"against {bought} KiB for the index counts that keep coming buy")
        return 1
    print(f"PASS: a count asked once builds no index: {once:+d} KiB once, "
          f"{bought:+d} KiB for counts that keep coming, from {facts} to "
          f"{4 * facts} facts")
    return 0


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == '--one':
        done = subprocess.run(
            [os.environ['CETTA_BIN'], '--profile', 'extended', '--lang', 'petta',
             sys.argv[2]], capture_output=True, text=True, timeout=1200)
        peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        lines = done.stdout.strip().splitlines()
        print(f"{done.returncode} {peak} {lines[-1] if lines else ''}")
        sys.exit(0)
    sys.exit(main())
