#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "space.h"
#include "space_match_backend.h"
#include "shared_transition.h"
#include "symbol.h"
#include "fallible_alloc.h"

/* Link with --wrap=cetta_fallible_malloc. Only the private factory's
 * recoverable allocations are armed; ordinary runtime allocation is intact. */
void *__real_cetta_fallible_malloc(size_t size);
static unsigned allocation_call, fail_allocation;
static TermUniverse *inspect_universe, *migrate_universe;
static Space *inspect_source;
static bool saw_private_prefix;
static const char *store_phase;

void *__wrap_cetta_fallible_malloc(size_t size) {
    allocation_call++;
    if (migrate_universe && allocation_call == 2u) {
        TermUniverse *universe = migrate_universe;
        migrate_universe = NULL;
        assert(term_universe_migrate_store_format(
            universe, TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1));
    }
    if (fail_allocation && allocation_call == fail_allocation) {
        if (inspect_universe) {
            for (size_t i = 0u; i < inspect_universe->root_provider_len; i++) {
                TermUniverseRootProviderEntry *entry =
                    &inspect_universe->root_providers[i];
                if (entry->counted && entry->resource_owner == entry->ctx &&
                    entry->ctx != inspect_source) {
                    Space *draft = entry->ctx;
                    saw_private_prefix |= draft->native.len >= 1025u;
                }
            }
        }
        return NULL;
    }
    return __real_cetta_fallible_malloc(size);
}

static AtomId store(TermUniverse *universe, Atom *atom) {
    AtomId id = term_universe_store_atom_id(universe, NULL, atom);
    if (id == CETTA_ATOM_ID_NONE)
        fprintf(stderr, "store failed: phase=%s kind=%d error=%s injection=%u/%u\n",
                store_phase, atom ? (int)atom->kind : -1,
                term_universe_error_name(term_universe_last_error_code(universe)),
                allocation_call, fail_allocation);
    assert(id != CETTA_ATOM_ID_NONE);
    return id;
}

static Atom *pair(Arena *arena, SymbolId head, Atom *a, Atom *b) {
    Atom *items[] = {atom_symbol_id(arena, head), a, b};
    return atom_expr(arena, items, 3u);
}

static int compare_ids(const void *a, const void *b) {
    AtomId left = *(const AtomId *)a, right = *(const AtomId *)b;
    return (left > right) - (left < right);
}

static void assert_bag(Space *copy, const AtomId *expected, size_t count) {
    assert(space_length64(copy) == count);
    AtomId *actual = malloc(sizeof(*actual) * count);
    AtomId *sorted = malloc(sizeof(*sorted) * count);
    assert(actual && sorted);
    memcpy(sorted, expected, sizeof(*sorted) * count);
    for (size_t i = 0u; i < count; i++) {
        actual[i] = space_get_atom_id_at64(copy, i);
        assert(actual[i] != CETTA_ATOM_ID_NONE);
    }
    qsort(actual, count, sizeof(*actual), compare_ids);
    qsort(sorted, count, sizeof(*sorted), compare_ids);
    assert(memcmp(actual, sorted, sizeof(*actual) * count) == 0);
    free(actual);
    free(sorted);
}

static void dispose(Space *space) {
    space_free(space);
    free(space);
}

enum { RAW_ROWS = 1025u, PAIRS = 4200u, SOURCE_ROWS = RAW_ROWS + PAIRS + 2u };

