#!/usr/bin/env python3
"""Parse and aggregate CeTTa's PeTTa-machine observability records."""

from __future__ import annotations

import statistics


STATS_PREFIX = "PETTA_MACHINE_STATS "
RUNTIME_COUNTER_PREFIX = "runtime-counter "
PUBLICATION_STATS_PREFIX = "PETTA_PUBLICATION_STATS "
CONTROLLER_STATS_PREFIX = "CETTA_CONTROLLER_STATS "


def parse_stats_line(line: str) -> dict[str, int]:
    if not line.startswith(STATS_PREFIX):
        raise ValueError("not a PeTTa machine statistics line")
    fields: dict[str, int] = {}
    for item in line[len(STATS_PREFIX):].split():
        key, separator, value = item.partition("=")
        if not separator or not key or not value:
            raise ValueError(f"malformed statistics field: {item!r}")
        if key in fields:
            raise ValueError(f"duplicate statistics field: {key}")
        fields[key] = int(value)
    return fields


def parse_controller_stats_line(line: str) -> dict[str, int | str]:
    if not line.startswith(CONTROLLER_STATS_PREFIX):
        raise ValueError("not a search-controller statistics line")
    fields: dict[str, int | str] = {}
    for item in line[len(CONTROLLER_STATS_PREFIX):].split():
        key, separator, value = item.partition("=")
        if not separator or not key or not value:
            raise ValueError(f"malformed controller field: {item!r}")
        if key in fields:
            raise ValueError(f"duplicate controller field: {key}")
        fields[key] = value if key in {
            "requested", "selection", "active", "storage", "advisor"
        } else int(value)
    if not {"requested", "active", "storage"}.issubset(fields):
        raise ValueError(f"incomplete controller statistics: {fields!r}")
    return fields


def aggregate_controller_stats(
    invocations: list[dict[str, int | str]],
) -> dict[str, int]:
    aggregate = {
        "records": len(invocations),
        "active_fifo": 0,
        "active_ratio": 0,
        "active_inline_depth_first": 0,
        "active_refused": 0,
        "storage_shared_terms_owned_state": 0,
        "storage_none": 0,
        "advisor_incremental_compression": 0,
        "advisor_none": 0,
    }
    for invocation in invocations:
        requested = invocation.get("requested")
        active = invocation.get("active")
        if requested not in {"fifo", "ratio"}:
            raise ValueError(
                f"unexpected requested controller: {requested!r}"
            )
        aggregate[f"requested_{requested}"] = (
            aggregate.get(f"requested_{requested}", 0) + 1
        )
        if active == "fifo":
            aggregate["active_fifo"] += 1
        elif active == "ratio":
            aggregate["active_ratio"] += 1
        elif active == "inline-depth-first":
            aggregate["active_inline_depth_first"] += 1
        elif active == "refused":
            aggregate["active_refused"] += 1
        else:
            raise ValueError(f"unexpected active controller: {active!r}")
        storage = invocation.get("storage")
        if storage == "shared-terms-owned-state":
            aggregate["storage_shared_terms_owned_state"] += 1
        elif storage == "none":
            aggregate["storage_none"] += 1
        else:
            raise ValueError(f"unexpected controller storage: {storage!r}")
        advisor = invocation.get("advisor", "none")
        if advisor == "incremental-compression":
            aggregate["advisor_incremental_compression"] += 1
        elif advisor == "none":
            aggregate["advisor_none"] += 1
        else:
            raise ValueError(f"unexpected controller advisor: {advisor!r}")
        for key, value in invocation.items():
            if key in {
                "requested", "selection", "active", "storage", "advisor"
            }:
                continue
            if not isinstance(value, int):
                raise ValueError(f"non-integral controller field: {key}")
            if key.startswith("max_"):
                aggregate[key] = max(aggregate.get(key, 0), value)
            else:
                aggregate[key] = aggregate.get(key, 0) + value
    return aggregate


def parse_runtime_counter_line(line: str) -> tuple[str, int]:
    if not line.startswith(RUNTIME_COUNTER_PREFIX):
        raise ValueError("not a runtime-counter line")
    fields = line.split()
    if len(fields) != 3 or fields[0] != "runtime-counter":
        raise ValueError(f"malformed runtime-counter line: {line!r}")
    return fields[1], int(fields[2])


