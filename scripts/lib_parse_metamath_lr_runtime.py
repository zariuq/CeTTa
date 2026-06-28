#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module.

The former Python LR runtime implementation has been removed from the active
CeTTa lib_parse lane. Metamath parser benchmarking/oracle scripts now target
the native generalized runtime directly.
"""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_lr_runtime.py is retired. "
    "The lib_parse generalized parser lane lives in MeTTa/C, not Python."
)

__all__: list[str] = []


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
