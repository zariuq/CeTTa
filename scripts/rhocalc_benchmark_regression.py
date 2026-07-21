#!/usr/bin/env python3
"""Fail when the current rho benchmark matrix materially regresses."""

from __future__ import annotations

import csv
import sys
from pathlib import Path

from rhocalc_paired_samples import (
    MATERIAL_MAX_MEDIAN_RATIO,
    MATERIAL_MIN_ABSOLUTE_SLACK_S,
)

MAX_MEDIAN_RATIO = MATERIAL_MAX_MEDIAN_RATIO
MIN_ABSOLUTE_SLACK_S = MATERIAL_MIN_ABSOLUTE_SLACK_S
EXPECTED_BASELINE_COMMIT = "9050a2d171946f6edf417152f259b7bc4e98907d"


def row_key(row: dict[str, str]) -> tuple[str, str, int, str]:
    return (
        row["mode"],
        row["workload"],
        int(row["threads"]),
        row["executor_mode"],
    )


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    baseline_path = root / "benchmarks/rhocalc_performance_baseline_9050a2d.tsv"
    current_paths = {
        "rhocalc_threaded_standard":
            root / "runtime/rhocalc_threaded_standard_current.csv",
        "rhocalc_cost_threaded_heavy":
            root / "runtime/rhocalc_cost_threaded_heavy_current.csv",
    }
    if not baseline_path.is_file():
        print(f"FAIL missing rho benchmark baseline: {baseline_path}", file=sys.stderr)
        return 2

    with baseline_path.open(newline="", encoding="utf-8") as handle:
        baselines = list(csv.DictReader(handle, delimiter="\t"))

    failed = 0
    checked = 0
    for witness, current_path in current_paths.items():
        if not current_path.is_file():
            print(f"FAIL missing current rho benchmark matrix: {current_path}", file=sys.stderr)
            failed += 1
            continue
        current = {row_key(row): row for row in read_rows(current_path)}
        witness_baselines = [row for row in baselines if row["witness"] == witness]
        for baseline in witness_baselines:
            key = row_key(baseline)
            candidate = current.get(key)
            if candidate is None:
                print(f"FAIL {witness} missing row {key}", file=sys.stderr)
                failed += 1
                continue
            paired_baseline = candidate.get("baseline_median_s", "")
            if paired_baseline:
                baseline_commit = candidate.get("baseline_commit", "")
                if baseline_commit != EXPECTED_BASELINE_COMMIT:
                    print(
                        f"FAIL {witness} paired baseline commit "
                        f"{baseline_commit!r} is not {EXPECTED_BASELINE_COMMIT}",
                        file=sys.stderr,
                    )
                    failed += 1
                    continue
                base_s = float(paired_baseline)
                baseline_source = "paired"
            else:
                base_s = float(baseline["median_s"])
                baseline_source = "recorded"
            current_s = float(candidate["median_s"])
            limit_s = max(
                base_s * MAX_MEDIAN_RATIO,
                base_s + MIN_ABSOLUTE_SLACK_S,
            )
            ratio = current_s / base_s
            checked += 1
            status = "PASS" if current_s <= limit_s else "FAIL"
            print(
                f"{status} {witness} mode={key[0]} workload={key[1]} "
                f"threads={key[2]} executor={key[3]} median_s={current_s:.9f} "
                f"baseline_s={base_s:.9f} baseline_source={baseline_source} "
                f"ratio={ratio:.3f} limit_s={limit_s:.9f}"
            )
            if current_s > limit_s:
                failed += 1

    if failed:
        print(
            f"SUMMARY rho-benchmark-regression {checked} checked, {failed} failed",
            file=sys.stderr,
        )
        return 1
    print(f"SUMMARY rho-benchmark-regression {checked}/{len(baselines)} PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
