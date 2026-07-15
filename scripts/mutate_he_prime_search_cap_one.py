#!/usr/bin/env python3
"""Create the premise-cap-one mutant used by the typed-search mutation gate."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_he_prime_search_cap_one.py INPUT OUTPUT", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text()
    marker = "                /* Every matching candidate is a retained continuation. */"
    if source.count(marker) != 1:
        print("mutation marker must occur exactly once", file=sys.stderr)
        return 2
    mutant = source.replace(marker, "                if (continuation) return true;")
    Path(sys.argv[2]).write_text(mutant)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
