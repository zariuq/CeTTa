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
import random

from rhocalc_m3_rholang_cli_compare import (
    contract_name_key,
    contract_proc_key,
)
from rhocalc_tiny_semantics import (
    ProcRecv,
    ProcSend,
    parse_mrho_proc,
    top_level_components,
)


@dataclass(frozen=True)
class RunResult:
    status: int
    residual: str
    stderr: str
    elapsed: float


@dataclass(frozen=True)
class Workload:
    name: str
    expr: str
    validate: object
    reductions: int
    note: str


def par(parts: list[str]) -> str:
    if not parts:
        return "rho:nil"
    if len(parts) == 1:
        return parts[0]
    return "(rho:par " + " ".join(parts) + ")"


def payload(name: str) -> str:
    return f"(rho:send ${name} rho:nil)"


def nested_payload(prefix: str, depth: int) -> str:
    term = "rho:nil"
    for i in range(depth - 1, -1, -1):
        term = f"(rho:send ${prefix}{i} {term})"
    return term


def run_rho(bin_path: str, expr: str, *, threads: int,
            input_path: Path | None = None) -> RunResult:
    cmd = [
        bin_path,
        "--num-threads",
        str(threads),
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
    ]
    if input_path is None:
        cmd.extend(["-e", expr])
    else:
        cmd.append(str(input_path))
    start = time.perf_counter()
    proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    elapsed = time.perf_counter() - start
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    residual = lines[0] if lines else ""
    return RunResult(proc.returncode, residual, proc.stderr.strip(), elapsed)


def observations(proc_text: str) -> dict[str, str]:
    proc = parse_mrho_proc(proc_text)
    out: dict[str, str] = {}
    for component in top_level_components(proc):
        if isinstance(component, ProcSend):
            out[contract_name_key(component.channel)] = contract_proc_key(component.payload)
    return out


def send_components_on(proc_text: str, channel: str) -> list[ProcSend]:
    proc = parse_mrho_proc(proc_text)
    out: list[ProcSend] = []
    for component in top_level_components(proc):
        if isinstance(component, ProcSend) and contract_name_key(component.channel) == channel:
            out.append(component)
    return out


def recv_components_on(proc_text: str, channel: str) -> list[ProcRecv]:
    proc = parse_mrho_proc(proc_text)
    out: list[ProcRecv] = []
    for component in top_level_components(proc):
        if isinstance(component, ProcRecv) and contract_name_key(component.channel) == channel:
            out.append(component)
    return out


def chain_branch(branch: int, depth: int) -> list[str]:
    body = f"(rho:send $out{branch} (rho:drop $x{branch}x{depth}))"
    for step in range(depth - 1, -1, -1):
        next_ch = f"c{branch}x{step + 1}"
        body = par([
            f"(rho:recv ${next_ch} $x{branch}x{step + 1} {body})",
            f"(rho:send ${next_ch} (rho:drop $x{branch}x{step}))",
        ])
    return [
        f"(rho:recv $c{branch}x0 $x{branch}x0 {body})",
        f"(rho:send $c{branch}x0 {payload(f'p{branch}')})",
    ]


def shuffled(parts: list[str], rng: random.Random) -> list[str]:
    out = list(parts)
    rng.shuffle(out)
    return out


def chain_farm(width: int, depth: int, rng: random.Random) -> Workload:
    parts: list[str] = []
    expected: dict[str, str] = {}
    for branch in range(width):
        parts.extend(chain_branch(branch, depth))
        expected[f"out{branch}"] = f"send(p{branch} nil)"

    def validate(result: RunResult) -> tuple[bool, str]:
        if result.status != 0:
            return False, result.stderr or result.residual
        actual = observations(result.residual)
        return actual == expected, f"expected {len(expected)} outputs, got {len(actual)}"

    return Workload(
        name=f"chain-farm-w{width}-d{depth}",
        expr=par(shuffled(parts, rng)),
        validate=validate,
        reductions=width * (depth + 1),
        note="independent deep rho chains; closest pure-rho analogue of useful parallel work",
    )


