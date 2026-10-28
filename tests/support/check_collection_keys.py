#!/usr/bin/env python3
"""Stable key encoding against the authored route and an independent oracle."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def text(value):
    if isinstance(value, (list, tuple)):
        return '(' + ' '.join(map(text, value)) + ')'
    return json.dumps(value)


def encode(values):
    keys, codes, firsts, positions = [], [], [], {}
    for index, key in enumerate(values):
        if key not in positions:
            positions[key] = len(keys)
            keys.append(key)
            firsts.append(index)
        codes.append(positions[key])
    return f'(List:Factorization {text(keys)} {text(codes)} {text(firsts)})'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    args = parser.parse_args()
    binary = str(args.cetta.resolve())
    rng = random.Random(230918)
    datasets = [[], ['b', 'a', 'b', 'c', 'a'], [0, 4294967296, 0, -1, 4294967296],
                ['Empty', 'true', 'a\x00b', 'a\x00b', 'a', 'é', 'e']]
    datasets += [[f'key-{rng.randrange(31)}' for _ in range(size)] for size in (7, 64, 512)]
    calls = []
    for values in datasets:
        calls.append(f'!(== (list:factorize {text(values)}) {encode(values)})')
        calls.append(f'!(== (list:factorize {text(values)}) (list:factorize-reference {text(values)}))')
    calls += [
        '!(== (list:factorize (list:from-expression ("b" "a" "b"))) '
        '(List:Factorization ("b" "a") (0 1 0) (0 1)))',
        '!(== (list:factorize ((K 1) (K 2) (K 1))) '
        '(List:Factorization ((K 1) (K 2)) (0 1 0) (0 1)))',
        '!(== (__cetta_lib_list_factorize (1 1.0)) List:FactorizeFallback)',
        '!(== (__cetta_lib_list_factorize ((K 1) (K 1.0))) List:FactorizeFallback)',
        '!(== (list:factorize (1 1.0 2 1)) (List:Factorization (1 2) (0 0 1 0) (0 2)))',
        '!(== (list:factorize (0.0 -0.0 2.0)) (list:factorize-reference (0.0 -0.0 2.0)))',
        '!(== (__cetta_lib_list_factorize (a $open)) List:FactorizeFallback)',
    ]
    probe = '!(__cetta_lib_list_factorize ("b" "a" "b"))\n'
    source = '!(import! &self (library list))\n' + probe + '\n'.join(calls)
    checks = 0
    for reference in ('0', '1'):
        run = subprocess.run([binary, '--lang', 'petta', '-e', source],
            cwd=ROOT, text=True, capture_output=True, timeout=60,
            env=dict(os.environ, CETTA_KEY_ENCODING_REFERENCE=reference))
        lines = run.stdout.splitlines()
        expected = encode(['b', 'a', 'b']) if reference == '0' else 'List:FactorizeFallback'
        assert run.returncode == 0 and lines == ['true', expected] + ['true'] * len(calls), (
            reference, run.stdout, run.stderr)
        checks += len(calls) + 1
    # One small delivery check for each other public result boundary, not a
    # backend/configuration build matrix.
    for language in ('he', 'prime'):
        source = '!(import! &self list)\n!(list:factorize ("b" "a" "b"))'
        run = subprocess.run([binary, '--lang', language, '-e', source],
            cwd=ROOT, text=True, capture_output=True, timeout=30)
        assert run.returncode == 0 and run.stdout.splitlines()[-1] == '[' + encode(['b', 'a', 'b']) + ']', (
            language, run.stdout, run.stderr)
        checks += 1
    print(f'CollectionKeys {checks} 0')


if __name__ == '__main__':
    main()
