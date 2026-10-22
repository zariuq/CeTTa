/* Test the actual private guard and existing resolver, not a second copy.
 * Link-time section collection removes unused runtime entry points. */
#include "../../src/open_equation_machine.c"
#include "../test_runtime_stats_stubs.h"
#include <assert.h>

static uint64_t checks;
static void check(CettaOpenEquationCursor *cursor, Atom *value, bool expected) {
    bool certified = oem_callable_argument_guard(cursor, value);
    assert(certified == expected);
    ArenaMark mark = arena_mark(&cursor->region);
    Atom *resolved = oem_resolve_operand(cursor, value);
    assert(resolved);
    if (certified) {
        assert(!atom_is_error(resolved));
        assert(!atom_has_thread_local_resource(resolved));
    }
    arena_reset(&cursor->region, mark);
    checks++;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena source;
    arena_init_detached(&source);
    CettaOpenEquationCursor cursor = {0};
    cursor.epoch = (CettaFrameIdentity){17u};
    Atom *cells[3] = {NULL, NULL, NULL};
    cursor.cells = cells;
    cursor.cell_len = 3u;
    arena_init_detached(&cursor.region);
    Atom *x = atom_var_with_id(&source, "x", var_epoch_id(var_id_from_base(1u), cursor.epoch));
    Atom *y = atom_var_with_id(&source, "y", var_epoch_id(var_id_from_base(2u), cursor.epoch));
    Atom *z = atom_var_with_id(&source, "z", var_epoch_id(var_id_from_base(3u), cursor.epoch));
    Atom *tag = atom_symbol(&source, "Node");
    Atom *done = atom_symbol(&source, "done");
    Atom *error = atom_symbol(&source, "Error");
    StateCell state = {.value = done};
    Atom *resource = atom_state(&source, &state);
    assert(atom_has_thread_local_resource(resource));
    Atom *one = atom_expr2(&source, tag, x);
    Atom *two = atom_expr3(&source, tag, x, y);
    Atom *head = atom_expr2(&source, x, done);
    check(&cursor, one, true);
    check(&cursor, two, false); /* Conservative, not an error. */
    check(&cursor, resource, false);
    cells[0] = y;
    check(&cursor, one, true); /* Aliased, still open. */
    cells[1] = resource;
    check(&cursor, one, false); /* Actual GV_STATE introduced by binding. */
    cells[1] = NULL; /* Backtrack: the same immutable header becomes safe. */
    check(&cursor, one, true);
    cells[0] = error;
    check(&cursor, head, false); /* A bound head makes the root an Error. */
    cells[0] = done;
    check(&cursor, head, true);
    cells[0] = NULL;
    Atom *deep = x;
    for (unsigned depth = 0; depth < 32768u; depth++)
        deep = atom_expr2(&source, tag, deep);
    check(&cursor, deep, true);
    cells[0] = y;
    cells[1] = z;
    cells[2] = resource;
    check(&cursor, deep, false);
    cells[2] = done;
    check(&cursor, deep, true);
    cells[0] = cells[1] = cells[2] = NULL;
    /* A large shared DAG. Repeated occurrences are one support. */
    Atom *dag = x;
    for (unsigned depth = 0; depth < 22u; depth++)
        dag = atom_expr3(&source, tag, dag, dag);
    check(&cursor, dag, true);
    cells[0] = resource;
    check(&cursor, dag, false);
    cells[0] = NULL;
    /* Repeated live mutation, aliasing and rollback against the real resolver. */
    Atom *variants[] = {NULL, done, resource, y, one};
    for (unsigned i = 0; i < 2000u; i++) {
        cells[0] = variants[i % 4u]; /* Exclude self occurrence. */
        cells[1] = (i % 7u == 0u) ? resource : NULL;
        check(&cursor, one, cells[0] != resource &&
              !(cells[0] == y && cells[1] == resource));
        cells[0] = NULL;
        cells[1] = NULL;
        check(&cursor, one, true);
    }
    /* Root dereference, foreign scope and list-rest normalization. */
    Atom *err_value = atom_expr2(&source, error, done);
    cells[0] = err_value;
    check(&cursor, x, false);
    cells[0] = done;
    check(&cursor, x, true);
    cells[0] = NULL;
    check(&cursor, atom_expr2(&source, tag, err_value), true);
    Atom *foreign = atom_var_with_id(&source, "foreign",
        var_epoch_id(var_id_from_base(1u), (CettaFrameIdentity){18u}));
    check(&cursor, foreign, true);
    check(&cursor, atom_expr2(&source, tag, foreign), true);
    Atom *prefix[] = {done};
    Atom *rest = atom_list_with_rest(&source, prefix, 1u, x);
    check(&cursor, rest, true);
    Atom *tail_values[] = {done, done};
    cells[0] = atom_list(&source, tail_values, 2u);
    check(&cursor, rest, true);
    Atom *normalized_rest = oem_resolve_operand(&cursor, rest);
    assert(normalized_rest && normalized_rest->expr.len != rest->expr.len);
    cells[0] = resource;
    check(&cursor, rest, false);
    cells[0] = NULL;
    check(&cursor, rest, true);
    printf("PASS: %llu guard/resolver checks; resource binding, Error head, aliases, rollback, deep graph and shared DAG\n", (unsigned long long)checks);
    /* Count term bytes independently of preparation. The graph is built
     * once per size. Each resolver allocation is observed before reset;
     * guard calls run on the same immutable syntax and live store. */
    printf("depth,calls,construction_term_bytes,guard_term_bytes,resolver_term_bytes\n");
    const unsigned sizes[] = {16u, 256u, 4096u, 32768u};
    for (unsigned sample = 0; sample < 4u; sample++) {
        unsigned depth = sizes[sample];
        unsigned calls = sample == 3u ? 10u : 100u;
        size_t before_build = source.live_bytes;
        Atom *graph = x;
        for (unsigned n = 0; n < depth; n++)
            graph = atom_expr2(&source, tag, graph);
        size_t construction = source.live_bytes - before_build;
        size_t guard_bytes = 0u, resolver_bytes = 0u;
        for (unsigned call = 0; call < calls; call++) {
            ArenaMark mark = arena_mark(&cursor.region);
            size_t before = cursor.region.live_bytes;
            assert(oem_callable_argument_guard(&cursor, graph));
            guard_bytes += cursor.region.live_bytes - before;
            Atom *resolved = oem_resolve_operand(&cursor, graph);
            assert(resolved && !atom_is_error(resolved) &&
                   !atom_has_thread_local_resource(resolved));
            resolver_bytes += cursor.region.live_bytes - before;
            arena_reset(&cursor.region, mark);
        }
        printf("%u,%u,%zu,%zu,%zu\n", depth, calls, construction,
               guard_bytes, resolver_bytes);
    }
    free(cursor.resolve_stack);
    free(cursor.resolve_results);
    arena_free(&cursor.region);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
