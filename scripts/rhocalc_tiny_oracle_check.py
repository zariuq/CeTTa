#!/usr/bin/env python3

from __future__ import annotations

import sys
from pathlib import Path

from rhocalc_bounded_reachability import split_successor_set
from rhocalc_tiny_semantics import immediate_successors_from_mrho, strip_comments


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: rhocalc_tiny_oracle_check.py <bin> <fixture.mrho>", file=sys.stderr)
        return 2

    bin_path = sys.argv[1]
    fixture = Path(sys.argv[2])

    try:
        text = strip_comments(fixture.read_text(encoding="utf-8"))
        expected_file = fixture.with_suffix(".expected")
        expected = set(split_successor_set(expected_file.read_text(encoding="utf-8")))
        actual = set(immediate_successors_from_mrho(text))
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if actual == expected:
        return 0

    print("tiny oracle and checked-in successor set differ", file=sys.stderr)
    print(f"fixture: {fixture}", file=sys.stderr)
    only_expected = sorted(expected - actual)
    only_actual = sorted(actual - expected)
    if only_actual:
        print("only-tiny-oracle:", file=sys.stderr)
        for item in only_actual:
            print(item, file=sys.stderr)
    if only_expected:
        print("only-expected:", file=sys.stderr)
        for item in only_expected:
            print(item, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
