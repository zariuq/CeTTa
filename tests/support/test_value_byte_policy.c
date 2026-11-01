#include "library.h"
#include "atom_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    CettaLibraryContext *source=calloc(1,sizeof(*source));
    CettaLibraryContext *captured=calloc(1,sizeof(*captured));
    CettaLibraryContext *other=calloc(1,sizeof(*other));
    assert(source && captured && other);
    assert(cetta_library_context_init_prime_snapshot(source));
    assert(cetta_library_value_byte_limit(source)==CETTA_ATOM_WIRE_BYTES);
    source->value_byte_limit=8u*CETTA_ATOM_WIRE_BYTES;
    assert(cetta_library_context_init_prime_snapshot_from(captured,source));
    assert(cetta_library_value_byte_limit(captured)==source->value_byte_limit);
    assert(cetta_library_context_init_prime_snapshot(other));
    assert(cetta_library_value_byte_limit(other)==CETTA_ATOM_WIRE_BYTES);
    other->value_byte_limit=4u*CETTA_ATOM_WIRE_BYTES;
    assert(cetta_library_value_byte_limit(source)==8u*CETTA_ATOM_WIRE_BYTES);
    assert(cetta_library_value_byte_limit(captured)==8u*CETTA_ATOM_WIRE_BYTES);
    assert(cetta_library_value_byte_limit(NULL)==CETTA_ATOM_WIRE_BYTES);
    cetta_library_context_free(other);cetta_library_context_free(captured);cetta_library_context_free(source);
    free(other);free(captured);free(source);symbol_table_free(&symbols);g_symbols=NULL;
    puts("PASS: portable byte policy is per context and inherited by private captures");
    return 0;
}
