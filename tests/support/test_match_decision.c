#include "match_decision.h"
#include "parser.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Atom *parse_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void expect_refs(
    CettaMatchDecision *decision, Space *space, Atom *query,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    uint64_t ready_arguments,
    const uint32_t *expected, size_t expected_count) {
    const uint32_t *actual = NULL;
    size_t actual_count = 0u;
    assert(cetta_match_decision_select(
               decision, space, semantic_identity,
               query, ready_arguments,
               NULL, NULL, &actual, &actual_count) ==
           CETTA_MATCH_DECISION_SELECT_READY);
    if (actual_count != expected_count) {
        fprintf(stderr, "candidate count: got %zu expected %zu query=",
                actual_count, expected_count);
        atom_print(query, stderr);
        fputs(" refs=", stderr);
        for (size_t index = 0u; index < actual_count; index++)
            fprintf(stderr, "%s%u", index ? "," : "", actual[index]);
        fputc('\n', stderr);
    }
    assert(actual_count == expected_count);
    for (size_t index = 0u; index < expected_count; index++)
        assert(actual[index] == expected[index]);
}

static void expect_part_refs(
    CettaMatchDecision *decision, Space *space, Atom *query,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    uint64_t ready_arguments,
    const uint32_t *expected, size_t expected_count) {
    assert(query && query->kind == ATOM_EXPR && query->expr.len > 0u);
    const uint32_t *actual = NULL;
    size_t actual_count = 0u;
    assert(cetta_match_decision_select_parts(
               decision, space, semantic_identity,
               query->expr.elems[0], &query->expr.elems[1],
               query->expr.len - 1u, ready_arguments,
               &actual, &actual_count) ==
           CETTA_MATCH_DECISION_SELECT_READY);
    assert(actual_count == expected_count);
    for (size_t index = 0u; index < expected_count; index++)
        assert(actual[index] == expected[index]);
}

static uint32_t *copy_refs(
    CettaMatchDecision *decision, Space *space, Atom *query,
    CettaMatchDecisionSemanticIdentity semantic_identity,
    uint64_t ready_arguments, size_t *count) {
    const uint32_t *selected = NULL;
    *count = 0u;
    assert(cetta_match_decision_select(
               decision, space, semantic_identity,
               query, ready_arguments, NULL, NULL,
               &selected, count) == CETTA_MATCH_DECISION_SELECT_READY);
    uint32_t *copy = *count ? malloc(sizeof(*copy) * *count) : NULL;
    assert(*count == 0u || copy);
    for (size_t index = 0u; index < *count; index++)
        copy[index] = selected[index];
    return copy;
}

static bool refs_are_subsequence(
    const uint32_t *small, size_t small_count,
    const uint32_t *large, size_t large_count) {
    size_t cursor = 0u;
    for (size_t index = 0u; index < large_count && cursor < small_count;
         index++) {
        if (large[index] == small[cursor])
            cursor++;
    }
    return cursor == small_count;
}

static CettaMatchDecisionPatternClass opaque_argument(
    void *context, uint32_t source_ref,
    const CettaExprIndex *path, uint32_t path_len,
    Atom *pattern) {
    (void)context;
    (void)source_ref;
    (void)path;
    (void)path_len;
    (void)pattern;
    return CETTA_MATCH_DECISION_PATTERN_OPAQUE;
}

static bool reject_source_ref(
    void *context, uint32_t source_ref,
    Atom *pattern, Atom *query) {
    (void)pattern;
    (void)query;
    return source_ref != *(const uint32_t *)context;
}

