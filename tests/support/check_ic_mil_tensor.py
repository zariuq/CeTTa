#!/usr/bin/env python3
"""Native MIL proposals in common numerical IC cost and resumable search."""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as bytes_source
from check_binary_value import Text, encoded
from check_ic_numeric import archive, ctn, vector
from check_ic_numeric_arrays import archive as array_archive
from check_ic_search import check_controls
from check_tensor_program import FAMILY, array
from check_tensor_program import source_array
from check_tensor_sequences import normalized
from check_tensor_program_compose import artifact, linked


HELPERS = r'''
!(import! &self (library lib_ic_mil_tensor))
(= (mil-fit-test:region $first $second)
   (let* (($last (cons-atom (MIL.TensorPrimitive "second" $second) ()))
          ($bindings (cons-atom (MIL.TensorPrimitive "shift" $first) $last))
          ($skeletons (cons-atom (MIL.Chain (MIL.Hole left) (MIL.Hole right)) ())))
     (MIL.TensorRegion $bindings $skeletons)))
(= (mil-fit-test:view $original $expected $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let* (($prediction (ic:predict Numeric:MIL $best (tensor:from-values f64 (3) (-2.0 0.5 3.0))))
              ($closed (ic:release $state)))
         (MILFit (== $best $expected) $seen $accepted $faults $reason $first
           (case $prediction
             (((IC:Prediction $values) (tensor:inspect $values)) ($no-prediction NoPrediction))))))
      ($bad-result $bad-result))))
(= (mil-fit-test:pieces $original $expected (IC:PreparedSearch $state))
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $statistics BudgetExhausted $fault $next)
       (MILResume BudgetExhausted (mil-fit-test:view $original $expected (ic:resume $next 256))))
      ($not-paused (mil-fit-test:view $original $expected $not-paused)))))
(= (mil-fit-test:cancel $original $expected (IC:PreparedSearch $state))
   (case (ic:cancel $state)
     (((IC:SearchResult $best $statistics Cancelled $fault $next)
       (MILCancelled Cancelled (mil-fit-test:view $original $expected (ic:resume $next 256)))))))
(= (mil-fit-test:changed-model (IC:PreparedSearch $state))
   (case $state
     (((IC:SearchState (IC:Task (MIL.TensorModel $domain $bindings) $original $observation $guards $quantum)
          $queue $best $statistics $fault)
       (let* (($result (ic:resume
          (IC:SearchState (IC:Task (MIL.TensorModel $domain ()) $original $observation $guards $quantum)
            $queue $best $statistics $fault) 0)) ($closed (ic:release $state))) $result)))))
(= (mil-fit-test:changed-source (IC:PreparedSearch $state))
   (let* (($change (add-atom &self MILTensorDependencyChanged))
          ($result (ic:resume $state 0)) ($closed (ic:release $state))) $result))
(= (mil-fit-test:array-view $expected $inputs $prediction $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let* (($predicted (ic:predict Numeric:MIL $best $inputs)) ($closed (ic:release $state)))
         (MILArrayFit (== $best $expected) $seen $accepted $faults $reason $first
           (case $predicted
             (((IC:Prediction $value) (== $value $prediction)) ($not-predicted $not-predicted))))))
      ($bad-array-result $bad-array-result))))
(= (mil-fit-test:no-gain $original $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let $closed (ic:release $state) (MILNoGain (== $best $original) $seen $accepted $faults $reason $first)))
      ($bad-no-gain $bad-no-gain))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    def primitive(operation, constant):
        return (FAMILY, 3, [['input', 0, 'F64Tensor'], ['constant', array((), [constant])],
                ['op', 13, [], [1, 0]], ['op', operation, [], [0, 2]]])

    shift, double = primitive(0, 1.), primitive(2, 2.)
    first, second = (artifact(family, [root], rows) for family, root, rows in (shift, double))
    program, _root, _rows = linked(shift, double)
    inputs = [i / 8.0 - 40.0 for i in range(768)]
    outputs = [2.0 * (x + 1.0) for x in inputs]
    original = archive(inputs, outputs, [True]*len(inputs), 0.0)
    expected = encoded(['Numeric:Archive', Text('NC1'), ctn(inputs), ctn([1]*len(inputs), 'u8'), ctn([0.0]),
                        ['Numeric:Program', program]])
    assert len(expected) < len(original), (len(original), len(expected))
    base = ['MILFit', 'true', '4', '1', '0', 'RegionExhausted', 'IC:NoFault',
            ['TensorData', 'f64', ['3'], [['0', '-2.0'], ['1', '3.0'], ['2', '8.0']]]]
    code = [HELPERS]
    wants = []

    def add(expression, result):
        index = len(wants)
        code.append(f'!(println! (MILFitCase {index} {expression}))')
        wants.append(['MILFitCase', str(index), result])

    bindings = f'(mil-fit-test:region {bytes_source(first)} {bytes_source(second)})'
    source = bytes_source(original)
    best = bytes_source(expected)
    prepared = f'(ic:prepare-fit Numeric:MIL {source} {bindings} ())'
    add(f'(mil-fit-test:view {source} {best} (ic:fit Numeric:MIL {source} {bindings} () 256))', base)
    add(f'(mil-fit-test:pieces {source} {best} {prepared})', ['MILResume', 'BudgetExhausted', base])
    add(f'(mil-fit-test:cancel {source} {best} {prepared})', ['MILCancelled', 'Cancelled', base])
    add(f'(mil-fit-test:changed-model {prepared})', ['IC:ResumeRejected', 'InvalidArtifact'])
    add(f'(ic:prepare-fit Numeric:MIL {source} (MIL.TensorRegion () ((MIL.Choice (MIL.Hole x) (MIL.Hole y)))) ())',
        ['IC:FitRefused', 'Region'])
    add(f'(ic:prepare-fit Numeric:MIL {source} (MIL.TensorRegion ((MIL.TensorPrimitive "bad" {bytes_source(b"bad")})) ()) ())',
        ['IC:FitRefused', 'Region'])
    # The same accepted program is a complete NC2 artifact over rank-two data.
    matrix = array((24, 32), inputs)
    matrix_output = array((24, 32), outputs)
    mask = array((24, 32), [True]*len(inputs), 'BoolTensor')
    matrix_original = array_archive([matrix], matrix_output, mask, 0.0)
    matrix_expected = array_archive([matrix], matrix_output, mask, 0.0, program)
    assert len(matrix_expected) < len(matrix_original)
    query = array((2, 2), [-2., 0., 0.5, 3.])
    predicted = array((2, 2), [-2., 2., 3., 8.])
    add(f'(mil-fit-test:array-view {bytes_source(matrix_expected)} ({source_array(query)}) '
        f'{source_array(predicted)} (ic:fit Numeric:MIL {bytes_source(matrix_original)} {bindings} () 256))',
        ['MILArrayFit', 'true', '4', '1', '0', 'RegionExhausted', 'IC:NoFault', 'true'])
    # Classification uses the same typed producer with a Boolean result sort.
    from copy import deepcopy
    def minimal(operation, constant):
        item = primitive(operation, constant)
        family = deepcopy(FAMILY)
        family[2] = [FAMILY[2][13], FAMILY[2][operation]]
        rows = item[2]
        rows[2][1], rows[3][1] = 0, 1
        return family, 3, rows
    class_shift, sign = minimal(0, 1.), minimal(5, 0.)
    class_first = artifact(class_shift[0], [class_shift[1]], class_shift[2])
    sign_bytes = artifact(sign[0], [sign[1]], sign[2])
    class_program, _, _ = linked(class_shift, sign)
    class_inputs = [i/8. - 40. for i in range(3072)]
    labels = array((len(class_inputs),), [x+1. > 0. for x in class_inputs], 'BoolTensor')
    class_mask = array((len(class_inputs),), [True]*len(class_inputs), 'BoolTensor')
    class_original = array_archive([array((len(class_inputs),), class_inputs)], labels, class_mask)
    class_expected = array_archive([array((len(class_inputs),), class_inputs)], labels, class_mask, model=class_program)
    assert len(class_expected) < len(class_original)
    class_region = f'(mil-fit-test:region {bytes_source(class_first)} {bytes_source(sign_bytes)})'
    class_query = array((4,), [-2., -1., 0., 2.])
    class_prediction = array((4,), [False, False, True, True], 'BoolTensor')
    add(f'(mil-fit-test:array-view {bytes_source(class_expected)} ({source_array(class_query)}) '
        f'{source_array(class_prediction)} (ic:fit Numeric:MIL {bytes_source(class_original)} {class_region} () 256))',
        ['MILArrayFit', 'true', '1', '1', '0', 'RegionExhausted', 'IC:NoFault', 'true'])
    small = archive([0., 1., 2.], [2., 4., 6.], [True]*3, 0.0)
    add(f'(mil-fit-test:no-gain {bytes_source(small)} (ic:fit Numeric:MIL {bytes_source(small)} {bindings} () 256))',
        ['MILNoGain', 'true', '4', '0', '0', 'RegionExhausted', 'IC:NoFault'])
    add(f'(ic-numeric:program-candidate IC:ArrayArchive {source} {bytes_source(program)})',
        ['IC:ProducerFault', 'Original'])
    add(f'(ic-numeric:program-candidate IC:NumericArchive {bytes_source(matrix_original)} {bytes_source(program)})',
        ['IC:ProducerFault', 'Original'])
    add(f'(ic:prepare-fit Numeric:MIL {source} {bindings} (NotASpace))',
        ['IC:SearchRefused', 'InvalidSources'])
    add(f'(mil-fit-test:changed-source {prepared})', ['IC:ResumeRejected', 'ChangedDependencies'])
    text = '\n'.join(code)+'\n'
    check_controls(text)
    path = args.output/'mil-fit.metta'
    path.write_text(text)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=120)
    (args.output/'stdout.log').write_text(result.stdout)
    (args.output/'stderr.log').write_text(result.stderr)
    actual = [form for form in parse_forms(result.stdout) if isinstance(form, list) and form and form[0] == 'MILFitCase']
    errors = [{'case': i, 'wanted': wanted, 'actual': found}
              for i, (wanted, found) in enumerate(itertools.zip_longest(wants, actual))
              if normalized(wanted) != normalized(found)]
    receipt = {'passed': result.returncode == 0 and not errors, 'checks': len(wants), 'observations': len(actual),
               'errors': errors, 'original_bytes': len(original), 'best_bytes': len(expected),
               'scope': 'typed immutable MIL vocabulary, complete NC1/NC2/TP1 costs, resumed common IC search and new-input prediction'}
    (args.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
