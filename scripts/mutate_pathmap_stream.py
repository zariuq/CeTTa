#!/usr/bin/env python3
"""Make chain execution materialize its last join before publishing rows."""

from __future__ import annotations

import argparse
from pathlib import Path


MUTATIONS = (
    (
        "        uint32_t accum_steps = (nsteps > 1) ? nsteps - 1 : nsteps;\n",
        "        uint32_t accum_steps = nsteps;\n",
    ),
    (
        "        if (nsteps > 1 && ncur > 0) {\n",
        "        if (accum_steps < nsteps && ncur > 0) {\n",
    ),
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    # The accumulation frontier now includes every leg. The former last-leg
    # cursor must not run that leg twice; its completed frontier is published
    # by the existing materialized-body branch instead. Query and progress
    # reporting implementations remain intact.
    for marker, replacement in MUTATIONS:
        count = source.count(marker)
        if count != 1:
            raise SystemExit(
                f"last-leg stream marker occurs {count} times, expected 1"
            )
        source = source.replace(marker, replacement)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
