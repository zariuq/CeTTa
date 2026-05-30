#!/usr/bin/env python3

from __future__ import annotations

import re
import sys

from rhocalc_bounded_reachability import normalize_expr, run_cmd
from rhocalc_tiny_semantics import immediate_output_barbs_from_mrho


def expected_channels(spec: str) -> set[str]:
    spec = spec.strip()
    if spec == "empty":
        return set()
    return {item.strip() for item in spec.split(",") if item.strip()}


def cetta_observed_channels(
    bin_path: str,
    syntax: str,
    fixture: str,
) -> set[str]:
    proc = run_cmd([
        bin_path,
        "--lang",
        "rhocalc",
        "--syntax",
        syntax,
        fixture,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    lines = [line.strip() for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("cetta produced no residual output")
    residual = normalize_expr(bin_path, syntax, lines[0])
    channels = set()
    for barb in immediate_output_barbs_from_mrho(residual):
        if not barb.startswith("$"):
            raise ValueError(
                f"unsupported CeTTa overlap barb '{barb}'; expected a plain name variable"
            )
        channels.add(barb[1:])
    return channels


def parse_rholang_storage_channels(output: str) -> set[str]:
    marker = "Storage Contents:"
    if marker not in output:
        raise RuntimeError("rholang-cli output missing 'Storage Contents:' section")
    storage = output.split(marker, 1)[1].strip()
    if storage.startswith("The space is empty."):
        return set()

    items: list[str] = []
    depth = 0
    start = 0
    for i, ch in enumerate(storage):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "|" and depth == 0:
            items.append(storage[start:i].strip())
            start = i + 1
    items.append(storage[start:].strip())

    channels = set()
    for item in items:
        if not item:
            continue
        match = re.match(r'^"([^"]+)"!\(', item)
        if match:
            channels.add(match.group(1))
    if not channels:
        raise RuntimeError("could not parse unmatched sends from rholang-cli storage output")
    return channels


def rholang_observed_channels(rholang_cli: str, fixture: str) -> set[str]:
    proc = run_cmd([
        rholang_cli,
        "--unmatched-sends-only",
        fixture,
    ])
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or proc.stdout.strip())
    return parse_rholang_storage_channels(proc.stdout)


def main() -> int:
    if len(sys.argv) != 7:
        print(
            "usage: rhocalc_m3_rholang_cli_compare.py "
            "<cetta-bin> <rholang-cli> <cetta-syntax> <cetta-fixture> <rholang-fixture> <expected-channels>",
            file=sys.stderr,
        )
        return 2

    cetta_bin = sys.argv[1]
    rholang_cli = sys.argv[2]
    cetta_syntax = sys.argv[3]
    cetta_fixture = sys.argv[4]
    rholang_fixture = sys.argv[5]
    expected = expected_channels(sys.argv[6])

    try:
        cetta_channels = cetta_observed_channels(cetta_bin, cetta_syntax, cetta_fixture)
        rholang_channels = rholang_observed_channels(rholang_cli, rholang_fixture)
    except (RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if cetta_channels != expected:
        print("cetta observed channel set does not match expected overlap contract",
              file=sys.stderr)
        print(f"expected: {sorted(expected)}", file=sys.stderr)
        print(f"observed: {sorted(cetta_channels)}", file=sys.stderr)
        return 1

    if rholang_channels != expected:
        print("rholang-cli observed channel set does not match expected overlap contract",
              file=sys.stderr)
        print(f"expected: {sorted(expected)}", file=sys.stderr)
        print(f"observed: {sorted(rholang_channels)}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
