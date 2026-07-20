#!/usr/bin/env python3
"""Run counter-first storage and transfer ladders for runtime readiness."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import subprocess
import sys
import time
from collections import defaultdict
from io import StringIO
from pathlib import Path
from typing import Any

from cetta_readiness_model import (
    ReadinessModelError,
    growth_verdict,
    linear_fit,
    load_property_manifest,
    memory_rate_verdict,
    paired_growth_verdict,
    sha256_file,
)


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "benchmarks/main_readiness_properties.json"
BACKEND_SCRIPT = ROOT / "scripts/bench_space_backend_matrix.sh"
TRANSFER_SCRIPT = ROOT / "scripts/bench_space_transfer_matrix.sh"
DEFAULT_BACKENDS = (
    "native",
    "pathmap",
    "mork-live",
    "mork-open-act",
    "mork-load-act",
)
DEFAULT_TRANSFERS = (
    "native-to-pathmap",
    "native-to-mork-live",
    "mork-live-to-native",
)


def comma_list(value: str) -> tuple[str, ...]:
    values = tuple(part.strip() for part in value.split(",") if part.strip())
    if not values:
        raise argparse.ArgumentTypeError("expected a nonempty comma-separated list")
    return values


def size_list(value: str) -> tuple[int, ...]:
    try:
        values = tuple(int(part) for part in comma_list(value))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("sizes must be integers") from exc
    if any(size <= 42 for size in values) or tuple(sorted(set(values))) != values:
        raise argparse.ArgumentTypeError(
            "sizes must be unique, increasing integers greater than 42"
        )
    return values


def parse_args() -> argparse.Namespace:
    manifest = load_property_manifest(MANIFEST)
    parser = argparse.ArgumentParser(
        description=(
            "Run correctness/counter contracts over geometric storage and transfer "
            "ladders; timing and RSS slopes remain secondary evidence."
        )
    )
    parser.add_argument(
        "--binary",
        type=Path,
        default=Path(
            os.environ.get(
                "CETTA_BIN", ROOT / "runtime/cetta-main-runtime-stats"
            )
        ),
    )
    baseline_default = os.environ.get("CETTA_READINESS_BASELINE_STATS_BIN")
    parser.add_argument(
        "--baseline-binary",
        type=Path,
        default=Path(baseline_default) if baseline_default else None,
        help="runtime-stats binary for alternating scaling comparisons",
    )
    parser.add_argument(
        "--require-baseline",
        action="store_true",
        help="fail rather than emit observational-only unpaired evidence",
    )
    parser.add_argument(
        "--sizes",
        type=size_list,
        default=tuple(manifest["routine_ladder_sizes"]),
    )
    parser.add_argument(
        "--backend-modes", type=comma_list, default=DEFAULT_BACKENDS
    )
    parser.add_argument(
        "--transfer-cases", type=comma_list, default=DEFAULT_TRANSFERS
    )
    parser.add_argument("--per-case-timeout", type=int, default=900)
    parser.add_argument(
        "--max-time-slope",
        type=float,
        default=1.35,
        help="provisional bound; recorded unless --enforce-growth is supplied",
    )
    parser.add_argument(
        "--max-rss-slope",
        type=float,
        default=1.25,
        help="provisional bound; recorded unless --enforce-growth is supplied",
    )
    parser.add_argument(
        "--enforce-growth",
        action="store_true",
        help="fail on provisional time/RSS slope bounds after calibration",
    )
    parser.add_argument("--max-paired-time-ratio", type=float, default=1.5)
    parser.add_argument("--paired-time-slack-ms", type=float, default=50.0)
    parser.add_argument("--max-time-slope-delta", type=float, default=0.35)
    parser.add_argument("--max-paired-rss-ratio", type=float, default=1.25)
    parser.add_argument("--paired-rss-slack-kib", type=float, default=16384.0)
    parser.add_argument("--max-rss-slope-delta", type=float, default=0.25)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "runtime/main_readiness_ladders_current.json",
    )
    args = parser.parse_args()
    if args.per_case_timeout <= 0:
        parser.error("--per-case-timeout must be positive")
    if args.max_time_slope <= 0 or args.max_rss_slope <= 0:
        parser.error("growth-slope bounds must be positive")
    paired_positive = (
        args.max_paired_time_ratio,
        args.max_time_slope_delta,
        args.max_paired_rss_ratio,
        args.max_rss_slope_delta,
    )
    if any(value <= 0 for value in paired_positive):
        parser.error("paired growth bounds must be positive")
    if args.paired_time_slack_ms < 0 or args.paired_rss_slack_kib < 0:
        parser.error("paired growth slack must be nonnegative")
    if args.require_baseline and args.baseline_binary is None:
        parser.error(
            "--require-baseline needs --baseline-binary or "
            "CETTA_READINESS_BASELINE_STATS_BIN"
        )
    unknown_backends = set(args.backend_modes) - set(DEFAULT_BACKENDS)
    unknown_transfers = set(args.transfer_cases) - set(DEFAULT_TRANSFERS)
    if unknown_backends:
        parser.error(f"unknown backend modes: {sorted(unknown_backends)}")
    if unknown_transfers:
        parser.error(f"unknown transfer cases: {sorted(unknown_transfers)}")
    return args


def parse_tsv_row(output: str, kind: str) -> dict[str, str]:
    lines = [
        line
        for line in output.splitlines()
        if line.startswith("kind\t") or line.startswith(f"{kind}\t")
    ]
    if len(lines) != 2:
        raise ReadinessModelError(
            f"expected one {kind} header and row, found {len(lines)} TSV lines"
        )
    reader = csv.DictReader(StringIO("\n".join(lines)), delimiter="\t")
    row = next(reader, None)
    if row is None or row.get("kind") != kind:
        raise ReadinessModelError(f"malformed {kind} TSV row")
    return dict(row)


def integer(row: dict[str, str], name: str) -> int:
    value = row.get(name, "")
    if value in ("", "na"):
        raise ReadinessModelError(
            f"runtime-stats counter {name!r} is unavailable; use a stats build"
        )
    try:
        return int(value)
    except ValueError as exc:
        raise ReadinessModelError(f"{name!r} is not an integer: {value!r}") from exc


def primary_contract(kind: str, row: dict[str, str]) -> list[str]:
    failures: list[str] = []
    size = integer(row, "fact_count")
    count = integer(row, "count")
    term_inserts = integer(row, "term_universe_inserts")
    term_blob = integer(row, "term_universe_blob_bytes")
    atom_capacity = integer(row, "space_atom_id_capacity_bytes_peak")
    pathmap_stores = integer(row, "pathmap_direct_store")
    mork_adds = integer(row, "mork_add_call")
    mork_batch_items = integer(row, "mork_add_batch_items")

    if term_inserts <= 0 or term_blob <= 0 or atom_capacity <= 0:
        failures.append("deterministic storage-byte counters must be positive")

    if kind == "backend":
        mode = row["mode"]
        if count != size - 1:
            failures.append(f"backend residual count {count} != {size - 1}")
        if mode == "native":
            expected = (0, 0, 0)
            if term_inserts < size:
                failures.append(
                    f"native term-universe inserts {term_inserts} < {size}"
                )
        elif mode == "pathmap":
            expected = (size, 0, 0)
        elif mode == "mork-live":
            expected = (0, size, 0)
        elif mode in ("mork-open-act", "mork-load-act"):
            expected = (0, 0, 0)
        else:
            failures.append(f"unknown backend mode {mode!r}")
            expected = (pathmap_stores, mork_adds, mork_batch_items)
    elif kind == "transfer":
        case_id = row["case_id"]
        if count != size:
            failures.append(f"transfer residual count {count} != {size}")
        if row.get("route_class") != "materialized-shim":
            failures.append(
                "representative transfer must report materialized-shim route"
            )
        if case_id == "native-to-pathmap":
            expected = (size, 0, 0)
        elif case_id == "native-to-mork-live":
            expected = (0, 0, size)
        elif case_id == "mork-live-to-native":
            expected = (0, size, 0)
        else:
            failures.append(f"unknown transfer case {case_id!r}")
            expected = (pathmap_stores, mork_adds, mork_batch_items)
    else:
        raise ReadinessModelError(f"unknown ladder kind {kind!r}")

    observed = (pathmap_stores, mork_adds, mork_batch_items)
    if observed != expected:
        failures.append(
            "implementation-route counters "
            f"{observed!r} != expected {expected!r}"
        )
    return failures


def run_case(
    *,
    kind: str,
    case: str,
    size: int,
    binary: Path,
    timeout: int,
    role: str = "candidate",
) -> dict[str, Any]:
    script = BACKEND_SCRIPT if kind == "backend" else TRANSFER_SCRIPT
    command = [
        script.relative_to(ROOT).as_posix(),
        case,
        str(size),
        "1",
        "suite_total",
    ]
    environment = os.environ.copy()
    environment["CETTA_BIN"] = str(binary)
    environment["CETTA_BENCH_EMIT_RUNTIME_STATS"] = "1"
    print(
        f"READINESS-LADDER START role={role} kind={kind} case={case} size={size}",
        flush=True,
    )
    started = time.monotonic()
    result = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    elapsed = time.monotonic() - started
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        raise ReadinessModelError(
            f"{kind} case {case}/{size} exited {result.returncode}"
        )
    row = parse_tsv_row(result.stdout, kind)
    failures = primary_contract(kind, row)
    print(
        f"READINESS-LADDER {'PASS' if not failures else 'FAIL'} "
        f"role={role} kind={kind} case={case} size={size} "
        f"elapsed_s={elapsed:.3f}",
        flush=True,
    )
    return {
        "kind": kind,
        "role": role,
        "case": case,
        "size": size,
        "command": command,
        "elapsed_seconds": elapsed,
        "stdout_sha256": hashlib.sha256(result.stdout.encode()).hexdigest(),
        "row": row,
        "primary_failures": failures,
    }


def series_evidence(
    runs: list[dict[str, Any]],
    *,
    max_time_slope: float,
    max_rss_slope: float,
    memory_limits: dict[str, float] | None = None,
    baseline_runs: list[dict[str, Any]] | None = None,
    max_paired_time_ratio: float = 1.5,
    paired_time_slack_ns: float = 50_000_000.0,
    max_time_slope_delta: float = 0.35,
    max_paired_rss_ratio: float = 1.25,
    paired_rss_slack_kib: float = 16384.0,
    max_rss_slope_delta: float = 0.25,
) -> dict[str, Any]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for run in runs:
        grouped[(run["kind"], run["case"])].append(run)
    baseline_grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for run in baseline_runs or []:
        baseline_grouped[(run["kind"], run["case"])].append(run)
    evidence: dict[str, Any] = {}
    for (kind, case), samples in sorted(grouped.items()):
        samples.sort(key=lambda sample: sample["size"])
        time_samples = [
            (sample["size"], float(sample["row"]["scenario_ns"]))
            for sample in samples
        ]
        rss_samples = [
            (sample["size"], float(sample["row"]["rss_kb"]))
            for sample in samples
        ]
        modeled_memory = [
            (
                sample["size"],
                float(sample["row"]["term_universe_blob_bytes"])
                + float(sample["row"]["space_atom_id_capacity_bytes_peak"]),
            )
            for sample in samples
        ]
        memory_rate, memory_intercept = linear_fit(modeled_memory)
        series_name = f"{kind}:{case}"
        memory_limit = (memory_limits or {}).get(series_name)
        item: dict[str, Any] = {
            "growth": growth_verdict(
                time_samples,
                rss_samples,
                max_time_slope=max_time_slope,
                max_rss_slope=max_rss_slope,
            ),
            "modeled_bytes_per_entry": memory_rate,
            "modeled_memory_intercept_bytes": memory_intercept,
            "modeled_memory": (
                memory_rate_verdict(
                    modeled_memory,
                    max_bytes_per_entry=memory_limit,
                )
                if memory_limit is not None
                else None
            ),
            "sizes": [sample["size"] for sample in samples],
        }
        if baseline_runs is not None:
            baseline_samples = baseline_grouped.get((kind, case), [])
            baseline_samples.sort(key=lambda sample: sample["size"])
            item["paired"] = paired_growth_verdict(
                time_samples,
                [
                    (sample["size"], float(sample["row"]["scenario_ns"]))
                    for sample in baseline_samples
                ],
                rss_samples,
                [
                    (sample["size"], float(sample["row"]["rss_kb"]))
                    for sample in baseline_samples
                ],
                max_time_ratio=max_paired_time_ratio,
                time_absolute_slack=paired_time_slack_ns,
                max_time_slope_delta=max_time_slope_delta,
                max_rss_ratio=max_paired_rss_ratio,
                rss_absolute_slack=paired_rss_slack_kib,
                max_rss_slope_delta=max_rss_slope_delta,
            )
        else:
            item["paired"] = None
        evidence[series_name] = item
    return evidence


def main() -> int:
    args = parse_args()
    binary = args.binary.expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"FAIL: missing executable stats binary: {binary}", file=sys.stderr)
        return 2
    baseline_binary = (
        args.baseline_binary.expanduser().resolve()
        if args.baseline_binary is not None
        else None
    )
    if baseline_binary is not None and (
        not baseline_binary.is_file() or not os.access(baseline_binary, os.X_OK)
    ):
        print(
            f"FAIL: missing executable baseline stats binary: {baseline_binary}",
            file=sys.stderr,
        )
        return 2
    manifest = load_property_manifest(MANIFEST)
    routine_scale_representatives = {
        prop["routine_scale_representative"]
        for prop in manifest["properties"]
        if "routine_scale_representative" in prop
    }
    runs: list[dict[str, Any]] = []
    baseline_runs: list[dict[str, Any]] = []
    try:
        cases: list[tuple[str, str, int]] = []
        for size in args.sizes:
            for mode in args.backend_modes:
                witness = f"space-ladder:{mode}"
                if (
                    size == args.sizes[-1]
                    and witness not in routine_scale_representatives
                ):
                    continue
                cases.append(("backend", mode, size))
            for case in args.transfer_cases:
                witness = f"transfer-ladder:{case}"
                if (
                    size == args.sizes[-1]
                    and witness not in routine_scale_representatives
                ):
                    continue
                cases.append(("transfer", case, size))
        for index, (kind, case, size) in enumerate(cases):
            order = [("candidate", binary)]
            if baseline_binary is not None:
                order = (
                    [("baseline", baseline_binary), ("candidate", binary)]
                    if index % 2 == 0
                    else [("candidate", binary), ("baseline", baseline_binary)]
                )
            for role, selected_binary in order:
                result = run_case(
                    kind=kind,
                    case=case,
                    size=size,
                    binary=selected_binary,
                    timeout=args.per_case_timeout,
                    role=role,
                )
                (runs if role == "candidate" else baseline_runs).append(result)
        series = series_evidence(
            runs,
            max_time_slope=args.max_time_slope,
            max_rss_slope=args.max_rss_slope,
            memory_limits=manifest["modeled_memory_bytes_per_entry_limits"],
            baseline_runs=(baseline_runs if baseline_binary is not None else None),
            max_paired_time_ratio=args.max_paired_time_ratio,
            paired_time_slack_ns=args.paired_time_slack_ms * 1_000_000.0,
            max_time_slope_delta=args.max_time_slope_delta,
            max_paired_rss_ratio=args.max_paired_rss_ratio,
            paired_rss_slack_kib=args.paired_rss_slack_kib,
            max_rss_slope_delta=args.max_rss_slope_delta,
        )
    except (ReadinessModelError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    primary_failures = [
        {
            "kind": run["kind"],
            "case": run["case"],
            "size": run["size"],
            "failures": run["primary_failures"],
        }
        for run in runs
        if run["primary_failures"]
    ]
    baseline_primary_failures = [
        {
            "kind": run["kind"],
            "case": run["case"],
            "size": run["size"],
            "failures": run["primary_failures"],
        }
        for run in baseline_runs
        if run["primary_failures"]
    ]
    absolute_growth_bound_exceedances = [
        name for name, item in series.items() if not item["growth"]["passed"]
    ]
    memory_failures = [
        name
        for name, item in series.items()
        if item["modeled_memory"] is not None
        and not item["modeled_memory"]["passed"]
    ]
    paired_failures = [
        name
        for name, item in series.items()
        if item["paired"] is not None and not item["paired"]["passed"]
    ]
    passed = (
        not primary_failures
        and not baseline_primary_failures
        and not memory_failures
        and not paired_failures
        and (
            not args.enforce_growth
            or not absolute_growth_bound_exceedances
        )
    )
    try:
        binary_label = binary.relative_to(ROOT).as_posix()
    except ValueError:
        binary_label = binary.name
    summary = {
        "schema": "cetta.main-readiness.ladders.v1",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "status": "passed" if passed else "failed",
        "qualification": (
            "absolute-and-paired-growth-enforced"
            if args.enforce_growth
            else "counter-primary-paired-growth-enforced"
        ),
        "manifest_schema": manifest["schema"],
        "manifest_sha256": sha256_file(MANIFEST),
        "binary": binary_label,
        "binary_sha256": sha256_file(binary),
        "baseline_binary": (
            baseline_binary.name if baseline_binary is not None else None
        ),
        "baseline_binary_sha256": (
            sha256_file(baseline_binary) if baseline_binary is not None else None
        ),
        "sizes": list(args.sizes),
        "backend_modes": list(args.backend_modes),
        "transfer_cases": list(args.transfer_cases),
        "primary_failures": primary_failures,
        "baseline_primary_failures": baseline_primary_failures,
        "absolute_growth_bound_exceedances": (
            absolute_growth_bound_exceedances
        ),
        "paired_growth_failures": paired_failures,
        "modeled_memory_failures": memory_failures,
        "growth_enforced": args.enforce_growth,
        "series": series,
        "runs": runs,
        "baseline_runs": baseline_runs,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    result_hash = sha256_file(args.output)
    print(f"WITNESS_EVIDENCE_SCHEMA={summary['schema']}")
    print(f"WITNESS_EVIDENCE_STATUS={summary['status']}")
    print(f"WITNESS_EVIDENCE_SHA256={result_hash}")
    print(f"WITNESS_EVIDENCE_RUNS={len(runs)}")
    print(f"WITNESS_EVIDENCE_BASELINE_RUNS={len(baseline_runs)}")
    print(f"WITNESS_EVIDENCE_GROWTH_ENFORCED={int(args.enforce_growth)}")
    print(f"WITNESS_EVIDENCE_PAIRED_GROWTH_ENFORCED={int(baseline_binary is not None)}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
