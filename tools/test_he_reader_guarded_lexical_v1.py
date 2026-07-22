#!/usr/bin/env python3
"""One-scan guarded lexical differential for the stable HE reader."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

from gslt2parse_schema_v1 import parse_sexprs, render
from parser_pack_lr1_v1 import (
    analyze_lr1,
    composite_grammar,
    parse_abi_stream,
    parse_plan_stream,
    summary_json,
)
from probe_cursor_hybrid_v1 import CursorHybridProbeFailure
from probe_cursor_hybrid_v1 import run_cursor_hybrid_growth
from probe_prepared_final_forest_v1 import FinalForestProbeFailure
from probe_prepared_final_forest_v1 import FlatReplaySummary
from probe_prepared_final_forest_v1 import TOKEN_COUNTS
from probe_prepared_final_forest_v1 import run_growth_probe
from probe_prepared_final_forest_v1 import run_flat_replay_canary
from run_he_parser_oracle_v1 import build_oracle
from test_he_parser_authority_v1 import ATOM_CASES
from test_he_parser_authority_v1 import run_case as run_he_case
from test_he_reader_guard_exec_v1 import expected_decision
from test_he_reader_guard_exec_v1 import matrix_digest
from test_he_reader_guard_exec_v1 import pin_authorities
from test_he_reader_guard_exec_v1 import require_native_seals
from test_he_reader_guard_exec_v1 import run_native as run_scalar
from test_he_reader_guard_exec_v1 import run_petta
from test_finite_horn_chart_v1 import run_json
from test_parser_action_bytecode_compiler_v1 import GUARD_ACTION_ROWS
from test_parser_action_bytecode_compiler_v1 import (
    guard_action_program_arguments,
)
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_plan_prime_v1 import PROFILES
from test_parser_pack_guard_plan_prime_v1 import REGULAR_COMPILER
from test_parser_pack_guard_plan_prime_v1 import evidence_stream
from test_parser_pack_guard_plan_prime_v1 import profile_guard_row
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row
from test_parser_pack_guarded_lexical_v1 import GUARDED_COMPILER
from test_parser_pack_guarded_lexical_v1 import run_exec
from test_parser_pack_guarded_lexical_v1 import run_plan
from test_parser_pack_lexical_v1 import compile_case_tags
from test_regular_span_dfa_v1 import abi_scalar
from test_regular_span_dfa_v1 import answer_digest


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
PROFILE = PROFILES["he"]

EXPECTED_GUARDED_ANSWER_COUNT = 33
EXPECTED_GUARDED_ANSWER_DIGEST = (
    "5f2ca06d86bfbaa3f1a9d0a4d10038bf94f8f7f6df60e96eef89e270477ce578"
)
EXPECTED_GUARDED_PLAN_DIGEST = (
    "199d0c654093053436a11af5eb90784b3442502c88995450fdb838e8bf33e611"
)
EXPECTED_EXECUTION_PLAN_DIGEST = (
    "24560d68edf40a5e9884433772bb2de0c4e9f9a8247cb3c5abeba595321b6cc2"
)
EXPECTED_CURSOR_CERTIFICATE_DIGEST = (
    "263801583da050bef8b829c5507102102710c828e881bf7408038a099314d39d"
)
EXPECTED_CURSOR_PROGRAM_DIGEST = (
    "1fd6017c914f790969ce001a5b7b2eca5dab3d8b4d497ca1587009af592f000e"
)
EXPECTED_GUARD_ACTION_COMPILER_DIGEST = (
    "ca623a23e19f2e5369b59906ada3cbcefcfbd2ccba64dedb9c7df6e463877cbf"
)
EXPECTED_GUARD_ACTION_ANSWER_DIGEST = (
    "345adba3c308773b8bfd339f171f600f64e3af6399a2ef4dc97a2e22067a7edb"
)
EXPECTED_GUARD_ACTION_PROGRAM_DIGEST = (
    "e48b1b598f529e9fa1f81d4000844b531e46a4c7e6c799114a661266ed264369"
)
GUARD_ACTION_COMPILER_COMPONENTS = (
    "compiler/parser_action_bytecode_compiler_v1.metta",
    "compiler/parser_pack_guard_compiler_v1.metta",
    "compiler/parser_pack_guard_action_link_v1.metta",
)
EXPECTED_PLAN_MATRIX_DIGEST = (
    "ecb3ed5e57e6838064ec6f34d045acefa8e5c61b6fdc5630f55a8239c673c49c"
)
EXPECTED_EXECUTION_MATRIX_DIGEST = (
    "29c9c42a55d3aae5c6f92339e342903f329d1181b369c24b1a3f7b5bf3ba0158"
)
EXPECTED_SLR_SHADOW_MATRIX_DIGEST = (
    "13424babd9547bdd268263c3dfcfe0791cd1fb2bd77cf6b39803f9e6b2ff1a99"
)
EXPECTED_CURSOR_TRACE_MATRIX_DIGEST = (
    "fc839185c79ea612bc79034b04074b935fc285eef977730b77b68658965be30b"
)
EXPECTED_CURSOR_SEMANTIC_MATRIX_DIGEST = (
    "7005ceead05d7b7480a0c75c5d170382472abc8fe9bf547580008efa0fe3f216"
)
EXPECTED_CURSOR_HYBRID_SEMANTIC_MATRIX_DIGEST = (
    "28e701a9d20d29cc99f6bf27847dd72c7cedbe9651ee8680d4b718a4c85be68c"
)
EXPECTED_CURSOR_HYBRID_GROWTH_DIGEST = (
    "8bd5481eb619f9df6f2fa251d31c481f70d04e4a118906aa8c53b46b84610f88"
)
GUARDED_TAGS = ("he-comment", "he-variable", "he-word")
NEGATIVE_TAGS = ("he-document", "he-string")


class GateFailure(RuntimeError):
    pass


def compiler_arguments() -> list[str]:
    result = [
        str(PRESENTATIONS / "parserpack" / "parser_pack_core_v1.metta"),
        str(PRESENTATIONS / "reflection" / "finite_horn_reflection_v1.metta"),
        str(PRESENTATIONS / "compiler" / "syntax_compiler_v1.metta"),
        str(PRESENTATIONS / "compiler" / "regular_span_compiler_v1.metta"),
        str(GUARDED_COMPILER),
    ]
    for relative in ABI_CASES["he"]["sources"]:
        result.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return result


def run_compiler(chart_binary: Path, query: str) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(chart_binary),
            *compiler_arguments(),
            "--query-text",
            query,
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
            "native HE guarded compiler failed: " + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("native HE guarded compiler emitted non-JSON") from error
    if not isinstance(row, dict) or not isinstance(row.get("terms"), list):
        raise GateFailure("native HE guarded compiler response changed shape")
    return row


def answer_tag(term_text: str) -> str:
    terms = parse_sexprs(term_text, source="HE guarded NFA answer")
    if len(terms) != 1:
        raise GateFailure("HE guarded NFA answer is not singular")
    term = terms[0]
    if not isinstance(term, tuple) or len(term) != 5:
        raise GateFailure("HE guarded NFA answer has the wrong arity")
    return render(term[1])


def native_guarded_answers(chart_binary: Path) -> tuple[str, ...]:
    row = run_compiler(
        chart_binary,
        "(compile-guarded-span-nfa ?tag ?guard-state ?guard ?edge)",
    )
    terms = tuple(str(term) for term in row["terms"])
    tags = tuple(answer_tag(term) for term in terms)
    if (
        row.get("outcome") != "Ambiguous"
        or row.get("answers") != EXPECTED_GUARDED_ANSWER_COUNT
        or row.get("term_digest") != EXPECTED_GUARDED_ANSWER_DIGEST
        or terms != tuple(sorted(terms))
        or len(set(terms)) != len(terms)
        or tuple(sorted(set(tags))) != GUARDED_TAGS
        or any(tags.count(tag) != 11 for tag in GUARDED_TAGS)
    ):
        raise GateFailure("native HE guarded answer set changed")
    return terms


def guard_action_compiler_digest() -> str:
    payload = ["ParserPackGuardActionCompilerV1\n"]
    for relative in GUARD_ACTION_COMPILER_COMPONENTS:
        digest = sha256((PRESENTATIONS / relative).read_bytes()).hexdigest()
        payload.append(f"{relative}\0{digest}\n")
    observed = sha256("".join(payload).encode("utf-8")).hexdigest()
    if observed != EXPECTED_GUARD_ACTION_COMPILER_DIGEST:
        raise GateFailure("guard-action compiler component seal changed")
    return observed


def native_guard_action_answers(chart_binary: Path) -> tuple[str, ...]:
    expected = GUARD_ACTION_ROWS["union"]
    result = run_json(
        chart_binary,
        [
            *guard_action_program_arguments(),
            "--query-text",
            "(compile-guard-extended-action-program "
            "?owner ?label ?arity ?action ?code)",
            "--timeout",
            "30",
        ],
    )
    terms = result.get("terms")
    if (
        result.get("outcome") != "Ambiguous"
        or result.get("answers") != expected["count"]
        or result.get("term_digest") != expected["digest"]
        or not isinstance(terms, list)
        or len(terms) != expected["count"]
        or terms != sorted(terms)
    ):
        raise GateFailure("HE guard-extended action program changed")
    return tuple(str(term) for term in terms)


def parse_petta_guarded_output(output: str) -> tuple[str, tuple[str, ...]]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "guarded-span-oracle-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("PeTTa HE guarded compiler framing changed")
    fields: dict[str, object] = {"answer": []}
    for line in lines[1:-1]:
        name, separator, value = line.partition("\t")
        if not separator:
            raise GateFailure("PeTTa HE guarded compiler record is malformed")
        if name == "answer":
            answers = fields[name]
            assert isinstance(answers, list)
            answers.append(value)
        elif name in fields:
            raise GateFailure(f"PeTTa HE guarded compiler duplicated {name}")
        else:
            fields[name] = value
    answers = fields["answer"]
    source_digest = fields.get("source-digest")
    if (
        not isinstance(answers, list)
        or not isinstance(source_digest, str)
        or fields.get("answers") != str(len(answers))
        or fields.get("answer-digest") != answer_digest(answers)
        or answers != sorted(answers)
    ):
        raise GateFailure("PeTTa HE guarded compiler seals changed")
    return source_digest, tuple(answers)


def petta_guarded_answers(
    petta_root: Path,
) -> tuple[str, tuple[str, ...]]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "guarded_span_oracle_v1.pl"
    )
    source_digest: str | None = None
    answers: list[str] = []
    for tag in GUARDED_TAGS:
        completed = subprocess.run(
            [
                "swipl",
                "-q",
                "-f",
                str(oracle),
                "--",
                str(PRESENTATIONS),
                tag,
                *(str(relative) for relative in ABI_CASES["he"]["sources"]),
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
                "PeTTa HE guarded compiler failed: "
                + completed.stderr.strip()
            )
        observed_source, observed_answers = parse_petta_guarded_output(
            completed.stdout
        )
        if source_digest is None:
            source_digest = observed_source
        elif source_digest != observed_source:
            raise GateFailure("PeTTa HE guarded source digests differ by tag")
        answers.extend(observed_answers)
    normalized = tuple(sorted(answers))
    if (
        source_digest is None
        or len(normalized) != EXPECTED_GUARDED_ANSWER_COUNT
        or len(set(normalized)) != len(normalized)
        or answer_digest(normalized) != EXPECTED_GUARDED_ANSWER_DIGEST
    ):
        raise GateFailure("PeTTa HE guarded union changed")
    return source_digest, normalized


def negative_compiler_gates(
    chart_binary: Path, petta_root: Path
) -> int:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "guarded_span_oracle_v1.pl"
    )
    agreements = 0
    for tag in NEGATIVE_TAGS:
        row = run_compiler(
            chart_binary,
            f"(compile-guarded-span-nfa {tag} ?guard-state ?guard ?edge)",
        )
        completed = subprocess.run(
            [
                "swipl",
                "-q",
                "-f",
                str(oracle),
                "--",
                str(PRESENTATIONS),
                tag,
                *(str(relative) for relative in ABI_CASES["he"]["sources"]),
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
            raise GateFailure("PeTTa negative HE guarded query failed")
        _, petta_answers = parse_petta_guarded_output(completed.stdout)
        if (
            row.get("outcome") != "NoAnswer"
            or row.get("answers") != 0
            or row.get("terms") != []
            or petta_answers
        ):
            raise GateFailure(f"non-trailing HE definition {tag} was compiled")
        agreements += 1
    return agreements


def require_plan_seals(
    plan: dict[str, object], lr1: dict[str, object]
) -> None:
    expected = {
        "base-pack-digest": PROFILE.base_pack_digest,
        "lexical-plan-digest": PROFILE.lexical_plan_digest,
        "guard-plan-digest": PROFILE.guard_plan_digest,
        "guarded-nfa-answer-digest": EXPECTED_GUARDED_ANSWER_DIGEST,
        "guarded-nfa-answers": str(EXPECTED_GUARDED_ANSWER_COUNT),
        "guarded-tag-count": "3",
        "guarded-plan-digest": EXPECTED_GUARDED_PLAN_DIGEST,
        "guarded-terminal-extension-count": "3",
        "composite-grammar-productions": "397",
        "slr-states": "49",
        "slr-conflicts": "0",
        "start-closed": "1",
    }
    changed = {
        field: (value, plan.get(field))
        for field, value in expected.items()
        if plan.get(field) != value
    }
    if changed:
        raise GateFailure(f"HE guarded lexical plan seals changed: {changed}")
    if lr1.get("conflicts") != 0 or lr1.get("accepts") != 1:
        raise GateFailure("HE guarded lexical canonical LR(1) is not deterministic")


def require_execution_seals(row: dict[str, object]) -> None:
    expected = {
        "base-pack-digest": PROFILE.base_pack_digest,
        "lexical-plan-digest": PROFILE.lexical_plan_digest,
        "guard-plan-digest": PROFILE.guard_plan_digest,
        "guarded-plan-digest": EXPECTED_GUARDED_PLAN_DIGEST,
        "execution-plan-digest": EXPECTED_EXECUTION_PLAN_DIGEST,
        "cursor-certificate-digest": EXPECTED_CURSOR_CERTIFICATE_DIGEST,
        "cursor-certificate-eligible": "1",
        "cursor-certificate-failure-mask": "0",
        "cursor-certificate-slr-states": "49",
        "cursor-certificate-slr-conflicts": "0",
        "cursor-certificate-slr-accepts": "1",
        "cursor-certificate-reachable-nonterminals": "33",
        "cursor-certificate-reachable-productions": "40",
        "cursor-certificate-active-terminals": "8",
        "cursor-certificate-scalar-terminals": "4",
        "cursor-certificate-ordinary-span-tags": "1",
        "cursor-certificate-guarded-span-tags": "3",
        "cursor-certificate-guard-lookahead-tags": "3",
        "cursor-certificate-zero-input-cycles": "0",
        "cursor-certificate-unmapped-terminals": "0",
        "cursor-certificate-duplicate-terminal-sources": "0",
        "cursor-certificate-empty-tokens": "0",
        "cursor-certificate-nullable-tokens": "0",
        "cursor-certificate-non-prefix-free-tokens": "0",
        "cursor-certificate-unsupported-guards": "0",
        "cursor-certificate-boundary-crossings": "0",
        "cursor-certificate-first-domain-overlaps": "0",
        "cursor-program-built": "1",
        "cursor-program-digest": EXPECTED_CURSOR_PROGRAM_DIGEST,
        "cursor-program-dfa-states": "241",
        "cursor-program-dfa-transitions": "1162",
        "cursor-program-slr-states": "49",
        "cursor-program-slr-terminals": "35",
        "cursor-program-slr-nonterminals": "339",
        "cursor-program-slr-productions": "397",
        "cursor-program-slr-authored-productions": "397",
        "cursor-program-slr-actions": "1764",
        "cursor-program-slr-gotos": "16611",
        "cursor-program-terminal-sources": "8",
        "cursor-program-scalar-sources": "4",
        "cursor-program-ordinary-span-sources": "1",
        "cursor-program-guarded-span-sources": "3",
        "cursor-program-ranges": "34",
        "cursor-program-mutation-killed": "2",
        "cursor-program-actions-bound": "1",
        "cursor-program-action-compiler-digest": (
            EXPECTED_GUARD_ACTION_COMPILER_DIGEST
        ),
        "cursor-program-action-answer-digest": (
            EXPECTED_GUARD_ACTION_ANSWER_DIGEST
        ),
        "cursor-program-action-program-digest": (
            EXPECTED_GUARD_ACTION_PROGRAM_DIGEST
        ),
        "cursor-program-action-productions": "397",
        "cursor-program-action-instructions": "517",
        "cursor-program-action-agreements": "397",
        "cursor-program-action-mutations-killed": "3",
        "cursor-program-value-programs": "7",
        "cursor-program-value-programs-complete": "1",
        "cursor-program-value-program-conflicts": "0",
        "cursor-program-direct-semantic-agreement": "1",
        "cursor-program-semantic-agreement": "1",
        "cursor-program-hybrid-semantic-agreement": "1",
        "cursor-program-hybrid-bytes-agreement": "1",
        "cursor-program-hybrid-bytes-source-passes": "1",
        "cursor-program-source-passes": "0",
        "dfa-states": "241",
        "dfa-transitions": "1162",
        "source-passes": "1",
        "dfa-scans": "1",
        "view-replay-source-passes": "0",
        "view-replay-dfa-scans": "1",
        "gll-outcome": "completed",
        "glr-outcome": "completed",
    }
    changed = {
        field: (value, row.get(field))
        for field, value in expected.items()
        if row.get(field) != value
    }
    if changed:
        raise GateFailure(f"HE guarded lexical execution seals changed: {changed}")
    if (
        row.get("gll-decision") != row.get("glr-decision")
        or row.get("gll-forest-digest") != row.get("glr-forest-digest")
        or row.get("gll-forest-digest")
        != row.get("view-replay-forest-digest")
        or row.get("gll-result") != row.get("glr-result")
    ):
        raise GateFailure("HE guarded lexical GLL/GLR executions differ")
    for field in (
        "cursor-program-trace-digest",
        "cursor-program-action-program-digest",
        "guard-relation-digest",
        "projected-relation-digest",
        "gll-forest-digest",
    ):
        if re.fullmatch(r"[0-9a-f]{64}", str(row.get(field, ""))) is None:
            raise GateFailure(f"HE guarded lexical {field} is not a digest")
    count_fields = (
        "combined-tokens",
        "ordinary-tokens",
        "guard-body-tokens",
        "guarded-candidates",
        "guarded-tokens",
        "projected-tokens",
        "ordinary-witness-runs",
        "guard-witness-runs",
        "guarded-witness-runs",
    )
    try:
        counts = {name: int(str(row[name])) for name in count_fields}
    except (KeyError, ValueError) as error:
        raise GateFailure("HE guarded lexical receipt omitted a count") from error
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
        raise GateFailure("HE guarded lexical receipt is inconsistent")


def require_final_forest_benchmark_canary(row: dict[str, object]) -> None:
    if (
        row.get("benchmark-kind") != "prepared-final-forest-kernels"
        or row.get("benchmark-iterations") != "2"
    ):
        raise GateFailure("HE prepared final-forest benchmark framing changed")
    for backend in ("slr", "gll", "glr"):
        try:
            nanoseconds = int(str(row[f"benchmark-{backend}-nanoseconds"]))
            work = int(str(row[f"benchmark-{backend}-work-items"]))
            graph = int(str(row[f"benchmark-{backend}-graph-nodes"]))
            stack = int(str(row[f"benchmark-{backend}-stack-nodes"]))
            nodes = int(str(row[f"benchmark-{backend}-forest-nodes"]))
            choices = int(str(row[f"benchmark-{backend}-forest-choices"]))
            roots = int(str(row[f"benchmark-{backend}-forest-roots"]))
        except (KeyError, ValueError) as error:
            raise GateFailure(
                f"HE {backend.upper()} benchmark receipt is malformed"
            ) from error
        if (
            nanoseconds <= 0
            or work <= 0
            or graph <= 0
            or nodes <= 0
            or roots <= 0
            or stack < 0
            or choices < 0
        ):
            raise GateFailure(
                f"HE {backend.upper()} benchmark receipt is not positive"
            )
    if (
        row.get("benchmark-slr-work-items")
        != row.get("slr-shadow-work-items")
        or row.get("benchmark-gll-forest-nodes")
        != row.get("gll-forest-nodes")
        or row.get("benchmark-glr-forest-nodes")
        != row.get("glr-forest-nodes")
    ):
        raise GateFailure(
            "HE prepared final-forest benchmark changed a sealed receipt"
        )
    try:
        hybrid_nanoseconds = int(
            str(row["benchmark-cursor-hybrid-bytes-nanoseconds"])
        )
        hybrid_value_work = int(
            str(row["benchmark-cursor-hybrid-value-parser-work"])
        )
        hybrid_fallback_work = int(
            str(row["benchmark-cursor-hybrid-prepared-fallback-work"])
        )
        hybrid_value_runs = int(
            str(row["benchmark-cursor-hybrid-value-program-runs"])
        )
        hybrid_fallback_runs = int(
            str(row["benchmark-cursor-hybrid-prepared-fallback-runs"])
        )
        hybrid_tokens = int(str(row["benchmark-cursor-hybrid-tokens"]))
        hybrid_passes = int(
            str(row["benchmark-cursor-hybrid-source-passes"])
        )
    except (KeyError, ValueError) as error:
        raise GateFailure(
            "HE hybrid cursor benchmark receipt is malformed"
        ) from error
    if (
        hybrid_nanoseconds <= 0
        or hybrid_value_work <= 0
        or hybrid_fallback_work != 0
        or hybrid_value_runs <= 0
        or hybrid_fallback_runs != 0
        or hybrid_tokens != int(str(row["cursor-program-tokens"]))
        or hybrid_passes != 1
    ):
        raise GateFailure("HE hybrid cursor benchmark receipt changed")


def require_cursor_hybrid_only_canary(
    row: dict[str, object],
    reference: dict[str, object],
    input_len: int,
    iterations: int,
) -> None:
    expected = {
        "base-pack-digest": PROFILE.base_pack_digest,
        "lexical-plan-digest": PROFILE.lexical_plan_digest,
        "execution-plan-digest": EXPECTED_EXECUTION_PLAN_DIGEST,
        "cursor-program-digest": EXPECTED_CURSOR_PROGRAM_DIGEST,
        "outcome": "accepted",
        "input-bytes": str(input_len),
        "source-passes": "1",
        "benchmark-kind": "cursor-hybrid-bytes",
        "benchmark-iterations": str(iterations),
        "benchmark-cursor-hybrid-source-passes": "1",
    }
    changed = {
        field: (value, row.get(field))
        for field, value in expected.items()
        if row.get(field) != value
    }
    try:
        nanoseconds = int(
            str(row["benchmark-cursor-hybrid-bytes-nanoseconds"])
        )
        tokens = int(str(row["tokens"]))
        benchmark_tokens = int(
            str(row["benchmark-cursor-hybrid-tokens"])
        )
    except (KeyError, ValueError) as error:
        raise GateFailure(
            "HE cursor-only benchmark receipt is malformed"
        ) from error
    results = reference.get("gll-result")
    if (
        changed
        or nanoseconds <= 0
        or tokens <= 0
        or benchmark_tokens != tokens
        or row.get("trace-digest")
        != reference.get("cursor-program-trace-digest")
        or not isinstance(results, list)
        or len(results) != 1
        or row.get("semantic-result") != results[0]
    ):
        raise GateFailure(
            "HE cursor-only benchmark changed sealed semantics: "
            + repr(changed)
        )


def plan_matrix_digest(
    plan: dict[str, object], lr1: dict[str, object]
) -> str:
    fields = {
        "base": plan["base-pack-digest"],
        "guard": plan["guard-plan-digest"],
        "guarded": plan["guarded-plan-digest"],
        "guarded_answers": plan["guarded-nfa-answer-digest"],
        "lexical": plan["lexical-plan-digest"],
        "lr1": lr1,
        "slr_conflicts": plan["slr-conflicts"],
        "slr_states": plan["slr-states"],
    }
    return matrix_digest([fields])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--scalar-exec-binary", type=Path, required=True)
    parser.add_argument("--plan-binary", type=Path, required=True)
    parser.add_argument("--exec-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--he-root", type=Path, required=True)
    parser.add_argument("--probe-final-forest-growth", action="store_true")
    parser.add_argument("--probe-cursor-hybrid-growth", action="store_true")
    parser.add_argument("--probe-iterations", type=int, default=3)
    arguments = parser.parse_args()
    if arguments.probe_iterations <= 0:
        parser.error("probe-iterations must be positive")

    chart_binary = arguments.chart_binary.resolve()
    scalar_exec_binary = arguments.scalar_exec_binary.resolve()
    plan_binary = arguments.plan_binary.resolve()
    exec_binary = arguments.exec_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    he_root = arguments.he_root.resolve()
    if not all(
        path.is_file()
        for path in (
            chart_binary,
            scalar_exec_binary,
            plan_binary,
            exec_binary,
        )
    ):
        raise GateFailure("native HE guarded lexical dependency is absent")
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
    action_terms = native_guard_action_answers(chart_binary)
    petta_source_digest, petta_guarded_terms = petta_guarded_answers(petta_root)
    if (
        guarded_terms != petta_guarded_terms
        or petta_source_digest != source_digest
    ):
        raise GateFailure("C and PeTTa HE guarded compiler results differ")
    compiler_negative_agreements = negative_compiler_gates(
        chart_binary, petta_root
    )

    regular_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    guarded_digest = sha256(GUARDED_COMPILER.read_bytes()).hexdigest()
    action_compiler_digest = guard_action_compiler_digest()
    records: list[dict[str, object]] = []
    slr_shadow_records: list[dict[str, object]] = []
    cursor_trace_records: list[dict[str, object]] = []
    cursor_semantic_records: list[dict[str, object]] = []
    cursor_hybrid_semantic_records: list[dict[str, object]] = []
    accepted = 0
    rejected = 0
    authority_agreements = 0
    scalar_shadow_agreements = 0
    semantic_agreements = 0
    invalid_fail_closed = 0
    source_passes = 0
    dfa_scans = 0
    slr_shadow_accepted = 0
    slr_shadow_needs_general = 0
    slr_shadow_resource_limit = 0
    slr_shadow_invalid_utf8 = 0
    cursor_program_agreements = 0
    cursor_semantic_agreements = 0
    cursor_hybrid_semantic_agreements = 0
    cursor_hybrid_mutations = 0
    action_program_digests: set[str] = set()
    final_forest_benchmark_canaries = 0
    cursor_hybrid_only_canaries = 0
    growth_summary = None
    cursor_hybrid_growth_summary = None
    flat_replay_summary: FlatReplaySummary | None = None
    growth_authority_agreements = 0
    growth_reference_agreements = 0
    cursor_hybrid_growth_authority_agreements = 0

    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-he-guarded-lexical-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / "he.abi"
        lexical_path = directory / "he-lexical.nfa"
        guard_path = directory / "he-guard.nfa"
        evidence_path = directory / "he-guard.evidence"
        guarded_path = directory / "he-guarded.nfa"
        action_path = directory / "he-actions.pbc"
        abi_path.write_bytes(abi)
        lexical_path.write_text(
            "\n".join(lexical_terms) + "\n", encoding="utf-8"
        )
        guard_path.write_text(
            "\n".join(guard_terms) + "\n", encoding="utf-8"
        )
        evidence_path.write_bytes(evidence_stream(PROFILE, guard_row))
        guarded_path.write_text(
            "\n".join(guarded_terms) + "\n", encoding="utf-8"
        )
        action_path.write_text(
            "\n".join(action_terms) + "\n", encoding="utf-8"
        )
        paths = (
            abi_path,
            lexical_path,
            guard_path,
            evidence_path,
            guarded_path,
        )
        plan = parse_plan_stream(
            run_plan(
                plan_binary,
                paths,
                regular_digest,
                guarded_digest,
                expect_success=True,
            )
        )
        lr1 = summary_json(
            analyze_lr1(composite_grammar(parse_abi_stream(abi), plan))
        )
        require_plan_seals(plan, lr1)
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
            if decision == "accepted":
                accepted += 1
            else:
                rejected += 1

            if petta.get("decode") == "invalid-utf8":
                run_scalar(
                    scalar_exec_binary,
                    abi_path,
                    guard_path,
                    evidence_path,
                    input_path,
                    regular_digest,
                    expect_success=False,
                )
                run_exec(
                    exec_binary,
                    paths,
                    input_path,
                    regular_digest,
                    guarded_digest,
                    expect_success=False,
                    action_path=action_path,
                    action_compiler_digest=action_compiler_digest,
                )
                invalid_fail_closed += 1
                slr_shadow_invalid_utf8 += 1
                records.append(
                    {
                        "decision": decision,
                        "input": input_bytes.hex(),
                        "label": label,
                        "mode": "invalid-utf8",
                    }
                )
                slr_shadow_records.append(
                    {
                        "forest": "",
                        "input": input_bytes.hex(),
                        "label": label,
                        "outcome": "invalid-utf8",
                        "work": 0,
                    }
                )
                continue

            scalar = run_scalar(
                scalar_exec_binary,
                abi_path,
                guard_path,
                evidence_path,
                input_path,
                regular_digest,
                expect_success=True,
            )
            require_native_seals(scalar)
            lexical = run_exec(
                exec_binary,
                paths,
                input_path,
                regular_digest,
                guarded_digest,
                expect_success=True,
                action_path=action_path,
                action_compiler_digest=action_compiler_digest,
                benchmark_iterations=2 if label == "word" else None,
            )
            require_execution_seals(lexical)
            action_program_digests.add(
                str(lexical["cursor-program-action-program-digest"])
            )
            if lexical.get("cursor-program-outcome") != decision:
                raise GateFailure(
                    f"HE cursor program decision differs for {label}"
                )
            try:
                cursor_counts = {
                    name: int(str(lexical[f"cursor-program-{name}"]))
                    for name in (
                        "cursor-scans",
                        "tokens",
                        "scalar-tokens",
                        "ordinary-span-tokens",
                        "guarded-span-tokens",
                        "shifts",
                        "reductions",
                        "max-stack",
                        "dfa-work",
                        "parser-work",
                        "semantic-terminal-values",
                        "semantic-action-executions",
                        "semantic-action-instructions",
                        "semantic-mutations-killed",
                        "hybrid-value-program-runs",
                        "hybrid-value-terminal-values",
                        "hybrid-value-action-executions",
                        "hybrid-value-action-instructions",
                        "hybrid-prepared-fallback-runs",
                        "hybrid-prepared-fallback-work",
                        "hybrid-value-parser-work",
                        "hybrid-mutations-killed",
                    )
                }
            except (KeyError, ValueError) as error:
                raise GateFailure(
                    f"HE cursor program receipt is malformed for {label}"
                ) from error
            if (
                any(value < 0 for value in cursor_counts.values())
                or cursor_counts["tokens"] != cursor_counts["shifts"]
                or cursor_counts["tokens"]
                != cursor_counts["scalar-tokens"]
                + cursor_counts["ordinary-span-tokens"]
                + cursor_counts["guarded-span-tokens"]
                or cursor_counts["cursor-scans"]
                != cursor_counts["tokens"] + 1
                or cursor_counts["max-stack"] < 1
                or cursor_counts["semantic-terminal-values"]
                != cursor_counts["shifts"]
                or cursor_counts["semantic-action-executions"]
                != cursor_counts["reductions"]
                or cursor_counts["semantic-action-instructions"]
                < cursor_counts["semantic-action-executions"]
                or cursor_counts["semantic-mutations-killed"]
                != (
                    int(
                        cursor_counts["ordinary-span-tokens"]
                        + cursor_counts["guarded-span-tokens"]
                        > 0
                    )
                    + 3
                    * int(
                        int(str(lexical["cursor-program-value-programs"]))
                        > 0
                    )
                )
                or cursor_counts["hybrid-value-program-runs"]
                < cursor_counts["ordinary-span-tokens"]
                + cursor_counts["guarded-span-tokens"]
                or cursor_counts["hybrid-prepared-fallback-runs"] != 0
                or cursor_counts["hybrid-prepared-fallback-work"] != 0
                or cursor_counts["hybrid-value-parser-work"]
                < cursor_counts["hybrid-prepared-fallback-work"]
                or cursor_counts["hybrid-value-action-instructions"]
                < cursor_counts["hybrid-value-action-executions"]
                or cursor_counts["hybrid-mutations-killed"] != 0
            ):
                raise GateFailure(
                    f"HE cursor program receipt changed shape for {label}: "
                    f"{cursor_counts}"
                )
            cursor_program_agreements += 1
            cursor_semantic_agreements += 1
            cursor_hybrid_semantic_agreements += 1
            cursor_hybrid_mutations += cursor_counts[
                "hybrid-mutations-killed"
            ]
            cursor_trace_records.append(
                {
                    "cursor_scans": lexical["cursor-program-cursor-scans"],
                    "decision": lexical["cursor-program-outcome"],
                    "dfa_work": lexical["cursor-program-dfa-work"],
                    "guarded": lexical[
                        "cursor-program-guarded-span-tokens"
                    ],
                    "input": input_bytes.hex(),
                    "label": label,
                    "max_stack": lexical["cursor-program-max-stack"],
                    "ordinary": lexical[
                        "cursor-program-ordinary-span-tokens"
                    ],
                    "parser_work": lexical["cursor-program-parser-work"],
                    "reductions": lexical["cursor-program-reductions"],
                    "scalar": lexical["cursor-program-scalar-tokens"],
                    "shifts": lexical["cursor-program-shifts"],
                    "tokens": lexical["cursor-program-tokens"],
                    "trace": lexical["cursor-program-trace-digest"],
                }
            )
            cursor_semantic_records.append(
                {
                    "actions": cursor_counts["semantic-action-executions"],
                    "decision": lexical["cursor-program-outcome"],
                    "input": input_bytes.hex(),
                    "instructions": cursor_counts[
                        "semantic-action-instructions"
                    ],
                    "label": label,
                    "mutations": cursor_counts[
                        "semantic-mutations-killed"
                    ],
                    "terminal_values": cursor_counts[
                        "semantic-terminal-values"
                    ],
                }
            )
            cursor_hybrid_semantic_records.append(
                {
                    "decision": lexical["cursor-program-outcome"],
                    "fallback_runs": cursor_counts[
                        "hybrid-prepared-fallback-runs"
                    ],
                    "fallback_work": cursor_counts[
                        "hybrid-prepared-fallback-work"
                    ],
                    "input": input_bytes.hex(),
                    "label": label,
                    "value_parser_work": cursor_counts[
                        "hybrid-value-parser-work"
                    ],
                    "value_program_runs": cursor_counts[
                        "hybrid-value-program-runs"
                    ],
                }
            )
            if label == "word":
                require_final_forest_benchmark_canary(lexical)
                final_forest_benchmark_canaries += 1
                cursor_only = run_exec(
                    exec_binary,
                    paths,
                    input_path,
                    regular_digest,
                    guarded_digest,
                    expect_success=True,
                    action_path=action_path,
                    action_compiler_digest=action_compiler_digest,
                    benchmark_iterations=3,
                    cursor_hybrid_only=True,
                )
                require_cursor_hybrid_only_canary(
                    cursor_only, lexical, len(input_bytes), 3
                )
                cursor_hybrid_only_canaries += 1
            slr_outcome = lexical.get("slr-shadow-outcome")
            try:
                slr_work = int(str(lexical["slr-shadow-work-items"]))
            except (KeyError, ValueError) as error:
                raise GateFailure(
                    f"HE SLR shadow omitted its work receipt for {label}"
                ) from error
            if slr_work < 0:
                raise GateFailure(
                    f"HE SLR shadow reported negative work for {label}"
                )
            slr_digest = lexical.get("slr-shadow-forest-digest")
            if slr_outcome == "accepted":
                slr_shadow_accepted += 1
                if slr_digest != lexical.get("gll-forest-digest"):
                    raise GateFailure(
                        f"HE SLR and GLL forest digests differ for {label}"
                    )
            elif slr_outcome == "needs-general":
                slr_shadow_needs_general += 1
                if slr_digest != "":
                    raise GateFailure(
                        f"delegated HE SLR shadow exposed a digest for {label}"
                    )
            elif slr_outcome == "resource-limit":
                slr_shadow_resource_limit += 1
                if slr_digest != "":
                    raise GateFailure(
                        f"resource-limited HE SLR shadow exposed a digest for {label}"
                    )
            else:
                raise GateFailure(
                    f"HE SLR shadow has an unknown outcome for {label}: "
                    f"{slr_outcome!r}"
                )
            slr_shadow_records.append(
                {
                    "forest": slr_digest,
                    "input": input_bytes.hex(),
                    "label": label,
                    "outcome": slr_outcome,
                    "work": slr_work,
                }
            )
            if (
                scalar.get("gll-decision") != decision
                or lexical.get("gll-decision") != decision
                or scalar.get("gll-result") != lexical.get("gll-result")
            ):
                raise GateFailure(
                    f"HE scalar and guarded lexical paths differ for {label}"
                )
            scalar_shadow_agreements += 1
            if lexical.get("gll-result") != petta.get("results"):
                raise GateFailure(f"C and PeTTa HE semantics differ for {label}")
            semantic_agreements += 1
            source_passes += int(str(lexical["source-passes"]))
            dfa_scans += int(str(lexical["dfa-scans"]))
            if first_valid_input is None:
                first_valid_input = input_path
            records.append(
                {
                    "decision": decision,
                    "forest": lexical["gll-forest-digest"],
                    "guard_relation": lexical["guard-relation-digest"],
                    "guarded_tokens": lexical["guarded-tokens"],
                    "input": input_bytes.hex(),
                    "label": label,
                    "projected_relation": lexical[
                        "projected-relation-digest"
                    ],
                    "projected_tokens": lexical["projected-tokens"],
                    "results": lexical["gll-result"],
                }
            )

        if first_valid_input is None:
            raise GateFailure("HE guarded lexical matrix has no valid input")
        corrupt_guarded = directory / "corrupt-guarded.nfa"
        corrupt_guarded.write_bytes(b"")
        corrupt_paths = (*paths[:4], corrupt_guarded)
        run_plan(
            plan_binary,
            corrupt_paths,
            regular_digest,
            guarded_digest,
            expect_success=False,
        )
        missing_tag = directory / "missing-guarded-tag.nfa"
        missing_tag.write_text(
            "\n".join(
                term
                for term in guarded_terms
                if answer_tag(term) != GUARDED_TAGS[0]
            )
            + "\n",
            encoding="utf-8",
        )
        missing_paths = (*paths[:4], missing_tag)
        run_exec(
            exec_binary,
            missing_paths,
            first_valid_input,
            regular_digest,
            guarded_digest,
            expect_success=False,
        )
        mutations = 3
        if arguments.probe_cursor_hybrid_growth:
            def execute_cursor_hybrid_growth(
                input_path: Path, iterations: int
            ) -> dict[str, object]:
                row = run_exec(
                    exec_binary,
                    paths,
                    input_path,
                    regular_digest,
                    guarded_digest,
                    expect_success=True,
                    action_path=action_path,
                    action_compiler_digest=action_compiler_digest,
                    benchmark_iterations=iterations,
                    cursor_hybrid_only=True,
                )
                expected_identity = {
                    "base-pack-digest": PROFILE.base_pack_digest,
                    "lexical-plan-digest": PROFILE.lexical_plan_digest,
                    "execution-plan-digest": EXPECTED_EXECUTION_PLAN_DIGEST,
                    "cursor-program-digest": EXPECTED_CURSOR_PROGRAM_DIGEST,
                }
                if any(
                    row.get(field) != value
                    for field, value in expected_identity.items()
                ):
                    raise GateFailure(
                        "HE cursor growth artifact identity changed"
                    )
                return row

            def validate_cursor_hybrid_growth_authority(
                family: str, size: int, input_path: Path
            ) -> None:
                nonlocal cursor_hybrid_growth_authority_agreements
                if family == "word":
                    expected = (("sym", "a" * size),)
                elif family == "string":
                    expected = (("sym", '"' + "a" * size + '"'),)
                elif family == "many-words":
                    expected = tuple(("sym", "a") for _ in range(size))
                else:
                    raise GateFailure("unknown HE cursor growth family")
                if run_he_case(he_oracle, "atoms", input_path) != expected:
                    raise GateFailure(
                        f"HE authority changed for {family} size {size}"
                    )
                cursor_hybrid_growth_authority_agreements += 1

            try:
                cursor_hybrid_growth_summary = run_cursor_hybrid_growth(
                    "he",
                    directory,
                    arguments.probe_iterations,
                    EXPECTED_CURSOR_HYBRID_GROWTH_DIGEST,
                    execute_cursor_hybrid_growth,
                    validate_cursor_hybrid_growth_authority,
                )
            except CursorHybridProbeFailure as error:
                raise GateFailure(str(error)) from error
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
                    action_path=action_path,
                    action_compiler_digest=action_compiler_digest,
                    benchmark_iterations=iterations,
                )

            def validate_growth_authority(
                input_path: Path,
                token_count: int,
                row: dict[str, object],
            ) -> None:
                nonlocal growth_authority_agreements
                nonlocal growth_reference_agreements
                expected_value = ("expr", ())
                leaves = 1
                while leaves < token_count:
                    expected_value = (
                        "expr", (expected_value, expected_value)
                    )
                    leaves *= 2
                authority = run_he_case(he_oracle, "atoms", input_path)
                expected = (expected_value,)
                if authority != expected:
                    raise GateFailure(
                        "HE balanced-form authority changed during growth probe"
                    )
                growth_authority_agreements += 1
                if token_count == TOKEN_COUNTS[-1]:
                    return
                reference = run_petta(petta_root, input_path)
                if (
                    reference.get("decision") != "accepted"
                    or row.get("gll-result") != reference.get("results")
                ):
                    raise GateFailure(
                        "HE growth probe differs from the PeTTa reference"
                    )
                growth_reference_agreements += 1

            def validate_flat_authority(
                input_path: Path,
                leaf_count: int,
                row: dict[str, object],
            ) -> tuple[int, int]:
                expected = ((
                    "expr",
                    tuple(("expr", ()) for _ in range(leaf_count)),
                ),)
                authority = run_he_case(he_oracle, "atoms", input_path)
                if authority != expected:
                    raise GateFailure("HE flat replay authority changed")
                reference = run_petta(petta_root, input_path)
                if (
                    reference.get("decision") != "accepted"
                    or row.get("gll-result") != reference.get("results")
                ):
                    raise GateFailure(
                        "HE flat replay differs from the PeTTa reference"
                    )
                return 1, 1

            try:
                growth_summary = run_growth_probe(
                    "he",
                    directory,
                    arguments.probe_iterations,
                    execute_growth,
                    validate_growth_authority,
                )
                flat_replay_summary = run_flat_replay_canary(
                    "he",
                    directory,
                    arguments.probe_iterations,
                    execute_growth,
                    validate_flat_authority,
                )
            except FinalForestProbeFailure as error:
                raise GateFailure(str(error)) from error

    observed_plan_digest = plan_matrix_digest(plan, lr1)
    if (
        EXPECTED_PLAN_MATRIX_DIGEST
        and observed_plan_digest != EXPECTED_PLAN_MATRIX_DIGEST
    ):
        raise GateFailure(
            f"HE guarded lexical plan matrix changed: {observed_plan_digest}"
        )
    observed_execution_digest = matrix_digest(records)
    if (
        EXPECTED_EXECUTION_MATRIX_DIGEST
        and observed_execution_digest != EXPECTED_EXECUTION_MATRIX_DIGEST
    ):
        raise GateFailure(
            "HE guarded lexical execution matrix changed: "
            + observed_execution_digest
        )
    observed_slr_shadow_digest = matrix_digest(slr_shadow_records)
    observed_cursor_trace_digest = matrix_digest(cursor_trace_records)
    observed_cursor_semantic_digest = matrix_digest(cursor_semantic_records)
    observed_cursor_hybrid_semantic_digest = matrix_digest(
        cursor_hybrid_semantic_records
    )
    if (
        cursor_program_agreements != len(ATOM_CASES) - invalid_fail_closed
        or len(cursor_trace_records) != cursor_program_agreements
    ):
        raise GateFailure("HE cursor program coverage is incomplete")
    if not EXPECTED_CURSOR_TRACE_MATRIX_DIGEST:
        raise GateFailure(
            "HE cursor trace seal is not ratified: "
            + observed_cursor_trace_digest
        )
    if observed_cursor_trace_digest != EXPECTED_CURSOR_TRACE_MATRIX_DIGEST:
        raise GateFailure(
            "HE cursor trace matrix changed: "
            + observed_cursor_trace_digest
        )
    if not EXPECTED_CURSOR_SEMANTIC_MATRIX_DIGEST:
        print(
            "HE_CURSOR_SEMANTIC_RECORDS="
            + json.dumps(
                cursor_semantic_records, ensure_ascii=False, sort_keys=True
            )
        )
        print("HE_CURSOR_SEMANTIC_MATRIX=" + observed_cursor_semantic_digest)
        raise GateFailure("HE cursor semantic execution seal is not ratified")
    if (
        len(cursor_semantic_records) != len(cursor_trace_records)
        or cursor_semantic_agreements != len(cursor_trace_records)
        or observed_cursor_semantic_digest
        != EXPECTED_CURSOR_SEMANTIC_MATRIX_DIGEST
    ):
        raise GateFailure(
            "HE cursor semantic execution matrix changed: "
            + observed_cursor_semantic_digest
        )
    if not EXPECTED_CURSOR_HYBRID_SEMANTIC_MATRIX_DIGEST:
        print(
            "HE_CURSOR_HYBRID_SEMANTIC_RECORDS="
            + json.dumps(
                cursor_hybrid_semantic_records,
                ensure_ascii=False,
                sort_keys=True,
            )
        )
        print(
            "HE_CURSOR_HYBRID_SEMANTIC_MATRIX="
            + observed_cursor_hybrid_semantic_digest
        )
        raise GateFailure(
            "HE cursor hybrid semantic execution seal is not ratified"
        )
    if (
        len(cursor_hybrid_semantic_records) != len(cursor_trace_records)
        or cursor_hybrid_semantic_agreements != len(cursor_trace_records)
        or cursor_hybrid_mutations != 0
        or observed_cursor_hybrid_semantic_digest
        != EXPECTED_CURSOR_HYBRID_SEMANTIC_MATRIX_DIGEST
    ):
        raise GateFailure(
            "HE cursor hybrid semantic execution matrix changed: "
            + observed_cursor_hybrid_semantic_digest
        )
    if (
        len(slr_shadow_records) != len(ATOM_CASES)
        or slr_shadow_accepted
        + slr_shadow_needs_general
        + slr_shadow_resource_limit
        + slr_shadow_invalid_utf8
        != len(ATOM_CASES)
        or final_forest_benchmark_canaries != 1
        or cursor_hybrid_only_canaries != 1
    ):
        raise GateFailure("HE SLR shadow matrix is incomplete")
    if not EXPECTED_SLR_SHADOW_MATRIX_DIGEST:
        print(
            "HE_SLR_SHADOW_COUNTS="
            + repr(
                (
                    slr_shadow_accepted,
                    slr_shadow_needs_general,
                    slr_shadow_resource_limit,
                    slr_shadow_invalid_utf8,
                )
            )
        )
        print(
            "HE_SLR_SHADOW_RECORDS="
            + json.dumps(
                slr_shadow_records, ensure_ascii=False, sort_keys=True
            )
        )
        print("HE_SLR_SHADOW_MATRIX=" + observed_slr_shadow_digest)
        raise GateFailure("HE SLR shadow execution seal is not ratified")
    if observed_slr_shadow_digest != EXPECTED_SLR_SHADOW_MATRIX_DIGEST:
        raise GateFailure(
            "HE SLR shadow execution matrix changed: "
            + observed_slr_shadow_digest
        )
    print(
        f"(HEReaderGuardedLexicalPlanV1Summary {len(lexical_terms)} "
        f"{len(guard_terms)} {len(guarded_terms)} {lexical_agreements} "
        f"{compiler_negative_agreements} {plan['slr-states']} "
        f"{plan['slr-conflicts']} {lr1['states']} {lr1['conflicts']} "
        f"{EXPECTED_GUARDED_PLAN_DIGEST} {observed_plan_digest} 0)"
    )
    print(
        f"(HEReaderGuardedLexicalExecV1Summary {len(ATOM_CASES)} "
        f"{accepted} {rejected} {authority_agreements} "
        f"{scalar_shadow_agreements} {semantic_agreements} "
        f"{invalid_fail_closed} {source_passes} {dfa_scans} {mutations} "
        f"{EXPECTED_EXECUTION_PLAN_DIGEST} {observed_execution_digest} 0)"
    )
    print(
        "(HEReaderCursorCertificateV1Summary "
        "1 49 0 1 33 40 8 4 1 3 3 0 "
        f"{EXPECTED_CURSOR_CERTIFICATE_DIGEST} 0)"
    )
    print(
        "(HEReaderCursorProgramV1Summary "
        "241 1162 49 35 339 397 397 1764 16611 8 4 1 3 34 7 0 1 "
        f"{EXPECTED_CURSOR_PROGRAM_DIGEST} 0)"
    )
    if action_program_digests != {EXPECTED_GUARD_ACTION_PROGRAM_DIGEST}:
        raise GateFailure("HE cursor action program identity changed by input")
    print(
        "(HEReaderCursorActionProgramV1Summary "
        "397 517 397 3 "
        f"{EXPECTED_GUARD_ACTION_COMPILER_DIGEST} "
        f"{EXPECTED_GUARD_ACTION_ANSWER_DIGEST} "
        f"{EXPECTED_GUARD_ACTION_PROGRAM_DIGEST} 0)"
    )
    print(
        "(HEReaderCursorExecutionV1Summary "
        f"{len(cursor_trace_records)} {cursor_program_agreements} 0 "
        f"{observed_cursor_trace_digest} 0)"
    )
    print(
        "(HEReaderCursorSemanticV1Summary "
        f"{len(cursor_semantic_records)} {cursor_semantic_agreements} "
        f"{observed_cursor_semantic_digest} 0)"
    )
    print(
        "(HEReaderCursorHybridSemanticV1Summary "
        f"{len(cursor_hybrid_semantic_records)} "
        f"{cursor_hybrid_semantic_agreements} "
        f"{cursor_hybrid_mutations} "
        f"{observed_cursor_hybrid_semantic_digest} 0)"
    )
    print(
        f"(HEReaderSLRShadowV1Summary {len(slr_shadow_records)} "
        f"{slr_shadow_accepted} {slr_shadow_needs_general} "
        f"{slr_shadow_resource_limit} {slr_shadow_invalid_utf8} "
        f"{observed_slr_shadow_digest} 0)"
    )
    print(
        "(PreparedFinalForestBenchmarkCanaryV1Summary "
        f"{final_forest_benchmark_canaries} 3 0)"
    )
    print(
        "(HEReaderCursorHybridBytesBenchmarkCanaryV1Summary "
        f"{cursor_hybrid_only_canaries} 3 0)"
    )
    if cursor_hybrid_growth_summary is not None:
        if (
            cursor_hybrid_growth_authority_agreements
            != cursor_hybrid_growth_summary.cases
        ):
            raise GateFailure("HE cursor growth authority coverage changed")
        print(
            "(HEReaderCursorHybridGrowthV1Summary "
            f"{cursor_hybrid_growth_summary.cases} "
            f"{cursor_hybrid_growth_summary.iterations} "
            f"{cursor_hybrid_growth_authority_agreements} "
            f"{cursor_hybrid_growth_summary.time_slopes['word']:.6f} "
            f"{cursor_hybrid_growth_summary.work_slopes['word']:.6f} "
            f"{cursor_hybrid_growth_summary.time_slopes['string']:.6f} "
            f"{cursor_hybrid_growth_summary.work_slopes['string']:.6f} "
            f"{cursor_hybrid_growth_summary.time_slopes['many-words']:.6f} "
            f"{cursor_hybrid_growth_summary.work_slopes['many-words']:.6f} "
            f"{cursor_hybrid_growth_summary.structural_digest} 0)"
        )
    if growth_summary is not None:
        if (
            growth_authority_agreements != growth_summary.cases
            or growth_reference_agreements != growth_summary.cases - 1
        ):
            raise GateFailure("HE growth authority coverage changed")
        print(
            "(PreparedFinalForestGrowthV1Summary he "
            f"{growth_summary.cases} {growth_summary.iterations} "
            f"{growth_authority_agreements} "
            f"{growth_reference_agreements} "
            f"{growth_summary.process_time_slope:.6f} "
            f"{growth_summary.time_slopes['slr']:.6f} "
            f"{growth_summary.time_slopes['gll']:.6f} "
            f"{growth_summary.time_slopes['glr']:.6f} "
            f"{growth_summary.structural_digest} 0)"
        )
        if (
            growth_summary.cursor_hybrid_time_slope is not None
            and growth_summary.cursor_hybrid_work_slope is not None
            and growth_summary.largest_cursor_hybrid_nanoseconds is not None
            and growth_summary.largest_cursor_hybrid_work is not None
        ):
            print(
                "(HEReaderCursorHybridBalancedReferenceGrowthV1Summary "
                f"{growth_summary.cases} {growth_summary.iterations} "
                f"{growth_summary.cursor_hybrid_time_slope:.6f} "
                f"{growth_summary.cursor_hybrid_work_slope:.6f} "
                f"{growth_summary.largest_cursor_hybrid_nanoseconds:.0f} "
                f"{growth_summary.largest_cursor_hybrid_work} 0)"
            )
    if flat_replay_summary is not None:
        print(
            "(PreparedFinalForestFlatReplayV1Summary he "
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
