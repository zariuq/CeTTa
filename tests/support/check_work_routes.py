#!/usr/bin/env python3
"""Check retained prefixes, fair residual transfer and non-destructive refusal."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    parser.add_argument('--fixtures', type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    fixtures = args.fixtures.resolve()
    checks = 0
    for language in ('petta', 'he'):
        expected = (fixtures / ('test_work_routes.petta.expected' if language == 'petta'
                                else 'test_work_routes.expected')).read_text()
        for outer_machine in ('0', '1'):
            run = subprocess.run([str(args.cetta.resolve()), '--lang', language,
                                  '--profile', 'extended',
                                  str(fixtures / 'test_work_routes.metta')],
                                 cwd=fixtures,
                                 env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=outer_machine),
                                 capture_output=True, text=True, timeout=60)
            if (run.returncode, run.stdout, run.stderr) != (0, expected, ''):
                sys.stderr.writelines(difflib.unified_diff(
                    expected.splitlines(keepends=True), run.stdout.splitlines(keepends=True),
                    fromfile='expected', tofile='actual'))
                sys.stderr.write(run.stderr)
                raise AssertionError(
                    f'{language}, outer machine {outer_machine}: exit {run.returncode}')
            checks += len(expected.splitlines())
    print(f'PASS: {checks} retained-work observations preserve prefixes, duplicates, '
          'deferred effects and refusal while a transferred frontier reaches its witness')


if __name__ == '__main__':
    main()
