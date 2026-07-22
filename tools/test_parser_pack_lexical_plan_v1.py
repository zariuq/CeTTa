#!/usr/bin/env python3
"""Proof-carrying lexical-plan seal for the stable document splitter."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import tempfile

from gslt2parse_schema_v1 import render
from parser_pack_lr1_v1 import (
    analyze_lr1,
    composite_grammar,
    parse_abi_stream,
    parse_plan_stream,
    summary_json,
)
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_lexical_v1 import compile_case_tags
from test_regular_span_dfa_v1 import abi_scalar


ROOT = Path(__file__).resolve().parents[1]
REGULAR_COMPILER = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "compiler"
    / "regular_span_compiler_v1.metta"
)

LEXICAL_CASE = {
    "language": "petta-splitter",
    "tag": "petta-splitter-top-skip",
    "answers": 53,
    "answer_digest":
        "442696ab2205d0f50d8ba3d8d9efe82702d9b6f31f2e902ce02511b1d6cab551",
    "extra_tags": (
        (
            "petta-splitter-comment-line",
            15,
            "7e9349c1d9cc9c2200b7cc124be116c98f6d9a31c4ea985edd771406b3b671d6",
        ),
        (
            "petta-splitter-quoted",
            15,
            "c622b353701b18ef5c99bfe0be3c5339e3f851dbb260dd76e91d589743bde715",
        ),
    ),
}

EXPECTED_NATIVE = {
    "base-pack-digest":
        "369958a39dec4e1073388f74381ef5d8d6293792318232e0fe7b677ecc5c54f3",
    "base-productions": "73",
    "base-states": "61",
    "base-terminals": "11",
    "lexical-nfa-answer-digest":
        "2d98378b58198ff117bdb51a0c5e16219b8a41f5661ac6d50466851e7f823eab",
    "lexical-nfa-answers": "83",
    "lexical-plan-digest":
        "918b25000dbbec3e95955f7f1fd8671a6e48ec4ac7ea0bfdeb0d960669a2b60b",
    "lexical-tag-count": "3",
    "lexical-terminal-extension-count": "3",
    "projected-grammar-productions": "73",
    "slr-accepts": "1",
    "slr-conflicts": "0",
    "slr-gotos": "63",
    "slr-reductions": "122",
    "slr-shifts": "17",
    "slr-states": "46",
    "start-closed": "1",
}

EXPECTED_LR1 = {
    "accepts": 1,
    "conflict_cells": 0,
    "conflicts": 0,
    "gotos": 78,
    "grammar_digest":
        "85c448a0ddc7ceb20fd5c4cdf161e485a2d9c5de1489517a9ad93250bc2a4b7f",
    "max_state_items": 87,
    "nonterminals": 32,
    "productions": 73,
    "reachable_productions": 38,
    "reductions": 123,
    "shifts": 22,
    "states": 52,
    "table_digest":
        "19e97099b6f75bb6240b9e59aeba2563c8607d4ec3c1f46ad95d41910f0d3437",
    "terminals": 8,
}

EXPECTED_ENTRIES = (
    (
        0,
        5,
        0,
        "petta-splitter-comment-line",
        "(pp-def petta-splitter-comment-line)",
    ),
    (
        1,
        6,
        1,
        "petta-splitter-quoted",
        "(pp-def petta-splitter-quoted)",
    ),
    (
        2,
        8,
        2,
        "petta-splitter-top-skip",
        "(pp-def petta-splitter-top-skip)",
    ),
)

EXPECTED_MATRIX_DIGEST = (
    "514637f4704f3e766d7be36668491b63d15f361f53c5a943e57ba03be0c41260"
)


class GateFailure(RuntimeError):
    pass


def run_plan(
    binary: Path,
    abi_path: Path,
    nfa_path: Path,
    compiler_digest: str,
    *,
    expect_success: bool,
) -> str:
    completed = subprocess.run(
        [str(binary), str(abi_path), str(nfa_path), compiler_digest],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=60,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native lexical-plan construction failed: "
                + completed.stderr.strip()
            )
        return completed.stdout
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("corrupt lexical-plan input did not fail closed")
    return ""


def entry_rows(plan: dict[str, object]) -> tuple[tuple[object, ...], ...]:
    entries = plan.get("lexical_entries")
    if not isinstance(entries, list):
        raise GateFailure("lexical plan omitted its entry inventory")
    rows = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise GateFailure("lexical plan contains a malformed entry")
        rows.append((
            entry.get("tag_index"),
            entry.get("state_id"),
            entry.get("terminal_index"),
            render(entry["tag"]),
            render(entry["state"]),
        ))
    return tuple(rows)


def matrix_digest(native: dict[str, object], lr1: dict[str, object]) -> str:
    payload = (
        "ParserPackLexicalPlanPeTTaSplitterV1\n"
        f"{native['base-pack-digest']}\n"
        f"{native['lexical-plan-digest']}\n"
        f"{native['slr-states']}\n"
        f"{native['slr-conflicts']}\n"
        f"{lr1['grammar_digest']}\n"
        f"{lr1['table_digest']}\n"
    )
    return sha256(payload.encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--plan-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    chart_binary = arguments.chart_binary.resolve()
    plan_binary = arguments.plan_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not plan_binary.is_file():
        raise GateFailure("native lexical-plan binary is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa foundation checkout is absent")

    abi = export_stream(petta_root, ABI_CASES["petta-splitter"])
    compiler_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    lexical_terms, tags, compiler_agreements = compile_case_tags(
        chart_binary,
        petta_root,
        dict(LEXICAL_CASE),
        abi_scalar(abi, "source-digest"),
    )
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-petta-splitter-lexical-plan-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "splitter.abi"
        nfa_path = directory / "splitter.nfa"
        abi_path.write_bytes(abi)
        nfa_path.write_text(
            "\n".join(lexical_terms) + "\n", encoding="utf-8"
        )
        output = run_plan(
            plan_binary, abi_path, nfa_path, compiler_digest,
            expect_success=True,
        )
        native = parse_plan_stream(output)
        changed = {
            field: (expected, native.get(field))
            for field, expected in EXPECTED_NATIVE.items()
            if native.get(field) != expected
        }
        if changed:
            raise GateFailure(f"native lexical-plan seal changed: {changed}")
        if entry_rows(native) != EXPECTED_ENTRIES:
            raise GateFailure("native lexical-plan entry inventory changed")
        lr1 = summary_json(analyze_lr1(composite_grammar(
            parse_abi_stream(abi), native
        )))
        if lr1 != EXPECTED_LR1:
            raise GateFailure(f"canonical LR(1) seal changed: {lr1}")

        duplicate_path = directory / "duplicate.nfa"
        duplicate_path.write_text(
            "\n".join((lexical_terms[0], *lexical_terms)) + "\n",
            encoding="utf-8",
        )
        run_plan(
            plan_binary, abi_path, duplicate_path, compiler_digest,
            expect_success=False,
        )
        run_plan(
            plan_binary, abi_path, nfa_path, "not-a-sha256-digest",
            expect_success=False,
        )

    digest = matrix_digest(native, lr1)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"lexical-plan matrix digest changed: {digest}")
    print(
        "(ParserPackLexicalPlanPeTTaSplitterV1Summary "
        f"{len(lexical_terms)} {len(tags)} {compiler_agreements} "
        f"2 {native['slr-states']} {native['slr-conflicts']} "
        f"{lr1['states']} {lr1['conflicts']} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
