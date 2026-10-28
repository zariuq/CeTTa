#!/usr/bin/env python3
"""Independent shared MIL substitutions and a freshly extracted IC background helper."""

from __future__ import annotations

import argparse
from collections import Counter
import itertools
import json
import os
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_search import check_controls
from check_source_codec import text_arg
from gen_semiring_fixtures import sexpr


PRIMITIVES = ('p', 'q', 'r', 's', 'lists')
DECLARATIONS = [('p', 'A', 'A'), ('q', 'A', 'A'), ('r', 'A', 'B'), ('s', 'B', 'A'),
                ('lists', ('List', 'A'), ('List', 'A'))]
ARITIES = {'MIL.Chain': 2, 'MIL.Choice': 2, 'MIL.Id': 1, 'MIL.Closure': 1, 'MIL.Map': 1}


def hole(name):
    return ['MIL.Hole', name]


def children(term):
    if isinstance(term, str) or term[:1] == ['MIL.Hole']:
        return []
    if term[:2] == ['MIL.HO1', 'MIL.MapOp'] and len(term) == 3:
        return term[2:]
    if term and term[0] in ARITIES and len(term) == ARITIES[term[0]] + 1:
        return term[1:]
    return None


def holes(term):
    if isinstance(term, list) and len(term) == 2 and term[0] == 'MIL.Hole':
        return [term[1]]
    parts = children(term)
    return None if parts is None else [key for child in parts for key in holes(child)]


def instantiate(term, assignment):
    if isinstance(term, str):
        return term
    if term[0] == 'MIL.Hole':
        return assignment[term[1]][0]
    prefix = term[:2] if term[0] == 'MIL.HO1' else term[:1]
    return prefix + [instantiate(child, assignment) for child in children(term)]


def types(term, assignment, declarations):
    if isinstance(term, str):
        return [(source, target) for name, source, target in declarations if name == term]
    if term[0] == 'MIL.Hole':
        return [assignment[term[1]][1:]]
    parts = children(term)
    if parts is None:
        return []
    if len(parts) == 1:
        inner = types(parts[0], assignment, declarations)
        if term[0] == 'MIL.Id':
            return inner
        if term[0] == 'MIL.Closure':
            return [(a, b) for a, b in inner if a == b]
        return [(('List', a), ('List', b)) for a, b in inner]
    pairs = itertools.product(types(parts[0], assignment, declarations),
                              types(parts[1], assignment, declarations))
    if term[0] == 'MIL.Chain':
        return [(a, c) for (a, b), (middle, c) in pairs if b == middle]
    return [(a, b) for (a, b), second in pairs if (a, b) == second]


def reference(term, target=None, declarations=DECLARATIONS, primitives=PRIMITIVES):
    keys = holes(term)
    if keys is None:
        return Counter()
    unique = list(dict.fromkeys(keys))
    choices = declarations if target is not None else [(name,) for name in primitives]
    result = Counter()
    for values in itertools.product(choices, repeat=len(unique)):
        assignment = dict(zip(unique, values))
        candidate = instantiate(term, assignment)
        multiplicity = types(term, assignment, declarations).count(target) if target is not None else 1
        result[sexpr(candidate)] += multiplicity
    return +result


def sort(value):
    return value if isinstance(value, str) else ['MIL.List', sort(value[1])]


def normalized(term):
    if isinstance(term, list):
        return [normalized(child) for child in term]
    return term.replace('MIL:', 'MIL.', 1) if term.startswith('MIL:') else term


