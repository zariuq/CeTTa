#include "registered_symbol_owner.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    RegisteredSymbols *table;
    RegisteredSymbolBorrow borrow;
    _Atomic uint64_t references;
} RegisteredSymbolOwner;

static void owner_retain(void *pointer) {
    RegisteredSymbolOwner *owner = pointer;
    uint64_t n = atomic_load_explicit(&owner->references, memory_order_relaxed);
    do {
        if (!n || n == UINT64_MAX) abort();
    } while (!atomic_compare_exchange_weak_explicit(&owner->references, &n, n + 1,
                  memory_order_relaxed, memory_order_relaxed));
}

static void owner_release(void *pointer) {
    RegisteredSymbolOwner *owner = pointer;
    uint64_t prior = atomic_fetch_sub_explicit(&owner->references, 1,
                                              memory_order_acq_rel);
    if (!prior) abort();
    if (prior == 1) {
        if (!registered_symbol_borrow_end(owner->table, &owner->borrow)) abort();
        free(owner);
    }
}

bool registered_symbol_own_in_arena(RegisteredSymbols *table,
        RegisteredSymbol source, Arena *destination, RegisteredSymbolView *out) {
    if (!table || !destination || !out) return false;
    RegisteredSymbolOwner *owner = malloc(sizeof(*owner));
    if (!owner) return false;
    owner->table = table;
    atomic_init(&owner->references, 1);
    if (!registered_symbol_borrow(table, source, &owner->borrow)) {
        free(owner);
        return false;
    }
    bool ok = arena_retain_owner(destination, owner, owner_retain, owner_release);
    if (ok) *out = (RegisteredSymbolView){owner->borrow.bytes,
        owner->borrow.length, owner->borrow.hash, owner};
    owner_release(owner);
    return ok;
}

bool registered_symbol_view_copy(Arena *destination,
        const RegisteredSymbolView *source, RegisteredSymbolView *out) {
    if (!destination || !source || !source->owner || !out) return false;
    if (!arena_retain_owner(destination, source->owner, owner_retain, owner_release))
        return false;
    *out = *source;
    return true;
}
