#!/usr/bin/env python3
"""Differential gate for the positive-lookahead ParserPack sidecar."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"

COMPILER = (
    "parserpack/parser_pack_core_v1.metta",
    "reflection/finite_horn_reflection_v1.metta",
    "compiler/syntax_compiler_v1.metta",
    "compiler/relational_cfg_lowering_v1.metta",
    "compiler/relational_cfg_link_v1.metta",
    "compiler/parser_pack_compiler_v1.metta",
    "compiler/parser_pack_guard_compiler_v1.metta",
)

CASES = {
    "he": (
        "core/syntax_core_v1.metta",
        "shared/lookahead_core_v1.metta",
        "shared/ground_relations_v1.metta",
        "shared/he_reader_scalar_classes_v1.metta",
        "languages/he_reader_v1.metta",
    ),
    "petta": (
        "core/syntax_core_v1.metta",
        "shared/lookahead_core_v1.metta",
        "shared/ground_relations_v1.metta",
        "shared/petta_form_reader_scalar_classes_v1.metta",
        "languages/petta_form_reader_v1.metta",
    ),
    "prime": (
        "core/syntax_core_v1.metta",
        "shared/lookahead_core_v1.metta",
        "shared/ground_relations_v1.metta",
        "shared/cetta_prime_scalar_classes_v1.metta",
        "languages/cetta_prime_reader_v1.metta",
    ),
    "canary": (
        "core/syntax_core_v1.metta",
        "shared/lookahead_core_v1.metta",
        "canaries/positive_guard_v1.metta",
    ),
}

EXPECTED_DIGESTS = {
    "he": "9640c3b9caacf354a8bd853765d9f15616e58d4a6b429af1c983bd90d185a02d",
    "petta": "a343ebd17f880a01428dc2b0a21f0eb758142eecdaee6934347be47283d4ece2",
    "prime": "f387c60f206ddea72b57aa09f127fb6e922b00a063dfe88cb4c3b97f9fdc7d6b",
    "canary": "177a93c6329859ae5ab0fdf45891e6be268cd5d4747628c68a8b553e9f79311b",
}

EXPECTED_COUNTS = {
    "he": 3,
    "petta": 1,
    "prime": 7,
    "canary": 3,
}

EXPECTED_PROVENANCE = {
    "he": {
        "source_digest": "ecf63c6d9c777f34e93d769af322b21bf2082c9e3d7850a4f6fb197e73539580",
        "pre_reflection_digest": "2d185650ca0a37ca952ce4ee5a0dcf3d425084efcdd0f93aebf28b131244385d",
        "environment_digest": "a847c6803f107b0f90a63873e306d1d948c799695986e501741673e8772e4ec7",
    },
    "petta": {
        "source_digest": "5f00a6f5734ec6babce012888dbef9642d7be1fcafcbc71f59c5b30bf2073d88",
        "pre_reflection_digest": "97b375b8b6e10a9120795420725baa97545055c99887ad8c95943f8136fe3126",
        "environment_digest": "3aa4efeeb2fb62a16b5462d746d7c6a49460bc2edeed15bde590127acd86a3a5",
    },
    "prime": {
        "source_digest": "9c39249e6f56edfff37766fecd63ecea2a2664eb2a920e27d74d47c3fbc3f1dd",
        "pre_reflection_digest": "84c4c49f968a26403fb7ae7eeb99ebcb45b6499b6c88d33ab8dda55d75dcb2b5",
        "environment_digest": "6d5b3e9cac3e5f1816062c269a4d7bfeb7148ecf1b210937cb879914f1dcfdce",
    },
    "canary": {
        "source_digest": "7e4f09fc2d85ae5c281a16daf9b9649210843bb09b0e5e3af8d85145dc421c26",
        "pre_reflection_digest": "570182be3cbe6658020ef8bf2d39e09733da9b2b2e13ecb2e208131f9ebfae23",
        "environment_digest": "21e4c4fc1d7f0796470f1f784b7a36a421805e0c1a4de75c1fc4ce18587fee33",
    },
}

EXPECTED_MATRIX_DIGEST = (
    "463d9964f4567ec14ae12779c77a4a0d74934cd70d3cdf76448d6162beb651f4"
)


class GateFailure(RuntimeError):
    pass


def program_arguments(label: str, sidecar: Path | None = None) -> list[str]:
    arguments: list[str] = []
    for relative in COMPILER:
        path = (
            sidecar
            if sidecar is not None
            and relative.endswith("parser_pack_guard_compiler_v1.metta")
            else PRESENTATIONS / relative
        )
        arguments.append(str(path))
    for relative in CASES[label]:
        arguments.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return arguments


def run_native(
    binary: Path,
    arguments: list[str],
    *,
    expected_code: int = 0,
) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        timeout=60,
    )
    if completed.returncode != expected_code:
        raise GateFailure(
            f"native guard compiler exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            f"native guard compiler emitted non-JSON output: {completed.stdout!r}"
        ) from error


def run_petta(petta_root: Path) -> dict:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_parser_pack_guard_compiler_v1.pl"
    )
    if not script.is_file():
        raise GateFailure(f"PeTTa guard oracle is absent: {script}")
    completed = subprocess.run(
        ["swipl", "-q", "-f", str(script), "--", str(PRESENTATIONS)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        timeout=60,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"PeTTa guard oracle exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            f"PeTTa guard oracle emitted non-JSON output: {completed.stdout!r}"
        ) from error


def native_rows(binary: Path) -> tuple[dict[str, dict], int]:
    rows: dict[str, dict] = {}
    certificate_count = 0
    query = "(compile-pack-positive-guard ?owner ?guard)"
    for label in CASES:
        result = run_native(
            binary,
            [
                *program_arguments(label),
                "--query-text",
                query,
                "--certs",
                "--timeout",
                "30",
            ],
        )
        expected_outcome = (
            "Unique" if EXPECTED_COUNTS[label] == 1 else "Ambiguous"
        )
        if (
            result.get("outcome") != expected_outcome
            or result.get("answers") != EXPECTED_COUNTS[label]
            or result.get("term_digest") != EXPECTED_DIGESTS[label]
            or not isinstance(result.get("terms"), list)
            or not isinstance(result.get("certs"), list)
            or len(result["terms"]) != EXPECTED_COUNTS[label]
            or len(result["certs"]) != EXPECTED_COUNTS[label]
        ):
            raise GateFailure(f"native {label} guard answer set changed")
        rows[label] = result
        certificate_count += len(result["certs"])
    return rows, certificate_count


def compare_petta(native: dict[str, dict], petta: dict) -> tuple[int, int, int]:
    if petta.get("protocol") != "parser-pack-positive-guard-compiler-v1":
        raise GateFailure("PeTTa guard protocol changed")
    rows = petta.get("rows")
    if not isinstance(rows, list):
        raise GateFailure("PeTTa guard rows are absent")
    indexed = {row.get("label"): row for row in rows if isinstance(row, dict)}
    agreements = 0
    for label, native_row in native.items():
        oracle = indexed.get(label)
        if (
            not oracle
            or oracle.get("count") != native_row.get("answers")
            or oracle.get("digest") != native_row.get("term_digest")
            or oracle.get("terms") != native_row.get("terms")
            or oracle.get("digest") != EXPECTED_DIGESTS[label]
        ):
            raise GateFailure(f"C and PeTTa guard answers differ for {label}")
        for field in (
            "source_digest",
            "pre_reflection_digest",
            "environment_digest",
        ):
            value = str(oracle.get(field, ""))
            if (
                not re.fullmatch(r"[0-9a-f]{64}", value)
                or value != EXPECTED_PROVENANCE[label][field]
            ):
                raise GateFailure(f"PeTTa {label} provenance changed at {field}")
        agreements += 1
    replay_count = petta.get("replay_count")
    negative_count = petta.get("negative_count")
    if replay_count != sum(EXPECTED_COUNTS.values()) or negative_count != 5:
        raise GateFailure("PeTTa guard replay or mutation inventory changed")
    return agreements, replay_count, negative_count


def replay_native_certificates(binary: Path, rows: dict[str, dict]) -> int:
    replayed = 0
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-positive-guard-certs-"
    ) as raw_temp:
        temp = Path(raw_temp)
        for label, row in rows.items():
            for index, term in enumerate(row["terms"]):
                certificate = temp / f"{label}-{index}.cert"
                emitted = run_native(
                    binary,
                    [
                        *program_arguments(label),
                        "--query-text",
                        term,
                        "--cert-out",
                        str(certificate),
                        "--timeout",
                        "30",
                    ],
                )
                if emitted.get("outcome") != "Unique" or not certificate.is_file():
                    raise GateFailure(f"native {label} guard certificate was not emitted")
                replay = run_native(
                    binary,
                    [
                        *program_arguments(label),
                        "--query-text",
                        term,
                        "--replay",
                        str(certificate),
                    ],
                )
                if replay.get("replay") != "ACCEPT":
                    raise GateFailure(f"native {label} guard certificate did not replay")
                replayed += 1
    return replayed


def mutation_gate(binary: Path) -> int:
    without_sidecar = [
        str(PRESENTATIONS / relative)
        for relative in COMPILER
        if not relative.endswith("parser_pack_guard_compiler_v1.metta")
    ]
    for relative in CASES["canary"]:
        without_sidecar.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    absent = run_native(
        binary,
        [
            *without_sidecar,
            "--query-text",
            "(compile-pack-positive-guard ?owner ?guard)",
            "--summary",
        ],
    )
    if absent.get("outcome") != "NoAnswer" or absent.get("answers") != 0:
        raise GateFailure("guard artifacts appeared without the sidecar compiler")

    source = PRESENTATIONS / "compiler" / "parser_pack_guard_compiler_v1.metta"
    text = source.read_text(encoding="utf-8")
    mutated = text.replace("(pa-slot q-zero)", "(pa-const q-zero)", 1)
    if mutated == text:
        raise GateFailure("guard action mutation did not alter the compiler")
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-positive-guard-mutation-"
    ) as raw_temp:
        path = Path(raw_temp) / source.name
        path.write_text(mutated, encoding="utf-8")
        result = run_native(
            binary,
            [
                *program_arguments("canary", path),
                "--query-text",
                "(compile-pack-positive-guard ?owner ?guard)",
                "--summary",
            ],
        )
        if result.get("term_digest") == EXPECTED_DIGESTS["canary"]:
            raise GateFailure("guard action mutation retained the sealed answer set")

    generic = source.read_text(encoding="utf-8").lower()
    if re.search(r"metamath|megalodon|tptp|cetta[-_ ]?prime|petta", generic):
        raise GateFailure("guest-language policy leaked into the guard compiler")
    return 3


def matrix_digest(rows: dict[str, dict]) -> str:
    payload = "ParserPackPositiveGuardCompilerV1\n"
    for label in sorted(rows):
        row = rows[label]
        payload += f"{label}\t{row['answers']}\t{row['term_digest']}\n"
    return sha256(payload.encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    petta_root = args.petta_root.resolve()
    if not binary.is_file():
        raise GateFailure(f"native chart binary is absent: {binary}")
    if not petta_root.is_dir():
        raise GateFailure(f"PeTTa root is absent: {petta_root}")

    rows, native_certificate_count = native_rows(binary)
    petta = run_petta(petta_root)
    agreements, petta_replays, petta_negatives = compare_petta(rows, petta)
    native_replays = replay_native_certificates(binary, rows)
    if (
        native_certificate_count != native_replays
        or native_replays != sum(EXPECTED_COUNTS.values())
    ):
        raise GateFailure("native guard certificate inventory changed")
    native_negatives = mutation_gate(binary)
    digest = matrix_digest(rows)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure("positive-guard compiler matrix digest changed")
    print(
        "(ParserPackPositiveGuardCompilerV1MatrixSummary "
        f"{len(rows)} {sum(row['answers'] for row in rows.values())} "
        f"{agreements} {native_replays + petta_replays} "
        f"{native_negatives + petta_negatives} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
