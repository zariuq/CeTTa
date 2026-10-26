#!/usr/bin/env python3
"""Fail when the current rho benchmark matrix materially regresses."""

from __future__ import annotations

import argparse
import csv
import math
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--matrix-dir", type=Path, default=root / "runtime")
    parser.add_argument("--baseline-commit", default=EXPECTED_BASELINE_COMMIT)
    parser.add_argument("--baseline-sha256")
    parser.add_argument("--require-paired", action="store_true")
    parser.add_argument("--max-ratio", type=float, default=MAX_MEDIAN_RATIO)
    parser.add_argument("--min-slack", type=float, default=MIN_ABSOLUTE_SLACK_S)
    args = parser.parse_args()
    if (not math.isfinite(args.max_ratio) or args.max_ratio < 1 or
            not math.isfinite(args.min_slack) or args.min_slack < 0):
        parser.error("invalid regression limits")
    if args.baseline_commit != EXPECTED_BASELINE_COMMIT and not (
            args.require_paired and args.baseline_sha256):
        parser.error("another baseline requires paired samples and a pinned binary digest")
    baseline_path = root / "benchmarks/rhocalc_performance_baseline_9050a2d.tsv"
    current_paths = {
        "rhocalc_threaded_standard":
            args.matrix_dir / "rhocalc_threaded_standard_current.csv",
        "rhocalc_cost_threaded_heavy":
            args.matrix_dir / "rhocalc_cost_threaded_heavy_current.csv",
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
        rows = read_rows(current_path)
        current = {row_key(row): row for row in rows}
        if len(current) != len(rows):
            print(f"FAIL {witness} duplicate matrix rows", file=sys.stderr)
            failed += 1
            continue
        witness_baselines = [row for row in baselines if row["witness"] == witness]
        if args.require_paired and set(current) != {row_key(row) for row in witness_baselines}:
            print(f"FAIL {witness} paired matrix differs from the workload inventory", file=sys.stderr)
            failed += 1
            continue
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
                if (baseline_commit != args.baseline_commit or
                        (args.baseline_sha256 and candidate.get("baseline_binary_sha256") != args.baseline_sha256)):
                    print(
                        f"FAIL {witness} paired baseline commit "
                        f"{baseline_commit!r} or binary digest differs from the requested baseline",
                        file=sys.stderr,
                    )
                    failed += 1
                    continue
                base_s = float(paired_baseline)
                baseline_source = "paired"
            elif args.require_paired:
                print(f"FAIL {witness} missing paired baseline {key}", file=sys.stderr)
                failed += 1
                continue
            else:
                base_s = float(baseline["median_s"])
                baseline_source = "recorded"
            current_s = float(candidate["median_s"])
            if not all(math.isfinite(value) and value > 0 for value in (base_s, current_s)):
                print(f"FAIL {witness} nonpositive or nonfinite timing {key}", file=sys.stderr)
                failed += 1
                continue
            limit_s = max(base_s * args.max_ratio, base_s + args.min_slack)
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
