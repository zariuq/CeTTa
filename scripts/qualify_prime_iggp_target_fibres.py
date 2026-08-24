#!/usr/bin/env python3
"""Requalify request-local native target fibres for whole-source absences."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import check_prime_iggp_manifest as corpus
from check_prime_iggp_presentations import (
    PresentationAuditError,
    audit as audit_presentations,
    validate as validate_presentations,
)
from prime_iggp_episode_evidence import (
    EVIDENCE_PATH as BASE_EVIDENCE_PATH,
    EpisodeEvidenceError,
    load_episode_evidence,
    validate_episode_evidence,
)
from prime_iggp_generation import GenerationError
from prime_iggp_positive_horn import PositiveHornBoundary
from prime_iggp_presentation import PresentationError
from prime_iggp_stratification import NegativeDependencyCycle
from prime_iggp_stratified_model import StratifiedModelBoundary
from prime_iggp_target_fibre_evidence import (
    EVIDENCE_PATH,
    METRIC_NAMES,
    TargetFibreEvidenceError,
    expected_target_outcome,
    load_target_fibre_evidence,
    validate_target_fibre_evidence,
)
from qualify_prime_iggp_stratified_episode import (
    CorpusLabelContradiction,
    compare_game,
)


def observe_target(
    snapshot_root: Path,
    repo: Path,
    runner: Path,
    game: str,
    target: str,
    *,
    batch_size: int,
    full_reference_max_groups: int,
    reference_groups: int,
) -> dict[str, object]:
    try:
        totals = compare_game(
            snapshot_root,
            repo,
            runner,
            game,
            batch_size=batch_size,
            full_reference_max_groups=full_reference_max_groups,
            reference_groups=reference_groups,
            target=target,
        )
    except NegativeDependencyCycle as exc:
        return {"outcome": "Refuted", "reason": str(exc)}
    except (PositiveHornBoundary, StratifiedModelBoundary) as exc:
        return {"outcome": "OutsideFragment", "reason": str(exc)}
    except CorpusLabelContradiction as exc:
        raise RuntimeError(
            f"{game}/{target} exposed an unledgered corpus contradiction: "
            f"{exc.receipt.digest}"
        ) from exc
    return {
        "outcome": "Established",
        "metrics": {name: totals[name] for name in METRIC_NAMES},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot-root", type=Path, required=True)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--full-reference-max-groups", type=int, default=512)
    parser.add_argument("--reference-groups", type=int, default=32)
    parser.add_argument("--games", nargs="*")
    parser.add_argument("--targets", nargs="*")
    args = parser.parse_args()
    if (
        args.batch_size <= 0
        or args.full_reference_max_groups < 0
        or args.reference_groups <= 0
    ):
        print("FAIL: target-fibre qualification limits are invalid", file=sys.stderr)
        return 2
    if not args.runner.is_file():
        print("FAIL: native target-fibre runner is missing", file=sys.stderr)
        return 2

    repo = Path(__file__).resolve().parents[1]
    try:
        audits, presentation_digest = audit_presentations(args.snapshot_root)
        validate_presentations(audits, presentation_digest)
        base = load_episode_evidence(repo / BASE_EVIDENCE_PATH)
        validate_episode_evidence(
            base,
            repo,
            audits=audits,
            presentation_digest=presentation_digest,
            snapshot_root=args.snapshot_root,
        )
        evidence = load_target_fibre_evidence(repo / EVIDENCE_PATH)
        validate_target_fibre_evidence(
            evidence, repo, base_evidence=base
        )
    except (
        EpisodeEvidenceError,
        OSError,
        PresentationAuditError,
        TargetFibreEvidenceError,
        UnicodeDecodeError,
    ) as exc:
        print(f"FAIL: IGGP target-fibre preflight: {exc}", file=sys.stderr)
        return 1

    known_games = tuple(evidence["games"])
    selected_games = tuple(args.games or known_games)
    unknown_games = set(selected_games) - set(known_games)
    if unknown_games:
        print(
            "FAIL: games do not require target-fibre evidence: "
            + ", ".join(sorted(unknown_games)),
            file=sys.stderr,
        )
        return 2
    selected_targets = tuple(args.targets or corpus.TARGETS)
    unknown_targets = set(selected_targets) - set(corpus.TARGETS)
    if unknown_targets:
        print(
            "FAIL: unknown IGGP task targets: "
            + ", ".join(sorted(unknown_targets)),
            file=sys.stderr,
        )
        return 2

    observed_counts = {
        "Established": 0,
        "Refuted": 0,
        "OutsideFragment": 0,
    }
    for game in selected_games:
        for target in selected_targets:
            expected = expected_target_outcome(evidence, game, target)
            try:
                observed = observe_target(
                    args.snapshot_root,
                    repo,
                    args.runner.resolve(),
                    game,
                    target,
                    batch_size=args.batch_size,
                    full_reference_max_groups=(
                        args.full_reference_max_groups
                    ),
                    reference_groups=args.reference_groups,
                )
            except (
                GenerationError,
                OSError,
                PresentationError,
                RuntimeError,
                UnicodeDecodeError,
                ValueError,
            ) as exc:
                print(
                    f"FAIL: IGGP {game}/{target} target fibre: {exc}",
                    file=sys.stderr,
                )
                return 1
            if observed != expected:
                print(
                    f"FAIL: IGGP {game}/{target} target-fibre drift: "
                    f"observed={observed} expected={expected}",
                    file=sys.stderr,
                )
                return 1
            outcome = observed["outcome"]
            observed_counts[outcome] += 1
            if outcome == "Established":
                metrics = observed["metrics"]
                print(
                    f"Established\t{game}/{target}\t"
                    f"states={metrics['states']} "
                    f"selected_forms={metrics['selected_forms']} "
                    f"typing_proofs={metrics['typing_proofs']} "
                    f"proof_edges={metrics['proof_edges']}"
                )
            else:
                print(
                    f"{outcome}\t{game}/{target}\t{observed['reason']}"
                )

    total = sum(observed_counts.values())
    print(
        "PrimeIggpNativeTargetFibreSummary "
        f"selected_tasks={total} "
        f"established={observed_counts['Established']} "
        f"refuted={observed_counts['Refuted']} "
        f"outside={observed_counts['OutsideFragment']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
