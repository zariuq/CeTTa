#!/usr/bin/env python3
"""Qualify selected corrected-stable rules; open semantics are not this gate."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from gen_semiring_fixtures import is_form, parse_forms


def expected_assertions(source):
    forms = parse_forms(source.read_text())
    count = sum(1 for index, form in enumerate(forms)
                if index and forms[index - 1] == '!' and is_form(form, 'test'))
    if not count:
        raise AssertionError(('no top-level test assertions', source.name))
    return count


def check_result(source, machine, expected, result):
    actual = result.stdout.count('✅')
    if (result.returncode or result.stderr or '❌' in result.stdout
            or actual != expected):
        raise AssertionError((machine, source.name, 'assertions', actual, expected,
                              result.returncode, result.stdout, result.stderr))
    return actual
def complete(result, expected):
    return (result.returncode == 0 and not result.stderr and
            '❌' not in result.stdout and result.stdout.count('✅') == expected)


def check_observer():
    for code, stdout, stderr, wanted in (
        (0, 'first ✅\nsecond ✅\n', '', True),
        (0, 'first ✅\n', '', False),
        (0, '', '', False),
        (0, '✅ ✅ ✅', '', False),
        (0, '✅ ✅ ❌', '', False),
        (2, '✅ ✅', '', False),
        (0, '✅ ✅', 'incomplete', False),
    ):
        result = subprocess.CompletedProcess([], code, stdout, stderr)
        if complete(result, 2) != wanted:
            raise AssertionError('corrected-stable assertion observer accepted a faulty run')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    root = ROOT
    fixtures = sorted((root / 'tests/petta').glob('corrected_stable_*.metta'))
    check_observer()
    count = 0
    for machine in ('0', '1'):
        for source in fixtures:
            expected = expected_assertions(source)
            result = subprocess.run([str(binary), '--lang', 'petta', str(source)],
                                    cwd=root, capture_output=True, text=True, timeout=30,
                                    env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine))
            count += check_result(source, machine, expected, result)
    print(f'PASS: {count} settled corrected-stable assertions on both equation routes')


if __name__ == '__main__':
    main()
