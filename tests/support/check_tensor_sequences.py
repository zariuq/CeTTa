#!/usr/bin/env python3
"""Independent coordinate reference for ordered packed sequence operations."""

from __future__ import annotations

import argparse
from decimal import Decimal
import hashlib
import itertools
import json
from pathlib import Path
import random
import re
import struct
import subprocess

from check_binary_codec import ROOT, parse_forms


def coordinates(shape):
    return itertools.product(*(range(size) for size in shape))


def ordinal(shape, point):
    result = 0
    for size, index in zip(shape, point):
        result = result * size + index
    return result


def scalar(value):
    if type(value) is bool:
        return "True" if value else "False"
    if type(value) is float:
        text = format(Decimal(repr(value)), "f")
        return text if "." in text else text + ".0"
    return repr(value)


def tensor(shape, values, dtype="i64"):
    dims = "(" + " ".join(map(str, shape)) + ")"
    rows = " ".join(f"({ordinal(shape, point)} {scalar(value)})"
                    for point, value in sorted(values.items()))
    return f"(tensor:from-coo {dtype} {dims} ({rows}) reject)"


def observation(shape, values, dtype="i64"):
    return ["TensorData", dtype, list(map(str, shape)),
            [[str(ordinal(shape, point)), scalar(value)] for point, value in sorted(values.items())]]


def scan(shape, values, axis, dtype):
    # Enumerate each complete coordinate fiber, without flat-coordinate
    # grouping, physical-layout access, or the native sorting algorithm.
    result = {}
    others = shape[:axis] + shape[axis+1:]
    for fiber in coordinates(others):
        total = None
        for position in range(shape[axis]):
            point = fiber[:axis] + (position,) + fiber[axis:]
            if point not in values:
                continue
            total = values[point] if total is None else total + values[point]
            if dtype == "f32":
                total = struct.unpack("f", struct.pack("f", total))[0]
            result[point] = total
    return result


def concatenate(shape, left, other_shape, right, axis):
    result_shape = tuple(a + b if i == axis else a for i, (a, b) in enumerate(zip(shape, other_shape)))
    result = {}
    for point in coordinates(result_shape):
        if point[axis] < shape[axis]:
            if point in left:
                result[point] = left[point]
        else:
            original = point[:axis] + (point[axis] - shape[axis],) + point[axis+1:]
            if original in right:
                result[point] = right[original]
    return result_shape, result


def permute(shape, values, axes):
    return tuple(shape[i] for i in axes), {tuple(point[i] for i in axes): value
                                         for point, value in values.items()}


