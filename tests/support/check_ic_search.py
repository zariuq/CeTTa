#!/usr/bin/env python3
"""Check native retained candidate search, aging, resumption and refusal."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import subprocess

from check_ic_archive import encode, node, hole, q, syntax
from check_binary_codec import ROOT, parse_forms


def check_controls(program=None):
    def visit(form, path):
        if not isinstance(form, list) or not form:
            return
        if form[0] == "quote":
            return
        if form[0] in ("case", "if", "let", "let*"):
            expected = 3 if form[0] in ("case", "let*") else 4
            assert len(form) == expected, (str(path), form[0], len(form))
            if form[0] == "case":
                assert isinstance(form[2], list) and all(isinstance(pair, list) and len(pair) == 2 for pair in form[2]), str(path)
                visit(form[1], path)
                for pair in form[2]:
                    visit(pair[1], path)
                return
        for child in form:
            visit(child, path)
    for name in ("lib_ic_search.metta", "lib_fair_queue.metta", "lib_ic_archive.metta"):
        path = ROOT / "lib/petta" / name
        for form in parse_forms(path.read_text()):
            if isinstance(form, list) and form and form[0] == "=":
                visit(form[2], path)
    if program is not None:
        for form in parse_forms(program):
            visit(form, "generated search controls")


def fixtures():
    slot = ["IC:Slot", "Root", "Term"]
    signature = [["IC:Operator", "Term", []], ["IC:Operator", "Term", []],
                 ["IC:Operator", "Term", [["IC:Slot", "Value", "Term"], ["IC:Slot", "Value", "Term"]]]]
    def body(value, depth=3):
        return value if not depth else node(2, body(value, depth - 1), body(value, depth - 1))
    values = [node(0), node(1)] * 5
    original = ["IC:Archive", q("IC1"), ["IC:Observation", "ExactSyntax", b""], signature,
                [["IC:Name", q("zero")], ["IC:Name", q("one")], ["IC:Name", q("pair")]], [], [],
                [["IC:Raw", [slot], [body(value)]] for value in values]]
    candidate = deepcopy(original)
    candidate[6] = [["IC:Definition", q("shared"), [slot], [body(hole("Value", 0))]]]
    candidate[7] = [["IC:Call", "0", [["IC:Binding", "Value", "0", value]]] for value in values]
    assert len(encode(candidate)) < len(encode(original))
    return original, candidate


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    check_controls()
    args.output.mkdir(parents=True, exist_ok=True)
    original, candidate = fixtures()
    code = ['!(import! &self (library lib_ic_search))', '''
(= (spam $original) (IC:Candidate $original))
(= (spam $original) (spam $original))
(= (stubborn $original $candidate) (stubborn $original $candidate))
(= (stubborn $original $candidate) (IC:Candidate $original))
(= (stubborn $original $candidate) (IC:Candidate $candidate))
(= (search-view $result $candidate)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (SearchObservation (== $best $candidate) $reason (> $turns 0) (>= $seen 2) $accepted $faults))
      ($other $other))))
(= (finite-search $original $candidate)
   (let $h (work-new (ic-search:from-list ((IC:Candidate $original) (IC:Candidate $candidate))))
     (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 10 $h)))
       (((IC:PreparedSearch $state) (ic:resume $state 200)) ($other $other)))))
''']
    expected = []

    def add(call, result):
        index = len(expected)
        code.append(f"!(println! (SearchCase {index} {call}))")
        expected.append(["SearchCase", str(index), result])

    binding = f'(($original {syntax(original)}) ($candidate {syntax(candidate)}))'
    def bound(call):
        return f'(let* {binding} {call})'
    add(bound('(search-view (finite-search $original $candidate) $candidate)'),
        ["SearchObservation", "true", "RegionExhausted", "true", "true", "1", "0"])
    add(bound('''(let* (($cheap (work-new (spam $original)))
                        ($later (work-new (ic-search:from-list ((IC:Candidate $original) (IC:Candidate $candidate))))))
      (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 0 $cheap) (IC:Branch 99 $later)))
        (((IC:PreparedSearch $state)
          (let $result (ic:resume $state 128)
            (case $result
              (((IC:SearchResult $best $stats $reason $fault $last)
                (let $released (ic:release $last) (search-view $result $candidate)))))))
         ($other $other))))'''),
        ["SearchObservation", "true", "BudgetExhausted", "true", "true", "1", "0"])
    add(bound('''(let $h (work-new (stubborn $original $candidate))
      (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 10 $h)))
        (((IC:PreparedSearch $state)
          (let $result (ic:resume $state 64)
            (case $result (((IC:SearchResult $best $stats $reason $fault $last)
              (let $released (ic:release $last) (search-view $result $candidate)))))))
         ($other $other))))'''),
        ["SearchObservation", "true", "BudgetExhausted", "true", "true", "1", "0"])
    add(bound('''(let $h (work-new (ic-search:from-list ((IC:Candidate $original) (IC:Candidate $candidate))))
      (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 1 $h)))
        (((IC:PreparedSearch $state)
          (case (ic:resume $state 1)
            (((IC:SearchResult $best $stats $reason $fault $later)
              (Resumed (ic:resume $state 1) (search-view (ic:resume $later 200) $candidate))))))
         ($other $other))))'''),
        ["Resumed", ["IC:ResumeRejected", "CursorChanged"],
         ["SearchObservation", "true", "RegionExhausted", "true", "true", "1", "0"]])
    add(bound('''(let $h (work-new (ic-search:from-list ((IC:Candidate $original) (IC:Candidate $candidate))))
      (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 1 $h)))
        (((IC:PreparedSearch $state)
          (case (ic:cancel $state)
            (((IC:SearchResult $best $stats $reason $fault $later)
              (CancelledResume $reason (search-view (ic:resume $later 200) $candidate))))))
         ($other $other))))'''),
        ["CancelledResume", "Cancelled", ["SearchObservation", "true", "RegionExhausted", "true", "true", "1", "0"]])
    add(bound('''(let $h (work-new (ic-search:from-list ((IC:ProducerFault InverseFailure) (IC:Candidate $candidate))))
      (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 1 $h)))
        (((IC:PreparedSearch $state)
          (case (ic:resume $state 200)
            (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason
                (IC:FirstFault $ticket $failure) $later)
              (FaultObservation (== $best $candidate) $reason $seen $accepted $faults $failure)))))
         ($other $other))))'''),
        ["FaultObservation", "true", "FaultedRegion", "1", "1", "1", "InverseFailure"])
    add(bound('''(let* (($source (new-space))
      ($h (work-new (ic-search:from-list ((IC:Candidate $candidate))))))
      (case (ic:prepare IC:ProgramArchive $original ($source) 64 ((IC:Branch 1 $h)))
        (((IC:PreparedSearch $state)
          (let* (($before (work:epoch $h)) ($edit (add-atom $source Changed))
                  ($result (ic:resume $state 10)))
            (let $unchanged (== $before (work:epoch $h))
              (let $released (ic:release $state) (DependencyObservation $result $unchanged)))))
         ($other $other))))'''),
        ["DependencyObservation", ["IC:ResumeRejected", "ChangedDependencies"], "true"])
    add('(ic-search:guards (NotASpace))', 'IC:None')
    add('(ic-search:guards-current? ((IC:Guard NotASpace 0)))', 'false')
    add(bound('''(let $h (work-new (ic-search:from-list ((IC:Candidate $candidate))))
      (case (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 1 $h)))
        (((IC:PreparedSearch $state)
          (case (ic:resume $state 0)
            (((IC:SearchResult $best $statistics $reason $fault $later)
              (let $released (ic:release $later) (ZeroBudget $statistics $reason))))))
         ($other $other))))'''),
        ["ZeroBudget", ["IC:Statistics", "0", "0", "0", "0"], "BudgetExhausted"])
    add(bound('''(case (ic:prepare IC:ProgramArchive $original () 64 ())
       (((IC:PreparedSearch $state)
         (case (ic:resume $state 0)
           (((IC:SearchResult $best $statistics $reason $fault $later)
             (EmptyRegion $statistics $reason)))))))'''),
        ["EmptyRegion", ["IC:Statistics", "0", "0", "0", "0"], "RegionExhausted"])
    add(bound('''(let $h (work-new (ic-search:from-list ((IC:Candidate $candidate))))
      (ic:prepare IC:ProgramArchive $original () 64 ((IC:Branch 1 $h) (IC:Branch 2 $h))))'''),
        ["IC:SearchRefused", "InvalidProducers"])
    fixture = args.output / "search.metta"
    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture.write_text(program)
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=60)
    (args.output / "search.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "SearchCase"]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "scope": "candidate search over owned retained producers; complete artifacts, inner and outer fairness, soft cancellation and stale snapshot refusal"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in wrong[:5]:
            print("expected", expected[i]); print("actual  ", repr(actual[i])[:2500])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
