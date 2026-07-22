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
        "35709e585896d9b3dcd4554e06ba0f0b684097bac1782163caa5dddc998f125f",
    "compiler-digest":
        "4ab357c261d8cf9b3fa9fe8ada3de7fde6263f463006bd81cbd6057e959450bd",
    "answer-set-digest":
        "81bb363ebd1cad73db03405344f5a9310e925ffce524e90bce505fd42802e69e",
    "program-digest":
        "42c439922caed3a4dfa4b72285c4c3ccc59daa7f81fc3330d6c918a43d0a6f40",
    "productions": "394",
    "instructions": "514",
    "push-slots": "431",
    "push-constants": "23",
    "applications": "60",
    "max-stack": "4",
    "tree-bytecode-agreements": "394",
    "projection-action-program-digest":
        "ecfed524ef705d258c25a9ea02e7e0c630483949d559e882acad50494919ddcc",
    "projection-action-agreements": "394",
    "projection-specialized-agreements": "394",
    "projection-interpreted-productions": "2",
    "projection-value-productions": "337",
    "projection-binary-productions": "55",
    "projection-pair-applications": "38",
    "projection-cons-applications": "13",
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
        or result.get("answers") != 394
        or result.get("term_digest") != EXPECTED["answer-set-digest"]
        or not isinstance(terms, list)
        or len(terms) != 394
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
