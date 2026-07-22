#!/usr/bin/env python3
"""Measure prepared SLR/GLL/GLR growth on a stable reader lattice."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from hashlib import sha256
import json
import math
from pathlib import Path
import time


TOKEN_COUNTS = (256, 512, 1024, 2048, 4096)
FLAT_LEAF_COUNT = 2048
BACKENDS = ("slr", "gll", "glr")
STRUCTURAL_METRICS = (
    "work_items",
    "graph_nodes",
    "forest_nodes",
    "forest_choices",
)
MAX_STRUCTURAL_SLOPE = 1.15
MAX_RETAINED_PAYLOAD_SLOPE = 1.15
RETAINED_PAYLOAD_COMPONENTS = (
    "source",
    "token-projection",
    "lattice",
    "gll-witness",
    "glr-witness",
    "gll-relation",
    "glr-relation",
    "slr-result",
    "gll-result",
    "glr-result",
)
EXPECTED_STRUCTURAL_DIGESTS = {
    "he": (
        "93bede3cb77193289f3572c3a589adbb75b31dcefa9a646de8db4d9b16faf31b"
    ),
    "petta": (
        "95761081d648dc4d2b60b2aeb1942f3cc062a1f8166746610a515e0b479c97c5"
    ),
}


class FinalForestProbeFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class FinalForestGrowthSummary:
    cases: int
    iterations: int
    structural_digest: str
    process_time_slope: float
    time_slopes: dict[str, float]
    structural_slopes: dict[str, float]
    retained_payload_slope: float
    largest_retained_payload_bytes: int
    largest_process_nanoseconds: int
    largest_nanoseconds: dict[str, float]
    cursor_hybrid_time_slope: float | None
    cursor_hybrid_work_slope: float | None
    largest_cursor_hybrid_nanoseconds: float | None
    largest_cursor_hybrid_work: int | None


@dataclass(frozen=True)
class FlatReplaySummary:
    leaves: int
    input_bytes: int
    iterations: int
    authority_agreements: int
    reference_agreements: int
    result_digest: str


def integer(row: dict[str, object], field: str) -> int:
    try:
        value = int(str(row[field]))
    except (KeyError, ValueError) as error:
        raise FinalForestProbeFailure(
            f"prepared final-forest probe omitted {field}"
        ) from error
    if value < 0:
        raise FinalForestProbeFailure(
            f"prepared final-forest probe made {field} negative"
        )
    return value


def retained_payload(
    row: dict[str, object], prefix: str
) -> tuple[dict[str, int], int]:
    components = {
        component: integer(
            row, f"{prefix}-{component}-bytes"
        )
        for component in RETAINED_PAYLOAD_COMPONENTS
    }
    total = integer(row, f"{prefix}-total-bytes")
    if total <= 0 or total != sum(components.values()):
        raise FinalForestProbeFailure(
            f"{prefix} payload accounting is inconsistent"
        )
    return components, total


def log_slope(xs: tuple[int, ...], ys: list[int | float]) -> float:
    if len(xs) != len(ys) or len(xs) < 2 or any(value <= 0 for value in ys):
        raise FinalForestProbeFailure("cannot fit prepared forest growth slope")
    log_x = [math.log2(value) for value in xs]
    log_y = [math.log2(value) for value in ys]
    mean_x = sum(log_x) / len(log_x)
    mean_y = sum(log_y) / len(log_y)
    denominator = sum((value - mean_x) ** 2 for value in log_x)
    if denominator == 0:
        raise FinalForestProbeFailure("prepared forest slope has no size range")
    return sum(
        (x_value - mean_x) * (y_value - mean_y)
        for x_value, y_value in zip(log_x, log_y, strict=True)
    ) / denominator


def records_digest(records: list[dict[str, object]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def balanced_form(token_count: int) -> bytes:
    if token_count <= 0 or token_count & (token_count - 1):
        raise FinalForestProbeFailure(
            "balanced-form leaf count must be a power of two"
        )
    form = b"()"
    leaves = 1
    while leaves < token_count:
        form = b"(" + form + form + b")"
        leaves *= 2
    return form


def flat_form(leaf_count: int) -> bytes:
    if leaf_count <= 0:
        raise FinalForestProbeFailure("flat-form leaf count must be positive")
    return b"(" + b"()" * leaf_count + b")"


def run_flat_replay_canary(
    dialect: str,
    directory: Path,
    iterations: int,
    execute: Callable[[Path, int], dict[str, object]],
    validate_authority: Callable[
        [Path, int, dict[str, object]], tuple[int, int]
    ],
) -> FlatReplaySummary:
    source = flat_form(FLAT_LEAF_COUNT)
    input_path = directory / f"{dialect}-flat-{FLAT_LEAF_COUNT}.metta"
    input_path.write_bytes(source)
    row = execute(input_path, iterations)
    if (
        row.get("benchmark-kind") != "prepared-final-forest-kernels"
        or integer(row, "benchmark-iterations") != iterations
        or row.get("slr-shadow-outcome") != "accepted"
        or row.get("gll-decision") != "accepted"
        or row.get("glr-decision") != "accepted"
        or row.get("gll-forest-digest") != row.get("glr-forest-digest")
        or row.get("gll-forest-digest")
        != row.get("slr-shadow-forest-digest")
        or row.get("gll-result") != row.get("glr-result")
        or integer(row, "input-bytes") != len(source)
    ):
        raise FinalForestProbeFailure(
            f"{dialect} flat replay canary changed semantics"
        )
    authority_agreements, reference_agreements = validate_authority(
        input_path, FLAT_LEAF_COUNT, row
    )
    results = row.get("gll-result")
    if not isinstance(results, list) or not results:
        raise FinalForestProbeFailure(
            f"{dialect} flat replay canary omitted its semantic result"
        )
    result_digest = sha256(json.dumps(
        results,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")).hexdigest()
    print(
        "prepared-final-forest-flat-replay\t"
        f"{dialect}\t{FLAT_LEAF_COUNT}\t{len(source)}\t"
        f"{integer(row, 'projected-tokens')}\t{result_digest}"
    )
    return FlatReplaySummary(
        leaves=FLAT_LEAF_COUNT,
        input_bytes=len(source),
        iterations=iterations,
        authority_agreements=authority_agreements,
        reference_agreements=reference_agreements,
        result_digest=result_digest,
    )


def run_growth_probe(
    dialect: str,
    directory: Path,
    iterations: int,
    execute: Callable[[Path, int], dict[str, object]],
    validate_authority: Callable[
        [Path, int, dict[str, object]], None
    ],
) -> FinalForestGrowthSummary:
    if not dialect or iterations <= 0:
        raise FinalForestProbeFailure("bad prepared forest probe arguments")
    records: list[dict[str, object]] = []
    timings: dict[str, list[float]] = {backend: [] for backend in BACKENDS}
    process_timings: list[int] = []
    retained_payloads: list[int] = []
    cursor_hybrid_available: bool | None = None
    cursor_hybrid_timings: list[float] = []
    cursor_hybrid_work: list[int] = []

    for token_count in TOKEN_COUNTS:
        source = balanced_form(token_count)
        input_path = directory / f"{dialect}-balanced-{token_count}.metta"
        input_path.write_bytes(source)
        process_start = time.monotonic_ns()
        row = execute(input_path, iterations)
        process_nanoseconds = time.monotonic_ns() - process_start
        process_timings.append(process_nanoseconds)
        if (
            row.get("benchmark-kind")
            != "prepared-final-forest-kernels"
            or integer(row, "benchmark-iterations") != iterations
            or row.get("slr-shadow-outcome") != "accepted"
            or row.get("gll-decision") != "accepted"
            or row.get("glr-decision") != "accepted"
            or row.get("gll-forest-digest")
            != row.get("glr-forest-digest")
            or row.get("gll-forest-digest")
            != row.get("slr-shadow-forest-digest")
            or integer(row, "input-bytes") != len(source)
        ):
            raise FinalForestProbeFailure(
                f"{dialect} prepared forest probe changed semantics at "
                f"{token_count} tokens"
            )
        validate_authority(input_path, token_count, row)
        retained_components, retained_total = retained_payload(
            row, "logical-retained"
        )
        replay_components, replay_total = retained_payload(
            row, "view-replay-logical-retained"
        )
        if (
            replay_components != retained_components
            or replay_total != retained_total
        ):
            raise FinalForestProbeFailure(
                f"{dialect} scalar-view replay changed retained payload"
            )
        retained_payloads.append(retained_total)
        record: dict[str, object] = {
            "dialect": dialect,
            "input_bytes": len(source),
            "projected_tokens": integer(row, "projected-tokens"),
            "token_count": token_count,
        }
        timing_fields: list[str] = []
        for backend in BACKENDS:
            total_nanoseconds = integer(
                row, f"benchmark-{backend}-nanoseconds"
            )
            if total_nanoseconds <= 0:
                raise FinalForestProbeFailure(
                    f"{dialect} {backend} timer did not advance"
                )
            per_iteration = total_nanoseconds / iterations
            timings[backend].append(per_iteration)
            timing_fields.append(f"{backend}={per_iteration:.0f}ns")
            for wire_name, record_name in (
                ("work-items", "work_items"),
                ("graph-nodes", "graph_nodes"),
                ("stack-nodes", "stack_nodes"),
                ("forest-nodes", "forest_nodes"),
                ("forest-choices", "forest_choices"),
                ("forest-roots", "forest_roots"),
            ):
                record[f"{backend}_{record_name}"] = integer(
                    row, f"benchmark-{backend}-{wire_name}"
                )
        hybrid_available = (
            "benchmark-cursor-hybrid-bytes-nanoseconds" in row
        )
        if cursor_hybrid_available is None:
            cursor_hybrid_available = hybrid_available
        elif cursor_hybrid_available != hybrid_available:
            raise FinalForestProbeFailure(
                f"{dialect} hybrid cursor benchmark availability changed"
            )
        if hybrid_available:
            hybrid_total_nanoseconds = integer(
                row, "benchmark-cursor-hybrid-bytes-nanoseconds"
            )
            hybrid_work = sum(
                integer(row, field)
                for field in (
                    "cursor-program-dfa-work",
                    "cursor-program-parser-work",
                    "cursor-program-semantic-terminal-values",
                    "cursor-program-semantic-action-instructions",
                    "cursor-program-hybrid-value-program-runs",
                    "cursor-program-hybrid-value-terminal-values",
                    "cursor-program-hybrid-value-action-instructions",
                    "cursor-program-hybrid-value-parser-work",
                )
            )
            if (
                hybrid_total_nanoseconds <= 0
                or hybrid_work <= 0
                or integer(
                    row, "benchmark-cursor-hybrid-source-passes"
                ) != 1
                or row.get("cursor-program-hybrid-bytes-agreement") != "1"
            ):
                raise FinalForestProbeFailure(
                    f"{dialect} hybrid cursor benchmark receipt changed"
                )
            hybrid_per_iteration = hybrid_total_nanoseconds / iterations
            cursor_hybrid_timings.append(hybrid_per_iteration)
            cursor_hybrid_work.append(hybrid_work)
            timing_fields.append(
                f"cursor-hybrid={hybrid_per_iteration:.0f}ns"
            )
        records.append(record)
        print(
            "prepared-final-forest-case\t"
            f"{dialect}\t{token_count}\t{len(source)}\t"
            f"{record['projected_tokens']}\t"
            + "\t".join(timing_fields)
            + "\t"
            + "\t".join(
                f"retained-{component}={value}B"
                for component, value in retained_components.items()
            )
            + f"\tretained-total={retained_total}B"
            + f"\tprocess={process_nanoseconds}ns"
        )

    process_time_slope = log_slope(TOKEN_COUNTS, process_timings)
    retained_payload_slope = log_slope(TOKEN_COUNTS, retained_payloads)
    if retained_payload_slope > MAX_RETAINED_PAYLOAD_SLOPE:
        raise FinalForestProbeFailure(
            f"{dialect} retained payload growth slope "
            f"{retained_payload_slope:.6f} exceeds "
            f"{MAX_RETAINED_PAYLOAD_SLOPE:.2f}"
        )
    time_slopes = {
        backend: log_slope(TOKEN_COUNTS, timings[backend])
        for backend in BACKENDS
    }
    cursor_hybrid_time_slope = (
        log_slope(TOKEN_COUNTS, cursor_hybrid_timings)
        if cursor_hybrid_available else None
    )
    cursor_hybrid_work_slope = (
        log_slope(TOKEN_COUNTS, cursor_hybrid_work)
        if cursor_hybrid_available else None
    )
    if (
        cursor_hybrid_work_slope is not None
        and cursor_hybrid_work_slope > MAX_STRUCTURAL_SLOPE
    ):
        raise FinalForestProbeFailure(
            f"{dialect} hybrid cursor work growth slope "
            f"{cursor_hybrid_work_slope:.6f} exceeds "
            f"{MAX_STRUCTURAL_SLOPE:.2f}"
        )
    structural_slopes: dict[str, float] = {}
    for backend in BACKENDS:
        for metric in STRUCTURAL_METRICS:
            field = f"{backend}_{metric}"
            slope = log_slope(
                TOKEN_COUNTS, [int(record[field]) for record in records]
            )
            structural_slopes[field] = slope
            if slope > MAX_STRUCTURAL_SLOPE:
                raise FinalForestProbeFailure(
                    f"{dialect} {field} growth slope {slope:.6f} "
                    f"exceeds {MAX_STRUCTURAL_SLOPE:.2f}"
                )
    for backend, slope in time_slopes.items():
        print(
            f"prepared-final-forest-slope\t{dialect}\t"
            f"{backend}-wall\t{slope:.6f}"
        )
    if cursor_hybrid_time_slope is not None:
        print(
            f"prepared-final-forest-slope\t{dialect}\t"
            f"cursor-hybrid-wall\t{cursor_hybrid_time_slope:.6f}"
        )
    if cursor_hybrid_work_slope is not None:
        print(
            f"prepared-final-forest-slope\t{dialect}\t"
            f"cursor-hybrid-work\t{cursor_hybrid_work_slope:.6f}"
        )
    print(
        f"prepared-final-forest-slope\t{dialect}\t"
        f"diagnostic-process-wall\t{process_time_slope:.6f}"
    )
    print(
        f"prepared-final-forest-slope\t{dialect}\t"
        f"logical-retained-payload\t{retained_payload_slope:.6f}"
    )
    for field, slope in structural_slopes.items():
        print(
            f"prepared-final-forest-slope\t{dialect}\t"
            f"{field}\t{slope:.6f}"
        )
    digest = records_digest(records)
    expected_digest = EXPECTED_STRUCTURAL_DIGESTS.get(dialect)
    if expected_digest is None or digest != expected_digest:
        raise FinalForestProbeFailure(
            f"{dialect} prepared forest structure changed: {digest}"
        )
    print(f"prepared-final-forest-structural-digest\t{dialect}\t{digest}")
    return FinalForestGrowthSummary(
        cases=len(records),
        iterations=iterations,
        structural_digest=digest,
        process_time_slope=process_time_slope,
        time_slopes=time_slopes,
        structural_slopes=structural_slopes,
        retained_payload_slope=retained_payload_slope,
        largest_retained_payload_bytes=retained_payloads[-1],
        largest_process_nanoseconds=process_timings[-1],
        largest_nanoseconds={
            backend: timings[backend][-1] for backend in BACKENDS
        },
        cursor_hybrid_time_slope=cursor_hybrid_time_slope,
        cursor_hybrid_work_slope=cursor_hybrid_work_slope,
        largest_cursor_hybrid_nanoseconds=(
            cursor_hybrid_timings[-1]
            if cursor_hybrid_timings else None
        ),
        largest_cursor_hybrid_work=(
            cursor_hybrid_work[-1] if cursor_hybrid_work else None
        ),
    )
