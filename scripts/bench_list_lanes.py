#!/usr/bin/env python3
"""Median-timed, result-checked benchmark for expression and inductive lists."""

from __future__ import annotations

import argparse
from pathlib import Path
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "runtime" / "list-lane-bench"
KERNELS = ("build-sum", "reverse-sum", "append-sum", "map-sum")
MODES = ("list", "clist")


def expected_value(kernel: str, size: int) -> int:
    if kernel in ("build-sum", "reverse-sum"):
        return size * (size - 1) // 2
    if kernel == "append-sum":
        return size * (size - 1)
    if kernel == "map-sum":
        return size * (size + 1) // 2
    raise ValueError(kernel)


def lane_arguments(lane: str) -> list[str]:
    if lane == "he":
        return ["--profile", "he-extended", "--lang", "he"]
    if lane == "prime":
        return ["--lang", "prime"]
    raise ValueError(lane)


def run_once(cetta: Path, lane: str, driver: Path, expected: int) -> float:
    started = time.perf_counter()
    completed = subprocess.run(
        [str(cetta), *lane_arguments(lane), str(driver)],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
        check=False,
    )
    elapsed = time.perf_counter() - started
    output = completed.stdout.strip()
    if completed.returncode != 0 or output != f"[()]\n[{expected}]":
        raise RuntimeError(
            f"{driver.name}:{lane} expected {expected}, exit="
            f"{completed.returncode}, output={output!r}"
        )
    return elapsed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("sizes", nargs="*", type=int, default=[100, 500])
    parser.add_argument("--cetta", type=Path, default=ROOT / "cetta")
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--repetitions", type=int, default=5)
    args = parser.parse_args()
    cetta = args.cetta.resolve()
    if not cetta.is_file() or args.warmups < 0 or args.repetitions < 1:
        return 2
    if any(size < 0 for size in args.sizes):
        print("sizes must be non-negative", file=sys.stderr)
        return 2

    WORK.mkdir(parents=True, exist_ok=True)
    print("kind    lane   mode   kernel          n       trial   seconds")
    for size in args.sizes:
        for lane in ("he", "prime"):
            for kernel in KERNELS:
                for mode in MODES:
                    expected = expected_value(kernel, size)
                    driver = WORK / f"{lane}_{mode}_{kernel}_{size}.metta"
                    driver.write_text(
                        "!(import! &self ../../benchmarks/list_lanes.metta)\n"
                        f"!(bench:{mode}-{kernel} {size})\n"
                    )
                    try:
                        for _ in range(args.warmups):
                            run_once(cetta, lane, driver, expected)
                        samples = [
                            run_once(cetta, lane, driver, expected)
                            for _ in range(args.repetitions)
                        ]
                    except (RuntimeError, subprocess.TimeoutExpired) as error:
                        print(f"FAIL: {error}", file=sys.stderr)
                        return 1
                    for trial, sample in enumerate(samples, start=1):
                        print(
                            f"RAW     {lane:<6} {mode:<6} {kernel:<15} "
                            f"{size:<7} {trial:<7} {sample:.6f}"
                        )
                    print(
                        f"MEDIAN  {lane:<6} {mode:<6} {kernel:<15} "
                        f"{size:<7} {'-':<7} "
                        f"{statistics.median(samples):.6f}"
                    )
    print("(ListLaneBenchmarkSummary CorrectnessChecked MedianTimed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
