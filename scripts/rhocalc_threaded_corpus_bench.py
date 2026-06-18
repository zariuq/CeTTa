#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from rhocalc_m3_rholang_cli_compare import (
    expected_observation_sets,
    extract_single_mrho_result,
    output_observations,
)
from rhocalc_tiny_semantics import parse_mrho_proc


@dataclass(frozen=True)
class CorpusWorkload:
    name: str
    fixture: Path
    expected: set[tuple[str, ...]]
    note: str


@dataclass(frozen=True)
class BuildResult:
    status: int
    mrho: str
    stderr: str
    elapsed: float


@dataclass(frozen=True)
class ExecResult:
    status: int
    residual: str
    stderr: str
    elapsed: float


CORE_WORKLOADS = [
    "metta-computed-payload",
    "metta-reflection-loader",
    "metta-private-session",
    "metta-threshold-quorum",
    "metta-registry-lookup",
    "metta-route-synthesis",
    "metta-job-queue-collector",
]


WORKLOAD_NOTES = {
    "metta-computed-payload":
        "computed rho payloads built from MeTTa data before strict-core execution",
    "metta-reflection-loader":
        "quoted-code loader that exposes reflection and drop/materialization cost",
    "metta-private-session":
        "private request/reply session with no public name leakage",
    "metta-threshold-quorum":
        "MeTTa-driven quorum decision over structured certificate data",
    "metta-registry-lookup":
        "registry-routed service resolution over MeTTa-managed topology data",
    "metta-route-synthesis":
        "synthesized routing skeleton with multiple public coordination sites",
    "metta-job-queue-collector":
        "exactly-once worker/collector coordination; clearest contract-style example",
}


def summarize_times(times: list[float]) -> tuple[float, float]:
    return statistics.median(times), min(times)


def executor_mode_for_threads(threads: int) -> str:
    return "threaded" if threads > 1 else "sequential"


def load_manifest_workloads(
    manifest_path: Path,
    *,
    names: list[str] | None,
) -> list[CorpusWorkload]:
    workloads: list[CorpusWorkload] = []
    with manifest_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle, delimiter="\t")
        for row in reader:
            if not row or row[0].startswith("#"):
                continue
            if len(row) < 4:
                raise ValueError(f"malformed manifest row: {row!r}")
            name, syntax, fixture, expected_spec = row[:4]
            if syntax != "metta":
                continue
            if names is not None and name not in names:
                continue
            workloads.append(
                CorpusWorkload(
                    name=name,
                    fixture=Path(fixture),
                    expected=expected_observation_sets(expected_spec),
                    note=WORKLOAD_NOTES.get(name, "validated MeTTa-built rho example"),
                )
            )
    if names is not None:
        by_name = {workload.name: workload for workload in workloads}
        missing = [name for name in names if name not in by_name]
        if missing:
            raise ValueError(f"manifest missing workloads: {', '.join(missing)}")
        workloads = [by_name[name] for name in names]
    return workloads


