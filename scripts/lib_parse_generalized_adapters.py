#!/usr/bin/env python3

from __future__ import annotations

"""Retired adapter loader for the old Python generalized parser path."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_generalized_adapters.py is retired. "
    "Language token adapters for parser integration must be MeTTa/C entrypoints."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
