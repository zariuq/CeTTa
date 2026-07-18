#!/usr/bin/env python3
"""Turn resource omission back into a zero-fuel bounded search."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_prime_unbounded_search.py INPUT OUTPUT", file=sys.stderr)
        return 2

    source = Path(sys.argv[1]).read_text()
    marker = "    ctx.fuel_limited = fuel_limited;"
    if source.count(marker) != 1:
        print("unbounded-search mutation marker must occur exactly once", file=sys.stderr)
        return 2

    mutant = source.replace(marker, "    ctx.fuel_limited = true;")
    Path(sys.argv[2]).write_text(mutant)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
