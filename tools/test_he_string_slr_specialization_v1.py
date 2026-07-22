#!/usr/bin/env python3
"""Certify deterministic native specialization of the HE string grammar."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
BUILDER = ROOT / "scripts" / "build_he_reader_gslt_v1.py"
sys.path.insert(0, str(ROOT / "tools"))

import test_parser_pack_abi_v1 as abi  # noqa: E402
from parser_pack_lr1_v1 import (  # noqa: E402
    analyze_lr1,
    parse_abi_stream,
    summary_json,
)


SUMMARY = re.compile(
    r"^\(ParserPackSLRSummaryV1 "
    r"(\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) 0\)$"
)
EXPECTED_NATIVE = (339, 394, 9, 353, 87, 439, 876, 1, 0)
EXPECTED_MUTANT = (341, 397, 9, 355, 87, 441, 878, 1, 1)
EXPECTED_LR1 = {
    "accepts": 1,
    "conflict_cells": 0,
    "conflicts": 0,
    "gotos": 451,
    "grammar_digest":
        "4086ffac5f80f9d1141efbec2b2e6ca48d02b0178d4b9251f11b22903695b71f",
    "max_state_items": 63,
    "nonterminals": 266,
    "productions": 394,
    "reachable_productions": 306,
    "reductions": 600,
    "shifts": 93,
    "states": 371,
    "table_digest":
        "88a754494ed3aaa72ab4ffdf9faa42cf8b16a3a13aa90cdca27e801a776fbadd",
    "terminals": 17,
}


class GateFailure(RuntimeError):
    pass


def generated_presentations(directory: Path, *, duplicate: bool) -> Path:
    presentations = directory / "presentations"
    shutil.copytree(PRESENTATIONS, presentations)
    subprocess.run(
        [
            sys.executable,
            str(BUILDER),
            "--classes",
            str(presentations / "shared" / "he_reader_scalar_classes_v1.metta"),
            "--reader",
            str(presentations / "languages" / "he_reader_v1.metta"),
        ],
        cwd=ROOT,
        check=True,
    )
    if duplicate:
        reader = presentations / "languages" / "he_reader_v1.metta"
        text = reader.read_text(encoding="utf-8")
        source = "(definition he-unicode-valid-body (ref he-unicode-state-000))"
        target = (
            "(definition he-unicode-valid-body "
            "(alt (ref he-unicode-state-000) "
            "(ref he-unicode-state-000)))"
        )
        if text.count(source) != 1:
            raise GateFailure("HE string ambiguity mutation anchor changed")
        reader.write_text(text.replace(source, target), encoding="utf-8")
    return presentations


def export_string_pack(petta_root: Path, presentations: Path) -> bytes:
    case = dict(abi.CASES["he"])
    case["start"] = "he-string"
    case["closure"] = "closed"
    original = abi.PRESENTATIONS
    try:
        abi.PRESENTATIONS = presentations
        return abi.export_stream(petta_root, case)
    finally:
        abi.PRESENTATIONS = original


def native_summary(
    binary: Path,
    directory: Path,
    stream: bytes,
    *,
    expect_success: bool,
) -> tuple[int, ...] | None:
    path = directory / "he-string.abi"
    path.write_bytes(stream)
    completed = subprocess.run(
        [str(binary), str(path)],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        if expect_success:
            raise GateFailure(
                "native HE string SLR summary failed: "
                + completed.stderr.strip()
            )
        return None
    if not expect_success:
        raise GateFailure("invalid HE string closure label was accepted")
    match = SUMMARY.fullmatch(completed.stdout.strip())
    if match is None:
        raise GateFailure("native HE string SLR summary changed shape")
    return tuple(int(field) for field in match.groups())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not binary.is_file() or not petta_root.is_dir():
        raise GateFailure("HE string specialization dependency is absent")

    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-he-string-specialization-"
    ) as raw:
        directory = Path(raw)
        candidate_dir = directory / "candidate"
        candidate_dir.mkdir()
        candidate_presentations = generated_presentations(
            candidate_dir, duplicate=False
        )
        candidate_stream = export_string_pack(
            petta_root, candidate_presentations
        )
        observed = native_summary(
            binary,
            candidate_dir,
            candidate_stream,
            expect_success=True,
        )
        if observed != EXPECTED_NATIVE:
            raise GateFailure(
                f"native HE string SLR seal changed: {observed!r}"
            )
        lr1 = summary_json(analyze_lr1(parse_abi_stream(candidate_stream)))
        if lr1 != EXPECTED_LR1:
            raise GateFailure(
                "HE string canonical LR(1) seal changed: "
                + json.dumps(lr1, sort_keys=True)
            )

        wrong_closure = candidate_stream.replace(
            b"closure\tclosed", b"closure\tpartial", 1
        )
        if wrong_closure == candidate_stream:
            raise GateFailure("HE string closure mutation anchor changed")
        native_summary(
            binary,
            candidate_dir,
            wrong_closure,
            expect_success=False,
        )

        mutant_dir = directory / "mutant"
        mutant_dir.mkdir()
        mutant_presentations = generated_presentations(
            mutant_dir, duplicate=True
        )
        mutant_stream = export_string_pack(petta_root, mutant_presentations)
        mutant = native_summary(
            binary,
            mutant_dir,
            mutant_stream,
            expect_success=True,
        )
        if mutant != EXPECTED_MUTANT:
            raise GateFailure(
                f"HE string ambiguity mutation changed: {mutant!r}"
            )

    print(
        "(HEStringSLRSpecializationV1Summary "
        f"{EXPECTED_NATIVE[3]} {EXPECTED_NATIVE[8]} "
        f"{EXPECTED_LR1['states']} {EXPECTED_LR1['conflicts']} "
        f"2 {EXPECTED_LR1['table_digest']} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
