#!/usr/bin/env python3
"""Bounded D4 capability witness with measured forward progress."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import selectors
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent.parent
CHAIN_RE = re.compile(rb"\[chain\]\s+([0-9]+)(k?) results")
COUNT_RE = re.compile(rb"^[0-9]+$")


def chain_result_count(match: re.Match[bytes]) -> int:
    count = int(match.group(1))
    return count * 1000 if match.group(2) == b"k" else count


def comma_list(value: str) -> tuple[str, ...]:
    values = tuple(part.strip() for part in value.split(",") if part.strip())
    if not values:
        raise argparse.ArgumentTypeError("expected a nonempty backend list")
    return values


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run a bounded D4 probe and require completion or multiple monotone "
            "progress checkpoints; a silent timeout is a failure."
        )
    )
    parser.add_argument("binary", type=Path)
    parser.add_argument("input", type=Path)
    parser.add_argument(
        "--backends",
        type=comma_list,
        default=("native", "native-candidate-exact", "pathmap"),
    )
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--min-checkpoints", type=int, default=2)
    parser.add_argument(
        "--max-silence-fraction",
        type=float,
        default=0.75,
        help="maximum fraction of the bounded run after the last checkpoint",
    )
    parser.add_argument(
        "--output", type=Path, default=ROOT / "runtime/d4_progress_current.json"
    )
    parser.add_argument(
        "--mode",
        choices=("witness", "census"),
        default="witness",
        help=(
            "witness requires completion or measured progress; census records "
            "a bounded capability frontier without granting readiness credit"
        ),
    )
    args = parser.parse_args()
    if args.duration <= 0:
        parser.error("--duration must be positive")
    if args.min_checkpoints < 2:
        parser.error("--min-checkpoints must be at least two")
    if not 0 < args.max_silence_fraction < 1:
        parser.error("--max-silence-fraction must lie strictly between zero and one")
    return args


def stop_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def run_backend(
    binary: Path,
    input_path: Path,
    backend: str,
    duration: float,
    min_checkpoints: int,
    max_silence_fraction: float,
) -> dict[str, Any]:
    command = [
        str(binary),
        "--profile",
        "extended",
        "--lang",
        "he",
        "--space-engine",
        backend,
        "--count-only",
        str(input_path),
    ]
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    assert process.stdout is not None
    os.set_blocking(process.stdout.fileno(), False)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    started = time.monotonic()
    deadline = started + duration
    buffer = b""
    lines: list[bytes] = []
    checkpoints: list[dict[str, float | int]] = []
    timed_out = False

    try:
        while True:
            now = time.monotonic()
            if process.poll() is not None:
                remaining = process.stdout.read() or b""
                buffer += remaining
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    lines.append(line)
                    match = CHAIN_RE.search(line)
                    if match:
                        checkpoints.append(
                            {
                                "elapsed_seconds": time.monotonic() - started,
                                "results": chain_result_count(match),
                            }
                        )
                break
            if now >= deadline:
                timed_out = True
                break
            events = selector.select(timeout=min(0.25, deadline - now))
            for key, _ in events:
                chunk = os.read(key.fd, 65536)
                if not chunk:
                    continue
                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    lines.append(line)
                    match = CHAIN_RE.search(line)
                    if match:
                        checkpoints.append(
                            {
                                "elapsed_seconds": time.monotonic() - started,
                                "results": chain_result_count(match),
                            }
                        )
    finally:
        selector.close()
        if timed_out:
            stop_process(process)
        else:
            process.wait()
    if buffer:
        lines.extend(buffer.splitlines())

    elapsed = time.monotonic() - started
    numeric_lines = [
        int(line) for line in lines if COUNT_RE.fullmatch(line.strip())
    ]
    completed_count = numeric_lines[-1] if numeric_lines else None
    monotone = all(
        int(right["results"]) > int(left["results"])
        and float(right["elapsed_seconds"]) > float(left["elapsed_seconds"])
        for left, right in zip(checkpoints, checkpoints[1:])
    )
    if checkpoints:
        last = checkpoints[-1]
        silence = elapsed - float(last["elapsed_seconds"])
    else:
        silence = elapsed
    if len(checkpoints) >= 2:
        first = checkpoints[0]
        progress_slope = (
            (int(last["results"]) - int(first["results"]))
            / (float(last["elapsed_seconds"]) - float(first["elapsed_seconds"]))
        )
    else:
        progress_slope = 0.0

    failures: list[str] = []
    if completed_count is not None and process.returncode == 0:
        status = "completed"
    else:
        status = "bounded-progress" if timed_out else "failed"
        if not timed_out:
            failures.append(f"process exited {process.returncode} without a count")
        if len(checkpoints) < min_checkpoints:
            failures.append(
                f"only {len(checkpoints)} progress checkpoints; "
                f"need {min_checkpoints}"
            )
        if not monotone or progress_slope <= 0:
            failures.append("progress checkpoints are not strictly monotone")
        if silence > duration * max_silence_fraction:
            failures.append(
                f"no recent progress for {silence:.3f}s of a {duration:.3f}s probe"
            )
    return {
        "backend": backend,
        "status": status,
        "passed": not failures,
        "failures": failures,
        "elapsed_seconds": elapsed,
        "exit_status": process.returncode,
        "completed_count": completed_count,
        "checkpoint_count": len(checkpoints),
        "checkpoints": checkpoints,
        "progress_slope_results_per_second": progress_slope,
        "last_checkpoint_silence_seconds": silence,
        "output_tail": [line.decode("utf-8", "replace") for line in lines[-10:]],
    }


def census_classification(result: dict[str, Any]) -> tuple[str, bool]:
    """Classify a non-authoritative capacity observation."""

    if result["passed"]:
        return "progress", True
    if result["status"] == "bounded-progress":
        return "boundary", True
    return "failed", False


def main() -> int:
    args = parse_args()
    binary = args.binary.expanduser().resolve()
    input_path = args.input.expanduser().resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print(f"FAIL: missing executable: {binary}", file=sys.stderr)
        return 2
    if not input_path.is_file():
        print(f"FAIL: missing D4 input: {input_path}", file=sys.stderr)
        return 2

    results: list[dict[str, Any]] = []
    for backend in args.backends:
        print(f"D4-PROGRESS START backend={backend}", flush=True)
        result = run_backend(
            binary,
            input_path,
            backend,
            args.duration,
            args.min_checkpoints,
            args.max_silence_fraction,
        )
        results.append(result)
        if args.mode == "witness":
            print(
                f"D4-PROGRESS {'PASS' if result['passed'] else 'FAIL'} "
                f"backend={backend} status={result['status']} "
                f"checkpoints={result['checkpoint_count']} "
                f"slope={result['progress_slope_results_per_second']:.3f}",
                flush=True,
            )
        else:
            classification, accepted = census_classification(result)
            result["census_classification"] = classification
            print(
                f"D4-CENSUS {classification.upper()} "
                f"backend={backend} status={result['status']} "
                f"checkpoints={result['checkpoint_count']} "
                f"slope={result['progress_slope_results_per_second']:.3f} "
                f"accepted={'yes' if accepted else 'no'}",
                flush=True,
            )

    if args.mode == "witness":
        accepted = all(result["passed"] for result in results)
        status = "passed" if accepted else "failed"
    else:
        accepted = all(census_classification(result)[1] for result in results)
        status = "observed" if accepted else "failed"
    try:
        binary_label = binary.relative_to(ROOT).as_posix()
    except ValueError:
        binary_label = binary.name
    try:
        input_label = input_path.relative_to(ROOT).as_posix()
    except ValueError:
        input_label = input_path.name
    summary = {
        "schema": "cetta.d4-progress.v2",
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "mode": args.mode,
        "authoritative_readiness_evidence": args.mode == "witness",
        "status": status,
        "binary": binary_label,
        "input": input_label,
        "duration_seconds": args.duration,
        "min_checkpoints": args.min_checkpoints,
        "max_silence_fraction": args.max_silence_fraction,
        "results": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    prefix = "D4_PROGRESS" if args.mode == "witness" else "D4_CENSUS"
    print(f"{prefix}_STATUS={summary['status']}")
    print(f"{prefix}_BACKENDS={len(results)}")
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
