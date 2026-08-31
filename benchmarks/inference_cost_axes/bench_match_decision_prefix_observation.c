#include "match_decision.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    PREFIX_BENCH_CLAUSES = 64u,
    PREFIX_BENCH_LEAVES = 16u,
    PREFIX_BENCH_QUERY = 42u,
    PREFIX_BENCH_MAX_DEPTH = 24u,
};

static uint32_t parse_u32(
        const char *text, uint32_t fallback, uint32_t maximum) {
    if (!text || !*text)
        return fallback;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (!end || *end != '\0' || value == 0ul || value > maximum)
        return fallback;
    return (uint32_t)value;
}

static Atom *make_pattern(
        Arena *arena, Atom *select_head,
        Atom *row_head, Atom *zero, Atom *one, uint32_t code) {
    Atom *row_elements[PREFIX_BENCH_LEAVES + 1u] = {row_head};
    for (uint32_t leaf = 0u; leaf < PREFIX_BENCH_LEAVES; leaf++) {
        row_elements[leaf + 1u] =
            (code & (UINT32_C(1) << (leaf % 6u))) != 0u
                ? one : zero;
    }
    Atom *row = atom_expr(
        arena, row_elements, PREFIX_BENCH_LEAVES + 1u);
    return atom_expr2(arena, select_head, row);
}

static Atom *make_open_query(
        Arena *arena, Atom *nest_head, const char *name,
        uint32_t observed_prefix_depth) {
    Atom *body = atom_var(arena, name);
    for (uint32_t level = 0u;
         body && level < observed_prefix_depth; level++) {
        body = atom_expr2(arena, nest_head, body);
    }
    return body;
}

