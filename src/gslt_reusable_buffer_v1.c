#include "gslt_reusable_buffer_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool cetta_gslt_reusable_buffer_init_v1(
    CettaGsltReusableBufferV1 *buffer, size_t element_size) {
    if (!buffer || element_size == 0u)
        return false;
    memset(buffer, 0, sizeof(*buffer));
    buffer->element_size = element_size;
    return true;
}

void cetta_gslt_reusable_buffer_free_v1(
    CettaGsltReusableBufferV1 *buffer) {
    if (!buffer)
        return;
    free(buffer->items);
    memset(buffer, 0, sizeof(*buffer));
}

CettaGsltReusableBufferResultV1 cetta_gslt_reusable_buffer_acquire_v1(
    CettaGsltReusableBufferV1 *buffer) {
    bool reused;

    if (!buffer || buffer->element_size == 0u || buffer->active)
        return CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1;
    reused = buffer->capacity != 0u;
    buffer->len = 0u;
    buffer->active = true;
    if (buffer->acquire_len != UINT64_MAX)
        buffer->acquire_len++;
    if (reused && buffer->reuse_len != UINT64_MAX)
        buffer->reuse_len++;
    return CETTA_GSLT_REUSABLE_BUFFER_OK_V1;
}

CettaGsltReusableBufferResultV1 cetta_gslt_reusable_buffer_release_v1(
    CettaGsltReusableBufferV1 *buffer) {
    if (!buffer || buffer->element_size == 0u || !buffer->active)
        return CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1;
    buffer->len = 0u;
    buffer->active = false;
    return CETTA_GSLT_REUSABLE_BUFFER_OK_V1;
}

static CettaGsltReusableBufferResultV1
cetta_gslt_reusable_buffer_grow_v1(
    CettaGsltReusableBufferV1 *buffer, uint32_t minimum_capacity) {
    uint32_t capacity;
    void *items;

    if (!buffer || buffer->element_size == 0u || !buffer->active)
        return CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1;
    if (minimum_capacity <= buffer->capacity)
        return CETTA_GSLT_REUSABLE_BUFFER_OK_V1;
    capacity = buffer->capacity ? buffer->capacity : 16u;
    while (capacity < minimum_capacity) {
        if (capacity > UINT32_MAX / 2u) {
            capacity = minimum_capacity;
            break;
        }
        capacity *= 2u;
    }
    if ((size_t)capacity > SIZE_MAX / buffer->element_size)
        return CETTA_GSLT_REUSABLE_BUFFER_RESOURCE_V1;
    items = realloc(buffer->items,
                    (size_t)capacity * buffer->element_size);
    if (!items)
        return CETTA_GSLT_REUSABLE_BUFFER_RESOURCE_V1;
    buffer->items = items;
    buffer->capacity = capacity;
    return CETTA_GSLT_REUSABLE_BUFFER_OK_V1;
}

CettaGsltReusableBufferResultV1 cetta_gslt_reusable_buffer_push_v1(
    CettaGsltReusableBufferV1 *buffer, const void *value,
    uint32_t maximum_len) {
    CettaGsltReusableBufferResultV1 result;

    if (!buffer || !value || buffer->element_size == 0u || !buffer->active)
        return CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1;
    if (buffer->len >= maximum_len)
        return CETTA_GSLT_REUSABLE_BUFFER_LIMIT_V1;
    result = cetta_gslt_reusable_buffer_grow_v1(
        buffer, buffer->len + 1u);
    if (result != CETTA_GSLT_REUSABLE_BUFFER_OK_V1)
        return result;
    memcpy((unsigned char *)buffer->items +
               (size_t)buffer->len * buffer->element_size,
           value, buffer->element_size);
    buffer->len++;
    return CETTA_GSLT_REUSABLE_BUFFER_OK_V1;
}

void *cetta_gslt_reusable_buffer_at_v1(
    CettaGsltReusableBufferV1 *buffer, uint32_t index) {
    if (!buffer || !buffer->active || index >= buffer->len)
        return NULL;
    return (unsigned char *)buffer->items +
           (size_t)index * buffer->element_size;
}

const void *cetta_gslt_reusable_buffer_at_const_v1(
    const CettaGsltReusableBufferV1 *buffer, uint32_t index) {
    if (!buffer || !buffer->active || index >= buffer->len)
        return NULL;
    return (const unsigned char *)buffer->items +
           (size_t)index * buffer->element_size;
}

bool cetta_gslt_reusable_buffer_truncate_v1(
    CettaGsltReusableBufferV1 *buffer, uint32_t len) {
    if (!buffer || !buffer->active || len > buffer->len)
        return false;
    buffer->len = len;
    return true;
}
