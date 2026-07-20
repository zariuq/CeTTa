#!/usr/bin/env python3
"""Run and record CeTTa's routine or exhaustive main-readiness matrix."""

from __future__ import annotations

import datetime as dt
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from cetta_readiness_model import (
    EVIDENCE_SCHEMA,
    calibration_subject,
    dependency_identity,
    evidence_key,
    load_property_manifest,
    repository_identity,
    resolve_source_anchors,
    sha256_file as model_sha256_file,
    terminate_process_session,
    working_tree_content_identity,
)


PERFORMANCE_BASELINE_COMMIT = "9050a2d171946f6edf417152f259b7bc4e98907d"
PROPERTY_MANIFEST = "benchmarks/main_readiness_properties.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run CeTTa runtime main-readiness gates with recorded evidence."
    )
    parser.add_argument(
        "--tier",
        choices=("routine", "exhaustive"),
        default="exhaustive",
        help="routine is the qualifying fast gate; exhaustive is the merge boundary",
    )
    parser.add_argument(
        "--reuse",
        action="store_true",
        help="reuse only an exact content-addressed passing artifact",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="validate identities and print commands without running them",
    )
    parser.add_argument(
        "--reuse-common-from",
        type=Path,
        help=(
            "for paired qualification only: compose the exhaustive tier with "
            "the exact passing common-prefix evidence from a routine tier"
        ),
    )
    parser.add_argument(
        "--reuse-common-summary-sha256",
        help="required caller-known digest for --reuse-common-from",
    )
    return parser.parse_args()


