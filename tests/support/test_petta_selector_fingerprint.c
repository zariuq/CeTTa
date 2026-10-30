#include "../../src/petta_specializer.c"
#include "library.h"
#include <assert.h>

static unsigned checks;

static void check_route(Arena *arena, Atom *first, Atom *wanted,
                        size_t expected) {
    Space space;
    space_init(&space);
    PettaSpecializerContext context = {.space = &space};
    Atom *head = atom_symbol(arena, "selector-route");
    Atom *base = atom_symbol(arena, "selector-callee");
    Atom *values[3] = {
        petta_semantics_partial_value(arena, base, &first, 1u),
        petta_semantics_partial_value(arena, base, &wanted, 1u),
        petta_semantics_partial_value(arena, base, &wanted, 1u),
    };
    CettaExprIndex path[] = {1u};
    PettaSpecializationBinding selectors[3] = {0};
    PettaSpecializationRecord records[3] = {0};
    for (size_t index = 0u; index < 3u; index++) {
        selectors[index] = (PettaSpecializationBinding){
            .value = values[index], .path = path, .path_len = 1u,
            .positional = true,
        };
        selectors[index].fingerprint_known = petta_selector_fingerprint(
            values[index], &selectors[index].fingerprint);
        records[index] = (PettaSpecializationRecord){
            .space = &space, .space_instance = space_instance_id(&space),
            .source = head->sym_id, .selectors = &selectors[index],
            .selector_len = 1u,
        };
    }
    PettaSpecializationRecords saved = g_petta_specializations;
    g_petta_specializations = (PettaSpecializationRecords){records, 3u, 3u};
    Atom *call = atom_expr2(arena, head, values[2]);
    assert(petta_find_record_for_call(&context, head->sym_id, call) ==
           &records[expected]);
    /* A collided selector must not win; duplicate exact selectors retain
     * their original order, and an obsolete space identity is skipped. */
    records[expected].space_instance++;
    assert(petta_find_record_for_call(&context, head->sym_id, call) ==
           &records[expected + 1u]);
    assert(!petta_find_record_for_call(&context, base->sym_id, call));
    g_petta_specializations = saved;
    space_free(&space);
    checks += 3u;
}

static void compare(Atom *left, Atom *right) {
    uint64_t first, second;
    bool first_known = petta_selector_fingerprint(left, &first);
    bool second_known = petta_selector_fingerprint(right, &second);
    bool exact = atom_eq(left, right);
    if (exact && first_known && second_known)
        assert(first == second);
    PettaSpecializationBinding selector = {
        .value = right, .fingerprint_known = second_known,
        .fingerprint = second_known ? second : 0u,
    };
    PettaSelectorObservation observations[8];
    size_t count = 0u;
    bool possible = petta_selector_may_equal(&selector, left, observations, &count);
    assert(!exact || possible);
    assert((possible && atom_eq(left, right)) == exact);
    assert(petta_selector_may_equal(&selector, left, observations, &count) == possible);
    checks++;
}


