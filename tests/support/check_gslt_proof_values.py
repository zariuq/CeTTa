#!/usr/bin/env python3
"""Compose the state/proof adapter's actual empty-value rules with their consumers."""
import argparse
import copy
import os
from pathlib import Path
import subprocess
import sys

from check_transform_swi_differential import read_forms, write_form
from native_observation import read_observation

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "experiments/gslt2parse_foundation/presentations/core"


def slice_source(path, names):
    presentation, = read_forms(path.read_text())
    signature, = [item for item in presentation if isinstance(item, list) and item[0] == "signature"]
    rewrites, = [item for item in presentation if isinstance(item, list) and item[0] == "rewrites"]
    selected = [rule for rule in rewrites[1:] if rule[1] in names]
    assert {rule[1] for rule in selected} == set(names)
    # A slice retains the constructor declarations required by its selected
    # terms, including declarations supplied by the full package's imports.
    declared = {(item[1], item[2]) for item in signature[1:]}
    todo = [rule[2][1] for rule in selected] + [goal for rule in selected for goal in rule[3][1:]]
    while todo:
        node = todo.pop()
        if not isinstance(node, list) or not node:
            continue
        key = (node[0], str(len(node) - 1))
        if key not in declared:
            signature.append(["operator", *key])
            declared.add(key)
        todo.extend(node[1:])
    return ["gslt-presentation-v1", presentation[1] + "Boundary", signature,
            ["equations"], ["rewrites", *selected]]


def symbol(value):
    return ["symbol", value.encode().hex()]


def run(command):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"command exited {result.returncode}: {result.stderr}\n{result.stdout}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    compiler, runtime = args.compiler.resolve(), args.runtime.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    adapter = slice_source(CORE / "relational_state_proof_bridge_v1.metta",
                           ["state-proof-sequence-nil-v1", "state-proof-build-apartness-nil-v1"])
    consumer = slice_source(CORE / "certificate_gslt_trace_semantics_v1.metta",
                            ["proof-sequence-compare-nil-nil-v1", "proof-trace-frame-project-nil-v1"])
    fixture = read_forms("""
(gslt-presentation-v1 EmptyValuesProvider
  (signature (operator ValuesViewV1 2) (operator BoundaryValuesV1 0)
    (operator ValuesEmptyViewV1 0) (operator EntriesNilV1 0))
  (equations)
  (rewrites
    (rule empty-values (head (ValuesViewV1 BoundaryValuesV1 ValuesEmptyViewV1)) (body))))
""")[0]
    consumer_path, fixture_path = work / "consumer.metta", work / "provider.metta"
    consumer_path.write_text(write_form(consumer) + "\n")
    fixture_path.write_text(write_form(fixture) + "\n")
    query = work / "query.metta"
    query.write_text("""
!(gslt:entry:StateProofSequenceV1:110 owner BoundaryValuesV1)
!(gslt:entry:StateProofBuildApartnessV1:1110 owner assertion EntriesNilV1)
!(let (gslt:result:StateProofSequenceV1:110 $sequence)
    (gslt:entry:StateProofSequenceV1:110 owner BoundaryValuesV1)
    (gslt:entry:ProofSequenceCompareV1:110 $sequence $sequence))
!(let (gslt:result:StateProofBuildApartnessV1:1110 $apartness)
    (gslt:entry:StateProofBuildApartnessV1:1110 owner assertion EntriesNilV1)
    (let (gslt:result:ProofTraceFrameProjectV1:100 $apartness $apartness)
      (gslt:entry:ProofTraceFrameProjectV1:100 ProofTraceFrameNilV1)
      JoinedProofEmptyValuesV1))
""")
    expected = [
        [["expression", 2], symbol("gslt:result:StateProofSequenceV1:110"),
         ["expression", 1], symbol("ProofSequenceNilV1")],
        [["expression", 2], symbol("gslt:result:StateProofBuildApartnessV1:1110"),
         ["expression", 1], symbol("ProofAssertionListNilV1")],
        [["expression", 2], symbol("gslt:result:ProofSequenceCompareV1:110"), symbol("ProofSequenceEqualV1")],
        [symbol("JoinedProofEmptyValuesV1")],
    ]
    modes = ["StateProofSequenceV1:110", "StateProofBuildApartnessV1:1110",
             "ProofSequenceCompareV1:110", "ProofTraceFrameProjectV1:100"]
    for closed in (False, True):
        for mutation in (None, "ProofSequenceNilV1", "ProofAssertionListNilV1"):
            source = copy.deepcopy(adapter)
            if mutation:
                changed = 0
                for rule in source[-1][1:]:
                    head = rule[2][1]
                    if head[-1] == [mutation]:
                        head[-1] = mutation
                        changed += 1
                assert changed == 1
            label = f"{'closed' if closed else 'generic'}-{mutation or 'canonical'}"
            adapter_path, program, receipt = [work / (label + suffix)
                                               for suffix in (".source.metta", ".metta", ".jsonl")]
            adapter_path.write_text(write_form(source) + "\n")
            command = [str(compiler), "petta-direct"]
            for path in (adapter_path, consumer_path, fixture_path):
                command += ["--source", str(path)]
            for mode in modes:
                command += ["--closed-entry-mode" if closed else "--entry-mode", mode]
            run(command + ["--out", str(program)])
            command = [str(runtime), "--run-observation", str(receipt)]
            arguments = ["--lang", "petta", str(program), str(query)]
            directory = os.environ.get("CETTA_GSLT_RECORD_DIR")
            if directory:
                command = [sys.executable, str(ROOT / "tests/support/gslt_invocation.py"),
                           "record", directory, "3", *command]
            run(command + arguments)
            observation = read_observation(receipt)
            assert len(observation["queries"]) == len(expected)
            actual = []
            for item in observation["queries"].values():
                assert item["terminal"]["stop"] == 0 and not item["effects"]
                actual.append(item["answers"])
            assert observation["final"]["contract_exit"] == 0 and not observation["outside_effects"]
            wanted = [[value] for value in expected]
            if mutation:
                changed = 0 if mutation == "ProofSequenceNilV1" else 1
                wanted[changed] = [expected[changed][:2] + [symbol(mutation)]]
                wanted[2 if changed == 0 else 3] = []
            assert actual == wanted, (label, actual)
            if mutation is None:
                print(f"PASS: {label} adapter and proof consumers agree on constructor values")
            else:
                print(f"PASS: {label} scalar/constructor mutation is detected")
    print("Proof value boundary: 8 positive observations and 4 detected source mutations")


if __name__ == "__main__":
    main()
