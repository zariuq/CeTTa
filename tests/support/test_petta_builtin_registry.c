#include "petta_builtin_registry.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena arena;
    arena_init_detached(&arena);
    size_t signatures = 0u;
    for (size_t index = 0u; index < petta_builtin_spec_count(); index++) {
        const PettaBuiltinSpec *spec = petta_builtin_spec_at(index);
        assert(spec == petta_builtin_spec_find(spec->name, spec->arity));
        for (size_t prior = 0u; prior < index; prior++) {
            const PettaBuiltinSpec *previous = petta_builtin_spec_at(prior);
            assert(strcmp(previous->name, spec->name) != 0 || previous->arity != spec->arity);
        }
        Atom *signature = petta_builtin_signature(&arena, spec);
        assert((signature != NULL) == spec->has_signature);
        if (signature) {
            signatures++;
            assert(signature->kind == ATOM_EXPR);
            assert(signature->expr.len == spec->arity + 2u);
        }
    }
    assert(petta_builtin_spec_count() == 183u && signatures == 67u);
    assert(!petta_builtin_spec_at(petta_builtin_spec_count()));
    assert(!petta_builtin_spec_find("missing-builtin", 2u));
    assert(!petta_builtin_spec_find("+", 1u));
    assert(!petta_builtin_spec_find(NULL, 0u));

    const PettaBuiltinSpec *superpose = petta_builtin_spec_find("superpose", 1u);
    Atom *first = petta_builtin_signature(&arena, superpose);
    Atom *second = petta_builtin_signature(&arena, superpose);
    Atom *input_variable = first->expr.elems[1]->expr.elems[1];
    assert(input_variable->kind == ATOM_VAR);
    assert(input_variable == first->expr.elems[2]);
    assert(!atom_eq(input_variable, second->expr.elems[2]));
    const PettaBuiltinSpec *equal = petta_builtin_spec_find("==", 2u);
    Atom *equality = petta_builtin_signature(&arena, equal);
    assert(equality->expr.elems[1]->kind == ATOM_VAR);
    assert(!atom_eq(equality->expr.elems[1], equality->expr.elems[2]));

    const PettaBuiltinSpec *boolean = petta_builtin_spec_find("and", 2u);
    assert(boolean->arguments[0] == PETTA_BUILTIN_TYPE_BOOL);
    assert(boolean->declared_grade == PETTA_ANALYSIS_CARDINALITY_UNDETERMINED);
    assert(boolean->cardinality_rule == PETTA_BUILTIN_CARD_MANIFEST_BOOLEANS);
    assert(boolean->flat_cardinality == PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC);
    Atom *boolean_inputs[] = {atom_symbol(&arena, "True"), atom_symbol(&arena, "False")};
    assert(petta_builtin_literal_cardinality(boolean, boolean_inputs, 2u) ==
           PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC);
    boolean_inputs[0] = atom_var_with_id(&arena, "bool", fresh_var_id());
    assert(petta_builtin_literal_cardinality(boolean, boolean_inputs, 2u) ==
           PETTA_ANALYSIS_CARDINALITY_NONDETERMINISTIC);
    assert(petta_builtin_literal_cardinality(boolean, boolean_inputs, 1u) ==
           PETTA_ANALYSIS_CARDINALITY_UNDETERMINED);
    assert(petta_builtin_spec_find("+", 2u)->flat_cardinality ==
           PETTA_ANALYSIS_CARDINALITY_DETERMINISTIC);
    assert(petta_builtin_spec_find("car-atom", 1u)->contextual_typing != NULL);
    assert(!petta_builtin_signature(&arena, petta_builtin_spec_find("car-atom", 1u)));
    assert(!petta_builtin_spec_find("collapse", 1u)->eager);

    arena_free(&arena);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: 183 builtin records, 67 fresh type schemes, mode-sensitive Boolean cardinality");
    return 0;
}