def wide_single_comm(width: int, rng: random.Random) -> Workload:
    parts: list[str] = []
    expected: dict[str, str] = {}
    for i in range(width):
        parts.append(f"(rho:recv $w{i} $x (rho:send $out{i} (rho:drop $x)))")
        parts.append(f"(rho:send $w{i} {payload(f'p{i}')})")
        expected[f"out{i}"] = f"send(p{i} nil)"

    def validate(result: RunResult) -> tuple[bool, str]:
        if result.status != 0:
            return False, result.stderr or result.residual
        actual = observations(result.residual)
        return actual == expected, f"expected {len(expected)} outputs, got {len(actual)}"

    return Workload(
        name=f"wide-single-comm-w{width}",
        expr=par(shuffled(parts, rng)),
        validate=validate,
        reductions=width,
        note="many tiny independent COMMs; exposes coordination overhead",
    )


def payload_copy(width: int, payload_depth: int, rng: random.Random) -> Workload:
    parts: list[str] = []
    expected: dict[str, str] = {}
    for i in range(width):
        pay = nested_payload(f"p{i}x", payload_depth)
        parts.append(f"(rho:recv $cp{i} $x (rho:send $out{i} (rho:drop $x)))")
        parts.append(f"(rho:send $cp{i} {pay})")
        expected[f"out{i}"] = contract_proc_key(parse_mrho_proc(pay))

    def validate(result: RunResult) -> tuple[bool, str]:
        if result.status != 0:
            return False, result.stderr or result.residual
        actual = observations(result.residual)
        return actual == expected, f"expected {len(expected)} outputs, got {len(actual)}"

    return Workload(
        name=f"payload-copy-w{width}-p{payload_depth}",
        expr=par(shuffled(parts, rng)),
        validate=validate,
        reductions=width,
        note="independent COMMs with larger payload copying/materialization",
    )


def hot_channel(sends: int, recvs: int, rng: random.Random) -> Workload:
    hot = "hotBench"
    parts = [
        f"(rho:recv ${hot} $x (rho:send $outHot{i} (rho:drop $x)))"
        for i in range(recvs)
    ]
    parts.extend(f"(rho:send ${hot} {payload(f'pHot{i}')})" for i in range(sends))
    allowed = {f"send(pHot{i} nil)" for i in range(sends)}

    def validate(result: RunResult) -> tuple[bool, str]:
        if result.status != 0:
            return False, result.stderr or result.residual
        actual = observations(result.residual)
        public = {k: v for k, v in actual.items() if k.startswith("outHot")}
        payloads = set(public.values())
        ok = (
            len(public) == recvs and
            len(payloads) == recvs and
            payloads <= allowed and
            len(send_components_on(result.residual, hot)) == sends - recvs and
            len(recv_components_on(result.residual, hot)) == 0
        )
        return ok, f"outputs={len(public)} leftover_sends={len(send_components_on(result.residual, hot))}"

    return Workload(
        name=f"hot-channel-s{sends}-r{recvs}",
        expr=par(shuffled(parts, rng)),
        validate=validate,
        reductions=recvs,
        note="contended bucket; checks linear consumption and contention cost",
    )


def workloads_for_mode(mode: str, seed: int) -> list[Workload]:
    rng = random.Random(seed)
    if mode == "quick":
        return [
            chain_farm(width=12, depth=10, rng=rng),
            wide_single_comm(width=96, rng=rng),
            payload_copy(width=24, payload_depth=12, rng=rng),
            hot_channel(sends=96, recvs=64, rng=rng),
        ]
    if mode == "standard":
        return [
            chain_farm(width=32, depth=20, rng=rng),
            wide_single_comm(width=384, rng=rng),
            payload_copy(width=64, payload_depth=24, rng=rng),
            hot_channel(sends=384, recvs=256, rng=rng),
        ]
    return [
        chain_farm(width=96, depth=36, rng=rng),
        wide_single_comm(width=1536, rng=rng),
        payload_copy(width=192, payload_depth=48, rng=rng),
        hot_channel(sends=1536, recvs=1024, rng=rng),
    ]


def summarize_times(times: list[float]) -> tuple[float, float]:
    return statistics.median(times), min(times)


def executor_mode_for_threads(threads: int) -> str:
    return "threaded" if threads > 1 else "sequential"


