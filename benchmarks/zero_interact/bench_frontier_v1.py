#!/usr/bin/env python3
"""Measure one indexed frontier/projection workload across CeTTa realizations.

The Zero interaction program reads an occurrence bag without changing its
world.  The MM2 program performs the nearest support-valued transaction and
publishes the projection.  Their final worlds are therefore different; this
benchmark compares end-to-end frontier production, not semantic equivalence.
"""

from __future__ import annotations

import argparse
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def write_programs(
    directory: Path, facts: int, queries: int, mode: str
) -> tuple[Path, Path, int]:
    rows = "".join(f"(fact k{index:08d})\n" for index in range(facts))
    if mode == "selective":
        target = f"k{facts - 1:08d}"
        interact_query = "".join(
            f"(match &self (fact {target}) (seen q{index:04d} {target}))\n"
            for index in range(queries)
        )
        mm2_query = "".join(
            "\n"
            f"(exec (0 frontier q{index:04d})\n"
            f"      (, (fact {target}))\n"
            f"      (, (seen q{index:04d} {target})))\n"
            for index in range(queries)
        )
        expected_answers = queries
    else:
        interact_query = "".join(
            f"(match &self (fact $x) (seen q{index:04d} $x))\n"
            for index in range(queries)
        )
        mm2_query = "".join(
            "\n"
            f"(exec (0 frontier q{index:04d})\n"
            "      (, (fact $x))\n"
            f"      (, (seen q{index:04d} $x)))\n"
            for index in range(queries)
        )
        expected_answers = facts * queries
    interact = directory / "frontier.metta"
    interact.write_text(rows + interact_query, encoding="utf-8")
    mm2 = directory / "frontier.mm2"
    mm2.write_text(rows + mm2_query, encoding="utf-8")
    return interact, mm2, expected_answers


def run_checked(command: list[str], expected_answers: int) -> None:
    completed = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command exited {completed.returncode}: {' '.join(command)}\n"
            f"{completed.stderr}"
        )
    actual_answers = completed.stdout.count("(seen ")
    if actual_answers != expected_answers:
        raise RuntimeError(
            f"expected {expected_answers} projected answers, got "
            f"{actual_answers}: {' '.join(command)}"
        )


def measure(command: list[str], warmup: int, runs: int) -> list[float]:
    for _ in range(warmup):
        subprocess.run(
            command,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    samples = []
    for _ in range(runs):
        start = time.perf_counter_ns()
        subprocess.run(
            command,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        samples.append((time.perf_counter_ns() - start) / 1_000_000.0)
    return samples


def percentile95(samples: list[float]) -> float:
    ordered = sorted(samples)
    return ordered[(95 * len(ordered) - 1) // 100]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, default=Path("./cetta"))
    parser.add_argument("--facts", type=int, default=1000)
    parser.add_argument("--queries", type=int, default=1)
    parser.add_argument(
        "--mode", choices=("frontier", "selective"), default="frontier"
    )
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--runs", type=int, default=20)
    args = parser.parse_args()
    if args.facts < 1 or args.queries < 1 or args.warmup < 0 or args.runs < 1:
        parser.error(
            "facts, queries, and runs must be positive; warmup must be nonnegative"
        )
    cetta = str(args.cetta.resolve())

    with tempfile.TemporaryDirectory(prefix="cetta-frontier-v1-") as raw:
        interact, mm2, expected_answers = write_programs(
            Path(raw), args.facts, args.queries, args.mode
        )
        cases = [
            (
                "zero-interact/native-c",
                [
                    cetta,
                    "--lang", "zero",
                    "--profile", "interact",
                    "--space-engine", "native",
                    "--gslt-realization", "compiled-worklist",
                    str(interact),
                ],
            ),
            (
                "zero-interact/rust-pathmap-c-abi",
                [
                    cetta,
                    "--lang", "zero",
                    "--profile", "interact",
                    "--space-engine", "pathmap",
                    "--gslt-realization", "compiled-worklist",
                    str(interact),
                ],
            ),
            (
                "mm2/upstream-rust-pathmap-c-abi",
                [
                    cetta,
                    "--lang", "mm2",
                    "--steps", str(args.queries),
                    str(mm2),
                ],
            ),
            (
                "mm2-gslt/native-c",
                [
                    cetta,
                    "--lang", "mm2",
                    "--profile", "gslt",
                    "--space-engine", "native",
                    "--steps", str(args.queries),
                    str(mm2),
                ],
            ),
            (
                "mm2-gslt/rust-pathmap-c-abi",
                [
                    cetta,
                    "--lang", "mm2",
                    "--profile", "gslt",
                    "--space-engine", "pathmap",
                    "--steps", str(args.queries),
                    str(mm2),
                ],
            ),
        ]

        for _, command in cases:
            run_checked(command, expected_answers)

        print(
            "case\tmode\tfacts\tqueries\truns\tmedian_ms\tmean_ms\tp95_ms\tmin_ms\tmax_ms"
        )
        for label, command in cases:
            samples = measure(command, args.warmup, args.runs)
            print(
                f"{label}\t{args.mode}\t{args.facts}\t{args.queries}\t{args.runs}\t"
                f"{statistics.median(samples):.3f}\t"
                f"{statistics.fmean(samples):.3f}\t"
                f"{percentile95(samples):.3f}\t"
                f"{min(samples):.3f}\t{max(samples):.3f}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
