#!/usr/bin/env python3
"""Check packed byte framing against an independent bit-level reference."""

import argparse
import json
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_semiring_fixtures import parse_forms, sexpr


def digits(value):
    return [] if value == 0 else [int(bit) for bit in bin(value)[2:]]


def frame(payload):
    length = digits(len(payload))
    return [1] * len(length) + [0] + length + payload


def encode(fields):
    bits = frame(digits(len(fields)))
    for field in fields:
        bits += frame([int(bit) for byte in field for bit in f"{byte:08b}"])
    bits += [0] * (-len(bits) % 8)
    return bytes(sum(bits[i + j] << (7 - j) for j in range(8)) for i in range(0, len(bits), 8))


def tensor(data, dtype="u8"):
    return f'(tensor:from-values {dtype} ({len(data)}) (' + " ".join(map(str, data)) + "))"


def observation(data):
    return ["TensorData", "u8", [str(len(data))], [[str(i), str(byte)] for i, byte in enumerate(data)]]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary = str(args.cetta.resolve())
    rng = random.Random(17027)
    records = [[], [b""], [b"", b""], [b"A"], [bytes(range(256))],
               ["λ●□".encode(), b"\0a\0", b"", b"(eval (+ 1 2))"]]
    for length in (0, 1, 2, 7, 8, 15, 16, 31, 32, 127, 128, 513):
        records.append([rng.randbytes(length), rng.randbytes(3)])
    code = ['!(import! &self (library binary))', '!(import! &self (library tensor))', '''
(= (binary-test $fields)
   (let $packed (binary:pack-fields $fields)
     (case (binary:unpack-fields $packed)
       (((Binary:Fields $decoded)
         (BinaryObservation (tensor:inspect $packed) (binary:packed-length $fields)
           (binary:packed-length (Binary:FieldLengths
             (map-atom $fields $field (index-atom (tensor:shape $field) 0))))
           (map-atom $decoded $field (tensor:inspect $field))))
        ($other $other)))))
''']
    expected = []
    for index, fields in enumerate(records):
        bindings = " ".join(f'($field{k} {tensor(data)})' for k, data in enumerate(fields))
        values = "(" + " ".join(f"$field{k}" for k in range(len(fields))) + ")"
        call = f"(binary-test {values})"
        if fields:
            call = f"(let* ({bindings}) {call})"
        code.append(f"!(println! (BinaryCase {index} {call}))")
        encoded = encode(fields)
        expected.append(["BinaryCase", str(index),
                         ["BinaryObservation", observation(encoded), str(len(encoded)), str(len(encoded)),
                          [observation(field) for field in fields]]])

    controls = [
        ('(binary:packed-length (Binary:FieldLengths (-1)))', ["Binary:Fault", "invalid-field-length"]),
        ('(binary:packed-length (Binary:FieldLengths (1.0)))', ["Binary:Fault", "invalid-field-length"]),
        ('(binary:packed-length (Binary:FieldLengths (unknown)))', ["Binary:Fault", "invalid-field-length"]),
        ('(binary:packed-length (Binary:FieldLengths (9223372036854775807)))', ["Binary:Fault", "size-overflow"]),
        (f'(binary:unpack-fields {tensor([])})', ["Binary:Fault", "truncated"]),
        (f'(binary:unpack-fields {tensor([0x80])})', ["Binary:Fault", "noncanonical"]),
        (f'(binary:unpack-fields {tensor([0xa0])})', ["Binary:Fault", "noncanonical"]),
        (f'(binary:unpack-fields {tensor([0xbb])})', ["Binary:Fault", "unaligned-field"]),
        (f'(binary:unpack-fields {tensor([0, 0])})', ["Binary:Fault", "trailing-data"]),
        (f'(binary:unpack-fields {tensor([1])})', ["Binary:Fault", "trailing-data"]),
        (f'(binary:unpack-fields {tensor([1], "i64")})', ["Binary:Fault", "expected-byte-vector"]),
        ('(binary:unpack-fields (tensor:from-coo u8 (2) ((0 0))))', ["Binary:Fault", "expected-byte-vector"]),
        ('(binary:pack-fields unknown)', ["binary:pack-fields", "unknown"]),
        ('(binary:pack-fields Custom)', "CustomResult"),
        (f'(binary:from-utf8 {tensor([255])})', ["Binary:Fault", "invalid-utf8"]),
        (f'(tensor:inspect (binary:utf8 (binary:from-utf8 {tensor([0, 65, 0])})))', observation(bytes([0, 65, 0]))),
        ('(tensor:inspect (binary:utf8 "λ●□"))', observation("λ●□".encode())),
        ('(tensor:inspect (binary:utf8 (binary:from-utf8 (tensor:from-values u8 (0) ()))))', observation(b"")),
        ('(let $bytes (tensor:view (tensor:from-values u8 (3) (10 20 30)) (3) (-1) 2) '
         '(tensor:inspect (binary:pack-fields ($bytes))))', observation(encode([bytes([30, 20, 10])]))),
        ('(let $bytes (tensor:from-coo u8 (2) ((0 0) (1 255))) '
         '(tensor:inspect (binary:pack-fields ($bytes))))', observation(encode([bytes([0, 255])]))),
    ]
    code.append('(= (binary:pack-fields Custom) CustomResult)')
    for value in (0, 1, 127, 128, 255, 256, 65535, 65536, 2**32, 2**63 - 1):
        raw = value.to_bytes((value.bit_length() + 7) // 8, "big")
        controls.append((f'(tensor:inspect (binary:unsigned {value}))', observation(raw)))
        controls.append((f'(binary:from-unsigned {tensor(raw)})', str(value)))
    controls.extend([
        ('(binary:unsigned -1)', ['Binary:Fault', 'negative-integer']),
        ('(binary:unsigned 1.5)', ['binary:unsigned', '1.5']),
        (f'(binary:from-unsigned {tensor([0])})', ['Binary:Fault', 'noncanonical']),
        (f'(binary:from-unsigned {tensor([0, 1])})', ['Binary:Fault', 'noncanonical']),
        (f'(binary:from-unsigned {tensor([128] + [0] * 7)})', ['Binary:Fault', 'size-overflow']),
        (f'(binary:from-unsigned {tensor([1] * 9)})', ['Binary:Fault', 'size-overflow']),
        ('(tensor:inspect (binary:symbol-utf8 λ●□))', observation("λ●□".encode())),
        (f'(tensor:inspect (binary:symbol-utf8 (binary:from-symbol-utf8 {tensor(b"(call x)")})))', observation(b"(call x)")),
        (f'(get-metatype (binary:from-symbol-utf8 {tensor(b"(call x)")}))', 'Symbol'),
        (f'(binary:from-symbol-utf8 {tensor([0])})', ['Binary:Fault', 'symbol-nul']),
        (f'(binary:from-symbol-utf8 {tensor([255])})', ['Binary:Fault', 'invalid-utf8']),
    ])
    for value, valid in (("0", True), (str(2**63 - 1), True), ("-1", False),
                         ("1.0", False), ("1.5", False), ('"1"', False), ("unknown", False)):
        controls.append((f'(if (binary:unsigned? {value}) Known NotKnown)', "Known" if valid else "NotKnown"))
    for call, result in controls:
        index = len(expected)
        code.append(f"!(println! (BinaryCase {index} {call}))")
        expected.append(["BinaryCase", str(index), result])
    fixture = args.output / "binary.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([binary, "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "binary.log").write_text(result.stdout + result.stderr)
    actual = [form for form in parse_forms(result.stdout)
              if isinstance(form, list) and form and form[0] == "BinaryCase"]
    mismatches = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    unimported = subprocess.run([binary, "--lang", "petta", "-e", "!(binary:pack-fields ())"],
                               cwd=ROOT, capture_output=True, text=True, timeout=10)
    unavailable = unimported.returncode == 0 and parse_forms(unimported.stdout) == [["binary:pack-fields", []]]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not mismatches and unavailable,
               "cases": len(expected), "answers": len(actual), "mismatches": mismatches,
               "unimported_unreduced": unavailable,
               "scope": "exact packed bytes and observations against independent reference; one production configuration"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in mismatches[:5]:
            print("expected", sexpr(expected[i])[:500])
            print("actual  ", sexpr(actual[i])[:500])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
