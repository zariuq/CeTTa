#!/usr/bin/env python3
"""Create a Prime canonical-binder mutant that captures an unbound name."""

from pathlib import Path
import sys


def replace_once(source: str, marker: str, replacement: str) -> str:
    if source.count(marker) != 1:
        raise ValueError(f"mutation marker must occur exactly once: {marker}")
    return source.replace(marker, replacement)


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: mutate_prime_canonical_binders.py INPUT OUTPUT",
            file=sys.stderr,
        )
        return 2
    try:
        source = Path(sys.argv[1]).read_text()
        source = replace_once(
            source,
            "        return prime_canonical_scope_index(scope, type->var_id, &index)\n"
            "            ? prime_canonical_var(a, index) : NULL;",
            "        return prime_canonical_scope_index(scope, type->var_id, &index)\n"
            "            ? prime_canonical_var(a, index) : type;",
        )
        source = replace_once(
            source,
            "                  !atom_has_vars(canonical) &&\n",
            "                  true &&\n",
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
