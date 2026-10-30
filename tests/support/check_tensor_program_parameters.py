#!/usr/bin/env python3
"""Independent finite parameter DAG fitting, shared slots and original-task checks."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
import math
import os
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_binary_value import Text, construct, encoded
from check_ic_numeric import archive as vector_archive, ctn
from check_ic_numeric_arrays import archive
from check_ic_search import check_controls
from check_tensor_program import FAMILY, array, canonical, records, source_array
from check_tensor_sequences import coordinates, normalized, scan


def projection(rows):
    used = sorted({row[1] for row in rows if row[0] == "op"})
    family = ["TP:Family", Text("TF1"), [FAMILY[2][index] for index in used]]
    return family, {code: index for index, code in enumerate(used)}


def projected(rows, indices):
    result = deepcopy(rows)
    for row in result:
        if row[0] == "op":
            row[1] = indices[row[1]]
    return result


def instantiate(rows, assignment):
    return [["constant", assignment[row[1]]] if row[0] == "parameter" else row
            for row in rows]


def evaluate(rows, inputs):
    """Recursive reference: no native template, worklist or memo table."""
    def visit(index):
        row = rows[index]
        if row[0] == "input":
            return inputs[row[1]]
        if row[0] == "constant":
            return row[1]
        operation, parameters, children = row[1:]
        values = [visit(child) for child in children]
        if operation in (15, 16, 17):
            wanted = {15: 'F64Tensor', 16: 'BoolTensor', 17: 'I64Tensor'}[operation]
            assert values[0][0] == 'BoolTensor' and values[1][0] == values[2][0] == wanted
            assert values[0][1] == values[1][1] == values[2][1]
            return array(values[0][1], {point: values[1 if flag else 2][2][point]
                                      for point, flag in values[0][2].items()
                                      if point in values[1 if flag else 2][2]}, values[1][0])
        if operation == 6:
            result = scan(values[0][1], values[0][2], parameters[0], 'f64')
            assert all(math.isfinite(value) for value in result.values())
            return array(values[0][1], result)
        if operation == 13:
            assert values[0][1] == ()
            return array(values[1][1], [values[0][2][()] for _ in coordinates(values[1][1])])
        if operation == 12:
            flat = [values[0][2][point] for point in coordinates(values[0][1])]
            return array(values[1][1], [flat[values[1][2][point]] for point in coordinates(values[1][1])])
        if operation == 10:
            assert parameters == ["sequential"]
            shape, vector_shape = values[0][1], values[1][1]
            assert len(shape) == 2 and vector_shape == (shape[1],)
            out = []
            for row_index in range(shape[0]):
                total = 0.0
                for column in range(shape[1]):
                    total += values[0][2][row_index, column]*values[1][2][column,]
                out.append(total)
            return array((shape[0],), out)
        shape = values[0][1]
        assert shape == values[1][1]
        function = [lambda x, y: x+y, lambda x, y: x-y, lambda x, y: x*y,
                    lambda x, y: x/y, lambda x, y: x<y, lambda x, y: x>y][operation]
        out = [function(values[0][2][point], values[1][2][point]) for point in coordinates(shape)]
        assert all(type(value) is bool or math.isfinite(value) for value in out)
        return array(shape, out, "BoolTensor" if operation in (4, 5) else "F64Tensor")
    return visit(len(rows)-1)


def model(rows, family, indices):
    roots, compact = canonical([len(rows)-1], records(projected(rows, indices)))
    return encoded(["TP:Program", Text("TP1"), family, ["Dag:Forest", roots, compact]])


def reference(inputs, target, mask, rows, domains, *, vector=False):
    family, indices = projection(rows)
    if vector:
        original = vector_archive(list(inputs[0][2].values()), list(target[2].values()),
                                  list(mask[2].values()), 0.0)
    else:
        original = archive(inputs, target, mask, 0.0 if target[0] == "F64Tensor" else None)
    best, seen, accepted, winning = original, 0, 0, None
    for assignment in itertools.product(*(range(len(values)) for _name, _sort, values in domains)):
        selected = {name: values[index] for (name, _sort, values), index in zip(domains, assignment)}
        concrete = instantiate(rows, selected)
        seen += 1
        try:
            result = evaluate(concrete, inputs)
        except (AssertionError, IndexError, KeyError, ZeroDivisionError, OverflowError):
            continue
        valid = result[:2] == target[:2] and all(
            not mask[2][point] or result[2][point] == target[2][point]
            for point in coordinates(target[1]))
        if not valid:
            continue
        program = model(concrete, family, indices)
        if vector:
            candidate = encoded(["Numeric:Archive", Text("NC1"), ctn(list(inputs[0][2].values())),
                                 ctn(list(mask[2].values()), "u8"), ctn([0.0]), ["Numeric:Program", program]])
        else:
            candidate = archive(inputs, target, mask, 0.0 if target[0] == "F64Tensor" else None, program)
        if len(candidate) < len(best):
            best, accepted, winning = candidate, accepted+1, concrete
    return original, best, seen, accepted, winning


def region(rows, domains):
    family, indices = projection(rows)
    template = [["TP:Parameter", Text(row[1])] if row[0] == "parameter"
                else records(projected([row], indices))[0] for row in rows]
    bindings, declarations = [], []
    for ordinal, (name, sort, values) in enumerate(domains):
        names = []
        for position, value in enumerate(values):
            variable = f"$parameter{ordinal}choice{position}"
            bindings.append(f"({variable} {source_array(value)})")
            names.append(variable)
        declarations.append(f'(TP:ParameterDomain {json.dumps(name)} {json.dumps(sort)} ({" ".join(names)}))')
    expression = (f'(tensor-program:prepare-parameters {construct(family)} '
                  f'{construct(["Dag:Forest", [len(rows)-1], template])} ({" ".join(declarations)}))')
    return f'(let* ({" ".join(bindings)}) {expression})' if bindings else expression


HELPERS = r'''
!(import! &self (library lib_tensor_program_parameters))
(= (parameter-test:view $expected $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let $closed (ic:release $state)
         (ParameterFit (== $best $expected) $seen $accepted $faults $reason $first))) ($other $other))))
(= (parameter-test:pieces $expected (IC:PreparedSearch $state))
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $statistics BudgetExhausted $fault $next)
       (ParameterResume BudgetExhausted (parameter-test:view $expected (ic:resume $next 4096))))
      ($other $other))))
(= (parameter-test:cancel $expected (IC:PreparedSearch $state))
   (case (ic:cancel $state)
     (((IC:SearchResult $best $statistics Cancelled $fault $next)
       (ParameterCancelled Cancelled (parameter-test:view $expected (ic:resume $next 4096)))))))
(= (parameter-test:predict $artifact $inputs $expected)
   (case (ic:predict Numeric:ParameterizedPrograms $artifact $inputs)
     (((IC:Prediction $actual) (== $actual $expected)) ($other $other))))
(= (parameter-test:types $program $inputs)
   (case (tensor-program:run $program $inputs)
     (((TP:Outputs $results)
       (map-atom $results $item
         (case $item
           (((TP:Value $sort $tensor) ($sort (tensor:shape $tensor))) ($other $other))))) ($other $other))))
(= (parameter-test:changed-model (IC:PreparedSearch $state))
   (case $state
     (((IC:SearchState (IC:Task (TP:ParameterModel $domain $region) $original $observation $guards $quantum)
         $queue $best $statistics $fault)
       (let* (($result (ic:resume
          (IC:SearchState (IC:Task (TP:ParameterModel $domain BadRegion) $original $observation $guards $quantum)
            $queue $best $statistics $fault) 0)) ($closed (ic:release $state))) $result)))))
(= (parameter-test:changed-source (IC:PreparedSearch $state))
   (let* (($changed (add-atom &self ParameterDependencyChanged))
          ($result (ic:resume $state 0)) ($closed (ic:release $state))) $result))
(= (parameter-test:program $region $assignment $expected)
   (case $region
     (((TP:ParameterRegion $family (Dag:Forest $roots $template) $rows)
       (case (tp-parameters:select $rows $assignment)
         (((TP:Some $values)
           (case (tp-parameters:instantiate $values $template)
             (((TP:Some $concrete)
               (case (tensor-program:pack $family (Dag:Forest $roots $concrete))
                 (((TP:Artifact $bytes) (== $bytes $expected)) ($other $other)))) ($other $other)))) ($other $other)))))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--library-source", type=Path,
                        help="explicit alternate complete library for original/learned source qualification")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT/"lib/petta/lib_tensor_program_parameters.metta").read_text())
    helpers = HELPERS
    if args.library_source:
        module = os.path.relpath(
            args.library_source.resolve(strict=True), args.output.resolve())
        helpers = helpers.replace(
            '(library lib_tensor_program_parameters)', json.dumps(module), 1)
        check_controls(args.library_source.read_text())
    code, expected, names, fits = [helpers], [], [], []

    def add(name, call, wanted="true"):
        index = len(expected)
        code.append(f'!(println! (ParameterCase {index} {call}))')
        expected.append(["ParameterCase", str(index), wanted])
        names.append(name)

    scalar = lambda value: array((), [float(value)])
    n = 768
    x = array((n,), [float(i % 17 - 8) for i in range(n)])
    z = array((n,), [float(i % 13 - 6) for i in range(n)])
    active = array((n,), [True]*n, "BoolTensor")
    rows = [["input", 0, "F64Tensor"], ["input", 1, "F64Tensor"],
            ["parameter", "a"], ["parameter", "b"], ["parameter", "c"],
            ["op", 13, [], [2, 0]], ["op", 2, [], [0, 5]],
            ["op", 13, [], [3, 1]], ["op", 2, [], [1, 7]], ["op", 0, [], [6, 8]],
            ["op", 13, [], [4, 0]], ["op", 0, [], [9, 10]]]
    domains = [("a", "F64Tensor", [scalar(-2), scalar(2)]),
               ("b", "F64Tensor", [scalar(-1.5), scalar(1.5)]),
               ("c", "F64Tensor", [scalar(0), scalar(3)])]
    target = evaluate(instantiate(rows, {"a": scalar(2), "b": scalar(-1.5), "c": scalar(3)}), [x, z])
    fixtures = [("three-coefficient-regression", [x, z], target, active, rows, domains, False)]
    shared = [["input", 0, "F64Tensor"], ["input", 1, "F64Tensor"],
              ["parameter", "a"], ["parameter", "a"],
              ["op", 13, [], [2, 0]], ["op", 13, [], [3, 1]],
              ["op", 2, [], [0, 4]], ["op", 2, [], [1, 5]], ["op", 0, [], [6, 7]]]
    shared_target = evaluate(instantiate(shared, {"a": scalar(2)}), [x, z])
    fixtures.append(("shared-parameter", [x, z], shared_target, active, shared, domains[:1], False))
    vector_rows = [["input", 0, "F64Tensor"], ["parameter", "a"], ["parameter", "b"],
                   ["op", 13, [], [1, 0]], ["op", 2, [], [0, 3]],
                   ["op", 13, [], [2, 0]], ["op", 0, [], [4, 5]]]
    vector_domains = [("a", "F64Tensor", [scalar(1), scalar(2)]),
                      ("b", "F64Tensor", [scalar(0), scalar(3)])]
    vector_target = evaluate(instantiate(vector_rows, {"a": scalar(2), "b": scalar(3)}), [x])
    fixtures.append(("nc1-affine", [x], vector_target, active, vector_rows, vector_domains, True))
    fixtures.append(("later-finite-answer-after-overflow", [x], vector_target, active, vector_rows,
                     [("a", "F64Tensor", [scalar(1.0e308), scalar(2)]),
                      ("b", "F64Tensor", [scalar(3)])], False))
    matrix = array((n, 2), [value for pair in zip(x[2].values(), z[2].values()) for value in pair])
    matrix_rows = [["input", 0, "F64Tensor"], ["parameter", "weights"], ["parameter", "intercept"],
                   ["op", 10, ["sequential"], [0, 1]], ["op", 13, [], [2, 3]], ["op", 0, [], [3, 4]]]
    matrix_domains = [("weights", "F64Tensor", [array((3,), [0., 0., 0.]), array((2,), [2., -1.])]),
                      ("intercept", "F64Tensor", [scalar(0), scalar(3)])]
    matrix_target = evaluate(instantiate(matrix_rows, {"weights": array((2,), [2., -1.]), "intercept": scalar(3)}), [matrix])
    fixtures.append(("matrix-regression-after-invalid-shape", [matrix], matrix_target, active,
                     matrix_rows, matrix_domains, False))
    class_x = array((3072,), [0., 10.] + [float(i % 41 - 20) for i in range(3070)])
    class_rows = [["input", 0, "F64Tensor"], ["parameter", "position"], ["parameter", "offset"],
                  ["op", 12, [], [0, 1]], ["op", 0, [], [3, 2]],
                  ["op", 13, [], [4, 0]], ["op", 5, [], [0, 5]]]
    class_domains = [("position", "I64Tensor", [array((), [0], "I64Tensor"), array((), [1], "I64Tensor")]),
                     ("offset", "F64Tensor", [scalar(-1.5), scalar(0.5)])]
    class_target = evaluate(instantiate(class_rows, {"position": array((), [1], "I64Tensor"), "offset": scalar(.5)}), [class_x])
    fixtures.append(("mixed-integer-float-classifier", [class_x], class_target,
                     array((3072,), [True]*3072, "BoolTensor"), class_rows, class_domains, False))
    duplicate_domains = [("a", "F64Tensor", [scalar(2), scalar(2)])]
    fixtures.append(("duplicate-alternatives", [x, z], shared_target, active, shared, duplicate_domains, False))
    altered = list(shared_target[2].values())
    altered[-1] = 123456.0
    fixtures.append(("masked-original-observation", [x, z], array((n,), altered),
                     array((n,), [True]*(n-1)+[False], "BoolTensor"), shared, domains[:1], False))
    small = array((3,), [1., 2., 3.])
    small_target = evaluate(instantiate(vector_rows, {"a": scalar(2), "b": scalar(3)}), [small])
    fixtures.append(("exact-but-larger-model", [small], small_target, array((3,), [True]*3, "BoolTensor"),
                     vector_rows, vector_domains, False))
    fixtures.append(("empty-domain", [x], vector_target, active, vector_rows,
                     [("a", "F64Tensor", []), vector_domains[1]], False))
    empty = array((0,), [])
    fixtures.append(("empty-shape", [empty], empty, array((0,), [], "BoolTensor"),
                     vector_rows, vector_domains, False))
    fixtures.append(("no-domains", [x], x, active, [["input", 0, "F64Tensor"]], [], False))
    prepared = None
    for label, inputs, output, mask, template, choices, vector_mode in fixtures:
        original, best, seen, accepted, winning = reference(inputs, output, mask, template, choices, vector=vector_mode)
        if label not in ("exact-but-larger-model", "empty-domain", "empty-shape"):
            assert accepted == 1, (label, seen, accepted, len(original), len(best))
        declared = region(template, choices)
        add(label+"/region", f'(tensor-program:parameter-region? {declared})')
        desired = ["ParameterFit", "true", str(seen), str(accepted), "0", "RegionExhausted", "IC:NoFault"]
        fitted = f'(ic:fit Numeric:ParameterizedPrograms {byte_tensor(original)} {declared} () 4096)'
        add(label+"/fit", f'(parameter-test:view {byte_tensor(best)} {fitted})', desired)
        fits.append({"name": label, "assignments": seen, "accepted": accepted,
                     "original_bytes": len(original), "best_bytes": len(best)})
        if label == "three-coefficient-regression":
            prepared = f'(ic:prepare-fit Numeric:ParameterizedPrograms {byte_tensor(original)} {declared} ())'
            add("partial-resume", f'(parameter-test:pieces {byte_tensor(best)} {prepared})',
                ["ParameterResume", "BudgetExhausted", desired])
            add("cancel-resume", f'(parameter-test:cancel {byte_tensor(best)} {prepared})',
                ["ParameterCancelled", "Cancelled", desired])
            add("changed-model", f'(parameter-test:changed-model {prepared})', ["IC:ResumeRejected", "InvalidArtifact"])
            add("invalid-source", f'(ic:prepare-fit Numeric:ParameterizedPrograms {byte_tensor(original)} {declared} (NotASpace))',
                ["IC:SearchRefused", "InvalidSources"])
            tape = [["IC:Proposal", ["TP:ParameterAssignment", list(map(str, choice))]]
                    for choice in itertools.product(range(2), repeat=3)]
            add("fifo-ordered-assignment-tape", f'''(case {declared}
              (((TP:ParameterRegion $family $forest $rows)
                (let* (($indices (tp-parameters:choice-indices $rows))
                       ($handle (work-new (tp-parameters:produce $indices)))
                       ($route (work-route $handle fifo)) ($quantum (work-budget $handle 1000000))
                       ($batch (work-take $handle 64)) ($closed (work-cancel $handle)))
                  ($route $batch)))))''', [[], ["taken", tape, "complete"]])
        if winning is not None:
            if label.startswith("matrix"):
                query = [array((3, 2), [1., 2., 0., -1., -2., 3.])]
            elif label.startswith("mixed"):
                query = [array((5,), [0., 10., 10.5, 11., 20.])]
            else:
                query = [array((2, 2), [-2., 0., 2., 7.]) for _ in inputs]
                if len(query) == 2:
                    query[1] = array((2, 2), [3., -1., 0., 2.])
                if vector_mode:
                    query = [array((3,), [-2., 0., 3.])]
            prediction = evaluate(winning, query)
            source_inputs = source_array(query[0]) if vector_mode else '('+' '.join(map(source_array, query))+')'
            add(label+"/new-input", f'(parameter-test:predict {byte_tensor(best)} {source_inputs} {source_array(prediction)})')
            if label.startswith("mixed"):
                family, indices = projection(template)
                model_bytes = model(winning, family, indices)
                add("mixed-exact-tp1", f'(parameter-test:program {declared} (1 1) {byte_tensor(model_bytes)})')
                add("mixed-forward-original", f'''(case (tensor-program:run {byte_tensor(model_bytes)} ({source_array(inputs[0])}))
                    (((TP:Outputs ((TP:Value "BoolTensor" $result))) (== $result {source_array(output)})) ($other $other)))''')
                roots, compact = canonical(list(range(len(winning))), records(projected(winning, indices)))
                prefix = encoded(["TP:Program", Text("TP1"), family, ["Dag:Forest", roots, compact]])
                add("mixed-forward-prefixes", f'(parameter-test:types {byte_tensor(prefix)} ({source_array(inputs[0])}))',
                    [[json.dumps(sort), list(map(str, shape))] for sort, shape in
                     [("F64Tensor", (3072,)), ("I64Tensor", ()), ("F64Tensor", ()),
                      ("F64Tensor", ()), ("F64Tensor", ()), ("F64Tensor", (3072,)), ("BoolTensor", (3072,))]])
        if label == "shared-parameter":
            family, indices = projection(template)
            concrete = instantiate(template, {"a": scalar(2)})
            add("shared-slot-complete-tp1", f'(parameter-test:program {declared} (1) {byte_tensor(model(concrete, family, indices))})')
            add("ordered-assignment-occurrences", f'''(case {declared}
               (((TP:ParameterRegion $family $forest $rows)
                 (collapse (tp-parameters:produce (tp-parameters:choice-indices $rows))))))''',
                [["IC:Proposal", ["TP:ParameterAssignment", ["0"]]],
                 ["IC:Proposal", ["TP:ParameterAssignment", ["1"]]]])
            for assignment in ("()", "(2)", "(-1)", "(1.0)", "(NoIndex)", "(1 0)", "NotAList"):
                add("bad-assignment/"+assignment, f'''(case {declared}
                  (((TP:ParameterRegion $family $forest $rows)
                    (ic-search:materialize (TP:ParameterModel IC:ArrayArchive
                      (TP:ParameterRegion $family $forest $rows)) NoOriginal (TP:ParameterAssignment {assignment})))))''',
                    ["IC:ProducerFault", "ParameterAssignment"])
    # Public admission rejects unknown, duplicated and unused names, bad sorts
    # and forward references; empty domains remain a legitimate empty region.
    invalid = [
        (shared, [("b", "F64Tensor", [scalar(2)])]),
        (shared, domains[:1]+domains[:1]),
        (shared, domains[:1]+[("unused", "F64Tensor", [scalar(2)])]),
        (shared, [("a", "I64Tensor", [scalar(2)])]),
        (shared, [("a", "BoolTensor", [array((), [True], "BoolTensor")])]),
        ([["op", 0, [], [0, 0]]], []),
    ]
    for index, (template, choices) in enumerate(invalid):
        add(f"invalid-template/{index}", f'(tensor-program:parameter-region? {region(template, choices)})', "false")
    for children in ('NotReferences', '(-1)', '(0.0)', '(0 $free)'):
        tag = byte_tensor(encoded(['TP:Apply', 0, []]))
        family = construct(["TP:Family", Text("TF1"), [FAMILY[2][0]]])
        add("malformed-child-indices/"+children,
            f'(tensor-program:prepare-parameters {family} (Dag:Forest (0) ((Binary:Node {tag} {children}))) ())',
            ['TP:Fault', 'ParameterTemplate'])
    for bad_values in ('NotValues', '(NotATensor)', '((tensor:from-values i64 () (2)))'):
        add("malformed-domain-values/"+bad_values,
            f'''(tensor-program:prepare-parameters (TP:Family "TF1" ())
              (Dag:Forest (0) ((TP:Parameter "x"))) ((TP:ParameterDomain "x" "F64Tensor" {bad_values})))''',
            ['TP:Fault', 'ParameterDomains'])
    add("non-region", '(ic:prepare-fit Numeric:ParameterizedPrograms NotAnArchive BadRegion ())',
        ["IC:FitRefused", "ParameterRegion"])
    wrong_sort = region([["parameter", "flag"]], [("flag", "BoolTensor", [array((), [True], "BoolTensor")])])
    raw_float = vector_archive([0., 1.], [0., 1.], [True, True], 0.0)
    add("task-root-sort-mismatch", f'(ic:prepare-fit Numeric:ParameterizedPrograms {byte_tensor(raw_float)} {wrong_sort} ())',
        ["IC:FitRefused", "Task"])
    assert prepared is not None
    add("changed-source", f'(parameter-test:changed-source {prepared})', ["IC:ResumeRejected", "ChangedDependencies"])
    source = '\n'.join(code)+'\n'
    check_controls(source)
    script = args.output/"parameter-programs.metta"
    script.write_text(source)
    run = subprocess.run([str(args.cetta.resolve()), '--lang', 'petta', str(script.resolve())],
                         cwd=ROOT, capture_output=True, text=True, timeout=180)
    (args.output/"stdout.log").write_text(run.stdout)
    (args.output/"stderr.log").write_text(run.stderr)
    actual = [form for form in parse_forms(run.stdout) if isinstance(form, list) and form and form[0] == 'ParameterCase']
    mismatches = [{'case': index, 'name': names[index], 'wanted': wanted, 'actual': got}
                  for index, (wanted, got) in enumerate(itertools.zip_longest(expected, actual))
                  if normalized(wanted) != normalized(got)]
    receipt = {'passed': run.returncode == 0 and not mismatches, 'checks': len(expected),
               'observations': len(actual), 'returncode': run.returncode, 'mismatches': mismatches, 'fits': fits,
               'scope': 'bounded shared typed parameter DAGs, full NC1/NC2 cost, original masks, resumed fitting and new-input prediction'}
    (args.output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps({**receipt, 'mismatches': [{'case': row['case'], 'name': row['name']} for row in mismatches]}))
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
