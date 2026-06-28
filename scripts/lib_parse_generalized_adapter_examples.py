#!/usr/bin/env python3

from __future__ import annotations

"""Retired adapter examples for the old Python generalized parser path."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_generalized_adapter_examples.py is retired. "
    "Parser adapter examples should be expressed through MeTTa/C grammar and lexer tests."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
