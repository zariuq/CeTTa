#!/usr/bin/env python3
"""Reintroduce the retired 64-entry inferred-type frontier."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_prime_type_capacity.py INPUT OUTPUT",
              file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text()
    marker = ".type_capacity = 0,"
    if source.count(marker) != 1:
        print("dynamic type-storage marker must occur exactly once",
              file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(source.replace(
        marker, ".type_capacity = 64u,"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
