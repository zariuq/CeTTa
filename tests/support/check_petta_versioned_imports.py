#!/usr/bin/env python3
"""Check versioned import resolution, no-op cycles and destination identity."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--fixtures', type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        'petta/language_versions/imports')
    args = parser.parse_args()
    binary = args.cetta.resolve()
    fixtures = args.fixtures.resolve()
    profiles = [(['--lang', 'petta'], False),
                (['--lang', 'petta@1.0.6'], False),
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
    print(f'PASS: {observations} import observations preserve versioned lookup, '
          'directory restoration, cycles, canonical paths and separate spaces')


if __name__ == '__main__':
    main()
