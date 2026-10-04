#ifndef CETTA_REGISTERED_SYMBOLS_H
#define CETTA_REGISTERED_SYMBOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A collectible namespace, separate from the process-lifetime SymbolId ABI.
 * Only owned handles cross this interface; a spelling lookup acquires a hold.
 * Tokens never repeat within a table. Exhaustion fails rather than wrapping.
 * Every successful acquire/clone creates exactly one release obligation.
 * Copying the handle bits does not create another obligation. */
typedef struct RegisteredSymbols RegisteredSymbols;
typedef struct {
    uint64_t table;
    uint64_t token;
} RegisteredSymbol;

/* A borrow is itself a hold. It may outlive the handle that opened it, but
 * bytes must not be used after registered_symbol_borrow_end. */
typedef struct {
    RegisteredSymbol hold;
    const uint8_t *bytes;
    uint32_t length;
    uint64_t hash;
} RegisteredSymbolBorrow;

typedef struct {
    uint64_t entries;
    uint64_t holds;
    uint64_t payload_bytes;
    uint64_t metadata_bytes;
    uint64_t peak_bytes;
    uint64_t reclaimed_entries;
    uint64_t reclaimed_bytes;
} RegisteredSymbolStats;

RegisteredSymbols *registered_symbols_new(void);
/* Refuses destruction while a handle or borrow remains. Destruction itself
 * requires external quiescence: no thread may begin a new table operation. */
bool registered_symbols_destroy(RegisteredSymbols *table);
bool registered_symbol_acquire(RegisteredSymbols *table,
    const uint8_t *bytes, uint32_t length, RegisteredSymbol *out);
/* Non-inserting lookup. A successful lookup still owns one hold. */
bool registered_symbol_find(RegisteredSymbols *table,
    const uint8_t *bytes, uint32_t length, RegisteredSymbol *out);
bool registered_symbol_clone(RegisteredSymbols *table,
    RegisteredSymbol source, RegisteredSymbol *out);
bool registered_symbol_release(RegisteredSymbols *table,
    RegisteredSymbol *owned);
bool registered_symbol_borrow(RegisteredSymbols *table,
    RegisteredSymbol source, RegisteredSymbolBorrow *out);
bool registered_symbol_borrow_end(RegisteredSymbols *table,
    RegisteredSymbolBorrow *borrow);
void registered_symbols_stats(RegisteredSymbols *table,
    RegisteredSymbolStats *out);

#ifdef CETTA_TEST_HOOKS
/* Only moves forward, with no outstanding entries. */
bool registered_symbols_set_next_token_test(RegisteredSymbols *table,
    uint64_t token);
#endif

#endif
