#!/usr/bin/env python3
"""Run counter-first storage and transfer ladders for runtime readiness."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import os
import random
import statistics
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
ROUTINE_TRANSFERS = (
    "native-to-pathmap",
    "native-to-mork-live",
    "mork-live-to-native",
)
EXHAUSTIVE_TRANSFERS = (
    *ROUTINE_TRANSFERS,
    "pathmap-to-native",
    "pathmap-to-mork-live",
    "mork-live-to-pathmap",
    "mork-live-to-open-act",
    "mork-live-to-load-act",
)
SCHEDULE_SEED = 0xC377A

# Coefficients for counters already emitted by the runtime.  The transfer
# scripts independently assert destination cardinality, exact lookup, and full
# scan results; these counters additionally pin the observed storage route.
TRANSFER_COUNTER_COEFFICIENTS = {
    "native-to-pathmap": (1, 0, 0),
    "native-to-mork-live": (0, 0, 1),
    "pathmap-to-native": (1, 0, 0),
    "pathmap-to-mork-live": (1, 0, 0),
    "mork-live-to-native": (0, 1, 0),
    "mork-live-to-pathmap": (1, 1, 0),
    "mork-live-to-open-act": (0, 1, 0),
    "mork-live-to-load-act": (0, 1, 0),
}


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
        "--tier",
        choices=("routine", "exhaustive"),
        default="routine",
        help="select the manifest-defined ladder and case coverage",
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
        "--candidate-only",
        action="store_true",
        help=(
            "ignore the readiness baseline and enforce candidate-side "
            "correctness, counters, memory, and absolute growth"
        ),
    )
    parser.add_argument(
        "--sizes",
        type=size_list,
        default=None,
    )
    parser.add_argument(
        "--backend-modes", type=comma_list, default=DEFAULT_BACKENDS
    )
    parser.add_argument("--transfer-cases", type=comma_list, default=None)
    parser.add_argument("--per-case-timeout", type=int, default=900)
    parser.add_argument(
        "--max-time-exponent",
        type=float,
        default=1.5,
        help="marginal-work exponent bound enforced by --enforce-growth",
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
    parser.add_argument("--max-time-increment-ratio", type=float, default=1.75)
    parser.add_argument(
        "--max-time-exponent-delta", type=float, default=0.35
    )
    parser.add_argument("--max-paired-rss-ratio", type=float, default=1.25)
    parser.add_argument("--paired-rss-slack-kib", type=float, default=16384.0)
    parser.add_argument("--max-rss-slope-delta", type=float, default=0.25)
    parser.add_argument(
        "--samples-per-point",
        type=int,
        default=1,
        help="repeat every ladder point and use metric medians for growth",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "runtime/main_readiness_ladders_current.json",
    )
    args = parser.parse_args()
    if args.sizes is None:
        args.sizes = tuple(manifest[f"{args.tier}_ladder_sizes"])
    if args.transfer_cases is None:
        args.transfer_cases = (
            ROUTINE_TRANSFERS
            if args.tier == "routine"
            else EXHAUSTIVE_TRANSFERS
        )
    if args.per_case_timeout <= 0:
        parser.error("--per-case-timeout must be positive")
    if args.max_time_exponent <= 0 or args.max_rss_slope <= 0:
        parser.error("growth bounds must be positive")
    paired_positive = (
        args.max_paired_time_ratio,
        args.max_time_increment_ratio,
        args.max_time_exponent_delta,
        args.max_paired_rss_ratio,
        args.max_rss_slope_delta,
    )
    if any(value <= 0 for value in paired_positive):
        parser.error("paired growth bounds must be positive")
    if args.samples_per_point <= 0:
        parser.error("--samples-per-point must be positive")
    if args.paired_time_slack_ms < 0 or args.paired_rss_slack_kib < 0:
        parser.error("paired growth slack must be nonnegative")
    if args.require_baseline and args.baseline_binary is None:
        parser.error(
            "--require-baseline needs --baseline-binary or "
            "CETTA_READINESS_BASELINE_STATS_BIN"
        )
    if args.require_baseline and args.candidate_only:
        parser.error("--require-baseline and --candidate-only are incompatible")
    if args.candidate_only:
        args.baseline_binary = None
    unknown_backends = set(args.backend_modes) - set(DEFAULT_BACKENDS)
    unknown_transfers = set(args.transfer_cases) - set(EXHAUSTIVE_TRANSFERS)
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


def realized_witness_ids(
    tier: str, runs: list[dict[str, Any]]
) -> list[str]:
    tier_suffix = "-exhaustive" if tier == "exhaustive" else ""
    return sorted(
        {
            f"{'space' if run['kind'] == 'backend' else 'transfer'}-ladder"
            f"{tier_suffix}:{run['case']}@{run['size']}"
            for run in runs
        }
    )


def required_ladder_witness_ids(manifest: dict[str, Any]) -> set[str]:
    prefixes = ("space-ladder-exhaustive:", "transfer-ladder-exhaustive:")
    return {
        witness
        for prop in manifest["properties"]
        for witness in prop["exhaustive_witnesses"]
        if witness.startswith(prefixes)
    }


def scheduled_cases(
    cases: list[tuple[str, str, int]], sample_index: int
) -> list[tuple[str, str, int]]:
    """Return one reproducible ordering without a size/time confound."""
    scheduled = list(cases)
    random.Random(SCHEDULE_SEED + sample_index).shuffle(scheduled)
    return scheduled


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
        coefficients = TRANSFER_COUNTER_COEFFICIENTS.get(case_id)
        if coefficients is None:
            failures.append(f"unknown transfer case {case_id!r}")
            expected = (pathmap_stores, mork_adds, mork_batch_items)
        else:
            expected = tuple(coefficient * size for coefficient in coefficients)
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
    for inherited_name in (
        "CETTA_BENCH_CORPUS_PATH",
        "CETTA_BENCH_ACT_PATH",
        "CETTA_BENCH_MATERIALIZED_RANGE",
        "CETTA_BENCH_RANGE_CHUNK",
        "CETTA_TRANSFER_ROUTE",
        "BENCH_KEEP_TMP",
    ):
        environment.pop(inherited_name, None)
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
    max_time_exponent: float,
    max_rss_slope: float,
    memory_limits: dict[str, float] | None = None,
    baseline_runs: list[dict[str, Any]] | None = None,
    max_paired_time_ratio: float = 1.5,
    paired_time_slack_ns: float = 50_000_000.0,
    max_time_increment_ratio: float = 1.75,
    max_time_exponent_delta: float = 0.35,
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
        samples_by_size: dict[int, list[dict[str, Any]]] = defaultdict(list)
        for sample in samples:
            samples_by_size[sample["size"]].append(sample)

        def median_field(size: int, field: str) -> float:
            return statistics.median(
                float(sample["row"][field])
                for sample in samples_by_size[size]
            )

        sizes = sorted(samples_by_size)
        time_samples = [
            (size, median_field(size, "scenario_ns")) for size in sizes
        ]
        rss_samples = [
            (size, median_field(size, "rss_kb")) for size in sizes
        ]
        modeled_memory = [
            (
                size,
                median_field(size, "term_universe_blob_bytes")
                + median_field(size, "space_atom_id_capacity_bytes_peak"),
            )
            for size in sizes
        ]
        memory_rate, memory_intercept = linear_fit(modeled_memory)
        series_name = f"{kind}:{case}"
        memory_limit = (memory_limits or {}).get(series_name)
        item: dict[str, Any] = {
            "growth": growth_verdict(
                time_samples,
                rss_samples,
                max_time_exponent=max_time_exponent,
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
            "sizes": sizes,
            "samples_per_size": {
                str(size): len(samples_by_size[size]) for size in sizes
            },
        }
        if baseline_runs is not None:
            baseline_samples = baseline_grouped.get((kind, case), [])
            baseline_samples.sort(key=lambda sample: sample["size"])
            baseline_by_size: dict[int, list[dict[str, Any]]] = defaultdict(list)
            for sample in baseline_samples:
                baseline_by_size[sample["size"]].append(sample)

            def baseline_median_field(size: int, field: str) -> float:
                return statistics.median(
                    float(sample["row"][field])
                    for sample in baseline_by_size[size]
                )

            item["paired"] = paired_growth_verdict(
                time_samples,
                [
                    (size, baseline_median_field(size, "scenario_ns"))
                    for size in sorted(baseline_by_size)
                ],
                rss_samples,
                [
                    (size, baseline_median_field(size, "rss_kb"))
                    for size in sorted(baseline_by_size)
                ],
                max_time_ratio=max_paired_time_ratio,
                time_absolute_slack=paired_time_slack_ns,
                max_time_increment_ratio=max_time_increment_ratio,
                max_time_exponent_delta=max_time_exponent_delta,
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
                    args.tier == "routine"
                    and size == args.sizes[-1]
                    and witness not in routine_scale_representatives
                ):
                    continue
                cases.append(("backend", mode, size))
            for case in args.transfer_cases:
                witness = f"transfer-ladder:{case}"
                if (
                    args.tier == "routine"
                    and size == args.sizes[-1]
                    and witness not in routine_scale_representatives
                ):
                    continue
                cases.append(("transfer", case, size))
        pair_index = 0
        for sample_index in range(args.samples_per_point):
            for kind, case, size in scheduled_cases(cases, sample_index):
                order = [("candidate", binary)]
                if baseline_binary is not None:
                    order = (
                        [("baseline", baseline_binary), ("candidate", binary)]
                        if pair_index % 2 == 0
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
                    result["sample_index"] = sample_index
                    (runs if role == "candidate" else baseline_runs).append(result)
                pair_index += 1
        series = series_evidence(
            runs,
            max_time_exponent=args.max_time_exponent,
            max_rss_slope=args.max_rss_slope,
            memory_limits=manifest["modeled_memory_bytes_per_entry_limits"],
            baseline_runs=(baseline_runs if baseline_binary is not None else None),
            max_paired_time_ratio=args.max_paired_time_ratio,
            paired_time_slack_ns=args.paired_time_slack_ms * 1_000_000.0,
            max_time_increment_ratio=args.max_time_increment_ratio,
            max_time_exponent_delta=args.max_time_exponent_delta,
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
    realized_witnesses = realized_witness_ids(args.tier, runs)
    missing_largest_witnesses = (
        sorted(required_ladder_witness_ids(manifest) - set(realized_witnesses))
        if args.tier == "exhaustive"
        else []
    )
    passed = (
        not primary_failures
        and not baseline_primary_failures
        and not memory_failures
        and not paired_failures
        and not missing_largest_witnesses
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
        "tier": args.tier,
        "qualification": (
            "absolute-and-paired-growth-enforced"
            if args.enforce_growth and baseline_binary is not None
            else "absolute-growth-enforced"
            if args.enforce_growth
            else "counter-primary-paired-growth-enforced"
            if baseline_binary is not None
            else "counter-primary-only"
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
        "samples_per_point": args.samples_per_point,
        "schedule_seed": SCHEDULE_SEED,
        "primary_failures": primary_failures,
        "baseline_primary_failures": baseline_primary_failures,
        "absolute_growth_bound_exceedances": (
            absolute_growth_bound_exceedances
        ),
        "paired_growth_failures": paired_failures,
        "modeled_memory_failures": memory_failures,
        "realized_witnesses": realized_witnesses,
        "missing_largest_scale_witnesses": missing_largest_witnesses,
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
