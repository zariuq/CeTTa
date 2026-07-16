#!/usr/bin/env python3
"""Reintroduce the retired 64-entry applicability frontier."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_prime_applicability_capacity.py INPUT OUTPUT",
              file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text()
    marker = (
        "static Bindings *applicability_bindings_push("
        "ApplicabilityBindings *vec) {\n"
        "    if (!vec || vec->len == UINT32_MAX) {")
    replacement = marker.replace("UINT32_MAX", "64u")
    if source.count(marker) != 1:
        print("dynamic applicability growth marker must occur exactly once",
              file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(source.replace(marker, replacement))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
