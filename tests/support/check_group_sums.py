#!/usr/bin/env python3
"""Ordered keyed reduction versus authored and independent observations."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def sexpr(value):
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, (tuple, list)):
        return '(' + ' '.join(map(sexpr, value)) + ')'
    if isinstance(value, float):
        text=repr(value)
        if 'e' in text and '.' not in text.split('e')[0]:
            text=text.replace('e','.0e')
        return text
    return repr(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    args = parser.parse_args()
    rng = random.Random(21084)
    datasets = [[], [('b', 1), ('a', 3), ('b', 4.5)],
                [('x', 1e16), ('y', 0), ('x', 1.0), ('x', -1e16), ('x', 1.0)],
                [(0, 3), (1, 7), (4294967296, 9), (0, 2)],
                [(1, 3), (1.0, 2), (0.0, 3), (-0.0, 5)]]
    for size in (1, 7, 64, 512, 2048):
        datasets.append([(f'key-{rng.randrange(17)}', rng.randrange(-1000, 1000) / 8)
                         for _ in range(size)])
    checks = 0
    for language, profile in [('petta', None), ('petta', 'extended'),
                              ('he', 'extended'), ('prime', None)]:
        library = '(library list)' if language.startswith('petta') else 'list'
        for reference in (False, True):
            calls = []
            for index, rows in enumerate(datasets):
                data = sexpr(rows)
                large_reference = len(rows)>128 and not language.startswith('petta')
                if not large_reference:
                    calls.append(f'!(let $rows{index} {data} '
                        f'(== (list:group-sums-number $rows{index}) '
                        f'(list:group-sums-number-reference $rows{index})))')
                blocks = [rows[i:i+7] for i in range(0, len(rows), 7)]
                if not large_reference:
                    calls.append(f'!(== (list:group-sums-number-blocks {sexpr(blocks)}) (list:group-sums-number-reference {data}))')
                if index == 4:  # Python merges integer and float keys.
                    continue
                if large_reference and reference:
                    continue
                totals = {}
                for key, value in rows:
                    totals[key] = totals.get(key, 0) + value
                calls.append(f'!(== (list:group-sums-number {data}) {sexpr(list(totals.items()))})')
            for data in ('(("x" 9223372036854775807) ("x" 1))',
                         '(("x" 18446744073709551616))'):
                calls.append(f'!(== (__cetta_lib_list_group_sums {data}) List:GroupSumsFallback)')
            calls.append('!(== (list:group-sums-number (((tuple "x") 2) ((tuple "x") 4))) (((tuple "x") 6)))')
            probe = '!(case (__cetta_lib_list_group_sums (("x" 1))) (((List:GroupedSums $rows) "native") ($other "reference")))\n'
            probe += '!(case (__cetta_lib_list_group_sums_blocks ((("x" 1)) ())) (((List:GroupedSums $rows) "native") ($other "reference")))\n'
            source = f'!(import! &self {library})\n' + probe + '\n'.join(calls)
            env = dict(os.environ, CETTA_GROUP_SUM_REFERENCE='1' if reference else '0')
            command = [str(args.cetta.resolve()), '--lang', language]
            if profile:
                command += ['--profile', profile]
            with tempfile.TemporaryDirectory(prefix='group-sums-', dir=ROOT/'runtime') as directory:
                program=Path(directory)/'controls.metta'
                program.write_text(source)
                result = subprocess.run(command + [str(program)], cwd=ROOT, env=env,
                                        text=True, capture_output=True, timeout=180)
            truth = 'true' if language.startswith('petta') else '[True]'
            lines = result.stdout.splitlines()
            kind = json.dumps('reference' if reference else 'native')
            if not language.startswith('petta'):
                kind = '[' + kind + ']'
            assert result.returncode == 0 and lines[-len(calls):] == [truth]*len(calls), (language, reference, result.stdout[-3000:], result.stderr)
            assert lines[-len(calls)-2:-len(calls)] == [kind]*2, (language, reference, result.stdout[:500])
            checks += len(calls)+2
    # One collection-observation fixture, not another dialect/backend matrix.
    # The hash selector is extended-profile syntax, with occurrence semantics.
    source = (ROOT / 'tests/petta/collection_observation.metta').read_text()
    source += '''\n!(let* (($s (new-space hash))
        ($added (add-atoms $s ((K a) (K a) (K b)))))
      (HashOccurrences (size-atom (collapse (match $s (K $v) $v)))))\n'''
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta',
                             '--profile', 'extended', '-e', source], cwd=ROOT,
                            text=True, capture_output=True, timeout=30)
    expected = (ROOT / 'tests/petta/collection_observation.expected').read_text()
    assert result.returncode == 0 and result.stdout == expected + '(HashOccurrences 3)\n', (
        result.stdout, result.stderr)
    checks += 6
    print(f'GroupedSums {checks} 0')


if __name__ == '__main__':
    main()
