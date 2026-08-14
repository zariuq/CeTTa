#!/usr/bin/env python3
"""Plant one semantic defect in Prime's open lambda-Pi presentation."""

from __future__ import annotations

import argparse
from pathlib import Path


MUTATIONS = {
    "drop-context-validation": (
        """      (body
        (PrimeLpContextValid ?context)
        (PrimeLpSynthCore ?context ?term ?type)))""",
        """      (body
        (PrimeLpSynthCore ?context ?term ?type)))""",
    ),
    "disable-beta": (
        "(PrimeLpNormalizes ?function (Lam ?domain ?body))",
        "(PrimeLpNormalizes ?function (Pi ?domain ?body))",
    ),
    "disable-eta": (
        "(PrimeLpUnusedAt PrimeZero ?normal-function)",
        "(PrimeLpUsesAt PrimeZero ?normal-function)",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    marker, replacement = MUTATIONS[args.mutation]
    count = source.count(marker)
    if count != 1:
        raise SystemExit(
            f"mutation marker {args.mutation!r} occurs {count} times, expected 1"
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source.replace(marker, replacement), encoding="utf-8")


if __name__ == "__main__":
    main()
