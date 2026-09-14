#include "match_decision.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PREFIX_BENCH_CLAUSES = 64u,
    PREFIX_BENCH_LEAVES = 16u,
    PREFIX_BENCH_MAX_LEAVES = 256u,
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
        Atom *row_head, Atom *zero, Atom *one, uint32_t code,
        uint32_t leaves) {
    Atom *row_elements[PREFIX_BENCH_MAX_LEAVES + 1u] = {row_head};
    for (uint32_t leaf = 0u; leaf < leaves; leaf++) {
        row_elements[leaf + 1u] =
            (code & (UINT32_C(1) << (leaf % 6u))) != 0u
                ? one : zero;
    }
    Atom *row = atom_expr(
        arena, row_elements, leaves + 1u);
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
    const char *mode_name = argc > 3 ? argv[3] : "conjunctive";
    CettaMatchDecisionMode mode;
    if (strcmp(mode_name, "conjunctive") == 0)
        mode = CETTA_MATCH_DECISION_CONJUNCTIVE;
    else if (strcmp(mode_name, "deep") == 0)
        mode = CETTA_MATCH_DECISION_DEEP;
    else
        return 2;
    const char *geometry = argc > 4 ? argv[4] : "all";
    const char *names[] = {"closed", "shallow-open", "middle-open",
        "deep-open", "unready", "absent", "parts"};
    unsigned geometry_mask = 0u;
    for (unsigned g = 0u; g < 7u; g++)
        if (strcmp(geometry, "all") == 0 || strcmp(geometry, names[g]) == 0)
            geometry_mask |= 1u << g;
    const uint32_t leaves = parse_u32(
        argc > 5 ? argv[5] : NULL, PREFIX_BENCH_LEAVES,
        PREFIX_BENCH_MAX_LEAVES);
    if (!geometry_mask || leaves < 6u)
        return 2;
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
                ? PREFIX_BENCH_QUERY : clause, leaves);
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
              mode,
              depth + 3u,
              cetta_match_decision_realization_from_process(),
              NULL, NULL)
        : NULL;
    Atom *query = clauses[PREFIX_BENCH_QUERY].pattern;
    Atom *open_query = make_open_query(
        &persistent, nest_head, "open-prefix", 1u);
    uint32_t middle_depth = depth > 1u ? depth / 2u : 1u;
    Atom *open_middle_query = make_open_query(
        &persistent, nest_head, "open-middle", middle_depth);
    Atom *open_deep_query = make_open_query(
        &persistent, nest_head, "open-deep", depth);
    Atom *absent_elements[] = {nest_head};
    Atom *absent_query = atom_expr(
        &persistent, absent_elements, 1u);
    valid = valid && decision && query && open_query &&
        open_middle_query && open_deep_query && absent_query;

    Atom *queries[] = {query, open_query, open_middle_query,
        open_deep_query, query, absent_query, query};
    uint64_t expected[7] = {0};
    for (unsigned g = 0u; g < 7u; g++) {
        for (unsigned c = 0u; c < PREFIX_BENCH_CLAUSES; c++) {
            const unsigned code = c == PREFIX_BENCH_CLAUSES - 1u
                ? PREFIX_BENCH_QUERY : c;
            /* The closed conjunctive intersection is {42, 63}. In deep
             * mode, the first minimum is bit 1: 32 occurrences. Bit 0
             * keeps 33, because the final odd code was replaced by 42.
             * Open or unavailable paths cannot discard any occurrence. */
            bool keep = g == 5u ? false : g != 0u && g != 6u ? true
                : mode == CETTA_MATCH_DECISION_CONJUNCTIVE
                    ? code == PREFIX_BENCH_QUERY : (code & 2u) != 0u;
            if (keep) expected[g] |= UINT64_C(1) << c;
        }
    }
    for (uint32_t iteration = 0u; valid && iteration < iterations;
         iteration++) {
        for (unsigned g = 0u; valid && g < 7u; g++) {
            if (!(geometry_mask & (1u << g))) continue;
            const uint32_t *selected = NULL;
            size_t selected_count = 0u;
            CettaMatchDecisionSelectState state = g == 6u
                ? cetta_match_decision_select_parts(
                    decision, &space, semantic_identity,
                    query->expr.elems[0], &query->expr.elems[1],
                    query->expr.len - 1u, UINT64_MAX,
                    &selected, &selected_count)
                : cetta_match_decision_select(
                    decision, &space, semantic_identity, queries[g],
                    g == 4u ? 0u : UINT64_MAX, NULL, NULL,
                    &selected, &selected_count);
            valid = state == CETTA_MATCH_DECISION_SELECT_READY &&
                selected_count <= PREFIX_BENCH_CLAUSES;
            uint64_t actual = 0u;
            for (size_t c = 0u; valid && c < selected_count; c++) {
                valid = selected && selected[c] < PREFIX_BENCH_CLAUSES &&
                    (c == 0u || selected[c - 1u] < selected[c]);
                if (valid) actual |= UINT64_C(1) << selected[c];
            }
            valid = valid && actual == expected[g];
            if (!valid)
                fprintf(stderr, "%s/%s: got %016" PRIx64
                        " expected %016" PRIx64 "\n",
                        mode_name, names[g], actual, expected[g]);
        }
    }

    CettaMatchDecisionStats stats = {0};
    cetta_match_decision_stats(decision, &stats);
    /* Counters are receipts for the benchmark report.  Only their conserved
     * attempt partition is a testable accounting law; selection results
     * above are the language contract. */
    valid = valid &&
        stats.prefix_observation_build_attempts ==
            stats.prefix_observation_build_commits +
                stats.prefix_observation_build_declines;

    printf("(MatchDecisionPrefixObservationBench %u %u %u %u %s "
           "%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 " %s %s)\n",
           PREFIX_BENCH_CLAUSES, leaves, depth, iterations,
           valid ? "pass" : "fail",
           stats.prefix_observation_direct_edges,
           stats.prefix_observation_trie_edges,
           stats.prefix_observation_node_visits,
           stats.prefix_observation_absorbed_suffixes,
           stats.prefix_observation_skipped_edges, mode_name, geometry);
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
