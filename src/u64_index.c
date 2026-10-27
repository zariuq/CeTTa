#include "u64_index.h"
#include <stdlib.h>
#include <string.h>

void cetta_u64_index_init(CettaU64Index *index) {
    if (index) memset(index, 0, sizeof(*index));
}

void cetta_u64_index_free(CettaU64Index *index) {
    if (!index) return;
    cetta_gslt_u32_index_free_v1(&index->prefixes);
    for (uint32_t i = 0; i < index->length; i++) cetta_gslt_u32_index_free_v1(&index->branches[i]);
    free(index->branches);
    cetta_u64_index_init(index);
}

bool cetta_u64_index_find(const CettaU64Index *index, uint64_t key, uint32_t *value) {
    uint32_t branch;
    return index && cetta_gslt_u32_index_find_v1(&index->prefixes, (uint32_t)(key >> 32), &branch) &&
        branch < index->length && cetta_gslt_u32_index_find_v1(&index->branches[branch], (uint32_t)key, value);
}

CettaGsltU32IndexInsertResultV1 cetta_u64_index_insert(CettaU64Index *index, uint64_t key, uint32_t value) {
    if (!index) return CETTA_GSLT_U32_INDEX_INVALID_V1;
    uint32_t branch;
    if (!cetta_gslt_u32_index_find_v1(&index->prefixes, (uint32_t)(key >> 32), &branch)) {
        if (index->length == UINT32_MAX) return CETTA_GSLT_U32_INDEX_RESOURCE_V1;
        if (index->length == index->capacity) {
            uint32_t next = index->capacity ? index->capacity * 2u : 4u;
            if (next < index->capacity || (uint64_t)next * sizeof(*index->branches) > SIZE_MAX)
                return CETTA_GSLT_U32_INDEX_RESOURCE_V1;
            CettaGsltU32IndexV1 *more = realloc(index->branches, (size_t)next * sizeof(*more));
            if (!more) return CETTA_GSLT_U32_INDEX_RESOURCE_V1;
            index->branches = more; index->capacity = next;
        }
        branch = index->length;
        cetta_gslt_u32_index_init_v1(&index->branches[branch]);
        CettaGsltU32IndexInsertResultV1 status = cetta_gslt_u32_index_insert_unique_v1(
            &index->prefixes, (uint32_t)(key >> 32), branch);
        if (status != CETTA_GSLT_U32_INDEX_INSERTED_V1) return status;
        index->length++;
    }
    return cetta_gslt_u32_index_insert_unique_v1(&index->branches[branch], (uint32_t)key, value);
}
