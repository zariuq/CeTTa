#!/usr/bin/env python3
"""Mechanically migrate the provisional typed v0 syntax into one-carrier v1.

The v0 evaluators never enforced their declared sorts.  This migration removes
those decorative annotations, preserves every term and Horn rule, and derives
only positive-arity operator declarations.  It is provenance tooling, not a
language definition or parser.
"""

from __future__ import annotations

from hashlib import sha256
from pathlib import Path
from typing import Sequence
import argparse

import gslt2parse_schema_v1 as schema


def _tag(value: schema.SExpr, expected: str, context: str) -> tuple[schema.SExpr, ...]:
    if not isinstance(value, tuple) or not value:
        raise schema.SchemaError(f"{context}: expected {expected} list")
    head = value[0]
    if not isinstance(head, schema.Symbol) or head.text != expected:
        raise schema.SchemaError(f"{context}: expected {expected}")
    return value


def source_digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def verify_source_digest(path: Path, expected: str) -> None:
    actual = source_digest(path)
    if actual != expected:
        raise schema.SchemaError(
            f"source digest mismatch: expected {expected}, got {actual}"
        )


def migrate(path: Path, *, name: str) -> schema.Presentation:
    forms = schema.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1:
        raise schema.SchemaError(f"{path}: expected exactly one v0 presentation")
    root = _tag(forms[0], "presentation", str(path))
    if len(root) < 2:
        raise schema.SchemaError(f"{path}: missing v0 presentation name")

    fields: dict[str, tuple[schema.SExpr, ...]] = {}
    for raw in root[2:]:
        field = raw if isinstance(raw, tuple) else ()
        if not field or not isinstance(field[0], schema.Symbol):
            raise schema.SchemaError(f"{path}: malformed v0 field")
        tag = field[0].text
        if tag not in {"types", "terms", "equations", "rewrites"}:
            raise schema.SchemaError(f"{path}: unknown v0 field {tag}")
        if tag in fields:
            raise schema.SchemaError(f"{path}: duplicate v0 field {tag}")
        fields[tag] = field
    missing = sorted({"types", "terms", "equations", "rewrites"} - fields.keys())
    if missing:
        raise schema.SchemaError(f"{path}: missing v0 fields: {', '.join(missing)}")
    if len(fields["equations"]) != 1:
        raise schema.SchemaError(f"{path}: v0 authored equations cannot migrate to v1")

    operators: list[schema.OperatorDecl] = []
    seen: set[tuple[str, int]] = set()
    for raw in fields["terms"][1:]:
        declaration = _tag(raw, "constructor", f"{path}: constructor")
        if len(declaration) < 3 or not isinstance(declaration[1], schema.Symbol):
            raise schema.SchemaError(f"{path}: malformed v0 constructor")
        arity = len(declaration) - 3
        if arity == 0:
            continue
        operator = schema.OperatorDecl(declaration[1].text, arity)
        key = (operator.name, operator.arity)
        if key in seen:
            raise schema.SchemaError(
                f"{path}: duplicate migrated operator {operator.name}/{operator.arity}"
            )
        seen.add(key)
        operators.append(operator)

    rules = tuple(schema.parse_rule(raw, path) for raw in fields["rewrites"][1:])
    return schema.Presentation(name, tuple(operators), rules, path)


def pretty_text(presentation: schema.Presentation) -> str:
    lines = [f"(gslt-presentation-v1 {presentation.name}", "  (signature"]
    for operator in sorted(
        presentation.operators, key=lambda item: (item.name, item.arity)
    ):
        lines.append(f"    (operator {operator.name} {operator.arity})")
    lines.append("  )")
    lines.append("  (equations)")
    lines.append("  (rewrites")
    for rule in sorted(presentation.rules, key=lambda item: item.name):
        form: schema.SExpr = (
            schema.Symbol("rule"),
            schema.Symbol(rule.name),
            (schema.Symbol("head"), rule.head),
            (schema.Symbol("body"), *rule.body),
        )
        lines.append("    " + schema.render(form))
    lines.append("  ))")
    return "\n".join(lines) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--expected-sha256", required=True)
    args = parser.parse_args(argv)
    try:
        verify_source_digest(args.source, args.expected_sha256)
        presentation = migrate(args.source, name=args.name)
    except (OSError, schema.SchemaError) as error:
        parser.exit(1, f"migration rejected: {error}\n")
    print(pretty_text(presentation), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
