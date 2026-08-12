#ifndef CETTA_GSLT_INDEXED_VALUE_TABLE_V1_H
#define CETTA_GSLT_INDEXED_VALUE_TABLE_V1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t tag;
    uint32_t first;
    uint32_t second;
} CettaGsltIndexedValueV1;

typedef struct {
    CettaGsltIndexedValueV1 *items;
    uint32_t len;
    uint32_t cap;
} CettaGsltIndexedValueTableV1;

void cetta_gslt_indexed_value_table_init_v1(
    CettaGsltIndexedValueTableV1 *table);
void cetta_gslt_indexed_value_table_free_v1(
    CettaGsltIndexedValueTableV1 *table);
void cetta_gslt_indexed_value_table_reset_v1(
    CettaGsltIndexedValueTableV1 *table);

bool cetta_gslt_indexed_value_table_push_v1(
    CettaGsltIndexedValueTableV1 *table,
    CettaGsltIndexedValueV1 value);
bool cetta_gslt_indexed_value_table_get_v1(
    const CettaGsltIndexedValueTableV1 *table,
    uint32_t index,
    CettaGsltIndexedValueV1 *value_out);

#endif
