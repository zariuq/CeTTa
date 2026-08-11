#!/usr/bin/env python3
"""Run the ParserPack compiler gate through Janus."""

from __future__ import annotations

from pathlib import Path
import sys

import janus_swi as janus


def main(arguments: list[str]) -> int:
    if len(arguments) != 1:
        raise SystemExit("expected the canonical presentation directory")

    gate = Path(__file__).with_name("test_parser_pack_compiler.pl").resolve()
    janus.consult(str(gate))
    presentation_root = str(Path(arguments[0]).resolve())
    result = janus.query_once(
        "test_parser_pack_compiler:"
        "run_parser_pack_gate(PresentationRoot, "
        "summary(Languages, Closed, Partial, Semantic, Mutations))",
        {"PresentationRoot": presentation_root},
    )
    if (
        result.get("truth") is not True
        or result.get("Languages") != 4
        or result.get("Closed") != 3
        or result.get("Partial") != 1
        or result.get("Semantic") != 10
        or result.get("Mutations") != 13
    ):
        raise RuntimeError(f"Janus ParserPack compiler failed: {result!r}")
    print("(ParserPackCompilerJanusSummary 1 1 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
