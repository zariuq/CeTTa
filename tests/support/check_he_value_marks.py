#!/usr/bin/env python3
"""HE's evaluated marks, across every route that answers an HE call.

HE marks an expression it evaluated and found unchanged, and never
evaluates a marked expression again; filling a template that is a single
variable clears the inserted value's own mark.  So a value computed now and
read after a rule for one of its parts appears shows which of its nodes were
marked: an unmarked node is evaluated again and takes the new rule.

Each program computes a value (VALUES), returns it through a function
(WRAPS), adds rules for (Box 1), (Pair 1 2) and (A B), and reads the value
again (OBSERVERS): whole, a field of it, a field taken by index, or held
inside a new constructor.  It then calls the function again.

A second family (TEMPLATES) fills each kind of template with a marked value
directly: an equation's body, let, if, case, chain, unify, a let pattern and
a match.

Laws checked:
  * the prepared-call and shared-execution routes answer every program as the
    canonical route does, in he and in he-compat;
  * each profile answers as he_value_marks_expected.json records:
    hyperon-experimental's answer (the fixes branch), except where CeTTa
    decides otherwise, recorded per profile:
      - a value HE answered is answered again as it is, in every profile,
        wherever it lands; hyperon-experimental evaluates a value again where
        it is a whole template that unify, let, chain or match fills;
      - he: a call that comes back stuck over its evaluated arguments is a
        value; an empty answer prints nothing;
      - a value of typed constructors is marked at every node, as HE marks
        each node it evaluates, so its fields stay values after a program
        change (typed-ev ... index).

--record <metta-repl> rewrites the expected answers: hyperon-experimental's,
and each profile's canonical answer where it differs.
"""

import argparse
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

RULES = ('(let $r1 (add-atom &self (= (Box 1) 91)) '
         '(let $r2 (add-atom &self (= (Pair 1 2) 92)) '
         '(add-atom &self (= (A B) 93))))')

# (name, definitions, value): every value has (Box _) at its top.
VALUES = [
    ('ctor', '', '(Box 1)'),
    ('nested', '', '(Box (Pair 1 2))'),
    ('over-call', '(= (mk) (Pair 1 2))', '(Box (mk))'),
    ('over-arith', '', '(Box (+ 0 1))'),
    ('over-raw', '', '(Box (cons-atom A (B)))'),
    ('over-quote', '', '(Box (quote (Pair 1 2)))'),
    ('over-collapse', '', '(Box (collapse (Pair 1 2)))'),
    ('over-car', '', '(Box (car-atom ((Pair 1 2) 3)))'),
    ('over-index', '', '(Box (index-atom ((Pair 1 2) 3) 0))'),
    ('match-data', '(val (Box (Pair 1 2)))', '(match &self (val $x) $x)'),
    ('match-tmpl', '(val 1)', '(match &self (val $x) (Box (Pair $x 2)))'),
    ('match-call-data', '(val (mk))\n(= (mk) (Box (Pair 1 2)))',
     '(match &self (val $x) $x)'),
    ('superpose', '', '(superpose ((Box (Pair 1 2))))'),
    ('unique', '', '(unique (superpose ((Box (Pair 1 2)) (Box (Pair 1 2)))))'),
    ('if', '', '(if (> 2 1) (Box (Pair 1 2)) none)'),
    ('case', '', '(case 1 ((1 (Box (Pair 1 2)))))'),
    ('let-var', '', '(let $y (Box (Pair 1 2)) $y)'),
    ('let-wrap', '', '(let $y (Pair 1 2) (Box $y))'),
    ('car-top', '', '(car-atom ((Box (Pair 1 2)) 3))'),
    ('cons-top', '', '(cons-atom Box ((Pair 1 2)))'),
    ('cdr-car', '', '(car-atom (cdr-atom (0 (Box (Pair 1 2)))))'),
    ('typed-ev', '(: Box (-> Pair Box))\n(: Pair (-> Number Number Pair))',
     '(Box (Pair 1 2))'),
    ('typed-atom', '(: Box (-> Atom Box))', '(Box (Pair 1 2))'),
    ('typed-call', '(: Box (-> Pair Box))\n(: Pair (-> Number Number Pair))\n'
     '(= (mk) (Pair 1 2))', '(Box (mk))'),
]
WRAPS = [
    ('direct', '(= (f) {v})'),
    ('recursive', '(= (f) (r 2))\n(= (r $n) (if (> $n 0) (r (- $n 1)) {v}))'),
    ('arg', '(= (f) (pass {v}))\n(= (pass $x) $x)'),
]
# A marked (Box 1), then a rule for (Box 1), then a template filled with it.
TEMPLATES = [
    ('equation', '(id2 $x)'),
    ('let', '(let $y $x $y)'),
    ('let-pattern', '(let ($y) ($x) $y)'),
    ('if', '(if True $x 0)'),
    ('case', '(case $x (($y $y)))'),
    ('chain', '(chain $x $y $y)'),
    ('unify', '(unify $x $y $y none)'),
    ('match', '(match &self (keep $y) $y)'),
]
OBSERVERS = [
    ('top', f'(let $x (f) (let $_ {RULES} $x))'),
    ('field', f'(let (Box $p) (f) (let $_ {RULES} $p))'),
    ('index', f'(let $x (f) (let $_ {RULES} (let $p (index-atom $x 1) $p)))'),
    ('kept', f'(let $x (f) (let $_ {RULES} (Keep $x)))'),
]

