#!/usr/bin/env python3
"""Plant one occurrence-loss defect in the counted PathMap batch seam."""

from __future__ import annotations

import argparse
from pathlib import Path


FUNCTIONS = {
    "store-drop-last": "pathmap_local_store_atom_ids_batch_direct",
    "remove-drop-last": "pathmap_local_remove_atom_ids_batch_direct",
}

MARKER = """        &scratch, s->native.universe, atom_ids, atom_count,
        &packet, &packet_len, &encode_error);"""
MUTATION = """        &scratch, s->native.universe, atom_ids, atom_count - 1u,
        &packet, &packet_len, &encode_error);"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mutation", choices=sorted(FUNCTIONS))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    function = FUNCTIONS[args.mutation]
    start = source.find(f"static SpaceBackendBatchResult {function}(")
    if start < 0:
        raise SystemExit(f"mutation function {function!r} is missing")
    next_function = source.find("\nstatic ", start + 1)
    if next_function < 0:
        raise SystemExit(f"mutation function {function!r} has no closing boundary")
    body = source[start:next_function]
    if body.count(MARKER) != 1:
        raise SystemExit(
            f"mutation marker {args.mutation!r} occurs {body.count(MARKER)} times, expected 1"
        )

    mutant = source[:start] + body.replace(MARKER, MUTATION) + source[next_function:]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(mutant, encoding="utf-8")


if __name__ == "__main__":
    main()
