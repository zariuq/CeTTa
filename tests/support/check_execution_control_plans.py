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
    assert len(spec.control_plans) == 9
    assert len(spec.control_forms) == 2
    assert len(spec.fold_control_heads) == 5
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
    print('PASS: control source plans preserve binding orders, force/return scopes, '
          'choice/failure and dialect commit/handler scopes; eighteen corruptions refused')


if __name__ == '__main__':
    main()
