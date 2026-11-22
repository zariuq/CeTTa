#include "match.h"
static uint64_t resolve_reads;
static bool counted_resolve(Bindings *bindings, BindingValue value, BindingValue *out) {
    resolve_reads++;
    return bindings_resolve_value_preview(bindings, value, out);
}
#define bindings_resolve_value_preview counted_resolve
#include "../../src/petta_search_machine.c"
#undef bindings_resolve_value_preview

/* Independent constructor traversal; no singleton-support observation. */
static bool reference(Bindings *bindings, BindingValue value) {
    BindingValue *pending = NULL;
    size_t length = 0, capacity = 0, expanded = 0;
    bool answer = false;
    for (;;) {
        BindingValue root;
        assert(++expanded < 1000000);
        assert(bindings_resolve_value_preview(bindings, value, &root));
        Atom *atom = root.skeleton;
        if (atom && atom->kind == ATOM_EXPR) {
            if (petta_semantics_is_open_cons_value(atom) ||
                atom_structural_may_have_list_carrier(atom)) {
                answer = true;
                break;
            }
            if (atom_has_vars(atom)) {
                if (length + atom->expr.len > capacity) {
                    capacity = (length + atom->expr.len) * 2 + 8;
                    pending = realloc(pending, capacity * sizeof(*pending));
                    assert(pending);
                }
                for (CettaExprIndex i=0; i<atom->expr.len; i++)
                    pending[length++] = binding_value_with_skeleton(root, atom->expr.elems[i]);
            }
        }
        if (!length) break;
        value = pending[--length];
    }
    free(pending);
    return answer;
}

