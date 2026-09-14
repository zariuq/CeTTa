#!/usr/bin/env python3
"""Audit the optional observation boundary of threaded Cost-Rho execution.

The fixture has exactly one funded COMM.  That pins one selected wave, so the
state-only and receipt-observed runs may be compared exactly without making a
confluence claim about independently scheduled rho programs.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


COUNTER_RE = re.compile(r"^runtime-counter\s+(\S+)\s+(\d+)\s*$")
RECEIPT_ONLY_COUNTERS = (
    "cost-rho-receipt-producer-propagated",
    "cost-rho-receipt-cause-source-scanned",
    "cost-rho-receipt-event-allocation",
    "cost-rho-receipt-event-materialized",
    "cost-rho-receipt-event-retained",
    "cost-rho-receipt-validation",
)
CLAIM_COUNTERS = (
    "cost-rho-parallel-acquired-claim",
    "cost-rho-parallel-committed-claim",
    "cost-rho-parallel-released-claim",
)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def counters(stderr: str) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in stderr.splitlines():
        match = COUNTER_RE.match(line.strip())
        if match:
            result[match.group(1)] = int(match.group(2))
    return result


def normalized(text: str) -> str:
    return " ".join(text.split())


def last_metta_result(stdout: str) -> str:
    """Return the last top-level result line.

    Loading the cost module produces an earlier ``[()]`` result.  The fixture's
    final query is deliberately last, so treating that module-load result as
    part of the residual would test output formatting instead of observation
    transparency.
    """
    result_lines = [
        line.strip()
        for line in stdout.splitlines()
        if line.strip().startswith("[") and line.strip().endswith("]")
    ]
    if not result_lines:
        raise AssertionError(f"expected a MeTTa result, got: {stdout}")
    rendered = result_lines[-1]
    return normalized(rendered[1:-1])


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def state_only_counter_failures(stats: dict[str, int]) -> list[str]:
    failures: list[str] = []
    if stats.get("cost-rho-parallel-state-only-run") != 1:
        failures.append("state-only mode counter must equal one")
    if stats.get("cost-rho-parallel-receipt-run") != 0:
        failures.append("state-only run attached the receipt observer")
    for name in RECEIPT_ONLY_COUNTERS:
        if stats.get(name) != 0:
            failures.append(f"state-only run performed receipt work ({name})")
    return failures


def claim_conservation_failures(stats: dict[str, int]) -> list[str]:
    acquired = stats.get("cost-rho-parallel-acquired-claim", 0)
    committed = stats.get("cost-rho-parallel-committed-claim", 0)
    released = stats.get("cost-rho-parallel-released-claim", 0)
    failures: list[str] = []
    if acquired <= 0:
        failures.append("claim acquisition counter did not advance")
    if acquired != committed + released:
        failures.append(
            "acquired claims must equal committed plus released claims "
            f"({acquired} != {committed} + {released})"
        )
    return failures


def state_only_command(binary: str, fixture: Path, threads: int) -> list[str]:
    return [
        binary,
        "--emit-runtime-stats",
        "--num-threads", str(threads),
        "--lang", "rhocalc",
        "--profile", "cost",
        "--syntax", "mrho",
        str(fixture),
    ]


def observed_command(binary: str, fixture: Path, threads: int) -> list[str]:
    return [
        binary,
        "--emit-runtime-stats",
        "--num-threads", str(threads),
        "--lang", "he",
        "--profile", "extended",
        str(fixture),
    ]


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(
            "usage: rhocalc_cost_observer_transparency.py <stats-cetta>",
            file=sys.stderr,
        )
        return 2

    binary = argv[1]
    root = Path(__file__).resolve().parent.parent
    state_fixture = root / "tests/rhocalc_cost_run/internal_split_tokens_basic.mrho"
    expected_state = root / "tests/rhocalc_cost_run/internal_split_tokens_basic.expected"
    observed_fixture = root / "tests/test_lts_rho_cost_observer_transparency.metta"
    expected_residual = normalized(expected_state.read_text())

    state = run(state_only_command(binary, state_fixture, 4))
    require(state.returncode == 0, f"threaded state-only run failed: {state.stderr}")
    require(normalized(state.stdout) == expected_residual,
            "threaded state-only funded residual changed")
    state_stats = counters(state.stderr)
    state_counter_failures = state_only_counter_failures(state_stats)
    require(not state_counter_failures,
            f"state-only counter contract failed: {state_counter_failures}")
    state_claim_failures = claim_conservation_failures(state_stats)
    require(not state_claim_failures,
            f"state-only claim conservation failed: {state_claim_failures}")
    print("PASS threaded state-only residual")
    print("PASS threaded state-only observer absent")
    print("PASS threaded state-only receipt work zero")
    print("PASS threaded state-only claim conservation")

    observed = run(observed_command(binary, observed_fixture, 4))
    require(observed.returncode == 0,
            f"threaded observed run failed: {observed.stderr}")
    observed_residual = last_metta_result(observed.stdout)
    require(observed_residual == expected_residual,
            "observer changed the fixed-wave funded residual")
    observed_stats = counters(observed.stderr)
    require(observed_stats.get("cost-rho-parallel-state-only-run") == 0,
            f"observed run entered state-only mode: {observed_stats}")
    require(observed_stats.get("cost-rho-parallel-receipt-run") == 1,
            f"observed mode counter mismatch: {observed_stats}")
    firings = observed_stats.get("cost-rho-parallel-firing", 0)
    require(firings == 1, f"fixture did not select one fixed firing: {observed_stats}")
    for name in (
        "cost-rho-receipt-event-allocation",
        "cost-rho-receipt-event-materialized",
        "cost-rho-receipt-event-retained",
    ):
        require(observed_stats.get(name) == firings,
                f"one receipt event was not produced per firing ({name}): {observed_stats}")
    require(observed_stats.get("cost-rho-receipt-producer-propagated", 0) > 0,
            f"observed run did not propagate producers: {observed_stats}")
    require(observed_stats.get("cost-rho-receipt-cause-source-scanned", 0) > 0,
            f"observed run did not inspect causal sources: {observed_stats}")
    require(observed_stats.get("cost-rho-receipt-validation") == 1,
            f"observed receipt was not validated once: {observed_stats}")
    observed_claim_failures = claim_conservation_failures(observed_stats)
    require(not observed_claim_failures,
            f"observed claim conservation failed: {observed_claim_failures}")
    print("PASS fixed-wave observer residual transparency")
    print("PASS receipt observer work materialized on demand")
    print("PASS observed claim conservation")

    for name in (
        "cost-rho-parallel-firing",
        "cost-rho-parallel-acquired-claim",
        "cost-rho-parallel-committed-claim",
        "cost-rho-funding-head-consumed",
    ):
        require(state_stats.get(name, 0) > 0,
                f"state-only operational counter did not advance ({name}): {state_stats}")
        require(state_stats.get(name) == observed_stats.get(name),
                f"observer changed fixed-wave operation ({name}): "
                f"state={state_stats} observed={observed_stats}")
    require(
        state_stats.get("cost-rho-parallel-released-claim", 0)
        == observed_stats.get("cost-rho-parallel-released-claim", 0),
        "observer changed fixed-wave released-claim count",
    )
    print("PASS fixed-wave reductions equal")
    print("PASS fixed-wave committed claims equal")
    print("PASS fixed-wave funding consumption equal")

    sequential_state = run(state_only_command(binary, state_fixture, 1))
    sequential_observed = run(observed_command(binary, observed_fixture, 1))
    require(sequential_state.returncode == 0,
            f"single-worker state-only run failed: {sequential_state.stderr}")
    require(sequential_observed.returncode == 0,
            f"single-worker observed run failed: {sequential_observed.stderr}")
    require(normalized(sequential_state.stdout) == expected_residual,
            "single-worker state-only funded residual changed")
    require(last_metta_result(sequential_observed.stdout) == expected_residual,
            "single-worker receipt projection changed the exact residual")
    require(normalized(sequential_state.stdout) == last_metta_result(sequential_observed.stdout),
            "single-worker observer-on/off residuals differ")
    print("PASS deterministic single-worker observer equality")
    print("SUMMARY cost-rho-observer-transparency 11/11 PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except AssertionError as error:
        print(f"FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
