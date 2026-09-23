#!/usr/bin/env python3
"""Generate a deep deterministic EBNF grammar and positive/negative inputs."""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--depth", type=int, default=600)
    parser.add_argument("--grammar", type=Path, required=True)
    parser.add_argument("--accepted", type=Path, required=True)
    parser.add_argument("--rejected", type=Path, required=True)
    args = parser.parse_args()
    if args.depth < 2:
        parser.error("--depth must be at least 2")

    rows = ["<s> ::= <r0>"]
    rows.extend(
        f'<r{index}> ::= "x" <r{index + 1}>'
        for index in range(args.depth - 1)
    )
    rows.append(f'<r{args.depth - 1}> ::= "x"')
    args.grammar.write_text("\n".join(rows) + "\n", encoding="utf-8")
    args.accepted.write_text("x" * args.depth + "\n", encoding="utf-8")
    args.rejected.write_text(
        "x" * (args.depth - 1) + "y\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
