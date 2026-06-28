#!/usr/bin/env python3

from __future__ import annotations

"""Retired audit wrapper for the old Python generalized parser path."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_generalized_audit.py is retired. "
    "Audit the native MeTTa/C parser lane through `make test-lib-parse-generalized`."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
