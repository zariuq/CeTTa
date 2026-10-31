#!/usr/bin/env python3
"""Check canonical finite-fuel admission before equation activation enters."""
import argparse
import os
from pathlib import Path
import subprocess


def expected(fuel):
    # The CLI rejects zero; the native source/physical controls exercise it.
    if fuel == 0:
        return 2, '', "error: invalid fuel '0'\n"
    if fuel < 10:
        return 1, '', 'error: observation incomplete: fuel-exhausted\n'
    if fuel < 19:
        return 1, 'binder-entered\n', 'error: observation incomplete: fuel-exhausted\n'
    return 0, 'binder-entered\n7\n', ''


def complete(result, fuel):
    return (result.returncode, result.stdout, result.stderr) == expected(fuel)


def check_observer():
    for status, out, err, fuel, accepted in (
        (0, 'binder-entered\n7\n', '', 19, True),
        (0, '7\n', '', 19, False),
        (0, 'binder-entered\nbinder-entered\n7\n', '', 19, False),
        (1, 'binder-entered\n7\n', '', 19, False),
        (0, 'binder-entered\n7\n', '', 16, False),
        (1, '', 'error: observation incomplete: fuel-exhausted\n', 10, False),
        (1, 'binder-entered\n', 'error: observation incomplete: fuel-exhausted\n', 10, True),
    ):
        result = subprocess.CompletedProcess([], status, out, err)
        if complete(result, fuel) != accepted:
            raise AssertionError('source-prefix observer accepted a corrupted observation')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cetta', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = args.cetta.resolve()
    source = root / 'tests/petta/source_budget_computed_pattern.metta'
    wanted = source.with_suffix('.expected').read_text()
    check_observer()
    checks = 0
    for materialized in (False, True):
        collection_controls = {'CETTA_TERM_UNIVERSE_COLLECTION', 'CETTA_EVAL_GC',
                               'CETTA_OEM_COLLECT_STRESS'}
        env = {key: value for key, value in os.environ.items()
               if not key.startswith('CETTA_') or key in collection_controls}
        env.update(CETTA_OPEN_EQUATIONS_REFERENCE='1', CETTA_PREPARED_PURE_REFERENCE='1')
        if materialized:
            env['CETTA_PETTA_EQUATION_BODY_ACTIVATION_REFERENCE'] = '1'
        for fuel in range(33):
            result = subprocess.run([str(binary), '--lang', 'petta', '--fuel', str(fuel),
                                     str(source)], cwd=root, env=env, capture_output=True,
                                    text=True, timeout=30)
            if not complete(result, fuel):
                raise AssertionError((materialized, fuel, result.returncode,
                                      result.stdout, result.stderr))
            checks += 1
        result = subprocess.run([str(binary), '--lang', 'petta', str(source)],
                                cwd=root, env=env, capture_output=True, text=True, timeout=30)
        if result.returncode or result.stdout != wanted or result.stderr:
            raise AssertionError(('unbounded', materialized, result.returncode,
                                  result.stdout, result.stderr))
        checks += 1
    print(f'PASS: {checks - 2} computed-pattern source observations and 2 CLI-zero rejections preserve effects and admission')


if __name__ == '__main__':
    main()
