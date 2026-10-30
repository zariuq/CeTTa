#!/usr/bin/env python3
"""Independent pointwise selection with explicit support and strict operands."""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_search import check_controls
from check_tensor_sequences import coordinates, normalized, observation, tensor


def where(condition, when_true, when_false):
    return {point: (when_true if flag else when_false)[point]
            for point, flag in condition.items()
            if point in (when_true if flag else when_false)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    code = ['!(import! &self (library tensor))']
    expected, names = [], []

    def add(name, call, wanted):
        index = len(expected)
        code.append(f'!(println! (WhereCase {index} {call}))')
        expected.append(['WhereCase', str(index), wanted])
        names.append(name)

    shape = (2, 3)
    points = list(coordinates(shape))
    for dtype in ('i64', 'index', 'u8', 'bool', 'f32', 'f64'):
        left = dict(zip(points, [False, True, False, True, True, False] if dtype == 'bool'
                        else [-0.0, 1., 2., 3., 4., 5.] if dtype in ('f32', 'f64')
                        else [0, 1, 2, 3, 4, 5]))
        right = dict(zip(points, [True, False, True, False, False, True] if dtype == 'bool'
                         else [9, 8, 7, 6, 5, 4]))
        if dtype in ('f32', 'f64'):
            right = {point: float(value) for point, value in right.items()}
        mask = dict(zip(points, (True, False, True, False, True, False)))
        for mask_sparse, left_sparse, right_sparse in itertools.product((False, True), repeat=3):
            selected_mask = {p: v for p, v in mask.items() if not mask_sparse or p != (1, 0)}
            selected_left = {p: v for p, v in left.items() if not left_sparse or p != (0, 2)}
            selected_right = {p: v for p, v in right.items() if not right_sparse or p != (1, 2)}
            for layout in ('coo', 'csr', 'csc'):
                source = lambda values, kind: f'(tensor:convert {tensor(shape, values, kind)} {layout})'
                call = f'(tensor:where {source(selected_mask, "bool")} {source(selected_left, dtype)} {source(selected_right, dtype)})'
                add(f'{dtype}/{layout}/{mask_sparse}-{left_sparse}-{right_sparse}',
                    f'(tensor:inspect {call})', observation(shape, where(selected_mask, selected_left, selected_right), dtype))
        # Dense is a separate input layout, not a densification of missing support.
        add(dtype+'/dense', f'(tensor:inspect (tensor:where '
            f'(tensor:convert {tensor(shape, mask, "bool")} dense) '
            f'(tensor:convert {tensor(shape, left, dtype)} dense) '
            f'(tensor:convert {tensor(shape, right, dtype)} dense)))',
            observation(shape, where(mask, left, right), dtype))

    for shape in ((), (0,), (2, 0, 3), (2, 2, 2)):
        values = dict.fromkeys(coordinates(shape), 7)
        for flag in (False, True):
            mask = dict.fromkeys(coordinates(shape), flag)
            add(f'shape/{shape}/{flag}', f'(tensor:inspect (tensor:where {tensor(shape, mask, "bool")} '
                f'{tensor(shape, values)} {tensor(shape, {})}))', observation(shape, values if flag else {}))

    for strides, offset in (((-3, 1), 3), ((1, 2), 0), ((0, 1), 1)):
        shape = (2, 3)
        mask = {(i, j): (i+j) % 2 == 0 for i, j in coordinates(shape)}
        left = {p: float(1 + offset + sum(i*s for i, s in zip(p, strides))) for p in coordinates(shape)}
        right = dict.fromkeys(coordinates(shape), -2.)
        view = f'(tensor:view (tensor:from-values f64 (6) (1.0 2.0 3.0 4.0 5.0 6.0)) '
        view += f'(2 3) ({" ".join(map(str, strides))}) {offset})'
        condition = f'(tensor:permute {tensor((3, 2), {(j, i): v for (i, j), v in mask.items()}, "bool")} (1 0))'
        add(f'view/{strides}', f'(tensor:inspect (tensor:where {condition} {view} {tensor(shape, right, "f64")}))',
            observation(shape, where(mask, left, right), 'f64'))

    condition = '(tensor:from-values bool (2) (True False))'
    branch = '(tensor:from-values i64 (2) (3 4))'
    for name, mask, left, right, fault in (
        ('condition-sort', branch, branch, branch, 'unsupported-dtype'),
        ('branch-sort', condition, branch, '(tensor:from-values f64 (2) (3.0 4.0))', 'unsupported-dtype'),
        ('condition-shape', '(tensor:from-values bool (1) (True))', branch, branch, 'shape-mismatch'),
        ('left-shape', condition, '(tensor:from-values i64 () (3))', branch, 'shape-mismatch'),
        ('right-shape', condition, branch, '(tensor:from-values i64 (1 2) (3 4))', 'shape-mismatch'),
        ('unused-invalid-shape', '(tensor:from-values bool (2) (True True))', branch,
         '(tensor:from-values i64 (0) ())', 'shape-mismatch')):
        add(name, f'(tensor:where {mask} {left} {right})', ['Tensor:Fault', fault])
    add('unknown-operand-stays-syntax', f'(get-metatype (tensor:where Unknown {branch} {branch}))', 'Expression')
    add('strict-operands', '''(tensor:inspect (tensor:where
        (let $written (println! ConditionWasEvaluated) (tensor:from-values bool () (True)))
        (let $written (println! TrueBranchWasEvaluated) (tensor:from-values i64 () (7)))
        (let $written (println! FalseBranchWasEvaluated) (tensor:from-values i64 () (9)))))''',
        observation((), {(): 7}))
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'program.metta'
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, text=True, capture_output=True, timeout=90)
    (args.output / 'program.log').write_text(result.stdout + result.stderr)
    forms = parse_forms(result.stdout)
    actual = [row for row in forms if isinstance(row, list) and row and row[0] == 'WhereCase']
    found = {row[1]: row for row in actual}
    wrong = [{'case': i, 'name': names[i], 'expected': want, 'actual': found.get(str(i))}
             for i, want in enumerate(expected) if normalized(found.get(str(i))) != normalized(want)]
    effects = [row for row in forms if row in ('ConditionWasEvaluated', 'TrueBranchWasEvaluated', 'FalseBranchWasEvaluated')]
    receipt = {'passed': result.returncode == 0 and len(actual) == len(expected) and not wrong
               and effects == ['ConditionWasEvaluated', 'TrueBranchWasEvaluated', 'FalseBranchWasEvaluated'],
               'checks': len(expected), 'answers': len(actual), 'mismatches': wrong, 'effects': effects,
               'scope': 'exact-shape pointwise selection; all dtypes; support, layouts, views, scalar bits, strict operands'}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({**receipt, 'mismatches': [row['name'] for row in wrong]}))
    for row in wrong[:3]:
        print(json.dumps(row))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
