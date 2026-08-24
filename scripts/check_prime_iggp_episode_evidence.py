#!/usr/bin/env python3
"""Validate Prime's task-level native IGGP episode evidence ledger."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from check_prime_iggp_presentations import (
    PresentationAuditError,
    audit as audit_presentations,
    validate as validate_presentations,
)
from prime_iggp_episode_evidence import (
    EVIDENCE_PATH,
    EpisodeEvidenceError,
    load_episode_evidence,
    validate_episode_evidence,
)


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=repo / EVIDENCE_PATH)
    parser.add_argument("--snapshot-root", type=Path)
    args = parser.parse_args()

    try:
        audits = None
        digest = None
        if args.snapshot_root is not None:
            audits, digest = audit_presentations(args.snapshot_root)
            validate_presentations(audits, digest)
        evidence = load_episode_evidence(args.manifest)
        validate_episode_evidence(
            evidence,
            repo,
            audits=audits,
            presentation_digest=digest,
            snapshot_root=args.snapshot_root,
        )
    except (
        EpisodeEvidenceError,
        OSError,
        PresentationAuditError,
        UnicodeDecodeError,
    ) as exc:
        print(f"FAIL: IGGP episode evidence: {exc}", file=sys.stderr)
        return 1

    summary = evidence["summary"]
    print(
        "PASS: IGGP native episode evidence accounts for 200 tasks: "
        f"{summary['established_tasks']} established, "
        f"{summary['corpus_contradiction_tasks']} corpus contradiction, "
        f"{summary['outside_source_image_tasks']} outside the current "
        "typed source image"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
