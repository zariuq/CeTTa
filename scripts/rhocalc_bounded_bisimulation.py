#!/usr/bin/env python3

from __future__ import annotations

import sys

from rhocalc_bounded_reachability import (
    normalize_expr,
    run_cmd,
    successor_set_from_expr,
)
from rhocalc_tiny_semantics import immediate_output_barbs_from_mrho


def normalize_file(bin_path: str, syntax: str, path: str) -> str:
    proc = run_cmd([
        bin_path,
        "--translate",
        "--lang",
        "rhocalc",
        "--syntax",
        syntax,
        "--lang",
        "rhocalc",
        "--syntax",
        "mrho",
        path,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return normalize_expr(bin_path, "mrho", proc.stdout.strip())


def reachable_layers(
    bin_path: str,
    start_expr: str,
    depth: int,
    cache: dict[tuple[str, int], tuple[frozenset[str], ...]],
) -> tuple[frozenset[str], ...]:
    key = (start_expr, depth)
    if key in cache:
        return cache[key]
    seen = {start_expr}
    frontier = {start_expr}
    layers: list[frozenset[str]] = [frozenset({start_expr})]
    for _ in range(depth):
        next_frontier: set[str] = set()
        for state in sorted(frontier):
            next_frontier.update(successor_set_from_expr(bin_path, "mrho", state))
        frontier = next_frontier - seen
        layers.append(frozenset(frontier))
        seen.update(frontier)
    cache[key] = tuple(layers)
    return cache[key]


def reachable_states_upto(
    bin_path: str,
    start_expr: str,
    depth: int,
    cache: dict[tuple[str, int], tuple[frozenset[str], ...]],
) -> set[str]:
    states: set[str] = set()
    for layer in reachable_layers(bin_path, start_expr, depth, cache):
        states.update(layer)
    return states


def reachable_barbs_upto(
    bin_path: str,
    start_expr: str,
    depth: int,
    reach_cache: dict[tuple[str, int], tuple[frozenset[str], ...]],
) -> set[str]:
    outputs: set[str] = set()
    for state in reachable_states_upto(bin_path, start_expr, depth, reach_cache):
        outputs.update(immediate_output_barbs_from_mrho(state))
    return outputs


def weak_match_candidates(
    bin_path: str,
    start_expr: str,
    depth: int,
    cache: dict[tuple[str, int], tuple[frozenset[str], ...]],
) -> list[tuple[int, str]]:
    candidates: list[tuple[int, str]] = []
    for cost, layer in enumerate(reachable_layers(bin_path, start_expr, depth, cache)):
        for state in sorted(layer):
            candidates.append((cost, state))
    return candidates


def bisim_failure(
    message: str,
    lhs: str,
    rhs: str,
    depth: int,
) -> tuple[bool, str]:
    detail = [
        message,
        f"depth: {depth}",
        "lhs:",
        lhs,
        "rhs:",
        rhs,
    ]
    return False, "\n".join(detail)


def bounded_barbed_bisimilar(
    bin_path: str,
    lhs: str,
    rhs: str,
    depth: int,
    reach_cache: dict[tuple[str, int], tuple[frozenset[str], ...]],
    memo: dict[tuple[str, str, int], tuple[bool, str | None]],
) -> tuple[bool, str | None]:
    key = (lhs, rhs, depth)
    if key in memo:
        return memo[key]

    lhs_reachable = reachable_barbs_upto(bin_path, lhs, depth, reach_cache)
    rhs_reachable = reachable_barbs_upto(bin_path, rhs, depth, reach_cache)

    if lhs_reachable != rhs_reachable:
        only_lhs = sorted(lhs_reachable - rhs_reachable)
        only_rhs = sorted(rhs_reachable - lhs_reachable)
        message = [
            "bounded weak barbs differ within the observation bound",
            f"depth: {depth}",
            "lhs:",
            lhs,
            "rhs:",
            rhs,
        ]
        if only_lhs:
            message.append("only-lhs-barbs:")
            message.extend(only_lhs)
        if only_rhs:
            message.append("only-rhs-barbs:")
            message.extend(only_rhs)
        memo[key] = (False, "\n".join(message))
        return memo[key]

    lhs_now = immediate_output_barbs_from_mrho(lhs)
    rhs_now = immediate_output_barbs_from_mrho(rhs)

    if not lhs_now.issubset(rhs_reachable):
        memo[key] = bisim_failure(
            "lhs immediate barbs are not reachable from rhs within the bound",
            lhs,
            rhs,
            depth,
        )
        return memo[key]
    if not rhs_now.issubset(lhs_reachable):
        memo[key] = bisim_failure(
            "rhs immediate barbs are not reachable from lhs within the bound",
            lhs,
            rhs,
            depth,
        )
        return memo[key]

    if depth == 0:
        memo[key] = (True, None)
        return memo[key]

    lhs_succ = set(successor_set_from_expr(bin_path, "mrho", lhs))
    rhs_succ = set(successor_set_from_expr(bin_path, "mrho", rhs))
    lhs_candidates = weak_match_candidates(bin_path, lhs, depth, reach_cache)
    rhs_candidates = weak_match_candidates(bin_path, rhs, depth, reach_cache)

    for successor in sorted(lhs_succ):
        matched = False
        for cost, candidate in rhs_candidates:
            ok, _ = bounded_barbed_bisimilar(
                bin_path,
                successor,
                candidate,
                depth - max(1, cost),
                reach_cache,
                memo,
            )
            if ok:
                matched = True
                break
        if not matched:
            memo[key] = bisim_failure(
                "lhs successor has no bounded bisimulation match in rhs closure",
                successor,
                rhs,
                depth - 1,
            )
            return memo[key]

    for successor in sorted(rhs_succ):
        matched = False
        for cost, candidate in lhs_candidates:
            ok, _ = bounded_barbed_bisimilar(
                bin_path,
                candidate,
                successor,
                depth - max(1, cost),
                reach_cache,
                memo,
            )
            if ok:
                matched = True
                break
        if not matched:
            memo[key] = bisim_failure(
                "rhs successor has no bounded bisimulation match in lhs closure",
                lhs,
                successor,
                depth - 1,
            )
            return memo[key]

    memo[key] = (True, None)
    return memo[key]


def main() -> int:
    if len(sys.argv) != 8:
        print(
            "usage: rhocalc_bounded_bisimulation.py "
            "<bin> <depth> <lhs-syntax> <lhs-file> <rhs-syntax> <rhs-file> <expect>",
            file=sys.stderr,
        )
        return 2

    bin_path = sys.argv[1]
    depth = int(sys.argv[2])
    lhs_syntax = sys.argv[3]
    lhs_path = sys.argv[4]
    rhs_syntax = sys.argv[5]
    rhs_path = sys.argv[6]
    expect = sys.argv[7]

    if depth < 0:
        print("depth must be non-negative", file=sys.stderr)
        return 2
    if expect not in ("yes", "no"):
        print("expect must be 'yes' or 'no'", file=sys.stderr)
        return 2

    try:
        lhs = normalize_file(bin_path, lhs_syntax, lhs_path)
        rhs = normalize_file(bin_path, rhs_syntax, rhs_path)
        reach_cache: dict[tuple[str, int], tuple[frozenset[str], ...]] = {}
        memo: dict[tuple[str, str, int], tuple[bool, str | None]] = {}
        ok, detail = bounded_barbed_bisimilar(
            bin_path, lhs, rhs, depth, reach_cache, memo
        )
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if expect == "yes":
        if ok:
            return 0
        if detail:
            print(detail, file=sys.stderr)
        return 1

    if ok:
        print("processes are bounded-bisimilar but expectation was 'no'", file=sys.stderr)
        print("lhs:", file=sys.stderr)
        print(lhs, file=sys.stderr)
        print("rhs:", file=sys.stderr)
        print(rhs, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