int main(void) {
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

    CettaMatchDecisionClause clauses[] = {
        {parse_one(&persistent,
             "(f (: $proof (-> (imp $p (imp $q $p)) $out)))"), 11u},
        {parse_one(&persistent,
             "(f (: $proof (-> (imp (imp $p (imp $q $r)) (imp (imp $p $q) (imp $p $r))) $out)))"), 22u},
        {parse_one(&persistent,
             "(f (: $proof (-> (imp (imp (neg $p) (neg $q)) (imp $q $p)) $out)))"), 33u},
        {parse_one(&persistent,
             "(f (: $proof (-> $domain $codomain)))"), 44u},
        /* A duplicate occurrence is distinct evidence and must survive in
         * source order rather than being set-normalized. */
        {parse_one(&persistent,
             "(f (: $proof2 (-> $domain2 $codomain2)))"), 45u},
    };
    for (size_t index = 0u;
         index < sizeof(clauses) / sizeof(clauses[0]); index++) {
        assert(clauses[index].pattern);
    }
    Atom *ax1_query = parse_one(
        &persistent,
        "(f (: pf (-> (imp a (imp b a)) result)))");
    Atom *ax2_query = parse_one(
        &persistent,
        "(f (: pf (-> (imp (imp a (imp b c)) (imp (imp a b) (imp a c))) result)))");
    Atom *negative_query = parse_one(
        &persistent,
        "(f (: pf (-> (neg a) result)))");
    Atom *open_query = parse_one(&persistent, "(f $unknown)");
    assert(ax1_query && ax2_query && negative_query && open_query);

    const CettaMatchDecisionSemanticIdentity semantic_identity = {
        .language_id = 3u,
        .profile_id = 2u,
        .match_policy_id = 1u,
        .demand_policy_id = 7u,
        .presentation_identity = UINT64_C(0x12345678),
        .compiler_identity = UINT64_C(0x4d44495200000001),
    };

    CettaMatchDecision *linear = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity, clauses,
        sizeof(clauses) / sizeof(clauses[0]),
        CETTA_MATCH_DECISION_LINEAR, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    CettaMatchDecision *deep = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity, clauses,
        sizeof(clauses) / sizeof(clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    CettaMatchDecision *opaque = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity, clauses,
        sizeof(clauses) / sizeof(clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u,
        (CettaMatchDecisionRealization){0}, opaque_argument, NULL);
    assert(linear && deep && opaque);
    assert(cetta_match_decision_retain(NULL) == NULL);
    CettaMatchDecision *deep_lease =
        cetta_match_decision_retain(deep);
    assert(deep_lease == deep);
    cetta_match_decision_free(deep);
    deep = deep_lease;

    const uint32_t all[] = {11u, 22u, 33u, 44u, 45u};
    const uint32_t ax1[] = {11u, 44u, 45u};
    /* Clause 11 is structurally compatible but its repeated `$p` observes
     * different ground subterms.  The equality refuter removes precisely that
     * occurrence while preserving authored order for every survivor. */
    const uint32_t ax2[] = {22u, 44u, 45u};
    const uint32_t general[] = {44u, 45u};
    expect_refs(linear, &space, ax1_query, semantic_identity,
                UINT64_MAX, all, 5u);
    expect_refs(deep, &space, ax1_query, semantic_identity,
                UINT64_MAX, ax1, 3u);
    expect_refs(deep, &space, ax2_query, semantic_identity,
                UINT64_MAX, ax2, 3u);
    expect_refs(deep, &space, negative_query, semantic_identity,
                UINT64_MAX, general, 2u);

    /* Unavailable or variable observations are not negative evidence. */
    expect_refs(deep, &space, ax1_query, semantic_identity,
                0u, all, 5u);
    expect_refs(deep, &space, open_query, semantic_identity,
                UINT64_MAX, all, 5u);
    expect_refs(opaque, &space, ax1_query, semantic_identity,
                UINT64_MAX, all, 5u);

    /* Register machines use the zero-allocation split-call API.  It must
     * preserve the whole-call selector's positive, negative, and unavailable
     * observations exactly. */
    CettaMatchDecision *parts = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity, clauses,
        sizeof(clauses) / sizeof(clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(parts);
    expect_part_refs(parts, &space, ax1_query, semantic_identity,
                     UINT64_MAX, ax1, 3u);
    expect_part_refs(parts, &space, negative_query, semantic_identity,
                     UINT64_MAX, general, 2u);
    expect_part_refs(parts, &space, ax1_query, semantic_identity,
                     0u, all, 5u);

    const uint32_t rejected = 44u;
    const uint32_t *verified = NULL;
    size_t verified_count = 0u;
    assert(cetta_match_decision_select(
               deep, &space, semantic_identity,
               ax1_query, UINT64_MAX,
               reject_source_ref, (void *)&rejected,
               &verified, &verified_count) ==
           CETTA_MATCH_DECISION_SELECT_READY);
    assert(verified_count == 2u);
    assert(verified[0] == 11u && verified[1] == 45u);

    CettaMatchDecisionStats stats = {0};
    cetta_match_decision_stats(deep, &stats);
    assert(stats.compilations == 1u);
    assert(stats.runs == 6u);
    assert(stats.clause_inputs == 30u);
    assert(stats.clause_survivors == 20u);
    assert(stats.linear_fallbacks == 2u);
    assert(stats.unavailable_path_fallbacks > 0u);

    /* Availability is an information order: revealing another argument may
     * only remove refuted occurrences.  This property does not need the
     * linear backend as a referee. */
    CettaMatchDecisionClause ladder_clauses[] = {
        {parse_one(&persistent, "(g A B)"), 101u},
        {parse_one(&persistent, "(g A $right)"), 102u},
        {parse_one(&persistent, "(g $left B)"), 103u},
        {parse_one(&persistent, "(g $left $right)"), 104u},
        {parse_one(&persistent, "(g C B)"), 105u},
    };
    Atom *ladder_query = parse_one(&persistent, "(g A B)");
    CettaMatchDecision *ladder = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity,
        ladder_clauses,
        sizeof(ladder_clauses) / sizeof(ladder_clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(ladder && ladder_query);
    size_t bottom_count = 0u;
    size_t left_count = 0u;
    size_t full_count = 0u;
    uint32_t *bottom = copy_refs(
        ladder, &space, ladder_query, semantic_identity,
        0u, &bottom_count);
    uint32_t *left = copy_refs(
        ladder, &space, ladder_query, semantic_identity,
        UINT64_C(1), &left_count);
    uint32_t *full = copy_refs(
        ladder, &space, ladder_query, semantic_identity,
        UINT64_C(3), &full_count);
    assert(bottom_count == 5u);
    assert(left_count == 4u);
    assert(full_count == 4u);
    assert(refs_are_subsequence(left, left_count, bottom, bottom_count));
    assert(refs_are_subsequence(full, full_count, left, left_count));
    free(full);
    free(left);
    free(bottom);
    cetta_match_decision_free(ladder);

    /* Distributed discrimination needs conjunction: every single board
     * position leaves a different impossible clause alive, while intersecting
     * all observable positions keeps exactly the structurally possible
     * occurrences.  The duplicate remains distinct and source ordered. */
    CettaMatchDecisionClause grid_clauses[] = {
        {parse_one(&persistent, "(grid (blank $a $b))"), 201u},
        {parse_one(&persistent, "(grid ($a blank $b))"), 202u},
        {parse_one(&persistent, "(grid ($a $b blank))"), 203u},
        {parse_one(&persistent, "(grid ($a $b $c))"), 204u},
        {parse_one(&persistent, "(grid (blank $x $y))"), 205u},
    };
    Atom *grid_query = parse_one(
        &persistent, "(grid (blank left right))");
    Atom *grid_open = parse_one(&persistent, "(grid $state)");
    CettaMatchDecision *conjunctive = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity,
        grid_clauses,
        sizeof(grid_clauses) / sizeof(grid_clauses[0]),
        CETTA_MATCH_DECISION_CONJUNCTIVE, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(conjunctive && grid_query && grid_open);
    const uint32_t grid_exact[] = {201u, 204u, 205u};
    const uint32_t grid_all[] = {201u, 202u, 203u, 204u, 205u};
    expect_refs(conjunctive, &space, grid_query, semantic_identity,
                UINT64_MAX, grid_exact, 3u);
    expect_refs(conjunctive, &space, grid_query, semantic_identity,
                0u, grid_all, 5u);
    expect_refs(conjunctive, &space, grid_open, semantic_identity,
                UINT64_MAX, grid_all, 5u);
    cetta_match_decision_free(conjunctive);

    /* Repeated source variables compile to cross-position equality
     * refuters.  A ground disagreement removes only nonlinear occurrences;
     * equal and unavailable observations retain authored order and duplicate
     * occurrences for the canonical matcher. */
    CettaMatchDecisionClause equality_clauses[] = {
        {parse_one(&persistent, "(equal $x $x)"), 301u},
        {parse_one(&persistent, "(equal $x $y)"), 302u},
        {parse_one(&persistent, "(equal $x $x)"), 303u},
    };
    Atom *equality_disagrees =
        parse_one(&persistent, "(equal left right)");
    Atom *equality_agrees =
        parse_one(&persistent, "(equal same same)");
    Atom *equality_unknown =
        parse_one(&persistent, "(equal $open right)");
    CettaMatchDecision *equality = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity,
        equality_clauses,
        sizeof(equality_clauses) / sizeof(equality_clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(equality && equality_disagrees && equality_agrees &&
           equality_unknown);
    const uint32_t equality_linear_only[] = {302u};
    const uint32_t equality_all[] = {301u, 302u, 303u};
    expect_refs(equality, &space, equality_disagrees, semantic_identity,
                UINT64_MAX, equality_linear_only, 1u);
    expect_refs(equality, &space, equality_agrees, semantic_identity,
                UINT64_MAX, equality_all, 3u);
    expect_refs(equality, &space, equality_unknown, semantic_identity,
                UINT64_MAX, equality_all, 3u);
    CettaMatchDecisionStats equality_stats = {0};
    cetta_match_decision_stats(equality, &equality_stats);
    assert(equality_stats.equality_checks == 6u);
    assert(equality_stats.equality_refutations == 2u);
    assert(equality_stats.equality_observation_reads == 0u);
    assert(equality_stats.equality_observation_fallbacks == 12u);
    assert(equality_stats.equality_observation_direct_edges == 12u);
    assert(equality_stats.equality_observation_graph_edges == 0u);
    assert(equality_stats.prefix_observation_build_attempts == 1u);
    assert(equality_stats.prefix_observation_build_commits == 0u);
    assert(equality_stats.prefix_observation_build_declines == 1u);
    cetta_match_decision_free(equality);

    /* Deep nonlinear occurrences demand the same read-only coordinates as
     * ordinary selection.  Their endpoint requests join the shared prefix
     * graph, while shallow disjoint equalities above remain on the direct
     * walker because the charged representation would not save work. */
    CettaMatchDecisionClause deep_equality_clauses[] = {
        {parse_one(&persistent,
            "(equal-deep (nest (pair $x $x)))"), 311u},
        {parse_one(&persistent,
            "(equal-deep (nest (pair $x $x)))"), 312u},
        {parse_one(&persistent,
            "(equal-deep (nest (pair $x $x)))"), 313u},
        {parse_one(&persistent,
            "(equal-deep (nest (pair $x $y)))"), 314u},
    };
    Atom *deep_equality_disagrees = parse_one(
        &persistent, "(equal-deep (nest (pair left right)))");
    Atom *deep_equality_agrees = parse_one(
        &persistent, "(equal-deep (nest (pair same same)))");
    Atom *deep_equality_unknown = parse_one(
        &persistent, "(equal-deep (nest (pair $open right)))");
    CettaMatchDecision *deep_equality = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity,
        deep_equality_clauses,
        sizeof(deep_equality_clauses) /
            sizeof(deep_equality_clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 8u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(deep_equality && deep_equality_disagrees &&
           deep_equality_agrees && deep_equality_unknown);
    const uint32_t deep_equality_linear_only[] = {314u};
    const uint32_t deep_equality_all[] = {311u, 312u, 313u, 314u};
    expect_refs(deep_equality, &space, deep_equality_disagrees,
                semantic_identity, UINT64_MAX,
                deep_equality_linear_only, 1u);
    expect_refs(deep_equality, &space, deep_equality_agrees,
                semantic_identity, UINT64_MAX,
                deep_equality_all, 4u);
    expect_refs(deep_equality, &space, deep_equality_unknown,
                semantic_identity, UINT64_MAX,
                deep_equality_all, 4u);
    CettaMatchDecisionStats deep_equality_stats = {0};
    cetta_match_decision_stats(deep_equality, &deep_equality_stats);
    assert(deep_equality_stats.equality_checks == 9u);
    assert(deep_equality_stats.equality_refutations == 3u);
    assert(deep_equality_stats.equality_observation_reads == 18u);
    assert(deep_equality_stats.equality_observation_fallbacks == 0u);
    assert(deep_equality_stats.equality_observation_direct_edges > 0u);
    assert(deep_equality_stats.equality_observation_graph_edges > 0u);
    assert(deep_equality_stats.equality_observation_graph_edges <
           deep_equality_stats.equality_observation_direct_edges);
    assert(deep_equality_stats.prefix_observation_build_attempts == 1u);
    assert(deep_equality_stats.prefix_observation_build_commits == 1u);
    assert(deep_equality_stats.prefix_observation_build_declines == 0u);
    assert(deep_equality_stats.prefix_observation_node_visits > 0u);
    assert(deep_equality_stats.prefix_observation_node_visits <=
           deep_equality_stats.prefix_observation_trie_edges * 3u);
    cetta_match_decision_free(deep_equality);

    /* A source-independent observation graph shares a long structural prefix
     * across literal, duplicate, and wildcard occurrences.  Present, unknown,
     * unavailable, absent, and split-register observations must retain the
     * same ordered candidate superset as independent path walking. */
    CettaMatchDecisionClause prefix_clauses[] = {
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row a))))))"), 401u},
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row b))))))"), 402u},
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row c))))))"), 403u},
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row d))))))"), 404u},
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row e))))))"), 405u},
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row f))))))"), 406u},
        {parse_one(&persistent,
            "(prefix (nest (nest (nest (nest (row a))))))"), 407u},
        {parse_one(&persistent, "(prefix $open)"), 408u},
    };
    for (size_t index = 0u;
         index < sizeof(prefix_clauses) / sizeof(prefix_clauses[0]);
         index++) {
        assert(prefix_clauses[index].pattern);
    }
    Atom *prefix_hit = parse_one(
        &persistent,
        "(prefix (nest (nest (nest (nest (row a))))))");
    Atom *prefix_open = parse_one(&persistent, "(prefix $query)");
    Atom *prefix_absent = parse_one(&persistent, "(prefix)");
    CettaMatchDecision *prefix = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity,
        prefix_clauses,
        sizeof(prefix_clauses) / sizeof(prefix_clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 12u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(prefix && prefix_hit && prefix_open && prefix_absent);
    const uint32_t prefix_exact[] = {401u, 407u, 408u};
    const uint32_t prefix_all[] = {
        401u, 402u, 403u, 404u, 405u, 406u, 407u, 408u,
    };
    const uint32_t prefix_wildcard[] = {408u};
    expect_refs(prefix, &space, prefix_hit, semantic_identity,
                UINT64_MAX, prefix_exact, 3u);
    expect_refs(prefix, &space, prefix_open, semantic_identity,
                UINT64_MAX, prefix_all, 8u);
    expect_refs(prefix, &space, prefix_hit, semantic_identity,
                0u, prefix_all, 8u);
    expect_refs(prefix, &space, prefix_absent, semantic_identity,
                UINT64_MAX, prefix_wildcard, 1u);
    expect_part_refs(prefix, &space, prefix_hit, semantic_identity,
                     UINT64_MAX, prefix_exact, 3u);
    CettaMatchDecisionStats prefix_stats = {0};
    cetta_match_decision_stats(prefix, &prefix_stats);
    assert(prefix_stats.prefix_observation_build_attempts == 1u);
    assert(prefix_stats.prefix_observation_build_commits == 1u);
    assert(prefix_stats.prefix_observation_build_declines == 0u);
    assert(prefix_stats.prefix_observation_runs == 5u);
    assert(prefix_stats.prefix_observation_node_visits > 0u);
    assert(prefix_stats.prefix_observation_node_visits <=
           prefix_stats.prefix_observation_trie_edges * 5u);
    assert(prefix_stats.prefix_observation_trie_edges * 2u + 1u <
           prefix_stats.prefix_observation_direct_edges);
    cetta_match_decision_free(prefix);

    /* Wide literal families must compile and select through the physical key
     * index rather than scanning one key per authored occurrence.  Wildcard
     * and duplicate exact occurrences remain an ordered bag. */
    const size_t wide_count = 10000u;
    CettaMatchDecisionClause *wide_clauses =
        calloc(wide_count, sizeof(*wide_clauses));
    assert(wide_clauses);
    char wide_source[96];
    for (size_t index = 0u; index < wide_count; index++) {
        if (index == 101u || index == 9001u) {
            snprintf(wide_source, sizeof(wide_source),
                     "(wide $key $value)");
        } else if (index == 5000u || index == 7000u) {
            snprintf(wide_source, sizeof(wide_source),
                     "(wide target $value)");
        } else {
            snprintf(wide_source, sizeof(wide_source),
                     "(wide key%zu $value)", index);
        }
        wide_clauses[index] = (CettaMatchDecisionClause){
            .pattern = parse_one(&persistent, wide_source),
            .source_ref = (uint32_t)(100000u + index),
        };
        assert(wide_clauses[index].pattern);
    }
    CettaMatchDecision *wide = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity,
        wide_clauses, wide_count,
        CETTA_MATCH_DECISION_DEEP, 0u,
        (CettaMatchDecisionRealization){0}, NULL, NULL);
    Atom *wide_hit = parse_one(&persistent, "(wide target observed)");
    Atom *wide_miss = parse_one(&persistent, "(wide missing observed)");
    Atom *wide_absent = parse_one(&persistent, "(wide)");
    assert(wide && wide_hit && wide_miss && wide_absent);
    const uint32_t wide_hit_refs[] = {
        100101u, 105000u, 107000u, 109001u,
    };
    const uint32_t wide_wildcard_refs[] = {100101u, 109001u};
    expect_refs(wide, &space, wide_hit, semantic_identity,
                UINT64_MAX, wide_hit_refs, 4u);
    expect_refs(wide, &space, wide_miss, semantic_identity,
                UINT64_MAX, wide_wildcard_refs, 2u);
    expect_refs(wide, &space, wide_absent, semantic_identity,
                UINT64_MAX, wide_wildcard_refs, 2u);
    const uint64_t wide_repeat_count = 1000u;
    for (uint64_t repeat = 0u; repeat < wide_repeat_count; repeat++) {
        expect_refs(wide, &space, wide_hit, semantic_identity,
                    UINT64_MAX, wide_hit_refs, 4u);
    }
    CettaMatchDecisionStats wide_stats = {0};
    cetta_match_decision_stats(wide, &wide_stats);
    const uint64_t wide_runs = 3u + wide_repeat_count;
    assert(wide_stats.runs == wide_runs);
    assert(wide_stats.clause_inputs == wide_count * wide_runs);
    assert(wide_stats.clause_survivors == 8u + 4u * wide_repeat_count);
