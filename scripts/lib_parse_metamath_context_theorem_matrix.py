#!/usr/bin/env python3

from __future__ import annotations

"""Retired oracle wrapper for the old Python Metamath context-theorem matrix."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_context_theorem_matrix.py is retired. "
    "Use make probe-lib-parse-metamath-context-theorem-matrix or "
    "tests/test_gparse_native_metamath_context_theorem_matrix.metta; the "
    "active context-theorem matrix evidence now runs through the native "
    "generalized MeTTa/C parser lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
