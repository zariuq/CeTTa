#!/usr/bin/env python3
"""Compare full fair-queue traces with a simple independent list model."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import subprocess

from check_incremental_compression import ROOT, parse_forms, sexpr


def reference(offers, fuel):
    entries = [(i, cost, value) for i, (cost, value) in enumerate(offers)]
    ticket, phase, trace = len(entries), 0, []
    for _ in range(fuel):
        if not entries:
            break
        index = 0 if phase == 7 else min(range(len(entries)), key=lambda i: (entries[i][1], entries[i][0]))
        selected = entries.pop(index)
        trace.append(selected)
        phase = (phase + 1) % 8
        children = [(0, "Loop")] if selected[2] == "Loop" else [(0, "X"), (0, "X")] if selected[2] == "Fork" else []
        for cost, value in children:
            entries.append((ticket, cost, value))
            ticket += 1
    wire = lambda e: ["Fair:Entry", str(e[0]), str(e[1]), e[2]]
    return ["FairObservation", [wire(e) for e in trace],
            ["Fair:Queue", str(phase), str(ticket), [wire(e) for e in entries]], "Valid"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [([], 0), ([], 8), ([(0, "Loop"), (99, "Old")], 32),
             ([(0, "Fork"), (0, "Loop"), (99, "Old")], 48),
             ([(1, "A"), (1, "A"), (1, "B")], 8)]
    rng = random.Random(2704)
    for _ in range(20):
        offers = [(rng.randrange(8), rng.choice(["A", "B", "Loop", "Fork"])) for _ in range(rng.randrange(1, 10))]
        cases.append((offers, rng.randrange(1, 40)))
    code = ['!(import! &self "lib/petta/lib_fair_queue.metta")', '''
(= (fair-build $queue $offers)
   (if (== $offers ()) $queue
     (let ($offer $tail) (decons-atom $offers)
       (case $offer (((Offer $cost $value)
         (fair-build (fair-queue:offer $queue $cost $value) $tail)))))))
(= (fair-children $value)
   (case $value ((Loop ((Offer 0 Loop))) (Fork ((Offer 0 X) (Offer 0 X))) ($other ()))))
(= (fair-run $queue $fuel)
   (if (== $fuel 0) (FairRun () $queue)
     (case (fair-queue:take $queue)
       ((Fair:Empty (FairRun () $queue))
        ((Fair:Taken $selected $rest)
         (case $selected
           (((Fair:Entry $id $cost $value)
             (let $next (fair-build $rest (fair-children $value))
               (case (fair-run $next (- $fuel 1))
                 (((FairRun $trace $last)
                   (let $all (cons-atom $selected $trace) (FairRun $all $last))))))))))))))
(= (fair-test $offers $fuel)
   (case (fair-run (fair-build (fair-queue:empty) $offers) $fuel)
     (((FairRun $trace $queue)
       (FairObservation $trace $queue (if (fair-queue:valid? $queue) Valid Invalid))))))
''']
    expected = []
    for offers, fuel in cases:
        index = len(expected)
        call = ["fair-test", [["Offer", str(cost), value] for cost, value in offers], str(fuel)]
        code.append(f"!(println! (FairCase {index} {sexpr(call)}))")
        expected.append(["FairCase", str(index), reference(offers, fuel)])
    for queue in ("(Fair:Queue 8 0 ())", "(Fair:Queue 0 -1 ())",
                  "(Fair:Queue 0 1 ((Fair:Entry 1 0 A)))",
                  "(Fair:Queue 0 2 ((Fair:Entry 1 0 A) (Fair:Entry 0 0 B)))",
                  "(Fair:Queue 0 1 ((Fair:Entry 0 1.0 A)))"):
        index = len(expected)
        code.append(f"!(println! (FairCase {index} (if (fair-queue:valid? {queue}) Valid Invalid)))")
        expected.append(["FairCase", str(index), "Invalid"])
    fixture = args.output / "fair.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "fair.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "FairCase"]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "scope": "complete service traces and residual queues, recurrent zero-cost work, aging and duplicate occurrences"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in wrong[:3]:
            print("expected", expected[i]); print("actual  ", actual[i])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
