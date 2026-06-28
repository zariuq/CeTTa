#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module from the old Python oracle payload path."""

import sys


RETIRED_MESSAGE = (
    "scripts/metta_payload_io.py is retired. Native MeTTa/C helpers now emit "
    "oracle payloads directly; do not reintroduce Python S-expression parsing "
    "for lib_parse integration."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
