#ifndef CETTA_REGISTERED_SYMBOL_OWNER_H
#define CETTA_REGISTERED_SYMBOL_OWNER_H

#include "atom.h"
#include "registered_symbols.h"

/* The receiving arena owns the borrow until its corresponding mark resets.
 * Copying this struct's bits does not extend that lifetime: use the copy
 * operation while the source owner is still alive. */
typedef struct {
    const uint8_t *bytes;
    uint32_t length;
    uint64_t hash;
    void *owner;
} RegisteredSymbolView;

bool registered_symbol_own_in_arena(RegisteredSymbols *table,
    RegisteredSymbol source, Arena *destination, RegisteredSymbolView *out);
bool registered_symbol_view_copy(Arena *destination,
    const RegisteredSymbolView *source, RegisteredSymbolView *out);

#endif
