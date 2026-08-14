#ifndef CETTA_GSLT_U32_INDEX_V1_H
#define CETTA_GSLT_U32_INDEX_V1_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_U32_INDEX_INSERTED_V1 = 0,
    CETTA_GSLT_U32_INDEX_DUPLICATE_V1 = 1,
    CETTA_GSLT_U32_INDEX_RESOURCE_V1 = 2,
    CETTA_GSLT_U32_INDEX_INVALID_V1 = 3
} CettaGsltU32IndexInsertResultV1;

/* An append-only finite map.  Occupancy is stored separately, so every
 * 32-bit key and value remains available to a generated plan. */
typedef struct {
    uint32_t *keys;
    uint32_t *values;
    uint8_t *occupied;
    uint32_t len;
    uint32_t cap;
} CettaGsltU32IndexV1;

void cetta_gslt_u32_index_init_v1(CettaGsltU32IndexV1 *index);
void cetta_gslt_u32_index_free_v1(CettaGsltU32IndexV1 *index);
void cetta_gslt_u32_index_reset_v1(CettaGsltU32IndexV1 *index);

CettaGsltU32IndexInsertResultV1 cetta_gslt_u32_index_insert_unique_v1(
    CettaGsltU32IndexV1 *index, uint32_t key, uint32_t value);
bool cetta_gslt_u32_index_find_v1(
    const CettaGsltU32IndexV1 *index, uint32_t key, uint32_t *value_out);
bool cetta_gslt_u32_index_shape_valid_v1(
    const CettaGsltU32IndexV1 *index);
bool cetta_gslt_u32_index_validate_v1(const CettaGsltU32IndexV1 *index);

#endif
