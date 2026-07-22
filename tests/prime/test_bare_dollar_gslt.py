#!/usr/bin/env python3
"""Check both lexical contenders against the generated Prime GSLT reader."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess


def invoke(
    engine: Path,
    core: Path,
    presentation: Path,
    source: str,
    interpreted: bool,
) -> str:
    command = [
        str(engine),
        str(core),
        str(presentation),
        "--root",
        "metta-document",
        "--input-text",
        source,
    ]
    if interpreted:
        command.append("--interpreted")
    proc = subprocess.run(command, text=True, capture_output=True, check=False)
    if proc.returncode != 0:
        raise AssertionError(
            f"reader exited {proc.returncode}: {proc.stderr}\n{proc.stdout}"
        )
    result = json.loads(proc.stdout)
    if result.get("outcome") != "Unique" or len(result.get("asts", [])) != 1:
        raise AssertionError(f"reader did not produce one AST: {result}")
    return result["asts"][0]


def stable_ast(
    engine: Path, core: Path, presentation: Path, source: str
) -> str:
    packed = invoke(engine, core, presentation, source, False)
    interpreted = invoke(engine, core, presentation, source, True)
    if packed != interpreted:
        raise AssertionError(
            f"packed/interpreted AST mismatch for {source!r}:\n"
            f"packed={packed}\ninterpreted={interpreted}"
        )
    return packed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--symbol-presentation", type=Path, required=True)
    parser.add_argument("--variable-presentation", type=Path, required=True)
    args = parser.parse_args()

    symbol_leaf = "(symbol (cons (cp 36) nil))"
    variable_leaf = "(variable nil)"
    checks = 0
    for source, occurrences in (("$", 1), ("(P $ $)", 2)):
        symbol_ast = stable_ast(
            args.engine, args.core, args.symbol_presentation, source
        )
        variable_ast = stable_ast(
            args.engine, args.core, args.variable_presentation, source
        )
        if symbol_ast.count(symbol_leaf) != occurrences:
            raise AssertionError(f"symbol contender drifted for {source!r}: {symbol_ast}")
        if variable_leaf in symbol_ast:
            raise AssertionError(f"symbol contender emitted a variable for {source!r}")
        if variable_ast.count(variable_leaf) != occurrences:
            raise AssertionError(
                f"variable contender drifted for {source!r}: {variable_ast}"
            )
        if symbol_leaf in variable_ast:
            raise AssertionError(f"variable contender emitted a symbol for {source!r}")
        checks += 8

    print(f"(PrimeBareDollarGSLTSummary {checks} {checks} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
