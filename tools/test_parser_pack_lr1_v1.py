#!/usr/bin/env python3
"""Adversarial canaries for the canonical LR(1) ParserPack analyzer."""

from __future__ import annotations

from parser_pack_lr1_v1 import (
    AnalysisFailure,
    Grammar,
    GrammarSymbol,
    Production,
    analyze_lr1,
    summary_json,
)
from gslt2parse_schema_v1 import Symbol


class GateFailure(RuntimeError):
    pass


def terminal(name: str) -> GrammarSymbol:
    return GrammarSymbol(True, Symbol(name))


def nonterminal(name: str) -> GrammarSymbol:
    return GrammarSymbol(False, Symbol(name))


def production(
    identity: str, lhs: str, *rhs: GrammarSymbol
) -> Production:
    return Production(
        Symbol(identity), Symbol(lhs), tuple(rhs), Symbol("canary-action")
    )


def require(condition: bool, label: str) -> None:
    if not condition:
        raise GateFailure(label)


def require_failure(grammar: Grammar, label: str) -> None:
    try:
        analyze_lr1(grammar)
    except AnalysisFailure:
        return
    raise GateFailure(label)


def main() -> int:
    checks = 0

    simple = analyze_lr1(Grammar(
        Symbol("S"),
        (production("simple", "S", terminal("a")),),
    ))
    require(
        summary_json(simple) == {
            "accepts": 1,
            "conflict_cells": 0,
            "conflicts": 0,
            "gotos": 1,
            "grammar_digest":
                "65326d8ac89d5e504da9d1c68272664505be8af95c4fffdcc68899db2ce82bfd",
            "max_state_items": 2,
            "nonterminals": 1,
            "productions": 1,
            "reachable_productions": 1,
            "reductions": 1,
            "shifts": 1,
            "states": 3,
            "table_digest":
                "62bbbb15444192c29ae6b7d0c2408ecea76d40e68e9d6af336db80a76b9a0e8b",
            "terminals": 1,
        },
        "single-production canonical table changed",
    )
    checks += 1

    nullable = analyze_lr1(Grammar(
        Symbol("S"),
        (
            production("nullable-start", "S", nonterminal("A"), terminal("b")),
            production("nullable-empty", "A"),
            production("nullable-value", "A", terminal("a")),
        ),
    ))
    require(
        nullable.conflict_len == 0
        and nullable.state_len == 5
        and nullable.reduce_len == 3,
        "nullable FIRST propagation changed",
    )
    checks += 1

    lr1_not_slr = analyze_lr1(Grammar(
        Symbol("S"),
        (
            production(
                "assignment", "S", nonterminal("L"),
                terminal("="), nonterminal("R")
            ),
            production("result", "S", nonterminal("R")),
            production("deref", "L", terminal("*"), nonterminal("R")),
            production("identifier", "L", terminal("id")),
            production("reference", "R", nonterminal("L")),
        ),
    ))
    require(
        lr1_not_slr.conflict_len == 0 and lr1_not_slr.state_len == 14,
        "canonical LR(1)-but-not-SLR grammar was rejected",
    )
    checks += 1

    ambiguous = analyze_lr1(Grammar(
        Symbol("S"),
        (
            production(
                "concatenate", "S", nonterminal("S"), nonterminal("S")
            ),
            production("atom", "S", terminal("a")),
        ),
    ))
    require(
        ambiguous.conflict_cell_len == 1
        and ambiguous.conflict_len == 1
        and {kind for kind, _ in ambiguous.conflicts[0].actions}
        == {"reduce", "shift"},
        "ambiguous grammar did not expose its shift/reduce conflict",
    )
    checks += 1

    reduce_reduce = analyze_lr1(Grammar(
        Symbol("S"),
        (
            production("choose-a", "S", nonterminal("A")),
            production("choose-b", "S", nonterminal("B")),
            production("a", "A", terminal("x")),
            production("b", "B", terminal("x")),
        ),
    ))
    require(
        reduce_reduce.conflict_cell_len == 1
        and reduce_reduce.conflict_len == 1
        and {kind for kind, _ in reduce_reduce.conflicts[0].actions}
        == {"reduce"},
        "reduce/reduce ambiguity was not retained",
    )
    checks += 1

    unreachable_ambiguity = analyze_lr1(Grammar(
        Symbol("S"),
        (
            production("live", "S", terminal("a")),
            production(
                "dead-concatenate", "U", nonterminal("U"), nonterminal("U")
            ),
            production("dead-atom", "U", terminal("u")),
        ),
    ))
    require(
        unreachable_ambiguity.production_len == 3
        and unreachable_ambiguity.reachable_production_len == 1
        and unreachable_ambiguity.conflict_len == 0,
        "unreachable productions affected the live LR table",
    )
    checks += 1

    epsilon_start = analyze_lr1(Grammar(
        Symbol("S"), (production("empty", "S"),)
    ))
    require(
        epsilon_start.state_len == 2
        and epsilon_start.reduce_len == 1
        and epsilon_start.conflict_len == 0,
        "empty start production changed",
    )
    checks += 1

    require_failure(
        Grammar(
            Symbol("S"),
            (production("open", "S", nonterminal("missing")),),
        ),
        "open grammar was accepted",
    )
    checks += 1

    print(f"(ParserPackLR1V1CanarySummary {checks} {checks} 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
