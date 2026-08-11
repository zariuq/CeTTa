#ifndef CETTA_GSLT2PARSE_RELATIONAL_STORE_V1_H
#define CETTA_GSLT2PARSE_RELATIONAL_STORE_V1_H

#include <stdbool.h>
#include <stdint.h>

/* Read/write access to an interned relational store.  Table identities and
 * their meaning come from generated plans; this interface only exposes the
 * generic row and value operations used by staged consumers. */
typedef struct {
    void *context;
    /* Nonzero identity unique for the lifetime of this store instance. */
    uint64_t identity;
    /* A successful snapshot certifies that rows [0, row_len_out) are an
     * immutable prefix: every later successful snapshot for this identity
     * has at least that length and exactly the same rows at those indices.
     * Stores return false for tables without this append-only property. */
    bool (*table_immutable_prefix)(void *context, uint32_t table_id,
                                   uint32_t *row_len_out);
    bool (*table_shape)(void *context, uint32_t table_id,
                        uint32_t *arity_out, uint32_t *key_arity_out,
                        uint32_t *row_len_out);
    bool (*table_row)(void *context, uint32_t table_id,
                      uint32_t row_index, uint32_t *values_out,
                      uint32_t value_capacity);
    bool (*table_find)(void *context, uint32_t table_id,
                       const uint32_t *key, uint32_t key_len,
                       uint32_t *values_out, uint32_t value_capacity);
    /* Traverse rows sharing a key prefix.  The opaque cursor carries all
     * backend traversal state; callers initialize it to UINT64_MAX.  Only
     * columns selected by column_mask are materialized in values_out, and
     * unselected columns are zero. */
    bool (*table_prefix_next)(void *context, uint32_t table_id,
                              const uint32_t *prefix,
                              uint32_t prefix_len,
                              uint32_t column_mask,
                              uint64_t *cursor_io,
                              uint32_t *values_out,
                              uint32_t value_capacity,
                              bool *found_out);
    bool (*value_intern)(void *context, const uint8_t *bytes,
                         uint32_t len, uint32_t *value_out);
    bool (*value_bytes)(void *context, uint32_t value,
                        const uint8_t **bytes_out, uint32_t *len_out);
} PPRelationalStoreV1;

static inline bool pprelational_store_v1_valid(
    const PPRelationalStoreV1 *store) {
    return store && store->context && store->identity != 0u &&
           store->table_immutable_prefix && store->table_shape &&
           store->table_row && store->table_find &&
           store->table_prefix_next && store->value_intern &&
           store->value_bytes;
}

/* Relational scalar values are interned byte strings.  Generated programs
 * use canonical unsigned decimal bytes when a scalar denotes an ordinal;
 * consumers must decode those bytes rather than treating the private intern
 * identifier as the ordinal itself. */
static inline bool pprelational_store_v1_value_u32_decimal(
    const PPRelationalStoreV1 *store, uint32_t value,
    uint32_t *decoded_out) {
    const uint8_t *bytes = NULL;
    uint32_t len = 0u;
    uint32_t decoded = 0u;
    uint32_t index;

    if (!store || !store->context || !store->value_bytes || !decoded_out ||
        !store->value_bytes(
            store->context, value, &bytes, &len) ||
        !bytes || len == 0u || (len > 1u && bytes[0] == (uint8_t)'0'))
        return false;
    for (index = 0u; index < len; index++) {
        uint32_t digit;
        if (bytes[index] < (uint8_t)'0' || bytes[index] > (uint8_t)'9')
            return false;
        digit = (uint32_t)(bytes[index] - (uint8_t)'0');
        if (decoded > (UINT32_MAX - digit) / UINT32_C(10))
            return false;
        decoded = decoded * UINT32_C(10) + digit;
    }
    *decoded_out = decoded;
    return true;
}

#endif
