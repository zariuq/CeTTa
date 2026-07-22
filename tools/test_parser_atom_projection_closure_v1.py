#!/usr/bin/env python3
"""Exact semantic-wrapper/domain closure gate for build-time specialization."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import tempfile

from gslt2parse_schema_v1 import render
from sexpr_atom_projection_plan_v1 import compile_artifact
from sexpr_atom_projection_plan_v1 import HE_READER, PROJECTION
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
SEMANTIC_COMPILER = (
    PRESENTATIONS / "compiler" / "semantic_text_span_compiler_v1.metta"
)
COMPILER_PATHS = (
    PRESENTATIONS / "reflection" / "finite_horn_reflection_v1.metta",
    PRESENTATIONS / "compiler" / "syntax_compiler_v1.metta",
    SEMANTIC_COMPILER,
    PRESENTATIONS / "compiler" / "regular_span_compiler_v1.metta",
)
WRAPPERS = ("simple-escape", "byte-escape", "unicode-escape")
EXPECTED_ANSWER_DIGEST = (
    "fd9506c997442c2510fd364e94211ba2ee401cc1d151a260f6410b93f9a20232"
)
EXPECTED_MATRIX_DIGEST = (
    "f0444cd85fd050a98d9053536b4542acec9da897bad4ef95c6c45a4a4930158c"
)


class GateFailure(RuntimeError):
    pass


def answer_digest(terms: tuple[str, ...]) -> str:
    payload = bytearray(b"FiniteHornAnswerSetV1\n")
    for term in terms:
        payload.extend(term.encode("utf-8"))
        payload.append(10)
    return sha256(payload).hexdigest()


def abi_field(stream: bytes, name: str) -> str:
    prefix = (name + "\t").encode("ascii")
    values = [
        line[len(prefix) :].decode("ascii")
        for line in stream.splitlines()
        if line.startswith(prefix)
    ]
    if len(values) != 1:
        raise GateFailure(f"ABI stream lacks unique {name}")
    return values[0]


def run_process(
    command: list[str], *, cwd: Path, timeout: int = 180
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=timeout,
    )


def native_answer_set(chart_binary: Path) -> tuple[str, ...]:
    terms: set[str] = set()
    for label in WRAPPERS:
        command = [str(chart_binary), *(str(path) for path in COMPILER_PATHS)]
        for source in ABI_CASES["he"]["sources"]:
            command.extend(("--reflect-source", str(PRESENTATIONS / source)))
        command.extend(
            (
                "--query-text",
                f"(compile-span-nfa (semantic-text {label}) ?edge)",
                "--timeout",
                "60",
            )
        )
        completed = run_process(command, cwd=ROOT)
        if completed.returncode != 0:
            raise GateFailure(
                "native semantic-text compiler failed: "
                + completed.stderr.strip()
            )
        try:
            row = json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            raise GateFailure(
                "native semantic-text compiler emitted malformed JSON"
            ) from error
        row_terms = row.get("terms")
        if (
            row.get("outcome") not in {"UniqueAnswer", "Ambiguous"}
            or not isinstance(row_terms, list)
            or not row_terms
            or row.get("answers") != len(row_terms)
            or row.get("term_digest")
            != answer_digest(tuple(str(term) for term in row_terms))
        ):
            raise GateFailure(f"native semantic-text answer set changed for {label}")
        before = len(terms)
        terms.update(str(term) for term in row_terms)
        if len(terms) - before != len(row_terms):
            raise GateFailure("semantic wrapper NFAs unexpectedly share answer rows")
    return tuple(sorted(terms))


def petta_answer_set(petta_root: Path) -> tuple[str, tuple[str, ...], str]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "semantic_text_span_oracle_v1.pl"
    )
    completed = run_process(
        [
            "swipl",
            "-q",
            "-f",
            str(oracle),
            "--",
            str(PRESENTATIONS),
            ",".join(WRAPPERS),
            *ABI_CASES["he"]["sources"],
        ],
        cwd=oracle.parent,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa semantic-text compiler failed: " + completed.stderr.strip()
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "semantic-text-span-oracle-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("PeTTa semantic-text oracle framing changed")
    fields: dict[str, str] = {}
    terms: list[str] = []
    for line in lines[1:-1]:
        name, separator, value = line.partition("\t")
        if not separator:
            raise GateFailure("malformed PeTTa semantic-text record")
        if name == "answer":
            terms.append(value)
        elif name in fields:
            raise GateFailure(f"duplicate PeTTa semantic-text field {name}")
        else:
            fields[name] = value
    observed = tuple(terms)
    if (
        fields.get("answers") != str(len(observed))
        or fields.get("answer-digest") != answer_digest(observed)
        or tuple(sorted(set(observed))) != observed
    ):
        raise GateFailure("PeTTa semantic-text answer accounting changed")
    return fields["source-digest"], observed, fields["answer-digest"]


def parse_native(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] != "parser-atom-projection-closure-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("native projection-closure framing changed")
    result: dict[str, object] = {"wrappers": []}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "wrapper" and len(fields) == 6:
            wrappers = result["wrappers"]
            assert isinstance(wrappers, list)
            wrappers.append(tuple(fields[1:]))
        elif len(fields) == 2 and fields[0] not in result:
            result[fields[0]] = fields[1]
        else:
            raise GateFailure(f"malformed projection-closure record: {line!r}")
    result["wrappers"] = tuple(result["wrappers"])
    return result


def run_native(
    binary: Path,
    abi_path: Path,
    nfa_path: Path,
    projection_path: Path,
    semantic_compiler_digest: str,
    *,
    extra: tuple[str, ...] = (),
    expect_success: bool = True,
) -> dict[str, object]:
    completed = run_process(
        [
            str(binary),
            str(abi_path),
            str(nfa_path),
            str(projection_path),
            semantic_compiler_digest,
            *extra,
        ],
        cwd=ROOT,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native projection closure failed: " + completed.stderr.strip()
            )
        return parse_native(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("invalid projection closure artifact did not fail closed")
    return {}


def artifact_text(projection_path: Path = PROJECTION) -> str:
    return render(
        compile_artifact("he-v1", projection_path, HE_READER)
    ) + "\n"


def mutate_projection(source: str, old: str, new: str, context: str) -> str:
    if source.count(old) != 1:
        raise GateFailure(f"{context}: mutation anchor changed")
    return source.replace(old, new)


def normalized_row(row: dict[str, object]) -> str:
    wrappers = row.get("wrappers")
    if not isinstance(wrappers, tuple):
        raise GateFailure("projection closure omitted wrapper rows")
    fields = (
        str(row.get("outcome")),
        str(row.get("bijection")),
        str(row.get("wrapper-count")),
        str(row.get("root-tag-count")),
        str(row.get("completed-wrappers")),
        str(row.get("all-included")),
        str(row.get("certified")),
        str(row.get("certificate-digest")),
        repr(wrappers),
    )
    return "|".join(fields)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--closure-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    closure_binary = arguments.closure_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not closure_binary.is_file():
        raise GateFailure("projection-closure native dependency is absent")
    if not petta_root.is_dir():
        raise GateFailure("paired PeTTa checkout is absent")

    abi = export_stream(petta_root, ABI_CASES["he"])
    source_digest = abi_field(abi, "source-digest")
    native_terms = native_answer_set(chart_binary)
    petta_source_digest, petta_terms, petta_digest = petta_answer_set(petta_root)
    observed_digest = answer_digest(native_terms)
    if (
        native_terms != petta_terms
        or observed_digest != petta_digest
        or observed_digest != EXPECTED_ANSWER_DIGEST
        or source_digest != petta_source_digest
    ):
        raise GateFailure("native and PeTTa semantic-text compiler answers differ")

    semantic_compiler_digest = sha256(SEMANTIC_COMPILER.read_bytes()).hexdigest()
    projection_source = PROJECTION.read_text(encoding="utf-8")
    rows: list[str] = []
    mutations = 0
    resources = 0

    with tempfile.TemporaryDirectory(prefix="gslt2parse-atom-closure-") as raw:
        directory = Path(raw)
        abi_path = directory / "he.abi"
        nfa_path = directory / "semantic-text.nfa"
        projection_path = directory / "projection.term"
        abi_path.write_bytes(abi)
        nfa_path.write_text("\n".join(native_terms) + "\n", encoding="utf-8")
        projection_path.write_text(artifact_text(), encoding="utf-8")

        baseline = run_native(
            closure_binary,
            abi_path,
            nfa_path,
            projection_path,
            semantic_compiler_digest,
        )
        expected_wrappers = (
            ("simple-escape", baseline["wrappers"][0][1], "1", baseline["wrappers"][0][3], "-"),
            ("byte-escape", baseline["wrappers"][1][1], "1", baseline["wrappers"][1][3], "-"),
            ("unicode-escape", baseline["wrappers"][2][1], "0", baseline["wrappers"][2][3], "-"),
        )
        if (
            baseline.get("source-digest") != source_digest
            or baseline.get("semantic-compiler-digest")
            != semantic_compiler_digest
            or baseline.get("semantic-nfa-answer-digest") != observed_digest
            or baseline.get("semantic-nfa-answers") != str(len(native_terms))
            or baseline.get("outcome") != "completed"
            or baseline.get("bijection") != "1"
            or baseline.get("wrapper-count") != "3"
            or baseline.get("root-tag-count") != "3"
            or baseline.get("completed-wrappers") != "3"
            or baseline.get("stopped-wrapper") != "-"
            or baseline.get("all-included") != "0"
            or baseline.get("certified") != "0"
            or baseline.get("wrappers") != expected_wrappers
            or len(str(baseline.get("certificate-digest", ""))) != 64
        ):
            raise GateFailure("baseline semantic/projection closure changed")
        rows.append(normalized_row(baseline))

        narrow_source = mutate_projection(
            projection_source,
            "he-v1 byte-escape 2 2 127 16",
            "he-v1 byte-escape 2 2 15 16",
            "narrow-byte-domain",
        )
        narrow_file = directory / "projection-narrow.metta"
        narrow_artifact = directory / "projection-narrow.term"
        narrow_file.write_text(narrow_source, encoding="utf-8")
        narrow_artifact.write_text(
            artifact_text(narrow_file), encoding="utf-8"
        )
        narrowed = run_native(
            closure_binary,
            abi_path,
            nfa_path,
            narrow_artifact,
            semantic_compiler_digest,
        )
        narrowed_wrappers = narrowed.get("wrappers")
        if (
            not isinstance(narrowed_wrappers, tuple)
            or len(narrowed_wrappers) != 3
            or narrowed_wrappers[0][2] != "1"
            or narrowed_wrappers[1][2:] != (
                "0",
                narrowed_wrappers[1][3],
                "31,30",
            )
            or narrowed_wrappers[2][2] != "0"
            or narrowed.get("all-included") != "0"
        ):
            raise GateFailure("narrowed byte-domain mutation survived")
        mutations += 1
        rows.append(normalized_row(narrowed))

        removed_block = """    (rule sexpr-wrapper-hex-he-unicode-escape
      (head
        (sexpr-wrapper-hex
          he-v1 unicode-escape 1 6 1114111 16))
      (body))
