#!/usr/bin/env python3
"""Qualify selected corrected-stable rules; open semantics are not this gate."""
import argparse
import os
from pathlib import Path
import subprocess


# Authored assertions in each fixture. A successful prefix is not a passing
# run: every assertion must have executed, on both equation routes.
FIXTURE_ASSERTIONS = {
    'corrected_stable_arithmetic.metta': 25,
    'corrected_stable_builtin_guards.metta': 11,
    'corrected_stable_dynamic_argument_scope.metta': 17,
    'corrected_stable_forward_application_plans.metta': 15,
    'corrected_stable_live_declarations.metta': 21,
    'corrected_stable_memo_definition_dependencies.metta': 13,
    'corrected_stable_named_space_lambda_revision.metta': 10,
    'corrected_stable_term_order.metta': 23,
}


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
    root = Path(__file__).resolve().parents[2]
    fixtures = sorted((root / 'tests/petta').glob('corrected_stable_*.metta'))
    if {source.name for source in fixtures} != set(FIXTURE_ASSERTIONS):
        raise AssertionError('corrected-stable fixture membership changed')
    check_observer()
    count = 0
    for machine in ('0', '1'):
        for source in fixtures:
            result = subprocess.run([str(binary), '--lang', 'petta', str(source)],
                                    cwd=root, capture_output=True, text=True, timeout=30,
                                    env=dict(os.environ, CETTA_PETTA_SEARCH_MACHINE=machine))
            if not complete(result, FIXTURE_ASSERTIONS[source.name]):
                raise AssertionError((machine, source.name, result.returncode,
                                      result.stdout, result.stderr))
            count += result.stdout.count('✅')
    print(f'PASS: {count} settled corrected-stable assertions on both equation routes')


if __name__ == '__main__':
    main()
