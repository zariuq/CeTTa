#!/usr/bin/env python3
"""Canonical LR(1) analysis for a proof-carrying ParserPack composite plan."""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
from dataclasses import dataclass
from hashlib import sha256
import json
from pathlib import Path
from typing import Iterable, TypeAlias

from gslt2parse_schema_v1 import SExpr, Symbol, parse_sexprs, render


class AnalysisFailure(RuntimeError):
    pass


@dataclass(frozen=True, slots=True)
class EndOfInput:
    pass


EOF = EndOfInput()
GrammarIdentity: TypeAlias = SExpr | EndOfInput


@dataclass(frozen=True, slots=True)
class GrammarSymbol:
    terminal: bool
    identity: SExpr


@dataclass(frozen=True, slots=True)
class Production:
    identity: SExpr
    lhs: SExpr
    rhs: tuple[GrammarSymbol, ...]
    action: SExpr


@dataclass(frozen=True, slots=True)
class Grammar:
    start: SExpr
    productions: tuple[Production, ...]


@dataclass(frozen=True, slots=True)
class LR1Conflict:
    state: int
    lookahead: GrammarIdentity
    actions: tuple[tuple[str, int], ...]


@dataclass(frozen=True, slots=True)
class LRActionCell:
    state: int
    lookahead: GrammarIdentity
    actions: tuple[tuple[str, int], ...]


@dataclass(frozen=True, slots=True)
class LR1Summary:
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
        raise AnalysisFailure(f"{context}: expected one canonical term")
    if render(terms[0]) != text:
        raise AnalysisFailure(f"{context}: term is not canonical")
    return terms[0]


def item_list(term: SExpr) -> tuple[GrammarSymbol, ...]:
    result: list[GrammarSymbol] = []
    current = term
    while current != Symbol("pp-items-nil"):
        cons = app(current, "pp-items-cons", 2)
        if cons is None:
            raise AnalysisFailure("ParserPack production has a malformed item list")
        terminal = app(cons[1], "pp-terminal", 1)
        nonterminal = app(cons[1], "pp-nonterminal", 1)
        if terminal is not None:
            result.append(GrammarSymbol(True, terminal[1]))
        elif nonterminal is not None:
            result.append(GrammarSymbol(False, nonterminal[1]))
        else:
            raise AnalysisFailure("ParserPack production has an unknown item")
        current = cons[2]
    return tuple(result)


def production(term: SExpr) -> Production:
    packed = app(term, "pp-production", 4)
    if packed is None:
        raise AnalysisFailure("unknown ParserPack production term")
    return Production(
        identity=packed[1],
        lhs=packed[2],
        rhs=item_list(packed[3]),
        action=packed[4],
    )


def parse_abi_stream(stream: bytes) -> Grammar:
    try:
        lines = stream.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise AnalysisFailure("ParserPack ABI stream is not UTF-8") from error
    if not lines or lines[0] != "parser-pack-abi-v1" or lines[-1] != "end":
        raise AnalysisFailure("ParserPack ABI stream has malformed framing")
    start: SExpr | None = None
    productions: list[Production] = []
    for line_number, line in enumerate(lines[1:-1], start=2):
        fields = line.split("\t")
        if len(fields) == 2 and fields[0] == "start":
            if start is not None:
                raise AnalysisFailure("ParserPack ABI repeats its start state")
            start = one_term(fields[1], f"ParserPack start line {line_number}")
        elif len(fields) == 2 and fields[0] == "production":
            productions.append(production(one_term(
                fields[1], f"ParserPack production line {line_number}"
            )))
    if start is None or not productions:
        raise AnalysisFailure("ParserPack ABI omits its start or productions")
    identities = [item.identity for item in productions]
    if len(identities) != len(set(identities)):
        raise AnalysisFailure("ParserPack ABI repeats a production identity")
    return Grammar(start, tuple(productions))


