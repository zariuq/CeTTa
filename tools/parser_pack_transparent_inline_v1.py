#!/usr/bin/env python3
"""Inline acyclic ParserPack scaffolding while preserving semantic actions.

The syntax compiler gives every SIR subexpression a distinct ``pp-sub``
nonterminal.  That representation is useful as a proof-relevant oracle, but
it can introduce LR conflicts before two otherwise deterministic branches
have consumed their shared prefix.  This pass removes only acyclic
``pp-sub`` states.  Named definitions, relational states, terminals, and
recursive ``pp-sub`` strongly-connected components remain explicit.
"""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
from typing import Iterable

from gslt2parse_schema_v1 import (
    SExpr,
    StringLiteral,
    Symbol,
    Variable,
    parse_sexprs,
    render,
)
from parser_pack_lr1_v1 import (
    EOF,
    Grammar,
    GrammarIdentity,
    GrammarSymbol,
    LRActionCell,
    LR1Conflict,
    Production,
    analyze_lr1,
    composite_grammar,
    grammar_digest,
    identity_text,
    nullable_first,
    parse_abi_stream,
    parse_plan_stream,
    prune_grammar,
    summary_json,
)


class InlineFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class InlineEntry:
    production: Production
    source_trace: tuple[SExpr, ...]


@dataclass(frozen=True, slots=True)
class InlineResult:
    grammar: Grammar
    entries: tuple[InlineEntry, ...]
    cyclic_states: tuple[SExpr, ...]
    removed_production_len: int


@dataclass(frozen=True, slots=True)
class SLRSummary:
    grammar_digest: str
    table_digest: str
    production_len: int
    reachable_production_len: int
    nonterminal_len: int
    terminal_len: int
    state_len: int
    shift_len: int
    goto_len: int
    reduce_len: int
    accept_len: int
    conflict_cell_len: int
    conflict_len: int
    max_state_items: int
    conflicts: tuple[LR1Conflict, ...]
    action_cells: tuple[LRActionCell, ...]


@dataclass(frozen=True, slots=True)
class ABIEnvelope:
    source_digest: str
    compiler_digest: str
    environment_digest: str
    pack_digest: str
    start: SExpr
    closure: str
    productions: tuple[SExpr, ...]
    classes: tuple[SExpr, ...]
    production_evidence: tuple[tuple[SExpr, SExpr, SExpr], ...]
    class_evidence: tuple[tuple[SExpr, SExpr, SExpr], ...]


@dataclass(frozen=True, slots=True)
class _Expansion:
    rhs: tuple[GrammarSymbol, ...]
    action: SExpr
    trace: tuple[SExpr, ...]


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
        raise InlineFailure(f"{context}: expected one canonical term")
    if render(terms[0]) != text:
        raise InlineFailure(f"{context}: term is not canonical")
    return terms[0]


def qindex(term: SExpr) -> int:
    value = 0
    current = term
    while current != Symbol("q-zero"):
        successor = app(current, "q-succ", 1)
        if successor is None:
            raise InlineFailure("semantic action contains a malformed slot")
        value += 1
        current = successor[1]
    return value


def qterm(value: int) -> SExpr:
    if value < 0:
        raise InlineFailure("semantic action slot cannot be negative")
    result: SExpr = Symbol("q-zero")
    for _ in range(value):
        result = (Symbol("q-succ"), result)
    return result


def _map_action_list(
    terms: SExpr,
    transform,
    *,
    context: str,
) -> SExpr:
    if terms == Symbol("pa-nil"):
        return terms
    node = app(terms, "pa-cons", 2)
    if node is None:
        raise InlineFailure(f"{context}: malformed semantic-action list")
    return (
        Symbol("pa-cons"),
        transform(node[1]),
        _map_action_list(node[2], transform, context=context),
    )


def shift_action(action: SExpr, offset: int) -> SExpr:
    slot = app(action, "pa-slot", 1)
    if slot is not None:
        return (Symbol("pa-slot"), qterm(qindex(slot[1]) + offset))
    if app(action, "pa-const", 1) is not None:
        return action
    apply = app(action, "pa-apply", 2)
    if apply is not None:
        return (
            Symbol("pa-apply"),
            apply[1],
            _map_action_list(
                apply[2],
                lambda item: shift_action(item, offset),
                context="action shift",
            ),
        )
    raise InlineFailure("unknown ParserPack semantic action")


