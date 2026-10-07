/* Exercise the private traversal and its active Need snapshot directly,
 * including sharing and invalid cycles that parsed programs cannot construct.
 * This translation unit replaces eval.o in the test executable. */
#include "../src/eval.c"
#include <assert.h>

static Atom *suspend(Arena *arena, Atom *origin) {
    PrimeNeedSnapshot next;
    uint64_t id;
    assert(prime_need_snapshot_allocate(
        arena, &g_prime_need_active, origin, &next, &id));
    g_prime_need_active = next;
    Atom *ref = prime_need_ref(arena, &g_prime_need_active, id);
    assert(ref);
    return ref;
}

static Atom *nest(Arena *arena, Atom *head, Atom *leaf, size_t depth) {
    for (size_t i = 0u; i < depth; i++) leaf = atom_expr2(arena, head, leaf);
    return leaf;
}

/* Rendered answers cannot expose an otherwise unreachable binding retained
 * in a function's continuation. Check the actual environment boundary as well
 * as the language-level closure examples in need_application.metta. */
static bool function_projection(Arena *arena) {
    Atom *caller = atom_var(arena, "caller");
    Atom *capture = atom_var(arena, "capture");
    Atom *private = atom_var(arena, "private");
    Atom *seven = atom_int(arena, 7);
    Atom *eleven = atom_int(arena, 11);
    Atom *body = atom_expr2(arena, atom_symbol(arena, "input"), caller);
    Atom *result = atom_expr2(arena, atom_symbol(arena, "closure"), capture);
    Bindings full, projected;
    bindings_init(&full);
    assert(bindings_add_var(&full, caller, seven));
    assert(bindings_add_var(&full, capture, eleven));
    assert(bindings_add_var(&full, private, atom_int(arena, 13)));
    assert(bindings_project_function_result(arena, body, result,
                                           &full, &projected));
    bool ok = atom_eq(bindings_apply(&projected, arena, caller), seven) &&
        atom_eq(bindings_apply(&projected, arena, capture), eleven) &&
        !bindings_lookup_value_id(&projected, private->var_id).skeleton;
    bindings_free(&projected);
    bindings_free(&full);
    if (!ok)
        fputs("FAIL: function projection retains caller and closure bindings only\n",
              stderr);
    return ok;
}

static bool cell_projection(Arena *arena) {
    Atom *capture = atom_var(arena, "cell-capture");
    Atom *private = atom_var(arena, "continuation-private");
    Atom *seven = atom_int(arena, 7);
    PrimeNeedCellView cell = {.origin = capture};
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId captured_id = capture->var_id;
    cell.capture_known = true;
    cell.capture_var_ids = &captured_id;
    cell.capture_var_count = 1u;
#endif
    Bindings caller, projected;
    bindings_init(&caller);
    assert(bindings_add_var(&caller, capture, seven));
    assert(bindings_add_var(&caller, private, atom_int(arena, 13)));
    assert(prime_need_project_cell_logical_env(arena, &cell, &caller, &projected));
    bool ok = atom_eq(bindings_apply(&projected, arena, capture), seven) &&
        !bindings_lookup_value_id(&projected, private->var_id).skeleton;
    bindings_free(&projected);
    bindings_free(&caller);
    if (!ok)
        fputs("FAIL: cell projection excludes unrelated continuation bindings\n",
              stderr);
    return ok;
}

