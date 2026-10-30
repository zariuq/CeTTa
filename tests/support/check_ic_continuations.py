#!/usr/bin/env python3
"""Dynamic proposal continuations use the common owned cost/age controller."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms
from check_ic_archive import syntax
from check_ic_search import check_controls, fixtures


HELPERS = r'''
(= (ic-search:observation IC:ContinuationProbe $original)
   (ic-search:observation IC:ProgramArchive $original))
(= (ic-search:consider IC:ContinuationProbe $original $best $candidate)
   (ic-search:consider IC:ProgramArchive $original $best $candidate))
(= (ic-search:materialize IC:ContinuationProbe $original $proposal)
   (case $proposal
     (((Probe:Events $events) (IC:Responses $events))
      ((Probe:Later $remaining $winner)
       (if (== $remaining 0) (IC:Candidate $winner)
         (IC:Responses ((IC:Candidate $original)
           (IC:Continue 99 (Probe:Later (- $remaining 1) $winner))))))
      (Probe:Loop (IC:Continue 0 Probe:Loop))
      ($other (IC:ProducerFault UnknownProbe)))))
(= (continuation-test:open $original $proposals)
   (let $handle (work-new (ic-search:from-list $proposals))
     (ic:prepare IC:ContinuationProbe $original () 64 ((IC:Branch 99 $handle)))))
(= (continuation-test:view $result $winner)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $state)
       (let $released (ic:release $state)
         (Continuation (== $best $winner) $seen $accepted $faults $reason
           (case $first ((IC:NoFault IC:NoFault) ((IC:FirstFault $ticket $why) $why))))))
      ($other $other))))
(= (continuation-test:pieces $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $stats BudgetExhausted $first $later)
       (if (> $remaining 0) (continuation-test:pieces $later (- $remaining 1))
         (let $released (ic:release $later) ProbeBudget))) ($other $other))))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    original, winner = fixtures()
    bindings = f"(($original {syntax(original)}) ($winner {syntax(winner)}))"
    code = ['!(import! &self (library lib_ic_search))', HELPERS]
    expected = []

    def add(body, wanted):
        index = len(expected)
        code.append(f"!(println! (ContinuationCase {index} (let* {bindings} {body})))")
        expected.append(["ContinuationCase", str(index), wanted])

    wanted = ["Continuation", "true", "3", "1", "0", "RegionExhausted", "IC:NoFault"]
    for run in ("(ic:resume $state 200)", "(continuation-test:pieces $state 200)"):
        add(f'''(case (continuation-test:open $original ((IC:Proposal (Probe:Later 2 $winner))))
          (((IC:PreparedSearch $state) (continuation-test:view {run} $winner)) ($other $other)))''', wanted)
    add('''(let* (($cheap (work-new (ic-search:from-list ((IC:Proposal Probe:Loop)))))
                   ($later (work-new (ic-search:from-list ((IC:Proposal (Probe:Later 2 $winner)))))))
      (case (ic:prepare IC:ContinuationProbe $original () 64 ((IC:Branch 0 $cheap) (IC:Branch 99 $later)))
        (((IC:PreparedSearch $state)
          (case (ic:resume $state 192)
            (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $first $next)
              (let $released (ic:release $next)
                (BehindLoop (== $best $winner) (>= $seen 3) $accepted $faults $reason))))))
         ($other $other))))''', ["BehindLoop", "true", "true", "1", "0", "BudgetExhausted"])
    for events, reason, seen in [
        ('((IC:ProducerFault InverseFailure) (IC:Continue 9 (Probe:Later 0 $winner)))', "InverseFailure", "1"),
        ('((IC:Continue -1 BadPriority) (IC:Continue 9 (Probe:Later 0 $winner)))', "InvalidPriority", "1"),
        ('((IC:Responses NotAnExpression) (IC:Continue 9 (Probe:Later 0 $winner)))', "InvalidResponses", "1"),
    ]:
        add(f'''(case (continuation-test:open $original ((IC:Proposal (Probe:Events {events}))))
          (((IC:PreparedSearch $state) (continuation-test:view (ic:resume $state 200) $winner)) ($other $other)))''',
            ["Continuation", "true", seen, "1", "1", "FaultedRegion", reason])
    add('''(case (continuation-test:open $original ((IC:Proposal (Probe:Events
             ((IC:Responses ((IC:Candidate $original) (IC:Candidate $original)))
              (IC:Continue 1 (Probe:Later 0 $winner)))))))
      (((IC:PreparedSearch $state) (continuation-test:view (ic:resume $state 200) $winner)) ($other $other)))''', wanted)
    add('''(case (continuation-test:open $original ((IC:Proposal (Probe:Later 2 $winner))))
      (((IC:PreparedSearch $state)
        (case (ic:resume $state 1)
          (((IC:SearchResult $best $stats $reason $first $next)
            (ResumedOnce (ic:resume $state 1)
              (continuation-test:view (ic:resume $next 200) $winner)))))) ($other $other)))''',
        ["ResumedOnce", ["IC:ResumeRejected", "CursorChanged"], wanted])
    add('''(case (continuation-test:open $original ((IC:Proposal (Probe:Later 2 $winner))))
      (((IC:PreparedSearch $state)
        (case (ic:resume $state 1)
          (((IC:SearchResult $best $stats $reason $first $next)
            (case (ic:cancel $next)
              (((IC:SearchResult $same $statistics $cancelled $fault $saved)
                (CancelledChild $cancelled
                  (continuation-test:view (ic:resume $saved 200) $winner))))))))) ($other $other)))''',
        ["CancelledChild", "Cancelled", wanted])
    source = "\n".join(code) + "\n"
    check_controls(source)
    path = args.output / "continuations.metta"
    path.write_text(source)
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())], cwd=ROOT,
                            capture_output=True, text=True, timeout=90)
    (args.output / "continuations.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "ContinuationCase"]
    indexed = {f[1]: f for f in actual}
    wrong = [{"case": i, "expected": wanted, "actual": indexed.get(str(i))} for i, wanted in enumerate(expected)
             if indexed.get(str(i)) != wanted]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "scope": "dynamic proposal continuations, ownership, cost/age fairness and checked original artifacts"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [row['case'] for row in wrong]}))
    for row in wrong[:3]:
        print(json.dumps(row)[:1600])
    if result.returncode:
        print(result.stderr[-1800:])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
