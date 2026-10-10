#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "space.h"
#include "space_match_backend.h"
#include "symbol.h"

enum { RANDOM_LEVELS = 12, ROWS_PER_LEVEL = 6, LONG_LEVELS = 127 };

typedef struct { int64_t source, target, value; } Row;
typedef struct { __int128 count, sum; } Moment;

static Atom *record(Arena *arena, SymbolId head, size_t level,
                    Atom *source, Atom *target, Atom *value) {
    Atom *items[] = {atom_symbol_id(arena, head), atom_int(arena, (int64_t)level),
                     source, target, value};
    return atom_expr(arena, items, 5u);
}

static Atom *variable(Arena *arena, const char *kind, size_t index) {
    char name[48];
    assert(snprintf(name, sizeof(name), "%s-%zu", kind, index) > 0);
    return atom_var(arena, name);
}

static Atom *pattern(Arena *arena, SymbolId head, size_t level) {
    return record(arena, head, level, variable(arena, "key", level),
                  variable(arena, "key", level + 1u), variable(arena, "value", level));
}

static void add_row(Space *space, Arena *arena, SymbolId head, size_t level, Row row) {
    Atom *fact = record(arena, head, level, atom_int(arena, row.source),
                        atom_int(arena, row.target), atom_int(arena, row.value));
    AtomId id = term_universe_store_atom_id(space->native.universe, NULL, fact);
    assert(id != CETTA_ATOM_ID_NONE);
    space_add_atom_id(space, id);
}

/* Enumerate row occurrences directly. The reference has no message tables,
 * matching engine, trie, or aggregate implementation in its calculation. */
static Moment enumerate(const Row rows[RANDOM_LEVELS][ROWS_PER_LEVEL],
                         size_t levels, size_t level, int64_t key,
                         size_t column, int64_t selected) {
    if (level == levels)
        return (Moment){1, selected};
    Moment total = {0};
    for (size_t index = 0u; index < ROWS_PER_LEVEL; index++) {
        Row row = rows[level][index];
        if (level > 0u && row.source != key)
            continue;
        Moment suffix = enumerate(rows, levels, level + 1u, row.target, column,
                                   level == column ? row.value : selected);
        total.count += suffix.count;
        total.sum += suffix.sum;
    }
    return total;
}

static uint32_t random_word(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state >> 16u;
}

static void random_paths(TermUniverse *universe, Arena *arena, SymbolId head) {
    uint32_t state = 8917u;
    for (size_t trial = 0u; trial < 8u; trial++) {
        Row rows[RANDOM_LEVELS][ROWS_PER_LEVEL];
        Atom *patterns[RANDOM_LEVELS];
        Space space;
        space_init_with_universe(&space, universe);
        space.kind = SPACE_KIND_STACK;
        for (size_t level = 0u; level < RANDOM_LEVELS; level++) {
            patterns[level] = pattern(arena, head, level);
            for (size_t index = 0u; index < ROWS_PER_LEVEL; index++) {
                Row row = {(int64_t)(random_word(&state) % 3u),
                           (int64_t)(random_word(&state) % 3u),
                           (int64_t)(random_word(&state) % 15u) - 7};
                if (index + 1u == ROWS_PER_LEVEL)
                    row = rows[level][0];
                rows[level][index] = row;
                add_row(&space, arena, head, level, row);
            }
        }
        for (size_t levels = 1u; levels <= RANDOM_LEVELS; levels++) {
            for (size_t column = 0u; column < levels; column++) {
                Moment expected = enumerate(rows, levels, 0u, 0, column, 0);
                Moment actual = {-1, -1};
                assert(space_native_flat_chain_aggregate(&space, patterns, levels,
                    true, column, 4u, &actual.count, &actual.sum));
                assert(actual.count == expected.count);
                assert(actual.sum == expected.sum);
            }
            Moment expected = enumerate(rows, levels, 0u, 0, 0u, 0);
            Moment actual = {-1, -1};
            assert(space_native_flat_chain_aggregate(&space, patterns, levels,
                false, 0u, 0u, &actual.count, &actual.sum));
            assert(actual.count == expected.count && actual.sum == 0);
        }
        /* Identifying a non-neighbour's key adds a correlation that a
         * single-key path message cannot preserve. */
        Atom *original = patterns[3];
        patterns[3] = record(arena, head, 3u, variable(arena, "key", 0u),
                              variable(arena, "key", 4u), variable(arena, "value", 3u));
        Moment sentinel = {19, 23};
        assert(!space_native_flat_chain_aggregate(&space, patterns, RANDOM_LEVELS,
            false, 0u, 0u, &sentinel.count, &sentinel.sum));
        assert(sentinel.count == 19 && sentinel.sum == 23);
        patterns[3] = original;
        space_free(&space);
    }
}

