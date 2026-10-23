#!/usr/bin/env python3
"""Packed observations and callback fusion against the authored evaluator."""
import argparse
import os
from pathlib import Path
import random
import re
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
    boolean_inputs = ('((tensor:from-values f64 (4) (0.0 0.0 1.0 1.0)) '
                      '(tensor:from-values i64 (4) (0 1 0 1)))')
    boolean_expected = {
        'and': '(TensorData f64 (4) ((0 0.0) (1 0.0) (2 0.0) (3 1.0)))',
        'or': '(TensorData f64 (4) ((0 0.0) (1 1.0) (2 1.0) (3 1.0)))',
    }
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
    cases += [(f'(tensor:inspect (tensor:zip-map f64 {boolean_inputs} bulk:bool-{name} ()))',
               expected) for name, expected in boolean_expected.items()]
    intersection_input = '(tensor:from-values i64 (4) (0 1 2 3))'
    intersection_expected = '(TensorData f64 (4) ((0 0.0) (1 1.0) (2 0.0) (3 1.0)))'
    cases += [(f'(tensor:inspect (tensor:zip-map f64 ({intersection_input}) bulk:intersection-size ()))',
               intersection_expected)]
    index_values = '(0 9223372036854775807 9223372036854775808 1 18446744073709551615 7 0)'
    index_input = f'(tensor:from-values index (7) {index_values})'
    index_expected = '(TensorData f64 (7) ((0 0.0) (1 2.0) (2 3.0) (3 1.0) (4 3.0) (5 2.0) (6 0.0)))'
    cases += [
        (f'(tensor:inspect (tensor:zip-map f64 ({index_input}) bulk:index-code ()))', index_expected),
        ('(tensor:inspect (tensor:zip-map f64 '
         '((tensor:view (tensor:from-values index (3) (0 1 100)) (3) (-1) 2)) bulk:index-code ()))',
         '(TensorData f64 (3) ((0 2.0) (1 1.0) (2 0.0)))'),
        ('(tensor:inspect (tensor:zip-map f64 '
         '((tensor:from-coo index (4) ((1 0) (3 18446744073709551615)))) bulk:index-code ()))',
         '(TensorData f64 (4) ((1 0.0) (3 3.0)))'),
        ('(tensor:inspect (tensor:zip-map f64 '
         '((tensor:from-values index () (18446744073709551615))) bulk:index-code ()))',
         '(TensorData f64 () ((0 3.0)))'),
        ('(tensor:inspect (tensor:zip-map f64 ((tensor:from-values index (0) ())) bulk:index-code ()))',
         '(TensorData f64 (0) ())'),
        ('(tensor:callback-input-descriptor index)', '(TensorCallbackInput index Unsigned64Atom)'),
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
(= (bulk:index-code $x)
   (if (== $x 0) 0.0 (if (== $x 1) 1.0 (if (<= $x 9223372036854775807) 2.0 3.0))))
(= (bulk:identity $x) $x)
(= (bulk:intersection-size $x)
   (let $singleton (cons-atom $x ())
     (let $common (intersection-atom $singleton (1 3)) (size-atom $common))))
(= (bulk:intersection-inline $x) (size-atom (intersection-atom (cons-atom $x ()) (1 3))))
(= (bulk:intersection-effect $x)
   (size-atom (intersection-atom () (let $_ (println! "HeldOperandEffect") (cons-atom $x ())))))
(= (bulk:bool-and $x $y) (if (and (> $x 0.0) (> $y 0)) 1.0 0.0))
(= (bulk:bool-or $x $y) (if (or (> $x 0.0) (> $y 0)) 1.0 0.0))
(= (bulk:bool-effect $x) (let $_ (println! "BulkBooleanEffect") (> $x 0.0)))
(= (bulk:and-effect $x) (if (and False (bulk:bool-effect $x)) 1.0 0.0))
(= (bulk:or-effect $x) (if (or True (bulk:bool-effect $x)) 1.0 0.0))
(= (bulk:bool-choice $x) True)
(= (bulk:bool-choice $x) False)
(= (bulk:and-choice $x) (if (and False (bulk:bool-choice $x)) 1.0 0.0))
(= (bulk:or-choice $x) (if (or True (bulk:bool-choice $x)) 1.0 0.0))
(= (bulk:and-invalid $x) (if (and False (+ $x 1.0)) 1.0 0.0))
(= (bulk:or-invalid $x) (if (or True (+ $x 1.0)) 1.0 0.0))
(= (bulk:and-empty $x) (if (and False (superpose ())) 1.0 0.0))
(= (bulk:or-empty $x) (if (or True (superpose ())) 1.0 0.0))
'''
    checks = 0
    for language in ('petta@1.0.6', 'he', 'prime'):
        module = '(library tensor)' if language.startswith('petta') else 'tensor'
        postings = '(library postings)' if language.startswith('petta') else 'postings'
        prefix = f'!(import! &self {module})\n!(import! &self {postings})\n' + definitions
        command = [str(binary), '--lang', language]
        if language == 'he': command += ['--profile', 'extended']
        for reference in ('0', '1'):
            active_cases = list(cases)
            if reference == '0':
                # Equal answers alone do not establish that packed execution was admitted.
                active_cases += [
                    ('(let $left (tensor:from-values f64 (4) (0.0 0.0 1.0 1.0)) '
                     '(let $right (tensor:from-values i64 (4) (0 1 0 1)) '
                     '(case (__cetta_lib_tensor_try_zip_map f64 ($left $right) '
                     f'bulk:bool-{name} ()) '
                     '(((Tensor:Mapped $native) (tensor:inspect $native)) ($other $other)))))',
                     expected) for name, expected in boolean_expected.items()]
                active_cases += [
                    (f'(let $input {intersection_input} '
                     '(case (__cetta_lib_tensor_try_zip_map f64 ($input) bulk:intersection-size ()) '
                     '(((Tensor:Mapped $native) (tensor:inspect $native)) ($other $other))))',
                     intersection_expected if language.startswith('petta') else 'Tensor:Fallback')]
                active_cases += [
                    (f'(let $input {index_input} '
                     '(case (__cetta_lib_tensor_try_zip_map f64 ($input) bulk:index-code ()) '
                     '(((Tensor:Mapped $native) (tensor:inspect $native)) ($other $other))))',
                     index_expected),
                    (f'(let $input {index_input} '
                     '(__cetta_lib_tensor_try_zip_map f64 ($input) bulk:identity ()))',
                     'Tensor:Fallback')]
                active_cases += [
                    (f'(let $input {a} '
                     f'(__cetta_lib_tensor_try_zip_map f64 ($input) bulk:{name} ()))',
                     'Tensor:Fallback')
                    for name in ('and-effect', 'or-effect', 'and-choice', 'or-choice',
                                 'and-invalid', 'or-invalid', 'and-empty', 'or-empty')]
            source = prefix + ''.join(
                f'!(let $actual {body} (== $actual {expected}))\n'
                for body, expected in active_cases)
            env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference, CETTA_POSTINGS_REFERENCE=reference)
            result = subprocess.run(command + ['-e', source], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=180)
            truth = 'true' if language.startswith('petta') else '[True]'
            assert result.returncode == 0 and result.stdout.splitlines()[-len(active_cases):] == [truth] * len(active_cases), (
                language, reference, result.stdout[-3000:], result.stderr[-2000:])
            assert 'BulkBooleanEffect' not in result.stdout, (language, 'speculative-effect')
            checks += len(active_cases)
        for name, extra in (
            ('choices', '!(collapse (tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (2) (1.0 2.0))) bulk:choose ())))'),
            ('effects', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (2) (1.0 2.0))) bulk:effect ()))'),
            ('index-effects', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values index (2) (1 2))) bulk:effect ()))'),
            ('logarithms', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (4) (0.5 1.0 2.0 4.0))) bulk:log (2.0)))'),
            ('domain', '!(tensor:inspect (tensor:zip-map f64 ((tensor:from-values f64 (2) (1.0 -1.0))) bulk:log (2.0)))'),
            ('strict-boolean', '\n'.join(
                f'!(collapse (tensor:inspect (tensor:zip-map f64 ({a}) bulk:{name} ())))'
                for name in ('and-effect', 'or-effect', 'and-choice', 'or-choice',
                             'and-invalid', 'or-invalid', 'and-empty', 'or-empty',
                             'intersection-inline', 'intersection-effect'))),
        ):
            observations = []
            for reference, fuel in (('0', False), ('1', False), ('0', True)):
                env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference)
                r = subprocess.run(command + (['--fuel', '10000000'] if fuel else []) + ['-e', prefix + extra],
                                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
                stdout = r.stdout
                if name == 'strict-boolean':
                    # Error payloads expose fresh bound-variable identities.
                    # Compare alpha-equivalently, preserving names and sharing.
                    identities = {}
                    def rename(match):
                        key = match.group(0)
                        return match[1] + '#' + str(identities.setdefault(key, len(identities)))
                    stdout = re.sub(r'(\$[^\s()#]+)#[0-9]+', rename, stdout)
                observations.append((r.returncode, stdout, r.stderr))
            assert observations[0] == observations[1] == observations[2], (language, name, observations)
            if name in ('effects', 'index-effects'): assert observations[0][1].count('BulkEffect') == 2
            if name == 'strict-boolean':
                # HE and PeTTa evaluate both operands. Prime's control form
                # short-circuits; its independent canonical run above agrees.
                effects = 0 if language == 'prime' else 6
                assert observations[0][1].count('BulkBooleanEffect') == effects
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
            assert r.returncode == 0 and b'CTN1' in r.stdout, (language, reference, r.stdout, r.stderr)
            observations.append(r.stdout)
        assert observations[0] == observations[1], (language, 'scalar-bit-patterns')
        checks += len(numbers)
    print(f'TensorBulk {checks} 0')


if __name__ == '__main__': main()
