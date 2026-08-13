#include "gslt_split_indexed_table_v1.h"

#include <limits.h>

static bool split_region_valid(
    const void *values, uint32_t len, size_t stride) {
    return (len == 0u || (values && stride != 0u)) &&
           (len == 0u || (size_t)(len - 1u) <= SIZE_MAX / stride);
}

bool cetta_gslt_split_indexed_table_validate_v1(
    const CettaGsltSplitIndexedTableV1 *table) {
    return table &&
           split_region_valid(
               table->prepared, table->prepared_len,
               table->prepared_stride) &&
           split_region_valid(
               table->saved, table->saved_len, table->saved_stride);
}

bool cetta_gslt_split_indexed_table_get_v1(
    const CettaGsltSplitIndexedTableV1 *table,
    uint64_t index,
    CettaGsltSplitIndexedValueV1 *value_out) {
    const uint8_t *base;
    uint64_t suffix_index;

    if (!value_out || !cetta_gslt_split_indexed_table_validate_v1(table))
        return false;
    value_out->value = NULL;
    value_out->kind = 0;
    if (index < table->prepared_len) {
        base = table->prepared;
        value_out->value = base + (size_t)index * table->prepared_stride;
        value_out->kind = CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_PREPARED;
        return true;
    }
    suffix_index = index - table->prepared_len;
    if (suffix_index >= table->saved_len)
        return false;
    base = table->saved;
    value_out->value =
        base + (size_t)suffix_index * table->saved_stride;
    value_out->kind = CETTA_GSLT_SPLIT_INDEXED_VALUE_V1_SAVED;
    return true;
}