static unsigned checks;
static Atom *ground_control;
static void check(PettaMachineImpl *machine, BindingValue value, bool expected) {
    Bindings *bindings = (Bindings *)search_context_bindings(&machine->search);
    BindingValue empty = binding_value_from_atom(ground_control);
    assert(reference(bindings, value) == expected);
    assert(petta_machine_values_reach_cell(machine, value, empty) == expected);
    checks++;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena source;
    arena_init_detached(&source);
    PettaMachineImpl machine = {0};
    assert(search_context_init_bindings_only(&machine.search, NULL));
    BindingsBuilder *builder = search_context_builder(&machine.search);
    Atom *tag = atom_symbol(&source, "support-node");
    Atom *leaf = atom_symbol(&source, "support-leaf");
    ground_control = leaf;
    Atom *x = atom_var_with_id(&source, "support-x", fresh_var_id());
    Atom *y = atom_var_with_id(&source, "support-y", fresh_var_id());
    Atom *single = atom_expr3(&source, tag, x, x);
    Atom *multiple = atom_expr3(&source, tag, x, y);
    Atom *cell = petta_semantics_open_cons_value(&source, leaf, atom_unit(&source));
    assert(!var_id_is_none(atom_single_variable_id(single)));
    assert(var_id_is_none(atom_single_variable_id(multiple)));
    check(&machine, binding_value_from_atom(single), false);
    check(&machine, binding_value_from_atom(multiple), false);
    check(&machine, binding_value_from_atom(cell), true);
    uint32_t clear = bindings_builder_save(builder);
    assert(bindings_builder_add_var_fresh(builder, x, y));
    uint32_t alias = bindings_builder_save(builder);
    Bindings *current = (Bindings *)bindings_builder_bindings(builder);
    BindingValue reached;
    assert(petta_value_single_variable(current, binding_value_from_atom(single), &reached) ==
           PETTA_SINGLE_VARIABLE_UNBOUND);
    check(&machine, binding_value_from_atom(single), false);
    assert(bindings_builder_add_var_fresh(builder, y, cell));
    assert(petta_value_single_variable(current, binding_value_from_atom(single), &reached) ==
           PETTA_SINGLE_VARIABLE_BOUND && reached.skeleton == cell);
    check(&machine, binding_value_from_atom(single), true);
    check(&machine, binding_value_from_atom(multiple), true);
    bindings_builder_rollback(builder, alias);
    check(&machine, binding_value_from_atom(single), false);
    bindings_builder_rollback(builder, clear);

    CettaFrameIdentity first, second;
    assert(cetta_frame_identity_acquire(&first));
    assert(cetta_frame_identity_acquire(&second));
    VarId ids[] = {x->var_id, y->var_id};
    assert(bindings_builder_register_contextual_frame(builder, ids, 2u, first));
    assert(bindings_builder_register_contextual_frame(builder, ids, 2u, second));
    Atom *framed = atom_var_with_id(&source, "first-x", var_epoch_id(x->var_id, first));
    assert(bindings_builder_add_var_fresh(builder, framed, cell));
    check(&machine, binding_value_from_context(single, first), true);
    check(&machine, binding_value_from_context(single, second), false);
    check(&machine, binding_value_from_persistent_context(single, first), true);
    assert(petta_value_single_variable(current, binding_value_from_context(single, first), &reached) ==
           PETTA_SINGLE_VARIABLE_BOUND && reached.skeleton == cell);
    assert(petta_value_single_variable(current, binding_value_from_context(single, second), &reached) ==
           PETTA_SINGLE_VARIABLE_UNBOUND);

    /* Sharing the cell skeleton doesn't identify the two lexical slots.
     * Match a bound head with an independent open head, then contradict it. */
    Atom *scoped_cell = petta_semantics_open_cons_value(&source, x, atom_unit(&source));
    BindingValue from_first = binding_value_from_context(scoped_cell, first);
    BindingValue from_second = binding_value_from_context(scoped_cell, second);
    uint32_t scoped_mark = bindings_builder_save(builder);
    assert(petta_semantics_match_list_values(
        &source, from_first, from_second, builder));
    Bindings *bound = (Bindings *)bindings_builder_bindings(builder);
    BindingValue second_head = bindings_lookup_context_id(bound, from_second, x->var_id);
    assert(second_head.skeleton == cell);
    bindings_builder_rollback(builder, scoped_mark);
    Atom *other = atom_var_with_id(&source, "second-x", var_epoch_id(x->var_id, second));
    assert(bindings_builder_add_var_fresh(builder, other, leaf));
    assert(!petta_semantics_match_list_values(
        &source, from_first, from_second, builder));
    bindings_builder_rollback(builder, scoped_mark);

    /* The same written `cons` is a constructor in an authored pattern
     * and merely data in a value-to-value comparison. */
    Atom *written = atom_expr3(&source, atom_symbol(&source, "cons"), leaf, atom_unit(&source));
    Atom *one = atom_expr(&source, &leaf, 1u);
    assert(petta_semantics_match_cons_constraint(&source, written, one, builder));
    assert(!petta_semantics_match_list_values(
        &source, binding_value_from_atom(written), binding_value_from_atom(one), builder));

    for (unsigned trial=0; trial<2000; trial++) {
        uint32_t mark = bindings_builder_save(builder);
        assert(bindings_builder_add_var_fresh(builder, x, (trial&1) ? cell : leaf));
        check(&machine, binding_value_from_atom(single), (trial&1) != 0);
        bindings_builder_rollback(builder, mark);
        check(&machine, binding_value_from_atom(single), false);
    }
    Atom *deep = x;
    for (unsigned depth=0; depth<32768; depth++) deep=atom_expr2(&source, tag, deep);
    resolve_reads = 0;
    check(&machine, binding_value_from_atom(deep), false);
    uint64_t unbound_reads = resolve_reads;
    uint32_t mark = bindings_builder_save(builder);
    assert(bindings_builder_add_var_fresh(builder, x, cell));
    resolve_reads = 0;
    check(&machine, binding_value_from_atom(deep), true);
    uint64_t bound_reads = resolve_reads;
    bindings_builder_rollback(builder, mark);
    assert(unbound_reads == 2 && bound_reads == 3);
    printf("PASS: %u carrier-support controls; live aliases, rollback, frame separation, persistent frames and depth 32768\n", checks);
    printf("deep_unbound_resolve_reads=%" PRIu64 " deep_bound_resolve_reads=%" PRIu64 "\n",unbound_reads,bound_reads);
    search_context_free(&machine.search);
    arena_free(&source);
    cetta_frame_identity_release(first);
    cetta_frame_identity_release(second);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
