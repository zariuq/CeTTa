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
