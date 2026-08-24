#!/usr/bin/env python3

import argparse
from pathlib import Path


MUTATIONS = {
    "dangling-bound-variable": (
        "if (index >= binder_depth)",
        "if (index > binder_depth)",
    ),
    "context-index-shift": (
        "atom_int(arena, (int64_t)(binder_depth + (uint64_t)i))",
        "atom_int(arena, (int64_t)i)",
    ),
    "binder-freshness": (
        "if (freshness == PATTERN_SCAN_FOUND)",
        "if (false)",
    ),
    "expected-formation-phase": (
        "result.phase = CETTA_PRIME_REGULAR_PATTERN_PHASE_EXPECTED_FORMATION;",
        "result.phase = CETTA_PRIME_REGULAR_PATTERN_PHASE_TERM_SYNTAX;",
    ),
    "malformed-list-as-budget": (
        "if (arguments_status == PATTERN_LIST_BUDGET)\n"
        "                status = CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;",
        "if (arguments_status != PATTERN_LIST_OK)\n"
        "                status = CETTA_PRIME_REGULAR_PATTERN_BUDGET_EXHAUSTED;",
    ),
    "syntax-lexical-resolution": (
        "atom_eq(binding->key, key)",
        "false",
    ),
    "syntax-matcher-binder": (
        "if (syntax->kind == ATOM_VAR) return REGULAR_TERM_NAME_MATCHER;",
        "if (syntax->kind == ATOM_VAR) return REGULAR_TERM_NAME_INVALID;",
    ),
    "syntax-multibinder-nesting": (
        "for (size_t i = count; i > 0u; i--)\n"
        "        nested = regular_term_pattern_lambda(arena, nested);",
        "for (size_t i = count; i > 1u; i--)\n"
        "        nested = regular_term_pattern_lambda(arena, nested);",
    ),
    "syntax-telescope-arity": (
        "(types_count != 1u && types_count != names_count)",
        "(types_count != 1u || types_count != names_count)",
    ),
    "syntax-expected-phase": (
        "result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_EXPECTED_FORMATION;",
        "result.phase = CETTA_PRIME_REGULAR_TERM_PHASE_TERM_SYNTAX;",
    ),
    "syntax-index-scope": (
        "if (!regular_term_binding_has_index(environment, direct_index))",
        "if (false && !regular_term_binding_has_index(environment, direct_index))",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("mutation", choices=sorted(MUTATIONS))
    args = parser.parse_args()

    text = args.source.read_text(encoding="utf-8")
    old, new = MUTATIONS[args.mutation]
    count = text.count(old)
    if count != 1:
        raise SystemExit(
            f"expected one {args.mutation} mutation site, found {count}")
    args.destination.write_text(text.replace(old, new, 1), encoding="utf-8")


if __name__ == "__main__":
    main()
