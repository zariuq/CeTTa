#!/usr/bin/env python3
"""Run the relational CFG lowering gate through Janus."""

from __future__ import annotations

from pathlib import Path
import sys

import janus_swi as janus


def main(arguments: list[str]) -> int:
    if len(arguments) != 1:
        raise SystemExit("expected the canonical presentation directory")

    gate = Path(__file__).with_name("test_relational_cfg_lowering.pl").resolve()
    janus.consult(str(gate))
    presentation_root = str(Path(arguments[0]).resolve())
    result = janus.query_once(
        "test_relational_cfg_lowering:"
        "run_cfg_lowering_gate(PresentationRoot, "
        "summary(Positive, Negative, Mutations))",
        {"PresentationRoot": presentation_root},
    )
    if (
        result.get("truth") is not True
        or result.get("Positive") != 14
        or result.get("Negative") != 10
        or result.get("Mutations") != 6
    ):
        raise RuntimeError(f"Janus relational CFG lowering failed: {result!r}")
    print("(RelationalCFGLoweringJanusSummary 1 1 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
