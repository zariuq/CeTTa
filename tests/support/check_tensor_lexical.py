#!/usr/bin/env python3
"""Packed lexical callbacks: admission, whole-map fallback and source currency."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, parse_forms, tensor as byte_tensor
from check_tensor_program import array, transport


def vector(values, dtype="f64"):
    return f"(tensor:from-values {dtype} ({len(values)}) ({' '.join(values)}))"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    unary = vector(["1.0", "-0.0", "3.0"])
    integers = vector(["4", "5", "6"], "i64")
    positives = [
        (f"({unary})", "(|-> ($x) (* $x $x))", "()", vector(["1.0", "0.0", "9.0"])),
        (f"({unary} {integers})", "(|-> ($x $y) (+ $x $y))", "()", vector(["5.0", "5.0", "9.0"])),
        (f"({unary})", "(|-> ($factor $x) (* $factor $x))", "(2.5)", vector(["2.5", "-0.0", "7.5"])),
        (f"({unary})", "(let $scale 2.5 (|-> ($x) (* $scale $x)))", "()", vector(["2.5", "-0.0", "7.5"])),
        (f"({unary})", "(|-> ($x) (if (> $x 0.0) $x 0.0))", "()", vector(["1.0", "0.0", "3.0"])),
        ("((tensor:from-values f64 (0) ()))", "(|-> ($x) (* $x 2.0))", "()", vector([])),
        ("((tensor:from-values f64 () (3.0)))", "(|-> ($x) (* $x 2.0))", "()", "(tensor:from-values f64 () (6.0))"),
        ("((tensor:from-coo f64 (4) ((1 -0.0) (3 7.0))))", "(|-> ($x) (* $x 2.0))", "()",
         "(tensor:from-coo f64 (4) ((1 -0.0) (3 14.0)))"),
        ("((tensor:view (tensor:from-values f64 (3) (1.0 2.0 3.0)) (3) (-1) 2))",
         "(|-> ($x) (* $x 2.0))", "()", vector(["6.0", "4.0", "2.0"])),
        ("((tensor:from-values index (2) (0 18446744073709551615)))",
         "(|-> ($x) (if (== $x 0) 0.0 1.0))", "()", vector(["0.0", "1.0"])),
    ]
    negatives = [
        (f"({unary})", '(|-> ($x) (let $_ (println! "SpeculativeLexicalEffect") $x))', "()"),
        (f"({unary})", "(|-> ($x) (superpose ($x 7.0)))", "()"),
        (f"({unary})", "(|-> ($x) (superpose ()))", "()"),
        (f"({unary})", "(|-> ($x $y) (+ $x $y))", "()"),
        (f"({unary} {unary} {unary})", "(|-> ($x $y $z) (+ (+ $x $y) $z))", "()"),
        (f"({unary})", "(quote (Lam (identity 1) (idx 0)))", "()"),
        (f"({unary})", "(|-> ($x) (Data $x))", "()"),
        (f"({vector(['1.0','0.0'])})", '(|-> ($x) (if (> $x 0.0) $x "not-a-number"))', "()"),
        (f"({unary})", "(let $resource (new-space) (|-> ($x) $resource))", "()"),
        (f"({vector(['1.0','0.0'])})", "(|-> ($x) (/ 1.0 $x))", "()"),
    ]
    prefix = ['!(import! &self (library tensor))']
    prefix += ['(= (lexical-test:look $x) (+ $x 1.0))', '''
(= (lexical-test:admitted $inputs $projection $captures $expected)
   (case (__cetta_lib_tensor_try_zip_map f64 $inputs $projection $captures)
     (((Tensor:Mapped $value) (Native (== $value $expected))) ($other $other))))
''']
    command = [str(args.cetta.resolve()), "--lang", "petta"]

    # The scalar service declares the floating exception policy. Qualify
    # each supported policy, not equal failures or an arbitrary new outcome.
    probe_source = '!(and (isinf-math (/ 1.0 0.0)) (> (/ 1.0 0.0) 0.0))\n'
    probe = subprocess.run(command + ['-e', probe_source], cwd=ROOT,
                           text=True, capture_output=True, timeout=60)
    (args.output / 'float-policy.log').write_text(probe.stdout + probe.stderr)
    if probe.returncode == 0 and probe.stdout == 'true\n' and not probe.stderr:
        float_policy = 'IEEE-positive-infinity'
    elif probe.returncode == 2 and 'evaluation_error zero_divisor' in probe.stderr:
        float_policy = 'raised-zero-divisor'
    else:
        raise AssertionError(('unsupported scalar exception policy', probe.returncode,
                              probe.stdout, probe.stderr))
    infinity = '(tensor:decode-bytes ' + byte_tensor(transport(array((2,), [1.0, float('inf')]))) + ')'

    def run(name, lines, expected, reference="0", fuel=False):
        source = "\n".join(prefix + lines) + "\n"
        path = args.output / (name + ".metta")
        path.write_text(source)
        env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference)
        process = subprocess.run(command + (["--fuel", "10000000"] if fuel else []) + [str(path.resolve())],
                                 cwd=ROOT, env=env, text=True, capture_output=True, timeout=60)
        (args.output / (name + ".log")).write_text(process.stdout + process.stderr)
        actual = [form for form in parse_forms(process.stdout)
                  if isinstance(form, list) and form and form[0] == "LexicalCase"]
        passed = process.returncode == 0 and actual == expected and "SpeculativeLexicalEffect" not in process.stdout
        return {"name": name, "passed": passed, "checks": len(expected), "answers": len(actual),
                "mismatches": [i for i, pair in enumerate(zip(actual, expected)) if pair[0] != pair[1]],
                "observations": actual if not passed else None, "exit": process.returncode}

    def run_raised(name, line, reference="0"):
        source = "\n".join(prefix + [line]) + "\n"
        path = args.output / (name + ".metta")
        path.write_text(source)
        env = dict(os.environ, CETTA_TENSOR_MAP_REFERENCE=reference)
        process = subprocess.run(command + [str(path.resolve())], cwd=ROOT, env=env,
                                 text=True, capture_output=True, timeout=60)
        diagnostic = process.stdout + process.stderr
        (args.output / (name + ".log")).write_text(diagnostic)
        passed = (process.returncode == 2 and "evaluation_error zero_divisor" in diagnostic and
                  "SpeculativeLexicalEffect" not in diagnostic)
        return {"name": name, "passed": passed, "checks": 2,
                "answers": 0, "mismatches": [],
                "observations": None if passed else diagnostic,
                "exit": process.returncode}

    native, expected_native, ordinary, expected_ordinary = [], [], [], []
    for i, (inputs, projection, captures, expected) in enumerate(positives):
        native.append(f"!(println! (LexicalCase {i} (lexical-test:admitted {inputs} {projection} {captures} {expected})))")
        # The existing compiler conservatively declines this free captured
        # frame. The authored callable still works; no capture authority is
        # inferred from the fact that the eventual value is numeric.
        expected_native.append(["LexicalCase", str(i), "Tensor:Fallback" if i == 3 else ["Native", "true"]])
        ordinary.append(f"!(println! (LexicalCase {i} (== (tensor:zip-map f64 {inputs} {projection} {captures}) {expected})))")
        expected_ordinary.append(["LexicalCase", str(i), "true"])
    for i, (inputs, projection, captures) in enumerate(negatives, len(positives)):
        if i == len(positives) + len(negatives) - 1 and float_policy == 'IEEE-positive-infinity':
            native.append(f"!(println! (LexicalCase {i} (lexical-test:admitted {inputs} {projection} {captures} {infinity})))")
            expected_native.append(['LexicalCase', str(i), ['Native', 'true']])
        else:
            native.append(f"!(println! (LexicalCase {i} (__cetta_lib_tensor_try_zip_map f64 {inputs} {projection} {captures})))")
            expected_native.append(["LexicalCase", str(i), "Tensor:Fallback"])
    # A dependency changes after the closure value was created. A new
    # operation must inspect the current source rather than a stale body.
    next_case = len(expected_native)
    native += [f'''!(let $callback (|-> ($x) (lexical-test:look $x))
      (let* (($first (lexical-test:admitted ({unary}) $callback () {vector(['2.0','1.0','4.0'])}))
             ($changed (add-atom &self (= (lexical-test:look $n) (* $n 2.0))))
             ($second (__cetta_lib_tensor_try_zip_map f64 ({unary}) $callback ())))
        (println! (LexicalCase {next_case} (Currency $first $second)))))''']
    expected_native.append(["LexicalCase", str(next_case), ["Currency", ["Native", "true"], "Tensor:Fallback"]])
    results = [run("native", native, expected_native),
               run("ordinary-native", ordinary, expected_ordinary),
               run("ordinary-reference", ordinary, expected_ordinary, reference="1")]
    exceptional_inputs = f"({vector(['1.0','0.0'])})"
    exceptional_projection = "(|-> ($x) (/ 1.0 $x))"
    if float_policy == 'raised-zero-divisor':
        results += [
            run_raised("ordinary-native-raised",
                f"!(tensor:zip-map f64 {exceptional_inputs} {exceptional_projection} ())"),
            run_raised("ordinary-reference-raised",
                f"!(tensor:zip-map f64 {exceptional_inputs} {exceptional_projection} ())", reference="1"),
        ]
    else:
        ieee = [f'!(println! (LexicalCase 0 '
                f'(== (tensor:zip-map f64 {exceptional_inputs} {exceptional_projection} ()) {infinity})))']
        results += [run('ordinary-native-infinity', ieee, [['LexicalCase', '0', 'true']]),
                    run('ordinary-reference-infinity', ieee, [['LexicalCase', '0', 'true']], reference='1')]
    # This is one admission control, not a configuration sweep: bounded
    # semantic fuel must retain the authored execution route.
    bounded = [f"!(println! (LexicalCase 0 (lexical-test:admitted {positives[0][0]} "
               f"{positives[0][1]} () {positives[0][3]})))"]
    results.append(run("bounded-admission", bounded, [["LexicalCase", "0", "Tensor:Fallback"]], fuel=True))
    receipt = {"passed": all(result["passed"] for result in results), "runs": results,
               "floating_exception_policy": float_policy,
               "checks": sum(result["checks"] for result in results),
               "scope": "unary/binary lexical packed mapping through the shared pure-expression compiler"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
