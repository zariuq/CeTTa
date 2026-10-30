#!/usr/bin/env python3
"""Independent TP1 linking bytes, numerical composition and failure dominance."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor
from check_binary_value import Text, encoded
from check_ic_search import check_controls
from check_tensor_program import FAMILY, OPS, array, canonical, records, reference, source_array
from check_tensor_sequences import normalized


def artifact(family, roots, rows):
    roots, projected = canonical(roots, records(rows))
    return encoded(['TP:Program', Text('TP1'), family, ['Dag:Forest', roots, projected]])


def linked(first, second):
    left_family, left_root, left_rows = first
    right_family, right_root, right_rows = second
    operators = []
    keys = []
    for row in left_family[2] + right_family[2]:
        key = encoded(row)
        if key not in keys:
            keys.append(key)
            operators.append(row)
    family = ['TP:Family', Text('TF1'), operators]
    result = deepcopy(left_rows)
    for row in result:
        if row[0] == 'op':
            row[1] = keys.index(encoded(left_family[2][row[1]]))
    aliases = []
    for row in right_rows:
        if row[0] == 'input':
            assert row[1] == 0
            aliases.append(left_root)
        elif row[0] == 'constant':
            aliases.append(len(result))
            result.append(deepcopy(row))
        else:
            aliases.append(len(result))
            result.append(['op', keys.index(encoded(right_family[2][row[1]])),
                           row[2], [aliases[index] for index in row[3]]])
    root = aliases[right_root]
    numerical = deepcopy(result)
    for row in numerical:
        if row[0] == 'op':
            row[1] = next(i for i, specification in enumerate(OPS) if specification[1] == operators[row[1]][4])
    return artifact(family, [root], result), root, numerical


HELPERS = r'''
!(import! &self (library lib_tensor_program_compose))
(= (compose-test:view $value)
   (case $value
     (((TP:Outputs $values)
       (ProgramResult (map-atom $values $item
         (case $item
           (((TP:Value $sort $tensor) ($sort (tensor:inspect $tensor))) ($failure $failure))))))
      ($other $other))))
(= (compose-test:packed $first $second $expected)
   (case (tensor-program:compose-strict $first $second)
     (((TP:Artifact $bytes) (== $bytes $expected)) ($refused $refused))))
(= (compose-test:run $first $second $inputs)
   (case (tensor-program:compose-strict $first $second)
     (((TP:Artifact $bytes) (compose-test:view (tensor-program:run $bytes $inputs))) ($refused-run $refused-run))))
(= (compose-test:sequential $first $second $inputs)
   (case (tensor-program:run $first $inputs)
     (((TP:Outputs ((TP:Value $sort $value))) (compose-test:view (tensor-program:run $second ($value))))
      ($first-failure (compose-test:view $first-failure)))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    identity = (FAMILY, 0, [['input', 0, 'F64Tensor']])
    scan = (FAMILY, 1, [['input', 0, 'F64Tensor'], ['op', 6, [0], [0]]])
    total = (FAMILY, 1, [['input', 0, 'F64Tensor'], ['op', 11, ['sequential'], [0]]])

    def affine(operation, constant, constant_first=False):
        return (FAMILY, 3, [['input', 0, 'F64Tensor'], ['constant', array((), [constant])],
                ['op', 13, [], [1, 0]], ['op', operation, [], [2, 0] if constant_first else [0, 2]]])

    shift = affine(0, 1.0)
    double = affine(2, 2.0)
    prefix = (FAMILY, 2, [['input', 0, 'F64Tensor'], ['constant', array((), [1.0])], ['op', 0, [], [1, 0]]])
    repeated = (FAMILY, 1, [['input', 0, 'F64Tensor'], ['op', 7, [0], [0, 0]]])
    matrix = (FAMILY, 2, [['input', 0, 'F64Tensor'], ['constant', array((2, 3), [1., 2., 3., 0., 1., 2.])],
                         ['op', 10, ['sequential'], [1, 0]]])
    gather = (FAMILY, 2, [['input', 0, 'F64Tensor'], ['constant', array((3,), [2, 0, 2], 'I64Tensor')],
                         ['op', 12, [], [0, 1]]])
    permutation = (FAMILY, 1, [['input', 0, 'F64Tensor'], ['op', 8, [[1, 0]], [0]]])
    vector_product = (FAMILY, 2, [['input', 0, 'F64Tensor'], ['constant', array((2,), [2., 3.])],
                                 ['op', 10, ['fixed4'], [0, 1]]])
    boolean_identity = (['TP:Family', Text('TF1'), []], 0, [['input', 0, 'BoolTensor']])
    sign = affine(5, 0.0)
    permuted_family = deepcopy(FAMILY)
    permuted_family[2].reverse()
    permuted_double = (permuted_family, double[1], deepcopy(double[2]))
    for row in permuted_double[2]:
        if row[0] == 'op':
            row[1] = len(OPS) - 1 - row[1]
    fixtures = [(shift, double, array((3,), [-1., 0., 3.])), (double, shift, array((3,), [-1., 0., 3.])),
        (repeated, scan, array((3,), [1., 2., 3.])), (matrix, scan, array((3,), [1., 2., 3.])),
        (scan, total, array((3,), [1., 2., 3.])), (gather, double, array((3,), [1., 2., 3.])),
        (permutation, vector_product, array((2, 3), [1., 2., 3., 4., 5., 6.])),
        (identity, double, array((0,), [])), (total, double, array((5,), {(1,): 0., (3,): 2.})),
        (shift, permuted_double, array((3,), [-1., 0., 3.])),
        (sign, boolean_identity, array((3,), [-1., 0., 3.])), (identity, prefix, array((), [3.]))]
    code = [HELPERS]
    expected = []

    def add(expression, wanted):
        i = len(expected)
        code.append(f'!(println! (ComposeCase {i} {expression}))')
        expected.append(['ComposeCase', str(i), wanted])

    for first, second, inputs in fixtures:
        before = artifact(first[0], [first[1]], first[2])
        after = artifact(second[0], [second[1]], second[2])
        combined, root, rows = linked(first, second)
        wanted = ['ProgramResult', reference([root], rows, [inputs])]
        add(f'(compose-test:packed {tensor(before)} {tensor(after)} {tensor(combined)})', 'true')
        add(f'(compose-test:run {tensor(before)} {tensor(after)} ({source_array(inputs)}))', wanted)
        add(f'(compose-test:sequential {tensor(before)} {tensor(after)} ({source_array(inputs)}))', wanted)

    base = artifact(identity[0], [0], identity[2])
    unused = artifact(FAMILY, [1], [['input', 0, 'F64Tensor'], ['constant', array((), [2.])]])
    competing = artifact(FAMILY, [4], [['input', 0, 'F64Tensor'], ['constant', array((), [1.])],
        ['constant', array((), [0.])], ['op', 3, [], [1, 2]], ['op', 0, [], [3, 0]]])
    for later in (unused, competing):
        add(f'(tensor-program:compose-strict {tensor(base)} {tensor(later)})', ['TP:Fault', 'NonStrict'])
    add(f'(tensor-program:compose-strict {tensor(artifact(sign[0], [sign[1]], sign[2]))} {tensor(base)})',
        ['TP:Fault', 'ComposeSort'])
    bad_index = artifact(FAMILY, [0], [['input', 1, 'F64Tensor']])
    no_input = artifact(FAMILY, [0], [['constant', array((), [2.])]])
    for invalid in (b'not-a-program', bad_index, no_input):
        add(f'(tensor-program:compose-strict {tensor(invalid)} {tensor(base)})', ['TP:Fault', 'InvalidComposition'])
    conflict_family = deepcopy(FAMILY)
    conflict_family[2][0][4] = 'TP:Multiply'
    conflict = artifact(conflict_family, [3], deepcopy(shift[2]))
    add(f'(tensor-program:compose-strict {tensor(base)} {tensor(conflict)})', ['TP:Fault', 'OperatorConflict'])
    # Failure identity is part of composition, not merely failure/no-failure.
    for later in (identity, prefix):
        after = artifact(later[0], [later[1]], later[2])
        wrong_input = source_array(array((), [3], 'I64Tensor'))
        wanted = ['ProgramResult', [['TP:Fault', 'InputSort']]]
        add(f'(compose-test:run {tensor(base)} {tensor(after)} ({wrong_input}))', wanted)
        add(f'(compose-test:sequential {tensor(base)} {tensor(after)} ({wrong_input}))', wanted)
    text = '\n'.join(code)+'\n'
    check_controls(text)
    path = args.output/'compose.metta'
    path.write_text(text)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=90)
    (args.output/'stdout.log').write_text(result.stdout)
    (args.output/'stderr.log').write_text(result.stderr)
    actual = [form for form in parse_forms(result.stdout) if isinstance(form, list) and form and form[0] == 'ComposeCase']
    errors = [{'case': i, 'wanted': wanted, 'actual': got}
              for i, (wanted, got) in enumerate(itertools.zip_longest(expected, actual))
              if normalized(wanted) != normalized(got)]
    receipt = {'passed': result.returncode == 0 and not errors, 'checks': len(expected), 'observations': len(actual),
               'errors': errors, 'scope': 'complete TP1 linking bytes, independent recursive numerical reference and ordered failure dominance'}
    (args.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
