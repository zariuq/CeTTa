#!/usr/bin/env python3
"""Coordinate-based row selection across scalar types, layouts and views."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_search import check_controls
from check_tensor_sequences import coordinates, normalized, observation, tensor


def select(shape, values, row_shape, rows):
    result = {}
    for target, source in rows.items():
        for trailing in coordinates(shape[1:]):
            point = (source,) + trailing
            if point in values:
                result[target + trailing] = values[point]
    return row_shape + shape[1:], result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cetta', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    code = ['!(import! &self (library tensor))']
    expected = []

    def add(call, wanted):
        index = len(expected)
        code.append(f'!(println! (RowCase {index} {call}))')
        expected.append(['RowCase', str(index), wanted])

    dtypes = ('i64', 'f64', 'f32', 'index', 'u8', 'bool')
    shape = (3, 2)
    request_shape = (4,)
    requests = {(0,): 2, (1,): 0, (2,): 2, (3,): 1}
    for dtype in dtypes:
        data = [False, True, True, False, False, True] if dtype == 'bool' else [0, 1, 2, 3, 4, 5]
        if dtype in ('f64', 'f32'):
            data = [-0.0, 1.0, 2.0, 3.0, 4.0, 5.0]
        values = dict(zip(coordinates(shape), data))
        for sparse in (False, True):
            selected = {point: value for i, (point, value) in enumerate(values.items()) if not sparse or i % 2 == 0}
            layouts = ('coo', 'csr', 'csc') if sparse else ('dense', 'coo', 'csr', 'csc')
            for layout in layouts:
                source = f'(tensor:convert {tensor(shape, selected, dtype)} {layout})'
                for absent_index in (False, True):
                    rows = {key: value for key, value in requests.items() if not absent_index or key != (1,)}
                    actual_shape, wanted = select(shape, selected, request_shape, rows)
                    add(f'(tensor:inspect (tensor:gather-rows {source} {tensor(request_shape, rows, "index")}))',
                        observation(actual_shape, wanted, dtype))
    for shape in ((3,), (3, 2, 2), (3, 0), (0, 2), (2, 0, 3)):
        values = {point: i for i, point in enumerate(coordinates(shape))}
        rows = {(0,): shape[0] - 1, (1,): 0} if shape[0] else {}
        row_shape = (2,) if shape[0] else (0,)
        actual_shape, wanted = select(shape, values, row_shape, rows)
        for layout in ('dense', 'coo'):
            add(f'(tensor:inspect (tensor:gather-rows (tensor:convert {tensor(shape, values)} {layout}) '
                f'{tensor(row_shape, rows, "index")}))', observation(actual_shape, wanted))
    base = '(tensor:from-values f64 (6) (1.0 2.0 3.0 4.0 5.0 6.0))'
    views = [((3, 2), (1, 3), 0), ((3, 2), (-2, 1), 4), ((3, 2), (0, 1), 2)]
    requests = {(0,): 2, (1,): 0, (2,): 2}
    for shape, strides, offset in views:
        values = {point: float(1 + offset + sum(a * b for a, b in zip(point, strides))) for point in coordinates(shape)}
        output_shape, wanted = select(shape, values, (3,), requests)
        view = f'(tensor:view {base} ({" ".join(map(str, shape))}) ({" ".join(map(str, strides))}) {offset})'
        row_view = '(tensor:view (tensor:from-values index (3) (2 0 2)) (3) (-1) 2)'
        add(f'(tensor:inspect (tensor:gather-rows {view} {row_view}))', observation(output_shape, wanted, 'f64'))
    for source, rows in (
        ('(tensor:from-values i64 () (1))', '(tensor:from-values index (1) (0))'),
        ('(tensor:from-values i64 (2) (1 2))', '(tensor:from-values i64 (1) (0))'),
        ('(tensor:from-values i64 (2) (1 2))', '(tensor:from-values index () (0))'),
        ('(tensor:from-values i64 (2 0) ())', '(tensor:from-values index (1) (2))'),
        ('(tensor:from-values i64 (2) (1 2))', '(tensor:from-values index (1) (2))')):
        add(f'(tensor:is-tensor (tensor:gather-rows {source} {rows}))', 'false')
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'program.metta'
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, text=True, capture_output=True, timeout=90)
    (args.output / 'program.log').write_text(result.stdout + result.stderr)
    actual = [row for row in parse_forms(result.stdout) if isinstance(row, list) and row and row[0] == 'RowCase']
    found = {row[1]: row for row in actual}
    wrong = [{'case': i, 'expected': want, 'actual': found.get(str(i))} for i, want in enumerate(expected)
             if normalized(found.get(str(i))) != normalized(want)]
    receipt = {'passed': result.returncode == 0 and len(actual) == len(expected) and not wrong,
               'checks': len(expected), 'answers': len(actual), 'mismatches': wrong,
               'scope': 'leading-axis ordered selection, all dtypes, support, duplicates, layouts, strided values and row requests'}
    (args.output / 'results.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'TensorGatherRows {len(expected)} {len(wrong)}')
    if wrong:
        print(json.dumps(wrong[:3], indent=2))
    raise SystemExit(0 if receipt['passed'] else 1)


if __name__ == '__main__':
    main()
