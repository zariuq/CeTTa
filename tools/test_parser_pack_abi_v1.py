#!/usr/bin/env python3
"""Cross-implementation gates for canonical ParserPack ABI streams."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"

CASES = {
    "petta-splitter": {
        "start": "petta-document",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/petta_document_splitter_scalar_classes_v1.metta",
            "languages/petta_document_splitter_v1.metta",
        ),
        "summary": (73, 24, 97, 1),
    },
    "petta": {
        "start": "petta-form",
        "closure": "partial",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/petta_form_reader_scalar_classes_v1.metta",
            "languages/petta_form_reader_v1.metta",
        ),
        "summary": (84, 31, 115, 0),
    },
    "he": {
        "start": "he-document",
        "closure": "partial",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/he_reader_scalar_classes_v1.metta",
            "languages/he_reader_v1.metta",
        ),
        "summary": (279, 144, 423, 0),
    },
    "prime": {
        "start": "cetta-prime-file",
        "closure": "partial",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/cetta_prime_scalar_classes_v1.metta",
            "languages/cetta_prime_reader_v1.metta",
        ),
        "summary": (179, 14, 193, 0),
    },
    "rho": {
        "start": "cetta-rho-document",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/char_core_v1.metta",
            "shared/cetta_rho_scalar_classes_v1.metta",
            "shared/cetta_rho_lexical_v1.metta",
            "languages/cetta_rho_reader_v1.metta",
        ),
        "summary": (292, 148, 440, 1),
    },
    "rho-mrho": {
        "start": "cetta-rho-mrho-document",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/he_reader_scalar_classes_v1.metta",
            "shared/rho_abstract_syntax_v1.metta",
            "languages/cetta_rho_mrho_reader_v1.metta",
        ),
        "summary": (125, 29, 154, 1),
    },
    "cost-rho": {
        "start": "cetta-cost-rho-document",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/ground_relations_v1.metta",
            "shared/char_core_v1.metta",
            "shared/cetta_rho_scalar_classes_v1.metta",
            "shared/cetta_rho_lexical_v1.metta",
            "languages/cetta_cost_rho_reader_v1.metta",
        ),
        "summary": (352, 148, 500, 1),
    },
    "metamath": {
        "start": "database",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "languages/metamath_appendix_e_v1.metta",
        ),
        "summary": (270, 375, 645, 1),
    },
    "megalodon": {
        "start": "mg-document",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/char_core_v1.metta",
            "languages/megalodon_dynamic_v1.metta",
        ),
        "summary": (138, 69, 209, 1),
    },
    "tptp": {
        "start": "tptp-file",
        "closure": "closed",
        "sources": (
            "core/syntax_core_v1.metta",
            "shared/lookahead_core_v1.metta",
            "shared/char_core_v1.metta",
            "languages/tptp_fof_cnf_v1.metta",
        ),
        "summary": (688, 562, 1250, 1),
    },
}

SUMMARY = re.compile(
    r"^\(ParserPackABIV1StreamSummary (\d+) (\d+) (\d+) ([01]) 0\)\n?$"
)


class GateFailure(RuntimeError):
    pass


def export_stream(
    petta_root: Path,
    case: dict,
    environment: dict[str, str] | None = None,
) -> bytes:
    exporter = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "parser_pack_abi_export_v1.pl"
    )
    completed = subprocess.run(
        [
            "swipl",
            "-q",
            "-f",
            str(exporter),
            "--",
            str(PRESENTATIONS),
            case["start"],
            case["closure"],
            *case["sources"],
        ],
        cwd=exporter.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=90,
        env=environment,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa ABI export failed: "
            + completed.stderr.decode("utf-8", errors="replace")
        )
    if not completed.stdout.startswith(b"parser-pack-abi-v1\n"):
        raise GateFailure("PeTTa ABI export omitted its canonical header")
    if not completed.stdout.endswith(b"end\n"):
        raise GateFailure("PeTTa ABI export omitted its end record")
    return completed.stdout


def run_native(binary: Path, stream: bytes, expect_success: bool) -> str:
    with tempfile.TemporaryDirectory(prefix="gslt2parse-parser-pack-abi-") as raw:
        path = Path(raw) / "pack.abi"
        path.write_bytes(stream)
        completed = subprocess.run(
            [str(binary), str(path)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=90,
        )
    if expect_success and completed.returncode != 0:
        raise GateFailure(
            f"native ABI load failed: {completed.stderr.strip()}"
        )
    if not expect_success:
        if completed.returncode == 0 or not completed.stderr.strip():
            raise GateFailure("corrupt ABI stream did not fail closed")
    return completed.stdout


def check_matrix(binary: Path, petta_root: Path) -> tuple[int, bytes]:
    passed = 0
    prime_stream = b""
    for label, case in CASES.items():
        stream = export_stream(petta_root, case)
        output = run_native(binary, stream, expect_success=True)
        match = SUMMARY.fullmatch(output)
        if not match:
            raise GateFailure(f"{label}: malformed native ABI summary: {output!r}")
        observed = tuple(int(value) for value in match.groups())
        if observed != case["summary"]:
            raise GateFailure(
                f"{label}: expected ABI summary {case['summary']}, got {observed}"
            )
        if label == "prime":
            prime_stream = stream
        passed += 1
    return passed, prime_stream


def replace_once(stream: bytes, old: bytes, new: bytes, label: str) -> bytes:
    if stream.count(old) < 1:
        raise GateFailure(f"mutation anchor missing: {label}")
    return stream.replace(old, new, 1)


def check_corruption(binary: Path, stream: bytes) -> int:
    digest_corrupt = re.sub(
        rb"(?m)^(pack-digest\t)[0-9a-f]",
        lambda match: match.group(1) + b"f",
        stream,
        count=1,
    )
    if digest_corrupt == stream:
        digest_corrupt = re.sub(
            rb"(?m)^(pack-digest\t)[0-9a-f]",
            lambda match: match.group(1) + b"0",
            stream,
            count=1,
        )
    if digest_corrupt == stream:
        raise GateFailure("pack-digest mutation did not change bytes")
    run_native(binary, digest_corrupt, expect_success=False)

    lines = stream.splitlines(keepends=True)
    for index, line in enumerate(lines):
        if line.startswith(b"production-evidence\t"):
            del lines[index]
            break
    else:
        raise GateFailure("production evidence mutation anchor missing")
    run_native(binary, b"".join(lines), expect_success=False)

    noncanonical = replace_once(
        stream,
        b"production\t(pp-production ",
        b"production\t(pp-production  ",
        "noncanonical term spacing",
    )
    run_native(binary, noncanonical, expect_success=False)
    return 3


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    binary = arguments.binary.resolve()
    petta_root = arguments.petta_root.resolve()
    matrix, prime_stream = check_matrix(binary, petta_root)
    corruptions = check_corruption(binary, prime_stream)
    print(f"(ParserPackABIV1MatrixSummary {matrix} {matrix} {corruptions} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