int main(int argc, char **argv) {
    const uint32_t depth = parse_u32(
        argc > 1 ? argv[1] : NULL, 12u, PREFIX_BENCH_MAX_DEPTH);
    const uint32_t iterations = parse_u32(
        argc > 2 ? argv[2] : NULL, 1000000u, UINT32_MAX);
    Arena persistent;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;

    arena_init(&persistent);
    arena_set_runtime_kind(
        &persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    space_init_with_universe(&space, &universe);

    Atom *select_head = atom_symbol(&persistent, "select-prefix");
    Atom *nest_head = atom_symbol(&persistent, "nest");
    Atom *row_head = atom_symbol(&persistent, "row");
    Atom *zero = atom_symbol(&persistent, "zero");
    Atom *one = atom_symbol(&persistent, "one");
    CettaMatchDecisionClause clauses[PREFIX_BENCH_CLAUSES] = {0};
    bool valid = select_head && nest_head && row_head && zero && one;
    for (uint32_t clause = 0u;
         valid && clause < PREFIX_BENCH_CLAUSES; clause++) {
        Atom *body = make_pattern(
            &persistent, select_head,
            row_head, zero, one,
            clause == PREFIX_BENCH_CLAUSES - 1u
                ? PREFIX_BENCH_QUERY : clause);
        for (uint32_t level = 0u; body && level < depth; level++)
            body = atom_expr2(&persistent, nest_head, body);
        clauses[clause] = (CettaMatchDecisionClause){body, clause};
        valid = clauses[clause].pattern != NULL;
    }

    const CettaMatchDecisionSemanticIdentity semantic_identity = {
        .language_id = 1u,
        .profile_id = 1u,
        .match_policy_id = 1u,
        .demand_policy_id = 1u,
        .presentation_identity = UINT64_C(0x505246584f425331),
        .compiler_identity = UINT64_C(0x505246584f425332),
    };
    CettaMatchDecision *decision = valid
        ? cetta_match_decision_compile(
              space_read_token(&space), semantic_identity,
              clauses, PREFIX_BENCH_CLAUSES,
              CETTA_MATCH_DECISION_CONJUNCTIVE,
              depth + 3u,
              cetta_match_decision_realization_from_process(),
              NULL, NULL)
        : NULL;
    Atom *query = clauses[PREFIX_BENCH_QUERY].pattern;
    Atom *open_query = make_open_query(
        &persistent, nest_head, "open-prefix", 1u);
    uint32_t middle_depth = depth > 2u ? depth / 2u : 2u;
    Atom *open_middle_query = make_open_query(
        &persistent, nest_head, "open-middle", middle_depth);
    Atom *open_deep_query = make_open_query(
        &persistent, nest_head, "open-deep", depth);
    Atom *absent_elements[] = {nest_head};
    Atom *absent_query = atom_expr(
        &persistent, absent_elements, 1u);
    valid = valid && decision && query && open_query &&
        open_middle_query && open_deep_query && absent_query;

    for (uint32_t iteration = 0u; valid && iteration < iterations;
         iteration++) {
        const uint32_t *selected = NULL;
        size_t selected_count = 0u;
        CettaMatchDecisionSelectState state =
            cetta_match_decision_select(
                decision, &space, semantic_identity, query,
                UINT64_MAX, NULL, NULL,
                &selected, &selected_count);
        valid = state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == 2u && selected &&
            selected[0] == PREFIX_BENCH_QUERY &&
            selected[1] == PREFIX_BENCH_CLAUSES - 1u;
        state = cetta_match_decision_select(
            decision, &space, semantic_identity, open_query,
            UINT64_MAX, NULL, NULL,
            &selected, &selected_count);
        valid = valid && state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == PREFIX_BENCH_CLAUSES && selected &&
            selected[0] == 0u &&
            selected[PREFIX_BENCH_CLAUSES - 1u] ==
                PREFIX_BENCH_CLAUSES - 1u;
        state = cetta_match_decision_select(
            decision, &space, semantic_identity, open_middle_query,
            UINT64_MAX, NULL, NULL,
            &selected, &selected_count);
        valid = valid && state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == PREFIX_BENCH_CLAUSES && selected &&
            selected[0] == 0u &&
            selected[PREFIX_BENCH_CLAUSES - 1u] ==
                PREFIX_BENCH_CLAUSES - 1u;
        state = cetta_match_decision_select(
            decision, &space, semantic_identity, open_deep_query,
            UINT64_MAX, NULL, NULL,
            &selected, &selected_count);
        valid = valid && state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == PREFIX_BENCH_CLAUSES && selected &&
            selected[0] == 0u &&
            selected[PREFIX_BENCH_CLAUSES - 1u] ==
                PREFIX_BENCH_CLAUSES - 1u;
        state = cetta_match_decision_select(
            decision, &space, semantic_identity, query,
            0u, NULL, NULL, &selected, &selected_count);
        valid = valid && state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == PREFIX_BENCH_CLAUSES;
        state = cetta_match_decision_select(
            decision, &space, semantic_identity, absent_query,
            UINT64_MAX, NULL, NULL,
            &selected, &selected_count);
        valid = valid && state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == 0u;
        state = cetta_match_decision_select_parts(
            decision, &space, semantic_identity,
            query->expr.elems[0], &query->expr.elems[1],
            query->expr.len - 1u, UINT64_MAX,
            &selected, &selected_count);
        valid = valid && state == CETTA_MATCH_DECISION_SELECT_READY &&
            selected_count == 2u && selected &&
            selected[0] == PREFIX_BENCH_QUERY &&
            selected[1] == PREFIX_BENCH_CLAUSES - 1u;
    }

    CettaMatchDecisionStats stats = {0};
    cetta_match_decision_stats(decision, &stats);
    const char *reference_value = getenv(
        "CETTA_MATCH_DECISION_PREFIX_OBSERVATION_REFERENCE");
    bool reference = reference_value && reference_value[0] != '\0' &&
        reference_value[0] != '0';
    const char *eager_reference_value = getenv(
        "CETTA_MATCH_DECISION_PREFIX_OBSERVATION_EAGER_REFERENCE");
    bool eager_reference = eager_reference_value &&
        eager_reference_value[0] != '\0' &&
        eager_reference_value[0] != '0';
    valid = valid && stats.prefix_observation_build_attempts ==
        (reference ? 0u : 1u) &&
        stats.prefix_observation_build_commits ==
        (reference ? 0u : 1u) &&
        stats.prefix_observation_build_declines == 0u &&
        stats.prefix_observation_runs ==
        (reference ? 0u : 7u * iterations) &&
        (reference || stats.prefix_observation_node_visits > 0u) &&
        (reference || eager_reference ||
            stats.prefix_observation_absorbed_suffixes > 0u) &&
        (reference || eager_reference ||
            stats.prefix_observation_skipped_edges >=
                stats.prefix_observation_absorbed_suffixes) &&
        (!eager_reference ||
            (stats.prefix_observation_absorbed_suffixes == 0u &&
             stats.prefix_observation_skipped_edges == 0u)) &&
        (reference || stats.prefix_observation_trie_edges * 2u + 1u <
            stats.prefix_observation_direct_edges);

    printf("(MatchDecisionPrefixObservationBench %u %u %u %u %s "
           "%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 ")\n",
           PREFIX_BENCH_CLAUSES, PREFIX_BENCH_LEAVES, depth, iterations,
           valid ? "pass" : "fail",
           stats.prefix_observation_direct_edges,
           stats.prefix_observation_trie_edges,
           stats.prefix_observation_node_visits,
           stats.prefix_observation_absorbed_suffixes,
           stats.prefix_observation_skipped_edges);
    if (decision)
        cetta_match_decision_free(decision);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&persistent);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    return valid ? 0 : 1;
}
