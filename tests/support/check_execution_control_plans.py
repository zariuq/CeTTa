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
    assert len(spec.control_plans) == 7
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
    print('PASS: control source plans admit both binding orders and distinct force/'
          'return scopes; eight corrupted layouts or lowerings are refused')


if __name__ == '__main__':
    main()
