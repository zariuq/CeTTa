#!/usr/bin/env python3
"""Qualify routine readiness against the unchanged exhaustive boundary."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

from cetta_main_readiness import (
    PROPERTY_MANIFEST,
    artifact_identity,
    common_readiness_commands,
)
from cetta_readiness_model import (
    ReadinessModelError,
    calibration_subject,
    evidence_key,
    load_property_manifest,
    qualification_verdict,
    resolve_source_anchors,
    sha256_file,
    terminate_process_session,
)


CALIBRATION_SCHEMA = "cetta.main-readiness.calibration.v2"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run paired routine/exhaustive main-readiness gates until the "
            "routine gate is qualified for the exact current source."
        )
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--status",
        action="store_true",
        help="show qualification status without running either gate",
    )
    mode.add_argument(
        "--dry-run",
        action="store_true",
        help="validate the current subject and show the paired commands",
    )
    mode.add_argument(
        "--require-qualified",
        action="store_true",
        help="fail unless the exact current subject has qualified",
    )
    parser.add_argument(
        "--allow-exhaustive",
        action="store_true",
        help="explicitly authorize the exhaustive heavy boundary",
    )
    parser.add_argument(
        "--max-new-pairs",
        type=int,
        help="stop after this many new pairs even if qualification is incomplete",
    )
    return parser.parse_args()


def mettapedia_root_from_environment() -> Path:
    configured = os.environ.get("METTAPEDIA_ROOT", "")
    if not configured:
        raise ReadinessModelError(
            "set METTAPEDIA_ROOT to the Lean project root"
        )
    root = Path(configured).expanduser().resolve()
    if not root.is_dir() or not any(
        (root / name).is_file() for name in ("lakefile.toml", "lakefile.lean")
    ):
        raise ReadinessModelError("METTAPEDIA_ROOT is not a Lean project root")
    return root


def current_subject(
    root: Path, mettapedia_root: Path
) -> tuple[dict[str, Any], str, int]:
    manifest = load_property_manifest(root / PROPERTY_MANIFEST)
    anchors = resolve_source_anchors(root, manifest)
    artifact = artifact_identity(
        root, mettapedia_root, "routine", manifest, anchors
    )
    subject = calibration_subject(artifact)
    return subject, evidence_key(subject), manifest["qualification_runs_required"]


def calibration_ledger_path(root: Path, subject_key: str) -> Path:
    return (
        root
        / "runtime/main-readiness/calibration"
        / f"{subject_key}.json"
    )


def load_ledger(
    path: Path,
    *,
    subject: dict[str, Any],
    subject_key: str,
    required_runs: int,
) -> dict[str, Any]:
    if not path.is_file():
        return {
            "schema": CALIBRATION_SCHEMA,
            "subject_key": subject_key,
            "subject": subject,
            "qualification_runs_required": required_runs,
            "pairs": [],
        }
    try:
        ledger = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReadinessModelError(f"invalid calibration ledger: {error}") from error
    if ledger.get("schema") != CALIBRATION_SCHEMA:
        raise ReadinessModelError("calibration ledger schema mismatch")
    if ledger.get("subject_key") != subject_key or ledger.get("subject") != subject:
        raise ReadinessModelError("calibration ledger subject mismatch")
    if ledger.get("qualification_runs_required") != required_runs:
        raise ReadinessModelError("calibration ledger run requirement mismatch")
    if not isinstance(ledger.get("pairs"), list):
        raise ReadinessModelError("calibration ledger has no pair list")
    return ledger


def write_ledger(
    path: Path,
    ledger: dict[str, Any],
    verdict: dict[str, Any],
) -> None:
    ledger["verdict"] = verdict
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(ledger, indent=2) + "\n", encoding="utf-8")


def evidence_path_from_line(line: str) -> str | None:
    stripped = line.strip()
    if stripped.startswith("EVIDENCE "):
        return stripped.removeprefix("EVIDENCE ").strip()
    marker = " evidence="
    if marker in stripped and stripped.startswith("MAIN-READINESS "):
        return stripped.rsplit(marker, 1)[1].strip()
    return None


def relative_to_root(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ReadinessModelError(
            "readiness evidence escaped the CeTTa repository"
        ) from error


def run_gate(
    root: Path,
    tier: str,
    environment: dict[str, str],
    log_path: Path,
    reuse_common_from: str | None = None,
    reuse_common_summary_sha256: str | None = None,
) -> dict[str, Any]:
    command = [
        sys.executable,
        "-u",
        "scripts/cetta_main_readiness.py",
        "--tier",
        tier,
    ]
    if reuse_common_from is not None:
        command.extend(["--reuse-common-from", reuse_common_from])
        if reuse_common_summary_sha256 is None:
            raise ReadinessModelError(
                "common-prefix reuse requires the routine summary digest"
            )
        command.extend(
            [
                "--reuse-common-summary-sha256",
                reuse_common_summary_sha256,
            ]
        )
    gate_environment = dict(environment)
    if tier == "exhaustive":
        gate_environment["BENCH_ALLOW_HEAVY"] = "1"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    evidence_name: str | None = None
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            cwd=root,
            env=gate_environment,
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
                parsed = evidence_path_from_line(line)
                if parsed is not None:
                    evidence_name = parsed
            exit_status = process.wait()
            terminate_process_session(process)
        except BaseException:
            terminate_process_session(process)
            raise
    if evidence_name is None:
        raise ReadinessModelError(
            f"{tier} gate returned {exit_status} without an evidence path"
        )
    evidence_path = Path(evidence_name)
    if not evidence_path.is_absolute():
        evidence_path = root / evidence_path
    summary_path = evidence_path / "summary.json"
    if not summary_path.is_file():
        raise ReadinessModelError(f"{tier} gate did not produce summary.json")
    try:
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReadinessModelError(
            f"{tier} gate produced an invalid summary: {error}"
        ) from error
    if summary.get("tier") != tier:
        raise ReadinessModelError(f"{tier} gate summary has the wrong tier")
    expected_status = "passed" if exit_status == 0 else "failed"
    if summary.get("status") != expected_status:
        raise ReadinessModelError(
            f"{tier} gate exit/status mismatch: {exit_status}/"
            f"{summary.get('status')!r}"
        )
    artifact = summary.get("artifact_identity_final")
    if not isinstance(artifact, dict):
        artifact = summary.get("artifact_identity_initial")
    if not isinstance(artifact, dict):
        raise ReadinessModelError(f"{tier} gate summary has no artifact identity")
    subject = calibration_subject(artifact)
    mutation_passed = any(
        isinstance(result, dict)
        and result.get("exit_status") == 0
        and "main-readiness-mutation-qualification"
        in result.get("command", [])
        for result in summary.get("commands", [])
    )
    reused_common_commands = sum(
        1
        for result in summary.get("commands", [])
        if isinstance(result, dict) and result.get("reused") is True
    )
    common_prefix_reuse = summary.get("common_prefix_reuse")
    common_prefix_source_sha256 = (
        common_prefix_reuse.get("summary_sha256")
        if isinstance(common_prefix_reuse, dict)
        else None
    )
    return {
        "tier": tier,
        "exit_status": exit_status,
        "status": summary["status"],
        "subject": subject,
        "subject_key": evidence_key(subject),
        "evidence": relative_to_root(evidence_path, root),
        "summary_sha256": sha256_file(summary_path),
        "performance_baseline": summary.get("performance_baseline"),
        "mutation_passed": mutation_passed,
        "reused_common_commands": reused_common_commands,
        "common_prefix_source_sha256": common_prefix_source_sha256,
        "log": relative_to_root(log_path, root),
    }


def pair_record(
    pair_id: str,
    subject_key: str,
    routine: dict[str, Any],
    exhaustive: dict[str, Any],
    started_utc: str,
) -> dict[str, Any]:
    baseline_match = (
        isinstance(routine.get("performance_baseline"), dict)
        and routine["performance_baseline"]
        == exhaustive.get("performance_baseline")
    )
    common_prefix_match = (
        exhaustive.get("reused_common_commands")
        == len(common_readiness_commands())
        and exhaustive.get("common_prefix_source_sha256")
        == routine.get("summary_sha256")
    )
    return {
        "pair_id": pair_id,
        "started_utc": started_utc,
        "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "subject_key": subject_key,
        "routine_status": routine["status"],
        "exhaustive_status": exhaustive["status"],
        "routine_mutants_killed": routine["mutation_passed"],
        "baseline_match": baseline_match,
        "common_prefix_match": common_prefix_match,
        "routine": {
            key: routine[key]
            for key in ("exit_status", "evidence", "summary_sha256", "log")
        },
        "exhaustive": {
            key: exhaustive[key]
            for key in (
                "exit_status",
                "evidence",
                "summary_sha256",
                "reused_common_commands",
                "log",
            )
        },
    }


def show_verdict(subject_key: str, verdict: dict[str, Any]) -> None:
    print(
        "MAIN-READINESS CALIBRATION "
        f"status={verdict['status']} "
        f"passed={verdict['passed_pairs']}/{verdict['required_runs']} "
        f"subject={subject_key}"
    )
    for failure in verdict["failures"]:
        print(f"MAIN-READINESS CALIBRATION BLOCKER {failure}")


def main() -> int:
    args = parse_args()
    if args.max_new_pairs is not None and args.max_new_pairs <= 0:
        print("FAIL: --max-new-pairs must be positive", file=sys.stderr)
        return 2
    if not (
        args.status
        or args.dry_run
        or args.require_qualified
        or args.allow_exhaustive
    ):
        print(
            "FAIL: pass --allow-exhaustive to authorize paired heavy runs",
            file=sys.stderr,
        )
        return 2
    root = Path(__file__).resolve().parent.parent
    try:
        mettapedia_root = mettapedia_root_from_environment()
        subject, subject_key, required_runs = current_subject(
            root, mettapedia_root
        )
        ledger_path = calibration_ledger_path(root, subject_key)
        ledger = load_ledger(
            ledger_path,
            subject=subject,
            subject_key=subject_key,
            required_runs=required_runs,
        )
        verdict = qualification_verdict(
            ledger["pairs"],
            required_runs=required_runs,
            subject_key=subject_key,
        )
    except (ReadinessModelError, RuntimeError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2

    show_verdict(subject_key, verdict)
    if args.status:
        return 0 if not verdict["failures"] else 1
    if args.require_qualified:
        return 0 if verdict["qualified"] else 1
    if args.dry_run:
        print(
            "MAIN-READINESS CALIBRATION DRY-RUN "
            f"{sys.executable} scripts/cetta_main_readiness.py --tier routine"
        )
        print(
            "MAIN-READINESS CALIBRATION DRY-RUN "
            f"{sys.executable} scripts/cetta_main_readiness.py --tier exhaustive"
        )
        return 0
    if verdict["qualified"]:
        return 0
    if verdict["failures"]:
        print(
            "FAIL: the exact calibration subject is blocked; inspect the "
            "recorded disagreement before rerunning",
            file=sys.stderr,
        )
        return 1

    environment = os.environ.copy()
    environment["METTAPEDIA_ROOT"] = str(mettapedia_root)
    new_pairs = 0
    while not verdict["qualified"]:
        if (
            args.max_new_pairs is not None
            and new_pairs >= args.max_new_pairs
        ):
            break
        started = dt.datetime.now(dt.timezone.utc)
        pair_id = started.strftime("%Y%m%dT%H%M%S.%fZ")
        logs = root / "runtime/main-readiness/calibration-logs" / pair_id
        try:
            routine = run_gate(
                root, "routine", environment, logs / "routine.log"
            )
            subject_after_routine, key_after_routine, _ = current_subject(
                root, mettapedia_root
            )
            if key_after_routine != subject_key or subject_after_routine != subject:
                raise ReadinessModelError(
                    "source or dependency changed between calibration tiers"
                )
            exhaustive = run_gate(
                root,
                "exhaustive",
                environment,
                logs / "exhaustive.log",
                routine["evidence"],
                routine["summary_sha256"],
            )
            subject_after_pair, key_after_pair, _ = current_subject(
                root, mettapedia_root
            )
            if key_after_pair != subject_key or subject_after_pair != subject:
                raise ReadinessModelError(
                    "source or dependency changed during the calibration pair"
                )
            if (
                routine["subject_key"] != subject_key
                or exhaustive["subject_key"] != subject_key
            ):
                raise ReadinessModelError(
                    "gate summaries do not describe the calibration subject"
                )
        except (ReadinessModelError, RuntimeError, ValueError) as error:
            print(f"FAIL: {error}", file=sys.stderr)
            return 2
        record = pair_record(
            pair_id,
            subject_key,
            routine,
            exhaustive,
            started.isoformat(),
        )
        ledger["pairs"].append(record)
        verdict = qualification_verdict(
            ledger["pairs"],
            required_runs=required_runs,
            subject_key=subject_key,
        )
        write_ledger(ledger_path, ledger, verdict)
        show_verdict(subject_key, verdict)
        new_pairs += 1
        if verdict["failures"]:
            return 1
    return 0 if verdict["qualified"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
