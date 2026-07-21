#!/usr/bin/env python3
"""Qualify main-readiness detectors against adversarial evidence mutations."""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import json
import os
import sys
from pathlib import Path
from typing import Any

from cetta_readiness_ladders import primary_contract, run_case, series_evidence
from cetta_readiness_model import (
    ReadinessModelError,
    load_property_manifest,
    sha256_file,
)
from rhocalc_cost_observer_transparency import (
    claim_conservation_failures,
    counters,
    normalized,
    run,
    state_only_command,
    state_only_counter_failures,
)


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "benchmarks/main_readiness_properties.json"
EXPECTED_MUTANTS = {
    "mutant:quadratic-growth",
    "mutant:per-entry-leak",
    "mutant:wrong-backend-routing",
    "mutant:observer-work-state-only",
    "mutant:broken-claim-rollback",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run real positive witnesses, mutate their structured evidence, and "
            "require the production readiness contracts to reject every mutant."
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
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "runtime/main_readiness_mutation_qualification.json",
    )
    return parser.parse_args()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ReadinessModelError(message)


def killed(name: str, failures: list[str], details: dict[str, Any]) -> dict[str, Any]:
    require(bool(failures), f"{name} survived the production readiness contract")
    return {
        "mutant": name,
        "status": "killed",
        "detector_failures": failures,
        "details": details,
    }


def run_counter_mutations(binary: Path) -> list[dict[str, Any]]:
    state_fixture = ROOT / "tests/rhocalc_cost_run/internal_split_tokens_basic.mrho"
    state_expected = ROOT / "tests/rhocalc_cost_run/internal_split_tokens_basic.expected"
    state = run(state_only_command(str(binary), state_fixture, 4))
    require(state.returncode == 0, "state-only observer witness failed")
    require(
        normalized(state.stdout) == normalized(state_expected.read_text()),
        "state-only observer witness residual changed",
    )
    state_stats = counters(state.stderr)
    require(
        not state_only_counter_failures(state_stats),
        "positive state-only observer counter witness failed",
    )
    observer_mutant = dict(state_stats)
    observer_mutant["cost-rho-receipt-event-retained"] = 1
    observer_failures = state_only_counter_failures(observer_mutant)

    rollback_fixture = (
        ROOT / "tests/rhocalc_cost_run/parallel_claim_rollback_competition.mrho"
    )
    rollback_expected = (
        ROOT / "tests/rhocalc_cost_run/parallel_claim_rollback_competition.expected"
    )
    rollback = run(state_only_command(str(binary), rollback_fixture, 4))
    require(rollback.returncode == 0, "claim-rollback witness failed")
    require(
        normalized(rollback.stdout) == normalized(rollback_expected.read_text()),
        "claim-rollback witness residual changed",
    )
    rollback_stats = counters(rollback.stderr)
    require(
        not claim_conservation_failures(rollback_stats),
        "positive claim-conservation witness failed",
    )
    acquired = rollback_stats.get("cost-rho-parallel-acquired-claim", 0)
    committed = rollback_stats.get("cost-rho-parallel-committed-claim", 0)
    released = rollback_stats.get("cost-rho-parallel-released-claim", 0)
    require(
        released > 0 and acquired > committed,
        "claim-rollback witness did not exercise partial acquisition and release",
    )
    rollback_mutant = dict(rollback_stats)
    rollback_mutant["cost-rho-parallel-released-claim"] = released - 1
    rollback_failures = claim_conservation_failures(rollback_mutant)

    return [
        killed(
            "mutant:observer-work-state-only",
            observer_failures,
            {"mutated_counter": "cost-rho-receipt-event-retained"},
        ),
        killed(
            "mutant:broken-claim-rollback",
            rollback_failures,
            {
                "positive_acquired": acquired,
                "positive_committed": committed,
                "positive_released": released,
            },
        ),
    ]


