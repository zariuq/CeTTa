#!/usr/bin/env python3
"""Generate a finite-Horn NIK replay specialization and C authority catalog."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
import re

import gslt2parse_schema_v1 as sx


class GenerationError(RuntimeError):
    pass


@dataclass(frozen=True)
class Formal:
    name: str
    depth: int


@dataclass(frozen=True)
class Rule:
    identifier: str
    formals: tuple[Formal, ...]
    premises: tuple[sx.SExpr, ...]
    conclusion: sx.SExpr


@dataclass(frozen=True)
class Authority:
    alias: str
    system_id: str
    revision: str
    digest: str
    presentation: sx.SExpr
    rules: tuple[Rule, ...]
    positive_goal: sx.SExpr
    positive_proof: sx.SExpr


def symbol(value: sx.SExpr, context: str) -> str:
    if not isinstance(value, sx.Symbol) or not value.text:
        raise GenerationError(f"{context}: expected a nonempty symbol")
    return value.text


def text(value: sx.SExpr, context: str) -> str:
    if isinstance(value, (sx.Symbol, sx.StringLiteral)) and value.text:
        return value.text
    raise GenerationError(f"{context}: expected nonempty text")


def form(value: sx.SExpr, context: str) -> tuple[sx.SExpr, ...]:
    if not isinstance(value, tuple) or not value:
        raise GenerationError(f"{context}: expected a nonempty form")
    return value


def tagged(value: sx.SExpr, tag: str, arity: int, context: str) -> tuple[sx.SExpr, ...]:
    items = form(value, context)
    if len(items) != arity + 1 or symbol(items[0], context) != tag:
        raise GenerationError(
            f"{context}: expected ({tag} ...) with arity {arity}"
        )
    return items


def wire_list(value: sx.SExpr, nil: str, cons: str, context: str) -> tuple[sx.SExpr, ...]:
    result: list[sx.SExpr] = []
    cursor = value
    while not (isinstance(cursor, sx.Symbol) and cursor.text == nil):
        cell = tagged(cursor, cons, 2, context)
        result.append(cell[1])
        cursor = cell[2]
    return tuple(result)


def make_wire_list(values: tuple[sx.SExpr, ...], nil: str, cons: str) -> sx.SExpr:
    result: sx.SExpr = sx.Symbol(nil)
    for value in reversed(values):
        result = (sx.Symbol(cons), value, result)
    return result


def parse_rule(value: sx.SExpr, context: str) -> Rule:
    items = tagged(value, "GRule", 4, context)
    identifier = text(items[1], f"{context} identifier")
    raw_formals = wire_list(items[2], "LNil", "LCons", f"{context} formals")
    formals: list[Formal] = []
    seen_names: set[str] = set()
    for index, raw in enumerate(raw_formals):
        entry = tagged(raw, "Formal", 2, f"{context} formal {index}")
        name = text(entry[1], f"{context} formal {index} name")
        if not isinstance(entry[2], int) or entry[2] < 0:
            raise GenerationError(f"{context} formal {index}: invalid depth")
        if name in seen_names:
            raise GenerationError(
                f"{context}: duplicate formal name {name!r}"
            )
        seen_names.add(name)
        formals.append(Formal(name, entry[2]))
    premises = wire_list(
        items[3], "LNil", "LCons", f"{context} premises"
    )
    return Rule(identifier, tuple(formals), premises, items[4])


def parse_authority(value: sx.SExpr, index: int) -> Authority:
    context = f"authority {index}"
    items = tagged(value, "authority", 6, context)
    alias = symbol(items[1], f"{context} alias")
    system_id = text(items[2], f"{context} system")
    revision = text(items[3], f"{context} revision")
    digest = text(items[4], f"{context} digest")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise GenerationError(f"{context}: digest is not lowercase SHA-256")
    presentation = tagged(items[5], "GPresentation", 3, f"{context} presentation")
    actual_digest = sha256(sx.render(items[5]).encode("utf-8")).hexdigest()
    if digest != actual_digest:
        raise GenerationError(
            f"{context}: digest does not identify the rendered presentation"
        )
    raw_rules = wire_list(
        presentation[3], "LNil", "LCons", f"{context} rules"
    )
    rules = tuple(
        parse_rule(raw, f"{context} rule {rule_index}")
        for rule_index, raw in enumerate(raw_rules)
    )
    positive = tagged(items[6], "positive", 2, f"{context} positive")
    return Authority(
        alias,
        system_id,
        revision,
        digest,
        items[5],
        rules,
        positive[1],
        positive[2],
    )


def parse_catalog(path: Path) -> tuple[Authority, ...]:
    forms = sx.parse_sexprs(path.read_text(encoding="utf-8"), source=str(path))
    if len(forms) != 1:
        raise GenerationError(f"{path}: expected one catalog form")
    root = form(forms[0], f"{path}: catalog")
    if symbol(root[0], f"{path}: catalog tag") != "nik-authority-catalog-v1":
        raise GenerationError(f"{path}: wrong catalog tag")
    authorities = tuple(
        parse_authority(raw, index)
        for index, raw in enumerate(root[1:])
    )
    if len(authorities) < 2:
        raise GenerationError(f"{path}: plural authority catalog required")
    aliases = [authority.alias for authority in authorities]
    identities = [
        (authority.system_id, authority.revision, authority.digest)
        for authority in authorities
    ]
    if len(set(aliases)) != len(aliases):
        raise GenerationError(f"{path}: duplicate authority alias")
    if len(set(identities)) != len(identities):
        raise GenerationError(f"{path}: duplicate authority identity")
    return authorities


def authority_term(authority: Authority) -> sx.SExpr:
    return (
        sx.Symbol("NIKAuthorityV1"),
        sx.Symbol(authority.alias),
        sx.StringLiteral(authority.system_id),
        sx.StringLiteral(authority.revision),
        sx.StringLiteral(authority.digest),
    )


def instantiate_pattern(
    value: sx.SExpr,
    formals: tuple[Formal, ...],
    variables: tuple[sx.Variable, ...],
    depth: int = 0,
) -> sx.SExpr:
    items = form(value, "rule pattern")
    tag = symbol(items[0], "rule pattern tag")
    if tag == "FVar" and len(items) == 2:
        name = text(items[1], "schema metavariable")
        for index, formal in enumerate(formals):
            if formal.name == name and formal.depth == depth:
                return variables[index]
        raise GenerationError(
            f"unbound schema metavariable {name!r} at depth {depth}"
        )
    if tag == "Var" and len(items) == 2:
        return value
    if tag == "PApp" and len(items) == 3:
        arguments = wire_list(items[2], "LNil", "LCons", "PApp arguments")
        rendered = tuple(
            instantiate_pattern(argument, formals, variables, depth)
            for argument in arguments
        )
        return (items[0], items[1], make_wire_list(rendered, "LNil", "LCons"))
    if tag == "PLam" and len(items) == 3:
        return (
            items[0],
            items[1],
            instantiate_pattern(items[2], formals, variables, depth + 1),
        )
    if tag == "PMultiLam" and len(items) == 4:
        if not isinstance(items[1], int) or items[1] < 0:
            raise GenerationError("PMultiLam has invalid arity")
        return (
            items[0],
            items[1],
            items[2],
            instantiate_pattern(
                items[3], formals, variables, depth + items[1]
            ),
        )
    if tag == "PSubst" and len(items) == 3:
        return (
            items[0],
            instantiate_pattern(items[1], formals, variables, depth + 1),
            instantiate_pattern(items[2], formals, variables, depth),
        )
    if tag == "PCollection" and len(items) == 4:
        elements = wire_list(
            items[2], "LNil", "LCons", "PCollection elements"
        )
        rendered = tuple(
            instantiate_pattern(element, formals, variables, depth)
            for element in elements
        )
        return (
            items[0],
            items[1],
            make_wire_list(rendered, "LNil", "LCons"),
            items[3],
        )
    raise GenerationError(f"unsupported serialized Pattern form: {sx.render(value)}")


@dataclass(frozen=True)
class HornRule:
    name: str
    head: sx.SExpr
    body: tuple[sx.SExpr, ...]


def specialized_rules(authorities: tuple[Authority, ...]) -> tuple[HornRule, ...]:
    output: list[HornRule] = []
    for authority_index, authority in enumerate(authorities):
        identity = authority_term(authority)
        output.append(
            HornRule(
                f"nik-authority-{authority_index}",
                (sx.Symbol("nik-authority"), identity),
                (),
            )
        )
        for rule_index, rule in enumerate(authority.rules):
            arguments = tuple(
                sx.Variable(f"a{authority_index}_{rule_index}_{index}")
                for index in range(len(rule.formals))
            )
            children = tuple(
                sx.Variable(f"p{authority_index}_{rule_index}_{index}")
                for index in range(len(rule.premises))
            )
            conclusion = instantiate_pattern(
                rule.conclusion, rule.formals, arguments
            )
            premises = tuple(
                instantiate_pattern(premise, rule.formals, arguments)
                for premise in rule.premises
            )
            instance = (
                sx.Symbol("GRuleInst"),
                sx.StringLiteral(rule.identifier),
                make_wire_list(arguments, "LNil", "LCons"),
            )
            proof = (
                sx.Symbol("GProof"),
                instance,
                make_wire_list(children, "PrNil", "PrCons"),
            )
            head = (sx.Symbol("nik-check"), identity, conclusion, proof)
            body = tuple(
                (sx.Symbol("nik-check"), identity, premise, child)
                for premise, child in zip(premises, children, strict=True)
            )
            output.append(
                HornRule(
                    f"nik-rule-{authority_index}-{rule_index}", head, body
                )
            )
    return tuple(output)


def collect_operators(value: sx.SExpr, output: set[tuple[str, int]]) -> None:
    if not isinstance(value, tuple):
        return
    if not value:
        raise GenerationError("empty applications are outside finite-Horn v1")
    head = symbol(value[0], "generated application head")
    output.add((head, len(value) - 1))
    for child in value[1:]:
        collect_operators(child, output)


def render_semantics(authorities: tuple[Authority, ...]) -> str:
    rules = specialized_rules(authorities)
    operators: set[tuple[str, int]] = set()
    for rule in rules:
        collect_operators(rule.head, operators)
        for goal in rule.body:
            collect_operators(goal, operators)
    lines = [
        "; Generated from the Lean-emitted Prime NIK authority catalog.",
        "; Regenerate; do not edit this specialization by hand.",
        "(gslt-presentation-v1 PrimeNikAuthorityRuntimeV1",
        "  (signature",
    ]
    for name, arity in sorted(operators):
        if arity <= 0:
            raise GenerationError(f"operator {name} has nonpositive arity")
        lines.append(f"    (operator {name} {arity})")
    lines.extend(["  )", "  (equations)", "  (rewrites"])
    for rule in rules:
        lines.append(f"    (rule {rule.name}")
        lines.append(f"      (head {sx.render(rule.head)})")
        if rule.body:
            lines.append("      (body")
            lines.extend(f"        {sx.render(goal)}" for goal in rule.body)
            lines.append("      ))")
        else:
            lines.append("      (body))")
    lines.extend(["  ))", ""])
    return "\n".join(lines)


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def render_header(symbol_name: str) -> str:
    upper = re.sub(r"[^A-Za-z0-9]", "_", symbol_name).upper()
    return f"""#ifndef {upper}_GENERATED_H