def substitute_action(
    action: SExpr,
    replacements: tuple[SExpr, ...],
) -> SExpr:
    slot = app(action, "pa-slot", 1)
    if slot is not None:
        index = qindex(slot[1])
        if index >= len(replacements):
            raise InlineFailure(
                "semantic action slot lies outside its production"
            )
        return replacements[index]
    if app(action, "pa-const", 1) is not None:
        return action
    apply = app(action, "pa-apply", 2)
    if apply is not None:
        return (
            Symbol("pa-apply"),
            apply[1],
            _map_action_list(
                apply[2],
                lambda item: substitute_action(item, replacements),
                context="action substitution",
            ),
        )
    raise InlineFailure("unknown ParserPack semantic action")


def is_transparent_state(identity: SExpr) -> bool:
    return app(identity, "pp-sub", 2) is not None


def _cyclic_transparent_states(
    grammar: Grammar,
) -> tuple[SExpr, ...]:
    nodes = {
        production.lhs
        for production in grammar.productions
        if is_transparent_state(production.lhs)
    }
    dependencies: dict[SExpr, set[SExpr]] = {
        node: set() for node in nodes
    }
    for production in grammar.productions:
        if production.lhs not in nodes:
            continue
        for symbol in production.rhs:
            if (
                not symbol.terminal
                and is_transparent_state(symbol.identity)
            ):
                dependencies[production.lhs].add(symbol.identity)
                nodes.add(symbol.identity)
    for node in nodes:
        dependencies.setdefault(node, set())

    next_index = 0
    indices: dict[SExpr, int] = {}
    lows: dict[SExpr, int] = {}
    stack: list[SExpr] = []
    on_stack: set[SExpr] = set()
    cyclic: set[SExpr] = set()

    def visit(node: SExpr) -> None:
        nonlocal next_index
        indices[node] = next_index
        lows[node] = next_index
        next_index += 1
        stack.append(node)
        on_stack.add(node)
        for child in sorted(dependencies[node], key=render):
            if child not in indices:
                visit(child)
                lows[node] = min(lows[node], lows[child])
            elif child in on_stack:
                lows[node] = min(lows[node], indices[child])
        if lows[node] != indices[node]:
            return
        component: list[SExpr] = []
        while True:
            child = stack.pop()
            on_stack.remove(child)
            component.append(child)
            if child == node:
                break
        if len(component) > 1 or node in dependencies[node]:
            cyclic.update(component)

    for node in sorted(nodes, key=render):
        if node not in indices:
            visit(node)
    return tuple(sorted(cyclic, key=render))


def _trace_term(trace: Iterable[SExpr]) -> SExpr:
    values = tuple(
        (
            Symbol("pp-inline-source-occurrence"),
            Symbol(_deep_term_digest(identity)),
        )
        for identity in trace
    )

    def balanced(begin: int, end: int) -> SExpr:
        length = end - begin
        if length == 0:
            return Symbol("pp-inline-trace-nil")
        if length == 1:
            return (Symbol("pp-inline-trace-one"), values[begin])
        middle = begin + length // 2
        return (
            Symbol("pp-inline-trace-append"),
            balanced(begin, middle),
            balanced(middle, end),
        )

    return balanced(0, len(values))


def _deep_render(term: SExpr) -> str:
    """Render canonical S-expressions without consuming Python call depth."""

    output: list[str] = []
    work: list[tuple[str, object]] = [("term", term)]
    while work:
        kind, value = work.pop()
        if kind == "text":
            output.append(value)  # type: ignore[arg-type]
            continue
        if isinstance(value, (Symbol, Variable, StringLiteral, int)):
            output.append(render(value))  # type: ignore[arg-type]
            continue
        if not isinstance(value, tuple):
            raise InlineFailure(
                f"non-S-expression value in canonical term: {value!r}"
            )
        output.append("(")
        work.append(("text", ")"))
        actions: list[tuple[str, object]] = []
        for index, item in enumerate(value):
            if index:
                actions.append(("text", " "))
            actions.append(("term", item))
        work.extend(reversed(actions))
    return "".join(output)


def _deep_term_digest(term: SExpr) -> str:
    return sha256(_deep_render(term).encode("utf-8")).hexdigest()


def _trace_digest(trace: Iterable[SExpr]) -> str:
    return _trace_reference_digest(
        _deep_term_digest(identity) for identity in trace
    )


def _trace_reference_digest(references: Iterable[str]) -> str:
    digest = sha256()
    digest.update(b"ParserPackTransparentInlineTraceV1\0")
    for reference in references:
        try:
            identity_digest = bytes.fromhex(reference)
        except ValueError as error:
            raise InlineFailure(
                "source occurrence reference is not a SHA-256 digest"
            ) from error
        if len(identity_digest) != 32:
            raise InlineFailure(
                "source occurrence reference is not a SHA-256 digest"
            )
        digest.update(len(identity_digest).to_bytes(8, "big"))
        digest.update(identity_digest)
    return digest.hexdigest()


