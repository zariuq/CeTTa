#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module for the old Python backend bridge."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_generalized_backend_runtime.py is retired. "
    "The backend contract is implemented through the MeTTa/C gparse lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
