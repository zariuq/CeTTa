#!/usr/bin/env python3
"""Clause alignment, global target scopes, polarity and occurrence policies."""
import argparse
from pathlib import Path
import os
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
binary = str(parser.parse_args().binary.resolve())

cases = [
    ('set', '((pos (p $x)) (pos (q $x)))',
     '((pos (q a)) (neg (r c)) (pos (p a)))', '(at $x)', '((at a))'),
    ('set', '((pos (p $x)) (pos (q $x)))',
     '((pos (p a)) (pos (q b)))', '(at $x)', '()'),
    ('set', '((pos (p $x)))', '((neg (p a)))', '$x', '()'),
    ('set', '((pos (p $x)) (pos (q $y)))', '((pos (p a)))', '$x', '()'),
    ('set', '()', '((pos (p a)))', 'identity', '(identity)'),
    ('multiset', '()', '()', 'identity', '(identity)'),
    ('set', '((pos (p $x)))', '()', '$x', '()'),
    ('set', '((pos (p $x)) (pos (p $y)))', '((pos (p a)))',
     '(at $x $y)', '((at a a))'),
    ('multiset', '((pos (p $x)) (pos (p $y)))', '((pos (p a)))',
     '(at $x $y)', '()'),
    ('multiset', '((pos (p $x)) (pos (p $y)))', '((pos (p a)) (pos (p a)))',
     '(at $x $y)', '((at a a) (at a a))'),
    ('set', '((pos (p $x)))', '((pos (p a)) (pos (p a)))', '$x', '(a a)'),
    # A target variable is rigid even in a literal the alignment does not use.
    ('set', '((pos (p $x)))', '((pos (p a)) (pos (q $x)))', '$x', '()'),
    ('set', '((pos (p $x)))', '((pos (p a)) (pos (q $u)))', '$x', '(a)'),
    ('set', '((pos (p $x)) (pos (q $x)))',
     '((pos (p $u)) (pos (q $u)))', '(at $x)', '((at $u))'),
    ('set', '((pos (p $x)) (pos (q $x)))',
     '((pos (p $u)) (pos (q $v)))', '(at $x)', '()'),
    ('set', '((pos (p $x $x)))', '((pos (p $u $v)))', '$x', '()'),
    ('set', '((pos (p $x $x)))', '((pos (p $u $u)))', '$x', '($u)'),
    ('set', '((pos (= a b)))', '((pos (= b a)))', 'wrong', '()'),
]

faults = [
    ('unknown', '()', '()', 'UnknownClausePolicy'),
    ('$policy', '()', '()', 'UnknownClausePolicy'),
    ('(danger)', '()', '()', 'UnknownClausePolicy'),
    ('set', 'a', '()', 'ExpectedSignedClause'),
    ('set', '$clause', '()', 'ExpectedSignedClause'),
    ('set', '()', '$clause', 'ExpectedSignedClause'),
    ('set', '((oops (p a)))', '()', 'ExpectedSignedClause'),
    ('set', '()', '((pos))', 'ExpectedSignedClause'),
    ('set', '()', '((neg a b))', 'ExpectedSignedClause'),
    ('multiset', '($literal)', '()', 'ExpectedSignedClause'),
    ('set', '()', '((pos (danger)) bad)', 'ExpectedSignedClause'),
]

def alpha(text):
    variables = {}
    return re.sub(r'\$[\w-]+(?:#\d+)?',
        lambda m: variables.setdefault(m[0], f'$v{len(variables)}'), text)

checked = 0
for lang in ('he', 'petta'):
    for reference in (False, True):
        env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
                   CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
        for policy, general, specific, template, expected in cases:
            source = ('!(import! &self pat:clause)\n'
                      # A stored literal is syntax even when its head is callable.
                      '(= (p $x) (trace! forbidden bad))\n'
                      f'!(collapse (let $w (pat:subsume {policy} {general} {specific}) '
                      f'(pat:apply $w {template})))')
            result = subprocess.run([binary, '--lang', lang, '--profile', 'extended',
                '-e', source], env=env, text=True, capture_output=True, timeout=30)
            output = f'[()]\n[{expected}]\n' if lang == 'he' else f'true\n{expected}\n'
            assert (result.returncode, alpha(result.stdout), result.stderr) == (0, alpha(output), ''), (
                lang, reference, source, result.returncode, result.stdout, result.stderr, output)
            checked += 1
        for policy, general, specific, reason in faults:
            call = f'(pat:subsume {policy} {general} {specific})'
            source = ('!(import! &self pat:clause)\n'
                      '(= (danger) (trace! forbidden bad))\n!' + call)
            result = subprocess.run([binary, '--lang', lang, '--profile', 'extended',
                '-e', source], env=env, text=True, capture_output=True, timeout=30)
            error = f'(Error {call} {reason})'
            output = f'[()]\n[{error}]\n' if lang == 'he' else f'true\n{error}\n'
            assert (result.returncode, alpha(result.stdout), result.stderr) == (0, alpha(output), ''), (
                lang, reference, source, result.returncode, result.stdout, result.stderr, output)
            checked += 1
print(f'PASS: {checked} clause subsumption, alignment, polarity and shared-scope controls')
