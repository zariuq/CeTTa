"""Evidence ledger for request-local native IGGP target fibres."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

import check_prime_iggp_manifest as corpus
from prime_iggp_episode_evidence import (
    EVIDENCE_PATH as BASE_EVIDENCE_PATH,
    EpisodeEvidenceError,
    load_episode_evidence,
    validate_episode_evidence,
)


SCHEMA = "cetta-prime-iggp-native-target-fibre-evidence-v1"
EVIDENCE_PATH = Path(
    "benchmarks/prime/ilp/iggp_target_fibre_evidence.json"
)
NATIVE_CONTRACT = "gdl-stratified-target-v1/GdlStratifiedEpisodesV2"
METRIC_NAMES = (
    "tasks",
    "states",
    "episodes",
    "source_forms",
    "selected_forms",
    "reachable_relations",
    "external_relations",
    "static_facts",
    "omitted_static_facts",
    "fact_occurrences",
    "omitted_fact_occurrences",
    "typing_proofs",
    "supports",
    "emitted_supports",
    "proof_edges",
    "reference_episodes",
)
SUMMARY_NAMES = (
    "tasks",
    "base_established_tasks",
    "base_contradiction_tasks",
    "base_outside_tasks",
    "target_fibre_established_tasks",
    "target_fibre_refuted_tasks",
    "target_fibre_outside_tasks",
    "effective_established_tasks",
    "effective_contradiction_tasks",
    "effective_refuted_tasks",
    "effective_outside_tasks",
)


class TargetFibreEvidenceError(RuntimeError):
    """The target-fibre evidence does not satisfy its contract."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def load_target_fibre_evidence(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise TargetFibreEvidenceError(
            f"cannot read target-fibre evidence: {exc}"
        ) from exc
    if not isinstance(value, dict):
        raise TargetFibreEvidenceError(
            "target-fibre evidence root is not an object"
        )
    return value


