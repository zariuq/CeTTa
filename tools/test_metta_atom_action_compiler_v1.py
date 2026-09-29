#!/usr/bin/env python3
"""Compare the actual authored compiler with typed atom-action exports."""

from __future__ import annotations

import argparse
from pathlib import Path

from gslt2parse_schema_v1 import Symbol, parse_sexprs, render
from test_finite_horn_chart_v1 import run_json
from test_parser_action_bytecode_compiler_v1 import ACTION_CASES, action_program_arguments


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    args = parser.parse_args()
    packets = parse_sexprs(args.fixtures.read_text(encoding="utf-8"))
    if not packets:
        raise ValueError("typed atom-action fixtures are empty")
    program = action_program_arguments(ACTION_CASES["he"])
    for packet in packets:
        if not isinstance(packet, tuple) or len(packet) != 7 or packet[0] not in (
            Symbol("GrammarConstructorActionCaseV1"),
            Symbol("GrammarConstructorActionRejectV1"),
        ):
            raise ValueError("malformed typed atom-action packet")
        action, expected_code = packet[3:5]
        result = run_json(args.chart_binary, [
            *program, "--query-text", f"(lower-pack-action {render(action)} ?code)",
            "--timeout", "30",
        ])
        terms = result.get("terms", [])
        if result.get("outcome") != "Unique" or len(terms) != 1:
            raise ValueError(f"authored compiler failed for {render(packet[1])}: {result}")
        expected = (Symbol("lower-pack-action"), action, expected_code)
        if parse_sexprs(terms[0]) != [expected]:
            raise ValueError(f"authored and typed code differ for {render(packet[1])}")
    print(f"(MeTTaAtomActionCompilerV1 cases={len(packets)} agreements={len(packets)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
