#!/usr/bin/env python3
"""Prepared postings against independent Python occurrence counts."""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sexpr(value):
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, (int, float)):
        return str(value)
    return '(' + ' '.join(map(sexpr, value)) + ')'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    args = parser.parse_args()
    rng = random.Random(810211)
    sets = [[['Empty', [4, 1, 1]], ['zero', []], ['same', [-4, 4]], ['same', [0]]], []]
    for count in (1, 8, 64):
        sets.append([[f'key-{i % 13}', [rng.randint(-100, 100) for _ in range(rng.randint(0, 60))]]
                     for i in range(count)])
    sets += [[['huge', [-2**63, 2**53, 2**53 + 1, 2**63 - 1]]],
             [['float', [1.5, -2.0, 1.5, 5.0]]]]
    calls, expectations = [], []
    for groups in sets:
        counts = []
        for bound in (-2**63, -100, -1, 0, 2, 100, 2**63 - 1, 1.5):
            expected = [[key, sum(x < bound for x in values)] for key, values in groups]
            expected = [row for row in expected if row[1]]
            counts.append(f'(postings:counts-before $p {bound})')
            expectations.append(sexpr(expected))
        expected_all = [[key, len(values)] for key, values in groups if values]
        expected_inspect = [[key, sorted(values)] for key, values in groups]
        expectations += [sexpr(expected_all), sexpr(expected_inspect)]
        calls.append(f'(let $p (postings:prepare {sexpr(groups)}) '
                     f'(Row {" ".join(counts)} (postings:counts $p) (postings:inspect $p)))')
    blocks = [[["a", "b"], 0, 4], [["a"], 0, 1], [["a"], 2, 3],
              [["b", "b"], 1, 2], [[], 0, -1]]
    table_inspection = [["a", [[0, [1, 4]], [2, [3]]]],
                        ["b", [[0, [4]], [1, [2, 2]]]]]
    table_expected = (
        f'(Table ((0 1) (2 1)) ((0 1) (1 2)) '
        f'(TensorData i64 (3) ((1 2))) {sexpr(table_inspection)})')
    table_check = (
        f'!(let $p (postings:prepare-blocks {sexpr(blocks)}) '
        f'(== (Table (postings:table-counts-before $p "a" 4 3) '
        f'(postings:table-counts $p "b" 3) '
        f'(tensor:inspect (postings:table-counts-tensor-before $p "b" 4 3)) '
        f'(postings:inspect-table $p)) {table_expected}))\n')
    table_lifecycle = (
        f'!(let* (($p (postings:prepare-blocks {sexpr(blocks)})) ($alias $p) '
        f'($seen (postings:inspect-table $p)) ($closed (postings:close $p))) '
        f'(== (TableLifecycle $seen (postings:inspect-table $alias)) '
        f'(TableLifecycle {sexpr(table_inspection)} '
        f'(Postings:Fault closed-or-forged-index))))\n')
    table_calls = []
    for block_count in (0, 1, 8, 32):
        random_blocks = []
        for _ in range(block_count):
            keys = [f'outer-{rng.randrange(7)}'
                    for _ in range(rng.randrange(5))]
            random_blocks.append([keys, rng.randrange(8), rng.randrange(-10, 11)])
        key_order = []
        grouped = {}
        for keys, group, position in random_blocks:
            for key in keys:
                if key not in grouped:
                    grouped[key] = {}
                    key_order.append(key)
                grouped[key].setdefault(group, []).append(position)
        inspection = [[key, [[group, sorted(positions)]
                             for group, positions in sorted(grouped[key].items())]]
                      for key in key_order]
        target = key_order[len(key_order) // 2] if key_order else 'absent'
        counts = []
        expected_counts = []
        for bound in (-11, 0, 5, 11):
            rows = [[group, sum(position < bound for position in positions)]
                    for group, positions in sorted(grouped.get(target, {}).items())]
            rows = [row for row in rows if row[1]]
            counts.append(
                f'(postings:table-counts-before $p {json.dumps(target)} {bound} 8)')
            expected_counts.append(rows)
        all_rows = [[group, len(positions)]
                    for group, positions in sorted(grouped.get(target, {}).items())]
        tensor_rows = [[group, sum(position < 5 for position in positions)]
                       for group, positions in sorted(grouped.get(target, {}).items())]
        tensor_rows = [row for row in tensor_rows if row[1]]
        actual = (f'(let $p (postings:prepare-blocks {sexpr(random_blocks)}) '
                  f'(TableCase {" ".join(counts)} '
                  f'(postings:table-counts $p {json.dumps(target)} 8) '
                  f'(tensor:inspect (postings:table-counts-tensor-before '
                  f'$p {json.dumps(target)} 5 8)) (postings:inspect-table $p)))')
        expected = ('(TableCase ' + ' '.join(map(sexpr, expected_counts)) + ' ' +
                    sexpr(all_rows) + ' (TensorData i64 (8) ' +
                    sexpr(tensor_rows) + ') ' + sexpr(inspection) + ')')
        table_calls.append((actual, expected))
    # Equality is checked by the language; no parser normalization can hide
    # duplicates, group order, exact integer spelling or an extra answer.
    expected_rows = ['(Row ' + ' '.join(expectations[i:i + 10]) + ')'
                     for i in range(0, len(expectations), 10)]
    total = 0
    for language, profile in [('petta@1.0.6', None), ('petta@1.0.6', 'extended'),
                              ('he', 'extended'), ('prime', None)]:
        for reference in (False, True):
            module = '(library postings)' if language.startswith('petta') else 'postings'
            kind = 'reference' if reference else 'native'
            kind_expected = json.dumps(kind) if language.startswith('petta') else '[' + json.dumps(kind) + ']'
            source = (f'!(import! &self {module})\n'
                      '!(let $p (postings:prepare (("kind" (0)))) '
                      '(case (postings:try-counts-before $p 1) ((Postings:Fallback "reference") '
                      '($other "native"))))\n'
                      + ''.join(f'!(let $actual {body} (== $actual {expected}))\n'
                                for body, expected in zip(calls, expected_rows)))
            source += table_check + table_lifecycle
            source += ''.join(
                f'!(let $actual {body} (== $actual {expected}))\n'
                for body, expected in table_calls)
            lifecycle='''!(let* (($p (postings:prepare (("owned" (1.5 2.0 2.0)))))
                 ($alias $p) ($seen (postings:inspect $p)) ($closed (postings:close $p)))
               (== (Lifecycle $seen (postings:inspect $alias))
                   (Lifecycle (("owned" (1.5 2.0 2.0))) (Postings:Fault closed-or-forged-index))))\n'''
            source+=lifecycle
            env = dict(os.environ, CETTA_POSTINGS_REFERENCE='1' if reference else '0')
            command = [str(args.cetta.resolve()), '--lang', language]
            if profile:
                command += ['--profile', profile]
            run = subprocess.run(command + ['-e', source], cwd=ROOT, env=env,
                                 capture_output=True, text=True, timeout=90)
            assert run.returncode == 0, (language, reference, run.stdout[-2000:], run.stderr)
            expected_true = 'true' if language.startswith('petta') else '[True]'
            true_count = len(calls) + 3 + len(table_calls)
            assert run.stdout.splitlines()[-true_count:] == [expected_true] * true_count, (language, reference, run.stdout[-2000:])
            assert run.stdout.splitlines()[-true_count - 1] == kind_expected, (language, reference, run.stdout[:500])
            total += len(expectations) + 4 + len(table_calls)

    # One primary-dialect control covers the borrowed-space attempt itself.
    # The surrounding matrix already covers the table observation and every
    # generated library spelling; repeating this source-space setup there
    # would add variants without adding a new invariant.
    space_source = '''!(import! &self (library postings))
!(let* (($source (new-space))
        ($added (add-atoms $source
          ((Block ("a" "a") 2 4) (Other ignored) (Block ("b") 1 3))))
        ($attempt (postings:prepare-space-blocks-attempt $source
          (Postings:BlockLayout Block 4 1 2 3 ("base") (Overlay)))))
   (case $attempt
     (((NativeHandle "postings-table-v1" $id)
        (== (postings:inspect-table $attempt)
          (("base" ((1 (3)) (2 (4))))
           ("a" ((2 (4 4)))) ("b" ((1 (3)))))))
      ($other False))))
!(let* (($source (new-space))
        ($added (add-atoms $source
          ((Block ("a") 2 4) (Overlay "a" 2 5)))))
   (== (postings:prepare-space-blocks-attempt $source
         (Postings:BlockLayout Block 4 1 2 3 () (Overlay)))
       Postings:Fallback))
!(let* (($source (new-space))
        ($added (add-atom $source (Block ("before") 0 1)))
        ($snapshot (postings:prepare-space-blocks-attempt $source
          (Postings:BlockLayout Block 4 1 2 3 () (Overlay))))
        ($mutated (add-atom $source (Block ("after") 0 2))))
   (== (postings:inspect-table $snapshot) (("before" ((0 (1)))))))
'''
    # Exact constructor matching ignores a wrong-width row; the optimized
    # attempt must decline before publishing, then the authored projection
    # observes only valid rows. Both shorter and longer rows are adversarial.
    for row in ['(Block ("bad") 0)', '(Block ("bad") 0 1 extra)',
                '(Block ("bad") 0 1 extra more)']:
        space_source += f'''!(let* (($s (new-space))
          ($added (add-atoms $s ((Block ("ok") 2 3) {row})))
          ($attempt (postings:prepare-space-blocks-attempt $s
            (Postings:BlockLayout Block 4 1 2 3 () ())))
          ($ordinary (postings:prepare-blocks
            (collapse (match $s (Block $keys $group $position)
              ($keys $group $position))))))
          (== (Observation $attempt (postings:inspect-table $ordinary))
              (Observation Postings:Fallback (("ok" ((2 (3))))))))\n'''
    for layout in ['(Postings:BlockLayout Block 4 4 2 3 () ())',
                   '(Postings:BlockLayout Block 0 1 2 3 () ())',
                   '(Postings:BlockLayout Block -1 1 2 3 () ())',
                   '(Postings:BlockLayout Block 4 1 2 3 () () extra)']:
        space_source += f'''!(let $s (new-space)
          (== (postings:prepare-space-blocks-attempt $s {layout})
            Postings:Fallback))\n'''
    run = subprocess.run(
        [str(args.cetta.resolve()), '--lang', 'petta@1.0.6', '-e', space_source],
        cwd=ROOT, capture_output=True, text=True, timeout=30)
    assert run.returncode == 0, (run.stdout, run.stderr)
    assert run.stdout.splitlines()[-10:] == ['true'] * 10, run.stdout

    for extra, environment in [([], dict(os.environ, CETTA_POSTINGS_SPACE_REFERENCE='1')),
                               (['--fuel', '100000'], os.environ.copy())]:
        run = subprocess.run(
            [str(args.cetta.resolve()), *extra, '--lang', 'petta@1.0.6', '-e',
             '''!(import! &self (library postings))
!(let* (($source (new-space))
        ($added (add-atom $source (Block ("a") 2 4))))
   (== (postings:prepare-space-blocks-attempt $source
         (Postings:BlockLayout Block 4 1 2 3 () ()))
       Postings:Fallback))
'''], cwd=ROOT, env=environment, capture_output=True, text=True, timeout=30)
        assert run.returncode == 0, (extra, run.stdout, run.stderr)
        assert run.stdout.splitlines()[-1:] == ['true'], (extra, run.stdout)
    total += 12
    print(f'Postings {total} 0')


if __name__ == '__main__':
    main()