def parse_plan_stream(output: str) -> dict[str, object]:
    lines = output.splitlines()
    if (
        not lines
        or lines[0] not in {
            "parser-pack-lexical-plan-v1",
            "parser-pack-positive-guard-plan-v1",
            "parser-pack-guarded-lexical-plan-v1",
        }
        or lines[-1] != "end"
    ):
        raise AnalysisFailure("composite plan stream has malformed framing")
    result: dict[str, object] = {
        "lexical_entries": [],
        "guard_entries": [],
        "guarded_lexical_entries": [],
    }
    for line_number, line in enumerate(lines[1:-1], start=2):
        fields = line.split("\t")
        if fields[0] == "lexical-entry" and len(fields) == 6:
            entries = result["lexical_entries"]
            assert isinstance(entries, list)
            entries.append({
                "tag_index": int(fields[1]),
                "state_id": int(fields[2]),
                "terminal_index": int(fields[3]),
                "tag": one_term(fields[4], f"lexical tag line {line_number}"),
                "state": one_term(fields[5], f"lexical state line {line_number}"),
            })
        elif fields[0] == "guard-entry" and len(fields) == 10:
            entries = result["guard_entries"]
            assert isinstance(entries, list)
            entries.append({
                "state_id": int(fields[1]),
                "terminal_id": int(fields[2]),
                "production_id": int(fields[3]),
                "state_is_extension": bool(int(fields[4])),
                "owner": one_term(fields[5], f"guard owner line {line_number}"),
                "state": one_term(fields[6], f"guard state line {line_number}"),
                "tag": one_term(fields[7], f"guard tag line {line_number}"),
                "body": one_term(fields[8], f"guard body line {line_number}"),
                "production": production(one_term(
                    fields[9], f"guard production line {line_number}"
                )),
            })
        elif fields[0] == "guarded-lexical-entry" and len(fields) == 9:
            entries = result["guarded_lexical_entries"]
            assert isinstance(entries, list)
            entries.append({
                "tag_index": int(fields[1]),
                "state_id": int(fields[2]),
                "terminal_index": int(fields[3]),
                "guard_entry_index": int(fields[4]),
                "tag": one_term(
                    fields[5], f"guarded lexical tag line {line_number}"
                ),
                "state": one_term(
                    fields[6], f"guarded lexical state line {line_number}"
                ),
                "guard_state": one_term(
                    fields[7], f"guarded lexical guard state line {line_number}"
                ),
                "guard_body": one_term(
                    fields[8], f"guarded lexical guard body line {line_number}"
                ),
            })
        elif len(fields) == 2 and fields[0] not in result:
            result[fields[0]] = fields[1]
        elif fields[0] in {
            "lexical-tag", "guard-tag", "guarded-tag",
            "guard-entry-evidence"
        }:
            continue
        else:
            raise AnalysisFailure(
                f"malformed composite plan record at line {line_number}"
            )
    return result


def composite_grammar(base: Grammar, plan: dict[str, object]) -> Grammar:
    lexical_entries = plan.get("lexical_entries")
    guard_entries = plan.get("guard_entries")
    guarded_entries = plan.get("guarded_lexical_entries")
    if (
        not isinstance(lexical_entries, list)
        or not isinstance(guard_entries, list)
        or not isinstance(guarded_entries, list)
    ):
        raise AnalysisFailure("composite plan omits its extension inventories")
    substitutions: dict[SExpr, SExpr] = {}
    for raw_entry in lexical_entries:
        if not isinstance(raw_entry, dict):
            raise AnalysisFailure("malformed lexical extension")
        state = raw_entry.get("state")
        tag = raw_entry.get("tag")
        if not isinstance(state, (Symbol, tuple)) or not isinstance(tag, (Symbol, tuple)):
            raise AnalysisFailure("lexical extension has malformed identities")
        terminal = (Symbol("pp-span-terminal"), tag)
        if state in substitutions and substitutions[state] != terminal:
            raise AnalysisFailure("lexical state has conflicting terminal projections")
        substitutions[state] = terminal
    for raw_entry in guarded_entries:
        if not isinstance(raw_entry, dict):
            raise AnalysisFailure("malformed guarded lexical extension")
        state = raw_entry.get("state")
        tag = raw_entry.get("tag")
        if not isinstance(state, (Symbol, tuple)) or not isinstance(tag, (Symbol, tuple)):
            raise AnalysisFailure(
                "guarded lexical extension has malformed identities"
            )
        terminal = (Symbol("pp-guarded-span-terminal"), tag)
        if state in substitutions and substitutions[state] != terminal:
            raise AnalysisFailure(
                "guarded lexical state has a conflicting terminal projection"
            )
        substitutions[state] = terminal
    productions: list[Production] = []
    for item in base.productions:
        rhs = tuple(
            GrammarSymbol(True, substitutions[symbol.identity])
            if not symbol.terminal and symbol.identity in substitutions
            else symbol
            for symbol in item.rhs
        )
        productions.append(Production(item.identity, item.lhs, rhs, item.action))
    for raw_entry in guard_entries:
        if not isinstance(raw_entry, dict) or not isinstance(
            raw_entry.get("production"), Production
        ):
            raise AnalysisFailure("malformed guard production extension")
        productions.append(raw_entry["production"])
    identities = [item.identity for item in productions]
    if len(identities) != len(set(identities)):
        raise AnalysisFailure("composite plan repeats a production identity")
    return Grammar(base.start, tuple(productions))


