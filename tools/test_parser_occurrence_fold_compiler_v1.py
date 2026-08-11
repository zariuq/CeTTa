#!/usr/bin/env python3
"""Gate the authored occurrence-fold GSLT and its normalized parser binding."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path

from gslt2parse_schema_v1 import SExpr, Symbol, parse_sexprs, render
from parser_pack_lr1_v1 import Production, parse_abi_stream
from parser_pack_transparent_inline_v1 import (
    InlineEntry,
    inline_transparent_substates,
)
from test_finite_horn_chart_v1 import COMMON, PRESENTATIONS, run_json
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_TOKEN_COUNT = 8
EXPECTED_TOKEN_DIGEST = (
    "62454c3cf6fff0394db5f979e35e7f73b4ff38e1b93b00cc83c07ab4c2b31159"
)
EXPECTED_SEMANTIC_TOKEN_COUNT = 6
EXPECTED_SEMANTIC_TOKEN_DIGEST = (
    "d9ec1ec0db744335cea456a4b27188d4c790f951694ec5a0922cbfb91f7170a2"
)
EXPECTED_PRODUCTION_COUNT = 10
EXPECTED_PRODUCTION_DIGEST = (
    "1beb7bb14721cd1aee57e6ee4171a5e3aaec905e4399ed9df597763fbb91ccb3"
)
EXPECTED_SHIFT_COUNT = 2
EXPECTED_SHIFT_DIGEST = (
    "fe458a7c7a23a06d86007906001daaa088073361fc3234a764511b2ec5f1f1d6"
)
EXPECTED_PLAN_DIGEST = (
    "7abaf03ab7a160c019196dbd272f199ad156af5fd204ca9555c4118dc22ae4cc"
)

EXPECTED_NODES = {
    "statement_axiom",
    "statement_block",
    "statement_const",
    "statement_disjoint",
    "statement_essential",
    "statement_float",
    "statement_include",
    "statement_theorem_compressed",
    "statement_theorem_normal",
    "statement_var",
}

EXPECTED_TAGS = {
    "compressed-token-lexeme",
    "include-token-lexeme",
    "kw-block-close-lexeme",
    "kw-block-open-lexeme",
    "label-token-lexeme",
    "proof-label-token-lexeme",
    "symbol-token-lexeme",
    "unknown-proof-lexeme",
}


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class FoldNode:
    node: Symbol
    operation: SExpr
    contract: SExpr


@dataclass(frozen=True, slots=True)
class FoldToken:
    tag: Symbol
    role: SExpr


@dataclass(frozen=True, slots=True)
class FoldSemanticToken:
    tag: Symbol
    role: SExpr
    node: Symbol
    production_label: SExpr


@dataclass(frozen=True, slots=True)
class FoldShift:
    role: SExpr
    operation: SExpr


def app(term: SExpr, name: str, arity: int) -> tuple[SExpr, ...] | None:
    if (
        isinstance(term, tuple)
        and len(term) == arity + 1
        and term[0] == Symbol(name)
    ):
        return term
    return None


def one_term(text: str, context: str) -> SExpr:
    terms = parse_sexprs(text, source=context)
    if len(terms) != 1:
        raise GateFailure(f"{context}: expected one term")
    return terms[0]


def compiler_arguments() -> list[str]:
    arguments = [str(PRESENTATIONS / relative) for relative in COMMON]
    arguments.extend(
        str(PRESENTATIONS / relative)
        for relative in (
            "compiler/regular_span_compiler_v1.metta",
            "core/occurrence_fold_core_v1.metta",
            "compiler/parser_occurrence_fold_compiler_v1.metta",
            "../../../langdef/metamath/source_fold_v1.metta",
        )
    )
    for relative in ABI_CASES["metamath"]["sources"]:
        arguments.extend(("--reflect-source", str(PRESENTATIONS / relative)))
    return arguments


def compiler_answers(binary: Path, query: str) -> dict:
    result = run_json(
        binary,
        [
            *compiler_arguments(),
            "--query-text",
            query,
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
        raise GateFailure("occurrence-fold compiler omitted proof evidence")
    return result


def parse_tokens(terms: list[str]) -> tuple[FoldToken, ...]:
    result: list[FoldToken] = []
    for index, text in enumerate(terms):
        term = app(one_term(text, f"token answer {index}"), "compile-occurrence-fold-token", 2)
        if term is None or not isinstance(term[1], Symbol):
            raise GateFailure("occurrence-fold token answer has malformed shape")
        result.append(FoldToken(term[1], term[2]))
    return tuple(result)


def parse_semantic_tokens(
    terms: list[str],
) -> tuple[FoldSemanticToken, ...]:
    result: list[FoldSemanticToken] = []
    for index, text in enumerate(terms):
        term = app(
            one_term(text, f"semantic-token answer {index}"),
            "compile-occurrence-fold-semantic-token",
            4,
        )
        if (
            term is None
            or not isinstance(term[1], Symbol)
            or not isinstance(term[3], Symbol)
        ):
            raise GateFailure(
                "occurrence-fold semantic-token answer has malformed shape"
            )
        result.append(
            FoldSemanticToken(term[1], term[2], term[3], term[4])
        )
    return tuple(result)


def parse_nodes(terms: list[str]) -> tuple[FoldNode, ...]:
    result: list[FoldNode] = []
    for index, text in enumerate(terms):
        term = app(
            one_term(text, f"production answer {index}"),
            "compile-occurrence-fold-production",
            8,
        )
        if term is None or not isinstance(term[4], Symbol):
            raise GateFailure(
                "occurrence-fold production answer has malformed shape"
            )
        action = root_node_action(term[7])
        if action != term[4]:
            raise GateFailure(
                "compiled occurrence-fold node differs from its action root"
            )
        result.append(FoldNode(term[4], term[5], term[6]))
    return tuple(result)


def parse_shifts(terms: list[str]) -> tuple[FoldShift, ...]:
    result: list[FoldShift] = []
    for index, text in enumerate(terms):
        term = app(
            one_term(text, f"shift answer {index}"),
            "compile-occurrence-fold-shift",
            2,
        )
        if term is None:
            raise GateFailure(
                "occurrence-fold shift answer has malformed shape"
            )
        result.append(FoldShift(term[1], term[2]))
    return tuple(result)


def action_list(term: SExpr) -> tuple[SExpr, ...]:
    values: list[SExpr] = []
    current = term
    while current != Symbol("pa-nil"):
        item = app(current, "pa-cons", 2)
        if item is None:
            raise GateFailure("semantic action has a malformed argument list")
        values.append(item[1])
        current = item[2]
    return tuple(values)


def root_node_action(action: SExpr) -> Symbol | None:
    apply = app(action, "pa-apply", 2)
    if apply is None or apply[1] != Symbol("node"):
        return None
    arguments = action_list(apply[2])
    if len(arguments) != 2:
        return None
    constant = app(arguments[0], "pa-const", 1)
    if constant is None or not isinstance(constant[1], Symbol):
        return None
    return constant[1]


def unique_rows(
    nodes: tuple[FoldNode, ...],
    tokens: tuple[FoldToken, ...],
    shifts: tuple[FoldShift, ...],
    semantic_tokens: tuple[FoldSemanticToken, ...],
) -> tuple[
    dict[Symbol, FoldNode],
    dict[Symbol, FoldToken],
    dict[SExpr, FoldShift],
    dict[Symbol, FoldSemanticToken],
]:
    node_rows: dict[Symbol, FoldNode] = {}
    token_rows: dict[Symbol, FoldToken] = {}
    shift_rows: dict[SExpr, FoldShift] = {}
    semantic_token_rows: dict[Symbol, FoldSemanticToken] = {}
    for row in nodes:
        if row.node in node_rows:
            raise GateFailure(f"duplicate occurrence-fold node {row.node.text}")
        node_rows[row.node] = row
    for row in tokens:
        if row.tag in token_rows:
            raise GateFailure(f"duplicate occurrence-fold token {row.tag.text}")
        token_rows[row.tag] = row
    for row in shifts:
        if row.role in shift_rows:
            raise GateFailure("duplicate occurrence-fold shift role")
        shift_rows[row.role] = row
    for row in semantic_tokens:
        if row.tag in semantic_token_rows:
            raise GateFailure(
                f"duplicate semantic token {row.tag.text}"
            )
        semantic_token_rows[row.tag] = row
    if {node.text for node in node_rows} != EXPECTED_NODES:
        raise GateFailure("occurrence-fold node inventory is incomplete")
    if {tag.text for tag in token_rows} != EXPECTED_TAGS:
        raise GateFailure("occurrence-fold token inventory is incomplete")
    token_roles = {row.role for row in token_rows.values()}
    if set(shift_rows) != {
        Symbol("mm-scope-open"),
        Symbol("mm-scope-close"),
    } or not set(shift_rows).issubset(token_roles):
        raise GateFailure("occurrence-fold shift inventory is incomplete")
    if set(semantic_token_rows) != {
        Symbol("compressed-token-lexeme"),
        Symbol("include-token-lexeme"),
        Symbol("label-token-lexeme"),
        Symbol("proof-label-token-lexeme"),
        Symbol("symbol-token-lexeme"),
        Symbol("unknown-proof-lexeme"),
    }:
        raise GateFailure(
            "occurrence-fold semantic-token inventory is incomplete"
        )
    for tag, row in semantic_token_rows.items():
        if tag not in token_rows or token_rows[tag].role != row.role:
            raise GateFailure(
                "semantic token differs from its authored token role"
            )
    return node_rows, token_rows, shift_rows, semantic_token_rows


def normalized_plan(
    entries: tuple[InlineEntry, ...],
    node_rows: dict[Symbol, FoldNode],
    token_rows: dict[Symbol, FoldToken],
    shift_rows: dict[SExpr, FoldShift],
    semantic_token_rows: dict[Symbol, FoldSemanticToken],
) -> tuple[bytes, int]:
    productions = tuple(entry.production for entry in entries)
    semantic_productions: dict[Symbol, Production] = {}
    for tag, row in semantic_token_rows.items():
        matches = tuple(
            entry.production
            for entry in entries
            if entry.source_trace.count(row.production_label) == 1
        )
        if (
            len(matches) != 1
            or root_node_action(matches[0].action) != row.node
        ):
            raise GateFailure(
                "semantic token does not have one exact normalized "
                "source-fiber production"
            )
        semantic_productions[tag] = matches[0]

    seen: dict[Symbol, Production] = {}
    lines: list[str] = []
    for production in productions:
        node = root_node_action(production.action)
        if node is None or node not in node_rows:
            continue
        if node in seen:
            raise GateFailure(
                f"normalized parser repeats fold node {node.text}"
            )
        seen[node] = production
        row = node_rows[node]
        lines.append(
            render(
                (
                    Symbol("occurrence-fold-production-v1"),
                    production.identity,
                    node,
                    row.operation,
                    row.contract,
                )
            )
        )
    if set(seen) != set(node_rows):
        raise GateFailure(
            "normalized parser does not realize every authored fold node"
        )
    for tag, row in token_rows.items():
        semantic = semantic_token_rows.get(tag)
        lines.append(
            render(
                (
                    (
                        Symbol("occurrence-fold-token-v3"),
                        tag,
                        row.role,
                        semantic.node,
                        semantic.production_label,
                        semantic_productions[tag].identity,
                    )
                    if semantic is not None
                    else (
                        Symbol("occurrence-fold-token-v1"),
                        tag,
                        row.role,
                    )
                )
            )
        )
    for role, row in shift_rows.items():
        lines.append(
            render(
                (
                    Symbol("occurrence-fold-shift-v1"),
                    role,
                    row.operation,
                )
            )
        )
    payload = ("\n".join(sorted(lines)) + "\n").encode("utf-8")
    return payload, len(seen)


def contract_gate(binary: Path) -> int:
    base = [
        str(PRESENTATIONS / "core/occurrence_fold_core_v1.metta"),
        str(ROOT / "langdef/metamath/source_fold_v1.metta"),
    ]
    cases = (
        (
            True,
            "(fold-contract-match "
            "(fold-seq (fold-one mm-label) "
            "(fold-seq (fold-plus mm-symbol) (fold-plus mm-proof-step))) "
            "(fold-cons mm-label "
            "(fold-cons mm-symbol "
            "(fold-cons mm-symbol "
            "(fold-cons mm-proof-step fold-nil)))) fold-nil)",
        ),
        (
            False,
            "(fold-contract-match "
            "(fold-seq (fold-one mm-label) "
            "(fold-seq (fold-plus mm-symbol) (fold-plus mm-proof-step))) "
            "(fold-cons mm-label "
            "(fold-cons mm-proof-step fold-nil)) fold-nil)",
        ),
        (
            True,
            "(fold-contract-match "
            "(fold-seq (fold-one mm-label) "
            "(fold-seq (fold-plus mm-symbol) "
            "(fold-seq (fold-star mm-proof-step) "
            "(fold-plus mm-compressed-word)))) "
            "(fold-cons mm-label "
            "(fold-cons mm-symbol "
            "(fold-cons mm-compressed-word fold-nil))) fold-nil)",
        ),
        (
            False,
            "(fold-contract-match "
            "(fold-seq (fold-one mm-symbol) (fold-plus mm-symbol)) "
            "(fold-cons mm-symbol fold-nil) fold-nil)",
        ),
    )
    passed = 0
    for expected, query in cases:
        result = run_json(
            binary,
            [*base, "--query-text", query, "--certs", "--timeout", "30"],
        )
        if (
            result.get("answers") != (1 if expected else 0)
            or result.get("outcome")
            != ("Unique" if expected else "NoAnswer")
        ):
            raise GateFailure("occurrence-fold contract verdict changed")
        passed += 1
    return passed


def mutation_gate(
    entries: tuple[InlineEntry, ...],
    nodes: tuple[FoldNode, ...],
    tokens: tuple[FoldToken, ...],
    shifts: tuple[FoldShift, ...],
    semantic_tokens: tuple[FoldSemanticToken, ...],
) -> int:
    killed = 0

    for (
        mutated_nodes,
        mutated_tokens,
        mutated_shifts,
        mutated_semantic_tokens,
    ) in (
        (nodes[:-1], tokens, shifts, semantic_tokens),
        (
            (
                *nodes,
                FoldNode(nodes[0].node, Symbol("foreign-operation"), nodes[0].contract),
            ),
            tokens,
            shifts,
            semantic_tokens,
        ),
        (
            nodes,
            (
                *tokens,
                FoldToken(tokens[0].tag, Symbol("foreign-role")),
            ),
            shifts,
            semantic_tokens,
        ),
        (
            (
                *nodes,
                FoldNode(
                    Symbol("foreign-node"),
                    Symbol("foreign-operation"),
                    Symbol("fold-none"),
                ),
            ),
            tokens,
            shifts,
            semantic_tokens,
        ),
        (
            nodes,
            tokens,
            (
                *shifts,
                FoldShift(shifts[0].role, Symbol("foreign-operation")),
            ),
            semantic_tokens,
        ),
        (
            nodes,
            tokens,
            shifts,
            semantic_tokens[:-1],
        ),
        (
            nodes,
            tokens,
            shifts,
            (
                FoldSemanticToken(
                    semantic_tokens[0].tag,
                    semantic_tokens[0].role,
                    semantic_tokens[0].node,
                    Symbol("foreign-source-production"),
                ),
                *semantic_tokens[1:],
            ),
        ),
    ):
        try:
            (
                node_rows,
                token_rows,
                shift_rows,
                semantic_token_rows,
            ) = unique_rows(
                tuple(mutated_nodes),
                tuple(mutated_tokens),
                tuple(mutated_shifts),
                tuple(mutated_semantic_tokens),
            )
            normalized_plan(
                entries,
                node_rows,
                token_rows,
                shift_rows,
                semantic_token_rows,
            )
        except GateFailure:
            killed += 1
        else:
            raise GateFailure("occurrence-fold plan mutation survived")
    return killed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--write-plan", type=Path)
    arguments = parser.parse_args()

    token_answers = compiler_answers(
        arguments.chart_binary,
        "(compile-occurrence-fold-token ?tag ?role)",
    )
    semantic_token_answers = compiler_answers(
        arguments.chart_binary,
        "(compile-occurrence-fold-semantic-token "
        "?tag ?role ?node ?label)",
    )
    production_answers = compiler_answers(
        arguments.chart_binary,
        "(compile-occurrence-fold-production "
        "?owner ?label ?arity ?node ?operation ?contract ?action ?code)",
    )
    shift_answers = compiler_answers(
        arguments.chart_binary,
        "(compile-occurrence-fold-shift ?role ?operation)",
    )
    if (
        token_answers.get("answers") != EXPECTED_TOKEN_COUNT
        or token_answers.get("term_digest") != EXPECTED_TOKEN_DIGEST
    ):
        raise GateFailure("occurrence-fold token compiler answer set changed")
    if (
        semantic_token_answers.get("answers")
        != EXPECTED_SEMANTIC_TOKEN_COUNT
        or semantic_token_answers.get("term_digest")
        != EXPECTED_SEMANTIC_TOKEN_DIGEST
    ):
        raise GateFailure(
            "occurrence-fold semantic-token compiler answer set changed"
        )
    if (
        production_answers.get("answers") != EXPECTED_PRODUCTION_COUNT
        or production_answers.get("term_digest")
        != EXPECTED_PRODUCTION_DIGEST
    ):
        raise GateFailure(
            "occurrence-fold production compiler answer set changed"
        )
    if (
        shift_answers.get("answers") != EXPECTED_SHIFT_COUNT
        or shift_answers.get("term_digest") != EXPECTED_SHIFT_DIGEST
    ):
        raise GateFailure(
            "occurrence-fold shift compiler answer set changed"
        )

    token_texts = token_answers["terms"]
    semantic_token_texts = semantic_token_answers["terms"]
    production_texts = production_answers["terms"]
    shift_texts = shift_answers["terms"]
    assert isinstance(token_texts, list)
    assert isinstance(semantic_token_texts, list)
    assert isinstance(production_texts, list)
    assert isinstance(shift_texts, list)
    tokens = parse_tokens(token_texts)
    semantic_tokens = parse_semantic_tokens(semantic_token_texts)
    nodes = parse_nodes(production_texts)
    shifts = parse_shifts(shift_texts)
    (
        node_rows,
        token_rows,
        shift_rows,
        semantic_token_rows,
    ) = unique_rows(
        nodes, tokens, shifts, semantic_tokens
    )

    source_stream = export_stream(
        arguments.petta_root, ABI_CASES["metamath"]
    )
    source = parse_abi_stream(source_stream)
    normalized = inline_transparent_substates(source)
    plan, production_len = normalized_plan(
        normalized.entries,
        node_rows,
        token_rows,
        shift_rows,
        semantic_token_rows,
    )
    plan_digest = sha256(plan).hexdigest()
    if plan_digest != EXPECTED_PLAN_DIGEST:
        raise GateFailure("normalized occurrence-fold plan changed")
    if arguments.write_plan is not None:
        arguments.write_plan.parent.mkdir(parents=True, exist_ok=True)
        arguments.write_plan.write_bytes(plan)

    contract_cases = contract_gate(arguments.chart_binary)
    mutations_killed = mutation_gate(
        normalized.entries,
        nodes,
        tokens,
        shifts,
        semantic_tokens,
    )

    print("parser-occurrence-fold-compiler-v1")
    print(f"token-answers\t{len(tokens)}")
    print(f"semantic-token-answers\t{len(semantic_tokens)}")
    print(f"production-answers\t{len(nodes)}")
    print(f"shift-answers\t{len(shifts)}")
    print(f"normalized-productions\t{production_len}")
    print(f"contract-cases\t{contract_cases}")
    print(f"mutations-killed\t{mutations_killed}")
    print(f"plan-digest\t{plan_digest}")
    print("end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
