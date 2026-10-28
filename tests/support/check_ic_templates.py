#!/usr/bin/env python3
"""Independent ranked-template matching and typed admission controls."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import subprocess

from check_incremental_compression import ROOT, parse_forms, sexpr


def node(index, *children):
    return ["IC:Node", str(index), list(children)]


def hole(role, index):
    return ["IC:Hole", role, str(index)]


def natural(value):
    return isinstance(value, str) and value.isascii() and value.isdecimal() and int(value) < 2**63


def slots_valid(slots):
    return isinstance(slots, list) and all(
        isinstance(slot, list) and len(slot) == 3 and slot[0] == "IC:Slot"
        and slot[1] in ("Root", "Code", "Value") and isinstance(slot[2], str)
        for slot in slots)


def reference(signature, slots, patterns, sources):
    if not isinstance(signature, list) or not slots_valid(slots) or not all(
        isinstance(row, list) and len(row) == 3 and row[0] == "IC:Operator"
        and isinstance(row[1], str) and slots_valid(row[2]) for row in signature
    ):
        return ["IC:Invalid", "Signature"]

    def admit(slot, term, holes):
        role, sort = slot[1:]
        if not isinstance(term, list) or len(term) != 3:
            return False
        if holes is not None and term[0] == "IC:Hole":
            if term[1] != role or not natural(term[2]):
                return False
            key = term[1], int(term[2])
            if key in holes and holes[key] != sort:
                return False
            holes[key] = sort
            return True
        if term[0] != "IC:Node" or not natural(term[1]) or int(term[1]) >= len(signature):
            return False
        row = signature[int(term[1])]
        return row[1] == sort and admit_many(row[2], term[2], holes)

    def admit_many(expected, terms, holes):
        return isinstance(terms, list) and len(expected) == len(terms) and all(
            admit(slot, term, holes) for slot, term in zip(expected, terms))

    if not admit_many(slots, patterns, {}):
        return ["IC:Invalid", "Template"]
    if not admit_many(slots, sources, None):
        return ["IC:Invalid", "Source"]
    environment = {}

    def match(pattern, source):
        if pattern[0] == "IC:Hole":
            key = pattern[1], int(pattern[2])
            if key in environment:
                return environment[key] == source
            environment[key] = source
            return True
        return pattern[1] == source[1] and len(pattern[2]) == len(source[2]) and all(
            match(p, s) for p, s in zip(pattern[2], source[2]))

    if not all(match(p, s) for p, s in zip(patterns, sources)):
        return "IC:None"
    bindings = [["IC:Binding", role, str(index), value]
                for (role, index), value in reversed(list(environment.items()))]
    return ["TemplateObservation", bindings, ["IC:Some", sources]]


def generalized(signature, slots, lefts, rights):
    occurrences = []

    def seed(role, left, right):
        if left[1] == right[1]:
            return node(left[1], *[seed(slot[1], a, b)
                                  for slot, a, b in zip(signature[int(left[1])][2], left[2], right[2])])
        key = role, left, right
        index = occurrences.index(key) if key in occurrences else len(occurrences)
        occurrences.append(key)
        return hole(role, index)

    patterns = [seed(slot[1], a, b) for slot, a, b in zip(slots, lefts, rights)]
    ls = reference(signature, slots, patterns, lefts)
    rs = reference(signature, slots, patterns, rights)
    assert ls[0] == rs[0] == "TemplateObservation"
    return ["GeneralizationObservation", patterns, ls[1], rs[1], ["IC:Some", lefts], ["IC:Some", rights]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    v = lambda sort: ["IC:Slot", "Value", sort]
    c = lambda sort: ["IC:Slot", "Code", sort]
    r = lambda sort: ["IC:Slot", "Root", sort]
    signature = [["IC:Operator", "Term", []], ["IC:Operator", "Term", []],
                 ["IC:Operator", "Term", [v("Term"), v("Term")]],
                 ["IC:Operator", "Term", [c("Term"), v("Term")]],
                 ["IC:Operator", "Colour", []],
                 ["IC:Operator", "Pair", [v("Term"), v("Colour")]]]
    h = hole("Value", 0)
    duplicate = node(2, h, h)
    cases = [
        (signature, [r("Term")], [duplicate], [node(2, node(0), node(0))]),
        (signature, [r("Term")], [duplicate], [node(2, node(0), node(1))]),
        (signature, [r("Term")], [node(3, hole("Code", 0), h)], [node(3, node(0), node(1))]),
        (signature, [r("Term")], [node(3, h, h)], [node(3, node(0), node(1))]),
        # One environment across a rewrite's two sides, not two unrelated matches.
        (signature, [r("Term"), r("Term")], [hole("Root", 0), hole("Root", 0)], [node(0), node(1)]),
        (signature, [r("Term"), r("Term")], [hole("Root", 0), hole("Root", 0)], [node(0), node(0)]),
        # Same hole at incompatible sorts is rejected before source matching.
        (signature, [r("Pair")], [node(5, h, h)], [node(5, node(0), node(4))]),
        (signature, [r("Term")], [duplicate], [node(2, node(0), node(4))]),
        (signature, [r("Term")], [node(2, h)], [node(2, node(0), node(0))]),
        (signature, [r("Term")], [duplicate], [node(2, node(0))]),
        (signature, [r("Term")], [node(99)], [node(0)]),
        (signature, [r("Term")], [node(-1)], [node(0)]),
        (signature, [r("Term")], [node("1.5")], [node(0)]),
        (signature, [r("Term")], [node("1.0")], [node(0)]),
        (signature, [r("Term")], [hole("Root", -1)], [node(0)]),
        (signature, [r("Term")], [hole("Root", "1.5")], [node(0)]),
        (signature, [r("Term")], [hole("Root", 0)], [["IC:Var", "0"]]),
        (signature, [r("Term")], [hole("Root", 0)], [["IC:Node", "0", [], "extra"]]),
        (signature, [r("Term")], [node(0)], [node(1)]),
        (signature, [], [], []),
        (signature, [r("Term")], "not-a-forest", []),
        ([["IC:Operator", "Term", [["IC:Slot", "Unknown", "Term"]]]], [], [], []),
    ]
    rng = random.Random(2701)

    def tree(depth):
        if not depth or rng.randrange(3) == 0:
            return node(rng.randrange(2))
        return node(2, tree(depth - 1), tree(depth - 1))

    # Hold out whole target trees, not just renamed leaves. Some share a hole
    # accidentally; others deliberately disagree at its later occurrence.
    for _ in range(40):
        left, right = tree(3), tree(3)
        target = node(2, left, left if rng.randrange(2) else right)
        cases.append((signature, [r("Term")], [duplicate], [target]))
    code = ['!(import! &self (library lib_ic_templates))', '''
(= (template-test $signature $slots $patterns $sources)
   (case (ic-template:match $signature $slots $patterns $sources)
     (((IC:Some $environment)
       (TemplateObservation $environment (ic-template:expand-list $environment $patterns)))
      ($other $other))))
(= (generalization-test $signature $slots $lefts $rights)
   (case (ic-template:generalize $signature $slots $lefts $rights)
     (((IC:Generalized $patterns $ls $rs)
       (GeneralizationObservation $patterns $ls $rs
         (ic-template:expand-list $ls $patterns) (ic-template:expand-list $rs $patterns)))
      ($other $other))))
''']
    expected = []
    for i, case in enumerate(cases):
        expected.append(["TemplateCase", str(i), reference(*case)])
        code.append(f'!(println! (TemplateCase {i} {sexpr(["template-test", *case])}))')
    pairs = [([r("Term")], [node(2, node(0), node(0))], [node(2, node(1), node(1))]),
             ([r("Term")], [node(2, node(2, node(0), node(0)), node(1))],
                            [node(2, node(2, node(1), node(1)), node(0))]),
             ([r("Term")], [node(3, node(0), node(0))], [node(3, node(1), node(1))]),
             ([r("Term"), r("Term")], [node(0), node(0)], [node(1), node(1)])]
    pairs += [([r("Term")], [tree(3)], [tree(3)]) for _ in range(12)]
    for slots, lefts, rights in pairs:
        for a, b in ((lefts, rights), (rights, lefts)):
            i = len(expected)
            expected.append(["TemplateCase", str(i), generalized(signature, slots, a, b)])
            code.append(f'!(println! (TemplateCase {i} '
                        f'{sexpr(["generalization-test", signature, slots, a, b])}))')
    fixture = args.output / "templates.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "templates.log").write_text(result.stdout + result.stderr)
    actual = [x for x in parse_forms(result.stdout)
              if isinstance(x, list) and x and x[0] == "TemplateCase"]
    different = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not different,
               "checks": len(expected), "answers": len(actual), "mismatches": different,
               "scope": "ranked first-order schemas, shared substitutions, typed admission and pairwise seeds; not binder matching or complete learner discovery"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in different[:5]:
            print("expected", sexpr(expected[i])[:1000])
            print("actual  ", sexpr(actual[i])[:1000])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
