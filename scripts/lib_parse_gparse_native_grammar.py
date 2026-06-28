#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module for the old Python native-grammar audit path."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_gparse_native_grammar.py is retired. "
    "Use the native MeTTa/C gparse entrypoints and tests directly; grammar "
    "loading authority now lives in the real parser lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
