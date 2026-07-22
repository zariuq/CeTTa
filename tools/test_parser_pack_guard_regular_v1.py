#!/usr/bin/env python3
"""Differential gate for regular lowering of positive ParserPack guards."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

from gslt2parse_schema_v1 import Symbol, parse_sexprs, render


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
    "compiler/regular_span_compiler_v1.metta",
    "compiler/parser_pack_guard_regular_link_v1.metta",
)

SOURCES = {
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
}

QUERY = "(compile-span-nfa (pp-positive-guard-tag ?state) ?edge)"
EXPECTED = {
    "he": {
        "count": 60,
        "tag_count": 3,
        "digest": "0a680733ee1509d1b72d7f70fd43caca2ca1d914b3f806c16d5090709e930d34",
    },
    "petta": {
        "count": 10,
        "tag_count": 1,
        "digest": "ed42222f9affc916ccbd1a27d8c5472a1e8dbc56e6238c91f7f36c18831a4d41",
    },
    "prime": {
        "count": 173,
        "tag_count": 7,
        "digest": "72229e01844933b6e5cd179fd92b792ffb5ab02c302cf5cf98403d4cd1768993",
    },
}
EXPECTED_MATRIX_DIGEST = (
    "2b44611def9122833c25d560c1b3a0162a90ed6a8f4f093e44abfc745052780f"
)


class GateFailure(RuntimeError):
    pass


def program_arguments(
    label: str = "prime", link: Path | None = None
) -> list[str]:
    arguments: list[str] = []
    for relative in COMPILER:
        path = (
            link
            if link is not None
            and relative.endswith("parser_pack_guard_regular_link_v1.metta")
            else PRESENTATIONS / relative
        )
        arguments.append(str(path))
    for relative in SOURCES[label]:
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
            f"native guard regular compiler exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            "native guard regular compiler emitted non-JSON output: "
            f"{completed.stdout!r}"
        ) from error


def run_petta(petta_root: Path) -> dict:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_parser_pack_guard_regular_v1.pl"
    )
    if not script.is_file():
        raise GateFailure(f"PeTTa guard regular oracle is absent: {script}")
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
            f"PeTTa guard regular oracle exited {completed.returncode}: "
            f"{completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure(
            f"PeTTa guard regular oracle emitted non-JSON output: "
            f"{completed.stdout!r}"
        ) from error


def answer_tag(text: str) -> str:
    terms = parse_sexprs(text, source="positive-guard NFA answer")
    if len(terms) != 1 or render(terms[0]) != text:
        raise GateFailure("positive-guard NFA answer is not canonical")
    answer = terms[0]
    if (
        not isinstance(answer, tuple)
        or len(answer) != 3
        or answer[0] != Symbol("compile-span-nfa")
    ):
        raise GateFailure("positive-guard NFA answer has the wrong relation")
    tag = answer[1]
    if (
        not isinstance(tag, tuple)
        or len(tag) != 2
        or tag[0] != Symbol("pp-positive-guard-tag")
    ):
        raise GateFailure("positive-guard NFA answer has the wrong tag")
    return render(tag)


def native_row(binary: Path, label: str = "prime") -> dict:
    expected = EXPECTED[label]
    result = run_native(
        binary,
        [
            *program_arguments(label),
            "--query-text",
            QUERY,
            "--certs",
            "--timeout",
            "30",
        ],
    )
    terms = result.get("terms")
    certs = result.get("certs")
    if (
        result.get("outcome") != "Ambiguous"
        or result.get("answers") != expected["count"]
        or result.get("term_digest") != expected["digest"]
        or not isinstance(terms, list)
        or not isinstance(certs, list)
        or len(terms) != expected["count"]
        or len(certs) != expected["count"]
        or terms != sorted(terms)
        or len(set(terms)) != expected["count"]
    ):
        raise GateFailure("native positive-guard NFA answer set changed")
    tags = sorted({answer_tag(term) for term in terms})
    if len(tags) != expected["tag_count"]:
        raise GateFailure("native positive-guard NFA tag inventory changed")
    result["guard_tags"] = tags
    return result


def compare_petta(
    native: dict[str, dict], petta: dict
) -> tuple[int, int, int]:
    if petta.get("protocol") != "parser-pack-positive-guard-regular-v1":
        raise GateFailure("PeTTa positive-guard NFA protocol changed")
    rows = petta.get("rows")
    if not isinstance(rows, list):
        raise GateFailure("PeTTa positive-guard NFA rows are absent")
    indexed = {row.get("label"): row for row in rows if isinstance(row, dict)}
    agreements = 0
    for label, native_row_value in native.items():
        expected = EXPECTED[label]
        oracle = indexed.get(label)
        if not isinstance(oracle, dict):
            raise GateFailure(f"PeTTa positive-guard NFA omitted {label}")
        expected_row = {
            "count": expected["count"],
            "digest": expected["digest"],
            "tag_count": expected["tag_count"],
            "tags": native_row_value["guard_tags"],
            "terms": native_row_value["terms"],
        }
        for field, value in expected_row.items():
            if oracle.get(field) != value:
                raise GateFailure(
                    f"C and PeTTa {label} guard NFA differ at {field}"
                )
        agreements += 1
    expected_replays = sum(
        int(row["count"]) + int(row["tag_count"])
        for row in EXPECTED.values()
    )
    if (
        petta.get("replay_count") != expected_replays
        or petta.get("negative_count") != 3
    ):
        raise GateFailure("PeTTa guard NFA replay or mutation inventory changed")
    return agreements, int(petta["replay_count"]), int(petta["negative_count"])


def replay_native_certificates(
    binary: Path, label: str, row: dict
) -> int:
    replayed = 0
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-positive-guard-nfa-certs-"
    ) as raw_temp:
        temp = Path(raw_temp)
        for index, (term, expected_certificate) in enumerate(
            zip(row["terms"], row["certs"], strict=True)
        ):
            certificate = temp / f"guard-nfa-{index}.cert"
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
            if (
                emitted.get("outcome") != "Unique"
                or not certificate.is_file()
                or certificate.read_text(encoding="utf-8").strip()
                != expected_certificate
            ):
                raise GateFailure(
                    f"native guard NFA certificate {index} changed"
                )
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
                raise GateFailure(
                    f"native guard NFA certificate {index} did not replay"
                )
            replayed += 1
    return replayed


def mutation_gate(binary: Path) -> int:
    without_link = [
        str(PRESENTATIONS / relative)
        for relative in COMPILER
        if not relative.endswith("parser_pack_guard_regular_link_v1.metta")
    ]
    for relative in SOURCES["prime"]:
        without_link.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    absent = run_native(
        binary,
        [*without_link, "--query-text", QUERY, "--summary"],
    )
    if absent.get("outcome") != "NoAnswer" or absent.get("answers") != 0:
        raise GateFailure("guard NFA appeared without its generic link")

    source = (
        PRESENTATIONS
        / "compiler"
        / "parser_pack_guard_regular_link_v1.metta"
    )
    text = source.read_text(encoding="utf-8")
    mutated = text.replace(
        "(compile-span-grammar ?body ?regex)",
        "(compile-span-grammar sir-eof ?regex)",
        1,
    )
    if mutated == text:
        raise GateFailure("guard NFA link mutation did not alter the source")
    with tempfile.TemporaryDirectory(
        prefix="gslt2parse-positive-guard-nfa-mutation-"
    ) as raw_temp:
        path = Path(raw_temp) / source.name
        path.write_text(mutated, encoding="utf-8")
        result = run_native(
            binary,
            [
                *program_arguments("prime", path),
                "--query-text",
                QUERY,
                "--summary",
                "--timeout",
                "30",
            ],
        )
        if result.get("term_digest") == EXPECTED["prime"]["digest"]:
            raise GateFailure("guard NFA body mutation retained its seal")

    generic = "\n".join(
        (PRESENTATIONS / relative).read_text(encoding="utf-8").lower()
        for relative in (
            "compiler/regular_span_compiler_v1.metta",
            "compiler/parser_pack_guard_regular_link_v1.metta",
        )
    )
    if re.search(r"metamath|megalodon|tptp|cetta[-_ ]?prime|petta", generic):
        raise GateFailure("guest-language policy leaked into guard NFA lowering")
    return 3


def matrix_digest(rows: dict[str, dict]) -> str:
    payload = "ParserPackPositiveGuardRegularV1\n"
    for label in sorted(rows):
        row = rows[label]
        payload += (
            f"{label}\t{row['answers']}\t{len(row['guard_tags'])}\t"
            f"{row['term_digest']}\n"
        )
    return sha256(payload.encode("utf-8")).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not binary.is_file():
        raise GateFailure(f"native chart binary is absent: {binary}")
    if not petta_root.is_dir():
        raise GateFailure(f"PeTTa root is absent: {petta_root}")

    rows = {label: native_row(binary, label) for label in SOURCES}
    petta = run_petta(petta_root)
    agreements, petta_replays, petta_negatives = compare_petta(rows, petta)
    native_replays = sum(
        replay_native_certificates(binary, label, row)
        for label, row in rows.items()
    )
    expected_native_replays = sum(int(row["count"]) for row in EXPECTED.values())
    if native_replays != expected_native_replays:
        raise GateFailure("native guard NFA replay inventory changed")
    native_negatives = mutation_gate(binary)
    digest = matrix_digest(rows)
    if EXPECTED_MATRIX_DIGEST and digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            f"positive-guard NFA matrix digest changed: {digest}"
        )
    print(
        "(ParserPackPositiveGuardRegularV1MatrixSummary "
        f"{sum(int(row['answers']) for row in rows.values())} "
        f"{sum(len(row['guard_tags']) for row in rows.values())} "
        f"{agreements} "
        f"{native_replays + petta_replays} "
        f"{native_negatives + petta_negatives} {digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