def run(cetta, language, source, path):
    check_controls(source)
    path.write_text(source)
    process = subprocess.run([str(cetta.resolve()), '--lang', language, str(path.resolve())],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
    path.with_suffix('.log').write_text(process.stdout + process.stderr)
    if process.returncode:
        raise RuntimeError((process.returncode, process.stderr[-1000:]))
    return parse_forms(process.stdout)


def helper_control(cetta, output):
    from check_ic_context_library import criterion_seeds
    from check_source_codec import reference
    source = (ROOT / 'lib/petta/lib_ic_numeric_arrays.metta').read_text()
    first, second = criterion_seeds(reference(source))
    extraction = f'''!(import! &self (library lib_ic_context_library))
!(let $original (syntax:read {text_arg(source)})
   (case (context-library:seed (ic:prepare-library Deferred $original) "ic-learned:mil" {first} {second})
     (((Library:Candidate $bytes $support)
       (case (ic:export-library $bytes)
         (((Library:Export $name $document $text $restored)
           (let $before (ic:source-pack $original)
             (MILHelperExport $support (tensor:shape $before) (tensor:shape $bytes)
               (== $restored $original)
               (case (ic:library-check $before $before $bytes)
                 (((IC:PackedAccepted $accepted $old-size $new-size) (== $accepted $bytes))
                  ($bad-check False)))
               (tensor:inspect (binary:utf8 $text)))))
          ($bad-export (MILHelperFault Export)))))
      ($bad-extraction (MILHelperFault Extraction)))))'''
    forms = run(cetta, 'petta', extraction, output / 'extract-helper.metta')
    exports = [form for form in forms if isinstance(form, list) and form and form[0] == 'MILHelperExport']
    assert len(exports) == 1 and exports[0][4:6] == ['true', 'true'], exports
    export = exports[0]
    text = bytes(int(row[1]) for row in export[6][3]).decode()
    definition = parse_forms(text)[0]
    assert definition[:1] == ['='] and definition[1] == ['ic-learned:mil', '$p0'], definition
    # The background definition is emitted by native IC in this invocation,
    # not copied from a prior receipt or hand-written as a learned function.
    core = json.dumps(os.path.relpath(
        ROOT / 'lib/ilp/metagol_core.metta', output.resolve()))
    prime = json.dumps(os.path.relpath(
        ROOT / 'lib/ilp/metagol_prime.metta', output.resolve()))
    code = f'''!(import! &self {core})
!(import! &self {prime})
{sexpr(definition)}
!(bind! &mil-helper-background (new-space))
!(add-atom &mil-helper-background (MIL.Primitive learned-criterion ic-learned:mil))
!(add-atom &mil-helper-background (MIL.Primitive exact? mil-helper:exact?))
!(add-atom &mil-helper-background (MIL.Primitive never mil-helper:never))
!(add-atom &mil-helper-background (MIL.Primitive leak mil-helper:leak))
!(add-atom &mil-helper-background (MIL.HOOperator MIL.MapOp mil:map-run))
!(add-atom &mil-helper-background (: learned-criterion (MIL.Rel TensorKind Criterion)))
!(add-atom &mil-helper-background (: exact? (MIL.Rel Criterion Bool)))
!(add-atom &mil-helper-background (: never (MIL.Rel Criterion Bool)))
!(add-atom &mil-helper-background (: leak (MIL.Rel Criterion Bool)))
(= (mil-helper:exact? $criterion) (== $criterion (IC:Some Numeric:Exact)))
(= (mil-helper:never $criterion) False)
(= (mil-helper:leak $criterion) True)
(= (mil-helper:leak $criterion) False)
(= (mil-helper:examples)
   (MIL.Examples
     ((MIL.Ex ("BoolTensor" "I64Tensor") (True True)) (MIL.Ex ("F64Tensor") (False)))
     ((MIL.Ex ("BoolTensor") (False))) MIL.Functional))
(= (mil-helper:skeleton) (MIL.HO1 MIL.MapOp (MIL.Chain (MIL.Hole classify) (MIL.Hole test))))
!(println! (MILHelperCandidates (collapse
   (mil:prime:typed-fill &mil-helper-background (mil-helper:skeleton)
     (MIL.Rel (MIL.List TensorKind) (MIL.List Bool))))))
!(println! (MILHelperWinners (collapse
   (mil:prime:typed-consistent-filled-candidate &mil-helper-background &mil-helper-background
     (mil-helper:examples) (mil-helper:skeleton) (MIL.Rel (MIL.List TensorKind) (MIL.List Bool)) 1000))))
!(println! (MILHelperHeldOut (collapse
   (let $program (mil:prime:typed-consistent-filled-candidate &mil-helper-background &mil-helper-background
          (mil-helper:examples) (mil-helper:skeleton) (MIL.Rel (MIL.List TensorKind) (MIL.List Bool)) 1000)
     (mil:run &mil-helper-background $program
       ("F64Tensor" "I64Tensor" "BoolTensor" "Unknown" "BoolTensor" "F64Tensor"))))))
!(println! (MILHelperEmpty (collapse
   (mil:run &mil-helper-background (MIL.HO1 MIL.MapOp (MIL.Chain learned-criterion exact?)) ()))))
!(println! (MILHelperNegativeOnly (collapse
   (mil:prime:typed-consistent-filled-candidate &mil-helper-background &mil-helper-background
     (MIL.Examples ((MIL.Ex ("BoolTensor") (True))) ((MIL.Ex ("BoolTensor") (False))) MIL.Relational)
     (mil-helper:skeleton) (MIL.Rel (MIL.List TensorKind) (MIL.List Bool)) 1000))))
'''
    actual = {form[0]: form[1] for form in run(cetta, 'petta', code, output/'helper-composition.metta')
              if isinstance(form, list) and form and str(form[0]).startswith('MILHelper')}
    winner = ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', 'learned-criterion', 'exact?']]
    assert Counter(map(sexpr, actual['MILHelperCandidates'])) == Counter(map(sexpr, [
        winner, ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', 'learned-criterion', 'never']],
        ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', 'learned-criterion', 'leak']]])), actual
    assert actual['MILHelperWinners'] == [winner], actual
    assert actual['MILHelperHeldOut'] == [['false', 'true', 'true', 'false', 'true', 'false']], actual
    assert actual['MILHelperEmpty'] == [[]], actual
    assert actual['MILHelperNegativeOnly'] == [winner], actual
    return {'passed': True, 'fresh_source_checked': True, 'support': int(export[1]), 'original_bytes': int(export[2][0]),
            'export_bytes': int(export[3][0]), 'winner': winner, 'held_out_answers': actual['MILHelperHeldOut'],
            'scope': 'fresh real-source helper used by native typed MIL; compositional list control, not held-out real-program-family utility'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lang', choices=('petta', 'prime'), default='petta')
    parser.add_argument('--skip-helper', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.lang == 'prime':
        prefix = os.path.relpath(ROOT / 'lib/ilp', args.output.resolve())
        imports = [f'!(import! &self {prefix}/{name})' for name in ('metagol_core.metta', 'metagol_prime.metta')]
    else:
        imports = [f'!(import! &self {json.dumps(os.path.relpath(ROOT / "lib/ilp" / name, args.output.resolve()))})'
                   for name in ('metagol_core.metta', 'metagol_prime.metta')]
    code = imports + ['!(bind! &mil-skeleton-bias (new-space))']
    for name in PRIMITIVES:
        code.append(f'!(add-atom &mil-skeleton-bias (MIL.FactPrimitive {name} {name}))')
    for name, source, target in DECLARATIONS:
        code.append(f'!(add-atom &mil-skeleton-bias (: {name} {sexpr(["MIL.Rel", sort(source), sort(target)])}))')
    code.extend(['!(add-atom &mil-skeleton-bias (MIL.Fact p sample wrong))',
                 '!(add-atom &mil-skeleton-bias (MIL.Fact q sample right))'])
    skeletons = [hole('x'), ['MIL.Chain', hole('x'), hole('x')], ['MIL.Chain', hole('x'), hole('y')],
        ['MIL.Choice', hole('x'), hole('x')], ['MIL.Choice', hole('x'), hole('y')],
        ['MIL.Chain', ['MIL.Choice', hole('x'), hole('y')], ['MIL.Choice', hole('x'), hole('y')]],
        ['MIL.Chain', hole('x'), ['MIL.Choice', hole('x'), hole('y')]],
        ['MIL.Map', hole('x')], ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', hole('x'), hole('x')]],
        ['MIL.HO1', 'MIL.MapOp', ['MIL.Chain', hole('x'), hole('y')]],
        ['MIL.Closure', hole('x')], ['MIL.Id', hole('x')],
        ['MIL.Choice', 'p', hole('x')], 'p', 'unregistered', ['Unsupported', hole('x')]]
    targets = [('A', 'A'), ('A', 'B'), ('B', 'A'), (('List', 'A'), ('List', 'A')),
               (('List', 'A'), ('List', 'B'))]
    expected = []

    def add(skeleton, target=None, bias='&mil-skeleton-bias', declarations=DECLARATIONS, primitives=PRIMITIVES,
            immutable=False):
        index = len(expected)
        function = 'mil:fill-holes' if target is None else 'mil:prime:typed-fill'
        suffix = '' if target is None else ' ' + sexpr(['MIL.Rel', sort(target[0]), sort(target[1])])
        code.append(f'!(println! (MILSkeletonCase {index} (collapse '
                    f'({function} {bias} {sexpr(skeleton)}{suffix}))))')
        expected.append(reference(skeleton, target, declarations, primitives))
        if target is not None and not immutable:
            rows = [['MIL.TypeRow', name, ['MIL.Rel', sort(source), sort(result)]]
                    for name, source, result in declarations]
            fields = '()'
            builders = []
            for position, row in enumerate(reversed(rows)):
                name = f'$mil-immutable-tail-{position}'
                builders.append(f'({name} (cons-atom {sexpr(row)} {fields}))')
                fields = name
            vocabulary = f'(MIL.TypeVocabulary {fields})'
            if builders:
                vocabulary = f'(let* ({" ".join(builders)}) {vocabulary})'
            add(skeleton, target, vocabulary, declarations, primitives, True)

    for skeleton in skeletons:
        for target in [None] + targets:
            add(skeleton, target)
    code.append('!(bind! &mil-duplicate-bias (new-space))')
    duplicate_names = ('p', 'p', 'q')
    duplicate_types = [(name, 'A', 'A') for name in duplicate_names]
    for name in duplicate_names:
        code.extend([f'!(add-atom &mil-duplicate-bias (MIL.FactPrimitive {name} {name}))',
                     f'!(add-atom &mil-duplicate-bias (: {name} (MIL.Rel A A)))'])
    for target in (None, ('A', 'A')):
        add(['MIL.Chain', hole('x'), hole('x')], target, '&mil-duplicate-bias', duplicate_types, duplicate_names)
        add(['MIL.Chain', hole('x'), hole('y')], target, '&mil-duplicate-bias', duplicate_types, duplicate_names)
    # A named hole denotes one closed declaration instance, not an implicit
    # polymorphic binder inferred from several overloaded declarations.
    code.append('!(bind! &mil-instance-bias (new-space))')
    instances = [('poly', 'A', 'A'), ('poly', 'B', 'B'), ('ab', 'A', 'B')]
    for name, source, target in instances:
        code.append(f'!(add-atom &mil-instance-bias (: {name} (MIL.Rel {source} {target})))')
    for term in (hole('x'), 'poly'):
        add(['MIL.Choice', ['MIL.Chain', term, 'ab'], ['MIL.Chain', 'ab', term]],
            ('A', 'B'), '&mil-instance-bias', instances)
    code.append('!(bind! &mil-empty-bias (new-space))')
    add(hole('x'), None, '&mil-empty-bias', (), ())
    add(hole('x'), ('A', 'A'), '&mil-empty-bias', (), ())
    for query in (
        '(mil:prime:typed-consistent-filled-candidate &mil-skeleton-bias &mil-skeleton-bias '
        '(MIL.Examples ((MIL.Ex sample right)) ((MIL.Ex sample wrong)) MIL.Relational) '
        '(MIL.Hole pick) (MIL.Rel A A) 1000)',
        '(mil:prime:consistent-filled-candidate &mil-skeleton-bias &mil-skeleton-bias '
        '(MIL.Examples ((MIL.Ex sample right)) ((MIL.Ex sample wrong)) MIL.Relational) '
        '(MIL.Hole pick) (MIL.Rel A A) 1000)',
    ):
        index=len(expected)
        code.append(f'!(println! (MILSkeletonCase {index} (collapse (once {query}))))')
        expected.append(Counter({'q':1}))
    text = '\n'.join(code) + '\n'
    forms = run(args.cetta, args.lang, text, args.output/'skeletons.metta')
    actual = [form for form in forms if isinstance(form, list) and form and form[0] == 'MILSkeletonCase']
    errors = []
    for i, (wanted, row) in enumerate(itertools.zip_longest(expected, actual)):
        got = Counter(sexpr(normalized(value) if args.lang == 'prime' else value)
                      for value in row[2]) if row is not None else None
        if wanted != got or (row is not None and row[1] != str(i)):
            errors.append({'case': i, 'wanted': wanted, 'actual': got})
    helper = None if args.skip_helper or errors else helper_control(args.cetta, args.output)
    result = {'passed': not errors, 'checks': len(expected), 'observations': len(actual),
              'language': args.lang, 'errors': errors, 'helper_composition': helper,
              'scope': 'independent global substitutions followed by type filtering, not per-occurrence choices'}
    (args.output/'receipt.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
