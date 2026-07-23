#!/usr/bin/env python3
"""Exhaustive source-law and end-to-end mutation gate for the HE reader."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from gslt2parse_schema_v1 import (
    Presentation,
    RuleDecl,
    SExpr,
    Symbol,
    Variable,
    admit,
    package_digest,
    parse_presentation,
    parse_sexprs,
    render,
)
from test_he_reader_guard_exec_v1 import parse_native_output
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_guard_compiler_v1 import COMPILER as GUARD_COMPILER
from test_parser_pack_guard_regular_v1 import COMPILER as REGULAR_COMPILER


ROOT = Path(__file__).resolve().parents[1]
PRESENTATIONS = ROOT / "experiments" / "gslt2parse_foundation" / "presentations"
HE_SOURCES = tuple(str(path) for path in ABI_CASES["he"]["sources"])
CLASS_SOURCE = "shared/he_reader_scalar_classes_v1.metta"
READER_SOURCE = "languages/he_reader_v1.metta"
REGULAR_COMPILER_SOURCE = "compiler/regular_span_compiler_v1.metta"

HE_WHITESPACE = frozenset({
    0x0009,
    0x000A,
    0x000B,
    0x000C,
    0x000D,
    0x0020,
    0x0085,
    0x00A0,
    0x1680,
    0x2000,
    0x2001,
    0x2002,
    0x2003,
    0x2004,
    0x2005,
    0x2006,
    0x2007,
    0x2008,
    0x2009,
    0x200A,
    0x2028,
    0x2029,
    0x202F,
    0x205F,
    0x3000,
})
HE_EOL = frozenset({0x000A, 0x000D})
HEX = frozenset(ord(char) for char in "0123456789abcdefABCDEF")
HEX_NONZERO = HEX - {ord("0")}
HEX_D = frozenset({ord("d"), ord("D")})
HEX_LOW_SEVEN = frozenset(ord(char) for char in "01234567")
HEX_NONZERO_NOND = HEX_NONZERO - HEX_D
HEX_ABOVE_ONE_NOND = HEX_NONZERO_NOND - {ord("1")}
SIMPLE_ESCAPES = frozenset(ord(char) for char in "'\"\\nrt")

EXPECTED_POINT_CLASSES = {
    "he-whitespace-scalar": HE_WHITESPACE,
    "he-newline-scalar": HE_EOL,
    "he-simple-escape-scalar": SIMPLE_ESCAPES,
    "he-hex-scalar": HEX,
    "he-hex-nonzero-scalar": HEX_NONZERO,
    "he-hex-d-scalar": HEX_D,
    "he-hex-low-seven-scalar": HEX_LOW_SEVEN,
    "he-zero-scalar": frozenset({ord("0")}),
    "he-one-scalar": frozenset({ord("1")}),
    "he-hex-high-eight-scalar": HEX - HEX_LOW_SEVEN,
    "he-hex-nonzero-nond-scalar": HEX_NONZERO_NOND,
    "he-hex-above-one-nond-scalar": HEX_ABOVE_ONE_NOND,
}
EXPECTED_EXCEPT_CLASSES = {
    "he-non-newline-scalar": HE_EOL,
    "he-string-plain-scalar": frozenset({0x22, 0x5C}),
    "he-word-start-scalar": HE_WHITESPACE
    | frozenset({0x22, 0x24, 0x28, 0x29, 0x3B}),
    "he-word-tail-scalar": HE_WHITESPACE
    | frozenset({0x28, 0x29, 0x3B}),
    "he-variable-tail-scalar": HE_WHITESPACE
    | frozenset({0x23, 0x28, 0x29, 0x3B}),
}
EXPECTED_ASCII_DISPATCH_DIGEST = (
    "e81e6efaa47ed16604a6ae1a5772146613195ed97af721e4f09cb2a2f2b64fd6"
)


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class ClassSpec:
    mode: str
    points: frozenset[int]

    def contains(self, codepoint: int) -> bool:
        return (
            codepoint in self.points
            if self.mode == "point"
            else codepoint not in self.points
        )


def symbol(term: SExpr, context: str) -> str:
    if not isinstance(term, Symbol):
        raise GateFailure(f"{context}: expected a symbol, got {render(term)}")
    return term.text


def application(term: SExpr, head: str, arity: int, context: str) -> tuple[SExpr, ...]:
    if (
        not isinstance(term, tuple)
        or len(term) != arity + 1
        or not isinstance(term[0], Symbol)
        or term[0].text != head
    ):
        raise GateFailure(
            f"{context}: expected {head}/{arity}, got {render(term)}"
        )
    return term


def codepoint(term: SExpr, context: str) -> int:
    node = application(term, "cp", 1, context)
    value = node[1]
    if not isinstance(value, int) or not 0 <= value <= 0x10FFFF:
        raise GateFailure(f"{context}: invalid scalar spelling {render(term)}")
    return value


def class_specs(path: Path) -> dict[str, ClassSpec]:
    presentation = parse_presentation(path)
    point_rows: dict[str, set[int]] = {}
    except_rows: dict[str, frozenset[int]] = {}
    for rule in presentation.rules:
        head = application(rule.head, "member", 2, f"rule {rule.name}")
        name = symbol(head[1], f"rule {rule.name} class")
        member = head[2]
        if isinstance(member, Variable):
            if member.name != "codepoint" or not rule.body:
                raise GateFailure(f"{rule.name}: malformed complement class")
            exclusions: list[int] = []
            for index, goal in enumerate(rule.body):
                different = application(
                    goal, "different", 2, f"{rule.name} body[{index}]"
                )
                if different[1] != member:
                    raise GateFailure(
                        f"{rule.name}: complement guard uses another variable"
                    )
                exclusions.append(
                    codepoint(different[2], f"{rule.name} body[{index}]")
                )
            if name in except_rows or name in point_rows:
                raise GateFailure(f"{name}: mixed or duplicate class definition")
            if exclusions != sorted(set(exclusions)):
                raise GateFailure(f"{name}: exclusions are not unique and sorted")
            except_rows[name] = frozenset(exclusions)
        else:
            if rule.body or name in except_rows:
                raise GateFailure(f"{rule.name}: malformed point class")
            point_rows.setdefault(name, set()).add(
                codepoint(member, f"rule {rule.name}")
            )

    specs = {
        name: ClassSpec("point", frozenset(points))
        for name, points in point_rows.items()
    }
    specs.update({
        name: ClassSpec("except", points)
        for name, points in except_rows.items()
    })
    return specs


def require_class_law(specs: dict[str, ClassSpec]) -> int:
    expected_names = set(EXPECTED_POINT_CLASSES) | set(EXPECTED_EXCEPT_CLASSES)
    if set(specs) != expected_names:
        raise GateFailure(
            "HE scalar class inventory changed: "
            f"expected {sorted(expected_names)!r}, observed {sorted(specs)!r}"
        )
    for name, points in EXPECTED_POINT_CLASSES.items():
        if specs[name] != ClassSpec("point", points):
            raise GateFailure(f"{name}: point membership differs from HE law")
    for name, points in EXPECTED_EXCEPT_CLASSES.items():
        if specs[name] != ClassSpec("except", points):
            raise GateFailure(f"{name}: complement differs from HE law")
    return 0x110000 - 0x800


def definition_map(path: Path) -> dict[str, SExpr]:
    presentation = parse_presentation(path)
    definitions: dict[str, SExpr] = {}
    for rule in presentation.rules:
        head = application(rule.head, "definition", 2, f"rule {rule.name}")
        name = symbol(head[1], f"rule {rule.name} definition")
        if name in definitions:
            raise GateFailure(f"duplicate HE definition {name}")
        if rule.body:
            raise GateFailure(f"HE definition {name} unexpectedly has premises")
        definitions[name] = head[2]
    return definitions


def sexpr(text: str) -> SExpr:
    terms = parse_sexprs(text, source="HE structural law")
    if len(terms) != 1:
        raise GateFailure("HE structural law is not singular")
    return terms[0]


EXPECTED_DEFINITIONS = {
    "he-comment-boundary": sexpr(
        "(alt (class he-newline-scalar) eof)"
    ),
    "he-word-boundary": sexpr(
        "(alt eof (alt (class he-whitespace-scalar) "
        "(alt (char (cp 40)) (alt (char (cp 41)) (char (cp 59))))))"
    ),
    "he-variable-boundary": sexpr(
        "(alt eof (alt (class he-whitespace-scalar) "
        "(alt (char (cp 40)) (alt (char (cp 41)) (char (cp 59))))))"
    ),
    "he-word": sexpr(
        "(node word (left (seq (class he-word-start-scalar) "
        "(star (class he-word-tail-scalar))) "
        "(peek (ref he-word-boundary))))"
    ),
    "he-variable": sexpr(
        "(node variable (left (right (char (cp 36)) "
        "(seq (class he-variable-tail-scalar) "
        "(star (class he-variable-tail-scalar)))) "
        "(peek (ref he-variable-boundary))))"
    ),
    "he-atom": sexpr(
        "(alt (ref he-string) (alt (ref he-variable) "
        "(alt (ref he-expression) (ref he-word))))"
    ),
    "he-document": sexpr(
        "(node document (left (right (ref he-skip) "
        "(star (left (ref he-top-atom) (ref he-skip)))) eof))"
    ),
}


def require_structural_law(definitions: dict[str, SExpr]) -> None:
    for name, expected in EXPECTED_DEFINITIONS.items():
        observed = definitions.get(name)
        if observed != expected:
            rendered = "<absent>" if observed is None else render(observed)
            raise GateFailure(
                f"HE structural law changed at {name}: {rendered}"
            )


def ascii_dispatch(specs: dict[str, ClassSpec]) -> tuple[str, dict[str, int]]:
    rows: list[str] = []
    counts: dict[str, int] = {}
    word_start = specs["he-word-start-scalar"]
    whitespace = specs["he-whitespace-scalar"]
    for value in range(128):
        if whitespace.contains(value):
            kind = "layout"
        elif value == 0x3B:
            kind = "comment"
        elif value == 0x22:
            kind = "string"
        elif value == 0x24:
            kind = "variable"
        elif value == 0x28:
            kind = "expression"
        elif value == 0x29:
            kind = "invalid-close"
        elif word_start.contains(value):
            kind = "word"
        else:
            kind = "invalid"
        rows.append(f"{value}\t{kind}\n")
        counts[kind] = counts.get(kind, 0) + 1
    if rows[0x23] != "35\tword\n":
        raise GateFailure("hash is not dispatched through authored HE word syntax")
    if rows[0x24] != "36\tvariable\n":
        raise GateFailure("dollar is not dispatched through HE variable syntax")
    digest = sha256("".join(rows).encode("utf-8")).hexdigest()
    if EXPECTED_ASCII_DISPATCH_DIGEST and digest != EXPECTED_ASCII_DISPATCH_DIGEST:
        raise GateFailure(f"HE ASCII dispatch matrix changed: {digest}")
    return digest, counts


def points_list(term: SExpr, context: str) -> frozenset[int]:
    points: list[int] = []
    cursor = term
    while cursor != Symbol("pp-points-nil"):
        node = application(cursor, "pp-points-cons", 2, context)
        points.append(codepoint(node[1], context))
        cursor = node[2]
    if points != sorted(set(points)):
        raise GateFailure(f"{context}: compiled points are not unique and sorted")
    return frozenset(points)


def abi_fields(stream: bytes) -> tuple[dict[str, str], dict[str, ClassSpec]]:
    scalars: dict[str, str] = {}
    point_rows: dict[str, set[int]] = {}
    except_rows: dict[str, frozenset[int]] = {}
    for raw_line in stream.decode("utf-8").splitlines():
        field, separator, value = raw_line.partition("\t")
        if not separator:
            continue
        if field in {"source-digest", "pack-digest"}:
            scalars[field] = value
        if field != "class-clause":
            continue
        terms = parse_sexprs(value, source="compiled HE class clause")
        if len(terms) != 1:
            raise GateFailure("compiled HE class clause is not singular")
        clause = terms[0]
        if (
            isinstance(clause, tuple)
            and len(clause) == 3
            and clause[0] == Symbol("pp-class-point")
        ):
            name = symbol(clause[1], "compiled point class")
            if name in except_rows:
                raise GateFailure(f"compiled HE class {name} is mixed")
            point_rows.setdefault(name, set()).add(
                codepoint(clause[2], f"compiled class {name}")
            )
        elif (
            isinstance(clause, tuple)
            and len(clause) == 3
            and clause[0] == Symbol("pp-class-except")
        ):
            name = symbol(clause[1], "compiled complement class")
            if name in point_rows or name in except_rows:
                raise GateFailure(f"compiled HE class {name} is duplicated")
            except_rows[name] = points_list(
                clause[2], f"compiled class {name}"
            )
        else:
            raise GateFailure(f"unknown compiled HE class clause {value}")
    specs = {
        name: ClassSpec("point", frozenset(points))
        for name, points in point_rows.items()
    }
    specs.update({
        name: ClassSpec("except", points)
        for name, points in except_rows.items()
    })
    return scalars, specs


def run_checked(command: list[str], *, cwd: Path, timeout: int = 180) -> bytes:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"command failed ({' '.join(command[:3])}): "
            + completed.stderr.decode("utf-8", errors="replace").strip()
        )
    return completed.stdout


def export_abi(petta_root: Path, presentation_root: Path) -> bytes:
    exporter = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "parser_pack_abi_export_v1.pl"
    )
    stream = run_checked(
        [
            "swipl",
            "-q",
            "-f",
            str(exporter),
            "--",
            str(presentation_root),
            "he-document",
            "partial",
            *HE_SOURCES,
        ],
        cwd=exporter.parent,
    )
    if not stream.startswith(b"parser-pack-abi-v1\n") or not stream.endswith(
        b"end\n"
    ):
        raise GateFailure("compiled HE ABI framing changed")
    return stream


def chart_query(
    chart_binary: Path,
    presentation_root: Path,
    compiler_sources: tuple[str, ...],
    query: str,
    *,
    certificates: bool,
) -> dict[str, object]:
    command = [str(chart_binary)]
    command.extend(str(presentation_root / path) for path in compiler_sources)
    for path in HE_SOURCES:
        command.extend(("--reflect-source", str(presentation_root / path)))
    command.extend(("--query-text", query, "--timeout", "30"))
    if certificates:
        command.append("--certs")
    output = run_checked(command, cwd=ROOT, timeout=90)
    try:
        result = json.loads(output)
    except json.JSONDecodeError as error:
        raise GateFailure("native finite-Horn compiler emitted non-JSON") from error
    if not isinstance(result, dict):
        raise GateFailure("native finite-Horn compiler response changed shape")
    return result


def petta_guard_row(petta_root: Path, presentation_root: Path) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "test_parser_pack_guard_compiler_v1.pl"
    )
    output = run_checked(
        ["swipl", "-q", "-f", str(script), "--", str(presentation_root)],
        cwd=script.parent,
        timeout=90,
    )
    try:
        result = json.loads(output)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa guard compiler emitted non-JSON") from error
    rows = result.get("rows") if isinstance(result, dict) else None
    if not isinstance(rows, list):
        raise GateFailure("PeTTa guard compiler rows are absent")
    for row in rows:
        if isinstance(row, dict) and row.get("label") == "he":
            return row
    raise GateFailure("PeTTa guard compiler omitted HE")


def guard_artifacts(
    chart_binary: Path,
    petta_root: Path,
    presentation_root: Path,
) -> tuple[tuple[str, ...], bytes]:
    guard = chart_query(
        chart_binary,
        presentation_root,
        GUARD_COMPILER,
        "(compile-pack-positive-guard ?owner ?guard)",
        certificates=True,
    )
    terms = guard.get("terms")
    certificates = guard.get("certs")
    if (
        guard.get("answers") != 3
        or not isinstance(terms, list)
        or not isinstance(certificates, list)
        or len(terms) != 3
        or len(certificates) != 3
    ):
        raise GateFailure("mutated HE positive-guard compilation changed shape")
    reference = petta_guard_row(petta_root, presentation_root)
    if (
        reference.get("count") != 3
        or reference.get("digest") != guard.get("term_digest")
        or reference.get("terms") != terms
    ):
        raise GateFailure("C and PeTTa mutated HE guards differ")

    nfa = chart_query(
        chart_binary,
        presentation_root,
        REGULAR_COMPILER,
        "(compile-span-nfa (pp-positive-guard-tag ?state) ?edge)",
        certificates=False,
    )
    nfa_terms = nfa.get("terms")
    if nfa.get("answers") != 60 or not isinstance(nfa_terms, list):
        raise GateFailure("mutated HE guard NFA changed shape")

    required = (
        "source_digest",
        "pre_reflection_digest",
        "environment_digest",
    )
    if any(not isinstance(reference.get(field), str) for field in required):
        raise GateFailure("mutated HE guard provenance is absent")
    derivations = sorted(zip(terms, certificates, strict=True))
    lines = [
        "parser-pack-positive-guard-evidence-v1",
        f"source-digest\t{reference['source_digest']}",
        f"pre-reflection-digest\t{reference['pre_reflection_digest']}",
        f"environment-digest\t{reference['environment_digest']}",
        f"answer-set-digest\t{reference['digest']}",
    ]
    lines.extend(
        f"derivation\t{term}\t{certificate}"
        for term, certificate in derivations
    )
    lines.append("end")
    return tuple(str(term) for term in nfa_terms), (
        "\n".join(lines) + "\n"
    ).encode("utf-8")


def petta_decision(
    petta_root: Path, presentation_root: Path, input_path: Path
) -> dict[str, object]:
    script = (
        petta_root
        / "experiments"
        / "gslt2parse_foundation"
        / "he_reader_gslt_reference_v1.pl"
    )
    output = run_checked(
        [
            "swipl",
            "-q",
            "-f",
            str(script),
            "--",
            str(presentation_root),
            str(input_path),
            "4096",
        ],
        cwd=script.parent,
    )
    try:
        row = json.loads(output)
    except json.JSONDecodeError as error:
        raise GateFailure("PeTTa mutated HE reference emitted non-JSON") from error
    if not isinstance(row, dict) or row.get("protocol") != "he-reader-gslt-reference-v1":
        raise GateFailure("PeTTa mutated HE reference framing changed")
    return row


def native_decision(
    exec_binary: Path,
    directory: Path,
    abi: bytes,
    guard_terms: tuple[str, ...],
    evidence: bytes,
    input_bytes: bytes,
    compiler_digest: str,
) -> dict[str, object]:
    abi_path = directory / "reader.abi"
    guard_path = directory / "guard.nfa"
    evidence_path = directory / "guard.evidence"
    input_path = directory / "input.metta"
    abi_path.write_bytes(abi)
    guard_path.write_text("\n".join(guard_terms) + "\n", encoding="utf-8")
    evidence_path.write_bytes(evidence)
    input_path.write_bytes(input_bytes)
    completed = subprocess.run(
        [
            str(exec_binary),
            str(abi_path),
            str(guard_path),
            str(evidence_path),
            str(input_path),
            compiler_digest,
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=180,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "native mutated HE reader failed: " + completed.stderr.strip()
        )
    return parse_native_output(completed.stdout)


def expect_contract_failure(presentation_root: Path) -> None:
    try:
        validate_source_contract(presentation_root)
    except GateFailure:
        return
    raise GateFailure("wrong HE source mutation retained the ratified contract")


def validate_source_contract(
    presentation_root: Path,
) -> tuple[dict[str, ClassSpec], str, dict[str, int], int]:
    specs = class_specs(presentation_root / CLASS_SOURCE)
    scalar_count = require_class_law(specs)
    require_structural_law(definition_map(presentation_root / READER_SOURCE))
    digest, counts = ascii_dispatch(specs)
    return specs, digest, counts, scalar_count


def mutate_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise GateFailure(f"{label}: mutation anchor count changed")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def run_mutant(
    label: str,
    mutation,
    input_bytes: bytes,
    expected_decision: str,
    chart_binary: Path,
    exec_binary: Path,
    petta_root: Path,
    baseline_source_digest: str,
) -> int:
    with tempfile.TemporaryDirectory(
        prefix=f"gslt2parse-he-source-{label}-"
    ) as raw:
        directory = Path(raw)
        presentation_root = directory / "presentations"
        shutil.copytree(PRESENTATIONS, presentation_root)
        mutation(presentation_root)
        expect_contract_failure(presentation_root)
        admitted = admit(
            tuple(presentation_root / path for path in HE_SOURCES)
        )
        source_digest = package_digest(admitted)
        if source_digest == baseline_source_digest:
            raise GateFailure(f"{label}: source mutation retained its digest")
        abi = export_abi(petta_root, presentation_root)
        fields, _ = abi_fields(abi)
        if fields.get("source-digest") != source_digest:
            raise GateFailure(f"{label}: compiled ABI lost source identity")
        guard_terms, evidence = guard_artifacts(
            chart_binary, petta_root, presentation_root
        )
        input_path = directory / "reference-input.metta"
        input_path.write_bytes(input_bytes)
        reference = petta_decision(petta_root, presentation_root, input_path)
        if reference.get("decision") != expected_decision:
            raise GateFailure(
                f"{label}: PeTTa mutation decision is "
                f"{reference.get('decision')!r}"
            )
        compiler_digest = sha256(
            (presentation_root / REGULAR_COMPILER_SOURCE).read_bytes()
        ).hexdigest()
        native = native_decision(
            exec_binary,
            directory,
            abi,
            guard_terms,
            evidence,
            input_bytes,
            compiler_digest,
        )
        if (
            native.get("gll-decision") != expected_decision
            or native.get("glr-decision") != expected_decision
            or native.get("gll-decision") != native.get("glr-decision")
            or native.get("gll-result") != native.get("glr-result")
            or native.get("gll-result") != reference.get("results")
            or native.get("source-passes") != "1"
        ):
            raise GateFailure(f"{label}: native and PeTTa mutation results differ")
    return 1


def hash_exclusion_mutation(root: Path) -> None:
    path = root / CLASS_SOURCE
    mutate_once(
        path,
        "(different ?codepoint (cp 34)) (different ?codepoint (cp 36))",
        "(different ?codepoint (cp 34)) (different ?codepoint (cp 35)) "
        "(different ?codepoint (cp 36))",
        "hash exclusion",
    )


def empty_variable_mutation(root: Path) -> None:
    path = root / READER_SOURCE
    mutate_once(
        path,
        "(seq (class he-variable-tail-scalar) "
        "(star (class he-variable-tail-scalar)))",
        "(star (class he-variable-tail-scalar))",
        "empty variable",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--exec-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    arguments = parser.parse_args()
    chart_binary = arguments.chart_binary.resolve()
    exec_binary = arguments.exec_binary.resolve()
    petta_root = arguments.petta_root.resolve()
    if not chart_binary.is_file() or not exec_binary.is_file():
        raise GateFailure("HE source-faithfulness native binary is absent")
    if not petta_root.is_dir():
        raise GateFailure("PeTTa source-faithfulness checkout is absent")

    specs, dispatch_digest, dispatch_counts, scalar_count = (
        validate_source_contract(PRESENTATIONS)
    )
    admitted: tuple[Presentation, ...] = admit(
        tuple(PRESENTATIONS / path for path in HE_SOURCES)
    )
    source_digest = package_digest(admitted)
    abi = export_abi(petta_root, PRESENTATIONS)
    fields, compiled_specs = abi_fields(abi)
    if fields.get("source-digest") != source_digest:
        raise GateFailure("compiled HE ABI source digest differs from source")
    if compiled_specs != specs:
        missing = sorted(set(specs) - set(compiled_specs))
        extra = sorted(set(compiled_specs) - set(specs))
        changed = sorted(
            name
            for name in set(specs) & set(compiled_specs)
            if specs[name] != compiled_specs[name]
        )
        raise GateFailure(
            "compiled HE ABI class relation differs from source: "
            f"missing={missing!r}, extra={extra!r}, changed={changed!r}"
        )

    mutations = 0
    mutations += run_mutant(
        "hash-excluded",
        hash_exclusion_mutation,
        b"#foo",
        "rejected",
        chart_binary,
        exec_binary,
        petta_root,
        source_digest,
    )
    mutations += run_mutant(
        "empty-variable",
        empty_variable_mutation,
        b"$",
        "accepted",
        chart_binary,
        exec_binary,
        petta_root,
        source_digest,
    )

    print(
        "(HEReaderSourceFaithfulnessV1Summary "
        f"{scalar_count} {len(specs)} 128 {len(dispatch_counts)} "
        f"{len(compiled_specs)} {mutations} {source_digest} "
        f"{fields.get('pack-digest', '')} {dispatch_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
