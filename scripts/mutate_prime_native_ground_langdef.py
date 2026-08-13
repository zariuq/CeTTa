#!/usr/bin/env python3
"""Create destructive Prime native-ground langdef mutations for CI canaries."""

from __future__ import annotations

import argparse
from pathlib import Path


MUTATIONS = {
    "conflicting-form-outcome": (
        "      (head (PrimeNormalizes PTwo PTwo))",
        "      (head (PrimeJudges (PForm PVNumber) PEstablished))",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    old, new = MUTATIONS[args.mutation]
    if text.count(old) != 1:
        parser.error(f"mutation anchor count changed for {args.mutation}")
    args.output.write_text(text.replace(old, new, 1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
