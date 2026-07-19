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

    bool ok = rhocalc_abt_substitution_correspondence_selftest(&arena);

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;

    printf("(RhoABTSubstitutionSummary 1 %u %u)\n",
           ok ? 1u : 0u, ok ? 0u : 1u);
    if (!ok) return 1;
    puts("PASS: nameful rho substitution agrees with canonical ABT substitution");
    return 0;
}
