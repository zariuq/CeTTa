#!/usr/bin/env python3
"""Differential and native certificate gate for semantic-mask lowering."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import tempfile

from gslt2parse_schema_v1 import parse_sexprs, render, Symbol
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
SEMANTIC_COMPILER = (
    PRESENTATIONS / "compiler" / "semantic_mask_span_compiler_v1.metta"
)
SEMANTIC_COMPILER_PREFIX = (
    PRESENTATIONS / "reflection" / "finite_horn_reflection_v1.metta",
    PRESENTATIONS / "compiler" / "syntax_compiler_v1.metta",
)
REGULAR_COMPILER = (
    *SEMANTIC_COMPILER_PREFIX,
    PRESENTATIONS / "compiler" / "regular_span_compiler_v1.metta",
)
GUARDED_REGULAR_COMPILER = (
    PRESENTATIONS / "parserpack" / "parser_pack_core_v1.metta",
    *REGULAR_COMPILER,
    PRESENTATIONS / "compiler" / "guarded_regular_span_compiler_v1.metta",
)
CANARY_SOURCES = (
    "core/syntax_core_v1.metta",
    "shared/lookahead_core_v1.metta",
    "shared/ground_relations_v1.metta",
    "canaries/semantic_mask_v1.metta",
)

EXPECTED_SOURCE_DIGEST = (
    "ecf63c6d9c777f34e93d769af322b21bf2082c9e3d7850a4f6fb197e73539580"
)
EXPECTED_COMPILER_DIGEST = (
    "61e47fd81d24ee79e119810f99ee49bccfd1e03a7675850bc16feee1e1cb2a16"
)
EXPECTED_ROOT_LINK_DIGEST = (
    "e0ab61597f3feec4d99ce9586a6294be1a897b27ebfb623f043eb73f78308f7e"
)
EXPECTED_ROOT_LINKS = 7
MASK_CASES = {
    "word": {
        "recognition": "he-word",
        "guarded": True,
        "answers": 11,
        "mask_digest": (
            "5dd4572dc7faf0840b7448100cf94698aae46675941004f7fc73e479645f2d4d"
        ),
        "recognition_digest": (
            "7a41ea1b00ecfbe910af920bb816dc7d5487e78ae654f8ef7a084d926280f4a2"
        ),
    },
    "variable": {
        "recognition": "he-variable",
        "guarded": True,
        "answers": 15,
        "mask_digest": (
            "450ccf1a889bce49199e3c3e1944724a154dab2abce262c4001762872db36af5"
        ),
        "recognition_digest": (
            "23913c837514116a645ab9ef84121799dea82b44b7578071d0b9deeba239a412"
        ),
    },
    "string": {
        "recognition": "he-string",
        "guarded": False,
        "answers": 716,
        "mask_digest": (
            "03d5be20a906cb44673e9d41e49f26bbc1db52307790563783d6d92d0cfb0ecc"
        ),
        "recognition_digest": (
            "36de2ebbf582532c577a334730beb952d9c09eed2edc8b8fe6fc899cad880b6f"
        ),
    },
}
ACTION_PROBES = {
    "word": ("abcλ", "$"),
    "variable": ("$abc", "abc"),
    "string": ('"a\\n\\x41\\u{42}"', '"\\q"'),
}
EXPECTED_CANARY_DIGEST = (
    "76b25195ec009298391f8a8add637a9d634717e482dacb4a50b8665075ea11ef"
)
EXPECTED_CERTIFICATES: dict[str, dict[str, str]] = {
    "word": {
        "source-digest": EXPECTED_SOURCE_DIGEST,
        "semantic-compiler-digest": EXPECTED_COMPILER_DIGEST,
        "mask-answer-digest": (
            "f95237fbaa861b0dab6ee58280ba05f3bb0b811fad1d3373427367a347a06537"
        ),
        "mask-answers": "12",
        "recognition-answer-digest": str(
            MASK_CASES["word"]["recognition_digest"]
        ),
        "recognition-answers": "11",
        "mask-build-outcome": "completed",
        "mask-action-build-outcome": "completed",
        "recognition-build-outcome": "completed",
        "mask-states": "3",
        "mask-transitions": "44",
        "mask-actions": "2",
        "mask-wrappers": "0",
        "mask-root-links": "1",
        "action-local": "1",
        "functionality-pairs": "3",
        "functionality-work": "3",
        "functional": "1",
        "mask-in-recognition": "1",
        "recognition-in-mask": "1",
        "erasure-equivalent": "1",
        "certificate-digest": (
            "dc9e001eee8fefcccc87500dbdd42d19ecbcdb9813346b5cf0de9756c72014fb"
        ),
    },
    "variable": {
        "source-digest": EXPECTED_SOURCE_DIGEST,
        "semantic-compiler-digest": EXPECTED_COMPILER_DIGEST,
        "mask-answer-digest": (
            "23fad975287370b444d90b68c856c8090367e94c3a7d7cb980aab662d41f6454"
        ),
        "mask-answers": "16",
        "recognition-answer-digest": str(
            MASK_CASES["variable"]["recognition_digest"]
        ),
        "recognition-answers": "15",
        "mask-build-outcome": "completed",
        "mask-action-build-outcome": "completed",
        "recognition-build-outcome": "completed",
        "mask-states": "4",
        "mask-transitions": "46",
        "mask-actions": "2",
        "mask-wrappers": "0",
        "mask-root-links": "1",
        "action-local": "1",
        "functionality-pairs": "4",
        "functionality-work": "4",
        "functional": "1",
        "mask-in-recognition": "1",
        "recognition-in-mask": "1",
        "erasure-equivalent": "1",
        "certificate-digest": (
            "22835708f6be04d383b7cb85291b0b8a9eaf2f9e7a77b87828c7551be3f279e7"
        ),
    },
    "string": {
        "source-digest": EXPECTED_SOURCE_DIGEST,
        "semantic-compiler-digest": EXPECTED_COMPILER_DIGEST,
        "mask-answer-digest": (
            "0c6a8fe766d2693f5aebb400f0ec1eab1042e613e68344be34a59f98b98c6abe"
        ),
        "mask-answers": "717",
        "recognition-answer-digest": str(
            MASK_CASES["string"]["recognition_digest"]
        ),
        "recognition-answers": "716",
        "mask-build-outcome": "completed",
        "mask-action-build-outcome": "completed",
        "recognition-build-outcome": "completed",
        "mask-states": "71",
        "mask-transitions": "261",
        "mask-actions": "5",
        "mask-wrappers": "3",
        "mask-root-links": "1",
        "action-local": "1",
        "functionality-pairs": "76",
        "functionality-work": "393",
        "functional": "1",
        "mask-in-recognition": "1",
        "recognition-in-mask": "1",
        "erasure-equivalent": "1",
        "certificate-digest": (
            "8f4b9465434c6524427df78190657b6a0c3b84620dbbfc86ee7c1b5dccc70eae"
        ),
    },
}
for _label, _probe in {
    "word": ("1", "4", "4", "0", "0", "15"),
    "variable": ("1", "4", "3", "1", "0", "10"),
    "string": ("1", "15", "1", "9", "5", "28"),
}.items():
    EXPECTED_CERTIFICATES[_label].update(
        {
            "action-probe-ran": _probe[0],
            "action-probe-scalars": _probe[1],
            "action-probe-copy": _probe[2],
            "action-probe-drop": _probe[3],
            "action-probe-wrapper": _probe[4],
            "action-probe-work": _probe[5],
        }
    )

for _label, _trace in {
    "word": {
        "mask-trace-build-outcome": "completed",
        "trace-states": "3",
        "trace-transitions": "52",
        "trace-action-local-transitions": "52",
        "trace-backsteps": "224",
        "trace-functionality-pairs": "3",
        "trace-functionality-work": "3",
        "trace-probe-ran": "1",
        "trace-probe-scalars": "4",
        "trace-probe-copy": "4",
        "trace-probe-drop": "0",
        "trace-probe-wrapper": "0",
        "trace-probe-work": "20",
        "certificate-digest": (
            "f524eec7219ac20d4897c4938d1ea63f2ae75c1e9389bdcde8ef77ed74403167"
        ),
    },
    "variable": {
        "mask-trace-build-outcome": "completed",
        "trace-states": "4",
        "trace-transitions": "49",
        "trace-action-local-transitions": "49",
        "trace-backsteps": "259",
        "trace-functionality-pairs": "4",
        "trace-functionality-work": "4",
        "trace-probe-ran": "1",
        "trace-probe-scalars": "4",
        "trace-probe-copy": "3",
        "trace-probe-drop": "1",
        "trace-probe-wrapper": "0",
        "trace-probe-work": "14",
        "certificate-digest": (
            "1090fd34ba75b8592c948e95f0ad4d46cc0ad9c6a0d82c7dbf60e46d0bedcf07"
        ),
    },
    "string": {
        "mask-trace-build-outcome": "completed",
        "trace-states": "71",
        "trace-transitions": "710",
        "trace-action-local-transitions": "710",
        "trace-backsteps": "16754",
        "trace-functionality-pairs": "76",
        "trace-functionality-work": "393",
        "trace-probe-ran": "1",
        "trace-probe-scalars": "15",
        "trace-probe-copy": "1",
        "trace-probe-drop": "9",
        "trace-probe-wrapper": "5",
        "trace-probe-work": "62",
        "certificate-digest": (
            "5f58ab94dd4d59318412a5c0457f99d2e8015bac817d2e9610a8a569dc1606b6"
        ),
    },
}.items():
    EXPECTED_CERTIFICATES[_label].update(_trace)


class GateFailure(RuntimeError):
    pass


def answer_digest(terms: tuple[str, ...]) -> str:
    payload = bytearray(b"FiniteHornAnswerSetV1\n")
    for term in terms:
        payload.extend(term.encode("utf-8"))
        payload.append(10)
    return sha256(payload).hexdigest()


def run_process(
    command: list[str], *, cwd: Path, timeout: int = 300
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


def native_query(
    chart_binary: Path,
    compiler_paths: tuple[Path, ...],
    source_paths: tuple[str, ...],
    query: str,
) -> dict[str, object]:
    command = [str(chart_binary), *(str(path) for path in compiler_paths)]
    for relative in source_paths:
        command.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    command.extend(("--query-text", query, "--timeout", "90"))
    completed = run_process(command, cwd=ROOT)
    if completed.returncode != 0:
        raise GateFailure(
            "native semantic-mask query failed: " + completed.stderr.strip()
        )
    try:
        row = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("native semantic-mask query emitted malformed JSON") from error
    terms = row.get("terms")
    if not isinstance(terms, list) or row.get("answers") != len(terms):
        raise GateFailure("native semantic-mask answer accounting changed")
    observed = tuple(str(term) for term in terms)
    if tuple(sorted(set(observed))) != observed:
        raise GateFailure("native semantic-mask answers are not canonical")
    if row.get("term_digest") != answer_digest(observed):
        raise GateFailure("native semantic-mask answer digest changed internally")
    row["terms"] = observed
    return row


def petta_answer_set(
    petta_root: Path, label: str
) -> tuple[str, tuple[str, ...], str]:
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "semantic_mask_span_oracle_v1.pl"
    )
    completed = run_process(
        [
            "swipl",
            "-q",
            "-f",
            str(oracle),
            "--",
            str(PRESENTATIONS),
            label,
            *ABI_CASES["he"]["sources"],
        ],
        cwd=oracle.parent,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "PeTTa semantic-mask query failed: " + completed.stderr.strip()
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "semantic-mask-span-oracle-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("PeTTa semantic-mask framing changed")
    fields: dict[str, str] = {}
    terms: list[str] = []
    for line in lines[1:-1]:
        name, separator, value = line.partition("\t")
        if not separator:
            raise GateFailure("malformed PeTTa semantic-mask record")
        if name == "answer":
            terms.append(value)
        elif name in fields:
            raise GateFailure(f"duplicate PeTTa semantic-mask field {name}")
        else:
            fields[name] = value
    observed = tuple(terms)
    if (
        tuple(sorted(set(observed))) != observed
        or fields.get("answers") != str(len(observed))
        or fields.get("answer-digest") != answer_digest(observed)
    ):
        raise GateFailure("PeTTa semantic-mask answer accounting changed")
    return fields["source-digest"], observed, fields["answer-digest"]


def guarded_recognition_terms(terms: tuple[str, ...]) -> tuple[str, ...]:
    normalized: list[str] = []
    for text in terms:
        parsed = parse_sexprs(text, source="guarded semantic-mask recognition")
        if len(parsed) != 1:
            raise GateFailure("guarded recognition answer is not singular")
        term = parsed[0]
        if (
            not isinstance(term, tuple)
            or len(term) != 5
            or not isinstance(term[0], Symbol)
            or term[0].text != "compile-guarded-span-nfa"
        ):
            raise GateFailure("guarded recognition answer changed shape")
        normalized.append(
            render((Symbol("compile-span-nfa"), term[1], term[4]))
        )
    observed = tuple(sorted(set(normalized)))
    if len(observed) != len(terms):
        raise GateFailure("guarded recognition normalization collapsed answers")
    return observed


def root_links_by_label(terms: tuple[str, ...]) -> dict[str, str]:
    links: dict[str, str] = {}
    definitions: set[str] = set()
    for text in terms:
        parsed = parse_sexprs(text, source="semantic-mask root links")
        if len(parsed) != 1:
            raise GateFailure("semantic-mask root link is not singular")
        term = parsed[0]
        if (
            not isinstance(term, tuple)
            or len(term) != 3
            or not isinstance(term[0], Symbol)
            or term[0].text != "compile-semantic-mask-root-link"
            or not isinstance(term[1], Symbol)
            or not isinstance(term[2], Symbol)
        ):
            raise GateFailure("semantic-mask root link changed shape")
        definition = term[1].text
        label = term[2].text
        if definition in definitions or label in links:
            raise GateFailure("semantic-mask root links are not bijective")
        definitions.add(definition)
        links[label] = text
    return links


def native_recognition(
    chart_binary: Path, case: dict[str, object]
) -> dict[str, object]:
    tag = str(case["recognition"])
    if not bool(case["guarded"]):
        return native_query(
            chart_binary,
            REGULAR_COMPILER,
            ABI_CASES["he"]["sources"],
            f"(compile-span-nfa {tag} ?edge)",
        )
    row = native_query(
        chart_binary,
        GUARDED_REGULAR_COMPILER,
        ABI_CASES["he"]["sources"],
        f"(compile-guarded-span-nfa {tag} ?guard-state ?guard ?edge)",
    )
    terms = row["terms"]
    assert isinstance(terms, tuple)
    normalized = guarded_recognition_terms(terms)
    row["terms"] = normalized
    row["answers"] = len(normalized)
    row["term_digest"] = answer_digest(normalized)
    return row


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


def parse_certificate(output: str) -> dict[str, str]:
    lines = output.splitlines()
    if not lines or lines[0] != "semantic-mask-nfa-v1" or lines[-1] != "end":
        raise GateFailure("semantic-mask native framing changed")
    fields: dict[str, str] = {}
    for line in lines[1:-1]:
        name, separator, value = line.partition("\t")
        if not separator or name in fields:
            raise GateFailure("malformed semantic-mask native record")
        fields[name] = value
    return fields


def run_certificate(
    binary: Path,
    abi_path: Path,
    mask_path: Path,
    recognition_path: Path,
    compiler_digest: str,
    *,
    expect_success: bool,
    probes: tuple[str, str] | None = None,
) -> dict[str, str]:
    command = [
        str(binary),
        str(abi_path),
        str(mask_path),
        str(recognition_path),
        compiler_digest,
    ]
    if probes is not None:
        command.extend(probes)
    completed = run_process(
        command,
        cwd=ROOT,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "semantic-mask native certificate failed: "
                + completed.stderr.strip()
            )
        return parse_certificate(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("corrupt semantic-mask artifact did not fail closed")
    return {}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--stream-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    stream_binary = arguments.stream_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not stream_binary.is_file():
        raise GateFailure("semantic-mask native dependency is absent")
    if not petta_root.is_dir():
        raise GateFailure("paired PeTTa checkout is absent")

    semantic_compilers = (*SEMANTIC_COMPILER_PREFIX, SEMANTIC_COMPILER)
    compiler_digest = sha256(SEMANTIC_COMPILER.read_bytes()).hexdigest()
    abi = export_stream(petta_root, ABI_CASES["he"])
    source_digest = abi_field(abi, "source-digest")
    compiled_cases: dict[
        str, tuple[tuple[str, ...], tuple[str, ...]]
    ] = {}
    for label, case in MASK_CASES.items():
        native_mask = native_query(
            chart_binary,
            semantic_compilers,
            ABI_CASES["he"]["sources"],
            f"(compile-semantic-mask-nfa {label} ?edge)",
        )
        recognition = native_recognition(chart_binary, case)
        petta_source_digest, petta_terms, petta_digest = petta_answer_set(
            petta_root, label
        )
        mask_terms = native_mask["terms"]
        recognition_terms = recognition["terms"]
        assert isinstance(mask_terms, tuple)
        assert isinstance(recognition_terms, tuple)
        if (
            native_mask.get("outcome") != "Ambiguous"
            or len(mask_terms) != case["answers"]
            or mask_terms != petta_terms
            or native_mask.get("term_digest") != petta_digest
            or petta_digest != case["mask_digest"]
            or recognition.get("outcome") != "Ambiguous"
            or len(recognition_terms) != case["answers"]
            or (
                case["recognition_digest"]
                and recognition.get("term_digest")
                != case["recognition_digest"]
            )
            or source_digest != petta_source_digest
        ):
            raise GateFailure(
                f"semantic-mask {label} C/PeTTa baseline changed: "
                f"mask={native_mask.get('term_digest')} "
                f"recognition={recognition.get('term_digest')}"
            )
        compiled_cases[label] = (mask_terms, recognition_terms)

    native_root_links = native_query(
        chart_binary,
        semantic_compilers,
        ABI_CASES["he"]["sources"],
        "(compile-semantic-mask-root-link ?name ?label)",
    )
    (
        root_link_source_digest,
        petta_root_links,
        petta_root_link_digest,
    ) = petta_answer_set(petta_root, "--root-links")
    native_root_link_terms = native_root_links["terms"]
    assert isinstance(native_root_link_terms, tuple)
    if (
        native_root_links.get("outcome") != "Ambiguous"
        or len(native_root_link_terms) != EXPECTED_ROOT_LINKS
        or native_root_link_terms != petta_root_links
        or native_root_links.get("term_digest") != petta_root_link_digest
        or petta_root_link_digest != EXPECTED_ROOT_LINK_DIGEST
        or root_link_source_digest != source_digest
    ):
        raise GateFailure(
            "semantic-mask root-link C/PeTTa baseline changed: "
            f"native={native_root_links.get('term_digest')} "
            f"petta={petta_root_link_digest}"
        )
    root_links = root_links_by_label(native_root_link_terms)
    if not set(MASK_CASES).issubset(root_links):
        raise GateFailure("semantic-mask cases lack compiler-derived root links")
    if (
        compiler_digest != EXPECTED_COMPILER_DIGEST
        or source_digest != EXPECTED_SOURCE_DIGEST
    ):
        raise GateFailure("semantic-mask provenance baseline changed")

    positive = native_query(
        chart_binary,
        semantic_compilers,
        CANARY_SOURCES,
        "(compile-semantic-mask-nfa semantic-mask-positive-root ?edge)",
    )
    negative = native_query(
        chart_binary,
        semantic_compilers,
        CANARY_SOURCES,
        "(compile-semantic-mask-nfa semantic-mask-nested-root ?edge)",
    )
    if (
        positive.get("outcome") != "Ambiguous"
        or positive.get("answers") != 7
        or positive.get("term_digest") != EXPECTED_CANARY_DIGEST
        or negative.get("outcome") != "NoAnswer"
        or negative.get("answers") != 0
    ):
        raise GateFailure("semantic-mask finite-fragment boundary changed")

    mutations = 0
    resources = 0
    baselines: dict[str, dict[str, str]] = {}
    with tempfile.TemporaryDirectory(prefix="gslt2parse-semantic-mask-") as raw:
        directory = Path(raw)
        abi_path = directory / "he.abi"
        abi_path.write_bytes(abi)
        paths: dict[str, tuple[Path, Path]] = {}
        for label, (mask_terms, recognition_terms) in compiled_cases.items():
            mask_path = directory / f"{label}-mask.nfa"
            recognition_path = directory / f"{label}-recognition.nfa"
            linked_mask_terms = tuple(
                sorted((*mask_terms, root_links[label]))
            )
            mask_path.write_text(
                "\n".join(linked_mask_terms) + "\n", encoding="utf-8"
            )
            recognition_path.write_text(
                "\n".join(recognition_terms) + "\n", encoding="utf-8"
            )
            paths[label] = (mask_path, recognition_path)
            baselines[label] = run_certificate(
                stream_binary,
                abi_path,
                mask_path,
                recognition_path,
                compiler_digest,
                expect_success=True,
                probes=ACTION_PROBES[label],
            )

        if EXPECTED_CERTIFICATES and baselines != EXPECTED_CERTIFICATES:
            raise GateFailure(
                "semantic-mask functionality certificates changed: "
                + json.dumps(baselines, sort_keys=True)
            )

        compiler_source = SEMANTIC_COMPILER.read_text(encoding="utf-8")
        anchor = """        (compile-semantic-mask-grammar
          (semantic-mask-wrapper ?label) ?body ?regex)))"""
        replacement = """        (compile-semantic-mask-grammar
          semantic-mask-drop ?body ?regex)))"""
        if compiler_source.count(anchor) != 1:
            raise GateFailure("semantic-mask action mutation anchor changed")
        mutated_compiler = directory / "semantic_mask_mutated_v1.metta"
        mutated_compiler.write_text(
            compiler_source.replace(anchor, replacement), encoding="utf-8"
        )
        mutated = native_query(
            chart_binary,
            (*SEMANTIC_COMPILER_PREFIX, mutated_compiler),
            ABI_CASES["he"]["sources"],
            "(compile-semantic-mask-nfa string ?edge)",
        )
        if mutated.get("term_digest") == MASK_CASES["string"]["mask_digest"]:
            raise GateFailure("semantic-mask action mutation survived")
        mutations += 1

        string_mask_terms, _ = compiled_cases["string"]
        _, string_recognition_path = paths["string"]
        corrupt_terms = list(
            sorted((*string_mask_terms, root_links["string"]))
        )
        corrupt_terms[0] = corrupt_terms[0].replace(
            "semantic-mask-nfa-accept", "semantic-mask-nfa-invalid", 1
        )
        corrupt_path = directory / "mask-corrupt.nfa"
        corrupt_path.write_text(
            "\n".join(corrupt_terms) + "\n", encoding="utf-8"
        )
        run_certificate(
            stream_binary,
            abi_path,
            corrupt_path,
            string_recognition_path,
            compiler_digest,
            expect_success=False,
        )
        mutations += 1
        resources += 1

        word_mask_terms, _ = compiled_cases["word"]
        bad_link_terms = tuple(
            sorted(
                (
                    *word_mask_terms,
                    root_links["word"].replace(
                        " he-word word)", " he-word absent-mask-tag)", 1
                    ),
                )
            )
        )
        bad_link_path = directory / "mask-bad-root-link.nfa"
        bad_link_path.write_text(
            "\n".join(bad_link_terms) + "\n", encoding="utf-8"
        )
        _, word_recognition_path = paths["word"]
        run_certificate(
            stream_binary,
            abi_path,
            bad_link_path,
            word_recognition_path,
            compiler_digest,
            expect_success=False,
        )
        mutations += 1
        resources += 1

    if not EXPECTED_CERTIFICATES:
        raise GateFailure(
            "ratify semantic-mask functionality certificates: "
            + json.dumps(baselines, sort_keys=True)
        )

    total_answers = sum(int(case["answers"]) for case in MASK_CASES.values())
    total_pairs = sum(
        int(baseline["functionality-pairs"])
        for baseline in baselines.values()
    )
    total_work = sum(
        int(baseline["functionality-work"])
        for baseline in baselines.values()
    )
    action_local = sum(
        int(baseline["action-local"]) for baseline in baselines.values()
    )
    matrix_digest = sha256(
        json.dumps(
            {
                "certificates": baselines,
                "root-links": native_root_link_terms,
            },
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    ).hexdigest()
    print(
        "(SemanticMaskSpanCompilerV1Summary "
        f"{len(MASK_CASES)} {total_answers} {total_answers} "
        f"{action_local} {len(native_root_link_terms)} {positive['answers']} "
        f"{total_pairs} "
        f"{total_work} {mutations} {resources} {matrix_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
