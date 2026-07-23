#!/usr/bin/env python3
"""Native HE ParserPack action-bytecode construction and execution gate."""

from __future__ import annotations

import argparse
from hashlib import sha256
from pathlib import Path
import subprocess
import tempfile

from test_finite_horn_chart_v1 import run_json
from test_parser_action_bytecode_compiler_v1 import (
    ACTION_CASES,
    action_program_arguments,
)
from test_parser_pack_abi_v1 import CASES as ABI_CASES, export_stream
from gslt2parse_schema_v1 import render
from sexpr_atom_projection_plan_v1 import compile_artifact


ROOT = Path(__file__).resolve().parents[1]
COMPILER = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "compiler"
    / "parser_action_bytecode_compiler_v1.metta"
)
EXPECTED = {
    "base-pack-digest":
        "a0334e22b53a8319b23870d008af3fcdd27426f26bffd09f14f54565c12936c0",
    "compiler-digest":
        "4ab357c261d8cf9b3fa9fe8ada3de7fde6263f463006bd81cbd6057e959450bd",
    "answer-set-digest":
        "4ea00d441b832d4d33fde53ea47ea8495c89da661821916634aedc9698957b8f",
    "program-digest":
        "7685da3feb748dcd704cbf2186659ecb8d3f77e9c21ba7d4eda679085e90a5f6",
    "productions": "279",
    "instructions": "383",
    "push-slots": "294",
    "push-constants": "37",
    "applications": "52",
    "max-stack": "4",
    "tree-bytecode-agreements": "279",
    "projection-action-program-digest":
        "33010e4e3445e829ef4307e6c063c57857483061f2fdf61ae53252b7b92dffde",
    "projection-action-agreements": "279",
    "projection-specialized-agreements": "279",
    "projection-interpreted-productions": "2",
    "projection-value-productions": "230",
    "projection-binary-productions": "47",
    "projection-pair-applications": "31",
    "projection-cons-applications": "12",
    "projection-node-applications": "9",
    "projection-action-mutations-killed": "4",
    "projection-document-agreements": "1",
    "mutations-killed": "7",
}


class GateFailure(RuntimeError):
    pass


def compiler_digest() -> str:
    observed = sha256(COMPILER.read_bytes()).hexdigest()
    if observed != EXPECTED["compiler-digest"]:
        raise GateFailure(
            "parser-action compiler source changed without a new receipt"
        )
    return observed


def compile_actions(chart_binary: Path) -> list[str]:
    case = ACTION_CASES["he"]
    expected_count, expected_digest = case["actions"]
    result = run_json(
        chart_binary,
        [
            *action_program_arguments(case),
            "--query-text",
            "(compile-pack-action-program "
            "?owner ?label ?arity ?action ?code)",
            "--timeout",
            "30",
        ],
    )
    terms = result.get("terms")
    if (
        result.get("outcome") != "Ambiguous"
        or result.get("answers") != expected_count
        or result.get("term_digest") != expected_digest
        or not isinstance(terms, list)
        or len(terms) != expected_count
        or not all(isinstance(term, str) for term in terms)
    ):
        raise GateFailure("HE action compiler answer set changed")
    return terms


def parse_receipt(output: str) -> dict[str, str]:
    lines = output.splitlines()
    if not lines or lines[0] != "parser-action-bytecode-v1":
        raise GateFailure("native action-bytecode receipt header changed")
    if lines[-1:] != ["end"]:
        raise GateFailure("native action-bytecode receipt terminator changed")
    receipt: dict[str, str] = {}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if len(fields) != 2 or fields[0] in receipt:
            raise GateFailure("native action-bytecode receipt is malformed")
        receipt[fields[0]] = fields[1]
    if receipt != EXPECTED:
        raise GateFailure(
            f"native action-bytecode receipt changed: {receipt!r}"
        )
    return receipt


def run_native(
    stream_binary: Path,
    abi_stream: bytes,
    action_terms: list[str],
    digest: str,
) -> dict[str, str]:
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-parser-action-bytecode-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "he.abi"
        actions_path = directory / "he.actions"
        projection_path = directory / "he.projection"
        abi_path.write_bytes(abi_stream)
        actions_path.write_text(
            "\n".join(action_terms) + "\n", encoding="utf-8"
        )
        projection_path.write_text(
            render(compile_artifact("he-v1")) + "\n", encoding="utf-8"
        )
        completed = subprocess.run(
            [
                str(stream_binary),
                str(abi_path),
                str(actions_path),
                digest,
                str(projection_path),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=90,
        )
    if completed.returncode != 0:
        raise GateFailure(
            "native action-bytecode gate failed: "
            f"{completed.stderr.strip()}"
        )
    return parse_receipt(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--stream-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    stream_binary = arguments.stream_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not stream_binary.is_file():
        raise GateFailure("required native action-bytecode binary is absent")
    digest = compiler_digest()
    action_terms = compile_actions(chart_binary)
    abi_stream = export_stream(petta_root, ABI_CASES["he"])
    receipt = run_native(
        stream_binary, abi_stream, action_terms, digest
    )
    print(
        "(ParserActionBytecodeV1NativeSummary "
        f"{receipt['productions']} {receipt['instructions']} "
        f"{receipt['push-slots']} {receipt['push-constants']} "
        f"{receipt['applications']} {receipt['max-stack']} "
        f"{receipt['tree-bytecode-agreements']} "
        f"{receipt['projection-action-agreements']} "
        f"{receipt['projection-specialized-agreements']} "
        f"{receipt['projection-interpreted-productions']} "
        f"{receipt['projection-value-productions']} "
        f"{receipt['projection-binary-productions']} "
        f"{receipt['projection-pair-applications']} "
        f"{receipt['projection-cons-applications']} "
        f"{receipt['projection-node-applications']} "
        f"{receipt['projection-action-mutations-killed']} "
        f"{receipt['projection-document-agreements']} "
        f"{receipt['mutations-killed']} "
        f"{receipt['base-pack-digest']} "
        f"{receipt['compiler-digest']} "
        f"{receipt['answer-set-digest']} "
        f"{receipt['program-digest']} "
        f"{receipt['projection-action-program-digest']} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
