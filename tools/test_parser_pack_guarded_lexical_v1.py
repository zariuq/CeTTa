#!/usr/bin/env python3
"""Proof-carrying trailing-context projection for the stable PeTTa form reader."""

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
from probe_prepared_final_forest_v1 import FinalForestProbeFailure
from probe_prepared_final_forest_v1 import FlatReplaySummary
from probe_prepared_final_forest_v1 import TOKEN_COUNTS
from probe_prepared_final_forest_v1 import run_growth_probe
from probe_prepared_final_forest_v1 import run_flat_replay_canary
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_plan_prime_v1 import evidence_stream
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row
from test_parser_pack_lexical_v1 import compile_case_tags
from test_petta_form_guard_exec_v1 import PROFILE
from test_petta_form_guard_exec_v1 import expected_decision
from test_petta_form_guard_exec_v1 import guard_row
from test_petta_form_guard_exec_v1 import pin_authority
from test_petta_form_guard_exec_v1 import run_petta_reference
from test_petta_parser_authority_v1 import FORM_CASES
from test_petta_parser_authority_v1 import run_case as run_authority_case
from test_regular_span_dfa_v1 import abi_scalar


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
REGULAR_COMPILER = (
    PRESENTATIONS / "compiler" / "regular_span_compiler_v1.metta"
)
GUARDED_COMPILER = (
    PRESENTATIONS
    / "compiler"
    / "guarded_regular_span_compiler_v1.metta"
)

LEXICAL_CASE = {
    "language": "petta",
    "tag": "petta-string",
    "answers": 28,
    "answer_digest":
        "f39aaa17aebd57d4219f8280c7d3551f3d0bc14c6f928d425486e0aad900fae0",
    "extra_tags": ((
        "petta-skip",
        7,
        "20664a3dc97bc27969788fc82700e0edb0ac13cd6cf7eabbbd542720ee613a7f",
    ),),
}

EXPECTED_GUARDED_ANSWERS = 67
EXPECTED_GUARDED_DIGEST = (
    "9d28563440ac2bcaa62b1bd1e015bb9e9b1319aefffc9ee927048f1f6438f5ae"
)
EXPECTED_NATIVE = {
    "base-pack-digest":
        "ce5c19b1ff36844ad3f54e19595ae4a809035bf655e5dbfe6658ed37407095b8",
    "base-productions": "84",
    "base-states": "71",
    "base-terminals": "13",
    "regular-compiler-digest":
        "3fc737afb6df4e45dc19dd50597694297b4dd672965ef12197a0a64c492d9189",
    "lexical-nfa-answer-digest":
        "d1eef1ec04272183187215019f3babc5b94599b210ae643004473108967f4c39",
    "lexical-nfa-answers": "35",
    "lexical-tag-count": "2",
    "lexical-plan-digest":
        "5f826f37f49ecf89e7b7c6b0f48f857eba9f88ad6940ce3e439f1fc987005c3d",
    "guard-source-digest":
        "5f00a6f5734ec6babce012888dbef9642d7be1fcafcbc71f59c5b30bf2073d88",
    "guard-pre-reflection-digest":
        "97b375b8b6e10a9120795420725baa97545055c99887ad8c95943f8136fe3126",
    "guard-environment-digest":
        "3aa4efeeb2fb62a16b5462d746d7c6a49460bc2edeed15bde590127acd86a3a5",
    "guard-answer-set-digest":
        "a343ebd17f880a01428dc2b0a21f0eb758142eecdaee6934347be47283d4ece2",
    "guard-evidence-count": "1",
    "guard-nfa-answer-digest":
        "ed42222f9affc916ccbd1a27d8c5472a1e8dbc56e6238c91f7f36c18831a4d41",
    "guard-nfa-answers": "10",
    "guard-tag-count": "1",
    "guard-plan-digest":
        "7814a2e726fb3e62519fbe57940ce0cacc7d83b4f5fe77f6487b6d4631006659",
    "guard-evidence-digest":
        "a5e7b8da5050b20fc55b36a1f10135b94562143f818a2b2d6f73f8860fb63511",
    "guarded-compiler-digest":
        "395867dc43f7a202ec500faf1dfb28b281fd04e699a63a48d4d90f937204c181",
    "guarded-nfa-answer-digest": EXPECTED_GUARDED_DIGEST,
    "guarded-nfa-answers": str(EXPECTED_GUARDED_ANSWERS),
    "guarded-tag-count": "1",
    "guarded-plan-digest":
        "a338fa2cb3c2c63dca764f3ecdf8b47cbaa580b7cf611d3ad8f27f223fcce1ef",
    "guarded-terminal-extension-count": "1",
    "composite-grammar-productions": "85",
    "slr-states": "36",
    "slr-shifts": "15",
    "slr-gotos": "43",
    "slr-reductions": "39",
    "slr-accepts": "1",
    "slr-conflicts": "0",
    "start-closed": "1",
}
EXPECTED_LR1 = {
    "accepts": 1,
    "conflict_cells": 0,
    "conflicts": 0,
    "gotos": 43,
    "grammar_digest":
        "d4c2ddf9754adb1f7aefe0291091fdf45a66dbea54549877b677177def7bba76",
    "max_state_items": 18,
    "nonterminals": 24,
    "productions": 85,
    "reachable_productions": 27,
    "reductions": 39,
    "shifts": 15,
    "states": 36,
    "table_digest":
        "f259a8234687ad7f4f3f704346eac2c932f7f57194b3053c6c3106c2351ad33d",
    "terminals": 6,
}
EXPECTED_MATRIX_DIGEST = (
    "65789fc1a5ebb285157ce36bd0f48ab76ed4c60c2a38e4d0073d9738f9f7b71d"
)
EXPECTED_EXECUTION_PLAN_DIGEST = (
    "1c6873759c8dd5725dbf609de5cecc58a7e589c9aa096c874877f822b17499b4"
)
EXPECTED_EXECUTION_MATRIX_DIGEST = (
    "fb9d472196d94951cd2943b1266c50507243fd93723ed80a53c857b3ec18def3"
)
EXPECTED_SLR_SHADOW_MATRIX_DIGEST = (
    "d57c5b504d9a65aaa22c186930228d04f3b4b75243661f83798d7eeaab0edb1b"
)


