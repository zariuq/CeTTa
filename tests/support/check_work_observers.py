#!/usr/bin/env python3
"""Check consuming counts and retained existential witnesses on both dialects."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--fixtures', type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    binary, fixtures = args.cetta.resolve(), args.fixtures.resolve()
    observations = 0
    for language in ('he', 'petta'):
        expected = (fixtures / ('test_work_observers.expected' if language == 'he'
                                else 'test_work_observers.petta.expected')).read_text()
        for machine in ('0', '1'):
            run = subprocess.run([str(binary), '--lang', language, '--profile', 'extended',
                                  str(fixtures / 'test_work_observers.metta')],
                                 env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine),
                                 capture_output=True, text=True, timeout=60)
            if (run.returncode, run.stdout, run.stderr) != (0, expected, ''):
                difference = ''.join(difflib.unified_diff(
                    expected.splitlines(True), run.stdout.splitlines(True),
                    fromfile='expected', tofile='actual'))
                raise AssertionError((language, machine, run.returncode,
                                      difference, run.stderr))
            observations += len(expected.splitlines())
    expected = (fixtures / 'he/work_observers/errors.expected').read_text()
    for machine in ('0', '1'):
        run = subprocess.run([str(binary), '--lang', 'he', '--profile', 'extended',
                              str(fixtures / 'he/work_observers/errors.metta')],
                             env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine),
                             capture_output=True, text=True, timeout=60)
        if (run.returncode, run.stdout, run.stderr) != (0, expected, ''):
            raise AssertionError(('he errors', machine, run.returncode,
                                  run.stdout, expected, run.stderr))
        observations += len(expected.splitlines())
    print(f'PASS: {observations} work observations preserve multiplicity, effects, '
          'pending ownership, budgeted residuals and validated factor counts')


if __name__ == '__main__':
    main()
