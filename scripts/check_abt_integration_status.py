#!/usr/bin/env python3
"""Validate the scoped ABT integration-status ledger and its live evidence."""

from __future__ import annotations

import argparse
import csv
from copy import deepcopy
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
LEDGER = ROOT / "tests" / "abt" / "integration_status.tsv"
MAKEFILE = ROOT / "Makefile"

FIELDS = [
    "id",
    "scope",
    "claim_status",
    "decision_status",
    "implementation_status",
    "statement",
    "derived_from",
    "counterfactual",
    "gates",
    "anchors",
]
REQUIRED_IDS = {
    "ABT-BINDER-REPRESENTATION",
    "ABT-HLB-SEMANTICS",
    "ABT-CYCLE-OWNERSHIP",
}
CLAIM_STATUSES = {"DERIVED", "EVIDENCED", "PROVISIONAL", "OPEN", "REFUTED"}
DECISION_STATUSES = {"RATIFIED", "CANDIDATE", "OPEN", "NOT-APPLICABLE"}
IMPLEMENTATION_STATUSES = {
    "IMPLEMENTED",
    "DOCUMENTED",
    "PARTIAL",
    "PLANNED",
    "DEFERRED",
}


def items(value: str) -> list[str]:
    return [] if value == "-" else [item.strip() for item in value.split(",")]


def safe_relative_path(value: str) -> Path | None:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        return None
    resolved = (ROOT / path).resolve()
    try:
        resolved.relative_to(ROOT.resolve())
    except ValueError:
        return None
    return resolved


def validate(rows: list[dict[str, str]], makefile: str) -> list[str]:
    errors: list[str] = []
    ids = [row.get("id", "") for row in rows]
    actual_ids = set(ids)
    missing = sorted(REQUIRED_IDS - actual_ids)
    extra = sorted(actual_ids - REQUIRED_IDS)
    duplicates = sorted({item for item in ids if ids.count(item) > 1})
    if missing:
        errors.append(f"ledger missing required rows: {', '.join(missing)}")
    if extra:
        errors.append(f"ledger has unknown rows: {', '.join(extra)}")
    if duplicates:
        errors.append(f"ledger has duplicate rows: {', '.join(duplicates)}")

    for row in rows:
        row_id = row.get("id", "<missing-id>")
        if set(row) != set(FIELDS):
            errors.append(f"{row_id}: schema does not match the declared fields")
            continue
        if not row["scope"] or not row["statement"]:
            errors.append(f"{row_id}: scope and statement must be nonempty")
        if row["claim_status"] not in CLAIM_STATUSES:
            errors.append(f"{row_id}: invalid claim status {row['claim_status']}")
        if row["decision_status"] not in DECISION_STATUSES:
            errors.append(f"{row_id}: invalid decision status {row['decision_status']}")
        if row["implementation_status"] not in IMPLEMENTATION_STATUSES:
            errors.append(
                f"{row_id}: invalid implementation status "
                f"{row['implementation_status']}"
            )
        if row["claim_status"] == "DERIVED" and (
            row["derived_from"] == "-" or row["counterfactual"] == "-"
        ):
            errors.append(
                f"{row_id}: DERIVED requires both derived_from and counterfactual"
            )

        gates = items(row["gates"])
        anchors = items(row["anchors"])
        if not gates:
            errors.append(f"{row_id}: no executable evidence gate")
        if not anchors:
            errors.append(f"{row_id}: no source evidence anchor")
        if row["implementation_status"] in {"IMPLEMENTED", "DOCUMENTED"} and (
            not gates or not anchors
        ):
            errors.append(
                f"{row_id}: {row['implementation_status']} requires gates and anchors"
            )

        for gate in gates:
            if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", gate):
                errors.append(f"{row_id}: malformed Make target {gate}")
                continue
            if re.search(rf"(?m)^{re.escape(gate)}\s*:", makefile) is None:
                errors.append(f"{row_id}: missing Make target {gate}")

        for anchor in anchors:
            if "::" not in anchor:
                errors.append(f"{row_id}: malformed source anchor {anchor}")
                continue
            relative, needle = anchor.split("::", 1)
            source = safe_relative_path(relative)
            if source is None:
                errors.append(f"{row_id}: unsafe source path {relative}")
                continue
            if not source.is_file():
                errors.append(f"{row_id}: missing source file {relative}")
                continue
            if not needle:
                errors.append(f"{row_id}: empty source anchor for {relative}")
                continue
            if needle not in source.read_text(encoding="utf-8"):
                errors.append(f"{row_id}: source anchor drifted: {anchor}")

    return errors


def read_ledger() -> list[dict[str, str]]:
    with LEDGER.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != FIELDS:
            raise ValueError(
                "ABT integration ledger header drifted: "
                f"expected {FIELDS}, got {reader.fieldnames}"
            )
        return list(reader)


def require_invalid(
    label: str, rows: list[dict[str, str]], makefile: str
) -> None:
    if not validate(rows, makefile):
        raise RuntimeError(f"ABT integration-ledger mutation survived: {label}")


def run_mutations(rows: list[dict[str, str]], makefile: str) -> int:
    caught = 0

    require_invalid("missing-row", deepcopy(rows[:-1]), makefile)
    caught += 1

    mutant = deepcopy(rows)
    mutant[0]["claim_status"] = "CERTAIN"
    require_invalid("invalid-status", mutant, makefile)
    caught += 1

    mutant = deepcopy(rows)
    mutant[1]["gates"] = "test-abt-nonexistent"
    require_invalid("missing-gate", mutant, makefile)
    caught += 1

    mutant = deepcopy(rows)
    mutant[2]["anchors"] = "src/match.c::not-a-real-cycle-owner"
    require_invalid("drifted-anchor", mutant, makefile)
    caught += 1

    mutant = deepcopy(rows)
    mutant[0]["counterfactual"] = "-"
    require_invalid("derived-without-counterfactual", mutant, makefile)
    caught += 1

    return caught


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mutation-suite", action="store_true")
    args = parser.parse_args()

    rows = read_ledger()
    makefile = MAKEFILE.read_text(encoding="utf-8")
    errors = validate(rows, makefile)
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"(ABTIntegrationLedgerSummary {len(rows)} {len(rows)} 0)")
    if args.mutation_suite:
        caught = run_mutations(rows, makefile)
        print(f"(ABTIntegrationLedgerMutationSummary {caught} {caught} 0)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
