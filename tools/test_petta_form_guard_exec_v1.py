#!/usr/bin/env python3
"""Three-way execution gate for the guarded PeTTa per-form reader."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile
from types import SimpleNamespace

from test_he_reader_guard_exec_v1 import matrix_digest
from test_he_reader_guard_exec_v1 import run_native
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_compiler_v1 import EXPECTED_DIGESTS
from test_parser_pack_guard_compiler_v1 import program_arguments
from test_parser_pack_guard_compiler_v1 import run_native as run_guard_native
from test_parser_pack_guard_plan_prime_v1 import REGULAR_COMPILER
from test_parser_pack_guard_plan_prime_v1 import evidence_stream
from test_parser_pack_guard_regular_v1 import answer_tag
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row
from test_petta_parser_authority_v1 import EXPECTED_PARSER_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_READER_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_REVISION
from test_petta_parser_authority_v1 import FORM_CASES
from test_petta_parser_authority_v1 import file_digest
from test_petta_parser_authority_v1 import revision
from test_petta_parser_authority_v1 import run_case as run_authority_case


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
PROFILE = SimpleNamespace(label="petta")

EXPECTED_BASE_PACK_DIGEST = (
    "ce5c19b1ff36844ad3f54e19595ae4a809035bf655e5dbfe6658ed37407095b8"
)
EXPECTED_GUARD_PLAN_DIGEST = (
    "e58b1113f3a3ab0247663055b227aa5675acb22e0ccd228e17ca07ca6a61a9eb"
)
EXPECTED_GUARD_EVIDENCE_DIGEST = (
    "7ea50aa72dec70f3b2c62dd6559b10308b7c5f03fb6a89afacdfda4ecc5e5d1e"
)
EXPECTED_GUARD_NFA_DIGEST = (
    "ed42222f9affc916ccbd1a27d8c5472a1e8dbc56e6238c91f7f36c18831a4d41"
)
EXPECTED_MATRIX_DIGEST = (
    "78c4d67b0607344b35de325a8bcc5f4b43ac4c25e671243eab2fb8871085eb58"
)
EXPECTED_HOST_ACTION_MATRIX_DIGEST = (
    "1b963618c89a1560ed88a0e8153e12b4eddcde21999872c4c92f511ef227e7fa"
)
EXPECTED_ACTION_REFERENCE_DIGEST = (
    "6c07e6efa0a6e60a9350c6463b65023c4d0a46cba310a39955ec968e3f12efd6"
)

HOST_ACTION_CASES = {
    "quoted-token-retains-internal-backslash": bytes.fromhex(
        "2822615c71625c2229"
    ),
}


class GateFailure(RuntimeError):
    pass


def expected_decision(expected: tuple[tuple[str, ...], ...]) -> str:
    if expected and expected[0][0] == "error":
        return "rejected"
    return "accepted"


def run_petta_reference(
    petta_root: Path, input_path: Path, max_depth: int = 4096
) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_form_gslt_reference_v1.pl"
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
            "PeTTa form LanguageDef reference failed: "
            + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa form reference emitted non-JSON") from error
    if (
        not isinstance(row, dict)
        or row.get("protocol") != "petta-form-gslt-reference-v1"
        or row.get("decode") != "valid-utf8"
        or row.get("outcome") != "completed"
        or not isinstance(row.get("host_results"), list)
        or not isinstance(row.get("host_classifications"), list)
    ):
        raise GateFailure("PeTTa form reference protocol changed")
    return row


def guard_row(chart_binary: Path) -> dict[str, object]:
    row = run_guard_native(
        chart_binary,
        [
            *program_arguments("petta"),
            "--query-text",
            "(compile-pack-positive-guard ?owner ?guard)",
            "--certs",
            "--timeout",
            "30",
        ],
    )
    if (
        row.get("outcome") != "Unique"
        or row.get("answers") != 1
        or row.get("term_digest") != EXPECTED_DIGESTS["petta"]
        or not isinstance(row.get("terms"), list)
        or not isinstance(row.get("certs"), list)
        or len(row["terms"]) != 1
        or len(row["certs"]) != 1
    ):
        raise GateFailure("native PeTTa form guard evidence changed")
    return row


def require_native_seals(row: dict[str, object]) -> None:
    expected = {
        "base-pack-digest": EXPECTED_BASE_PACK_DIGEST,
        "guard-plan-digest": EXPECTED_GUARD_PLAN_DIGEST,
        "guard-evidence-digest": EXPECTED_GUARD_EVIDENCE_DIGEST,
        "guard-nfa-answer-digest": EXPECTED_GUARD_NFA_DIGEST,
        "guard-nfa-answers": "10",
        "guard-tags": "1",
        "source-passes": "1",
        "gll-outcome": "completed",
        "glr-outcome": "completed",
    }
    changed = {
        field: (value, row.get(field))
        for field, value in expected.items()
        if row.get(field) != value
    }
    if changed:
        raise GateFailure(f"native guarded PeTTa seals changed: {changed}")
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
        raise GateFailure("native GLL and GLR guarded PeTTa executions differ")


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
    only_tag = answer_tag(guard_terms[0])
    without_tag = tuple(
        term for term in guard_terms if answer_tag(term) != only_tag
    )
    if without_tag:
        raise GateFailure("PeTTa form guard mutation retained its only tag")
    missing_tag = directory / "petta-form-guard-missing-tag.nfa"
    missing_tag.write_text("\n", encoding="utf-8")
    run_native(
        binary,
        abi_path,
        missing_tag,
        evidence_path,
        input_path,
        compiler_digest,
        expect_success=False,
    )

    unframed_evidence = directory / "petta-form-guard-unframed.evidence"
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
        raise GateFailure("outer PeTTa provenance seal accepted a wrong compiler")
    return 3


def pin_authority(petta_root: Path) -> None:
    if revision(petta_root) != EXPECTED_REVISION:
        raise GateFailure("PeTTa authority revision changed")
    if file_digest(petta_root / "src" / "parser.pl") != EXPECTED_PARSER_DIGEST:
        raise GateFailure("PeTTa per-form parser authority changed")
    if (
        file_digest(petta_root / "src" / "filereader.pl")
        != EXPECTED_READER_DIGEST
    ):
        raise GateFailure("PeTTa document reader authority changed")
    reference = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_form_gslt_reference_v1.pl"
    )
    if file_digest(reference) != EXPECTED_ACTION_REFERENCE_DIGEST:
        raise GateFailure("PeTTa host action reference changed")
    reference_text = reference.read_text(encoding="utf-8")
    if re.search(r"\bsread\s*\(|\bsexpr\s*\(", reference_text):
        raise GateFailure("PeTTa host action reference reparses source text")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--exec-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    exec_binary = arguments.exec_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not exec_binary.is_file():
        raise GateFailure("native guarded-reader binary is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa authority checkout is absent")
    pin_authority(petta_root)

    authority_oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "petta_parser_authority_v1.pl"
    )
    compiler_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    abi = export_stream(petta_root, ABI_CASES["petta"])
    evidence_row = guard_row(chart_binary)
    guard_nfa = guard_nfa_row(chart_binary, "petta")
    guard_terms = tuple(str(term) for term in guard_nfa["terms"])

    records: list[dict[str, object]] = []
    host_records: list[dict[str, object]] = []
    accepted = 0
    rejected = 0
    authority_agreements = 0
    dual_native_agreements = 0
    semantic_agreements = 0
    host_action_agreements = 0
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-petta-form-guard-exec-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "petta-form.abi"
        guard_path = directory / "petta-form-guard.nfa"
        evidence_path = directory / "petta-form-guard.evidence"
        abi_path.write_bytes(abi)
        guard_path.write_text(
            "\n".join(guard_terms) + "\n", encoding="utf-8"
        )
        evidence_path.write_bytes(evidence_stream(PROFILE, evidence_row))

        first_input: Path | None = None
        for index, (label, (input_bytes, expected)) in enumerate(
            FORM_CASES.items()
        ):
            input_path = directory / f"case-{index}.input"
            input_path.write_bytes(input_bytes)
            authority = run_authority_case(
                authority_oracle, petta_root, "form", input_path
            )
            if authority != expected:
                raise GateFailure(f"PeTTa authority changed for {label}")
            decision = expected_decision(expected)
            reference = run_petta_reference(petta_root, input_path)
            if reference.get("decision") != decision:
                raise GateFailure(
                    f"PeTTa authority and form LanguageDef differ for {label}"
                )
            authority_agreements += 1
            host_results = tuple(reference["host_results"])
            host_classifications = tuple(reference["host_classifications"])
            if decision == "accepted":
                authority_terms = tuple(
                    observation[1]
                    for observation in authority
                    if observation[0] == "term"
                )
                if (
                    len(authority_terms) != 1
                    or host_results != authority_terms
                    or len(host_classifications) != 1
                    or host_classifications[0]
                    not in ("function", "expression")
                ):
                    raise GateFailure(
                        f"PeTTa host action projection differs for {label}"
                    )
                host_action_agreements += 1
            elif host_results or host_classifications:
                raise GateFailure(
                    f"rejected PeTTa form retained host actions for {label}"
                )
            if decision == "accepted":
                accepted += 1
            else:
                rejected += 1

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
                    f"PeTTa authority and native guarded reader differ for {label}"
                )
            dual_native_agreements += 1
            native_results = native["gll-result"]
            if native_results != reference.get("results"):
                raise GateFailure(
                    f"C and PeTTa form semantics differ for {label}: "
                    f"{native_results!r} != {reference.get('results')!r}"
                )
            semantic_agreements += 1
            if first_input is None:
                first_input = input_path
            records.append(
                {
                    "decision": decision,
                    "forest": native["gll-forest-digest"],
                    "input": input_bytes.hex(),
                    "label": label,
                    "relation": native["guard-relation-digest"],
                    "results": native_results,
                }
            )
            host_records.append(
                {
                    "classifications": host_classifications,
                    "decision": decision,
                    "host_results": host_results,
                    "input": input_bytes.hex(),
                    "label": label,
                }
            )

        for offset, (label, input_bytes) in enumerate(
            HOST_ACTION_CASES.items(), start=len(FORM_CASES)
        ):
            input_path = directory / f"host-action-{offset}.input"
            input_path.write_bytes(input_bytes)
            authority = run_authority_case(
                authority_oracle, petta_root, "form", input_path
            )
            decision = expected_decision(authority)
            reference = run_petta_reference(petta_root, input_path)
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
            if (
                decision != "accepted"
                or reference.get("decision") != decision
                or native.get("gll-decision") != decision
                or native["gll-result"] != reference.get("results")
            ):
                raise GateFailure(f"PeTTa action canary differs for {label}")
            authority_terms = tuple(
                observation[1]
                for observation in authority
                if observation[0] == "term"
            )
            host_results = tuple(reference["host_results"])
            host_classifications = tuple(reference["host_classifications"])
            if (
                len(authority_terms) != 1
                or host_results != authority_terms
                or host_classifications != ("expression",)
            ):
                raise GateFailure(f"PeTTa action canary projection differs for {label}")
            host_action_agreements += 1
            host_records.append(
                {
                    "classifications": host_classifications,
                    "decision": decision,
                    "host_results": host_results,
                    "input": input_bytes.hex(),
                    "label": label,
                }
            )

        if first_input is None:
            raise GateFailure("PeTTa form matrix contains no input")
        mutations = mutation_gates(
            exec_binary,
            abi_path,
            guard_path,
            evidence_path,
            first_input,
            compiler_digest,
            guard_terms,
            directory,
        )

    digest = matrix_digest(records)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"guarded PeTTa form matrix changed: {digest}")
    host_digest = matrix_digest(host_records)
    if (
        EXPECTED_HOST_ACTION_MATRIX_DIGEST
        and host_digest != EXPECTED_HOST_ACTION_MATRIX_DIGEST
    ):
        raise GateFailure(f"PeTTa host action matrix changed: {host_digest}")
    print(
        f"(PeTTaFormGuardExecV1Summary {len(records)} {accepted} {rejected} "
        f"{authority_agreements} {dual_native_agreements} "
        f"{semantic_agreements} {mutations} {digest} 0)"
    )
    print(
        f"(PeTTaFormActionBridgeV1Summary {len(host_records)} "
        f"{host_action_agreements} {rejected} {host_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
