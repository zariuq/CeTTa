#!/usr/bin/env python3
"""Check one comparable MatchDecision telemetry contract across all lanes."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
COUNTERS = (
    "compile",
    "run",
    "clause-input",
    "clause-survivor",
    "linear-fallback",
    "unavailable-path",
    "invalidation",
    "exact-attempt",
    "whole-equation-freshen",
    "whole-equation-freshen-bytes",
)


def normalized_output(data: str) -> str:
    return data.rstrip("\n")


def run(binary: Path, lane: str, mode: str,
        arguments: tuple[str, ...], timeout: float) -> tuple[str, dict[str, int]]:
    environment = os.environ.copy()
    environment[f"CETTA_{lane.upper()}_MATCH_DECISION"] = mode
    completed = subprocess.run(
        (str(binary), "--emit-runtime-stats", *arguments),
        cwd=ROOT, env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=timeout, check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{lane}/{mode} exited {completed.returncode}:\n"
            + "\n".join(completed.stderr.splitlines()[-30:])
        )
    prefix = "runtime-counter match-decision-"
    stats: dict[str, int] = {}
    for line in completed.stderr.splitlines():
        if not line.startswith(prefix):
            continue
        name, value = line[len(prefix):].rsplit(" ", 1)
        stats[name] = int(value)
    missing = sorted(set(COUNTERS) - stats.keys())
    if missing:
        raise RuntimeError(
            f"{lane}/{mode} omitted counters: {', '.join(missing)}"
        )
    return normalized_output(completed.stdout), stats


def check_lane(lane: str, linear: dict[str, int],
               deep: dict[str, int]) -> list[str]:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(f"{lane}: {message}")

    require(linear["compile"] > 0, "linear compiled no artifact")
    require(linear["run"] > 0, "linear selected no candidates")
    require(linear["clause-input"] > 0, "linear saw no clauses")
    require(linear["clause-survivor"] == linear["clause-input"],
            "linear oracle pruned a clause")
    require(linear["linear-fallback"] == linear["run"],
            "linear oracle did not report one fallback per run")
    require(linear["unavailable-path"] == 0,
            "linear oracle inspected an unavailable deep path")
    require(linear["invalidation"] == 0 and deep["invalidation"] == 0,
            "stable fixture invalidated an artifact")
    require(deep["clause-input"] == linear["clause-input"],
            "deep and linear executions saw different clause inputs")
    require(deep["clause-survivor"] <= linear["clause-survivor"],
            "deep selection enlarged the linear candidate family")
    require(deep["clause-survivor"] < linear["clause-survivor"],
            "fixture did not witness deep refutation")
    require(deep["exact-attempt"] <= deep["clause-survivor"],
            "exact attempts exceeded conservative survivors")
    require(deep["exact-attempt"] <= linear["exact-attempt"],
            "deep selection increased authoritative matching")
    require(deep["whole-equation-freshen"] <= deep["exact-attempt"],
            "whole-equation freshening preceded candidate admission")
    require(linear["whole-equation-freshen"] <= linear["exact-attempt"],
            "linear whole-equation freshening exceeded exact attempts")
    for mode, stats in (("linear", linear), ("deep", deep)):
        calls = stats["whole-equation-freshen"]
        fresh_bytes = stats["whole-equation-freshen-bytes"]
        require((calls == 0) == (fresh_bytes == 0),
                f"{mode} freshen call/byte counters disagree")
    if lane == "petta":
        require(deep["whole-equation-freshen"] == 0,
                "PeTTa planned activation rebuilt a whole equation")
    else:
        require(deep["whole-equation-freshen"] == 0,
                f"{lane} unexpectedly copied whole equations")
    if lane == "prime":
        for mode, stats in (("linear", linear), ("deep", deep)):
            require(stats["compile"] < stats["run"],
                    f"{mode} rebuilt its revision-pinned decision per run")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60.0)
    arguments = parser.parse_args()
    binary = arguments.cetta.resolve()
    if not binary.is_file():
        parser.error(f"CeTTa binary not found: {binary}")

    cases = {
        "petta": (
            ("--lang", "petta", "--profile", "extended",
             "experiments/gslt2parse_foundation/migration_fixtures/"
             "nil_chaining/nil_hilbert_obfc_jarr_petta_v1.metta"),
            ROOT / "experiments/gslt2parse_foundation/migration_fixtures/"
                   "nil_chaining/nil_hilbert_obfc_jarr_petta_v1.expected",
        ),
        "he": (
            ("--lang", "he", "--profile", "extended",
             "tests/test_disc_trie.metta"),
            ROOT / "tests/test_disc_trie.expected",
        ),
        "prime": (
            ("--lang", "prime", "--prime-rewrite-frontier", "monolithic",
             "examples/prime/rewrite_frontier_tutorial/"
             "04_overlapping_supports.metta"),
            ROOT / "examples/prime/rewrite_frontier_tutorial/"
                   "04_overlapping_supports.monolithic.expected",
        ),
    }
    failures: list[str] = []
    for lane, (command, expected_path) in cases.items():
        expected = normalized_output(expected_path.read_text(encoding="utf-8"))
        try:
            linear_output, linear = run(
                binary, lane, "linear", command, arguments.timeout)
            deep_output, deep = run(
                binary, lane, "deep", command, arguments.timeout)
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append(f"{lane}: {error}")
            continue
        if linear_output != expected:
            failures.append(f"{lane}: linear output disagrees with golden")
        if deep_output != linear_output:
            failures.append(f"{lane}: deep output disagrees with linear")
        failures.extend(check_lane(lane, linear, deep))

    print(
        "(MatchDecisionStatsSummary lanes 3 "
        f"passed {3 - len({failure.split(':', 1)[0] for failure in failures})} "
        f"failed {len(failures)})"
    )
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
