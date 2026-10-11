#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "space.h"
#include "space_match_backend.h"
#include "symbol.h"

/* Measure the existing native cursor at both a prefix and full enumeration.
 * Alternating selected/rejected rows give a declaration-order oracle that
 * does not depend on the trie or on the cursor representation. */
static uint64_t now_ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static size_t heap_bytes(void) {
#ifdef __GLIBC__
    return mallinfo2().uordblks;
#else
    return 0u;
#endif
}

static uint64_t number(const char *text) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    assert(end && end != text && *end == '\0');
    return value;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s ROWS DISTINCT_KEYS PREFIX REPETITIONS\n", argv[0]);
        return 2;
    }
    CettaCount rows = number(argv[1]);
    size_t keys = (size_t)number(argv[2]);
    CettaCount prefix = number(argv[3]);
    size_t repetitions = (size_t)number(argv[4]);
    assert(rows > 16u && rows <= SIZE_MAX / sizeof(AtomId));
    assert(keys > 0u && keys <= rows && keys <= SIZE_MAX / (2u * sizeof(AtomId)));
    assert(repetitions > 0u);

    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena persistent, scratch;
    arena_init(&persistent);
    arena_init(&scratch);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);
    space.kind = SPACE_KIND_STACK;

    uint64_t load_start = now_ns();
    SymbolId head = symbol_intern_cstr(&symbols, "cursor-row");
    SymbolId selected = symbol_intern_cstr(&symbols, "selected");
    SymbolId rejected = symbol_intern_cstr(&symbols, "rejected");
    AtomId *choices = malloc(2u * keys * sizeof(*choices));
    AtomId *data = malloc((size_t)rows * sizeof(*data));
    assert(choices && data);
    for (size_t key = 0u; key < keys; key++) {
        for (size_t variant = 0u; variant < 2u; variant++) {
            Atom *items[] = {atom_symbol_id(&scratch, head),
                atom_int(&scratch, (int64_t)key),
                atom_symbol_id(&scratch, variant ? rejected : selected)};
            choices[2u * key + variant] = term_universe_store_atom_id(
                &universe, NULL, atom_expr(&scratch, items, 3u));
            assert(choices[2u * key + variant] != CETTA_ATOM_ID_NONE);
        }
    }
    for (CettaIndex row = 0u; row < rows; row++)
        data[row] = choices[2u * (row % keys) + (row % 2u)];
    assert(space_add_atom_ids_batch(&space, data, rows));
    free(data);
    free(choices);
    uint64_t load_ns = now_ns() - load_start;
    uint64_t index_start = now_ns();
    space_match_native_ensure_trie(&space);
    uint64_t index_ns = now_ns() - index_start;
    Atom *query_items[] = {atom_symbol_id(&scratch, head),
        atom_var(&scratch, "cursor-key"), atom_symbol_id(&scratch, selected)};
    Atom *query = atom_expr(&scratch, query_items, 3u);
    CettaCount expected = (rows + 1u) / 2u;
    if (prefix > expected)
        prefix = expected;

    for (size_t attempt = 0u; attempt < repetitions; attempt++) {
        SpaceOccurrenceCursor cursor;
        size_t before = heap_bytes();
        uint64_t start = now_ns();
        assert(space_occurrence_cursor_init(&space, query, &cursor));
        uint64_t prepared = now_ns();
        size_t retained = heap_bytes();
        uint64_t first = prepared;
        for (CettaIndex item = 0u; item < prefix; item++) {
            CettaIndex index = UINT64_MAX;
            assert(space_occurrence_cursor_next(&cursor, &index) ==
                   SPACE_OCCURRENCE_CURSOR_ITEM);
            assert(index == 2u * item);
            assert(space_occurrence_cursor_atom(&cursor, index));
            if (item == 0u)
                first = now_ns();
        }
        if (prefix == expected) {
            CettaIndex extra;
            assert(space_occurrence_cursor_next(&cursor, &extra) ==
                   SPACE_OCCURRENCE_CURSOR_END);
        }
        uint64_t pulled = now_ns();
        uint32_t branches = cursor.node_len;
        space_occurrence_cursor_release(&cursor);
        struct rusage usage;
        assert(getrusage(RUSAGE_SELF, &usage) == 0);
        printf("{\"rows\":%" PRIu64 ",\"keys\":%zu,\"prefix\":%" PRIu64
               ",\"attempt\":%zu,\"branches\":%" PRIu32
               ",\"load_ns\":%" PRIu64 ",\"index_ns\":%" PRIu64
               ",\"prepare_ns\":%" PRIu64 ",\"first_ns\":%" PRIu64
               ",\"prefix_ns\":%" PRIu64 ",\"cursor_heap_bytes\":%zu"
               ",\"peak_rss_kib\":%ld}\n",
               rows, keys, prefix, attempt, branches, load_ns, index_ns,
               prepared - start, first - start, pulled - start,
               retained > before ? retained - before : 0u, usage.ru_maxrss);
    }

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}
