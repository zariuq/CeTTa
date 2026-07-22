#!/usr/bin/env python3
"""Measure the sealed bytes-to-Atom cursor path without its general oracle."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from hashlib import sha256
import json
import math
from pathlib import Path


SIZES = (64, 256, 1024, 4096, 16384)
FAMILIES = ("word", "string", "many-words")
MAX_WORK_SLOPE = 1.15


class CursorHybridProbeFailure(RuntimeError):
    pass


@dataclass(frozen=True)
class CursorHybridGrowthSummary:
    cases: int
    iterations: int
    structural_digest: str
    time_slopes: dict[str, float]
    work_slopes: dict[str, float]
    largest_nanoseconds: dict[str, float]
    largest_work: dict[str, int]


def integer(row: dict[str, object], field: str) -> int:
    try:
        value = int(str(row[field]))
    except (KeyError, ValueError) as error:
        raise CursorHybridProbeFailure(
            f"cursor hybrid probe omitted {field}"
        ) from error
    if value < 0:
        raise CursorHybridProbeFailure(
            f"cursor hybrid probe made {field} negative"
        )
    return value


def log_slope(values: list[int | float]) -> float:
    if len(values) != len(SIZES) or any(value <= 0 for value in values):
        raise CursorHybridProbeFailure("cannot fit cursor hybrid growth slope")
    log_x = [math.log2(value) for value in SIZES]
    log_y = [math.log2(value) for value in values]
    mean_x = sum(log_x) / len(log_x)
    mean_y = sum(log_y) / len(log_y)
    denominator = sum((value - mean_x) ** 2 for value in log_x)
    return sum(
        (x_value - mean_x) * (y_value - mean_y)
        for x_value, y_value in zip(log_x, log_y, strict=True)
    ) / denominator


def source_for(family: str, size: int) -> bytes:
    if family == "word":
        return b"a" * size
    if family == "string":
        return b'"' + b"a" * size + b'"'
    if family == "many-words":
        return b"a " * size
    raise CursorHybridProbeFailure(f"unknown cursor family: {family}")


def records_digest(records: list[dict[str, object]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def run_cursor_hybrid_growth(
    dialect: str,
    directory: Path,
    iterations: int,
    expected_digest: str,
    execute: Callable[[Path, int], dict[str, object]],
    validate_authority: Callable[[str, int, Path], None],
) -> CursorHybridGrowthSummary:
    if not dialect or iterations <= 0:
        raise CursorHybridProbeFailure("bad cursor hybrid probe arguments")
    records: list[dict[str, object]] = []
    timings: dict[str, list[float]] = {family: [] for family in FAMILIES}
    work: dict[str, list[int]] = {family: [] for family in FAMILIES}

    for family in FAMILIES:
        for size in SIZES:
            source = source_for(family, size)
            input_path = directory / f"{dialect}-cursor-{family}-{size}.metta"
            input_path.write_bytes(source)
            row = execute(input_path, iterations)
            validate_authority(family, size, input_path)
            fallback_runs = integer(row, "prepared-fallback-runs")
            fallback_work = integer(row, "prepared-fallback-work")
            value_runs = integer(row, "value-program-runs")
            value_work = integer(row, "value-parser-work")
            nanoseconds = integer(
                row, "benchmark-cursor-hybrid-bytes-nanoseconds"
            )
            total_work = (
                integer(row, "dfa-work")
                + integer(row, "parser-work")
                + value_runs
                + value_work
            )
            semantic = row.get("semantic-result")
            if (
                row.get("outcome") != "accepted"
                or row.get("benchmark-kind") != "cursor-hybrid-bytes"
                or integer(row, "benchmark-iterations") != iterations
                or integer(row, "input-bytes") != len(source)
                or integer(row, "source-passes") != 1
                or integer(
                    row, "benchmark-cursor-hybrid-source-passes"
                ) != 1
                or nanoseconds <= 0
                or total_work <= 0
                or not isinstance(semantic, str)
                or not semantic
                or fallback_runs != 0
                or fallback_work != 0
                or value_runs <= 0
            ):
                raise CursorHybridProbeFailure(
                    f"{dialect} {family} cursor receipt changed at {size}"
                )
            per_iteration = nanoseconds / iterations
            timings[family].append(per_iteration)
            work[family].append(total_work)
            record = {
                "dfa_work": integer(row, "dfa-work"),
                "fallback_runs": fallback_runs,
                "fallback_work": fallback_work,
                "family": family,
                "input_bytes": len(source),
                "parser_work": integer(row, "parser-work"),
                "semantic_digest": sha256(semantic.encode("utf-8")).hexdigest(),
                "size": size,
                "tokens": integer(row, "tokens"),
                "trace": row.get("trace-digest"),
                "value_runs": value_runs,
                "value_work": value_work,
            }
            records.append(record)
            print(
                "cursor-hybrid-growth-case\t"
                f"{dialect}\t{family}\t{size}\t{len(source)}\t"
                f"{per_iteration:.0f}ns\t{total_work}work\t"
                f"{fallback_runs}fallback"
            )

    time_slopes = {
        family: log_slope(timings[family]) for family in FAMILIES
    }
    work_slopes = {
        family: log_slope(work[family]) for family in FAMILIES
    }
    for family, slope in work_slopes.items():
        if slope > MAX_WORK_SLOPE:
            raise CursorHybridProbeFailure(
                f"{dialect} {family} cursor work slope {slope:.6f} "
                f"exceeds {MAX_WORK_SLOPE:.2f}"
            )
    for family in FAMILIES:
        print(
            "cursor-hybrid-growth-slope\t"
            f"{dialect}\t{family}\twall\t{time_slopes[family]:.6f}"
        )
        print(
            "cursor-hybrid-growth-slope\t"
            f"{dialect}\t{family}\twork\t{work_slopes[family]:.6f}"
        )
    digest = records_digest(records)
    if not expected_digest:
        print("CURSOR_HYBRID_GROWTH_RECORDS=" + json.dumps(
            records, ensure_ascii=False, sort_keys=True
        ))
        print("CURSOR_HYBRID_GROWTH_DIGEST=" + digest)
        raise CursorHybridProbeFailure(
            f"{dialect} cursor hybrid growth seal is not ratified"
        )
    if digest != expected_digest:
        raise CursorHybridProbeFailure(
            f"{dialect} cursor hybrid growth structure changed: {digest}"
        )
    return CursorHybridGrowthSummary(
        cases=len(records),
        iterations=iterations,
        structural_digest=digest,
        time_slopes=time_slopes,
        work_slopes=work_slopes,
        largest_nanoseconds={
            family: timings[family][-1] for family in FAMILIES
        },
        largest_work={family: work[family][-1] for family in FAMILIES},
    )
