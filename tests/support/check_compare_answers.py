#!/usr/bin/env python3
"""The answer comparator agrees with the observation contract: a query's
answers agree as a bag, a collapse's elements as a bag, effects in order,
and nothing else is forgiven.

usage: check_compare_answers.py CETTA"""
import subprocess
import itertools
import collections
import sys
import tempfile
import json
import os
from pathlib import Path
from compare_answers import (answers_equal, observe_framed, compare_framed,
                             compare_completed_runs, collapse_equal, CollectionTrace, TOKEN, MARKER)

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
            cmd += ['--cetta', *(cetta if isinstance(cetta, list) else [cetta]),
                    '--lang', 'petta']
        return subprocess.run(cmd, capture_output=True).returncode == 0


def collection_trace_failures():
    failures = []
    cases = [
        ('bag rename/permutation', '((p $x) (q $x))', '((q $a) (p $a))', (), True),
        ('lost sharing', '((p $x) (q $x))', '((q $a) (p $b))', (), False),
        ('merged distinct variables', '((p $x) (q $y))', '((p $a) (q $a))', (), False),
        ('literal variable text', '("cost $x")', '("cost $y")', (), False),
        ('nested fields', '((p 1 2))', '((p 2 1))', (), False),
        ('ordered data probe', '(p 1 2)', '(p 2 1)', (0,), False),
        ('multiplicity', '(1 1 2)', '(1 2 2)', (), False),
        ('chronology', 'first\nsecond', 'second\nfirst', (), False),
        ('answer block permutation', 'seg-begin\n(p $x $x)\n7\nseg-end',
         'seg-begin\n7\n(p $a $a)\nseg-end', (), True),
        ('empty block is an observation', 'seg-begin\nseg-end', '', (), False),
        ('quoted parentheses', '("(a b)")', '("(b a)")', (), False),
        ('backtracking path', '((e $x $y) (e $y $z))',
         '((e $b $c) (e $a $b))', (), True),
        ('cycle differs from path', '((e $x $y) (e $y $z))',
         '((e $a $b) (e $b $a))', (), False),
    ]
    for name, left, right, ordered, wanted in cases:
        if (CollectionTrace(left, ordered_lines=ordered) ==
                CollectionTrace(right, ordered_lines=ordered)) != wanted:
            failures.append('collection trace: ' + name)
    for invalid in ('seg-begin', 'seg-end', 'seg-begin\nseg-begin\nseg-end'):
        try:
            CollectionTrace(invalid)
            failures.append('malformed collection trace accepted')
        except ValueError:
            pass

    # Independently enumerate complete variable bijections. This oracle does
    # not use candidate grouping, search frames, or rollback trails.
    def oracle(left, right):
        left = [TOKEN.findall(atom) for atom in left]
        right = [TOKEN.findall(atom) for atom in right]
        first = sorted({token for atom in left for token in atom if token.startswith('$')})
        second = sorted({token for atom in right for token in atom if token.startswith('$')})
        if len(first) != len(second):
            return False
        target = collections.Counter(tuple(atom) for atom in right)
        for renamed in itertools.permutations(second):
            mapping = dict(zip(first, renamed))
            if collections.Counter(tuple(mapping.get(token, token) for token in atom)
                                   for atom in left) == target:
                return True
        return False

    first = ['(p $x $x)', '(p $x $y)', '(q $x)', '(q "$x")', '(q 7)']
    second = ['(p $a $a)', '(p $a $b)', '(q $a)', '(q "$x")', '(q 7)']
    lefts = [bag for n in range(4) for bag in itertools.combinations_with_replacement(first, n)]
    rights = [bag for n in range(4) for bag in itertools.combinations_with_replacement(second, n)]
    for left in lefts:
        for right in rights:
            wanted = oracle(left, right)
            if collapse_equal('(' + ' '.join(left) + ')', '(' + ' '.join(right) + ')') != wanted:
                failures.append('collection bijection oracle mismatch')
                return failures
    left = '(' + ' '.join(f'(p $x{i} {i})' for i in range(2000)) + ')'
    right = '(' + ' '.join(f'(p $a{i} {i})' for i in reversed(range(2000))) + ')'
    if not collapse_equal(left, right):
        failures.append('large open collection refused')
    broken = right.replace('$a1 ', '$a0 ', 1)
    if collapse_equal(left, broken):
        failures.append('large collection merged distinct variables')
    if not failures:
        print(f'PASS: collection trace protocol, {len(lefts) * len(rights)} independent bijection checks and 2000 open elements')
    return failures


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
    failures = collection_trace_failures()
    independent = [
        ('alpha sharing', ['(p $x $x)'], ['(p $a $a)'], False, True),
        ('alias lost', ['(p $x $x)'], ['(p $a $b)'], False, False),
        ('bag multiplicity', ['1', '1', '2'], ['1', '2', '2'], False, False),
        ('collapse shared variable permuted', ['((p $x) (q $x))'],
         ['((q $z) (p $z))'], True, True),
        ('collapse sharing lost', ['((p $x) (q $x))'],
         ['((q $z) (p $y))'], True, False),
        ('collapse independent variables', ['((p $x) (p $y))'],
         ['((p $z) (p $z))'], True, False),
        ('nested list remains ordered', ['((p 1 2) (q 3))'],
         ['((q 3) (p 2 1))'], True, False),
        ('string variable spelling is data', ['"$x"'], ['"$y"'], False, False),
        ('PeTTa comma is part of a token', ['(a,b)'], ['(a b)'], True, False),
        ('PeTTa brackets are symbols', ['([x y])'], ['(y] [x)'], True, True),
    ]
    for name, left, right, collapse, want in independent:
        if answers_equal(left, right, collapse) != want:
            failures.append(name)
    representations = [
        ('(p $x $x)', '(p $a $a)', True),
        ('(p $x $x)', '(p $a $b)', False),
        ('"$x"', '"$y"', False),
    ]
    for left, right, want in representations:
        if answers_equal([json.dumps(left)], [json.dumps(right)], representation=True) != want:
            failures.append('repr provenance/sharing')
    # These executions have identical unframed output, but different query
    # boundaries. Counts from one implementation would conceal the defect.
    left = observe_framed(f'{MARKER}\n1\n{MARKER}\n2\n{MARKER}\n', [False, False])
    right = observe_framed(f'{MARKER}\n1\n2\n{MARKER}\n{MARKER}\n', [False, False])
    if compare_framed([False, False], left, right)[0]:
        failures.append('independent query counts')
    for incomplete in (f'{MARKER}\n1\n', f'{MARKER}\n1\n{MARKER}\ntrailing\n'):
        try:
            observe_framed(incomplete, [False])
            failures.append('unfinished framing accepted')
        except ValueError:
            pass
    for code, timeout in ((2, False), (0, True), (-11, False)):
        record = dict(returncode=code, timed_out=timeout, observations=left)
        if compare_completed_runs([False, False], record, record)[0]:
            failures.append('equal unsuccessful executions accepted')
    for name, actual, want in petta:
        if agrees('petta', PROGRAM, expected, actual, cetta) != want:
            failures.append(f'petta: {name}')
    relative_binary = os.path.relpath(Path(cetta).resolve())
    wrapper = ['sh', '-c', 'exec "$@"', 'cetta-wrapper', relative_binary]
    if not agrees('petta', PROGRAM, expected, petta[0][1], wrapper):
        failures.append('petta: wrapped execution changed its working directory')
    failing_wrapper = ['sh', '-c', '"$@"; exit 7', 'cetta-wrapper', relative_binary]
    if agrees('petta', PROGRAM, expected, petta[0][1], failing_wrapper):
        failures.append('petta: matching frames concealed an execution failure')
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
          f'({len(petta) + len(he) + len(independent) + len(representations) + 9} cases)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