def normalized(value):
    if isinstance(value, list):
        return [normalized(child) for child in value]
    if value in ("True", "true", "False", "false"):
        return ("boolean", value in ("True", "true"))
    if isinstance(value, str) and re.fullmatch(r"-?[0-9]+", value):
        return ("integer", int(value))
    if isinstance(value, str) and re.fullmatch(r"-?[0-9]+(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?", value):
        return ("float", float(value).hex())
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(270027)
    calls = []
    expected = []

    def add(call, result):
        index = len(expected)
        calls.append(f"!(println! (TensorSequenceCase {index} {call}))")
        expected.append(["TensorSequenceCase", str(index), result])

    for term, wanted in (("(tensor:from-values f64 (0) ())", "true"),
                         ("(tensor:from-coo i64 (2) ((1 0)))", "true"),
                         ("Unrecognized", "false"), ("(TensorData f64 (1) ((0 1.0)))", "false")):
        add(f"(tensor:is-tensor {term})", wanted)

    for shape in ((5,), (2, 3), (2, 3, 2), (0,), (2, 0, 3)):
        for sparse in (False, True):
            values = {point: rng.randrange(-4, 5) for point in coordinates(shape)
                      if not sparse or rng.randrange(3)}
            source = tensor(shape, values)
            for axis in range(len(shape)):
                add(f"(tensor:inspect (tensor:scan-sum {source} {axis}))",
                    observation(shape, scan(shape, values, axis, "i64")))
                other_shape = tuple(2 if i == axis else size for i, size in enumerate(shape))
                other = {point: rng.randrange(-4, 5) for point in coordinates(other_shape)
                         if not sparse or rng.randrange(3)}
                result_shape, result = concatenate(shape, values, other_shape, other, axis)
                add(f"(tensor:inspect (tensor:concatenate {source} {tensor(other_shape, other)} {axis}))",
                    observation(result_shape, result))
            for axes in itertools.permutations(range(len(shape))):
                result_shape, result = permute(shape, values, axes)
                add(f"(tensor:inspect (tensor:permute {source} ({' '.join(map(str, axes))})))",
                    observation(result_shape, result))

    for dtype, big in (("f32", 16777216.0), ("f64", 1e16)):
        shape = (4, 2)
        values = {(i, j): value for i, value in enumerate((big, 1.0, -big, 1.0)) for j in range(2)}
        for layout in ("dense", "csr", "csc"):
            source = f"(tensor:convert {tensor(shape, values, dtype)} {layout})"
            add(f"(tensor:inspect (tensor:scan-sum {source} 0))",
                observation(shape, scan(shape, values, 0, dtype), dtype))

    dense = "(tensor:from-values i64 (2 3) (1 2 3 4 5 6))"
    reversed_view = f"(tensor:view {dense} (3 2) (-1 3) 2)"
    reversed_values = {(0, 0): 3, (0, 1): 6, (1, 0): 2, (1, 1): 5, (2, 0): 1, (2, 1): 4}
    add(f"(tensor:inspect (tensor:scan-sum {reversed_view} 0))",
        observation((3, 2), scan((3, 2), reversed_values, 0, "i64")))
    add(f"(let $t {dense} (Kept (tensor:shape (tensor:scan-sum $t 1)) (tensor:inspect $t)))",
        ["Kept", ["2", "3"], observation((2, 3), {(i, j): i*3+j+1 for i in range(2) for j in range(3)})])
    # Duplicated values are occurrences; concatenation does not deduplicate.
    duplicate = "(tensor:from-values i64 (2) (7 7))"
    add(f"(tensor:inspect (tensor:concatenate {duplicate} {duplicate} 0))",
        observation((4,), {(i,): 7 for i in range(4)}))
    add("(tensor:inspect (tensor:permute (tensor:from-values f64 () (2.0)) ()))",
        observation((), {(): 2.0}, "f64"))

    faults = [
        (f"(tensor:scan-sum {dense} 2)", "shape-mismatch"),
        (f"(tensor:scan-sum {dense} -1)", "shape-mismatch"),
        ("(tensor:scan-sum (tensor:from-values bool (2) (True False)) 0)", "unsupported-dtype"),
        ("(tensor:scan-sum (tensor:from-values i64 (2) (9223372036854775807 1)) 0)", "integer-overflow"),
        ("(tensor:scan-sum (tensor:from-values u8 (2) (255 1)) 0)", "integer-overflow"),
        ("(tensor:scan-sum (tensor:from-values index (2) (18446744073709551615 1)) 0)", "integer-overflow"),
        (f"(tensor:permute {dense} (0 0))", "shape-mismatch"),
        (f"(tensor:permute {dense} (0 2))", "shape-mismatch"),
        (f"(tensor:permute {dense} (0))", "shape-mismatch"),
        (f"(tensor:concatenate {dense} (tensor:from-values i64 (3) (1 2 3)) 0)", "shape-mismatch"),
        (f"(tensor:concatenate {dense} (tensor:from-values i64 (2 2) (1 2 3 4)) 0)", "shape-mismatch"),
        (f"(tensor:concatenate {dense} (tensor:cast {dense} f64) 1)", "unsupported-dtype"),
    ]
    for call, reason in faults:
        add(call, ["Tensor:Fault", reason])
    add("(tensor:scan-sum Unrecognized 0)", ["tensor:scan-sum", "Unrecognized", "0"])
    add("(tensor:permute Unrecognized (0))", ["tensor:permute", "Unrecognized", ["0"]])
    # Sparse logical cardinality is not an instruction to allocate or visit
    # all absent coordinates. Neither scan nor permutation fills them.
    huge = "(tensor:from-coo i64 (18446744073709551615) ((18446744073709551614 7)))"
    add(f"(tensor:at (tensor:scan-sum {huge} 0) 18446744073709551614)", ["tensor:present", "7"])
    add(f"(tensor:at (tensor:permute {huge} (0)) 18446744073709551614)", ["tensor:present", "7"])

    for dtype, code, fmt, values in (("i64", 1, "q", [-2, 0, 7]),
                                      ("f64", 2, "d", [0.0, -0.0, 1.5]),
                                      ("u8", 3, "B", [0, 128, 255]),
                                      ("f32", 4, "f", [-0.0, 1.5, 3.25])):
        wire = b"CTN1" + bytes([code]) + struct.pack("<IQ", 1, len(values))
        wire += struct.pack("<" + fmt * len(values), *values)
        source = f"(tensor:from-values {dtype} (3) ({' '.join(map(scalar, values))}))"
        add(f"(tensor:inspect (tensor:encode-bytes {source}))",
            observation((len(wire),), {(i,): byte for i, byte in enumerate(wire)}, "u8"))
        packed = f"(tensor:from-values u8 ({len(wire)}) ({' '.join(map(str, wire))}))"
        add(f"(tensor:inspect (tensor:decode-bytes {packed}))",
            observation((3,), {(i,): value for i, value in enumerate(values)}, dtype))
        # A fully supported COO byte vector is a byte carrier too.
        packed_coo = tensor((len(wire),), {(i,): byte for i, byte in enumerate(wire)}, "u8")
        add(f"(tensor:inspect (tensor:decode-bytes {packed_coo}))",
            observation((3,), {(i,): value for i, value in enumerate(values)}, dtype))
    add("(tensor:decode-bytes (tensor:from-values u8 (3) (67 84 78)))", ["Tensor:Fault", "invalid-encoding"])
    add("(tensor:decode-bytes (tensor:from-values i64 (1) (0)))", ["Tensor:Fault", "unsupported-dtype"])
    add("(tensor:decode-bytes (tensor:from-coo u8 (5) ((0 67))))", ["Tensor:Fault", "invalid-encoding"])
    add("(tensor:encode-bytes (tensor:from-values bool (1) (True)))", ["Tensor:Fault", "invalid-encoding"])
    add("(tensor:encode-bytes Unrecognized)", ["tensor:encode-bytes", "Unrecognized"])

    results = []
    for language, flags in (("petta", ["--lang", "petta"]), ("he", ["--lang", "he"])):
        fixture = args.output / f"{language}.metta"
        library = "(library tensor)" if language == "petta" else "tensor"
        fixture.write_text(f"!(import! &self {library})\n" + "\n".join(calls) + "\n")
        command = [str(args.cetta.resolve()), *flags, "--fuel", "10000000", str(fixture.resolve())]
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        (args.output / f"{language}.log").write_text(run.stdout + run.stderr)
        actual = [form for form in parse_forms(run.stdout)
                  if isinstance(form, list) and form and form[0] == "TensorSequenceCase"]
        # Boolean spelling is a language policy; coordinate values are shared.
        wrong = [i for i, (left, right) in enumerate(zip(actual, expected))
                 if normalized(left) != normalized(right)]
        results.append({"language": language, "exit": run.returncode, "answers": len(actual), "wrong": wrong})
        if wrong:
            for i in wrong[:4]:
                print(language, i, "expected", expected[i], "actual", actual[i])
        if run.returncode:
            print(run.stderr[-1500:])
    unimported = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", "-e",
                                "!(tensor:is-tensor Unrecognized)"], cwd=ROOT,
                               capture_output=True, text=True, timeout=10)
    unavailable = unimported.returncode == 0 and parse_forms(unimported.stdout) == [
        ["tensor:is-tensor", "Unrecognized"]]
    passed = unavailable and all(row["exit"] == 0 and row["answers"] == len(expected)
                                and not row["wrong"] for row in results)
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    receipt = {"passed": passed, "cases_per_language": len(expected), "results": results,
               "unimported_unreduced": unavailable,
               "binary_sha256": digest(args.cetta.resolve()), "reference": "coordinate-fiber and dictionary model",
               "source_sha256": {name: digest(ROOT / name) for name in (
                   "src/tensor_sequence.inc", "src/tensor.c", "src/tensor.h", "src/library_tensor.c",
                   "src/symbol.h", "lib/tensor.metta", "tests/support/check_tensor_sequences.py")}}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
