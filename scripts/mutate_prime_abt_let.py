#!/usr/bin/env python3
"""Create a Prime let mutant that reverses positional binder nesting."""

from pathlib import Path
import sys


MARKER = """for (uint32_t i = slots.len; i > 0u; i--) {
        Atom *closed = prime_abt_abstract(
            signature, arena, slots.names[i - 1u], scope);"""
MUTATION = """for (uint32_t i = 0u; i < slots.len; i++) {
        Atom *closed = prime_abt_abstract(
            signature, arena, slots.names[i], scope);"""


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_prime_abt_let.py INPUT OUTPUT", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    if source.count(MARKER) != 1:
        print("Prime ABT let mutation marker drifted", file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(
        source.replace(MARKER, MUTATION), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
