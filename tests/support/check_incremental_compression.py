#!/usr/bin/env python3
"""Focused Python/MeTTa correspondence for the checked IC reference family."""

from __future__ import annotations

import argparse
import itertools
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_semiring_fixtures import parse_forms, sexpr


def unary(number):
    result = "IC:Z"
    for _ in range(number):
        result = ["IC:S", result]
    return result


def feature(number):
    return ["IC:Repeat", unary(number)]


def data(bits):
    return ["IC:B1" if value else "IC:B0" for value in bits]


def reference(source, proposals, fuel):
    """Checked.lean's first-accepted policy and RepeatedBlock.lean's model."""
    residual = list(source)
    chosen = []
    stop = "IC:BudgetExhausted"
    for _ in range(fuel):
        accepted = None
        for count in proposals:
            if count < 2 or len(residual) % count:
                continue
            block = residual[:len(residual) // count]
            if block * count == residual and count + 2 + len(block) + 1 < len(residual) + 1:
                accepted = count, block
                break
        if accepted is None:
            stop = "IC:NoCandidateAccepted"
            break
        count, residual = accepted
        chosen.append(count)
    restored = list(residual)
    for count in reversed(chosen):
        restored *= count
    assert restored == list(source)
    cost = sum(count + 2 for count in chosen) + len(residual) + 1
    assert cost <= len(source) + 1
    return ["IC:Observation", [feature(n) for n in chosen], data(residual), stop,
            ["IC:Some", data(restored)], ["IC:Some", str(cost)]]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = []
    for length in range(5):
        for bits in itertools.product((False, True), repeat=length):
            cases.append((bits, [0, 1, 3, 2], 3))
    for block in ([False], [True, False], [False, False, True], [True, False, True, False, False]):
        for count in (2, 3, 8, 16):
            for fuel in (0, 1, 4):
                cases.append((block * count, [3, 2, 4, 5, 6, 7, 8], fuel))
    statements = ['!(import! &self "lib/petta/lib_ic_repeated_block.metta")', '''
(= (ic-test $proposals $fuel $source)
   (case (ic:compress IC:RepeatedBits $proposals $fuel $source)
     (((IC:Plan $features $residual $stop)
       (let* (($restored (ic:restore IC:RepeatedBits $features $residual))
              ($cost (ic:plan-cost IC:RepeatedBits $features $residual)))
         (IC:Observation $features $residual $stop $restored $cost)))
      ($other $other))))
''']
    expected = []
    for index, (bits, candidates, fuel) in enumerate(cases):
        call = ["ic-test", [feature(n) for n in candidates], unary(fuel), data(bits)]
        statements.append(f"!(println! (ICCase {index} {sexpr(call)}))")
        expected.append(["ICCase", str(index), reference(bits, candidates, fuel)])
    for index, (call, wanted) in enumerate([
        (["ic:compress", "IC:RepeatedBits", [], "bad-fuel", []], ["IC:Invalid", "Fuel"]),
        (["ic:compress", "IC:RepeatedBits", [], unary(1), ["not-a-bit"]], ["IC:Invalid", "Source"]),
        (["ic:compress", "IC:RepeatedBits", [], unary(1), ["1", "1.0"]], ["IC:Invalid", "Source"]),
    ], start=len(expected)):
        statements.append(f"!(println! (ICCase {index} {sexpr(call)}))")
        expected.append(["ICCase", str(index), wanted])
    fixture = args.output / "correspondence.metta"
    fixture.write_text("\n".join(statements) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "correspondence.log").write_text(result.stdout + result.stderr)
    actual = [form for form in parse_forms(result.stdout) if isinstance(form, list) and form and form[0] == "ICCase"]
    mismatches = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    passed = result.returncode == 0 and len(actual) == len(expected) and not mismatches
    receipt = {"passed": passed, "checks": len(expected), "answers": len(actual),
               "mismatches": mismatches, "exit_code": result.returncode,
               "scope": "finite correspondence, not a universal host-runtime refinement proof"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not passed:
        for i in mismatches[:3]:
            print("expected", sexpr(expected[i]))
            print("actual  ", sexpr(actual[i]))
        print(result.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
