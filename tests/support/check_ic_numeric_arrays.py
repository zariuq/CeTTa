#!/usr/bin/env python3
"""Independent NC2 framing, typed array tasks and bounded multi-input discovery."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
import math
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, construct, encoded
from check_ic_numeric import archive as vector_archive
from check_ic_numeric_program import family as vector_family, reference as vector_reference
from check_ic_search import check_controls
from check_tensor_program import array, canonical, records, source_array, transport
from check_tensor_sequences import coordinates, normalized


def field(value):
    return encoded(["TP:Constant", Text(value[0]), transport(value)])


def archive(inputs, output, mask, tolerance=None, model=None):
    comparison = "Numeric:Exact" if tolerance is None else [
        "Numeric:Absolute", field(array((1,), [tolerance]))]
    body = ["Numeric:Raw", field(output)] if model is None else ["Numeric:Program", model]
    return encoded(["Numeric:ArrayArchive", Text("NC2"), list(map(field, inputs)),
                    Text(output[0]), field(mask), comparison, body])


def family(operators):
    return ["TP:Family", Text("TF1"), [["TP:Operator", Text(recipe[3:]),
            list(map(Text, inputs)), Text(output), recipe] for recipe, inputs, output, _params in operators]]


def programs(input_sorts, constants, operators, wanted, maximum):
    """Recursive grammar, independent of the authored explicit builder worklist."""
    seeds = [("input", i, sort) for i, sort in enumerate(input_sorts)]
    seeds += [("constant", value) for value in constants]
    initial = list(input_sorts) + [value[0] for value in constants]

    def descend(rows, sorts, used):
        for root, sort in enumerate(sorts):
            if sort == wanted:
                yield root, deepcopy(rows)
        if used == maximum:
            return
        for index, (_recipe, inputs, output, alternatives) in enumerate(operators):
            for params in alternatives:
                choices = [[i for i, sort in enumerate(sorts) if sort == expected] for expected in inputs]
                for children in itertools.product(*choices):
                    yield from descend(rows + [("op", index, params, list(children))], sorts + [output], used + 1)

    yield from descend(seeds, initial, 0)


def predict(rows, root, inputs, operators):
    # Each root is expanded recursively; no native DAG fold, memoization or
    # native tensor result is used to determine the expected observation.
    def visit(index):
        row = rows[index]
        if row[0] == "input":
            return inputs[row[1]]
        if row[0] == "constant":
            return row[1]
        _tag, op, _parameters, children = row
        values = [visit(child) for child in children]
        recipe, _sorts, output, _alternatives = operators[op]
        if recipe in ('TP:Where', 'TP:WhereBool', 'TP:WhereI64'):
            if values[0][0] != 'BoolTensor' or not values[1][0] == values[2][0] == output:
                raise ValueError('sort')
            if values[0][1] != values[1][1] or values[1][1] != values[2][1]:
                raise ValueError('shape')
            return array(values[0][1], {point: values[1 if flag else 2][2][point]
                                       for point, flag in values[0][2].items()
                                       if point in values[1 if flag else 2][2]}, output)
        left, right = values
        if recipe == "TP:Matvec":
            if len(left[1]) != 2 or len(right[1]) != 1 or left[1][1] != right[1][0]:
                raise ValueError("shape")
            result = []
            for r in range(left[1][0]):
                total = 0.0
                for c in range(left[1][1]):
                    total += left[2][r, c] * right[2][c,]
                result.append(total)
            return array((left[1][0],), result, output)
        if left[1] != right[1]:
            raise ValueError("shape")
        operation = {"TP:Less": lambda a, b: a < b, "TP:Greater": lambda a, b: a > b,
                     "TP:Add": lambda a, b: a + b}[recipe]
        return array(left[1], [operation(left[2][point], right[2][point]) for point in coordinates(left[1])], output)

    try:
        result = visit(root)
        return result if all(math.isfinite(x) for x in result[2].values()) else None
    except (ValueError, OverflowError):
        return None


def reference(inputs, output, mask, tolerance, constants, operators, maximum):
    original = archive(inputs, output, mask, tolerance)
    best, accepted, seen = original, 0, 0
    for root, rows in programs([value[0] for value in inputs], constants, operators, output[0], maximum):
        seen += 1
        roots, compact = canonical([root], records(rows))
        model = encoded(["TP:Program", Text("TP1"), family(operators), ["Dag:Forest", roots, compact]])
        candidate = archive(inputs, output, mask, tolerance, model)
        actual = predict(rows, root, inputs, operators)
        valid = actual is not None and actual[:2] == output[:2] and all(
            not mask[2][point] or (actual[2][point] == output[2][point] if tolerance is None else
                                 abs(actual[2][point] - output[2][point]) <= tolerance)
            for point in coordinates(output[1]))
        if valid and len(candidate) < len(best):
            best, accepted = candidate, accepted + 1
    return original, best, seen, accepted


HELPERS = r'''
(= (array-test:prepared $result $expected)
   (case $result
     (((IC:NumericArtifact $bytes) (== $bytes $expected)) ($other $other))))
(= (array-test:observation $bytes $expected)
   (case (ic-arrays:observe $bytes)
     (((IC:Some (Numeric:ArrayValues $inputs $sort $mask $comparison $output)) (== $output $expected))
      ($other $other))))
(= (array-test:accepts $original $candidate)
   (case ((ic-arrays:observe $original) (ic-arrays:observe $candidate))
     ((((IC:Some $source) (IC:Some $result)) (ic-packed:accepts? IC:ArrayArchive $source $result))
      ($other InvalidObservation))))
(= (array-test:prediction $bytes $inputs $expected)
   (case (ic:predict Numeric:Programs $bytes $inputs)
     (((IC:Prediction $result) (== $result $expected)) ($other $other))))
(= (array-test:pieces $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (if (> $remaining 0) (array-test:pieces $next (- $remaining 1))
         (let $released (ic:release $next) (ArrayTestFault Budget)))) ($other $other))))
(= (array-test:fit $original $region $pieces $expected)
   (case (ic:prepare-fit Numeric:Programs $original $region ())
     (((IC:PreparedSearch $state)
       (let $result (if $pieces (array-test:pieces $state 1000) (ic:resume $state 1000))
         (case $result
           (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $next)
             (ArrayFit (== $best $expected) $seen $accepted $faults $reason (tensor:shape $best)))
            ($other $other))))) ($other $other))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("lib_ic_numeric_arrays.metta", "lib_ic_numeric_values.metta", "lib_ic_numeric.metta",
                 "lib_ic_numeric_program.metta", "lib_tensor_program_search.metta"):
        check_controls((ROOT / "lib/petta" / name).read_text())
    # Preparing through the base library before adding the optional search
    # adapter catches unresolved delegation accidentally classified as data.
    code = ['!(import! &self (library lib_ic_numeric))']
    # Install search-dependent helpers only after importing that adapter below.
    code.append(HELPERS[:HELPERS.index('(= (array-test:prediction')])
    expected, fits = [], []

    def add(call, wanted="true"):
        index = len(expected)
        code.append(f"!(println! (ArrayCase {index} {call}))")
        expected.append(["ArrayCase", str(index), wanted])

    def prepare(inputs, output, mask, tolerance):
        criterion = "Numeric:Exact" if tolerance is None else f"(Numeric:Absolute {tolerance})"
        values = "(" + " ".join(map(source_array, inputs)) + ")"
        return f"(ic:prepare-numeric (Numeric:Inputs {values}) {source_array(output)} {source_array(mask)} {criterion})"

    matrix = array((2, 2), [1.0, 2.0, 1.0, 2.0])
    integers = array((2, 2), [1, 2, 1, 2], "I64Tensor")
    labels = array((2, 2), [True, False, True, False], "BoolTensor")
    keep = array((2, 2), [True] * 4, "BoolTensor")
    for inputs, output, mask, tolerance in [
        ([matrix, matrix], matrix, keep, 0.01),
        ([matrix, integers], labels, keep, None),
        ([matrix], integers, keep, None),
        ([], array((), [3.0]), array((), [True], "BoolTensor"), 0.0),
        ([array((0, 3), [])], array((0, 2), []), array((0, 2), [], "BoolTensor"), 0.0),
    ]:
        blob = archive(inputs, output, mask, tolerance)
        add(f"(array-test:prepared {prepare(inputs, output, mask, tolerance)} {byte_tensor(blob)})")
        add(f"(array-test:observation {byte_tensor(blob)} {source_array(output)})")
    original = archive([matrix], matrix, keep, 0.25)
    # Dense canonicalization preserves explicit zeroes and complete sparse or
    # strided storage, without implying that holes are zero observations.
    dense_blob = archive([matrix], matrix, keep, 0.0)
    add(f'''(array-test:prepared
      (ic:prepare-numeric (Numeric:Inputs ((tensor:convert {source_array(matrix)} coo)))
        {source_array(matrix)} {source_array(keep)} (Numeric:Absolute 0.0)) {byte_tensor(dense_blob)})''')
    transposed = array((2, 2), [1.0, 1.0, 2.0, 2.0])
    add(f'''(array-test:prepared
      (ic:prepare-numeric (Numeric:Inputs ((tensor:view {source_array(matrix)} (2 2) (1 2) 0)))
        {source_array(matrix)} {source_array(keep)} (Numeric:Absolute 0.0))
      {byte_tensor(archive([transposed], matrix, keep, 0.0))})''')
    changed = array((2, 2), [1.125, 2.0, 1.0, 2.0])
    add(f"(array-test:accepts {byte_tensor(original)} {byte_tensor(archive([matrix], changed, keep, 0.25))})")
    far = array((2, 2), [1.375, 2.0, 1.0, 2.0])
    add(f"(array-test:accepts {byte_tensor(original)} {byte_tensor(archive([matrix], far, keep, 0.25))})", "false")
    # A tolerance is always checked against the original, not accumulated
    # between successive accepted states.
    zero = array((2, 2), [0.0] * 4)
    current = array((2, 2), [0.2] * 4)
    drift = array((2, 2), [0.4] * 4)
    add(f"(ic-packed:check IC:ArrayArchive {byte_tensor(archive([matrix], zero, keep, 0.25))} "
        f"{byte_tensor(archive([matrix], current, keep, 0.25))} {byte_tensor(archive([matrix], drift, keep, 0.25))})",
        ["IC:PackedRejected", "Reconstruction"])
    masked = array((2, 2), [False, True, False, True], "BoolTensor")
    other_labels = array((2, 2), [False, False, False, False], "BoolTensor")
    add(f"(array-test:accepts {byte_tensor(archive([matrix], labels, masked))} "
        f"{byte_tensor(archive([matrix], other_labels, masked))})")
    add(f"(array-test:accepts {byte_tensor(archive([matrix], labels, keep))} "
        f"{byte_tensor(archive([matrix], other_labels, keep))})", "false")
    for inputs, mask, tolerance in [([changed], keep, 0.25), ([matrix, matrix], keep, 0.25),
                                    ([matrix], masked, 0.25), ([matrix], keep, 0.5)]:
        add(f"(array-test:accepts {byte_tensor(original)} {byte_tensor(archive(inputs, matrix, mask, tolerance))})", "false")
    add(f"(array-test:accepts {byte_tensor(archive([matrix, changed], matrix, keep, 0.25))} "
        f"{byte_tensor(archive([changed, matrix], matrix, keep, 0.25))})", "false")
    for call, reason in [
        (prepare([matrix], matrix, labels, -1.0), "Observation"),
        (prepare([matrix], matrix, labels, None), "Observation"),
        (prepare([matrix], integers, labels, 0.0), "Observation"),
        (prepare([matrix], matrix, array((4,), [True] * 4, "BoolTensor"), 0.0), "Shape"),
        (prepare([array((2, 2), {(0, 0): 1.0})], matrix, keep, 0.0), "Observation"),
        (f"(ic:prepare-numeric (Numeric:Inputs ({source_array(matrix)})) {source_array(matrix)} "
         f"{source_array(keep)} (Numeric:Absolute NoPrecision))", "Observation"),
        (f"(ic:prepare-numeric (Numeric:Inputs ({source_array(matrix)})) {source_array(matrix)} "
         f"{source_array(keep)} (Numeric:Absolute 1.0e309))", "Observation"),
    ]:
        add(call, ["IC:NumericFault", reason])
    add(f"(ic-arrays:observe {byte_tensor(original + b'broken')})", "IC:None")
    # Physical Bool admission rejects a stored byte 2 instead of accepting an
    # arbitrary U8 payload merely because its declaration says BoolTensor.
    damaged_field = encoded(["TP:Constant", Text("BoolTensor"), transport(keep)[:-1] + b"\2"])
    damaged = encoded(["Numeric:ArrayArchive", Text("NC2"), [field(matrix)], Text("F64Tensor"), damaged_field,
                       ["Numeric:Absolute", field(array((1,), [0.0]))], ["Numeric:Raw", field(matrix)]])
    add(f"(ic-arrays:observe {byte_tensor(damaged)})", "IC:None")
    for precision in (array((), [0.0]), array((0,), []), array((2,), [0.0, 0.0]),
                      array((1,), [-0.5]), array((1,), [math.inf]), array((1,), [True], "BoolTensor")):
        invalid = encoded(["Numeric:ArrayArchive", Text("NC2"), [field(matrix)], Text("F64Tensor"), field(keep),
                           ["Numeric:Absolute", field(precision)], ["Numeric:Raw", field(matrix)]])
        add(f"(ic-arrays:observe {byte_tensor(invalid)})", "IC:None")
    # The base module is already loaded. Later registration must still give
    # NC1 and NC2 the same complete materialization/checking path.
    code.append('!(import! &self (library lib_ic_numeric_program))')
    code.append(HELPERS[HELPERS.index('(= (array-test:prediction'):])
    count = 256
    xs = [(float(i % 7 - 3), float(i % 5 - 2)) for i in range(count)]
    training_matrix = array((count, 2), [x for row in xs for x in row])
    targets = array((count,), [2.0 * x - y for x, y in xs])
    mask = array((count,), [True] * count, "BoolTensor")
    weights = [array((2,), [2.0, -1.0]), array((2,), [1.0, 1.0])]
    matvec = [("TP:Matvec", ["F64Tensor", "F64Tensor"], "F64Tensor", [["sequential"]])]
    classify_count = 2048
    x = array((classify_count,), [float(i % 7 - 3) for i in range(classify_count)])
    y = array((classify_count,), [0.5] * classify_count)
    classes = array((classify_count,), [value < 0.5 for value in x[2].values()], "BoolTensor")
    comparison = [("TP:Less", ["F64Tensor", "F64Tensor"], "BoolTensor", [[]]),
                  ("TP:Greater", ["F64Tensor", "F64Tensor"], "BoolTensor", [[]])]
    models = []
    for inputs, output, keep_mask, tolerance, constants, operators in [
        ([training_matrix], targets, mask, 0.0, weights, matvec),
        ([x, y], classes, array((classify_count,), [True] * classify_count, "BoolTensor"), None, [], comparison),
    ]:
        raw, best, seen, accepted = reference(inputs, output, keep_mask, tolerance, constants, operators, 1)
        assert accepted > 0 and len(best) < len(raw)
        domains = [["TP:Domain", i, row[3]] for i, row in enumerate(operators)]
        constants_source = "(" + " ".join(f'(TP:ConstantSeed "{value[0]}" {source_array(value)})' for value in constants) + ")"
        sorts = construct(list(map(Text, [value[0] for value in inputs])))
        region = f"(tensor-program:prepare-region {construct(family(operators))} {sorts} {constants_source} {construct(domains)} 1)"
        add(f"(array-test:prepared {prepare(inputs, output, keep_mask, tolerance)} {byte_tensor(raw)})")
        wanted = ["ArrayFit", "true", str(seen), str(accepted), "0", "RegionExhausted", [str(len(best))]]
        for pieces in ("False", "True"):
            add(f"(array-test:fit {byte_tensor(raw)} {region} {pieces} {byte_tensor(best)})", wanted)
        models.append((raw, best, region))
        fits.append({"operators": [row[0] for row in operators], "candidates": seen, "accepted": accepted,
                     "original_bytes": len(raw), "best_bytes": len(best)})
    matrix_raw, matrix_best, matrix_region = models[0]
    classes_raw, classes_best, _region = models[1]
    held_matrix = array((2, 2), [1.0, 2.0, 3.0, 4.0])
    add(f"(array-test:prediction {byte_tensor(matrix_best)} ({source_array(held_matrix)}) {source_array(array((2,), [0.0, 2.0]))})")
    test_x, test_y = array((3,), [0.0, 2.0, -1.0]), array((3,), [1.0, 1.0, -2.0])
    add(f"(array-test:prediction {byte_tensor(classes_best)} ({source_array(test_x)} {source_array(test_y)}) "
        f"{source_array(array((3,), [True, False, False], 'BoolTensor'))})")
    for inputs in ("()", f"({source_array(test_x)})", f"({source_array(labels)} {source_array(test_y)})"):
        add(f"(ic:predict Numeric:Programs {byte_tensor(classes_best)} {inputs})", ["IC:NumericFault", "Inputs"])
    add(f"(ic:predict Numeric:Programs {byte_tensor(matrix_raw)} ({source_array(held_matrix)}))", ["IC:NumericFault", "NoModel"])
    vector_inputs, vector_outputs, vector_mask = [1.0] * 128, [float(i + 1) for i in range(128)], [True] * 128
    vector_ops = [("prefix", "TP:Scan", 1)]
    vector_raw = vector_archive(vector_inputs, vector_outputs, vector_mask, 0.0)
    vector_best, vector_seen, vector_accepted = vector_reference(vector_inputs, vector_outputs, vector_mask, 0.0, vector_ops, 1)
    vector_region = f'(tensor-program:prepare-region {construct(vector_family(vector_ops))} ("F64Tensor") () ((TP:Domain 0 ((0)))) 1)'
    add(f"(array-test:fit {byte_tensor(vector_raw)} {vector_region} False {byte_tensor(vector_best)})",
        ["ArrayFit", "true", str(vector_seen), str(vector_accepted), "0", "RegionExhausted", [str(len(vector_best))]])
    add(f'''(case (ic:prepare-fit Numeric:Programs {byte_tensor(matrix_raw)} {matrix_region} ())
       (((IC:PreparedSearch $state)
         (let* (($change (add-atom &self ArrayTaskDependencyChanged)) ($result (ic:resume $state 0))
                ($closed (ic:release $state))) $result)) ($other $other)))''',
        ["IC:ResumeRejected", "ChangedDependencies"])
    add('(tensor-program:proposals-for "Unknown" (TP:Region (TP:Family "TF1" ()) () () () 0))',
        ["TP:Fault", "RootSort"])
    # A declared Tensor and invented inspectors cannot substitute for owned
    # native storage, even on the new multiple-input entry point.
    code.append('''
(: ArrayTaskImpostor Tensor)
(= (tensor:dtype ArrayTaskImpostor) f64)
(= (tensor:shape ArrayTaskImpostor) (1))
(= (tensor:all-finite ArrayTaskImpostor) True)
''')
    add(f"(ic:prepare-numeric (Numeric:Inputs (ArrayTaskImpostor)) {source_array(matrix)} {source_array(keep)} (Numeric:Absolute 0.0))",
        ["IC:NumericFault", "Observation"])
    source = "\n".join(code) + "\n"
    check_controls(source)
    path = args.output / "arrays.metta"
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())], cwd=ROOT,
                            text=True, capture_output=True, timeout=120)
    (args.output / "arrays.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "ArrayCase"]
    found = {f[1]: f for f in actual}
    wrong = [{"case": i, "expected": want, "actual": found.get(str(i))} for i, want in enumerate(expected)
             if normalized(found.get(str(i))) != normalized(want)]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong, "fits": fits,
               "scope": "NC2 complete bytes, typed matrix/classification tasks and bounded forward discovery; not inverse completeness"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [row["case"] for row in wrong]}))
    for row in wrong[:4]:
        print(json.dumps(row)[:2200])
    if result.returncode:
        print(result.stderr[-1800:])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