def run_metta_build(bin_path: str, fixture: Path) -> BuildResult:
    start = time.perf_counter()
    proc = subprocess.run(
        [bin_path, str(fixture)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    elapsed = time.perf_counter() - start
    mrho = ""
    if proc.returncode == 0:
        try:
            mrho = extract_single_mrho_result(proc.stdout)
        except RuntimeError:
            mrho = ""
    return BuildResult(
        status=proc.returncode,
        mrho=mrho,
        stderr=proc.stderr.strip(),
        elapsed=elapsed,
    )


def run_rhocalc_file(bin_path: str, input_path: Path, *, threads: int) -> ExecResult:
    cmd = [
        bin_path,
        "--num-threads",
        str(threads),
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
        str(input_path),
    ]
    start = time.perf_counter()
    proc = subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    elapsed = time.perf_counter() - start
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    residual = lines[0] if lines else ""
    return ExecResult(
        status=proc.returncode,
        residual=residual,
        stderr=proc.stderr.strip(),
        elapsed=elapsed,
    )


def validate_execution(workload: CorpusWorkload, result: ExecResult) -> tuple[bool, str]:
    if result.status != 0:
        return False, result.stderr or result.residual or "nonzero exit"
    if not result.residual:
        return False, "no residual output"
    observed = output_observations(parse_mrho_proc(result.residual))
    if observed not in workload.expected:
        expected = ";".join(
            "empty" if not item else ",".join(item)
            for item in sorted(workload.expected)
        )
        got = "empty" if not observed else ",".join(observed)
        return False, f"expected one of {expected}; got {got}"
    return True, ""


def mrho_output_path(spill_dir: Path, workload: CorpusWorkload) -> Path:
    safe = "".join(
        char if char.isalnum() or char in "-_" else "_"
        for char in workload.name
    )
    return spill_dir / f"{safe}.mrho"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark end-to-end MeTTa-built rho coordination workloads."
    )
    parser.add_argument("bin_path")
    parser.add_argument(
        "--manifest",
        default="tests/rhocalc_m3_may_must.tsv",
        help="manifest describing validated MeTTa rho fixtures",
    )
    parser.add_argument(
        "--suite",
        choices=["core", "all-metta"],
        default="core",
        help="curated core contract-style workloads or every MeTTa rho fixture",
    )
    parser.add_argument(
        "--workloads",
        help="comma-separated manifest workload names to benchmark explicitly",
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--csv", metavar="PATH")
    parser.add_argument(
        "--spill-dir",
        default="runtime/rhocalc_threaded_corpus_bench",
        help="directory for materialized mrho replay inputs",
    )
    args = parser.parse_args(argv[1:])
    if args.runs < 1:
        parser.error("--runs must be positive")
    thread_counts = [int(part) for part in args.threads.split(",") if part]
    if not thread_counts or any(count < 1 for count in thread_counts):
        parser.error("--threads must be a comma-separated list of positive integers")
    if 1 not in thread_counts:
        thread_counts.insert(0, 1)

    requested_names: list[str] | None = None
    if args.workloads:
        requested_names = [
            name.strip() for name in args.workloads.split(",") if name.strip()
        ]
    elif args.suite == "core":
        requested_names = list(CORE_WORKLOADS)

    manifest_path = Path(args.manifest)
    workloads = load_manifest_workloads(manifest_path, names=requested_names)
    rows: list[dict[str, object]] = []
    spill_dir = Path(args.spill_dir)
    spill_dir.mkdir(parents=True, exist_ok=True)
    ok = True

    print(f"MANIFEST {manifest_path}")
    print(f"THREADS {','.join(str(count) for count in thread_counts)}")
    print(f"RUNS {args.runs}")
    print(f"SPILL {spill_dir}")
    print()

    for workload in workloads:
        workload_ok = True
        print(f"WORKLOAD {workload.name}")
        print(f"FIXTURE {workload.fixture}")
        print(f"NOTE {workload.note}")

        build_times: list[float] = []
        built_mrho: str | None = None
        for _ in range(args.runs):
            build = run_metta_build(args.bin_path, workload.fixture)
            if build.status != 0 or not build.mrho:
                print(
                    f"FAIL {workload.name} build: "
                    f"{build.stderr or 'did not produce a rho term'}"
                )
                ok = False
                workload_ok = False
                break
            if built_mrho is None:
                built_mrho = build.mrho
            elif built_mrho != build.mrho:
                print(f"FAIL {workload.name} build: repeated builds produced different mrho")
                ok = False
                workload_ok = False
                break
            build_times.append(build.elapsed)
        if not workload_ok or built_mrho is None:
            print()
            continue

        build_median, build_best = summarize_times(build_times)
        build_row = {
            "suite": args.suite,
            "workload": workload.name,
            "fixture": str(workload.fixture),
            "stage": "build",
            "threads": "",
            "executor_mode": "metta-build",
            "runs": args.runs,
            "median_s": f"{build_median:.9f}",
            "best_s": f"{build_best:.9f}",
            "speedup_vs_seq": "",
            "speedup_vs_first_parallel": "",
            "samples_s": ";".join(f"{sample:.9f}" for sample in build_times),
            "note": workload.note,
        }
        rows.append(build_row)
        print(f"RESULT stage=build median_s={build_median:.6f} best_s={build_best:.6f}")

        input_path = mrho_output_path(spill_dir, workload)
        input_path.write_text(built_mrho)
        print(f"INPUT {input_path}")

        sequential_exec_median: float | None = None
        first_parallel_exec_median: float | None = None
        for threads in thread_counts:
            exec_times: list[float] = []
            for _ in range(args.runs):
                result = run_rhocalc_file(args.bin_path, input_path, threads=threads)
                valid, detail = validate_execution(workload, result)
                if not valid:
                    print(f"FAIL {workload.name} execute threads={threads}: {detail}")
                    ok = False
                    workload_ok = False
                    break
                exec_times.append(result.elapsed)
            if not workload_ok or not exec_times:
                break
            median, best = summarize_times(exec_times)
            if threads == 1:
                sequential_exec_median = median
            elif first_parallel_exec_median is None:
                first_parallel_exec_median = median
            assert sequential_exec_median is not None
            speedup_vs_seq = sequential_exec_median / median if median > 0 else 0.0
            speedup_vs_first_parallel = (
                first_parallel_exec_median / median
                if first_parallel_exec_median is not None and median > 0
                else ""
            )
            mode = executor_mode_for_threads(threads)
            rows.append({
                "suite": args.suite,
                "workload": workload.name,
                "fixture": str(workload.fixture),
                "stage": "execute",
                "threads": threads,
                "executor_mode": mode,
                "runs": args.runs,
                "median_s": f"{median:.9f}",
                "best_s": f"{best:.9f}",
                "speedup_vs_seq": f"{speedup_vs_seq:.6f}",
                "speedup_vs_first_parallel": (
                    f"{speedup_vs_first_parallel:.6f}"
                    if speedup_vs_first_parallel != ""
                    else ""
                ),
                "samples_s": ";".join(f"{sample:.9f}" for sample in exec_times),
                "note": workload.note,
            })
            end_to_end_median = build_median + median
            end_to_end_best = build_best + best
            end_to_end_speedup = (
                (build_median + sequential_exec_median) / end_to_end_median
                if end_to_end_median > 0
                else 0.0
            )
            rows.append({
                "suite": args.suite,
                "workload": workload.name,
                "fixture": str(workload.fixture),
                "stage": "end_to_end",
                "threads": threads,
                "executor_mode": mode,
                "runs": args.runs,
                "median_s": f"{end_to_end_median:.9f}",
                "best_s": f"{end_to_end_best:.9f}",
                "speedup_vs_seq": f"{end_to_end_speedup:.6f}",
                "speedup_vs_first_parallel": "",
                "samples_s": "",
                "note": workload.note,
            })
            summary = (
                "RESULT "
                f"stage=execute threads={threads} mode={mode} "
                f"median_s={median:.6f} best_s={best:.6f} "
                f"speedup_vs_seq={speedup_vs_seq:.3f}"
            )
            if speedup_vs_first_parallel != "":
                summary += (
                    f" speedup_vs_first_parallel={speedup_vs_first_parallel:.3f}"
                )
            summary += (
                f" end_to_end_median_s={end_to_end_median:.6f} "
                f"end_to_end_speedup_vs_seq={end_to_end_speedup:.3f}"
            )
            print(summary)
        print()

    if args.csv:
        csv_path = Path(args.csv)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            fieldnames = [
                "suite",
                "workload",
                "fixture",
                "stage",
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
