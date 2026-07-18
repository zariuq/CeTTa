#!/usr/bin/env python3
"""Validate the executable Prime coverage crosswalk."""

from __future__ import annotations

import csv
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
COVERAGE = ROOT / "tests/prime/coverage.tsv"

EXPECTED_SUITES = {f"B{i:02d}-{name}" for i, name in enumerate((
    "CURRENT", "JUDGMENTS", "BINDING", "UNIVERSES", "PI-SIGMA-ID",
    "INDUCTIVES", "GUARDED", "CONVERSION", "EQUALITY", "GSLT-REFLECTION",
    "EFFECTS", "GRADUAL", "RESULTS", "NONDETERMINISM", "SEARCH",
    "EVIDENCE-GUIDANCE", "GRADES-MODES", "PROVENANCE", "DIAGNOSTICS",
    "OPTIMIZATION", "SPACES", "CONCURRENCY", "HOTG", "CROSS-FOUNDATION",
    "MIGRATION",
))}
EXPECTED_FEATURES = (
    {f"K{i:02d}" for i in range(1, 13)}
    | {f"R{i:02d}" for i in range(1, 16)}
    | {f"S{i:02d}" for i in range(1, 7)}
    | {f"G{i:02d}" for i in range(1, 6)}
)
EXPECTED_CASES = (
    {f"MIG-{i:03d}" for i in range(1, 6)}
    | {f"PKG-{i:03d}" for i in range(1, 7)}
    | {f"BND-{i:03d}" for i in range(1, 9)}
    | {f"CNV-{i:03d}" for i in range(1, 10)}
    | {f"DTT-{i:03d}" for i in range(1, 13)}
    | {f"IND-{i:03d}" for i in range(1, 10)}
    | {f"ELB-{i:03d}" for i in range(1, 7)}
    | {f"EFF-{i:03d}" for i in range(1, 9)}
    | {f"NDT-{i:03d}" for i in range(1, 11)}
    | {f"SPC-{i:03d}" for i in range(1, 10)}
    | {f"HOTG-{i:03d}" for i in range(1, 11)}
    | {f"NWI-{i:03d}" for i in range(1, 5)}
    | {f"REF-{i:03d}" for i in range(1, 7)}
    | {f"RES-{i:03d}" for i in range(1, 10)}
)
EXPECTED_REQUIREMENTS = {f"PR{i}" for i in range(1, 13)}
VALID_RUNTIME = {"implemented", "partial", "planned", "deferred"}
VALID_THEOREM = {"proved", "partial", "missing"}


def tokens(value: str) -> list[str]:
    return [] if value == "-" else value.split(",")


def exact_inventory(label: str, expected: set[str], actual: list[str]) -> list[str]:
    errors: list[str] = []
    actual_set = set(actual)
    missing = sorted(expected - actual_set)
    extra = sorted(actual_set - expected)
    duplicates = sorted({item for item in actual if actual.count(item) > 1})
    if missing:
        errors.append(f"{label}: missing {', '.join(missing)}")
    if extra:
        errors.append(f"{label}: unknown {', '.join(extra)}")
    if duplicates:
        errors.append(f"{label}: duplicate {', '.join(duplicates)}")
    return errors


def main() -> int:
    errors: list[str] = []
    with COVERAGE.open(newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))

    suites: list[str] = []
    features: list[str] = []
    cases: list[str] = []
    requirements: set[str] = set()

    for row in rows:
        suite = row["suite"]
        suites.append(suite)
        features.extend(tokens(row["registry_features"]))
        cases.extend(tokens(row["matrix_cases"]))
        requirements.update(tokens(row["requirements"]))
        if row["runtime_status"] not in VALID_RUNTIME:
            errors.append(f"{suite}: invalid runtime status {row['runtime_status']}")
        if row["theorem_status"] not in VALID_THEOREM:
            errors.append(f"{suite}: invalid theorem status {row['theorem_status']}")
        fixture = row["fixture"]
        if fixture != "-" and not (ROOT / fixture).is_file():
            errors.append(f"{suite}: missing fixture {fixture}")

    errors.extend(exact_inventory("suites", EXPECTED_SUITES, suites))
    errors.extend(exact_inventory("registry features", EXPECTED_FEATURES, features))
    errors.extend(exact_inventory("matrix cases", EXPECTED_CASES, cases))
    if requirements != EXPECTED_REQUIREMENTS:
        errors.append(
            "requirements: expected "
            + ", ".join(sorted(EXPECTED_REQUIREMENTS))
            + "; got "
            + ", ".join(sorted(requirements))
        )

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"PASS: {len(suites)}/25 PrimeBench suites cross-mapped")
    print(f"PASS: {len(features)}/38 registry features cross-mapped exactly once")
    print(f"PASS: {len(cases)}/111 conformance cases cross-mapped exactly once")
    print("PASS: PR1..PR12 all have executable coverage ownership")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
