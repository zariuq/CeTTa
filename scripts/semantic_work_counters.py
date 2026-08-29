#!/usr/bin/env python3
"""Normalize CeTTa and SWI-PeTTa work into one semantic counter schema."""

from __future__ import annotations

from collections.abc import Mapping


SWI_PREFIX = "semantic-counter "
FIELDS = (
    "rule-candidates-considered",
    "rule-candidates-rejected",
    "unifications-attempted",
    "unifications-succeeded",
    "rule-bodies-entered",
    "result-occurrences",
    "answers-produced",
    "ordered-occurrences-published",
)


def _required_integer(source: Mapping[str, int | float], name: str) -> int:
    if name not in source:
        raise ValueError(f"missing CeTTa machine counter: {name}")
    value = source[name]
    if not isinstance(value, int):
        raise ValueError(f"CeTTa machine counter is not integral: {name}")
    if value < 0:
        raise ValueError(f"CeTTa machine counter is negative: {name}")
    return value


def validate_semantic_work(counters: Mapping[str, int]) -> None:
    missing = [name for name in FIELDS if name not in counters]
    extra = sorted(set(counters).difference(FIELDS))
    if missing or extra:
        raise ValueError(
            f"invalid semantic counter fields: missing={missing}, extra={extra}"
        )
    if any(counters[name] < 0 for name in FIELDS):
        raise ValueError("semantic counters must be nonnegative")
    considered = counters["rule-candidates-considered"]
    rejected = counters["rule-candidates-rejected"]
    succeeded = counters["unifications-succeeded"]
    if considered != rejected + succeeded:
        raise ValueError(
            "candidate partition is not exact: "
            f"{considered} != {rejected} + {succeeded}"
        )
    if counters["unifications-attempted"] != considered:
        raise ValueError("each considered occurrence must have one semantic attempt")
    if counters["rule-bodies-entered"] != succeeded:
        raise ValueError("each successful head match must enter one rule body")
    if (
        counters["ordered-occurrences-published"]
        != counters["answers-produced"]
    ):
        raise ValueError("published occurrence count must equal answer count")


def from_cetta_machine(
    machine: Mapping[str, int | float], published_answers: int,
) -> dict[str, int]:
    """Project implementation counters onto source-occurrence semantics.

    Match-decision inputs and unification_calls are intentionally excluded:
    they count internal plan coordinates and operand operations, respectively.
    """

    if published_answers < 0:
        raise ValueError("published answer count is negative")
    considered = _required_integer(machine, "clause_snapshot_candidates")
    rejected = (
        _required_integer(machine, "clause_candidates_shape_pruned")
        + _required_integer(machine, "clause_attempts_rejected_before_body")
    )
    succeeded = _required_integer(machine, "clause_bodies_scheduled")
    counters = {
        "rule-candidates-considered": considered,
        "rule-candidates-rejected": rejected,
        "unifications-attempted": considered,
        "unifications-succeeded": succeeded,
        "rule-bodies-entered": succeeded,
        "result-occurrences": _required_integer(
            machine, "clause_result_occurrences"
        ),
        "answers-produced": published_answers,
        "ordered-occurrences-published": published_answers,
    }
    validate_semantic_work(counters)
    return counters


def parse_swi(stderr: str) -> tuple[dict[str, int], str]:
    counters: dict[str, int] = {}
    ordinary: list[str] = []
    for line in stderr.splitlines(keepends=True):
        payload = line.rstrip("\r\n")
        if not payload.startswith(SWI_PREFIX):
            ordinary.append(line)
            continue
        fields = payload.split()
        if len(fields) != 3 or fields[0] != "semantic-counter":
            raise ValueError(f"malformed SWI semantic counter: {payload!r}")
        name = fields[1]
        if name in counters:
            raise ValueError(f"duplicate SWI semantic counter: {name}")
        try:
            value = int(fields[2])
        except ValueError as error:
            raise ValueError(
                f"non-integral SWI semantic counter: {payload!r}"
            ) from error
        counters[name] = value
    validate_semantic_work(counters)
    return counters, "".join(ordinary)
