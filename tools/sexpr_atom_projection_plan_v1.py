#!/usr/bin/env python3
"""Compile declarative S-expression host-Atom projection facts."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path

import gslt2parse_schema_v1 as schema


ROOT = Path(__file__).resolve().parents[1]
PROJECTION = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "shared"
    / "sexpr_atom_projection_v1.metta"
)
HE_READER = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "languages"
    / "he_reader_v1.metta"
)
PROFILES = {"he-v1": HE_READER}


class PlanError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class Profile:
    name: str
    node: str
    pair: str
    cons: str
    nil: str
    codepoint: str
    document: str
    expression: str


def symbol(term: schema.SExpr, context: str) -> str:
    if not isinstance(term, schema.Symbol):
        raise PlanError(f"{context}: expected symbol, found {schema.render(term)}")
    return term.text


def form(term: schema.SExpr, context: str) -> tuple[schema.SExpr, ...]:
    if not isinstance(term, tuple):
        raise PlanError(f"{context}: expected form, found {schema.render(term)}")
    return term


def facts(
    presentation: schema.Presentation, relation: str, arity: int
) -> list[tuple[schema.SExpr, ...]]:
    found: list[tuple[schema.SExpr, ...]] = []
    for rule in presentation.rules:
        head = form(rule.head, f"rule {rule.name}")
        if head and isinstance(head[0], schema.Symbol) and head[0].text == relation:
            if len(head) != arity + 1:
                raise PlanError(f"{relation}: wrong fact arity in {rule.name}")
            if rule.body:
                raise PlanError(f"{relation}: non-fact rule {rule.name}")
            found.append(head[1:])
    return found


def decode_list(term: schema.SExpr, context: str) -> list[schema.SExpr]:
    values: list[schema.SExpr] = []
    cursor = term
    while not (
        isinstance(cursor, schema.Symbol)
        and cursor.text == "sexpr-projection-nil"
    ):
        items = form(cursor, context)
        if (
            len(items) != 3
            or symbol(items[0], context) != "sexpr-projection-cons"
        ):
            raise PlanError(f"{context}: malformed projection list")
        values.append(items[1])
        cursor = items[2]
    return values


def encode_list(values: list[schema.SExpr]) -> schema.SExpr:
    result: schema.SExpr = schema.Symbol("sap-nil")
    for value in reversed(values):
        result = (schema.Symbol("sap-cons"), value, result)
    return result


def presentation_node_labels(term: schema.SExpr) -> set[str]:
    labels: set[str] = set()
    if isinstance(term, tuple):
        if (
            len(term) == 3
            and isinstance(term[0], schema.Symbol)
            and term[0].text == "node"
            and isinstance(term[1], schema.Symbol)
        ):
            labels.add(term[1].text)
        for child in term:
            labels.update(presentation_node_labels(child))
    return labels


def reader_node_labels(reader: schema.Presentation) -> set[str]:
    labels: set[str] = set()
    for rule in reader.rules:
        labels.update(presentation_node_labels(rule.head))
        for premise in rule.body:
            labels.update(presentation_node_labels(premise))
    return labels


def load_profile(
    projection: schema.Presentation, profile_name: str
) -> Profile:
    rows = [
        row
        for row in facts(projection, "sexpr-atom-profile", 8)
        if symbol(row[0], "projection profile") == profile_name
    ]
    if len(rows) != 1:
        raise PlanError(f"{profile_name}: expected exactly one profile fact")
    row = rows[0]
    return Profile(profile_name, *(symbol(value, profile_name) for value in row[1:]))


def compile_plan(
    profile_name: str,
    projection_path: Path = PROJECTION,
    reader_path: Path | None = None,
) -> schema.SExpr:
    if profile_name not in PROFILES and reader_path is None:
        raise PlanError(f"unknown S-expression projection profile: {profile_name}")
    projection = schema.parse_presentation(projection_path)
    reader = schema.parse_presentation(reader_path or PROFILES[profile_name])
    profile = load_profile(projection, profile_name)
    available_labels = reader_node_labels(reader)

    compiled_rules: list[schema.SExpr] = []
    source_labels: set[str] = set()
    for row in facts(projection, "sexpr-atom-rule", 4):
        if symbol(row[0], "atom-rule profile") != profile_name:
            continue
        source = symbol(row[1], "atom-rule source")
        output_kind = symbol(row[2], "atom-rule output kind")
        text_mode = symbol(row[3], "atom-rule text mode")
        if output_kind not in {"symbol", "string"}:
            raise PlanError(f"{source}: unsupported output kind {output_kind}")
        if text_mode not in {"raw", "decoded", "decoded-quoted"}:
            raise PlanError(f"{source}: unsupported text mode {text_mode}")
        if source in source_labels:
            raise PlanError(f"duplicate projection source label {source}")
        source_labels.add(source)
        compiled_rules.append(
            (
                schema.Symbol("sap-text-rule"),
                schema.Symbol(source),
                schema.Symbol(output_kind),
                schema.Symbol(text_mode),
            )
        )

    for row in facts(projection, "sexpr-variable-rule", 4):
        if symbol(row[0], "variable-rule profile") != profile_name:
            continue
        source = symbol(row[1], "variable-rule source")
        text_mode = symbol(row[2], "variable-rule text mode")
        anonymous = symbol(row[3], "variable-rule anonymous policy")
        if text_mode != "raw":
            raise PlanError(f"{source}: v1 variable text must be raw")
        if anonymous != "no-anonymous":
            raise PlanError(f"{source}: unsupported v1 anonymous policy")
        if source in source_labels:
            raise PlanError(f"duplicate projection source label {source}")
        source_labels.add(source)
        compiled_rules.append(
            (
                schema.Symbol("sap-variable-rule"),
                schema.Symbol(source),
                schema.Symbol(text_mode),
                schema.Symbol(anonymous),
            )
        )

    compiled_wrappers: list[schema.SExpr] = []
    wrapper_labels: set[str] = set()
    for row in facts(projection, "sexpr-wrapper-map", 4):
        if symbol(row[0], "wrapper-map profile") != profile_name:
            continue
        label = symbol(row[1], "wrapper-map label")
        default = symbol(row[2], "wrapper-map default")
        if default not in {"identity", "reject"}:
            raise PlanError(f"{label}: unsupported wrapper-map default")
        if label in wrapper_labels:
            raise PlanError(f"duplicate text wrapper {label}")
        wrapper_labels.add(label)
        mappings: list[schema.SExpr] = []
        seen_inputs: set[int] = set()
        for mapping in decode_list(row[3], f"{label} codepoint map"):
            items = form(mapping, f"{label} codepoint map")
            if len(items) != 3 or symbol(items[0], label) != "sexpr-codepoint-map":
                raise PlanError(f"{label}: malformed codepoint map")
            if not isinstance(items[1], int) or not isinstance(items[2], int):
                raise PlanError(f"{label}: codepoint map values must be integers")
            if items[1] in seen_inputs:
                raise PlanError(f"{label}: duplicate codepoint-map input")
            if not (0 <= items[1] <= 0x10FFFF and 0 <= items[2] <= 0x10FFFF):
                raise PlanError(f"{label}: codepoint map is outside Unicode")
            seen_inputs.add(items[1])
            mappings.append(
                (schema.Symbol("sap-codepoint-map"), items[1], items[2])
            )
        if not mappings:
            raise PlanError(f"{label}: empty codepoint map")
        compiled_wrappers.append(
            (
                schema.Symbol("sap-map-wrapper"),
                schema.Symbol(label),
                schema.Symbol(default),
                encode_list(mappings),
            )
        )

    for row in facts(projection, "sexpr-wrapper-hex", 6):
        if symbol(row[0], "wrapper-hex profile") != profile_name:
            continue
        label = symbol(row[1], "wrapper-hex label")
        if label in wrapper_labels:
            raise PlanError(f"duplicate text wrapper {label}")
        wrapper_labels.add(label)
        numbers = row[2:]
        if any(not isinstance(value, int) for value in numbers):
            raise PlanError(f"{label}: hex bounds must be integers")
        min_digits, max_digits, max_value, radix = numbers
        if radix != 16:
            raise PlanError(f"{label}: v1 supports only radix 16")
        if not (1 <= min_digits <= max_digits <= 8):
            raise PlanError(f"{label}: invalid hex digit bounds")
        if not (0 <= max_value <= 0x10FFFF):
            raise PlanError(f"{label}: invalid maximum scalar")
        compiled_wrappers.append(
            (
                schema.Symbol("sap-hex-wrapper"),
                schema.Symbol(label),
                min_digits,
                max_digits,
                max_value,
            )
        )

    required = source_labels | wrapper_labels | {
        profile.document,
        profile.expression,
    }
    missing = required - available_labels
    if missing:
        raise PlanError(
            f"{profile_name}: projection labels absent from reader: {sorted(missing)}"
        )
    structural_labels = {
        profile.node,
        profile.pair,
        profile.cons,
        profile.nil,
        profile.codepoint,
        profile.document,
        profile.expression,
    }
    if len(structural_labels) != 7:
        raise PlanError("S-expression carrier labels must be distinct")
    if (source_labels | wrapper_labels) & structural_labels:
        raise PlanError("structural labels overlap leaf or wrapper labels")
    if source_labels & wrapper_labels:
        raise PlanError("leaf and wrapper labels overlap")
    if not compiled_rules:
        raise PlanError(f"{profile_name}: projection has no leaf rules")

    return (
        schema.Symbol("sap-plan-v1"),
        schema.Symbol(profile.node),
        schema.Symbol(profile.pair),
        schema.Symbol(profile.cons),
        schema.Symbol(profile.nil),
        schema.Symbol(profile.codepoint),
        schema.Symbol(profile.document),
        schema.Symbol(profile.expression),
        encode_list(compiled_rules),
        encode_list(compiled_wrappers),
    )


def compile_artifact(
    profile_name: str,
    projection_path: Path = PROJECTION,
    reader_path: Path | None = None,
) -> schema.SExpr:
    selected_reader = reader_path or PROFILES[profile_name]
    plan = compile_plan(profile_name, projection_path, selected_reader)
    canonical_plan = schema.render(plan).encode("utf-8")
    return (
        schema.Symbol("sap-artifact-v1"),
        schema.Symbol(profile_name),
        schema.Symbol(sha256(projection_path.read_bytes()).hexdigest()),
        schema.Symbol(sha256(selected_reader.read_bytes()).hexdigest()),
        schema.Symbol(sha256(Path(__file__).read_bytes()).hexdigest()),
        schema.Symbol(sha256(canonical_plan).hexdigest()),
        plan,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=tuple(PROFILES), required=True)
    parser.add_argument("--projection", type=Path, default=PROJECTION)
    parser.add_argument("--reader", type=Path)
    arguments = parser.parse_args()
    print(
        schema.render(
            compile_artifact(
                arguments.profile,
                arguments.projection,
                arguments.reader,
            )
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
