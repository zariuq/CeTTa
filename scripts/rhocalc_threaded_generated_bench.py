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

from rhocalc_m3_rholang_cli_compare import extract_single_mrho_result, output_observations
from rhocalc_tiny_semantics import normalize_proc, parse_mrho_proc, proc_to_mrho, strip_comments


@dataclass(frozen=True)
class GeneratedBenchmarkSpec:
    slug: str
    note: str


@dataclass(frozen=True)
class GeneratedCase:
    spec: GeneratedBenchmarkSpec
    size: int
    case_name: str
    metta: Path
    mrho: Path
    rho: Path


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
    runtime_counters: dict[str, int]


GENERATED_CORE = [
    GeneratedBenchmarkSpec(
        slug="certificate-quorum",
        note="quorum-style certificate routing built from MeTTa certificate data",
    ),
    GeneratedBenchmarkSpec(
        slug="demand-index",
        note="watch/fact demand routing over structured MeTTa inputs",
    ),
    GeneratedBenchmarkSpec(
        slug="indexed-demand",
        note="index-addressed demand fan-in with generated MeTTa builder load",
    ),
    GeneratedBenchmarkSpec(
        slug="route-policy",
        note="policy-routed forwarding generated from MeTTa route tables",
    ),
    GeneratedBenchmarkSpec(
        slug="route-synthesis",
        note="synthesized forwarding network with many concurrent coordination sites",
    ),
    GeneratedBenchmarkSpec(
        slug="job-queue-collector",
        note="shared job queue with exactly-once settlement into a collector chain",
    ),
    GeneratedBenchmarkSpec(
        slug="private-session",
        note="independent request/reply sessions generated from repeated MeTTa contracts",
    ),
]

RUNTIME_COUNTER_NAMES = [
    "parallel-queue-push",
    "parallel-queue-pop",
    "parallel-queue-wait",
    "parallel-queue-depth-peak",
    "parallel-queue-active-peak",
    "parallel-worker-task",
    "rho-async-endpoint-publish",
    "rho-async-endpoint-queued",
    "rho-async-endpoint-match",
]


def summarize_times(times: list[float]) -> tuple[float, float]:
    return statistics.median(times), min(times)


def executor_mode_for_threads(threads: int) -> str:
    return "threaded" if threads > 1 else "sequential"


def canonical_mrho(text: str) -> str:
    return proc_to_mrho(normalize_proc(parse_mrho_proc(strip_comments(text).strip())))


def parse_runtime_counters(stderr: str) -> dict[str, int]:
    counters: dict[str, int] = {}
    for raw in stderr.splitlines():
        line = raw.strip()
        if not line.startswith("runtime-counter "):
            continue
        parts = line.split()
        if len(parts) != 3:
            continue
        _, name, value = parts
        try:
            counters[name] = int(value, 10)
        except ValueError:
            continue
    return counters


def load_sizes(size_file: Path) -> list[int]:
    sizes: list[int] = []
    for raw in size_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        sizes.append(int(line, 10))
    return sizes


def resolve_specs(names: list[str] | None) -> list[GeneratedBenchmarkSpec]:
    by_slug = {spec.slug: spec for spec in GENERATED_CORE}
    if names is None:
        return list(GENERATED_CORE)
    missing = [name for name in names if name not in by_slug]
    if missing:
        raise ValueError(f"unknown benchmark slugs: {', '.join(missing)}")
    return [by_slug[name] for name in names]


def generate_case(root: Path, spec: GeneratedBenchmarkSpec, size: int,
                  generated_root: Path) -> GeneratedCase:
    bench_dir = root / "benchmarks" / "rho" / spec.slug
    generated_root.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        [str(bench_dir / "generate.sh"), str(size), str(generated_root)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            proc.stderr.strip() or proc.stdout.strip() or f"generator failed for {spec.slug}"
        )
    line = proc.stdout.strip().splitlines()[-1]
    parts = line.split("\t")
    if len(parts) != 4:
        raise RuntimeError(f"unexpected generator output for {spec.slug}: {line!r}")
    case_name, metta, mrho, rho = parts
    return GeneratedCase(
        spec=spec,
        size=size,
        case_name=case_name,
        metta=Path(metta),
        mrho=Path(mrho),
        rho=Path(rho),
    )


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


