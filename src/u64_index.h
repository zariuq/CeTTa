#ifndef CETTA_U64_INDEX_H
#define CETTA_U64_INDEX_H
#include "gslt_u32_index_v1.h"

/* Exact append-only u64 -> u32 indexing, composing the existing u32 index
 * over high and low words. Neither word nor value reserves a sentinel.
 * Mutation is caller-synchronized; returned values confer no ownership. */
typedef struct {
    CettaGsltU32IndexV1 prefixes;
    CettaGsltU32IndexV1 *branches;
    uint32_t length, capacity;
} CettaU64Index;

void cetta_u64_index_init(CettaU64Index *);
void cetta_u64_index_free(CettaU64Index *);
bool cetta_u64_index_find(const CettaU64Index *, uint64_t, uint32_t *);
CettaGsltU32IndexInsertResultV1 cetta_u64_index_insert(CettaU64Index *, uint64_t, uint32_t);
#endif
