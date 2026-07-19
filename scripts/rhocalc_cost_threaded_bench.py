#!/usr/bin/env python3
"""Measure the public Cost-Rho CLI across sequential and threaded execution."""

from __future__ import annotations

import argparse
import csv
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from rhocalc_bench_provenance import PROVENANCE_FIELDS, benchmark_provenance


@dataclass(frozen=True)
class RunResult:
    status: int
    residual: str
    stderr: str
    elapsed: float


@dataclass(frozen=True)
class Workload:
    name: str
    expression: str
    reactions: int
    expected_purses: int
    note: str


def stack(token: str, count: int) -> str:
    term = "rho:cost:stack-empty"
    for _ in range(count):
        term = f"(rho:cost:stack-cons {token} {term})"
    return term


def reaction(index: int, channel: str, sender: str, receiver: str) -> list[str]:
    return [
        (
            f"(rho:cost:signed (rho:recv {channel} $m{index} "
            f"(rho:cost:signed rho:nil cont{index})) {receiver})"
        ),
        (
            f"(rho:cost:signed (rho:send {channel} "
            f"(rho:cost:signed rho:nil payload{index})) {sender})"
        ),
    ]


def independent_reactions(width: int) -> Workload:
    parts: list[str] = []
    for index in range(width):
        channel = f"pay{index}"
        sender = f"sender{index}"
        receiver = f"receiver{index}"
        parts.extend(reaction(index, channel, sender, receiver))
        parts.append(f"(rho:cost:purse {channel} {stack(receiver, 1)})")
        parts.append(f"(rho:cost:purse {channel} {stack(sender, 1)})")
    return Workload(
        name=f"independent-funded-w{width}",
        expression="(rho:cost:par " + " ".join(parts) + ")",
        reactions=width,
        expected_purses=2 * width,
        note="independent funded reactions; exposes useful cost-wave parallelism",
    )


def shared_purse_reactions(width: int) -> Workload:
    channel = "sharedPay"
    sender = "sharedSender"
    receiver = "sharedReceiver"
    parts: list[str] = []
    for index in range(width):
        parts.extend(reaction(index, channel, sender, receiver))
    parts.extend([
        f"(rho:cost:purse {channel} {stack(receiver, width)})",
        f"(rho:cost:purse {channel} {stack(sender, width)})",
    ])
    return Workload(
        name=f"shared-purse-w{width}",
        expression="(rho:cost:par " + " ".join(parts) + ")",
        reactions=width,
        expected_purses=2,
        note="one funding pair is shared by every reaction; claims must serialize",
    )


def workloads_for_mode(mode: str) -> list[Workload]:
    if mode == "quick":
        return [independent_reactions(32), shared_purse_reactions(12)]
    if mode == "standard":
        return [independent_reactions(128), shared_purse_reactions(24)]
    return [independent_reactions(256), shared_purse_reactions(48)]


def executor_mode(threads: int) -> str:
    return "threaded" if threads > 1 else "sequential"


def run_cost(bin_path: str, input_path: Path, threads: int,
             timeout: float) -> RunResult:
    command = [
        bin_path,
        "--num-threads",
        str(threads),
        "--lang",
        "rhocalc",
        "--profile",
        "cost",
        "--syntax",
        "mrho",
        str(input_path),
    ]
    start = time.perf_counter()
    proc = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    elapsed = time.perf_counter() - start
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    residual = lines[0] if len(lines) == 1 else ""
    return RunResult(proc.returncode, residual, proc.stderr.strip(), elapsed)