#ifdef CETTA_MATCH_DECISION_DISABLE_EXACT_KEY_INDEX
    assert(wide_stats.generic_key_policy_scans >= wide_count * wide_runs);
    assert(wide_stats.key_index_select_probes == 0u);
#else
    assert(wide_stats.generic_key_policy_scans == 0u);
    assert(wide_stats.key_index_select_probes <
           128u + wide_repeat_count * 16u);
#endif
    assert(wide_stats.key_index_build_probes < wide_count * 20u);
    assert(wide_stats.prefix_observation_build_attempts == 1u);
    assert(wide_stats.prefix_observation_build_commits == 0u);
    assert(wide_stats.prefix_observation_build_declines == 1u);
    assert(wide_stats.prefix_observation_runs == 0u);
    cetta_match_decision_free(wide);
    free(wide_clauses);

    /* Textually identical clauses under another matcher policy are another
     * semantic world, not a cache hit. */
    CettaMatchDecisionSemanticIdentity changed_semantics =
        semantic_identity;
    changed_semantics.match_policy_id++;
    const uint32_t *stale = NULL;
    size_t stale_count = 0u;
    assert(!cetta_match_decision_is_current(
        deep, &space, changed_semantics));
    assert(cetta_match_decision_select(
               deep, &space, changed_semantics,
               ax1_query, UINT64_MAX,
               NULL, NULL, &stale, &stale_count) ==
           CETTA_MATCH_DECISION_SELECT_INVALIDATED);
    assert(!stale && stale_count == 0u);

    /* A decision also belongs to exactly one live Space revision. */
    space_add(&space, parse_one(&persistent, "mutation"));
    assert(!cetta_match_decision_is_current(
        deep, &space, semantic_identity));
    assert(cetta_match_decision_select(
               deep, &space, semantic_identity,
               ax1_query, UINT64_MAX,
               NULL, NULL, &stale, &stale_count) ==
           CETTA_MATCH_DECISION_SELECT_INVALIDATED);
    assert(!stale && stale_count == 0u);

    cetta_match_decision_free(opaque);
    cetta_match_decision_free(parts);
    cetta_match_decision_free(deep);
    cetta_match_decision_free(linear);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&persistent);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    puts("PASS: revision-pinned ordered MatchDecision oracle");
    return 0;
}
