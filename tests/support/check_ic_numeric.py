#!/usr/bin/env python3
"""Independent finite affine search, complete NC1 costs and original-task checks."""

from __future__ import annotations

import argparse
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor as byte_tensor
from check_binary_value import Text, encoded
from check_ic_search import check_controls
from check_tensor_sequences import normalized, scalar


def vector(values, dtype="f64"):
    return f"(tensor:from-values {dtype} ({len(values)}) ({' '.join(map(scalar, values))}))"


def ctn(values, dtype="f64"):
    code, fmt = {"f64": (2, "d"), "u8": (3, "B")}[dtype]
    return b"CTN1" + bytes([code]) + struct.pack("<IQ", 1, len(values)) + struct.pack("<" + fmt * len(values), *values)


def archive(inputs, outputs, mask, tolerance, parameters=None):
    body = ["Numeric:Raw", ctn(outputs)] if parameters is None else ["Numeric:Affine", ctn(parameters)]
    return encoded(["Numeric:Archive", Text("NC1"), ctn(inputs), ctn(list(map(int, mask)), "u8"),
                    ctn([tolerance]), body])


def reference(inputs, outputs, mask, tolerance, domains):
    """Enumerate the finite product independently; never compare to the last fit."""
    original = archive(inputs, outputs, mask, tolerance)
    best, seen, accepted, faults, first_fault = original, 0, 0, 0, "IC:NoFault"
    for parameters in itertools.product(*(values for _kind, values in domains)):
        if any(kind == "i64" and int(float(value)) != value for (kind, _values), value in zip(domains, parameters)):
            faults += 1
            first_fault = "ParameterConversion"
            continue
        a, b = map(float, parameters)
        predicted = [a * x + b for x in inputs]
        candidate = archive(inputs, outputs, mask, tolerance, (a, b))
        seen += 1
        valid = all(math.isfinite(y) for y in predicted) and all(
            not keep or abs(y - target) <= tolerance for keep, y, target in zip(mask, predicted, outputs))
        if valid and len(candidate) < len(best):
            best, accepted = candidate, accepted + 1
    return ["NumericResult", observation(best), str(seen), str(accepted), str(faults),
            "FaultedRegion" if faults else "RegionExhausted", first_fault]


