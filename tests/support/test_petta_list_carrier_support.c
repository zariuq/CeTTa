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
    check(&machine, binding_value_from_atom(single), false);
    assert(bindings_builder_add_var_fresh(builder, y, cell));
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