CANONICAL = {'CETTA_HE_SHARED_EXECUTION': '0', 'CETTA_PREPARED_PURE_REFERENCE': '1'}
PREPARED = {'CETTA_HE_SHARED_EXECUTION': '0'}
ROUTES = [('he', 'extended', CANONICAL), ('he-prepared', 'extended', PREPARED),
          ('he-shared', 'extended', {}),
          ('compat', 'he-compat', CANONICAL), ('compat-shared', 'he-compat', {})]


def programs():
    for (value, definitions, form), (wrap, body), (observer, read) in \
            itertools.product(VALUES, WRAPS, OBSERVERS):
        yield (f'{value}.{wrap}.{observer}',
               f'{definitions}\n{body.format(v=form)}\n(= (g) {read})\n!(g)\n!(f)\n')
    for name, fill in TEMPLATES:
        yield (f'template.{name}',
               '(= (f) (Box 1))\n(= (id2 $z) $z)\n'
               '!(let $x (f) (let $_ (add-atom &self (keep $x)) '
               f'(let $_ (add-atom &self (= (Box 1) 91)) {fill})))\n')


def answers(text):
    """Each printed answer set as a sorted tuple: HE's answers form a bag."""
    lines = re.findall(r'\[[^\[\]]*(?:\[[^\[\]]*\][^\[\]]*)*\]', text)
    if not lines:
        return text.strip()
    return [sorted(x.strip() for x in line[1:-1].split(', ')) for line in lines]


def run(command, path, extra_env):
    env = dict(os.environ)
    env.update(extra_env)
    try:
        process = subprocess.run(command + [path.name], cwd=path.parent,
                                 capture_output=True, text=True, timeout=120,
                                 env=env)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'
    out = ' '.join(process.stdout.split())
    return out if out else 'ERROR ' + ' '.join(process.stderr.split())[:120]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--expected', default=str(
        Path(__file__).with_name('he_value_marks_expected.json')))
    parser.add_argument('--record', metavar='METTA_REPL')
    parser.add_argument('--jobs', type=int, default=6)
    args = parser.parse_args()
    binary = str(Path(args.binary).resolve())

    with tempfile.TemporaryDirectory(prefix='he-value-marks-') as scratch:
        root = Path(scratch)
        jobs = []
        for name, text in programs():
            path = root / f'{name}.metta'
            path.write_text(text)
            for route, profile, extra in ROUTES:
                jobs.append((name, route, [binary, '--lang', 'he', '--profile',
                                           profile], path, extra))
            if args.record:
                jobs.append((name, 'rust', [args.record], path, {}))
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            results = list(pool.map(
                lambda job: (job[0], job[1], run(job[2], job[3], job[4])), jobs))
    table = {}
    for name, route, out in results:
        table.setdefault(name, {})[route] = out

    if args.record:
        expected = {}
        for name, row in sorted(table.items()):
            entry = {'rust': row['rust']}
            for profile, route in (('he', 'he'), ('he-compat', 'compat')):
                if answers(row[route]) != answers(row['rust']):
                    entry[profile] = row[route]
            expected[name] = entry
        Path(args.expected).write_text(json.dumps(expected, indent=1) + '\n')
        for profile in ('he', 'he-compat'):
            differs = [n for n, e in expected.items() if profile in e]
            print(f'{profile} differs from hyperon-experimental on '
                  f'{len(differs)} of {len(expected)} programs')
        return 0

    expected = json.loads(Path(args.expected).read_text())
    failures = []
    for name, row in sorted(table.items()):
        for route, out in row.items():
            if out == 'TIMEOUT' or out.startswith('ERROR'):
                failures.append(f'{name} {route}: {out}')
        for fast, reference in (('he-prepared', 'he'), ('he-shared', 'he'),
                                ('compat-shared', 'compat')):
            if answers(row[fast]) != answers(row[reference]):
                failures.append(f'{name}: {fast} {row[fast]} but {reference} '
                                f'{row[reference]}')
        want = expected.get(name)
        if want is None:
            failures.append(f'{name}: no expected answers recorded')
            continue
        for profile, route in (('he', 'he'), ('he-compat', 'compat')):
            wanted = want.get(profile, want['rust'])
            if answers(row[route]) != answers(wanted):
                failures.append(f'{name}: {profile} {row[route]} but '
                                f'expected {wanted}')
    if failures:
        for failure in failures[:40]:
            print('FAIL:', failure)
        print(f'FAIL: {len(failures)} disagreements over {len(table)} programs')
        return 1
    print(f'PASS: {len(table)} programs keep HE\'s evaluated marks on the '
          f'canonical, prepared and shared routes, in he and he-compat')
    return 0


if __name__ == '__main__':
    sys.exit(main())
