#!/usr/bin/env python3
"""Full-file semantic and timing differential for prepared HE GSLT parsing."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
from hashlib import sha256
import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
from typing import Any

from gslt2parse_schema_v1 import render
from sexpr_atom_projection_plan_v1 import compile_artifact
from test_he_document_pipeline_v1 import compile_semantic_mask_artifact
from test_he_document_pipeline_v1 import EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST
from test_he_document_pipeline_v1 import EXPECTED_SEMANTIC_MASK_BINDING_DIGEST
from test_he_document_pipeline_v1 import EXPECTED_SEMANTIC_MASK_COMPILER_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_CURSOR_CERTIFICATE_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_CURSOR_PROGRAM_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_EXECUTION_PLAN_DIGEST
from test_he_reader_guarded_lexical_v1 import EXPECTED_GUARD_ACTION_PROGRAM_DIGEST
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
from test_stable_parser_parse_only_v1 import build_he_helper
from test_stable_parser_parse_only_v1 import corpus_cases
from test_stable_parser_parse_only_v1 import pin_authorities
from test_stable_parser_parse_only_v1 import records_digest
from test_stable_parser_parse_only_v1 import run_he
from test_stable_parser_parse_only_v1 import structural_record


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_MATRIX_DIGEST = (
    "7b72e3364011354425891a217ecde239b6b5eaee7cd450b1c231e63da59b8f0d"
)


class GateFailure(RuntimeError):
    pass


def parse_stream(raw: bytes) -> dict[str, str]:
    try:
        lines = raw.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise GateFailure("prepared HE parse-only helper emitted invalid UTF-8") from error
    if (
        not lines
        or lines[0] != "he-gslt-document-parse-only-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("prepared HE parse-only receipt framing changed")
    fields: dict[str, str] = {}
    for line in lines[1:-1]:
        name, separator, value = line.partition("\t")
        if not separator or not name or name in fields:
            raise GateFailure("prepared HE parse-only receipt is malformed")
        fields[name] = value
    return fields


def nonnegative_int(fields: dict[str, str], name: str, *, positive: bool) -> int:
    try:
        value = int(fields[name])
    except (KeyError, ValueError) as error:
        raise GateFailure(f"prepared HE parse-only receipt omitted {name}") from error
    if value < 0 or (positive and value == 0):
        raise GateFailure(f"prepared HE parse-only receipt has invalid {name}")
    return value


def run_prepared(
    binary: Path,
    paths: tuple[Path, ...],
    projection: Path,
    input_path: Path,
    regular_digest: str,
    guarded_digest: str,
    action_digest: str,
    semantic_mask_digest: str,
    projection_plan_digest: str,
    warmup: int,
    iterations: int,
) -> dict[str, Any]:
    completed = subprocess.run(
        [
            str(binary),
            *(str(path) for path in paths),
            str(projection),
            str(input_path),
            regular_digest,
            guarded_digest,
            action_digest,
            semantic_mask_digest,
            str(warmup),
            str(iterations),
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=900,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "prepared HE parse-only helper failed: "
            + completed.stderr.decode("utf-8", errors="replace").strip()
        )
    fields = parse_stream(completed.stdout)
    if (
        fields.get("prepared-builds") != "1"
        or fields.get("source-passes-per-parse") != "1"
        or fields.get("prepared-fallback-runs-per-parse") != "0"
        or fields.get("execution-plan-digest") != EXPECTED_EXECUTION_PLAN_DIGEST
        or fields.get("cursor-certificate-digest")
        != EXPECTED_CURSOR_CERTIFICATE_DIGEST
        or fields.get("cursor-program-digest") != EXPECTED_CURSOR_PROGRAM_DIGEST
        or fields.get("action-program-digest")
        != EXPECTED_GUARD_ACTION_PROGRAM_DIGEST
        or fields.get("semantic-mask-compiler-digest")
        != EXPECTED_SEMANTIC_MASK_COMPILER_DIGEST
        or fields.get("semantic-mask-answer-digest")
        != EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST
        or fields.get("semantic-mask-binding-digest")
        != EXPECTED_SEMANTIC_MASK_BINDING_DIGEST
        or fields.get("semantic-mask-bound-terminals") != "3"
        or fields.get("atom-projection-plan-digest") != projection_plan_digest
    ):
        raise GateFailure("prepared HE parse-only staging receipt changed")
    semantic = fields.get("semantic-fnv64", "")
    if len(semantic) != 16 or any(char not in "0123456789abcdef" for char in semantic):
        raise GateFailure("prepared HE parse-only fingerprint is malformed")
    return {
        "input_bytes": nonnegative_int(fields, "input-bytes", positive=False),
        "items": nonnegative_int(fields, "top-level-atoms", positive=True),
        "nodes": nonnegative_int(fields, "atom-nodes", positive=True),
        "semantic": semantic,
        "warmup": nonnegative_int(fields, "warmup-iterations", positive=False),
        "iterations": nonnegative_int(fields, "measured-iterations", positive=True),
        "nanoseconds": nonnegative_int(fields, "total-nanoseconds", positive=True),
        "input_scalars": nonnegative_int(fields, "input-scalars", positive=False),
        "tokens": nonnegative_int(fields, "cursor-tokens", positive=False),
        "dfa_work": nonnegative_int(fields, "cursor-dfa-work", positive=False),
        "parser_work": nonnegative_int(
            fields, "cursor-parser-work", positive=False
        ),
        "terminal_values": nonnegative_int(
            fields, "terminal-values", positive=False
        ),
        "action_executions": nonnegative_int(
            fields, "action-executions", positive=False
        ),
        "action_instructions": nonnegative_int(
            fields, "action-instructions", positive=False
        ),
        "value_program_runs": nonnegative_int(
            fields, "value-program-runs", positive=False
        ),
        "value_parser_work": nonnegative_int(
            fields, "value-parser-work", positive=False
        ),
        "semantic_mask_runs": nonnegative_int(
            fields, "semantic-mask-runs", positive=False
        ),
        "semantic_mask_input_scalars": nonnegative_int(
            fields, "semantic-mask-input-scalars", positive=False
        ),
        "semantic_mask_retained_scalars": nonnegative_int(
            fields, "semantic-mask-retained-scalars", positive=False
        ),
        "semantic_mask_work": nonnegative_int(
            fields, "semantic-mask-work", positive=False
        ),
    }


def timed_samples(
    runner: Any,
    probe: dict[str, Any],
    target_ms: int,
    samples: int,
) -> tuple[int, int]:
    probe_ns = max(1, probe["nanoseconds"] // probe["iterations"])
    iterations = max(
        1,
        min(10_000, round(target_ms * 1_000_000 / probe_ns)),
    )
    values: list[int] = []
    for _ in range(samples):
        observed = runner(2, iterations)
        if (
            observed["input_bytes"] != probe["input_bytes"]
            or observed["items"] != probe["items"]
            or observed["nodes"] != probe["nodes"]
            or observed["semantic"] != probe["semantic"]
        ):
            raise GateFailure("HE parse-only semantics changed during timing")
        values.append(observed["nanoseconds"] // iterations)
    median = int(statistics.median(values))
    if median <= 0:
        raise GateFailure("HE parse-only timer resolution is insufficient")
    return iterations, median


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--pipeline-binary", type=Path, required=True)
    parser.add_argument("--he-root", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--target-ms", type=int, default=250)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--artifacts-dir", type=Path)
    arguments = parser.parse_args()
    if arguments.target_ms <= 0 or arguments.samples <= 0:
        parser.error("target-ms and samples must be positive")

    chart_binary = arguments.chart_binary.resolve()
    pipeline_binary = arguments.pipeline_binary.resolve()
    he_root = arguments.he_root.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not pipeline_binary.is_file():
        raise GateFailure("prepared HE parse-only binary dependency is absent")
    pin_authorities(he_root, petta_root)
    he_binary = build_he_helper(he_root)

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
    petta_source, petta_guarded = petta_guarded_answers(petta_root)
    if petta_source != source_digest or petta_guarded != guarded_terms:
        raise GateFailure("prepared HE parse-only compiler artifacts differ")
    regular_digest = sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    guarded_digest = sha256(GUARDED_COMPILER.read_bytes()).hexdigest()
    action_terms = native_guard_action_answers(chart_binary)
    action_digest = guard_action_compiler_digest()
    semantic_mask_terms, semantic_mask_answer_digest = (
        compile_semantic_mask_artifact(chart_binary)
    )
    semantic_mask_compiler_digest = EXPECTED_SEMANTIC_MASK_COMPILER_DIGEST
    if semantic_mask_answer_digest != EXPECTED_SEMANTIC_MASK_ANSWER_DIGEST:
        raise GateFailure("prepared HE semantic-mask artifact changed")
    projection_artifact = compile_artifact("he-v1")
    if not isinstance(projection_artifact, tuple) or len(projection_artifact) != 7:
        raise GateFailure("prepared HE parse-only projection changed shape")
    projection_plan_digest = getattr(projection_artifact[5], "text", None)
    if not isinstance(projection_plan_digest, str):
        raise GateFailure("prepared HE projection omitted its plan digest")

    he_cases = [
        (label, path)
        for label, dialect, path in corpus_cases(he_root, petta_root)
        if dialect == "he"
    ]
    records: list[dict[str, Any]] = []
    probes: list[
        tuple[
            str,
            Path,
            dict[str, Any],
            dict[str, Any],
            Any,
            Any,
        ]
    ] = []

    if arguments.artifacts_dir is None:
        artifact_context = tempfile.TemporaryDirectory(
            prefix="gslt2parse-he-parse-only-"
        )
    else:
        persistent_directory = arguments.artifacts_dir.resolve()
        persistent_directory.mkdir(parents=True, exist_ok=True)
        artifact_context = nullcontext(persistent_directory)

    with artifact_context as raw:
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
        projection_path.write_text(render(projection_artifact) + "\n", encoding="utf-8")
        paths = (
            abi_path,
            lexical_path,
            guard_path,
            evidence_path,
            guarded_path,
            action_path,
            semantic_mask_path,
        )

        for label, path in he_cases:
            if not path.is_file():
                raise GateFailure(f"HE parse-only corpus is absent: {label}")
            source = path.read_bytes()
            old_probe = run_he(he_binary, path, 1, 1)
            old_probe["source_sha256"] = sha256(source).hexdigest()
            new_probe = run_prepared(
                pipeline_binary,
                paths,
                projection_path,
                path,
                regular_digest,
                guarded_digest,
                action_digest,
                semantic_mask_compiler_digest,
                projection_plan_digest,
                1,
                1,
            )
            new_probe["source_sha256"] = sha256(source).hexdigest()
            old_record = structural_record(label, "he", old_probe)
            new_record = structural_record(label, "he", new_probe)
            if old_record != new_record:
                raise GateFailure(
                    f"prepared GSLT and Rust HE Atom streams differ for {label}: "
                    f"{new_record!r} != {old_record!r}"
                )
            records.append(new_record)

            def old_runner(
                warmup: int,
                iterations: int,
                *,
                corpus: Path = path,
            ) -> dict[str, Any]:
                return run_he(he_binary, corpus, warmup, iterations)

            def new_runner(
                warmup: int,
                iterations: int,
                *,
                corpus: Path = path,
            ) -> dict[str, Any]:
                return run_prepared(
                    pipeline_binary,
                    paths,
                    projection_path,
                    corpus,
                    regular_digest,
                    guarded_digest,
                    action_digest,
                    semantic_mask_compiler_digest,
                    projection_plan_digest,
                    warmup,
                    iterations,
                )

            probes.append(
                (label, path, old_probe, new_probe, old_runner, new_runner)
            )

        observed_digest = records_digest(records)
        if not EXPECTED_MATRIX_DIGEST:
            print("HE_GSLT_PARSE_ONLY_RECORDS=" + json.dumps(records, sort_keys=True))
            print("HE_GSLT_PARSE_ONLY_MATRIX=" + observed_digest)
            raise GateFailure("HE GSLT parse-only matrix seal is not ratified")
        if observed_digest != EXPECTED_MATRIX_DIGEST:
            raise GateFailure(
                "HE GSLT parse-only matrix changed: " + observed_digest
            )

        if arguments.benchmark:
            for label, _, old_probe, new_probe, old_runner, new_runner in probes:
                old_iterations, old_ns = timed_samples(
                    old_runner, old_probe, arguments.target_ms, arguments.samples
                )
                new_iterations, new_ns = timed_samples(
                    new_runner, new_probe, arguments.target_ms, arguments.samples
                )
                ratio = new_ns / old_ns
                mib_per_second = (
                    new_probe["input_bytes"]
                    * 1_000_000_000
                    / new_ns
                    / (1024 * 1024)
                )
                print(
                    f"(HEGSLTParseOnlyV1Receipt {label} "
                    f"{new_probe['input_bytes']} {new_probe['items']} "
                    f"{old_iterations} {new_iterations} {arguments.samples} "
                    f"{old_ns} {new_ns} {ratio:.6f} {mib_per_second:.3f})"
                )
                print(
                    f"(HEGSLTParseOnlyV1Work {label} "
                    f"{new_probe['input_scalars']} {new_probe['tokens']} "
                    f"{new_probe['dfa_work']} {new_probe['parser_work']} "
                    f"{new_probe['terminal_values']} "
                    f"{new_probe['action_executions']} "
                    f"{new_probe['action_instructions']} "
                    f"{new_probe['value_program_runs']} "
                    f"{new_probe['value_parser_work']} "
                    f"{new_probe['semantic_mask_runs']} "
                    f"{new_probe['semantic_mask_input_scalars']} "
                    f"{new_probe['semantic_mask_retained_scalars']} "
                    f"{new_probe['semantic_mask_work']})"
                )

    print(
        f"(HEGSLTParseOnlyV1Summary {len(records)} {len(records)} "
        f"{lexical_agreements} {EXPECTED_MATRIX_DIGEST} 0)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
