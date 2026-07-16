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
            "HE_INPUT HE_OUTPUT PRIME_INPUT PRIME_OUTPUT",
            file=sys.stderr,
        )
        return 2
    try:
        he_source = Path(sys.argv[1]).read_text()
        he_source = replace_once(
            he_source,
            "        HeEdge e = consistency(actual, expected, f);",
            "        Bindings discarded;\n"
            "        bindings_init(&discarded);\n"
            "        HeEdge e = consistency_bind(actual, expected, f, &discarded);\n"
            "        bindings_free(&discarded);",
        )
        prime_source = Path(sys.argv[3]).read_text()
        prime_source = replace_once(
            prime_source,
            "        if (prime_var_is_bound(context, type->var_id)) {",
            "        if (true || prime_var_is_bound(context, type->var_id)) {",
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    Path(sys.argv[2]).write_text(he_source)
    Path(sys.argv[4]).write_text(prime_source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
