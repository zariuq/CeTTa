#!/usr/bin/env python3
"""Three-way execution gate for the guarded HE reader LanguageDef."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

from run_he_parser_oracle_v1 import authority_digest
from run_he_parser_oracle_v1 import authority_revision
from run_he_parser_oracle_v1 import build_oracle
from test_he_parser_authority_v1 import ATOM_CASES
from test_he_parser_authority_v1 import EXPECTED_REVISION
from test_he_parser_authority_v1 import EXPECTED_SOURCE_DIGEST
from test_he_parser_authority_v1 import run_case as run_he_case
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_plan_prime_v1 import REGULAR_COMPILER
from test_parser_pack_guard_plan_prime_v1 import PROFILES
from test_parser_pack_guard_plan_prime_v1 import evidence_stream
from test_parser_pack_guard_plan_prime_v1 import profile_guard_row
from test_parser_pack_guard_regular_v1 import answer_tag
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
PROFILE = PROFILES["he"]

EXPECTED_GUARD_PLAN_DIGEST = (
    "8b2ac9b156824617cb60bc324137e31afebb829fdb3c990275bb49d1130cc7ee"
)
EXPECTED_GUARD_EVIDENCE_DIGEST = (
    "d17d1eac27c5bae323f25b6e8a05a2ee63a014ade91f0d31d2f87535fda0edc0"
)
EXPECTED_MATRIX_DIGEST = (
    "824a1752f0950fcb07ef7171bca39c65366615c0c461d3da49331851197c114f"
)
EXPECTED_HOST_ACTION_MATRIX_DIGEST = (
    "ae7b2002aa7090892e85adf073f0be458c3fa0f0882d33a7c40c4029442e2b70"
)
EXPECTED_ACTION_REFERENCE_DIGEST = (
    "40a7b37232dc171b8a288922a7c83c60189eaac7788a72f49d032a3ad0e66589"
)


class GateFailure(RuntimeError):
    pass


def expected_decision(expected: tuple[tuple[object, ...], ...]) -> str:
    if any(value and value[0] == "error" for value in expected):
        return "rejected"
    return "accepted"


def authority_atom(value: tuple[object, ...]) -> dict[str, object]:
    kind = value[0]
    if kind in ("sym", "var") and len(value) == 2:
        return {"kind": kind, "value": value[1]}
    if kind == "expr" and len(value) == 2:
        children = value[1]
        if not isinstance(children, tuple):
            raise GateFailure("HE authority expression children changed shape")
        return {
            "kind": "expr",
            "children": [authority_atom(child) for child in children],
        }
    raise GateFailure(f"HE authority atom cannot be projected: {value!r}")


def parse_native_output(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "parser-pack-positive-guard-ref-exec-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("native guarded-reader stream has malformed framing")
    row: dict[str, object] = {"gll-result": [], "glr-result": []}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if len(fields) != 2:
            raise GateFailure(f"malformed native guarded-reader record: {line!r}")
        field, value = fields
        if field in ("gll-result", "glr-result"):
            values = row[field]
            assert isinstance(values, list)
            values.append(value)
        elif field in row:
            raise GateFailure(f"duplicate native guarded-reader field: {field}")
        else:
            row[field] = value
    return row


def run_native(
    binary: Path,
    abi_path: Path,
    guard_path: Path,
    evidence_path: Path,
    input_path: Path,
    compiler_digest: str,
    *,
    expect_success: bool,
) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(binary),
            str(abi_path),
            str(guard_path),
            str(evidence_path),
            str(input_path),
            compiler_digest,
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=180,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native guarded HE reader failed: " + completed.stderr.strip()
            )
        return parse_native_output(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("invalid guarded-reader input did not fail closed")
    return {}


def run_petta(
    petta_root: Path, input_path: Path, max_depth: int = 4096
) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "he_reader_gslt_reference_v1.pl"
    )
    completed = subprocess.run(
        [
            "swipl",
            "-q",
            "-f",
            str(script),
            "--",
            str(PRESENTATIONS),
            str(input_path),
            str(max_depth),
        ],
        cwd=script.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=180,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa HE LanguageDef reference failed: "
            + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa HE reference emitted non-JSON") from error
    if (
        not isinstance(row, dict)
        or row.get("protocol") != "he-reader-gslt-reference-v1"
        or not isinstance(row.get("host_documents"), list)
    ):
        raise GateFailure("PeTTa HE reference protocol changed")
    return row


def pin_authorities(petta_root: Path, he_root: Path) -> None:
    if authority_revision(he_root) != EXPECTED_REVISION:
        raise GateFailure("HE parser authority revision changed")
    if authority_digest(he_root) != EXPECTED_SOURCE_DIGEST:
        raise GateFailure("HE parser authority source changed")
    reference = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "he_reader_gslt_reference_v1.pl"
    )
    if sha256(reference.read_bytes()).hexdigest() != EXPECTED_ACTION_REFERENCE_DIGEST:
        raise GateFailure("HE host action reference changed")
    reference_text = reference.read_text(encoding="utf-8")
    if re.search(r"\bsread\s*\(|\bsexpr\s*\(|\bSExprParser\b", reference_text):
        raise GateFailure("HE host action reference reparses source text")


def require_native_seals(row: dict[str, object]) -> None:
    expected = {
        "base-pack-digest": PROFILE.base_pack_digest,
        "guard-plan-digest": EXPECTED_GUARD_PLAN_DIGEST,
        "guard-evidence-digest": EXPECTED_GUARD_EVIDENCE_DIGEST,
        "guard-nfa-answer-digest": PROFILE.guard_nfa_digest,
        "guard-nfa-answers": "55",
        "guard-tags": "3",
        "source-passes": "1",
        "gll-outcome": "completed",
        "glr-outcome": "completed",
    }
    for field, value in expected.items():
        if row.get(field) != value:
            raise GateFailure(
                f"native guarded HE seal changed at {field}: "
                f"expected {value!r}, observed {row.get(field)!r}"
            )
    if (
        row.get("gll-decision") != row.get("glr-decision")
        or row.get("gll-forest-digest") != row.get("glr-forest-digest")
        or row.get("gll-result") != row.get("glr-result")
        or row.get("guard-witness-runs") != row.get("guard-tokens")
        or row.get("guard-witness-matches") != row.get("guard-tokens")
        or re.fullmatch(
            r"[0-9a-f]{64}", str(row.get("guard-relation-digest", ""))
        )
        is None
    ):
        raise GateFailure("native GLL and GLR guarded HE executions differ")


def mutation_gates(
    binary: Path,
    abi_path: Path,
    guard_path: Path,
    evidence_path: Path,
    input_path: Path,
    compiler_digest: str,
    guard_terms: tuple[str, ...],
    directory: Path,
) -> int:
    first_tag = answer_tag(guard_terms[0])
    missing_tag_terms = tuple(
        term for term in guard_terms if answer_tag(term) != first_tag
    )
    missing_tag = directory / "he-guard-missing-tag.nfa"
    missing_tag.write_text(
        "\n".join(missing_tag_terms) + "\n", encoding="utf-8"
    )
    run_native(
        binary,
        abi_path,
        missing_tag,
        evidence_path,
        input_path,
        compiler_digest,
        expect_success=False,
    )

    unframed_evidence = directory / "he-guard-unframed.evidence"
    unframed_evidence.write_bytes(evidence_path.read_bytes().rstrip(b"\n"))
    run_native(
        binary,
        abi_path,
        guard_path,
        unframed_evidence,
        input_path,
        compiler_digest,
        expect_success=False,
    )

    wrong_compiler = run_native(
        binary,
        abi_path,
        guard_path,
        evidence_path,
        input_path,
        "0" * 64,
        expect_success=True,
    )
    try:
        require_native_seals(wrong_compiler)
    except GateFailure:
        pass
    else:
        raise GateFailure("outer provenance seal accepted a wrong compiler digest")
    return 3


def matrix_digest(records: list[dict[str, object]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--exec-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--he-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    exec_binary = arguments.exec_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    he_root = arguments.he_root.resolve()
    if not chart_binary.is_file() or not exec_binary.is_file():
        raise GateFailure("native guarded-reader binary is absent")
    if not petta_root.is_dir() or not he_root.is_dir():
        raise GateFailure("HE or PeTTa authority checkout is absent")
    pin_authorities(petta_root, he_root)

    he_oracle = build_oracle(he_root)
    compiler_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    abi = export_stream(petta_root, ABI_CASES["he"])
    guard_row = profile_guard_row(chart_binary, PROFILE)
    guard_nfa = guard_nfa_row(chart_binary, "he")
    guard_terms = tuple(str(term) for term in guard_nfa["terms"])

    records: list[dict[str, object]] = []
    host_records: list[dict[str, object]] = []
    accepted = 0
    rejected = 0
    authority_agreements = 0
    dual_native_agreements = 0
    semantic_agreements = 0
    host_action_agreements = 0
    invalid_fail_closed = 0
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-he-guard-exec-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "he.abi"
        guard_path = directory / "he-guard.nfa"
        evidence_path = directory / "he-guard.evidence"
        abi_path.write_bytes(abi)
        guard_path.write_text(
            "\n".join(guard_terms) + "\n", encoding="utf-8"
        )
        evidence_path.write_bytes(evidence_stream(PROFILE, guard_row))

        first_valid_input: Path | None = None
        for index, (label, (input_bytes, expected)) in enumerate(
            ATOM_CASES.items()
        ):
            input_path = directory / f"case-{index}.input"
            input_path.write_bytes(input_bytes)
            authority = run_he_case(he_oracle, "atoms", input_path)
            if authority != expected:
                raise GateFailure(f"HE authority changed for {label}")
            decision = expected_decision(expected)
            petta = run_petta(petta_root, input_path)
            if petta.get("decision") != decision:
                raise GateFailure(
                    f"HE authority and PeTTa LanguageDef differ for {label}"
                )
            authority_agreements += 1
            host_documents = petta["host_documents"]
            if decision == "accepted":
                expected_host_documents = [
                    [authority_atom(value) for value in authority]
                ]
                if host_documents != expected_host_documents:
                    raise GateFailure(
                        f"HE host action projection differs for {label}: "
                        f"{host_documents!r} != {expected_host_documents!r}"
                    )
                host_action_agreements += 1
            elif host_documents:
                raise GateFailure(
                    f"rejected HE input retained host actions for {label}"
                )
            host_records.append(
                {
                    "decision": decision,
                    "host_documents": host_documents,
                    "input": input_bytes.hex(),
                    "label": label,
                }
            )
            if decision == "accepted":
                accepted += 1
            else:
                rejected += 1

            if petta.get("decode") == "invalid-utf8":
                run_native(
                    exec_binary,
                    abi_path,
                    guard_path,
                    evidence_path,
                    input_path,
                    compiler_digest,
                    expect_success=False,
                )
                invalid_fail_closed += 1
                records.append(
                    {
                        "decision": decision,
                        "input": input_bytes.hex(),
                        "label": label,
                        "mode": "invalid-utf8",
                        "results": [],
                    }
                )
                continue

            native = run_native(
                exec_binary,
                abi_path,
                guard_path,
                evidence_path,
                input_path,
                compiler_digest,
                expect_success=True,
            )
            require_native_seals(native)
            if native.get("gll-decision") != decision:
                raise GateFailure(
                    f"HE authority and native guarded reader differ for {label}"
                )
            dual_native_agreements += 1
            native_results = native["gll-result"]
            petta_results = petta.get("results")
            if native_results != petta_results:
                raise GateFailure(
                    f"C and PeTTa HE semantics differ for {label}: "
                    f"{native_results!r} != {petta_results!r}"
                )
            semantic_agreements += 1
            if first_valid_input is None:
                first_valid_input = input_path
            records.append(
                {
                    "decision": decision,
                    "forest": native["gll-forest-digest"],
                    "input": input_bytes.hex(),
                    "label": label,
                    "mode": "valid-utf8",
                    "relation": native["guard-relation-digest"],
                    "results": native_results,
                }
            )

        if first_valid_input is None:
            raise GateFailure("HE matrix contains no valid input")
        mutations = mutation_gates(
            exec_binary,
            abi_path,
            guard_path,
            evidence_path,
            first_valid_input,
            compiler_digest,
            guard_terms,
            directory,
        )

    digest = matrix_digest(records)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            f"guarded HE execution matrix changed: {digest}"
        )
    host_digest = matrix_digest(host_records)
    if (
        EXPECTED_HOST_ACTION_MATRIX_DIGEST
        and host_digest != EXPECTED_HOST_ACTION_MATRIX_DIGEST
    ):
        raise GateFailure(f"HE host action matrix changed: {host_digest}")
    print(
        f"(HEReaderGuardExecV1Summary {len(records)} {accepted} {rejected} "
        f"{authority_agreements} {dual_native_agreements} "
        f"{semantic_agreements} {invalid_fail_closed + mutations} "
        f"{digest} 0)"
    )
    print(
        f"(HEReaderActionBridgeV1Summary {len(host_records)} "
        f"{host_action_agreements} {rejected} {host_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