def parse_publication_stats_line(line: str) -> dict[str, int]:
    if not line.startswith(PUBLICATION_STATS_PREFIX):
        raise ValueError("not a PeTTa publication statistics line")
    fields: dict[str, int] = {}
    for item in line[len(PUBLICATION_STATS_PREFIX):].split():
        key, separator, value = item.partition("=")
        if not separator or not key or not value:
            raise ValueError(f"malformed publication field: {item!r}")
        if key in fields:
            raise ValueError(f"duplicate publication field: {key}")
        fields[key] = int(value)
    if set(fields) != {"answers", "ordered_occurrences"}:
        raise ValueError(f"incomplete publication statistics: {fields!r}")
    if fields["answers"] != fields["ordered_occurrences"]:
        raise ValueError("publication answer/occurrence count disagrees")
    return fields


def extract_publication_stats(
    stderr: str,
) -> tuple[list[dict[str, int]], str]:
    publications: list[dict[str, int]] = []
    ordinary: list[str] = []
    for line in stderr.splitlines(keepends=True):
        payload = line[:-1] if line.endswith("\n") else line
        if payload.startswith(PUBLICATION_STATS_PREFIX):
            publications.append(parse_publication_stats_line(payload))
        else:
            ordinary.append(line)
    return publications, "".join(ordinary)


def extract_observability(
    stderr: str,
) -> tuple[list[dict[str, int]], dict[str, int], str]:
    invocations: list[dict[str, int]] = []
    runtime_counters: dict[str, int] = {}
    ordinary: list[str] = []
    for line in stderr.splitlines(keepends=True):
        payload = line[:-1] if line.endswith("\n") else line
        if payload.startswith(STATS_PREFIX):
            invocations.append(parse_stats_line(payload))
        elif payload.startswith(RUNTIME_COUNTER_PREFIX):
            name, value = parse_runtime_counter_line(payload)
            if name in runtime_counters:
                raise ValueError(f"duplicate runtime counter: {name}")
            runtime_counters[name] = value
        else:
            ordinary.append(line)
    return invocations, runtime_counters, "".join(ordinary)


def extract_stats(stderr: str) -> tuple[list[dict[str, int]], str]:
    invocations: list[dict[str, int]] = []
    ordinary: list[str] = []
    for line in stderr.splitlines(keepends=True):
        payload = line[:-1] if line.endswith("\n") else line
        if payload.startswith(STATS_PREFIX):
            invocations.append(parse_stats_line(payload))
        else:
            ordinary.append(line)
    return invocations, "".join(ordinary)


def extract_machine_and_controller_stats(
    stderr: str,
) -> tuple[
    list[dict[str, int]],
    list[dict[str, int | str]],
    str,
]:
    machine: list[dict[str, int]] = []
    controller: list[dict[str, int | str]] = []
    ordinary: list[str] = []
    for line in stderr.splitlines(keepends=True):
        payload = line[:-1] if line.endswith("\n") else line
        if payload.startswith(STATS_PREFIX):
            machine.append(parse_stats_line(payload))
        elif payload.startswith(CONTROLLER_STATS_PREFIX):
            controller.append(parse_controller_stats_line(payload))
        else:
            ordinary.append(line)
    return machine, controller, "".join(ordinary)


def aggregate_invocations(
    invocations: list[dict[str, int]],
) -> dict[str, int | float]:
    if not invocations:
        raise RuntimeError("run emitted no PETTA_MACHINE_STATS records")
    aggregate: dict[str, int | float] = {"invocations": len(invocations)}
    time_to_first_answer: list[int] = []
    first_answer_transitions: list[int] = []
    for invocation in invocations:
        for key, value in invocation.items():
            if key == "time_to_first_answer_ns":
                time_to_first_answer.append(value)
            elif key == "first_answer_transition":
                first_answer_transitions.append(value)
            elif key.startswith("max_"):
                aggregate[key] = max(int(aggregate.get(key, 0)), value)
            else:
                aggregate[key] = int(aggregate.get(key, 0)) + value
    aggregate["ttfa_ns_median"] = statistics.median(time_to_first_answer)
    aggregate["ttfa_ns_max"] = max(time_to_first_answer)
    aggregate["first_answer_transition_median"] = statistics.median(
        first_answer_transitions
    )
    aggregate["first_answer_transition_max"] = max(first_answer_transitions)
    return aggregate