def _exact_object(
    value: object, keys: tuple[str, ...], label: str
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(keys):
        raise TargetFibreEvidenceError(f"{label} has the wrong fields")
    return value


def _natural(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise TargetFibreEvidenceError(f"{label} is not a natural number")
    return value


def _outcome(value: object, label: str) -> dict[str, Any]:
    if not isinstance(value, dict) or "outcome" not in value:
        raise TargetFibreEvidenceError(f"{label} has no outcome")
    outcome = value["outcome"]
    if outcome == "Established":
        record = _exact_object(value, ("outcome", "metrics"), label)
        metrics = _exact_object(
            record["metrics"], METRIC_NAMES, f"{label} metrics"
        )
        for name in METRIC_NAMES:
            _natural(metrics[name], f"{label}.{name}")
        if metrics["tasks"] != 1 or metrics["reference_episodes"] == 0:
            raise TargetFibreEvidenceError(
                f"{label} is not a fully witnessed single target"
            )
        if (
            metrics["selected_forms"] > metrics["source_forms"]
            or metrics["typing_proofs"] < metrics["fact_occurrences"]
        ):
            raise TargetFibreEvidenceError(
                f"{label} metrics violate target-fibre conservation"
            )
        return record
    if outcome in {"OutsideFragment", "Refuted"}:
        record = _exact_object(value, ("outcome", "reason"), label)
        if not isinstance(record["reason"], str) or not record["reason"]:
            raise TargetFibreEvidenceError(f"{label} has no reason")
        return record
    raise TargetFibreEvidenceError(f"{label} has an unknown outcome")


def validate_target_fibre_evidence(
    data: dict[str, Any],
    repo: Path,
    *,
    base_evidence: dict[str, Any] | None = None,
) -> None:
    _exact_object(data, ("schema", "source", "summary", "games"), "root")
    if data["schema"] != SCHEMA:
        raise TargetFibreEvidenceError("target-fibre schema changed")

    source = _exact_object(
        data["source"],
        (
            "base_episode_evidence",
            "base_episode_evidence_sha256",
            "presentation_digest",
            "native_contract",
        ),
        "source",
    )
    if source["base_episode_evidence"] != BASE_EVIDENCE_PATH.as_posix():
        raise TargetFibreEvidenceError("base evidence path changed")
    base_path = repo / BASE_EVIDENCE_PATH
    if sha256_file(base_path) != source["base_episode_evidence_sha256"]:
        raise TargetFibreEvidenceError("base evidence identity changed")
    base = base_evidence or load_episode_evidence(base_path)
    try:
        validate_episode_evidence(base, repo)
    except EpisodeEvidenceError as exc:
        raise TargetFibreEvidenceError(str(exc)) from exc
    if (
        source["presentation_digest"]
        != base["source"]["presentation_digest"]
        or source["native_contract"] != NATIVE_CONTRACT
    ):
        raise TargetFibreEvidenceError("target-fibre source identity changed")

    games = data["games"]
    if not isinstance(games, dict):
        raise TargetFibreEvidenceError("target-fibre games are not an object")
    outside_games = base["outside_source_image"]
    if tuple(games) != tuple(outside_games):
        raise TargetFibreEvidenceError(
            "target-fibre games do not match whole-source absences"
        )

    counts = {"Established": 0, "Refuted": 0, "OutsideFragment": 0}
    for game, value in games.items():
        if not isinstance(value, dict):
            raise TargetFibreEvidenceError(f"{game} evidence is not an object")
        if set(value) == {"uniform_outcome"}:
            record = _outcome(value["uniform_outcome"], game)
            if record["outcome"] == "Established":
                raise TargetFibreEvidenceError(
                    f"{game} cannot share one established target receipt"
                )
            counts[record["outcome"]] += len(corpus.TARGETS)
            continue
        if set(value) != {"target_outcomes"}:
            raise TargetFibreEvidenceError(
                f"{game} does not choose one evidence representation"
            )
        targets = value["target_outcomes"]
        if (
            not isinstance(targets, dict)
            or tuple(targets) != corpus.TARGETS
        ):
            raise TargetFibreEvidenceError(
                f"{game} target outcome partition is not exact"
            )
        for target, target_value in targets.items():
            record = _outcome(target_value, f"{game}/{target}")
            counts[record["outcome"]] += 1

    base_summary = base["summary"]
    expected_summary = {
        "tasks": base_summary["tasks"],
        "base_established_tasks": base_summary["established_tasks"],
        "base_contradiction_tasks": base_summary[
            "corpus_contradiction_tasks"
        ],
        "base_outside_tasks": base_summary["outside_source_image_tasks"],
        "target_fibre_established_tasks": counts["Established"],
        "target_fibre_refuted_tasks": counts["Refuted"],
        "target_fibre_outside_tasks": counts["OutsideFragment"],
        "effective_established_tasks": (
            base_summary["established_tasks"] + counts["Established"]
        ),
        "effective_contradiction_tasks": base_summary[
            "corpus_contradiction_tasks"
        ],
        "effective_refuted_tasks": counts["Refuted"],
        "effective_outside_tasks": counts["OutsideFragment"],
    }
    summary = _exact_object(data["summary"], SUMMARY_NAMES, "summary")
    for name in SUMMARY_NAMES:
        _natural(summary[name], name)
    if summary != expected_summary:
        raise TargetFibreEvidenceError(
            "target-fibre summary disagrees with its outcomes"
        )
    if (
        counts["Established"]
        + counts["Refuted"]
        + counts["OutsideFragment"]
        != base_summary["outside_source_image_tasks"]
        or summary["effective_established_tasks"]
        + summary["effective_contradiction_tasks"]
        + summary["effective_refuted_tasks"]
        + summary["effective_outside_tasks"]
        != summary["tasks"]
    ):
        raise TargetFibreEvidenceError(
            "effective task outcome partition is not exact"
        )


def expected_target_outcome(
    data: dict[str, Any], game: str, target: str
) -> dict[str, Any]:
    game_record = data["games"][game]
    if "uniform_outcome" in game_record:
        return game_record["uniform_outcome"]
    return game_record["target_outcomes"][target]
