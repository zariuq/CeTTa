#!/usr/bin/env python3
"""Generate a same-head PeTTa clause-selection scaling witness."""

from __future__ import annotations

import argparse
from pathlib import Path


def render(width: int, repetitions: int) -> str:
    target = width // 2
    lines = [
        "; Generated wide-clause selection probe.",
        f"; width={width} repetitions={repetitions} target=key-{target}",
        "(wide:marker marker)",
    ]
    lines.extend(
        "(= (wide:pick key-{} $token) "
        "(match &self (wide:marker marker) value-{}))".format(index, index)
        for index in range(width)
    )
    lines.append("(= (wide:episode) (let* (")
    lines.extend(
        f"  ($ignored (wide:pick key-{target} token))"
        for _ in range(repetitions)
    )
    lines.extend((") done))", "!(wide:episode)"))
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--repetitions", type=int, default=1000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.width < 1:
        parser.error("--width must be positive")
    if args.repetitions < 1:
        parser.error("--repetitions must be positive")
    args.output.write_text(
        render(args.width, args.repetitions), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
