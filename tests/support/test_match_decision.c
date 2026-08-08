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
        CETTA_MATCH_DECISION_LINEAR, 0u, NULL, NULL);
    CettaMatchDecision *deep = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity, clauses,
        sizeof(clauses) / sizeof(clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u, NULL, NULL);
    CettaMatchDecision *opaque = cetta_match_decision_compile(
        space_read_token(&space), semantic_identity, clauses,
        sizeof(clauses) / sizeof(clauses[0]),
        CETTA_MATCH_DECISION_DEEP, 0u,
        opaque_argument, NULL);
    assert(linear && deep && opaque);

    const uint32_t all[] = {11u, 22u, 33u, 44u, 45u};
    const uint32_t ax1[] = {11u, 44u, 45u};
    /* Clause 11 remains a legal false positive: its repeated `$p` is the
     * correlation that rejects this query, and MatchDecision deliberately
     * leaves variable equality to the exact matcher. */
    const uint32_t ax2[] = {11u, 22u, 44u, 45u};
    const uint32_t general[] = {44u, 45u};
    expect_refs(linear, &space, ax1_query, semantic_identity,
                UINT64_MAX, all, 5u);
    expect_refs(deep, &space, ax1_query, semantic_identity,
                UINT64_MAX, ax1, 3u);
    expect_refs(deep, &space, ax2_query, semantic_identity,
                UINT64_MAX, ax2, 4u);
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
        CETTA_MATCH_DECISION_DEEP, 0u, NULL, NULL);
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
    assert(stats.clause_survivors == 21u);
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
        CETTA_MATCH_DECISION_DEEP, 0u, NULL, NULL);
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
