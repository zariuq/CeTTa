#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mork_space_bridge_runtime.h"
#include "space_match_backend.h"
#include "symbol.h"

static bool force_constructor_failure;
static bool simulate_missing_constructor;
static unsigned constructor_calls;
static unsigned packet_calls;

bool __real_cetta_mork_bridge_supports_query_only_v2_cursor(void);
bool __real_cetta_mork_bridge_query_cursor_new_query_only_v2(
    CettaMorkSpaceHandle *, const uint8_t *, size_t,
    CettaMorkQueryCursorHandle **);
bool __real_cetta_mork_bridge_space_query_bindings_query_only_v2(
    CettaMorkSpaceHandle *, const uint8_t *, size_t,
    uint8_t **, size_t *, uint64_t *);

bool __wrap_cetta_mork_bridge_supports_query_only_v2_cursor(void) {
    return !simulate_missing_constructor &&
           __real_cetta_mork_bridge_supports_query_only_v2_cursor();
}

bool __wrap_cetta_mork_bridge_query_cursor_new_query_only_v2(
    CettaMorkSpaceHandle *space, const uint8_t *pattern, size_t len,
    CettaMorkQueryCursorHandle **out_cursor) {
    constructor_calls++;
    if (force_constructor_failure || simulate_missing_constructor) {
        *out_cursor = NULL;
        return false;
    }
    return __real_cetta_mork_bridge_query_cursor_new_query_only_v2(
        space, pattern, len, out_cursor);
}

bool __wrap_cetta_mork_bridge_space_query_bindings_query_only_v2(
    CettaMorkSpaceHandle *space, const uint8_t *pattern, size_t len,
    uint8_t **out_packet, size_t *out_len, uint64_t *out_rows) {
    packet_calls++;
    return __real_cetta_mork_bridge_space_query_bindings_query_only_v2(
        space, pattern, len, out_packet, out_len, out_rows);
}

typedef struct {
    unsigned visits;
    unsigned b_values;
    unsigned c_values;
    bool ground;
    bool keep_going;
} Probe;

static bool visit(const Bindings *bindings, void *context) {
    Probe *probe = context;
    assert(bindings != NULL);
    if (probe->ground) {
        Atom *value = bindings_lookup_value_id((Bindings *)bindings, 42u).skeleton;
        assert(value != NULL);
        if (atom_is_symbol(value, "b"))
            probe->b_values++;
        else {
            assert(atom_is_symbol(value, "c"));
            probe->c_values++;
        }
    }
    probe->visits++;
    return probe->keep_going;
}

static void check_query(CettaMorkSpaceHandle *space, Arena *arena, Atom *query,
                        unsigned expected_packets, bool ground, bool keep_going) {
    Probe probe = {.ground = ground, .keep_going = keep_going};
    constructor_calls = packet_calls = 0;
    space_match_backend_clear_error();
    bool ok = space_match_backend_mork_visit_bindings_direct(
        space, arena, query, visit, &probe);
    assert(ok == keep_going);
    assert(probe.visits == (keep_going ? 2u : 1u));
    if (ground && keep_going) {
        assert(probe.b_values == 1u);
        assert(probe.c_values == 1u);
    }
    assert(constructor_calls == 1u);
    assert(packet_calls == expected_packets);
}

static CettaMorkSpaceHandle *make_space(const char *facts) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
    uint64_t added = 0;
    assert(space != NULL);
    assert(cetta_mork_bridge_space_add_text(space, facts, &added));
    assert(added == 2u);
    return space;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    Atom *items[] = {
        atom_symbol(&arena, "edge"),
        atom_symbol(&arena, "a"),
        atom_var_with_spelling(&arena, symbol_intern_cstr(&symbols, "query"), 42u),
    };
    Atom *query = atom_expr(&arena, items, 3);
    assert(__real_cetta_mork_bridge_supports_query_only_v2_cursor());

    CettaMorkSpaceHandle *ground = make_space("(edge a b)\n(edge a c)");
    check_query(ground, &arena, query, 0u, true, true);
    check_query(ground, &arena, query, 0u, true, false);

    force_constructor_failure = true;
    check_query(ground, &arena, query, 0u, true, true);
    check_query(ground, &arena, query, 0u, true, false);
    force_constructor_failure = false;

    simulate_missing_constructor = true;
    check_query(ground, &arena, query, 1u, true, true);
    check_query(ground, &arena, query, 1u, true, false);
    simulate_missing_constructor = false;
    cetta_mork_bridge_space_free(ground);

    CettaMorkSpaceHandle *open = make_space("(edge a $stored)\n(edge a c)");
    check_query(open, &arena, query, 0u, false, true);
    check_query(open, &arena, query, 0u, false, false);
    cetta_mork_bridge_space_free(open);

    arena_free(&arena);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: cursor admission falls back once; absent constructor retains packet compatibility");
    return 0;
}
