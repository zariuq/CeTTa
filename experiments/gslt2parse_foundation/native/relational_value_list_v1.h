#ifndef CETTA_GSLT2PARSE_RELATIONAL_VALUE_LIST_V1_H
#define CETTA_GSLT2PARSE_RELATIONAL_VALUE_LIST_V1_H

#include <stdbool.h>
#include <stdint.h>

#include "parser_occurrence_fold_v1.h"

typedef struct {
    const uint8_t *bytes;
    uint32_t byte_len;
    uint32_t item_len;
    uint32_t item_index;
    uint32_t byte_offset;
} PPRelationalValueListV1Cursor;

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} PPRelationalValueV1Slice;

bool pprelational_value_list_v1_encode_role(
    const PPOccurrenceFoldV1Step *step,
    uint32_t role_id,
    uint32_t skip,
    uint8_t **bytes_out,
    uint32_t *len_out);

bool pprelational_value_list_v1_encode_items(
    const PPRelationalValueV1Slice *items,
    uint32_t item_len,
    uint8_t **bytes_out,
    uint32_t *len_out);

bool pprelational_value_list_v1_cursor_init(
    const uint8_t *bytes,
    uint32_t byte_len,
    PPRelationalValueListV1Cursor *out);

bool pprelational_value_list_v1_cursor_next(
    PPRelationalValueListV1Cursor *cursor,
    const uint8_t **bytes_out,
    uint32_t *len_out);

#endif
