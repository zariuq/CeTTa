#!/usr/bin/env python3
"""Independent finite binding signature and contextual-template controls."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_archive import q, syntax
from check_ic_search import check_controls


def var(index):
    return "v", index


def op(index, *children):
    return "o", index, children


def meta(index, *children):
    return "m", index, children


def atom(value):
    return ["Wire:Atom", str(value)]


def pair(first, second):
    return ["Wire:Pair", first, second]


def args(values):
    result = atom(0)
    for value in reversed(values):
        result = pair(value, result)
    return result


def wire(term, schema=False):
    if term[0] == "v":
        return pair(atom(0), atom(term[1]))
    kind, index, children = term
    operator = pair(atom(int(kind == "m")), atom(index)) if schema else atom(index)
    return pair(atom(1), pair(operator, args([wire(child, schema) for child in children])))


def family_wire(sorts, operators, parameters):
    return ["Scoped:Family", sorts,
            [["Scoped:Operator", q(name), result,
              [["Scoped:Slot", binders, sort] for binders, sort in slots]]
             for name, result, slots in operators],
            [["Scoped:Parameter", q(name), role, dependencies, sort]
             for name, role, dependencies, sort in parameters]]


def fixture():
    sorts = ["T", "A", "B"]
    operators = [("zero", "T", []), ("one", "T", []),
                 ("pair", "T", [([], "T"), ([], "T")]),
                 ("lambda", "T", [(["T"], "T")]),
                 ("two", "T", [(["T", "T"], "T")]),
                 ("mixed", "T", [(["A", "B"], "B")]),
                 ("a", "A", []), ("b", "B", []),
                 ("pair-ab", "T", [([], "A"), ([], "B")])]
    parameters = [("P", "Code", ["T"], "T"), ("Q", "Value", ["T", "T"], "T"),
                  ("C", "Root", [], "T"), ("mixed", "Code", ["A", "B"], "B"),
                  ("other", "Value", ["T"], "T")]
    return sorts, operators, parameters


def reference(operators, parameters, ordinary, sort, pattern, target):
    def valid(term, context, expected, schema):
        if term[0] == "v":
            index = term[1]
            return isinstance(index, int) and 0 <= index < len(context) and context[index] == expected
        kind, index, children = term
        rows = parameters if kind == "m" and schema else operators if kind == "o" else []
        if not isinstance(index, int) or not 0 <= index < len(rows):
            return False
        row = rows[index]
        result, slots = (row[3], [([], s) for s in row[2]]) if kind == "m" else (row[1], row[2])
        return result == expected and len(children) == len(slots) and all(
            valid(child, binders + context, wanted, schema)
            for child, (binders, wanted) in zip(children, slots))

    if not valid(pattern, ordinary, sort, True):
        return ["IC:Invalid", "Template"]
    if not valid(target, [], sort, False):
        return ["IC:Invalid", "Source"]

    def transport(term, mapping, local=0):
        if term[0] == "v":
            index = term[1]
            if index < local:
                return term
            if index - local not in mapping:
                return None
            return var(local + mapping[index - local])
        kind, index, children = term
        result = [transport(child, mapping, local + len(slot[0]))
                  for child, slot in zip(children, operators[index][2])]
        return None if any(value is None for value in result) else op(index, *result)

    variables, metas = {}, {}

    def assign(values, index, body):
        if body is None:
            return False
        if index in values:
            return values[index] == body
        values[index] = body
        return True

    def spine(children, bound):
        indices = [child[1] for child in children if child[0] == "v" and child[1] < len(bound)]
        return indices if len(indices) == len(children) and len(set(indices)) == len(indices) else None

    def match(p, t, bound):
        if p[0] == "v":
            index = p[1]
            return p == t if index < len(bound) else assign(variables, index - len(bound), transport(t, {}))
        kind, index, children = p
        if kind == "m":
            positions = spine(children, bound)
            return positions is not None and assign(metas, index, transport(t, dict(zip(positions, range(len(positions))))))
        return t[0] == "o" and t[1] == index and all(
            match(a, b, slot[0] + bound) for a, b, slot in zip(children, t[2], operators[index][2]))

    def expand(p, bound):
        if p[0] == "v":
            return p if p[1] < len(bound) else variables[p[1] - len(bound)]
        kind, index, children = p
        if kind == "m":
            positions = spine(children, bound)
            return transport(metas[index], dict(enumerate(positions)))
        return op(index, *[expand(child, slot[0] + bound) for child, slot in zip(children, operators[index][2])])

    if not match(pattern, target, []):
        return "IC:None"
    reconstructed = expand(pattern, [])
    assert reconstructed == target
    captures = ["Scoped:Captures",
                [["IC:Binding", "Root", str(index), wire(body)] for index, body in reversed(list(variables.items()))],
                [["IC:Binding", parameters[index][1], str(index), wire(body)] for index, body in reversed(list(metas.items()))]]
    return ["ScopedObservation", captures, ["IC:Some", wire(reconstructed)]]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    arguments = ap.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_scoped_templates.metta").read_text())
    sorts, operators, parameters = fixture()
    family = family_wire(sorts, operators, parameters)
    family_call = syntax(family)
    program = ['!(import! &self (library lib_scoped_templates))', '''
(= (scoped-test $family $ordinary $sort $pattern $source)
   (case (scoped:match $family $ordinary $sort $pattern $source)
     (((IC:Some $captures)
       (ScopedObservation $captures (scoped:expand $family $ordinary $sort $captures $pattern)))
      ($other $other))))
''']
    expected = []

    def add(call, result):
        i = len(expected)
        program.append(f'!(println! (ScopedCase {i} {call}))')
        expected.append(["ScopedCase", str(i), result])

    cases = [
        ([], "T", op(0), op(0)),
        (["T"], "T", var(0), op(0)),
        (["T"], "T", op(2, var(0), var(0)), op(2, op(0), op(0))),
        (["T"], "T", op(2, var(0), var(0)), op(2, op(0), op(1))),
        ([], "T", op(3, var(0)), op(3, var(0))),
        ([], "T", op(4, meta(0, var(1))), op(4, var(1))),
        ([], "T", op(4, meta(1, var(1), var(0))), op(4, op(2, var(0), var(1)))),
        ([], "T", op(4, meta(0, var(1))), op(4, var(0))),
        ([], "T", op(3, meta(1, var(0), var(0))), op(3, var(0))),
        ([], "T", op(3, meta(0, op(0))), op(3, op(0))),
        (["T"], "T", op(3, var(1)), op(3, op(3, var(0)))),
        ([], "T", op(3, meta(2)), op(3, op(0))),
        ([], "T", op(3, meta(2)), op(3, var(0))),
        ([], "T", op(2, op(3, meta(0, var(0))), op(4, meta(0, var(1)))),
         op(2, op(3, var(0)), op(4, var(1)))),
        ([], "T", op(2, op(3, meta(0, var(0))), op(4, meta(0, var(1)))),
         op(2, op(3, var(0)), op(4, op(0)))),
        ([], "T", op(2, op(3, meta(0, var(0))), op(3, meta(4, var(0)))),
         op(2, op(3, op(0)), op(3, op(1)))),
        ([], "T", op(5, meta(3, var(0), var(1))), op(5, var(1))),
        ([], "T", op(5, meta(3, var(1), var(0))), op(5, var(1))),
        ([], "T", op(4, meta(0, var(1))), op(4, op(3, var(0)))),
        ([], "T", var(0), op(0)),
        ([], "T", op(0), var(0)),
        (["T"], "T", op(2, var(0), op(3, meta(0, var(0)))),
         op(2, op(0), op(3, var(0)))),
    ]
    for ordinary, sort, pattern, target in cases:
        expected_value = reference(operators, parameters, ordinary, sort, pattern, target)
        add(f'(scoped-test {family_call} {syntax(ordinary)} {sort} '
            f'{syntax(wire(pattern, True))} {syntax(wire(target))})', expected_value)

    bad_family = deepcopy(family)
    bad_family[2][0][2] = "missing"
    add(f'(scoped:match {syntax(bad_family)} () T {syntax(wire(op(0), True))} {syntax(wire(op(0)))})',
        ["IC:Invalid", "Signature"])
    bad_binder = deepcopy(family)
    bad_binder[2][3][3][0][1] = ["missing"]
    add(f'(scoped:family? {syntax(bad_binder)})', "false")
    add(f'(scoped:match {family_call} (missing) T {syntax(wire(op(0), True))} {syntax(wire(op(0)))})',
        ["IC:Invalid", "Context"])
    add(f'(scoped:match {family_call} () T (Wire:Pair (Wire:Atom 0) (Wire:Atom 0.0)) {syntax(wire(op(0)))})',
        ["IC:Invalid", "Template"])
    add(f'(scoped:match {family_call} () T {syntax(wire(op(0), True))} '
        '(Wire:Pair (Wire:Atom 1) (Wire:Pair (Wire:Atom 0.0) (Wire:Atom 0))))',
        ["IC:Invalid", "Source"])
    add(f'(scoped:match {family_call} () T {syntax(wire(op(0), True))} '
        f'{syntax(wire(op(0, op(0))))})', ["IC:Invalid", "Source"])
    for bad in (
        '(Wire:Pair (Wire:Atom 1.0) (Wire:Pair (Wire:Atom 0) (Wire:Atom 0)))',
        '(Wire:Pair (Wire:Atom 1) (Wire:Pair (Wire:Atom 0) (Wire:Atom 0.0)))',
    ):
        add(f'(scoped:match {family_call} () T {syntax(wire(op(0), True))} {bad})',
            ["IC:Invalid", "Source"])
    for call, value in (
        ('(scoped:variable 1)', wire(var(1))),
        ('(scoped:base 0 ())', wire(op(0))),
        ('(scoped:rigid 0 ())', wire(op(0), True)),
        ('(scoped:meta 0 ((scoped:variable 0)))', wire(meta(0, var(0)), True)),
    ):
        add(call, value)
    pattern = op(3, meta(0, var(0)))
    captures = ["Scoped:Captures", [], [["IC:Binding", "Code", "0", wire(var(0))]]]
    for change in ("duplicate", "role", "body", "index"):
        broken = deepcopy(captures)
        if change == "duplicate":
            broken[2].append(deepcopy(broken[2][0]))
        elif change == "role":
            broken[2][0][1] = "Value"
        elif change == "body":
            broken[2][0][3] = wire(op(7))
        else:
            broken[2][0][2] = "0.0"
        add(f'(scoped:expand {family_call} () T {syntax(broken)} {syntax(wire(pattern, True))})',
            ["IC:Invalid", "Captures"])
    add(f'(scoped:expand {family_call} () T (Scoped:Captures () ()) {syntax(wire(pattern, True))})', "IC:None")
    add(f'(scoped:expand {family_call} (T) T (Scoped:Captures () ()) {syntax(wire(op(0), True))})',
        ["IC:Some", wire(op(0))])

    program.append('''
(= (scoped-forest-test $family $ordinary $sorts $patterns $sources)
   (case (scoped:match-many $family $ordinary $sorts $patterns $sources)
     (((IC:Some $captures)
       (ScopedObservation $captures (scoped:expand-many $family $ordinary $sorts $captures $patterns)))
      ($other $other))))
''')
    patterns = [op(3, meta(0, var(0))), op(4, meta(0, var(1)))]
    pattern_wires = [wire(p, True) for p in patterns]
    for targets in ([op(3, var(0)), op(4, var(1))], [op(3, var(0)), op(4, op(0))]):
        observed = reference(operators, parameters, [], "T", op(2, *patterns), op(2, *targets))
        expected_forest = observed if observed == "IC:None" else [
            "ScopedObservation", observed[1], ["IC:Some", [wire(t) for t in targets]]]
        add(f'(scoped-forest-test {family_call} () (T T) {syntax(pattern_wires)} '
            f'{syntax([wire(t) for t in targets])})', expected_forest)
    add(f'(scoped-forest-test {family_call} () () () ())',
        ["ScopedObservation", ["Scoped:Captures", [], []], ["IC:Some", []]])
    add(f'(scoped:match-many {family_call} () (T T) {syntax(pattern_wires[:1])} '
        f'{syntax([wire(op(3, var(0))), wire(op(4, var(1)))])})', ["IC:Invalid", "Template"])
    add(f'(scoped:match-many {family_call} () (T T) {syntax(pattern_wires)} '
        f'{syntax([wire(op(3, var(0)))])})', ["IC:Invalid", "Source"])
    unused_family = family_wire(["T", "absent"], [("constant", "T", [])],
                                [("unused", "Code", [], "absent")])
    add(f'(scoped-test {syntax(unused_family)} (absent) T '
        f'{syntax(wire(op(0), True))} {syntax(wire(op(0)))})',
        ["ScopedObservation", ["Scoped:Captures", [], []], ["IC:Some", wire(op(0))]])

    # Read and assemble an actual authored module, then use the projected
    # binder table for matching and expansion, not just a label comparison.
    program += ['!(import! &self (library lib_scoped_module))',
                '!(import! &self (library lib_module))']
    module_sorts = [q("Name"), q("Expr")]
    module_operators = [("NameX", q("Name"), []), ("NameY", q("Name"), []),
                        ("Zero", q("Expr"), []), ("Ref", q("Expr"), [([], q("Name"))]),
                        ("Lam", q("Expr"), [([q("Name")], q("Expr"))]),
                        ("Pair", q("Expr"), [([], q("Expr")), ([], q("Expr"))])]
    module_parameters = [("P", "Code", [q("Name")], q("Expr"))]
    module_family = family_wire(module_sorts, module_operators, module_parameters)
    module_pattern = op(4, op(4, meta(0, var(1))))
    module_target = op(4, op(4, op(3, var(1))))
    module_result = reference(module_operators, module_parameters, [], q("Expr"),
                              module_pattern, module_target)
    add(f'''(let* (($reader (langdef:load-baked-parser "module-core-reader"))
                    ($document (module:read-file $reader "scoped-bindings" "tests/module_values/BindingValues.module"))
                    ($closed (langdef:close-parser $reader))
                    ($presentation (module:assemble $document "Syntax" LNil LNil OriginViewV1 "scoped-template"))
                    ($language (module:core $presentation "Bindings"))
                    ($projected (scoped:family-from-language $language {syntax(module_family[3])}))
                    ($authority (langdef:load-language-value $language))
                    ($admitted (langdef:admit-pattern-data $authority "Expr"
                      (PApp "Lam" (LCons (PLam BNone
                        (PApp "Lam" (LCons (PLam BNone
                          (PApp "Ref" (LCons (Var 1) LNil))) LNil))) LNil))))
                    ($released (langdef:close-language $authority)))
        (case $projected
          (((IC:Some $family)
            (ScopedModuleObservation (== $family {syntax(module_family)})
              (scoped-test $family () "Expr" {syntax(wire(module_pattern, True))} {syntax(wire(module_target))})
              $admitted))
           ($other $other))))''',
        ["ScopedModuleObservation", "true", module_result, ["LangDef:TermAdmitted"]])
    missing_binding = deepcopy(module_family)
    missing_binding[2][4][3][0][1] = []
    add(f'(scoped:match {syntax(missing_binding)} () "Expr" '
        f'{syntax(wire(module_pattern, True))} {syntax(wire(module_target))})', ["IC:Invalid", "Template"])
    add('(scoped:family-from-language '
        '(GSLTLanguageDefWireV1 "unsupported" (LCons (TypeDecl "Number" CarrierBuiltinInt) LNil) LNil LNil LNil) ())',
        ["Scoped:Refused", "UnsupportedCarrier"])
    add('(scoped:family-from-language '
        '(GSLTLanguageDefWireV1 "bad-slot" (LCons (TypeDecl "T" CarrierAst) LNil) '
        '(LCons (GrammarRule "F" "T" (LCons (TermSimple "x" (TArrow (TBase "T") (TBase "T"))) LNil) LNil EvalNone) LNil) '
        'LNil LNil) ())', ["Scoped:Refused", "UnsupportedParameter"])

    # Constructor names are metadata; an effectful host function carrying the
    # same name must not be invoked by these wire operations.
    effect_family = deepcopy(family)
    effect_family[2][0][1] = q("scoped-host-effect")
    program.append('(= (scoped-host-effect) (println! "SCOPED-MUST-NOT-RUN"))')
    add(f'(scoped-test {syntax(effect_family)} () T {syntax(wire(op(0), True))} {syntax(wire(op(0)))})',
        ["ScopedObservation", ["Scoped:Captures", [], []], ["IC:Some", wire(op(0))]])
    text = "\n".join(program) + "\n"
    check_controls(text)
    path = arguments.output / "scoped.metta"
    path.write_text(text)
    run = subprocess.run([str(arguments.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=60)
    (arguments.output / "scoped.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "ScopedCase"]
    indexed = {f[1]: f for f in actual}
    differences = [{"case": i, "expected": want, "actual": indexed.get(str(i))}
                   for i, want in enumerate(expected) if indexed.get(str(i)) != want]
    inert = all(line.strip() not in ("SCOPED-MUST-NOT-RUN", q("SCOPED-MUST-NOT-RUN"))
                for line in run.stdout.splitlines())
    receipt = {"passed": run.returncode == 0 and not differences and len(actual) == len(expected) and inert,
               "checks": len(expected), "answers": len(actual), "mismatches": differences,
               "operator_execution_absent": inert,
               "module_fixture": "tests/module_values/BindingValues.module",
               "scope": "finite binding-wire instance of scoped matching and partial reconstruction; not general higher-order unification"}
    (arguments.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [{"case": row["case"]} for row in differences]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
