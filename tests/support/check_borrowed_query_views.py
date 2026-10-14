#!/usr/bin/env python3
"""Observe query views through real callers, current bindings and consumers."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CASES = [{'name': 'flat-values',
  'source': '(probe 1 a)\n'
            '(probe 2 b)\n'
            '(probe 2 b)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup 2)\n',
  'petta': 'b\nb\n',
  'he': '[b, b]\n'},
 {'name': 'ground-subtree-slot',
  'source': '(probe (box 1) 7)\n'
            '(probe (box 2) 9)\n'
            '(probe (box 2) 9)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup (box 2))\n',
  'petta': '9\n9\n',
  'he': '[9, 9]\n'},
 {'name': 'repeated-variable',
  'source': '(same 1 1)\n'
            '(same 1 2)\n'
            '(same 2 2)\n'
            '(same 2 2)\n'
            '(= (lookup) (match &self (same $x $x) $x))\n'
            '!(lookup)\n',
  'petta': '1\n2\n2\n',
  'he': '[1, 2, 2]\n'},
 {'name': 'aliased-arguments',
  'source': '(same 1 1)\n'
            '(same 1 2)\n'
            '(same 2 2)\n'
            '(same 2 2)\n'
            '(= (lookup $a $b) (match &self (same $a $b) $a))\n'
            '!(lookup $shared $shared)\n',
  'petta': '1\n2\n2\n',
  'he': '[1, 2, 2]\n'},
 {'name': 'nested-open-template',
  'source': '(probe (box 1) 1)\n'
            '(probe (box 1) 2)\n'
            '(probe (box 2) 2)\n'
            '(probe (box 2) 2)\n'
            '(= (lookup) (match &self (probe (box $x) $x) $x))\n'
            '!(lookup)\n',
  'petta': '1\n2\n2\n',
  'he': '[1, 2, 2]\n'},
 {'name': 'open-nested-slot',
  'source': '(probe (box 1) 7)\n'
            '(probe (box 2) 9)\n'
            '(probe (box 2) 9)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup (box $shared))\n',
  'petta': '7\n9\n9\n',
  'he': '[7, 9, 9]\n'},
 {'name': 'branch-rollback',
  'source': '(probe 1 bad)\n'
            '(probe 2 good)\n'
            '(ok good)\n'
            '(= (lookup $key) (match &self (probe $key $x) (match &self (ok $x) $key)))\n'
            '!(lookup $shared)\n',
  'petta': '2\n',
  'he': '[2]\n'},
 {'name': 'occurs-check',
  'source': '(cycle-row $a (box $a))\n'
            '(= (lookup) (match &self (cycle-row $x $x) hit))\n'
            '!(lookup)\n',
  'petta': '',
  'he': ''},
 {'name': 'list-argument',
  'source': '(probe [1 2] 7)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup [1 2])\n',
  'petta': '7\n',
  'he': '[7]\n'},
 {'name': 'stored-row-freshening',
  'source': '(probe $x $x)\n'
            '(probe 1 2)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup 1)\n'
            '!(lookup 2)\n',
  'petta': '1\n2\n2\n',
  'he': '[1, 2]\n[2]\n'},
 {'name': 'data-mutation',
  'source': '(probe 2 9)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup 2)\n'
            '!(add-atom &self (probe 2 10))\n'
            '!(lookup 2)\n',
  'petta': '9\ntrue\n9\n10\n',
  'he': '[9]\n[()]\n[9, 10]\n'},
 {'name': 'effectful-consumer',
  'source': '(probe 2 9)\n'
            '(probe 2 9)\n'
            '(= (lookup $key) (match &self (probe $key $x) (let $u (add-atom &self (seen '
            '$x)) $x)))\n'
            '!(lookup 2)\n'
            '!(collapse (match &self (seen $x) $x))\n',
  'petta': '9\n9\n(9 9)\n',
  'he': '[9, 9]\n[(9 9)]\n'},
 {'name': 'callable-payload',
  'source': '(probe 2 (choice))\n'
            '(probe 2 (choice))\n'
            '(= (choice) left)\n'
            '(= (choice) right)\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(lookup 2)\n',
  'petta': '(choice)\n(choice)\n',
  'he': '[left, right, left, right]\n'},
 {'name': 'deep-live-slot',
  'source': '(= (nest $n $v) (if (== $n 0) $v (nest (- $n 1) (Box $v))))\n'
            '(= (lookup $key) (match &self (probe $key $x) $x))\n'
            '!(let $key (nest 256 7) (let $u (add-atom &self (probe $key 11)) (lookup '
            '$key)))\n',
  'petta': '11\n',
  'he': '[11]\n'},
 {'name': 'captured-space',
  'source': '(= (seed $s $n) (if (== $n 0) () (let $u (add-atom $s (padding $n)) (seed $s '
            '(- $n 1)))))\n'
            '(= (lookup $s $key) (match $s (probe $key $x) $x))\n'
            '!(let $s (new-space) (let $u (seed $s 128) (let $a (add-atom $s (probe 2 17)) '
            '(let $b (add-atom $s (probe 2 17)) (lookup $s 2)))))\n',
  'petta': '17\n17\n',
  'he': '[17, 17]\n'},
 {'name': 'constant-callable-field',
  'source': '(= (tag) (let $u (println! should-stay-data) constant))\n'
            '(probe (tag) 9)\n'
            '(probe (tag) 9)\n'
            '(= (lookup) (match &self (probe (tag) $x) $x))\n'
            '!(lookup)\n',
  'petta': '9\n9\n',
  'he': '[9, 9]\n'},
 {'name': 'many-variable-fields',
  'source': '(= (tag) (let $u (println! should-stay-data) constant))\n'
            '(probe (tag) 1 2 3 4 5 6 7 8 9)\n'
            '(probe (tag) 1 2 3 4 5 6 7 8 9)\n'
            '(= (lookup) (match &self (probe (tag) $a $b $c $d $e $f $g $h $i) (pair $a $i)))\n'
            '!(lookup)\n',
  'petta': '(pair 1 9)\n(pair 1 9)\n',
  'he': '[(pair 1 9), (pair 1 9)]\n'}]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    profiles = [('petta', ['--lang', 'petta']),
                ('petta-extended', ['--lang', 'petta', '--profile', 'extended']),
                ('he', ['--lang', 'he']),
                ('he-compat', ['--lang', 'he', '--profile', 'he-compat']),
                ('he-extended', ['--lang', 'he', '--profile', 'extended'])]
    observations = 0
    # Ensure native storage's indexed region is exercised. The fillers carry
    # no declarations or equations and cannot match the public query heads.
    prefix = ''.join(f'(filler {i})\n' for i in range(200))
    with tempfile.TemporaryDirectory(prefix='borrowed-query.', dir='runtime') as folder:
        for case in CASES:
            source = Path(folder) / (case['name'] + '.metta')
            source.write_text(prefix + case['source'])
            for profile, options in profiles:
                pe = profile.startswith('petta')
                expected = case['petta'] if pe else case['he']
                status = 0
                if case['name'] == 'occurs-check' and profile == 'he-compat':
                    expected = '[]\n'
                # A literal-list argument is an extended-profile value.
                # Other profiles retain their arity or inert-data behavior.
                if case['name'] == 'list-argument':
                    if profile == 'petta':
                        status, expected = 2, ''
                    elif profile in ('he', 'he-compat'):
                        expected = '[(lookup [1 2])]\n'
                for machine, reference in [('0', '1'), ('1', '0'), ('1', '1')]:
                    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine,
                               CETTA_OPEN_EQUATIONS_REFERENCE=reference)
                    run = subprocess.run([str(binary), *options, str(source)],
                                         env=env, capture_output=True, text=True, timeout=40)
                    assert (run.returncode, run.stdout) == (status, expected), (
                        case['name'], profile, machine, reference,
                        run.returncode, run.stdout, expected, run.stderr)
                    if status:
                        assert 'function_input_arities lookup (1)' in run.stderr, run.stderr
                    else:
                        assert run.stderr == '', run.stderr
                    observations += 1
    print(f'PASS: {observations} query observations preserve aliases, repeated '
          'occurrences, rollback, occurs checks, captures, deep terms, effects '
          'and dialect-specific interpretation')


if __name__ == '__main__':
    main()
