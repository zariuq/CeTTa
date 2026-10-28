#!/usr/bin/env python3
"""Check source-plan admission before any native artifact is emitted."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from gen_execution_contracts import Spec, SpecError


def main():
    text = (ROOT / 'lib/gslt_execution_contracts.metta').read_text()
    spec = Spec(text)
    assert len(spec.control_plans) == 11
    selection = '(source-head case 2 match-branches (operands 0 1 absent) (fold none))'
    for replacement in [
            selection + '\n  ' + selection,
            selection.replace('operands 0 1 absent', 'operands 0 0 absent'),
            selection.replace('operands 0 1 absent', 'operands 0 absent absent'),
            selection.replace('operands 0 1 absent', 'operands 0 2 absent'),
            selection.replace('fold none', 'fold branch')]:
        try:
            Spec(text.replace(selection, replacement))
        except SpecError:
            continue
        raise AssertionError(('invalid case plan was admitted', replacement))
    assert len(spec.control_forms) == 2
    assert len(spec.variadic_forms) == 2
    collection = '(source-head collapse 1 collect (operands 0 absent absent) (fold none))'
    for replacement in [
            collection + '\n  ' + collection,
            collection.replace('operands 0', 'operands absent'),
            collection.replace('operands 0', 'operands 1'),
            collection.replace('fold none', 'fold evaluate')]:
        try:
            Spec(text.replace(collection, replacement))
        except SpecError:
            continue
        raise AssertionError(('invalid collection plan was admitted', replacement))
    series = '(variadic-form petta prog1 sequence-first)'
    for before, after in [
            (series, series + '\n  ' + series),
            (series, series.replace('petta', 'he')),
            (series, series.replace('prog1', 'unregistered')),
            (series, series.replace('sequence-first', 'sequence-last')),
            (series, series.replace('sequence-first', 'sequence')),
            (series, ''),
            (series, series.replace('sequence-first)', 'sequence-first extra)')),
            ('(source-head empty 0 failure (operands absent absent absent) (fold none))',
             '(source-head empty 0 sequence-first (operands absent absent absent) (fold none))')]:
        try:
            Spec(text.replace(before, after))
        except SpecError:
            continue
        raise AssertionError(('invalid variadic sequence was admitted', after))
    assert set(spec.implicit_operations) == {'return-value', 'construct', 'invoke'}
    assert len(spec.fold_control_heads) == 5
    assert len(spec.invocation_heads) == 10 and len(spec.invocation_forms) == 10
    invocation = '(invocation-head member 2 relational-member 0 2)'
    predicate = '(invocation-form petta predicate 1 predicate-value 0 1)'
    operation = '(invocation-operation host)'
    for before, after in [
            (invocation, invocation + '\n  ' + invocation),
            (invocation, invocation.replace('member 2', 'unknown 2')),
            (invocation, invocation.replace('relational-member', 'unknown')),
            (invocation, invocation.replace(' 0 2)', ' 1 2)')),
            (invocation, invocation.replace(' 0 2)', ' 0 3)')),
            (invocation, invocation.replace('member 2', 'member 256')),
            (invocation, invocation.replace('member 2', 'member -1')),
            (predicate, predicate.replace('petta predicate', 'he predicate')),
            (predicate, predicate.replace('petta predicate', 'petta unknown')),
            (predicate, predicate + '\n  ' + predicate),
            (operation, operation + '\n  ' + operation),
            (operation, ''),
            (invocation, '')]:
        try:
            Spec(text.replace(before, after))
        except SpecError:
            continue
        raise AssertionError(('invalid invocation plan was admitted', after))
    preparation = '(preparation-step install-type 0)'
    for corrupted in [
            text.replace(preparation, preparation + '\n  ' + preparation),
            text.replace(preparation, '(preparation-step unknown 0)'),
            text.replace(preparation, '(preparation-step install-type 1)'),
            text.replace(preparation, '(preparation-step install-type 5)'),
            text.replace(preparation, '(preparation-step install-type -1)'),
            text.replace(preparation, '(preparation-step install-type 0 extra)'),
            text.replace(preparation, '')]:
        try:
            Spec(corrupted)
        except SpecError:
            continue
        raise AssertionError('invalid preparation stage layout was admitted')
    row = '(source-head let 3 sequence (operands 1 0 2) (fold bind))'
    assert text.count(row) == 1
    corruptions = [
        row.replace('operands 1 0 2', 'operands 3 0 2'),
        row.replace('operands 1 0 2', 'operands 1 1 2'),
        row.replace('operands 1 0 2', 'operands absent 0 2'),
        row.replace('sequence (operands', 'unknown (operands'),
        row.replace('(fold bind)', '(fold branch)'),
        row.replace('let 3', 'let 2'),
        row.replace('operands 1 0 2', 'operands 1 0'),
        row + '\n  ' + row,
    ]
    for corrupted in corruptions:
        try:
            Spec(text.replace(row, corrupted))
        except SpecError:
            continue
        raise AssertionError(('invalid control source plan was admitted', corrupted))
    zero = '(source-head empty 0 failure (operands absent absent absent) (fold none))'
    choice = '(source-head superpose 1 ordered-choice (operands 0 absent absent) (fold none))'
    for before, after in [
            (zero, zero.replace('operands absent absent absent', 'operands 0 absent absent')),
            (zero, zero.replace('empty 0', 'empty 1')),
            (choice, choice.replace('operands 0 absent absent', 'operands absent absent absent')),
            (choice, choice.replace('(fold none)', '(fold evaluate)'))]:
        try:
            Spec(text.replace(before, after))
        except SpecError:
            continue
        raise AssertionError(('invalid choice/failure source plan was admitted', after))
    commit = '(dialect-form petta cut 0 scoped-commit (operands absent absent absent) (fold none))'
    handler = '(dialect-form petta catch 1 handle (operands 0 absent absent) (fold none))'
    for before, after in [
            (commit, commit.replace('petta cut', 'he cut')),
            (commit, commit.replace('petta cut', 'petta unknown-form')),
            (commit, commit.replace('cut 0', 'cut 1')),
            (handler, handler.replace('catch 1', 'catch 0')),
            (handler, handler.replace('(fold none)', '(fold evaluate)')),
            (commit, commit + '\n  ' + commit)]:
        try:
            Spec(text.replace(before, after))
        except SpecError:
            continue
        raise AssertionError(('invalid dialect control plan was admitted', after))
    implicit = '(implicit-operation return-value)'
    for corrupted in [
            text.replace(implicit, implicit + '\n  ' + implicit),
            text.replace(implicit, '(implicit-operation missing)'),
            text.replace(implicit, implicit + '\n  (implicit-operation invoke extra)'),
            text.replace(row, '(source-head let 0 invoke (operands absent absent absent) (fold none))')]:
        try:
            Spec(corrupted)
        except SpecError:
            continue
        raise AssertionError('implicit computation bypassed occurrence admission')
    sequence = '(sequence-order source-first source first last)'
    for corrupted in [text.replace(sequence, sequence + '\n  ' + sequence),
                      text.replace(sequence, '(sequence-order source-first source last first)'),
                      text.replace(sequence, '(sequence-order source-first source source last)')]:
        try:
            Spec(corrupted)
        except SpecError:
            continue
        raise AssertionError('invalid executable sequence order was admitted')
    print('PASS: computation plans preserve basic occurrences, control layouts, '
          'sequence order and dialect scopes; malformed plans refused')


if __name__ == '__main__':
    main()
