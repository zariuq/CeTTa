#!/usr/bin/env python3
"""Differential authority and failed-initialization controls for he-compat.

Requires an existing upstream Python installation; never builds a reference.
Each fixture is one complete run in both engines.

HE 0.2.10 leaves a module whose code failed stuck for the rest of the run,
together with the modules it had loaded; the repaired branch forgets the failed
attempt and runs the module again. The expectations of the probes in REPAIRED
are the repaired branch's. Run against the release, its different observation
of those probes is kept in the receipt as a record of the defect.
Both interfaces print answers after execution. Query boundaries and explicit
effects remain ordered; each query's answers form a bag with multiplicity.
Their output interleaving is not claimed to be evaluation order.
Error messages are observed through MeTTa case expressions; unexpected process
failures are not classified as semantic mismatches. A mismatch fails the gate.

CeTTa-only native-library authority is checked separately against the lexical
binding contract in every HE profile that provides these libraries. It is not
an upstream Python reference observation: imported bindings and wrappers execute,
but previously parsed child and Python-constructed symbols stay data.

A malformed token pattern registered by a Python module has no reference
verdict either: the reference aborts the whole process. CeTTa's settled policy
is checked instead: the import fails with an error atom and no token of that
module is published.
"""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from metta_result_observation import output_observation


FIXTURE = Path(__file__).resolve().parents[1] / "tests/he/module_compatibility_v1"
REPAIRED = ("P6", "P7", "P9")
CASES = {
    "P1": "parsed_operations",
    "P2": "constructed_effect",
    "P3": "constructed_arithmetic",
    "P4": "constructed_equation",
    "P5": "parse_scope",
    "P6": "repeated_failed_import",
    "P7": "repeated_python_failure",
    "P8": "first_failed_import",
    "P9": "nested_success_failed_parent",
    "P10": "profile_operations",
    "P13": "intrinsic_operation_types",
    "P14": "own_storage_view",
    "P15": "own_storage_view_invalid",
    "P16": "closed_operation_equation_heads",
    "P17": "operation_result_fields",
    "P18": "registered_directory",
}


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return {"sha256": sha256(data).hexdigest(), "bytes": len(data),
            "regex_syntax_versions": sorted({v.decode() for v in re.findall(
                rb"regex-syntax-([0-9]+\.[0-9]+\.[0-9]+)", data)})}


def reference_worker(name: str, separate_calls: bool = False) -> None:
    import hyperon
    import hyperonpy
    from hyperon import Environment, MeTTa

    if name == "identity":
        print(json.dumps({"python": identity(Path(sys.executable)),
                          "package": identity(Path(hyperon.__file__)),
                          "extension": identity(Path(hyperonpy.__file__))}))
        return
    runner = MeTTa(env_builder=Environment.custom_env(working_dir=str(FIXTURE)))
    source = (FIXTURE / (name + ".metta")).read_text()
    results = []
    for program in source.splitlines() if separate_calls else [source]:
        results.extend(runner.run(program))
    for answers in results:
        print("[" + ", ".join(map(str, answers)) + "]", flush=True)


def run(command: list[str], environment: dict) -> str:
    process = subprocess.Popen(command, cwd=FIXTURE, env=environment,
                               text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, start_new_session=True)
    try:
        stdout, stderr = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.communicate()
        raise RuntimeError("Diagnostic timeout; no language result was inferred")
    result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
    if result.returncode != 0 or result.stderr:
        raise RuntimeError(f"Unexpected engine failure ({result.returncode}): "
                           f"{result.stderr}\n{result.stdout}")
    return result.stdout