def _trace_references(term: SExpr) -> tuple[str, ...]:
    if term == Symbol("pp-inline-trace-nil"):
        return ()
    one = app(term, "pp-inline-trace-one", 1)
    if one is not None:
        occurrence = app(one[1], "pp-inline-source-occurrence", 1)
        if occurrence is None or not isinstance(occurrence[1], Symbol):
            raise InlineFailure(
                "inline certificate has a malformed source occurrence"
            )
        return (occurrence[1].text,)
    append = app(term, "pp-inline-trace-append", 2)
    if append is None:
        raise InlineFailure("inline certificate has a malformed trace")
    return (
        _trace_references(append[1])
        + _trace_references(append[2])
    )


def _items_term(rhs: Iterable[GrammarSymbol]) -> SExpr:
    result: SExpr = Symbol("pp-items-nil")
    values = tuple(rhs)
    for symbol in reversed(values):
        item = (
            Symbol("pp-terminal")
            if symbol.terminal
            else Symbol("pp-nonterminal"),
            symbol.identity,
        )
        result = (Symbol("pp-items-cons"), item, result)
    return result


def production_term(production: Production) -> SExpr:
    return (
        Symbol("pp-production"),
        production.identity,
        production.lhs,
        _items_term(production.rhs),
        production.action,
    )


def inline_transparent_substates(
    grammar: Grammar,
    *,
    max_paths_per_state: int = 10000,
    max_productions: int = 100000,
) -> InlineResult:
    if max_paths_per_state <= 0 or max_productions <= 0:
        raise InlineFailure("inline limits must be positive")
    by_lhs: dict[SExpr, list[Production]] = defaultdict(list)
    for production in grammar.productions:
        by_lhs[production.lhs].append(production)
    for productions in by_lhs.values():
        productions.sort(key=lambda item: render(production_term(item)))

    cyclic = set(_cyclic_transparent_states(grammar))
    memo: dict[SExpr, tuple[_Expansion, ...]] = {}

    def expand_symbol(
        symbol: GrammarSymbol,
        stack: tuple[SExpr, ...],
    ) -> tuple[_Expansion, ...]:
        if (
            symbol.terminal
            or not is_transparent_state(symbol.identity)
            or symbol.identity in cyclic
        ):
            return (
                _Expansion(
                    (symbol,),
                    (Symbol("pa-slot"), Symbol("q-zero")),
                    (),
                ),
            )
        state = symbol.identity
        if state in memo:
            return memo[state]
        if state in stack:
            raise InlineFailure(
                "acyclic-state classifier missed a recursive pp-sub state"
            )
        definitions = by_lhs.get(state)
        if not definitions:
            raise InlineFailure(
                f"transparent state has no production: {render(state)}"
            )
        paths: list[_Expansion] = []
        for production in definitions:
            for expansion in expand_production(
                production, stack + (state,)
            ):
                paths.append(
                    _Expansion(
                        expansion.rhs,
                        expansion.action,
                        (production.identity,) + expansion.trace,
                    )
                )
                if len(paths) > max_paths_per_state:
                    raise InlineFailure(
                        "transparent state expansion exceeds its path limit"
                    )
        result = tuple(paths)
        memo[state] = result
        return result

    def expand_production(
        production: Production,
        stack: tuple[SExpr, ...] = (),
    ) -> tuple[_Expansion, ...]:
        combinations: list[
            tuple[tuple[GrammarSymbol, ...], tuple[SExpr, ...], tuple[SExpr, ...]]
        ] = [((), (), ())]
        for symbol in production.rhs:
            next_combinations: list[
                tuple[
                    tuple[GrammarSymbol, ...],
                    tuple[SExpr, ...],
                    tuple[SExpr, ...],
                ]
            ] = []
            for prefix, actions, trace in combinations:
                for expansion in expand_symbol(symbol, stack):
                    next_combinations.append(
                        (
                            prefix + expansion.rhs,
                            actions
                            + (shift_action(expansion.action, len(prefix)),),
                            trace + expansion.trace,
                        )
                    )
                    if len(next_combinations) > max_paths_per_state:
                        raise InlineFailure(
                            "production expansion exceeds its path limit"
                        )
            combinations = next_combinations
        return tuple(
            _Expansion(
                rhs,
                substitute_action(production.action, actions),
                trace,
            )
            for rhs, actions, trace in combinations
        )

    entries: list[InlineEntry] = []
    removed_production_len = 0
    for production in grammar.productions:
        if (
            is_transparent_state(production.lhs)
            and production.lhs not in cyclic
        ):
            removed_production_len += 1
            continue
        for expansion in expand_production(production):
            trace = (production.identity,) + expansion.trace
            normalized = Production(
                (
                    Symbol("pp-inline-label"),
                    Symbol(_trace_digest(trace)),
                ),
                production.lhs,
                expansion.rhs,
                expansion.action,
            )
            entries.append(InlineEntry(normalized, trace))
            if len(entries) > max_productions:
                raise InlineFailure(
                    "normalized ParserPack exceeds its production limit"
                )
    entries.sort(key=lambda item: render(production_term(item.production)))
    labels = [render(entry.production.identity) for entry in entries]
    if len(labels) != len(set(labels)):
        raise InlineFailure("normalized ParserPack repeats a production label")
    result_grammar = Grammar(
        grammar.start,
        tuple(entry.production for entry in entries),
    )
    return InlineResult(
        result_grammar,
        tuple(entries),
        tuple(sorted(cyclic, key=render)),
        removed_production_len,
    )


