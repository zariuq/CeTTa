#!/usr/bin/env python3
"""Run and record CeTTa's complete Cost-Rho main-readiness matrix."""

from __future__ import annotations

import datetime as dt
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


PERFORMANCE_BASELINE_COMMIT = "9050a2d171946f6edf417152f259b7bc4e98907d"


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
    binary_hash = sha256_file(binary)
    if binary_hash is None:
        raise RuntimeError("the pinned performance baseline binary is missing")
    print(
        "MAIN-READINESS PERFORMANCE BASELINE "
        f"commit={PERFORMANCE_BASELINE_COMMIT[:12]} sha256={binary_hash}",
        flush=True,
    )
    return {
        "commit": PERFORMANCE_BASELINE_COMMIT,
        "binary": str(binary),
        "binary_sha256": binary_hash,
        "build_log": log_path.name,
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
        )
        assert process.stdout is not None
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
            log.flush()
        status = process.wait()
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
        "binaries": binary_identity(root),
    }
    print(
        f"MAIN-READINESS {'PASS' if status == 0 else 'FAIL'} {index} "
        f"status={status} elapsed_s={elapsed:.3f}",
        flush=True,
    )
    return result


def main() -> int:
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
    if os.environ.get("BENCH_ALLOW_HEAVY") != "1":
        print(
            "FAIL: set BENCH_ALLOW_HEAVY=1 to authorize the complete heavy matrix",
            file=sys.stderr,
        )
        return 2

    identity = candidate_identity(root)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    evidence = root / "runtime" / "main-readiness" / (
        f"{stamp}-{identity['commit'][:12]}"
    )
    evidence.mkdir(parents=True, exist_ok=False)

    environment = os.environ.copy()
    environment["METTAPEDIA_ROOT"] = str(mettapedia_root)
    environment["BENCH_ALLOW_HEAVY"] = "1"
    try:
        performance_baseline = prepare_performance_baseline(root, evidence)
    except RuntimeError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 2
    environment["RHO_BENCH_BASELINE_BIN"] = performance_baseline["binary"]
    commands = [
        ["make", "BUILD=main", "test-correctness-all"],
        ["make", "BUILD=main", "test-asan"],
        ["make", "BUILD=main", "test-tsan"],
        ["make", "BUILD=main", "test-rhocalc-cost-commit-audit"],
        ["make", "BUILD=main", "test-rhocalc-cost-commit-audit-asan"],
        ["make", "BUILD=main", "test-rhocalc-cost-commit-audit-tsan"],
        ["make", "BUILD=main", "test-rhocalc-cost-differential-required"],
        ["make", "BUILD=main", "bench-light"],
        [
            "make",
            "BUILD=main",
            "BENCH_ALLOW_HEAVY=1",
            "RHO_BENCH_ENFORCE_BASELINE=1",
            "bench-heavy",
        ],
    ]

    summary: dict[str, Any] = {
        "schema": "cetta.main-readiness.cost-rho.v1",
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
            if key != "binary"
        },
        "commands": [],
        "status": "running",
    }
    summary_path = evidence / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")

    for index, command in enumerate(commands, 1):
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
    summary["status"] = "passed"
    summary["final_candidate"] = candidate_identity(root)
    summary["final_binaries"] = binary_identity(root)
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")
    print(f"MAIN-READINESS PASS evidence={evidence}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
