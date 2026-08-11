#!/usr/bin/env python3
"""Gate the explicit occurrence-fold to indexed-checker composition."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path

from gslt2parse_schema_v1 import SExpr, Symbol, parse_sexprs, render
from test_finite_horn_chart_v1 import PRESENTATIONS, run_json


EXPECTED_EFFECTS = {
    "checker-check-compressed",
    "checker-check-normal",
    "checker-close-scope",
    "checker-declare-constants",
    "checker-declare-distinct",
    "checker-declare-essential",
    "checker-declare-floating",
    "checker-declare-rule",
    "checker-declare-variables",
    "checker-complete-scope",
    "checker-open-scope",
    "checker-resolve-source",
}

EXPECTED_EFFECT_DIGEST = (
    "b7421e1428abc56e67240ef5bcdcb65568c0cb82be090241d86a45ed04cabc7e"
)
EXPECTED_COMPILED_DIGEST = (
    "59e6e45cda6668ac4d4e16ae3df53e3c95462bead05420bb4783a51088507a30"
)
EXPECTED_POLICY_DIGEST = (
    "359b85f1308e905c08cda5430161330f614b00b32ea5c03dc824fdfb830551da"
)
EXPECTED_PLAN_DIGEST = (
    "bb5764ea8a1ed4669ddc3ae2135ffbdc400e293ce5fe52f1a03fb1fe5597aae4"
)

EXPECTED_MAPPING = {
    "mm-check-theorem-compressed": "checker-check-compressed",
    "mm-check-theorem-normal": "checker-check-normal",
    "mm-close-scope": "checker-close-scope",
    "mm-declare-axiom": "checker-declare-rule",
    "mm-declare-constants": "checker-declare-constants",
    "mm-declare-disjoint": "checker-declare-distinct",
    "mm-declare-essential": "checker-declare-essential",
    "mm-declare-floating": "checker-declare-floating",
    "mm-declare-variables": "checker-declare-variables",
    "mm-complete-block": "checker-complete-scope",
    "mm-open-scope": "checker-open-scope",
    "mm-resolve-include": "checker-resolve-source",
}

EXPECTED_POLICIES = {
    "checker-allow-duplicate-floating": "policy-false",
    "checker-allow-inner-constants": "policy-false",
    "checker-allow-toplevel-essential": "policy-true",
    "checker-reject-unknown-steps": "policy-false",
    "checker-reject-active-source-cycles": "policy-true",
    "checker-skip-completed-sources": "policy-true",
}


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class EffectRow:
    operation: Symbol
    effect: Symbol


@dataclass(frozen=True, slots=True)
class PolicyRow:
    key: Symbol
    value: Symbol


def one_term(text: str, context: str) -> SExpr:
    terms = parse_sexprs(text, source=context)
    if len(terms) != 1:
        raise GateFailure(f"{context}: expected one term")
    return terms[0]


def application(
    term: SExpr, head: str, arity: int
) -> tuple[SExpr, ...] | None:
    if (
        isinstance(term, tuple)
        and len(term) == arity + 1
        and term[0] == Symbol(head)
    ):
        return term
    return None


def source_arguments() -> list[str]:
    return [
        str(PRESENTATIONS / "core/occurrence_fold_core_v1.metta"),
        str(ROOT / "langdef/metamath/source_fold_v1.metta"),
    ]


def compiler_arguments() -> list[str]:
    return [
        *source_arguments(),
        str(PRESENTATIONS / "core/indexed_checker_fold_core_v1.metta"),
        str(PRESENTATIONS / "compiler/indexed_checker_fold_compiler_v1.metta"),
        str(PRESENTATIONS / "languages/metamath_source_checker_v1.metta"),
    ]


def query(binary: Path, arguments: list[str], text: str) -> dict:
    result = run_json(
        binary,
        [
            *arguments,
            "--query-text",
            text,
            "--certs",
            "--timeout",
            "30",
        ],
    )
    terms = result.get("terms")
    certificates = result.get("certs")
    if (
        not isinstance(terms, list)
        or not isinstance(certificates, list)
        or len(terms) != len(certificates)
        or any(not certificate for certificate in certificates)
    ):
        raise GateFailure("indexed-checker compiler omitted proof evidence")
    return result


def source_operations(binary: Path) -> set[str]:
    nodes = query(
        binary,
        source_arguments(),
        "(source-fold-node ?node ?operation ?contract)",
    )
    shifts = query(
        binary,
        source_arguments(),
        "(source-fold-shift ?role ?operation)",
    )
    operations: set[str] = set()
    for index, text in enumerate(nodes["terms"]):
        term = application(
            one_term(text, f"node source {index}"),
            "source-fold-node",
            3,
        )
        if term is None or not isinstance(term[2], Symbol):
            raise GateFailure("source fold emitted a malformed node")
        operations.add(term[2].text)
    for index, text in enumerate(shifts["terms"]):
        term = application(
            one_term(text, f"shift source {index}"),
            "source-fold-shift",
            2,
        )
        if term is None or not isinstance(term[2], Symbol):
            raise GateFailure("source fold emitted a malformed shift")
        operations.add(term[2].text)
    if len(operations) != len(nodes["terms"]) + len(shifts["terms"]):
        raise GateFailure("source operation identities are not unique")
    return operations


def parse_effect_rows(terms: list[str]) -> tuple[EffectRow, ...]:
    rows: list[EffectRow] = []
    for index, text in enumerate(terms):
        term = application(
            one_term(text, f"effect answer {index}"),
            "compile-indexed-checker-effect",
            2,
        )
        if (
            term is None
            or not isinstance(term[1], Symbol)
            or not isinstance(term[2], Symbol)
        ):
            raise GateFailure("indexed-checker effect answer is malformed")
        rows.append(EffectRow(term[1], term[2]))
    return tuple(rows)


def parse_policy_rows(terms: list[str]) -> tuple[PolicyRow, ...]:
    rows: list[PolicyRow] = []
    for index, text in enumerate(terms):
        term = application(
            one_term(text, f"policy answer {index}"),
            "compile-indexed-checker-policy",
            2,
        )
        if (
            term is None
            or not isinstance(term[1], Symbol)
            or not isinstance(term[2], Symbol)
        ):
            raise GateFailure("indexed-checker policy answer is malformed")
        rows.append(PolicyRow(term[1], term[2]))
    return tuple(rows)


def validate_rows(
    rows: tuple[EffectRow, ...],
    operations: set[str],
) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for row in rows:
        if row.operation.text in mapping:
            raise GateFailure("one source operation selected multiple effects")
        if row.operation.text not in operations:
            raise GateFailure("effect row has no source occurrence")
        if row.effect.text not in EXPECTED_EFFECTS:
            raise GateFailure("effect row escaped the indexed-checker algebra")
        mapping[row.operation.text] = row.effect.text
    if set(mapping) != operations:
        raise GateFailure("not every source operation selected an effect")
    if mapping != EXPECTED_MAPPING:
        raise GateFailure("source operation/effect composition changed")
    return mapping


def validate_policies(rows: tuple[PolicyRow, ...]) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for row in rows:
        if row.key.text in mapping:
            raise GateFailure("one checker policy has multiple values")
        if row.key.text not in EXPECTED_POLICIES:
            raise GateFailure("checker policy escaped the generic algebra")
        if row.value.text not in {"policy-false", "policy-true"}:
            raise GateFailure("checker policy has a non-Boolean value")
        mapping[row.key.text] = row.value.text
    if mapping != EXPECTED_POLICIES:
        raise GateFailure("source-authored checker policy changed")
    return mapping


def normalized_plan(
    rows: tuple[EffectRow, ...],
    policies: tuple[PolicyRow, ...],
) -> bytes:
    lines = [
        render(
            (
                Symbol("indexed-checker-effect-v1"),
                row.operation,
                row.effect,
            )
        )
        for row in rows
    ]
    lines.extend(
        render(
            (
                Symbol("indexed-checker-policy-v1"),
                row.key,
                row.value,
            )
        )
        for row in policies
    )
    lines.sort()
    return ("\n".join(lines) + "\n").encode("utf-8")


def mutation_gate(
    rows: tuple[EffectRow, ...],
    policies: tuple[PolicyRow, ...],
    operations: set[str],
) -> int:
    killed = 0
    first = rows[0]
    second = rows[1]
    mutations = (
        rows[:-1],
        (*rows, EffectRow(first.operation, second.effect)),
        (*rows, EffectRow(Symbol("foreign-operation"), first.effect)),
        (
            EffectRow(first.operation, Symbol("foreign-effect")),
            *rows[1:],
        ),
        (
            EffectRow(first.operation, second.effect),
            EffectRow(second.operation, first.effect),
            *rows[2:],
        ),
    )
    for mutation in mutations:
        try:
            validate_rows(tuple(mutation), operations)
        except GateFailure:
            killed += 1
        else:
            raise GateFailure("indexed-checker composition mutation survived")
    policy_first = policies[0]
    policy_mutations = (
        policies[:-1],
        (*policies, policy_first),
        (
            PolicyRow(Symbol("foreign-policy"), policy_first.value),
            *policies[1:],
        ),
        (
            PolicyRow(policy_first.key, Symbol("foreign-value")),
            *policies[1:],
        ),
        (
            PolicyRow(
                policy_first.key,
                Symbol(
                    "policy-true"
                    if policy_first.value.text == "policy-false"
                    else "policy-false"
                ),
            ),
            *policies[1:],
        ),
    )
    for mutation in policy_mutations:
        try:
            validate_policies(tuple(mutation))
        except GateFailure:
            killed += 1
        else:
            raise GateFailure("indexed-checker policy mutation survived")
    return killed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--write-plan", type=Path)
    arguments = parser.parse_args()

    operations = source_operations(arguments.chart_binary)
    effect_inventory = query(
        arguments.chart_binary,
        compiler_arguments(),
        "(indexed-checker-effect ?effect)",
    )
    inventory = {
        term[1].text
        for index, text in enumerate(effect_inventory["terms"])
        if (
            (term := application(
                one_term(text, f"effect inventory {index}"),
                "indexed-checker-effect",
                1,
            ))
            is not None
            and isinstance(term[1], Symbol)
        )
    }
    if inventory != EXPECTED_EFFECTS:
        raise GateFailure("indexed-checker effect inventory changed")
    if (
        effect_inventory.get("answers") != len(EXPECTED_EFFECTS)
        or effect_inventory.get("term_digest") != EXPECTED_EFFECT_DIGEST
    ):
        raise GateFailure("indexed-checker effect proof set changed")

    compiled = query(
        arguments.chart_binary,
        compiler_arguments(),
        "(compile-indexed-checker-effect ?operation ?effect)",
    )
    rows = parse_effect_rows(compiled["terms"])
    if (
        compiled.get("answers") != len(EXPECTED_MAPPING)
        or compiled.get("term_digest") != EXPECTED_COMPILED_DIGEST
    ):
        raise GateFailure("indexed-checker composition proof set changed")
    validate_rows(rows, operations)
    compiled_policies = query(
        arguments.chart_binary,
        compiler_arguments(),
        "(compile-indexed-checker-policy ?key ?value)",
    )
    policies = parse_policy_rows(compiled_policies["terms"])
    if (
        compiled_policies.get("answers") != len(EXPECTED_POLICIES)
        or (
            EXPECTED_POLICY_DIGEST
            and compiled_policies.get("term_digest")
            != EXPECTED_POLICY_DIGEST
        )
    ):
        raise GateFailure("indexed-checker policy proof set changed")
    validate_policies(policies)
    payload = normalized_plan(rows, policies)
    digest = sha256(payload).hexdigest()
    if digest != EXPECTED_PLAN_DIGEST:
        raise GateFailure("indexed-checker normalized plan changed")
    if arguments.write_plan is not None:
        arguments.write_plan.parent.mkdir(parents=True, exist_ok=True)
        arguments.write_plan.write_bytes(payload)
    killed = mutation_gate(rows, policies, operations)

    print("indexed-checker-fold-compiler-v1")
    print(f"source-operations\t{len(operations)}")
    print(f"checker-effects\t{len(inventory)}")
    print(f"compiled-effects\t{len(rows)}")
    print(f"compiled-policies\t{len(policies)}")
    print(f"policy-digest\t{compiled_policies.get('term_digest')}")
    print(f"mutations-killed\t{killed}")
    print(f"plan-digest\t{digest}")
    print("end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