HELPERS = r'''
(= (numeric-result $result)
   (case $result
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $state)
       (NumericResult (tensor:inspect $best) $seen $accepted $faults $reason
         (case $fault ((IC:NoFault IC:NoFault) ((IC:FirstFault $ticket $why) $why)))))
      ($other $other))))
(= (numeric-pieces $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best $statistics BudgetExhausted $fault $next)
       (if (> $remaining 0) (numeric-pieces $next (- $remaining 1))
         (let $released (ic:release $next) (NumericTestFault PieceBudget))))
      ($other $other))))
(= (numeric-prediction $result $inputs)
   (case $result
     (((IC:SearchResult $best $statistics $reason $fault $state)
       (case (ic:predict Numeric:Affine $best $inputs)
         (((IC:Prediction $values) (tensor:inspect (ic-numeric:encode $values)))
          ($other $other))))
      ($other $other))))
(= (numeric-wait-winner $state $remaining)
   (case (ic:resume $state 1)
     (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $next)
       (if (> $accepted 0)
         (case $next
           (((IC:SearchState (IC:Task $model $original $observation $guards $quantum) $queue $winner $stats $first)
             (ApproximateResume
               (== (ic-search:observation $model $winner) (IC:Some $observation))
               (numeric-result (ic:resume $next 1000))))))
         (if (and (== $reason BudgetExhausted) (> $remaining 0))
           (numeric-wait-winner $next (- $remaining 1))
           (let $released (ic:release $next) (NumericTestFault MissingWinner))))))))
(= (numeric-forged $original $winner)
   (case (ic:prepare IC:NumericArchive $original () 64 ())
     (((IC:PreparedSearch (IC:SearchState $task $queue $best $statistics $first))
       (ic:resume (IC:SearchState $task $queue $winner $statistics $first) 0)))))
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("lib_ic_parameters.metta", "lib_ic_numeric.metta"):
        check_controls((ROOT / "lib/petta" / name).read_text())
    code = ['!(import! &self (library lib_ic_numeric))', HELPERS]
    expected = []

    def add(call, wanted):
        index = len(expected)
        code.append(f"!(println! (NumericCase {index} {call}))")
        expected.append(["NumericCase", str(index), wanted])

    inputs = list(map(float, range(8)))
    outputs = [2.0 * x + 3.0 for x in inputs]
    mask = [True] * len(inputs)
    original = archive(inputs, outputs, mask, 0.0)
    exact = archive(inputs, outputs, mask, 0.0, (2.0, 3.0))
    assert len(exact) < len(original)
    prepare = f"(ic:prepare-numeric {vector(inputs)} {vector(outputs)} {vector(mask, 'bool')} 0.0)"
    add(f"(case {prepare} (((IC:NumericArtifact $bytes) (tensor:inspect $bytes)) ($other $other)))",
        observation(original))
    add(f"(ic-parameters:prepare ({vector([2, -1, 2], 'i64')} {vector([0.5, -0.0])}))",
        ["IC:ParameterRegion", [[["IC:IntegerParameter", "2"], ["IC:IntegerParameter", "-1"],
                                  ["IC:IntegerParameter", "2"]],
                                 [["IC:FloatParameter", "0.5"], ["IC:FloatParameter", "-0.0"]]]])
    add(f"(collapse (ic-parameters:enumerate (((IC:IntegerParameter 1) (IC:IntegerParameter 1))"
        " ((IC:FloatParameter 0.0) (IC:FloatParameter 2.0)))))",
        [[["IC:IntegerParameter", "1"], ["IC:FloatParameter", str(b)]] for b in (0.0, 2.0, 0.0, 2.0)])

    cases = [
        (inputs, outputs, mask, 0.0, [("i64", [0, 1, 2]), ("f64", [0.0, 3.0, 5.0])]),
        (inputs, outputs, mask, 0.0, [("i64", [2, 2]), ("f64", [3.0, 3.0])]),
        (inputs, outputs, mask, 0.0, [("i64", []), ("f64", [3.0])]),
        (inputs, outputs, mask, 0.0, [("i64", [0, 1]), ("f64", [0.0, 4.0])]),
        (inputs, [999.0] + outputs[1:], [False] + mask[1:], 0.0,
         [("i64", [0, 2]), ("f64", [3.0])]),
        (inputs, outputs, [False] * len(mask), 0.0, [("i64", [1, 2]), ("f64", [0.0, 3.0])]),
        (inputs, [0.0] * len(inputs), mask, 0.5, [("i64", [0]), ("f64", [0.5, -0.5])]),
        (inputs, outputs, mask, 0.0, [("i64", [9007199254740993, 2]), ("f64", [3.0])]),
        ([], [], [], 0.0, [("i64", [2]), ("f64", [3.0])]),
        ([1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0], [5.0] * 8, mask, 0.0,
         [("f64", [1.0, 2.0]), ("i64", [3, 4])]),
        (inputs, outputs, mask, 0.0, [("f64", [1e308]), ("f64", [3.0])]),
        ([3.0] * 8, [6.0] * 8, mask, 1.0, [("i64", [0, 1, 2]), ("f64", [3.0, 4.0, 5.0])]),
    ]

    def fit_call(xs, ys, keep, tolerance, domains, pieces=False):
        raw = archive(xs, ys, keep, tolerance)
        region = "(" + " ".join(vector(values, kind) for kind, values in domains) + ")"
        if pieces:
            return f"""(case (ic:prepare-fit Numeric:Affine {byte_tensor(raw)} {region} ())
              (((IC:PreparedSearch $state) (numeric-result (numeric-pieces $state 1000))) ($other $other)))"""
        return f"(numeric-result (ic:fit Numeric:Affine {byte_tensor(raw)} {region} () 1000))"

    for values in cases:
        add(fit_call(*values), reference(*values))
    # Finite endpoints can have an overflowing difference. A failed numeric
    # comparison rejects the candidate; a masked coordinate needs no arithmetic.
    add("(ic-numeric:near 0.0 True 1.0e308 -1.0e308)", "False")
    add("(ic-numeric:near 0.0 True -1.0e308 1.0e308)", "False")
    add("(ic-numeric:near 0.0 False 1.0e308 -1.0e308)", "True")
    add("(ic-numeric:near 1.0 True 7.0 8.0)", "True")
    add(f'''(case (ic-parameters:prepare ({vector([0, 1, 2], 'i64')} {vector([3, 4, 5], 'i64')}))
      (((IC:ParameterRegion $choices)
        (let* (($h (work-new (ic-parameters:proposals $choices)))
                ($route (work-route $h fifo)) ($quantum (work-budget $h 100000))
                ($result (work-take $h 20)) ($released (work-cancel $h))) $result))))''',
        ["taken", [["IC:Proposal", ["IC:ParameterProposal", [["IC:IntegerParameter", str(a)],
                                                               ["IC:IntegerParameter", str(b)]]]]
                   for a, b in itertools.product(range(3), range(3, 6))], "complete"])
    add(fit_call(*cases[0], pieces=True), reference(*cases[0]))
    approximate = cases[6]
    raw_approx = archive(*approximate[:4])
    add(f"""(case (ic:prepare-fit Numeric:Affine {byte_tensor(raw_approx)}
        ({vector([0], 'i64')} {vector([0.5, -0.5])}) ())
        (((IC:PreparedSearch $state) (numeric-wait-winner $state 1000)) ($other $other)))""",
        ["ApproximateResume", "false", reference(*approximate)])
    add(f"""(case (ic:prepare-fit Numeric:Affine {byte_tensor(original)}
        ({vector([0, 2], 'i64')} {vector([3.0])}) ())
        (((IC:PreparedSearch $state)
          (case (ic:cancel $state)
            (((IC:SearchResult $best $stats $reason $fault $next)
              (CancelledFit $reason (numeric-result (ic:resume $next 1000)))))))))""",
        ["CancelledFit", "Cancelled", reference(inputs, outputs, mask, 0.0,
                                                 [("i64", [0, 2]), ("f64", [3.0])])])
    region = f"({vector([2], 'i64')} {vector([3.0])})"
    test_x = [0.5, 10.0, -2.0]
    add(f"(numeric-prediction (ic:fit Numeric:Affine {byte_tensor(original)} {region} () 1000) {vector(test_x)})",
        observation(ctn([4.0, 23.0, -1.0])))
    add(f"(ic:predict Numeric:Affine {byte_tensor(original)} {vector(test_x)})", ["IC:NumericFault", "NoModel"])
    add(f"(ic:predict Numeric:Affine {byte_tensor(exact)} {vector([1], 'i64')})", ["IC:NumericFault", "Inputs"])
    add(f"(numeric-forged {byte_tensor(original)} {byte_tensor(archive(inputs, outputs, mask, 0.0, (0.0, 0.0)))})",
        ["IC:ResumeRejected", "InvalidArtifact"])
    # This positive forged-state check only observes the refusal boundary,
    # not authority: the domain checker must independently validate the winner.
    add(f"(numeric-result (numeric-forged {byte_tensor(original)} {byte_tensor(exact)}))",
        ["NumericResult", observation(exact), "0", "0", "0", "RegionExhausted", "IC:NoFault"])

    drift_raw = archive(inputs, [0.0] * 8, mask, 1.0)
    drift_current = archive(inputs, [0.0] * 8, mask, 1.0, (0.0, 0.75))
    drift_wrong = archive(inputs, [0.0] * 8, mask, 1.0, (0.0, 1.5))
    add(f"(ic-packed:check IC:NumericArchive {byte_tensor(drift_raw)} {byte_tensor(drift_current)} {byte_tensor(drift_wrong)})",
        ["IC:PackedRejected", "Reconstruction"])
    for bad in ("(IC:IntegerParameter 2.5)", "(IC:IntegerParameter 9007199254740993)",
                "(IC:FloatParameter nope)", "(WrongParameter 2)"):
        add(f"(case (ic-parameters:float {bad}) (((IC:ParameterFault $why) Refused) ($other Unexpected)))", "Refused")
    for domains in ("()", f"({vector([2], 'u8')} {vector([3.0])})", "not-a-region"):
        add(f"(ic:prepare-fit Numeric:Affine {byte_tensor(original)} {domains} ())",
            ["IC:FitRefused", "ParameterDomains"])
    for args_text in (
        f"{vector(inputs)} {vector(outputs[:-1])} {vector(mask, 'bool')} 0.0",
        f"{vector(inputs)} {vector(outputs)} {vector(mask, 'bool')} -1.0",
        f"{vector(inputs)} {vector(outputs)} {vector(mask, 'bool')} NoPrecision",
        f"{vector(inputs)} {vector(outputs)} {vector(mask, 'bool')} 1.0e309",
        f"{vector(inputs)} {vector(outputs)} {vector([1] * 8, 'u8')} 0.0",
        f"(tensor:from-coo f64 (8) ((0 0.0))) {vector(outputs)} {vector(mask, 'bool')} 0.0",
    ):
        add(f"(ic:prepare-numeric {args_text})", ["IC:NumericFault", "InvalidObservation"])
    damaged = encoded(["Numeric:Archive", Text("NC1"), ctn(inputs), ctn([2] * 8, "u8"),
                       ctn([0.0]), ["Numeric:Affine", ctn([2.0, 3.0])]])
    add(f"(ic-numeric:observe {byte_tensor(damaged)})", "IC:None")
    add(f"(ic:predict Numeric:Affine {byte_tensor(damaged)} {vector(test_x)})", ["IC:NumericFault", "NoModel"])
    add(f"(ic-numeric:observe {byte_tensor(original + b'bad')})", "IC:None")

    code.append('''
