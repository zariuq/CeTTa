#!/usr/bin/env python3
"""Lean alignment for the pure cost-rho wire boundary (opt-in; not a test).

The CeTTa-only behaviour is checked by scripts/check_rhocalc_cost_prefixes.py.
This tool runs the same cases through the Mettapedia Lean reducer and compares
the two causal prefixes, and has Lean replay the receipts CeTTa printed.  It is
an alignment check between two independent implementations, not a proof about
compiled C.
"""

from __future__ import annotations

import copy
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

from check_rhocalc_cost_prefixes import (
    Case,
    make_branch_cases,
    make_cases,
    run_branch_outcome_checks,
    run_cetta,
)
from mettapedia_paths import discover_mettapedia_root
from rhocalc_cost_wire import (
    Wire,
    bvar,
    drop,
    natural,
    node,
    render_signature,
    signature,
    symbol,
    term_nil,
    wire_node,
)


SCHEMA = "cetta.cost-rho.causal-prefix.v2"
RECEIPT_REPLAY_SCHEMA = "cetta.cost-rho.receipt-replay.v1"


def lean_request(case: Case) -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "fuel": case.fuel,
        "searchFuel": case.search_fuel,
        "term": case.term,
    }


def run_lean(mettapedia_root: Path, runner: Path,
             cases: list[Case]) -> list[dict[str, Any]]:
    request_json = json.dumps(
        [lean_request(case) for case in cases], separators=(",", ":"), sort_keys=True
    )
    env = os.environ.copy()
    env["LAKE_JOBS"] = "1"
    env["LEAN_NUM_THREADS"] = "1"
    target = (
        "Mettapedia.Languages.ProcessCalculi.RhoCalculus.Cost."
        "BudgetedDifferential"
    )
    build = subprocess.run(
        ["lake", "build", target],
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if build.returncode != 0:
        raise RuntimeError(build.stdout.strip() + "\n" + build.stderr.strip())
    proc = subprocess.run(
        ["nice", "-n", "19", "lake", "env", "lean", "--run", str(runner), request_json],
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout.strip() + "\n" + proc.stderr.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("Lean differential runner produced no JSON")
    batch = json.loads(lines[-1])
    if not isinstance(batch, dict):
        raise RuntimeError("Lean differential runner produced a non-object batch")
    preconditions = batch.get("preconditions")
    outcomes = batch.get("outcomes")
    if not isinstance(preconditions, list) or len(preconditions) != len(cases):
        raise RuntimeError(
            f"expected {len(cases)} Lean precondition records, saw "
            f"{len(preconditions) if isinstance(preconditions, list) else type(preconditions).__name__}"
        )
    required = {"wellFormed": True, "canonical": True,
                "encodingCanonical": True}
    for case, checks in zip(cases, preconditions, strict=True):
        if checks != required:
            raise RuntimeError(
                f"theorem preconditions failed closed for {case.name}: {checks!r}"
            )
    if not isinstance(outcomes, list) or len(outcomes) != len(cases):
        raise RuntimeError(
            f"expected {len(cases)} Lean outcomes, saw "
            f"{len(outcomes) if isinstance(outcomes, list) else type(outcomes).__name__}"
        )
    return outcomes


def run_receipt_replay(
    mettapedia_root: Path,
    runner: Path,
    cases: list[Case],
    sequential: list[dict[str, Wire]],
    threaded: list[dict[str, Wire]],
    branch_samples: list[tuple[str, Wire, Wire]],
) -> int:
    labeled_results: list[tuple[str, Wire, Wire]] = []
    for mode, outcomes in (("sequential", sequential), ("threaded", threaded)):
        if len(outcomes) != len(cases):
            raise RuntimeError(
                f"receipt replay expected {len(cases)} {mode} outcomes, "
                f"saw {len(outcomes)}"
            )
        for case, outcome in zip(cases, outcomes, strict=True):
            result = outcome.get("result")
            if not isinstance(result, dict):
                raise RuntimeError(
                    f"receipt replay missing result wire for {mode}:{case.name}"
                )
            labeled_results.append((f"{mode}:{case.name}", case.term, result))
    labeled_results.extend(branch_samples)

    requests: list[dict[str, Any]] = []
    labels: list[str] = []
    expected: list[str] = []
    for label, term, result in labeled_results:
        requests.append(
            {
                "schema": RECEIPT_REPLAY_SCHEMA,
                "term": term,
                "result": result,
            }
        )
        labels.append(label)
        expected.append("accepted")

    # Fail-closed probes exercise the actual C wire result rather than a
    # separately constructed Lean fixture.
    first_result = sequential[0].get("result")
    if not isinstance(first_result, dict):
        raise RuntimeError("receipt replay tamper probe lacks a seed result")

    bad_id = copy.deepcopy(first_result)
    try:
        _, prefix_fields = wire_node(bad_id, "causal-prefix")
        _, events = wire_node(prefix_fields[1], "receipt")
        _, event_fields = wire_node(events[0], "event")
        event_fields[0] = natural(999_999)
    except (IndexError, TypeError, ValueError) as exc:
        raise RuntimeError(f"could not construct receipt-ID tamper probe: {exc}") from exc
    requests.append(
        {
            "schema": RECEIPT_REPLAY_SCHEMA,
            "term": cases[0].term,
            "result": bad_id,
        }
    )
    labels.append("tamper:event-id")
    expected.append("rejected")

    bad_residual = copy.deepcopy(first_result)
    try:
        _, prefix_fields = wire_node(bad_residual, "causal-prefix")
        prefix_fields[2] = term_nil()
    except (IndexError, TypeError, ValueError) as exc:
        raise RuntimeError(
            f"could not construct receipt-residual tamper probe: {exc}"
        ) from exc
    requests.append(
        {
            "schema": RECEIPT_REPLAY_SCHEMA,
            "term": cases[0].term,
            "result": bad_residual,
        }
    )
    labels.append("tamper:residual")
    expected.append("rejected")

    # A top-level bound index is raw syntax but not a scoped Cost-Rho term.
    # Pair it with a self-consistent empty receipt/residual so rejection cannot
    # be attributed merely to an unrelated event or residual mismatch.
    malformed_scope_term = drop(bvar(0))
    malformed_scope_result = node(
        "causal-prefix",
        [
            symbol("quiescent"),
            node("receipt", []),
            malformed_scope_term,
        ],
    )
    requests.append(
        {
            "schema": RECEIPT_REPLAY_SCHEMA,
            "term": malformed_scope_term,
            "result": malformed_scope_result,
        }
    )
    labels.append("malformed-scope:source")
    expected.append("rejected")

    env = os.environ.copy()
    env["LAKE_JOBS"] = "1"
    env["LEAN_NUM_THREADS"] = "1"
    target = (
        "Mettapedia.Languages.ProcessCalculi.RhoCalculus.Cost."
        "ReceiptReplayDifferential"
    )
    build = subprocess.run(
        ["lake", "build", target],
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if build.returncode != 0:
        raise RuntimeError(build.stdout.strip() + "\n" + build.stderr.strip())

    request_json = json.dumps(requests, separators=(",", ":"), sort_keys=True)
    proc = subprocess.run(
        ["nice", "-n", "19", "lake", "env", "lean", "--run", str(runner),
         "--stdin"],
        input=request_json,
        cwd=mettapedia_root,
        env=env,
        check=False,
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout.strip() + "\n" + proc.stderr.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("Lean receipt replay produced no JSON")
    batch = json.loads(lines[-1])
    outcomes = batch.get("outcomes") if isinstance(batch, dict) else None
    if not isinstance(outcomes, list) or len(outcomes) != len(expected):
        raise RuntimeError(
            f"receipt replay expected {len(expected)} verdicts, saw "
            f"{len(outcomes) if isinstance(outcomes, list) else type(outcomes).__name__}"
        )
    mismatches = [
        (label, want, saw)
        for label, want, saw in zip(labels, expected, outcomes, strict=True)
        if saw != want
    ]
    if mismatches:
        rendered = ", ".join(
            f"{label}: expected {want}, saw {saw}"
            for label, want, saw in mismatches[:8]
        )
        raise RuntimeError(f"Lean receipt replay verdict mismatch: {rendered}")
    return len(labeled_results)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: rhocalc_cost_differential.py <cetta-bin>", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    try:
        mettapedia_root = discover_mettapedia_root()
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    runner = Path(__file__).resolve().parents[1] / "tests" / "rhocalc_cost_differential.lean"
    replay_runner = (
        Path(__file__).resolve().parents[1]
        / "tests"
        / "rhocalc_cost_receipt_replay.lean"
    )
    cases = make_cases()
    branch_cases = make_branch_cases()

    boundary_exclusions = 0
    try:
        render_signature(signature(""))
    except ValueError:
        boundary_exclusions += 1
    else:
        print("cost-rho differential harness accepted an empty symbol atom", file=sys.stderr)
        return 1

    try:
        cetta, raw_lines = run_cetta(bin_path, cases)
        threaded, threaded_raw_lines = run_cetta(bin_path, cases, 4)
        branch_count, branch_samples = run_branch_outcome_checks(
            bin_path, branch_cases
        )
        lean = run_lean(mettapedia_root, runner, cases)
        replay_count = run_receipt_replay(
            mettapedia_root, replay_runner, cases, cetta, threaded,
            branch_samples
        )
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"cost-rho differential harness failed: {exc}", file=sys.stderr)
        return 1

    mismatches = 0
    for case, cetta_outcome, lean_outcome, raw_line in zip(
        cases, cetta, lean, raw_lines, strict=True
    ):
        if cetta_outcome == lean_outcome:
            continue
        mismatches += 1
        print(f"MISMATCH: {case.name}", file=sys.stderr)
        print(
            "  CeTTa: " + json.dumps(cetta_outcome, sort_keys=True, separators=(",", ":")),
            file=sys.stderr,
        )
        print(
            "  Lean:  " + json.dumps(lean_outcome, sort_keys=True, separators=(",", ":")),
            file=sys.stderr,
        )
        print(f"  raw:   {raw_line}", file=sys.stderr)
        if mismatches == 8:
            break

    if mismatches:
        print(f"FAIL: {mismatches} bounded cost-rho differential mismatches", file=sys.stderr)
        return 1

    threaded_mismatches = 0
    for case, sequential_outcome, threaded_outcome, raw_line in zip(
        cases, cetta, threaded, threaded_raw_lines, strict=True
    ):
        if threaded_outcome == sequential_outcome:
            continue
        threaded_mismatches += 1
        print(f"THREADED MISMATCH: {case.name}", file=sys.stderr)
        print(
            "  sequential: " + json.dumps(
                sequential_outcome, sort_keys=True, separators=(",", ":")
            ),
            file=sys.stderr,
        )
        print(
            "  threaded:   " + json.dumps(
                threaded_outcome, sort_keys=True, separators=(",", ":")
            ),
            file=sys.stderr,
        )
        print(f"  raw:        {raw_line}", file=sys.stderr)
        if threaded_mismatches == 8:
            break

    if threaded_mismatches:
        print(
            f"FAIL: {threaded_mismatches} threaded/sequential cost-rho "
            "prefix mismatches",
            file=sys.stderr,
        )
        return 1

    print(
        f"PASS: {len(cases)} bounded cost-rho CeTTa/Lean cases; "
        f"{len(cases)} threaded/sequential prefix cases; "
        f"{replay_count} compiled-C receipts replayed by Lean; "
        "2 tampered receipts and 1 malformed-scope receipt rejected; "
        f"{branch_count} exhaustive/threaded branch outcome sets; "
        f"{len(cases)}/{len(cases)} theorem precondition records accepted; "
        f"{boundary_exclusions} out-of-fragment boundary case rejected; "
        "testing boundary only, not a universal theorem about compiled C"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
