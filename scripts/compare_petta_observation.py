#!/usr/bin/env python3
"""Compare two PeTTa output records under an explicit observation contract."""

from __future__ import annotations

import argparse
from collections import Counter
import difflib
from pathlib import Path

from petta_corpus_manifest import (
    STDOUT_EXACT_STREAM,
    STDOUT_OCCURRENCE_BAG,
    STDOUT_OBSERVATION_CONTRACTS,
    stdout_observation,
)


def report_bag_delta(expected: Counter[str], actual: Counter[str]) -> None:
    for label, delta in (
        ("missing", expected - actual),
        ("unexpected", actual - expected),
    ):
        for line, count in sorted(delta.items()):
            print(f"{label} x{count}: {line.rstrip()!r}")


def compare_paths(contract: str, expected_path: Path, actual_path: Path) -> bool:
    expected_text = expected_path.read_text(encoding="utf-8")
    actual_text = actual_path.read_text(encoding="utf-8")
    expected = stdout_observation(expected_text, contract)
    actual = stdout_observation(actual_text, contract)
    if actual == expected:
        return True
    if contract == STDOUT_EXACT_STREAM:
        print("".join(difflib.unified_diff(
            str(expected).splitlines(keepends=True),
            str(actual).splitlines(keepends=True),
            fromfile="expected",
            tofile="actual",
        )), end="")
    elif contract == STDOUT_OCCURRENCE_BAG:
        assert isinstance(expected, Counter)
        assert isinstance(actual, Counter)
        report_bag_delta(expected, actual)
    return False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--contract", required=True,
        choices=sorted(STDOUT_OBSERVATION_CONTRACTS),
    )
    parser.add_argument("--expected", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return 0 if compare_paths(
            args.contract, args.expected, args.actual
        ) else 1
    except (OSError, ValueError) as error:
        print(f"FAIL: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