def prune_grammar(grammar: Grammar) -> Grammar:
    by_lhs: dict[SExpr, list[Production]] = defaultdict(list)
    for item in grammar.productions:
        by_lhs[item.lhs].append(item)
    reachable: set[SExpr] = {grammar.start}
    queue = deque((grammar.start,))
    while queue:
        lhs = queue.popleft()
        if lhs not in by_lhs:
            raise AnalysisFailure(
                f"composite grammar remains open at {render(lhs)}"
            )
        for item in by_lhs[lhs]:
            for symbol in item.rhs:
                if not symbol.terminal and symbol.identity not in reachable:
                    reachable.add(symbol.identity)
                    queue.append(symbol.identity)
    return Grammar(
        grammar.start,
        tuple(item for item in grammar.productions if item.lhs in reachable),
    )


def identity_text(identity: GrammarIdentity) -> str:
    return "$end" if identity is EOF else render(identity)


def grammar_digest(grammar: Grammar) -> str:
    lines = [f"start\t{render(grammar.start)}"]
    for item in grammar.productions:
        rhs = " ".join(
            ("T:" if symbol.terminal else "N:") + render(symbol.identity)
            for symbol in item.rhs
        )
        lines.append(
            f"production\t{render(item.identity)}\t{render(item.lhs)}\t"
            f"{rhs}\t{render(item.action)}"
        )
    return sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def nullable_first(
    grammar: Grammar,
) -> tuple[set[SExpr], dict[SExpr, set[SExpr]]]:
    nonterminals = {item.lhs for item in grammar.productions}
    nullable: set[SExpr] = set()
    first: dict[SExpr, set[SExpr]] = {identity: set() for identity in nonterminals}
    changed = True
    while changed:
        changed = False
        for item in grammar.productions:
            all_nullable = True
            for symbol in item.rhs:
                if symbol.terminal:
                    before = len(first[item.lhs])
                    first[item.lhs].add(symbol.identity)
                    changed |= len(first[item.lhs]) != before
                    all_nullable = False
                    break
                before = len(first[item.lhs])
                first[item.lhs].update(first[symbol.identity])
                changed |= len(first[item.lhs]) != before
                if symbol.identity not in nullable:
                    all_nullable = False
                    break
            if all_nullable and item.lhs not in nullable:
                nullable.add(item.lhs)
                changed = True
    return nullable, first


def sequence_first(
    sequence: Iterable[GrammarSymbol],
    lookahead: GrammarIdentity,
    nullable: set[SExpr],
    first: dict[SExpr, set[SExpr]],
) -> set[GrammarIdentity]:
    result: set[GrammarIdentity] = set()
    for symbol in sequence:
        if symbol.terminal:
            result.add(symbol.identity)
            return result
        result.update(first[symbol.identity])
        if symbol.identity not in nullable:
            return result
    result.add(lookahead)
    return result


