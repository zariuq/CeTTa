#!/usr/bin/env python3
"""Independent bounded scoped grammar and corpus-guided checked discovery."""

from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor
from check_binary_value import Text, construct, encoded
from check_ic_scoped_archive import dto
from check_ic_search import check_controls
from check_scoped_templates import family_wire, meta, op, reference, var, wire


def match_forest(operators, parameters, ordinary, roots, patterns, sources):
    # One extra constructor is only a reference adapter for the heterogeneous
    # forest. Matching both components together retains their shared captures.
    signature = operators + [("forest", "Reference:Forest", [([], sort) for sort in roots])]
    return reference(signature, parameters, ordinary, "Reference:Forest",
                     op(len(operators), *patterns), op(len(operators), *sources))


def patterns_for(operators, parameters, ordinary, roots, sources, depth):
    """Generate syntax first, then independently match the complete forest.

    Unlike the runtime generator, this does not prune partial captures during
    traversal. Equality of the final regions checks that the early pruning
    neither loses nor invents a supported pattern.
    """
    def term(bound, sort, source, height):
        if source[0] == "v" and 0 <= source[1] < len(bound) and bound[source[1]] == sort:
            yield source
        for index, actual in enumerate(ordinary):
            if actual == sort:
                yield var(len(bound) + index)
        for index, (_, role, dependencies, actual) in enumerate(parameters):
            if actual == sort:
                for positions in itertools.permutations(range(len(bound)), len(dependencies)):
                    if [bound[i] for i in positions] == dependencies:
                        yield meta(index, *(var(i) for i in positions))
        if height and source[0] == "o":
            _, index, children = source
            _, actual, slots = operators[index]
            assert actual == sort and len(slots) == len(children)
            choices = [list(term(binders + bound, wanted, child, height - 1))
                       for (binders, wanted), child in zip(slots, children)]
            for arguments in itertools.product(*choices):
                yield op(index, *arguments)

    choices = [list(term([], sort, source, depth)) for sort, source in zip(roots, sources)]
    result = []
    for forest in itertools.product(*choices):
        found = match_forest(operators, parameters, ordinary, roots, list(forest), sources)
        if isinstance(found, list) and found[0] == "ScopedObservation":
            result.append(list(forest))
    assert len({repr(value) for value in result}) == len(result)
    return result


def factor(archive, operators, parameters, ordinary, roots, patterns, name="discovered"):
    candidate = deepcopy(archive)
    index = len(candidate[4])
    declaration = ["Scoped:Definition", Text(name), ordinary,
                   dto(family_wire([], [], parameters)[3]), roots,
                   [dto(wire(pattern, True)) for pattern in patterns]]
    candidate[4].append(declaration)
    count = 0
    for i, item in enumerate(archive[5]):
        assert item[0] == "Scoped:Raw"
        if item[2] != roots:
            continue
        sources = [unwire(value) for value in item[3]]
        found = match_forest(operators, parameters, ordinary, roots, patterns, sources)
        if isinstance(found, list) and found[0] == "ScopedObservation":
            candidate[5][i] = ["Scoped:Call", item[1], index, dto(found[1])]
            count += 1
    return candidate, count


def unwire(value):
    def arguments(data):
        return [] if data == ["Wire:Atom", 0] else [unwire(data[1])] + arguments(data[2])
    if value[1] == ["Wire:Atom", 0]:
        return var(value[2][1])
    return op(value[2][1][1], *arguments(value[2][2]))


def printed(value):
    if isinstance(value, Text):
        return json.dumps(str(value), ensure_ascii=False)
    if type(value) is int:
        return str(value)
    if isinstance(value, list):
        return [printed(child) for child in value]
    return value


