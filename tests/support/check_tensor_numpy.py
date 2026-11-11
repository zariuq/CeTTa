#!/usr/bin/env python3
"""Compare tensor reductions with NumPy and exact stored-input rational sums."""
import argparse
from fractions import Fraction
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from library_spelling import read_forms


def number(value):
    text = repr(float(value))
    if 'e' in text:
        mantissa, exponent = text.split('e')
        if '.' not in mantissa:
            mantissa += '.0'
        text = f'{mantissa}e{int(exponent)}'
    return text


def tensor(values):
    code = 'f32' if values.dtype.itemsize == 4 else 'f64'
    shape = ' '.join(map(str, values.shape))
    data = ' '.join(number(x) for x in values.flat)
    return f'(tensor:from-values {code} ({shape}) ({data}))'


def sequential(row, vector):
    value = 0.
    for left, right in zip(row, vector):
        value = value + float(left) * float(right)
    return value


def unpack(value, rows):
    # Read actual scalar bits through CTN1, independently of display precision.
    length = 17 + 8 * rows
    assert value[:2] == ['TensorData', 'u8'] and value[2] == [str(length)], value
    assert [int(row[0]) for row in value[3]] == list(range(length)), value
    data = bytes(int(row[1]) for row in value[3])
    assert data[:17] == struct.pack('<4sBIQ', b'CTN1', 2, 1, rows), data[:17]
    return list(struct.unpack('<' + 'd' * rows, data[17:]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cetta', type=Path, required=True)
    binary = parser.parse_args().cetta.resolve()
    rng = np.random.default_rng(20261103)
    cases = []
    for left_dtype in (np.float32, np.float64):
        for right_dtype in (np.float32, np.float64):
            for columns in (0, 1, 3, 17, 65):
                cases.append((rng.standard_normal((3, columns)).astype(left_dtype),
                              rng.standard_normal(columns).astype(right_dtype)))
    cases += [
        (np.array([[1e16, 1., -1e16]]), np.ones(3)),
        (np.array([[1e16, -1e16, 1.]]), np.ones(3)),
        (np.array([[np.nextafter(0., 1.)]]), np.array([.5])),
        (np.array([[np.nextafter(np.float32(0), np.float32(1))]], dtype=np.float32),
         np.array([np.finfo(np.float32).max], dtype=np.float32)),
        (np.array([[1e150, -1e150, 1e149]]), np.array([1e100, 1e100, 1e100])),
    ]
    references = []
    for matrix, vector in cases:
        exact = [sum((Fraction(float(a)) * Fraction(float(b))
                      for a, b in zip(row, vector)), Fraction()) for row in matrix]
        # Match CeTTa's specified f64 accumulation, including f32 operands.
        numpy = matrix.astype(np.float64) @ vector.astype(np.float64)
        references.append((exact, numpy, [sequential(row, vector) for row in matrix]))
    forms = []
    for index, (matrix, vector) in enumerate(cases):
        for layout in ('dense', 'coo', 'csr', 'csc'):
            source = tensor(matrix)
            if layout != 'dense':
                source = f'(tensor:convert {source} {layout})'
            forms.append(f'''!(let* (($m {source}) ($v {tensor(vector)})
                    ((Tensor:Bounds $mid $lo $hi) (tensor:matvec-bounds $m $v))
                    ((Tensor:Bounds $dot $dl $du) (tensor:dot-bounds {tensor(matrix[0])} $v)))
                (NumPyCheck {index} {layout}
                  (tensor:inspect (tensor:encode-bytes (tensor:matvec $m $v)))
                  (tensor:inspect (tensor:encode-bytes $mid))
                  (tensor:inspect (tensor:encode-bytes $lo))
                  (tensor:inspect (tensor:encode-bytes $hi))
                  (tensor:inspect (tensor:encode-bytes
                    (tensor:from-values f64 (3) ($dot $dl $du))))))''')
    records = 0
    env = {key: value for key, value in os.environ.items() if not key.startswith('CETTA_')}
    with tempfile.TemporaryDirectory(prefix='tensor-numpy-', dir=ROOT / 'runtime') as temporary:
        source = Path(temporary) / 'compare.metta'
        for language in ('petta', 'he'):
            module = '(library tensor)' if language == 'petta' else 'tensor'
            source.write_text(f'!(import! &self {module})\n' + '\n'.join(forms))
            run = subprocess.run([str(binary), '--lang', language, '--profile', 'extended',
                                  '--fuel', '10000000', str(source)], cwd=ROOT, env=env,
                                 text=True, capture_output=True, timeout=120)
            assert run.returncode == 0 and not run.stderr, (language, run.returncode, run.stderr)
            text = run.stdout
            if language == 'he':
                lines = text.splitlines()
                assert all(line.startswith('[') and line.endswith(']') for line in lines), text[-1500:]
                text = '\n'.join(line[1:-1] for line in lines)
            output = [value for value in read_forms(text)
                      if isinstance(value, list) and value and value[0] == 'NumPyCheck']
            assert len(output) == len(forms), (language, len(output), len(forms), run.stdout[-1500:])
            for report in output:
                index = int(report[1]); exact, numpy, ordered = references[index]
                rows = len(exact)
                ordinary, center, lower, upper = [unpack(value, rows) for value in report[3:7]]
                assert center == ordered, (language, index, report[2], center, ordered)
                for row in range(rows):
                    assert Fraction(lower[row]) <= exact[row] <= Fraction(upper[row]), (
                        language, index, report[2], row, lower[row], exact[row], upper[row])
                    assert lower[row] <= ordinary[row] <= upper[row], (index, row, 'ordinary')
                    assert lower[row] <= numpy[row] <= upper[row], (index, row, 'numpy')
                dot = unpack(report[7], 3)
                assert dot[0] == ordered[0]
                assert Fraction(dot[1]) <= exact[0] <= Fraction(dot[2]), dot
                records += 1
    print(f'TensorNumPy {records} matvec and {records} dot records pass; NumPy {np.__version__}')


if __name__ == '__main__':
    main()