def capture(command: list[str], root: Path) -> str:
    result = subprocess.run(
        command,
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return result.stdout.strip()


def sha256_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def explicit_input_identity(root: Path) -> dict[str, str]:
    paths = [
        "Makefile",
        PROPERTY_MANIFEST,
        "benchmarks/witness_catalog.tsv",
        "benchmarks/baseline_records.tsv",
        "benchmarks/rhocalc_performance_baseline_9050a2d.tsv",
        "scripts/cetta_main_readiness.py",
        "scripts/cetta_readiness_model.py",
        "scripts/cetta_readiness_ladders.py",
        "scripts/cetta_readiness_thresholds.py",
        "scripts/cetta_readiness_mutation_qualification.py",
        "scripts/cetta_readiness_calibrate.py",
        "scripts/bench_d4_progress.py",
        "scripts/test_cetta_readiness_model.py",
        "scripts/run_witness.sh",
        "scripts/compare_witness.sh",
        "scripts/bench_space_backend_matrix.sh",
        "scripts/bench_space_transfer_matrix.sh",
        "scripts/bench_space_scale_ladder.sh",
        "scripts/rhocalc_paired_samples.py",
        "scripts/test_rhocalc_paired_samples.py",
        "scripts/rhocalc_threaded_bench.py",
        "scripts/rhocalc_cost_threaded_bench.py",
        "scripts/rhocalc_benchmark_regression.py",
        "scripts/rhocalc_cost_observer_transparency.py",
        "tests/rhocalc_cost_run/parallel_claim_rollback_competition.mrho",
        "tests/rhocalc_cost_run/parallel_claim_rollback_competition.expected",
        "tests/test_manifest.tsv",
    ]
    result: dict[str, str] = {}
    for name in paths:
        path = root / name
        if not path.is_file():
            raise RuntimeError(f"required readiness input is missing: {name}")
        result[name] = model_sha256_file(path)
    return result


def build_configuration_identity(root: Path) -> dict[str, Any]:
    generated = {
        path.relative_to(root).as_posix(): model_sha256_file(path)
        for path in sorted((root / "runtime/bootstrap").glob("build_config.*.h"))
        if path.is_file()
    }
    return {
        "generated": generated,
        "gcc": capture(["gcc", "--version"], root).splitlines()[0],
        "make": capture(["make", "--version"], root).splitlines()[0],
        "python": sys.version,
        "rustc": capture(["rustc", "--version"], root),
        "cargo": capture(["cargo", "--version"], root),
    }


def artifact_identity(
    root: Path,
    mettapedia_root: Path,
    tier: str,
    manifest: dict[str, Any],
    anchors: list[dict[str, Any]],
) -> dict[str, Any]:
    return {
        "schema": EVIDENCE_SCHEMA,
        "tier": tier,
        "candidate": working_tree_content_identity(root),
        "binaries": binary_identity(root),
        "inputs": explicit_input_identity(root),
        "compiler_and_build": build_configuration_identity(root),
        "dependencies": dependency_identity(
            {
                "PathMap": root.parent / "PathMap",
                "MORK": root.parent / "MORK",
                "Mettapedia": mettapedia_root,
            }
        ),
        "property_schema": manifest["schema"],
        "property_manifest_sha256": model_sha256_file(root / PROPERTY_MANIFEST),
        "source_anchors": anchors,
    }


def source_identity(artifact: dict[str, Any]) -> dict[str, Any]:
    """The inputs that must remain byte-stable while a gate is running."""
    compiler_and_build = artifact["compiler_and_build"]
    return {
        "schema": artifact["schema"],
        "tier": artifact["tier"],
        "candidate": artifact["candidate"],
        "inputs": artifact["inputs"],
        "toolchain": {
            key: value
            for key, value in compiler_and_build.items()
            if key != "generated"
        },
        "dependencies": artifact["dependencies"],
        "property_schema": artifact["property_schema"],
        "property_manifest_sha256": artifact["property_manifest_sha256"],
        "source_anchors": artifact["source_anchors"],
    }


def ledger_record_path(root: Path, tier: str, key: str) -> Path:
    return root / "runtime/main-readiness/ledger" / tier / f"{key}.json"


def passing_reuse_record(
    path: Path, *, tier: str, key: str
) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if (
        record.get("schema") != EVIDENCE_SCHEMA
        or record.get("status") != "passed"
        or record.get("tier") != tier
        or record.get("key") != key
    ):
        return None
    evidence = Path(record.get("evidence", ""))
    summary = evidence / "summary.json"
    if not evidence.is_dir() or not summary.is_file():
        return None
    try:
        prior_summary = json.loads(summary.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if (
        prior_summary.get("status") != "passed"
        or prior_summary.get("tier") != tier
        or prior_summary.get("evidence_key_final") != key
        or record.get("summary_sha256") != model_sha256_file(summary)
    ):
        return None
    final_artifact = prior_summary.get("artifact_identity_final")
    if not isinstance(final_artifact, dict) or evidence_key(final_artifact) != key:
        return None
    return record


def candidate_identity(root: Path) -> dict[str, Any]:
    untracked = capture(
        ["git", "ls-files", "--others", "--exclude-standard"], root
    ).splitlines()
    untracked_hashes = {
        name: sha256_file(root / name)
        for name in sorted(untracked)
        if (root / name).is_file()
    }
    diff = subprocess.run(
        ["git", "diff", "--binary", "HEAD"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    ).stdout
    return {
        "commit": capture(["git", "rev-parse", "HEAD"], root),
        "branch": capture(["git", "branch", "--show-current"], root),
        "status": capture(["git", "status", "--short"], root).splitlines(),
        "tracked_diff_sha256": hashlib.sha256(diff).hexdigest(),
        "untracked_sha256": untracked_hashes,
    }


def binary_identity(root: Path) -> dict[str, str | None]:
    return {
        "cetta_sha256": sha256_file(root / "cetta"),
        "main_stats_sha256": sha256_file(
            root / "runtime/cetta-main-runtime-stats"
        ),
    }


def worktree_paths(root: Path) -> list[Path]:
    output = capture(["git", "worktree", "list", "--porcelain"], root)
    return [
        Path(line.removeprefix("worktree ")).resolve()
        for line in output.splitlines()
        if line.startswith("worktree ")
    ]


def baseline_source_is_usable(root: Path) -> bool:
    status = capture(
        ["git", "status", "--porcelain", "--untracked-files=all"], root
    )
    for line in status.splitlines():
        path = line[3:].split(" -> ")[-1]
        if not path.lower().endswith(".md"):
            return False
    return True


def prepare_performance_baseline(
    candidate_root: Path, evidence: Path
) -> dict[str, str]:
    explicit = os.environ.get("CETTA_PERF_BASELINE_ROOT", "")
    candidates = [Path(explicit).expanduser().resolve()] if explicit else [
        path for path in worktree_paths(candidate_root)
        if path != candidate_root
    ]
    baseline_root: Path | None = None
    for path in candidates:
        if not path.is_dir():
            continue
        if capture(["git", "rev-parse", "HEAD"], path) != PERFORMANCE_BASELINE_COMMIT:
            continue
        if not baseline_source_is_usable(path):
            continue
        baseline_root = path
        break
    if baseline_root is None:
        raise RuntimeError(
            "no source-clean worktree at the pinned performance baseline; "
            "set CETTA_PERF_BASELINE_ROOT to one"
        )

    log_path = evidence / "00-performance-baseline-build.log"
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            ["make", "BUILD=main", "ENABLE_RUNTIME_STATS=0", "cetta"],
            cwd=baseline_root,
            text=True,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode != 0:
        raise RuntimeError("the pinned performance baseline failed to build")
    binary = baseline_root / "cetta"
    stats_log_path = evidence / "00-performance-baseline-stats-build.log"
    stats_binary = baseline_root / "runtime/cetta-main-runtime-stats"
    with stats_log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(
            [
                "make",
                "BUILD=main",
                "ENABLE_RUNTIME_STATS=1",
                "runtime/cetta-main-runtime-stats",
            ],
            cwd=baseline_root,
            text=True,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=False,
        )
    if result.returncode != 0:
        raise RuntimeError(
            "the pinned performance baseline stats binary failed to build"
        )
    binary_hash = sha256_file(binary)
    stats_binary_hash = sha256_file(stats_binary)
    if binary_hash is None or stats_binary_hash is None:
        raise RuntimeError("the pinned performance baseline binary is missing")
    print(
        "MAIN-READINESS PERFORMANCE BASELINE "
        f"commit={PERFORMANCE_BASELINE_COMMIT[:12]} "
        f"sha256={binary_hash} stats_sha256={stats_binary_hash}",
        flush=True,
    )
    return {
        "commit": PERFORMANCE_BASELINE_COMMIT,
        "binary": str(binary),
        "binary_sha256": binary_hash,
        "stats_binary": str(stats_binary),
        "stats_binary_sha256": stats_binary_hash,
        "build_log": log_path.name,
        "stats_build_log": stats_log_path.name,
    }


def run_logged(
    index: int,
    command: list[str],
    root: Path,
    evidence: Path,
    environment: dict[str, str],
) -> dict[str, Any]:
    label = f"{index:02d}-{command[-1].replace('/', '_')}"
    log_path = evidence / f"{label}.log"
    started = dt.datetime.now(dt.timezone.utc)
    start_clock = time.monotonic()
    print(f"MAIN-READINESS START {index}: {' '.join(command)}", flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            cwd=root,
            env=environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
            start_new_session=True,
        )
        try:
            assert process.stdout is not None
            for line in process.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                log.write(line)
                log.flush()
            status = process.wait()
            terminate_process_session(process)
        except BaseException:
            terminate_process_session(process)
            raise
    finished = dt.datetime.now(dt.timezone.utc)
    elapsed = time.monotonic() - start_clock
    result = {
        "index": index,
        "command": command,
        "started_utc": started.isoformat(),
        "finished_utc": finished.isoformat(),
        "elapsed_seconds": elapsed,
        "exit_status": status,
        "log": log_path.name,
        "log_sha256": model_sha256_file(log_path),
        "binaries": binary_identity(root),
    }
    print(
        f"MAIN-READINESS {'PASS' if status == 0 else 'FAIL'} {index} "
        f"status={status} elapsed_s={elapsed:.3f}",
        flush=True,
    )
    return result


def common_readiness_commands() -> list[list[str]]:
    return [
        ["make", "BUILD=main", "test-correctness-all"],
        ["make", "BUILD=main", "test-asan"],
        ["make", "BUILD=main", "test-tsan"],
        ["make", "BUILD=main", "test-rhocalc-cost-commit-audit"],
        ["make", "BUILD=main", "test-rhocalc-cost-commit-audit-asan"],
        ["make", "BUILD=main", "test-rhocalc-cost-commit-audit-tsan"],
        ["make", "BUILD=main", "test-rhocalc-cost-differential-required"],
    ]


def readiness_commands(tier: str) -> list[list[str]]:
    common = common_readiness_commands()
    if tier == "routine":
        return common + [
            ["make", "BUILD=main", "test-main-readiness-model"],
            ["make", "BUILD=main", "main-readiness-mutation-qualification"],
            ["make", "BUILD=main", "main-readiness-space-ladders"],
            ["make", "BUILD=main", "main-readiness-thresholds"],
            ["make", "BUILD=main", "bench-d3-nodup-backends"],
            [
                "make",
                "BUILD=main",
                "D4_PROBE_TIMEOUT=10",
                "bench-d4-backends",
            ],
            ["make", "BUILD=main", "RHO_BENCH_RUNS=1", "bench-light"],
            ["make", "BUILD=main", "main-readiness-rho-adaptive"],
        ]
    if tier == "exhaustive":
        return common + [
            ["make", "BUILD=main", "bench-light"],
            [
                "make",
                "BUILD=main",
                "BENCH_ALLOW_HEAVY=1",
                "RHO_BENCH_ENFORCE_BASELINE=1",
                "bench-heavy",
            ],
        ]
    raise RuntimeError(f"unsupported readiness tier: {tier}")


def reusable_common_results(
    root: Path,
    evidence: Path,
    current_artifact: dict[str, Any],
    current_baseline: dict[str, str],
    prior_evidence: Path,
    expected_summary_sha256: str,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    prior = prior_evidence.expanduser().resolve()
    if prior.is_file() and prior.name == "summary.json":
        prior_summary_path = prior
        prior = prior.parent
    else:
        prior_summary_path = prior / "summary.json"
    actual_summary_sha256 = model_sha256_file(prior_summary_path)
    if actual_summary_sha256 != expected_summary_sha256:
        raise RuntimeError("common-prefix summary failed its caller digest check")
    try:
        prior.relative_to(root.resolve())
    except ValueError as error:
        raise RuntimeError("common-prefix evidence is outside this repository") from error
    try:
        summary = json.loads(prior_summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read common-prefix evidence: {error}") from error
    if (
        summary.get("schema") != EVIDENCE_SCHEMA
        or summary.get("tier") != "routine"
        or summary.get("status") != "passed"
    ):
        raise RuntimeError("common-prefix evidence is not a passing routine gate")
    prior_artifact = summary.get("artifact_identity_final")
    if not isinstance(prior_artifact, dict):
        raise RuntimeError("routine evidence has no final artifact identity")
    if calibration_subject(prior_artifact) != calibration_subject(current_artifact):
        raise RuntimeError("routine and exhaustive calibration subjects differ")
    expected_baseline = {
        key: value
        for key, value in current_baseline.items()
        if key not in ("binary", "stats_binary")
    }
    if summary.get("performance_baseline") != expected_baseline:
        raise RuntimeError("routine and exhaustive performance baselines differ")

    expected_commands = common_readiness_commands()
    prior_results = summary.get("commands")
    if not isinstance(prior_results, list) or len(prior_results) < len(
        expected_commands
    ):
        raise RuntimeError("routine evidence has an incomplete common prefix")
    summary_hash = actual_summary_sha256
    reused: list[dict[str, Any]] = []
    for index, (expected, prior_result) in enumerate(
        zip(expected_commands, prior_results), 1
    ):
        if (
            not isinstance(prior_result, dict)
            or prior_result.get("command") != expected
            or prior_result.get("exit_status") != 0
        ):
            raise RuntimeError(
                f"routine common-prefix result {index} is not reusable"
            )
        prior_log_name = prior_result.get("log")
        if not isinstance(prior_log_name, str):
            raise RuntimeError(f"routine common-prefix result {index} has no log")
        prior_log = prior / prior_log_name
        if not prior_log.is_file():
            raise RuntimeError(f"routine common-prefix log {index} is missing")
        prior_log_hash = model_sha256_file(prior_log)
        if prior_result.get("log_sha256") != prior_log_hash:
            raise RuntimeError(
                f"routine common-prefix log {index} failed its digest check"
            )
        copied_log = evidence / f"{index:02d}-reused-{Path(prior_log_name).name}"
        shutil.copy2(prior_log, copied_log)
        result = dict(prior_result)
        result.update(
            {
                "index": index,
                "log": copied_log.name,
                "reused": True,
                "reused_from_summary_sha256": summary_hash,
                "reused_log_sha256": prior_log_hash,
            }
        )
        reused.append(result)
    metadata = {
        "source": prior.relative_to(root.resolve()).as_posix(),
        "summary_sha256": summary_hash,
        "commands": len(reused),
    }
    return reused, metadata


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    mettapedia = os.environ.get("METTAPEDIA_ROOT", "")
    if not mettapedia:
        print(
            "FAIL: set METTAPEDIA_ROOT to the Lean project root; required "
            "C-to-Lean gates may not be skipped",
            file=sys.stderr,
        )
        return 2
    mettapedia_root = Path(mettapedia).expanduser().resolve()
    if not mettapedia_root.is_dir() or not any(
        (mettapedia_root / name).is_file()
        for name in ("lakefile.toml", "lakefile.lean")
    ):
        print(
            "FAIL: METTAPEDIA_ROOT is not a Lean project root",
            file=sys.stderr,
        )
        return 2
    if args.tier == "exhaustive" and os.environ.get("BENCH_ALLOW_HEAVY") != "1":
        print(
            "FAIL: set BENCH_ALLOW_HEAVY=1 to authorize the complete heavy matrix",
            file=sys.stderr,
        )
        return 2
    if args.reuse_common_from is not None and args.tier != "exhaustive":
        print(
            "FAIL: --reuse-common-from is valid only for the exhaustive tier",
            file=sys.stderr,
        )
        return 2
    if (args.reuse_common_from is None) != (
        args.reuse_common_summary_sha256 is None
    ):
        print(
            "FAIL: common-prefix path and summary digest must be supplied together",
            file=sys.stderr,
        )
        return 2

    try:
        manifest = load_property_manifest(root / PROPERTY_MANIFEST)
        anchors = resolve_source_anchors(root, manifest)
        initial_artifact = artifact_identity(
            root, mettapedia_root, args.tier, manifest, anchors
        )
    except (RuntimeError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2
    initial_key = evidence_key(initial_artifact)
    reuse_path = ledger_record_path(root, args.tier, initial_key)
    if args.reuse:
        reused = passing_reuse_record(
            reuse_path, tier=args.tier, key=initial_key
        )
        if reused is not None:
            print(
                "MAIN-READINESS REUSE "
                f"tier={args.tier} key={initial_key} evidence={reused['evidence']}",
                flush=True,
            )
            return 0

    identity = candidate_identity(root)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    evidence = root / "runtime" / "main-readiness" / (
        f"{stamp}-{identity['commit'][:12]}-{args.tier}"
    )
    evidence.mkdir(parents=True, exist_ok=False)

    environment = os.environ.copy()
    environment["METTAPEDIA_ROOT"] = str(mettapedia_root)
    if args.tier == "exhaustive":
        environment["BENCH_ALLOW_HEAVY"] = "1"
    commands = readiness_commands(args.tier)
    if args.dry_run:
        summary = {
            "schema": EVIDENCE_SCHEMA,
            "tier": args.tier,
            "evidence_key_initial": initial_key,
            "artifact_identity_initial": initial_artifact,
            "candidate": identity,
            "commands": commands,
            "status": "dry-run",
        }
        summary_path = evidence / "summary.json"
        summary_path.write_text(json.dumps(summary, indent=2) + "\n")
        for index, command in enumerate(commands, 1):
            print(f"MAIN-READINESS DRY-RUN {index}: {' '.join(command)}")
        print(
            f"MAIN-READINESS DRY-RUN tier={args.tier} "
            f"key={initial_key} evidence={evidence}",
            flush=True,
        )
        return 0
    try:
        performance_baseline = prepare_performance_baseline(root, evidence)
    except RuntimeError as exc:
        summary = {
            "schema": EVIDENCE_SCHEMA,
            "tier": args.tier,
            "evidence_key_initial": initial_key,
            "artifact_identity_initial": initial_artifact,
            "candidate": identity,
            "commands": commands,
            "status": "failed",
            "failure": str(exc),
        }
        (evidence / "summary.json").write_text(
            json.dumps(summary, indent=2) + "\n", encoding="utf-8"
        )
        print(f"FAIL: {exc}", file=sys.stderr)
        print(f"EVIDENCE {evidence}", flush=True)
        return 2
    environment["RHO_BENCH_BASELINE_BIN"] = performance_baseline["binary"]
    environment["CETTA_READINESS_BASELINE_STATS_BIN"] = performance_baseline[
        "stats_binary"
    ]
    summary: dict[str, Any] = {
        "schema": EVIDENCE_SCHEMA,
        "tier": args.tier,
        "evidence_key_initial": initial_key,
        "artifact_identity_initial": initial_artifact,
        "candidate": identity,
        "host": {
            "platform": platform.platform(),
            "python": sys.version,
            "compiler": capture(["gcc", "--version"], root).splitlines()[0],
            "make": capture(["make", "--version"], root).splitlines()[0],
        },
        "mettapedia_commit": capture(
            ["git", "-C", str(mettapedia_root), "rev-parse", "HEAD"], root
        ),
        "performance_baseline": {
            key: value
            for key, value in performance_baseline.items()
            if key not in ("binary", "stats_binary")
        },
        "commands": [],
        "status": "running",
    }
    reused_results: list[dict[str, Any]] = []
    if args.reuse_common_from is not None:
        try:
            reused_results, reuse_metadata = reusable_common_results(
                root,
                evidence,
                initial_artifact,
                performance_baseline,
                args.reuse_common_from,
                args.reuse_common_summary_sha256,
            )
        except (RuntimeError, ValueError) as error:
            summary["status"] = "failed"
            summary["failure"] = str(error)
            (evidence / "summary.json").write_text(
                json.dumps(summary, indent=2) + "\n", encoding="utf-8"
            )
            print(f"FAIL: {error}", file=sys.stderr)
            print(f"EVIDENCE {evidence}", flush=True)
            return 2
        summary["common_prefix_reuse"] = reuse_metadata
        print(
            "MAIN-READINESS REUSE-COMMON "
            f"commands={len(reused_results)} "
            f"summary_sha256={reuse_metadata['summary_sha256']}",
            flush=True,
        )
    summary_path = evidence / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")

    for index, command in enumerate(commands, 1):
        if index <= len(reused_results):
            result = reused_results[index - 1]
            print(
                f"MAIN-READINESS REUSED {index}: {' '.join(command)}",
                flush=True,
            )
        else:
            result = run_logged(
                index, command, root, evidence, environment
            )
        summary["commands"].append(result)
        summary["status"] = "running" if result["exit_status"] == 0 else "failed"
        summary_path.write_text(json.dumps(summary, indent=2) + "\n")
        if result["exit_status"] != 0:
            print(f"EVIDENCE {evidence}", flush=True)
            return result["exit_status"] or 1

    for name in (
        "rhocalc_threaded_standard_current.csv",
        "rhocalc_cost_threaded_heavy_current.csv",
    ):
        source = root / "runtime" / name
        if source.is_file():
            shutil.copy2(source, evidence / name)
    final_artifact = artifact_identity(
        root, mettapedia_root, args.tier, manifest, anchors
    )
    if source_identity(final_artifact) != source_identity(initial_artifact):
        summary["status"] = "failed"
        summary["failure"] = (
            "candidate, dependency, or suite source changed while readiness ran"
        )
        summary["artifact_identity_final"] = final_artifact
        summary_path.write_text(json.dumps(summary, indent=2) + "\n")
        print(
            "MAIN-READINESS FAIL candidate, dependency, or suite source changed "
            "while readiness ran",
            file=sys.stderr,
        )
        print(f"EVIDENCE {evidence}", flush=True)
        return 1
    final_key = evidence_key(final_artifact)
    summary["status"] = "passed"
    summary["final_candidate"] = candidate_identity(root)
    summary["final_binaries"] = binary_identity(root)
    summary["artifact_identity_final"] = final_artifact
    summary["evidence_key_final"] = final_key
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")
    record = {
        "schema": EVIDENCE_SCHEMA,
        "status": "passed",
        "tier": args.tier,
        "key": final_key,
        "evidence": str(evidence),
        "summary_sha256": model_sha256_file(summary_path),
    }
    final_ledger_path = ledger_record_path(root, args.tier, final_key)
    final_ledger_path.parent.mkdir(parents=True, exist_ok=True)
    final_ledger_path.write_text(json.dumps(record, indent=2) + "\n")
    print(
        f"MAIN-READINESS PASS tier={args.tier} key={final_key} "
        f"evidence={evidence}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
