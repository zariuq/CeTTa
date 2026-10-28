#!/usr/bin/env python3
"""Complete scoped archives, independent costs and resumable checked learning."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_binary_value import Text, construct, encoded
from check_ic_search import check_controls
from check_scoped_templates import family_wire, fixture, meta, op, reference, var, wire


def dto(value):
    if isinstance(value, list):
        return [dto(v) for v in value]
    if isinstance(value, str) and value.startswith('"'):
        return Text(json.loads(value))
    if isinstance(value, str) and value.isdecimal():
        return int(value)
    return value


def repeated_family():
    sorts, operators, _ = fixture()
    params = [("P", "Code", ["T"], "T")]
    family = family_wire(sorts, operators, params)

    def wrap(hole, bound_index, schema):
        result = hole
        for _ in range(3):
            result = op(2, op(2, var(bound_index), op(0)), result)
        return result

    patterns = [op(3, wrap(meta(0, var(0)), 0, True)),
                op(3, op(3, wrap(meta(0, var(1)), 1, True)))]
    packets = []
    for i in range(8):
        outer, inner = (var(0), var(1)) if i % 2 == 0 else (op(1), op(1))
        sources = [op(3, wrap(outer, 0, False)), op(3, op(3, wrap(inner, 1, False)))]
        packets.append([["Source:At", Text("binding-library"), i], ["T", "T"], sources])
    # This valid source disagrees on the repeated parameter across components.
    sources = [op(3, wrap(var(0), 0, False)), op(3, op(3, wrap(op(1), 1, False)))]
    packets.append([["Source:At", Text("binding-library"), 8], ["T", "T"], sources])
    archive = ["Scoped:Archive", Text("SC1"), sorts, dto(family[2]), [],
               [["Scoped:Raw", origin, roots, [dto(wire(s)) for s in sources]]
                for origin, roots, sources in packets]]
    definition = ["Scoped:Definition", Text("shared"), [], dto(family[3]), ["T", "T"],
                  [dto(wire(p, True)) for p in patterns]]
    candidate = deepcopy(archive)
    candidate[4] = [definition]
    support = 0
    for i, (origin, roots, sources) in enumerate(packets):
        # A synthetic rigid pair asks the independent matcher to retain one
        # environment across the two actual rule components.
        matched = reference(operators, params, [], "T", op(2, *patterns), op(2, *sources))
        if matched != "IC:None":
            assert matched[0] == "ScopedObservation"
            candidate[5][i] = ["Scoped:Call", origin, 0, dto(matched[1])]
            support += 1
    assert support == 8 and candidate[5][-1][0] == "Scoped:Raw"
    assert len(encoded(candidate)) < len(encoded(archive))
    observation_value = ["Scoped:Observation", sorts, archive[3],
                         [["Scoped:Packet", origin, roots, [dto(wire(s)) for s in sources]]
                          for origin, roots, sources in packets]]
    return family, archive, candidate, definition, observation_value, support


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_ic_scoped_archive.metta").read_text())
    family, original, candidate, definition, observed, support = repeated_family()
    before, after = encoded(original), encoded(candidate)
    program = ['!(import! &self (library lib_ic_scoped_archive))', '''
(= (scoped-learning-view $result $expected)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (ScopedLearning (== $best $expected) $reason $seen $accepted $faults))
      ($other $other))))
(= (ic-packed:observe PackedControl:Near $bytes)
   (case (binary-value:unpack $bytes)
     (((BinaryValue:Value (PackedControl:Sample $measurement $padding))
       (if (and (binary:unsigned? $measurement) (binary-tree:bytes? $padding))
         (IC:Some $measurement) IC:None))
      ($other IC:None))))
(= (ic-packed:accepts? PackedControl:Near $original $candidate)
   (and (<= $candidate (+ $original 1)) (<= $original (+ $candidate 1))))
(= (packed-control-view $result $expected)
   (case $result
     (((IC:PackedAccepted $bytes $before $after)
       (PackedControl:Accepted (== $bytes $expected) $before $after))
      ($other $other))))
(= (scoped-completion $result)
   (case $result
     (((IC:SearchResult $best $stats $reason $fault $saved) $reason)
      ($other $other))))
(= (scoped-cancel-control (IC:PreparedSearch $state) $expected)
   (case (ic:cancel $state)
     (((IC:SearchResult $best $stats $reason $fault $saved)
       (ScopedCancelled $reason (scoped-completion (ic:resume $saved 20)) (== $best $expected)))
      ($other $other))))
(= (scoped-dependency-control (IC:PreparedSearch $state) $environment)
   (let $changed (add-atom $environment (ScopedControl:RevisionChanged))
     (let $answer (ic:resume $state 20)
       (let $released (ic:release $state)
         (ScopedDependency $answer (== $released ()))))))
''']
    expected = []

    def add(call, wanted):
        i = len(expected)
        check_controls(call)
        program.append(f'!(println! (ScopedArchiveCase {i} {call}))')
        expected.append(["ScopedArchiveCase", str(i), wanted])

    binds = f'(($original {construct(original)}) ($candidate {construct(candidate)}))'
    add(f'''(let* {binds}
      (let* (($before (ic:scoped-pack $original)) ($after (ic:scoped-pack $candidate)))
        (case ($before $after)
          ((((Binary:TreeBytes $x) (Binary:TreeBytes $y))
            (ScopedArchiveObservation
              (== (ic:scoped-restore $x) (IC:Some {construct(observed)}))
              (== (ic:scoped-restore $y) (IC:Some {construct(observed)}))
              (tensor:shape $x) (tensor:shape $y)))
           ($other $other)))))''',
        ["ScopedArchiveObservation", "true", "true", [str(len(before))], [str(len(after))]])
    add(f'''(let* {binds}
      (case (ic:scoped-factor $original "shared" () {construct(definition[3])}
              (T T) {construct(definition[5])})
        (((IC:ScopedProposal $factored $support)
          (ScopedFactorObservation (== $factored $candidate) $support))
         ($other $other))))''', ["ScopedFactorObservation", "true", str(support)])
    add(f'''(case (ic:scoped-check {tensor(before)} {tensor(before)} {tensor(after)})
      (((IC:ScopedAccepted $bytes $old $new)
        (ScopedAccepted $old $new (== $bytes {tensor(after)}))) ($other $other)))''',
        ["ScopedAccepted", str(len(before)), str(len(after)), "true"])
    add(f'(ic:scoped-check {tensor(before)} {tensor(before)} {tensor(before)})',
        ["IC:ScopedRejected", "NotSmaller"])
    changed = deepcopy(candidate)
    changed[5][0][1][-1] = 999
    add(f'(ic:scoped-check {tensor(before)} {tensor(before)} {tensor(encoded(changed))})',
        ["IC:ScopedRejected", "Reconstruction"])
    changed = deepcopy(candidate)
    changed[5][0][3][2][0][3] = dto(wire(op(0)))
    add(f'(ic:scoped-check {tensor(before)} {tensor(before)} {tensor(encoded(changed))})',
        ["IC:ScopedRejected", "Reconstruction"])
    for label, change in (
        ("bad index", lambda a: a[5][0].__setitem__(2, 99)),
        ("bad role", lambda a: a[5][0][3][2][0].__setitem__(1, "Value")),
        ("wrong body sort", lambda a: a[5][0][3][2][0].__setitem__(3, dto(wire(op(7))))),
        ("unknown sort", lambda a: a[3][0].__setitem__(2, "unknown")),
        ("version", lambda a: a.__setitem__(1, Text("bad"))),
    ):
        broken = deepcopy(candidate)
        change(broken)
        add(f'(ic:scoped-unpack {tensor(encoded(broken))})', "IC:None")
    for payload in (b"", before + b"\0"):
        add(f'(ic:scoped-unpack {tensor(payload)})', "IC:None")

    # Small exact bytes independently fix the protocol layout, without
    # printing a full copy of every large witness in the transcript.
    small = ["Scoped:Archive", Text("SC1"), ["T"],
             [["Scoped:Operator", Text("z"), "T", []]], [],
             [["Scoped:Raw", Text("small"), ["T"], [dto(wire(op(0)))]]]]
    add(f'''(case (ic:scoped-pack {construct(small)})
      (((Binary:TreeBytes $bytes) (tensor:inspect $bytes)) ($other $other)))''', observation(encoded(small)))

    # A decreasing node description can still be too costly after charging its
    # parameter declarations and the whole new definition.
    tiny_candidate = deepcopy(small)
    tiny_candidate[4] = [["Scoped:Definition", Text("costly"), ["T"], [], ["T"], [dto(wire(var(0), True))]]]
    tiny_candidate[5] = [["Scoped:Call", Text("small"), 0,
                         ["Scoped:Captures", [["IC:Binding", "Root", 0, dto(wire(op(0)))]], []]]]
    assert len(encoded(tiny_candidate)) > len(encoded(small))
    add(f'(ic:scoped-check {tensor(encoded(small))} {tensor(encoded(small))} {tensor(encoded(tiny_candidate))})',
        ["IC:ScopedRejected", "NotSmaller"])

    proposal = ["Scoped:Factor", original, definition[1], definition[2], definition[3], definition[4], definition[5]]
    no_gain = ["Scoped:Factor", original, Text("identity"), ["T", "T"], [], ["T", "T"],
               [dto(wire(var(0), True)), dto(wire(var(1), True))]]
    candidates = ["IC:Candidates", [no_gain, proposal]]
    add(f'''(let* (($bytes {tensor(before)}) ($expected {tensor(after)}))
       (case (ic:learn-scoped $bytes {construct(candidates)} 256)
         (((IC:PreparedSearch $state)
           (scoped-learning-view (ic:resume $state 200) $expected)) ($other $other))))''',
        ["ScopedLearning", "true", "RegionExhausted", "2", "1", "0"])
    add(f'''(let* (($bytes {tensor(before)}) ($expected {tensor(after)}))
      (case (ic:learn-scoped $bytes {construct(candidates)} 256)
        (((IC:PreparedSearch $state)
          (case (ic:resume $state 1)
            (((IC:SearchResult $best $statistics $reason $fault $later)
              (ScopedResume $reason
                (scoped-learning-view (ic:resume $later 200) $expected)))))) ($other $other))))''',
        ["ScopedResume", "BudgetExhausted", ["ScopedLearning", "true", "RegionExhausted", "2", "1", "0"]])
    # Preparation drops unused proposal parameters from the raw artifact.
    source_packets = [["Scoped:Packet", item[1], item[2], item[3]] for item in original[5]]
    add(f'''(case (ic:prepare-scoped {construct(dto(family))} {construct(source_packets)})
      (((IC:ScopedTask $bytes) (== $bytes {tensor(before)})) ($other $other)))''', "true")

    # A task may declare a nontransitive error tolerance. Comparing with the
    # preceding winner would accumulate drift; the fixed original must govern
    # every candidate, even when every successive encoding is cheaper.
    near = [encoded(["PackedControl:Sample", i, bytes(32 - 16 * i)]) for i in range(3)]
    assert len(near[0]) > len(near[1]) > len(near[2])
    for original_index, current_index, candidate_index, wanted in (
        (0, 0, 1, ["PackedControl:Accepted", "true", str(len(near[0])), str(len(near[1]))]),
        (0, 1, 2, ["IC:PackedRejected", "Reconstruction"]),
        (1, 1, 2, ["PackedControl:Accepted", "true", str(len(near[1])), str(len(near[2]))]),
        (0, 2, 2, ["IC:PackedRejected", "Reconstruction"]),
    ):
        args_bytes = ' '.join(tensor(near[i]) for i in (original_index, current_index, candidate_index))
        add(f'(packed-control-view (ic-packed:check PackedControl:Near {args_bytes}) {tensor(near[candidate_index])})',
            wanted)

    add(f'''(let $bytes {tensor(encoded(small))}
      (scoped-cancel-control (ic:learn-scoped $bytes (IC:Candidates ()) 256) $bytes))''',
        ["ScopedCancelled", "Cancelled", "RegionExhausted", "true"])
    add(f'''(let $environment (context-space)
      (scoped-dependency-control
        (ic:learn-scoped {tensor(encoded(small))} (IC:Candidates ()) 256) $environment))''',
        ["ScopedDependency", ["IC:ResumeRejected", "ChangedDependencies"], "true"])

    text = "\n".join(program) + "\n"
    check_controls(text)
    path = args.output / "scoped-archives.metta"
    path.write_text(text)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=90)
    (args.output / "scoped-archives.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "ScopedArchiveCase"]
    indexed = {f[1]: f for f in actual}
    differences = [{"case": i, "expected": want, "actual": indexed.get(str(i))}
                   for i, want in enumerate(expected) if indexed.get(str(i)) != want]
    receipt = {"passed": run.returncode == 0 and not differences and len(actual) == len(expected),
               "checks": len(expected), "answers": len(actual), "mismatches": differences,
               "source_occurrences": len(original[5]), "factored_occurrences": support,
               "before_bytes": len(before), "after_bytes": len(after),
               "scope": "complete scoped archive cost, original observation, shared captures, locations and resumed candidate search"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [{"case": row["case"]} for row in differences]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
