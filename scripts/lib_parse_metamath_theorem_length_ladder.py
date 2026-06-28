#!/usr/bin/env python3

from __future__ import annotations

"""Retired oracle wrapper for the old Python Metamath theorem-length ladder."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_theorem_length_ladder.py is retired. "
    "Use make probe-lib-parse-metamath-theorem-length-ladder or "
    "tests/test_lib_parse_metamath_theorem_length_ladder_native.metta; the "
    "active theorem-length evidence now runs through the native MeTTa/C parser "
    "lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
