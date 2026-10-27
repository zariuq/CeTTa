#!/usr/bin/env python3
"""Check ordinary heap/work imports on one production PeTTa configuration."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import subprocess

from check_binary_codec import ROOT, parse_forms


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    expected = []
    code = ['''
!(println! (SearchLibraryCase 0 (heap:empty)))
!(println! (SearchLibraryCase 1 (work-new (+ 1 1))))
!(import! &self (library heap))
!(import! &self (library work))
(= (heap-drain $heap)
   (case (heap:take $heap)
     ((Heap:Empty ())
      ((Heap:Item $key $value $rest)
       (let $tail (heap-drain $rest) (cons-atom ($key $value) $tail))))))
(= (heap-observe $heap)
   ((heap:size $heap) (if (heap:valid? $heap) Valid Invalid) (heap-drain $heap)))
(= (finite-source) A)
(= (finite-source) A)
(= (finite-source) B)
(= (delayed-source $n) (if (== $n 0) (finite-source) (delayed-source (- $n 1))))
(= (work-drain $h $fuel $values $paused)
   (if (== $fuel 0) (FailedToFinish $values)
     (case (work-take $h 1)
       (((taken $next complete) (WorkObservation (union-atom $values $next) $paused))
        ((taken $next paused) (work-drain $h (- $fuel 1) (union-atom $values $next) Yes))
        ((taken $next more-possible) (work-drain $h (- $fuel 1) (union-atom $values $next) $paused))
        ($other $other)))))
''']
    expected.extend([["SearchLibraryCase", "0", ["heap:empty"]],
                     ["SearchLibraryCase", "1", ["work-new", ["+", "1", "1"]]]])

    def add(call, result):
        index = len(expected)
        code.append(f"!(println! (SearchLibraryCase {index} {call}))")
        expected.append(["SearchLibraryCase", str(index), result])

    add("(heap-observe (heap:empty))", ["0", "Valid", []])
    rng = random.Random(2703)
    for length in (1, 4, 17, 64):
        entries = [(rng.randrange(9), i) for i in range(length)]
        bindings = ["($h0 (heap:empty))"]
        for i, (priority, serial) in enumerate(entries):
            bindings.append(f"($h{i+1} (heap:insert $h{i} (Key {priority} {serial}) V{serial}))")
        add(f"(let* ({' '.join(bindings)}) (HeapObservation (heap-observe $h{length}) (heap:size $h0)))",
            ["HeapObservation", [str(length), "Valid",
             [[["Key", str(p), str(s)], f"V{s}"] for p, s in sorted(entries)]], "0"])
    add("(let* (($h (heap:empty)) ($a (heap:insert $h 1 A)) ($b (heap:insert $a 1 B))) (heap-drain $b))",
        [["1", "B"], ["1", "A"]])
    for heap in ("(heap nil 1)", "(heap (t X 2 ()) 0)", "(heap (t X 2 ((t Y 1 ()))) 2)",
                 "(heap (t X 2 ((t Y 3 ()))) 3)", "(heap (t X 2 ()) 1)", "unknown"):
        valid = heap == "(heap (t X 2 ()) 1)"
        add(f"(if (heap:valid? {heap}) Valid Invalid)", "Valid" if valid else "Invalid")
    add("(let $h (work-new (delayed-source 6)) (let $u (work-budget $h 2) (work-drain $h 200 () No)))",
        ["WorkObservation", ["A", "A", "B"], "Yes"])
    add("(let $h (work-new (finite-source)) ((work-take $h 1) (work-take $h 1) (work-take $h 4)))",
        [["taken", ["A"], "more-possible"], ["taken", ["A"], "more-possible"], ["taken", ["B"], "complete"]])
    add("(let $h (work-new (finite-source)) (let $u (work-cancel $h) "
        "(case (work-take $h 1) (((Error $call WorkReleased) Released) ($other $other)))))", "Released")
    add("(let $h (work-new (finite-source)) "
        "(case $h (((NativeHandle $kind $id) "
        "((case (work-take (NativeHandle $kind (+ $id 0)) 1) "
        "(((Error $call WorkReleased) Refused) ($other $other))) (work-take $h 1))))))",
        ["Refused", ["taken", ["A"], "more-possible"]])
    add("(let $h (work-new (finite-source)) (let $before (work:epoch $h) "
        "(let $zero (work-take $h 0) (if (== $before (work:epoch $h)) Unchanged Changed))))", "Changed")
    code += ['''
(= (meter-mark $x) (let $u (add-atom &self (meter-seen $x)) $x))
''']
    add("(let $h (work-new (meter-mark (superpose (1 2 3)))) "
        "(let* (($r (work-route $h completed)) ($b (work-budget $h 1)) ($t (work-take $h 1))) "
        "($t (collapse (match &self (meter-seen $x) $x)))))",
        [["taken", [], ["failed", "WorkBudgetRequiresResumableRoute"]], []])
    add("(let $h (work-new (meter-mark (superpose (4 5 6)))) "
        "((work-take $h 1) (work-take $h 5) (collapse (match &self (meter-seen $x) $x))))",
        [["taken", ["4"], "more-possible"], ["taken", ["5", "6"], "complete"], ["4", "5", "6"]])
    fixture = args.output / "libraries.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "libraries.log").write_text(result.stdout + result.stderr)
    actual = [x for x in parse_forms(result.stdout)
              if isinstance(x, list) and x and x[0] == "SearchLibraryCase"]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    # One running query has already declined work-new when the import arrives.
    cache = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", "-e",
        '!(let $before (work-new (+ 1 1)) (let $u (import! &self (library work)) '
        '($before (let $after (work-new (+ 1 1)) (work-take $after 1)))))'],
        cwd=ROOT, capture_output=True, text=True, timeout=10)
    cache_ok = cache.returncode == 0 and parse_forms(cache.stdout) == [
        [["work-new", ["+", "1", "1"]], ["taken", ["2"], "more-possible"]]]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong and cache_ok,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "late_import_refresh": cache_ok,
               "scope": "ordinary imports, persistent heap ordering/invariants, bounded prefixes, duplicate answers, effects and release"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in wrong[:4]:
            print("expected", expected[i]); print("actual  ", actual[i])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