class GateFailure(RuntimeError):
    pass


def guarded_compiler_arguments() -> list[str]:
    result = [
        str(PRESENTATIONS / "parserpack" / "parser_pack_core_v1.metta"),
        str(PRESENTATIONS / "reflection" / "finite_horn_reflection_v1.metta"),
        str(PRESENTATIONS / "compiler" / "syntax_compiler_v1.metta"),
        str(REGULAR_COMPILER),
        str(GUARDED_COMPILER),
    ]
    for relative in ABI_CASES["petta"]["sources"]:
        result.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return result


def guarded_native(chart_binary: Path) -> tuple[str, ...]:
    completed = subprocess.run(
        [
            str(chart_binary),
            *guarded_compiler_arguments(),
            "--query-text",
            "(compile-guarded-span-nfa petta-token-like "
            "?guard-state ?guard ?edge)",
            "--timeout",
            "30",
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=60,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "native guarded regular compiler failed: "
            + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("native guarded compiler emitted non-JSON") from error
    terms = row.get("terms")
    if (
        row.get("outcome") != "Ambiguous"
        or row.get("answers") != EXPECTED_GUARDED_ANSWERS
        or row.get("term_digest") != EXPECTED_GUARDED_DIGEST
        or not isinstance(terms, list)
        or len(terms) != EXPECTED_GUARDED_ANSWERS
        or terms != sorted(terms)
        or len(set(terms)) != EXPECTED_GUARDED_ANSWERS
    ):
        raise GateFailure("native guarded compiler answer set changed")
    return tuple(terms)


def guarded_petta(petta_root: Path) -> tuple[str, tuple[str, ...]]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "guarded_span_oracle_v1.pl"
    )
    completed = subprocess.run(
        [
            "swipl",
            "-q",
            "-f",
            str(oracle),
            "--",
            str(PRESENTATIONS),
            "petta-token-like",
            *ABI_CASES["petta"]["sources"],
        ],
        cwd=oracle.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=60,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa guarded regular compiler failed: "
            + completed.stderr.strip()
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "guarded-span-oracle-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("PeTTa guarded oracle returned malformed framing")
    fields: dict[str, object] = {"answer": []}
    for line in lines[1:-1]:
        parts = line.split("\t")
        if len(parts) != 2:
            raise GateFailure(f"malformed PeTTa guarded record: {line!r}")
        name, value = parts
        if name == "answer":
            answers = fields["answer"]
            assert isinstance(answers, list)
            answers.append(value)
        elif name in fields:
            raise GateFailure(f"duplicate PeTTa guarded field: {name}")
        else:
            fields[name] = value
    answers = fields["answer"]
    if (
        fields.get("answer-digest") != EXPECTED_GUARDED_DIGEST
        or fields.get("answers") != str(EXPECTED_GUARDED_ANSWERS)
        or not isinstance(answers, list)
        or len(answers) != EXPECTED_GUARDED_ANSWERS
        or answers != sorted(answers)
    ):
        raise GateFailure("PeTTa guarded compiler answer set changed")
    source_digest = fields.get("source-digest")
    if not isinstance(source_digest, str):
        raise GateFailure("PeTTa guarded compiler omitted source provenance")
    return source_digest, tuple(answers)


def run_plan(
    binary: Path,
    paths: tuple[Path, Path, Path, Path, Path],
    regular_digest: str,
    guarded_digest: str,
    *,
    expect_success: bool,
) -> str:
    completed = subprocess.run(
        [
            str(binary),
            *(str(path) for path in paths),
            regular_digest,
            guarded_digest,
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=90,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native guarded lexical projection failed: "
                + completed.stderr.strip()
            )
        return completed.stdout
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("corrupt guarded lexical input did not fail closed")
    return ""


def parse_exec_stream(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "parser-pack-guarded-lexical-exec-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("guarded lexical execution has malformed framing")
    result: dict[str, object] = {"gll-result": [], "glr-result": []}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if len(fields) != 2:
            raise GateFailure(f"malformed guarded lexical record: {line!r}")
        field, value = fields
        if field in ("gll-result", "glr-result"):
            values = result[field]
            assert isinstance(values, list)
            values.append(value)
        elif field in result:
            raise GateFailure(f"duplicate guarded lexical field: {field}")
        else:
            result[field] = value
    return result


def parse_cursor_hybrid_stream(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "parser-pack-guarded-lexical-cursor-hybrid-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("cursor-only hybrid execution has malformed framing")
    result: dict[str, object] = {}
    for line in lines[1:-1]:
        field, separator, value = line.partition("\t")
        if not separator or field in result:
            raise GateFailure(
                f"malformed cursor-only hybrid record: {line!r}"
            )
        result[field] = value
    return result


def run_exec(
    binary: Path,
    paths: tuple[Path, Path, Path, Path, Path],
    input_path: Path,
    regular_digest: str,
    guarded_digest: str,
    *,
    expect_success: bool,
    action_path: Path | None = None,
    action_compiler_digest: str | None = None,
    benchmark_iterations: int | None = None,
    cursor_hybrid_only: bool = False,
    emit_c_path: Path | None = None,
    emit_c_prefix: str | None = None,
) -> dict[str, object]:
    if benchmark_iterations is not None and benchmark_iterations <= 0:
        raise GateFailure("benchmark iterations must be positive")
    if cursor_hybrid_only and (
        benchmark_iterations is None
        or action_path is None
        or action_compiler_digest is None
    ):
        raise GateFailure(
            "cursor-only hybrid execution requires actions and iterations"
        )
    if (emit_c_path is None) != (emit_c_prefix is None):
        raise GateFailure("generated C path and prefix must be supplied together")
    if emit_c_path is not None and (
        benchmark_iterations is not None
        or cursor_hybrid_only
        or action_path is None
        or action_compiler_digest is None
    ):
        raise GateFailure("generated C requires bound actions and no benchmark")
    command = [
        str(binary),
        *(str(path) for path in paths),
        str(input_path),
        regular_digest,
        guarded_digest,
    ]
    if (action_path is not None or action_compiler_digest is not None):
        if action_path is None or action_compiler_digest is None:
            raise GateFailure(
                "action answers and compiler digest must be supplied together"
            )
        command.extend((str(action_path), action_compiler_digest))
    if benchmark_iterations is not None:
        if cursor_hybrid_only:
            command.append("--cursor-hybrid-only")
        command.append(str(benchmark_iterations))
    elif emit_c_path is not None:
        command.extend(("--emit-c", str(emit_c_path), str(emit_c_prefix)))
    completed = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=90,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native guarded lexical execution failed: "
                + completed.stderr.strip()
            )
        if cursor_hybrid_only:
            return parse_cursor_hybrid_stream(completed.stdout)
        return parse_exec_stream(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("invalid guarded lexical execution did not fail closed")
    return {}


def require_exec_seals(row: dict[str, object]) -> None:
    expected = {
        "base-pack-digest": EXPECTED_NATIVE["base-pack-digest"],
        "lexical-plan-digest": EXPECTED_NATIVE["lexical-plan-digest"],
        "guard-plan-digest": EXPECTED_NATIVE["guard-plan-digest"],
        "guarded-plan-digest": EXPECTED_NATIVE["guarded-plan-digest"],
        "execution-plan-digest": EXPECTED_EXECUTION_PLAN_DIGEST,
        "cursor-certificate-eligible": "0",
        "cursor-program-built": "0",
        "dfa-states": "18",
        "dfa-transitions": "154",
        "source-passes": "1",
        "dfa-scans": "1",
        "view-replay-source-passes": "0",
        "view-replay-dfa-scans": "1",
        "ordinary-witness-parser-family-builds": "1",
        "ordinary-witness-prepared-starts": "2",
        "guard-witness-parser-family-builds": "1",
        "guard-witness-prepared-starts": "1",
        "guarded-witness-parser-family-builds": "1",
        "guarded-witness-prepared-starts": "1",
        "final-parser-table-builds": "1",
        "gll-outcome": "completed",
        "glr-outcome": "completed",
    }
    changed = {
        field: (value, row.get(field))
        for field, value in expected.items()
        if row.get(field) != value
    }
    if changed:
        raise GateFailure(f"guarded lexical execution seals changed: {changed}")
    if (
        row.get("gll-decision") != row.get("glr-decision")
        or row.get("gll-forest-digest") != row.get("glr-forest-digest")
        or row.get("gll-forest-digest")
        != row.get("view-replay-forest-digest")
        or row.get("gll-result") != row.get("glr-result")
    ):
        raise GateFailure("guarded lexical GLL and GLR results differ")
    for field in (
        "guard-relation-digest",
        "projected-relation-digest",
        "gll-forest-digest",
    ):
        value = row.get(field)
        if (
            not isinstance(value, str)
            or len(value) != 64
            or any(character not in "0123456789abcdef" for character in value)
        ):
            raise GateFailure(f"guarded lexical {field} is not a digest")
    integer_fields = (
        "combined-tokens",
        "ordinary-tokens",
        "guard-body-tokens",
        "guarded-candidates",
        "guarded-tokens",
        "projected-tokens",
        "ordinary-witness-runs",
        "guard-witness-runs",
        "guarded-witness-runs",
        "final-parse-work-items",
    )
    try:
        counts = {field: int(str(row[field])) for field in integer_fields}
    except (KeyError, ValueError) as error:
        raise GateFailure("guarded lexical execution omitted a receipt count") from error
    if (
        counts["combined-tokens"]
        != counts["ordinary-tokens"]
        + counts["guard-body-tokens"]
        + counts["guarded-candidates"]
        or counts["projected-tokens"]
        != counts["ordinary-tokens"] + counts["guarded-tokens"]
        or counts["guarded-tokens"] > counts["guarded-candidates"]
        or counts["ordinary-witness-runs"] != counts["ordinary-tokens"]
        or counts["guard-witness-runs"] != counts["guard-body-tokens"]
        or counts["guarded-witness-runs"] != counts["guarded-tokens"]
    ):
        raise GateFailure("guarded lexical execution receipt is inconsistent")


def execution_matrix_digest(records: list[dict[str, object]]) -> str:
    payload = json.dumps(
        records, ensure_ascii=False, sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return sha256(payload).hexdigest()


def guarded_entry_rows(plan: dict[str, object]) -> tuple[tuple[object, ...], ...]:
    entries = plan.get("guarded_lexical_entries")
    if not isinstance(entries, list):
        raise GateFailure("guarded lexical plan omitted its entry inventory")
    result = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise GateFailure("guarded lexical plan has a malformed entry")
        result.append((
            entry.get("tag_index"),
            entry.get("state_id"),
            entry.get("terminal_index"),
            entry.get("guard_entry_index"),
            render(entry["tag"]),
            render(entry["state"]),
            render(entry["guard_state"]),
            render(entry["guard_body"]),
        ))
    return tuple(result)


def matrix_digest(native: dict[str, object], lr1: dict[str, object]) -> str:
    payload = (
        "ParserPackGuardedLexicalPeTTaFormV1\n"
        f"{native['base-pack-digest']}\n"
        f"{native['lexical-plan-digest']}\n"
        f"{native['guard-plan-digest']}\n"
        f"{native['guarded-plan-digest']}\n"
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
    parser.add_argument("--exec-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--probe-final-forest-growth", action="store_true")
    parser.add_argument("--probe-iterations", type=int, default=3)
    arguments = parser.parse_args()
    if arguments.probe_iterations <= 0:
        parser.error("probe-iterations must be positive")
    chart_binary = arguments.chart_binary.resolve()
    plan_binary = arguments.plan_binary.resolve()
    exec_binary = arguments.exec_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if (
        not chart_binary.is_file()
        or not plan_binary.is_file()
        or not exec_binary.is_file()
    ):
        raise GateFailure("native guarded lexical binary is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa foundation checkout is absent")
    pin_authority(petta_root)

    abi = export_stream(petta_root, ABI_CASES["petta"])
    source_digest = abi_scalar(abi, "source-digest")
    lexical_terms, _, lexical_agreements = compile_case_tags(
        chart_binary, petta_root, dict(LEXICAL_CASE), source_digest
    )
    evidence_row = guard_row(chart_binary)
    guard_nfa = guard_nfa_row(chart_binary, "petta")
    guard_terms = tuple(str(term) for term in guard_nfa["terms"])
    native_guarded_terms = guarded_native(chart_binary)
    guarded_source_digest, petta_guarded_terms = guarded_petta(petta_root)
    if (
        native_guarded_terms != petta_guarded_terms
        or guarded_source_digest != source_digest
    ):
        raise GateFailure("C and PeTTa guarded compiler results differ")

    regular_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    guarded_digest = sha256(GUARDED_COMPILER.read_bytes()).hexdigest()
    mutations = 0
    execution_records: list[dict[str, object]] = []
    execution_accepted = 0
    execution_rejected = 0
    execution_authority_agreements = 0
    execution_semantic_agreements = 0
    execution_mutations = 0
    slr_shadow_records: list[dict[str, object]] = []
    slr_shadow_accepted = 0
    slr_shadow_needs_general = 0
    slr_shadow_resource_limit = 0
    growth_summary = None
    flat_replay_summary: FlatReplaySummary | None = None
    growth_authority_agreements = 0
    growth_reference_agreements = 0
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-petta-guarded-lexical-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "petta.abi"
        lexical_path = directory / "petta-lexical.nfa"
        guard_path = directory / "petta-guard.nfa"
        evidence_path = directory / "petta-guard.evidence"
        guarded_path = directory / "petta-guarded.nfa"
        abi_path.write_bytes(abi)
        lexical_path.write_text(
            "\n".join(lexical_terms) + "\n", encoding="utf-8"
        )
        guard_path.write_text(
            "\n".join(guard_terms) + "\n", encoding="utf-8"
        )
        evidence_path.write_bytes(evidence_stream(PROFILE, evidence_row))
        guarded_path.write_text(
            "\n".join(native_guarded_terms) + "\n", encoding="utf-8"
        )
        paths = (
            abi_path,
            lexical_path,
            guard_path,
            evidence_path,
            guarded_path,
        )
        output = run_plan(
            plan_binary,
            paths,
            regular_digest,
            guarded_digest,
            expect_success=True,
        )
        native = parse_plan_stream(output)
        lr1 = summary_json(analyze_lr1(composite_grammar(
            parse_abi_stream(abi), native
        )))

        authority_oracle = (
            petta_root
            / "experiments"
            / "gslt2parse_foundation"
            / "petta_parser_authority_v1.pl"
        )
        for case_index, (label, (input_bytes, expected)) in enumerate(
            FORM_CASES.items()
        ):
            input_path = directory / f"exec-case-{case_index}.input"
            input_path.write_bytes(input_bytes)
            authority = run_authority_case(
                authority_oracle, petta_root, "form", input_path
            )
            if authority != expected:
                raise GateFailure(f"PeTTa authority changed for {label}")
            reference = run_petta_reference(petta_root, input_path)
            decision = expected_decision(expected)
            if reference.get("decision") != decision:
                raise GateFailure(
                    f"PeTTa authority and LanguageDef differ for {label}"
                )
            execution_authority_agreements += 1
            row = run_exec(
                exec_binary, paths, input_path,
                regular_digest, guarded_digest,
                expect_success=True,
            )
            require_exec_seals(row)
            if row.get("gll-decision") != decision:
                raise GateFailure(
                    f"one-pass C and PeTTa authority differ for {label}"
                )
            if tuple(row["gll-result"]) != tuple(reference["results"]):
                raise GateFailure(
                    f"one-pass C and PeTTa semantics differ for {label}"
                )
            if (
                int(str(row.get("input-bytes"))) != len(input_bytes)
                or int(str(row.get("input-scalars")))
                != reference.get("input_scalars")
            ):
                raise GateFailure(
                    f"one-pass C input receipt differs for {label}"
                )
            slr_outcome = row.get("slr-shadow-outcome")
            try:
                slr_work = int(str(row["slr-shadow-work-items"]))
            except (KeyError, ValueError) as error:
                raise GateFailure(
                    f"SLR shadow omitted its work receipt for {label}"
                ) from error
            slr_digest = row.get("slr-shadow-forest-digest")
            if slr_outcome == "accepted":
                slr_shadow_accepted += 1
                if slr_digest != row.get("gll-forest-digest"):
                    raise GateFailure(
                        f"SLR and GLL forest digests differ for {label}"
                    )
            elif slr_outcome == "needs-general":
                slr_shadow_needs_general += 1
                if slr_digest != "":
                    raise GateFailure(
                        f"delegated SLR shadow exposed a digest for {label}"
                    )
            elif slr_outcome == "resource-limit":
                slr_shadow_resource_limit += 1
            else:
                raise GateFailure(
                    f"SLR shadow has an unknown outcome for {label}: "
                    f"{slr_outcome!r}"
                )
            slr_shadow_records.append({
                "forest": slr_digest,
                "input": input_bytes.hex(),
                "label": label,
                "outcome": slr_outcome,
                "work": slr_work,
            })
            execution_semantic_agreements += 1
            if decision == "accepted":
                execution_accepted += 1
            else:
                execution_rejected += 1
            execution_records.append({
                "decision": decision,
                "forest": row["gll-forest-digest"],
                "guard_relation": row["guard-relation-digest"],
                "guarded_tokens": int(str(row["guarded-tokens"])),
                "input": input_bytes.hex(),
                "label": label,
                "projected_relation": row["projected-relation-digest"],
                "projected_tokens": int(str(row["projected-tokens"])),
                "parser_tables": {
                    "ordinary_family_builds": int(str(
                        row["ordinary-witness-parser-family-builds"]
                    )),
                    "ordinary_starts": int(str(
                        row["ordinary-witness-prepared-starts"]
                    )),
                    "guard_family_builds": int(str(
                        row["guard-witness-parser-family-builds"]
                    )),
                    "guard_starts": int(str(
                        row["guard-witness-prepared-starts"]
                    )),
                    "guarded_family_builds": int(str(
                        row["guarded-witness-parser-family-builds"]
                    )),
                    "guarded_starts": int(str(
                        row["guarded-witness-prepared-starts"]
                    )),
                    "final_builds": int(str(
                        row["final-parser-table-builds"]
                    )),
                },
                "results": row["gll-result"],
            })

        invalid_utf8 = directory / "exec-invalid-utf8.input"
        invalid_utf8.write_bytes(b"\x80")
        run_exec(
            exec_binary, paths, invalid_utf8,
            regular_digest, guarded_digest,
            expect_success=False,
        )
        execution_mutations += 1

        duplicate_path = directory / "guarded-duplicate.nfa"
        duplicate_path.write_text(
            "\n".join((native_guarded_terms[0], *native_guarded_terms)) + "\n",
            encoding="utf-8",
        )
        run_plan(
            plan_binary,
            (*paths[:4], duplicate_path),
            regular_digest,
            guarded_digest,
            expect_success=False,
        )
        mutations += 1

        wrong_body_path = directory / "guarded-wrong-body.nfa"
        wrong_body_path.write_text(
            "\n".join(
                term.replace(
                    "(sir-ref petta-token-boundary)",
                    "(sir-ref petta-skip)",
                )
                for term in native_guarded_terms
            )
            + "\n",
            encoding="utf-8",
        )
        run_plan(
            plan_binary,
            (*paths[:4], wrong_body_path),
            regular_digest,
            guarded_digest,
            expect_success=False,
        )
        mutations += 1

        wrong_state_path = directory / "guarded-wrong-state.nfa"
        wrong_state_path.write_text(
            "\n".join(
                term.replace(
                    "(pp-sub (pp-def petta-token-like) right)",
                    "(pp-sub (pp-def petta-token-like) left)",
                )
                for term in native_guarded_terms
            )
            + "\n",
            encoding="utf-8",
        )
        run_plan(
            plan_binary,
            (*paths[:4], wrong_state_path),
            regular_digest,
            guarded_digest,
            expect_success=False,
        )
        mutations += 1

        bad_evidence_path = directory / "guard-wrong-answer-set.evidence"
        bad_evidence_path.write_text(
            evidence_path.read_text(encoding="utf-8").replace(
                str(evidence_row["term_digest"]), "0" * 64, 1
            ),
            encoding="utf-8",
        )
        run_plan(
            plan_binary,
            (abi_path, lexical_path, guard_path,
             bad_evidence_path, guarded_path),
            regular_digest,
            guarded_digest,
            expect_success=False,
        )
        mutations += 1

        run_plan(
            plan_binary,
            paths,
            regular_digest,
            "not-a-sha256-digest",
            expect_success=False,
        )
        mutations += 1
        if arguments.probe_final_forest_growth:
            def execute_growth(
                input_path: Path, iterations: int
            ) -> dict[str, object]:
                return run_exec(
                    exec_binary,
                    paths,
                    input_path,
                    regular_digest,
                    guarded_digest,
                    expect_success=True,
                    benchmark_iterations=iterations,
                )

            def validate_growth_authority(
                input_path: Path,
                token_count: int,
                row: dict[str, object],
            ) -> None:
                nonlocal growth_authority_agreements
                nonlocal growth_reference_agreements
                expected_text = "[]"
                leaves = 1
                while leaves < token_count:
                    expected_text = f"[{expected_text},{expected_text}]"
                    leaves *= 2
                expected = (("term", expected_text),)
                authority = run_authority_case(
                    authority_oracle, petta_root, "form", input_path
                )
                if authority != expected:
                    raise GateFailure(
                        "PeTTa balanced-form authority changed during growth probe"
                    )
                growth_authority_agreements += 1
                if token_count == TOKEN_COUNTS[-1]:
                    return
                reference = run_petta_reference(petta_root, input_path)
                if (
                    reference.get("decision") != "accepted"
                    or tuple(row["gll-result"])
                    != tuple(reference["results"])
                ):
                    native_results = row.get("gll-result")
                    reference_results = reference.get("results")
                    native_digest = sha256(json.dumps(
                        native_results,
                        ensure_ascii=False,
                        sort_keys=True,
                    ).encode("utf-8")).hexdigest()
                    reference_digest = sha256(json.dumps(
                        reference_results,
                        ensure_ascii=False,
                        sort_keys=True,
                    ).encode("utf-8")).hexdigest()
                    raise GateFailure(
                        "PeTTa growth probe differs from its reference: "
                        f"decision={reference.get('decision')!r}, "
                        f"native-results={len(native_results or [])}, "
                        f"reference-results={len(reference_results or [])}, "
                        f"native-digest={native_digest}, "
                        f"reference-digest={reference_digest}"
                    )
                growth_reference_agreements += 1

            def validate_flat_authority(
                input_path: Path,
                leaf_count: int,
                row: dict[str, object],
            ) -> tuple[int, int]:
                expected_text = "[" + ",".join(
                    "[]" for _ in range(leaf_count)
                ) + "]"
                authority = run_authority_case(
                    authority_oracle, petta_root, "form", input_path
                )
                if authority != (("term", expected_text),):
                    raise GateFailure(
                        "PeTTa flat replay authority changed"
                    )
                reference = run_petta_reference(petta_root, input_path)
                if (
                    reference.get("decision") != "accepted"
                    or tuple(row["gll-result"])
                    != tuple(reference["results"])
                ):
                    raise GateFailure(
                        "PeTTa flat replay differs from its reference"
                    )
                return 1, 1

            try:
                growth_summary = run_growth_probe(
                    "petta",
                    directory,
                    arguments.probe_iterations,
                    execute_growth,
                    validate_growth_authority,
                )
                flat_replay_summary = run_flat_replay_canary(
                    "petta",
                    directory,
                    arguments.probe_iterations,
                    execute_growth,
                    validate_flat_authority,
                )
            except FinalForestProbeFailure as error:
                raise GateFailure(str(error)) from error

    if not EXPECTED_NATIVE or not EXPECTED_LR1 or not EXPECTED_MATRIX_DIGEST:
        scalar_native = {
            key: value
            for key, value in native.items()
            if isinstance(value, str)
        }
        print("NATIVE=" + json.dumps(scalar_native, sort_keys=True))
        print("ENTRIES=" + repr(guarded_entry_rows(native)))
        print("LR1=" + json.dumps(lr1, sort_keys=True))
        print("MATRIX=" + matrix_digest(native, lr1))
        raise GateFailure("guarded lexical seal constants are not ratified")
    changed = {
        field: (expected, native.get(field))
        for field, expected in EXPECTED_NATIVE.items()
        if native.get(field) != expected
    }
    if changed:
        raise GateFailure(f"native guarded lexical seal changed: {changed}")
    expected_entries = ((
        0,
        8,
        0,
        0,
        "petta-token-like",
        "(pp-def petta-token-like)",
        "(pp-sub (pp-def petta-token-like) right)",
        "(sir-ref petta-token-boundary)",
    ),)
    if guarded_entry_rows(native) != expected_entries:
        raise GateFailure("guarded lexical entry inventory changed")
    if lr1 != EXPECTED_LR1:
        raise GateFailure(f"guarded lexical LR(1) seal changed: {lr1}")
    digest = matrix_digest(native, lr1)
    if digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(f"guarded lexical matrix digest changed: {digest}")
    exec_digest = execution_matrix_digest(execution_records)
    if not EXPECTED_EXECUTION_MATRIX_DIGEST:
        print("EXECUTION_MATRIX=" + exec_digest)
        raise GateFailure("guarded lexical execution seal is not ratified")
    if exec_digest != EXPECTED_EXECUTION_MATRIX_DIGEST:
        raise GateFailure(
            f"guarded lexical execution matrix changed: {exec_digest}"
        )
    slr_shadow_digest = execution_matrix_digest(slr_shadow_records)
    if not EXPECTED_SLR_SHADOW_MATRIX_DIGEST:
        print("SLR_SHADOW_COUNTS=" + repr((
            slr_shadow_accepted,
            slr_shadow_needs_general,
            slr_shadow_resource_limit,
        )))
        print("SLR_SHADOW_RECORDS=" + json.dumps(
            slr_shadow_records, ensure_ascii=False, sort_keys=True
        ))
        print("SLR_SHADOW_MATRIX=" + slr_shadow_digest)
        raise GateFailure("SLR shadow execution seal is not ratified")
    if slr_shadow_digest != EXPECTED_SLR_SHADOW_MATRIX_DIGEST:
        raise GateFailure(
            f"SLR shadow execution matrix changed: {slr_shadow_digest}"
        )
    if (
        len(execution_records) != len(FORM_CASES)
        or execution_accepted + execution_rejected != len(FORM_CASES)
        or execution_authority_agreements != len(FORM_CASES)
        or execution_semantic_agreements != len(FORM_CASES)
        or len(slr_shadow_records) != len(FORM_CASES)
        or slr_shadow_accepted + slr_shadow_needs_general +
            slr_shadow_resource_limit != len(FORM_CASES)
    ):
        raise GateFailure("guarded lexical execution matrix is incomplete")
    print(
        "(ParserPackGuardedLexicalPeTTaFormV1Summary "
        f"{len(lexical_terms)} {len(guard_terms)} "
        f"{len(native_guarded_terms)} {lexical_agreements} 1 "
        f"{mutations} {native['slr-states']} {native['slr-conflicts']} "
        f"{lr1['states']} {lr1['conflicts']} {digest} 0)"
    )
    print(
        "(ParserPackGuardedLexicalExecV1Summary "
        f"{len(execution_records)} {execution_accepted} "
        f"{execution_rejected} {execution_authority_agreements} "
        f"{execution_semantic_agreements} {execution_mutations} "
        f"{EXPECTED_EXECUTION_PLAN_DIGEST} {exec_digest} 0)"
    )
    print(
        "(ParserPackSLRShadowV1Summary "
        f"{len(slr_shadow_records)} {slr_shadow_accepted} "
        f"{slr_shadow_needs_general} {slr_shadow_resource_limit} "
        f"{slr_shadow_digest} 0)"
    )
    if growth_summary is not None:
        if (
            growth_authority_agreements != growth_summary.cases
            or growth_reference_agreements != growth_summary.cases - 1
        ):
            raise GateFailure(
                "PeTTa growth authority coverage changed"
            )
        print(
            "(PreparedFinalForestGrowthV1Summary petta "
            f"{growth_summary.cases} {growth_summary.iterations} "
            f"{growth_authority_agreements} "
            f"{growth_reference_agreements} "
            f"{growth_summary.process_time_slope:.6f} "
            f"{growth_summary.time_slopes['slr']:.6f} "
            f"{growth_summary.time_slopes['gll']:.6f} "
            f"{growth_summary.time_slopes['glr']:.6f} "
            f"{growth_summary.structural_digest} 0)"
        )
    if flat_replay_summary is not None:
        print(
            "(PreparedFinalForestFlatReplayV1Summary petta "
            f"{flat_replay_summary.leaves} "
            f"{flat_replay_summary.input_bytes} "
            f"{flat_replay_summary.iterations} "
            f"{flat_replay_summary.authority_agreements} "
            f"{flat_replay_summary.reference_agreements} "
            f"{flat_replay_summary.result_digest} 0)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