static void large_product(TermUniverse *universe, Arena *arena, SymbolId head) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    Atom *patterns[LONG_LEVELS];
    for (size_t level = 0u; level < LONG_LEVELS; level++) {
        patterns[level] = pattern(arena, head, level);
        add_row(&space, arena, head, level, (Row){0, 0, 1});
        add_row(&space, arena, head, level, (Row){0, 0, 1});
    }
    for (size_t levels = 4u; levels < LONG_LEVELS; levels++) {
        Moment actual = {0};
        assert(space_native_flat_chain_aggregate(&space, patterns, levels,
            true, levels / 2u, 4u, &actual.count, &actual.sum));
        assert(actual.count == ((__int128)1 << levels));
        assert(actual.sum == actual.count);
    }
    Moment sentinel = {19, 23};
    assert(!space_native_flat_chain_aggregate(&space, patterns, LONG_LEVELS,
        false, 0u, 0u, &sentinel.count, &sentinel.sum));
    assert(sentinel.count == 19 && sentinel.sum == 23);
    uint64_t count = 99u;
    assert(space_native_flat_chain_count(&space, arena, patterns, 63u, NULL, &count));
    assert(count == (UINT64_C(1) << 63u));
    count = 99u;
    assert(!space_native_flat_chain_count(&space, arena, patterns, 64u, NULL, &count));
    assert(count == 99u);
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        true, 4u, 4u, &sentinel.count, &sentinel.sum));
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        true, 2u, 5u, &sentinel.count, &sentinel.sum));
    space_free(&space);
}

static void admission_and_changes(TermUniverse *universe, Arena *arena,
                                  SymbolId head) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    Atom *patterns[4];
    for (size_t level = 0u; level < 4u; level++) {
        patterns[level] = pattern(arena, head, level);
        add_row(&space, arena, head, level, (Row){0, 0, -3});
    }
    Moment actual = {0};
    assert(space_native_flat_chain_aggregate(&space, patterns, 4u,
        true, 2u, 4u, &actual.count, &actual.sum));
    assert(actual.count == 1 && actual.sum == -3);
    add_row(&space, arena, head, 1u, (Row){0, 0, 7});
    assert(space_native_flat_chain_aggregate(&space, patterns, 4u,
        true, 2u, 4u, &actual.count, &actual.sum));
    assert(actual.count == 2 && actual.sum == -6);
    assert(space_remove(&space, space_get_at64(&space, 2u)));
    assert(space_native_flat_chain_aggregate(&space, patterns, 4u,
        true, 2u, 4u, &actual.count, &actual.sum));
    assert(actual.count == 0 && actual.sum == 0);

    Moment sentinel = {19, 23};
    /* Two shared keys, a disconnected premise and a cycle require richer
     * messages than this route's one outgoing key. */
    Atom *original = patterns[1];
    patterns[1] = record(arena, head, 1u, variable(arena, "key", 1u),
                          variable(arena, "key", 2u), variable(arena, "value", 0u));
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        false, 0u, 0u, &sentinel.count, &sentinel.sum));
    patterns[1] = record(arena, head, 1u, variable(arena, "other", 0u),
                          variable(arena, "other", 1u), variable(arena, "other", 2u));
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        false, 0u, 0u, &sentinel.count, &sentinel.sum));
    patterns[1] = original;
    original = patterns[3];
    patterns[3] = record(arena, head, 3u, variable(arena, "key", 3u),
                          variable(arena, "key", 0u), variable(arena, "value", 3u));
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        false, 0u, 0u, &sentinel.count, &sentinel.sum));
    patterns[3] = original;
    /* A relevant open fact cannot be summarized by ground integer messages.
     * Count-only reads can still admit a ground, noninteger payload. */
    Atom *open = record(arena, head, 0u, variable(arena, "open", 0u),
                        atom_int(arena, 0), atom_int(arena, 1));
    AtomId open_id = term_universe_store_atom_id(universe, NULL, open);
    assert(open_id != CETTA_ATOM_ID_NONE);
    space_add_atom_id(&space, open_id);
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        false, 0u, 0u, &sentinel.count, &sentinel.sum));
    assert(space_remove_atom_id(&space, open_id));
    Atom *nonnumeric = record(arena, head, 2u, atom_int(arena, 0),
                              atom_int(arena, 0), atom_symbol(arena, "payload"));
    AtomId payload_id = term_universe_store_atom_id(universe, NULL, nonnumeric);
    assert(payload_id != CETTA_ATOM_ID_NONE);
    space_add_atom_id(&space, payload_id);
    assert(space_native_flat_chain_aggregate(&space, patterns, 4u,
        false, 0u, 0u, &actual.count, &actual.sum));
    assert(actual.count == 2 && actual.sum == 0);
    assert(!space_native_flat_chain_aggregate(&space, patterns, 4u,
        true, 2u, 4u, &sentinel.count, &sentinel.sum));
    assert(sentinel.count == 19 && sentinel.sum == 23);
    space_free(&space);
}