static void check_artifact_catalog(Arena *arena) {
    Space space, other;
    space_init(&space);
    space_init(&other);
    assert(g_petta_specializations.len == 0u);
    PettaSpecializerContext context = {.space = &space, .persistent = arena};
    CettaExprIndex path[] = {1u};
    PettaSpecializationBinding binding = {.value = atom_symbol(arena, "artifact-value"),
        .path = path, .path_len = 1u, .positional = true};
    PettaSpecializationBindings selected = {&binding, 1u, 1u};
    Atom *source = atom_symbol(arena, "artifact-source");
    Atom *head[18], *equation[18];
    PettaSpecializerPatternNode *pattern[18];
    PettaPlanNode plan = {.role = PETTA_PLAN_VALUE};
    for (unsigned index = 0u; index < 18u; index++) {
        char name[48];
        snprintf(name, sizeof(name), "artifact-target-%u", index);
        head[index] = atom_symbol(arena, name);
        Atom *variable = atom_var_with_id(arena, "$value", fresh_var_id());
        equation[index] = atom_expr3(arena,
            atom_symbol_id(arena, g_builtin_syms.equals),
            atom_expr2(arena, head[index], variable), variable);
        pattern[index] = calloc(1u, sizeof(*pattern[index]));
        assert(pattern[index]);
        if (index == 17u) {
            assert(!petta_artifact_head_possible(head[index]->sym_id));
            assert(!petta_specializer_pattern_root(&space, equation[index]));
        }
        PettaSpecializationRecord *record = petta_create_record(
            &context, source->sym_id, head[index]->sym_id, &selected);
        assert(record && !context.capacity);
        if (index == 17u)
            assert(g_petta_artifact_heads_attempted && g_petta_artifact_heads_complete);
        assert(petta_record_add(record, equation[index], pattern[index], NULL));
        assert(petta_record_add(record, equation[index], NULL, &plan));
        if (index >= 16u) {
            assert(petta_artifact_head_possible(head[index]->sym_id));
            assert(petta_specializer_pattern_root(&space, equation[index]) == pattern[index]);
            assert(petta_specializer_equation_plan(&space, equation[index]) == &plan);
        }
    }
    Atom *renamed = atom_var_with_id(arena, "$renamed", fresh_var_id());
    Atom *query = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.equals),
        atom_expr2(arena, head[17], renamed), renamed);
    assert(petta_specializer_pattern_root(&space, query) == pattern[17]);
    assert(!petta_specializer_pattern_root(&other, query));
    petta_remove_record_at(17u);
    assert(!petta_artifact_head_possible(head[17]->sym_id));
    assert(!petta_specializer_pattern_root(&space, query));
    petta_remove_record_at(0u);
    assert(petta_specializer_pattern_root(&space, equation[16]) == pattern[16]);
    assert(!petta_artifact_head_possible(head[0]->sym_id));
    /* Incomplete construction must permit the authoritative traversal. */
    g_petta_artifact_heads_complete = false;
    assert(petta_artifact_head_possible(head[0]->sym_id));
    assert(!petta_specializer_pattern_root(&space, equation[0]));
    petta_specializer_reset_thread();
    assert(!g_petta_artifact_heads_attempted);
    space_free(&other);
    space_free(&space);
    checks += 20u;
}