#define {upper}_GENERATED_H

#include <stddef.h>

typedef struct {{
    const char *alias;
    const char *system_id;
    const char *revision;
    const char *digest;
    const char *presentation_metta;
    const char *positive_goal_metta;
    const char *positive_proof_metta;
}} CettaNikAuthorityV1;

extern const CettaNikAuthorityV1 {symbol_name}[];
extern const size_t {symbol_name}_count;
extern const char {symbol_name}_catalog_sha256[];

#endif /* {upper}_GENERATED_H */
"""


def render_registry_source(
    authorities: tuple[Authority, ...],
    symbol_name: str,
    header_include: str,
    catalog_digest: str,
) -> str:
    rows = []
    for authority in authorities:
        rows.append(
            "    {\n"
            f"        .alias = {c_string(authority.alias)},\n"
            f"        .system_id = {c_string(authority.system_id)},\n"
            f"        .revision = {c_string(authority.revision)},\n"
            f"        .digest = {c_string(authority.digest)},\n"
            f"        .presentation_metta = {c_string(sx.render(authority.presentation))},\n"
            f"        .positive_goal_metta = {c_string(sx.render(authority.positive_goal))},\n"
            f"        .positive_proof_metta = {c_string(sx.render(authority.positive_proof))},\n"
            "    },"
        )
    joined = "\n".join(rows)
    return f"""#include {c_string(header_include)}

const CettaNikAuthorityV1 {symbol_name}[] = {{
{joined}
}};

const size_t {symbol_name}_count =
    sizeof({symbol_name}) / sizeof({symbol_name}[0]);

const char {symbol_name}_catalog_sha256[] = {c_string(catalog_digest)};
"""


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--semantic", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", arguments.symbol):
        raise GenerationError("registry symbol is not a C identifier")
    authorities = parse_catalog(arguments.catalog)
    catalog_digest = sha256(arguments.catalog.read_bytes()).hexdigest()
    write_if_changed(arguments.semantic, render_semantics(authorities))
    write_if_changed(arguments.header, render_header(arguments.symbol))
    write_if_changed(
        arguments.source,
        render_registry_source(
            authorities,
            arguments.symbol,
            arguments.header_include,
            catalog_digest,
        ),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
