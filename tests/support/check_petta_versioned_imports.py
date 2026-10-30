#!/usr/bin/env python3
"""Check versioned import resolution, no-op cycles and destination identity."""
import argparse
import os
from pathlib import Path
import subprocess

from check_petta_corrected_stable import complete


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--fixtures', type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        'petta/language_versions/imports')
    args = parser.parse_args()
    binary = args.cetta.resolve()
    fixtures = args.fixtures.resolve()
    profiles = [(['--lang', 'petta'], True),
                (['--lang', 'petta@1.1'], True),
                (['--lang', 'petta', '--profile', 'typecheck-v2'], True),
                (['--lang', 'petta', '--profile', 'typecheck-v3'], True)]
    observations = 0
    for options, modern in profiles:
        expected = {
            'paths': 'true\ntrue\n' + ('(nested)' if modern else '(top)') + '\nouter\n',
            'self': 'true\n(here)\n',
            'spaces': 'true\ntrue\ntrue\ntrue\n(earlier seen)\n(seen)\n',
        }
        if modern:
            expected['cycles'] = 'is true, should true. ✅ \ntrue\ntrue\n(b a)\n'
        # Parse errors on the typed line have their own source diagnostic.
        # Stable must expose loading errors without enabling that checker.
        if options == ['--lang', 'petta']:
            expected['recovery'] = 'Error\nError\ntrue\n(recovered)\n'
        for machine in ('0', '1'):
            environment = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine)
            for case, stdout in expected.items():
                run = subprocess.run([str(binary), *options,
                                      str(fixtures / case / 'main.metta')],
                                     cwd=fixtures, env=environment,
                                     capture_output=True, text=True, timeout=30)
                actual = (run.returncode, run.stdout, run.stderr)
                wanted = (0, stdout, '')
                if actual != wanted:
                    raise AssertionError((options, machine, case, actual, wanted))
                observations += 1
    extension_assertions = 0
    extension_fixtures = Path(__file__).resolve().parents[1] / 'petta'
    for options in (['--lang', 'petta'], ['--lang', 'petta', '--profile', 'extended']):
        cases = {'explicit_library_imports': 16, 'explicit_library_import_failure': 5}
        if '--profile' in options:
            cases['explicit_library_import_spaces'] = 8
        for machine in ('0', '1'):
            for case, assertions in cases.items():
                run = subprocess.run(
                    [str(binary), *options, str(extension_fixtures / (case + '.metta'))],
                    cwd=fixtures, env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine),
                    capture_output=True, text=True, timeout=30)
                if not complete(run, assertions):
                    raise AssertionError((options, machine, case, run.returncode,
                                          run.stdout, run.stderr))
                extension_assertions += assertions
    print(f'PASS: {observations} import observations preserve versioned lookup, '
          'directory restoration, cycles, canonical paths and separate spaces')
    print(f'PASS: {extension_assertions} explicit library assertions preserve inert names, '
          'qualified exports, sequential batches and per-space imports')


if __name__ == '__main__':
    main()
