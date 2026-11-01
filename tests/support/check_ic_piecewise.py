#!/usr/bin/env python3
"""Bounded native conditional discovery, parameter fitting and new-input checks."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, construct
from check_ic_numeric_arrays import archive, family, reference as grammar_reference
from check_ic_search import check_controls
from check_tensor_program import array, source_array
from check_tensor_program_parameters import HELPERS, evaluate, instantiate, reference, region
from check_tensor_sequences import coordinates, normalized


def parameter_fixtures():
    scalar = lambda value: array((), [float(value)])
    n = 4096
    x = array((n,), [float(i % 17 - 8) / 2 for i in range(n)])
    z = array((n,), [float(i % 13 - 6) / 2 for i in range(n)])
    active = array((n,), [True] * n, 'BoolTensor')
    queries = [array((2, 4), [-1., .5, .49, .51, 0., 2., -3., 4.]),
               array((2, 4), [1.5, 1.49, 1.51, 1.5, -1., 2., 0., -2.])]
    classifier = [['input', 0, 'F64Tensor'], ['input', 1, 'F64Tensor'],
                  ['parameter', 'split'], ['parameter', 'second-split'],
                  ['op', 13, [], [2, 0]], ['op', 13, [], [3, 1]],
                  ['op', 4, [], [0, 4]], ['op', 4, [], [1, 5]], ['op', 5, [], [0, 4]],
                  ['op', 16, [], [6, 7, 8]]]
    choices = [('split', 'F64Tensor', [scalar(-1), scalar(.5)]),
               ('second-split', 'F64Tensor', [scalar(0), scalar(1.5)])]
    assignment = {'split': scalar(.5), 'second-split': scalar(1.5)}
    target = evaluate(instantiate(classifier, assignment), [x, z])
    fixtures = [('nested-boolean-decisions', [x, z], target, active, classifier, choices, queries)]
    # The same observed target with one hidden disagreement must pass; exposing
    # that disagreement must reject every candidate, not silently enlarge error.
    changed = deepcopy(target)
    changed[2][(n-1,)] = not changed[2][(n-1,)]
    partial = array((n,), [True] * (n-1) + [False], 'BoolTensor')
    fixtures.append(('masked-boolean-decisions', [x, z], changed, partial, classifier, choices, queries))
    fixtures.append(('negative-boolean-example', [x, z], changed, active, classifier, choices, None))
    short_inputs = [array((32,), list(value[2].values())[:32]) for value in (x, z)]
    short_target = evaluate(instantiate(classifier, assignment), short_inputs)
    fixtures.append(('exact-but-larger-tree', short_inputs, short_target,
                     array((32,), [True] * 32, 'BoolTensor'), classifier, choices, None))
    affine = [['input', 0, 'F64Tensor'], ['parameter', 'split'], ['parameter', 'left-slope'],
              ['parameter', 'right-slope'], ['parameter', 'intercept'],
              ['op', 13, [], [1, 0]], ['op', 4, [], [0, 5]],
              ['op', 13, [], [2, 0]], ['op', 2, [], [0, 7]],
              ['op', 13, [], [3, 0]], ['op', 2, [], [0, 9]],
              ['op', 13, [], [4, 0]], ['op', 0, [], [8, 11]], ['op', 0, [], [10, 11]],
              ['op', 15, [], [6, 12, 13]]]
    choices = [('split', 'F64Tensor', [array((2,), [0., 1.]), scalar(-1), scalar(.5)]),
               ('left-slope', 'F64Tensor', [scalar(1), scalar(2)]),
               ('right-slope', 'F64Tensor', [scalar(-2), scalar(-1)]),
               ('intercept', 'F64Tensor', [scalar(0), scalar(3)])]
    assignment = {'split': scalar(.5), 'left-slope': scalar(2), 'right-slope': scalar(-1), 'intercept': scalar(3)}
    target = evaluate(instantiate(affine, assignment), [x])
    fixtures.append(('piecewise-affine-shared-intercept', [x], target, active, affine, choices, queries[:1]))
    position = array((n,), [float(i) for i in range(n)])
    drift = [['input', 0, 'F64Tensor'], ['parameter', 'split'], ['parameter', 'before'], ['parameter', 'after'],
             ['op', 13, [], [1, 0]], ['op', 4, [], [0, 4]], ['op', 13, [], [2, 0]],
             ['op', 13, [], [3, 0]], ['op', 15, [], [5, 6, 7]], ['op', 6, [0], [8]]]
    choices = [('split', 'F64Tensor', [scalar(120.5), scalar(128.5)]),
               ('before', 'F64Tensor', [scalar(1), scalar(1.5)]),
               ('after', 'F64Tensor', [scalar(-1), scalar(-.5)])]
    target = evaluate(instantiate(drift, {'split': scalar(128.5), 'before': scalar(1.5), 'after': scalar(-.5)}), [position])
    fixtures.append(('cumulative-drift-change', [position], target, active, drift, choices,
                     [array((6,), [127., 128., 128.5, 129., 130., 131.])]))
    return fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    code = [HELPERS, r'''
(= (piecewise-test:predict-fitted $result $inputs $expected)
   (case $result
     (((IC:SearchResult $best $statistics $reason $fault $state)
       (let* (($prediction (parameter-test:predict $best $inputs $expected)) ($closed (ic:release $state)))
         (FittedPrediction $prediction $reason))) ($other $other))))
(= (piecewise-test:resume $expected (IC:PreparedSearch $state))
   (case (ic:resume $state 0)
     (((IC:SearchResult $best (IC:Statistics 0 0 0 0) BudgetExhausted IC:NoFault $next)
       (case (ic:cancel $next)
         (((IC:SearchResult $same $statistics Cancelled $fault $last)
           (PiecewiseResume (== $same $best) (parameter-test:view $expected (ic:resume $last 8192)))))))
      ($other $other))))
''']
    expected, names, fits = [], [], []

    def add(name, call, wanted='true'):
        index = len(expected)
        code.append(f'!(println! (PiecewiseCase {index} {call}))')
        expected.append(['PiecewiseCase', str(index), wanted])
        names.append(name)

    for name, inputs, target, mask, rows, choices, queries in parameter_fixtures():
        original, best, seen, accepted, winning = reference(inputs, target, mask, rows, choices)
        assert (accepted > 0) == (queries is not None), (name, accepted)
        declared = region(rows, choices)
        fitting = f'(ic:fit Numeric:ParameterizedPrograms {byte_tensor(original)} {declared} () 8192)'
        desired = ['ParameterFit', 'true', str(seen), str(accepted), '0', 'RegionExhausted', 'IC:NoFault']
        add(name+'/region', f'(tensor-program:parameter-region? {declared})')
        add(name+'/complete-fit', f'(parameter-test:view {byte_tensor(best)} {fitting})', desired)
        fits.append({'name': name, 'assignments': seen, 'accepted': accepted,
                     'original_bytes': len(original), 'best_bytes': len(best)})
        if winning is None:
            continue
        prediction = evaluate(winning, queries)
        query = '(' + ' '.join(map(source_array, queries)) + ')'
        add(name+'/new-input-from-native-result',
            f'(piecewise-test:predict-fitted {fitting} {query} {source_array(prediction)})',
            ['FittedPrediction', 'true', 'RegionExhausted'])
        add(name+'/standalone-prediction', f'(parameter-test:predict {byte_tensor(best)} {query} {source_array(prediction)})')
        if name == 'nested-boolean-decisions':
            add(name+'/cancel-resume', f'(piecewise-test:resume {byte_tensor(best)} '
                f'(ic:prepare-fit Numeric:ParameterizedPrograms {byte_tensor(original)} {declared} ()))',
                ['PiecewiseResume', 'true', desired])
            # The exact split value belongs to the else branch; nearby unseen
            # values on both sides have deliberately different predictions.
            assert list(prediction[2].values()) == [False, False, False, True, True, True, True, True]
            bad_query = '(' + source_array(queries[0]) + ' ' + source_array(array((8,), [1.] * 8)) + ')'
            add(name+'/new-shape-refusal', f'(ic:predict Numeric:ParameterizedPrograms {byte_tensor(best)} {bad_query})',
                ['IC:NumericFault', 'Prediction'])

    # Discover children from the full declared one-operation grammar, rather
    # than supplying the correct ternary application as a template.
    n = 2048
    for recipe, sort, a, b in (('TP:Where', 'F64Tensor', 2., -3.),
                              ('TP:WhereBool', 'BoolTensor', True, False),
                              ('TP:WhereI64', 'I64Tensor', 2, -3)):
        condition = array((n,), [i % 3 == 0 for i in range(n)], 'BoolTensor')
        left = array((n,), [a if i % 2 else b for i in range(n)], sort)
        right = array((n,), [b if i % 2 else a for i in range(n)], sort)
        target = array((n,), [left[2][point] if flag else right[2][point] for point, flag in condition[2].items()], sort)
        operators = [(recipe, ['BoolTensor', sort, sort], sort, [[]])]
        keep = array((n,), [True] * n, 'BoolTensor')
        tolerance = 0.0 if sort == 'F64Tensor' else None
        original, best, seen, accepted = grammar_reference([condition, left, right], target, keep, tolerance, [], operators, 1)
        assert accepted > 0 and len(best) < len(original), (recipe, seen, accepted)
        declared = f'(tensor-program:prepare-region {construct(family(operators))} '
        declared += f'{construct(list(map(Text, ["BoolTensor", sort, sort])))} () ((TP:Domain 0 (()))) 1)'
        desired = ['ParameterFit', 'true', str(seen), str(accepted), '0', 'RegionExhausted', 'IC:NoFault']
        fitting = f'(ic:fit Numeric:Programs {byte_tensor(original)} {declared} () 8192)'
        add(recipe+'/bounded-child-discovery', f'(parameter-test:view {byte_tensor(best)} {fitting})', desired)
        queries = [array((4,), [True, False, False, True], 'BoolTensor'),
                   array((4,), [a, b, a, b], sort), array((4,), [b, a, b, a], sort)]
        prediction = array((4,), [a, a, b, b], sort)
        query = '(' + ' '.join(map(source_array, queries)) + ')'
        add(recipe+'/new-input-from-native-result', f'(piecewise-test:predict-fitted {fitting} {query} {source_array(prediction)})',
            ['FittedPrediction', 'true', 'RegionExhausted'])
        fits.append({'name': recipe+'/bounded-child-discovery', 'candidates': seen, 'accepted': accepted,
                     'original_bytes': len(original), 'best_bytes': len(best)})
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'piecewise.metta'
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=240)
    (args.output / 'program.log').write_text(result.stdout + result.stderr)
    actual = [row for row in parse_forms(result.stdout) if isinstance(row, list) and row and row[0] == 'PiecewiseCase']
    mismatches = [{'case': i, 'name': names[i] if i < len(names) else 'unexpected-answer', 'expected': want, 'actual': got}
                  for i, (want, got) in enumerate(itertools.zip_longest(expected, actual))
                  if normalized(want) != normalized(got)]
    receipt = {'passed': result.returncode == 0 and not mismatches, 'checks': len(expected), 'answers': len(actual),
               'returncode': result.returncode, 'mismatches': mismatches, 'fits': fits,
               'scope': 'finite declared conditional DAGs, native fitting/discovery, complete NC2 bytes, original masks, boundary negatives and unseen numerical inputs; not full tree or WILLIAM search'}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({**receipt, 'mismatches': [row['name'] for row in mismatches]}))
    for row in mismatches[:3]:
        print(json.dumps(row)[:1800])
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