def scoped_observation(probe: str, output: str, scope: dict | None):
    observed = output_observation(output)
    if scope:
        entries = scope["deferred_observations"].get(probe, [])
        if isinstance(entries, list):
            queries = [line for line in (FIXTURE / (CASES[probe] + ".metta"))
                       .read_text().splitlines() if line.startswith("!")]
            skipped = {entry["query"] for entry in entries}
            for entry in entries:
                assert queries[entry["query"]] == entry["source"], entry
            retained = []
            query = 0
            for kind, value in observed:
                if kind != "answers" or query not in skipped:
                    retained.append((kind, value))
                query += kind == "answers"
            assert query == len(queries), (probe, query, len(queries))
            return retained
    return observed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--reference-python", type=Path, required=True)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--module-only", action="store_true",
                        help="Use the explicit module/deferred-observation manifest")
    args = parser.parse_args()
    binary = args.binary.resolve()
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    environment.pop("PYTHONPATH", None)
    reference = [str(args.reference_python.absolute()), str(Path(__file__).resolve()), "--reference"]
    reference_identity = json.loads(run(reference + ["identity"], environment))
    records = []
    scope = json.loads((FIXTURE / "scope.json").read_text())
    for probe, name in CASES.items():
        if args.module_only and probe not in scope["module_probes"]:
            continue
        expected = run(reference + [name], environment)
        stored = (FIXTURE / (name + ".expected")).read_text()
        release_defect = None
        if output_observation(expected) != output_observation(stored):
            if probe not in REPAIRED:
                raise RuntimeError(f"Reference drift in {probe}: {expected!r}")
            release_defect = expected
            expected = stored
        for route in ("0", "1"):
            env = dict(environment, CETTA_OPEN_EQUATIONS_REFERENCE=route)
            actual = run([str(binary), "--lang", "he", "--profile", "he-compat",
                          str(FIXTURE / (name + ".metta"))], env)
            record = {"probe": probe, "fixture": name, "equation_reference": route,
                      "reference": expected, "release_defect": release_defect,
                      "actual": actual,
                      "agrees": scoped_observation(probe, expected, scope if args.module_only else None) ==
                                scoped_observation(probe, actual, scope if args.module_only else None)}
            records.append(record)
            print(f"{'PASS' if record['agrees'] else 'FAIL'} {probe} {name} route={route}")
    policy = []
    lexical_authority = (FIXTURE / "ambient_native_authority.policy.expected").read_text()
    for profile in (() if args.module_only else ("he", "he-compat", "extended", "he-prime")):
        for route in ("0", "1"):
            actual = run([str(binary), "--lang", "he", "--profile", profile,
                          str(FIXTURE / "ambient_native_authority.metta")],
                         dict(environment, CETTA_OPEN_EQUATIONS_REFERENCE=route))
            agrees = output_observation(actual) == output_observation(lexical_authority)
            policy.append({"probe": "P11", "profile": profile,
                           "equation_reference": route, "actual": actual,
                           "expected_policy": lexical_authority, "agrees": agrees,
                           "classification": "Only imported provider operation bindings execute; "
                                             "previously parsed and constructed symbols stay data"})
            print(f"{'PASS' if agrees else 'FAIL'} P11 lexical authority {profile} route={route} (policy)")
    invalid_registration = (FIXTURE / "invalid_registration.policy.expected").read_text()
    for route in (() if args.module_only else ("0", "1")):
        actual = run([str(binary), "--lang", "he", "--profile", "he-compat",
                      str(FIXTURE / "invalid_registration.metta")],
                     dict(environment, CETTA_OPEN_EQUATIONS_REFERENCE=route))
        agrees = output_observation(actual) == output_observation(invalid_registration)
        policy.append({"probe": "P12", "equation_reference": route, "actual": actual,
                       "expected_policy": invalid_registration, "agrees": agrees,
                       "classification": "The reference aborts on a malformed Python token pattern; "
                                         "CeTTa refuses the import with an error atom and publishes no token"})
        print(f"{'PASS' if agrees else 'FAIL'} P12 invalid_registration route={route} (policy)")
    failures = (sum(not item["agrees"] for item in records) +
                sum(not item.get("agrees", True) for item in policy))
    separate_calls = run([*reference[:-1], "--reference-separate-calls", CASES["P6"]], environment)
    whole_run = next(row["reference"] for row in records if row["probe"] == "P6")
    assert whole_run.count("initialization-effect\n") == 3
    assert separate_calls.count("initialization-effect\n") == 3
    receipt = {"format": "he-module-compatibility-v1", "native_identity": identity(binary),
               "scope": scope if args.module_only else "complete archived panel",
               "reference_identity": reference_identity, "records": records,
               "policy_observations": policy, "failures": failures,
               "answer_observation": "per-query bags; explicit effects and query boundaries unchanged",
               "reference_run_boundary_control": {
                   "one_run": whole_run, "separate_run_calls": separate_calls,
                   "meaning": "Every import of a failed module is a new attempt, "
                              "within one run and across runs."},
               "fixture_sha256": {str(p.relative_to(FIXTURE)): sha256(p.read_bytes()).hexdigest()
                                  for p in sorted(FIXTURE.rglob("*"))
                                  if p.is_file() and "__pycache__" not in p.parts},
               "driver_sha256": sha256(Path(__file__).read_bytes()).hexdigest(),
               "limits": ["No physical-memory or concurrency claim.",
                          "Error payload wording is hidden by explicit case expressions.",
                          "Answers are printed after evaluation; no answer/effect interleaving claim.",
                          "The Python reference is identified independently of any neighboring checkout."]}
    if args.receipt:
        with args.receipt.open("x") as stream:
            json.dump(receipt, stream, indent=2, sort_keys=True)
            stream.write("\n")
    print(f"{sum(row['agrees'] for row in records)}/{len(records)} compatibility comparisons agree; "
          f"{sum(row['agrees'] for row in policy)}/{len(policy)} policy controls pass")
    raise SystemExit(bool(failures))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] in ("--reference", "--reference-separate-calls"):
        reference_worker(sys.argv[2], sys.argv[1] == "--reference-separate-calls")
    else:
        main()
