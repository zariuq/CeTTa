#!/usr/bin/env python3

from __future__ import annotations

"""Retired Python integration syntax for lib_parse generalized parsing.

The implementation path is the native MeTTa/C lane: `lib/gparse.metta`,
`src/lib_parse_native_grammar.c`, and the `test-lib-parse-generalized` gate.
This file is kept only as a porting breadcrumb so old imports fail explicitly
instead of preserving a second parser-facing runtime.
"""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_gparse_native_runtime.py is retired. "
    "Use the MeTTa/C gparse API and `make test-lib-parse-generalized`; "
    "do not add a Python generalized parser runtime."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
