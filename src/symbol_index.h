#ifndef CETTA_SYMBOL_INDEX_H
#define CETTA_SYMBOL_INDEX_H

#include "symbol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* Symbols to small indexes (nodes of a graph being explored): open
 * addressing, kept at most half full, so finding a node costs a constant
 * expected number of probes however many there are. */
typedef struct {
    SymbolId *keys;
    size_t *values;   /* index + 1; 0 marks an empty slot */
    size_t mask;
    size_t used;
} CettaSymbolIndex;

static inline size_t cetta_symbol_index_slot(SymbolId key, size_t mask) {
    return (size_t)(((uint64_t)key * UINT64_C(0x9e3779b97f4a7c15)) >> 32) & mask;
}

static inline void cetta_symbol_index_free(CettaSymbolIndex *index) {
    free(index->keys);
    free(index->values);
    *index = (CettaSymbolIndex){0};
}

/* The index stored for `key`, or SIZE_MAX. */
static inline size_t cetta_symbol_index_find(const CettaSymbolIndex *index,
                                             SymbolId key) {
    if (!index->values)
        return SIZE_MAX;
    for (size_t slot = cetta_symbol_index_slot(key, index->mask);;
         slot = (slot + 1u) & index->mask) {
        if (index->values[slot] == 0u)
            return SIZE_MAX;
        if (index->keys[slot] == key)
            return index->values[slot] - 1u;
    }
}

/* Store `value` for a `key` not yet present; false only when the table
 * cannot grow. */
static inline bool cetta_symbol_index_put(CettaSymbolIndex *index,
                                          SymbolId key, size_t value) {
    if (!index->values || (index->used + 1u) * 2u > index->mask + 1u) {
        size_t size = index->values ? (index->mask + 1u) * 2u : 64u;
        SymbolId *keys = malloc(size * sizeof(*keys));
        size_t *values = calloc(size, sizeof(*values));
        if (!keys || !values) {
            free(keys);
            free(values);
            return false;
        }
        for (size_t s = 0u; index->values && s <= index->mask; s++) {
            if (index->values[s] == 0u)
                continue;
            size_t slot = cetta_symbol_index_slot(index->keys[s], size - 1u);
            while (values[slot])
                slot = (slot + 1u) & (size - 1u);
            keys[slot] = index->keys[s];
            values[slot] = index->values[s];
        }
        free(index->keys);
        free(index->values);
        index->keys = keys;
        index->values = values;
        index->mask = size - 1u;
    }
    size_t slot = cetta_symbol_index_slot(key, index->mask);
    while (index->values[slot])
        slot = (slot + 1u) & index->mask;
    index->keys[slot] = key;
    index->values[slot] = value + 1u;
    index->used++;
    return true;
}

#endif /* CETTA_SYMBOL_INDEX_H */
