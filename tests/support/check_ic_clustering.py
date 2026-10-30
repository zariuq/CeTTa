#!/usr/bin/env python3
"""Independent bounded prototype discovery, complete model cost and prediction."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, encoded
from check_ic_numeric_arrays import archive
from check_ic_search import check_controls
from check_tensor_program import array, compact_integer_literal, source_array, transport
from check_tensor_sequences import coordinates, normalized


FAMILY = ["TP:Family", Text("TF1"), [["TP:Operator", Text("GatherRows"),
          [Text("F64Tensor"), Text("I64Tensor")], Text("F64Tensor"), "TP:GatherRows"]]]


def model(centers, labels, width):
    values = array((len(centers), width), [x for row in centers for x in row])
    records = [["Binary:Leaf", encoded(["TP:Constant", Text("F64Tensor"), transport(values)])],
               ["Binary:Leaf", compact_integer_literal(labels)],
               ["Binary:Node", encoded(["TP:Apply", 0, []]), [0, 1]]]
    return encoded(["TP:Program", Text("TP1"), FAMILY, ["Dag:Forest", [2], records]])


def nearest(point, observed, centers):
    best = None
    for index, center in enumerate(centers):
        distance = 0.0
        for x, y, use in zip(point, center, observed):
            delta = x - y if use else 0.0
            distance += delta * delta
        if not math.isfinite(distance):
            raise ArithmeticError("distance")
        if best is None or distance < best[1]:
            best = index, distance
    return best


def propose(points, mask, count, start, updates):
    """Whole-array reference, independent of the authored sliced cursors."""
    width = len(points[0])
    centers = [[x if use else 0.0 for x, use in zip(points[start], mask[start])]]
    chosen = [start]
    while len(centers) < count:
        farthest = None
        for index, (point, observed) in enumerate(zip(points, mask)):
            _label, distance = nearest(point, observed, centers)
            if index not in chosen and (farthest is None or distance > farthest[1]):
                farthest = index, distance
        index = farthest[0]
        chosen.append(index)
        centers.append([x if use else 0.0 for x, use in zip(points[index], mask[index])])
    for iteration in range(updates + 1):
        labels = [nearest(point, observed, centers)[0] for point, observed in zip(points, mask)]
        yield centers, labels, iteration
        if iteration == updates:
            return
        next_centers = []
        for group, previous in enumerate(centers):
            row = []
            for coordinate in range(width):
                total, observed_count = 0.0, 0.0
                for point, observed, label in zip(points, mask, labels):
                    member = observed[coordinate] and label == group
                    total += point[coordinate] if member else 0.0
                    observed_count += 1.0 if member else 0.0
                value = total / observed_count if observed_count else previous[coordinate]
                if not math.isfinite(value):
                    raise ArithmeticError("mean")
                row.append(value)
            next_centers.append(row)
        bits = lambda rows: b"".join(struct.pack("<d", x) for row in rows for x in row)
        if bits(next_centers) == bits(centers):
            return
        centers = next_centers


def reference(points, mask, tolerance, counts, starts, updates):
    width = len(points[0])
    output = array((len(points), width), [x for row in points for x in row])
    observed = array(output[1], [x for row in mask for x in row], "BoolTensor")
    original = archive([], output, observed, tolerance)
    best, winner, candidates = original, None, []
    for count in counts:
        for start in starts:
            try:
                for centers, labels, iteration in propose(points, mask, count, start, updates):
                    candidate = archive([], output, observed, tolerance, model(centers, labels, width))
                    valid = all(not use or abs(x - y) <= tolerance
                                for point, row_mask, label in zip(points, mask, labels)
                                for x, y, use in zip(point, centers[label], row_mask))
                    candidates.append({"count": count, "start": start, "iteration": iteration,
                                       "bytes": len(candidate), "accepted_observation": valid})
                    if valid and len(candidate) < len(best):
                        best, winner = candidate, (centers, labels)
            except ArithmeticError:
                pass
    return original, best, winner, candidates


HELPERS = r'''
(= (cluster-test:view $result $expected)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $state)
       (ClusterResult (== $best $expected) $reason $fault $faults (> $turns 0))) ($other $other))))
(= (cluster-test:pieces $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $fault $next)
       (if (> $remaining 0) (cluster-test:pieces $next (- $remaining 1))
         (let $closed (ic:release $next) (ClusterTestFault Budget)))) ($other $other))))
(= (cluster-test:fit $original $region $pieces $expected)
   (case (ic:prepare-fit Numeric:Clusters $original $region ())
     (((IC:PreparedSearch $state)
       (cluster-test:view (if $pieces (cluster-test:pieces $state 1000) (ic:resume $state 1000)) $expected))
      ($other $other))))
(= (cluster-test:prediction $artifact $inputs $labels $values)
   (case (ic:predict Numeric:Clusters $artifact $inputs)
     (((IC:ClusterPrediction $actual-labels $actual-values)
       (and (== $labels $actual-labels) (== $values $actual-values))) ($other $other))))
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for filename in ("lib_ic_clustering.metta", "lib_tensor_prototypes.metta", "lib_tensor_program.metta"):
        check_controls((ROOT / "lib/petta" / filename).read_text())
    code = ['!(import! &self (library lib_ic_clustering))', HELPERS]
    expected, workloads = [], []

    def add(call, result):
        index = len(expected)
        try:
            check_controls(call)
        except Exception as error:
            raise ValueError(f'ill-formed clustering control {index}: {call[-300:]}') from error
        code.append(f'!(println! (ClusterCase {index} {call}))')
        expected.append(["ClusterCase", str(index), result])

    points = [[(-3.0 if i % 2 == 0 else 3.0) + ((i // 2) % 4 - 1.5) * 0.08,
               (-6.0 if i % 2 == 0 else 6.0) + ((i // 8) % 4 - 1.5) * 0.08] for i in range(128)]
    complete = [[True, True] for _ in points]
    masked = [[True, i % 3 != 0] for i in range(128)]
    cases = [("updated-prototypes", points, complete, 0.13, [1, 2, 3], [0], 3),
             ("masked-and-duplicate-rows", points, masked, 0.15, [2], [1], 3),
             ("empty-group-first-tie", [[2.5] for _ in range(256)], [[True] for _ in range(256)], 0.0, [2], [0], 3),
             ("sliced-centroid-update", [[(-2.0 if i % 2 == 0 else 2.0) + (0.1 if i < 4 else -0.1)
                                          for _ in range(40)] for i in range(8)],
              [[True] * 40 for _ in range(8)], 0.11, [2], [0], 2),
             ("mean-overflow-keeps-winner", [[1e308] for _ in range(256)],
              [[True] for _ in range(256)], 0.0, [1], [0], 2),
             ("unobserved-rows", points, [[False, False] for _ in points], 0.0, [1], [0], 2),
             ("zero-width", [[] for _ in range(3)], [[] for _ in range(3)], 0.0, [2], [0], 1),
             ("distance-overflow", [[1e308], [-1e308]], [[True], [True]], 0.0, [2], [0], 1)]
    bindings = []
    for name, data, mask, tolerance, counts, starts, updates in cases:
        original, best, winner, proposals = reference(data, mask, tolerance, counts, starts, updates)
        if name in ("updated-prototypes", "sliced-centroid-update"):
            assert winner is not None and len(best) < len(original)
            assert not any(row["accepted_observation"] for row in proposals if row["iteration"] == 0)
        if name in ("empty-group-first-tie", "mean-overflow-keeps-winner"):
            assert winner is not None and len(best) < len(original)
        region = f'(TP:ClusterRegion ({" ".join(map(str, counts))}) ({" ".join(map(str, starts))}) {updates})'
        bound = f'(($original {byte_tensor(original)}) ($expected {byte_tensor(best)}))'
        bindings.append((original, best, region, bound))
        add(f'(let* {bound} (cluster-test:fit $original {region} False $expected))',
            ["ClusterResult", "true", "RegionExhausted", "IC:NoFault", "0", "true"])
        if name in ("updated-prototypes", "sliced-centroid-update"):
            add(f'(let* {bound} (cluster-test:fit $original {region} True $expected))',
                ["ClusterResult", "true", "RegionExhausted", "IC:NoFault", "0", "true"])
        workloads.append({"name": name, "original_bytes": len(original), "best_bytes": len(best),
                          "proposals": proposals, "checked_model": winner is not None})
        if winner is not None and name == "updated-prototypes":
            centers, _labels = winner
            new_points = [[-3.05, -6.02], [2.98, 6.04], [-3.05, -6.02], [0.0, 0.0]]
            new_mask = [[True, True], [True, True], [True, True], [False, False]]
            labels = [nearest(point, row_mask, centers)[0] for point, row_mask in zip(new_points, new_mask)]
            values = array((4, 2), [x for label in labels for x in centers[label]])
            inputs = source_array(array((4, 2), [x for row in new_points for x in row]))
            observed = source_array(array((4, 2), [x for row in new_mask for x in row], "BoolTensor"))
            label_source = source_array(array((4,), labels, "I64Tensor"))
            add(f'(let* {bound} (cluster-test:prediction $expected (Numeric:Points {inputs} {observed}) '
                f'{label_source} {source_array(values)}))', "true")
            add(f'(let* {bound} (cluster-test:prediction $expected '
                '(tensor:from-values f64 (0 2) ()) (tensor:from-values i64 (0) ()) '
                '(tensor:from-values f64 (0 2) ())))', "true")
            reverse_labels = list(reversed([nearest(point, [True, True], centers)[0] for point in new_points]))
            reverse_values = array((4, 2), [x for label in reverse_labels for x in centers[label]])
            add(f'(let* {bound} (cluster-test:prediction $expected (tensor:view {inputs} (4 2) (-2 1) 6) '
                f'{source_array(array((4,), reverse_labels, "I64Tensor"))} {source_array(reverse_values)}))', "true")

    original, best, region, bound = bindings[0]
    add(f'''(let* {bound} (case (ic:prepare-fit Numeric:Clusters $original {region} ())
      (((IC:PreparedSearch $state) (case (ic:resume $state 0)
        (((IC:SearchResult $value $stats $reason $fault $next)
          (let $closed (ic:release $next) (ZeroBudget (== $value $original) $stats $reason)))))))))''',
        ["ZeroBudget", "true", ["IC:Statistics", "0", "0", "0", "0"], "BudgetExhausted"])
    add(f'''(let* {bound} (case (ic:prepare-fit Numeric:Clusters $original {region} ())
      (((IC:PreparedSearch $state) (case (ic:cancel $state)
        (((IC:SearchResult $value $stats $reason $fault $next)
          (Cancelled $reason (cluster-test:view (ic:resume $next 1000) $expected)))))))))''',
        ["Cancelled", "Cancelled", ["ClusterResult", "true", "RegionExhausted", "IC:NoFault", "0", "true"]])
    add(f'''(let* {bound} (let $dependency (new-space)
      (case (ic:prepare-fit Numeric:Clusters $original {region} ($dependency))
        (((IC:PreparedSearch $state)
          (let* (($changed (add-atom $dependency Changed)) ($result (ic:resume $state 1))
                 ($closed (ic:release $state))) $result))))))''', ["IC:ResumeRejected", "ChangedDependencies"])
    for bad_region in ('(TP:ClusterRegion (0) (0) 1)', '(TP:ClusterRegion (129) (0) 1)',
                       '(TP:ClusterRegion (2) (128) 1)', '(TP:ClusterRegion (2) (0) -1)',
                       '(TP:ClusterRegion Wrong (0) 1)', '(TP:ClusterRegion (2) (0) 1.0)'):
        add(f'(let* {bound} (ic:prepare-fit Numeric:Clusters $original {bad_region} ()))',
            ["IC:FitRefused", "ClusterRegion"])
    add(f'(let* {bound} (ic:prepare-fit Numeric:Clusters $original {region} NotSources))',
        ["IC:FitRefused", "ClusterSources"])
    empty = archive([], array((0, 2), []), array((0, 2), [], 'BoolTensor'), 0.0)
    add(f'(ic:prepare-fit Numeric:Clusters {byte_tensor(empty)} (TP:ClusterRegion () () 1) ())',
        ["IC:FitRefused", "ClusterTask"])
    add(f'(let* {bound} (ic:predict Numeric:Clusters $original (tensor:from-values f64 (1 2) (0.0 0.0))))',
        ["IC:NumericFault", "NoModel"])
    centers = '(tensor:from-values f64 (2 2) (-3.0 -6.0 3.0 6.0))'
    labels = '(tensor:from-values i64 (128) (' + ' '.join('0' for _ in range(128)) + '))'
    invalid = [f'(TP:ClusterScan Wrong 2 0 {centers} () 0 () TP:None)',
               f'(TP:ClusterScan Fit 2 4 {centers} () 0 () TP:None)',
               f'(TP:ClusterScan Fit 2 0 {centers} () 1 (2) TP:None)',
               f'(TP:ClusterScan Seed 2 0 {centers} (0 0) 0 () TP:None)',
               f'(TP:ClusterScan Seed 2 0 {centers} (0 1) 1 () (TP:Farthest 0 0.0))',
               f'(TP:ClusterUpdate 3 {centers} {labels} 0 0 ())',
               f'(TP:ClusterUpdate 0 {centers} {labels} 0 2 ())',
               f'(TP:ClusterUpdate 0 {centers} {labels} 1 0 (0.0))',
               f'(TP:ClusterUpdate 0 {centers} (tensor:from-values i64 (1) (0)) 0 0 ())']
    for proposal in invalid:
        add(f'(let* {bound} (ic-search:materialize (TP:ClusterModel {region}) $original {proposal}))',
            ["IC:ProducerFault", "ClusterCursor"])
    # Repeated region entries remain repeated trajectories, not deduplicated
    # evidence. All four branches publish both their seed and updated model.
    expected_seen = 4 * len([row for row in workloads[0]['proposals'] if row['count'] == 2])
    add(f'''(let* {bound} (case (ic:fit Numeric:Clusters $original (TP:ClusterRegion (2 2) (0 0) 3) () 1000)
      (((IC:SearchResult $value (IC:Statistics $turns $seen $accepted $faults) $reason $fault $state)
        (RepeatedRegion (== $value $expected) $seen $accepted $faults $reason)))))''',
        ["RepeatedRegion", "true", str(expected_seen), "1", "0", "RegionExhausted"])
    source = '\n'.join(code) + '\n'
    check_controls(source)
    path = args.output / 'program.metta'
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(path.resolve())],
                            cwd=ROOT, text=True, capture_output=True, timeout=180)
    (args.output / 'program.log').write_text(result.stdout + result.stderr)
    actual = [row for row in parse_forms(result.stdout) if isinstance(row, list) and row and row[0] == 'ClusterCase']
    found = {row[1]: row for row in actual}
    wrong = [{'case': i, 'expected': want, 'actual': found.get(str(i))} for i, want in enumerate(expected)
             if normalized(found.get(str(i))) != normalized(want)]
    receipt = {'passed': result.returncode == 0 and len(actual) == len(expected) and not wrong,
               'checks': len(expected), 'answers': len(actual), 'mismatches': wrong,
               'workloads': workloads, 'scope': 'bounded farthest-first/Lloyd proposals, NC2 original-task checking, TP1 bytes and first-minimum prediction'}
    (args.output / 'results.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'ICClustering {len(expected)} {len(wrong)}')
    if wrong:
        print(json.dumps(wrong[:3], indent=2))
    raise SystemExit(0 if receipt['passed'] else 1)


if __name__ == '__main__':
    main()