def analyze_lr1(unpruned: Grammar) -> LR1Summary:
    grammar = prune_grammar(unpruned)
    nullable, first = nullable_first(grammar)
    productions = list(grammar.productions)
    augmented_lhs = (Symbol("pp-lr1-augmented"), grammar.start)
    augmented_identity = (Symbol("pp-lr1-start"), grammar.start)
    augmented = Production(
        augmented_identity,
        augmented_lhs,
        (GrammarSymbol(False, grammar.start),),
        Symbol("pp-lr1-accept"),
    )
    augmented_index = len(productions)
    productions.append(augmented)
    by_lhs: dict[SExpr, list[int]] = defaultdict(list)
    for index, item in enumerate(productions):
        by_lhs[item.lhs].append(index)

    Item: TypeAlias = tuple[int, int, GrammarIdentity]

    def closure(seed: Iterable[Item]) -> frozenset[Item]:
        result = set(seed)
        queue = deque(seed)
        while queue:
            production_index, dot, lookahead = queue.popleft()
            item = productions[production_index]
            if dot >= len(item.rhs) or item.rhs[dot].terminal:
                continue
            next_nonterminal = item.rhs[dot].identity
            lookaheads = sequence_first(
                item.rhs[dot + 1:], lookahead, nullable, first
            )
            for child_index in by_lhs[next_nonterminal]:
                for child_lookahead in lookaheads:
                    child = (child_index, 0, child_lookahead)
                    if child not in result:
                        result.add(child)
                        queue.append(child)
        return frozenset(result)

    initial = closure(((augmented_index, 0, EOF),))
    states: list[frozenset[Item]] = [initial]
    state_ids: dict[frozenset[Item], int] = {initial: 0}
    transitions: dict[tuple[int, GrammarSymbol], int] = {}
    queue = deque((0,))
    while queue:
        state_index = queue.popleft()
        grouped: dict[GrammarSymbol, list[Item]] = defaultdict(list)
        for production_index, dot, lookahead in states[state_index]:
            item = productions[production_index]
            if dot < len(item.rhs):
                grouped[item.rhs[dot]].append(
                    (production_index, dot + 1, lookahead)
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

    actions: dict[tuple[int, GrammarIdentity], set[tuple[str, int]]] = defaultdict(set)
    goto_len = 0
    for (state_index, symbol), target in transitions.items():
        if symbol.terminal:
            actions[(state_index, symbol.identity)].add(("shift", target))
        else:
            goto_len += 1
    for state_index, state in enumerate(states):
        for production_index, dot, lookahead in state:
            item = productions[production_index]
            if dot != len(item.rhs):
                continue
            if production_index == augmented_index and lookahead is EOF:
                actions[(state_index, EOF)].add(("accept", 0))
            elif production_index != augmented_index:
                actions[(state_index, lookahead)].add(("reduce", production_index))

    conflicts: list[LR1Conflict] = []
    action_cells: list[LRActionCell] = []
    shift_len = 0
    reduce_len = 0
    accept_len = 0
    conflict_len = 0
    for (state_index, lookahead), values in sorted(
        actions.items(), key=lambda item: (item[0][0], identity_text(item[0][1]))
    ):
        ordered = tuple(sorted(values))
        action_cells.append(LRActionCell(state_index, lookahead, ordered))
        shift_len += sum(kind == "shift" for kind, _ in ordered)
        reduce_len += sum(kind == "reduce" for kind, _ in ordered)
        accept_len += sum(kind == "accept" for kind, _ in ordered)
        if len(ordered) > 1:
            conflict_len += len(ordered) - 1
            conflicts.append(LR1Conflict(state_index, lookahead, ordered))

    digest_payload = {
        "actions": [
            [state, identity_text(lookahead), sorted(values)]
            for (state, lookahead), values in sorted(
                actions.items(),
                key=lambda item: (item[0][0], identity_text(item[0][1])),
            )
        ],
        "grammar": grammar_digest(grammar),
        "states": [
            [
                [production_index, dot, identity_text(lookahead)]
                for production_index, dot, lookahead in sorted(
                    state,
                    key=lambda value: (
                        value[0], value[1], identity_text(value[2])
                    ),
                )
            ]
            for state in states
        ],
        "transitions": [
            [state, "T" if symbol.terminal else "N", render(symbol.identity), target]
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
    table_digest = sha256(json.dumps(
        digest_payload, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")).hexdigest()
    terminals = {
        symbol.identity
        for item in grammar.productions
        for symbol in item.rhs
        if symbol.terminal
    }
    nonterminals = {item.lhs for item in grammar.productions}
    return LR1Summary(
        grammar_digest=grammar_digest(grammar),
        table_digest=table_digest,
        production_len=len(unpruned.productions),
        reachable_production_len=len(grammar.productions),
        nonterminal_len=len(nonterminals),
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


def summary_json(summary: LR1Summary) -> dict[str, object]:
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--abi", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--show-conflicts", action="store_true")
    arguments = parser.parse_args()
    base = parse_abi_stream(arguments.abi.read_bytes())
    plan = parse_plan_stream(arguments.plan.read_text(encoding="utf-8"))
    summary = analyze_lr1(composite_grammar(base, plan))
    result = summary_json(summary)
    if arguments.show_conflicts:
        result["conflict_details"] = [
            {
                "actions": list(conflict.actions),
                "lookahead": identity_text(conflict.lookahead),
                "state": conflict.state,
            }
            for conflict in summary.conflicts
        ]
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AnalysisFailure as error:
        raise SystemExit(f"FAIL: {error}")
