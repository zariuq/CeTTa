#include "relational_value_list_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ppvalue_list_v1_domain[8] = {
    'R', 'S', 'L', 'i', 's', 't', '1', '\n',
};

static void ppvalue_list_v1_write_u32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

static uint32_t ppvalue_list_v1_read_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           (uint32_t)bytes[3];
}

bool pprelational_value_list_v1_encode_items(
    const PPRelationalValueV1Slice *items,
    uint32_t item_len,
    uint8_t **bytes_out,
    uint32_t *len_out) {
    size_t total = sizeof(ppvalue_list_v1_domain) + 4u;
    size_t cursor;
    uint32_t index;
    uint8_t *bytes;

    if ((!items && item_len > 0u) || !bytes_out || !len_out)
        return false;
    *bytes_out = NULL;
    *len_out = 0u;
    for (index = 0u; index < item_len; index++) {
        if (!items[index].bytes || items[index].len == 0u ||
            total > UINT32_MAX - 4u ||
            items[index].len > UINT32_MAX - total - 4u)
            return false;
        total += 4u + items[index].len;
    }
    bytes = malloc(total);
    if (!bytes)
        return false;
    memcpy(bytes, ppvalue_list_v1_domain,
           sizeof(ppvalue_list_v1_domain));
    ppvalue_list_v1_write_u32(
        bytes + sizeof(ppvalue_list_v1_domain), item_len);
    cursor = sizeof(ppvalue_list_v1_domain) + 4u;
    for (index = 0u; index < item_len; index++) {
        ppvalue_list_v1_write_u32(bytes + cursor, items[index].len);
        cursor += 4u;
        memcpy(bytes + cursor, items[index].bytes, items[index].len);
        cursor += items[index].len;
    }
    *bytes_out = bytes;
    *len_out = (uint32_t)total;
    return true;
}

bool pprelational_value_list_v1_encode_role(
    const PPOccurrenceFoldV1Step *step,
    uint32_t role_id,
    uint32_t skip,
    uint8_t **bytes_out,
    uint32_t *len_out) {
    size_t total = sizeof(ppvalue_list_v1_domain) + 4u;
    uint32_t count = 0u;
    uint32_t seen = 0u;
    uint32_t index;
    uint8_t *bytes;
    size_t cursor;

    if (!step || !bytes_out || !len_out)
        return false;
    *bytes_out = NULL;
    *len_out = 0u;
    for (index = 0u; index < step->value_len; index++) {
        const PPOccurrenceFoldV1Value *value = &step->values[index];
        if (value->role_id != role_id || seen++ < skip)
            continue;
        if (!value->bytes || value->byte_len == 0u ||
            value->byte_len > UINT32_MAX ||
            total > UINT32_MAX - 4u ||
            value->byte_len > UINT32_MAX - total - 4u)
            return false;
        total += 4u + value->byte_len;
        if (count == UINT32_MAX)
            return false;
        count++;
    }
    bytes = malloc(total);
    if (!bytes)
        return false;
    memcpy(bytes, ppvalue_list_v1_domain,
           sizeof(ppvalue_list_v1_domain));
    ppvalue_list_v1_write_u32(
        bytes + sizeof(ppvalue_list_v1_domain), count);
    cursor = sizeof(ppvalue_list_v1_domain) + 4u;
    seen = 0u;
    for (index = 0u; index < step->value_len; index++) {
        const PPOccurrenceFoldV1Value *value = &step->values[index];
        if (value->role_id != role_id || seen++ < skip)
            continue;
        ppvalue_list_v1_write_u32(
            bytes + cursor, (uint32_t)value->byte_len);
        cursor += 4u;
        memcpy(bytes + cursor, value->bytes, value->byte_len);
        cursor += value->byte_len;
    }
    *bytes_out = bytes;
    *len_out = (uint32_t)total;
    return true;
}

bool pprelational_value_list_v1_cursor_init(
    const uint8_t *bytes,
    uint32_t byte_len,
    PPRelationalValueListV1Cursor *out) {
    PPRelationalValueListV1Cursor result;
    uint32_t index;
    uint32_t offset;

    if (!bytes || !out ||
        byte_len < sizeof(ppvalue_list_v1_domain) + 4u ||
        memcmp(bytes, ppvalue_list_v1_domain,
               sizeof(ppvalue_list_v1_domain)) != 0)
        return false;
    memset(&result, 0, sizeof(result));
    result.bytes = bytes;
    result.byte_len = byte_len;
    result.item_len = ppvalue_list_v1_read_u32(
        bytes + sizeof(ppvalue_list_v1_domain));
    result.byte_offset = sizeof(ppvalue_list_v1_domain) + 4u;
    offset = result.byte_offset;
    for (index = 0u; index < result.item_len; index++) {
        uint32_t item_len;
        if (offset > byte_len || byte_len - offset < 4u)
            return false;
        item_len = ppvalue_list_v1_read_u32(bytes + offset);
        offset += 4u;
        if (item_len == 0u || item_len > byte_len - offset)
            return false;
        offset += item_len;
    }
    if (offset != byte_len)
        return false;
    *out = result;
    return true;
}

bool pprelational_value_list_v1_cursor_next(
    PPRelationalValueListV1Cursor *cursor,
    const uint8_t **bytes_out,
    uint32_t *len_out) {
    uint32_t item_len;

    if (!cursor || !bytes_out || !len_out ||
        cursor->item_index >= cursor->item_len ||
        cursor->byte_offset > cursor->byte_len ||
        cursor->byte_len - cursor->byte_offset < 4u)
        return false;
    item_len = ppvalue_list_v1_read_u32(
        cursor->bytes + cursor->byte_offset);
    cursor->byte_offset += 4u;
    if (item_len == 0u ||
        item_len > cursor->byte_len - cursor->byte_offset)
        return false;
    *bytes_out = cursor->bytes + cursor->byte_offset;
    *len_out = item_len;
    cursor->byte_offset += item_len;
    cursor->item_index++;
    return true;
}
