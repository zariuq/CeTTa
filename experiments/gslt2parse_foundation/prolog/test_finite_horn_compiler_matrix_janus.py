#!/usr/bin/env python3
"""Run the four-language compiler matrix through Janus."""

from __future__ import annotations

from pathlib import Path
import sys

import janus_swi as janus


def main(arguments: list[str]) -> int:
    if len(arguments) != 1:
        raise SystemExit("expected the canonical presentation directory")

    gate = Path(__file__).with_name(
        "test_finite_horn_compiler_matrix.pl"
    ).resolve()
    janus.consult(str(gate))
    presentation_root = str(Path(arguments[0]).resolve())
    result = janus.query_once(
        "test_finite_horn_compiler_matrix:"
        "run_compiler_matrix(PresentationRoot, Complete, Partial)",
        {"PresentationRoot": presentation_root},
    )
    if (
        result.get("truth") is not True
        or result.get("Complete") != 4
        or result.get("Partial") != 0
    ):
        raise RuntimeError(f"Janus compiler matrix failed: {result!r}")
    print("(FiniteHornCompilerMatrixJanusSummary 1 1 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
