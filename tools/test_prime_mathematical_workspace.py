#!/usr/bin/env python3
"""Check mathematical computation, scoped publication and existing consumers."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys


FIXTURES = (
    "examples/prime/first_verified_program",
    "examples/prime/predicate_types",
    "examples/prime/predicate_guards",
    "examples/prime/standard_api",
    "tests/prime/routes/standard_api_editing",
    "tests/prime/routes/standard_api_contracts",
    "tests/prime/routes/standard_api_revisions",
    "tests/prime/routes/standard_api_control",
    "tests/prime/scoped/standard_api_classifier_budget",
    "examples/prime/mathematical_workspace",
    "examples/prime/admitted_top_k",
    "examples/prime/admitted_top_k_properties",
    "examples/prime/mathematical_routes",
    "tests/prime/scoped/definition_normalization",
    "tests/prime/scoped/admitted_execution",
    "tests/prime/scoped/admitted_scope_policies",
    "tests/prime/scoped/admitted_call_composition",
    "tests/prime/scoped/open_mathematical_code",
    "tests/prime/scoped/checked_case_definitions",
    "tests/prime/scoped/map_fusion_dtt",
    "tests/prime/scoped/certified_family_shortcut",
    "tests/prime/scoped/native_dependent_search",
    "tests/prime/scoped/definition_admission",
    "tests/prime/scoped/definition_revision_closure",
    "tests/prime/scoped/typing_dependency_currentness",
    "tests/prime/scoped/level_least_instance",
    "tests/prime/scoped/level_schema_synthesis",
    "tests/prime/scoped/level_instance_from_all_uses",
    "tests/prime/scoped/imported_proof_replay",
    "tests/prime/scoped/four_faces",
    "tests/prime/conformance/type_judgments",
    "tests/prime/conformance/regular_kernel_constructors",
    "tests/prime/conformance/regular_declarations",
    "tests/prime/profiles/megalodon_hotg/scoped/native_proof_normalization",
)

FIXTURE_OPTIONS = {
    "tests/prime/scoped/standard_api_classifier_budget": (["--fuel", "50"], 1),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = args.binary.resolve()
    libraries = ("mathematical_workspace.metta", "natural_top_k.metta", "natural_top_k_properties.metta")
    binary_digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    library_digests = {name: hashlib.sha256((root / "lib/prime" / name).read_bytes()).hexdigest()
                       for name in libraries}
    failed = []
    observations = []
    for name in FIXTURES:
        flags, expected_exit = FIXTURE_OPTIONS.get(name, ([], 0))
        source_digest = hashlib.sha256((root / (name + ".metta")).read_bytes()).hexdigest()
        expected = (root / (name + ".expected")).read_text()
        result = subprocess.run(
            [str(binary), "--lang", "prime", *flags, str(root / (name + ".metta"))],
            cwd=root, text=True, capture_output=True, timeout=args.timeout,
        )
        source_current = source_digest == hashlib.sha256((root / (name + ".metta")).read_bytes()).hexdigest()
        expected_current = expected == (root / (name + ".expected")).read_text()
        observations.append({
            "fixture": name,
            "source_sha256": source_digest,
            "expected_sha256": hashlib.sha256(expected.encode()).hexdigest(),
            "actual_sha256": hashlib.sha256(result.stdout.encode()).hexdigest(),
            "exit_code": result.returncode,
            "expected_exit_code": expected_exit,
            "arguments": flags,
            "stderr": result.stderr,
            "inputs_current": source_current and expected_current,
            "passed": source_current and expected_current and result.returncode == expected_exit and not result.stderr and result.stdout == expected,
        })
        if not observations[-1]["passed"]:
            failed.append(name)
            print(f"FAIL {name}: exit {result.returncode}", file=sys.stderr)
            print(result.stderr or result.stdout, file=sys.stderr)
        else:
            print(f"PASS {name}")
    print(f"Mathematical workspace: {len(FIXTURES) - len(failed)}/{len(FIXTURES)}")
    inputs_current = (binary_digest == hashlib.sha256(binary.read_bytes()).hexdigest() and
        all(digest == hashlib.sha256((root / "lib/prime" / name).read_bytes()).hexdigest()
            for name, digest in library_digests.items()))
    if not inputs_current:
        print("FAIL: binary or library changed during qualification", file=sys.stderr)
    if args.report:
        report = {
            "schema": "prime-mathematical-workspace-v1",
            "checked_at": datetime.now(timezone.utc).isoformat(),
            "binary": binary.name,
            "binary_sha256": binary_digest,
            "libraries": library_digests,
            "inputs_current": inputs_current,
            "passed": inputs_current and not failed,
            "observations": observations,
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    return bool(failed) or not inputs_current


if __name__ == "__main__":
    sys.exit(main())
