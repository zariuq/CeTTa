#!/usr/bin/env python3
"""Range/repeat library controls in primary and extended delivery policies."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    cases = [
        ('(list:range 0)', '()'),
        ('(list:range -3)', '()'),
        ('(list:range 5)', '(0 1 2 3 4)'),
        ('(list:range -3 3)', '(-3 -2 -1 0 1 2)'),
        ('(list:range 3 3)', '()'),
        ('(list:range 9 -9)', '()'),
        ('(list:range -9223372036854775808 -9223372036854775806)',
         '(-9223372036854775808 -9223372036854775807)'),
        ('(list:range 9223372036854775805 9223372036854775807)',
         '(9223372036854775805 9223372036854775806)'),
        ('(list:range 9223372036854775807 -9223372036854775808)', '()'),
        ('(list:range (+ 1 3))', '(0 1 2 3)'),
        ('(seq:range 4)', '(0 1 2 3 4)'),
        ('(let $range (list:range -2048 2048) (size-atom $range))', '4096'),
        ('(list:repeat 0 held)', '()'),
        ('(list:repeat -9223372036854775808 held)', '()'),
        ('(list:repeat 3 7)', '(7 7 7)'),
        ('(list:repeat 2 (+ 1 2))', '(3 3)'),
        ('(list:repeat 2 (UnknownPayload 17))', '((UnknownPayload 17) (UnknownPayload 17))'),
        ('(seq:repeat 2)', '(7 7)'),
    ]
    negative = [
        ('(list:range -9223372036854775808 9223372036854775807)', 'RangeTooLarge'),
        ('(list:range -1 9223372036854775807)', 'RangeTooLarge'),
        ('(list:repeat 9223372036854775807 held)', 'RepeatTooLarge'),
        ('(list:range 2.0)', '(BadArgType 1 Number Number)'),
        ('(list:repeat 2.0 held)', '(BadArgType 1 Number Number)'),
        ('(list:range "wrong")', '(BadArgType 1 Number String)'),
        ('(list:range 0 "wrong")', '(BadArgType 2 Number String)'),
        ('(list:repeat "wrong" held)', '(BadArgType 1 Number String)'),
    ]
    checks = 0
    for language, profile in (('petta', None), ('petta', 'extended'),
                              ('he', 'extended'), ('prime', None)):
        petta = language.startswith('petta')
        module = '(library list)' if petta else 'list'
        prelude = f'''!(import! &self {module})
(= (seq:range $n) (list:range (+ $n 1)))
(= (seq:repeat $n) (list:repeat $n (+ 3 4)))
(= (seq:effect) (let $_ (println! "SequenceEffect") 7))
(= (seq:legacy-repeat $n $value) (repeat-atom $n $value))
'''
        source = prelude + ''.join(f'!(let $actual {body} (== $actual {expected}))\n'
                                  for body, expected in cases)
        if language != 'prime':
            source += ''.join(f'!(case {"(catch " + body + ")" if petta else body} '
                              f'(((Error $call {reason}) Rejected) ($other Unexpected)))\n'
                              for body, reason in negative)
        command = [str(binary), '--lang', language]
        if profile:
            command += ['--profile', profile]
        observations = []
        for reference, fuel in (('0', False), ('1', False), ('0', True)):
            env = dict(os.environ, CETTA_PREPARED_PURE_CODE_REFERENCE=reference)
            result = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) + ['-e', source],
                                    cwd=ROOT, env=env, text=True, capture_output=True, timeout=120)
            expected = ['true' if petta else '[True]'] * len(cases)
            if language != 'prime':
                expected += ['Rejected' if petta else '[Rejected]'] * len(negative)
            assert result.returncode == 0 and result.stdout.splitlines()[-len(expected):] == expected, (
                language, reference, fuel, result.stdout[-2000:], result.stderr[-2000:])
            observations.append((result.stdout, result.stderr))
            checks += len(expected)
            if language == 'prime':
                # Prime propagates a native Error past the surrounding case.
                # Test each rejection in its own request, not a later request
                # which that propagation intentionally prevents from running.
                for body, reason in negative:
                    rejected = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) +
                        ['-e', prelude + '!' + body], cwd=ROOT, env=env,
                        text=True, capture_output=True, timeout=120)
                    assert rejected.returncode == 0 and '(Error ' in rejected.stdout and reason in rejected.stdout, (
                        body, rejected.returncode, rejected.stdout, rejected.stderr)
                    assert '__cetta_lib_' not in rejected.stdout
                    checks += 1
        assert observations[0] == observations[1] == observations[2]
        effect = prelude + '!(list:repeat 3 (seq:effect))\n'
        result = subprocess.run(command + ['-e', effect], cwd=ROOT,
                                text=True, capture_output=True, timeout=120)
        assert result.returncode == 0 and result.stdout.count('SequenceEffect') == (0 if language == 'prime' else 1), (
            language, result.stdout, result.stderr)
        checks += 1
        if profile == 'extended' or language == 'prime':
            for body in ('(list:repeat 3 (seq:effect))', '(list:repeat 0 (seq:effect))',
                         '(list:repeat 3 (+ 1 2))', '(list:repeat 0 (+ 1 2))'):
                modern = subprocess.run(command + ['-e', prelude + '!' + body], cwd=ROOT,
                                        text=True, capture_output=True, timeout=120)
                legacy = subprocess.run(command + ['-e', prelude + '!' + body.replace('list:repeat', 'seq:legacy-repeat')],
                                        cwd=ROOT, text=True, capture_output=True, timeout=120)
                assert (modern.returncode, modern.stdout, modern.stderr) == (
                    legacy.returncode, legacy.stdout, legacy.stderr), (language, modern.stdout, legacy.stdout)
                checks += 1
        if language == 'he':
            # Overloaded native signatures must not multiply an arity or
            # argument-type error. Both HE drivers own this same contract.
            fixture = ROOT / 'tests/test_range_repeat_atom_syntax.metta'
            expected = fixture.with_suffix('.expected').read_text()
            for canonical in ('0', '1'):
                for fuel in (False, True):
                    env = dict(os.environ, CETTA_HE_CANONICAL_EXECUTION=canonical)
                    result = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) +
                        [str(fixture)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
                    assert result.returncode == 0 and result.stdout == expected and not result.stderr, (
                        canonical, fuel, result.returncode, result.stdout, result.stderr)
                    checks += len(expected.splitlines())
    print(f'ListNumericSequences {checks} 0')


if __name__ == '__main__':
    main()
