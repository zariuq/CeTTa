#!/usr/bin/env python3
"""Check comparison folding against library availability and redefinition."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ROUTES = {
    'production': {},
    'machine': {'CETTA_OPEN_EQUATIONS_REFERENCE': '1'},
    'prepared-reference': {'CETTA_PREPARED_PURE_REFERENCE': '1'},
    'ordinary': {'CETTA_OPEN_EQUATIONS_REFERENCE': '1',
                 'CETTA_PREPARED_PURE_REFERENCE': '1',
                 'CETTA_PETTA_MATCH_EXISTENCE_FUSION': '0'},
}
SOURCE = '''!(bind! &s (new-space))
!(add-atom &s (seen 1))
(= (present $x) (if (!= () (collapse (once (match &s (seen $x) t)))) yes no))
!(!= () (collapse (once (match &s (seen 1) t))))
!(!= () (collapse (once (match &s (seen 3) t))))
!(present 1)
!(present 3)
!(!= 2 2)
!(!= 2 3)
!(!= same same)
!(!= "same" same)
!(!= ($x $x) ($x $x))
!(!= ($x $x) ($y $y))
!(if (!= 2 3) yes no)
!(if (!= 2 2) yes no)
!(unknown-comparison 2 2)
'''


def run(binary, source, expected, changes, statistics=False, language='petta'):
    env = {k: v for k, v in os.environ.items() if not k.startswith('CETTA_')}
    env.update(CETTA_PETTA_SEARCH_MACHINE='1', **changes)
    if statistics:
        env['CETTA_PETTA_MACHINE_STATS'] = '1'
    result = subprocess.run([str(binary), '--lang', language, str(source)],
                            cwd=ROOT, env=env, text=True,
                            capture_output=True, timeout=90)
    diagnostics = result.stderr
    if statistics:
        diagnostics = '\n'.join(line for line in diagnostics.splitlines()
                                if ' match_existence_observer_folds=' not in line)
    if result.returncode or diagnostics or result.stdout != expected:
        raise AssertionError((source.name, changes, result.returncode,
                              result.stdout, result.stderr, expected))
    return sum(int(n) for n in re.findall(
        r'\bmatch_existence_observer_folds=(\d+)', result.stderr))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--library', choices=('0', '1'), required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    library = args.library == '1'
    expected = ('true\ntrue\nfalse\nyes\nno\n'
                'false\ntrue\nfalse\ntrue\nfalse\ntrue\nyes\nno\n'
                '(unknown-comparison 2 2)\n')
    count = 0
    with tempfile.TemporaryDirectory(prefix='comparison-authority-',
                                     dir=ROOT / 'runtime') as temporary:
        source = Path(temporary) / 'comparison.metta'
        source.write_text(SOURCE)
        for name, changes in ROUTES.items():
            folds = run(binary, source, expected, changes, name == 'machine')
            if name == 'machine' and not folds:
                raise AssertionError(('comparison admission', library, folds))
            count += 1
        source.write_text('!(!= 2 2)\n!(unknown-comparison 2 2)\n')
        for changes in ROUTES.values():
            run(binary, source, '[(!= 2 2)]\n[(unknown-comparison 2 2)]\n',
                changes, language='he')
            count += 1
    if library:
        source = ROOT / 'tests/petta/native_comparison_redefined.metta'
        expected = source.with_suffix('.expected').read_text()
        for changes in ROUTES.values():
            run(binary, source, expected, changes)
            count += 1
        setup = '''!(import! &self (library lib_import))
!(import_prolog_functions_from_file "tests/petta/fixtures/native_comparison_callbacks.pl" (comparison-in-query comparison-after-callback))
!(bind! &b (new-space))
!(add-atom &b (seen 1))
(= (present $x) (if (!= () (collapse (once (match &b (seen $x) t)))) yes no))
'''
        with tempfile.TemporaryDirectory(prefix='comparison-callback-',
                                         dir=ROOT / 'runtime') as temporary:
            source = Path(temporary) / 'callback.metta'
            for callback, answer in [('comparison-in-query', '(yes no)'),
                                      ('comparison-after-callback', 'yes')]:
                source.write_text(setup + f'!({callback})\n!(present 1)\n')
                expected = f'true\ntrue\ntrue\ntrue\n{answer}\nno\n'
                for changes in ROUTES.values():
                    run(binary, source, expected, changes)
                    count += 1
            source.write_text('''!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/native_comparison_callbacks.pl"))))
!(bind! &b (new-space))
!(add-atom &b (seen 1))
(= (present $x) (if (!= () (collapse (once (match &b (seen $x) t)))) yes no))
!(present 1)
!(let $_ (callPredicate (Predicate (quote (comparison-before-callback $after)))) $after)
''')
            expected = 'true\ntrue\nyes\nno\n'
            for changes in ROUTES.values():
                run(binary, source, expected, changes)
                count += 1
    print(f'PASS: {count} comparison-authority controls preserve operation '
          'meaning, admission and unknown data')


if __name__ == '__main__':
    main()
