#!/usr/bin/env python3
"""Plant a route-loss defect in PathMap chain execution."""

from __future__ import annotations

import argparse
from pathlib import Path


MARKER = """        bool allow_chain_flatten = true;
"""
MUTATION = """        bool allow_chain_flatten = false;
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    count = source.count(MARKER)
    if count != 1:
        raise SystemExit(
            f"chain-flatten stream marker occurs {count} times, expected 1"
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source.replace(MARKER, MUTATION), encoding="utf-8")


if __name__ == "__main__":
    main()
