#!/usr/bin/env python3
"""Create the bind-and-discard/implicit-formation Prime variable mutant."""

from pathlib import Path
import sys


def replace_once(source: str, marker: str, replacement: str) -> str:
    if source.count(marker) != 1:
        raise ValueError(f"mutation marker must occur exactly once: {marker}")
    return source.replace(marker, replacement)


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: mutate_prime_variable_discipline.py "
            "PRIME_INPUT PRIME_OUTPUT FRAGMENTS_INPUT FRAGMENTS_OUTPUT",
            file=sys.stderr,
        )
        return 2
    try:
        prime_source = Path(sys.argv[1]).read_text()
        prime_source = replace_once(
            prime_source,
            "        if (prime_var_is_bound(context, type->var_id)) {",
            "        if (true || prime_var_is_bound(context, type->var_id)) {",
        )
        fragments = Path(sys.argv[3]).read_text()
        # Bind and discard: an open expected type is accepted as if solved.
        fragments = replace_once(
            fragments,
            "            : prime_he_static_type(expected, &ok) && atom_eq(r.type, expected);",
            "            : atom_has_vars(expected) ||\n"
            "              (prime_he_static_type(expected, &ok) && atom_eq(r.type, expected));",
        )
        # Implicit generalization: rigid declaration variables become solvable.
        fragments = replace_once(
            fragments,
            "    if (atom_has_vars(type) && !prime_he_scheme_admitted(space, term)) {",
            "    if (false && atom_has_vars(type) && !prime_he_scheme_admitted(space, term)) {",
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(prime_source)
    Path(sys.argv[4]).write_text(fragments)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
