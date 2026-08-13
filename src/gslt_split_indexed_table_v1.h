#ifndef CETTA_GSLT_SPLIT_INDEXED_TABLE_V1_H
#define CETTA_GSLT_SPLIT_INDEXED_TABLE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_PREPARED = 1,
    CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_SAVED = 2
} CettaGsltSplitIndexedValueKindV1;

typedef struct {
    const void *value;
    CettaGsltSplitIndexedValueKindV1 kind;
} CettaGsltSplitIndexedValueV1;

typedef struct {
    const void *prepared;
    uint32_t prepared_len;
    size_t prepared_stride;
    const void *saved;
    uint32_t saved_len;
    size_t saved_stride;
} CettaGsltSplitIndexedTableV1;

bool cetta_gslt_split_indexed_table_validate_v1(
    const CettaGsltSplitIndexedTableV1 *table);

bool cetta_gslt_split_indexed_table_get_v1(
    const CettaGsltSplitIndexedTableV1 *table,
    uint64_t index,
    CettaGsltSplitIndexedValueV1 *value_out);

#endif
