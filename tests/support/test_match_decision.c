#include "match_decision.h"
#include "match.h"
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

static void expect_cursor_refs(
    CettaMatchDecision *decision, Space *space,
    CettaMatchDecisionSemanticIdentity semantics,
    const CettaMatchDecisionQueryViewV1 *query,
    const uint32_t *expected, size_t expected_count) {
    const uint32_t *actual = NULL;
    size_t count = 0u;
    assert(cetta_match_decision_select_view_v1(
        decision, space, semantics, query, UINT64_MAX, NULL, NULL,
        &actual, &count) == CETTA_MATCH_DECISION_SELECT_READY);
    assert(count == expected_count);
    for (size_t index = 0u; index < count; index++)
        assert(actual[index] == expected[index]);
}

typedef struct {
    Atom *value;
    size_t reads;
} PivotObservation;

static CettaGsltTermViewStatusV1 observe_pivot_argument(
        void *context, CettaGsltTermCursorV1 source,
        CettaGsltTermCursorV1 *target) {
    PivotObservation *observation = context;
    observation->reads++;
    *target = (CettaGsltTermCursorV1){
        observation->value ? observation->value : source.source, NULL};
    return CETTA_GSLT_TERM_VIEW_OK_V1;
}

static void test_pivot_observation_demand(
        Arena *arena, Space *space,
        CettaMatchDecisionSemanticIdentity semantics) {
    CettaMatchDecisionClause clauses[] = {
        {parse_one(arena, "(pivot alpha $x)"), 10u},
        {NULL, 20u},
        {parse_one(arena, "(pivot alpha (nested red))"), 30u},
        {parse_one(arena, "(pivot beta (nested blue))"), 40u},
    };
    clauses[1].pattern = clauses[0].pattern;
    Atom *call = parse_one(arena, "(pivot alpha $deferred)");
    CettaGsltTermCursorV1 arguments[] = {
        {call->expr.elems[1], NULL}, {call->expr.elems[2], NULL}};
    PivotObservation observation = {parse_one(arena, "(nested green)"), 0u};
    CettaMatchDecisionQueryViewV1 query = {
        .head = {call->expr.elems[0], NULL},
        .arguments = arguments, .arity = 2u,
        .observer = {observe_pivot_argument, &observation},
    };
    const uint32_t duplicates[] = {10u, 20u};
    const uint32_t unknown[] = {10u, 20u, 30u};
    for (unsigned separated = 0u; separated < 2u; separated++) {
        CettaMatchDecision *decision = cetta_match_decision_compile(
            space_read_token(space), semantics, clauses, 4u,
            CETTA_MATCH_DECISION_DEEP, 0u,
            (CettaMatchDecisionRealization){0}, NULL, NULL);
        assert(decision);
        observation.reads = 0u;
        expect_cursor_refs(decision, space, semantics, &query, duplicates, 2u);
        /* Read the deferred coordinate when it can remove a candidate;
         * once alpha alone separates the duplicates, that read is dead. */
        assert(separated ? observation.reads == 0u : observation.reads > 0u);
        Atom *value = observation.value;
        observation.value = NULL;
        expect_cursor_refs(decision, space, semantics, &query,
                           separated ? duplicates : unknown,
                           separated ? 2u : 3u);
        observation.value = value;
        cetta_match_decision_free(decision);
        clauses[2].pattern = parse_one(arena, "(pivot beta (nested red))");
    }
}

/* Exhausting the index's sampled paths must leave a sound superset. The
 * last field is intentionally different, beyond the retained prefix. */
static void test_bounded_path_precision(
        Arena *arena, Space *space,
        CettaMatchDecisionSemanticIdentity semantics) {
    const size_t width = 8192u;
    Atom **fields = malloc((width + 1u) * sizeof(*fields));
    assert(fields);
    fields[0] = atom_symbol(arena, "bounded-paths");
    Atom *a = atom_symbol(arena, "path-a");
    Atom *b = atom_symbol(arena, "path-b");
    for (size_t i = 1u; i <= width; i++) fields[i] = a;
    Atom *query = atom_expr(arena, fields, width + 1u);
    fields[width] = b;
    Atom *different = atom_expr(arena, fields, width + 1u);
    free(fields);
    CettaMatchDecisionClause clauses[] = {
        {query, 101u}, {different, 102u}, {query, 103u}};
    const uint32_t superset[] = {101u, 102u, 103u};
    for (size_t m = 0u; m < 2u; m++) {
        CettaMatchDecision *decision = cetta_match_decision_compile(
            space_read_token(space), semantics, clauses, 3u,
            m == 0u ? CETTA_MATCH_DECISION_DEEP
                     : CETTA_MATCH_DECISION_CONJUNCTIVE,
            0u, (CettaMatchDecisionRealization){0}, NULL, NULL);
        assert(decision);
        expect_refs(decision, space, query, semantics,
                    UINT64_MAX, superset, 3u);
        Bindings bindings; bindings_init(&bindings);
        assert(match_atoms(query, clauses[0].pattern, &bindings));
        assert(!match_atoms(query, clauses[1].pattern, &bindings));
        assert(match_atoms(query, clauses[2].pattern, &bindings));
        bindings_free(&bindings);
        cetta_match_decision_free(decision);
    }
}

