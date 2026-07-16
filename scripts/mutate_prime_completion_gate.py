#!/usr/bin/env python3
"""Forge evaluator closure for the Prime completion mutation test."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_prime_completion_gate.py INPUT OUTPUT", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text()
    marker = "out->closure_certified = outcome.completion == CETTA_EVAL_COMPLETE;"
    if source.count(marker) != 1:
        print("completion gate marker must occur exactly once", file=sys.stderr)
        return 2
    mutant = source.replace(marker, "out->closure_certified = true;")
    Path(sys.argv[2]).write_text(mutant)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
