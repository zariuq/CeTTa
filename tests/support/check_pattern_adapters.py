#!/usr/bin/env python3
"""Directional control forms preserve demand, caller scope and attempt-local protection."""
import argparse
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('binary', type=Path)
binary = str(parser.parse_args().binary.resolve())
checks = 0


def alpha(text):
    variables = {}
    return re.sub(r'\$[\w-]+(?:#\d+)?',
                  lambda m: variables.setdefault(m[0], f'$v{len(variables)}'), text)


def check(language, source, answers, *, module='pat', reference=False, effects=()):
    global checks
    env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
               CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
    result = subprocess.run([binary, '--lang', language, '--profile', 'extended',
        '-e', f'!(import! &self {module})\n' + source], env=env,
        capture_output=True, text=True, timeout=40)
    values = ['()' if language == 'he' else 'true'] + answers
    output = ''.join(f'[{v}]\n' if language == 'he' else v + '\n' for v in values)
    effect_text = ''.join(effect + '\n' for effect in effects)
    expected = (0, alpha((effect_text if language == 'petta' else '') + output),
                alpha(effect_text if language == 'he' else ''))
    actual = result.returncode, alpha(result.stdout), alpha(result.stderr)
    assert actual == expected, (language, reference, module, source, actual, expected)
    checks += 1


cases = [
    ('!(pat:let% (p $x) (p 2) (+ $x 10))', ['12']),
    ('!(pat:unify% (p $x) (p 2) (+ $x 10) bad)', ['12']),
    ('!(pat:unify% (p $x $x) (p a b) bad good)', ['good']),
    ('!(pat:unify% (p $x $x) (p $u $v) bad good)', ['good']),
    ('!(pat:unify% (p $x $x) (p $u $u) (same $x) bad)', ['(same $u)']),
    ('!(pat:unify% a b bad (+ 8 9))', ['17']),
    ('(= (danger) (trace! forbidden boom))\n'
     '!(pat:unify% $x (danger) (pat:apply (pat:solve match% ()) (saved $x)) bad)',
     ['(saved (danger))']),
    ('!(let $n 5 (pat:let% (p $x) (p (+ $n 1)) (+ $x 10)))', ['16']),
    ('!(collapse (pat:let% (p $x $x) (p a b) bad))', ['()']),
    ('!(pat:unify% a b never (pat:unify% (p $x) (p 2) (+ $x 10) no))', ['12']),
    ('!(pat:let% $dummy a (pat:let% (p $x) (p 2) (+ $x 10)))', ['12']),
    ('!(pat:let% $x $u (unify $x a (found $u) bad))', ['(found a)']),
    ('!(let $r (pat:unify% $x $u (unify $x a yes bad) no) (after $r $u))',
     ['(after yes a)']),
    ('(= (score $x) (+ $x 10))\n'
     '!(pat:case% (p 2) (((p 1) no) ((p $x) (score $x)) ($other bad)))', ['12']),
    ('!(pat:case% (p $u) (((p a) bad) ((p $x) (same $x))))', ['(same $u)']),
    ('!(pat:case% (p 2) (((p $x) first) ((p 2) second)))', ['first']),
    ('!(collapse (pat:case% a ()))', ['()']),
    ('!(collapse (pat:case% a ((b bad))))', ['()']),
    ('!(pat:case% (p 2) (((p $x) (pat:let% $y (+ $x 1) (+ $y 10)))))', ['13']),
    # Nesting starts a new matching attempt; a joint batch is explicit.
    ('!(pat:let% $x a (pat:let% $y $x (same $x $y)))', ['(same a a)']),
    ('!(pat:let% (p $x) (p $u) (pat:let% (q $x) (q a) (nested $u)))', ['(nested a)']),
    ('!(collapse (pat:solve match% (((p $x) (p $u)) ((q $x) (q a)))))', ['()']),
    ('!(collapse (pat:solve match% (((p $x) (p a)) ((q $x) (q b)))))', ['()']),
    ('!(pat:apply (pat:solve match% (((p $x) (p $u)) ((q $y) (q $u)))) (same $x $y))',
     ['(same $u $u)']),
    ('!(collapse (pat:let% $x (superpose (a b)) (pat:let% $y $x (pair $x $y))))',
     ['((pair a a) (pair b b))']),
    ('(= (local-client $n) (pat:let% $x $n (pat:let% $y (+ $x 1) (+ $x $y))))\n'
     '!(local-client 2)\n!(local-client 5)', ['5', '11']),
    # Importing the namespace does not install short names.
    ('!(unify% a a yes no)\n!(match% &self (p a))',
     ['(unify% a a yes no)', '(match% &self (p a))']),
]

for language in ('he', 'petta'):
    for reference in (False, True):
        for source, answers in cases:
            check(language, source, answers, reference=reference)
        check(language,
              '!(pat:let% $x (trace! value (+ 1 2)) (trace! body (+ $x 4)))',
              ['7'], reference=reference, effects=['value', 'body'])
        check(language,
              '!(pat:case% (trace! value (p 2)) '
              '(((p 1) (trace! forbidden bad)) '
              '((p $x) (trace! chosen (+ $x 10))) '
              '($other (trace! forbidden2 bad))))',
              ['12'], reference=reference, effects=['value', 'chosen'])
        check(language,
              '!(collapse (pat:let% a b (pat:let% $x (trace! forbidden a) '
              '(trace! forbidden2 bad))))', ['()'], reference=reference)
        check(language, '!(adapter-client 2)\n!(adapter-client 3)', ['12', '13'],
              module='tests/fixtures/pattern_adapters', reference=reference)

# PeTTa distinguishes a bound expression value from an authored call.
# A spelling adapter must not erase that occurrence distinction.
for reference in (False, True):
    for module, prefix in [('pat', 'pat:')]:
        for control in [
            f'({prefix}let% $x (quote (danger)) $x)',
            f'(let $v (quote (danger)) ({prefix}let% $x $v $x))',
            f'({prefix}unify% $x (danger) $x bad)',
            f'({prefix}case% (quote (danger)) (($x $x)))',
            f'({prefix}let% $x (quote (danger)) ({prefix}let% $y $x $y))',
        ]:
            check('petta', '(= (danger) (trace! forbidden 5))\n!' + control,
                  ['(danger)'], module=module, reference=reference)

print(f'PASS: {checks} directional adapter, effect, caller-context and joint-scope controls')
