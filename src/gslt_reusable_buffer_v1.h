#ifndef CETTA_GSLT_REUSABLE_BUFFER_V1_H
#define CETTA_GSLT_REUSABLE_BUFFER_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_REUSABLE_BUFFER_OK_V1 = 0,
    CETTA_GSLT_REUSABLE_BUFFER_INVALID_V1 = 1,
    CETTA_GSLT_REUSABLE_BUFFER_RESOURCE_V1 = 2,
    CETTA_GSLT_REUSABLE_BUFFER_LIMIT_V1 = 3,
} CettaGsltReusableBufferResultV1;

/* A caller-owned, single-lease buffer.  Release clears the logical sequence
 * while retaining capacity; clients must not retain element pointers across
 * release.  The active lease makes accidental nested reuse fail closed. */
typedef struct {
    void *items;
    uint32_t len;
    uint32_t capacity;
    size_t element_size;
    uint64_t acquire_len;
    uint64_t reuse_len;
    bool active;
} CettaGsltReusableBufferV1;

bool cetta_gslt_reusable_buffer_init_v1(
    CettaGsltReusableBufferV1 *buffer, size_t element_size);

void cetta_gslt_reusable_buffer_free_v1(
    CettaGsltReusableBufferV1 *buffer);

CettaGsltReusableBufferResultV1 cetta_gslt_reusable_buffer_acquire_v1(
    CettaGsltReusableBufferV1 *buffer);

CettaGsltReusableBufferResultV1 cetta_gslt_reusable_buffer_release_v1(
    CettaGsltReusableBufferV1 *buffer);

CettaGsltReusableBufferResultV1 cetta_gslt_reusable_buffer_push_v1(
    CettaGsltReusableBufferV1 *buffer, const void *value,
    uint32_t maximum_len);

void *cetta_gslt_reusable_buffer_at_v1(
    CettaGsltReusableBufferV1 *buffer, uint32_t index);

const void *cetta_gslt_reusable_buffer_at_const_v1(
    const CettaGsltReusableBufferV1 *buffer, uint32_t index);

bool cetta_gslt_reusable_buffer_truncate_v1(
    CettaGsltReusableBufferV1 *buffer, uint32_t len);

#endif
