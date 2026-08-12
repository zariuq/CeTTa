#include "gslt_indexed_value_table_v1.h"

#include <limits.h>
#include <stdlib.h>

enum { CETTA_GSLT_INDEXED_VALUE_TABLE_INITIAL_CAPACITY_V1 = 16u };

void cetta_gslt_indexed_value_table_init_v1(
    CettaGsltIndexedValueTableV1 *table) {
    if (!table)
        return;
    table->items = NULL;
    table->len = 0u;
    table->cap = 0u;
}

void cetta_gslt_indexed_value_table_free_v1(
    CettaGsltIndexedValueTableV1 *table) {
    if (!table)
        return;
    free(table->items);
    cetta_gslt_indexed_value_table_init_v1(table);
}

void cetta_gslt_indexed_value_table_reset_v1(
    CettaGsltIndexedValueTableV1 *table) {
    if (table)
        table->len = 0u;
}

static bool cetta_gslt_indexed_value_table_reserve_v1(
    CettaGsltIndexedValueTableV1 *table, uint32_t required) {
    CettaGsltIndexedValueV1 *grown;
    uint32_t cap;

    if (!table)
        return false;
    if (required <= table->cap)
        return true;
    cap = table->cap != 0u
              ? table->cap
              : CETTA_GSLT_INDEXED_VALUE_TABLE_INITIAL_CAPACITY_V1;
    while (cap < required) {
        if (cap > UINT32_MAX / 2u) {
            cap = required;
            break;
        }
        cap *= 2u;
    }
    if ((size_t)cap > SIZE_MAX / sizeof(*table->items))
        return false;
    grown = realloc(table->items, (size_t)cap * sizeof(*table->items));
    if (!grown)
        return false;
    table->items = grown;
    table->cap = cap;
    return true;
}

bool cetta_gslt_indexed_value_table_push_v1(
    CettaGsltIndexedValueTableV1 *table,
    CettaGsltIndexedValueV1 value) {
    if (!table || table->len == UINT32_MAX ||
        !cetta_gslt_indexed_value_table_reserve_v1(table, table->len + 1u))
        return false;
    table->items[table->len++] = value;
    return true;
}

bool cetta_gslt_indexed_value_table_get_v1(
    const CettaGsltIndexedValueTableV1 *table,
    uint32_t index,
    CettaGsltIndexedValueV1 *value_out) {
    if (!table || !value_out || index >= table->len)
        return false;
    *value_out = table->items[index];
    return true;
}
