#!/usr/bin/env python3

from __future__ import annotations

"""Retired oracle wrapper for the old Python Metamath defs component ladder."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_defs_component_ladder.py is retired. "
    "Use make probe-lib-parse-metamath-defs-component-ladder or "
    "tests/test_gparse_native_metamath_defs_component_ladder.metta; the active "
    "defs component-ladder evidence now runs through the native generalized "
    "MeTTa/C parser lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