def validate(workload: Workload, result: RunResult,
             baseline: str | None) -> tuple[bool, str]:
    if result.status != 0:
        return False, result.stderr or f"exit status {result.status}"
    if not result.residual:
        return False, "public Cost-Rho CLI did not return exactly one residual"
    if result.residual.count("(rho:cost:purse ") != workload.expected_purses:
        return False, "unexpected residual purse multiplicity"
    if result.residual.count("(rho:cost:signed rho:nil ") != workload.reactions:
        return False, "unexpected residual continuation multiplicity"
    forbidden = ("(rho:recv ", "(rho:send ", "rho:cost:stack-cons")
    if any(fragment in result.residual for fragment in forbidden):
        return False, "a redex or funded token survived the completed run"
    if baseline is not None and result.residual != baseline:
        return False, "threaded and sequential canonical residuals differ"
    return True, ""


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Measure the public --lang rhocalc --profile cost executor while "
            "checking sequential/threaded residual agreement."
        )
    )
    parser.add_argument("bin_path")
    parser.add_argument(
        "--mode", choices=["quick", "standard", "heavy"], default="quick"
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument(
        "--require-speedup",
        action="store_true",
        help="fail unless the independent workload improves at the largest thread count",
    )
    parser.add_argument(
        "--spill-dir",
        default="runtime/rhocalc_cost_threaded_bench_inputs",
        help="directory for generated Cost-Rho inputs",
    )
    parser.add_argument("--csv", metavar="PATH", help="write benchmark rows")
    args = parser.parse_args(argv[1:])

    if args.runs < 1:
        parser.error("--runs must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    thread_counts = [int(part) for part in args.threads.split(",") if part]
    if not thread_counts or any(count < 1 for count in thread_counts):
        parser.error("--threads must be a comma-separated list of positive integers")
    if 1 not in thread_counts:
        thread_counts.insert(0, 1)

    spill_dir = Path(args.spill_dir)
    spill_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []
    provenance = benchmark_provenance(args.bin_path)
    ok = True
    independent_speedup = 1.0

    print("PROFILE cost")
    print(f"MODE {args.mode}")
    print(f"RUNS {args.runs}")
    print("THREADS " + ",".join(str(count) for count in thread_counts))
    print()

    for workload in workloads_for_mode(args.mode):
        input_path = spill_dir / f"{args.mode}_{workload.name}.mrho"
        input_path.write_text(workload.expression + "\n")
        print(f"WORKLOAD {workload.name}")
        print(f"NOTE {workload.note}")
        print(f"REACTIONS {workload.reactions}")
        print(f"INPUT {input_path}")

        baseline: str | None = None
        sequential_median: float | None = None
        first_threaded_median: float | None = None
        for threads in thread_counts:
            times: list[float] = []
            failed = False
            for _ in range(args.runs):
                try:
                    result = run_cost(
                        args.bin_path, input_path, threads, args.timeout
                    )
                except (OSError, subprocess.TimeoutExpired) as exc:
                    print(f"FAIL {workload.name} threads={threads}: {exc}")
                    ok = False
                    failed = True
                    break
                valid, detail = validate(workload, result, baseline)
                if not valid:
                    print(f"FAIL {workload.name} threads={threads}: {detail}")
                    ok = False
                    failed = True
                    break
                if baseline is None:
                    baseline = result.residual
                times.append(result.elapsed)
            if failed:
                break
            if not times:
                continue

            median = statistics.median(times)
            best = min(times)
            if threads == 1:
                sequential_median = median
            elif first_threaded_median is None:
                first_threaded_median = median
            if sequential_median is None:
                print(f"FAIL {workload.name}: no valid sequential baseline")
                ok = False
                break
            speedup_vs_sequential = sequential_median / median
            speedup_vs_first_threaded: float | None = None
            if first_threaded_median is not None:
                speedup_vs_first_threaded = first_threaded_median / median
            if (
                workload.name.startswith("independent-funded-")
                and threads == max(thread_counts)
            ):
                independent_speedup = speedup_vs_sequential

            rows.append({
                **provenance,
                "profile": "cost",
                "mode": args.mode,
                "workload": workload.name,
                "reactions": workload.reactions,
                "threads": threads,
                "executor_mode": executor_mode(threads),
                "runs": args.runs,
                "median_s": f"{median:.9f}",
                "best_s": f"{best:.9f}",
                "speedup_vs_seq": f"{speedup_vs_sequential:.6f}",
                "speedup_vs_first_parallel": (
                    f"{speedup_vs_first_threaded:.6f}"
                    if speedup_vs_first_threaded is not None
                    else ""
                ),
                "samples_s": ";".join(f"{sample:.9f}" for sample in times),
                "note": workload.note,
            })
            summary = (
                f"RESULT threads={threads} mode={executor_mode(threads)} "
                f"median_s={median:.6f} best_s={best:.6f} "
                f"speedup_vs_seq={speedup_vs_sequential:.3f}"
            )
            if speedup_vs_first_threaded is not None:
                summary += (
                    " speedup_vs_first_parallel="
                    f"{speedup_vs_first_threaded:.3f}"
                )
            print(summary)
        print()

    if args.require_speedup and independent_speedup <= 1.0:
        print("FAIL independent funded workload did not improve over one thread")
        ok = False

    if args.csv:
        csv_path = Path(args.csv)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_path.open("w", newline="") as handle:
            fieldnames = [
                *PROVENANCE_FIELDS,
                "profile",
                "mode",
                "workload",
                "reactions",
                "threads",
                "executor_mode",
                "runs",
                "median_s",
                "best_s",
                "speedup_vs_seq",
                "speedup_vs_first_parallel",
                "samples_s",
                "note",
            ]
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"CSV {csv_path}")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
