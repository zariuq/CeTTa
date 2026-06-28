#!/usr/bin/env python3

from __future__ import annotations

"""Retired oracle wrapper for the old Python Metamath plus/weq variant matrix."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_plus_weq_variant_matrix.py is retired. "
    "Use make probe-lib-parse-metamath-plus-weq-variant-matrix or "
    "tests/test_gparse_native_metamath_plus_weq_variant_matrix.metta; the "
    "active plus/weq variant evidence now runs through the native generalized "
    "MeTTa/C parser lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
