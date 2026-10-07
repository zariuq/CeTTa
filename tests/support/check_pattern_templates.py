#!/usr/bin/env python3
"""Template retrieval preserves dialect evaluation and binding direction."""
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
    def line(value):
        names = {}
        return re.sub(r'"(?:\\.|[^"\\])*"|\$[\w-]+(?:#\d+)?',
                      lambda m: names.setdefault(m[0], f'$v{len(names)}')
                          if m[0].startswith('$') else m[0], value)
    return '\n'.join(line(value) for value in text.splitlines())


def check(language, source, answers, *, reference=False, effects=()):
    global checks
    env = dict(os.environ, CETTA_OPEN_EQUATIONS_REFERENCE=str(int(reference)),
               CETTA_HE_SHARED_EXECUTION=str(int(not reference)))
    result = subprocess.run([binary, '--lang', language, '--profile', 'extended',
        '-e', '!(import! &self pat)\n' + source], env=env,
        capture_output=True, text=True, timeout=40)
    values = ['()' if language == 'he' else 'true'] + answers
    output = ''.join(f'[{v}]\n' if language == 'he' else v + '\n' for v in values)
    effect_text = ''.join(effect + '\n' for effect in effects)
    expected = (0, alpha((effect_text if language == 'petta' else '') + output),
                alpha(effect_text if language == 'he' else ''))
    actual = result.returncode, alpha(result.stdout), alpha(result.stderr)
    assert actual == expected, (language, reference, source, actual, expected)
    checks += 1


cases = [
    ('(edge $s b) (edge a b) (edge a b)\n'
     '!(collapse (pat:match% &self (edge a $y) (destination $y)))',
     ['((destination b) (destination b))']),
    ('(edge $s b) (edge a b) (edge a b)\n'
     '!(collapse (pat:%match &self (edge a b) $stored (covered $stored)))',
     ['((covered (edge $s b)) (covered (edge a b)) (covered (edge a b)))']),
    ('(p a) (p b) (q a) (q b)\n'
     '!(collapse (pat:match% &self (, (p $x) (q $x)) (answer $x)))',
     ['((answer a) (answer b))']),
    ('(p $u) (q a)\n!(collapse (pat:match% &self (, (p $x) (q $x)) bad))', ['()']),
    ('(p $u) (q a)\n!(collapse (pat:match% &self (, (q $x) (p $x)) bad))', ['()']),
    ('(p a) (q b)\n!(collapse (pat:match% &self (, (p $x) (q $x)) bad))', ['()']),
    ('(p $u $u) (p $u $v)\n'
     '!(collapse (pat:%match &self (p a b) $stored $stored))', ['((p $u $v))']),
    ('(p $u) (q $v)\n'
     '!(collapse (pat:%match &self (, (p a) (q b)) $stored $stored))',
     ['(((p $u) (q $v)))']),
    ('!(pat:match% &self (,) identity)\n!(pat:%match &self (,) $stored $stored)',
     ['identity', '()']),
    ('(row $s)\n!(collapse (pat:%match &self (row $q) $q bad))', ['()']),
    ('(row $s)\n'
     '!(let $answer (pat:%match &self (row $q) $stored selected) (after $answer $q))',
     ['(after selected $q)']),
    ('(p a)\n!(let $answer (pat:match% &self (p $x) selected) (after $answer $x))',
     ['(after selected a)']),
    ('(p 2)\n(= (score $x) (+ $x 10))\n'
     '!(pat:match% &self (p $x) (score $x))', ['12']),
    ('(p 2)\n(= (client $tag) (pat:match% &self (p $x) (answer $tag $x)))\n'
     '!(client first)\n!(client second)', ['(answer first 2)', '(answer second 2)']),
    ('(p a)\n!(let $fn pat:match% '
     '(let $answer ($fn &self (p $x) selected) (after $answer $x)))',
     ['(after selected a)']),
    ('(p $s)\n!(let $fn pat:%match '
     '($fn &self (p a) $stored (answer $stored)))', ['(answer (p $s))']),
    ('(p a) (p b)\n'
     '!(collapse (pat:match% &self (p $x) '
     '(let $ignored (add-atom &self (p c)) (answer $x))))\n'
     '!(collapse (pat:match% &self (p $x) $x))',
     ['((answer a) (answer b))', '(a b c c)']),
    ('(p $s $s) (p $s $t)\n!(collapse (pat:match% &self (p $x $x) (same $x $x)))',
     ['((same $s $s))']),
    # Protection governs the attempt; an explicit body unification is ordinary.
    ('(p $s)\n!(pat:match% &self (p $x) (unify $x a (answer $x) bad))', ['(answer a)']),
    ('(= (danger) (trace! forbidden boom))\n'
     '!(pat:is-instance (danger) (danger))', ['TRUE']),
    ('!(let $answer (pat:is-instance (f a) (f $x)) (after $answer $x))',
     ['(after TRUE $x)']),
    ('!(let $answer (pat:is-variant (f $x $y) (f $y $x)) (after $answer $x $y))',
     ['(after TRUE $x $y)']),
    ('!(pat:is-instance (f $u $u) (f $x $x))\n'
     '!(pat:is-instance (f $u $v) (f $x $x))\n'
     '!(pat:is-instance (f $x $y) (f $y $x))\n'
     '!(pat:is-variant (f $x $y) (f $y $x))\n'
     '!(pat:is-variant (f $x $x) (f $u $v))',
     ['TRUE', 'FALSE', 'FALSE', 'TRUE', 'FALSE']),
    ('!(pat:is-instance "$x" "$x")\n'
     '!(pat:is-instance "$x" "$y")\n'
     '!(pat:is-variant "$x" "$y")', ['TRUE', 'FALSE', 'FALSE']),
]

for language in ('he', 'petta'):
    for reference in (False, True):
        for source, answers in cases:
            answers = [a.replace('TRUE', 'True' if language == 'he' else 'true')
                        .replace('FALSE', 'False' if language == 'he' else 'false') for a in answers]
            check(language, source, answers, reference=reference)
        check(language, '(p a) (p a)\n'
              '!(collapse (pat:match% &self (p $x) (trace! body (answer $x))))',
              ['((answer a) (answer a))'], reference=reference, effects=['body', 'body'])
        check(language, '(p a)\n'
              '!(collapse (pat:match% &self (p b) (trace! forbidden bad)))',
              ['()'], reference=reference)

assert alpha('(text "$x")') != alpha('(text "$y")')
assert alpha('(pair $x $x)') != alpha('(pair $x $y)')
print(f'PASS: {checks} template, direction, joint-protection, predicate and effect controls')
