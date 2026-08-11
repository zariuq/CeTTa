#!/usr/bin/env python3
"""Run the generic ParserPack GLR gate through PeTTa's Janus boundary."""

from __future__ import annotations

from pathlib import Path
import sys

import janus_swi as janus


EXPECTED_MATRIX_DIGEST = (
    "177187618bc421bd10d34677b1275dda0a6f95e380588748a48a4aa79348b69d"
)


def main(arguments: list[str]) -> int:
    if len(arguments) != 1:
        raise SystemExit("expected the canonical presentation directory")

    gate = Path(__file__).with_name("test_parser_pack_glr.pl").resolve()
    janus.consult(str(gate))
    presentation_root = str(Path(arguments[0]).resolve())
    result = janus.query_once(
        "test_parser_pack_glr:"
        "run_parser_pack_glr_gate(PresentationRoot, "
        "summary(Hand, Languages, Boundaries, Tables, Replays, Digest))",
        {"PresentationRoot": presentation_root},
    )
    if (
        result.get("truth") is not True
        or result.get("Hand") != 3
        or result.get("Languages") != 14
        or result.get("Boundaries") != 7
        or result.get("Tables") != 3
        or result.get("Replays") != 18
        or result.get("Digest") != EXPECTED_MATRIX_DIGEST
    ):
        raise RuntimeError(f"Janus ParserPack GLR failed: {result!r}")
    print("(ParserPackGLRJanusSummary 1 1 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
