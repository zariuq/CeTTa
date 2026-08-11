#!/usr/bin/env python3
"""Run the generic ParserPack GLL gate through PeTTa's Janus boundary."""

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

    gate = Path(__file__).with_name("test_parser_pack_gll.pl").resolve()
    janus.consult(str(gate))
    presentation_root = str(Path(arguments[0]).resolve())
    result = janus.query_once(
        "test_parser_pack_gll:"
        "run_parser_pack_gll_gate(PresentationRoot, "
        "summary(Hand, Languages, Mutations, Replays, Digest))",
        {"PresentationRoot": presentation_root},
    )
    if (
        result.get("truth") is not True
        or result.get("Hand") != 3
        or result.get("Languages") != 14
        or result.get("Mutations") != 8
        or result.get("Replays") != 19
        or result.get("Digest") != EXPECTED_MATRIX_DIGEST
    ):
        raise RuntimeError(f"Janus ParserPack GLL failed: {result!r}")
    print("(ParserPackGLLJanusSummary 1 1 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
