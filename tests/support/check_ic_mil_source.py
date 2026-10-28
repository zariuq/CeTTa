#!/usr/bin/env python3
"""Source-backed typed MIL, independently framed dictionaries and lexical controls."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor
from check_binary_tree import packed
from check_binary_value import Text, encoded as value_bytes, construct
from check_ic_archive import syntax, uint
from check_ic_context_library import candidate, criterion_seeds, encoded as library_bytes
from check_ic_search import check_controls
from check_ic_library_stack import encoded as joint_library_bytes
from check_source_codec import encode_value, reference


def family(bindings, constructors=()):
    return ['MIL.SourceFamily',
            [['MIL.SourceConstructor', Text(name), arity] for name, arity in constructors],
            [['MIL.SourceBinding', Text(name), ['MIL.Rel', source, target]]
             for name, source, target in bindings]]


def background_bytes(declarations, document):
    return packed(('node', uint(9), [('leaf', b'SB1'),
                    ('leaf', value_bytes(declarations)), encode_value('Syntax:Document', document)]))


def canonical(term):
    inventory = []
    def visit(value):
        if value[0] == 'Syntax:Variable':
            index = int(value[1])
            if index not in inventory:
                inventory.append(index)
            return ['Syntax:Variable', str(inventory.index(index))]
        if value[0] == 'Syntax:Expr':
            return ['Syntax:Expr', [visit(child) for child in value[1]]]
        return deepcopy(value)
    return visit(term), inventory


HELPERS = r'''
!(import! &self (library lib_ic_mil_source))
(= (source-mil-test:prepared $bytes) (ic:prepare-background Source:MIL $bytes ()))
(= (source-mil-test:view $prepared $program $input)
   (case $prepared
     (((MIL.SourcePrepared $family $document $types $sem) (ic:run-background $prepared $program $input))
      ($refused $refused))))
(= (source-mil-test:types $prepared $skeleton $type)
   (case $prepared
     (((MIL.SourcePrepared $family $document $types $sem)
       (collapse (mil:prime:typed-fill-vocabulary $types $skeleton $type)))
      ($refused $refused))))
(= (source-mil-test:learn $prepared $skeleton $type $examples)
   (case $prepared
     (((MIL.SourcePrepared $family $document $types $sem)
       (collapse
         (let $program (mil:prime:typed-fill-vocabulary $types $skeleton $type)
           (if (mil:consistent $sem $program $examples) $program (superpose ())))))
      ($refused $refused))))
(= (source-mil-test:repack $bytes)
   (case (mil-source:unpack $bytes)
     (((MIL.SourceBundle $family $document)
       (case (mil-source:pack $family $document)
         (((Binary:TreeBytes $after) (== $bytes $after)) ($refused $refused))))
      ($refused $refused))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('lib_ic_mil_source.metta', 'lib_source_context.metta', 'lib_ic_context_library.metta'):
        source = (ROOT/'lib/petta'/name).read_text()
        check_controls(source)
        definitions = [row for row in parse_forms(source) if isinstance(row, list) and row[:1] == ['=']]
        assert len(definitions) == source.count('(= ('), name
        assert all(len(row) == 3 for row in definitions), name
    code, expected, wires, corpus = [HELPERS], [], [], []

    def add(call, result):
        index = len(expected)
        code.append(f'!(println! (SourceMILCase {index} {call}))')
        expected.append(['SourceMILCase', str(index), result])

    def prepared(text, declarations):
        blob = background_bytes(declarations, reference(text))
        wires.append({'bytes': len(blob), 'definitions': len(reference(text)[3])})
        return f'(source-mil-test:prepared {tensor(blob)})', blob

    def outputs(preparation, program, value, expected_outputs):
        add(f'(source-mil-test:view {preparation} {program} {value})',
            ['MIL.SourceOutputs', expected_outputs])

    # Source-frame canonicalization observes all syntax, not just executable
    # positions. Its variable identities are private ordinals, not host vars.
    for term in [
        ['Syntax:Variable', '99'],
        ['Syntax:Expr', [['Syntax:Variable', '9'], ['Syntax:Variable', '2'], ['Syntax:Variable', '9']]],
        reference('(quote (pair $x $y $x))')[3][0][2],
        reference('(|-> ($x) (let $y $x (pair $y $x $y)))')[3][0][2],
    ]:
        after, inventory = canonical(term)
        add(f'(== (mil-source:canonical {syntax(term)} ()) '
            f'(MIL.SourceCanonical {syntax(after)} ({" ".join(map(str,inventory))})))', 'true')

    increment = family([('ic-learned:inc', 'Number', 'Number')])
    inc, inc_blob = prepared('(= (ic-learned:inc $x) (+ $x 1))', increment)
    add(f'(source-mil-test:repack {tensor(inc_blob)})', 'true')
    add(f'(== (mil-source:unpack {tensor(inc_blob)}) '
        f'(MIL.SourceBundle {construct(increment)} {syntax(reference("(= (ic-learned:inc $x) (+ $x 1))"))}))', 'true')
    outputs(inc, '(MIL.SourceRef 0)', '4', ['5'])
    chain = '(MIL.Chain (MIL.SourceRef 0) (MIL.SourceRef 0))'
    outputs(inc, chain, '4', ['6'])
    outputs(inc, '(MIL.Choice (MIL.SourceRef 0) (MIL.SourceRef 0))', '4', ['5', '5'])
    outputs(inc, '(MIL.Map (MIL.SourceRef 0))', '(1 3 1)', [['2', '4', '2']])
    outputs(inc, '(MIL.HO1 MIL.MapOp (MIL.SourceRef 0))', '()', [[]])
    outputs(inc, '(MIL.SourceRef 9)', '4', [])
    shared = '(MIL.Chain (MIL.Hole p) (MIL.Hole p))'
    add(f'(source-mil-test:types {inc} {shared} (MIL.Rel Number Number))',
        [['MIL.Chain', ['MIL.SourceRef', '0'], ['MIL.SourceRef', '0']]])
    add(f'(source-mil-test:types {inc} {shared} (MIL.Rel Text Number))', [])
    examples = '(MIL.Examples ((MIL.Ex 3 5) (MIL.Ex 12 14)) ((MIL.Ex 3 4)) MIL.Functional)'
    add(f'(source-mil-test:learn {inc} {shared} (MIL.Rel Number Number) {examples})',
        [['MIL.Chain', ['MIL.SourceRef', '0'], ['MIL.SourceRef', '0']]])
    add(f'(source-mil-test:learn {inc} {shared} (MIL.Rel Number Number) '
        '(MIL.Examples ((MIL.Ex 3 5)) ((MIL.Ex 3 5)) MIL.Functional))', [])
    outputs(inc, chain, '101', ['103'])
    identity, _ = prepared('(= (ic-learned:identity $x) $x)',
                           family([('ic-learned:identity','Value','Value')]))
    outputs(identity, '(MIL.SourceRef 0)', '7', ['7'])
    delayed_identity, _ = prepared('(= (ic-learned:force $x) ($x))',
                                   family([('ic-learned:force','Value','Value')]))
    outputs(delayed_identity, '(MIL.SourceRef 0)', '7', ['7'])

    pair, _ = prepared('(= (ic-learned:sum $x $y) (+ $x $y))',
                       family([('ic-learned:sum', 'Pair', 'Number')]))
    outputs(pair, '(MIL.SourceRef 0)', '(MIL.Args 3 4)', ['7'])
    outputs(pair, '(MIL.SourceRef 0)', '(MIL.Args 3)', [])
    outputs(pair, '(MIL.SourceRef 0)', '(3 4)', [])
    constant, _ = prepared('(= (ic-learned:constant) 7)',
                           family([('ic-learned:constant', 'Unit', 'Number')]))
    outputs(constant, '(MIL.SourceRef 0)', '(MIL.Args)', ['7'])
    outputs(constant, '(MIL.SourceRef 0)', '(MIL.Args 8)', [])
    delayed, _ = prepared('(= (ic-learned:twice $x) (+ ($x) ($x)))',
                          family([('ic-learned:twice', 'Number', 'Number')]))
    outputs(delayed, '(MIL.SourceRef 0)', '6', ['12'])
    choices, _ = prepared('(= (ic-learned:dup $x) (superpose ($x (+ $x 1) $x)))',
                          family([('ic-learned:dup', 'Number', 'Number')]))
    outputs(choices, '(MIL.SourceRef 0)', '4', ['4', '5', '4'])
    outputs(choices, '(MIL.Chain (MIL.SourceRef 0) (MIL.SourceRef 0))', '4',
            ['4', '5', '4', '5', '6', '5', '4', '5', '4'])
    absence, _ = prepared('(= (ic-learned:partial $x) (if (> $x 0) $x (superpose ())))',
                          family([('ic-learned:partial', 'Number', 'Number')]))
    outputs(absence, '(MIL.SourceRef 0)', '0', [])
    outputs(absence, '(MIL.SourceRef 0)', '3', ['3'])

    dependencies, _ = prepared(
        '(= (ic-learned:use $x) (ic-learned:twice (|-> () (+ $x 1)))) '
        '(= (ic-learned:twice $x) (+ ($x) ($x)))',
        family([('ic-learned:use', 'Number', 'Number')]))
    outputs(dependencies, '(MIL.SourceRef 0)', '3', ['8'])
    quoted, _ = prepared(
        '(= (ic-learned:quoted) (quote (ic-learned:inc 9))) '
        '(= (ic-learned:inc $x) (+ $x 1))',
        family([('ic-learned:quoted', 'Unit', 'Syntax')]))
    outputs(quoted, '(MIL.SourceRef 0)', '(MIL.Args)', [['ic-learned:inc', '9']])

    joint = reference(
        '(= (ic-learned:joint-first $x) (+ (ic-learned:joint-second 3) $x)) '
        '(= (ic-learned:joint-second $x) (* $x 2)) '
        '(= (receiver $x) (ic-learned:joint-first $x)) '
        '(= (not-a-library-helper $x) (println! $x))')
    joint_family = family([('ic-learned:joint-first', 'Number', 'Number')])
    joint_dictionary = deepcopy(joint)
    joint_dictionary[3] = joint_dictionary[3][:2]
    joint_sb = background_bytes(joint_family, joint_dictionary)
    joint_artifact = joint_library_bytes([('LC2', 'ic-learned:joint-first'),
                                          ('LC2', 'ic-learned:joint-second')], joint)
    add(f'(case (ic:source-background {tensor(joint_artifact)} {construct(joint_family)}) '
        f'(((MIL.SourceBytes $bytes) (== $bytes {tensor(joint_sb)})) ($bad $bad)))', 'true')
    outputs(f'(source-mil-test:prepared {tensor(joint_sb)})', '(MIL.SourceRef 0)', '4', ['10'])

    for text, result in [
        ('(= (ic-learned:unused $x) 7)', 'UnsupportedInterface'),
        ('(= (ic-learned:mixed $x) (+ $x ($x)))', 'UnsupportedInterface'),
        ('(= (ic-learned:unknown $x) (unknown-external $x))', 'UnsupportedBody'),
        ('(= (ic-learned:capture $x) (let $y $x $y))', 'UnsupportedBody'),
        ('(= (ic-learned:self $x) (ic-learned:self $x))', 'Dependencies'),
    ]:
        name = parse_forms(text)[0][1][0]
        failed, _ = prepared(text, family([(name, 'Number', 'Number')]))
        add(failed, ['MIL.SourceRefused', result])
    duplicate_bindings = family([('ic-learned:inc', 'Number', 'Number')]*2)
    failed, _ = prepared('(= (ic-learned:inc $x) (+ $x 1))', duplicate_bindings)
    add(failed, ['MIL.SourceRefused', 'Dependencies'])
    failed, _ = prepared('(= (ic-learned:inc $x) (+ $x 1))', family([('absent', 'Number', 'Number')]))
    add(failed, ['MIL.SourceRefused', 'Dependencies'])
    add(f'(source-mil-test:prepared {tensor(inc_blob[:-1])})', ['MIL.SourceRefused', 'Preparation'])
    wrong_reader = deepcopy(reference('(= (ic-learned:inc $x) (+ $x 1))'))
    wrong_reader[2][2] = json.dumps('0.0.0')
    add(f'(source-mil-test:prepared {tensor(background_bytes(increment, wrong_reader))})',
        ['MIL.SourceRefused', 'Family'])

    # The real helper is extracted afresh, rather than replaced by a hand-coded
    # callable. Its complete unchanged definition becomes the SB1 vocabulary.
    for relative, seed, declarations, runs in [
        ('lib/petta/lib_pln.metta', (11, 16), family([('ic-learned:test', 'Triple', 'Number')]),
         [('(MIL.Args 1.0 1.0 1.0)', ['1.0']), ('(MIL.Args 0.0 1.0 1.0)', ['0.5'])]),
        ('lib/petta/lib_ic_numeric_arrays.metta', None,
         family([('ic-learned:test', 'TensorKind', 'Criterion')], [('IC:Some', 1), ('IC:None', 0), ('Numeric:Exact', 0)]),
         [('\"BoolTensor\"', [['IC:Some', 'Numeric:Exact']]), ('\"I64Tensor\"', [['IC:Some', 'Numeric:Exact']]),
          ('\"F64Tensor\"', ['IC:None']), ('\"Unknown\"', ['IC:None'])]),
    ]:
        source = reference((ROOT/relative).read_text())
        first, second = seed or criterion_seeds(source)
        learned, support, _pattern = candidate(source, 'ic-learned:test', first, second)
        dictionary = deepcopy(learned)
        dictionary[3] = dictionary[3][:1]
        sb = background_bytes(declarations, dictionary)
        artifact = library_bytes('ic-learned:test', learned)
        add(f'(case (ic:source-background {tensor(artifact)} {construct(declarations)}) '
            f'(((MIL.SourceBytes $bytes) (== $bytes {tensor(sb)})) ($bad $bad)))', 'true')
        add(f'(case (context-library:seed (ic:prepare-library Deferred {syntax(source)}) '
            f'"ic-learned:test" {first} {second}) '
            f'(((Library:Candidate $artifact $support) '
            f'  (case (ic:source-background $artifact {construct(declarations)}) '
            f'    (((MIL.SourceBytes $bytes) (== $bytes {tensor(sb)})) ($bad $bad)))) ($bad $bad)))', 'true')
        prep = f'(source-mil-test:prepared {tensor(sb)})'
        for value, result in runs:
            outputs(prep, '(MIL.SourceRef 0)', value, result)
        if 'numeric_arrays' in relative:
            outputs(prep, '(MIL.Map (MIL.SourceRef 0))',
                    '("F64Tensor" "BoolTensor" "Unknown" "I64Tensor" "BoolTensor")',
                    [['IC:None', ['IC:Some', 'Numeric:Exact'], 'IC:None', ['IC:Some', 'Numeric:Exact'], ['IC:Some', 'Numeric:Exact']]])
            composition = deepcopy(dictionary)
            composition[3].extend(reference(
                '(= (ic-learned:exact? $x) (== $x (IC:Some Numeric:Exact))) '
                '(= (ic-learned:leak $x) (superpose ((== $x $x) False)))')[3])
            composition_family = family([
                ('ic-learned:test', 'TensorKind', 'Criterion'),
                ('ic-learned:exact?', 'Criterion', 'Bool'),
                ('ic-learned:leak', 'Criterion', 'Bool')],
                [('IC:Some', 1), ('IC:None', 0), ('Numeric:Exact', 0)])
            composed = f'(source-mil-test:prepared {tensor(background_bytes(composition_family,composition))})'
            skeleton = '(MIL.HO1 MIL.MapOp (MIL.Chain (MIL.Hole classify) (MIL.Hole test)))'
            examples = ('(MIL.Examples '
                        '((MIL.Ex ("BoolTensor" "I64Tensor") (True True))) '
                        '((MIL.Ex ("BoolTensor") (False))) MIL.Relational)')
            exact_program = ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', ['MIL.SourceRef','0'], ['MIL.SourceRef','1']]]
            leaking_program = ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', ['MIL.SourceRef','0'], ['MIL.SourceRef','2']]]
            add(f'(source-mil-test:learn {composed} {skeleton} '
                '(MIL.Rel (MIL.List TensorKind) (MIL.List Bool)) '
                '(MIL.Examples ((MIL.Ex ("BoolTensor" "I64Tensor") (True True))) () MIL.Relational))',
                [exact_program, leaking_program])
            add(f'(source-mil-test:learn {composed} {skeleton} '
                f'(MIL.Rel (MIL.List TensorKind) (MIL.List Bool)) {examples})',
                [exact_program])
            outputs(composed, '(MIL.HO1 MIL.MapOp (MIL.Chain (MIL.SourceRef 0) (MIL.SourceRef 1)))',
                    '("F64Tensor" "Unknown" "BoolTensor" "I64Tensor" "BoolTensor")',
                    [['false','false','true','true','true']])
        corpus.append({'source': relative, 'support': support, 'background_bytes': len(sb)})

    effect, _ = prepared('(= (ic-learned:print $x) (println! (superpose ($x $x))))',
                         family([('ic-learned:print', 'Text', 'Bool')]))
    add(f'(case {effect} (((MIL.SourcePrepared $f $d $t $s) PreparedWithoutEffects) ($bad $bad)))',
        'PreparedWithoutEffects')
    outputs(effect, '(MIL.SourceRef 0)', '"SourceEffect"', ['true', 'true'])
    add(f'(let* (($prepared {inc}) ($changed (add-atom (context-space) source-mil-unrelated))) '
        '(ic:run-background $prepared (MIL.SourceRef 0) 2))', ['MIL.SourceRefused', 'SourceChanged'])
    combined = '\n'.join(code)+'\n'
    check_controls(combined)
    path = args.output/'source-mil.metta'
    path.write_text(combined)
    run = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                         cwd=ROOT, capture_output=True, text=True, timeout=90)
    (args.output/'source-mil.stdout').write_text(run.stdout)
    (args.output/'source-mil.stderr').write_text(run.stderr)
    actual = [row for row in parse_forms(run.stdout) if isinstance(row, list) and row[:1] == ['SourceMILCase']]
    differences = [{'index': index, 'expected': wanted, 'actual': got}
                   for index, (wanted, got) in enumerate(zip(expected, actual)) if wanted != got]
    if len(actual) != len(expected):
        differences.append({'expected_count': len(expected), 'actual_count': len(actual)})
    effects = [row for row in parse_forms(run.stdout) if row == '\"SourceEffect\"']
    isolated = []
    def isolate(label, preamble, source, declarations, value, wanted, static_rejection=False):
        blob = background_bytes(declarations, reference(source))
        control = HELPERS+'\n'+preamble+f'''
!(println! (SourceMILIsolated
   (source-mil-test:view (source-mil-test:prepared {tensor(blob)}) (MIL.SourceRef 0) {value})))
'''
        check_controls(control)
        filename = args.output/f'{label}.metta'
        filename.write_text(control)
        result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(filename.resolve())],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        (args.output/f'{label}.stdout').write_text(result.stdout)
        (args.output/f'{label}.stderr').write_text(result.stderr)
        found = [row for row in parse_forms(result.stdout) if isinstance(row,list) and row[:1] == ['SourceMILIsolated']]
        if static_rejection:
            passed = (result.returncode != 0 and not found and
                      'permission_error modify static_procedure (/ + 3)' in result.stderr)
        else:
            passed = result.returncode == 0 and not result.stderr and found == [['SourceMILIsolated', wanted]]
        isolated.append({'name': label, 'passed': passed, 'actual': found,
                         'static_declaration_rejection': static_rejection})

    isolate('lexical-dependency', '(= (ic-learned:inc $x) 999)',
            '(= (ic-learned:double $x) (ic-learned:inc (ic-learned:inc $x))) '
            '(= (ic-learned:inc $x) (+ $x 1))',
            family([('ic-learned:double', 'Number', 'Number')]), '3', ['MIL.SourceOutputs', ['5']])
    override_family = family([('ic-learned:box', 'Number', 'Box')], [('Box', 1)])
    isolate('constructor-override', '(= (Box $x) 999)', '(= (ic-learned:box $x) (Box $x))',
            override_family, '3', ['MIL.SourceRefused', 'NonstandardOperators'])
    imported = args.output/'imported-override.metta'
    imported.write_text('(= (Box $x) 999)\n')
    isolate('imported-constructor-override', f'!(import! &self {json.dumps(str(imported.resolve()))})',
            '(= (ic-learned:box $x) (Box $x))', override_family, '3',
            ['MIL.SourceRefused', 'NonstandardOperators'])
    isolate('zero-call-override', '(= (MIL.Args) 999)', '(= (ic-learned:constant) 7)',
            family([('ic-learned:constant', 'Unit', 'Number')]), '(MIL.Args)',
            ['MIL.SourceRefused', 'NonstandardOperators'])
    isolate('factory-tag-override', '(= (MIL.SourceCallables $items) 999)',
            '(= (ic-learned:inc $x) (+ $x 1))', increment, '3',
            ['MIL.SourceRefused', 'NonstandardOperators'])
    isolate('builtin-override', '(= (+ $x $y) 999)', '(= (ic-learned:inc $x) (+ $x 1))',
            increment, '3', None, static_rejection=True)
    report = {'passed': run.returncode == 0 and not run.stderr and not differences and len(effects) == 2,
              'checks': len(expected), 'answers': len(actual), 'differences': differences,
              'effect_occurrences': len(effects), 'independent_dictionaries': wires, 'real_source': corpus,
              'isolated_controls': isolated,
              'scope': 'charged source-backed vocabulary, lexical linking, typed MIL filling and finite example observations; no source-program compression acceptance or held-out real-program-family speed claim'}
    report['passed'] = report['passed'] and all(control['passed'] for control in isolated)
    (args.output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))
    if not report['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
