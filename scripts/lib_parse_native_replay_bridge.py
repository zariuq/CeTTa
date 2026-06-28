#!/usr/bin/env python3

from __future__ import annotations

"""Retired compatibility module for the old Python native replay audit path."""

import sys


RETIRED_MESSAGE = (
    "scripts/lib_parse_native_replay_bridge.py is retired. "
    "Use the native MeTTa/C shared-cert replay checks directly; replay "
    "authority now lives in the real parser lane."
)


def main() -> int:
    print(RETIRED_MESSAGE, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
