#!/usr/bin/env python3
"""Check opt-in tensor bounds, typed refusals and authored extensions."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def tensor(shape, values, dtype='f64'):
    return f'(tensor:from-values {dtype} ({shape}) ({values}))'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    binary = parser.parse_args().cetta.resolve()
    vector = tensor('3', '1.0 1.0 1.0')
    left = tensor('3', '1.0e16 1.0 -1.0e16')
    matrix = tensor('2 3', '1.0e16 1.0 -1.0e16 2.0 3.0 4.0')
    cases = [
        f'''(let (Tensor:Bounds $value $lower $upper) (tensor:dot-bounds {left} {vector})
             (and (== $value 0.0) (and (< $lower 1.0) (> $upper 1.0))))''',
        f'''(let (Tensor:Bounds $value $lower $upper) (tensor:dot-bounds {left} {vector})
             (and (< (- $upper $lower) 100.0) (>= (- $upper $lower) 2.0)))''',
        f'''(let (Tensor:Bounds $value $lower $upper)
                 (tensor:dot-bounds {tensor('2', '2.0 3.0', 'f32')} {tensor('2', '4.0 5.0')})
             (and (== $value 23.0) (and (<= $lower 23.0) (>= $upper 23.0))))''',
        f'''(== (tensor:dot-bounds {tensor('0', '')} {tensor('0', '')})
                (Tensor:Bounds 0.0 0.0 0.0))''',
        '(== (tensor:dot-bounds UserValue UserValue) ExtendedBounds)',
        '(== (tensor:matvec-bounds UserValue UserValue) ExtendedBounds)',
        '''(== (tensor:dot-bounds Uninterpreted Uninterpreted)
                (tensor:dot-bounds Uninterpreted Uninterpreted))''',
        '''(== (tensor:observation-descriptor dotBounds)
                (TensorPolicy dotBounds (StoredCoordinateOrder NearestGradualSeparate
                    StoredRealInputEnclosure TypedRefusal)))''',
    ]
    for source in [matrix, f'(tensor:convert {matrix} coo)',
                   f'(tensor:convert {matrix} csr)', f'(tensor:convert {matrix} csc)']:
        cases.append(f'''(let (Tensor:Bounds $value $lower $upper)
                                  (tensor:matvec-bounds {source} {vector})
              (let* (((tensor:present $lo) (tensor:at $lower 0))
                     ((tensor:present $hi) (tensor:at $upper 0)))
                (and (== (tensor:at $value 0) (tensor:present 0.0))
                     (and (<= $lo 1.0) (>= $hi 1.0)))))''')
        cases.append(f'''(let (Tensor:Bounds $value $lower $upper)
                                  (tensor:matvec-bounds {source} {vector})
              (let* (((tensor:present $lo) (tensor:at $lower 1))
                     ((tensor:present $hi) (tensor:at $upper 1)))
                (and (== (tensor:at $value 1) (tensor:present 9.0))
                     (and (<= $lo 9.0) (>= $hi 9.0)))))''')
    cases += [
        f'''(let (Tensor:Bounds $value $lower $upper)
                 (tensor:matvec-bounds {tensor('2 0', '')} {tensor('0', '')})
             (and (== $value {tensor('2', '0.0 0.0')})
                  (and (== $lower $value) (== $upper $value))))''',
        f'''(let (Tensor:Bounds $value $lower $upper)
                 (tensor:matvec-bounds {tensor('0 3', '')} {vector})
             (and (== (tensor:shape $value) (0))
                  (and (== $lower $value) (== $upper $value))))''',
    ]
    failures = [
        (f'(tensor:dot-bounds {tensor("1", "2", "i64")} {tensor("1", "3.0")})', 'unsupported-dtype'),
        (f'(tensor:dot-bounds {tensor("2", "2.0 3.0")} {vector})', 'shape-mismatch'),
        (f'(tensor:matvec-bounds {left} {vector})', 'shape-mismatch'),
        (f'(tensor:matvec-bounds {matrix} (tensor:from-coo f64 (3) ((0 1.0))))', 'shape-mismatch'),
        (f'(tensor:dot-bounds {tensor("1", "1.7976931348623157e308")} {tensor("1", "1.0")})', 'uncertified'),
    ]
    cases += [f'(== {expression} (Tensor:Fault {reason}))' for expression, reason in failures]
    count = 0
    for language in ('petta', 'he'):
        module = '(library tensor)' if language == 'petta' else 'tensor'
        source = f'!(import! &self {module})\n' + '''
(= (tensor:dot-bounds UserValue UserValue) ExtendedBounds)
(= (tensor:matvec-bounds UserValue UserValue) ExtendedBounds)
''' + ''.join('!' + case + '\n' for case in cases)
        truth = 'true' if language == 'petta' else '[True]'
        for ordinary in (False, True):
            env = {k: v for k, v in os.environ.items() if not k.startswith('CETTA_')}
            if ordinary:
                env.update(CETTA_OPEN_EQUATIONS_REFERENCE='1', CETTA_PREPARED_PURE_REFERENCE='1')
            run = subprocess.run([str(binary), '--lang', language, '--profile', 'extended',
                                  '--fuel', '1000000', '-e', source],
                                 cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
            actual = run.stdout.splitlines()[-len(cases):]
            if run.returncode or run.stderr or actual != [truth] * len(cases):
                raise AssertionError((language, ordinary, run.returncode, run.stdout, run.stderr))
            count += len(cases)
            # A native result is already a value even when its head has equations.
            code_shaped = f'''!(import! &self {module})
(= (Tensor:Bounds $value $lower $upper) WrongReduction)
!(let (Tensor:Bounds $value $lower $upper)
      (tensor:dot-bounds {tensor('0', '')} {tensor('0', '')})
    (and (== $value 0.0) (and (== $lower 0.0) (== $upper 0.0))))
'''
            run = subprocess.run([str(binary), '--lang', language, '--profile', 'extended',
                                  '-e', code_shaped], cwd=ROOT, env=env,
                                 capture_output=True, text=True, timeout=60)
            if run.returncode or run.stderr or run.stdout.splitlines()[-1:] != [truth]:
                raise AssertionError((language, ordinary, 'returned value', run.stdout, run.stderr))
            count += 1
    print(f'TensorBoundsWorkflows {count} 0')


if __name__ == '__main__':
    main()
