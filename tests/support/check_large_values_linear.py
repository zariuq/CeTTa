#!/usr/bin/env python3
"""Values larger than any walk budget cost linear work in HE: a list built
by one call and passed by let to another, at n and 2n cells, typed and
untyped, costs at most 2.25x the instructions at 2n as at n, with the
answers n(n+1)/2.  The typed program's cost per cell is below the untyped
one's: its types tell HE what the untyped program must evaluate again.
Usage: check_large_values_linear.py BINARY"""
import os
import subprocess
import sys
import tempfile

BUILD = ("(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))\n"
         "(= (sum Nil) 0)\n(= (sum (Cons $x $xs)) (+ $x (sum $xs)))\n"
         "!(let $l (build @N@ Nil) (sum $l))\n")
TYPES = ("(: List (-> Type Type))\n(: Nil (List $a))\n(: Cons (-> $a (List $a) (List $a)))\n"
         "(: build (-> Number (List Number) (List Number)))\n(: sum (-> (List Number) Number))\n")
SIZES = (5000, 10000)


def measure(binary, directory, name, text):
    path = os.path.join(directory, name + '.metta')
    with open(path, 'w') as handle:
        handle.write(text)
    counts = path + '.perf'
    run = subprocess.run(['perf', 'stat', '-x,', '-e', 'instructions:u', '-o', counts,
                          '--', binary, '--lang', 'he', path],
                         capture_output=True, text=True, timeout=600)
    instructions = None
    with open(counts) as handle:
        for line in handle:
            fields = line.split(',')
            if len(fields) > 2 and 'instructions' in fields[2] and fields[0].isdigit():
                instructions = int(fields[0])
    return run.stdout.strip(), instructions


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix='large-values-linear-') as directory:
        return check(binary, directory)


def check(binary, directory):
    failures = []
    slopes = {}
    for family, prefix in (('untyped', ''), ('typed', TYPES)):
        counts = []
        for n in SIZES:
            answer, instructions = measure(binary, directory, f'{family}_{n}',
                                           prefix + BUILD.replace('@N@', str(n)))
            if answer != f'[{n * (n + 1) // 2}]' or instructions is None:
                failures.append(f'{family} n={n}: answer {answer!r}')
            counts.append(instructions or 0)
        ratio = counts[1] / counts[0] if counts[0] else float('inf')
        print(f'{family}: {counts[0]:,} -> {counts[1]:,} instructions ({ratio:.2f}x)')
        if ratio > 2.25:
            failures.append(f'{family}: {ratio:.2f}x from {SIZES[0]} to {SIZES[1]} cells')
        slopes[family] = (counts[1] - counts[0]) / (SIZES[1] - SIZES[0])
    if slopes['untyped'] > 0:
        per_cell = slopes['typed'] / slopes['untyped']
        print(f'per cell: typed {slopes["typed"]:,.0f}, untyped '
              f'{slopes["untyped"]:,.0f} ({per_cell:.2f}x)')
        if per_cell >= 1.0:
            failures.append(f'typed costs {per_cell:.2f}x untyped per cell')
    if failures:
        print('FAIL: ' + '; '.join(failures))
        return 1
    print('PASS: large HE values cost linear work, typed and untyped, and typed less per cell')
    return 0


if __name__ == '__main__':
    sys.exit(main())
