#!/usr/bin/env python3

from __future__ import annotations

"""Retired comparison wrapper for the old Python rho parser harness."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_rho_generalized_compare.py is retired. "
    "Rho parser checks now run through the native MeTTa/C generalized parser gate."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
