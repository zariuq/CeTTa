#include "atom.h"
#include "state_cell.h"
#include "symbol.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

extern bool rhocalc_managed_state_observer_selftest(void (*)(void));
void *__real_cetta_fallible_calloc(size_t, size_t);
static bool refuse_next;

void *__wrap_cetta_fallible_calloc(size_t count, size_t width) {
    if (refuse_next) {
        refuse_next = false;
        return NULL;
    }
    return __real_cetta_fallible_calloc(count, width);
}

static void refuse_observation_allocation(void) {
    refuse_next = true;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    assert(rhocalc_managed_state_observer_selftest(refuse_observation_allocation));
    assert(!refuse_next);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: managed Rho state snapshots preserve captured topology, source identities, cyclic cloning and promotion through retirement/refused observation allocation");
    return 0;
}