static void test_scoped_cursors(
        Arena *arena, Space *space,
        CettaMatchDecisionSemanticIdentity semantics, bool shared_prefix) {
    Atom *source = parse_one(arena, "(scope $scope-x)");
    Atom *outer_value = parse_one(arena, shared_prefix
        ? "(nest (nest (nest (box $scope-y))))" : "(box $scope-y)");
    Atom *x = source->expr.elems[1];
    Atom *y = outer_value;
    while (y->kind == ATOM_EXPR)
        y = y->expr.elems[1];
    assert(y->kind == ATOM_VAR);
    Atom *a = parse_one(arena, "a");
    Atom *b = parse_one(arena, "b");
    const uint32_t epoch = 71u;
    BindingsBuilder builder;
    assert(bindings_builder_init(&builder, NULL));
    assert(bindings_builder_add_var_fresh(&builder, y, a));
    uint32_t first = builder.current.len;
    uint32_t mark = bindings_builder_save(&builder);
    assert(bindings_builder_add_id_fresh(&builder,
        var_epoch_id(x->var_id, epoch), x->sym_id, outer_value));
    assert(bindings_builder_add_id_fresh(&builder,
        var_epoch_id(y->var_id, epoch), y->sym_id, b));
    BindingsDenseEpochFrame frame;
    bindings_dense_epoch_frame_init(&frame);
    VarId ids[] = {x->var_id};
    Atom *variables[] = {x};
    assert(bindings_dense_epoch_frame_prepare(
        &frame, &builder, ids, variables, 1u, epoch, first));
    BindingsTermCursorContextV1 context = {&builder.current, &frame};
    CettaGsltTermCursorV1 argument = {x, &frame};
    CettaMatchDecisionQueryViewV1 query = {
        .head = {.source = source->expr.elems[0]},
        .arguments = &argument, .arity = 1u,
        .observer = {bindings_resolve_term_cursor_v1, &context},
    };
    CettaMatchDecisionClause clauses[] = {
        {parse_one(arena, shared_prefix
            ? "(scope (nest (nest (nest (box a)))))" : "(scope (box a))"), 10u},
        {parse_one(arena, shared_prefix
            ? "(scope (nest (nest (nest (box b)))))" : "(scope (box b))"), 20u},
        {parse_one(arena, shared_prefix
            ? "(scope (nest (nest (nest (box a)))))" : "(scope (box a))"), 30u},
    };
    const uint32_t outer_refs[] = {10u, 30u};
    const uint32_t local_refs[] = {20u};
    const uint32_t open_refs[] = {10u, 20u, 30u};
    Atom *forced = bindings_apply_dense_epoch_frame_then_all(
        &builder, arena, source, &frame);
    assert(forced && atom_eq(forced, clauses[0].pattern));
    for (int mode = CETTA_MATCH_DECISION_DEEP;
         mode <= CETTA_MATCH_DECISION_CONJUNCTIVE; mode++) {
        for (unsigned realization = 0u; realization < 8u; realization++) {
            CettaMatchDecision *decision = cetta_match_decision_compile(
                space_read_token(space), semantics, clauses, 3u,
                (CettaMatchDecisionMode)mode, 0u,
                (CettaMatchDecisionRealization){
                    .use_direct_prefix_observation = (realization & 1u) != 0u,
                    .use_eager_prefix_observation = (realization & 2u) != 0u,
                    .use_direct_equality_observation = (realization & 4u) != 0u,
                }, NULL, NULL);
            assert(decision);
            CettaMatchDecisionStats stats;
            cetta_match_decision_stats(decision, &stats);
            if (shared_prefix && (realization & 1u) == 0u)
                assert(stats.prefix_observation_build_commits == 1u);
            size_t bytes = arena_accounted_live_bytes(arena);
            expect_cursor_refs(decision, space, semantics, &query, outer_refs, 2u);
            assert(arena_accounted_live_bytes(arena) == bytes);
            expect_refs(decision, space, forced, semantics,
                        UINT64_MAX, outer_refs, 2u);

            /* The same raw subtree in the source namespace sees local y=b.
             * A dereference of x must instead enter the outer y=a world. */
            argument.source = outer_value;
            expect_cursor_refs(decision, space, semantics, &query, local_refs, 1u);
            argument.source = x;
            bindings_builder_rollback(&builder, mark);
            expect_cursor_refs(decision, space, semantics, &query, open_refs, 3u);
            assert(bindings_builder_add_id_fresh(&builder,
                var_epoch_id(x->var_id, epoch), x->sym_id, outer_value));
            assert(bindings_builder_add_id_fresh(&builder,
                var_epoch_id(y->var_id, epoch), y->sym_id, b));
            assert(bindings_dense_epoch_frame_prepare(
                &frame, &builder, ids, variables, 1u, epoch, first));
            expect_cursor_refs(decision, space, semantics, &query, outer_refs, 2u);
            cetta_match_decision_free(decision);
        }
    }
    bindings_dense_epoch_frame_free(&frame);
    bindings_builder_free(&builder);
}

