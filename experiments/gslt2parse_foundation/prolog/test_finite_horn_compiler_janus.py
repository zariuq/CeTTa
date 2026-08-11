#!/usr/bin/env python3
"""Run the finite-Horn compiler gate through the Janus Python boundary."""

from __future__ import annotations

from pathlib import Path
import sys

import janus_swi as janus


def main(arguments: list[str]) -> int:
    if len(arguments) != 4:
        raise SystemExit(
            "expected paths for syntax core, lookahead core, compiler, and language"
        )

    gate = Path(__file__).with_name("test_finite_horn_compiler.pl").resolve()
    janus.consult(str(gate))
    paths = [str(Path(argument).resolve()) for argument in arguments]
    result = janus.query_once(
        "test_finite_horn_compiler:run_compiler_gate(Paths, Total)",
        {"Paths": paths},
    )
    if result.get("truth") is not True or result.get("Total") != 8:
        raise RuntimeError(f"Janus compiler gate failed: {result!r}")
    print("(FiniteHornCompilerJanusCanarySummary 1 1 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
