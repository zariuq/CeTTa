#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

/* A separate executable changes allocation identities before loading any
 * program. Semantic fixtures must still observe the same values and order. */
void __real_symbol_table_init_builtins(SymbolTable *table, BuiltinSyms *builtins);

void __wrap_symbol_table_init_builtins(SymbolTable *table, BuiltinSyms *builtins) {
    enum { SHIFT = 257 };
    for (unsigned i = 0u; i < SHIFT; i++) {
        char spelling[64];
        snprintf(spelling, sizeof(spelling), "symbol-id-shift-fixture-%u", i);
        (void)symbol_intern_cstr(table, spelling);
    }
    __real_symbol_table_init_builtins(table, builtins);
#define CHECK_SHIFTED(field, spelling) \
    if (builtins->field <= SHIFT) abort();
    CETTA_BUILTIN_SYMBOLS(CHECK_SHIFTED)
#undef CHECK_SHIFTED
}