def workload_input_path(spill_dir: Path, workload: Workload,
                        mode: str, seed: int) -> Path:
    safe_name = "".join(
        char if char.isalnum() or char in "-_" else "_"
        for char in workload.name
    )
    return spill_dir / f"{mode}_seed{seed}_{safe_name}.mrho"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Measure --num-threads speed/crossover on validated rho workloads."
    )
    parser.add_argument("bin_path")
    parser.add_argument("--mode", choices=["quick", "standard", "heavy"],
                        default="quick")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--threads", default="1,2,4,8")
    parser.add_argument("--seed", type=lambda text: int(text, 0),
                        default=0xC377A)
    parser.add_argument("--csv", metavar="PATH",
                        help="write per-thread benchmark rows to CSV")
    parser.add_argument("--spill-dir", default="runtime/rhocalc_threaded_bench_inputs",
                        help="directory for generated benchmark rho input files")
    parser.add_argument("--require-speedup", action="store_true",
                        help="fail if chain-farm does not improve over one thread")
    args = parser.parse_args(argv[1:])
    if args.runs < 1:
        parser.error("--runs must be positive")
    thread_counts = [int(part) for part in args.threads.split(",") if part]
    if not thread_counts or any(count < 1 for count in thread_counts):
        parser.error("--threads must be a comma-separated list of positive integers")
    if 1 not in thread_counts:
        thread_counts.insert(0, 1)

    ok = True
    chain_speedup = 1.0
    rows: list[dict[str, object]] = []
    spill_dir = Path(args.spill_dir)
    spill_dir.mkdir(parents=True, exist_ok=True)
    replay = (
        f"{args.bin_path} --mode {args.mode} --runs {args.runs} "
        f"--threads {','.join(str(count) for count in thread_counts)} "
        f"--seed {args.seed} --spill-dir {spill_dir}"
    )
    print(f"SEED {args.seed}")
    print(f"REPLAY scripts/rhocalc_threaded_bench.py {replay}")
    print()
    for workload in workloads_for_mode(args.mode, args.seed):
        input_path = workload_input_path(spill_dir, workload, args.mode, args.seed)
        input_path.write_text(workload.expr)
        print(f"WORKLOAD {workload.name}")
        print(f"NOTE {workload.note}")
        print(f"REDUCTIONS {workload.reductions}")
        print(f"INPUT {input_path}")
        sequential_baseline_median: float | None = None
        first_threaded_median: float | None = None
        for threads in thread_counts:
            times: list[float] = []
            for _ in range(args.runs):
                result = run_rho(args.bin_path, workload.expr, threads=threads,
                                 input_path=input_path)
                valid, detail = workload.validate(result)  # type: ignore[attr-defined]
                if not valid:
                    print(f"FAIL {workload.name} threads={threads}: {detail}")
                    ok = False
                    break
                times.append(result.elapsed)
            if not times:
                continue
            median, best = summarize_times(times)
            mode = executor_mode_for_threads(threads)
            if threads == 1:
                sequential_baseline_median = median
            elif first_threaded_median is None:
                first_threaded_median = median
            assert sequential_baseline_median is not None
            speedup_vs_seq = (
                sequential_baseline_median / median if median > 0 else 0.0
            )
            speedup_vs_first_parallel = (
                first_threaded_median / median
                if first_threaded_median is not None and median > 0
                else ""
            )
            if workload.name.startswith("chain-farm") and threads == max(thread_counts):
                chain_speedup = speedup_vs_seq
            rows.append({
                "mode": args.mode,
                "seed": args.seed,
                "workload": workload.name,
                "reductions": workload.reductions,
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
                "samples_s": ";".join(f"{sample:.9f}" for sample in times),
                "note": workload.note,
            })
            summary = (
                "RESULT "
                f"threads={threads} "
                f"mode={mode} "
                f"median_s={median:.6f} "
                f"best_s={best:.6f} "
                f"speedup_vs_seq={speedup_vs_seq:.3f}"
            )
            if speedup_vs_first_parallel != "":
                summary += (
                    f" speedup_vs_first_parallel={speedup_vs_first_parallel:.3f}"
                )
            print(
                summary
            )
        print()

    if args.require_speedup and chain_speedup <= 1.0:
        print("FAIL chain-farm did not improve over one thread")
        ok = False
    if args.csv:
        csv_path = Path(args.csv)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_path.open("w", newline="") as handle:
            fieldnames = [
                "mode",
                "seed",
                "workload",
                "reductions",
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