static void test_scoped_equality(
        Arena *arena, Space *space,
        CettaMatchDecisionSemanticIdentity semantics) {
    Atom *names = parse_one(arena, "(names $source $outer $ordinary)");
    Atom *source = names->expr.elems[1];
    Atom *outer = names->expr.elems[2];
    Atom *ordinary = names->expr.elems[3];
    Atom *head = atom_symbol(arena, "view-equality");
    Atom *box = atom_symbol(arena, "box");
    Atom *a = atom_symbol(arena, "value-a");
    Atom *b = atom_symbol(arena, "value-b");
    Atom *left = atom_expr2(arena, box, source);
    Atom *right = atom_expr2(arena, box, ordinary);
    CettaMatchDecisionClause clauses[] = {
        {parse_one(arena, "(view-equality $x $x)"), 1u},
        {parse_one(arena, "(view-equality $x $y)"), 2u},
        {NULL, 3u},
    };
    clauses[2].pattern = clauses[0].pattern;
    const uint32_t all[] = {1u, 2u, 3u}, independent[] = {2u};
    for (int mode = CETTA_MATCH_DECISION_DEEP;
         mode <= CETTA_MATCH_DECISION_CONJUNCTIVE; mode++) {
        BindingsBuilder builder;
        assert(bindings_builder_init(&builder, NULL));
        assert(bindings_builder_add_var_fresh(&builder, outer, a));
        assert(bindings_builder_add_var_fresh(&builder, ordinary, b));
        assert(bindings_builder_add_var_fresh(&builder, source, b));
        uint32_t first = builder.current.len;
        uint32_t mark = bindings_builder_save(&builder);
        const uint32_t epoch = 73u;
        assert(bindings_builder_add_id_fresh(&builder,
            var_epoch_id(source->var_id, epoch), source->sym_id, outer));
        BindingsDenseEpochFrame frame;
        bindings_dense_epoch_frame_init(&frame);
        VarId ids[] = {source->var_id};
        Atom *variables[] = {source};
        assert(bindings_dense_epoch_frame_prepare(
            &frame, &builder, ids, variables, 1u, epoch, first));
        BindingsTermCursorContextV1 context = {&builder.current, &frame};
        CettaGsltTermCursorV1 arguments[] = {{left, &frame}, {right, NULL}};
        CettaMatchDecisionQueryViewV1 query = {
            .head = {head, NULL}, .arguments = arguments, .arity = 2u,
            .observer = {bindings_resolve_term_cursor_v1, &context},
        };
        CettaMatchDecision *decision = cetta_match_decision_compile(
            space_read_token(space), semantics, clauses, 3u,
            (CettaMatchDecisionMode)mode, 0u,
            (CettaMatchDecisionRealization){0}, NULL, NULL);
        assert(decision);
        size_t bytes = arena_accounted_live_bytes(arena);
        uint64_t growth = builder.growth_count;
        expect_cursor_refs(decision, space, semantics, &query, independent, 1u);
        assert(arena_accounted_live_bytes(arena) == bytes);
        assert(builder.growth_count == growth);
        /* One raw subtree in two environments must not be treated as equal. */
        arguments[1].source = left;
        expect_cursor_refs(decision, space, semantics, &query, independent, 1u);
        /* Equal observations preserve both occurrences of the same clause. */
        arguments[1] = arguments[0];
        expect_cursor_refs(decision, space, semantics, &query, all, 3u);
        arguments[1] = (CettaGsltTermCursorV1){right, NULL};
        bindings_builder_rollback(&builder, mark);
        expect_cursor_refs(decision, space, semantics, &query, all, 3u);
        assert(bindings_dense_epoch_frame_prepare(
            &frame, &builder, ids, variables, 1u, epoch, first));
        /* An unbound authored variable cannot borrow its ordinary namesake. */
        expect_cursor_refs(decision, space, semantics, &query, all, 3u);
        assert(bindings_builder_add_id_fresh(&builder,
            var_epoch_id(source->var_id, epoch), source->sym_id, b));
        assert(bindings_dense_epoch_frame_refresh(&frame, &builder));
        expect_cursor_refs(decision, space, semantics, &query, all, 3u);
        /* A bounded observation may be less precise than complete forcing.
         * It must leave the unknown suffix to the authoritative matcher. */
        Atom *deep_left = source, *deep_right = a;
        for (size_t depth = 0u; depth < 40u; depth++) {
            deep_left = atom_expr2(arena, box, deep_left);
            deep_right = atom_expr2(arena, box, deep_right);
        }
        arguments[0] = (CettaGsltTermCursorV1){deep_left, &frame};
        arguments[1] = (CettaGsltTermCursorV1){deep_right, NULL};
        expect_cursor_refs(decision, space, semantics, &query, all, 3u);
        Atom *forced = bindings_apply_dense_epoch_frame_then_all(
            &builder, arena, deep_left, &frame);
        assert(forced);
        expect_refs(decision, space, atom_expr3(arena, head, forced, deep_right),
                    semantics, UINT64_MAX, independent, 1u);
        const size_t width = 8192u;
        Atom **fields = malloc(width * sizeof(*fields));
        assert(fields);
        for (size_t index = 0u; index < width; index++) fields[index] = a;
        fields[width - 1u] = source;
        Atom *wide_left = atom_expr(arena, fields, width);
        fields[width - 1u] = a;
        Atom *wide_right = atom_expr(arena, fields, width);
        free(fields);
        arguments[0] = (CettaGsltTermCursorV1){wide_left, &frame};
        arguments[1] = (CettaGsltTermCursorV1){wide_right, NULL};
        expect_cursor_refs(decision, space, semantics, &query, all, 3u);
        forced = bindings_apply_dense_epoch_frame_then_all(
            &builder, arena, wide_left, &frame);
        assert(forced);
        expect_refs(decision, space, atom_expr3(arena, head, forced, wide_right),
                    semantics, UINT64_MAX, independent, 1u);
        cetta_match_decision_free(decision);
        bindings_dense_epoch_frame_free(&frame);
        bindings_builder_free(&builder);
    }
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

    test_scoped_cursors(&persistent, &space, semantic_identity, false);
    test_scoped_cursors(&persistent, &space, semantic_identity, true);
    test_pivot_observation_demand(&persistent, &space, semantic_identity);

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
    Atom *open_expression_head = parse_one(&persistent,
        "(f ($head $proof (-> $domain $codomain)))");
    expect_refs(deep, &space, open_expression_head, semantic_identity,
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

    /* An unknown head retains every compatible identity. Independent arity
     * and child observations still refute, in both indexed and generic
     * policies and every prefix/equality realization. Five distinct lists
     * also exceed the complete-key shortcut's three-list bound. */
    CettaMatchDecisionClause partial_clauses[] = {
        {parse_one(&persistent, "(partial (a one))"), 1u},
        {parse_one(&persistent, "(partial (b one))"), 2u},
        {parse_one(&persistent, "(partial (c one))"), 3u},
        {parse_one(&persistent, "(partial (d one))"), 4u},
        {parse_one(&persistent, "(partial (e one))"), 5u},
        {parse_one(&persistent, "(partial (a one))"), 6u},
        {parse_one(&persistent, "(partial (a one two))"), 7u},
    };
    Atom *partial_query = parse_one(&persistent, "(partial ($head one))");
    Atom *partial_mismatch = parse_one(&persistent, "(partial ($head wrong))");
    Atom *partial_arity = parse_one(&persistent, "(partial ($head one two))");
    Atom *partial_closed = parse_one(&persistent, "(partial (a one))");
    const uint32_t partial_refs[] = {1u, 2u, 3u, 4u, 5u, 6u};
    const uint32_t partial_closed_refs[] = {1u, 6u};
    const uint32_t partial_arity_refs[] = {7u};
    assert(partial_query && partial_mismatch && partial_arity && partial_closed);
    for (size_t i = 0u; i < 7u; i++) {
        Bindings witness;
        bindings_init(&witness);
        assert(match_atoms(partial_query, partial_clauses[i].pattern,
                           &witness) == (i < 6u));
        bindings_free(&witness);
    }
    for (unsigned mode = CETTA_MATCH_DECISION_DEEP;
         mode <= CETTA_MATCH_DECISION_CONJUNCTIVE; mode++) {
        for (unsigned realization = 0u; realization < 8u; realization++) {
            CettaMatchDecision *partial = cetta_match_decision_compile(
                space_read_token(&space), semantic_identity,
                partial_clauses, 7u, (CettaMatchDecisionMode)mode, 0u,
                (CettaMatchDecisionRealization){
                    .use_direct_prefix_observation = (realization & 1u) != 0u,
                    .use_eager_prefix_observation = (realization & 2u) != 0u,
                    .use_direct_equality_observation = (realization & 4u) != 0u,
                }, NULL, NULL);
            assert(partial);
            expect_refs(partial, &space, partial_query, semantic_identity,
                        UINT64_MAX, partial_refs, 6u);
            expect_part_refs(partial, &space, partial_query, semantic_identity,
                             UINT64_MAX, partial_refs, 6u);
            expect_refs(partial, &space, partial_mismatch, semantic_identity,
                        UINT64_MAX, NULL, 0u);
            expect_refs(partial, &space, partial_arity, semantic_identity,
                        UINT64_MAX, partial_arity_refs, 1u);
            expect_refs(partial, &space, partial_closed, semantic_identity,
                        UINT64_MAX, partial_closed_refs, 2u);
            cetta_match_decision_free(partial);
        }
    }

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
    cetta_match_decision_free(prefix);

    /* Wide literal families exercise high-cardinality selection.  Wildcard
     * and duplicate exact occurrences remain an ordered bag; the cost model
     * is measured by the benchmark, not prescribed by this semantic test. */
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

    /* A full-read decision also belongs to exactly one live Space revision. */
    space_add(&space, parse_one(&persistent, "mutation"));
    assert(!cetta_match_decision_is_current(
        deep, &space, semantic_identity));
    assert(cetta_match_decision_select(
               deep, &space, semantic_identity,
               ax1_query, UINT64_MAX,
               NULL, NULL, &stale, &stale_count) ==
           CETTA_MATCH_DECISION_SELECT_INVALIDATED);
    assert(!stale && stale_count == 0u);

    /* A PeTTa clause selector is more narrowly derived: data is outside its
       equation-pattern input, while a program edit still invalidates it. */
    Atom *equation_a = parse_one(
        &persistent, "(= (projection-case alpha) first)");
    Atom *equation_b = parse_one(
        &persistent, "(= (projection-case beta) second)");
    assert(equation_a && equation_b);
    space_add(&space, equation_a);
    space_add(&space, equation_b);
    Atom *projection_query = parse_one(
        &persistent, "(projection-case alpha)");
    assert(projection_query);
    CettaMatchDecisionClause projection_clauses[] = {
        {equation_a->expr.elems[1], 0u},
        {equation_b->expr.elems[1], 1u},
    };
    CettaMatchDecision *projection =
        cetta_match_decision_compile_equation_projection(
            space_equation_token(&space), semantic_identity,
            projection_clauses, 2u, CETTA_MATCH_DECISION_DEEP, 0u,
            (CettaMatchDecisionRealization){0}, NULL, NULL);
    assert(projection);
    const uint32_t projection_refs[] = {0u};
    expect_refs(projection, &space, projection_query, semantic_identity,
                UINT64_MAX, projection_refs, 1u);
    space_add(&space, parse_one(&persistent, "data-only-edit"));
    assert(cetta_match_decision_is_current(
        projection, &space, semantic_identity));
    expect_refs(projection, &space, projection_query, semantic_identity,
                UINT64_MAX, projection_refs, 1u);
    space_add(&space, parse_one(
        &persistent, "(= (projection-case gamma) third)"));
    assert(!cetta_match_decision_is_current(
        projection, &space, semantic_identity));
    assert(cetta_match_decision_select(
               projection, &space, semantic_identity,
               projection_query, UINT64_MAX,
               NULL, NULL, &stale, &stale_count) ==
           CETTA_MATCH_DECISION_SELECT_INVALIDATED);
    assert(!stale && stale_count == 0u);
    cetta_match_decision_free(projection);

    test_bounded_path_precision(&persistent, &space, semantic_identity);
    test_scoped_equality(&persistent, &space, semantic_identity);

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
