#!/usr/bin/env python3

from __future__ import annotations

"""Retired Python prototype module.

The authoritative shared-witness / cert / replay path now lives in CeTTa/MeTTa+C.
This file remains only as a marker that the former Python witness implementation
has been intentionally removed from the active parser lane.
"""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_shared_witness.py is retired. "
    "The shared witness / cert / replay path lives in MeTTa/C, not Python."
)

__all__: list[str] = []


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
