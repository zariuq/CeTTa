#!/usr/bin/env python3
"""Disable the producer and replay occurs checks for the Prime soundness gate."""

from pathlib import Path
import sys


def replace_exact(source: str, marker: str, replacement: str, count: int = 1) -> str:
    if source.count(marker) != count:
        raise ValueError(
            f"mutation marker must occur exactly {count} time(s): {marker}"
        )
    return source.replace(marker, replacement)


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: mutate_prime_occurs_check.py "
            "MATCH_INPUT MATCH_OUTPUT HE_INPUT HE_OUTPUT",
            file=sys.stderr,
        )
        return 2
    try:
        match_source = Path(sys.argv[1]).read_text()
        match_source = replace_exact(
            match_source,
            "    if (!bindings_add_inplace_internal(&next, var_id, spelling, val,\n"
            "                                       true, false) ||\n"
            "        bindings_has_loop(&next)) {",
            "    if (!bindings_add_inplace_internal(&next, var_id, spelling, val,\n"
            "                                       true, false)) {",
        )

        he_source = Path(sys.argv[3]).read_text()
        he_source = replace_exact(
            he_source,
            "    if (bindings_has_loop(env)) return false;\n",
            "",
            count=2,
        )
        he_source = replace_exact(
            he_source,
            "        if (chain_unify_must(ctx, ts.items[i], goal, &trial, &resolved) &&\n"
            "            !bindings_has_loop(&trial)) {",
            "        if (chain_unify_must(ctx, ts.items[i], goal, &trial, &resolved)) {",
        )
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2

    Path(sys.argv[2]).write_text(match_source)
    Path(sys.argv[4]).write_text(he_source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
