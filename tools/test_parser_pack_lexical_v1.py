#!/usr/bin/env python3
"""GSLT-derived lexical projection differential for native GLL and GLR."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_parser_pack_gll_v1 import CASES as PARSER_CASES
from test_parser_pack_gll_v1 import STARTS
from test_parser_pack_gll_v1 import input_bytes
from test_parser_pack_gll_v1 import oracle_rows
from test_parser_pack_gll_v1 import run_native as run_scannerless
from test_regular_span_dfa_v1 import CASES as REGULAR_CASES
from test_regular_span_dfa_v1 import abi_scalar
from test_regular_span_dfa_v1 import answer_digest
from test_regular_span_dfa_v1 import c_compiler_answers
from test_regular_span_dfa_v1 import petta_compiler_answers
from test_regular_span_dfa_v1 import reference_tokens


ROOT = Path(__file__).resolve().parents[1]
REGULAR_COMPILER = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "compiler"
    / "regular_span_compiler_v1.metta"
)
MATRIX_DIGEST = "cf6f155b6102ec6864754f62b6e7946bb30ea5f227faad0b5661f92028892ed5"

SUPPORTED_CASES = (
    ("metamath", "whitespace"),
    ("megalodon", "single_name"),
    ("tptp", "basic_fof"),
)
PRIME_START = "(pp-def cetta-prime-file)"


class GateFailure(RuntimeError):
    pass


def regular_case(language: str) -> dict[str, object]:
    matches = [case for case in REGULAR_CASES if case["language"] == language]
    if len(matches) != 1:
        raise GateFailure(f"no unique regular-span case for {language}")
    return dict(matches[0])


def parser_case(language: str, label: str) -> tuple[int, ...]:
    matches = [
        codepoints
        for case_language, case_label, codepoints in PARSER_CASES
        if (case_language, case_label) == (language, label)
    ]
    if len(matches) != 1:
        raise GateFailure(f"no unique parser case for {language}/{label}")
    return matches[0]


def compile_tag(
    chart_binary: Path,
    petta_root: Path,
    case: dict[str, object],
    source_digest: str,
) -> tuple[str, ...]:
    language = str(case["language"])
    tag = str(case["tag"])
    c_result = c_compiler_answers(chart_binary, case)
    petta_result = petta_compiler_answers(petta_root, case)
    c_terms = tuple(c_result.get("terms", ()))
    petta_terms = petta_result.get("terms")
    expected_count = int(case["answers"])
    expected_digest = str(case["answer_digest"])
    if (
        not isinstance(petta_terms, tuple)
        or c_result.get("outcome") != "Ambiguous"
        or c_result.get("answers") != expected_count
        or c_result.get("term_digest") != expected_digest
        or c_terms != petta_terms
        or petta_result.get("answers") != str(expected_count)
        or petta_result.get("answer-digest") != expected_digest
        or petta_result.get("source-digest") != source_digest
        or answer_digest(c_terms) != expected_digest
    ):
        raise GateFailure(
            f"{language}/{tag}: C and PeTTa compiler answer sets differ"
        )
    return tuple(sorted(c_terms))


def compile_case_tags(
    chart_binary: Path,
    petta_root: Path,
    case: dict[str, object],
    source_digest: str,
) -> tuple[tuple[str, ...], tuple[str, ...], int]:
    specs = [(
        str(case["tag"]),
        int(case["answers"]),
        str(case["answer_digest"]),
    )]
    specs.extend(
        (str(tag), int(count), str(digest))
        for tag, count, digest in case.get("extra_tags", ())
    )
    all_terms: list[str] = []
    tags: list[str] = []
    for tag, count, digest in specs:
        tagged_case = dict(case)
        tagged_case["tag"] = tag
        tagged_case["answers"] = count
        tagged_case["answer_digest"] = digest
        all_terms.extend(compile_tag(
            chart_binary, petta_root, tagged_case, source_digest
        ))
        tags.append(tag)
    if len(set(all_terms)) != len(all_terms):
        raise GateFailure(
            f"{case['language']}: independently compiled tag NFAs overlap"
        )
    return tuple(sorted(all_terms)), tuple(tags), len(specs)


def parse_projection_output(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if not lines or lines[0] != "regular-span-dfa-v1" or lines[-1] != "end":
        raise GateFailure("native lexical projection returned malformed framing")
    parsed: dict[str, object] = {
        "tokens": [],
        "projection-gll-results": [],
        "projection-glr-results": [],
    }
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "token" and len(fields) == 6:
            tokens = parsed["tokens"]
            assert isinstance(tokens, list)
            tokens.append((
                fields[1],
                int(fields[2]),
                int(fields[3]),
                int(fields[4]),
                int(fields[5]),
            ))
        elif fields[0] == "projection-gll-result" and len(fields) == 2:
            results = parsed["projection-gll-results"]
            assert isinstance(results, list)
            results.append(fields[1])
        elif fields[0] == "projection-glr-result" and len(fields) == 2:
            results = parsed["projection-glr-results"]
            assert isinstance(results, list)
            results.append(fields[1])
        elif len(fields) == 2 and fields[0] not in parsed:
            parsed[fields[0]] = fields[1]
        else:
            raise GateFailure(f"malformed native projection record: {line!r}")
    parsed["tokens"] = tuple(parsed["tokens"])
    parsed["projection-gll-results"] = tuple(
        parsed["projection-gll-results"]
    )
    parsed["projection-glr-results"] = tuple(
        parsed["projection-glr-results"]
    )
    return parsed


def run_projection(
    binary: Path,
    abi_path: Path,
    nfa_path: Path,
    input_path: Path,
    start: str,
    compiler_digest: str,
    *,
    expect_success: bool = True,
) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(binary),
            str(abi_path),
            str(nfa_path),
            str(input_path),
            "--project-start",
            start,
            "--regular-compiler-digest",
            compiler_digest,
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native lexical projection failed: " + completed.stderr.strip()
            )
        return parse_projection_output(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("invalid lexical projection did not fail closed")
    return {}


def require_receipts(
    language: str,
    observed: dict[str, object],
    stream: bytes,
    terms: tuple[str, ...],
    payload: bytes,
    codepoints: tuple[int, ...],
    compiler_digest: str,
) -> None:
    expected_tokens = reference_tokens(terms, stream, payload)
    token_count = len(expected_tokens)
    if observed.get("tokens") != expected_tokens:
        raise GateFailure(f"{language}: projected DFA differs from NFA oracle")
    expected = {
        "build-outcome": "completed",
        "scan-outcome": "completed",
        "source-digest": abi_scalar(stream, "source-digest"),
        "nfa-answer-digest": answer_digest(terms),
        "nfa-answers": str(len(terms)),
        "regular-compiler-digest": compiler_digest,
        "source-passes": "1",
        "decoded-bytes": str(len(payload)),
        "input-bytes": str(len(payload)),
        "input-scalars": str(len(codepoints)),
        "token-count": str(token_count),
        "projection-source-passes": "1",
        "projection-decoded-bytes": str(len(payload)),
        "projection-witness-values": str(token_count),
        "projection-gll-witness-runs": str(token_count),
        "projection-glr-witness-runs": str(token_count),
        "projection-gll-witness-outcome": "completed",
        "projection-glr-witness-outcome": "completed",
    }
    for field, value in expected.items():
        if observed.get(field) != value:
            raise GateFailure(
                f"{language}: lexical projection receipt changed: "
                f"{field}={observed.get(field)!r}"
            )
    plan_digest = observed.get("projection-plan-digest")
    if not isinstance(plan_digest, str) or re.fullmatch(
        r"[0-9a-f]{64}", plan_digest
    ) is None:
        raise GateFailure(f"{language}: projection plan digest is malformed")


def require_supported_exact(
    language: str,
    label: str,
    observed: dict[str, object],
    scannerless_gll: dict[str, object],
    scannerless_glr: dict[str, object],
    oracle: dict[str, object],
) -> None:
    projected_gll = observed["projection-gll-results"]
    projected_glr = observed["projection-glr-results"]
    for field, expected in (
        ("projection-gll-outcome", "completed"),
        ("projection-glr-outcome", "completed"),
        ("projection-gll-decision", oracle["decision"]),
        ("projection-glr-decision", oracle["decision"]),
    ):
        if observed.get(field) != expected:
            raise GateFailure(
                f"{language}/{label}: projected parser field differs: {field}"
            )
    for backend, scannerless in (
        ("GLL", scannerless_gll),
        ("GLR", scannerless_glr),
    ):
        for field in ("decision", "forest-digest", "results"):
            if scannerless.get(field) != oracle[field]:
                raise GateFailure(
                    f"{language}/{label}: scannerless {backend} {field} "
                    "differs from PeTTa"
                )
    if (
        projected_gll != oracle["results"]
        or projected_glr != oracle["results"]
        or projected_gll != scannerless_gll["results"]
        or projected_glr != scannerless_glr["results"]
    ):
        raise GateFailure(
            f"{language}/{label}: projected semantic results differ"
        )
    if (
        observed.get("projection-gll-forest-digest")
        != observed.get("projection-glr-forest-digest")
        or observed.get("projection-gll-forest-nodes")
        != observed.get("projection-glr-forest-nodes")
    ):
        raise GateFailure(
            f"{language}/{label}: projected GLL and GLR forests differ"
        )


def matrix_row(
    language: str,
    label: str,
    tags: tuple[str, ...],
    observed: dict[str, object],
) -> str:
    results = observed["projection-gll-results"]
    assert isinstance(results, tuple)
    return (
        f"{language}/{label}/{'+'.join(tags)}/"
        f"{observed['nfa-answer-digest']}/"
        f"{observed['projection-plan-digest']}/"
        f"{observed['token-count']}/"
        f"{observed['projection-gll-forest-digest']}/"
        f"{observed['projection-gll-decision']}:"
        + "|".join(results)
    )


def check_supported_case(
    chart_binary: Path,
    projection_binary: Path,
    gll_binary: Path,
    glr_binary: Path,
    petta_root: Path,
    oracle: dict[tuple[str, str], dict[str, object]],
    compiler_digest: str,
    language: str,
    label: str,
    temp: Path,
) -> tuple[str, tuple[Path, Path, Path], int]:
    codepoints = parser_case(language, label)
    payload = input_bytes(codepoints)
    stream = export_stream(petta_root, ABI_CASES[language])
    case = regular_case(language)
    case["input"] = payload.decode("utf-8")
    terms, tags, compiler_agreements = compile_case_tags(
        chart_binary,
        petta_root,
        case,
        abi_scalar(stream, "source-digest"),
    )
    abi_path = temp / f"{language}.abi"
    nfa_path = temp / f"{language}.nfa"
    input_path = temp / f"{language}-{label}.utf8"
    abi_path.write_bytes(stream)
    nfa_path.write_text("\n".join(terms) + "\n", encoding="utf-8")
    input_path.write_bytes(payload)
    observed = run_projection(
        projection_binary,
        abi_path,
        nfa_path,
        input_path,
        STARTS[language],
        compiler_digest,
    )
    require_receipts(
        language,
        observed,
        stream,
        terms,
        payload,
        codepoints,
        compiler_digest,
    )
    scannerless_gll = run_scannerless(
        gll_binary, abi_path, STARTS[language], input_path
    )
    scannerless_glr = run_scannerless(
        glr_binary,
        abi_path,
        STARTS[language],
        input_path,
        protocol="parser-pack-glr-stream-v1",
        backend="GLR",
    )
    require_supported_exact(
        language,
        label,
        observed,
        scannerless_gll,
        scannerless_glr,
        oracle[(language, label)],
    )
    return (
        matrix_row(language, label, tags, observed),
        (abi_path, nfa_path, input_path),
        compiler_agreements,
    )


def check_prime_boundary(
    chart_binary: Path,
    projection_binary: Path,
    gll_binary: Path,
    glr_binary: Path,
    petta_root: Path,
    compiler_digest: str,
    temp: Path,
) -> tuple[str, tuple[Path, Path, Path], int]:
    language = "prime"
    codepoints = (9, 10, 11, 12, 13, 32, 34, 40, 41, 59, 36, 97, 955)
    payload = input_bytes(codepoints)
    stream = export_stream(petta_root, ABI_CASES[language])
    case = regular_case(language)
    case["input"] = payload.decode("utf-8")
    terms, tags, compiler_agreements = compile_case_tags(
        chart_binary,
        petta_root,
        case,
        abi_scalar(stream, "source-digest"),
    )
    abi_path = temp / "prime.abi"
    nfa_path = temp / "prime.nfa"
    input_path = temp / "prime-token-boundaries.utf8"
    abi_path.write_bytes(stream)
    nfa_path.write_text("\n".join(terms) + "\n", encoding="utf-8")
    input_path.write_bytes(payload)
    observed = run_projection(
        projection_binary,
        abi_path,
        nfa_path,
        input_path,
        PRIME_START,
        compiler_digest,
    )
    require_receipts(
        language,
        observed,
        stream,
        terms,
        payload,
        codepoints,
        compiler_digest,
    )
    direct_gll = run_scannerless(
        gll_binary, abi_path, PRIME_START, input_path
    )
    direct_glr = run_scannerless(
        glr_binary,
        abi_path,
        PRIME_START,
        input_path,
        protocol="parser-pack-glr-stream-v1",
        backend="GLR",
    )
    for field in ("projection-gll-outcome", "projection-glr-outcome"):
        if observed.get(field) != "unsupported-open-pack":
            raise GateFailure(f"Prime typed projection boundary changed: {field}")
    if (
        direct_gll.get("outcome") != "unsupported-open-pack"
        or direct_glr.get("outcome") != "unsupported-open-pack"
        or observed.get("projection-gll-decision") != "rejected"
        or observed.get("projection-glr-decision") != "rejected"
        or observed["projection-gll-results"] != ()
        or observed["projection-glr-results"] != ()
    ):
        raise GateFailure("Prime open-pack boundary is no longer exact")
    row = (
        f"prime/open-peek/{'+'.join(tags)}/"
        f"{observed['nfa-answer-digest']}/"
        f"{observed['projection-plan-digest']}/"
        f"{observed['token-count']}/"
        f"{observed['projection-gll-witness-outcome']}/"
        f"{observed['projection-glr-witness-outcome']}/"
        f"{observed['projection-gll-outcome']}/"
        f"{observed['projection-glr-outcome']}"
    )
    return row, (abi_path, nfa_path, input_path), compiler_agreements


def check_malformed_digest(
    projection_binary: Path,
    paths: tuple[Path, Path, Path],
) -> int:
    abi_path, nfa_path, input_path = paths
    run_projection(
        projection_binary,
        abi_path,
        nfa_path,
        input_path,
        STARTS["metamath"],
        "not-a-sha256-digest",
        expect_success=False,
    )
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--projection-binary", type=Path, required=True)
    parser.add_argument("--gll-binary", type=Path, required=True)
    parser.add_argument("--glr-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    projection_binary = arguments.projection_binary.resolve()
    gll_binary = arguments.gll_binary.resolve()
    glr_binary = arguments.glr_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    compiler_digest = hashlib.sha256(REGULAR_COMPILER.read_bytes()).hexdigest()
    oracle = oracle_rows(petta_root)
    rows: list[str] = []
    compiler_agreements = 0
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-parser-pack-lexical-"
    ) as raw:
        temp = Path(raw)
        metamath_paths: tuple[Path, Path, Path] | None = None
        for language, label in SUPPORTED_CASES:
            row, paths, agreements = check_supported_case(
                chart_binary,
                projection_binary,
                gll_binary,
                glr_binary,
                petta_root,
                oracle,
                compiler_digest,
                language,
                label,
                temp,
            )
            rows.append(row)
            compiler_agreements += agreements
            if language == "metamath":
                metamath_paths = paths
        prime_row, _, prime_agreements = check_prime_boundary(
            chart_binary,
            projection_binary,
            gll_binary,
            glr_binary,
            petta_root,
            compiler_digest,
            temp,
        )
        rows.append(prime_row)
        compiler_agreements += prime_agreements
        assert metamath_paths is not None
        corruptions = check_malformed_digest(
            projection_binary, metamath_paths
        )
    matrix_payload = "\n".join((compiler_digest, *rows))
    digest = hashlib.sha256(matrix_payload.encode("utf-8")).hexdigest()
    if digest != MATRIX_DIGEST:
        raise GateFailure(
            "lexical projection matrix digest changed: "
            f"{digest}\n" + "\n".join(rows)
        )
    supported = len(SUPPORTED_CASES)
    languages = supported + 1
    print(
        f"(ParserPackLexicalGSLTV1MatrixSummary {languages} "
        f"{compiler_agreements} "
        f"{supported} {supported} {supported} 1 {corruptions} "
        f"{MATRIX_DIGEST} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