(: IC-NumericImpostor Tensor)
(= (tensor:dtype IC-NumericImpostor) i64)
(= (tensor:shape IC-NumericImpostor) (1))
(= (tensor:entries IC-NumericImpostor)
   (TensorEntries (tensor:from-values index (1) (0)) (tensor:from-values i64 (1) (7))))
(= (tensor:all-finite IC-NumericImpostor) True)
(= (tensor:at IC-NumericImpostor $index) (tensor:present 7))
''')
    add("(ic-parameters:prepare (IC-NumericImpostor))", ["IC:ParameterFault", "InvalidDomains"])

    code.append('''
(= (ic-search:observation IC:GuardProbe $original)
   (let $written (add-atom (index-atom $original 0) DecoderRead) (IC:Some GuardObservation)))
''')
    add('''(let* (($counter (new-space)) ($source (new-space)))
      (case (ic:prepare IC:GuardProbe ($counter) ($source) 64 ())
        (((IC:PreparedSearch $state)
          (let* (($before (size-atom (collapse (match $counter DecoderRead Counted))))
                  ($changed (add-atom $source Changed))
                  ($result (ic:resume $state 0)))
            (GuardBeforeDecode $result $before
              (== $before (size-atom (collapse (match $counter DecoderRead Counted))))))))))''',
        ["GuardBeforeDecode", ["IC:ResumeRejected", "ChangedDependencies"], "1", "true"])
    add('''(let* (($counter (new-space)) ($source (new-space)))
      (case (ic:prepare IC:GuardProbe ($counter) ($source) 64 ())
        (((IC:PreparedSearch $state)
          (let* (($before (size-atom (collapse (match $counter DecoderRead Counted))))
                  ($result (ic:resume $state 0)))
            (case $result
              (((IC:SearchResult $best $stats $reason $fault $next)
                (DecoderCounterLive $before
                  (size-atom (collapse (match $counter DecoderRead Counted))) $reason)))))))))''',
        ["DecoderCounterLive", "1", "2", "RegionExhausted"])
    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture = args.output / "numeric.metta"
    fixture.write_text(program)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=90)
    (args.output / "numeric.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "NumericCase"]
    found = {f[1]: f for f in actual}
    wrong = [{"case": i, "expected": want, "actual": found.get(str(i))}
             for i, want in enumerate(expected) if normalized(found.get(str(i))) != normalized(want)]
    receipt = {"passed": run.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "exact_fit_bytes": [len(original), len(exact)],
               "scope": "finite affine grid, packed NC1 bytes, original-bound tolerance, masks, resume and held-out prediction"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [row["case"] for row in wrong]}))
    for row in wrong[:3]:
        print(json.dumps(row)[:2400])
    if run.returncode:
        print(run.stderr[-2000:])
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