def run_rhocalc_file(bin_path: str, input_path: Path, *, threads: int,
                     emit_runtime_stats: bool) -> ExecResult:
    cmd = [
        bin_path,
        "--num-threads",
        str(threads),
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
    ]
    if emit_runtime_stats:
        cmd.append("--emit-runtime-stats")
    cmd.append(str(input_path))
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
        runtime_counters=parse_runtime_counters(proc.stderr),
    )


def validate_execution(result: ExecResult) -> tuple[bool, str]:
    if result.status != 0:
        return False, result.stderr or result.residual or "nonzero exit"
    if not result.residual:
        return False, "no residual output"
    try:
        output_observations(parse_mrho_proc(result.residual))
    except Exception as exc:
        return False, f"could not parse residual observations: {exc}"
    return True, ""


def generated_input_path(spill_dir: Path, case: GeneratedCase) -> Path:
    return spill_dir / f"{case.case_name}.mrho"


def pick_size(root: Path, slug: str, mode: str) -> int:
    sizes = load_sizes(root / "benchmarks" / "rho" / slug / "sizes.tsv")
    if not sizes:
        raise ValueError(f"no sizes available for {slug}")
    return sizes[-1] if mode == "largest" else sizes[0]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Benchmark scalable MeTTa-built rho benchmark generators."
    )
    parser.add_argument("bin_path")
    parser.add_argument(
        "--benchmarks",
        help="comma-separated benchmark slugs; defaults to the generated core suite",
    )
    parser.add_argument(
        "--size-mode",
        choices=["smallest", "largest"],
        default="largest",
        help="use the smallest or largest size from each benchmark's sizes.tsv",
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument(
        "--emit-runtime-stats",
        action="store_true",
        help="request --emit-runtime-stats and record executor/rho counters",
    )
    parser.add_argument("--csv", metavar="PATH")
    parser.add_argument(
        "--generated-dir",
        default="runtime/rhocalc_threaded_generated_bench/generated",
        help="directory for generator outputs",
    )
    parser.add_argument(
        "--spill-dir",
        default="runtime/rhocalc_threaded_generated_bench/built",
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

    root = Path.cwd()
    requested = (
        [name.strip() for name in args.benchmarks.split(",") if name.strip()]
        if args.benchmarks
        else None
    )
    specs = resolve_specs(requested)
    generated_dir = Path(args.generated_dir)
    spill_dir = Path(args.spill_dir)
    generated_dir.mkdir(parents=True, exist_ok=True)
    spill_dir.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, object]] = []
    ok = True

    print(f"THREADS {','.join(str(count) for count in thread_counts)}")
    print(f"RUNS {args.runs}")
    print(f"GENERATED {generated_dir}")
    print(f"SPILL {spill_dir}")
    print()

    for spec in specs:
        size = pick_size(root, spec.slug, args.size_mode)
        case = generate_case(root, spec, size, generated_dir / spec.slug)
        workload_ok = True
        generated_canonical = canonical_mrho(case.mrho.read_text(encoding="utf-8"))

        print(f"WORKLOAD {case.case_name}")
        print(f"METTA {case.metta}")
        print(f"MRHO {case.mrho}")
        print(f"SIZE {case.size}")
        print(f"NOTE {spec.note}")

        build_times: list[float] = []
        built_mrho: str | None = None
        for _ in range(args.runs):
            build = run_metta_build(args.bin_path, case.metta)
            if build.status != 0 or not build.mrho:
                print(
                    f"FAIL {case.case_name} build: "
                    f"{build.stderr or 'did not produce a rho term'}"
                )
                ok = False
                workload_ok = False
                break
            built_canonical = canonical_mrho(build.mrho)
            if built_canonical != generated_canonical:
                print(f"FAIL {case.case_name} build: MeTTa output diverged from generated mrho")
                ok = False
                workload_ok = False
                break
            if built_mrho is None:
                built_mrho = build.mrho
            build_times.append(build.elapsed)
        if not workload_ok or built_mrho is None:
            print()
            continue

        build_median, build_best = summarize_times(build_times)
        rows.append({
            "benchmark": spec.slug,
            "case_name": case.case_name,
            "size": case.size,
            "stage": "build",
            "threads": "",
            "executor_mode": "metta-build",
            "runs": args.runs,
            "median_s": f"{build_median:.9f}",
            "best_s": f"{build_best:.9f}",
            "speedup_vs_seq": "",
            "speedup_vs_first_parallel": "",
            "samples_s": ";".join(f"{sample:.9f}" for sample in build_times),
            **{name.replace("-", "_"): "" for name in RUNTIME_COUNTER_NAMES},
            "note": spec.note,
        })
        print(f"RESULT stage=build median_s={build_median:.6f} best_s={build_best:.6f}")

        input_path = generated_input_path(spill_dir, case)
        input_path.write_text(built_mrho)
        print(f"INPUT {input_path}")

        sequential_exec_median: float | None = None
        first_parallel_exec_median: float | None = None
        for threads in thread_counts:
            exec_times: list[float] = []
            sample_counters: dict[str, int] = {}
            for _ in range(args.runs):
                result = run_rhocalc_file(
                    args.bin_path,
                    input_path,
                    threads=threads,
                    emit_runtime_stats=args.emit_runtime_stats,
                )
                valid, detail = validate_execution(result)
                if not valid:
                    print(f"FAIL {case.case_name} execute threads={threads}: {detail}")
                    ok = False
                    workload_ok = False
                    break
                if args.emit_runtime_stats:
                    missing = [
                        name for name in RUNTIME_COUNTER_NAMES
                        if name not in result.runtime_counters
                    ]
                    if missing:
                        print(
                            f"FAIL {case.case_name} execute threads={threads}: "
                            f"missing runtime counters: {', '.join(missing)}"
                        )
                        ok = False
                        workload_ok = False
                        break
                    sample_counters = result.runtime_counters
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
                "benchmark": spec.slug,
                "case_name": case.case_name,
                "size": case.size,
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
                **{
                    name.replace("-", "_"): sample_counters.get(name, "")
                    for name in RUNTIME_COUNTER_NAMES
                },
                "note": spec.note,
            })
            end_to_end_median = build_median + median
            end_to_end_best = build_best + best
            end_to_end_speedup = (
                (build_median + sequential_exec_median) / end_to_end_median
                if end_to_end_median > 0
                else 0.0
            )
            rows.append({
                "benchmark": spec.slug,
                "case_name": case.case_name,
                "size": case.size,
                "stage": "end_to_end",
                "threads": threads,
                "executor_mode": mode,
                "runs": args.runs,
                "median_s": f"{end_to_end_median:.9f}",
                "best_s": f"{end_to_end_best:.9f}",
                "speedup_vs_seq": f"{end_to_end_speedup:.6f}",
                "speedup_vs_first_parallel": "",
                "samples_s": "",
                **{
                    name.replace("-", "_"): sample_counters.get(name, "")
                    for name in RUNTIME_COUNTER_NAMES
                },
                "note": spec.note,
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
            if args.emit_runtime_stats:
                summary += (
                    f" queue_wait={sample_counters['parallel-queue-wait']}"
                    f" queue_depth_peak={sample_counters['parallel-queue-depth-peak']}"
                    f" endpoint_queued={sample_counters['rho-async-endpoint-queued']}"
                    f" endpoint_match={sample_counters['rho-async-endpoint-match']}"
                )
            print(summary)
        print()

    if args.csv:
        csv_path = Path(args.csv)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            fieldnames = [
                "benchmark",
                "case_name",
                "size",
                "stage",
                "threads",
                "executor_mode",
                "runs",
                "median_s",
                "best_s",
                "speedup_vs_seq",
                "speedup_vs_first_parallel",
                "samples_s",
                *[name.replace("-", "_") for name in RUNTIME_COUNTER_NAMES],
                "note",
            ]
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(rows)
        print(f"CSV {csv_path}")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
