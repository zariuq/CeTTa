#!/usr/bin/env python3
"""Disable bind-time finite-tree checking for the Prime soundness gate."""

from pathlib import Path
import sys


def replace_exact(source: str, marker: str, replacement: str, count: int = 1) -> str:
    if source.count(marker) != count:
        raise ValueError(
            f"mutation marker must occur exactly {count} time(s): {marker}"
        )
    return source.replace(marker, replacement)


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: mutate_prime_occurs_check.py "
            "MATCH_INPUT MATCH_OUTPUT",
            file=sys.stderr,
        )
        return 2
    try:
        match_source = Path(sys.argv[1]).read_text()
        # Finite-tree checking now belongs to the shared binding kernel.
        # Refuse no bind, while leaving the cycle memo unknown so application
        # still uses its terminating traversal for malformed environments.
        match_source = replace_exact(
            match_source,
            "        BindingsReachability evidence) {\n"
            "    if (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC)",
            "        BindingsReachability evidence) {\n"
            "    bindings->cycle_state = BINDINGS_CYCLE_UNKNOWN;\n"
            "    return BINDINGS_BIND_ADMIT;\n"
            "    if (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC)",
        )

    except ValueError as error:
        print(error, file=sys.stderr)
        return 2

    Path(sys.argv[2]).write_text(match_source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
