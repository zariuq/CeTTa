#!/usr/bin/env python3
"""Check module lookup and registration against identified references.

The default profiles retain Opt's namespace-upsert registration extension;
he-compat follows the reference's refusal of a repeated registration. Both
contracts are recorded, including effects and query boundaries. The existing
default-profile rebinding fixture is compared directly with pinned Opt.
"""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/he/module_operations_v1"
CASES = ("top", "register_file", "register_directory", "ordered_failure",
         "ordered_success", "own_storage", "own_storage_invalid",
         "own_targeted_include", "own_include_alias_values",
         "own_include_alias_top", "include_frontiers", "include_errors",
         "include_context", "runner_document", "runner_context", "runner_resources",
         "runner_exports", "named_parent_scope", "name_tree",
         "runner_stops_at_error", "import_destination", "include_fault_prefix",
         "export_names", "failed_import_reason", "registered_python",
         "include_error_before_fault", "registered_nested",
         "registered_file_child", "include_python", "bind_name")
sys.path.insert(0, str(ROOT / "scripts"))
from metta_result_observation import output_observation


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return {"sha256": sha256(data).hexdigest(), "bytes": len(data)}


def reference_worker(name: str) -> None:
    import hyperon
    import hyperonpy
    from hyperon import Environment, MeTTa

    if name == "identity":
        print(json.dumps({"python": identity(Path(sys.executable)),
                          "package": identity(Path(hyperon.__file__)),
                          "extension": identity(Path(hyperonpy.__file__))}))
        return
    runner = MeTTa(env_builder=Environment.custom_env(working_dir=str(FIXTURE)))
    for answers in runner.run((FIXTURE / f"{name}.metta").read_text()):
        print("[" + ", ".join(map(str, answers)) + "]", flush=True)


