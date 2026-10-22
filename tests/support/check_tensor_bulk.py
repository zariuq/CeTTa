#!/usr/bin/env python3
"""Packed observations and callback fusion against the authored evaluator."""
import argparse
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    args = parser.parse_args()
    binary = args.cetta.resolve()
    a = '(tensor:from-values f64 (3) (1.0 -0.0 3.0))'
    b = '(tensor:from-values i64 (3) (4 5 6))'
    sparse = '(tensor:from-coo f64 (4) ((1 -0.0) (3 7.0)))'
    cases = [
        (f'(tensor:inspect (tensor:zip-map f64 ({a}) bulk:square ()))',
         '(TensorData f64 (3) ((0 1.0) (1 0.0) (2 9.0)))'),
        (f'(tensor:inspect (tensor:zip-map f64 ({a} {b}) bulk:affine (2.0)))',
         '(TensorData f64 (3) ((0 6.0) (1 5.0) (2 12.0)))'),
        (f'(tensor:layout (tensor:zip-map f64 ({a}) bulk:square ()))', 'coo'),
        (f'(tensor:encode (tensor:zip-map f64 ({a}) bulk:square ()))', '(Tensor:Fault invalid-encoding)'),
        (f'(let (TensorEntries $keys $values) (tensor:entries {sparse}) '
         '(Entries (tensor:inspect $keys) (tensor:inspect $values)))',
         '(Entries (TensorData index (2) ((0 1) (1 3))) (TensorData f64 (2) ((0 -0.0) (1 7.0))))'),
        (f'(tensor:inspect (tensor:gather {sparse} (tensor:from-values index (4) (3 0 1 3))))',
         '(TensorData f64 (4) ((0 7.0) (2 -0.0) (3 7.0)))'),
        (f'(tensor:gather {sparse} (tensor:from-values index (1) (4)))', '(Tensor:Fault index-out-of-bounds)'),
        (f'(tensor:inspect (tensor:merge {sparse} {sparse} sum))',
         '(TensorData f64 (4) ((1 -0.0) (3 14.0)))'),
        (f'(tensor:merge {sparse} {sparse} reject)', '(Tensor:Fault duplicate-coordinate)'),
        (f'(tensor:inspect (tensor:merge {sparse} {sparse} first))',
         '(TensorData f64 (4) ((1 -0.0) (3 7.0)))'),
        (f'(tensor:inspect (tensor:zip-map f64 ({sparse}) bulk:square ()))',
         '(TensorData f64 (4) ((1 0.0) (3 49.0)))'),
        ('(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (0) ())) bulk:square ()))',
         '(TensorData f64 (0) ())'),
        ('(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 () (2.0))) bulk:square ()))',
         '(TensorData f64 () ((0 4.0)))'),
        (f'(tensor:zip-map f64 ({a} (tensor:from-values f64 (1) (1.0))) bulk:add ())',
         '(Tensor:Fault shape-mismatch)'),
        (f'(tensor:zip-map f64 ({sparse} (tensor:from-coo f64 (4) ((0 1.0) (3 2.0)))) bulk:add ())',
         '(Tensor:Fault shape-mismatch)'),
        (f'(tensor:inspect (tensor:zip-map f64 ({a}) bulk:typed ()))',
         '(TensorData f64 (3) ((0 2.0) (1 0.0) (2 6.0)))'),
        ('(tensor:gather unrecognized unrecognized)', '(tensor:gather unrecognized unrecognized)'),
        ('(tensor:merge unrecognized unrecognized sum)', '(tensor:merge unrecognized unrecognized sum)'),
        ('(let $p (postings:prepare-events ((3 5) (1 2) (3 1) (1 2) (3 1))) '
         '(let $seen (postings:inspect $p) (let $counts (postings:counts-tensor-before $p 3 4) '
         '(let $_ (postings:close $p) (Counts $seen (tensor:inspect $counts))))))',
         '(Counts ((1 (2 2)) (3 (1 1 5))) (TensorData i64 (4) ((1 2) (3 2))))'),
    ]
    definitions = '''
(= (bulk:square $x) (* $x $x))
(= (bulk:add $x $y) (+ $x $y))
(= (bulk:affine $factor $x $y) (+ (* $factor $x) $y))
(: bulk:typed (-> Number Number))
(= (bulk:typed $x) (* $x 2.0))
(= (bulk:choose $x) (+ $x 1.0))
(= (bulk:choose $x) (- $x 1.0))
(= (bulk:effect $x) (let $_ (println! "BulkEffect") (* $x 2.0)))
(= (bulk:log $base $x) (log-math $base $x))
(= (bulk:poly $a $b $x) (+ (* $a $x) $b))
'''
    checks = 0
    for language in ('petta@1.0.6', 'he', 'prime'):
        module = '(library tensor)' if language.startswith('petta') else 'tensor'
        postings = '(library postings)' if language.startswith('petta') else 'postings'
        prefix = f'!(import! &self {module})\n!(import! &self {postings})\n' + definitions
        command = [str(binary), '--lang', language]
        if language == 'he': command += ['--profile', 'extended']
        source = prefix + ''.join(f'!(let $actual {body} (== $actual {expected}))\n' for body, expected in cases)
        for reference in ('0', '1'):
            env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference, CETTA_POSTINGS_REFERENCE=reference)
            result = subprocess.run(command + ['-e', source], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=180)
            truth = 'true' if language.startswith('petta') else '[True]'
            assert result.returncode == 0 and result.stdout.splitlines()[-len(cases):] == [truth] * len(cases), (
                language, reference, result.stdout[-3000:], result.stderr[-2000:])
            checks += len(cases)
        for name, extra in (
            ('choices', '!(collapse (tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (2) (1.0 2.0))) bulk:choose ())))'),
            ('effects', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (2) (1.0 2.0))) bulk:effect ()))'),
            ('logarithms', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (4) (0.5 1.0 2.0 4.0))) bulk:log (2.0)))'),
            ('domain', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (2) (1.0 -1.0))) bulk:log (2.0)))'),
        ):
            observations = []
            for reference, fuel in (('0', False), ('1', False), ('0', True)):
                env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference)
                r = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) + ['-e', prefix + extra],
                                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
                observations.append((r.returncode, r.stdout, r.stderr))
            assert observations[0] == observations[1] == observations[2], (language, name, observations)
            if name == 'effects': assert observations[0][1].count('BulkEffect') == 2
            if name != 'domain': assert observations[0][0] == 0 and observations[0][1].splitlines()[-1]
            checks += 3
        # CTN1 records scalar bits. Observing the extracted dense values
        # compares signed zero and small magnitudes without text tolerances.
        rng = random.Random(47019)
        numbers = [-0.0, 0.0, 1e-300, -1e-300] + [
            rng.uniform(-10000, 10000) for _ in range(124)]
        def metta_float(value):
            mantissa, *exponent = repr(value).split('e')
            if '.' not in mantissa:
                mantissa += '.0'
            return mantissa + ('e' + exponent[0] if exponent else '')
        literal = '(' + ' '.join(metta_float(x) for x in numbers) + ')'
        bits = prefix + f'''!(let $mapped
          (tensor:zip-map f64 ((tensor:from-values f64 ({len(numbers)}) {literal}))
            bulk:poly (-0.25 -0.0))
          (let (TensorEntries $coordinates $values) (tensor:entries $mapped)
            (tensor:encode $values)))'''
        observations = []
        for reference in ('0', '1'):
            env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference)
            if language == 'prime' and reference == '1':
                # The authored callback must remain collectible without a
                # borrowed evaluation-Space hidden in its persistent Need
                # path. This budget crosses the formerly failing safe point.
                env['CETTA_GC_BUDGET_MB'] = '1'
            r = subprocess.run(command + ['-e', bits], cwd=ROOT, env=env,
                               capture_output=True, timeout=180)
            assert r.returncode == 0 and b'CTN1' in r.stdout, (language, r.stdout, r.stderr)
            observations.append(r.stdout)
        assert observations[0] == observations[1], (language, 'scalar-bit-patterns')
        checks += len(numbers)
    print(f'TensorBulk {checks} 0')


if __name__ == '__main__': main()