static void check_admission_cache_invalidation(Arena *arena) {
    Space space;
    space_init(&space);
    SpaceProgramToken program = space_program_token(&space);
    SymbolId source = atom_symbol(arena, "cache-generation-source")->sym_id;
    Atom *head = atom_symbol_id(arena, source);
    Atom *equation = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.equals),
        atom_expr2(arena, head, atom_var(arena, "cache-argument")),
        atom_int(arena, 7));
    Atom *declaration = atom_expr3(
        arena, atom_symbol(arena, ":"), head, atom_symbol(arena, "Atom"));
    Atom *open_equation = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.equals),
        atom_var(arena, "cache-open-head"), atom_int(arena, 9));
    uint64_t instance = space_instance_id(&space);
    for (unsigned transition = 0u; transition < 6u; transition++) {
        PettaRelationRelevance relevance = PETTA_RELATION_RELEVANCE_UNKNOWN;
        uint8_t result = 0u;
        petta_relation_relevance_cache_store(
            program, source, PETTA_RELATION_RELEVANCE_IRRELEVANT);
        petta_query_forest_cache_store(&space, instance, 13u, 17u,
                                      source, 1u, 3u);
        assert(petta_relation_relevance_cache_lookup(program, source,
                                                     &relevance));
        assert(relevance == PETTA_RELATION_RELEVANCE_IRRELEVANT);
        assert(petta_query_forest_cache_lookup(&space, instance, 13u, 17u,
                                              source, 1u, &result));
        assert(result == 3u);
        assert(!petta_query_forest_cache_lookup(&space, instance, 14u, 17u,
                                               source, 1u, &result));
        uint64_t generation = g_petta_admission_cache_generation;
        petta_specializer_note_mutation(&space, atom_int(arena, 11));
        assert(g_petta_admission_cache_generation == generation);
        assert(petta_relation_relevance_cache_lookup(program, source,
                                                     &relevance));
        assert(petta_query_forest_cache_lookup(&space, instance, 13u, 17u,
                                              source, 1u, &result));
        switch (transition) {
        case 0u: petta_specializer_note_mutation(&space, equation); break;
        case 1u: petta_specializer_note_mutation(&space, declaration); break;
        case 2u: petta_specializer_note_mutation(&space, open_equation); break;
        case 3u: petta_specializer_note_import(&space); break;
        case 4u: petta_specializer_reset_thread(); break;
        default: petta_relation_relevance_cache_clear(); break;
        }
        assert(g_petta_admission_cache_generation != generation);
        /* Even the old complete inquiry must miss after invalidation. */
        assert(!petta_relation_relevance_cache_lookup(program, source,
                                                      &relevance));
        assert(!petta_query_forest_cache_lookup(&space, instance, 13u, 17u,
                                               source, 1u, &result));
        checks += 11u;
    }
    /* Generation one can still occupy any slot when the counter reaches its
     * last value. Reuse must erase both tables, including unqueried slots. */
    for (size_t index = 0u; index < PETTA_RELATION_RELEVANCE_CACHE_SLOTS; index++)
        g_petta_relation_relevance_cache[index].generation = 1u;
    for (size_t index = 0u; index < PETTA_QUERY_FOREST_CACHE_SLOTS; index++)
        g_petta_query_forest_cache[index].generation = 1u;
    g_petta_admission_cache_generation = UINT64_MAX;
    petta_relation_relevance_cache_clear();
    assert(g_petta_admission_cache_generation == 1u);
    for (size_t index = 0u; index < PETTA_RELATION_RELEVANCE_CACHE_SLOTS; index++) {
        assert(g_petta_relation_relevance_cache[index].generation == 0u);
        checks++;
    }
    for (size_t index = 0u; index < PETTA_QUERY_FOREST_CACHE_SLOTS; index++) {
        assert(g_petta_query_forest_cache[index].generation == 0u);
        checks++;
    }
    PettaRelationRelevance relevance;
    uint8_t result;
    assert(!petta_relation_relevance_cache_lookup(program, source, &relevance));
    assert(!petta_query_forest_cache_lookup(&space, instance, 13u, 17u,
                                           source, 1u, &result));
    petta_relation_relevance_cache_store(
        program, source, PETTA_RELATION_RELEVANCE_CERTAIN);
    petta_query_forest_cache_store(&space, instance, 13u, 17u, source, 1u, 5u);
    assert(petta_relation_relevance_cache_lookup(program, source, &relevance));
    assert(relevance == PETTA_RELATION_RELEVANCE_CERTAIN);
    assert(petta_query_forest_cache_lookup(&space, instance, 13u, 17u,
                                          source, 1u, &result) && result == 5u);
    petta_specializer_reset_thread();
    space_free(&space);
    checks += 6u;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena source, copies;
    arena_init_detached(&source);
    arena_init_detached(&copies);
    CettaLibraryContext library;
    cetta_library_context_init_for_language_profile(&library, CETTA_LANGUAGE_PETTA, NULL);
    CettaLibraryContext *previous = eval_current_library_context();
    eval_set_library_context(&library);
    assert(petta_selector_fingerprint_enabled());
    check_artifact_catalog(&source);
    check_admission_cache_invalidation(&source);

    Atom *a = atom_symbol(&source, "selector-a");
    Atom *b = atom_symbol(&source, "selector-b");
    Atom *pool[512] = {a, b, atom_int(&source, 1), atom_int(&source, 2),
        atom_float(&source, 0.0), atom_float(&source, -0.0),
        atom_string(&source, "abcdefghijklmnop-one"),
        atom_string(&source, "abcdefghijklmnop-two")};
    uint32_t random = 119u;
    for (unsigned index = 8u; index < 512u; index++) {
        random = random * 1664525u + 1013904223u;
        Atom *children[4] = {index & 1u ? a : b,
            pool[(random >> 4u) % index], pool[(random >> 13u) % index],
            pool[(random >> 22u) % index]};
        pool[index] = atom_expr(&source, children, 2u + index % 3u);
        compare(pool[index], atom_deep_copy(&copies, pool[index]));
        for (unsigned other = 0u; other < 64u; other++)
            compare(pool[index], pool[(other * 7u) % index]);
    }

    /* The omitted string suffix deliberately collides. Exact equality must
     * still reject it; a fingerprint never establishes a match. */
    uint64_t first, second;
    assert(petta_selector_fingerprint(pool[6], &first));
    assert(petta_selector_fingerprint(pool[7], &second));
    assert(first == second && !atom_eq(pool[6], pool[7]));
    compare(pool[6], pool[7]);
    check_route(&source, pool[6], pool[7], 1u);

    PettaSelectorObservation observations[8];
    size_t count = 0u;
    PettaSpecializationBinding selector = {.value = pool[2]};
    selector.fingerprint_known = petta_selector_fingerprint(
        selector.value, &selector.fingerprint);
    for (int value = 10; value < 18; value++)
        assert(!petta_selector_may_equal(&selector, atom_int(&source, value),
                                        observations, &count));
    assert(count == 8u);
    assert(petta_selector_may_equal(&selector, atom_int(&source, 18),
                                    observations, &count));
    assert(count == 8u);
    checks += 11u;

    Atom *deep_a = a, *deep_b = b;
    for (unsigned depth = 0u; depth < 128u; depth++) {
        deep_a = atom_expr2(&source, a, deep_a);
        deep_b = atom_expr2(&source, a, deep_b);
    }
    assert(petta_selector_fingerprint(deep_a, &first));
    assert(petta_selector_fingerprint(deep_b, &second));
    assert(first == second && !atom_eq(deep_a, deep_b));
    compare(deep_a, deep_b);
    Atom *wide_fields[1024];
    for (unsigned index = 0u; index < 1024u; index++) wide_fields[index] = a;
    Atom *wide_a = atom_expr(&source, wide_fields, 1024u);
    wide_fields[1023] = b;
    Atom *wide_b = atom_expr(&source, wide_fields, 1024u);
    assert(petta_selector_fingerprint(wide_a, &first));
    assert(petta_selector_fingerprint(wide_b, &second));
    assert(first == second);
    compare(wide_a, wide_b);

    /* Opaque mutable contents are absent from the stored observation. */
    StateCell cell = {.value = a}, other = {.value = a};
    Atom *left = atom_expr2(&source, a, atom_state(&source, &cell));
    Atom *right = atom_expr2(&source, a, atom_state(&source, &other));
    assert(petta_selector_fingerprint(left, &first));
    compare(left, right);
    cell.value = b;
    assert(petta_selector_fingerprint(left, &second) && first == second);
    compare(left, right);

    Atom unknown;
    atom_header_copy(&unknown, atom_expr(&source, wide_fields, 1u));
    unknown.structural_facts = 0u;
    assert(!petta_selector_fingerprint(&unknown, &first));
    unknown.structural_facts = ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_HAS_RATIONAL;
    assert(!petta_selector_fingerprint(&unknown, &first));

    /* Each lookup observes a new arena epoch; the tiny local memo is not
     * carried across a call or a reset. */
    ArenaMark mark = arena_mark(&copies);
    Atom *ephemeral = atom_expr2(&copies, a, atom_int(&copies, 7));
    compare(ephemeral, atom_expr2(&source, a, atom_int(&source, 7)));
    arena_reset(&copies, mark);
    ephemeral = atom_expr2(&copies, a, atom_int(&copies, 9));
    compare(ephemeral, atom_expr2(&source, a, atom_int(&source, 7)));

    CettaLibraryContext he;
    cetta_library_context_init_for_language_profile(&he, CETTA_LANGUAGE_HE, NULL);
    eval_set_library_context(&he);
    assert(!petta_selector_fingerprint_enabled());
    left = atom_expr2(&source, a, atom_int(&source, 1));
    right = atom_expr2(&source, a, atom_float(&source, 1.0));
    assert(atom_eq(left, right));
    assert(petta_selector_fingerprint(left, &first));
    assert(petta_selector_fingerprint(right, &second) && first != second);
    check_route(&source, atom_int(&source, 1), atom_float(&source, 1.0), 0u);
    eval_set_library_context(previous);
    cetta_library_context_free(&he);
    cetta_library_context_free(&library);
    arena_free(&copies);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("PASS: %u selector/index controls; collisions, bounded depth/width, mutable payloads, reset, HE refusal and artifact invalidation\n", checks);
    return 0;
}
