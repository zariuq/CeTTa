#!/usr/bin/env python3
"""The answer comparator agrees with the observation contract: a query's
answers agree as a bag, a collapse's elements as a bag, effects in order,
and nothing else is forgiven.

usage: check_compare_answers.py CETTA"""
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / 'compare_answers.py'
PROGRAM = '''(A 1)
(A 2)
(A 3)
!(println! "first")
!(match &self (A $x) $x)
!(println! "second")
!(collapse (match &self (A $x) $x))
'''


def agrees(lang, program, expected, actual, cetta):
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / 'p.metta').write_text(program)
        (d / 'e').write_text(expected)
        (d / 'a').write_text(actual)
        cmd = ['python3', str(TOOL), '--lang', lang, '--program', str(d / 'p.metta'),
               '--expected', str(d / 'e'), '--actual', str(d / 'a')]
        if lang == 'petta':
            cmd += ['--cetta', cetta, '--lang', 'petta']
        return subprocess.run(cmd, capture_output=True).returncode == 0


def main():
    cetta = sys.argv[1]
    expected = '"first"\n"second"\ntrue\n1\n2\n3\ntrue\n(1 2 3)\n'
    petta = [
        ('answers permuted', '"first"\n"second"\ntrue\n3\n1\n2\ntrue\n(1 2 3)\n', True),
        ('collapse permuted', '"first"\n"second"\ntrue\n1\n2\n3\ntrue\n(3 1 2)\n', True),
        ('an answer wrong', '"first"\n"second"\ntrue\n1\n2\n2\ntrue\n(1 2 3)\n', False),
        ('effects reordered', '"second"\n"first"\ntrue\n1\n2\n3\ntrue\n(1 2 3)\n', False),
        ('an answer moved to another query', '"first"\n"second"\ntrue\n1\n2\n3\n(1 2 3)\ntrue\n', False),
        ('a collapse duplicate', '"first"\n"second"\ntrue\n1\n2\n3\ntrue\n(1 2 2)\n', False),
    ]
    he_expected = 'first\nsecond\n[()]\n[1, 2, 3]\n[()]\n[(1 2 3)]\n'
    he = [
        ('answers permuted', 'first\nsecond\n[()]\n[3, 1, 2]\n[()]\n[(1 2 3)]\n', True),
        ('collapse permuted', 'first\nsecond\n[()]\n[1, 2, 3]\n[()]\n[(2 3 1)]\n', True),
        ('an answer wrong', 'first\nsecond\n[()]\n[1, 2, 2]\n[()]\n[(1 2 3)]\n', False),
        ('order inside an answer', 'first\nsecond\n[()]\n[1, 2, 3]\n[()]\n[(1 2 3)]\n', True),
        ('effects reordered', 'second\nfirst\n[()]\n[1, 2, 3]\n[()]\n[(1 2 3)]\n', False),
    ]
    failures = []
    for name, actual, want in petta:
        if agrees('petta', PROGRAM, expected, actual, cetta) != want:
            failures.append(f'petta: {name}')
    for name, actual, want in he:
        if agrees('he', PROGRAM, he_expected, actual, cetta) != want:
            failures.append(f'he: {name}')
    # A list a program builds is ordered: only a collapse at the top of a
    # query is compared as a bag.
    built = '!(cons-atom 1 (2 3))\n'
    if agrees('petta', built, '(1 2 3)\n', '(3 2 1)\n', cetta):
        failures.append('petta: a built list compared as a bag')
    if failures:
        print('FAIL: the answer comparator disagrees with the observation contract:')
        for failure in failures:
            print('  ' + failure)
        return 1
    print(f'PASS: answers compare as bags, collapses by their elements, effects in order '
          f'({len(petta) + len(he) + 1} cases)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
