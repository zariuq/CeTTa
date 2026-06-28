#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module for the old Python lexer audit path."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_metta_lexer_bridge.py is retired. "
    "Use native MeTTa/C lexer entrypoints and grammar-as-data tests directly; "
    "tokenization authority now lives in the real parser lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
