#!/usr/bin/env python3

from __future__ import annotations

"""Retired adapter for the old Python rho parser harness."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_rho_token_adapter.py is retired. "
    "Rho tokenization for parser integration lives in MeTTa/C; Python may only "
    "transport native oracle data during porting."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
