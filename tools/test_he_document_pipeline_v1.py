#!/usr/bin/env python3
"""Exact host-Atom differential for the prepared HE C document pipeline."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile
from typing import Any

from gslt2parse_schema_v1 import render
from he_reader_ratified_cases_v1 import RATIFIED_ATOM_CASES
from he_reader_ratified_cases_v1 import check_rust_observation
from run_he_parser_oracle_v1 import build_oracle
from sexpr_atom_projection_plan_v1 import compile_artifact
from test_he_parser_authority_v1 import run_case as run_he_case
from test_he_reader_guard_exec_v1 import expected_decision
from test_he_reader_guard_exec_v1 import matrix_digest
from test_he_reader_guard_exec_v1 import pin_authorities
from test_he_reader_guard_exec_v1 import run_petta
from test_he_reader_guarded_lexical_v1 import EXPECTED_EXECUTION_PLAN_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_CURSOR_CERTIFICATE_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_CURSOR_PROGRAM_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_GUARD_ACTION_ANSWER_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_GUARD_ACTION_COMPILER_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_GUARD_ACTION_PROGRAM_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_GUARDED_PLAN_DIGEST
from test_he_reader_guarded_lexical_v1 import GUARDED_COMPILER
from test_he_reader_guarded_lexical_v1 import guard_action_compiler_digest
from test_he_reader_guarded_lexical_v1 import PROFILE
from test_he_reader_guarded_lexical_v1 import native_guard_action_answers
from test_he_reader_guarded_lexical_v1 import native_guarded_answers
from test_he_reader_guarded_lexical_v1 import petta_guarded_answers
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_plan_prime_v1 import REGULAR_COMPILER
from test_parser_pack_guard_plan_prime_v1 import evidence_stream
from test_parser_pack_guard_plan_prime_v1 import profile_guard_row
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row
from test_parser_pack_lexical_v1 import compile_case_tags
from test_regular_span_dfa_v1 import abi_scalar
from test_semantic_mask_span_compiler_v1 import answer_digest as mask_answer_digest
from test_semantic_mask_span_compiler_v1 import EXPECTED_COMPILER_DIGEST as EXPECTED_SEMANTIC_MASK_COMPILER_DIGEST
from test_semantic_mask_span_compiler_v1 import EXPECTED_ROOT_LINK_DIGEST as EXPECTED_SEMANTIC_MASK_ROOT_LINK_DIGEST
from test_semantic_mask_span_compiler_v1 import EXPECTED_ROOT_LINKS as EXPECTED_SEMANTIC_MASK_ROOT_LINKS
from test_semantic_mask_span_compiler_v1 import MASK_CASES as SEMANTIC_MASK_CASES
from test_semantic_mask_span_compiler_v1 import native_query as native_semantic_mask_query
from test_semantic_mask_span_compiler_v1 import root_links_by_label
from test_semantic_mask_span_compiler_v1 import SEMANTIC_COMPILER
from test_semantic_mask_span_compiler_v1 import SEMANTIC_COMPILER_PREFIX


ROOT = Path(__file__).resolve().parents[1]
HEX_DIGEST = re.compile(r"[0-9a-f]{64}")
EXPECTED_MATRIX_DIGEST = (
    "b2b1dd78a8b4087df48ed71d2b09cfd4d52d6009cd6692e6857a5def8a4f23e1"
)
EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST = (
    "978a1efdabf631d4736d2b0cd7dee832cfe2c40a3d45f8b0eec97fc5b289e4a1"
)
EXPECTED_SEMANTIC_MASK_BINDING_DIGEST = (
    "be9e4de55893f3f7980493d07c10a218a7053b48fc34a3f42705c3b8da3b366e"
)
EXPECTED_MASK_EXECUTION = {
    "word": (1, 5, 5),
    "variable": (1, 2, 1),
    "repeated-variable": (3, 5, 3),
    "string-escapes": (1, 26, 8),
    "empty-expression": (0, 0, 0),
}


class GateFailure(RuntimeError):
    pass


def parse_pipeline_output(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "he-document-pipeline-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("HE document pipeline framing changed")
    row: dict[str, object] = {"atom": []}
    for line in lines[1:-1]:
        field, separator, value = line.partition("\t")
        if not separator:
            raise GateFailure("HE document pipeline record is malformed")
        if field == "atom":
            atoms = row[field]
            assert isinstance(atoms, list)
            try:
                atom = json.loads(value)
            except json.JSONDecodeError as error:
                raise GateFailure("HE document pipeline emitted malformed Atom JSON") from error
            if not isinstance(atom, dict):
                raise GateFailure("HE document pipeline Atom is not an object")
            atoms.append(atom)
        elif field in row:
            raise GateFailure(f"HE document pipeline duplicated {field}")
        else:
            row[field] = value
    return row


def run_pipeline(
    binary: Path,
    paths: tuple[Path, ...],
    projection_path: Path,
    input_path: Path,
    regular_digest: str,
    guarded_digest: str,
    action_digest: str,
    semantic_mask_digest: str,
    *,
    expect_success: bool,
) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(binary),
            *(str(path) for path in paths),
            str(projection_path),
            str(input_path),
            regular_digest,
            guarded_digest,
            action_digest,
            semantic_mask_digest,
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
                "prepared HE document pipeline failed: "
                + completed.stderr.strip()
            )
        return parse_pipeline_output(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("invalid prepared HE pipeline artifact did not fail closed")
    return {}


def compile_semantic_mask_artifact(
    chart_binary: Path,
) -> tuple[tuple[str, ...], str]:
    compiler_paths = (*SEMANTIC_COMPILER_PREFIX, SEMANTIC_COMPILER)
    terms: list[str] = []
    for label in ("word", "variable", "string"):
        row = native_semantic_mask_query(
            chart_binary,
            compiler_paths,
            ABI_CASES["he"]["sources"],
            f"(compile-semantic-mask-nfa {label} ?edge)",
        )
        observed = row.get("terms")
        expected = SEMANTIC_MASK_CASES[label]
        if (
            row.get("outcome") != "Ambiguous"
            or not isinstance(observed, tuple)
            or len(observed) != expected["answers"]
            or row.get("term_digest") != expected["mask_digest"]
        ):
            raise GateFailure(f"HE {label} semantic-mask artifact changed")
        terms.extend(observed)
    root_row = native_semantic_mask_query(
        chart_binary,
        compiler_paths,
        ABI_CASES["he"]["sources"],
        "(compile-semantic-mask-root-link ?name ?label)",
    )
    root_terms = root_row.get("terms")
    if (
        root_row.get("outcome") != "Ambiguous"
        or not isinstance(root_terms, tuple)
        or len(root_terms) != EXPECTED_SEMANTIC_MASK_ROOT_LINKS
        or root_row.get("term_digest") !=
            EXPECTED_SEMANTIC_MASK_ROOT_LINK_DIGEST
    ):
        raise GateFailure("HE semantic-mask root-link inventory changed")
    links = root_links_by_label(root_terms)
    for label in ("word", "variable", "string"):
        if label not in links:
            raise GateFailure(f"HE semantic-mask root link lacks {label}")
        terms.append(links[label])
    artifact = tuple(sorted(set(terms)))
    if len(artifact) != 745:
        raise GateFailure("HE semantic-mask artifact collapsed source answers")
    digest = mask_answer_digest(artifact)
    if digest != EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST:
        raise GateFailure("HE semantic-mask artifact digest changed")
    return artifact, digest


def canonical_authority_atom(
    value: tuple[Any, ...], variables: dict[str, int]
) -> dict[str, object]:
    kind = value[0]
    if kind == "sym" and len(value) == 2 and isinstance(value[1], str):
        return {"kind": "sym", "hex": value[1].encode("utf-8").hex()}
    if kind == "var" and len(value) == 2 and isinstance(value[1], str):
        spelling = value[1]
        if spelling not in variables:
            variables[spelling] = len(variables)
        return {
            "kind": "var",
            "ordinal": variables[spelling],
            "hex": spelling.encode("utf-8").hex(),
        }
    if kind == "expr" and len(value) == 2 and isinstance(value[1], tuple):
        return {
            "kind": "expr",
            "children": [
                canonical_authority_atom(child, variables)
                for child in value[1]
            ],
        }
    raise GateFailure(f"cannot canonicalize HE authority Atom: {value!r}")


def canonical_authority_document(
    document: tuple[tuple[Any, ...], ...]
) -> list[dict[str, object]]:
    return [canonical_authority_atom(atom, {}) for atom in document]


def canonical_reference_atom(
    value: object, variables: dict[str, int]
) -> dict[str, object]:
    if not isinstance(value, dict) or not isinstance(value.get("kind"), str):
        raise GateFailure("PeTTa host projection changed shape")
    kind = value["kind"]
    if kind in ("sym", "var"):
        spelling = value.get("value")
        if not isinstance(spelling, str):
            raise GateFailure("PeTTa host leaf omitted its spelling")
        result: dict[str, object] = {
            "kind": kind,
            "hex": spelling.encode("utf-8").hex(),
        }
        if kind == "var":
            if spelling not in variables:
                variables[spelling] = len(variables)
            result["ordinal"] = variables[spelling]
        return result
    if kind == "expr":
        children = value.get("children")
        if not isinstance(children, list):
            raise GateFailure("PeTTa host expression omitted its children")
        return {
            "kind": "expr",
            "children": [
                canonical_reference_atom(child, variables)
                for child in children
            ],
        }
    raise GateFailure(f"PeTTa host projection exposed unknown kind {kind!r}")


def canonical_reference_documents(value: object) -> list[list[dict[str, object]]]:
    if not isinstance(value, list):
        raise GateFailure("PeTTa host document set changed shape")
    documents: list[list[dict[str, object]]] = []
    for document in value:
        if not isinstance(document, list):
            raise GateFailure("PeTTa host document is not a list")
        documents.append(
            [canonical_reference_atom(atom, {}) for atom in document]
        )
    return documents


def symbol_text(value: object, context: str) -> str:
    text = getattr(value, "text", None)
    if not isinstance(text, str):
        raise GateFailure(f"{context} is not a symbolic digest")
    return text


def require_receipt(
    row: dict[str, object],
    expected_projection: dict[str, str],
    input_bytes: bytes,
    input_scalars: int,
) -> None:
    expected = {
        "base-pack-digest": PROFILE.base_pack_digest,
        "lexical-plan-digest": PROFILE.lexical_plan_digest,
        "guard-plan-digest": PROFILE.guard_plan_digest,
        "guarded-plan-digest": EXPECTED_GUARDED_PLAN_DIGEST,
        "execution-plan-digest": EXPECTED_EXECUTION_PLAN_DIGEST,
        "cursor-certificate-digest": EXPECTED_CURSOR_CERTIFICATE_DIGEST,
        "cursor-program-digest": EXPECTED_CURSOR_PROGRAM_DIGEST,
        "action-compiler-digest": EXPECTED_GUARD_ACTION_COMPILER_DIGEST,
        "action-answer-digest": EXPECTED_GUARD_ACTION_ANSWER_DIGEST,
        "action-program-digest": EXPECTED_GUARD_ACTION_PROGRAM_DIGEST,
        "projection-action-program-digest":
            "1b4e40b04f9f41068b0ad6d22586a253649c47963ada2d8268ef091462d7fb6c",
        "semantic-mask-compiler-digest":
            EXPECTED_SEMANTIC_MASK_COMPILER_DIGEST,
        "semantic-mask-answer-digest":
            EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST,
        "semantic-mask-binding-digest":
            EXPECTED_SEMANTIC_MASK_BINDING_DIGEST,
        "projection-action-productions": "282",
        "semantic-mask-bound-terminals": "3",
        "prepared-builds": "1",
        "source-passes": "1",
        "prepared-fallback-runs": "0",
        "input-bytes": str(len(input_bytes)),
        "input-scalars": str(input_scalars),
        "replay-agreement": "1",
        **expected_projection,
    }
    changed = {
        field: (value, row.get(field))
        for field, value in expected.items()
        if row.get(field) != value
    }
    if changed:
        raise GateFailure(f"prepared HE pipeline receipt changed: {changed}")
    for field in (
        "cursor-trace-digest",
    ):
        if HEX_DIGEST.fullmatch(str(row.get(field, ""))) is None:
            raise GateFailure(f"prepared HE pipeline omitted {field}")
    try:
        projected_tokens = int(str(row["projected-tokens"]))
        value_program_runs = int(str(row["value-program-runs"]))
        mask_runs = int(str(row["semantic-mask-runs"]))
        mask_input_scalars = int(str(row["semantic-mask-input-scalars"]))
        mask_retained_scalars = int(
            str(row["semantic-mask-retained-scalars"])
        )
        mask_work = int(str(row["semantic-mask-work"]))
    except (KeyError, ValueError) as error:
        raise GateFailure("prepared HE pipeline omitted semantic receipts") from error
    if (
        projected_tokens < 0
        or value_program_runs < 0
        or mask_runs < 0
        or mask_input_scalars < 0
        or mask_retained_scalars < 0
        or mask_work < 0
        or mask_runs + value_program_runs > projected_tokens
        or mask_input_scalars > input_scalars
        or mask_retained_scalars > mask_input_scalars
        or (mask_runs == 0) != (mask_input_scalars == 0)
        or (mask_runs == 0) != (mask_work == 0)
    ):
        raise GateFailure("prepared HE semantic-mask receipt is inconsistent")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--pipeline-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--he-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    pipeline_binary = arguments.pipeline_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    he_root = arguments.he_root.resolve()
    if not chart_binary.is_file() or not pipeline_binary.is_file():
        raise GateFailure("prepared HE pipeline binary dependency is absent")
    if not petta_root.is_dir() or not he_root.is_dir():
        raise GateFailure("HE or PeTTa authority checkout is absent")
    pin_authorities(petta_root, he_root)

    he_oracle = build_oracle(he_root)
    abi = export_stream(petta_root, ABI_CASES["he"])
    source_digest = abi_scalar(abi, "source-digest")
    lexical_terms, _, lexical_agreements = compile_case_tags(
        chart_binary,
        petta_root,
        dict(PROFILE.lexical_case),
        source_digest,
    )
    guard_row = profile_guard_row(chart_binary, PROFILE)
    guard_terms = tuple(
        str(term) for term in guard_nfa_row(chart_binary, "he")["terms"]
    )
    guarded_terms = native_guarded_answers(chart_binary)
    petta_source_digest, petta_guarded_terms = petta_guarded_answers(petta_root)
    if (
        guarded_terms != petta_guarded_terms
        or petta_source_digest != source_digest
    ):
        raise GateFailure("C and PeTTa prepared HE compiler artifacts differ")

    regular_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    guarded_digest = sha256(GUARDED_COMPILER.read_bytes()).hexdigest()
    action_terms = native_guard_action_answers(chart_binary)
    action_digest = guard_action_compiler_digest()
    semantic_mask_terms, semantic_mask_answer_digest = (
        compile_semantic_mask_artifact(chart_binary)
    )
    semantic_mask_compiler_digest = sha256(
        SEMANTIC_COMPILER.read_bytes()
    ).hexdigest()
    if (
        semantic_mask_answer_digest != EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST
        or semantic_mask_compiler_digest !=
            EXPECTED_SEMANTIC_MASK_COMPILER_DIGEST
    ):
        raise GateFailure("HE semantic-mask provenance baseline changed")
    projection_artifact = compile_artifact("he-v1")
    if not isinstance(projection_artifact, tuple) or len(projection_artifact) != 7:
        raise GateFailure("HE Atom projection artifact changed shape")
    projection_text = render(projection_artifact) + "\n"
    expected_projection = {
        "atom-projection-source-digest": symbol_text(
            projection_artifact[2], "projection source digest"
        ),
        "atom-projection-reader-digest": symbol_text(
            projection_artifact[3], "projection reader digest"
        ),
        "atom-projection-compiler-digest": symbol_text(
            projection_artifact[4], "projection compiler digest"
        ),
        "atom-projection-plan-digest": symbol_text(
            projection_artifact[5], "projection plan digest"
        ),
    }

    records: list[dict[str, object]] = []
    accepted = 0
    rejected = 0
    invalid_utf8 = 0
    authority_agreements = 0
    authority_disagreements = 0
    reference_agreements = 0
    atom_agreements = 0
    replay_agreements = 0
    input_paths_by_label: dict[str, Path] = {}

    with tempfile.TemporaryDirectory(prefix="gslt2parse-he-document-") as raw:
        directory = Path(raw)
        abi_path = directory / "he.abi"
        lexical_path = directory / "he-lexical.nfa"
        guard_path = directory / "he-guard.nfa"
        evidence_path = directory / "he-guard.evidence"
        guarded_path = directory / "he-guarded.nfa"
        action_path = directory / "he-actions.term"
        semantic_mask_path = directory / "he-semantic-mask.term"
        projection_path = directory / "he-atom-projection.term"
        abi_path.write_bytes(abi)
        lexical_path.write_text("\n".join(lexical_terms) + "\n", encoding="utf-8")
        guard_path.write_text("\n".join(guard_terms) + "\n", encoding="utf-8")
        evidence_path.write_bytes(evidence_stream(PROFILE, guard_row))
        guarded_path.write_text("\n".join(guarded_terms) + "\n", encoding="utf-8")
        action_path.write_text("\n".join(action_terms) + "\n", encoding="utf-8")
        semantic_mask_path.write_text(
            "\n".join(semantic_mask_terms) + "\n", encoding="utf-8"
        )
        projection_path.write_text(projection_text, encoding="utf-8")
        paths = (
            abi_path,
            lexical_path,
            guard_path,
            evidence_path,
            guarded_path,
            action_path,
            semantic_mask_path,
        )
        first_valid_input: Path | None = None

        for index, (label, (input_bytes, expected)) in enumerate(
            RATIFIED_ATOM_CASES.items()
        ):
            input_path = directory / f"case-{index}.input"
            input_path.write_bytes(input_bytes)
            input_paths_by_label[label] = input_path
            authority = run_he_case(he_oracle, "atoms", input_path)
            try:
                rust_relation = check_rust_observation(label, authority)
            except RuntimeError as error:
                raise GateFailure(str(error)) from error
            if rust_relation == "agrees":
                authority_agreements += 1
            else:
                authority_disagreements += 1
            reference = run_petta(petta_root, input_path)
            decision = expected_decision(expected)
            if reference.get("decision") != decision:
                raise GateFailure(
                    f"ratified HE contract and PeTTa decisions differ for {label}"
                )

            if reference.get("decode") == "invalid-utf8":
                run_pipeline(
                    pipeline_binary,
                    paths,
                    projection_path,
                    input_path,
                    regular_digest,
                    guarded_digest,
                    action_digest,
                    semantic_mask_compiler_digest,
                    expect_success=False,
                )
                invalid_utf8 += 1
                records.append(
                    {
                        "decision": decision,
                        "input": input_bytes.hex(),
                        "label": label,
                        "mode": "invalid-utf8-fail-closed",
                        "rust_relation": rust_relation,
                    }
                )
                continue

            try:
                row = run_pipeline(
                    pipeline_binary,
                    paths,
                    projection_path,
                    input_path,
                    regular_digest,
                    guarded_digest,
                    action_digest,
                    semantic_mask_compiler_digest,
                    expect_success=True,
                )
            except GateFailure as error:
                raise GateFailure(f"{label}: {error}") from error
            input_scalars = reference.get("input_scalars")
            if not isinstance(input_scalars, int):
                raise GateFailure("PeTTa reference omitted its scalar receipt")
            require_receipt(row, expected_projection, input_bytes, input_scalars)
            if label in EXPECTED_MASK_EXECUTION:
                observed_mask_execution = (
                    int(str(row["semantic-mask-runs"])),
                    int(str(row["semantic-mask-input-scalars"])),
                    int(str(row["semantic-mask-retained-scalars"])),
                )
                if observed_mask_execution != EXPECTED_MASK_EXECUTION[label]:
                    raise GateFailure(
                        f"HE semantic-mask execution changed for {label}: "
                        f"{observed_mask_execution!r}"
                    )
            replay_agreements += 1
            if row.get("decision") != decision:
                raise GateFailure(f"prepared C decision differs for {label}")

            if decision == "accepted":
                expected_atoms = canonical_authority_document(expected)
                atoms = row.get("atom")
                if atoms != expected_atoms:
                    raise GateFailure(
                        f"prepared C host Atoms differ for {label}: "
                        f"{atoms!r} != {expected_atoms!r}"
                    )
                if row.get("atom-count") != str(len(expected_atoms)):
                    raise GateFailure(f"prepared C Atom count differs for {label}")
                reference_documents = canonical_reference_documents(
                    reference.get("host_documents")
                )
                if reference_documents != [expected_atoms]:
                    raise GateFailure(f"PeTTa host Atoms differ for {label}")
                accepted += 1
                atom_agreements += 1
            else:
                if row.get("atom") != [] or row.get("atom-count") != "0":
                    raise GateFailure(f"rejected input retained host Atoms for {label}")
                if reference.get("host_documents") != []:
                    raise GateFailure(f"rejected PeTTa input retained Atoms for {label}")
                rejected += 1
            reference_agreements += 1
            if first_valid_input is None:
                first_valid_input = input_path
            records.append(
                {
                    "atoms": row["atom"],
                    "decision": decision,
                    "cursor_trace": row["cursor-trace-digest"],
                    "input": input_bytes.hex(),
                    "label": label,
                    "rust_relation": rust_relation,
                    "projected_tokens": row["projected-tokens"],
                }
            )

        if first_valid_input is None:
            raise GateFailure("prepared HE pipeline matrix has no valid input")
        plan_digest = expected_projection["atom-projection-plan-digest"]
        bad_digest_path = directory / "projection-bad-digest.term"
        bad_digest_path.write_text(
            projection_text.replace(plan_digest, "0" * 64, 1),
            encoding="utf-8",
        )
        wrong_profile_path = directory / "projection-wrong-profile.term"
        wrong_profile_path.write_text(
            projection_text.replace(
                "(sap-artifact-v1 he-v1 ",
                "(sap-artifact-v1 wrong-profile ",
                1,
            ),
            encoding="utf-8",
        )
        malformed_path = directory / "projection-malformed.term"
        malformed_path.write_text("(sap-artifact-v1)\n", encoding="utf-8")
        for mutation in (bad_digest_path, wrong_profile_path, malformed_path):
            run_pipeline(
                pipeline_binary,
                paths,
                mutation,
                first_valid_input,
                regular_digest,
                guarded_digest,
                action_digest,
                semantic_mask_compiler_digest,
                expect_success=False,
            )
        missing_action_path = directory / "actions-missing.term"
        missing_action_path.write_text(
            "\n".join(action_terms[:-1]) + "\n", encoding="utf-8"
        )
        malformed_action_path = directory / "actions-malformed.term"
        malformed_action_path.write_text("(not-action-bytecode)\n", encoding="utf-8")
        duplicate_action_path = directory / "actions-duplicate.term"
        duplicate_action_path.write_text(
            "\n".join((*action_terms, action_terms[0])) + "\n",
            encoding="utf-8",
        )
        for mutation in (
            missing_action_path,
            malformed_action_path,
            duplicate_action_path,
        ):
            run_pipeline(
                pipeline_binary,
                (*paths[:-2], mutation, paths[-1]),
                projection_path,
                first_valid_input,
                regular_digest,
                guarded_digest,
                action_digest,
                semantic_mask_compiler_digest,
                expect_success=False,
            )
        missing_mask_link_path = directory / "semantic-mask-missing-link.term"
        missing_mask_link = "(compile-semantic-mask-root-link he-word word)"
        if semantic_mask_terms.count(missing_mask_link) != 1:
            raise GateFailure("HE word semantic-mask root link changed shape")
        missing_mask_link_path.write_text(
            "\n".join(
                term for term in semantic_mask_terms
                if term != missing_mask_link
            ) + "\n",
            encoding="utf-8",
        )
        run_pipeline(
            pipeline_binary,
            (*paths[:-1], missing_mask_link_path),
            projection_path,
            first_valid_input,
            regular_digest,
            guarded_digest,
            action_digest,
            semantic_mask_compiler_digest,
            expect_success=False,
        )
        simple_wrapper = "(semantic-mask-wrapper simple-escape)"
        absent_wrapper = "(semantic-mask-wrapper absent-escape)"
        byte_wrapper = "(semantic-mask-wrapper byte-escape)"
        absent_wrapper_terms = tuple(
            term.replace(simple_wrapper, absent_wrapper)
            for term in semantic_mask_terms
        )
        wrong_wrapper_terms = tuple(
            term.replace(simple_wrapper, byte_wrapper)
            for term in semantic_mask_terms
        )
        if (
            absent_wrapper_terms == semantic_mask_terms
            or wrong_wrapper_terms == semantic_mask_terms
        ):
            raise GateFailure("HE simple-escape semantic action is absent")
        absent_wrapper_path = directory / "semantic-mask-absent-wrapper.term"
        absent_wrapper_path.write_text(
            "\n".join(absent_wrapper_terms) + "\n", encoding="utf-8"
        )
        run_pipeline(
            pipeline_binary,
            (*paths[:-1], absent_wrapper_path),
            projection_path,
            first_valid_input,
            regular_digest,
            guarded_digest,
            action_digest,
            semantic_mask_compiler_digest,
            expect_success=False,
        )
        wrong_wrapper_path = directory / "semantic-mask-wrong-wrapper.term"
        wrong_wrapper_path.write_text(
            "\n".join(wrong_wrapper_terms) + "\n", encoding="utf-8"
        )
        string_escape_input = input_paths_by_label.get("string-escapes")
        if string_escape_input is None:
            raise GateFailure("HE string-escape mutation witness is absent")
        run_pipeline(
            pipeline_binary,
            (*paths[:-1], wrong_wrapper_path),
            projection_path,
            string_escape_input,
            regular_digest,
            guarded_digest,
            action_digest,
            semantic_mask_compiler_digest,
            expect_success=False,
        )
        wrong_compiler_row = run_pipeline(
            pipeline_binary,
            paths,
            projection_path,
            first_valid_input,
            regular_digest,
            guarded_digest,
            "0" * 64,
            semantic_mask_compiler_digest,
            expect_success=True,
        )
        if (
            wrong_compiler_row.get("action-compiler-digest") != "0" * 64
            or wrong_compiler_row.get("cursor-program-digest")
            == EXPECTED_CURSOR_PROGRAM_DIGEST
        ):
            raise GateFailure(
                "action compiler identity did not alter the bound receipt"
            )
        wrong_semantic_compiler_row = run_pipeline(
            pipeline_binary,
            paths,
            projection_path,
            first_valid_input,
            regular_digest,
            guarded_digest,
            action_digest,
            "0" * 64,
            expect_success=True,
        )
        if (
            wrong_semantic_compiler_row.get("semantic-mask-compiler-digest")
            != "0" * 64
            or wrong_semantic_compiler_row.get("semantic-mask-binding-digest")
            == EXPECTED_SEMANTIC_MASK_BINDING_DIGEST
        ):
            raise GateFailure(
                "semantic-mask compiler identity did not alter the binding"
            )
        mutations = 11

    observed_digest = matrix_digest(records)
    if not EXPECTED_MATRIX_DIGEST:
        print("HE_DOCUMENT_PIPELINE_RECORDS=" + json.dumps(records, sort_keys=True))
        print("HE_DOCUMENT_PIPELINE_MATRIX=" + observed_digest)
        raise GateFailure("prepared HE pipeline matrix seal is not ratified")
    if observed_digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            "prepared HE pipeline matrix changed: " + observed_digest
        )
    print(
        f"(HEDocumentPipelineV1Summary {len(RATIFIED_ATOM_CASES)} {accepted} "
        f"{rejected} {invalid_utf8} {authority_agreements} "
        f"{authority_disagreements} "
        f"{reference_agreements} {atom_agreements} {replay_agreements} "
        f"{lexical_agreements} {mutations} {EXPECTED_MATRIX_DIGEST} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
