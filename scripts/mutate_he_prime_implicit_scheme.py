#!/usr/bin/env python3
"""Create a mutant that implicitly generalizes unmarked open declarations."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mutate_he_prime_implicit_scheme.py INPUT OUTPUT", file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text()
    marker = "        if (!scheme && (atom_has_vars(term) || atom_has_vars(type))) continue;"
    if source.count(marker) != 1:
        print("mutation marker must occur exactly once", file=sys.stderr)
        return 2
    mutant = source.replace(marker, "        /* mutant: implicitly generalize every open declaration */")
    Path(sys.argv[2]).write_text(mutant)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
