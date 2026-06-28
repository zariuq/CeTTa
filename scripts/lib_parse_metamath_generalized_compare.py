#!/usr/bin/env python3

from __future__ import annotations

"""Retired comparison wrapper for the old Python parser harness."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_generalized_compare.py is retired. "
    "Metamath remains a benchmark corpus; parser implementation and comparison "
    "belong in the generic MeTTa/C gparse lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
