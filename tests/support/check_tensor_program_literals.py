#!/usr/bin/env python3
"""Exact integer literal storage and TP1 backward-compatibility controls."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, encoded
from check_ic_search import check_controls
from check_tensor_program import array, compact_integer_literal, source_array, transport
from check_tensor_sequences import normalized


HELPERS = r'''
(= (literal-test:check $value $expected)
   (case (tensor-program:compact-constant "I64Tensor" $value)
     (((Binary:Leaf $bytes)
       (LiteralResult (== $bytes $expected) (== (tensor-program:leaf-type $bytes) (TP:Some "I64Tensor"))
         (case (tensor-program:leaf () $bytes)
           (((TP:Value "I64Tensor" $restored) (== $restored $value)) ($invalid False))))) ($invalid $invalid))))
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cetta', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    code = ['!(import! &self (library lib_tensor_program))', HELPERS]
    expected, fixtures = [], []

    def add(call, wanted):
        index = len(expected)
        code.append(f'!(println! (LiteralCase {index} {call}))')
        expected.append(['LiteralCase', str(index), wanted])

    for labels in ([], [0], [0, 255], [0, 1, 2, 255] * 16, [-1, 0, 255] * 16,
                   [0, 256] * 16, [-(1 << 63), (1 << 63) - 1]):
        value = array((len(labels),), labels, 'I64Tensor')
        ordinary = encoded(['TP:Constant', Text('I64Tensor'), transport(value)])
        chosen = compact_integer_literal(labels)
        add(f'(literal-test:check {source_array(value)} {byte_tensor(chosen)})',
            ['LiteralResult', 'true', 'true', 'true'])
        fixtures.append({'elements': len(labels), 'ordinary_bytes': len(ordinary), 'chosen_bytes': len(chosen)})
    assert any(row['chosen_bytes'] < row['ordinary_bytes'] for row in fixtures)
    assert fixtures[0]['ordinary_bytes'] == fixtures[0]['chosen_bytes']
    u8 = b'CTN1\x03' + struct.pack('<IQ', 1, 2) + bytes([0, 255])
    i64 = transport(array((2,), [0, 255], 'I64Tensor'))
    for record in (['TP:PackedConstant', Text('F64Tensor'), Text('u8'), u8],
                   ['TP:PackedConstant', Text('I64Tensor'), Text('i8'), u8],
                   ['TP:PackedConstant', Text('I64Tensor'), Text('u8'), i64],
                   ['TP:PackedConstant', Text('I64Tensor'), Text('u8'), u8[:-1]],
                   ['TP:PackedConstant', Text('I64Tensor'), u8],
                   ['TP:Constant', Text('I64Tensor'), u8]):
        add(f'(tensor-program:leaf-type {byte_tensor(encoded(record))})', 'TP:None')
    # An existing ordinary literal keeps its old bytes and meaning.
    add(f'(== (tensor-program:constant "I64Tensor" {source_array(array((2,), [0, 255], "I64Tensor"))}) '
        f'(Binary:Leaf {byte_tensor(encoded(["TP:Constant", Text("I64Tensor"), i64]))}))', 'true')
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'program.metta'
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, text=True, capture_output=True, timeout=90)
    (args.output / 'program.log').write_text(result.stdout + result.stderr)
    actual = [row for row in parse_forms(result.stdout) if isinstance(row, list) and row and row[0] == 'LiteralCase']
    found = {row[1]: row for row in actual}
    wrong = [{'case': i, 'expected': want, 'actual': found.get(str(i))} for i, want in enumerate(expected)
             if normalized(found.get(str(i))) != normalized(want)]
    receipt = {'passed': result.returncode == 0 and len(actual) == len(expected) and not wrong,
               'checks': len(expected), 'answers': len(actual), 'mismatches': wrong, 'fixtures': fixtures,
               'scope': 'explicit packed I64 literals, exact widening, full leaf cost, malformed storage and ordinary CTN1 compatibility'}
    (args.output / 'results.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'TensorProgramLiterals {len(expected)} {len(wrong)}')
    if wrong:
        print(json.dumps(wrong[:3], indent=2))
    raise SystemExit(0 if receipt['passed'] else 1)


if __name__ == '__main__':
    main()
