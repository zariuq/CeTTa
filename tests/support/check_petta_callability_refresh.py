#!/usr/bin/env python3
"""Check versioned translation after new MeTTa and foreign definitions."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    fixtures = Path(__file__).resolve().parents[1] / 'petta/language_versions'
    profiles = [(['--lang', 'petta'], False),
                (['--lang', 'petta@1.0.6'], False),
                (['--lang', 'petta@1.1'], True),
                (['--lang', 'petta', '--profile', 'typecheck-v2'], True),
                (['--lang', 'petta', '--profile', 'typecheck-v3'], True)]
    observations = 0
    for options, modern in profiles:
        expected = {
            'late_definition': '(later 41)\ntrue\n42\n' +
                               ('42\n' if modern else '(later 41)\n'),
            # Foreign import makes direct calls available without refreshing
            # the MeTTa caller's earlier translation in either version.
            'late_foreign': '(succ 41)\ntrue\n42\n(succ 41)\n',
        }
        for machine in ('0', '1'):
            environment = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine)
            for case, stdout in expected.items():
                run = subprocess.run([str(binary), *options,
                                      str(fixtures / (case + '.metta'))],
                                     env=environment, capture_output=True,
                                     text=True, timeout=30)
                actual = (run.returncode, run.stdout, run.stderr)
                if actual != (0, stdout, ''):
                    raise AssertionError((options, machine, case, actual, stdout))
                observations += 1
    print(f'PASS: {observations} translation observations preserve the default '
          'and distinguish late MeTTa definitions from foreign imports')


if __name__ == '__main__':
    main()
