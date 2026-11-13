#!/usr/bin/env python3
"""Ordered reads of a priority space cost linear work, whatever the number of
key shapes read in turn and however deep their keys stand.

Usage: check_priority_order_scaling.py BINARY

Counted in instructions (perf stat), at n and 2n rows:
- drains of interleaved lanes, rows (k i (w i mod L)) taken lane after lane
  by a first answer, for 5, 8 and 16 lanes: a full space costs at most 1.5x
  a strict one, and each costs at most 2.3x at 2n what it costs at n;
- keys at the 9th, 16th and 32nd element of rows sharing every element
  before them, read by first answers: at most 2.3x at 2n;
- a lane of 64 or 65 rows standing after every other row, read by first
  answers in a full space: at most 2.3x at 2n, and the 65-row lane at most
  1.5x the 64-row one.
A memo of a few key shapes, a key prefix of a few elements, or a gather of
an index's rows bounded by a constant makes one of them grow with the rows.
"""
import os
import subprocess
import sys
import tempfile

SIZES = (1600, 3200)
LINEAR = 2.3
PROFILE_RATIO = 1.5

LANES = """!(bind! &p (new-space {kind}))
(= (fill $i $m) (if (== $i $m) done (let $l (% $i {lanes}) (let $_ (add-atom &p (k $i (w $l))) (fill (+ $i 1) $m)))))
!(fill 0 {n})
(= (probe $k $h) (if (== $k 0) $h (let $l (% $k {lanes}) (let $r (collapse (once (transform &p (take (k $y (w $l))) $y))) (probe (- $k 1) (+ $h (size-atom $r)))))))
!(hits (probe {n} 0))
!(left (space-len &p))
"""

KEYS = """!(bind! &p (new-space priority))
(= (fill $i) (if (> $i {n}) done (let $_ (add-atom &p (k{front} $i $i)) (fill (+ $i 1)))))
!(fill 1)
(= (probe $i $acc) (if (> $i {n}) $acc (let $v (once (match &p (k{front} $i $x) $x)) (probe (+ $i 1) (+ $acc $v)))))
!(probe 1 0)
"""

LATE_LANE = """!(bind! &p (new-space priority full))
(= (fill $i $m $l) (if (== $i $m) done (let $_ (add-atom &p (k $i $l)) (fill (+ $i 1) $m $l))))
!(fill 0 {n} a)
!(fill {n} {end} b)
(= (probe $k $h) (if (== $k 0) $h (let $r (collapse (once (match &p (k $y b) $y))) (probe (- $k 1) (+ $h (size-atom $r))))))
!(probe {n} 0)
"""


def measure(binary, directory, name, text):
    path = os.path.join(directory, name + '.metta')
    with open(path, 'w') as handle:
        handle.write(text)
    counts = path + '.perf'
    run = subprocess.run(['perf', 'stat', '-x,', '-e', 'instructions:u', '-o', counts,
                          '--', binary, '--lang', 'petta', '--profile', 'extended',
                          path],
                         capture_output=True, text=True, timeout=1200)
    instructions = None
    with open(counts) as handle:
        for line in handle:
            fields = line.split(',')
            if len(fields) > 2 and 'instructions' in fields[2] and fields[0].isdigit():
                instructions = int(fields[0])
    answers = [line for line in run.stdout.splitlines() if line != 'true']
    return answers, instructions


def main():
    binary = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix='priority-order-scaling-') as directory:
        return check(binary, directory)


def check(binary, directory):
    failures = []

    def series(name, texts, expected):
        counts = []
        for n, text in zip(SIZES, texts):
            answers, instructions = measure(binary, directory, f'{name}_{n}', text)
            if answers[-len(expected(n)):] != expected(n) or instructions is None:
                failures.append(f'{name}, {n} rows: answered {answers[-3:]}')
                return None
            counts.append(instructions)
        ratio = counts[1] / counts[0]
        print(f'{name}: {counts[0]:,} -> {counts[1]:,} instructions ({ratio:.2f}x)')
        if ratio > LINEAR:
            failures.append(f'{name}: {ratio:.2f}x from {SIZES[0]} to {SIZES[1]} rows')
        return counts

    for lanes in (5, 8, 16):
        found = {}
        for profile, kind in (('strict', 'priority'), ('full', 'priority full')):
            found[profile] = series(
                f'{lanes} lanes, {profile}',
                [LANES.format(kind=kind, lanes=lanes, n=n) for n in SIZES],
                lambda n: [f'(hits {n})', '(left 0)'])
        if found['strict'] and found['full']:
            for n, strict, full in zip(SIZES, found['strict'], found['full']):
                if full > PROFILE_RATIO * strict:
                    failures.append(f'{lanes} lanes at {n} rows: full {full / strict:.2f}x strict')
    for element in (9, 16, 32):
        front = ''.join(f' s{j}' for j in range(1, element - 1))
        series(f'key at element {element}',
               [KEYS.format(front=front, n=n) for n in SIZES],
               lambda n: [str(n * (n + 1) // 2)])
    lane = {}
    for rows in (64, 65):
        lane[rows] = series(
            f'late lane of {rows} rows',
            [LATE_LANE.format(n=n, end=n + rows) for n in SIZES],
            lambda n: [str(n)])
    if lane[64] and lane[65] and lane[65][1] > PROFILE_RATIO * lane[64][1]:
        failures.append(f'late lane: 65 rows cost {lane[65][1] / lane[64][1]:.2f}x 64')
    if failures:
        print('FAIL: ' + '; '.join(failures))
        return 1
    print('PASS: drains of 5, 8 and 16 interleaved lanes cost a full priority space '
          'at most 1.5x a strict one; lanes, keys at the 9th, 16th and 32nd '
          'element and a late lane of 64 or 65 rows cost linear work')
    return 0


if __name__ == '__main__':
    sys.exit(main())
