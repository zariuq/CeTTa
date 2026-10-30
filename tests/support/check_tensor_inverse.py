#!/usr/bin/env python3
"""Independent backward proposals, full NC2 costs and held-out prediction."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, construct, encoded
from check_ic_numeric_arrays import archive
from check_ic_search import check_controls
from check_tensor_program import array, canonical, records, source_array
from check_tensor_sequences import coordinates, normalized


RECIPES = ["TP:Add", "TP:Subtract", "TP:Multiply", "TP:Divide", "TP:BroadcastLike"]
FAMILY = ["TP:Family", Text("TF1"), [["TP:Operator", Text(recipe[3:]),
           [Text("F64Tensor"), Text("F64Tensor")], Text("F64Tensor"), recipe] for recipe in RECIPES]]


def proposals(inputs, desired, mask, path):
    """Recursive inverse relation, without controller cursors or a work queue."""
    if not path:
        yield desired
        return
    op, slot, known_index = path[0]
    known = inputs[known_index]
    if known[1] != desired[1]:
        return
    if op == 4:
        seen = set()
        for point in coordinates(desired[1]):
            if not mask[2][point]:
                continue
            value = desired[2][point]
            key = struct.pack("<d", value)
            if key in seen:
                continue
            seen.add(key)
            yield from proposals(inputs, array((), [value]), array((), [True], "BoolTensor"), path[1:])
        if not seen:
            yield from proposals(inputs, array((), [0.0]), array((), [False], "BoolTensor"), path[1:])
        return
    values, remaining = [], []
    for point in coordinates(desired[1]):
        active = mask[2][point]
        y = desired[2][point] if active else 0.0
        x = known[2][point] if active else 0.0
        try:
            if op == 0:
                value = y - x
            elif op == 1:
                value = y + x if slot == 0 else x - y
            elif op == 2:
                value = y / x if x != 0.0 else 0.0
                active = active and x != 0.0
            else:
                value = y * x if slot == 0 else x / y if y != 0.0 else 1.0
                active = active and (slot == 0 or y != 0.0)
            if not math.isfinite(value):
                return
        except OverflowError:
            return
        values.append(value)
        remaining.append(active)
    yield from proposals(inputs, array(desired[1], values), array(desired[1], remaining, "BoolTensor"), path[1:])


def program(inputs, path, parameter):
    rows = [("input", i, "F64Tensor") for i in range(len(inputs))] + [("constant", parameter)]
    root = len(rows) - 1
    for op, slot, known in reversed(path):
        children = [root, known] if slot == 0 else [known, root]
        rows.append(("op", op, [], children))
        root = len(rows) - 1
    roots, compact = canonical([root], records(rows))
    return encoded(["TP:Program", Text("TP1"), FAMILY, ["Dag:Forest", roots, compact]])


def forward(inputs, path, parameter):
    value = parameter
    for op, slot, known_index in reversed(path):
        known = inputs[known_index]
        if op == 4:
            if value[1] != ():
                return None
            value = array(known[1], [value[2][()] for _ in coordinates(known[1])])
            continue
        if value[1] != known[1]:
            return None
        result = []
        for point in coordinates(value[1]):
            a, b = (value[2][point], known[2][point]) if slot == 0 else (known[2][point], value[2][point])
            try:
                result.append([lambda: a + b, lambda: a - b, lambda: a * b, lambda: a / b][op]())
            except (ZeroDivisionError, OverflowError):
                return None
        if not all(math.isfinite(x) for x in result):
            return None
        value = array(value[1], result)
    return value


def reference(inputs, output, mask, tolerance, path):
    original = archive(inputs, output, mask, tolerance)
    best, seen, accepted, selected = original, 0, 0, None
    for parameter in proposals(inputs, output, mask, path):
        seen += 1
        model = program(inputs, path, parameter)
        candidate = archive(inputs, output, mask, tolerance, model)
        actual = forward(inputs, path, parameter)
        valid = actual is not None and actual[1] == output[1] and all(
            not mask[2][point] or abs(actual[2][point] - output[2][point]) <= tolerance
            for point in coordinates(output[1]))
        if valid and len(candidate) < len(best):
            best, accepted, selected = candidate, accepted + 1, parameter
    return original, best, seen, accepted, selected


HELPERS = r'''
(= (inverse-test:pieces $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $first $next)
       (if (> $remaining 0) (inverse-test:pieces $next (- $remaining 1))
         (let $closed (ic:release $next) InverseTestBudget))) ($other $other))))
(= (inverse-test:view $result $expected)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let $closed (ic:release $state)
         (InverseFit (== $best $expected) $seen $accepted $faults $reason (< $turns 100)))) ($other $other))))
(= (inverse-test:run $original $region $pieces $expected)
   (case (ic:prepare-fit Numeric:InversePrograms $original $region ())
     (((IC:PreparedSearch $state)
       (inverse-test:view (if $pieces (inverse-test:pieces $state 4000) (ic:resume $state 4000)) $expected))
      ($other $other))))
(= (inverse-test:spin) (IC:Responses ()))
(= (inverse-test:spin) (inverse-test:spin))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_tensor_inverse.metta").read_text())
    code = ['!(import! &self (library lib_tensor_inverse))', HELPERS]
    expected, fits, models = [], [], []

    def add(call, wanted="true"):
        index = len(expected)
        code.append(f"!(println! (InverseCase {index} {call}))")
        expected.append(["InverseCase", str(index), wanted])

    def region(inputs, path):
        steps = [["TP:InverseBinary", op, [], slot, known] for op, slot, known in path]
        paths = [["TP:InversePath", 99, steps]]
        sorts = list(map(Text, ["F64Tensor"] * len(inputs)))
        return f"(tensor-program:prepare-inverse {construct(FAMILY)} {construct(sorts)} () {construct(paths)})"

    count = 384
    x = array((count,), [float(i % 7 - 3) for i in range(count)])
    nz = array((count,), [float(i % 6 + 1) for i in range(count)])
    z = array((count,), [float(i % 5 - 2) for i in range(count)])
    mask = array((count,), [True] * count, "BoolTensor")
    cases = [
        ([x], [2.0 * a for a in x[2].values()], 0.0, [(2, 1, 0), (4, 0, 0)]),
        ([x], [a + 3.0 for a in x[2].values()], 0.0, [(0, 1, 0), (4, 0, 0)]),
        ([x], [3.0 - a for a in x[2].values()], 0.0, [(1, 0, 0), (4, 0, 0)]),
        ([x], [a - 3.0 for a in x[2].values()], 0.0, [(1, 1, 0), (4, 0, 0)]),
        ([x], [a / 2.0 for a in x[2].values()], 0.0, [(3, 1, 0), (4, 0, 0)]),
        ([nz], [6.0 / a for a in nz[2].values()], 0.0, [(3, 0, 0), (4, 0, 0)]),
        ([x, z], [2.0 * a + b for a, b in zip(x[2].values(), z[2].values())], 0.0,
         [(0, 1, 1), (2, 1, 0), (4, 0, 0)]),
        ([array((32, 12), list(x[2].values()))], [2.0 * a for a in x[2].values()], 0.0,
         [(2, 1, 0), (4, 0, 0)]),
        ([x], [0.0, 0.5] + [1.0] * (count - 2), 0.6, [(4, 0, 0)]),
    ]
    for inputs, outputs, tolerance, path in cases:
        target = array(inputs[0][1], outputs)
        case_mask = array(target[1], [True] * count, "BoolTensor")
        original, best, seen, accepted, parameter = reference(inputs, target, case_mask, tolerance, path)
        assert accepted == 1 and parameter is not None, (seen, accepted)
        declared = region(inputs, path)
        add(f"(tensor-program:inverse-region? {declared})")
        wanted = ["InverseFit", "true", str(seen), str(accepted), "0", "RegionExhausted", "true"]
        for pieces in ("False", "True"):
            add(f"(inverse-test:run {byte_tensor(original)} {declared} {pieces} {byte_tensor(best)})", wanted)
        new_inputs = [array((3,), [-2.0, 3.0, 0.0])] if len(inputs) == 1 else [
            array((3,), [-2.0, 3.0, 0.0]), array((3,), [5.0, -1.0, 2.0])]
        if inputs[0] is nz:
            new_inputs = [array((3,), [2.0, 3.0, 6.0])]
        prediction = forward(new_inputs, path, parameter)
        assert prediction is not None
        values = "(" + " ".join(map(source_array, new_inputs)) + ")"
        add(f'''(case (ic:predict Numeric:InversePrograms {byte_tensor(best)} {values})
           (((IC:Prediction $actual) (== $actual {source_array(prediction)})) ($other $other)))''')
        fits.append({"path": path, "samples": seen, "accepted": accepted,
                     "original_bytes": len(original), "best_bytes": len(best)})
        models.append((original, best, declared, wanted))
    original, best, declared, wanted = models[-1]
    add(f'''(case (ic:prepare-fit Numeric:InversePrograms {byte_tensor(original)} {declared} ())
       (((IC:PreparedSearch (IC:SearchState $task $queue $winner $stats $first))
         (let $cheap (work-new (inverse-test:spin))
           (case (ic-search:open ((IC:Branch 0 $cheap)) 64 $queue)
             (((IC:Some $combined)
               (case (ic:resume (IC:SearchState $task $combined $winner $stats $first) 256)
                 (((IC:SearchResult $actual $statistics $reason $fault $next)
                   (let $closed (ic:release $next)
                     (BehindLoop (== $actual {byte_tensor(best)}) $reason)))))))))) ($other $other)))''',
        ["BehindLoop", "true", "BudgetExhausted"])
    for paths in ("((TP:InversePath -1 ()))", "((TP:InversePath 0 ((TP:InverseBinary 4 () 1 0))))",
                  "((TP:InversePath 0 ((TP:InverseBinary 2 () 1 9))))"):
        add(f'(tensor-program:prepare-inverse {construct(FAMILY)} ("F64Tensor") () {paths})', ["TP:Fault", "InversePaths"])
    add('(ic:prepare-fit Numeric:InversePrograms NotAnArchive (TP:InverseRegion Bad () () ()) ())',
        ["IC:FitRefused", "InverseRegion"])
    # An inconsistent zero-product target cannot become a valid model merely
    # because backward propagation marked its parameter unconstrained.
    zero = array((count,), [0.0] * count)
    raw = archive([zero], array((count,), [1.0] * count), mask, 0.0)
    zero_region = region([zero], [(2, 1, 0), (4, 0, 0)])
    add(f"(inverse-test:run {byte_tensor(raw)} {zero_region} False {byte_tensor(raw)})",
        ["InverseFit", "true", "1", "0", "0", "RegionExhausted", "true"])
    for inputs, target, keep in [
        ([x], array((count,), [5.0] * count), array((count,), [False] * count, "BoolTensor")),
        ([array((0,), [])], array((0,), []), array((0,), [], "BoolTensor")),
    ]:
        path_steps = [(4, 0, 0)]
        raw, best, seen, accepted, _parameter = reference(inputs, target, keep, 0.0, path_steps)
        add(f"(inverse-test:run {byte_tensor(raw)} {region(inputs, path_steps)} False {byte_tensor(best)})",
            ["InverseFit", "true", str(seen), str(accepted), "0", "RegionExhausted", "true"])
    seed = construct(records([("input", 0, "F64Tensor")]))
    small, small_mask = array((3,), [1.0, 2.0, 3.0]), array((3,), [True] * 3, "BoolTensor")
    add(f'''(ic-search:materialize IC:InverseArrays NoOriginal
       (TP:InverseStep {construct(FAMILY)} {seed} ({source_array(small)})
         {source_array(small)} {source_array(small_mask)} ((TP:InverseBinary 99 () 1 0)) () 0))''',
        ["IC:ProducerFault", "InverseState"])
    for position, seen_values, masks in [("-1", "()", small_mask), ("4", "()", small_mask),
                                         ("NoPosition", "()", small_mask),
                                         ("0", "NotAList", small_mask), ("0", "()", small)]:
        add(f'''(ic-search:materialize IC:InverseArrays NoOriginal
          (TP:InverseSamples {construct(FAMILY)} {seed} ({source_array(small)})
            {source_array(small)} {source_array(masks)} () () 0 {position} {seen_values} False))''',
            ["IC:ProducerFault", "InverseCursor"])
    add(f'''(ic-search:materialize IC:InverseArrays NoOriginal
       (TP:InverseStep {construct(FAMILY)} NotRecords ()
         {source_array(small)} {source_array(small_mask)} () () 0))''', ["IC:ProducerFault", "InverseState"])
    add(f'(tensor-program:inverse-region? (TP:InverseRegion {construct(FAMILY)} NotRecords () ()))', "false")
    fixed_family = ["TP:Family", Text("TF1"), [["TP:Operator", Text("fixed-broadcast"),
                    [Text("F64Tensor")], Text("F64Tensor"), "TP:Broadcast"]]]
    fixed_target = array((count,), [2.0] * count)
    fixed_raw = archive([x], fixed_target, mask, 0.0)
    fixed_rows = [("input", 0, "F64Tensor"), ("constant", array((), [2.0])),
                  ("op", 0, [[count]], [1])]
    roots, compact = canonical([2], records(fixed_rows))
    fixed_model = encoded(["TP:Program", Text("TP1"), fixed_family, ["Dag:Forest", roots, compact]])
    fixed_best = archive([x], fixed_target, mask, 0.0, fixed_model)
    for size, best, accepted in ((count, fixed_best, "1"), (count - 1, fixed_raw, "0")):
        fixed_paths = [["TP:InversePath", 0, [["TP:InverseUnary", 0, [[size]]]]]]
        declared = f'(tensor-program:prepare-inverse {construct(fixed_family)} ("F64Tensor") () {construct(fixed_paths)})'
        add(f"(inverse-test:run {byte_tensor(fixed_raw)} {declared} False {byte_tensor(best)})",
            ["InverseFit", "true", "1", accepted, "0", "RegionExhausted", "true"])
    add(f'''(case (tp-inverse:binary TP:Add 1
         {source_array(array((2,), [-1.0e308, 3.0]))} {source_array(array((2,), [1.0e308, 1.0]))}
         {source_array(array((2,), [False, True], 'BoolTensor'))})
       (((TP:Constraint $values $mask) (== $values {source_array(array((2,), [0.0, 2.0]))})) ($other $other)))''')
    source = "\n".join(code) + "\n"
    path = args.output / "inverse.metta"
    path.write_text(source)
    check_controls(source)
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())], cwd=ROOT,
                            capture_output=True, text=True, timeout=120)
    (args.output / "inverse.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "InverseCase"]
    indexed = {f[1]: f for f in actual}
    wrong = [{"case": i, "expected": wanted, "actual": indexed.get(str(i))} for i, wanted in enumerate(expected)
             if normalized(indexed.get(str(i))) != normalized(wanted)]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong, "fits": fits,
               "scope": "typed backward path proposals, sampled scalar parameters and forward original-task acceptance; not all IEEE preimages"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [row['case'] for row in wrong]}))
    for row in wrong[:3]:
        print(json.dumps(row)[:1800])
    if result.returncode:
        print(result.stderr[-1800:])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
