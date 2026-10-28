#!/usr/bin/env python3
"""Revision observer against authored evaluation and real mutable sources."""
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
    expressions = [
        '(postings:sources-current ())',
        '''(let* (($s (new-space)) ($r (_minimal-space-revision $s)))
          (postings:sources-current (($s $r))))''',
        '''(let* (($s (new-space)) ($r (_minimal-space-revision $s))
                   ($guards (($s $r))) ($changed (add-atom $s Changed)))
          (not (postings:sources-current $guards)))''',
        '''(let* (($a (new-space)) ($b (new-space))
                   ($ar (_minimal-space-revision $a)) ($br (_minimal-space-revision $b))
                   ($guards (($a $ar) ($b $br))) ($changed (add-atom $b Changed)))
          (not (postings:sources-current $guards)))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($guards (($a $r) ($a $r))))
          (postings:sources-current $guards))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($guards (($a (+ 0.0 $r)))))
          (postings:sources-current $guards))''',
        '''(let $a (new-space) (not (postings:sources-current (($a -1)))))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($tail (cons-atom $r ())) ($row (cons-atom $a $tail))
                   ($guards (cons-atom $row ())))
          (postings:sources-current $guards))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($guards (__cetta_lib_list_from_expression (($a $r)))))
          (postings:sources-current $guards))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($row (__cetta_lib_list_from_expression ($a $r)))
                   ($guards ($row)))
          (postings:sources-current $guards))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($guards (($a $r))) ($changed (add-atom $a Temporary))
                   ($removed (remove-atom $a Temporary)))
          (not (postings:sources-current $guards)))''',
        '''(let* (($a (new-space)) ($r (_minimal-space-revision $a))
                   ($guards (($a $r))) ($before (sources:test-wrapper $guards))
                   ($changed (add-atom $a Changed)) ($after (sources:test-wrapper $guards)))
          (and $before (not $after)))''',
    ]
    checks = 0
    for language, profile in (('petta', None), ('petta', 'extended'),
                              ('he', 'extended'), ('prime', None)):
        petta = language.startswith('petta')
        module = '(library postings)' if petta else 'postings'
        prefix = (f'!(import! &self {module})\n'
                  '(= (sources:test-wrapper $guards) (postings:sources-current $guards))\n')
        source = prefix + ''.join('!' + expression + '\n' for expression in expressions)
        command = [str(binary), '--lang', language]
        if profile:
            command += ['--profile', profile]
        observations = []
        for reference, fuel in (('0', False), ('1', False), ('0', True)):
            env = dict(os.environ, CETTA_POSTINGS_SOURCES_REFERENCE=reference)
            result = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) + ['-e', source],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            expected = ['true' if petta else '[True]'] * len(expressions)
            if language == 'prime':
                # Prime's total structural equality distinguishes a Float
                # revision from an Int. Native admission declines that row.
                expected[5] = '[False]'
            assert result.returncode == 0 and result.stdout.splitlines()[-len(expected):] == expected, (
                language, reference, fuel, result.stdout[-2500:], result.stderr[-1500:])
            observations.append((result.stdout, result.stderr))
            checks += len(expected)
        assert observations[0] == observations[1] == observations[2]
        probe = prefix + '''!(let* (($s (new-space)) ($r (_minimal-space-revision $s)))
          (case (__cetta_lib_postings_sources_current (($s $r)))
            ((Postings:Fallback "reference") ($other "native"))))'''
        for reference, fuel in (('0', False), ('1', False), ('0', True)):
            env = dict(os.environ, CETTA_POSTINGS_SOURCES_REFERENCE=reference)
            result = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) + ['-e', probe],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            mode = 'reference' if reference == '1' or fuel else 'native'
            expected = f'"{mode}"' if petta else f'["{mode}"]'
            assert result.returncode == 0 and result.stdout.splitlines()[-1] == expected, (
                language, reference, fuel, result.stdout, result.stderr)
            checks += 1
        for body in (
            'unrecognized-collection',
            '(BrokenRow)',
            '((17 0))',
            '''(let* (($s (new-space)) ($r (_minimal-space-revision $s)))
              (cons-atom ($s $r) ((MalformedRow))))''',
            '''(let* (($s (new-space)) ($r (_minimal-space-revision $s))
                       ($shown (println! "SourcesEffect"))) (($s $r)))''',
        ):
            seen = []
            for reference in ('0', '1'):
                env = dict(os.environ, CETTA_POSTINGS_SOURCES_REFERENCE=reference)
                result = subprocess.run(command + ['-e', prefix +
                    f'!(postings:sources-current {body})'], cwd=ROOT, env=env,
                    capture_output=True, text=True, timeout=120)
                seen.append((result.returncode, result.stdout, result.stderr))
            assert seen[0] == seen[1], (language, body, seen)
            if 'SourcesEffect' in body:
                assert seen[0][0] == 0 and seen[0][1].count('SourcesEffect') == 1, seen
            checks += 1
    print(f'PostingsSources {checks} 0')


if __name__ == '__main__':
    main()