def run(command: list[str]) -> str:
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", PYTHONUNBUFFERED="1")
    environment.pop("PYTHONPATH", None)
    process = subprocess.Popen(command, cwd=ROOT, env=environment,
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
    if process.returncode or stderr:
        raise RuntimeError(f"Unexpected process failure: {process.returncode}\n{stderr}\n{stdout}")
    return stdout


def profile_expectation(name: str, reference: str, profile: str) -> str:
    if name in ("include_frontiers", "include_errors", "include_context",
                "include_fault_prefix", "failed_import_reason",
                "include_error_before_fault") and profile != "he-compat":
        # Default profiles retain Opt's documented Unit/once include policy.
        return (FIXTURE / f"{name}.default.expected").read_text()
    if name == "own_targeted_include" and profile != "he-compat":
        # Targeted include is Opt's existing default-profile extension.
        # The own-storage alias must retain write-through and rollback.
        return (FIXTURE / f"{name}.default.expected").read_text()
    if profile == "he-compat" or name not in ("register_file", "register_directory"):
        return reference
    lines = reference.splitlines()
    answer_positions = [i for i, line in enumerate(lines) if line.startswith("[")]
    # Opt permits namespace upserts in its default profiles. Registration
    # reuses the loaded identity, without running its initializer again.
    repeated_query = {"register_file": 6, "register_directory": 4}[name]
    index = answer_positions[repeated_query]
    assert lines[index] == "[refused]", (name, lines)
    lines[index] = "[registered]"
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--reference-python", action="append", required=True, type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--case", action="append", choices=CASES,
                        help="Select a recorded package for a frozen executable; default is all")
    args = parser.parse_args()
    binary = args.binary.resolve()
    baseline = args.baseline.resolve()
    references = []
    records = []
    for reference_python in args.reference_python:
        # Preserve a venv's executable path: resolving its symlink selects the
        # base interpreter and silently changes which Hyperon is imported.
        command = [str(reference_python.absolute()), "-I", str(Path(__file__).resolve()),
                   "--reference"]
        references.append(json.loads(run(command + ["identity"])))
        for name in args.case or CASES:
            reference = run(command + [name])
            assert output_observation(reference) == output_observation(
                (FIXTURE / f"{name}.expected").read_text()), (name, reference)
            for profile in ("he", "he-compat", "extended"):
                actual = run([str(binary), "--lang", "he", "--profile", profile,
                              str(FIXTURE / f"{name}.metta")])
                expected = profile_expectation(name, reference, profile)
                agrees = output_observation(actual) == output_observation(expected)
                records.append({"case": name, "profile": profile,
                                "reference_identity": references[-1],
                                "reference": reference, "expected": expected,
                                "actual": actual, "agrees": agrees})
                print(f"{'PASS' if agrees else 'FAIL'} {name} {profile}")
    for profile in ("he", "extended"):
        for name in ("include_frontiers", "include_errors"):
            if args.case and name not in args.case:
                continue
            # These controls isolate the existing default include contract:
            # no new module-space operation or context-identity repair.
            command = ["--lang", "he", "--profile", profile,
                       str(FIXTURE / f"{name}.metta")]
            opt_output = run([str(baseline), *command])
            expected = (FIXTURE / f"{name}.default.expected").read_text()
            assert output_observation(opt_output) == output_observation(expected), opt_output
            actual = run([str(binary), *command])
            agrees = output_observation(actual) == output_observation(expected)
            records.append({"case": f"existing-{name}", "profile": profile,
                            "baseline": opt_output, "expected": expected,
                            "actual": actual, "agrees": agrees})
            print(f"{'PASS' if agrees else 'FAIL'} existing-{name} {profile}")
        command = ["--lang", "he", "--profile", profile,
                   str(ROOT / "tests/test_register_module_rebind.metta")]
        expected = run([str(baseline), *command])
        actual = run([str(binary), *command])
        agrees = output_observation(actual) == output_observation(expected)
        records.append({"case": "existing-directory-rebinding", "profile": profile,
                        "expected": expected, "actual": actual, "agrees": agrees})
        print(f"{'PASS' if agrees else 'FAIL'} existing-directory-rebinding {profile}")
        if args.case and "own_targeted_include" not in args.case:
            continue
        # The existing targeted-include contract is independently observed
        # without the new own-storage operation. Both CLIs print the retained
        # query results after execution, while effects occur during execution.
        command = ["--lang", "he", "--profile", profile,
                   str(FIXTURE / "own_targeted_include_baseline.metta")]
        opt_output = run([str(baseline), *command])
        fixture_expected = (FIXTURE / "own_targeted_include.default.expected").read_text()
        # The qualified failure-prefix repair deliberately preserves the
        # completed print that Opt's atomic file parser suppressed.
        opt_expected = fixture_expected.replace("OWN-TARGET-PREFIX\n", "")
        assert output_observation(opt_output) == output_observation(opt_expected), opt_output
        actual = run([str(binary), *command])
        agrees = output_observation(actual) == output_observation(fixture_expected)
        records.append({"case": "existing-targeted-include", "profile": profile,
                        "baseline": opt_output, "expected": fixture_expected,
                        "named_repair": "completed effects before a parse failure",
                        "actual": actual, "agrees": agrees})
        print(f"{'PASS' if agrees else 'FAIL'} existing-targeted-include {profile}")
    failures = sum(not row["agrees"] for row in records)
    receipt = {"format": "he-module-operations-v1", "binary": identity(binary),
               "baseline": identity(baseline), "references": references,
               "records": records, "failures": failures,
               "selected_cases": args.case,
               "driver_sha256": identity(Path(__file__).resolve())["sha256"],
               "fixtures_sha256": {str(path.relative_to(FIXTURE)): identity(path)["sha256"]
                                   for path in sorted(FIXTURE.rglob("*")) if path.is_file()}}
    if args.receipt:
        with args.receipt.open("x") as stream:
            json.dump(receipt, stream, indent=2, sort_keys=True)
            stream.write("\n")
    print(f"{len(records) - failures}/{len(records)} module operation controls agree")
    return bool(failures)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--reference":
        reference_worker(sys.argv[2])
    else:
        raise SystemExit(main())
