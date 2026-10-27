#!/usr/bin/env python3
"""Bounded typed grammar, repeated parameters and checked discovery controls."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_ic_archive import ROOT, encode, q, source, syntax
from check_ic_search import check_controls
from check_ic_templates import generalized, hole, node, parse_forms, reference


def enumerate_templates(signature, slots, depth):
    """Independent state-threaded finite grammar, not parsed from the library."""
    def many(wanted, height, position, bindings):
        if not wanted:
            yield [], position, bindings
            return
        for head, middle, assigned in term(wanted[0], height, position, bindings):
            for rest, end, complete in many(wanted[1:], height, middle, assigned):
                yield [head] + rest, end, complete

    def term(slot, height, position, bindings):
        _, role, sort = slot
        yield hole(role, position), position + 1, [(role, sort, position)] + bindings
        for old_role, old_sort, index in bindings:
            if role == old_role and sort == old_sort:
                yield hole(role, index), position + 1, bindings
        if height:
            for index, (_, result, inputs) in enumerate(signature):
                if result == sort:
                    for children, end, assigned in many(inputs, height - 1, position, bindings):
                        yield node(index, *children), end, assigned

    return [forest for forest, _, _ in many(slots, depth, 0, [])]


def factor(archive, slots, patterns):
    candidate = deepcopy(archive)
    index = len(candidate[6])
    candidate[6].append(["IC:Definition", q("common"), slots, patterns])
    count = 0
    packets = source(archive)
    for i, (_, actual, values) in enumerate(packets):
        if actual != slots:
            continue
        observed = reference(archive[3], slots, patterns, values)
        if isinstance(observed, list) and observed[0] == "TemplateObservation":
            count += 1
            candidate[7][i] = ["IC:Call", str(index), observed[1]]
    assert source(candidate) == packets
    return candidate, count


def three_programs():
    slots = [["IC:Slot", "Root", "Root"]]
    signature = [["IC:Operator", "Root", [["IC:Slot", "Value", s] for s in ("A", "B", "C")]]]
    signature += [["IC:Operator", s, []] for s in ("A", "A", "B", "B", "C", "C")]
    signature.append(["IC:Operator", "Root", [["IC:Slot", "Value", "Root"]]])
    rows = [node(0, node(1), node(3), node(5)), node(0, node(1), node(4), node(6)),
            node(0, node(2), node(3), node(6))]

    def wrap(term):
        for _ in range(6):
            term = node(7, term)
        return term

    values = [[wrap(row)] for row in rows]
    archive = ["IC:Archive", q("IC1"), ["IC:Observation", "ExactSyntax", b""], signature,
               [["IC:Name", q("f" + str(i))] for i in range(len(signature))], [], [],
               [["IC:Raw", slots, value] for value in values]]
    witness = [wrap(node(0, hole("Value", 0), hole("Value", 1), hole("Value", 2)))]
    pairs = [generalized(signature, slots, left, right)[1]
             for left, right in itertools.combinations(values, 2)]
    assert all(pattern != witness for pattern in pairs)
    assert all(sum(reference(signature, slots, pattern, value) != "IC:None" for value in values) == 2
               for pattern in pairs)
    candidate, count = factor(archive, slots, witness)
    assert count == 3 and len(encode(candidate)) < len(encode(archive))
    return archive, slots, witness


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_ic_grammar.metta").read_text())
    code = ['!(import! &self "lib/petta/lib_ic_grammar.metta")']
    expected = []

    def add(call, value, bag=False):
        i = len(expected)
        code.append(f"!(println! (GrammarCase {i} {call}))")
        expected.append((value, bag))

    value_slot = ["IC:Slot", "Value", "T"]
    root_slot = ["IC:Slot", "Root", "T"]
    small = [["IC:Operator", "T", []], ["IC:Operator", "T", [value_slot]]]
    for signature, slots, depth in [([], [], 0), ([], [root_slot], 0),
                                    ([], [value_slot] * 3, 0),
                                    ([], [value_slot, root_slot, value_slot], 0),
                                    (small, [root_slot], 0), (small, [root_slot], 1),
                                    (small, [root_slot], 3), (small, [value_slot] * 2, 1),
                                    (small, [["IC:Slot", "Root", "Missing"]], 2)]:
        templates = enumerate_templates(signature, slots, depth)
        assert len({json.dumps(pattern) for pattern in templates}) == len(templates)
        add(f'(collapse (ic-grammar:enumerate {syntax(signature)} {syntax(slots)} {depth}))',
            [["IC:Template", pattern] for pattern in templates], True)
    add('(ic-grammar:enumerate () ((IC:Slot Bad T)) 1)', ["IC:GrammarFault", "InvalidGrammar"])
    add('(ic-grammar:enumerate () () -1)', ["IC:GrammarFault", "InvalidGrammar"])
    # Same role but different sorts cannot share a parameter.
    unequal = [["IC:Slot", "Value", "A"], ["IC:Slot", "Value", "B"]]
    add(f'(collapse (ic-grammar:enumerate () {syntax(unequal)} 0))',
        [["IC:Template", [hole("Value", 0), hole("Value", 1)]]], True)

    archive, slots, witness = three_programs()
    templates = enumerate_templates(archive[3], slots, 8)
    assert witness in templates
    add(f'(ic-search:member? (IC:Template {syntax(witness)}) '
        f'(collapse (ic-grammar:enumerate {syntax(archive[3])} {syntax(slots)} 8)))', "true")
    candidate, count = factor(archive, slots, witness)
    add(f'''(case (ic-archive:install {syntax(archive)} {syntax(archive)} "common"
                    {syntax(slots)} {syntax(witness)})
       (((IC:ArchiveAccepted $candidate $bytes $before $after)
         (ThreeProgramObservation (ic-archive:restore $candidate) $before $after))
        ($other $other)))''',
        ["ThreeProgramObservation", ["IC:Some", source(archive)], str(len(encode(archive))),
         str(len(encode(candidate)))])
    # A small whole grammar is fully searched through the retained controller.
    repeated = ["IC:Archive", q("IC1"), ["IC:Observation", "ExactSyntax", b""], small,
                [["IC:Name", q("zero")], ["IC:Name", q("next")]], [], [],
                [["IC:Raw", [root_slot], [node(1, node(1, node(0)))]] for _ in range(8)]]
    possibilities = [factor(repeated, [root_slot], p) for p in enumerate_templates(small, [root_slot], 3)]
    optimum = min([len(encode(repeated))] + [len(encode(a)) for a, n in possibilities if n >= 2])
    add(f'''(case (ic:learn-programs {syntax(repeated)} "common" ({syntax(root_slot)}) 3 2048)
      (((IC:PreparedSearch $state)
        (case (ic:resume $state 1000)
          (((IC:SearchResult $best $stats $reason $fault $last)
            (case (ic-archive:pack $best)
              (((Binary:TreeBytes $bytes) (LearnedObservation $reason (tensor:shape $bytes)))
               ($other $other))))
           ($other $other))))
       ($other $other)))''', ["LearnedObservation", "RegionExhausted", [str(optimum)]])
    possibilities = [factor(archive, slots, p) for p in templates]
    optimum = min([len(encode(archive))] + [len(encode(a)) for a, n in possibilities if n >= 2])
    add(f'''(case (ic:learn-programs {syntax(archive)} "common" {syntax(slots)} 8 2048)
      (((IC:PreparedSearch $state)
        (case (ic:resume $state 10000)
          (((IC:SearchResult $best $stats $reason $fault $last)
            (case (ic-archive:pack $best)
              (((Binary:TreeBytes $bytes) (LearnedObservation $reason (tensor:shape $bytes)))
               ($other $other))))
           ($other $other))))
       ($other $other)))''', ["LearnedObservation", "RegionExhausted", [str(optimum)]])
    add(f'''(case (ic:learn-programs {syntax(repeated)} "common" ({syntax(root_slot)}) 3 2048)
      (((IC:PreparedSearch $state)
        (let $changed (add-atom &self (GrammarChanged 1))
          (let $result (ic:resume $state 1)
            (let $closed (ic:release $state) $result))))
       ($other $other)))''', ["IC:ResumeRejected", "ChangedDependencies"])
    add('(ic-grammar:term () Value T 0 (IC:HoleState 9223372036854775807 ()))',
        ["IC:GrammarFault", "Capacity"])
    add(f'(ic:learn-programs {syntax(repeated)} 123 ({syntax(root_slot)}) 3 2048)',
        ["IC:SearchRefused", "InvalidGrammar"])

    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture = args.output / "grammar.metta"
    fixture.write_text(program)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=120)
    (args.output / "grammar.log").write_text(run.stdout + run.stderr)
    answers = [form for form in parse_forms(run.stdout)
               if isinstance(form, list) and form and form[0] == "GrammarCase"]
    mismatches = []
    for i, (wanted, bag) in enumerate(expected):
        actual = [form[2] for form in answers if form[1] == str(i)]
        if len(actual) != 1:
            mismatches.append({"case": i, "answers": actual})
        elif bag:
            if Counter(map(json.dumps, actual[0])) != Counter(map(json.dumps, wanted)):
                mismatches.append({"case": i, "expected": wanted, "actual": actual[0]})
        elif actual[0] != wanted:
            mismatches.append({"case": i, "expected": wanted, "actual": actual[0]})
    receipt = {"passed": run.returncode == 0 and not mismatches and len(answers) == len(expected),
               "checks": len(expected), "answers": len(answers), "mismatches": mismatches,
               "three_program_grammar_size": len(templates), "three_program_support": count,
               "three_program_bytes": [len(encode(archive)), len(encode(candidate))],
               "scope": "finite typed canonical grammar, pairwise-missed template and complete small-region checked learning"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
