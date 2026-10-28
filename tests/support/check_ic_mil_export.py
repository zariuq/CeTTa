#!/usr/bin/env python3
"""Checked MP1 export, independent source costs and fresh-process execution."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor
from check_binary_value import Text
from check_ic_context_library import candidate, criterion_seeds
from check_ic_mil_program import artifact, evaluate
from check_ic_mil_source import background_bytes, family
from check_ic_search import check_controls
from check_portable_value import ArrayValue
from check_source_codec import encode as source_bytes, reference


def literal(value):
    if isinstance(value, Text):
        return json.dumps(str(value), ensure_ascii=False)
    if isinstance(value, list):
        return '(' + ' '.join(map(literal, value)) + ')'
    return str(value)


def observed(value):
    if isinstance(value, Text):
        return json.dumps(str(value), ensure_ascii=False)
    if isinstance(value, list):
        return [observed(item) for item in value]
    return str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.cetta.resolve()
    records, definitions, checks = [], [], 0
    imports = '!(import! &self (library lib_ic_mil_export))\n'
    helper = '''(= (export-test:view $original $model $name)
      (case (ic:export-library Source:MIL $original $model $name)
        (((MIL.ProgramExport $actual $document $text $bytes $costs)
          (Exported $actual $text $costs (== $bytes $model)
            (mil-export:declared? (Syntax:Reader "petta" "1.0.6" "") $actual))) ($refused $refused))))
(= (export-test:learn $original $expected $name $skeleton)
   (case (mil-program:unpack $expected)
     (((MIL.Task $header $type $mode $inputs $negative $forbidden (MIL.SourceProgram $background $program))
       (case (ic:fit Source:MIL $original (MIL.SourceRegion $background ($skeleton)) () 4096)
         (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
           (let $closed (ic:release $state)
             (if (and (== $best $expected) (and (> $seen 0) (and (> $accepted 0)
                    (and (== $faults 0) (== $reason RegionExhausted)))))
               (export-test:view $original $best $name) (MIL.ExportRefused Discovery))))
          ($failed $failed)))) ($failed $failed))))\n'''

    def run(label, code, marker):
        check_controls(code)
        path = args.output / (label + '.metta')
        path.write_text(code)
        process = subprocess.run([str(binary), '--lang', 'petta', str(path.resolve())],
                                 cwd=ROOT, capture_output=True, text=True, timeout=120)
        (args.output / (label + '.stdout')).write_text(process.stdout)
        (args.output / (label + '.stderr')).write_text(process.stderr)
        assert process.returncode == 0 and not process.stderr, (label, process.returncode, process.stderr)
        assert not any(line.strip() == '"EXPORT-DATA-MUST-NOT-RUN"' for line in process.stdout.splitlines()), label
        rows = [row for row in parse_forms(process.stdout) if isinstance(row, list) and row[:1] == [marker]]
        assert len(rows) == 1, (label, rows, process.stdout[-1500:])
        return rows[0]

    def export(label, original, model, name, skeleton=None):
        nonlocal checks
        operation = 'view' if skeleton is None else 'learn'
        extra = '' if skeleton is None else ' ' + literal(skeleton)
        code = imports + helper + f'!(println! (ExportCase (export-test:{operation} {tensor(original)} {tensor(model)} {json.dumps(name, ensure_ascii=False)}{extra})))\n'
        row = run(label + '-export', code, 'ExportCase')[1]
        assert row[0] == 'Exported' and len(row) == 6 and row[4:] == ['true', 'false'], row
        assert json.loads(row[1]) == name
        text = json.loads(row[2], strict=False)
        document = reference(text)
        costs = ['MIL.ExportCosts', str(len(original)), str(len(model)), str(len(source_bytes(document))), str(len(text.encode()))]
        assert row[3] == costs, (label, row[3], costs)
        assert len(document[3]) == 2 and len(parse_forms(text)) == 2, label
        assert all(name not in text for name in ('mil-export:', 'mil-program:', 'mil:run', 'ic:predict'))
        check_controls(text)
        checks += 5
        return text, [int(item) for item in costs[1:]]

    def qualify(label, background, target, program, positive, negative, mode, inputs, primitives):
        nonlocal checks
        original = artifact(target, positive, negative, mode)
        model = artifact(target, positive, negative, mode, background, program)
        assert len(model) < len(original), (label, len(model), len(original))
        name = 'ic-learned:export-' + label
        def template(node):
            if node[0] == 'MIL.SourceRef':
                return ['MIL.Hole', 'primitive' + str(node[1])]
            return [node[0], *[template(child) if isinstance(child, list) else child for child in node[1:]]]
        searched = label in ('reference', 'chain', 'array-kind')
        text, costs = export(label, original, model, name, template(program) if searched else None)
        for route in ('ordinary', 'raw'):
            # Fresh processes import no IC library and deliberately define
            # conflicting ambient helper names. The exported vocabulary is lexical.
            ambient = '(= (ic-learned:shift $x) 999) (= (ic-learned:criterion $x) 999)\n'
            queries = '\n'.join(f'!(println! (ExportAnswers {i} (collapse ({name} {literal(value)}))))'
                                for i, value in enumerate(inputs))
            path = args.output / (label + '-' + route + '.metta')
            path.write_text(text + '\n' + ambient + queries + '\n')
            environment = dict(os.environ)
            if route == 'raw':
                environment['CETTA_PREPARED_PURE_REFERENCE'] = '1'
            else:
                environment.pop('CETTA_PREPARED_PURE_REFERENCE', None)
            process = subprocess.run([str(binary), '--lang', 'petta', str(path.resolve())], cwd=ROOT,
                                     env=environment, capture_output=True, text=True, timeout=60)
            (args.output / (label + '-' + route + '.stdout')).write_text(process.stdout)
            (args.output / (label + '-' + route + '.stderr')).write_text(process.stderr)
            assert process.returncode == 0 and not process.stderr, (label, route, process.stderr)
            assert not any(line.strip() == '"EXPORT-DATA-MUST-NOT-RUN"' for line in process.stdout.splitlines())
            actual = [row for row in parse_forms(process.stdout) if isinstance(row, list) and row[:1] == ['ExportAnswers']]
            expected = [['ExportAnswers', str(i), observed(evaluate(program, value, primitives))]
                        for i, value in enumerate(inputs)]
            assert [row[:2] for row in actual] == [row[:2] for row in expected], (label, route, actual)
            for got, wanted in zip(actual, expected):
                bag = lambda values: Counter(json.dumps(value, ensure_ascii=False) for value in values)
                assert bag(got[2]) == bag(wanted[2]), (label, route, got, wanted)
            checks += len(expected)
        records.append({'name': label, 'original_bytes': costs[0], 'model_bytes': costs[1],
                        'export_source_bytes': costs[2], 'rendered_utf8_bytes': costs[3], 'new_inputs': len(inputs),
                        'native_typed_search_winner': searched})
        definitions.append((label, original, model, text))
        return original, model

    document = reference('(= (ic-learned:shift $x) (+ $x 1.25)) (= (ic-learned:double $x) (* $x 2))')
    background = background_bytes(family([('ic-learned:shift', 'Number', 'Number'), ('ic-learned:double', 'Number', 'Number')]), document)
    primitives = [lambda x: [x + 1.25], lambda x: [x * 2]]
    target = ['MIL.Rel', 'Number', 'Number']
    for label, program in [
        ('reference', ['MIL.SourceRef', 0]),
        ('identity', ['MIL.Id', ['MIL.SourceRef', 0]]),
        ('chain', ['MIL.Chain', ['MIL.SourceRef', 0], ['MIL.SourceRef', 1]]),
        ('duplicates', ['MIL.Choice', ['MIL.SourceRef', 0], ['MIL.SourceRef', 0]]),
    ]:
        positive = [(x, evaluate(program, x, primitives)[0]) for x in range(96)]
        qualify(label, background, target, program, positive, [], 'MIL.Functional',
                [-3.0, 0.5, 2.0, -3.0], primitives)

    for label, program in [
        ('map', ['MIL.Map', ['MIL.SourceRef', 0]]),
        ('higher-order', ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', ['MIL.SourceRef', 0], ['MIL.SourceRef', 1]]]),
        ('cartesian', ['MIL.Map', ['MIL.Choice', ['MIL.SourceRef', 0], ['MIL.SourceRef', 1]]]),
    ]:
        rows = [[0.0, 1.0, 0.0]] * 96
        positive = [(row, evaluate(program, row, primitives)[0]) for row in rows]
        qualify(label, background, ['MIL.Rel', ['MIL.List', 'Number'], ['MIL.List', 'Number']],
                program, positive, [], 'MIL.Relational', [[], [0.0], [-1.25, 2.0, -1.25]], primitives)

    identity = background_bytes(family([('ic-learned:identity', 'Data', 'Data')]), reference('(= (ic-learned:identity $x) $x)'))
    datum = ['println!', Text('EXPORT-DATA-MUST-NOT-RUN')]
    qualify('value-barrier', identity, ['MIL.Rel', 'Data', 'Data'], ['MIL.SourceRef', 0],
            [(datum, datum)] * 96, [], 'MIL.Functional', [datum, ['Error', 'origin', Text('literal')], []], [lambda x: [x]])

    dependent = background_bytes(family([('ic-learned:shift', 'Number', 'Number'), ('ic-learned:twice', 'Number', 'Number')]),
                                reference('(= (ic-learned:shift $x) (+ $x 1.25)) '
                                          '(= (ic-learned:twice $x) (ic-learned:shift (ic-learned:shift $x)))'))
    qualify('dependencies', dependent, target, ['MIL.SourceRef', 1], [(x, x + 2.5) for x in range(96)], [],
            'MIL.Functional', [-3.0, 0.5, 2.0], [lambda x: [x + 1.25], lambda x: [x + 2.5]])

    pair = background_bytes(family([('ic-learned:add-pair', 'Pair', 'Number')]),
                            reference('(= (ic-learned:add-pair $x $y) (+ $x $y))'))
    qualify('argument-pair', pair, ['MIL.Rel', 'Pair', 'Number'], ['MIL.SourceRef', 0],
            [(['MIL.Args', x, 2], x + 2) for x in range(96)], [], 'MIL.Functional',
            [['MIL.Args', 3, 4], ['MIL.Args', -2.0, 0.5]], [lambda row: [row[1] + row[2]]])

    constant = background_bytes(family([('ic-learned:constant', 'Unit', 'Data')]),
                                reference('(= (ic-learned:constant) (quote (Error origin "literal")))'))
    label = ['Error', 'origin', Text('literal')]
    qualify('zero-argument', constant, ['MIL.Rel', 'Unit', 'Data'], ['MIL.SourceRef', 0],
            [(['MIL.Args'], label)] * 96, [], 'MIL.Functional', [['MIL.Args']], [lambda _: [label]])

    partial = background_bytes(family([('ic-learned:reciprocal', 'Number', 'Number')]),
                               reference('(= (ic-learned:reciprocal $x) (if (== $x 0) (superpose ()) (/ 1.0 $x)))'))
    reciprocal = [lambda x: [] if x == 0 else [1.0 / x]]
    qualify('map-failure', partial, ['MIL.Rel', ['MIL.List', 'Number'], ['MIL.List', 'Number']],
            ['MIL.Map', ['MIL.SourceRef', 0]], [([1.0, 2.0], [1.0, 0.5])] * 96, [], 'MIL.Functional',
            [[1.0, 0.0, 2.0], [], [2.0]], reciprocal)

    array_source = reference((ROOT / 'lib/petta/lib_ic_numeric_arrays.metta').read_text())
    first, second = criterion_seeds(array_source)
    extracted, support, _pattern = candidate(array_source, 'ic-learned:criterion', first, second)
    dictionary = deepcopy(extracted)
    dictionary[3] = dictionary[3][:1] + reference('(= (ic-learned:exact $x) (== $x (IC:Some Numeric:Exact)))')[3]
    declarations = family([('ic-learned:criterion', 'TensorKind', 'Criterion'), ('ic-learned:exact', 'Criterion', 'Bool')],
                          [('IC:Some', 1), ('IC:None', 0), ('Numeric:Exact', 0)])
    array = background_bytes(declarations, dictionary)
    kind_primitives = [lambda kind: [['IC:Some', 'Numeric:Exact'] if kind in ('BoolTensor', 'I64Tensor') else 'IC:None'],
                       lambda value: ['true' if value == ['IC:Some', 'Numeric:Exact'] else 'false']]
    program = ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', ['MIL.SourceRef', 0], ['MIL.SourceRef', 1]]]
    qualify('array-kind', array, ['MIL.Rel', ['MIL.List', 'TensorKind'], ['MIL.List', 'Bool']], program,
            [([Text('BoolTensor'), Text('I64Tensor')], ['true', 'true'])] * 96,
            [([Text('F64Tensor')], ['true'])], 'MIL.Relational',
            [[Text('F64Tensor'), Text('Unknown'), Text('BoolTensor'), Text('I64Tensor'), Text('BoolTensor')], []], kind_primitives)

    _, original, model, _ = definitions[0]
    original_call, model_call = tensor(original), tensor(model)
    imported = args.output / 'imported-name-owner.metta'
    imported.write_text('(: ic-learned:collision (-> Atom Atom))\n')
    refusals = [
        ('bad-name', '', '"ic-learned:x) (println! BAD)"', model_call, ['MIL.ExportRefused', 'Name']),
        ('native-name', '', '"println!"', model_call, ['MIL.ExportRefused', 'Name']),
        ('equation-collision', '(= (ic-learned:collision $x) $x)', '"ic-learned:collision"', model_call,
         ['MIL.ExportRefused', 'NameOrEnvironment']),
        ('type-collision', '(: ic-learned:collision (-> Atom Atom))', '"ic-learned:collision"', model_call,
         ['MIL.ExportRefused', 'NameOrEnvironment']),
        ('imported-name', f'!(import! &self {json.dumps(str(imported.resolve()))})', '"ic-learned:collision"', model_call,
         ['MIL.ExportRefused', 'NameOrEnvironment']),
        ('background-name', '', '"ic-learned:shift"', model_call, ['MIL.ExportRefused', 'NameOrEnvironment']),
        ('map-override', '(= (map-atom $items $item $body) BAD)', '"ic-learned:safe"', model_call,
         ['MIL.ExportRefused', 'Environment']),
        ('literal-baseline', '', '"ic-learned:safe"', original_call, ['MIL.ExportRefused', 'NotSmaller']),
        ('invalid-model', '', '"ic-learned:safe"', '(tensor:from-values u8 (1) (0))', ['MIL.ExportRefused', 'InvalidArtifact']),
    ]
    for label, extra, name, proposed, expected in refusals:
        code = imports + extra + f'\n!(println! (Refusal (ic:export-library Source:MIL {original_call} {proposed} {name})))\n'
        assert run(label, code, 'Refusal')[1] == expected, label
        checks += 1
    assert imported.read_text() == '(: ic-learned:collision (-> Atom Atom))\n'
    # Renaming is presentation, but both occurrences of the public name and
    # every byte in the rendered source are charged again.
    renamed_text, renamed_costs = export('unicode-rename', original, model, 'ic-learned:λ-longer-name')
    original_costs = records[0]
    assert renamed_costs[2] != original_costs['export_source_bytes']
    assert renamed_costs[3] != original_costs['rendered_utf8_bytes']
    renamed_result = run('unicode-rename-execution', renamed_text +
                         '\n!(println! (Renamed (collapse (ic-learned:λ-longer-name 0.5))))\n', 'Renamed')
    assert renamed_result == ['Renamed', ['1.75']], renamed_result
    checks += 1

    effect_background = background_bytes(family([('ic-learned:effect', 'Number', 'Number')]),
        reference('(= (ic-learned:effect $x) (println! "EXPORT-DATA-MUST-NOT-RUN"))'))
    effect_model = artifact(target, [(x, x + 1.25) for x in range(96)], [], 'MIL.Functional',
                            effect_background, ['MIL.SourceRef', 0])
    effect_code = (imports + f'!(println! (Refusal (ic:export-library Source:MIL {original_call} '
                   f'{tensor(effect_model)} "ic-learned:effect-export")))\n')
    assert run('effect-refusal', effect_code, 'Refusal') == ['Refusal', ['MIL.ExportRefused', 'InvalidArtifact']]
    checks += 1

    divide_background = background_bytes(family([('ic-learned:divide', 'Number', 'Number')]),
        reference('(= (ic-learned:divide $x) (/ 1 $x))'))
    divide_positive = [(value, 1.0 / value) for value in [1.0, 2.0, 4.0, 8.0] * 24]
    divide_original = artifact(target, divide_positive, [], 'MIL.Functional')
    divide_model = artifact(target, divide_positive, [], 'MIL.Functional', divide_background, ['MIL.SourceRef', 0])
    divide_text, _ = export('divide', divide_original, divide_model, 'ic-learned:divide-export')
    fault_helper = '''(= (export-test:faulted $result)
      (case $result (((ExportCompleted $answers) False) ($raised True))))\n'''
    original_fault = imports + fault_helper + f'''!(println! (FaultRows
      (collapse (case (ic:prepare-background Source:MIL {tensor(divide_background)} ())
        (((MIL.SourcePrepared $family $document $types $semantics)
          (export-test:faulted (catch (ExportCompleted (mil:run $semantics (MIL.SourceRef 0) 0))))))))))\n'''
    exported_fault = divide_text + '\n' + fault_helper + '''!(println! (FaultRows
      (collapse (export-test:faulted (catch (ExportCompleted (ic-learned:divide-export 0)))))))\n'''
    assert run('original-fault', original_fault, 'FaultRows') == ['FaultRows', []]
    assert run('exported-fault', exported_fault, 'FaultRows') == ['FaultRows', []]
    checks += 2
    # collapse has its own failure observation; compare that observation too,
    # rather than confusing it with the uncollapsed relation's raised result.
    assert run('original-collapsed-fault', original_fault.replace('(mil:run ', '(mil:outputs '), 'FaultRows') == ['FaultRows', ['false']]
    assert run('exported-collapsed-fault', exported_fault.replace('(ic-learned:divide-export 0)',
               '(collapse (ic-learned:divide-export 0))'), 'FaultRows') == ['FaultRows', ['false']]
    checks += 2
    # Floating exception policy belongs to the active arithmetic service.
    # Core builds may retain IEEE infinity while SWI-backed PeTTa can raise
    # under its float flags.  The MIL relation gives an inner raise its own
    # failure observation, distinct from the direct primitive checked below;
    # source interpretation and export must agree under either policy.
    original_infinity = run('original-infinity',
        original_fault.replace('(MIL.SourceRef 0) 0)', '(MIL.SourceRef 0) 0.0)'), 'FaultRows')
    exported_infinity = run('exported-infinity',
        exported_fault.replace('divide-export 0)', 'divide-export 0.0)'), 'FaultRows')
    assert original_infinity == exported_infinity
    checks += 2
    direct_fault = fault_helper + '''!(println! (FaultRows
      (collapse (export-test:faulted (catch (ExportCompleted (/ 1 0)))))))\n'''
    assert run('direct-primitive-fault', direct_fault, 'FaultRows') == ['FaultRows', ['true']]
    checks += 1
    packed_value = ArrayValue('f64', (3,), (0.0, -0.0, 1.25))
    tensor_background = background_bytes(family([('ic-learned:identity', 'Tensor', 'Tensor')]),
                                        reference('(= (ic-learned:identity $x) $x)'))
    tensor_type = ['MIL.Rel', 'Tensor', 'Tensor']
    tensor_examples = [(packed_value, packed_value)] * 32
    tensor_original = artifact(tensor_type, tensor_examples, [], 'MIL.Functional')
    tensor_model = artifact(tensor_type, tensor_examples, [], 'MIL.Functional', tensor_background, ['MIL.SourceRef', 0])
    tensor_text, _ = export('packed-value', tensor_original, tensor_model, 'ic-learned:packed-export')
    tensor_check = tensor_text + '''
!(import! &self (library tensor))
!(let* (($value (tensor:from-values f64 (3) (0.0 -0.0 1.25)))
        ($result (ic-learned:packed-export $value)))
   (println! (PackedValue (== (tensor:encode $result) (tensor:encode $value)))))
'''
    assert run('packed-value-execution', tensor_check, 'PackedValue') == ['PackedValue', 'true']
    checks += 1
    check_controls((ROOT / 'lib/petta/lib_ic_mil_export.metta').read_text())
    receipt = {'passed': True, 'checks': checks, 'workloads': records, 'real_source_support': support,
               'fresh_processes_without_ic_imports': True, 'source_costs_independently_checked': True,
               'automatic_installation': False, 'unseen_partial_and_ieee_outcomes_checked': True, 'runtime_speedup_claimed': False,
               'scope': 'checked finite source models, lexical standalone exports, explicit value demand, answer multiplicity and sequence positions, new inputs and charged renaming; no universal generalization claim'}
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
