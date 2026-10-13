/* A failed native call in Prime is zero answers, never the datum `Empty`.
 * Prime's no-result sentinel, like PeTTa's, is the internal atom no program
 * can write: collection drops it, and the datum `Empty` stays data.  HE keeps
 * the datum as its sentinel.  This translation unit replaces eval.o in the
 * test executable. */
#include "../src/eval.c"
#include <assert.h>
#include <stdio.h>

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);

    Atom *datum = atom_empty(&arena);
    Atom *value = atom_symbol(&arena, "Some");

    /* Prime: the failure is the internal sentinel, which collection drops. */
    g_active_language_id = CETTA_LANGUAGE_PRIME;
    Atom *failed = eval_call_outcome_atom(&arena, cetta_call_failure());
    assert(failed);
    assert(!atom_is_empty(failed));
    assert(atom_is_petta_no_result(failed));
    assert(atom_is_legacy_empty_sentinel(failed));
    assert(!atom_is_legacy_empty_sentinel(datum));
    assert(!atom_is_legacy_empty_or_error(datum));
    assert(eval_call_outcome_atom(&arena, cetta_call_value(value)) == value);
    assert(eval_call_outcome_atom(&arena, cetta_call_value(datum)) == datum);

    /* PeTTa: unchanged. */
    g_active_language_id = CETTA_LANGUAGE_PETTA;
    failed = eval_call_outcome_atom(&arena, cetta_call_failure());
    assert(failed && atom_is_petta_no_result(failed) && !atom_is_empty(failed));
    assert(atom_is_legacy_empty_sentinel(failed));
    assert(!atom_is_legacy_empty_sentinel(datum));

    /* HE: unchanged, the datum `Empty` is its sentinel. */
    g_active_language_id = CETTA_LANGUAGE_HE;
    failed = eval_call_outcome_atom(&arena, cetta_call_failure());
    assert(failed && atom_is_empty(failed));
    assert(atom_is_legacy_empty_sentinel(datum));

    puts("PASS: a failed call in Prime is the internal no-result sentinel, never the datum Empty; HE and PeTTa unchanged");
    arena_free(&arena);
    return 0;
}
