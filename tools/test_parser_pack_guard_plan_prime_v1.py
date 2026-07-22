#!/usr/bin/env python3
"""Cross-executor composition gate for a versioned reader profile."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_guard_compiler_v1 import EXPECTED_DIGESTS
from test_parser_pack_guard_compiler_v1 import EXPECTED_COUNTS
from test_parser_pack_guard_compiler_v1 import EXPECTED_PROVENANCE
from test_parser_pack_guard_compiler_v1 import program_arguments
from test_parser_pack_guard_compiler_v1 import run_native as run_guard_native
from test_parser_pack_guard_regular_v1 import answer_tag
from test_parser_pack_guard_regular_v1 import native_row as guard_nfa_row
from test_parser_pack_lexical_v1 import compile_case_tags
from test_regular_span_dfa_v1 import abi_scalar
from parser_pack_lr1_v1 import (
    AnalysisFailure,
    analyze_lr1,
    composite_grammar,
    parse_abi_stream,
    parse_plan_stream,
    summary_json,
)


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
REGULAR_COMPILER = (
    PRESENTATIONS / "compiler" / "regular_span_compiler_v1.metta"
)

@dataclass(frozen=True, slots=True)
class ReaderProfile:
    label: str
    profile_name: str
    summary_name: str
    matrix_domain: str
    lexical_case: dict[str, object]
    base_pack_digest: str
    lexical_nfa_digest: str
    lexical_plan_digest: str
    guard_nfa_digest: str
    guard_plan_digest: str
    evidence_digest: str
    matrix_digest: str
    composite_terminal_extensions: int
    composite_grammar_productions: int


PROFILES = {
    "he": ReaderProfile(
        label="he",
        profile_name="he-empty-tokenizer-reader-v1",
        summary_name="ParserPackPositiveGuardPlanHEV1MatrixSummary",
        matrix_domain="ParserPackPositiveGuardPlanHEV1",
        lexical_case={
            "language": "he",
            "tag": "he-string",
            "answers": 716,
            "answer_digest":
                "36de2ebbf582532c577a334730beb952d9c09eed2edc8b8fe6fc899cad880b6f",
            "extra_tags": (
                (
                    "he-comment-boundary", 8,
                    "7f3036d6953c0ea8e6b6ecf6bb6876d1b290c44f3f0522d237538fd4ee319e44",
                ),
                (
                    "he-word-boundary", 23,
                    "1eba771b1ea2e9be57772e78672a795c85952417ac8fee689d256267c242ff17",
                ),
                (
                    "he-variable-boundary", 23,
                    "dd7396f812e94a06ba02832d8e527cdcd39331ff95d68015cede5b79c816fe41",
                ),
            ),
        },
        base_pack_digest=
            "a0334e22b53a8319b23870d008af3fcdd27426f26bffd09f14f54565c12936c0",
        lexical_nfa_digest=
            "32b753c815e05d46d4152c517b5749d4beafe5810aabca89d8d91dc2edf7440e",
        lexical_plan_digest=
            "fb8658f0924ad4933dc75fb6e749687d1b63efaa94613aaf780362e041cc799b",
        guard_nfa_digest=
            "0a680733ee1509d1b72d7f70fd43caca2ca1d914b3f806c16d5090709e930d34",
        guard_plan_digest=
            "1302f1eba1e7c64223c3d40498df28766d6931f077fe9b159847013d1c03b895",
        evidence_digest=
            "2490b06eb70b2295cfa161f1d3fa8d73a33d3c7686264ce55619261e9e4f95cb",
        matrix_digest=
            "2cf1dc1b61a70a188cbe75b0ee5e36ebb90d329caba0bd302aebaea6e01610d4",
        composite_terminal_extensions=7,
        composite_grammar_productions=282,
    ),
    "prime": ReaderProfile(
        label="prime",
        profile_name="cetta-prime-reader-v1",
        summary_name="ParserPackPositiveGuardPlanPrimeV1MatrixSummary",
        matrix_domain="ParserPackPositiveGuardPlanPrimeV1",
        lexical_case={
            "language": "prime",
            "tag": "cetta-prime-string-escape",
            "answers": 7,
            "answer_digest":
                "be77d7dc410f2c08036f60b96c4ce1907884212fbe1293e3a6d4e8a274851fa3",
            "extra_tags": ((
                "cetta-prime-token-boundary", 28,
                "ac38d7df7c8658e497d1dd2ffe455140a70319574ee474d13297d0ae2364e3c2",
            ),),
        },
        base_pack_digest=
            "103d3ba48e6cebf4649e37387f5b379cbad3be3a9f12dc4e6a6c33c0bc17fc50",
        lexical_nfa_digest=
            "42c38a0bb68c48422d499d8f1983e01c29274419e5df771ce92b3db751c336a7",
        lexical_plan_digest=
            "870d472f3e0aa8a2f0d6176b9057087c99acecf8e538f26dabe9b7efed3927a2",
        guard_nfa_digest=
            "72229e01844933b6e5cd179fd92b792ffb5ab02c302cf5cf98403d4cd1768993",
        guard_plan_digest=
            "774bacc52b488bfa63e1f7af2e8f051b7cb555743a0644c4488db5aaa7a5b6d6",
        evidence_digest=
            "b06ca9109b4a03aaceae80be0c28d2060d78cc3c4a210b974ab6803121790f70",
        matrix_digest=
            "c3b3c17f747762fba00c0d7c8674dcdc7add71bf93671e52248ab98894846ec7",
        composite_terminal_extensions=9,
        composite_grammar_productions=186,
    ),
}


DETERMINISM_SEALS = {
    "he": {
        "slr": {
            "slr-accepts": "1",
            "slr-conflicts": "0",
            "slr-gotos": "155",
            "slr-reductions": "362",
            "slr-shifts": "38",
            "slr-states": "82",
        },
        "lr1": {
            "accepts": 1,
            "conflict_cells": 0,
            "conflicts": 0,
            "gotos": 181,
            "grammar_digest":
                "e9e218d104b9659917c4926457464df960331b53257a996a91d222cb3b54d3b7",
            "max_state_items": 146,
            "nonterminals": 56,
            "productions": 282,
            "reachable_productions": 66,
            "reductions": 546,
            "shifts": 47,
            "states": 119,
            "table_digest":
                "1007e25c12b884dcd3bab0a371f28fd7b6fe12843f07abc8154aaa2e61ec687c",
            "terminals": 14,
        },
    },
}


class GateFailure(RuntimeError):
    pass


def profile_guard_row(
    chart_binary: Path, profile: ReaderProfile
) -> dict[str, object]:
    expected_count = EXPECTED_COUNTS[profile.label]
    row = run_guard_native(
        chart_binary,
        [
            *program_arguments(profile.label),
            "--query-text",
            "(compile-pack-positive-guard ?owner ?guard)",
            "--certs",
            "--timeout",
            "30",
        ],
    )
    if (
        row.get("outcome") != "Ambiguous"
        or row.get("answers") != expected_count
        or row.get("term_digest") != EXPECTED_DIGESTS[profile.label]
        or not isinstance(row.get("terms"), list)
        or not isinstance(row.get("certs"), list)
        or len(row["terms"]) != expected_count
        or len(row["certs"]) != expected_count
    ):
        raise GateFailure(
            f"native {profile.label} positive-guard evidence changed"
        )
    return row


def replay_guard_roots(
    chart_binary: Path,
    profile: ReaderProfile,
    row: dict[str, object],
) -> int:
    terms = row["terms"]
    certificates = row["certs"]
    assert isinstance(terms, list)
    assert isinstance(certificates, list)
    replayed = 0
    with tempfile.TemporaryDirectory(
        prefix=f"gslt2parse-{profile.label}-guard-plan-replay-"
    ) as raw:
        directory = Path(raw)
        for index, (term, expected_certificate) in enumerate(
            zip(terms, certificates, strict=True)
        ):
            path = directory / f"guard-{index}.cert"
            emitted = run_guard_native(
                chart_binary,
                [
                    *program_arguments(profile.label),
                    "--query-text",
                    str(term),
                    "--cert-out",
                    str(path),
                    "--timeout",
                    "30",
                ],
            )
            if (
                emitted.get("outcome") != "Unique"
                or not path.is_file()
                or path.read_text(encoding="utf-8").strip()
                != expected_certificate
            ):
                raise GateFailure(
                    f"{profile.label} guard evidence {index} changed"
                )
            replay = run_guard_native(
                chart_binary,
                [
                    *program_arguments(profile.label),
                    "--query-text",
                    str(term),
                    "--replay",
                    str(path),
                ],
            )
            if replay.get("replay") != "ACCEPT":
                raise GateFailure(
                    f"{profile.label} guard evidence {index} did not replay"
                )
            replayed += 1
    return replayed


def evidence_stream(
    profile: ReaderProfile, row: dict[str, object]
) -> bytes:
    provenance = EXPECTED_PROVENANCE[profile.label]
    terms = row["terms"]
    certificates = row["certs"]
    assert isinstance(terms, list)
    assert isinstance(certificates, list)
    derivations = sorted(zip(terms, certificates, strict=True))
    lines = [
        "parser-pack-positive-guard-evidence-v1",
        f"source-digest\t{provenance['source_digest']}",
        f"pre-reflection-digest\t{provenance['pre_reflection_digest']}",
        f"environment-digest\t{provenance['environment_digest']}",
        f"answer-set-digest\t{EXPECTED_DIGESTS[profile.label]}",
    ]
    lines.extend(
        f"derivation\t{term}\t{certificate}"
        for term, certificate in derivations
    )
    lines.append("end")
    return ("\n".join(lines) + "\n").encode("utf-8")


def run_petta(
    petta_root: Path, profile: ReaderProfile
) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_parser_pack_guard_plan_prime_v1.pl"
    )
    completed = subprocess.run(
        [
            "swipl", "-q", "-f", str(script), "--",
            str(PRESENTATIONS), profile.label,
        ],
        cwd=script.parent,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=90,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"PeTTa {profile.label} guard-plan reference failed: "
            + completed.stderr.strip()
        )
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa guard-plan reference emitted non-JSON") from error
    if not isinstance(result, dict):
        raise GateFailure("PeTTa guard-plan reference omitted its object")
    return result


def parse_native_output(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "parser-pack-positive-guard-plan-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("native guard-plan stream has malformed framing")
    result: dict[str, object] = {
        "lexical-tags": [],
        "lexical-entries": [],
        "guard-tags": [],
        "guard-entries": [],
        "guard-entry-evidence": [],
    }
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "lexical-tag" and len(fields) == 2:
            tags = result["lexical-tags"]
            assert isinstance(tags, list)
            tags.append(fields[1])
        elif fields[0] == "guard-tag" and len(fields) == 2:
            tags = result["guard-tags"]
            assert isinstance(tags, list)
            tags.append(fields[1])
        elif fields[0] == "lexical-entry" and len(fields) == 6:
            entries = result["lexical-entries"]
            assert isinstance(entries, list)
            entries.append([
                int(fields[1]),
                int(fields[2]),
                int(fields[3]),
                fields[4],
                fields[5],
            ])
        elif fields[0] == "guard-entry" and len(fields) == 10:
            entries = result["guard-entries"]
            assert isinstance(entries, list)
            entries.append([
                int(fields[1]),
                int(fields[2]),
                int(fields[3]),
                int(fields[4]),
                *fields[5:],
            ])
        elif fields[0] == "guard-entry-evidence" and len(fields) == 4:
            evidence = result["guard-entry-evidence"]
            assert isinstance(evidence, list)
            evidence.append([int(value) for value in fields[1:]])
        elif len(fields) == 2 and fields[0] not in result:
            result[fields[0]] = fields[1]
        else:
            raise GateFailure(f"malformed native guard-plan record: {line!r}")
    return result


def run_native_plan_stream(
    binary: Path,
    abi_path: Path,
    lexical_path: Path,
    guard_path: Path,
    evidence_path: Path,
    regular_compiler_digest: str,
    *,
    expect_success: bool = True,
) -> str:
    completed = subprocess.run(
        [
            str(binary),
            str(abi_path),
            str(lexical_path),
            str(guard_path),
            str(evidence_path),
            regular_compiler_digest,
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
                "native Prime guard-plan composition failed: "
                + completed.stderr.strip()
            )
        return completed.stdout
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("corrupt Prime guard-plan input did not fail closed")
    return ""


def run_native_plan(
    binary: Path,
    abi_path: Path,
    lexical_path: Path,
    guard_path: Path,
    evidence_path: Path,
    regular_compiler_digest: str,
    *,
    expect_success: bool = True,
) -> dict[str, object]:
    output = run_native_plan_stream(
        binary, abi_path, lexical_path, guard_path, evidence_path,
        regular_compiler_digest, expect_success=expect_success,
    )
    if expect_success:
        return parse_native_output(output)
    return {}


def require_exact(
    profile: ReaderProfile,
    native: dict[str, object],
    petta: dict[str, object],
    lexical_terms: tuple[str, ...],
    guard_row: dict[str, object],
    guard_nfa: dict[str, object],
    compiler_digest: str,
) -> None:
    if (
        petta.get("protocol")
        != "parser-pack-positive-guard-plan-v1"
        or petta.get("profile") != profile.profile_name
    ):
        raise GateFailure(
            f"PeTTa {profile.label} guard-plan protocol changed"
        )
    scalar_pairs = {
        "base-pack-digest": "base_pack_digest",
        "base-states": "base_state_count",
        "base-terminals": "base_terminal_count",
        "base-productions": "base_production_count",
        "regular-compiler-digest": "regular_compiler_digest",
        "lexical-nfa-answer-digest": "lexical_nfa_answer_digest",
        "lexical-nfa-answers": "lexical_nfa_answer_count",
        "lexical-plan-digest": "lexical_plan_digest",
        "guard-source-digest": "guard_source_digest",
        "guard-pre-reflection-digest": "guard_pre_reflection_digest",
        "guard-environment-digest": "guard_environment_digest",
        "guard-answer-set-digest": "guard_answer_set_digest",
        "guard-nfa-answer-digest": "guard_nfa_answer_digest",
        "guard-nfa-answers": "guard_nfa_answer_count",
        "guard-plan-digest": "guard_plan_digest",
        "guard-entry-count": "guard_entry_count",
        "guard-state-extension-count": "guard_state_extension_count",
        "start-closed": "start_closed",
    }
    numeric = {
        "base-states",
        "base-terminals",
        "base-productions",
        "lexical-nfa-answers",
        "guard-nfa-answers",
        "guard-entry-count",
        "guard-state-extension-count",
        "start-closed",
    }
    for native_field, petta_field in scalar_pairs.items():
        native_value: object = native.get(native_field)
        if native_field in numeric and isinstance(native_value, str):
            native_value = int(native_value)
        if native_value != petta.get(petta_field):
            raise GateFailure(
                f"C and PeTTa {profile.label} plans differ at {native_field}: "
                f"C={native_value!r}, PeTTa={petta.get(petta_field)!r}"
            )
    expected_scalars = {
        "base-pack-digest": profile.base_pack_digest,
        "regular-compiler-digest": compiler_digest,
        "lexical-nfa-answer-digest": profile.lexical_nfa_digest,
        "lexical-plan-digest": profile.lexical_plan_digest,
        "guard-answer-set-digest": EXPECTED_DIGESTS[profile.label],
        "guard-nfa-answer-digest": profile.guard_nfa_digest,
        "guard-plan-digest": profile.guard_plan_digest,
        "composite-terminal-extension-count":
            str(profile.composite_terminal_extensions),
        "guard-production-extension-count":
            str(EXPECTED_COUNTS[profile.label]),
        "composite-grammar-productions":
            str(profile.composite_grammar_productions),
    }
    for field, value in expected_scalars.items():
        if native.get(field) != value:
            raise GateFailure(
                f"{profile.label} composite plan changed at {field}: "
                f"expected {value!r}, observed {native.get(field)!r}"
            )
    if (
        native.get("lexical-entries") != petta.get("lexical_entries")
        or native.get("guard-entries") != petta.get("guard_entries")
        or native.get("guard-tags") != petta.get("guard_tags")
        or petta.get("lexical_nfa_terms") != list(lexical_terms)
        or petta.get("guard_answer_terms") != guard_row.get("terms")
        or petta.get("guard_nfa_terms") != guard_nfa.get("terms")
    ):
        raise GateFailure(
            f"C and PeTTa {profile.label} plan inventories differ"
        )
    evidence_layout = native.get("guard-entry-evidence")
    expected_evidence_layout = [
        [index, index, 1]
        for index in range(EXPECTED_COUNTS[profile.label])
    ]
    if evidence_layout != expected_evidence_layout:
        raise GateFailure(f"native {profile.label} proof layout changed")
    evidence_digest = native.get("guard-evidence-digest")
    if (
        not isinstance(evidence_digest, str)
        or re.fullmatch(r"[0-9a-f]{64}", evidence_digest) is None
        or (
            profile.evidence_digest
            and evidence_digest != profile.evidence_digest
        )
    ):
        raise GateFailure(
            f"native {profile.label} evidence digest changed: "
            + str(evidence_digest)
        )


def require_determinism_seal(
    profile: ReaderProfile,
    abi: bytes,
    plan_stream: str,
    native: dict[str, object],
) -> None:
    seal = DETERMINISM_SEALS.get(profile.label)
    if seal is None:
        return
    expected_slr = seal["slr"]
    assert isinstance(expected_slr, dict)
    observed_slr = {field: native.get(field) for field in expected_slr}
    for field, expected in expected_slr.items():
        if native.get(field) != expected:
            raise GateFailure(
                f"{profile.label} deterministic SLR seal changed at {field}: "
                f"expected {expected!r}, observed {native.get(field)!r}; "
                f"full observed seal={observed_slr!r}"
            )
    try:
        lr1 = summary_json(analyze_lr1(composite_grammar(
            parse_abi_stream(abi), parse_plan_stream(plan_stream)
        )))
    except AnalysisFailure as error:
        raise GateFailure(
            f"{profile.label} canonical LR(1) analysis failed: {error}"
        ) from error
    expected_lr1 = seal["lr1"]
    if lr1 != expected_lr1:
        raise GateFailure(
            f"{profile.label} deterministic LR(1) seal changed: {lr1}"
        )


def mutation_gates(
    profile: ReaderProfile,
    binary: Path,
    abi_path: Path,
    lexical_path: Path,
    guard_path: Path,
    evidence_path: Path,
    compiler_digest: str,
    guard_terms: tuple[str, ...],
    guard_tags: list[str],
    directory: Path,
) -> int:
    if not guard_tags:
        raise GateFailure(
            f"{profile.label} guard plan has no tags to mutate"
        )
    missing_tag = directory / "guard-missing-tag.nfa"
    reduced = tuple(
        term for term in guard_terms if answer_tag(term) != guard_tags[0]
    )
    missing_tag.write_text("\n".join(reduced) + "\n", encoding="utf-8")
    run_native_plan(
        binary, abi_path, lexical_path, missing_tag, evidence_path,
        compiler_digest, expect_success=False,
    )

    wrong_digest = directory / "guard-wrong-digest.evidence"
    evidence = evidence_path.read_text(encoding="utf-8")
    wrong_digest.write_text(
        evidence.replace(
            EXPECTED_DIGESTS[profile.label], "0" * 64, 1
        ),
        encoding="utf-8",
    )
    run_native_plan(
        binary, abi_path, lexical_path, guard_path, wrong_digest,
        compiler_digest, expect_success=False,
    )

    duplicate_lexical = directory / "lexical-duplicate.nfa"
    lexical_lines = lexical_path.read_text(encoding="utf-8").splitlines()
    duplicate_lexical.write_text(
        "\n".join((lexical_lines[0], *lexical_lines)) + "\n",
        encoding="utf-8",
    )
    run_native_plan(
        binary, abi_path, duplicate_lexical, guard_path, evidence_path,
        compiler_digest, expect_success=False,
    )

    unframed_evidence = directory / "guard-unframed.evidence"
    unframed_evidence.write_bytes(evidence_path.read_bytes().rstrip(b"\n"))
    run_native_plan(
        binary, abi_path, lexical_path, guard_path, unframed_evidence,
        compiler_digest, expect_success=False,
    )

    run_native_plan(
        binary, abi_path, lexical_path, guard_path, evidence_path,
        "not-a-sha256-digest", expect_success=False,
    )
    return 5


def matrix_digest(
    profile: ReaderProfile, native: dict[str, object]
) -> str:
    payload = (
        f"{profile.matrix_domain}\n"
        f"{profile.profile_name}\n"
        f"{native['base-pack-digest']}\n"
        f"{native['lexical-plan-digest']}\n"
        f"{native['guard-plan-digest']}\n"
        f"{native['guard-evidence-digest']}\n"
    )
    return sha256(payload.encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--plan-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument(
        "--profile", choices=sorted(PROFILES), default="prime"
    )
    arguments = parser.parse_args()
    profile = PROFILES[arguments.profile]
    chart_binary = arguments.chart_binary.resolve()
    plan_binary = arguments.plan_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not plan_binary.is_file():
        raise GateFailure("native guard-plan binary is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa guard-plan checkout is absent")

    compiler_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    abi = export_stream(petta_root, ABI_CASES[profile.label])
    lexical_terms, _, lexical_agreements = compile_case_tags(
        chart_binary,
        petta_root,
        dict(profile.lexical_case),
        abi_scalar(abi, "source-digest"),
    )
    guard_row = profile_guard_row(chart_binary, profile)
    guard_replays = replay_guard_roots(chart_binary, profile, guard_row)
    guard_nfa = guard_nfa_row(chart_binary, profile.label)
    guard_terms = tuple(str(term) for term in guard_nfa["terms"])
    petta = run_petta(petta_root, profile)

    with tempfile.TemporaryDirectory(
        prefix=f"gslt2parse-{profile.label}-guard-plan-"
    ) as raw:
        directory = Path(raw)
        abi_path = directory / f"{profile.label}.abi"
        lexical_path = directory / f"{profile.label}-lexical.nfa"
        guard_path = directory / f"{profile.label}-guard.nfa"
        evidence_path = directory / f"{profile.label}-guard.evidence"
        abi_path.write_bytes(abi)
        lexical_path.write_text(
            "\n".join(lexical_terms) + "\n", encoding="utf-8"
        )
        guard_path.write_text(
            "\n".join(guard_terms) + "\n", encoding="utf-8"
        )
        evidence_path.write_bytes(evidence_stream(profile, guard_row))
        plan_stream = run_native_plan_stream(
            plan_binary,
            abi_path,
            lexical_path,
            guard_path,
            evidence_path,
            compiler_digest,
        )
        native = parse_native_output(plan_stream)
        require_exact(
            profile, native, petta, lexical_terms,
            guard_row, guard_nfa, compiler_digest,
        )
        require_determinism_seal(profile, abi, plan_stream, native)
        mutations = mutation_gates(
            profile,
            plan_binary,
            abi_path,
            lexical_path,
            guard_path,
            evidence_path,
            compiler_digest,
            guard_terms,
            list(native["guard-tags"]),
            directory,
        )
    digest = matrix_digest(profile, native)
    if profile.matrix_digest and digest != profile.matrix_digest:
        raise GateFailure(
            f"{profile.label} guard-plan matrix digest changed: {digest}"
        )
    print(
        f"({profile.summary_name} "
        f"{len(lexical_terms)} {len(guard_terms)} "
        f"{EXPECTED_COUNTS[profile.label]} "
        f"1 {lexical_agreements} {guard_replays} "
        f"{mutations} {native['guard-plan-digest']} "
        f"{native['guard-evidence-digest']} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
