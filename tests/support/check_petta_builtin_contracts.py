#!/usr/bin/env python3
"""Check versioned primitive behavior and explicit builtin declarations."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--fixtures', type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        'petta/language_versions/builtins')
    args = parser.parse_args()
    binary, fixtures = args.cetta.resolve(), args.fixtures.resolve()
    profiles = [(['--lang', 'petta'], False),
                (['--lang', 'petta@1.1'], True),
                (['--lang', 'petta', '--profile', 'typecheck-v2'], True),
                (['--lang', 'petta', '--profile', 'typecheck-v3'], True)]
    observations = 0
    for options, modern in profiles:
        expected = {
            'declarations': (fixtures / 'declarations.expected').read_text(),
            'declarations_not_imported': '(add_builtin_signatures &self)\n0\n',
            'empty_car': (
                '(Error car_atom_empty car-atom)\nleft\n()\n()\n'
                '(Error car_atom_empty car-atom)\n(Error car_atom_empty car-atom)\n'
                if modern else '()\nleft\n()\n()\nreached\n(before)\n'),
        }
        for machine in ('0', '1'):
            environment = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine)
            for case, stdout in expected.items():
                run = subprocess.run([str(binary), *options,
                                      str(fixtures / (case + '.metta'))],
                                     cwd=fixtures, env=environment,
                                     capture_output=True, text=True, timeout=30)
                actual = (run.returncode, run.stdout, run.stderr)
                wanted = (0, stdout, '')
                if actual != wanted:
                    raise AssertionError((options, machine, case, actual, wanted))
                observations += 1
            if modern:
                run = subprocess.run([str(binary), *options, '--strict',
                                      str(fixtures / 'empty_car_uncaught.metta')],
                                     cwd=fixtures, env=environment,
                                     capture_output=True, text=True, timeout=30)
                wanted = (2, '', 'error: uncaught PeTTa error: car_atom_empty: '
                                 'car-atom expects a non-empty expression\n')
                actual = (run.returncode, run.stdout, run.stderr)
                if actual != wanted:
                    raise AssertionError((options, machine, 'uncaught', actual, wanted))
                observations += 1
    print(f'PASS: {observations} builtin observations preserve explicit import, '
          'declaration order and multiplicity, and versioned caught errors')


if __name__ == '__main__':
    main()
