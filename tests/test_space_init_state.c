/* A space starts in a defined state wherever its storage comes from: arena
 * and stack storage are not zeroed. In particular declares_subtypes starts
 * false, the claim HE type inference relies on to skip querying the space
 * for subtype rows. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "space.h"
#include "term_universe.h"

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena;
    arena_init(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);

    static Space storage;
    memset(&storage, 0xff, sizeof(storage));
    space_init_with_universe(&storage, &universe);
    assert(!storage.declares_subtypes);
    assert(!storage.module_own_view && !storage.overlay_base);
    space_free(&storage);

    memset(&storage, 0xff, sizeof(storage));
    space_init(&storage);
    assert(!storage.declares_subtypes);
    space_free(&storage);

    term_universe_free(&universe);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: a space initialized over unzeroed storage declares no subtypes");
    return 0;
}
