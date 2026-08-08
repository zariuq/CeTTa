#!/usr/bin/env python3
"""Create a Prime chain mutant that drops repeated-name refinement."""

from pathlib import Path
import sys


MARKER = "TAIL_REENTER(refinement);"
MUTATION = (
    "(void)refinement;\n"
    "            TAIL_REENTER(body);"
)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_prime_abt_chain.py INPUT OUTPUT", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    if source.count(MARKER) != 1:
        print("Prime ABT chain mutation marker drifted", file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(
        source.replace(MARKER, MUTATION), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
