#!/usr/bin/env python3
"""Observe projection semantics across call preparation and execution routes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


EFFECTS = '''(: mark (-> Number %Undefined%))
(= (mark $value)
   (let $_ (add-atom &self (seen $value)) (payload $value tail)))
!(let $items (mark (superpose (1 1 2))) (car-atom $items))
!(collapse (match &self (seen $value) $value))
!(let $items (mark (superpose (3 3))) (cdr-atom $items))
!(collapse (match &self (seen $value) $value))
'''
LIVE = '''(= (project $value) (car-atom (later $value)))
!(project 0)
!(add-atom &self (unrelated row))
!(project 1)
!(add-atom &self (= (later $value) (answer $value)))
!(project 2)
!(add-atom &self (= (later $value) (second $value)))
!(collapse (project 3))
'''
CHOICE = '''(= (choose) left)
(= (choose) right)
!(car-atom ((choose) tail))
!(let $items ((choose) tail) (car-atom $items))
'''


def cases(petta, modern):
    wrap = (lambda value: value + '\n') if petta else (lambda value: '[' + value + ']\n')
    entries = []
    for operation in ('car-atom', 'cdr-atom'):
        for value in ('()', '5'):
            source = f'!({operation} {value})\n'
            if not petta:
                expected = (0, wrap(f'(Error ({operation} {value}) "{operation} expects a '
                                    'non-empty expression as an argument")'), '')
            elif modern and operation == 'car-atom' and value == '()':
                expected = (2, '', 'error: uncaught PeTTa error: car_atom_empty: '
                                  'car-atom expects a non-empty expression\n')
            else:
                expected = (0, wrap('()'), '')
            entries.append((operation + '-' + ('empty' if value == '()' else 'scalar'), source, expected))
    entries += [('head', '!(car-atom (a b))\n', (0, wrap('a'), '')),
                ('tail', '!(cdr-atom (a b))\n', (0, wrap('(b)'), ''))]
    effects = ('payload\npayload\npayload\n(1 1 2)\n(3 tail)\n(3 tail)\n(1 1 2 3 3)\n'
               if petta else '[payload, payload, payload]\n[(1 1 2)]\n'
                    '[(3 tail), (3 tail)]\n[(1 1 2 3 3)]\n')
    choice = 'left\nright\nleft\nright\n' if petta else '[left, right]\n[left, right]\n'
    live = ('later\ntrue\nlater\ntrue\n' + ('answer\ntrue\n(answer second)\n'
            if modern else 'later\ntrue\n(later)\n')) if petta else (
            '[later]\n[()]\n[later]\n[()]\n[answer]\n[()]\n[(answer second)]\n')
    entries += [('effects', EFFECTS, (0, effects, '')),
                ('returned-choice', CHOICE, (0, choice, '')),
                ('live-callability', LIVE, (0, live, ''))]
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    profiles = [('petta', ['--lang', 'petta'], True, False),
                ('petta-extended', ['--lang', 'petta', '--profile', 'extended'], True, False),
                ('petta11', ['--lang', 'petta@1.1'], True, True),
                ('he', ['--lang', 'he'], False, False),
                ('he-compat', ['--lang', 'he', '--profile', 'he-compat'], False, False),
                ('he-extended', ['--lang', 'he', '--profile', 'extended'], False, False)]
    rows = []
    for profile, options, petta, modern in profiles:
        for name, source, expected in cases(petta, modern):
            path = args.out / (profile + '-' + name + '.metta')
            path.write_text(source)
            for machine, reference in [('0', '1'), ('1', '0'), ('1', '1')]:
                env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine,
                           CETTA_OPEN_EQUATIONS_REFERENCE=reference)
                run = subprocess.run([str(binary), *options, str(path)],
                                     cwd=args.out, env=env, capture_output=True,
                                     text=True, timeout=30)
                actual = (run.returncode, run.stdout, run.stderr)
                row = dict(profile=profile, case=name, machine=machine,
                           reference=reference, correct=actual == expected,
                           rc=run.returncode, stdout=run.stdout, stderr=run.stderr)
                rows.append(row)
                if actual != expected:
                    (args.out / 'observations.json').write_text(json.dumps(rows, indent=2) + '\n')
                    raise AssertionError((profile, name, machine, reference, actual, expected))
    result = dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), rows=rows)
    (args.out / 'observations.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'PASS: {len(rows)} projection observations preserve empty-value policy, '
          'repeated replies, effects and versioned live callability')


if __name__ == '__main__':
    main()
