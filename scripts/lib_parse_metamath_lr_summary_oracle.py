#!/usr/bin/env python3

from __future__ import annotations

"""Retired oracle wrapper from the old LR-specific Metamath parser pass."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metamath_lr_summary_oracle.py is retired. "
    "Use the native generalized parser gates and "
    "scripts/lib_parse_metamath_sealed_oracle.py for current oracle evidence."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