"""
        missing_source = mutate_projection(
            projection_source, removed_block, "", "missing-wrapper"
        )
        missing_file = directory / "projection-missing.metta"
        missing_artifact = directory / "projection-missing.term"
        missing_file.write_text(missing_source, encoding="utf-8")
        missing_artifact.write_text(
            artifact_text(missing_file), encoding="utf-8"
        )
        run_native(
            closure_binary,
            abi_path,
            nfa_path,
            missing_artifact,
            semantic_compiler_digest,
            expect_success=False,
        )
        mutations += 1

        limited = run_native(
            closure_binary,
            abi_path,
            nfa_path,
            projection_path,
            semantic_compiler_digest,
            extra=("--pair-limit", "1"),
        )
        if (
            limited.get("outcome") != "inclusion-pair-limit"
            or limited.get("certified") != "0"
            or limited.get("stopped-wrapper") != "0"
        ):
            raise GateFailure("inclusion resource limit was not explicit")
        resources += 1
        rows.append(normalized_row(limited))

    matrix_digest = sha256("\n".join(rows).encode("utf-8")).hexdigest()
    if matrix_digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            "projection-closure matrix digest changed: " + matrix_digest
        )
    print(
        "(ParserAtomProjectionClosureV1Summary "
        f"{len(WRAPPERS)} {len(native_terms)} 1 {mutations} {resources} "
        f"{observed_digest} {matrix_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