def discovery_fixture():
    operators = [("zero", "T", []), ("lambda", "T", [(["T"], "T")]),
                 ("next", "T", [([], "T")]), ("one", "T", [])]
    parameters = [("P", "Code", ["T"], "T")]

    def source(body):
        for _ in range(3):
            body = op(2, body)
        return op(1, body)

    family = dto(family_wire(["T"], operators, parameters))
    sources = [source(var(0))] * 4 + [source(op(3))] * 4 + [op(0)]
    archive = ["Scoped:Archive", Text("SC1"), ["T"], family[2], [],
               [["Scoped:Raw", ["Source:At", Text("guided-binding"), i], ["T"], [dto(wire(term))]]
                for i, term in enumerate(sources)]]
    region = [patterns for guide in dict.fromkeys(sources)
              for patterns in patterns_for(operators, parameters, [], ["T"], [guide], 5)]
    options = [factor(archive, operators, parameters, [], ["T"], patterns) for patterns in region]
    admissible = [candidate for candidate, support in options if support >= 2]
    winner = min([archive] + admissible, key=lambda candidate: len(encoded(candidate)))
    assert len(encoded(winner)) < len(encoded(archive))
    return family, operators, parameters, sources, archive, region, winner


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_scoped_grammar.metta").read_text())
    program = ['!(import! &self "lib/petta/lib_scoped_grammar.metta")', '''
(= (guided-search-result $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $last)
       (GuidedSearch $reason (tensor:shape $best) $seen (== $faults 0)))
      ($other $other))))
(= (guided-search-view (IC:PreparedSearch $state) $budget)
   (guided-search-result (ic:resume $state $budget)))
(= (guided-resume-view (IC:PreparedSearch $state))
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $statistics $reason $fault $saved)
       (GuidedResume $reason (guided-search-result (ic:resume $saved 3000))))
      ($other $other))))
''']
    expected = []

    def add(call, wanted, bag=False):
        check_controls(call)
        i = len(expected)
        program.append(f'!(println! (ScopedGrammarCase {i} {call}))')
        expected.append((printed(wanted), bag))

    operators = [("zero", "T", []), ("one", "T", []),
                 ("pair", "T", [([], "T"), ([], "T")]),
                 ("lambda", "T", [(["T"], "T")]),
                 ("two", "T", [(["T", "T"], "T")]),
                 ("mixed", "T", [(["A", "B"], "B")]), ("a", "A", []), ("b", "B", [])]
    parameters = [("P", "Code", ["T"], "T"), ("Q", "Value", ["T", "T"], "T"),
                  ("C", "Root", [], "T"), ("mixed", "Code", ["A", "B"], "B")]
    family = dto(family_wire(["T", "A", "B"], operators, parameters))
    cases = [([], [], [], 0), ([], ["T"], [op(0)], 0), ([], ["T"], [op(0)], 1),
             (["T"], ["T"], [op(0)], 1), ([], ["T"], [op(3, var(0))], 1),
             ([], ["T"], [op(4, var(1))], 1),
             ([], ["T"], [op(4, op(2, var(0), var(1)))], 2),
             ([], ["T"], [op(5, var(1))], 1),
             ([], ["T", "T"], [op(3, var(0)), op(3, op(3, var(1)))], 3),
             (["T"], ["T", "T"], [op(0), op(1)], 1)]
    region_sizes = []
    for ordinary, roots, sources, depth in cases:
        patterns = patterns_for(operators, parameters, ordinary, roots, sources, depth)
        region_sizes.append(len(patterns))
        call = (f'(collapse (scoped-grammar:enumerate {construct(family)} {construct(ordinary)} '
                f'{construct(roots)} {construct([dto(wire(s)) for s in sources])} {depth}))')
        add(call, [["Scoped:Template", [wire(pattern, True) for pattern in forest]] for forest in patterns], True)

    # An actually empty bounded region is a completed search, not malformed
    # input or a budget stop. Its source still contains a rigid constructor.
    no_parameters = deepcopy(family)
    no_parameters[3] = []
    add(f'(collapse (scoped-grammar:enumerate {construct(no_parameters)} () (T) '
        f'{construct([dto(wire(op(0)))])} 0))', [], True)
    for ordinary, roots, sources, depth in (([], ["unknown"], [op(0)], "1"),
                                           ([], ["T"], [op(0)], "-1"),
                                           ([], ["T"], [op(0)], "0.0"),
                                           ([], ["T"], [op(0, op(0))], "1")):
        add(f'(scoped-grammar:enumerate {construct(family)} {construct(ordinary)} '
            f'{construct(roots)} {construct([dto(wire(s)) for s in sources])} {depth})',
            ["Scoped:GrammarFault", "InvalidGrammar"])

    family, ops, params, sources, archive, region, winner = discovery_fixture()
    before, after = encoded(archive), encoded(winner)
    raw_packets = [["Scoped:Packet", item[1], item[2], item[3]] for item in archive[5]]
    add(f'(collapse (scoped-grammar:guide (T) {construct(raw_packets)} ()))',
        [["Scoped:Guide", [wire(term)]] for term in dict.fromkeys(sources)], True)
    # Independent complete candidate enumeration fixes both coverage and the
    # minimum actual encoded size, not a proxy node count.
    add(f'''(case (scoped-archive:observe {construct(archive)})
      (((IC:Some (Scoped:Observation $sorts $operators $packets))
        (collapse (scoped-grammar:produce {construct(archive)} {construct(family)}
          "discovered" () (T) 5 $packets)))))''',
        [["IC:Proposal", ["Scoped:Factor", archive, Text("discovered"), [], family[3], ["T"],
                           [dto(wire(p, True)) for p in patterns]]] for patterns in region], True)
    add(f'(guided-search-view (ic:learn-scoped {tensor(before)} '
        f'(IC:Grammar {construct(family)} "discovered" () (T) 5) 1024) 3000)',
        ["GuidedSearch", "RegionExhausted", [str(len(after))], str(len(region)), "true"])
    different = deepcopy(family)
    different[2][0][1] = Text("different")
    add(f'(ic:learn-scoped {tensor(before)} (IC:Grammar {construct(different)} "discovered" () (T) 5) 1024)',
        ["IC:SearchRefused", "DifferentSignature"])
    add(f'(guided-search-view (ic:learn-scoped {tensor(before)} '
        f'(IC:Grammar {construct(family)} "discovered" () (T) 0) 1024) 20)',
        ["GuidedSearch", "RegionExhausted", [str(len(before))], "0", "true"])
    add(f'(guided-resume-view (ic:learn-scoped {tensor(before)} '
        f'(IC:Grammar {construct(family)} "discovered" () (T) 5) 1024))',
        ["GuidedResume", "BudgetExhausted",
         ["GuidedSearch", "RegionExhausted", [str(len(after))], str(len(region)), "true"]])
    add(f'(ic:learn-scoped {tensor(before)} (IC:Grammar {construct(family)} "discovered" () (T) 5) 0)',
        ["IC:SearchRefused", "InvalidQuantum"])

    text = "\n".join(program) + "\n"
    check_controls(text)
    path = args.output / "scoped-grammar.metta"
    path.write_text(text)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=120)
    (args.output / "scoped-grammar.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "ScopedGrammarCase"]
    indexed = {f[1]: f[2] for f in actual}
    differences = []
    for i, (wanted, bag) in enumerate(expected):
        observed = indexed.get(str(i))
        if bag:
            equal = isinstance(observed, list) and Counter(map(repr, observed)) == Counter(map(repr, wanted))
        else:
            equal = observed == wanted
        if not equal:
            differences.append({"case": i, "expected": wanted, "actual": observed})
    receipt = {"passed": run.returncode == 0 and not differences and len(actual) == len(expected),
               "checks": len(expected), "answers": len(actual), "mismatches": differences,
               "small_region_sizes": region_sizes, "discovery_proposals": len(region),
               "before_bytes": len(before), "best_bytes": len(after),
               "scope": "declared finite scoped grammar, independent late-filter comparison and corpus-guided checked search"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [{"case": d["case"]} for d in differences]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
