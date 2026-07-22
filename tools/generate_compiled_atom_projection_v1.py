#!/usr/bin/env python3
"""Emit one declarative S-expression Atom projection as C data."""

from __future__ import annotations

import argparse
from pathlib import Path
import re

from gslt2parse_schema_v1 import render
from sexpr_atom_projection_plan_v1 import compile_artifact


def c_identifier(value: str) -> str:
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is None:
        raise ValueError(f"invalid C identifier: {value!r}")
    return value


def emit(
    profile: str,
    prefix: str,
    projection: Path,
    reader: Path,
) -> str:
    identifier = c_identifier(prefix)
    payload = render(
        compile_artifact(profile, projection, reader)
    ).encode("utf-8")
    guard = f"CETTA_GENERATED_{identifier.upper()}_H"
    rows = []
    for offset in range(0, len(payload), 16):
        row = payload[offset : offset + 16]
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in row) + ",")
    return "\n".join(
        [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "",
            "/* Generated from the declarative S-expression Atom projection. */",
            f"static const uint8_t {identifier}_bytes[] = {{",
            *rows,
            "};",
            f"static const size_t {identifier}_len = sizeof({identifier}_bytes);",
            "",
            f"#endif /* {guard} */",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--projection", type=Path, required=True)
    parser.add_argument("--reader", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        emit(
            arguments.profile,
            arguments.prefix,
            arguments.projection.resolve(),
            arguments.reader.resolve(),
        ),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
