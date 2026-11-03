#ifndef CETTA_GSLT_U32_INDEX_V1_H
#define CETTA_GSLT_U32_INDEX_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_U32_INDEX_INSERTED_V1 = 0,
    CETTA_GSLT_U32_INDEX_DUPLICATE_V1 = 1,
    CETTA_GSLT_U32_INDEX_RESOURCE_V1 = 2,
    CETTA_GSLT_U32_INDEX_INVALID_V1 = 3
} CettaGsltU32IndexInsertResultV1;

/* An append-only finite map.  Occupancy is stored separately, so every
 * 32-bit key and value remains available to a generated plan. Fast slots
 * admit at most 32 probes; exact balanced overflow handles longer chains.
 * Reset preserves the fast capacity and placement, retiring overflow nodes. */
struct CettaGsltU32OverflowV1;

typedef struct {
    uint32_t *keys;
    uint32_t *values;
    uint8_t *occupied;
    uint32_t len;
    uint32_t cap;
    struct CettaGsltU32OverflowV1 *overflow;
    uint32_t overflow_len;
    uint32_t placement_salt;
} CettaGsltU32IndexV1;

void cetta_gslt_u32_index_init_v1(CettaGsltU32IndexV1 *index);
void cetta_gslt_u32_index_free_v1(CettaGsltU32IndexV1 *index);
void cetta_gslt_u32_index_reset_v1(CettaGsltU32IndexV1 *index);
/* Dynamic arrays and exact overflow controls, excluding the embedded index.
 * A saturated result cannot be used to forgive memory pressure. */
size_t cetta_gslt_u32_index_bytes_v1(const CettaGsltU32IndexV1 *index);

CettaGsltU32IndexInsertResultV1 cetta_gslt_u32_index_insert_unique_v1(
    CettaGsltU32IndexV1 *index, uint32_t key, uint32_t value);
bool cetta_gslt_u32_index_find_v1(
    const CettaGsltU32IndexV1 *index, uint32_t key, uint32_t *value_out);
bool cetta_gslt_u32_index_shape_valid_v1(
    const CettaGsltU32IndexV1 *index);
bool cetta_gslt_u32_index_validate_v1(const CettaGsltU32IndexV1 *index);

#endif
