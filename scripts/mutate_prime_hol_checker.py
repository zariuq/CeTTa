#!/usr/bin/env python3
"""Create the unchecked-HE-declarations Prime mutant.

The mutant accepts every translated judgment without asking the hosted HOL
Light checker.  Prime's own source is copied unchanged so that it includes
the mutated fragment file.
"""

from pathlib import Path
import sys


def replace_once(source: str, marker: str, replacement: str) -> str:
    if source.count(marker) != 1:
        raise ValueError(f"mutation marker must occur exactly once: {marker}")
    return source.replace(marker, replacement)


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: mutate_prime_hol_checker.py "
            "PRIME_INPUT PRIME_OUTPUT FRAGMENTS_INPUT FRAGMENTS_OUTPUT",
            file=sys.stderr,
        )
        return 2
    try:
        fragments = Path(sys.argv[3]).read_text()
        fragments = replace_once(
            fragments,
            "    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_REQUEST);\n",
            "    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_HOL_GUEST_REQUEST);\n"
            "    if (commands) return PRIME_HOL_ACCEPTED;\n",
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(Path(sys.argv[1]).read_text())
    Path(sys.argv[4]).write_text(fragments)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