def analyze_slr(unpruned: Grammar) -> SLRSummary:
    grammar = prune_grammar(unpruned)
    productions = list(grammar.productions)
    augmented = Production(
        (Symbol("pp-slr-start"), grammar.start),
        (Symbol("pp-slr-augmented"), grammar.start),
        (GrammarSymbol(False, grammar.start),),
        Symbol("pp-slr-accept"),
    )
    augmented_index = len(productions)
    productions.append(augmented)
    by_lhs: dict[SExpr, list[int]] = defaultdict(list)
    for index, production in enumerate(productions):
        by_lhs[production.lhs].append(index)

    nullable, first = nullable_first(grammar)
    nonterminals = {production.lhs for production in productions}
    follow: dict[SExpr, set[GrammarIdentity]] = {
        identity: set() for identity in nonterminals
    }
    follow[grammar.start].add(EOF)
    changed = True
    while changed:
        changed = False
        for production in productions:
            trailer = set(follow[production.lhs])
            for symbol in reversed(production.rhs):
                if symbol.terminal:
                    trailer = {symbol.identity}
                    continue
                before = len(follow[symbol.identity])
                follow[symbol.identity].update(trailer)
                changed |= len(follow[symbol.identity]) != before
                if symbol.identity in nullable:
                    trailer.update(first[symbol.identity])
                else:
                    trailer = set(first[symbol.identity])

    Item = tuple[int, int]

    def closure(seed: Iterable[Item]) -> frozenset[Item]:
        result = set(seed)
        queue = deque(seed)
        while queue:
            production_index, dot = queue.popleft()
            production = productions[production_index]
            if (
                dot >= len(production.rhs)
                or production.rhs[dot].terminal
            ):
                continue
            for child_index in by_lhs[production.rhs[dot].identity]:
                child = (child_index, 0)
                if child not in result:
                    result.add(child)
                    queue.append(child)
        return frozenset(result)

    initial = closure(((augmented_index, 0),))
    states: list[frozenset[Item]] = [initial]
    state_ids = {initial: 0}
    transitions: dict[tuple[int, GrammarSymbol], int] = {}
    queue = deque((0,))
    while queue:
        state_index = queue.popleft()
        grouped: dict[GrammarSymbol, list[Item]] = defaultdict(list)
        for production_index, dot in states[state_index]:
            production = productions[production_index]
            if dot < len(production.rhs):
                grouped[production.rhs[dot]].append(
                    (production_index, dot + 1)
                )
        for symbol in sorted(
            grouped,
            key=lambda value: (not value.terminal, render(value.identity)),
        ):
            target = closure(grouped[symbol])
            target_index = state_ids.get(target)
            if target_index is None:
                target_index = len(states)
                state_ids[target] = target_index
                states.append(target)
                queue.append(target_index)
            transitions[(state_index, symbol)] = target_index

    actions: dict[
        tuple[int, GrammarIdentity], set[tuple[str, int]]
    ] = defaultdict(set)
    goto_len = 0
    for (state_index, symbol), target in transitions.items():
        if symbol.terminal:
            actions[(state_index, symbol.identity)].add(("shift", target))
        else:
            goto_len += 1
    for state_index, state in enumerate(states):
        for production_index, dot in state:
            production = productions[production_index]
            if dot != len(production.rhs):
                continue
            if production_index == augmented_index:
                actions[(state_index, EOF)].add(("accept", 0))
            else:
                for lookahead in follow[production.lhs]:
                    actions[(state_index, lookahead)].add(
                        ("reduce", production_index)
                    )

    conflicts: list[LR1Conflict] = []
    action_cells: list[LRActionCell] = []
    shift_len = 0
    reduce_len = 0
    accept_len = 0
    conflict_len = 0
    for (state_index, lookahead), values in sorted(
        actions.items(),
        key=lambda item: (item[0][0], identity_text(item[0][1])),
    ):
        ordered = tuple(sorted(values))
        action_cells.append(LRActionCell(state_index, lookahead, ordered))
        shift_len += sum(kind == "shift" for kind, _ in ordered)
        reduce_len += sum(kind == "reduce" for kind, _ in ordered)
        accept_len += sum(kind == "accept" for kind, _ in ordered)
        if len(ordered) > 1:
            conflict_len += len(ordered) - 1
            conflicts.append(
                LR1Conflict(state_index, lookahead, ordered)
            )

    table_payload = {
        "actions": [
            [state, identity_text(lookahead), sorted(values)]
            for (state, lookahead), values in sorted(
                actions.items(),
                key=lambda item: (
                    item[0][0],
                    identity_text(item[0][1]),
                ),
            )
        ],
        "grammar": grammar_digest(grammar),
        "states": [
            [list(item) for item in sorted(state)]
            for state in states
        ],
        "transitions": [
            [
                state,
                "T" if symbol.terminal else "N",
                render(symbol.identity),
                target,
            ]
            for (state, symbol), target in sorted(
                transitions.items(),
                key=lambda item: (
                    item[0][0],
                    not item[0][1].terminal,
                    render(item[0][1].identity),
                ),
            )
        ],
    }
    table_digest = sha256(
        json.dumps(
            table_payload,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    ).hexdigest()
    terminals = {
        symbol.identity
        for production in grammar.productions
        for symbol in production.rhs
        if symbol.terminal
    }
    return SLRSummary(
        grammar_digest=grammar_digest(grammar),
        table_digest=table_digest,
        production_len=len(unpruned.productions),
        reachable_production_len=len(grammar.productions),
        nonterminal_len=len(
            {production.lhs for production in grammar.productions}
        ),
        terminal_len=len(terminals),
        state_len=len(states),
        shift_len=shift_len,
        goto_len=goto_len,
        reduce_len=reduce_len,
        accept_len=accept_len,
        conflict_cell_len=len(conflicts),
        conflict_len=conflict_len,
        max_state_items=max(map(len, states)),
        conflicts=tuple(conflicts),
        action_cells=tuple(action_cells),
    )


def slr_summary_json(summary: SLRSummary) -> dict[str, object]:
    return {
        "accepts": summary.accept_len,
        "conflict_cells": summary.conflict_cell_len,
        "conflicts": summary.conflict_len,
        "gotos": summary.goto_len,
        "grammar_digest": summary.grammar_digest,
        "max_state_items": summary.max_state_items,
        "nonterminals": summary.nonterminal_len,
        "productions": summary.production_len,
        "reachable_productions": summary.reachable_production_len,
        "reductions": summary.reduce_len,
        "shifts": summary.shift_len,
        "states": summary.state_len,
        "table_digest": summary.table_digest,
        "terminals": summary.terminal_len,
    }


def parse_abi_envelope(stream: bytes) -> ABIEnvelope:
    try:
        lines = stream.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise InlineFailure("ParserPack ABI is not UTF-8") from error
    if not lines or lines[0] != "parser-pack-abi-v1" or lines[-1] != "end":
        raise InlineFailure("ParserPack ABI has malformed framing")
    expected_metadata = (
        "source-digest",
        "compiler-digest",
        "environment-digest",
        "pack-digest",
        "start",
        "closure",
    )
    metadata: dict[str, str] = {}
    cursor = 1
    for name in expected_metadata:
        if cursor >= len(lines) - 1:
            raise InlineFailure("ParserPack ABI omits metadata")
        fields = lines[cursor].split("\t")
        cursor += 1
        if len(fields) != 2 or fields[0] != name:
            raise InlineFailure(f"ParserPack ABI expected {name}")
        metadata[name] = fields[1]
    productions: list[SExpr] = []
    classes: list[SExpr] = []
    production_evidence: list[tuple[SExpr, SExpr, SExpr]] = []
    class_evidence: list[tuple[SExpr, SExpr, SExpr]] = []
    phase = 0
    for line_number, line in enumerate(
        lines[cursor:-1], start=cursor + 1
    ):
        fields = line.split("\t")
        if len(fields) == 2 and fields[0] == "production" and phase <= 0:
            productions.append(
                one_term(fields[1], f"ABI production line {line_number}")
            )
        elif (
            len(fields) == 2
            and fields[0] == "class-clause"
            and phase <= 1
        ):
            phase = 1
            classes.append(
                one_term(fields[1], f"ABI class line {line_number}")
            )
        elif (
            len(fields) == 4
            and fields[0] == "production-evidence"
            and phase <= 2
        ):
            phase = 2
            production_evidence.append(
                tuple(
                    one_term(
                        value,
                        f"ABI production evidence line {line_number}",
                    )
                    for value in fields[1:]
                )
            )
        elif (
            len(fields) == 4
            and fields[0] == "class-evidence"
            and phase <= 3
        ):
            phase = 3
            class_evidence.append(
                tuple(
                    one_term(
                        value,
                        f"ABI class evidence line {line_number}",
                    )
                    for value in fields[1:]
                )
            )
        else:
            raise InlineFailure(
                f"malformed ABI record at line {line_number}"
            )
    start = one_term(metadata["start"], "ABI start")
    if metadata["closure"] not in {"closed", "partial"}:
        raise InlineFailure("ParserPack ABI has invalid closure metadata")
    result = ABIEnvelope(
        metadata["source-digest"],
        metadata["compiler-digest"],
        metadata["environment-digest"],
        metadata["pack-digest"],
        start,
        metadata["closure"],
        tuple(productions),
        tuple(classes),
        tuple(production_evidence),
        tuple(class_evidence),
    )
    if parser_pack_digest(result.productions, result.classes) != result.pack_digest:
        raise InlineFailure("ParserPack ABI artifact digest is stale")
    return result


def _term_list(values: Iterable[SExpr]) -> SExpr:
    result: SExpr = Symbol("nil")
    terms = tuple(values)
    for value in reversed(terms):
        result = (Symbol("cons"), value, result)
    return result


def parser_pack_digest(
    productions: Iterable[SExpr],
    classes: Iterable[SExpr],
) -> str:
    production_terms = tuple(
        sorted(productions, key=_deep_render)
    )
    class_terms = tuple(sorted(classes, key=_deep_render))
    pack = (
        Symbol("parser-pack-v1"),
        _term_list(production_terms),
        _term_list(class_terms),
    )
    return sha256(_deep_render(pack).encode("utf-8")).hexdigest()


def _composed_compiler_digest(envelope: ABIEnvelope) -> str:
    own_digest = sha256(Path(__file__).read_bytes()).hexdigest()
    payload = (
        "ParserPackTransparentInlineCompilerV1\n"
        f"{envelope.compiler_digest}\n"
        f"{own_digest}\n"
    )
    return sha256(payload.encode("ascii")).hexdigest()


def emit_normalized_abi(
    source_stream: bytes,
    result: InlineResult,
) -> bytes:
    envelope = parse_abi_envelope(source_stream)
    productions = tuple(
        production_term(entry.production) for entry in result.entries
    )
    pack_digest = parser_pack_digest(productions, envelope.classes)
    compiler_digest = _composed_compiler_digest(envelope)
    owner: SExpr = (
        Symbol("pp-transparent-inline-v1"),
        Symbol(envelope.pack_digest),
    )
    output = [
        "parser-pack-abi-v1",
        f"source-digest\t{envelope.source_digest}",
        f"compiler-digest\t{compiler_digest}",
        f"environment-digest\t{envelope.environment_digest}",
        f"pack-digest\t{pack_digest}",
        f"start\t{_deep_render(envelope.start)}",
        f"closure\t{envelope.closure}",
    ]
    for production in productions:
        output.append(f"production\t{_deep_render(production)}")
    for clause in sorted(envelope.classes, key=_deep_render):
        output.append(f"class-clause\t{_deep_render(clause)}")
    for entry, production in zip(result.entries, productions, strict=True):
        answer: SExpr = (
            Symbol("compile-pack-production"),
            owner,
            production,
        )
        certificate: SExpr = (
            Symbol("pp-transparent-inline-certificate-v1"),
            Symbol(envelope.source_digest),
            Symbol(envelope.compiler_digest),
            Symbol(envelope.environment_digest),
            Symbol(envelope.pack_digest),
            len(entry.source_trace),
            Symbol(_trace_digest(entry.source_trace)),
            _trace_term(entry.source_trace),
        )
        output.append(
            "production-evidence\t"
            + "\t".join(
                map(_deep_render, (production, answer, certificate))
            )
        )
    for artifact, answer, certificate in envelope.class_evidence:
        output.append(
            "class-evidence\t"
            + "\t".join(
                map(_deep_render, (artifact, answer, certificate))
            )
        )
    output.append("end")
    return ("\n".join(output) + "\n").encode("utf-8")


def verify_normalized_abi_evidence(
    source_stream: bytes,
    normalized_stream: bytes,
) -> None:
    """Check the content-addressed source fibers in a normalized ABI."""

    source = parse_abi_envelope(source_stream)
    normalized = parse_abi_envelope(normalized_stream)
    if (
        normalized.source_digest != source.source_digest
        or normalized.environment_digest != source.environment_digest
        or normalized.start != source.start
        or normalized.closure != source.closure
        or normalized.compiler_digest != _composed_compiler_digest(source)
    ):
        raise InlineFailure("normalized ABI metadata is not source-derived")
    if (
        {_deep_render(item) for item in normalized.classes}
        != {_deep_render(item) for item in source.classes}
        or {
            tuple(_deep_render(value) for value in item)
            for item in normalized.class_evidence
        }
        != {
            tuple(_deep_render(value) for value in item)
            for item in source.class_evidence
        }
    ):
        raise InlineFailure("normalized ABI changed lexical class evidence")

    source_grammar = parse_abi_stream(source_stream)
    source_references = {
        _deep_term_digest(production.identity)
        for production in source_grammar.productions
    }
    production_keys = {
        _deep_render(production): production
        for production in normalized.productions
    }
    if len(production_keys) != len(normalized.productions):
        raise InlineFailure("normalized ABI repeats a production")
    observed: set[str] = set()
    owner: SExpr = (
        Symbol("pp-transparent-inline-v1"),
        Symbol(source.pack_digest),
    )
    for artifact, answer, certificate in normalized.production_evidence:
        key = _deep_render(artifact)
        if key not in production_keys or key in observed:
            raise InlineFailure(
                "normalized production evidence is missing or duplicated"
            )
        observed.add(key)
        if answer != (
            Symbol("compile-pack-production"),
            owner,
            artifact,
        ):
            raise InlineFailure(
                "normalized production evidence has the wrong owner"
            )
        certified = app(
            certificate, "pp-transparent-inline-certificate-v1", 7
        )
        if certified is None:
            raise InlineFailure(
                "normalized production evidence has a malformed certificate"
            )
        expected_metadata: tuple[SExpr, ...] = (
            Symbol(source.source_digest),
            Symbol(source.compiler_digest),
            Symbol(source.environment_digest),
            Symbol(source.pack_digest),
        )
        if certified[1:5] != expected_metadata:
            raise InlineFailure(
                "normalized production certificate changed source identity"
            )
        if not isinstance(certified[5], int) or certified[5] <= 0:
            raise InlineFailure(
                "normalized production certificate has invalid trace length"
            )
        if not isinstance(certified[6], Symbol):
            raise InlineFailure(
                "normalized production certificate has invalid trace digest"
            )
        references = _trace_references(certified[7])
        if (
            len(references) != certified[5]
            or any(
                reference not in source_references
                for reference in references
            )
            or _trace_reference_digest(references) != certified[6].text
        ):
            raise InlineFailure(
                "normalized production certificate has an invalid source fiber"
            )
        packed = app(artifact, "pp-production", 4)
        label = None if packed is None else app(
            packed[1], "pp-inline-label", 1
        )
        if (
            label is None
            or not isinstance(label[1], Symbol)
            or label[1] != certified[6]
        ):
            raise InlineFailure(
                "normalized production label is not its source-fiber digest"
            )
    if observed != set(production_keys):
        raise InlineFailure("normalized ABI omits production evidence")


def verify_reproducible_normalization(
    source_stream: bytes,
    normalized_stream: bytes,
) -> InlineResult:
    """Recompile and require byte-identical output as a development gate."""

    verify_normalized_abi_evidence(source_stream, normalized_stream)
    result = inline_transparent_substates(parse_abi_stream(source_stream))
    if emit_normalized_abi(source_stream, result) != normalized_stream:
        raise InlineFailure("normalized ABI is not a reproducible compilation")
    return result


def _action_instructions(action: SExpr) -> tuple[SExpr, ...]:
    slot = app(action, "pa-slot", 1)
    if slot is not None:
        qindex(slot[1])
        return ((Symbol("pbc-push-slot"), slot[1]),)
    constant = app(action, "pa-const", 1)
    if constant is not None:
        return ((Symbol("pbc-push-const"), constant[1]),)
    apply_action = app(action, "pa-apply", 2)
    if apply_action is None:
        raise InlineFailure("unknown ParserPack semantic action")
    instructions: list[SExpr] = []
    arity = 0
    current = apply_action[2]
    while current != Symbol("pa-nil"):
        cons = app(current, "pa-cons", 2)
        if cons is None:
            raise InlineFailure("semantic action has a malformed argument list")
        instructions.extend(_action_instructions(cons[1]))
        arity += 1
        current = cons[2]
    instructions.append((
        Symbol("pbc-apply"),
        apply_action[1],
        qterm(arity),
    ))
    return tuple(instructions)


def _instruction_list(instructions: Iterable[SExpr]) -> SExpr:
    result: SExpr = Symbol("pbc-nil")
    values = tuple(instructions)
    for instruction in reversed(values):
        result = (Symbol("pbc-cons"), instruction, result)
    return result


def _static_action_compiler_digest(base_compiler_digest: str) -> str:
    if (
        len(base_compiler_digest) != 64
        or any(char not in "0123456789abcdef" for char in base_compiler_digest)
    ):
        raise InlineFailure("base action compiler digest is not SHA-256")
    own_digest = sha256(Path(__file__).read_bytes()).hexdigest()
    payload = (
        "ParserPackStaticActionCompilerV1\n"
        f"{base_compiler_digest}\n"
        f"{own_digest}\n"
    )
    return sha256(payload.encode("ascii")).hexdigest()


def emit_static_action_answers(
    source_stream: bytes,
    normalized_stream: bytes,
    base_compiler_digest: str,
) -> tuple[bytes, str]:
    """Stage normalized semantic actions into canonical bytecode answers."""

    verify_normalized_abi_evidence(source_stream, normalized_stream)
    source = parse_abi_envelope(source_stream)
    grammar = parse_abi_stream(normalized_stream)
    owner: SExpr = (
        Symbol("pp-transparent-inline-v1"),
        Symbol(source.pack_digest),
    )
    answers: list[str] = []
    for production in grammar.productions:
        instructions = _action_instructions(production.action)
        for instruction in instructions:
            pushed = app(instruction, "pbc-push-slot", 1)
            if pushed is not None and qindex(pushed[1]) >= len(production.rhs):
                raise InlineFailure(
                    "compiled semantic action reads outside its production"
                )
        answer: SExpr = (
            Symbol("compile-pack-action-program"),
            owner,
            production.identity,
            qterm(len(production.rhs)),
            production.action,
            _instruction_list(instructions),
        )
        answers.append(_deep_render(answer))
    if len(answers) != len(set(answers)):
        raise InlineFailure("static action compiler repeated an answer")
    output = ("\n".join(sorted(answers)) + "\n").encode("utf-8")
    return output, _static_action_compiler_digest(base_compiler_digest)


def _conflicts(
    values: Iterable[LR1Conflict],
) -> list[dict[str, object]]:
    return [
        {
            "actions": list(conflict.actions),
            "lookahead": identity_text(conflict.lookahead),
            "state": conflict.state,
        }
        for conflict in values
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--abi", type=Path, required=True)
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--emit-abi", type=Path)
    parser.add_argument("--show-conflicts", action="store_true")
    arguments = parser.parse_args()
    source_stream = arguments.abi.read_bytes()
    source = parse_abi_stream(source_stream)
    normalized = inline_transparent_substates(source)
    analyzed = normalized.grammar
    if arguments.plan:
        analyzed = composite_grammar(
            analyzed,
            parse_plan_stream(
                arguments.plan.read_text(encoding="utf-8")
            ),
        )
    slr = analyze_slr(analyzed)
    lr1 = analyze_lr1(analyzed)
    report: dict[str, object] = {
        "cyclic_transparent_states": len(normalized.cyclic_states),
        "normalized_productions": len(normalized.grammar.productions),
        "removed_transparent_productions":
            normalized.removed_production_len,
        "source_productions": len(source.productions),
        "slr": slr_summary_json(slr),
        "lr1": summary_json(lr1),
    }
    if arguments.show_conflicts:
        report["slr_conflict_details"] = _conflicts(slr.conflicts)
        report["lr1_conflict_details"] = _conflicts(lr1.conflicts)
    if arguments.emit_abi:
        emitted = emit_normalized_abi(source_stream, normalized)
        arguments.emit_abi.write_bytes(emitted)
        report["emitted_abi_sha256"] = sha256(emitted).hexdigest()
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except InlineFailure as error:
        raise SystemExit(f"FAIL: {error}")
