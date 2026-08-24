"""Typed task-level evidence ledger for native IGGP episode qualification."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

import check_prime_iggp_manifest as corpus
from check_prime_iggp_presentations import (
    GameAudit,
    SOURCE_DIGEST,
    source_path,
    task_presentation_family,
)


SCHEMA = "cetta-prime-iggp-native-episode-evidence-v1"
EVIDENCE_PATH = Path("benchmarks/prime/ilp/iggp_episode_evidence.json")
METRIC_NAMES = (
    "tasks",
    "states",
    "episodes",
    "static_facts",
    "fact_occurrences",
    "typing_proofs",
    "supports",
    "proof_edges",
)
CONTRADICTION_NAMES = (
    "evaluated_tasks",
    "established_tasks",
    "contradiction_tasks",
    "evaluated_states",
    "evaluated_episodes",
    "typing_proofs",
    "supports",
    "proof_edges",
    "contradiction_states",
    "missing_labels",
    "extra_labels",
    "target_states",
    "digest",
    "first_episode",
    "first_missing",
    "first_extra",
)


class EpisodeEvidenceError(RuntimeError):
    """The episode evidence ledger does not satisfy its contract."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def load_episode_evidence(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EpisodeEvidenceError(f"cannot read episode evidence: {exc}") from exc
    if not isinstance(value, dict):
        raise EpisodeEvidenceError("episode evidence root is not an object")
    return value


