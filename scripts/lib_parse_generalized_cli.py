#!/usr/bin/env python3

from __future__ import annotations

"""Retired CLI for the old Python generalized parser integration surface."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_generalized_cli.py is retired. "
    "Use native MeTTa/C tests and entrypoints (`gparse`, "
    "`make test-lib-parse-generalized`) instead."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
