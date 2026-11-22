#define CETTA_TEST_HOOKS 1

#include <stdio.h>

#include "atom.h"
#include "rhocalc_core.h"
#include "symbol.h"

int main(void) {
    SymbolTable symbols;
    Arena arena;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    bool abt_ok = rhocalc_abt_substitution_correspondence_selftest(&arena);
    bool order_ok = rhocalc_structural_order_selftest(&arena);
    unsigned passed = (abt_ok ? 1u : 0u) + (order_ok ? 1u : 0u);

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;

    printf("(RhoCoreCorrespondenceSummary 2 %u %u)\n",
           passed, 2u - passed);
    if (!abt_ok || !order_ok) return 1;
    puts("PASS: nameful rho substitution agrees with canonical ABT substitution");
    puts("PASS: the structural order identifies congruent terms and orders the rest");
    return 0;
}
