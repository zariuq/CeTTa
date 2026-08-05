#!/usr/bin/env python3
"""Plant one correctness or optimization-contract defect in a PeTTa contender."""

from __future__ import annotations

import argparse
from pathlib import Path


MUTATIONS = {
    "specializer-reject-callable": (
        """        if (atom->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(
                context->space, &context->scratch,
                atom->sym_id)) {
            return PETTA_RELEVANCE_YES;
        }
""",
        """        if (atom->kind == ATOM_SYMBOL &&
            petta_symbol_is_callable(
                context->space, &context->scratch,
                atom->sym_id)) {
            return PETTA_RELEVANCE_NO;
        }
        """,
    ),
    "source-memo-ignore-arena-reset": (
        """        memo->source_arena != source_arena ||
        memo->source_arena_identity != source_arena->identity ||
        memo->source_arena_reset_epoch != source_arena->reset_epoch) {
""",
        """        memo->source_arena != source_arena ||
        memo->source_arena_identity != source_arena->identity) {
""",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    marker, replacement = MUTATIONS[args.mutation]
    count = source.count(marker)
    if count != 1:
        raise SystemExit(
            f"mutation marker {args.mutation!r} occurs {count} times, expected 1"
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(source.replace(marker, replacement), encoding="utf-8")


if __name__ == "__main__":
    main()