static void caller_binding_views(TermUniverse *universe, Arena *arena,
                                 SymbolId head) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    Atom *patterns[4];
    for (size_t level = 0u; level < 4u; level++) {
        patterns[level] = pattern(arena, head, level);
        add_row(&space, arena, head, level, (Row){0, 0, -3});
        add_row(&space, arena, head, level, (Row){1, 1, 5});
    }
    uint64_t count = 99u;
    assert(space_native_flat_chain_count(&space, arena, patterns, 4u, NULL, &count));
    assert(count == 2u);
    Bindings seed;
    bindings_init(&seed);
    Atom *first = variable(arena, "key", 0u);
    assert(bindings_add_var(&seed, first, atom_int(arena, 0)));
    assert(space_native_flat_chain_count(&space, arena, patterns, 4u, &seed, &count));
    assert(count == 1u);
    assert(patterns[0]->expr.elems[2]->kind == ATOM_VAR);
    bindings_free(&seed);

    bindings_init(&seed);
    assert(bindings_add_var(&seed, variable(arena, "key", 4u), atom_int(arena, 1)));
    assert(space_native_flat_chain_count(&space, arena, patterns, 4u, &seed, &count));
    assert(count == 1u);
    bindings_free(&seed);

    /* Fixing an internal separator disconnects this encoding; identifying
     * its endpoints creates a cycle. Neither is the admitted path any more,
     * and ordinary conjunction counting retains both correlations. */
    bindings_init(&seed);
    assert(bindings_add_var(&seed, variable(arena, "key", 1u), atom_int(arena, 0)));
    count = 99u;
    assert(!space_native_flat_chain_count(&space, arena, patterns, 4u, &seed, &count));
    assert(count == 99u);
    assert(space_match_count_conjunction64(&space, arena, patterns, 4u, &seed, &count));
    assert(count == 1u);
    bindings_free(&seed);

    bindings_init(&seed);
    assert(bindings_add_var(&seed, variable(arena, "key", 4u), first));
    count = 99u;
    assert(!space_native_flat_chain_count(&space, arena, patterns, 4u, &seed, &count));
    assert(count == 99u);
    assert(space_match_count_conjunction64(&space, arena, patterns, 4u, &seed, &count));
    assert(count == 2u);
    bindings_free(&seed);
    assert(space_native_flat_chain_count(&space, arena, patterns, 4u, NULL, &count));
    assert(count == 2u);
    space_free(&space);
}

static void repeated_count_probes(TermUniverse *universe, Arena *arena, SymbolId head) {
    Space space;
    space_init_with_universe(&space, universe);
    space.kind = SPACE_KIND_STACK;
    for (size_t key = 0u; key < 1024u; key++)
        add_row(&space, arena, head, 0u, (Row){(int64_t)key, 7, 9});
    Atom *query = pattern(arena, head, 0u);
    for (size_t key = 0u; key < 16u; key++) {
        Bindings seed;
        bindings_init(&seed);
        assert(bindings_add_var(&seed, variable(arena, "key", 0u), atom_int(arena, (int64_t)key)));
        CettaGsltTermViewV1 view = bindings_term_view_v1(query, &seed);
        uint64_t count = 0u;
        CettaIndex examined = 0u;
        assert(space_match_count_flat_linear_view64(&space, arena, &view,
            CETTA_GSLT_TERM_VIEW_OPEN_BINDINGS_DEAD_V1, &count, &examined));
        assert(count == 1u);
        if (key == 15u)
            assert(examined < 16u);
        bindings_free(&seed);
    }
    /* Reuse the demanded access path after occurrence-preserving updates. */
    add_row(&space, arena, head, 0u, (Row){15, 7, 9});
    add_row(&space, arena, head, 0u, (Row){15, 7, 9});
    Atom *bound = record(arena, head, 0u, atom_int(arena, 15),
        variable(arena, "open", 0u), variable(arena, "open", 1u));
    uint64_t count = 0u;
    CettaIndex examined = 0u;
    assert(space_match_count_flat_linear64(&space, arena, bound, &count, &examined));
    assert(count == 3u && examined < 16u);
    Atom *occurrence = record(arena, head, 0u, atom_int(arena, 15),
        atom_int(arena, 7), atom_int(arena, 9));
    assert(space_remove(&space, occurrence));
    assert(space_match_count_flat_linear64(&space, arena, bound, &count, &examined));
    assert(count == 2u && examined < 16u);
    /* An absent key stays absent after an indexed probe. */
    Atom *absent = record(arena, head, 0u, atom_int(arena, 2048),
        variable(arena, "open", 0u), variable(arena, "open", 1u));
    assert(space_match_count_flat_linear64(&space, arena, absent, &count, &examined));
    assert(count == 0u);
    space_free(&space);
    puts("PASS: repeated count views demand bounded candidates and preserve duplicate, removed and absent occurrences");
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    Arena persistent, arena;
    arena_init(&persistent);
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    SymbolId head = symbol_intern_cstr(&symbols, "path-row");
    random_paths(&universe, &arena, head);
    large_product(&universe, &arena, head);
    admission_and_changes(&universe, &arena, head);
    caller_binding_views(&universe, &arena, head);
    repeated_count_probes(&universe, &arena, head);
    term_universe_free(&universe);
    arena_free(&arena);
    arena_free(&persistent);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: long path aggregates match occurrence enumeration and retain overflow boundaries");
    return 0;
}
