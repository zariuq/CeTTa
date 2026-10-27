#!/usr/bin/env python3
"""Owner-bound numerical screening against ordinary MeTTa composition."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    matrix = '(tensor:from-values f64 (3 2) (10.0 1.0 3.0 -2.0 -5.0 1.0))'
    query = '(tensor:from-values f64 (2) (1.0 2.0))'
    ties = '(tensor:from-values f64 (3 2) (1.0 2.0 1.0 2.0 0.0 0.0))'
    matrices = [
        matrix, ties,
        '(tensor:from-values f32 (3 2) (10.0 1.0 3.0 -2.0 -5.0 1.0))',
        '(tensor:from-coo f64 (3 2) ((0 -0.0) (3 2.0) (5 -1.0)))',
        '(tensor:from-values f64 (3 2) (1.0e200 0.0 -1.0e200 0.0 0.0 0.0))',
        f'(tensor:view {matrix} (3 2) (-2 1) 4)',
    ]
    cases = []
    masks = ['all', '(tensor:from-values bool (3) (False True True))',
             '(tensor:from-values bool (3) (False False False))',
             '(tensor:from-coo bool (3) ((1 True) (2 False)))']
    for source in matrices:
        for k in (0, 1, 2, 99):
            cases.append(f'''(let* (($matrix {source}) ($query {query})
                ($index (tensor:screen-index $matrix))
                ($receipt (tensor:screen $index $query {k}))
                ($expected (screen:test-reference $matrix $query {k})))
              (== (tensor:rerank $receipt) $expected))''')
            for mask in masks:
                cases.append(f'''(let* (($matrix {source}) ($query {query}) ($mask {mask})
                    ($index (tensor:screen-index $matrix))
                    ($receipt (tensor:screen $index $query $mask {k}))
                    ($expected (screen:test-masked-reference $matrix $query $mask {k})))
                  (== (tensor:rerank $receipt) $expected))''')
    cases += [
        '''(let* (($matrix (tensor:from-coo f64 (1000000000 1) ((999999999 4.0))))
              ($query (tensor:from-values f64 (1) (2.0)))
              ($mask (tensor:from-coo bool (1000000000) ((999999999 True))))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query $mask 1))
              ((TensorRanking Exact $ids $scores) (tensor:rerank $receipt)))
            (and (== (tensor:at $ids 0) (tensor:present 999999999))
                 (== (tensor:at $scores 0) (tensor:present 8.0))))''',
        '''(let* (($matrix (tensor:from-values f64 (1000000000 0) ()))
              ($query (tensor:from-values f64 (0) ()))
              ($mask (tensor:from-coo bool (1000000000) ((999999999 True))))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query $mask 1))
              ((TensorRanking Exact $ids $scores) (tensor:rerank $receipt)))
            (and (== (tensor:at $ids 0) (tensor:present 999999999))
                 (== (tensor:at $scores 0) (tensor:present 0.0))))''',
        '(== (tensor:screen-index UserExtension) ExtendedIndex)',
        '(== (tensor:rerank UserExtension) ExtendedRanking)',
        f'''(let* (($matrix {matrix}) ($query {query})
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 1))
              ($closed (tensor:close $index)))
            (== (tensor:rerank $receipt) (screen:test-reference $matrix $query 1)))''',
        f'''(let* (($matrix {matrix}) ($query {query})
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 1)))
            (and (== (tensor:index-source $index) $matrix)
                 (== (tensor:index-source $receipt) $matrix)))''',
        f'''(let* (($matrix {matrix}) ($query {query})
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 1))
              ((TensorScreenInfo $guarantee $profile $k $source $q $mask $ids $lower $upper)
                (tensor:screen-info $receipt)))
            (and (and (== $k 1) (== $source $matrix))
                 (and (and (== $q $query) (== $mask all))
                      (== $ids (tensor:screen-candidates $receipt)))))''',
        f'''(let* (($matrix {matrix}) ($query {query})
              ($mask (tensor:from-values bool (3) (False True True)))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query $mask 2))
              ((TensorScreenInfo $g $p $k $source $q $held-mask $ids $lo $hi)
                (tensor:screen-info $receipt)))
            (and (== $held-mask $mask)
                 (== (tensor:rerank $receipt)
                     (screen:test-masked-reference $matrix $query $mask 2))))''',
        f'''(let* (($matrix {matrix}) ($index (tensor:screen-index $matrix))
              ($one (tensor:from-values f64 (2) (1.0 0.0)))
              ($two (tensor:from-values f64 (2) (0.0 1.0)))
              ($first (tensor:screen $index $one 1))
              ($second (tensor:screen $index $two 1)))
            (and (== (tensor:rerank $first) (screen:test-reference $matrix $one 1))
                 (== (tensor:rerank $second) (screen:test-reference $matrix $two 1))))''',
        '''(let* (($matrix (tensor:from-values f64 (0 2) ()))
              ($query (tensor:from-values f64 (2) (1.0 2.0)))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 1)))
            (== (tensor:rerank $receipt) (screen:test-reference $matrix $query 1)))''',
        '''(let* (($matrix (tensor:from-values f64 (3 0) ()))
              ($query (tensor:from-values f64 (0) ()))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 2)))
            (== (tensor:rerank $receipt) (screen:test-reference $matrix $query 2)))''',
        '''(let* (($matrix (tensor:from-values f64 (3 1) (1.0e-200 2.0e-200 -1.0e-200)))
              ($query (tensor:from-values f64 (1) (1.0e-110)))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 2)))
            (== (tensor:rerank $receipt) (screen:test-reference $matrix $query 2)))''',
        f'''(let* (($matrix {matrix}) ($query (tensor:from-values f64 (2) (-0.0 0.0)))
              ($index (tensor:screen-index $matrix))
              ($receipt (tensor:screen $index $query 1)))
            (== (tensor:rerank $receipt) (screen:test-reference $matrix $query 1)))''',
    ]
    failures = [
        ('(tensor:screen-index (tensor:from-values f64 (1) (1.0)))', '(Tensor:Fault shape-mismatch)'),
        ('(tensor:screen-index (tensor:from-values i64 (1 1) (1)))', '(Tensor:Fault unsupported-dtype)'),
        (f'''(let $index (tensor:screen-index {matrix})
             (tensor:screen $index (tensor:from-values f64 (1) (1.0)) 1))''', '(Tensor:Fault shape-mismatch)'),
        (f'''(let $index (tensor:screen-index {matrix})
             (tensor:screen $index {query} -1))''', '(Tensor:Fault invalid-value)'),
        (f'''(let $index (tensor:screen-index {matrix})
             (tensor:screen $index {query} (tensor:from-values bool (2) (True False)) 1))''',
         '(Tensor:Fault shape-mismatch)'),
        (f'''(let $index (tensor:screen-index {matrix})
             (tensor:screen $index {query} (tensor:from-values u8 (3) (1 0 1)) 1))''',
         '(Tensor:Fault shape-mismatch)'),
        (f'''(let* (($index (tensor:screen-index {matrix})) ($alias $index)
                   ($closed (tensor:close $alias)))
             (tensor:screen $index {query} 1))''', '(Tensor:Fault closed-screen-index)'),
        (f'''(let* (($index (tensor:screen-index {matrix}))
                   ($receipt (tensor:screen $index {query} 1))
                   ($alias $receipt) ($closed (tensor:close $alias)))
             (tensor:rerank $receipt))''', '(Tensor:Fault closed-screen-receipt)'),
        ('(tensor:screen-index unrecognized)', '(tensor:screen-index unrecognized)'),
        ('(tensor:screen unrecognized unrecognized 1)', '(tensor:screen unrecognized unrecognized 1)'),
        ('(tensor:screen unrecognized unrecognized unrecognized 1)',
         '(tensor:screen unrecognized unrecognized unrecognized 1)'),
        ('(tensor:screen-info unrecognized)', '(tensor:screen-info unrecognized)'),
        ('(tensor:screen-candidates unrecognized)', '(tensor:screen-candidates unrecognized)'),
        ('(tensor:rerank unrecognized)', '(tensor:rerank unrecognized)'),
        ('(tensor:rerank (TensorScreenInfo CompleteSuperset fake 1 source query mask candidates lower upper))',
         '(tensor:rerank (TensorScreenInfo CompleteSuperset fake 1 source query mask candidates lower upper))'),
    ]
    checks = 0
    for language, profile in (('petta', None), ('petta', 'extended'),
                              ('he', 'extended'), ('prime', None)):
        petta = language.startswith('petta')
        module = '(library tensor)' if petta else 'tensor'
        prefix = f'!(import! &self {module})\n' + '''
(= (tensor:screen-index UserExtension) ExtendedIndex)
(= (tensor:rerank UserExtension) ExtendedRanking)
(= (screen:test-reference $matrix $query $k)
   (screen:test-masked-reference $matrix $query all $k))
(= (screen:test-masked-reference $matrix $query $mask $k)
   (let* (($scores (tensor:matvec $matrix $query sequential))
          ($indices (tensor:top-k $scores $mask $k)))
     (TensorRanking Exact $indices (tensor:gather $scores $indices))))
'''
        command = [str(binary), '--lang', language]
        if profile:
            command += ['--profile', profile]
        truth = 'true' if petta else '[True]'
        source = prefix + ''.join('!' + case + '\n' for case in cases)
        for reference, fuel in (('0', False), ('1', False), ('0', True)):
            env = dict(os.environ, CETTA_TENSOR_SCREEN_REFERENCE=reference)
            result = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) + ['-e', source],
                cwd=ROOT, env=env, text=True, capture_output=True, timeout=120)
            assert result.returncode == 0 and result.stdout.splitlines()[-len(cases):] == [truth] * len(cases), (
                language, reference, fuel, result.stdout[-3000:], result.stderr[-1000:])
            checks += len(cases)
        for expression, expected in failures:
            result = subprocess.run(command + ['-e', prefix +
                f'!(let $actual {expression} (== $actual {expected}))'],
                cwd=ROOT, text=True, capture_output=True, timeout=120)
            assert result.returncode == 0 and result.stdout.splitlines()[-1] == truth, (
                language, expression, result.stdout, result.stderr)
            checks += 1
        # This detects a vacuous always-full-scan implementation.
        for reference in ('0', '1'):
            count = 3 if reference == '1' else 1
            guarantee = 'Unscreened' if reference == '1' else 'CompleteSuperset'
            profile_name = 'FullScanFallback' if reference == '1' else 'NearestGradualSequential'
            expression = f'''(let* (($index (tensor:screen-index {matrix}))
                ($receipt (tensor:screen $index {query} 1))
                ((TensorScreenInfo $g $p $k $source $query $mask $ids $lower $upper)
                  (tensor:screen-info $receipt)))
              (and (and (== $g {guarantee}) (== $p {profile_name}))
                   (== (tensor:shape $ids) ({count}))))'''
            result = subprocess.run(command + ['-e', prefix + '!' + expression],
                cwd=ROOT, env=dict(os.environ, CETTA_TENSOR_SCREEN_REFERENCE=reference),
                text=True, capture_output=True, timeout=120)
            assert result.returncode == 0 and result.stdout.splitlines()[-1] == truth, (
                language, reference, result.stdout, result.stderr)
            checks += 1
    print(f'TensorScreeningWorkflows {checks} 0')


if __name__ == '__main__':
    main()
