#!/usr/bin/env python3
"""Measure the revision-pinned RuleMachineProgram path without changing its semantics.

The benchmark separates four public operations:

* compiling the authored BFC package to RuleMachineProgram, both as one cold
  demanded operation and as a repeated cache-eligible public call;
* validating and executing an already materialized RuleMachineProgram program;
* executing the same program through the native structural backend; and
* publishing one new axiom block onto the persistent program chain.

Every timed run is answer-checked.  Repeated-process timings include the public
CeTTa API's validation cost and expose cache-mixed throughput; semantic-clock
timings demand one operation and exclude process startup and source loading.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import statistics
import subprocess
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATED = ROOT / "tests/prime/nil_rule_machine_guests.generated.metta"
EXPECTED = ROOT / "tests/prime/nil_rule_machine_guests.generated.expected"


def definitions() -> str:
    text = GENERATED.read_text(encoding="utf-8")
    marker = "\n!(compile:artifact-info"
    if marker not in text:
        raise RuntimeError(f"query marker missing from {GENERATED}")
    return text.split(marker, 1)[0].rstrip() + "\n"


def expected_lines() -> list[str]:
    return [line for line in EXPECTED.read_text(encoding="utf-8").splitlines() if line]


def invoke(cetta: pathlib.Path, source: str) -> tuple[float, list[str]]:
    with tempfile.TemporaryDirectory(prefix="cetta-rule-machine-bench-") as tmp:
        path = pathlib.Path(tmp) / "bench.metta"
        path.write_text(source, encoding="utf-8")
        start = time.perf_counter_ns()
        completed = subprocess.run(
            [str(cetta), "--lang", "prime", str(path)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        elapsed_ns = time.perf_counter_ns() - start
    if completed.returncode != 0:
        raise RuntimeError(
            f"CeTTa exited {completed.returncode}\nstdout:\n{completed.stdout}"
            f"\nstderr:\n{completed.stderr}"
        )
    lines = [line for line in completed.stdout.splitlines() if line]
    return elapsed_ns / 1_000_000_000.0, lines


def materialize(cetta: pathlib.Path) -> str:
    _, lines = invoke(cetta, definitions() + "\n!(nil-bfc-rule-program)\n")
    if len(lines) != 1 or not lines[0].startswith("[(rule-program-v1 "):
        raise RuntimeError(f"unexpected materialized program: {lines!r}")
    return lines[0][1:-1]


def materialize_definition(cetta: pathlib.Path, name: str, result_head: str) -> str:
    _, lines = invoke(cetta, definitions() + f"\n!({name})\n")
    if len(lines) != 1 or not lines[0].startswith(f"[({result_head} "):
        raise RuntimeError(f"unexpected materialized {name}: {lines!r}")
    return lines[0][1:-1]


def benchmark(
    cetta: pathlib.Path,
    name: str,
    prefix: str,
    query: str,
    expected: str,
    operations: int,
    repetitions: int,
) -> dict[str, object]:
    source = prefix + "\n" + "\n".join(f"!{query}" for _ in range(operations)) + "\n"
    elapsed: list[float] = []
    output_hash: str | None = None
    for _ in range(repetitions):
        seconds, lines = invoke(cetta, source)
        if lines != [expected] * operations:
            mismatch = next(
                (i for i, line in enumerate(lines) if i >= operations or line != expected),
                min(len(lines), operations),
            )
            raise RuntimeError(
                f"{name}: answer mismatch at operation {mismatch}; "
                f"got {len(lines)} lines, expected {operations}"
            )
        digest = hashlib.sha256("\n".join(lines).encode()).hexdigest()
        if output_hash is not None and digest != output_hash:
            raise RuntimeError(f"{name}: output identity changed between repetitions")
        output_hash = digest
        elapsed.append(seconds)
    median_total = statistics.median(elapsed)
    return {
        "name": name,
        "operations_per_process": operations,
        "repetitions": repetitions,
        "median_total_ms": median_total * 1000.0,
        "median_us_per_operation": median_total * 1_000_000.0 / operations,
        "min_us_per_operation": min(elapsed) * 1_000_000.0 / operations,
        "max_us_per_operation": max(elapsed) * 1_000_000.0 / operations,
        "answer_sha256": output_hash,
    }


def benchmark_semantic_clock(
    cetta: pathlib.Path,
    name: str,
    prefix: str,
    query: str,
    expected: str,
    repetitions: int,
) -> dict[str, object]:
    """Measure one demanded query with CeTTa's in-language monotonic clock."""
    source = (
        "!(import! &self system)\n"
        + prefix
        + "\n!(system:timed (delay "
        + query
        + "))\n"
    )
    expected_payload = expected[1:-1]
    elapsed_ns: list[int] = []
    output_hash: str | None = None
    timed_line = re.compile(r"^\[\(system:timed-result ([0-9]+) (.*)\)\]$")
    for _ in range(repetitions):
        _, lines = invoke(cetta, source)
        if len(lines) != 2 or lines[0] != "[()]":
            raise RuntimeError(f"{name}: unexpected timed output: {lines!r}")
        matched = timed_line.fullmatch(lines[1])
        if not matched or matched.group(2) != expected_payload:
            raise RuntimeError(
                f"{name}: timed answer mismatch; got {lines[1]!r}, "
                f"expected payload {expected_payload!r}"
            )
        elapsed_ns.append(int(matched.group(1)))
        digest = hashlib.sha256(matched.group(2).encode()).hexdigest()
        if output_hash is not None and digest != output_hash:
            raise RuntimeError(f"{name}: timed answer identity changed")
        output_hash = digest
    return {
        "name": name,
        "repetitions": repetitions,
        "median_ms_per_operation": statistics.median(elapsed_ns) / 1_000_000.0,
        "min_ms_per_operation": min(elapsed_ns) / 1_000_000.0,
        "max_ms_per_operation": max(elapsed_ns) / 1_000_000.0,
        "answer_sha256": output_hash,
        "measurement_scope": (
            "CLOCK_MONOTONIC inside CeTTa; source loading and process startup excluded; "
            "the explicitly delayed query is forced before the end timestamp"
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=pathlib.Path, default=ROOT / "cetta")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--fast-operations", type=int, default=200)
    parser.add_argument("--search-operations", type=int, default=25)
    args = parser.parse_args()
    cetta = args.cetta.resolve()
    if args.repetitions < 1 or args.fast_operations < 1 or args.search_operations < 1:
        parser.error("all repetition and operation counts must be positive")

    expected = expected_lines()
    cold_info = expected[2]
    search_answer = expected[3]
    linked_info = expected[5]
    synthesis_answer = expected[13]
    sumo_answer = expected[17]
    program = materialize(cetta)
    synthesis_artifact = materialize_definition(
        cetta, "nil-synthesis-r1-artifact", "compiled-artifact"
    )
    sumo_artifact = materialize_definition(
        cetta, "nil-sumo-artifact", "compiled-artifact"
    )
    query_target = "(imp (imp (imp p q) r) (imp q r))"
    results = [
        benchmark(
            cetta,
            "repeated-package-to-rule-program-cache-mixed",
            definitions(),
            "(compile:rule-program-info (nil-bfc-rule-program))",
            cold_info,
            args.fast_operations,
            args.repetitions,
        ),
        benchmark(
            cetta,
            "hot-rule-program-bytecode",
            "",
            f"(compile:rule-program-run {program} 13 1000000 10 {query_target})",
            search_answer,
            args.search_operations,
            args.repetitions,
        ),
        benchmark(
            cetta,
            "hot-rule-program-native",
            "",
            f"(compile:rule-program-run-native {program} 13 1000000 10 {query_target})",
            search_answer,
            args.search_operations,
            args.repetitions,
        ),
        benchmark(
            cetta,
            "repeated-incremental-publish-cache-mixed",
            definitions(),
            f"(compile:rule-program-info "
            f"(compile:rule-program-link {program} bfc-r1 (nil-bfc-special-block)))",
            linked_info,
            args.fast_operations,
            args.repetitions,
        ),
    ]
    semantic_clock_results = [
        benchmark_semantic_clock(
            cetta,
            "cold-package-to-rule-program",
            definitions(),
            "(compile:rule-program-info (nil-bfc-rule-program))",
            cold_info,
            args.repetitions,
        ),
        benchmark_semantic_clock(
            cetta,
            "hot-rule-program-bytecode",
            "",
            f"(compile:rule-program-run {program} 13 1000000 10 {query_target})",
            search_answer,
            args.repetitions,
        ),
        benchmark_semantic_clock(
            cetta,
            "hot-rule-program-native",
            "",
            f"(compile:rule-program-run-native {program} 13 1000000 10 {query_target})",
            search_answer,
            args.repetitions,
        ),
        benchmark_semantic_clock(
            cetta,
            "incremental-publish-one-rule",
            definitions(),
            f"(compile:rule-program-info "
            f"(compile:rule-program-link {program} bfc-r1 (nil-bfc-special-block)))",
            linked_info,
            args.repetitions,
        ),
        benchmark_semantic_clock(
            cetta,
            "hot-synthesis-artifact",
            "",
            f"(compile:run {synthesis_artifact} 2 10000 100 "
            f"(arrow String Number Number))",
            synthesis_answer,
            args.repetitions,
        ),
        benchmark_semantic_clock(
            cetta,
            "hot-sumo-artifact-depth4",
            "",
            f"(compile:run {sumo_artifact} 4 1000000 100 "
            f"(objectTransferred JohnsCarry JohnsFlower))",
            sumo_answer,
            args.repetitions,
        ),
    ]
    print(
        json.dumps(
            {
                "summary": "RuleMachineBenchmarkV1",
                "cetta": str(cetta),
                "artifact": "HilbertBFCProgramV1",
                "measurement_scope": (
                    "wall clock through the public CeTTa API; process startup and file parsing "
                    "are amortized across operations; program validation is included"
                ),
                "program_chars": len(program),
                "results": results,
                "semantic_clock_results": semantic_clock_results,
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
