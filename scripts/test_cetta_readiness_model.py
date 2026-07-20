#!/usr/bin/env python3
"""Adversarial self-tests for the runtime-readiness evidence model."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from bench_d4_progress import census_classification
from cetta_readiness_model import (
    ReadinessModelError,
    calibration_subject,
    counter_contract,
    evidence_key,
    growth_verdict,
    load_property_manifest,
    memory_rate_verdict,
    paired_growth_verdict,
    qualification_verdict,
    resolve_source_anchors,
    route_contract,
    terminate_process_session,
    working_tree_content_identity,
)


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "benchmarks/main_readiness_properties.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def session_pids(session_id: int) -> list[int]:
    output = subprocess.check_output(
        ["ps", "-eo", "pid=,sid="], text=True
    )
    return [
        int(fields[0])
        for line in output.splitlines()
        if len(fields := line.split()) == 2 and int(fields[1]) == session_id
    ]


def check_session_cleanup(command: list[str], *, leader_exits: bool) -> None:
    process = subprocess.Popen(command, start_new_session=True)
    try:
        if leader_exits:
            process.wait(timeout=2)
        time.sleep(0.1)
        require(session_pids(process.pid), "session-cleanup fixture has no process")
        terminate_process_session(process)
        require(not session_pids(process.pid), "readiness process session survived cleanup")
    finally:
        terminate_process_session(process)


def process_rows() -> list[tuple[int, int, int]]:
    output = subprocess.check_output(
        ["ps", "-eo", "pid=,ppid=,sid="], text=True
    )
    return [
        tuple(map(int, fields))
        for line in output.splitlines()
        if len(fields := line.split()) == 3
    ]


def descendants_of(parent: int) -> list[tuple[int, int, int]]:
    descendants = {parent}
    rows = process_rows()
    changed = True
    while changed:
        changed = False
        for pid, ppid, _sid in rows:
            if ppid in descendants and pid not in descendants:
                descendants.add(pid)
                changed = True
    return [row for row in rows if row[0] in descendants]


def check_nested_session_cleanup() -> None:
    process = subprocess.Popen(
        ["bash", "-c", "setsid bash -c 'sleep 300' & wait"],
        start_new_session=True,
    )
    try:
        time.sleep(0.1)
        nested = descendants_of(process.pid)
        nested_pids = {pid for pid, _ppid, _sid in nested}
        require(
            any(sid != process.pid for _pid, _ppid, sid in nested),
            "nested-session fixture did not create a private child session",
        )
        terminate_process_session(process)
        live_pids = {pid for pid, _ppid, _sid in process_rows()}
        require(
            not (nested_pids & live_pids),
            "nested readiness process session survived cleanup",
        )
    finally:
        terminate_process_session(process)


def benchmark_runtime_artifacts() -> set[Path]:
    roots = (
        ROOT / "runtime/bench_space_backend",
        ROOT / "runtime/bench_space_transfer",
    )
    return {
        artifact.resolve()
        for runtime_root in roots
        if runtime_root.exists()
        for artifact in runtime_root.iterdir()
        if artifact.is_dir() and artifact.name.startswith(".bench_space_")
    }


def run_scale_fixture(environment: dict[str, str]) -> subprocess.CompletedProcess[str]:
    artifacts_before = benchmark_runtime_artifacts()
    try:
        return subprocess.run(
            ["./scripts/bench_space_scale_ladder.sh", "100", "1"],
            cwd=ROOT,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    finally:
        for artifact in benchmark_runtime_artifacts() - artifacts_before:
            shutil.rmtree(artifact)


def check_scale_census_exit_contract() -> None:
    runtime = ROOT / "runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="scale-census-contract-", dir=runtime
    ) as scratch:
        fake_binary = Path(scratch) / "fake-cetta"
        fake_binary.write_text(
            "#!/usr/bin/env bash\n"
            "if [ \"${CETTA_SCALE_FIXTURE_MODE:-fail}\" = timeout ]; then\n"
            "  sleep 5\n"
            "fi\n"
            "exit 7\n",
            encoding="utf-8",
        )
        fake_binary.chmod(0o755)
        base_environment = os.environ.copy()
        base_environment.update(
            {
                "CETTA_BIN": str(fake_binary),
                "BACKEND_MODES_STR": "native",
                "TRANSFER_CASES_STR": "native-to-pathmap",
            }
        )
        failure_environment = dict(base_environment)
        failure_environment.update(
            {"CETTA_SCALE_FIXTURE_MODE": "fail", "TIMEOUT_S": "5"}
        )
        failed = run_scale_fixture(failure_environment)
        require(failed.returncode != 0, "scale census accepted execution failure")
        require(
            "SCALE_CENSUS_STATUS=failed" in failed.stdout
            and "SCALE_CENSUS_FAILURES=2" in failed.stdout,
            "scale census did not preserve genuine failure evidence",
        )
        print("PASS scale census rejects genuine execution failures")

        timeout_environment = dict(base_environment)
        timeout_environment.update(
            {"CETTA_SCALE_FIXTURE_MODE": "timeout", "TIMEOUT_S": "0.05"}
        )
        observed = run_scale_fixture(timeout_environment)
        require(
            observed.returncode == 0,
            "non-authoritative scale timeout made the capability census fail",
        )
        require(
            "SCALE_CENSUS_STATUS=observed" in observed.stdout
            and "SCALE_CENSUS_FAILURES=0" in observed.stdout
            and "SCALE_CENSUS_TIMEOUTS=2" in observed.stdout
            and "SCALE_CENSUS_AUTHORITATIVE_READINESS=0" in observed.stdout,
            "scale timeout was not explicitly classified as non-authoritative",
        )
        print("PASS scale timeouts remain non-authoritative observations")


def main() -> int:
    manifest = load_property_manifest(MANIFEST)
    anchors = resolve_source_anchors(ROOT, manifest)
    require(anchors, "source anchors did not resolve")
    require(
        {anchor["property"] for anchor in anchors}
        == {prop["id"] for prop in manifest["properties"]},
        "not every property has a resolved source anchor",
    )
    print(f"PASS readiness manifest {len(manifest['properties'])} properties")
    print(f"PASS readiness source anchors {len(anchors)} resolved")
    require(
        manifest["negative_calibration_witnesses"],
        "negative calibration registry is empty",
    )
    print(
        "PASS readiness negative registry "
        f"{len(manifest['negative_calibration_witnesses'])} resolved"
    )

    check_session_cleanup(
        ["bash", "-c", "(sleep 300) & wait"], leader_exits=False
    )
    print("PASS live readiness process session is cleaned recursively")
    check_session_cleanup(
        ["bash", "-c", "sleep 300 & exit 0"], leader_exits=True
    )
    print("PASS exited readiness session leader cannot strand descendants")
    check_nested_session_cleanup()
    print("PASS nested readiness process sessions are cleaned recursively")
    check_scale_census_exit_contract()

    candidate_content = working_tree_content_identity(ROOT)
    require(
        set(candidate_content)
        == {
            "schema",
            "file_count",
            "paths_sha256",
            "content_sha256",
            "excluded_paths",
        },
        "candidate content identity leaked Git topology",
    )
    require(
        candidate_content["excluded_paths"] == ["README.md"],
        "the concurrent README edit was not isolated from readiness identity",
    )
    print("PASS candidate identity is content-addressed, not commit-addressed")

    sizes = [10000.0, 30000.0, 100000.0, 300000.0]
    linear = [(size, 0.00002 * size + 0.2) for size in sizes]
    linear_rss = [(size, 16000.0 + 0.04 * size) for size in sizes]
    require(growth_verdict(linear, linear_rss)["passed"],
            "positive linear calibration was rejected")
    print("PASS positive linear growth calibration")

    quadratic = [(size, 1e-8 * size * size) for size in sizes]
    require(not growth_verdict(quadratic, linear_rss)["passed"],
            "quadratic-growth mutant survived")
    print("PASS mutant quadratic-growth rejected")

    baseline_bytes = [(size, 24.0 * size + 4096.0) for size in sizes]
    require(
        memory_rate_verdict(
            baseline_bytes, max_bytes_per_entry=48.0
        )["passed"],
        "positive bytes-per-entry calibration was rejected",
    )
    leaking_bytes = [(size, 96.0 * size + 4096.0) for size in sizes]
    require(
        not memory_rate_verdict(
            leaking_bytes, max_bytes_per_entry=48.0
        )["passed"],
        "per-entry-leak mutant survived",
    )
    print("PASS mutant per-entry-leak rejected")

    paired_arguments = {
        "max_time_ratio": 1.5,
        "time_absolute_slack": 0.01,
        "max_time_slope_delta": 0.35,
        "max_rss_ratio": 1.25,
        "rss_absolute_slack": 4096.0,
        "max_rss_slope_delta": 0.25,
    }
    baseline_time = [(size, 1e-7 * size**1.8) for size in sizes]
    candidate_time = [(size, value * 1.05) for size, value in baseline_time]
    baseline_rss_growth = [(size, 1000.0 + size**0.8) for size in sizes]
    candidate_rss_growth = [
        (size, value * 1.05) for size, value in baseline_rss_growth
    ]
    require(
        paired_growth_verdict(
            candidate_time,
            baseline_time,
            candidate_rss_growth,
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "baseline-equivalent superlinear implementation was rejected",
    )
    print("PASS paired growth accepts unchanged implementation shape")

    quadratic_candidate = [(size, 1e-8 * size**2) for size in sizes]
    linear_baseline = [(size, 2e-5 * size) for size in sizes]
    require(
        not paired_growth_verdict(
            quadratic_candidate,
            linear_baseline,
            candidate_rss_growth,
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "paired quadratic-growth mutant survived",
    )
    print("PASS paired growth rejects a complexity-class regression")

    require(
        not paired_growth_verdict(
            [(size, value * 2.0) for size, value in baseline_time],
            baseline_time,
            candidate_rss_growth,
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "paired constant-time slowdown survived",
    )
    print("PASS paired growth rejects a material constant slowdown")

    require(
        not paired_growth_verdict(
            candidate_time,
            baseline_time,
            [(size, value * 2.0) for size, value in baseline_rss_growth],
            baseline_rss_growth,
            **paired_arguments,
        )["passed"],
        "paired RSS-growth mutant survived",
    )
    print("PASS paired growth rejects material RSS growth")

    route_ok, _ = route_contract("pathmap-primary", "native-space")
    require(not route_ok, "wrong-backend-routing mutant survived")
    print("PASS mutant wrong-backend-routing rejected")

    observer_counters = {
        "cost-rho-parallel-state-only-run": 1,
        "cost-rho-receipt-event-retained": 1,
    }
    observer_ok, _ = counter_contract(
        observer_counters,
        positive=("cost-rho-parallel-state-only-run",),
        zero=("cost-rho-receipt-event-retained",),
    )
    require(not observer_ok, "observer-work-state-only mutant survived")
    print("PASS mutant observer-work-state-only rejected")

    claim_counters = {"acquired": 7, "committed": 3, "released": 3}
    claim_ok, _ = counter_contract(
        claim_counters,
        conservation=(("acquired", "committed", "released"),),
    )
    require(not claim_ok, "broken-claim-rollback mutant survived")
    print("PASS mutant broken-claim-rollback rejected")

    identity = {
        "schema": "cetta.main-readiness.evidence.v3",
        "candidate": "source-a",
        "binary": "binary-a",
        "inputs": "inputs-a",
        "compiler": "gcc-a",
        "dependencies": {"mork": "mork-a"},
        "suite": "suite-a",
    }
    original = evidence_key(identity)
    for field in ("candidate", "binary", "inputs", "compiler", "suite"):
        changed = dict(identity)
        changed[field] = f"{field}-b"
        require(evidence_key(changed) != original,
                f"evidence key ignored {field}")
    changed_dependencies = json.loads(json.dumps(identity))
    changed_dependencies["dependencies"]["mork"] = "mork-b"
    require(evidence_key(changed_dependencies) != original,
            "evidence key ignored dependency commit")
    print("PASS content-addressed evidence key covers all required dimensions")

    artifact = {
        "schema": "cetta.main-readiness.evidence.v3",
        "tier": "routine",
        "candidate": {"tree": "candidate-a", "diff": "diff-a"},
        "binaries": {"cetta": "binary-a"},
        "inputs": {"suite": "inputs-a"},
        "compiler_and_build": {
            "generated": {"config.h": "generated-a"},
            "gcc": "gcc-a",
            "make": "make-a",
            "python": "python-a",
        },
        "dependencies": {"MORK": {"tree": "mork-a"}},
        "property_schema": "properties-v1",
        "property_manifest_sha256": "manifest-a",
        "source_anchors": [{"path": "source.c", "sha256": "source-a"}],
    }
    subject = calibration_subject(artifact)
    changed_incidentals = json.loads(json.dumps(artifact))
    changed_incidentals["tier"] = "exhaustive"
    changed_incidentals["binaries"]["cetta"] = "binary-b"
    changed_incidentals["compiler_and_build"]["generated"]["config.h"] = (
        "generated-b"
    )
    require(
        calibration_subject(changed_incidentals) == subject,
        "calibration subject included tier-local build products",
    )
    print("PASS calibration subject excludes tier-local build products")

    subject_key = evidence_key(subject)
    records = [
        {
            "pair_id": f"pair-{index}",
            "subject_key": subject_key,
            "routine_status": "passed",
            "exhaustive_status": "passed",
            "routine_mutants_killed": True,
            "baseline_match": True,
            "common_prefix_match": True,
        }
        for index in range(5)
    ]
    require(
        qualification_verdict(
            records, required_runs=5, subject_key=subject_key
        )["qualified"],
        "five clean differential pairs did not qualify",
    )
    print("PASS five clean differential pairs qualify")

    disagreement = json.loads(json.dumps(records))
    disagreement[-1]["exhaustive_status"] = "failed"
    require(
        not qualification_verdict(
            disagreement, required_runs=5, subject_key=subject_key
        )["qualified"],
        "routine/exhaustive disagreement was accepted",
    )
    print("PASS routine/exhaustive disagreement blocks qualification")

    unqualified_mutant = json.loads(json.dumps(records))
    unqualified_mutant[-1]["routine_mutants_killed"] = False
    require(
        not qualification_verdict(
            unqualified_mutant, required_runs=5, subject_key=subject_key
        )["qualified"],
        "surviving mutant was accepted",
    )
    print("PASS surviving mutant blocks qualification")

    unauthenticated_prefix = json.loads(json.dumps(records))
    unauthenticated_prefix[-1]["common_prefix_match"] = False
    require(
        not qualification_verdict(
            unauthenticated_prefix,
            required_runs=5,
            subject_key=subject_key,
        )["qualified"],
        "unauthenticated common-prefix evidence was accepted",
    )
    print("PASS unauthenticated common-prefix reuse blocks qualification")

    require(
        census_classification({"passed": True, "status": "completed"})
        == ("progress", True),
        "completed D4 census was not classified as observed progress",
    )
    require(
        census_classification(
            {"passed": False, "status": "bounded-progress"}
        )
        == ("boundary", True),
        "bounded D4 census was incorrectly granted readiness credit",
    )
    require(
        census_classification({"passed": False, "status": "failed"})
        == ("failed", False),
        "failed D4 process was accepted as a capability boundary",
    )
    print("PASS D4 capability census remains non-authoritative")

    malformed = json.loads(MANIFEST.read_text(encoding="utf-8"))
    malformed["properties"][1]["largest_scale_class"] = (
        malformed["properties"][0]["largest_scale_class"]
    )
    scratch = ROOT / "runtime/main-readiness-manifest-negative.json"
    scratch.parent.mkdir(parents=True, exist_ok=True)
    scratch.write_text(json.dumps(malformed), encoding="utf-8")
    try:
        try:
            load_property_manifest(scratch)
        except ReadinessModelError:
            pass
        else:
            raise AssertionError("duplicate largest-scale class was accepted")
    finally:
        scratch.unlink(missing_ok=True)
    print("PASS duplicate largest-scale class rejected")

    malformed = json.loads(MANIFEST.read_text(encoding="utf-8"))
    malformed["properties"][0]["largest_scale_witness"] = (
        malformed["properties"][0]["routine_witnesses"][0]
    )
    scratch.write_text(json.dumps(malformed), encoding="utf-8")
    try:
        try:
            load_property_manifest(scratch)
        except ReadinessModelError:
            pass
        else:
            raise AssertionError("routine proxy was accepted as largest-scale witness")
    finally:
        scratch.unlink(missing_ok=True)
    print("PASS largest-scale witness must belong to exhaustive evidence")
    print("SUMMARY cetta-readiness-model 30/30 PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ReadinessModelError) as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
