#!/usr/bin/env python3
"""Compile MatchDecisionPolicyV1 into the finite table consumed by C."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse

import gslt2parse_schema_v1 as sx


class GenerationError(RuntimeError):
    pass


OBSERVATIONS = ("unknown", "absent", "literal", "expression")
KEYS = ("wildcard", "literal", "expression-arity", "expression-head")
RELATIONS = ("different", "equal")
OUTCOMES = ("fallback", "keep", "refute")


@dataclass(frozen=True, slots=True)
class PolicyRule:
    name: str
    observation: sx.SExpr
    key: sx.SExpr
    arity: sx.SExpr
    identity: sx.SExpr
    outcome: str


def symbol(term: sx.SExpr, context: str) -> str:
    if not isinstance(term, sx.Symbol):
        raise GenerationError(
            f"{context}: expected symbol, got {sx.render(term)}"
        )
    return term.text


def application(term: sx.SExpr, head: str, arity: int,
                context: str) -> tuple[sx.SExpr, ...]:
    if (
        not isinstance(term, tuple)
        or len(term) != arity + 1
        or not isinstance(term[0], sx.Symbol)
        or term[0].text != head
    ):
        raise GenerationError(
            f"{context}: expected {head}/{arity}, got {sx.render(term)}"
        )
    return term


def parse_rule(rule: sx.RuleDecl) -> PolicyRule:
    if rule.body:
        raise GenerationError(f"{rule.name}: policy facts may not have bodies")
    head = application(rule.head, "candidate-policy", 5, rule.name)
    outcome = symbol(head[5], f"{rule.name}.outcome")
    if outcome not in OUTCOMES:
        raise GenerationError(f"{rule.name}: unknown outcome {outcome}")
    return PolicyRule(
        rule.name, head[1], head[2], head[3], head[4], outcome
    )


def matches(pattern: sx.SExpr, value: str) -> bool:
    if isinstance(pattern, sx.Variable):
        return True
    return isinstance(pattern, sx.Symbol) and pattern.text == value


def exact_key_plan_outcome(observation: str, key: str,
                           arity: str, identity: str) -> str:
    """The finite policy shape executable by direct key lookup.

    This is an optimization certificate, not a second semantic source: the
    authored rules are first elaborated into ``table`` and receive the
    certificate only when every cell has this exact conservative shape.
    Changed policies keep compiling, but the runtime then uses its generic
    table interpreter.
    """
    if observation == "unknown":
        return "fallback"
    if key == "wildcard":
        return "keep"
    if observation == "absent":
        return "refute"
    if observation == "literal":
        return (
            "keep"
            if key == "literal" and identity == "equal"
            else "refute"
        )
    if observation == "expression":
        if key == "expression-arity" and arity == "equal":
            return "keep"
        if (
            key == "expression-head"
            and arity == "equal"
            and identity == "equal"
        ):
            return "keep"
        return "refute"
    raise AssertionError(f"unknown observation {observation}")


def supports_exact_key_plan(
        table: list[list[list[list[str]]]]) -> bool:
    return all(
        table[observation_index][key_index][arity_index][identity_index]
        == exact_key_plan_outcome(observation, key, arity, identity)
        for observation_index, observation in enumerate(OBSERVATIONS)
        for key_index, key in enumerate(KEYS)
        for arity_index, arity in enumerate(RELATIONS)
        for identity_index, identity in enumerate(RELATIONS)
    )


def render_header(digest: str, table: list[list[list[list[str]]]]) -> str:
    outcome_values = {"fallback": 0, "keep": 1, "refute": 2}
    exact_key_plan = 1 if supports_exact_key_plan(table) else 0
    lines = [
        "/* Generated from MatchDecisionPolicyV1; do not edit. */",
        "#ifndef CETTA_MATCH_DECISION_POLICY_V1_GENERATED_H",
        "#define CETTA_MATCH_DECISION_POLICY_V1_GENERATED_H",
        "",
        f'#define CETTA_MATCH_DECISION_POLICY_GSLT_DIGEST "{digest}"',
        f'#define CETTA_MATCH_DECISION_POLICY_GSLT_IDENTITY "MatchDecisionPolicyV1-{digest}"',
        f"#define CETTA_MATCH_DECISION_POLICY_ID UINT64_C(0x{digest[:16]})",
        f"#define CETTA_MATCH_DECISION_POLICY_EXACT_KEY_PLAN_V1 {exact_key_plan}",
        "",
        "enum {",
        "    CETTA_MD_POLICY_FALLBACK = 0,",
        "    CETTA_MD_POLICY_KEEP = 1,",
        "    CETTA_MD_POLICY_REFUTE = 2,",
        "};",
        "",
        "static const unsigned char cetta_md_policy_v1[4][4][2][2] = {",
    ]
    for observation_index, observation in enumerate(OBSERVATIONS):
        lines.append(f"    /* {observation} */ {{")
        for key_index, key in enumerate(KEYS):
            lines.append(f"        /* {key} */ {{")
            for arity_index, arity in enumerate(RELATIONS):
                values = ", ".join(
                    str(outcome_values[
                        table[observation_index][key_index]
                             [arity_index][identity_index]
                    ])
                    for identity_index, _ in enumerate(RELATIONS)
                )
                lines.append(f"            {{{values}}}, /* arity {arity} */")
            lines.append("        },")
        lines.append("    },")
    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def generate(policy_path: Path) -> str:
    presentations = sx.admit([policy_path])
    if len(presentations) != 1 or presentations[0].name != "MatchDecisionPolicyV1":
        raise GenerationError("expected exactly MatchDecisionPolicyV1")
    rules = [parse_rule(rule) for rule in presentations[0].rules]
    if not rules:
        raise GenerationError("policy has no semantic rules")

    table: list[list[list[list[str]]]] = []
    for observation in OBSERVATIONS:
        observation_rows: list[list[list[str]]] = []
        for key in KEYS:
            key_rows: list[list[str]] = []
            for arity in RELATIONS:
                arity_row: list[str] = []
                for identity in RELATIONS:
                    matching = [
                        rule for rule in rules
                        if matches(rule.observation, observation)
                        and matches(rule.key, key)
                        and matches(rule.arity, arity)
                        and matches(rule.identity, identity)
                    ]
                    if len(matching) != 1:
                        names = ", ".join(rule.name for rule in matching)
                        raise GenerationError(
                            "policy must be total and disjoint at "
                            f"({observation}, {key}, {arity}, {identity}); "
                            f"matched {len(matching)} rules: {names}"
                        )
                    arity_row.append(matching[0].outcome)
                key_rows.append(arity_row)
            observation_rows.append(key_rows)
        table.append(observation_rows)
    return render_header(sx.package_digest(presentations), table)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        rendered = generate(arguments.policy)
    except (OSError, sx.SchemaError, GenerationError) as error:
        parser.exit(1, f"MatchDecisionPolicyGenerationError: {error}\n")
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
