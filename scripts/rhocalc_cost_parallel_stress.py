#!/usr/bin/env python3
"""Bounded stress checks for the cost-rho concurrent-wave executor."""

from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
from dataclasses import dataclass

from rhocalc_cost_differential import (
    A,
    PAY,
    Wire,
    node,
    parse_cetta_prefix,
    proc_nil,
    purse,
    render_term,
    signature,
    signed,
    term_par,
    whole_redex,
    wire_node,
)


@dataclass(frozen=True)
class Prefix:
    status: str
    events: list[Wire]
    state: Wire


def json_key(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def unpack_prefix(value: Wire) -> Prefix:
    _, fields = wire_node(value, "causal-prefix")
    status = fields[0].get("symbol")
    if not isinstance(status, str):
        raise ValueError(f"malformed prefix status: {fields[0]!r}")
    _, events = wire_node(fields[1], "receipt")
    return Prefix(status, events, fields[2])


def run_prefix(
    bin_path: str,
    term: Wire,
    *,
    threads: int,
    fuel: int,
    search_fuel: int = 1_000_000,
    timeout: float = 15.0,
) -> Prefix:
    script = "\n".join([
        "!(import! &self lts:rho:cost)",
        f"!(pragma! num-threads {threads})",
        f"!(lts:rho:cost:causal-prefix {fuel} {search_fuel} {render_term(term)})",
        "",
    ])
    proc = subprocess.run(
        [bin_path, "--profile", "extended", "--lang", "he", "/dev/stdin"],
        input=script,
        check=False,
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if len(lines) != 3 or lines[:2] != ["[()]", "[()]"]:
        raise RuntimeError(f"unexpected CeTTa prefix output: {lines!r}")
    return unpack_prefix(parse_cetta_prefix(lines[2]))


def event_fields(event: Wire) -> list[Wire]:
    _, fields = wire_node(event, "event")
    return fields


def event_id(event: Wire) -> int:
    value = event_fields(event)[0].get("natural")
    if not isinstance(value, int):
        raise ValueError(f"malformed event id: {event!r}")
    return value


def event_causes(event: Wire) -> list[int]:
    _, causes = wire_node(event_fields(event)[1], "causes")
    values: list[int] = []
    for cause in causes:
        value = cause.get("natural")
        if not isinstance(value, int):
            raise ValueError(f"malformed event cause: {cause!r}")
        values.append(value)
    return values


def event_label(event: Wire) -> str:
    fields = event_fields(event)
    return json_key([fields[2], fields[3]])


def canonical_unique_label_receipt(events: list[Wire]) -> tuple[tuple[str, ...], tuple[tuple[str, str], ...]]:
    by_id = {event_id(event): event_label(event) for event in events}
    if len(by_id) != len(events) or len(set(by_id.values())) != len(events):
        raise ValueError("receipt labels are not unique in unique-label stress case")
    labels = tuple(sorted(by_id.values()))
    edges = tuple(sorted(
        (by_id[cause], by_id[event_id(event)])
        for event in events
        for cause in event_causes(event)
    ))
    return labels, edges


def unique_reactions(count: int, *, shared_syntax: bool) -> Wire:
    parts: list[Wire] = []
    for index in range(count):
        syntax = PAY if shared_syntax else signature(f"syntax-{index}")
        spend = signature(f"spend-{index}")
        done = signed(proc_nil(), signature(f"done-{index}"))
        parts.extend([whole_redex(syntax, spend, body=done), purse(syntax, [spend])])
    return term_par(parts)


def compare_unique_case(bin_path: str, term: Wire, count: int) -> None:
    sequential = run_prefix(bin_path, term, threads=1, fuel=count + 1)
    threaded = run_prefix(bin_path, term, threads=4, fuel=count + 1)
    if sequential.status != "quiescent" or threaded.status != "quiescent":
        raise AssertionError(f"unexpected statuses: {sequential.status}, {threaded.status}")
    if json_key(sequential.state) != json_key(threaded.state):
        raise AssertionError("threaded and sequential residual states differ")
    if len(sequential.events) != count or len(threaded.events) != count:
        raise AssertionError("threaded or sequential receipt lost a firing")
    if canonical_unique_label_receipt(sequential.events) != canonical_unique_label_receipt(threaded.events):
        raise AssertionError("threaded and sequential causal receipts are not isomorphic")


def independent_locations(bin_path: str) -> None:
    compare_unique_case(bin_path, unique_reactions(24, shared_syntax=False), 24)


def same_syntax_disjoint_occurrences(bin_path: str) -> None:
    term = unique_reactions(20, shared_syntax=True)
    threaded = run_prefix(bin_path, term, threads=4, fuel=21)
    if threaded.status != "quiescent" or len(threaded.events) != 20:
        raise AssertionError("same-syntax disjoint reactions did not all fire")
    if any(event_causes(event) for event in threaded.events):
        raise AssertionError("same syntax introduced false causality between disjoint purses")
    compare_unique_case(bin_path, term, 20)


def one_purse_serializes(bin_path: str) -> None:
    count = 12
    redexes = [
        whole_redex(
            PAY,
            A,
            body=signed(proc_nil(), signature(f"serial-done-{index}")),
        )
        for index in range(count)
    ]
    term = term_par([*redexes, purse(PAY, [A] * count)])
    threaded = run_prefix(bin_path, term, threads=4, fuel=count + 1)
    if threaded.status != "quiescent" or len(threaded.events) != count:
        raise AssertionError("serialized purse run did not consume every funded redex")
    for index, event in enumerate(threaded.events):
        expected = [] if index == 0 else [index - 1]
        if event_id(event) != index or event_causes(event) != expected:
            raise AssertionError("single-purse receipt is not the expected causal chain")


def budget_monotonicity(bin_path: str) -> None:
    term = unique_reactions(6, shared_syntax=False)
    budgets = [0, 1, 2, 4, 8, 16, 32, 64, 128, 10_000]
    runs = [
        run_prefix(bin_path, term, threads=4, fuel=7, search_fuel=budget)
        for budget in budgets
    ]
    counts = [len(run.events) for run in runs]
    if counts != sorted(counts):
        raise AssertionError(f"larger search budget lost firings: {counts!r}")
    determinate = [index for index, run in enumerate(runs) if run.status != "search-exhausted"]
    if not determinate:
        raise AssertionError("no budget reached a determinate verdict")
    first = determinate[0]
    final = runs[first]
    for run in runs[first:]:
        if (run.status != final.status or json_key(run.state) != json_key(final.state) or
                canonical_unique_label_receipt(run.events) !=
                canonical_unique_label_receipt(final.events)):
            raise AssertionError("determinate verdict changed at a larger search budget")


def large_cover_is_lazy(bin_path: str) -> None:
    coin = signature("coin")
    demand = signature(*(["coin"] * 10))
    term = term_par([
        whole_redex(PAY, demand),
        *(purse(PAY, [coin]) for _ in range(20)),
    ])
    zero = run_prefix(
        bin_path, term, threads=4, fuel=0, search_fuel=0, timeout=5.0
    )
    first = run_prefix(
        bin_path, term, threads=4, fuel=1, search_fuel=12, timeout=5.0
    )
    if zero.status != "fuel-exhausted" or zero.events:
        raise AssertionError("zero fuel searched or emitted a large-cover witness")
    if first.status != "fuel-exhausted" or len(first.events) != 1:
        raise AssertionError("lazy large-cover search did not yield its first witness")


def randomized_independent(bin_path: str, repeat: int) -> None:
    rng = random.Random(0xC057_2026)
    for _ in range(repeat):
        count = rng.randint(2, 12)
        compare_unique_case(
            bin_path,
            unique_reactions(count, shared_syntax=rng.choice([False, True])),
            count,
        )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bin_path")
    parser.add_argument("--repeat", type=int, default=8)
    args = parser.parse_args(argv[1:])
    if args.repeat < 1:
        parser.error("--repeat must be positive")

    checks = [
        ("independent-locations", lambda: independent_locations(args.bin_path)),
        ("same-syntax-disjoint-occurrences", lambda: same_syntax_disjoint_occurrences(args.bin_path)),
        ("one-purse-serialization", lambda: one_purse_serializes(args.bin_path)),
        ("search-budget-monotonicity", lambda: budget_monotonicity(args.bin_path)),
        ("large-cover-laziness", lambda: large_cover_is_lazy(args.bin_path)),
        ("randomized-independent-receipts", lambda: randomized_independent(args.bin_path, args.repeat)),
    ]
    for name, check in checks:
        try:
            check()
        except (AssertionError, OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL: cost-rho parallel stress {name}: {exc}")
            return 1
        print(f"PASS: cost-rho parallel stress {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