def run_storage_mutations(
    binary: Path, manifest: dict[str, Any]
) -> list[dict[str, Any]]:
    sizes = (100, 300, 1000, 3000)
    native_runs = [
        run_case(
            kind="backend",
            case="native",
            size=size,
            binary=binary,
            timeout=120,
        )
        for size in sizes
    ]
    require(
        not [failure for item in native_runs for failure in item["primary_failures"]],
        "positive native storage witness failed its counter contract",
    )
    memory_limits = manifest["modeled_memory_bytes_per_entry_limits"]
    positive_series = series_evidence(
        native_runs,
        max_time_slope=1.35,
        max_rss_slope=1.25,
        memory_limits=memory_limits,
        baseline_runs=copy.deepcopy(native_runs),
    )
    native_memory = positive_series["backend:native"]["modeled_memory"]
    require(
        native_memory is not None and native_memory["passed"],
        "positive native modeled-memory witness failed",
    )
    require(
        positive_series["backend:native"]["paired"]["passed"],
        "positive paired-growth witness failed",
    )

    quadratic_baseline = copy.deepcopy(native_runs)
    quadratic_runs = copy.deepcopy(native_runs)
    for baseline_item, mutant_item in zip(quadratic_baseline, quadratic_runs):
        baseline_item["row"]["scenario_ns"] = str(
            100_000 * baseline_item["size"]
        )
        mutant_item["row"]["scenario_ns"] = str(
            1_000 * mutant_item["size"] ** 2
        )
    quadratic_series = series_evidence(
        quadratic_runs,
        max_time_slope=1.35,
        max_rss_slope=1.25,
        memory_limits=memory_limits,
        baseline_runs=quadratic_baseline,
    )
    quadratic_growth = quadratic_series["backend:native"]["paired"]
    quadratic_failures = (
        []
        if quadratic_growth["passed"]
        else [
            "candidate scaling diverges from the paired positive baseline"
        ]
    )

    leak_runs = copy.deepcopy(native_runs)
    for item in leak_runs:
        item["row"]["term_universe_blob_bytes"] = str(
            4096 + 256 * item["size"]
        )
        item["row"]["space_atom_id_capacity_bytes_peak"] = "0"
    leak_series = series_evidence(
        leak_runs,
        max_time_slope=1.35,
        max_rss_slope=1.25,
        memory_limits=memory_limits,
    )
    leak_memory = leak_series["backend:native"]["modeled_memory"]
    require(leak_memory is not None, "native modeled-memory detector disappeared")
    leak_failures = (
        []
        if leak_memory["passed"]
        else [
            "modeled memory rate "
            f"{leak_memory['bytes_per_entry']:.6f} exceeds "
            f"{leak_memory['max_bytes_per_entry']:.6f} bytes per entry"
        ]
    )

    pathmap = run_case(
        kind="backend",
        case="pathmap",
        size=100,
        binary=binary,
        timeout=120,
    )
    require(
        not pathmap["primary_failures"],
        "positive PathMap route witness failed",
    )
    wrong_route = dict(pathmap["row"])
    wrong_route["pathmap_direct_store"] = "0"
    wrong_route["mork_add_call"] = wrong_route["fact_count"]
    route_failures = primary_contract("backend", wrong_route)

    return [
        killed(
            "mutant:quadratic-growth",
            quadratic_failures,
            {"mutated_series": "backend:native"},
        ),
        killed(
            "mutant:per-entry-leak",
            leak_failures,
            {"mutated_series": "backend:native"},
        ),
        killed(
            "mutant:wrong-backend-routing",
            route_failures,
            {"mutated_series": "backend:pathmap"},
        ),
    ]


def main() -> int:
    args = parse_args()
    binary = args.binary.expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"FAIL: missing executable stats binary: {binary.name}", file=sys.stderr)
        return 2
    manifest = load_property_manifest(MANIFEST)
    declared_mutants = set(manifest["qualification_mutants"])
    require(
        declared_mutants == EXPECTED_MUTANTS,
        "property manifest mutation set differs from the qualification corpus",
    )
    try:
        results = [
            *run_storage_mutations(binary, manifest),
            *run_counter_mutations(binary),
        ]
    except (OSError, ReadinessModelError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    killed_mutants = {item["mutant"] for item in results if item["status"] == "killed"}
    passed = killed_mutants == EXPECTED_MUTANTS
    try:
        binary_label = binary.relative_to(ROOT).as_posix()
    except ValueError:
        binary_label = binary.name
    summary = {
        "schema": "cetta.main-readiness.mutation-qualification.v1",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "status": "passed" if passed else "failed",
        "manifest_schema": manifest["schema"],
        "manifest_sha256": sha256_file(MANIFEST),
        "binary": binary_label,
        "binary_sha256": sha256_file(binary),
        "expected_mutants": sorted(EXPECTED_MUTANTS),
        "killed_mutants": sorted(killed_mutants),
        "results": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"WITNESS_EVIDENCE_SCHEMA={summary['schema']}")
    print(f"WITNESS_EVIDENCE_STATUS={summary['status']}")
    print(f"WITNESS_EVIDENCE_SHA256={sha256_file(args.output)}")
    print(f"WITNESS_EVIDENCE_MUTANTS_KILLED={len(killed_mutants)}")
    for item in results:
        print(f"PASS killed {item['mutant']}")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ReadinessModelError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