def _exact_object(
    value: object, keys: Iterable[str], label: str
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(keys):
        raise EpisodeEvidenceError(f"{label} has the wrong fields")
    return value


def _natural(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise EpisodeEvidenceError(f"{label} is not a natural number")
    return value


def _relative_path(value: object, label: str) -> Path:
    if not isinstance(value, str):
        raise EpisodeEvidenceError(f"{label} is not a path string")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise EpisodeEvidenceError(f"{label} is not repository-relative")
    return path


def source_image_blockers(game: GameAudit) -> tuple[str, ...]:
    blockers = []
    if game.forms == 0:
        blockers.append("no-live-gdl")
    elif (
        not game.finite_assignment_found
        or not game.checked_type_of_extensions
        or not game.checked_type_of_occurrence_judgments
    ):
        blockers.append("finite-type-obstruction")
    if game.foreign_code_lines:
        blockers.append("foreign-source-code")
    return tuple(blockers)


def _validate_metrics(value: object, game: str) -> dict[str, int]:
    metrics = _exact_object(value, METRIC_NAMES, f"{game} metrics")
    result = {
        name: _natural(metrics[name], f"{game}.{name}")
        for name in METRIC_NAMES
    }
    if result["tasks"] != len(corpus.TARGETS):
        raise EpisodeEvidenceError(f"{game} does not cover every task target")
    return result


def _validate_contradiction(
    value: object, game: str, target: str
) -> dict[str, Any]:
    receipt = _exact_object(
        value, CONTRADICTION_NAMES, f"{game}/{target} contradiction"
    )
    for name in CONTRADICTION_NAMES[:11]:
        _natural(receipt[name], f"{game}/{target}.{name}")
    if (
        receipt["evaluated_tasks"] != len(corpus.TARGETS)
        or receipt["established_tasks"] + receipt["contradiction_tasks"]
        != receipt["evaluated_tasks"]
    ):
        raise EpisodeEvidenceError(
            f"{game}/{target} task outcome partition is invalid"
        )
    target_states = receipt["target_states"]
    if (
        not isinstance(target_states, list)
        or any(
            not isinstance(item, list)
            or len(item) != 2
            or item[0] not in corpus.TARGETS
            or isinstance(item[1], bool)
            or not isinstance(item[1], int)
            or item[1] <= 0
            for item in target_states
        )
        or [item[0] for item in target_states] != sorted(
            item[0] for item in target_states
        )
    ):
        raise EpisodeEvidenceError(
            f"{game}/{target} target-state partition is invalid"
        )
    if [item[0] for item in target_states] != [target]:
        raise EpisodeEvidenceError(
            f"{game}/{target} receipt crosses task targets"
        )
    if (
        receipt["contradiction_tasks"] != len(target_states)
        or receipt["established_tasks"]
        != receipt["evaluated_tasks"] - len(target_states)
        or sum(item[1] for item in target_states)
        != receipt["contradiction_states"]
        or receipt["missing_labels"] + receipt["extra_labels"] == 0
    ):
        raise EpisodeEvidenceError(
            f"{game}/{target} contradiction accounting is inconsistent"
        )
    digest = receipt["digest"]
    if (
        not isinstance(digest, str)
        or len(digest) != 64
        or any(character not in "0123456789abcdef" for character in digest)
    ):
        raise EpisodeEvidenceError(
            f"{game}/{target} contradiction digest is malformed"
        )
    for name in ("first_episode",):
        if not isinstance(receipt[name], str) or not receipt[name]:
            raise EpisodeEvidenceError(
                f"{game}/{target}.{name} is not a retained witness"
            )
    for name in ("first_missing", "first_extra"):
        if not isinstance(receipt[name], list) or any(
            not isinstance(item, str) for item in receipt[name]
        ):
            raise EpisodeEvidenceError(
                f"{game}/{target}.{name} is malformed"
            )
    if not receipt["first_missing"] and not receipt["first_extra"]:
        raise EpisodeEvidenceError(
            f"{game}/{target} contradiction has no witness"
        )
    return receipt


def _expected_summary(
    established: dict[str, Any],
    partial: dict[str, Any],
    outside: dict[str, Any],
) -> dict[str, int]:
    established_tasks = len(established) * len(corpus.TARGETS)
    contradiction_tasks = 0
    for game, value in partial.items():
        record = value
        established_tasks += len(record["established_targets"])
        contradiction_tasks += len(record["corpus_contradictions"])
    return {
        "games": len(corpus.GAMES),
        "tasks": len(corpus.GAMES) * len(corpus.TARGETS),
        "source_eligible_games": len(established) + len(partial),
        "source_eligible_tasks": (
            len(established) + len(partial)
        ) * len(corpus.TARGETS),
        "fully_established_games": len(established),
        "established_tasks": established_tasks,
        "corpus_contradiction_games": len(partial),
        "corpus_contradiction_tasks": contradiction_tasks,
        "outside_source_image_games": len(outside),
        "outside_source_image_tasks": len(outside) * len(corpus.TARGETS),
    }


def validate_episode_evidence(
    data: dict[str, Any],
    repo: Path,
    *,
    audits: tuple[GameAudit, ...] | None = None,
    presentation_digest: str | None = None,
    snapshot_root: Path | None = None,
) -> None:
    expected_top = {
        "schema",
        "source",
        "summary",
        "task_presentation_overrides",
        "established_games",
        "partially_established_games",
        "outside_source_image",
    }
    if set(data) != expected_top or data.get("schema") != SCHEMA:
        raise EpisodeEvidenceError("episode evidence schema changed")

    source = _exact_object(
        data["source"],
        (
            "corpus_manifest",
            "corpus_manifest_sha256",
            "presentation_digest",
            "native_contract",
        ),
        "episode source",
    )
    manifest_rel = _relative_path(
        source["corpus_manifest"], "corpus manifest"
    )
    manifest = repo / manifest_rel
    if not manifest.is_file():
        raise EpisodeEvidenceError("corpus manifest is missing")
    if sha256_file(manifest) != source["corpus_manifest_sha256"]:
        raise EpisodeEvidenceError("corpus manifest identity changed")
    corpus_data = corpus.load_manifest(manifest)
    corpus.validate_manifest(corpus_data, repo)
    expected_digest = presentation_digest or SOURCE_DIGEST
    if source["presentation_digest"] != expected_digest:
        raise EpisodeEvidenceError("presentation identity changed")
    if source["native_contract"] != "gdl-stratified-episode-v1":
        raise EpisodeEvidenceError("native episode contract changed")

    established = data["established_games"]
    partial = data["partially_established_games"]
    outside = data["outside_source_image"]
    overrides = data["task_presentation_overrides"]
    if not all(isinstance(value, dict) for value in (
        established, partial, outside, overrides
    )):
        raise EpisodeEvidenceError("episode partitions are not objects")
    game_partition = set(established) | set(partial) | set(outside)
    if (
        game_partition != set(corpus.GAMES)
        or set(established) & set(partial)
        or set(established) & set(outside)
        or set(partial) & set(outside)
    ):
        raise EpisodeEvidenceError("episode game partition is not exact")
    if tuple(established) != tuple(
        game for game in corpus.GAMES if game in established
    ):
        raise EpisodeEvidenceError("established game order changed")
    if tuple(partial) != tuple(game for game in corpus.GAMES if game in partial):
        raise EpisodeEvidenceError("partial game order changed")
    if tuple(outside) != tuple(game for game in corpus.GAMES if game in outside):
        raise EpisodeEvidenceError("outside-image game order changed")

    for game, value in established.items():
        _validate_metrics(value, game)
    for game, value in partial.items():
        record = _exact_object(
            value,
            ("established_targets", "corpus_contradictions"),
            f"{game} partial evidence",
        )
        established_targets = record["established_targets"]
        contradictions = record["corpus_contradictions"]
        if (
            not isinstance(established_targets, list)
            or any(target not in corpus.TARGETS for target in established_targets)
            or established_targets != [
                target for target in corpus.TARGETS if target in established_targets
            ]
            or not isinstance(contradictions, dict)
            or not contradictions
            or set(established_targets) & set(contradictions)
            or set(established_targets) | set(contradictions)
            != set(corpus.TARGETS)
        ):
            raise EpisodeEvidenceError(f"{game} task partition is not exact")
        for target, receipt in contradictions.items():
            _validate_contradiction(receipt, game, target)

    summary = _exact_object(
        data["summary"],
        _expected_summary(established, partial, outside),
        "episode summary",
    )
    expected_summary = _expected_summary(established, partial, outside)
    if summary != expected_summary:
        raise EpisodeEvidenceError("episode summary disagrees with task rows")

    for game, record in outside.items():
        _exact_object(
            record,
            (
                "blockers",
                "foreign_code_lines",
                "finite_type_assignment",
                "checked_type_of_extension",
                "checked_occurrences",
                "empty_finite_domains",
            ),
            f"{game} outside-source-image evidence",
        )
        blockers = record["blockers"]
        allowed_blockers = {
            "no-live-gdl",
            "finite-type-obstruction",
            "foreign-source-code",
        }
        if (
            not isinstance(blockers, list)
            or not blockers
            or len(blockers) != len(set(blockers))
            or any(blocker not in allowed_blockers for blocker in blockers)
            or not isinstance(record["finite_type_assignment"], bool)
            or not isinstance(record["checked_type_of_extension"], bool)
        ):
            raise EpisodeEvidenceError(f"{game} has no source-image blocker")
        for field in (
            "foreign_code_lines",
            "checked_occurrences",
            "empty_finite_domains",
        ):
            _natural(record[field], f"{game}.{field}")

    for game, record in overrides.items():
        if game not in corpus.GAMES:
            raise EpisodeEvidenceError("task-presentation override names no game")
        override = _exact_object(
            record, ("source", "instances"), f"{game} task presentation"
        )
        _relative_path(override["source"], f"{game} task source")
        if not isinstance(override["instances"], list) or not override["instances"]:
            raise EpisodeEvidenceError(f"{game} task instances are empty")
        for index, path in enumerate(override["instances"]):
            _relative_path(path, f"{game} task instance {index}")

    if audits is None:
        return
    audit_by_game = {game.game: game for game in audits}
    source_eligible = {
        game.game
        for game in audits
        if game.checked_type_of_extensions == 1
        and game.checked_type_of_occurrence_judgments > 0
        and game.foreign_code_lines == 0
    }
    if source_eligible != set(established) | set(partial):
        raise EpisodeEvidenceError("source-image partition changed")
    for game, record in outside.items():
        audit = audit_by_game[game]
        expected = {
            "blockers": list(source_image_blockers(audit)),
            "foreign_code_lines": audit.foreign_code_lines,
            "finite_type_assignment": bool(audit.finite_assignment_found),
            "checked_type_of_extension": bool(audit.checked_type_of_extensions),
            "checked_occurrences": audit.checked_type_of_occurrence_judgments,
            "empty_finite_domains": audit.arc_empty_components,
        }
        if record != expected:
            raise EpisodeEvidenceError(f"{game} source-image evidence changed")
    if snapshot_root is None:
        return
    expected_overrides = {}
    for game in corpus.GAMES:
        family = task_presentation_family(snapshot_root, game)
        canonical = source_path(snapshot_root, game)
        if family.source_path == canonical:
            continue
        expected_overrides[game] = {
            "source": family.source_path.relative_to(snapshot_root).as_posix(),
            "instances": [
                path.relative_to(snapshot_root).as_posix()
                for path in family.instance_paths
            ],
        }
    if overrides != expected_overrides:
        raise EpisodeEvidenceError("task-presentation composition changed")


def expected_game_totals(data: dict[str, Any]) -> dict[str, dict[str, int]]:
    return {
        game: _validate_metrics(value, game)
        for game, value in data["established_games"].items()
    }


def expected_corpus_contradictions(
    data: dict[str, Any],
) -> dict[str, dict[str, object]]:
    result = {}
    for game, partial in data["partially_established_games"].items():
        contradictions = partial["corpus_contradictions"]
        if len(contradictions) != 1:
            raise EpisodeEvidenceError(
                f"{game}: runtime comparison expects one game-level receipt"
            )
        target, receipt = next(iter(contradictions.items()))
        checked = _validate_contradiction(receipt, game, target)
        result[game] = {
            **checked,
            "target_states": tuple(
                (item[0], item[1]) for item in checked["target_states"]
            ),
            "first_missing": tuple(checked["first_missing"]),
            "first_extra": tuple(checked["first_extra"]),
        }
    return result
