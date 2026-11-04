#include "eval.h"
#include "symbol.h"
#include "state_cell.h"

#include <assert.h>
#include <stdio.h>

/* Private adapter, available only in the evaluator's test-hook build. */
extern bool cetta_test_transaction_state_views(Space *, Arena *, int64_t[8]);
extern bool cetta_test_transaction_capture_context(Space *, Arena *);
extern bool cetta_test_worker_resource_leases(Space *, Arena *);
extern bool cetta_test_worker_storage_owner_lifetime(Space *, Arena *);
extern bool cetta_test_foreign_state_update_lifetime(Space *, Arena *);
extern bool cetta_test_worker_private_collection(Space *);
extern bool cetta_test_worker_machine_scope_lifetime(Space *);
extern bool cetta_test_foreign_snapshot_universe(Space *, bool, unsigned);
extern bool cetta_test_worker_state_receipt_lifetime(Space *, unsigned, bool);

static size_t context_provider_count(const TermUniverse *universe,
                                     const Space *context) {
    size_t count = 0u;
    for (size_t i = 0u; i < universe->root_provider_len; i++)
        count += universe->root_providers[i].resource_owner == context;
    return count;
}

static void capture_context_lifetime(void) {
    Arena storage, explicit_space, captured, copied;
    arena_init_detached(&storage);
    arena_init_detached(&explicit_space);
    arena_init_detached(&captured);
    arena_init_detached(&copied);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &storage);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    Space root;
    space_init_with_universe(&root, &universe);
    size_t baseline = universe.root_provider_len;
    Atom *request[] = {atom_symbol_id(&explicit_space,
                                      g_builtin_syms.new_space)};
    ResultSet created;
    result_set_init(&created);
    eval_top(&root, &explicit_space,
             atom_expr(&explicit_space, request, 1u), &created);
    assert(created.len == 1u && created.items[0]->kind == ATOM_GROUNDED &&
           created.items[0]->ground.gkind == GV_SPACE);
    Space *context = created.items[0]->ground.ptr;
    assert(context && context->arena_hold &&
           context->native.universe == &universe);
    assert(context_provider_count(&universe, context) == 2u);
    Atom *fact = atom_expr2(&storage,
        atom_symbol(&storage, "capture-context-value"), atom_int(&storage, 23));
    assert(space_admit_atom(context, &storage, fact));

    Atom *empty = atom_capture(&captured, NULL);
    assert(empty && empty->ground.gkind == GV_CAPTURE && !empty->ground.ptr);
    assert(captured.retained_resource_bytes == 0u);
    /* The wrapper owns the context lease. The closure descriptor itself
     * stays live here, independently of both wrapper arenas. */
    CaptureClosure closure = {.space_ptr = context};
    Atom *capture = atom_capture(&captured, &closure);
    Atom *copy = atom_deep_copy(&copied, capture);
    assert(copy && copy->kind == ATOM_GROUNDED &&
           copy->ground.gkind == GV_CAPTURE && copy->ground.ptr == &closure);
    result_set_free(&created);
    arena_free(&explicit_space);

    assert(space_reclaim_unheld());
    /* Check registration before dereferencing the context: the old
     * constructor retires it at this exact boundary. */
    assert(context_provider_count(&universe, context) == 2u);
    assert(captured.retained_resource_bytes == sizeof(Space));
    assert(copied.retained_resource_bytes == sizeof(Space));
    assert(term_universe_collect(&universe, NULL));
    assert(space_length64(context) == 1u &&
           atom_eq(space_get_at64(context, 0u), fact));

    arena_free(&captured);
    assert(space_reclaim_unheld());
    assert(context_provider_count(&universe, context) == 2u);
    assert(term_universe_collect(&universe, NULL));
    assert(space_length64(context) == 1u &&
           atom_eq(space_get_at64(context, 0u), fact));

    arena_free(&copied);
    assert(space_reclaim_unheld());
    assert(context_provider_count(&universe, context) == 0u);
    assert(universe.root_provider_len == baseline);
    space_free(&root);
    term_universe_free(&universe);
    arena_free(&storage);
    puts("PASS: capture and generic copies lease managed contexts through source release and collection, then release the final context");
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;
    eval_set_library_context(NULL);
    for (unsigned pass = 0u; pass < 64u && state_cell_reclamation_due(); pass++)
        (void)state_cell_reclaim_bounded(64u);
    assert(!state_cell_reclamation_due());
    StateCellDiagnostics states_before = state_cell_diagnostics();
    capture_context_lifetime();
    Arena arena;
    arena_init_detached(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space root;
    space_init_with_universe(&root, &universe);
    int64_t observed[8] = {0};
    const int64_t expected[8] = {18, 17, 19, 17, 23, 32, 1, 31};
    assert(cetta_test_transaction_state_views(&root, &arena, observed));
    for (size_t i = 0u; i < 8u; i++) {
        if (observed[i] != expected[i])
            fprintf(stderr, "native StateCell observation %zu: got %lld, expected %lld\n",
                i, (long long)observed[i], (long long)expected[i]);
        assert(observed[i] == expected[i]);
    }
    assert(cetta_test_transaction_capture_context(&root, &arena));
    assert(cetta_test_worker_resource_leases(&root, &arena));
    assert(cetta_test_worker_storage_owner_lifetime(&root, &arena));
    assert(cetta_test_foreign_state_update_lifetime(&root, &arena));
    for (unsigned route = 0u; route < 3u; route++) {
        assert(cetta_test_worker_state_receipt_lifetime(&root, route, false));
        assert(cetta_test_worker_state_receipt_lifetime(&root, route, true));
    }
    puts("PASS: state receipt append and both promotion routes retain cell identity until the final receipt arena retires");
    assert(cetta_test_worker_private_collection(&root));
    assert(cetta_test_worker_machine_scope_lifetime(&root));
    puts("PASS: worker scheduler collects its private records and unobserved cycles while retaining scoped readers and leaving the shared universe untouched");
    for (unsigned persistent = 0u; persistent < 2u; persistent++) {
        assert(cetta_test_foreign_snapshot_universe(&root, false, persistent));
        assert(cetta_test_foreign_snapshot_universe(&root, true, persistent));
    }
    assert(cetta_test_foreign_snapshot_universe(&root, true, 2u));
    puts("PASS: snapshots, persistent copies and real bind publication retain foreign term storage through copied wrappers and final row teardown");
    puts("PASS: publication preserves Space/capture owners and independent State identities through the final reader release");
    space_free(&root);
    term_universe_free(&universe);
    arena_free(&arena);
    for (unsigned pass = 0u; pass < 64u && state_cell_reclamation_due(); pass++)
        (void)state_cell_reclaim_bounded(64u);
    StateCellDiagnostics states_after = state_cell_diagnostics();
    assert(!state_cell_reclamation_due() &&
           states_after.nodes == states_before.nodes &&
           states_after.images == states_before.images &&
           states_after.image_bytes == states_before.image_bytes);
    symbol_table_free(&symbols);
    var_intern_free(&variables);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: native StateCell views inherit parent writes, preserve outer rollback, and export live canonical owners");
    return 0;
}
