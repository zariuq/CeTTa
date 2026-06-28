#!/usr/bin/env python3

from __future__ import annotations

"""Retired oracle wrapper for the old Python Metamath frontier probe."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_frontier_probe.py is retired. "
    "Use make probe-lib-parse-metamath-frontier or "
    "tests/test_gparse_native_metamath_frontier.metta; the active frontier "
    "evidence now runs through the native MeTTa/C gparse lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
