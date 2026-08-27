#!/usr/bin/env python3
"""Profile native PeTTa work after receipt-selected oracle validation."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import petta_corpus_manifest as corpus  # noqa: E402
from petta_machine_stats import (  # noqa: E402
    aggregate_controller_stats,
    aggregate_invocations,
    extract_machine_and_controller_stats,
)


DEFAULT_WORKLOADS = (
    "holbenchmark.metta",
    "hyperpose_primes.metta",
    "fib.metta",
    "matespacefast.metta",
    "scale.metta",
)
def median_runs(runs: list[dict[str, Any]]) -> dict[str, int | float]:
    keys = sorted(
        set().union(*(run["aggregate"].keys() for run in runs))
    )
    medians: dict[str, int | float] = {
        "process_elapsed_ns": statistics.median(
            run["process_elapsed_ns"] for run in runs
        ),
    }
    for key in keys:
        values = [run["aggregate"].get(key, 0) for run in runs]
        medians[key] = statistics.median(values)
    return medians


def run_workload(
    cetta: Path,
    petta_dir: Path,
    entry: dict[str, Any],
    timeout_seconds: float,
    candidate_environment: dict[str, str],
) -> dict[str, Any]:
    source = petta_dir / entry["source"]
    if corpus.sha256_file(source) != entry["source_sha256"]:
        raise RuntimeError(f"{entry['name']}: source differs from manifest")
    environment = os.environ.copy()
    for key in tuple(environment):
        if key.startswith("CETTA_PETTA_") or key == (
            "CETTA_TERM_UNIVERSE_SOURCE_ID_MEMO"
        ):
            del environment[key]
    environment.update(candidate_environment)
    started_ns = time.monotonic_ns()
    exit_code, stdout, stderr, timed_out, output_limit = (
        corpus.run_bounded_process(
            [str(cetta), "--lang", "petta", str(source)],
            cetta.parent,
            environment,
            None,
            timeout_seconds,
        )
    )
    process_elapsed_ns = time.monotonic_ns() - started_ns
    if timed_out:
        raise RuntimeError(f"{entry['name']}: timed out")
    if output_limit:
        raise RuntimeError(f"{entry['name']}: exceeded output limit")
    invocations, controller_invocations, ordinary_stderr = (
        extract_machine_and_controller_stats(stderr)
    )
    normalized_stdout = corpus.normalize_cetta_stdout(
        stdout, petta_dir, (cetta.parent,)
    )
    normalized_stderr = corpus.normalize_oracle_stderr(
        ordinary_stderr, petta_dir, (cetta.parent,)
    )
    oracle = entry["oracle"]
    controller_aggregate = aggregate_controller_stats(
        controller_invocations
    )
    stdout_contract = (
        corpus.STDOUT_OCCURRENCE_BAG
        if controller_aggregate["active_fifo"] > 0
        else corpus.STDOUT_EXACT_STREAM
    )
    stdout_observation_equal = corpus.stdout_observation_equal(
        normalized_stdout, oracle["stdout"], stdout_contract
    )
    stdout_stream_equal = corpus.stdout_observation_equal(
        normalized_stdout,
        oracle["stdout"],
        corpus.STDOUT_EXACT_STREAM,
    )
    qualified = (
        exit_code == oracle["exit"]
        and stdout_observation_equal
        and normalized_stderr == oracle["stderr"]
    )
    if not qualified:
        raise RuntimeError(
            f"{entry['name']}: oracle mismatch "
            f"(exit {exit_code!r} != {oracle['exit']!r}, "
            f"stdout_contract={stdout_contract}, "
            f"stdout_observation_equal={stdout_observation_equal}, "
            f"stdout_stream_equal={stdout_stream_equal}, "
            f"stderr_equal={normalized_stderr == oracle['stderr']})"
        )
    aggregate = aggregate_invocations(invocations)
    aggregate.update(
        {
            f"controller_{key}": value
            for key, value in controller_aggregate.items()
        }
    )
    return {
        "qualified": True,
        "stdout_contract": stdout_contract,
        "stdout_observation_equal": stdout_observation_equal,
        "stdout_stream_equal": stdout_stream_equal,
        "process_elapsed_ns": process_elapsed_ns,
        "invocation_stats": invocations,
        "controller_stats": controller_invocations,
        "aggregate": aggregate,
    }


def write_summary_tsv(path: Path, results: dict[str, Any]) -> None:
    columns = (
        "workload",
        "runs",
        "stdout_contract",
        "stdout_observation_equal",
        "stdout_stream_equal",
        "process_seconds",
        "machine_seconds",
        "invocations",
        "transitions",
        "clause_snapshot_calls",
        "clause_snapshot_cache_hits",
        "clause_snapshot_equality_checks",
        "clause_candidates",
        "clause_match_attempts",
        "clause_branches_scheduled",
        "clause_match_allocated_bytes",
        "unification_calls",
        "unification_binding_writes",
        "binding_apply_rewrites",
        "binding_apply_allocated_bytes",
        "binding_apply_environment_entries",
        "binding_apply_epoch_calls",
        "binding_apply_epoch_suffix_entries",
        "solve_expression_apply_calls",
        "solve_expression_apply_allocated_bytes",
        "solve_expression_open_template_admitted_calls",
        "solve_expression_open_template_admitted_allocated_bytes",
        "solve_expected_apply_calls",
        "solve_expected_apply_allocated_bytes",
        "solve_expected_open_template_admitted_calls",
        "solve_expected_open_template_admitted_allocated_bytes",
        "activation_materialization_calls",
        "activation_materialization_allocated_bytes",
        "activation_open_template_admitted_calls",
        "activation_open_template_admitted_allocated_bytes",
        "atom_freshen_allocated_bytes",
        "specializer_prepare_calls",
        "specializer_prepare_filtered",
        "specializer_prepare_relevance_bounded",
        "specializer_prepare_rewritten",
        "specializer_prepare_unchanged",
        "specializer_prepare_capacity_declines",
        "specializer_prepare_elapsed_ns",
        "match_existence_observer_folds",
        "child_machine_init_attempts",
        "child_machine_init_successes",
        "child_machine_projected_entries",
        "child_machine_projection_elapsed_ns",
        "child_machine_init_elapsed_ns",
        "child_machine_destroy_calls",
        "child_machine_destroy_elapsed_ns",
        "heap_bytes_reclaimed",
        "ttfa_ms_max",
        "max_goal_depth",
        "max_choice_depth",
        "max_binding_entries",
        "max_binding_apply_environment_entries",
        "max_binding_apply_epoch_suffix_entries",
        "controller_records",
        "controller_admitted",
        "controller_active_fifo",
        "controller_active_inline_depth_first",
        "controller_active_refused",
        "controller_storage_full_image",
        "controller_storage_none",
        "controller_scheduling_rounds",
        "controller_transitions",
        "controller_expansions",
        "controller_successors",
        "controller_captures",
        "controller_restores",
        "controller_capture_elapsed_ns",
        "controller_restore_elapsed_ns",
        "controller_expansion_elapsed_ns",
        "controller_refusals",
        "controller_answers",
        "controller_max_frontier",
        "controller_max_frontier_shared_bytes",
        "controller_max_frontier_exclusive_bytes",
    )
    rows = ["\t".join(columns)]
    for name, result in results.items():
        median = result["median"]
        values: dict[str, str | int | float] = {
            "workload": name,
            "runs": len(result["runs"]),
            "stdout_contract": result["runs"][0]["stdout_contract"],
            "stdout_observation_equal": int(all(
                run["stdout_observation_equal"]
                for run in result["runs"]
            )),
            "stdout_stream_equal": int(all(
                run["stdout_stream_equal"]
                for run in result["runs"]
            )),
            "process_seconds": (
                float(median["process_elapsed_ns"]) / 1e9
            ),
            "machine_seconds": (
                float(median.get("active_elapsed_ns", 0)) / 1e9
            ),
            "ttfa_ms_max": float(median.get("ttfa_ns_max", 0)) / 1e6,
        }
        for column in columns:
            if column not in values:
                values[column] = median.get(column, 0)
        rows.append(
            "\t".join(
                f"{values[column]:.6f}"
                if isinstance(values[column], float)
                else str(values[column])
                for column in columns
            )
        )
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--petta-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--workload", action="append", dest="workloads"
    )
    parser.add_argument(
        "--specializer-relevance-filter",
        choices=("0", "1"),
        default="0",
    )
    parser.add_argument(
        "--specializer-route-cache",
        choices=("0", "1"),
        default="1",
    )
    parser.add_argument(
        "--term-universe-source-id-memo",
        choices=("0", "1"),
        default="0",
    )
    parser.add_argument(
        "--clause-body-activation",
        choices=("0", "1"),
        default="0",
    )
    parser.add_argument(
        "--search-controller",
        choices=("inline-depth-first", "fifo"),
        default="inline-depth-first",
    )
    parser.add_argument(
        "--paired-baseline",
        action="store_true",
        help=(
            "interleave each candidate run with the all-contenders-off "
            "reference on the same binary"
        ),
    )
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")

    cetta = args.cetta.resolve()
    petta_dir = args.petta_dir.resolve()
    manifest_path = args.manifest.resolve()
    corpus.verify_manifest(petta_dir, manifest_path, True)
    manifest = corpus.load_manifest(manifest_path)
    entries = {entry["name"]: entry for entry in manifest["entries"]}
    workloads = tuple(args.workloads or DEFAULT_WORKLOADS)
    unknown = sorted(set(workloads) - set(entries))
    if unknown:
        raise RuntimeError("unknown workloads: " + ", ".join(unknown))

    candidate_environment = {
        "CETTA_PETTA_SEARCH_MACHINE": "1",
        "CETTA_PETTA_MACHINE_STATS": "1",
        "CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER": (
            args.specializer_relevance_filter
        ),
        "CETTA_PETTA_SPECIALIZER_ROUTE_CACHE": (
            args.specializer_route_cache
        ),
        "CETTA_TERM_UNIVERSE_SOURCE_ID_MEMO": (
            args.term_universe_source_id_memo
        ),
        "CETTA_PETTA_CLAUSE_BODY_ACTIVATION": (
            args.clause_body_activation
        ),
        "CETTA_PETTA_CLAUSE_BODY_ACTIVATION_REFERENCE": "0",
        "CETTA_SEARCH_CONTROLLER": args.search_controller,
    }
    baseline_environment = {
        "CETTA_PETTA_SEARCH_MACHINE": "1",
        "CETTA_PETTA_MACHINE_STATS": "1",
        "CETTA_PETTA_SPECIALIZER_RELEVANCE_FILTER": "0",
        "CETTA_PETTA_SPECIALIZER_ROUTE_CACHE": (
            args.specializer_route_cache
        ),
        "CETTA_TERM_UNIVERSE_SOURCE_ID_MEMO": "0",
        "CETTA_PETTA_CLAUSE_BODY_ACTIVATION": "0",
        "CETTA_PETTA_CLAUSE_BODY_ACTIVATION_REFERENCE": "1",
        "CETTA_SEARCH_CONTROLLER": args.search_controller,
    }

    results: dict[str, Any] = {}
    baseline_results: dict[str, Any] = {}
    for name in workloads:
        print(f"[profile] {name}", flush=True)
        runs: list[dict[str, Any]] = []
        baseline_runs: list[dict[str, Any]] = []
        for index in range(args.runs):
            legs = [("candidate", candidate_environment)]
            if args.paired_baseline:
                legs = [
                    ("baseline", baseline_environment),
                    ("candidate", candidate_environment),
                ]
                if index % 2 == 1:
                    legs.reverse()
            for label, environment in legs:
                run = run_workload(
                    cetta,
                    petta_dir,
                    entries[name],
                    args.timeout,
                    environment,
                )
                if label == "candidate":
                    runs.append(run)
                else:
                    baseline_runs.append(run)
                print(
                    f"  {label} {index + 1}/{args.runs}: qualified "
                    f"contract={run['stdout_contract']} "
                    f"stream="
                    f"{'exact' if run['stdout_stream_equal'] else 'reordered'}, "
                    f"machine="
                    f"{run['aggregate']['active_elapsed_ns'] / 1e9:.3f}s",
                    flush=True,
                )
        results[name] = {
            "runs": runs,
            "median": median_runs(runs),
        }
        if args.paired_baseline:
            baseline_results[name] = {
                "runs": baseline_runs,
                "median": median_runs(baseline_runs),
            }

    revision = subprocess.run(
        ["git", "-C", str(cetta.parent), "rev-parse", "HEAD"],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout.strip()
    candidate_diff = subprocess.run(
        ["git", "-C", str(cetta.parent), "diff", "--binary"],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
    document = {
        "schema": (
            "cetta-petta-machine-profile-v4-paired"
            if args.paired_baseline
            else "cetta-petta-machine-profile-v4"
        ),
        "cetta_revision": revision,
        "candidate_diff_sha256": corpus.sha256_bytes(candidate_diff),
        "cetta_binary_sha256": corpus.sha256_file(cetta),
        "petta_revision": manifest["petta_revision"],
        "runs_per_workload": args.runs,
        "candidate_environment": candidate_environment,
        "measurement": (
            "CLOCK_MONOTONIC time accumulated inside petta_machine_next; "
            "process elapsed time retained separately"
        ),
        "results": results,
    }
    if args.paired_baseline:
        document["baseline_environment"] = baseline_environment
        document["baseline_results"] = baseline_results
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_summary_tsv(args.out.with_suffix(".tsv"), results)
    if args.paired_baseline:
        baseline_tsv = args.out.with_name(
            args.out.stem + ".baseline.tsv"
        )
        write_summary_tsv(baseline_tsv, baseline_results)
    legs_per_workload = 2 if args.paired_baseline else 1
    print(
        f"PASS: {len(results)} workloads qualified across {args.runs} runs "
        f"and {legs_per_workload} leg(s) under controller receipts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
