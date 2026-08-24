#!/usr/bin/env python3
"""Validate the request-local native IGGP target-fibre evidence."""

from __future__ import annotations

from pathlib import Path
import sys

from prime_iggp_target_fibre_evidence import (
    EVIDENCE_PATH,
    TargetFibreEvidenceError,
    load_target_fibre_evidence,
    validate_target_fibre_evidence,
)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    try:
        evidence = load_target_fibre_evidence(repo / EVIDENCE_PATH)
        validate_target_fibre_evidence(evidence, repo)
    except (OSError, TargetFibreEvidenceError, UnicodeDecodeError) as exc:
        print(f"FAIL: IGGP target-fibre evidence: {exc}", file=sys.stderr)
        return 1
    summary = evidence["summary"]
    print(
        "PASS: IGGP request-local native evidence accounts for 200 tasks: "
        f"{summary['effective_established_tasks']} established, "
        f"{summary['effective_contradiction_tasks']} corpus contradiction, "
        f"{summary['effective_refuted_tasks']} refuted, "
        f"{summary['effective_outside_tasks']} outside the current native "
        "realization family"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
