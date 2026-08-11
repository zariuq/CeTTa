#!/usr/bin/env python3
"""Adversarial gate for proof-relevant ParserPack transparent inlining."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
from pathlib import Path
import subprocess
import sys
import tempfile

from gslt2parse_schema_v1 import SExpr, Symbol, parse_sexprs, render
from parser_pack_lr1_v1 import (
    Grammar,
    GrammarSymbol,
    LR1Summary,
    Production,
    analyze_lr1,
    composite_grammar,
    parse_abi_stream,
    parse_plan_stream,
    summary_json,
)
from parser_pack_transparent_inline_v1 import (
    InlineFailure,
    SLRSummary,
    analyze_slr,
    emit_normalized_abi,
    emit_static_action_answers,
    inline_transparent_substates,
    qindex,
    qterm,
    slr_summary_json,
    verify_normalized_abi_evidence,
    verify_reproducible_normalization,
)
from test_parser_pack_abi_v1 import CASES as ABI_CASES
from test_parser_pack_abi_v1 import export_stream
from test_indexed_checker_fold_compiler_v1 import (
    EXPECTED_PLAN_DIGEST as INDEXED_CHECKER_EFFECT_PLAN_DIGEST,
)
from test_regular_span_dfa_v1 import (
    ClassClause,
    Edge,
    abi_scalar,
    answer_digest,
    c_compiler_answers,
    class_clauses,
    class_matches,
    closure,
    codepoint,
    parse_nfa,
)


ROOT = Path(__file__).resolve().parents[1]
REGULAR_COMPILER = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "compiler"
    / "regular_span_compiler_v1.metta"
)
GUARDED_COMPILER = (
    ROOT
    / "experiments"
    / "gslt2parse_foundation"
    / "presentations"
    / "compiler"
    / "guarded_regular_span_compiler_v1.metta"
)

LEXICAL_ROOTS = {
    "comment": (
        119,
        "a63ea78b74a7900bb269473274c9f0632733dcc254f833f2aeeda1f815c4eac7",
    ),
    "label-token-lexeme": (
        22,
        "cae92432a1a449a25496ee8249f8ced01cb101c55c173278a0a3ba3914ec93af",
    ),
    "proof-label-token-lexeme": (
        22,
        "d78c2d4411a4a12c1734501444463ac3b5db9cdd7498e72b93745e3ab73fff97",
    ),
    "symbol-token-lexeme": (
        22,
        "3a5e0d577d459022b9b3b23efd9a6440fe5cbfeb2f639cf979d01b7370fd950c",
    ),
    "include-token-lexeme": (
        22,
        "51d6b8ef7cf83c0326727f9f007968457ba5e9679b9d41d5a668c2931d84587d",
    ),
    "compressed-token-lexeme": (
        22,
        "edd2f46130880e778f4ca2aa0f3c95fdecaf63b4f003ce72e220c096d82f52c5",
    ),
    "kw-constant-lexeme": (
        22,
        "c80acd6559998bd2b2a071b91619e70aafdffe3fa76f5f675b894bfcb567c5cb",
    ),
    "kw-variable-lexeme": (
        22,
        "024194b8b5146e59108be89756937b2e3a1879a9031136466007878983047ae2",
    ),
    "kw-disjoint-lexeme": (
        22,
        "254c9ed1d173b9f222cc33f3fd42385e424b04f97865f8dc8cb6058a097acf1e",
    ),
    "kw-float-lexeme": (
        22,
        "62d984b37d26cea67b9c53e02ce9ece66d2c0cf39da994b511b6212a570d3662",
    ),
    "kw-essential-lexeme": (
        22,
        "4d5459dfd3cbb6d35113efc1c1886b7a12552e59901ca2b3a71e8b0c4258c77c",
    ),
    "kw-axiom-lexeme": (
        22,
        "6cfd446c398fa5c3761e2ea140d918b9b242cd554cfe536a15184acc40f5a041",
    ),
    "kw-theorem-lexeme": (
        22,
        "4b262d4844b42affb3b185d1b1a97a6ffc6f8e28539649c897b9e0eb197ea392",
    ),
    "kw-proof-lexeme": (
        22,
        "532fb66986dec736637d80f5b3b248292ab9064ee276644b3396f6af1a96c6a7",
    ),
    "kw-end-lexeme": (
        22,
        "cccc58a891d3d5e5b86bfe5f82997b6c2d6964f2b71f1b1f30abddf5c2657bcf",
    ),
    "kw-block-open-lexeme": (
        22,
        "46413073a0641e94f5df6c9640e0e586241ebc1fb97310d458706e1d5b7f4aee",
    ),
    "kw-block-close-lexeme": (
        22,
        "f9c012f149c74a019adaba223389bcd162ffc00cd4f794bff0d2183066e07890",
    ),
    "kw-include-open-lexeme": (
        22,
        "ef329bf69991952fc5933a787312aae0ff4172f94e6c88242e02001b752e70bf",
    ),
    "kw-include-close-lexeme": (
        22,
        "925e4be13c69f24d9184b069cfe20e28316cab367dc604f0231c2d998b072d74",
    ),
    "left-paren-lexeme": (
        18,
        "2cdf4e70538b55c6b12413d9af0348ee84fe764427abf15fab630d7a3a573ea9",
    ),
    "right-paren-lexeme": (
        18,
        "a25fe7dd5c15fbd14d220ab2d3c7ff1c4c001fb3a026f1c1462d48af9f730413",
    ),
    "unknown-proof-lexeme": (
        18,
        "938d232b16f3c267d287ab7f386c59b1a189f3818b2fb9ed0b2f75c67fea0f55",
    ),
}

EXPECTED_SOURCE_SLR = {
    "conflict_cells": 2,
    "conflicts": 2,
    "productions": 441,
    "reachable_productions": 325,
    "states": 380,
}
EXPECTED_SOURCE_LR1 = {
    "conflict_cells": 2,
    "conflicts": 2,
    "productions": 441,
    "reachable_productions": 325,
    "states": 632,
}
EXPECTED_NORMALIZED_SLR = {
    "conflict_cells": 0,
    "conflicts": 0,
    "grammar_digest":
        "2b9d19a5cdc642f259f66f3fa6ec74303eaa60bc7fec0b408e164651a28734ef",
    "productions": 126,
    "reachable_productions": 80,
    "states": 132,
    "table_digest":
        "8b77d14484b30041f494640a056cd15b319c572c9125511105331a4e01a9f6d8",
}
EXPECTED_NORMALIZED_LR1 = {
    "conflict_cells": 0,
    "conflicts": 0,
    "grammar_digest":
        "2b9d19a5cdc642f259f66f3fa6ec74303eaa60bc7fec0b408e164651a28734ef",
    "productions": 126,
    "reachable_productions": 80,
    "states": 238,
    "table_digest":
        "4d26e018af41db9180e9ea939192ca9db84a7f4bdba4905bfa7a002c804bec89",
}


class GateFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class LexicalLanguage:
    tag: SExpr
    terminal: SExpr
    starts: frozenset[SExpr]
    edges: tuple[Edge, ...]
    accepts: frozenset[SExpr]
    coaccessible: frozenset[SExpr]


@dataclass(frozen=True, slots=True)
class LexicalActionOverlap:
    left: SExpr
    right: SExpr
    witness: tuple[int, ...]
    states: tuple[int, ...]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GateFailure(message)


def lexical_language(
    answer_texts: list[str],
) -> LexicalLanguage:
    starts, edges, accepts = parse_nfa(answer_texts)
    coaccessible = set(accepts)
    changed = True
    while changed:
        changed = False
        for edge in edges:
            if edge.target in coaccessible and edge.source not in coaccessible:
                coaccessible.add(edge.source)
                changed = True
    tags = {
        tag
        for state_tags in accepts.values()
        for tag in state_tags
    }
    if len(tags) != 1:
        raise GateFailure("lexical NFA does not have one rendered tag")
    rendered_tag = next(iter(tags))
    parsed_tags = parse_sexprs(
        rendered_tag, source="lexical NFA rendered tag"
    )
    if len(parsed_tags) != 1:
        raise GateFailure("lexical NFA tag is not one canonical term")
    tag = parsed_tags[0]
    return LexicalLanguage(
        tag=tag,
        terminal=(Symbol("pp-span-terminal"), tag),
        starts=frozenset(starts),
        edges=edges,
        accepts=frozenset(accepts),
        coaccessible=frozenset(coaccessible),
    )


def lexical_step(
    language: LexicalLanguage,
    states: frozenset[SExpr],
    scalar: int,
    clauses: tuple[ClassClause, ...],
) -> frozenset[SExpr]:
    targets = {
        edge.target
        for edge in language.edges
        if edge.source in states
        and (
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
    }
    return frozenset(
        closure(targets, language.edges, eof=False)
    )


def lexical_prefix_overlap_witness(
    left: LexicalLanguage,
    right: LexicalLanguage,
    clauses: tuple[ClassClause, ...],
    explicit_points: frozenset[int],
) -> tuple[int, ...] | None:
    probes = {0, 0x10FFFF}
    for point in explicit_points:
        for candidate in (point - 1, point, point + 1):
            if (
                0 <= candidate <= 0x10FFFF
                and not 0xD800 <= candidate <= 0xDFFF
            ):
                probes.add(candidate)
    alphabet = tuple(sorted(probes))
    left_start = frozenset(closure(
        set(left.starts), left.edges, eof=False
    ))
    right_start = frozenset(closure(
        set(right.starts), right.edges, eof=False
    ))
    queue = [(left_start, right_start, ())]
    seen = {(left_start, right_start)}
    read = 0
    while read < len(queue):
        left_states, right_states, witness = queue[read]
        read += 1
        left_ordinary = bool(left_states & left.accepts)
        right_ordinary = bool(right_states & right.accepts)
        left_at_eof = bool(
            frozenset(closure(
                set(left_states), left.edges, eof=True
            )) & left.accepts
        )
        right_at_eof = bool(
            frozenset(closure(
                set(right_states), right.edges, eof=True
            )) & right.accepts
        )
        left_can_accept = bool(left_states & left.coaccessible)
        right_can_accept = bool(right_states & right.coaccessible)
        if (
            (left_ordinary and right_can_accept)
            or (right_ordinary and left_can_accept)
            or (left_at_eof and right_at_eof)
        ):
            return witness
        for scalar in alphabet:
            left_next = lexical_step(
                left, left_states, scalar, clauses
            )
            right_next = lexical_step(
                right, right_states, scalar, clauses
            )
            pair = (left_next, right_next)
            if not left_next or not right_next or pair in seen:
                continue
            seen.add(pair)
            queue.append((left_next, right_next, witness + (scalar,)))
    return None


def lexical_action_overlaps(
    summary: LR1Summary | SLRSummary,
    languages: tuple[LexicalLanguage, ...],
    clauses: tuple[ClassClause, ...],
) -> tuple[LexicalActionOverlap, ...]:
    action_cells = {
        (cell.state, cell.lookahead): cell.actions
        for cell in summary.action_cells
    }
    explicit_points = frozenset(
        point
        for clause in clauses
        for point in clause.points
    ) | frozenset(
        codepoint(edge.payload)
        for language in languages
        for edge in language.edges
        if edge.kind == "char" and edge.payload is not None
    )
    result: list[LexicalActionOverlap] = []
    for left_index, left in enumerate(languages):
        for right in languages[left_index + 1:]:
            witness = lexical_prefix_overlap_witness(
                left, right, clauses, explicit_points
            )
            if witness is None:
                continue
            states: list[int] = []
            for state in range(summary.state_len):
                left_actions = action_cells.get((state, left.terminal))
                right_actions = action_cells.get((state, right.terminal))
                if left_actions is None or right_actions is None:
                    continue
                if (
                    len(left_actions) == len(right_actions) == 1
                    and left_actions[0] == right_actions[0]
                    and left_actions[0][0] == "reduce"
                ):
                    continue
                states.append(state)
            if states:
                result.append(LexicalActionOverlap(
                    left=left.tag,
                    right=right.tag,
                    witness=witness,
                    states=tuple(states),
                ))
    return tuple(result)


def qterm(value: int) -> SExpr:
    result: SExpr = Symbol("q-zero")
    for _ in range(value):
        result = (Symbol("q-succ"), result)
    return result


def action_list(*values: SExpr) -> SExpr:
    result: SExpr = Symbol("pa-nil")
    for value in reversed(values):
        result = (Symbol("pa-cons"), value, result)
    return result


def slot(index: int) -> SExpr:
    return (Symbol("pa-slot"), qterm(index))


def apply(name: str, *arguments: SExpr) -> SExpr:
    return (
        Symbol("pa-apply"),
        Symbol(name),
        action_list(*arguments),
    )


def evaluate_list(term: SExpr, values: tuple[SExpr, ...]) -> tuple[SExpr, ...]:
    result: list[SExpr] = []
    current = term
    while current != Symbol("pa-nil"):
        if (
            not isinstance(current, tuple)
            or len(current) != 3
            or current[0] != Symbol("pa-cons")
        ):
            raise GateFailure("test action contains a malformed list")
        result.append(evaluate_action(current[1], values))
        current = current[2]
    return tuple(result)


def evaluate_action(action: SExpr, values: tuple[SExpr, ...]) -> SExpr:
    if (
        isinstance(action, tuple)
        and len(action) == 2
        and action[0] == Symbol("pa-slot")
    ):
        index = qindex(action[1])
        if index >= len(values):
            raise GateFailure("test action reads outside its production")
        return values[index]
    if (
        isinstance(action, tuple)
        and len(action) == 2
        and action[0] == Symbol("pa-const")
    ):
        return action[1]
    if (
        isinstance(action, tuple)
        and len(action) == 3
        and action[0] == Symbol("pa-apply")
    ):
        return (
            action[1],
            *evaluate_list(action[2], values),
        )
    raise GateFailure("test encountered an unknown semantic action")


def bytecode_instructions(term: SExpr) -> tuple[SExpr, ...]:
    result: list[SExpr] = []
    current = term
    while current != Symbol("pbc-nil"):
        if (
            not isinstance(current, tuple)
            or len(current) != 3
            or current[0] != Symbol("pbc-cons")
        ):
            raise GateFailure("compiled bytecode has a malformed list")
        result.append(current[1])
        current = current[2]
    return tuple(result)


def bytecode_list(instructions: tuple[SExpr, ...]) -> SExpr:
    result: SExpr = Symbol("pbc-nil")
    for instruction in reversed(instructions):
        result = (Symbol("pbc-cons"), instruction, result)
    return result


def bytecode_slot_dependencies(
    code: SExpr,
    arity: int,
) -> frozenset[int]:
    dependencies: set[int] = set()
    for instruction in bytecode_instructions(code):
        if (
            isinstance(instruction, tuple)
            and len(instruction) == 2
            and instruction[0] == Symbol("pbc-push-slot")
        ):
            index = qindex(instruction[1])
            if index >= arity:
                raise GateFailure(
                    "semantic dependency reads outside its production"
                )
            dependencies.add(index)
    return frozenset(dependencies)


@dataclass(frozen=True, slots=True)
class SemanticRequirements:
    nonterminals: frozenset[SExpr]
    terminals: frozenset[SExpr]


def semantic_requirements(
    grammar: Grammar,
    action_code_by_label: dict[SExpr, SExpr],
) -> SemanticRequirements:
    required_nonterminals = {grammar.start}
    required_terminals: set[SExpr] = set()
    changed = True
    while changed:
        changed = False
        for production in grammar.productions:
            if production.lhs not in required_nonterminals:
                continue
            code = action_code_by_label.get(production.identity)
            if code is None:
                raise GateFailure(
                    "semantic dependency lacks one production action"
                )
            dependencies = bytecode_slot_dependencies(
                code, len(production.rhs)
            )
            for index in dependencies:
                symbol = production.rhs[index]
                if symbol.terminal:
                    required_terminals.add(symbol.identity)
                elif symbol.identity not in required_nonterminals:
                    required_nonterminals.add(symbol.identity)
                    changed = True
    return SemanticRequirements(
        nonterminals=frozenset(required_nonterminals),
        terminals=frozenset(required_terminals),
    )


def observable_comment_mutation(
    grammar: Grammar,
    action_terms: tuple[SExpr, ...],
    comment_terminal: SExpr,
) -> tuple[SExpr, int, SemanticRequirements]:
    term_by_label = {
        term[2]: term
        for term in action_terms
        if isinstance(term, tuple) and len(term) == 6
    }
    code_by_label = {
        label: term[5]
        for label, term in term_by_label.items()
    }
    baseline = semantic_requirements(grammar, code_by_label)
    if comment_terminal in baseline.terminals:
        raise GateFailure(
            "comment is already observable before the dependency mutation"
        )
    for production in grammar.productions:
        if production.lhs not in baseline.nonterminals:
            continue
        code = code_by_label[production.identity]
        dependencies = bytecode_slot_dependencies(
            code, len(production.rhs)
        )
        for index in range(len(production.rhs)):
            if index in dependencies:
                continue
            mutated_instructions = (
                *bytecode_instructions(code),
                (Symbol("pbc-push-slot"), qterm(index)),
                (
                    Symbol("pbc-apply"),
                    Symbol("semantic-dependency-probe"),
                    qterm(2),
                ),
            )
            mutated_code = bytecode_list(mutated_instructions)
            mutated_codes = dict(code_by_label)
            mutated_codes[production.identity] = mutated_code
            mutated_requirements = semantic_requirements(
                grammar, mutated_codes
            )
            if comment_terminal not in mutated_requirements.terminals:
                continue
            mutated_action: SExpr = (
                Symbol("pa-apply"),
                Symbol("semantic-dependency-probe"),
                (
                    Symbol("pa-cons"),
                    term_by_label[production.identity][4],
                    (
                        Symbol("pa-cons"),
                        (Symbol("pa-slot"), qterm(index)),
                        Symbol("pa-nil"),
                    ),
                ),
            )
            values = tuple(
                Symbol(f"slot-{slot}")
                for slot in range(len(production.rhs))
            )
            if evaluate_bytecode(
                mutated_code, values
            ) != evaluate_action(mutated_action, values):
                raise GateFailure(
                    "semantic dependency mutation is not valid bytecode"
                )
            return (
                production.identity,
                index,
                mutated_requirements,
            )
    raise GateFailure(
        "no valid action mutation exposes the skipped comment value"
    )


def evaluate_bytecode(code: SExpr, values: tuple[SExpr, ...]) -> SExpr:
    stack: list[SExpr] = []
    for instruction in bytecode_instructions(code):
        if (
            isinstance(instruction, tuple)
            and len(instruction) == 2
            and instruction[0] == Symbol("pbc-push-slot")
        ):
            index = qindex(instruction[1])
            if index >= len(values):
                raise GateFailure("bytecode reads outside its production")
            stack.append(values[index])
        elif (
            isinstance(instruction, tuple)
            and len(instruction) == 2
            and instruction[0] == Symbol("pbc-push-const")
        ):
            stack.append(instruction[1])
        elif (
            isinstance(instruction, tuple)
            and len(instruction) == 3
            and instruction[0] == Symbol("pbc-apply")
        ):
            arity = qindex(instruction[2])
            if arity > len(stack):
                raise GateFailure("bytecode application underflows its stack")
            arguments = tuple(stack[len(stack) - arity:])
            if arity:
                del stack[len(stack) - arity:]
            stack.append((instruction[1], *arguments))
        else:
            raise GateFailure("compiled bytecode has an unknown instruction")
    if len(stack) != 1:
        raise GateFailure("compiled bytecode did not produce one result")
    return stack[0]


def synthetic_common_prefix_gate() -> None:
    start = Symbol("synthetic-start")

    def sub(name: str) -> SExpr:
        return (
            Symbol("pp-sub"),
            Symbol("synthetic-root"),
            Symbol(name),
        )

    def terminal(name: str) -> GrammarSymbol:
        return GrammarSymbol(True, Symbol(name))

    def nonterminal(identity: SExpr) -> GrammarSymbol:
        return GrammarSymbol(False, identity)

    normal, compressed, normal_label, compressed_label = (
        sub("normal"),
        sub("compressed"),
        sub("normal-label"),
        sub("compressed-label"),
    )
    grammar = Grammar(start, (
        Production(
            Symbol("start-normal"),
            start,
            (nonterminal(normal),),
            apply("root", slot(0)),
        ),
        Production(
            Symbol("start-compressed"),
            start,
            (nonterminal(compressed),),
            apply("root", slot(0)),
        ),
        Production(
            Symbol("normal"),
            normal,
            (
                nonterminal(normal_label),
                terminal("keyword"),
                terminal("normal-tail"),
            ),
            apply("normal", slot(0), slot(1), slot(2)),
        ),
        Production(
            Symbol("compressed"),
            compressed,
            (
                nonterminal(compressed_label),
                terminal("keyword"),
                terminal("compressed-tail"),
            ),
            apply("compressed", slot(0), slot(1), slot(2)),
        ),
        Production(
            Symbol("normal-label"),
            normal_label,
            (terminal("label"),),
            apply("label", slot(0)),
        ),
        Production(
            Symbol("compressed-label"),
            compressed_label,
            (terminal("label"),),
            apply("label", slot(0)),
        ),
    ))
    require(
        analyze_lr1(grammar).conflict_len == 1
        and analyze_slr(grammar).conflict_len == 1,
        "synthetic compiler scaffolding did not expose its conflict",
    )
    normalized = inline_transparent_substates(grammar)
    require(
        len(normalized.grammar.productions) == 2
        and analyze_lr1(normalized.grammar).conflict_len == 0
        and analyze_slr(normalized.grammar).conflict_len == 0,
        "transparent inlining did not defer the common-prefix choice",
    )
    by_tail = {
        production.rhs[-1].identity: production
        for production in normalized.grammar.productions
    }
    label, keyword, normal_tail, compressed_tail = map(
        Symbol, ("L", "K", "N", "C")
    )
    require(
        evaluate_action(
            by_tail[Symbol("normal-tail")].action,
            (label, keyword, normal_tail),
        )
        == (
            Symbol("root"),
            (
                Symbol("normal"),
                (Symbol("label"), label),
                keyword,
                normal_tail,
            ),
        ),
        "normal-branch semantic action changed under inlining",
    )
    require(
        evaluate_action(
            by_tail[Symbol("compressed-tail")].action,
            (label, keyword, compressed_tail),
        )
        == (
            Symbol("root"),
            (
                Symbol("compressed"),
                (Symbol("label"), label),
                keyword,
                compressed_tail,
            ),
        ),
        "compressed-branch semantic action changed under inlining",
    )


def synthetic_recursion_and_failure_gate() -> None:
    start = Symbol("recursive-start")
    recursive = (
        Symbol("pp-sub"),
        Symbol("recursive-root"),
        Symbol("body"),
    )
    grammar = Grammar(start, (
        Production(
            Symbol("recursive-entry"),
            start,
            (GrammarSymbol(False, recursive),),
            slot(0),
        ),
        Production(
            Symbol("recursive-step"),
            recursive,
            (
                GrammarSymbol(True, Symbol("item")),
                GrammarSymbol(False, recursive),
            ),
            apply("cons", slot(0), slot(1)),
        ),
        Production(
            Symbol("recursive-empty"),
            recursive,
            (),
            (Symbol("pa-const"), Symbol("nil")),
        ),
    ))
    normalized = inline_transparent_substates(grammar)
    require(
        normalized.cyclic_states == (recursive,)
        and any(
            production.lhs == recursive
            for production in normalized.grammar.productions
        ),
        "recursive transparent state was unsafely inlined",
    )

    malformed = Grammar(Symbol("bad-start"), (
        Production(
            Symbol("bad-slot"),
            Symbol("bad-start"),
            (GrammarSymbol(True, Symbol("item")),),
            slot(1),
        ),
    ))
    try:
        inline_transparent_substates(malformed)
    except InlineFailure:
        pass
    else:
        raise GateFailure("out-of-range semantic action slot was accepted")

    branch = (
        Symbol("pp-sub"),
        Symbol("wide-root"),
        Symbol("branch"),
    )
    wide = Grammar(Symbol("wide-start"), (
        Production(
            Symbol("wide-entry"),
            Symbol("wide-start"),
            (
                GrammarSymbol(False, branch),
                GrammarSymbol(False, branch),
                GrammarSymbol(False, branch),
            ),
            apply("triple", slot(0), slot(1), slot(2)),
        ),
        Production(
            Symbol("wide-a"),
            branch,
            (GrammarSymbol(True, Symbol("a")),),
            slot(0),
        ),
        Production(
            Symbol("wide-b"),
            branch,
            (GrammarSymbol(True, Symbol("b")),),
            slot(0),
        ),
    ))
    try:
        inline_transparent_substates(wide, max_paths_per_state=4)
    except InlineFailure:
        pass
    else:
        raise GateFailure("path explosion limit did not fail closed")


def run_abi_loader(
    binary: Path,
    stream: bytes,
    *,
    expect_success: bool,
) -> str:
    with tempfile.TemporaryDirectory(
        prefix="parser-pack-transparent-inline-abi-"
    ) as raw:
        path = Path(raw) / "pack.abi"
        path.write_bytes(stream)
        completed = subprocess.run(
            [str(binary), str(path)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=90,
        )
    if expect_success:
        if completed.returncode != 0:
            raise GateFailure(
                "native ABI loader rejected normalized pack: "
                + completed.stderr.strip()
            )
        return completed.stdout
    if completed.returncode == 0 or not completed.stderr.strip():
        raise GateFailure("corrupt normalized ABI did not fail closed")
    return ""


def run_plan(
    binary: Path,
    abi_path: Path,
    nfa_path: Path,
) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(binary),
            str(abi_path),
            str(nfa_path),
            sha256(REGULAR_COMPILER.read_bytes()).hexdigest(),
        ],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "native lexical-plan construction failed: "
            + completed.stderr.strip()
        )
    return parse_plan_stream(completed.stdout)


def zero_guard_exec_arguments(
    binary: Path,
    abi_path: Path,
    lexical_path: Path,
    evidence_path: Path,
    empty_path: Path,
    input_path: Path,
    *,
    action_path: Path | None = None,
    action_compiler_digest: str | None = None,
    occurrence_fold_path: Path | None = None,
    indexed_checker_effect_path: Path | None = None,
    indexed_checker_effect_digest: str | None = None,
    emit_c_path: Path | None = None,
    emit_c_prefix: str | None = None,
) -> list[str]:
    arguments = [
        str(binary),
        str(abi_path),
        str(lexical_path),
        str(empty_path),
        str(evidence_path),
        str(empty_path),
        str(input_path),
        sha256(REGULAR_COMPILER.read_bytes()).hexdigest(),
        sha256(GUARDED_COMPILER.read_bytes()).hexdigest(),
    ]
    optional = (
        action_path,
        action_compiler_digest,
        occurrence_fold_path,
        indexed_checker_effect_path,
        indexed_checker_effect_digest,
        emit_c_path,
        emit_c_prefix,
    )
    if any(item is not None for item in optional):
        if action_path is None or action_compiler_digest is None:
            raise GateFailure(
                "cursor C emission requires the action program identity"
            )
        arguments.extend((str(action_path), action_compiler_digest))
        if occurrence_fold_path is not None:
            arguments.extend(
                ("--occurrence-fold", str(occurrence_fold_path))
            )
        if (
            indexed_checker_effect_path is not None
            or indexed_checker_effect_digest is not None
        ):
            if (
                occurrence_fold_path is None
                or indexed_checker_effect_path is None
                or indexed_checker_effect_digest is None
            ):
                raise GateFailure(
                    "checker effects require an authenticated "
                    "occurrence-fold composition"
                )
            arguments.extend((
                "--indexed-checker-effects",
                str(indexed_checker_effect_path),
                indexed_checker_effect_digest,
            ))
        if emit_c_path is not None or emit_c_prefix is not None:
            if emit_c_path is None or emit_c_prefix is None:
                raise GateFailure(
                    "cursor C output path and identifier prefix must agree"
                )
            arguments.extend(("--emit-c", str(emit_c_path), emit_c_prefix))
    return arguments


def run_zero_guard_exec(
    binary: Path,
    abi_path: Path,
    lexical_path: Path,
    evidence_path: Path,
    empty_path: Path,
    input_path: Path,
    *,
    action_path: Path | None = None,
    action_compiler_digest: str | None = None,
    occurrence_fold_path: Path | None = None,
    indexed_checker_effect_path: Path | None = None,
    indexed_checker_effect_digest: str | None = None,
    emit_c_path: Path | None = None,
    emit_c_prefix: str | None = None,
) -> dict[str, str | list[str]]:
    arguments = zero_guard_exec_arguments(
        binary, abi_path, lexical_path, evidence_path,
        empty_path, input_path,
        action_path=action_path,
        action_compiler_digest=action_compiler_digest,
        occurrence_fold_path=occurrence_fold_path,
        indexed_checker_effect_path=indexed_checker_effect_path,
        indexed_checker_effect_digest=indexed_checker_effect_digest,
        emit_c_path=emit_c_path,
        emit_c_prefix=emit_c_prefix,
    )
    completed = subprocess.run(
        arguments,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "zero-guard Metamath execution failed: "
            + completed.stderr.strip()
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "parser-pack-guarded-lexical-exec-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("zero-guard execution emitted malformed framing")
    result: dict[str, str | list[str]] = {
        "gll-result": [],
        "glr-result": [],
    }
    for line in lines[1:-1]:
        field, separator, value = line.partition("\t")
        if not separator:
            raise GateFailure("zero-guard execution emitted a malformed row")
        if field in ("gll-result", "glr-result"):
            values = result[field]
            assert isinstance(values, list)
            values.append(value)
        elif field in result:
            raise GateFailure(
                f"zero-guard execution repeated field {field}"
            )
        else:
            result[field] = value
    return result


def require_zero_guard_exec_failure(
    binary: Path,
    abi_path: Path,
    lexical_path: Path,
    evidence_path: Path,
    empty_path: Path,
    input_path: Path,
    *,
    action_path: Path,
    action_compiler_digest: str,
    occurrence_fold_path: Path,
    indexed_checker_effect_path: Path,
    indexed_checker_effect_digest: str,
    diagnostic: str,
) -> None:
    arguments = zero_guard_exec_arguments(
        binary, abi_path, lexical_path, evidence_path,
        empty_path, input_path,
        action_path=action_path,
        action_compiler_digest=action_compiler_digest,
        occurrence_fold_path=occurrence_fold_path,
        indexed_checker_effect_path=indexed_checker_effect_path,
        indexed_checker_effect_digest=indexed_checker_effect_digest,
    )
    completed = subprocess.run(
        arguments,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
    )
    require(
        completed.returncode != 0
        and diagnostic in completed.stderr
        and "indexed-inference-state-digest\t" not in completed.stdout,
        "semantic checker mutation survived or published state: "
        + completed.stderr.strip(),
    )


def build_generated_cursor(
    generated_c: Path,
    generated_binary: Path,
    prefix: str,
    occurrence_fold: bool = False,
    indexed_checker_effects: bool = False,
) -> None:
    arguments = [
        "make",
        "-s",
        "build-parser-pack-cursor-generated-v1",
        f"GENERATED_CURSOR_C={generated_c}",
        f"GENERATED_CURSOR_BIN={generated_binary}",
        f"GENERATED_CURSOR_PREFIX={prefix}",
    ]
    if occurrence_fold:
        arguments.append("GENERATED_CURSOR_OCCURRENCE_FOLD=1")
    if indexed_checker_effects:
        if not occurrence_fold:
            raise GateFailure(
                "generated checker effects require an occurrence fold"
            )
        arguments.append(
            "GENERATED_CURSOR_INDEXED_CHECKER_EFFECTS=1"
        )
    completed = subprocess.run(
        arguments,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=120,
    )
    if completed.returncode != 0 or not generated_binary.is_file():
        raise GateFailure(
            "generated Metamath cursor did not compile: "
            + completed.stderr.strip()
        )


def run_generated_cursor(
    binary: Path,
    input_path: Path,
    *arguments: str,
) -> dict[str, str | list[str]]:
    completed = subprocess.run(
        [str(binary), str(input_path), *arguments],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=30,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "generated Metamath cursor execution failed: "
            + completed.stderr.strip()
        )
    lines = completed.stdout.splitlines()
    if (
        not lines
        or lines[0] != "parser-pack-cursor-generated-v1"
        or lines[-1] != "end"
    ):
        raise GateFailure("generated cursor emitted malformed framing")
    result: dict[str, str | list[str]] = {"semantic-result": []}
    for line in lines[1:-1]:
        field, separator, value = line.partition("\t")
        if not separator:
            raise GateFailure("generated cursor emitted a malformed row")
        if field == "semantic-result":
            values = result[field]
            assert isinstance(values, list)
            values.append(value)
        elif field in result:
            raise GateFailure(f"generated cursor repeated field {field}")
        else:
            result[field] = value
    return result


def require_generated_cursor_failure(
    binary: Path,
    input_path: Path,
    *,
    diagnostic: str,
) -> None:
    completed = subprocess.run(
        [str(binary), str(input_path), "--occurrence-fold"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=30,
    )
    require(
        completed.returncode != 0
        and diagnostic in completed.stderr
        and "indexed-inference-state-digest\t" not in completed.stdout,
        "generated source-composition failure was accepted or published "
        "state: " + completed.stderr.strip(),
    )


def selected(summary: dict[str, object], expected: dict[str, object]) -> bool:
    return all(summary.get(key) == value for key, value in expected.items())


def real_metamath_gate(
    chart_binary: Path,
    plan_binary: Path,
    abi_binary: Path,
    exec_binary: Path,
    petta_root: Path,
    generated_c_output: Path | None = None,
) -> dict[str, str | list[str]]:
    source_stream = export_stream(petta_root, ABI_CASES["metamath"])
    source = parse_abi_stream(source_stream)
    require(
        len(source.productions) == 441,
        "current Metamath ParserPack does not have 441 productions",
    )
    normalized = inline_transparent_substates(source)
    require(
        len(normalized.grammar.productions) == 126
        and len(normalized.cyclic_states) == 8
        and normalized.removed_production_len == 324,
        "Metamath transparent-normalization inventory changed",
    )
    emitted = emit_normalized_abi(source_stream, normalized)
    verify_normalized_abi_evidence(source_stream, emitted)
    reproduced = verify_reproducible_normalization(
        source_stream, emitted
    )
    require(
        reproduced.grammar == normalized.grammar,
        "reproducible normalization changed the grammar",
    )
    require(
        run_abi_loader(abi_binary, emitted, expect_success=True).strip()
        == "(ParserPackABIV1StreamSummary 126 375 501 1 0)",
        "native ABI summary changed",
    )

    base_action_compiler = (
        ROOT
        / "experiments"
        / "gslt2parse_foundation"
        / "presentations"
        / "compiler"
        / "parser_action_bytecode_compiler_v1.metta"
    )
    action_stream, action_compiler_digest = emit_static_action_answers(
        source_stream,
        emitted,
        sha256(base_action_compiler.read_bytes()).hexdigest(),
    )
    action_terms = parse_sexprs(
        action_stream.decode("utf-8"),
        source="statically compiled Metamath actions",
    )
    require(
        len(action_terms) == len(normalized.grammar.productions) == 126
        and len(action_compiler_digest) == 64,
        "static action compiler did not cover every normalized production",
    )
    action_by_label: dict[SExpr, tuple[SExpr, SExpr]] = {}
    for term in action_terms:
        if (
            not isinstance(term, tuple)
            or len(term) != 6
            or term[0] != Symbol("compile-pack-action-program")
        ):
            raise GateFailure("static action answer has malformed shape")
        if term[2] in action_by_label:
            raise GateFailure("static action answer repeats a label")
        action_by_label[term[2]] = (term[4], term[5])
    for production in normalized.grammar.productions:
        action, code = action_by_label[production.identity]
        values = tuple(
            Symbol(f"slot-{index}")
            for index in range(len(production.rhs))
        )
        require(
            action == production.action
            and evaluate_bytecode(code, values)
            == evaluate_action(production.action, values),
            "static action bytecode changed production semantics",
        )

    action_mutation = action_stream.replace(
        b"pbc-push-slot", b"pbc-push-const", 1
    )
    require(
        action_mutation != action_stream,
        "action-bytecode mutation did not fire",
    )
    mutated_terms = parse_sexprs(
        action_mutation.decode("utf-8"),
        source="mutated statically compiled Metamath actions",
    )
    mutation_killed = False
    for term in mutated_terms:
        if not isinstance(term, tuple) or len(term) != 6:
            raise GateFailure("mutated action answer changed shape")
        production = next(
            (
                item
                for item in normalized.grammar.productions
                if item.identity == term[2]
            ),
            None,
        )
        if production is None:
            raise GateFailure("mutated action answer has a foreign label")
        values = tuple(
            Symbol(f"slot-{index}")
            for index in range(len(production.rhs))
        )
        if evaluate_bytecode(term[5], values) != evaluate_action(
            production.action, values
        ):
            mutation_killed = True
            break
    require(mutation_killed, "action-bytecode mutation survived")

    artifact_mutation = emitted.replace(
        b"pp-inline-label", b"pp-inline-labeX", 1
    )
    require(
        artifact_mutation != emitted,
        "artifact mutation did not fire",
    )
    run_abi_loader(abi_binary, artifact_mutation, expect_success=False)

    evidence_mutation = emitted.replace(
        b"pp-inline-source-occurrence",
        b"pp-inline-source-occurrence-mutant",
        1,
    )
    require(
        evidence_mutation != emitted,
        "evidence mutation did not fire",
    )
    try:
        verify_normalized_abi_evidence(
            source_stream, evidence_mutation
        )
    except InlineFailure:
        pass
    else:
        raise GateFailure("source-fiber evidence mutation survived")

    lexical_terms: list[str] = []
    lexical_answers: dict[str, list[str]] = {}
    for tag, (expected_count, expected_digest) in LEXICAL_ROOTS.items():
        result = c_compiler_answers(
            chart_binary,
            {"language": "metamath", "tag": tag},
        )
        terms = result.get("terms")
        require(
            result.get("outcome") == "Ambiguous"
            and result.get("answers") == expected_count
            and result.get("term_digest") == expected_digest
            and isinstance(terms, list),
            f"{tag}: lexical GSLT compilation seal changed",
        )
        lexical_answers[tag] = terms
        lexical_terms.extend(terms)
    require(
        len(lexical_terms) == 569
        and len(set(lexical_terms)) == len(lexical_terms),
        "Metamath lexical-root compilation is incomplete or overlapping",
    )
    lexical_languages = {
        tag: lexical_language(lexical_answers[tag])
        for tag in LEXICAL_ROOTS
    }

    with tempfile.TemporaryDirectory(
        prefix="parser-pack-transparent-inline-metamath-"
    ) as raw:
        directory = Path(raw)
        source_path = directory / "source.abi"
        normalized_path = directory / "normalized.abi"
        nfa_path = directory / "lexical.nfa"
        action_path = directory / "actions.answers"
        occurrence_fold_path = directory / "occurrence-fold.answers"
        indexed_checker_effect_path = (
            directory / "indexed-checker-effects.answers"
        )
        source_path.write_bytes(source_stream)
        normalized_path.write_bytes(emitted)
        action_path.write_bytes(action_stream)
        fold_compile = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" /
                    "test_parser_occurrence_fold_compiler_v1.py"),
                "--chart-binary",
                str(chart_binary),
                "--petta-root",
                str(petta_root),
                "--write-plan",
                str(occurrence_fold_path),
            ],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=90,
        )
        require(
            fold_compile.returncode == 0
            and occurrence_fold_path.is_file(),
            "authored occurrence-fold plan did not compile: "
            + fold_compile.stderr.strip(),
        )
        checker_effect_compile = subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" /
                    "test_indexed_checker_fold_compiler_v1.py"),
                "--chart-binary",
                str(chart_binary),
                "--write-plan",
                str(indexed_checker_effect_path),
            ],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
            timeout=90,
        )
        require(
            checker_effect_compile.returncode == 0
            and indexed_checker_effect_path.is_file(),
            "authored indexed-checker effect plan did not compile: "
            + checker_effect_compile.stderr.strip(),
        )
        nfa_path.write_text(
            "\n".join(sorted(lexical_terms)) + "\n",
            encoding="utf-8",
        )
        source_plan = run_plan(plan_binary, source_path, nfa_path)
        normalized_plan = run_plan(
            plan_binary, normalized_path, nfa_path
        )
        normalized_composite = composite_grammar(
            normalized.grammar, normalized_plan
        )
        action_codes = {
            label: action_and_code[1]
            for label, action_and_code in action_by_label.items()
        }
        semantic_liveness = semantic_requirements(
            normalized_composite, action_codes
        )
        whitespace_terminals = {
            symbol.identity
            for production in normalized_composite.productions
            for symbol in production.rhs
            if symbol.terminal
            and symbol.identity
            == (Symbol("pp-terminal-class"), Symbol("whitespace"))
        }
        require(
            len(whitespace_terminals) == 1,
            "composed Metamath grammar lost its whitespace terminal",
        )
        expected_live_lexemes = frozenset(
            lexical_languages[name].terminal
            for name in (
                "compressed-token-lexeme",
                "include-token-lexeme",
                "label-token-lexeme",
                "proof-label-token-lexeme",
                "symbol-token-lexeme",
            )
        )
        require(
            semantic_liveness.terminals == expected_live_lexemes,
            "independent semantic liveness did not select exactly the "
            "five final-AST-bearing Metamath lexemes",
        )
        (
            dependency_mutation_label,
            dependency_mutation_slot,
            mutated_liveness,
        ) = observable_comment_mutation(
            normalized_composite,
            tuple(action_terms),
            lexical_languages["comment"].terminal,
        )
        empty_path = directory / "empty.answers"
        evidence_path = directory / "empty-guard.evidence"
        input_path = directory / "small.mm"
        scoped_input_path = directory / "scoped.mm"
        nested_scoped_input_path = directory / "nested-scoped.mm"
        rejected_input_path = directory / "rejected.mm"
        normal_proof_input_path = directory / "normal-proof.mm"
        compressed_proof_input_path = directory / "compressed-proof.mm"
        unknown_normal_input_path = directory / "unknown-normal.mm"
        unknown_compressed_input_path = (
            directory / "unknown-compressed.mm"
        )
        dv_violation_input_path = directory / "dv-violation.mm"
        include_library_path = directory / "include-library.mm"
        include_basic_path = directory / "include-basic.mm"
        include_duplicate_path = directory / "include-duplicate.mm"
        include_cycle_a_path = directory / "include-cycle-a.mm"
        include_cycle_b_path = directory / "include-cycle-b.mm"
        generated_c_path = directory / "metamath_cursor.generated.c"
        generated_binary = directory / "metamath_cursor"
        generated_prefix = "metamath_reader_cursor_v1"
        empty_path.write_bytes(b"")
        evidence_path.write_text(
            "\n".join((
                "parser-pack-positive-guard-evidence-v1",
                "source-digest\t"
                + abi_scalar(emitted, "source-digest"),
                "pre-reflection-digest\t"
                + abi_scalar(emitted, "compiler-digest"),
                "environment-digest\t"
                + abi_scalar(emitted, "environment-digest"),
                "answer-set-digest\t" + answer_digest(()),
                "end",
                "",
            )),
            encoding="utf-8",
        )
        input_path.write_bytes(b"$c wff $. $( generated comment $)\n")
        scoped_input_path.write_bytes(
            b"$c wff $. ${ $v x $. $}\n"
        )
        nested_scoped_input_path.write_bytes(
            b"$c wff $. ${ ${ $v x $. $} $}\n"
        )
        rejected_input_path.write_bytes(b"$c wff\n")
        normal_proof_input_path.write_bytes(
            b"$c wff $. $v x $. hx $f wff x $. "
            b"axid $a wff x $. "
            b"thid $p wff x $= hx axid $.\n"
        )
        compressed_proof_input_path.write_bytes(
            b"$c wff $. $v x $. hx $f wff x $. "
            b"axid $a wff x $. "
            b"thid $p wff x $= ( axid ) AB $.\n"
        )
        unknown_normal_input_path.write_bytes(
            b"$c wff $. $v x $. hx $f wff x $. "
            b"thq $p wff x $= ? $.\n"
        )
        unknown_compressed_input_path.write_bytes(
            b"$c wff $. $v x $. hx $f wff x $. "
            b"thq $p wff x $= ( ) ? $.\n"
        )
        dv_violation_input_path.write_bytes(
            b"$c wff pair $. $v x y $. "
            b"hx $f wff x $. hy $f wff y $. "
            b"${ $d x y $. axxy $a wff pair x y $. $} "
            b"bad $p wff pair x y $= hx hy axxy $.\n"
        )
        include_library_path.write_bytes(b"$c wff $.\n")
        include_basic_path.write_bytes(
            b"$[ include-library.mm $]\n$v x $.\n"
        )
        include_duplicate_path.write_bytes(
            b"$[ include-library.mm $]\n"
            b"$[ include-library.mm $]\n"
            b"$v x $.\n"
        )
        include_cycle_a_path.write_bytes(
            b"$[ include-cycle-b.mm $]\n"
        )
        include_cycle_b_path.write_bytes(
            b"$[ include-cycle-a.mm $]\n"
        )
        zero_guard_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
            emit_c_path=generated_c_path,
            emit_c_prefix=generated_prefix,
        )
        scoped_fold_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, scoped_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        nested_scoped_fold_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, nested_scoped_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        rejected_fold_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, rejected_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        normal_proof_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, normal_proof_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        compressed_proof_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, compressed_proof_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        unknown_normal_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, unknown_normal_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        unknown_compressed_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, unknown_compressed_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        include_basic_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, include_basic_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        include_duplicate_execution = run_zero_guard_exec(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, include_duplicate_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
        )
        require_zero_guard_exec_failure(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, include_cycle_a_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
            diagnostic="active source-resolution cycle",
        )
        require_zero_guard_exec_failure(
            exec_binary, normalized_path, nfa_path,
            evidence_path, empty_path, dv_violation_input_path,
            action_path=action_path,
            action_compiler_digest=action_compiler_digest,
            occurrence_fold_path=occurrence_fold_path,
            indexed_checker_effect_path=indexed_checker_effect_path,
            indexed_checker_effect_digest=(
                INDEXED_CHECKER_EFFECT_PLAN_DIGEST
            ),
            diagnostic="distinctness side condition is not preserved",
        )
        build_generated_cursor(
            generated_c_path, generated_binary, generated_prefix,
            occurrence_fold=True,
            indexed_checker_effects=True,
        )
        generated_execution = run_generated_cursor(
            generated_binary, input_path
        )
        generated_event_execution = run_generated_cursor(
            generated_binary, input_path, "--events"
        )
        generated_fold_execution = run_generated_cursor(
            generated_binary, input_path, "--occurrence-fold"
        )
        generated_scoped_fold_execution = run_generated_cursor(
            generated_binary, scoped_input_path, "--occurrence-fold"
        )
        generated_nested_scoped_fold_execution = run_generated_cursor(
            generated_binary, nested_scoped_input_path,
            "--occurrence-fold"
        )
        generated_rejected_fold_execution = run_generated_cursor(
            generated_binary, rejected_input_path, "--occurrence-fold"
        )
        generated_normal_proof_execution = run_generated_cursor(
            generated_binary, normal_proof_input_path,
            "--occurrence-fold"
        )
        generated_compressed_proof_execution = run_generated_cursor(
            generated_binary, compressed_proof_input_path,
            "--occurrence-fold"
        )
        generated_unknown_normal_execution = run_generated_cursor(
            generated_binary, unknown_normal_input_path,
            "--occurrence-fold"
        )
        generated_unknown_compressed_execution = run_generated_cursor(
            generated_binary, unknown_compressed_input_path,
            "--occurrence-fold"
        )
        generated_include_basic_execution = run_generated_cursor(
            generated_binary, include_basic_path, "--occurrence-fold"
        )
        generated_include_duplicate_execution = run_generated_cursor(
            generated_binary, include_duplicate_path, "--occurrence-fold"
        )
        require_generated_cursor_failure(
            generated_binary,
            include_cycle_a_path,
            diagnostic="active source-resolution cycle",
        )
        if generated_c_output is not None:
            generated_c_output.parent.mkdir(parents=True, exist_ok=True)
            generated_c_output.write_bytes(generated_c_path.read_bytes())

    require(
        normalized_plan.get("lexical-plan-digest")
        == "b8cd056179fe86f596c45de8bd45d845d02229a7ef6280559615cf4c8f661366",
        "normalized Metamath lexical-plan identity changed",
    )
    source_composite = composite_grammar(source, source_plan)
    source_slr = slr_summary_json(analyze_slr(source_composite))
    source_lr1 = summary_json(analyze_lr1(source_composite))
    normalized_slr = slr_summary_json(
        analyze_slr(normalized_composite)
    )
    normalized_lr1 = summary_json(
        analyze_lr1(normalized_composite)
    )
    languages = tuple(lexical_languages.values())
    clauses = class_clauses(emitted)
    normalized_slr_analysis = analyze_slr(normalized_composite)
    normalized_lr1_analysis = analyze_lr1(normalized_composite)
    slr_lexical_overlaps = lexical_action_overlaps(
        normalized_slr_analysis, languages, clauses
    )
    lr1_lexical_overlaps = lexical_action_overlaps(
        normalized_lr1_analysis, languages, clauses
    )
    require(
        selected(source_slr, EXPECTED_SOURCE_SLR)
        and selected(source_lr1, EXPECTED_SOURCE_LR1),
        "the source compiler scaffolding no longer exposes two conflicts",
    )
    require(
        selected(normalized_slr, EXPECTED_NORMALIZED_SLR)
        and selected(normalized_lr1, EXPECTED_NORMALIZED_LR1),
        "normalized Metamath grammar is not deterministically staged",
    )
    require(
        zero_guard_execution.get("guard-body-tokens") == "0"
        and zero_guard_execution.get("guarded-candidates") == "0"
        and zero_guard_execution.get("guarded-tokens") == "0"
        and zero_guard_execution.get("guard-witness-runs") == "0"
        and zero_guard_execution.get(
            "guard-witness-parser-family-builds"
        ) == "0"
        and zero_guard_execution.get(
            "guarded-witness-parser-family-builds"
        ) == "0"
        and zero_guard_execution.get("gll-outcome") == "completed"
        and zero_guard_execution.get("glr-outcome") == "completed"
        and zero_guard_execution.get("gll-decision")
        == zero_guard_execution.get("glr-decision")
        and zero_guard_execution.get("gll-result")
        == zero_guard_execution.get("glr-result"),
        "empty guard family is not an execution-level identity",
    )
    require(
        zero_guard_execution.get("cursor-program-built") == "1"
        and zero_guard_execution.get("cursor-program-actions-bound") == "1"
        and zero_guard_execution.get("cursor-certificate-failure-mask")
        == "0"
        and zero_guard_execution.get(
            "cursor-program-action-productions"
        ) == "126"
        and zero_guard_execution.get(
            "cursor-program-action-instructions"
        ) == "334",
        "Metamath cursor/action staging seal changed: "
        f"productions={zero_guard_execution.get('cursor-program-action-productions')} "
        f"instructions={zero_guard_execution.get('cursor-program-action-instructions')}",
    )
    require(
        zero_guard_execution.get("occurrence-fold-built") == "1"
        and zero_guard_execution.get("occurrence-fold-roles") == "7"
        and zero_guard_execution.get("occurrence-fold-nodes") == "10"
        and zero_guard_execution.get("occurrence-fold-operations") == "12"
        and zero_guard_execution.get(
            "occurrence-fold-terminal-bindings"
        ) == "8"
        and zero_guard_execution.get(
            "occurrence-fold-production-bindings"
        ) == "10"
        and zero_guard_execution.get("occurrence-fold-steps") == "1"
        and zero_guard_execution.get("occurrence-fold-shift-steps") == "0"
        and zero_guard_execution.get("occurrence-fold-reduce-steps") == "1"
        and zero_guard_execution.get("occurrence-fold-values") == "1"
        and zero_guard_execution.get("occurrence-fold-committed") == "1"
        and len(
            str(zero_guard_execution.get(
                "occurrence-fold-trace-digest", ""
            ))
        ) == 64,
        "compiled occurrence-fold plan did not transact the real cursor "
        "stream",
    )
    require(
        zero_guard_execution.get("indexed-checker-effect-built") == "1"
        and zero_guard_execution.get(
            "indexed-checker-effect-operations"
        ) == "12"
        and zero_guard_execution.get(
            "indexed-checker-effect-kinds"
        ) == "12"
        and len(str(zero_guard_execution.get(
            "indexed-checker-effect-plan-digest", ""
        ))) == 64,
        "compiled source operations did not select the complete "
        "indexed-checker effect algebra",
    )
    require(
        zero_guard_execution.get("indexed-inference-committed") == "1"
        and len(str(zero_guard_execution.get(
            "indexed-inference-state-digest", ""
        ))) == 64
        and zero_guard_execution.get(
            "indexed-inference-occurrence-digest"
        ) == zero_guard_execution.get("occurrence-fold-trace-digest")
        and zero_guard_execution.get("indexed-inference-effects") == "1"
        and zero_guard_execution.get(
            "indexed-inference-scope-opens"
        ) == "0"
        and zero_guard_execution.get(
            "indexed-inference-scope-closes"
        ) == "0"
        and zero_guard_execution.get(
            "indexed-inference-scope-completions"
        ) == "0"
        and zero_guard_execution.get("indexed-inference-atoms") == "1"
        and zero_guard_execution.get("indexed-inference-objects") == "1"
        and zero_guard_execution.get("indexed-inference-constants") == "1"
        and zero_guard_execution.get("indexed-inference-variables") == "0"
        and zero_guard_execution.get("indexed-inference-hypotheses") == "0"
        and zero_guard_execution.get("indexed-inference-rules") == "0"
        and zero_guard_execution.get(
            "indexed-inference-active-hypotheses"
        ) == "0"
        and zero_guard_execution.get(
            "indexed-inference-active-distinct"
        ) == "0"
        and zero_guard_execution.get(
            "indexed-inference-scope-depth"
        ) == "0",
        "source-owned indexed inference state did not transactionally "
        "apply the compiled constructor-declaration effect",
    )
    require(
        scoped_fold_execution.get("gll-decision") == "accepted"
        and scoped_fold_execution.get("glr-decision") == "accepted"
        and scoped_fold_execution.get("occurrence-fold-steps") == "5"
        and scoped_fold_execution.get(
            "occurrence-fold-shift-steps"
        ) == "2"
        and scoped_fold_execution.get(
            "occurrence-fold-reduce-steps"
        ) == "3"
        and scoped_fold_execution.get("occurrence-fold-values") == "4"
        and scoped_fold_execution.get(
            "occurrence-fold-max-pending-values"
        ) == "1"
        and scoped_fold_execution.get(
            "occurrence-fold-committed"
        ) == "1"
        and scoped_fold_execution.get("occurrence-fold-trace-digest")
        != zero_guard_execution.get("occurrence-fold-trace-digest"),
        "scope-boundary operations did not occur in source order",
    )
    require(
        scoped_fold_execution.get("indexed-inference-committed") == "1"
        and scoped_fold_execution.get("indexed-inference-effects") == "5"
        and scoped_fold_execution.get(
            "indexed-inference-scope-opens"
        ) == "1"
        and scoped_fold_execution.get(
            "indexed-inference-scope-closes"
        ) == "1"
        and scoped_fold_execution.get(
            "indexed-inference-scope-completions"
        ) == "1"
        and scoped_fold_execution.get("indexed-inference-atoms") == "2"
        and scoped_fold_execution.get("indexed-inference-objects") == "2"
        and scoped_fold_execution.get("indexed-inference-constants") == "1"
        and scoped_fold_execution.get("indexed-inference-variables") == "1"
        and scoped_fold_execution.get("indexed-inference-hypotheses") == "0"
        and scoped_fold_execution.get("indexed-inference-rules") == "0"
        and scoped_fold_execution.get(
            "indexed-inference-active-hypotheses"
        ) == "0"
        and scoped_fold_execution.get(
            "indexed-inference-active-distinct"
        ) == "0"
        and scoped_fold_execution.get(
            "indexed-inference-scope-depth"
        ) == "0"
        and scoped_fold_execution.get(
            "indexed-inference-occurrence-digest"
        ) == scoped_fold_execution.get("occurrence-fold-trace-digest"),
        "scope effects did not update and restore the source-owned "
        "inference state transactionally",
    )
    require(
        nested_scoped_fold_execution.get("gll-decision") == "accepted"
        and nested_scoped_fold_execution.get("glr-decision")
        == "accepted"
        and nested_scoped_fold_execution.get(
            "occurrence-fold-committed"
        ) == "1"
        and nested_scoped_fold_execution.get(
            "indexed-inference-committed"
        ) == "1"
        and nested_scoped_fold_execution.get(
            "indexed-inference-effects"
        ) == "8"
        and nested_scoped_fold_execution.get(
            "indexed-inference-scope-opens"
        ) == "2"
        and nested_scoped_fold_execution.get(
            "indexed-inference-scope-closes"
        ) == "2"
        and nested_scoped_fold_execution.get(
            "indexed-inference-scope-completions"
        ) == "2"
        and nested_scoped_fold_execution.get(
            "indexed-inference-scope-depth"
        ) == "0",
        "nested scopes did not preserve distinct open, close, and "
        "structural-completion occurrences",
    )
    require(
        rejected_fold_execution.get("gll-decision") == "rejected"
        and rejected_fold_execution.get("glr-decision") == "rejected"
        and rejected_fold_execution.get(
            "occurrence-fold-committed"
        ) == "0"
        and rejected_fold_execution.get(
            "occurrence-fold-trace-digest"
        ) == "",
        "a rejected source published its staged occurrence fold",
    )
    require(
        rejected_fold_execution.get("indexed-inference-committed") == "0"
        and rejected_fold_execution.get(
            "indexed-inference-state-digest"
        ) == ""
        and rejected_fold_execution.get(
            "indexed-inference-occurrence-digest"
        ) == ""
        and rejected_fold_execution.get("indexed-inference-atoms") == "0"
        and rejected_fold_execution.get("indexed-inference-objects") == "0",
        "a rejected source published its staged inference state",
    )
    for label, execution in (
        ("normal", normal_proof_execution),
        ("compressed", compressed_proof_execution),
    ):
        require(
            execution.get("gll-decision") == "accepted"
            and execution.get("glr-decision") == "accepted"
            and execution.get("occurrence-fold-committed") == "1"
            and execution.get("indexed-inference-committed") == "1"
            and execution.get("indexed-inference-effects") == "5"
            and execution.get("indexed-inference-proof-steps") == "2"
            and execution.get(
                "indexed-inference-rule-applications"
            ) == "1"
            and execution.get("indexed-inference-unknown-steps") == "0"
            and len(str(execution.get(
                "indexed-inference-proof-digest", ""
            ))) == 64,
            f"{label} proof did not traverse the source-owned indexed "
            "checker",
        )
    require(
        normal_proof_execution.get("indexed-inference-state-digest")
        == compressed_proof_execution.get(
            "indexed-inference-state-digest"
        )
        and normal_proof_execution.get(
            "indexed-inference-occurrence-digest"
        ) != compressed_proof_execution.get(
            "indexed-inference-occurrence-digest"
        )
        and normal_proof_execution.get(
            "indexed-inference-proof-digest"
        ) != compressed_proof_execution.get(
            "indexed-inference-proof-digest"
        ),
        "normal and compressed encodings did not preserve one semantic "
        "database state with distinct proof-occurrence provenance",
    )
    source_composition_fields = (
        "indexed-inference-state-digest",
        "indexed-inference-occurrence-digest",
        "indexed-inference-proof-digest",
        "indexed-inference-source-digest",
        "indexed-inference-sources",
        "indexed-inference-skipped-sources",
        "indexed-inference-source-max-depth",
        "indexed-inference-effects",
        "indexed-inference-objects",
        "indexed-inference-constants",
        "indexed-inference-variables",
    )
    for (
        label,
        dynamic,
        generated,
        expected_sources,
        expected_skips,
    ) in (
        (
            "basic include",
            include_basic_execution,
            generated_include_basic_execution,
            "2",
            "0",
        ),
        (
            "completed duplicate include",
            include_duplicate_execution,
            generated_include_duplicate_execution,
            "2",
            "1",
        ),
    ):
        require(
            dynamic.get("gll-decision") == "accepted"
            and dynamic.get("glr-decision") == "accepted"
            and dynamic.get("occurrence-fold-committed") == "1"
            and dynamic.get("indexed-inference-committed") == "1"
            and dynamic.get("indexed-inference-sources")
            == expected_sources
            and dynamic.get("indexed-inference-skipped-sources")
            == expected_skips
            and dynamic.get("indexed-inference-source-max-depth") == "2"
            and len(str(dynamic.get(
                "indexed-inference-source-digest", ""
            ))) == 64
            and generated.get("outcome") == "accepted"
            and generated.get("occurrence-fold-committed") == "1"
            and generated.get("indexed-inference-committed") == "1"
            and all(
                generated.get(field) == dynamic.get(field)
                for field in source_composition_fields
            ),
            f"{label} did not preserve the authenticated source DAG "
            "and transactional checker state through dynamic and AOT "
            "execution",
        )
    require(
        include_basic_execution.get("indexed-inference-source-digest")
        != include_duplicate_execution.get(
            "indexed-inference-source-digest"
        ),
        "source-DAG identity erased a completed duplicate include "
        "occurrence",
    )
    for label, dynamic, generated in (
        (
            "normal unknown",
            unknown_normal_execution,
            generated_unknown_normal_execution,
        ),
        (
            "compressed unknown",
            unknown_compressed_execution,
            generated_unknown_compressed_execution,
        ),
    ):
        require(
            dynamic.get("gll-decision") == "accepted"
            and dynamic.get("glr-decision") == "accepted"
            and dynamic.get("occurrence-fold-committed") == "1"
            and dynamic.get("indexed-inference-committed") == "1"
            and dynamic.get("indexed-inference-proof-steps") == "1"
            and dynamic.get(
                "indexed-inference-rule-applications"
            ) == "0"
            and dynamic.get("indexed-inference-unknown-steps") == "1"
            and generated.get("outcome") == "accepted"
            and generated.get("occurrence-fold-committed") == "1"
            and generated.get("indexed-inference-committed") == "1"
            and generated.get("indexed-inference-proof-steps") == "1"
            and generated.get(
                "indexed-inference-rule-applications"
            ) == "0"
            and generated.get("indexed-inference-unknown-steps") == "1"
            and generated.get("indexed-inference-state-digest")
            == dynamic.get("indexed-inference-state-digest")
            and generated.get("indexed-inference-proof-digest")
            == dynamic.get("indexed-inference-proof-digest"),
            f"{label} proof did not preserve the authored unknown "
            "occurrence through dynamic and AOT checking",
        )
    proof_fields = (
        "occurrence-fold-plan-digest",
        "occurrence-fold-steps",
        "occurrence-fold-values",
        "occurrence-fold-trace-digest",
        "indexed-inference-state-digest",
        "indexed-inference-occurrence-digest",
        "indexed-inference-effects",
        "indexed-inference-scope-opens",
        "indexed-inference-scope-closes",
        "indexed-inference-scope-completions",
        "indexed-inference-proof-steps",
        "indexed-inference-rule-applications",
        "indexed-inference-unknown-steps",
        "indexed-inference-proof-digest",
        "indexed-inference-rules",
    )
    for label, dynamic, generated in (
        (
            "normal",
            normal_proof_execution,
            generated_normal_proof_execution,
        ),
        (
            "compressed",
            compressed_proof_execution,
            generated_compressed_proof_execution,
        ),
    ):
        require(
            generated.get("outcome") == "accepted"
            and generated.get("occurrence-fold-committed") == "1"
            and generated.get("indexed-inference-committed") == "1"
            and generated.get("occurrence-fold-mutation-killed") == "1"
            and all(
                generated.get(field) == dynamic.get(field)
                for field in proof_fields
            ),
            f"AOT {label} proof execution changed its source-derived "
            "checker occurrences",
        )
    require(
        generated_execution.get("program-digest")
        == zero_guard_execution.get("cursor-program-digest")
        and generated_execution.get("outcome")
        == zero_guard_execution.get("cursor-program-outcome")
        and generated_execution.get("trace-digest")
        == zero_guard_execution.get("cursor-program-trace-digest")
        and generated_execution.get("tokens")
        == zero_guard_execution.get("cursor-program-tokens")
        and generated_execution.get("shifts")
        == zero_guard_execution.get("cursor-program-shifts")
        and generated_execution.get("reductions")
        == zero_guard_execution.get("cursor-program-reductions")
        and generated_execution.get("source-passes") == "1"
        and zero_guard_execution.get("cursor-program-source-passes") == "0"
        and generated_execution.get("mutation-killed") == "1",
        "generated native cursor changed the staged recognition trace",
    )
    require(
        generated_event_execution.get("program-digest")
        == generated_execution.get("program-digest")
        and generated_event_execution.get("outcome")
        == generated_execution.get("outcome")
        and generated_event_execution.get("trace-digest")
        == generated_execution.get("trace-digest")
        and generated_event_execution.get("tokens")
        == generated_execution.get("tokens")
        and generated_event_execution.get("shifts")
        == generated_execution.get("shifts")
        == generated_event_execution.get("event-shifts")
        and generated_event_execution.get("reductions")
        == generated_execution.get("reductions")
        == generated_event_execution.get("event-reductions")
        and generated_event_execution.get("source-passes") == "1"
        and generated_event_execution.get("execution-mode") == "events"
        and generated_event_execution.get("event-accepts") == "1"
        and generated_event_execution.get(
            "event-consumer-mutation-killed"
        ) == "1"
        and generated_event_execution.get("mutation-killed") == "1",
        "proof-relevant cursor events changed the staged recognition "
        "trace or escaped their source-identity audit",
    )
    require(
        generated_fold_execution.get("execution-mode")
        == "occurrence-fold"
        and generated_fold_execution.get("program-digest")
        == generated_execution.get("program-digest")
        and generated_fold_execution.get("outcome")
        == generated_execution.get("outcome")
        and generated_fold_execution.get("trace-digest")
        == generated_execution.get("trace-digest")
        and generated_fold_execution.get("occurrence-fold-plan-digest")
        == zero_guard_execution.get("occurrence-fold-plan-digest")
        and generated_fold_execution.get("occurrence-fold-roles")
        == zero_guard_execution.get("occurrence-fold-roles")
        and generated_fold_execution.get("occurrence-fold-nodes")
        == zero_guard_execution.get("occurrence-fold-nodes")
        and generated_fold_execution.get("occurrence-fold-operations")
        == zero_guard_execution.get("occurrence-fold-operations")
        and generated_fold_execution.get(
            "occurrence-fold-terminal-bindings"
        ) == zero_guard_execution.get(
            "occurrence-fold-terminal-bindings"
        )
        and generated_fold_execution.get(
            "occurrence-fold-production-bindings"
        ) == zero_guard_execution.get(
            "occurrence-fold-production-bindings"
        )
        and generated_fold_execution.get("occurrence-fold-steps")
        == zero_guard_execution.get("occurrence-fold-steps")
        and generated_fold_execution.get(
            "occurrence-fold-shift-steps"
        ) == zero_guard_execution.get("occurrence-fold-shift-steps")
        and generated_fold_execution.get(
            "occurrence-fold-reduce-steps"
        ) == zero_guard_execution.get("occurrence-fold-reduce-steps")
        and generated_fold_execution.get("occurrence-fold-values")
        == zero_guard_execution.get("occurrence-fold-values")
        and generated_fold_execution.get(
            "occurrence-fold-max-pending-values"
        ) == zero_guard_execution.get(
            "occurrence-fold-max-pending-values"
        )
        and generated_fold_execution.get("occurrence-fold-committed")
        == "1"
        and generated_fold_execution.get(
            "occurrence-fold-trace-digest"
        ) == zero_guard_execution.get("occurrence-fold-trace-digest")
        and generated_fold_execution.get(
            "occurrence-fold-mutation-killed"
        ) == "1",
        "AOT occurrence-fold plan changed its authored identities or "
        "proof-occurrence trace",
    )
    require(
        generated_fold_execution.get(
            "indexed-checker-effect-plan-digest"
        ) == zero_guard_execution.get(
            "indexed-checker-effect-plan-digest"
        )
        and generated_fold_execution.get(
            "indexed-checker-effect-operations"
        ) == zero_guard_execution.get(
            "indexed-checker-effect-operations"
        )
        and generated_fold_execution.get(
            "indexed-checker-effect-kinds"
        ) == zero_guard_execution.get(
            "indexed-checker-effect-kinds"
        )
        and generated_fold_execution.get(
            "indexed-checker-effect-mutation-killed"
        ) == "1"
        and generated_fold_execution.get(
            "indexed-checker-policy-mutation-killed"
        ) == "1",
        "AOT indexed-checker effect or policy selection changed its "
        "GSLT-derived composition",
    )
    require(
        generated_fold_execution.get("indexed-inference-committed")
        == "1"
        and generated_fold_execution.get(
            "indexed-inference-state-digest"
        ) == zero_guard_execution.get(
            "indexed-inference-state-digest"
        )
        and generated_fold_execution.get(
            "indexed-inference-occurrence-digest"
        ) == zero_guard_execution.get(
            "indexed-inference-occurrence-digest"
        )
        and generated_fold_execution.get("indexed-inference-effects")
        == zero_guard_execution.get("indexed-inference-effects")
        and generated_fold_execution.get("indexed-inference-atoms")
        == zero_guard_execution.get("indexed-inference-atoms")
        and generated_fold_execution.get("indexed-inference-objects")
        == zero_guard_execution.get("indexed-inference-objects"),
        "AOT execution changed the transactional indexed inference state",
    )
    require(
        generated_scoped_fold_execution.get("outcome") == "accepted"
        and generated_scoped_fold_execution.get(
            "occurrence-fold-steps"
        ) == scoped_fold_execution.get("occurrence-fold-steps")
        and generated_scoped_fold_execution.get(
            "occurrence-fold-shift-steps"
        ) == scoped_fold_execution.get(
            "occurrence-fold-shift-steps"
        )
        and generated_scoped_fold_execution.get(
            "occurrence-fold-reduce-steps"
        ) == scoped_fold_execution.get(
            "occurrence-fold-reduce-steps"
        )
        and generated_scoped_fold_execution.get(
            "occurrence-fold-values"
        ) == scoped_fold_execution.get("occurrence-fold-values")
        and generated_scoped_fold_execution.get(
            "occurrence-fold-max-pending-values"
        ) == scoped_fold_execution.get(
            "occurrence-fold-max-pending-values"
        )
        and generated_scoped_fold_execution.get(
            "occurrence-fold-committed"
        ) == "1"
        and generated_scoped_fold_execution.get(
            "occurrence-fold-trace-digest"
        ) == scoped_fold_execution.get(
            "occurrence-fold-trace-digest"
        ),
        "AOT occurrence fold changed scope-boundary operation order",
    )
    require(
        generated_scoped_fold_execution.get(
            "indexed-inference-state-digest"
        ) == scoped_fold_execution.get(
            "indexed-inference-state-digest"
        )
        and generated_scoped_fold_execution.get(
            "indexed-inference-occurrence-digest"
        ) == scoped_fold_execution.get(
            "indexed-inference-occurrence-digest"
        )
        and generated_scoped_fold_execution.get(
            "indexed-inference-effects"
        ) == scoped_fold_execution.get("indexed-inference-effects")
        and generated_scoped_fold_execution.get(
            "indexed-inference-scope-opens"
        ) == scoped_fold_execution.get(
            "indexed-inference-scope-opens"
        )
        and generated_scoped_fold_execution.get(
            "indexed-inference-scope-closes"
        ) == scoped_fold_execution.get(
            "indexed-inference-scope-closes"
        )
        and generated_scoped_fold_execution.get(
            "indexed-inference-scope-completions"
        ) == scoped_fold_execution.get(
            "indexed-inference-scope-completions"
        )
        and generated_scoped_fold_execution.get(
            "indexed-inference-scope-depth"
        ) == "0",
        "AOT execution changed scoped inference-state restoration",
    )
    nested_scope_fields = (
        "occurrence-fold-steps",
        "occurrence-fold-shift-steps",
        "occurrence-fold-reduce-steps",
        "occurrence-fold-values",
        "occurrence-fold-trace-digest",
        "indexed-inference-state-digest",
        "indexed-inference-occurrence-digest",
        "indexed-inference-effects",
        "indexed-inference-scope-opens",
        "indexed-inference-scope-closes",
        "indexed-inference-scope-completions",
        "indexed-inference-scope-depth",
    )
    require(
        generated_nested_scoped_fold_execution.get("outcome")
        == "accepted"
        and generated_nested_scoped_fold_execution.get(
            "occurrence-fold-committed"
        ) == "1"
        and generated_nested_scoped_fold_execution.get(
            "indexed-inference-committed"
        ) == "1"
        and all(
            generated_nested_scoped_fold_execution.get(field)
            == nested_scoped_fold_execution.get(field)
            for field in nested_scope_fields
        ),
        "AOT execution changed nested scope occurrence accounting",
    )
    require(
        generated_rejected_fold_execution.get("outcome") == "rejected"
        and generated_rejected_fold_execution.get(
            "occurrence-fold-committed"
        ) == "0"
        and generated_rejected_fold_execution.get(
            "occurrence-fold-trace-digest"
        ) == "",
        "AOT occurrence fold published a rejected source transaction",
    )
    require(
        generated_rejected_fold_execution.get(
            "indexed-inference-committed"
        ) == "0"
        and generated_rejected_fold_execution.get(
            "indexed-inference-state-digest"
        ) == ""
        and generated_rejected_fold_execution.get(
            "indexed-inference-occurrence-digest"
        ) == "",
        "AOT execution published a rejected inference-state transaction",
    )
    require(
        zero_guard_execution.get("cursor-program-value-programs") == "21"
        and zero_guard_execution.get(
            "cursor-program-value-programs-complete"
        ) == "1"
        and zero_guard_execution.get(
            "cursor-program-semantic-required-sources"
        ) == "5"
        and zero_guard_execution.get(
            "cursor-program-value-program-conflicts"
        ) == "5"
        and zero_guard_execution.get(
            "cursor-program-direct-semantic-agreement"
        ) == "1"
        and zero_guard_execution.get(
            "cursor-program-hybrid-semantic-agreement"
        ) == "1"
        and zero_guard_execution.get(
            "cursor-program-hybrid-prepared-fallback-runs"
        ) == "0"
        and generated_execution.get("semantic-complete") == "1"
        and generated_execution.get("semantic-required-sources") == "5"
        and generated_execution.get("semantic-result")
        == zero_guard_execution.get("gll-result")
        == zero_guard_execution.get("glr-result"),
        "build-time Metamath semantic specialization changed the "
        "general-engine result",
    )
    require(
        mutated_liveness.terminals
        == semantic_liveness.terminals
        | {lexical_languages["comment"].terminal}
        | whitespace_terminals
        and zero_guard_execution.get(
            "cursor-program-semantic-mutations-killed"
        ) == "5",
        "semantic-liveness mutation did not restore the observable "
        "comment dependency",
    )
    require(
        len(slr_lexical_overlaps)
        == int(zero_guard_execution[
            "cursor-certificate-token-prefix-overlaps"
        ]),
        "independent lexical-prefix/SLR analysis differs from native "
        "cursor certificate",
    )
    zero_guard_execution["diagnostic-lr1-token-prefix-overlaps"] = str(
        len(lr1_lexical_overlaps)
    )
    zero_guard_execution["semantic-dependency-mutation"] = (
        f"{render(dependency_mutation_label)}:"
        f"{dependency_mutation_slot}"
    )
    zero_guard_execution["diagnostic-lr1-overlap-details"] = [
        f"{render(overlap.left)} <> {render(overlap.right)} "
        + "on "
        + (
            "<empty>"
            if not overlap.witness
            else " ".join(
                f"U+{scalar:04X}" for scalar in overlap.witness
            )
        )
        + " in states "
        + ",".join(map(str, overlap.states))
        for overlap in lr1_lexical_overlaps
    ]
    return zero_guard_execution


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", type=Path, required=True)
    parser.add_argument("--plan-binary", type=Path, required=True)
    parser.add_argument("--abi-binary", type=Path, required=True)
    parser.add_argument("--exec-binary", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--generated-c-output", type=Path)
    arguments = parser.parse_args()
    paths = (
        arguments.chart_binary,
        arguments.plan_binary,
        arguments.abi_binary,
        arguments.exec_binary,
    )
    if any(not path.resolve().is_file() for path in paths):
        raise GateFailure("required native test binary is absent")
    if not arguments.petta_root.resolve().is_dir():
        raise GateFailure("PeTTa foundation checkout is absent")

    synthetic_common_prefix_gate()
    synthetic_recursion_and_failure_gate()
    execution = real_metamath_gate(
        arguments.chart_binary.resolve(),
        arguments.plan_binary.resolve(),
        arguments.abi_binary.resolve(),
        arguments.exec_binary.resolve(),
        arguments.petta_root.resolve(),
        arguments.generated_c_output,
    )
    print(
        "PASS: transparent ParserPack inlining preserves semantic actions, "
        "source fibers, recursion, and determinizes the Metamath grammar; "
        "cursor certificate "
        f"{execution['cursor-certificate-failure-mask']} "
        "(non-prefix-free "
        f"{execution['cursor-certificate-non-prefix-free-tokens']}, "
        "state-coactive token-prefix overlaps "
        f"{execution['cursor-certificate-token-prefix-overlaps']})"
        "; canonical-LR(1) coactive overlaps "
        f"{execution['diagnostic-lr1-token-prefix-overlaps']}"
    )
    details = execution["diagnostic-lr1-overlap-details"]
    assert isinstance(details, list)
    for detail in details:
        print("LR1 lexical overlap:", detail)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, InlineFailure) as error:
        raise SystemExit(f"FAIL: {error}")