static void full_demand_context(Arena *arena) {
    Space local = {0}, foreign = {0};
    Atom *body = atom_expr2(arena, atom_symbol(arena, "key"),
                            atom_int(arena, 7));
    Atom *parts[] = {atom_symbol(arena, "metta"), body,
        atom_symbol(arena, "%Undefined%"), atom_space(arena, &local)};
    Atom *request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == body);

    parts[3] = atom_space(arena, &foreign);
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == request);
    Atom *current_context = atom_symbol(arena, "context-space");
    parts[3] = atom_expr(arena, &current_context, 1u);
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == body);
    parts[2] = atom_symbol(arena, "Number");
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == request);
    parts[2] = atom_symbol(arena, "%Undefined%");
    parts[3] = atom_expr2(arena, atom_symbol(arena, "context-space"), body);
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == request);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    Arena arena;
    CettaLibraryContext context = {0};
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    cetta_eval_session_init(&context.session, CETTA_LANGUAGE_PRIME,
                           cetta_profile_prime_default());
    eval_set_library_context(&context);
    prime_need_snapshot_init(&g_prime_need_active);
    assert(prime_need_snapshot_begin(&g_prime_need_active));

    if (!function_projection(&arena) || !cell_projection(&arena))
        return 1;
    full_demand_context(&arena);

    Atom *x = atom_var(&arena, "x");
    Atom *y = atom_var(&arena, "y");
    Atom *box = atom_symbol(&arena, "Box");
    Atom *pair = atom_symbol(&arena, "Pair");
    Atom *deep = nest(&arena, box, x, 50000u);
    Atom *ref = suspend(&arena, deep);

    /* Occurrence depth and opening depth are independently unbounded.
     * Repeated references remain shared, and unrelated cells stay opaque. */
    assert(prime_open_capabilities_for_var(&arena, ref, x->var_id) == deep);
    assert(prime_open_capabilities_for_var(&arena, ref, y->var_id) == ref);
    Atom *outer = nest(&arena, box, ref, 50000u);
    Atom *opened = prime_open_capabilities_for_var(&arena, outer, x->var_id);
    assert(opened && opened != outer);
    for (size_t i = 0u; i < 50000u; i++) opened = opened->expr.elems[1];
    assert(opened == deep);
    assert(prime_open_capabilities_for_var(&arena, outer, y->var_id) == outer);

    /* Exponentially many unfolded paths, only a linear number of cells.
     * Rebuilding preserves the original aliasing of every changed node. */
    Atom *diamond = ref;
    for (size_t i = 0u; i < 80u; i++)
        diamond = atom_expr3(&arena, pair, diamond, diamond);
    opened = prime_open_capabilities_for_var(&arena, diamond, x->var_id);
    for (size_t i = 0u; i < 80u; i++) {
        assert(opened->expr.elems[1] == opened->expr.elems[2]);
        opened = opened->expr.elems[1];
    }
    assert(opened == deep);
    Atom *syntax_diamond = x;
    for (size_t i = 0u; i < 80u; i++)
        syntax_diamond = atom_expr3(&arena, pair, syntax_diamond, syntax_diamond);
    PrimeBinderElabMemo mentions = {0};
    bool found = true;
    assert(prime_binder_elab_memo_init(&mentions));
    assert(prime_atom_mentions_var(syntax_diamond, y->var_id, &mentions, &found));
    assert(!found && mentions.used == 82u);
    prime_binder_elab_memo_free(&mentions);

    Atom *pattern = nest(&arena, box, x, 50000u);
    assert(prime_open_capabilities_for_pattern(&arena, pattern, ref) == deep);

    /* An origin can refer to another capability that must also be opened. */
    Atom *chain = x;
    for (size_t i = 0u; i < 200u; i++)
        chain = suspend(&arena, atom_expr3(&arena, pair, x, chain));
    opened = prime_open_capabilities_for_var(&arena, chain, x->var_id);
    assert(opened);
    for (size_t i = 0u; i < 200u; i++) opened = opened->expr.elems[2];
    assert(opened == x);

    /* Manufacture invalid cyclic syntax only in this negative control. */
    Atom *cycle = atom_expr3(&arena, pair, x, x);
    Atom *cyclic_ref = suspend(&arena, cycle);
    cycle->expr.elems[2] = cyclic_ref;
    cycle->flags |= ATOM_FLAG_HAS_REGISTRY_REFS;
    assert(prime_open_capabilities_for_var(&arena, cyclic_ref, x->var_id) == NULL);
    /* An irrelevant cyclic capability is not expanded or forced. */
    assert(prime_open_capabilities_for_var(&arena, cyclic_ref, y->var_id) == cyclic_ref);
    cycle->expr.elems[2] = x;

    _Atomic bool cancelled = true;
    _Atomic bool *previous = eval_set_cancel_token(&cancelled);
    assert(prime_open_capabilities_for_var(&arena, ref, x->var_id) == NULL);
    assert(g_eval_cancel_observed);
    eval_set_cancel_token(previous);

    prime_need_snapshot_init(&g_prime_need_active);
    eval_set_library_context(NULL);
    arena_free(&arena);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: Prime capability opening preserves depth, sharing, opacity and cancellation");
    return 0;
}
