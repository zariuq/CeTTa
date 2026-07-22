#!/usr/bin/env python3
import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    parser.add_argument("--gate", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--bypass-recognition", action="store_true")
    parser.add_argument("--drop-effect-demand", action="store_true")
    args = parser.parse_args()

    library = Path(args.library).read_text()
    if args.bypass_recognition:
        anchor = "(if (prime-meta:source-recognized $space $source)"
        if library.count(anchor) != 1:
            raise SystemExit("recognition mutation anchor must occur exactly once")
        library = library.replace(anchor, "(if True", 1)
    if args.drop_effect_demand:
        anchor = """(= (prime-meta:after-unit $effect $value)
   (case $effect
     ((() $value)
      ($effect-error $effect-error))))"""
        replacement = """(= (prime-meta:after-unit $_effect $value)
   $value)"""
        if library.count(anchor) != 1:
            raise SystemExit("effect-demand mutation anchor must occur exactly once")
        library = library.replace(anchor, replacement, 1)

    gate = Path(args.gate).read_text()
    Path(args.output).write_text(library + "\n" + gate)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
