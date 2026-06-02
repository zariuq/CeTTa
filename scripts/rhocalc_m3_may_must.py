#!/usr/bin/env python3

from __future__ import annotations

import sys

from rhocalc_bounded_reachability import normalize_expr
from rhocalc_m3_rholang_cli_compare import (
    cetta_observed_outputs,
    expected_observation_sets,
    extract_single_mrho_result,
    format_expected_observation_sets,
    output_observations,
)
from rhocalc_bounded_reachability import run_cmd
from rhocalc_tiny_semantics import (
    immediate_successors,
    normalize_proc,
    parse_mrho_proc,
    proc_to_mrho,
    strip_comments,
)


def initial_mrho(bin_path: str, syntax: str, fixture: str) -> str:
    if syntax == "metta":
        proc = run_cmd([bin_path, fixture])
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
        return extract_single_mrho_result(proc.stdout)
    if syntax == "mrho":
        with open(fixture, "r", encoding="utf-8") as handle:
            return strip_comments(handle.read()).strip()
    return normalize_expr(bin_path, syntax, open(fixture, encoding="utf-8").read())


def normal_form_observation_sets(
    initial: str,
    *,
    max_depth: int = 32,
    max_states: int = 20000,
) -> set[tuple[str, ...]]:
    start = proc_to_mrho(normalize_proc(parse_mrho_proc(initial)))
    frontier = {start}
    seen = {start}
    normal_outputs: set[tuple[str, ...]] = set()

    for _ in range(max_depth + 1):
        next_frontier: set[str] = set()
        for state in sorted(frontier):
            successors = immediate_successors(parse_mrho_proc(state))
            if not successors:
                normal_outputs.add(output_observations(parse_mrho_proc(state)))
                continue
            for successor in successors:
                normalized = proc_to_mrho(normalize_proc(parse_mrho_proc(successor)))
                if normalized in seen:
                    continue
                seen.add(normalized)
                if len(seen) > max_states:
                    raise RuntimeError("reachable state bound exceeded")
                next_frontier.add(normalized)
        if not next_frontier:
            return normal_outputs
        frontier = next_frontier

    raise RuntimeError("normal-form depth bound exceeded")


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: rhocalc_m3_may_must.py <cetta-bin> <syntax> <fixture> "
            "<expected-output-set[;expected-output-set...]>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    syntax = sys.argv[2]
    fixture = sys.argv[3]

    try:
        expected = expected_observation_sets(sys.argv[4])
        observed = normal_form_observation_sets(initial_mrho(bin_path, syntax, fixture))
        chosen = cetta_observed_outputs(bin_path, syntax, fixture)
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if observed != expected:
        print("full normal-form observation frontier mismatch", file=sys.stderr)
        print(f"expected: {format_expected_observation_sets(expected)}", file=sys.stderr)
        print(f"observed: {format_expected_observation_sets(observed)}", file=sys.stderr)
        return 1

    if chosen not in expected:
        print("chosen reducer output is outside the full normal-form frontier", file=sys.stderr)
        print(f"expected: {format_expected_observation_sets(expected)}", file=sys.stderr)
        print(f"chosen: {format_expected_observation_sets({chosen})}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
