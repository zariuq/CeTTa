#ifndef CETTA_BINARY_CODEC_H
#define CETTA_BINARY_CODEC_H

#include <stddef.h>
#include <stdint.h>

/* E2-framed byte fields. Bits are most significant first; the final byte
 * is zero padded. The count and each length use canonical binary headers.
 * This is Framing.encode restricted to byte-aligned field payloads. */
typedef struct { const uint8_t *data; size_t size; } CettaBinaryField;
typedef struct { uint8_t *data; size_t size; } CettaBinaryOwnedField;
typedef enum {
    CETTA_BINARY_OK,
    CETTA_BINARY_OVERFLOW,
    CETTA_BINARY_NOMEM,
    CETTA_BINARY_TRUNCATED,
    CETTA_BINARY_NONCANONICAL,
    CETTA_BINARY_ALIGNMENT,
    CETTA_BINARY_TRAILING,
    CETTA_BINARY_INTERRUPTED
} CettaBinaryStatus;
typedef int (*CettaBinaryInterrupted)(void *context);

const char *cetta_binary_status_name(CettaBinaryStatus status);
CettaBinaryStatus cetta_binary_measure(const CettaBinaryField *fields, size_t count,
    size_t *bits, size_t *bytes);
CettaBinaryStatus cetta_binary_pack(const CettaBinaryField *fields, size_t count,
    CettaBinaryInterrupted interrupted, void *context, uint8_t **out, size_t *size);
CettaBinaryStatus cetta_binary_unpack(const uint8_t *data, size_t size,
    CettaBinaryInterrupted interrupted, void *context,
    CettaBinaryOwnedField **out, size_t *count);
void cetta_binary_fields_free(CettaBinaryOwnedField *fields, size_t count);

#ifdef CETTA_BINARY_TEST_HOOKS
void cetta_binary_test_fail_alloc_after(long successful_allocations);
#endif

#endif
