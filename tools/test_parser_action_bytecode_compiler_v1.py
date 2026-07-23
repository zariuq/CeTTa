#!/usr/bin/env python3
"""Exact C/PeTTa differential for the GSLT parser-action compiler."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess

from test_finite_horn_chart_v1 import COMMON, PRESENTATIONS, CASES, run_json


EXPECTED_MATRIX_DIGEST = (
    "44fc543a0f265e5d978b9370114530a0e8855656c9c27e2042b991bc3a11f913"
)
EXPECTED_GUARD_MATRIX_DIGEST = (
    "05a6bfff40ee347c1131ea14c865679e6a14085585ee4c2137bfb66ad5c08844"
)
GUARD_ACTION_ROWS = {
    "extension": {
        "relation": "compile-positive-guard-action-program",
        "count": 3,
        "digest":
            "510667b98747982e7c56a4f8495d5014c6c6650f2ba3a0a67aece90dc029e1f0",
        "petta_count": "guard_extension_count",
        "petta_digest": "guard_extension_digest",
        "petta_terms": "guard_extension_terms",
    },
    "union": {
        "relation": "compile-guard-extended-action-program",
        "count": 282,
        "digest":
            "9c4ff108eb75369434f5058333b5d921c9da217884eeb8ea02ff26ad3d926df4",
        "petta_count": "guard_union_count",
        "petta_digest": "guard_union_digest",
        "petta_terms": "guard_union_terms",
    },
}

ACTION_CASES = {
    "he": {
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/he_reader_scalar_classes_v1.metta",
            "languages/he_reader_v1.metta",
        ),
        "actions": (
            279,
            "4ea00d441b832d4d33fde53ea47ea8495c89da661821916634aedc9698957b8f",
        ),
    },
    **CASES,
}


class GateFailure(RuntimeError):
    pass


def action_program_arguments(case: dict) -> list[str]:
    arguments = [str(PRESENTATIONS / relative) for relative in COMMON]
    for relative in case["sources"]:
        arguments.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return arguments


def guard_action_program_arguments() -> list[str]:
    arguments = [str(PRESENTATIONS / relative) for relative in COMMON]
    arguments.extend(
        str(PRESENTATIONS / relative)
        for relative in (
            "compiler/parser_pack_guard_compiler_v1.metta",
            "compiler/parser_pack_guard_action_link_v1.metta",
        )
    )
    for relative in ACTION_CASES["he"]["sources"]:
        arguments.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return arguments


def native_rows(binary: Path) -> tuple[dict[str, dict], int]:
    rows: dict[str, dict] = {}
    certificate_count = 0
    query = (
        "(compile-pack-action-program "
        "?owner ?label ?arity ?action ?code)"
    )
    for label, case in ACTION_CASES.items():
        expected_count, expected_digest = case["actions"]
        result = run_json(
            binary,
            [
                *action_program_arguments(case),
                "--query-text",
                query,
                "--certs",
                "--timeout",
                "30",
            ],
        )
        expected_outcome = "Unique" if expected_count == 1 else "Ambiguous"
        terms = result.get("terms")
        certificates = result.get("certs")
        if (
            result.get("outcome") != expected_outcome
            or result.get("answers") != expected_count
            or result.get("term_digest") != expected_digest
            or not isinstance(terms, list)
            or len(terms) != expected_count
            or not isinstance(certificates, list)
            or len(certificates) != expected_count
        ):
            raise GateFailure(f"native {label} action answer set changed")
        rows[label] = result
        certificate_count += len(certificates)
    return rows, certificate_count


def native_guard_rows(binary: Path) -> tuple[dict[str, dict], int]:
    rows: dict[str, dict] = {}
    certificate_count = 0
    for label, expected in GUARD_ACTION_ROWS.items():
        relation = expected["relation"]
        result = run_json(
            binary,
            [
                *guard_action_program_arguments(),
                "--query-text",
                f"({relation} ?owner ?label ?arity ?action ?code)",
                "--certs",
                "--timeout",
                "30",
            ],
        )
        terms = result.get("terms")
        certificates = result.get("certs")
        if (
            result.get("outcome") != "Ambiguous"
            or result.get("answers") != expected["count"]
            or result.get("term_digest") != expected["digest"]
            or not isinstance(terms, list)
            or len(terms) != expected["count"]
            or not isinstance(certificates, list)
            or len(certificates) != expected["count"]
        ):
            raise GateFailure(f"native guard-action {label} changed")
        rows[label] = result
        certificate_count += len(certificates)
    return rows, certificate_count


def run_petta(petta_root: Path, presentation_root: Path) -> dict:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_parser_action_bytecode_compiler_v1.pl"
    )
    if not script.is_file():
        raise GateFailure(f"PeTTa action compiler oracle is absent: {script}")
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(script), "--", str(presentation_root)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        timeout=120,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"PeTTa action compiler oracle exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            "PeTTa action compiler oracle emitted non-JSON output: "
            f"{completed.stdout!r}"
        ) from error


def compare_rows(native: dict[str, dict], petta: dict) -> int:
    if petta.get("protocol") != "parser-action-bytecode-compiler-v1":
        raise GateFailure("PeTTa action compiler protocol changed")
    raw_rows = petta.get("rows")
    if not isinstance(raw_rows, list):
        raise GateFailure("PeTTa action compiler rows are absent")
    petta_rows = {
        row.get("label"): row for row in raw_rows if isinstance(row, dict)
    }
    agreements = 0
    for label, native_row in native.items():
        row = petta_rows.get(label)
        expected_count, expected_digest = ACTION_CASES[label]["actions"]
        if (
            not row
            or row.get("count") != expected_count
            or row.get("digest") != expected_digest
            or row.get("count") != native_row.get("answers")
            or row.get("digest") != native_row.get("term_digest")
            or row.get("terms") != native_row.get("terms")
        ):
            raise GateFailure(f"C and PeTTa action answers differ for {label}")
        agreements += 1
    if set(petta_rows) != set(native):
        raise GateFailure("PeTTa action compiler matrix labels changed")
    return agreements


def compare_guard_rows(native: dict[str, dict], petta: dict) -> int:
    agreements = 0
    for label, native_row in native.items():
        expected = GUARD_ACTION_ROWS[label]
        if (
            petta.get(expected["petta_count"]) != expected["count"]
            or petta.get(expected["petta_digest"]) != expected["digest"]
            or petta.get(expected["petta_terms"]) != native_row.get("terms")
        ):
            raise GateFailure(
                f"C and PeTTa guard-action answers differ for {label}"
            )
        agreements += 1
    return agreements


def matrix_digest() -> str:
    payload = ["ParserActionBytecodeCompilerV1\n"]
    for label, case in ACTION_CASES.items():
        count, digest = case["actions"]
        payload.append(f"{label}\0{count}\0{digest}\n")
    return sha256("".join(payload).encode("utf-8")).hexdigest()


def guard_matrix_digest() -> str:
    payload = ["ParserPackGuardActionLinkV1\n"]
    for label, row in GUARD_ACTION_ROWS.items():
        payload.append(f"{label}\0{row['count']}\0{row['digest']}\n")
    return sha256("".join(payload).encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--presentation-root", type=Path, required=True)
    args = parser.parse_args()

    binary = args.binary.resolve()
    petta_root = args.petta_root.resolve()
    presentation_root = args.presentation_root.resolve()
    if not binary.is_file():
        raise GateFailure(f"native finite-Horn engine is absent: {binary}")
    if not presentation_root.is_dir():
        raise GateFailure(
            f"canonical presentation directory is absent: {presentation_root}"
        )

    native, native_certificates = native_rows(binary)
    guard_native, guard_native_certificates = native_guard_rows(binary)
    petta = run_petta(petta_root, presentation_root)
    agreements = compare_rows(native, petta)
    guard_agreements = compare_guard_rows(guard_native, petta)
    expected_answers = sum(
        case["actions"][0] for case in ACTION_CASES.values()
    )
    if (
        native_certificates != expected_answers
        or petta.get("replay_count") != expected_answers
        or petta.get("positive_count") != 1
        or petta.get("negative_count") != 2
    ):
        raise GateFailure("action compiler evidence or fragment gates changed")
    digest = matrix_digest()
    if digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure("action compiler matrix seal changed")
    guard_digest = guard_matrix_digest()
    if (
        guard_native_certificates != 285
        or petta.get("guard_replay_count") != 285
        or guard_digest != EXPECTED_GUARD_MATRIX_DIGEST
    ):
        raise GateFailure("guard-action compiler evidence or seal changed")
    print(
        "(ParserActionBytecodeCompilerV1DifferentialSummary "
        f"{agreements} {native_certificates} {petta['replay_count']} "
        f"{petta['positive_count']} {petta['negative_count']} {digest} 0)"
    )
    print(
        "(ParserPackGuardActionLinkV1DifferentialSummary "
        f"{guard_agreements} {guard_native_certificates} "
        f"{petta['guard_replay_count']} "
        f"{GUARD_ACTION_ROWS['extension']['count']} "
        f"{GUARD_ACTION_ROWS['extension']['digest']} "
        f"{GUARD_ACTION_ROWS['union']['count']} "
        f"{GUARD_ACTION_ROWS['union']['digest']} "
        f"{guard_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
