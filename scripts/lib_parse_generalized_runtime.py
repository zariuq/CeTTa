#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module for the old Python generalized parser bridge."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_generalized_runtime.py is retired. "
    "The generalized parser lane lives in MeTTa/C, not Python."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