static void test_compact_copy_and_failure(Arena *arena, bool wide) {
    Arena persistent;
    arena_init_detached(&persistent);
    TermUniverse universe;
    assert(term_universe_init_with_store_format(&universe,
        wide ? TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1
             : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1));
    term_universe_set_persistent_arena(&universe, &persistent);
    Space source;
    space_init_with_universe(&source, &universe);
    SymbolId edge = symbol_intern_cstr(g_symbols, "copy-edge");
    SymbolId open = symbol_intern_cstr(g_symbols, "copy-open");
    AtomId expected[SOURCE_ROWS * 2u], tuples[PAIRS * 2u];
    store_phase = "raw scalar setup";
    for (size_t i = 0u; i < RAW_ROWS; i++) {
        expected[i] = store(&universe, atom_int(arena, (int64_t)i + 100000));
        space_add_atom_id(&source, expected[i]);
    }
    Atom *x = atom_var_with_spelling(arena,
        symbol_intern_cstr(g_symbols, "copy-x"), var_id_from_base(170001u));
    store_phase = "open fact setup";
    expected[RAW_ROWS] = store(&universe, pair(arena, open, x, x));
    space_add_atom_id(&source, expected[RAW_ROWS]);
    store_phase = "head and column setup";
    AtomId head = store(&universe, atom_symbol_id(arena, edge));
    for (size_t i = 0u; i < PAIRS; i++) {
        tuples[2u * i] = store(&universe, atom_int(arena, (int64_t)i));
        tuples[2u * i + 1u] = store(&universe, atom_int(arena, (int64_t)i + 1));
        AtomId parts[] = {head, tuples[2u * i], tuples[2u * i + 1u]};
        expected[RAW_ROWS + 1u + i] = tu_expr_from_ids(&universe, parts, 3u);
        assert(expected[RAW_ROWS + 1u + i] != CETTA_ATOM_ID_NONE);
    }
    assert(space_add_dense_tuples(&source, edge, head, 2u, tuples, PAIRS));
    expected[SOURCE_ROWS - 1u] = expected[RAW_ROWS + 2u];
    space_add_atom_id(&source, expected[SOURCE_ROWS - 1u]);
    assert(space_dense_facts(&source) == PAIRS);
    assert(space_length64(&source) == SOURCE_ROWS);
    memcpy(expected + SOURCE_ROWS, expected, SOURCE_ROWS * sizeof(*expected));
    const uint64_t revision = source.revision, prefix = source.prefix_epoch;
    const uint64_t epoch = source.native.dense_epoch;
    const CettaCount raw_length = source.native.len;
    size_t providers = universe.root_provider_len;
    size_t observers = universe.observer_len;

    Space *copy = (Space *)(uintptr_t)1u;
    allocation_call = 0u;
    fail_allocation = 2u;
    inspect_universe = &universe;
    inspect_source = &source;
    saw_private_prefix = false;
    assert(space_heap_additive_copy(&universe, &source, &source, &copy) ==
           SPACE_ADDITIVE_COPY_FAILED);
    assert(copy == NULL && saw_private_prefix);
    fail_allocation = 0u;
    inspect_universe = NULL;
    assert(universe.root_provider_len == providers);
    assert(universe.observer_len == observers);
    assert(source.revision == revision && source.prefix_epoch == prefix);
    assert(source.native.dense_epoch == epoch && source.native.len == raw_length);
    assert(space_dense_facts(&source) == PAIRS);

    /* Capture before copying, then change the source while the reader is
     * suspended. Its original occurrence bag must remain available. */
    Atom *y = atom_var_with_spelling(arena,
        symbol_intern_cstr(g_symbols, "copy-y"), var_id_from_base(170002u));
    Atom *query = pair(arena, edge, x, y);
    SpaceOccurrenceCursor reader;
    assert(space_occurrence_cursor_init(&source, query, &reader));
    assert(reader.kept != NULL);
    allocation_call = 0u;
    migrate_universe = wide ? NULL : &universe;
    assert(space_heap_additive_copy(&universe, &source, &source, &copy) ==
           SPACE_ADDITIVE_COPY_READY);
    assert(copy && copy != &source);
    assert(source.revision == revision && source.prefix_epoch == prefix);
    assert(source.native.dense_epoch == epoch && source.native.len == raw_length);
    assert(space_dense_facts(&source) == PAIRS);
    assert(source.native.atom_id_width_bits == 64u);
    assert(space_dense_facts(copy) != 0u);
    assert_bag(copy, expected, SOURCE_ROWS * 2u);
    Space empty;
    space_init_with_universe(&empty, &universe);
    Space *unique = NULL;
    assert(space_heap_additive_copy(&universe, &source, &empty, &unique) ==
           SPACE_ADDITIVE_COPY_READY);
    assert(space_dense_facts(unique) != 0u);
    assert(space_remove(&source, pair(arena, edge,
        atom_int(arena, 0), atom_int(arena, 1))));
    AtomId captured[PAIRS + 1u];
    size_t captured_count = 0u;
    CettaIndex index;
    Atom *value;
    store_phase = "captured reader oracle";
    while (space_occurrence_cursor_next(&reader, &index, &value) ==
           SPACE_OCCURRENCE_CURSOR_ITEM) {
        if (atom_head_symbol_id(value) != edge)
            continue;
        assert(captured_count < PAIRS + 1u);
        captured[captured_count++] = store(&universe, value);
    }
    assert(captured_count == PAIRS + 1u);
    qsort(captured, captured_count, sizeof(*captured), compare_ids);
    AtomId original_edges[PAIRS + 1u];
    memcpy(original_edges, expected + RAW_ROWS + 1u,
           sizeof(original_edges));
    qsort(original_edges, PAIRS + 1u, sizeof(*original_edges), compare_ids);
    assert(memcmp(captured, original_edges, sizeof(captured)) == 0);
    space_occurrence_cursor_release(&reader);
    dispose(copy);
    space_free(&source);
    term_universe_set_collection_mode(&universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(term_universe_collect(&universe, NULL));
    /* A kept fact need not retain its expression's record or its old ID.
     * Reconstruct the independent expected values after collection, using
     * the original column IDs that the destination must still retain. */
    for (size_t i = 0u; i < PAIRS; i++) {
        AtomId parts[] = {head, tuples[2u * i], tuples[2u * i + 1u]};
        expected[RAW_ROWS + 1u + i] = tu_expr_from_ids(&universe, parts, 3u);
        assert(expected[RAW_ROWS + 1u + i] != CETTA_ATOM_ID_NONE);
    }
    expected[SOURCE_ROWS - 1u] = expected[RAW_ROWS + 2u];
    assert_bag(unique, expected, SOURCE_ROWS);
    dispose(unique);
    space_free(&empty);
    term_universe_free(&universe);
    arena_free(&persistent);
}

static void test_live_order_and_unsupported(Arena *arena) {
    store_phase = "live order setup";
    Arena persistent, other_persistent;
    arena_init_detached(&persistent);
    arena_init_detached(&other_persistent);
    TermUniverse universe, other;
    term_universe_init(&universe);
    term_universe_init(&other);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_persistent_arena(&other, &other_persistent);
    Space source, empty, queue;
    space_init_with_universe(&source, &universe);
    space_init_with_universe(&empty, &universe);
    space_init_with_universe(&queue, &universe);
    queue.kind = SPACE_KIND_QUEUE;
    AtomId rows[70];
    for (size_t i = 0u; i < 70u; i++) {
        rows[i] = store(&universe, atom_int(arena, (int64_t)i));
        space_add_atom_id(&source, rows[i]);
        space_add_atom_id(&queue, rows[i]);
    }
    CettaIndex removed[] = {1u, 17u};
    assert(space_remove_logical_positions(&source, removed, 2u));
    assert(source.native.marked == 2u);
    Atom *popped;
    for (size_t i = 0u; i < 3u; i++)
        assert(space_pop(&queue, &popped));
    assert(queue.native.start != 0u);
    Space *copy = NULL;
    assert(space_heap_additive_copy(&universe, &source, &queue, &copy) ==
           SPACE_ADDITIVE_COPY_READY);
    assert(source.native.marked == 2u && queue.native.start == 3u);
    size_t at = 0u;
    for (size_t i = 0u; i < 70u; i++) {
        if (i != 1u && i != 17u)
            assert(space_get_atom_id_at64(copy, at++) == rows[i]);
    }
    for (size_t i = 3u; i < 70u; i++)
        assert(space_get_atom_id_at64(copy, at++) == rows[i]);
    assert(space_length64(copy) == at);
    dispose(copy);

    copy = (Space *)(uintptr_t)1u;
    assert(space_heap_additive_copy(&other, &source, &empty, &copy) ==
           SPACE_ADDITIVE_COPY_UNSUPPORTED && copy == NULL);
    source.imported_len = 1u;
    assert(space_heap_additive_copy(&universe, &source, &empty, &copy) ==
           SPACE_ADDITIVE_COPY_UNSUPPORTED && copy == NULL);
    source.imported_len = 0u;
    Space overlay;
    space_init_overlay(&overlay, &source);
    assert(space_heap_additive_copy(&universe, &overlay, &empty, &copy) ==
           SPACE_ADDITIVE_COPY_UNSUPPORTED && copy == NULL);
    space_free(&overlay);
    cetta_shared_transition_scope_enter();
    assert(space_heap_additive_copy(&universe, &source, &empty, &copy) ==
           SPACE_ADDITIVE_COPY_UNSUPPORTED && copy == NULL);
    cetta_shared_transition_scope_leave();
    space_free(&queue);
    space_free(&empty);
    space_free(&source);
    term_universe_free(&other);
    term_universe_free(&universe);
    arena_free(&other_persistent);
    arena_free(&persistent);
}

static void test_mutable_endpoint_and_deep_fallback(Arena *arena) {
    store_phase = "mutable endpoint setup";
    Arena persistent;
    arena_init_detached(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space source, empty, endpoint, deep;
    space_init_with_universe(&source, &universe);
    space_init_with_universe(&empty, &universe);
    space_init_with_universe(&endpoint, &universe);
    space_init_with_universe(&deep, &universe);
    AtomId resource = store(&universe, atom_space(arena, &endpoint));
    space_add_atom_id(&source, resource);
    space_add_atom_id(&source, resource);
    Space *copy = NULL;
    assert(space_heap_additive_copy(&universe, &source, &source, &copy) ==
           SPACE_ADDITIVE_COPY_READY);
    space_free(&source);
    Atom *after = atom_symbol(arena, "copy-resource-after");
    space_add(&endpoint, after);
    term_universe_set_collection_mode(&universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    assert(term_universe_collect(&universe, NULL));
    assert(space_length64(copy) == 4u);
    for (CettaIndex i = 0u; i < 4u; i++) {
        Atom *value = space_get_at64(copy, i);
        assert(value && value->kind == ATOM_GROUNDED &&
               value->ground.gkind == GV_SPACE && value->ground.ptr == &endpoint);
        assert(space_contains_exact(value->ground.ptr, after));
    }
    dispose(copy);

    deep.kind = SPACE_KIND_SET;
    deep.dense_from_first = true;
    SymbolId head = symbol_intern_cstr(g_symbols, "copy-deep");
    for (int64_t i = 0; i < 3; i++) {
        Atom *items[] = {atom_symbol(arena, "copy-leaf"), atom_int(arena, i)};
        space_add(&deep, pair(arena, head, atom_expr(arena, items, 2u),
                             atom_int(arena, i + 1)));
    }
    assert(space_dense_facts(&deep) != 0u);
    assert(space_heap_additive_copy(&universe, &deep, &empty, &copy) ==
           SPACE_ADDITIVE_COPY_UNSUPPORTED && copy == NULL);
    space_free(&deep);
    space_free(&endpoint);
    space_free(&empty);
    term_universe_free(&universe);
    arena_free(&persistent);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena arena;
    arena_init(&arena);
    test_compact_copy_and_failure(&arena, false);
    test_compact_copy_and_failure(&arena, true);
    test_live_order_and_unsupported(&arena);
    test_mutable_endpoint_and_deep_fallback(&arena);
    arena_free(&arena);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: additive copy bags, source forms, private failure, captured readers, widths and live order");
    return 0;
}
