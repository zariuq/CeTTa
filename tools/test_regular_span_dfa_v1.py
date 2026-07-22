#!/usr/bin/env python3
"""Differential gate from unchanged regular-span GSLT to one-pass C DFA."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from typing import Iterable

from gslt2parse_schema_v1 import SExpr, Symbol, parse_sexprs, render
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
REGULAR_COMPILER = (
    "reflection/finite_horn_reflection_v1.metta",
    "compiler/syntax_compiler_v1.metta",
    "compiler/regular_span_compiler_v1.metta",
)
MATRIX_DIGEST = "3153830cbe6bbff59ddbd046fd9e97e22aa5958f6d054492680e4fe336e6b2c7"

CASES = (
    {
        "language": "prime",
        "tag": "cetta-prime-string-escape",
        "input": "\\n\\λx",
        "answers": 7,
        "answer_digest":
            "be77d7dc410f2c08036f60b96c4ce1907884212fbe1293e3a6d4e8a274851fa3",
        "extra_tags": ((
            "cetta-prime-token-boundary",
            28,
            "ac38d7df7c8658e497d1dd2ffe455140a70319574ee474d13297d0ae2364e3c2",
        ),),
    },
    {
        "language": "metamath",
        "tag": "whitespace1",
        "input": " \tλ\n",
        "answers": 11,
        "answer_digest":
            "4b68c6066bd4fcd629aa63c80b415f3f8c8b0ce20d4c2e755f9c21475111bb6e",
        "extra_tags": ((
            "comment-pair-unit",
            12,
            "598a698d62a8b93d875cc860327efc0f47dafd050e82b59f997d9d0e3595a51c",
        ),),
    },
    {
        "language": "megalodon",
        "tag": "mg-name-raw",
        "input": "abc Z9 λa",
        "answers": 11,
        "answer_digest":
            "f8245a9262e320809af62a143757e3e53763fe318619f617d6a7eac328ce6cd8",
        "extra_tags": ((
            "mg-skip1",
            11,
            "593aea4d6c29e06fcd18c730d0e3fca6cd9930c5d52ca3578a7757c87ddc67f3",
        ),),
    },
    {
        "language": "tptp",
        "tag": "lower-word-raw",
        "input": "abc X a1 λz",
        "answers": 11,
        "answer_digest":
            "9bd0bef215b15242d6abe8c176d7434da407b87abdb63be3ecf83caa82e0d5c0",
        "extra_tags": ((
            "upper-word-raw",
            11,
            "5c69cc5a7b9c5182d2ca1e7a061482a13c36989d3bc5305e8f10d3b8de279529",
        ),),
    },
)


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class Edge:
    source: SExpr
    target: SExpr
    kind: str
    payload: SExpr | None = None


@dataclass(frozen=True, slots=True)
class ClassClause:
    identity: SExpr
    kind: str
    points: frozenset[int]


Token = tuple[str, int, int, int, int]


def app(term: SExpr, name: str, arity: int) -> tuple[SExpr, ...] | None:
    if (
        isinstance(term, tuple)
        and len(term) == arity + 1
        and term[0] == Symbol(name)
    ):
        return term
    return None


def one_term(text: str, source: str) -> SExpr:
    terms = parse_sexprs(text, source=source)
    if len(terms) != 1:
        raise GateFailure(f"{source}: expected one canonical term")
    if render(terms[0]) != text:
        raise GateFailure(f"{source}: term is not canonical")
    return terms[0]


def codepoint(term: SExpr) -> int:
    node = app(term, "cp", 1)
    if node is None or not isinstance(node[1], int):
        raise GateFailure("class or NFA character is not a codepoint")
    value = node[1]
    if value < 0 or value > 0x10FFFF or 0xD800 <= value <= 0xDFFF:
        raise GateFailure("class or NFA character is not a Unicode scalar")
    return value


def points_list(term: SExpr) -> frozenset[int]:
    result: set[int] = set()
    current = term
    while current != Symbol("pp-points-nil"):
        node = app(current, "pp-points-cons", 2)
        if node is None:
            raise GateFailure("malformed ParserPack point list")
        point = codepoint(node[1])
        if point in result:
            raise GateFailure("duplicate ParserPack class point")
        result.add(point)
        current = node[2]
    return frozenset(result)


def class_clauses(stream: bytes) -> tuple[ClassClause, ...]:
    clauses: list[ClassClause] = []
    for raw_line in stream.decode("utf-8").splitlines():
        fields = raw_line.split("\t")
        if fields[0] != "class-clause":
            continue
        if len(fields) != 2:
            raise GateFailure("malformed ParserPack class record")
        term = one_term(fields[1], "ParserPack class clause")
        point = app(term, "pp-class-point", 2)
        excluded = app(term, "pp-class-except", 2)
        if point is not None:
            clauses.append(ClassClause(
                point[1], "point", frozenset((codepoint(point[2]),))
            ))
        elif excluded is not None:
            clauses.append(ClassClause(
                excluded[1], "except", points_list(excluded[2])
            ))
        else:
            raise GateFailure("unknown ParserPack class clause")
    return tuple(clauses)


def class_matches(
    expression: SExpr,
    scalar: int,
    clauses: tuple[ClassClause, ...],
) -> bool:
    union = app(expression, "c-union", 2)
    if union is not None:
        return class_matches(union[1], scalar, clauses) or class_matches(
            union[2], scalar, clauses
        )
    matching = [clause for clause in clauses if clause.identity == expression]
    if not matching:
        raise GateFailure(f"class has no ParserPack clauses: {render(expression)}")
    return any(
        scalar in clause.points
        if clause.kind == "point"
        else scalar not in clause.points
        for clause in matching
    )


def parse_nfa(
    answer_texts: Iterable[str],
) -> tuple[set[SExpr], tuple[Edge, ...], dict[SExpr, set[str]]]:
    starts: set[SExpr] = set()
    edges: list[Edge] = []
    accepts: dict[SExpr, set[str]] = {}
    seen: set[SExpr] = set()
    for index, text in enumerate(answer_texts):
        answer = one_term(text, f"NFA answer {index}")
        if answer in seen:
            raise GateFailure("duplicate NFA answer")
        seen.add(answer)
        compiled = app(answer, "compile-span-nfa", 2)
        if compiled is None:
            raise GateFailure("unexpected regular-span compiler answer")
        tag = compiled[1]
        record = compiled[2]
        start = app(record, "nfa-start", 1)
        accept = app(record, "nfa-accept", 2)
        epsilon = app(record, "nfa-epsilon", 2)
        any_edge = app(record, "nfa-any", 2)
        eof = app(record, "nfa-eof", 2)
        char = app(record, "nfa-char", 3)
        klass = app(record, "nfa-class", 3)
        if start is not None:
            starts.add(start[1])
        elif accept is not None:
            if accept[2] != tag:
                raise GateFailure("NFA accept tag differs from answer tag")
            accepts.setdefault(accept[1], set()).add(render(tag))
        elif epsilon is not None:
            edges.append(Edge(epsilon[1], epsilon[2], "epsilon"))
        elif any_edge is not None:
            edges.append(Edge(any_edge[1], any_edge[2], "any"))
        elif eof is not None:
            edges.append(Edge(eof[1], eof[2], "eof"))
        elif char is not None:
            codepoint(char[2])
            edges.append(Edge(char[1], char[3], "char", char[2]))
        elif klass is not None:
            edges.append(Edge(klass[1], klass[3], "class", klass[2]))
        else:
            raise GateFailure("unknown regular-span NFA record")
    if not starts or not accepts:
        raise GateFailure("regular-span NFA lacks endpoints")
    return starts, tuple(edges), accepts


def closure(states: set[SExpr], edges: tuple[Edge, ...], eof: bool) -> set[SExpr]:
    result = set(states)
    changed = True
    while changed:
        changed = False
        for edge in edges:
            if (
                edge.source in result
                and (edge.kind == "epsilon" or (eof and edge.kind == "eof"))
                and edge.target not in result
            ):
                result.add(edge.target)
                changed = True
    return result


def reference_tokens(
    answer_texts: Iterable[str], stream: bytes, payload: bytes
) -> tuple[Token, ...]:
    starts, edges, accepts = parse_nfa(answer_texts)
    clauses = class_clauses(stream)
    try:
        text = payload.decode("utf-8", errors="strict")
    except UnicodeDecodeError as error:
        raise GateFailure("reference received invalid UTF-8") from error
    scalars = [ord(char) for char in text]
    byte_offsets = [0]
    for char in text:
        byte_offsets.append(byte_offsets[-1] + len(char.encode("utf-8")))
    tokens: set[Token] = set()

    def emit(states: set[SExpr], start: int, end: int) -> None:
        for state in states:
            for tag in accepts.get(state, set()):
                tokens.add((
                    tag, start, end, byte_offsets[start], byte_offsets[end]
                ))

    for start in range(len(scalars) + 1):
        states = closure(starts, edges, eof=False)
        emit(states, start, start)
        reached_end = True
        for scalar_index in range(start, len(scalars)):
            scalar = scalars[scalar_index]
            next_states: set[SExpr] = set()
            for edge in edges:
                if edge.source not in states:
                    continue
                matched = (
                    edge.kind == "any"
                    or (
                        edge.kind == "char"
                        and edge.payload is not None
                        and codepoint(edge.payload) == scalar
                    )
                    or (
                        edge.kind == "class"
                        and edge.payload is not None
                        and class_matches(edge.payload, scalar, clauses)
                    )
                )
                if matched:
                    next_states.add(edge.target)
            if not next_states:
                reached_end = False
                break
            states = closure(next_states, edges, eof=False)
            emit(states, start, scalar_index + 1)
        if reached_end:
            emit(closure(states, edges, eof=True), start, len(scalars))
    return tuple(sorted(
        tokens,
        key=lambda token: (token[1], token[2], token[0], token[3], token[4]),
    ))


def run_process(
    command: list[str], *, cwd: Path, timeout: int = 120
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


def c_compiler_answers(binary: Path, case: dict[str, object]) -> dict[str, object]:
    language = str(case["language"])
    tag = str(case["tag"])
    command = [str(binary)]
    command.extend(str(PRESENTATIONS / relative) for relative in REGULAR_COMPILER)
    for relative in ABI_CASES[language]["sources"]:
        command.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    command.extend((
        "--query-text", f"(compile-span-nfa {tag} ?edge)",
        "--timeout", "30",
    ))
    completed = run_process(command, cwd=ROOT)
    if completed.returncode != 0:
        raise GateFailure("C regular-span compiler failed: " + completed.stderr)
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("C regular-span compiler emitted malformed JSON") from error
    return result


def petta_compiler_answers(
    petta_root: Path, case: dict[str, object]
) -> dict[str, object]:
    language = str(case["language"])
    oracle = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "regular_span_oracle_v1.pl"
    )
    command = [
        "swipl", "-q", "-f", str(oracle), "--",
        str(PRESENTATIONS), str(case["tag"]),
        *ABI_CASES[language]["sources"],
    ]
    completed = run_process(command, cwd=oracle.parent)
    if completed.returncode != 0:
        raise GateFailure("PeTTa regular-span compiler failed: " + completed.stderr)
    lines = completed.stdout.splitlines()
    if not lines or lines[0] != "regular-span-oracle-v1" or lines[-1] != "end":
        raise GateFailure("PeTTa regular-span oracle returned malformed framing")
    result: dict[str, object] = {"terms": []}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if len(fields) != 2:
            raise GateFailure(f"malformed PeTTa oracle record: {line!r}")
        name, value = fields
        if name == "answer":
            terms = result["terms"]
            assert isinstance(terms, list)
            terms.append(value)
        elif name in result:
            raise GateFailure(f"duplicate PeTTa oracle field: {name}")
        else:
            result[name] = value
    result["terms"] = tuple(result["terms"])
    return result


def parse_native_output(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if not lines or lines[0] != "regular-span-dfa-v1" or lines[-1] != "end":
        raise GateFailure("native regular-span DFA returned malformed framing")
    result: dict[str, object] = {"tokens": []}
    for line in lines[1:-1]:
        fields = line.split("\t")
        if fields[0] == "token" and len(fields) == 6:
            tokens = result["tokens"]
            assert isinstance(tokens, list)
            tokens.append((
                fields[1],
                int(fields[2]), int(fields[3]),
                int(fields[4]), int(fields[5]),
            ))
        elif len(fields) == 2 and fields[0] not in result:
            result[fields[0]] = fields[1]
        else:
            raise GateFailure(f"malformed native DFA record: {line!r}")
    result["tokens"] = tuple(result["tokens"])
    return result


def run_native(
    binary: Path,
    abi_path: Path,
    nfa_path: Path,
    input_path: Path,
    *,
    expect_success: bool = True,
) -> dict[str, object]:
    completed = run_process(
        [str(binary), str(abi_path), str(nfa_path), str(input_path)],
        cwd=ROOT,
    )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure("native regular-span DFA failed: " + completed.stderr)
        return parse_native_output(completed.stdout)
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("corrupt regular-span input did not fail closed")
    return {}


def answer_digest(terms: Iterable[str]) -> str:
    payload = "FiniteHornAnswerSetV1\n" + "".join(
        f"{term}\n" for term in terms
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def abi_scalar(stream: bytes, name: str) -> str:
    prefix = name.encode("ascii") + b"\t"
    matches = [line[len(prefix):] for line in stream.splitlines()
               if line.startswith(prefix)]
    if len(matches) != 1:
        raise GateFailure(f"ParserPack ABI has no unique {name}")
    return matches[0].decode("utf-8")


def matrix_row(case: dict[str, object], result: dict[str, object]) -> str:
    tokens = result["tokens"]
    assert isinstance(tokens, tuple)
    tag_names = [str(case["tag"])] + [
        str(spec[0]) for spec in case.get("extra_tags", ())
    ]
    token_text = "|".join(
        f"{tag}:{ss}:{es}:{sb}:{eb}" for tag, ss, es, sb, eb in tokens
    )
    return (
        f"{case['language']}/{'+'.join(tag_names)}/"
        f"{result['nfa-answer-digest']}/"
        f"{result['dfa-states']}/{result['dfa-transitions']}/"
        f"{result['input-bytes']}/{result['input-scalars']}/{token_text}"
    )


def check_case(
    chart_binary: Path,
    dfa_binary: Path,
    petta_root: Path,
    case: dict[str, object],
    temp: Path,
) -> tuple[str, tuple[Path, Path, Path], int]:
    language = str(case["language"])
    stream = export_stream(petta_root, ABI_CASES[language])
    source_digest = abi_scalar(stream, "source-digest")
    tag_specs = [(
        str(case["tag"]), int(case["answers"]), str(case["answer_digest"])
    )]
    tag_specs.extend(
        (str(tag), int(count), str(digest))
        for tag, count, digest in case.get("extra_tags", ())
    )
    all_terms: list[str] = []
    for tag, expected_tag_count, expected_tag_digest in tag_specs:
        tagged_case = dict(case)
        tagged_case["tag"] = tag
        c_result = c_compiler_answers(chart_binary, tagged_case)
        petta_result = petta_compiler_answers(petta_root, tagged_case)
        c_terms = tuple(c_result.get("terms", ()))
        petta_terms = petta_result.get("terms")
        if not isinstance(petta_terms, tuple):
            raise GateFailure(f"{language}/{tag}: PeTTa omitted answer terms")
        if (
            c_result.get("outcome") != "Ambiguous"
            or c_result.get("answers") != expected_tag_count
            or c_result.get("term_digest") != expected_tag_digest
            or c_terms != petta_terms
            or petta_result.get("answers") != str(expected_tag_count)
            or petta_result.get("answer-digest") != expected_tag_digest
            or petta_result.get("source-digest") != source_digest
            or answer_digest(c_terms) != expected_tag_digest
        ):
            raise GateFailure(
                f"{language}/{tag}: C and PeTTa compiler answer sets differ"
            )
        all_terms.extend(c_terms)
    if len(set(all_terms)) != len(all_terms):
        raise GateFailure(f"{language}: independently tagged NFAs overlap")
    combined_terms = tuple(sorted(all_terms))
    expected_count = len(combined_terms)
    expected_digest = answer_digest(combined_terms)

    payload = str(case["input"]).encode("utf-8")
    abi_path = temp / f"{language}.abi"
    nfa_path = temp / f"{language}.nfa"
    input_path = temp / f"{language}.utf8"
    abi_path.write_bytes(stream)
    nfa_path.write_text("\n".join(combined_terms) + "\n", encoding="utf-8")
    input_path.write_bytes(payload)
    observed = run_native(dfa_binary, abi_path, nfa_path, input_path)
    expected_tokens = reference_tokens(combined_terms, stream, payload)
    if observed.get("tokens") != expected_tokens:
        raise GateFailure(f"{language}: C DFA token lattice differs from NFA oracle")
    if (
        observed.get("build-outcome") != "completed"
        or observed.get("scan-outcome") != "completed"
        or observed.get("source-digest") != source_digest
        or observed.get("nfa-answer-digest") != expected_digest
        or observed.get("nfa-answers") != str(expected_count)
        or observed.get("source-passes") != "1"
        or observed.get("decoded-bytes") != str(len(payload))
        or observed.get("input-bytes") != str(len(payload))
        or observed.get("input-scalars") != str(len(payload.decode("utf-8")))
    ):
        raise GateFailure(f"{language}: one-pass DFA accounting or provenance differs")
    return (
        matrix_row(case, observed),
        (abi_path, nfa_path, input_path),
        len(tag_specs),
    )


def check_boundaries(
    chart_binary: Path,
    dfa_binary: Path,
    petta_root: Path,
    temp: Path,
    paths: tuple[Path, Path, Path],
) -> int:
    abi_path, nfa_path, input_path = paths
    invalid = temp / "invalid.utf8"
    invalid.write_bytes(b"\xc0\x80")
    run_native(
        dfa_binary, abi_path, nfa_path, invalid, expect_success=False
    )

    terms = nfa_path.read_text(encoding="utf-8").splitlines()
    duplicate = temp / "duplicate.nfa"
    duplicate.write_text(
        "\n".join((terms[0], terms[0], *terms[1:])) + "\n",
        encoding="utf-8",
    )
    run_native(
        dfa_binary, abi_path, duplicate, input_path, expect_success=False
    )

    boundary_terms: list[str] = []
    for index, text in enumerate(terms):
        term = one_term(text, f"Prime boundary NFA answer {index}")
        compiled = app(term, "compile-span-nfa", 2)
        if (
            compiled is not None
            and compiled[1] == Symbol("cetta-prime-token-boundary")
        ):
            boundary_terms.append(text)
    if not boundary_terms:
        raise GateFailure("Prime boundary tag has no compiled NFA answers")
    boundary_nfa = temp / "prime-token-boundary.nfa"
    boundary_nfa.write_text(
        "\n".join(boundary_terms) + "\n", encoding="utf-8"
    )
    boundary_scalars = (9, 10, 11, 12, 13, 32, 34, 40, 41, 59, 36, 97, 955)
    boundary_payload = "".join(chr(value) for value in boundary_scalars).encode()
    boundary_input = temp / "prime-token-boundary.utf8"
    boundary_input.write_bytes(boundary_payload)
    boundary_result = run_native(
        dfa_binary, abi_path, boundary_nfa, boundary_input
    )
    boundary_tag = "cetta-prime-token-boundary"
    expected_boundary_tokens = tuple(
        (boundary_tag, index, index + 1, index, index + 1)
        for index in range(10)
    ) + ((boundary_tag, 13, 13, 14, 14),)
    if (
        boundary_result.get("tokens") != expected_boundary_tokens
        or boundary_result.get("source-passes") != "1"
        or boundary_result.get("decoded-bytes") != "14"
        or boundary_result.get("input-bytes") != "14"
        or boundary_result.get("input-scalars") != "13"
    ):
        raise GateFailure(
            "Prime token-boundary span relation or one-pass receipt changed"
        )

    unsupported = dict(CASES[0])
    unsupported["tag"] = "cetta-prime-variable"
    c_result = c_compiler_answers(chart_binary, unsupported)
    petta_result = petta_compiler_answers(petta_root, unsupported)
    empty_digest = answer_digest(())
    if (
        c_result.get("outcome") != "NoAnswer"
        or c_result.get("answers") != 0
        or c_result.get("term_digest") != empty_digest
        or petta_result.get("answers") != "0"
        or petta_result.get("answer-digest") != empty_digest
        or petta_result.get("terms") != ()
    ):
        raise GateFailure("peek-bearing regular-span boundary did not fail closed")
    return 4


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--dfa-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()

    chart_binary = arguments.chart_binary.resolve()
    dfa_binary = arguments.dfa_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    with tempfile.TemporaryDirectory(prefix="gslt2parse-regular-span-") as raw:
        temp = Path(raw)
        rows: list[str] = []
        compiler_agreements = 0
        boundary_paths: tuple[Path, Path, Path] | None = None
        for case in CASES:
            row, paths, agreements = check_case(
                chart_binary, dfa_binary, petta_root, case, temp
            )
            rows.append(row)
            compiler_agreements += agreements
            if case["language"] == "prime":
                boundary_paths = paths
        assert boundary_paths is not None
        boundaries = check_boundaries(
            chart_binary, dfa_binary, petta_root, temp, boundary_paths
        )
    digest = hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()
    if digest != MATRIX_DIGEST:
        raise GateFailure(f"regular-span matrix digest changed: {digest}")
    count = len(CASES)
    print(
        f"(RegularSpanGSLTDFAV1MatrixSummary {count} {compiler_agreements} "
        f"{count} "
        f"{count} {boundaries} {MATRIX_DIGEST} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
