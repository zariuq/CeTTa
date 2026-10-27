#include "binary_codec.h"

#include <stdbool.h>
#include <limits.h>
#include <stdlib.h>

typedef struct { uint8_t *data; size_t at; } Writer;
typedef struct { const uint8_t *data; size_t at, bits; } Reader;

#ifdef CETTA_BINARY_TEST_HOOKS
static long allocation_countdown = -1;
void cetta_binary_test_fail_alloc_after(long successful_allocations) {
    allocation_countdown = successful_allocations;
}
static bool allocation_allowed(void) {
    if (allocation_countdown < 0) return true;
    if (!allocation_countdown) return false;
    allocation_countdown--;
    return true;
}
#else
static bool allocation_allowed(void) { return true; }
#endif

static void *allocate(size_t bytes, bool clear) {
    if (!allocation_allowed()) return NULL;
    return clear ? calloc(bytes, 1) : malloc(bytes);
}

const char *cetta_binary_status_name(CettaBinaryStatus status) {
    static const char *const names[] = {"ok", "size-overflow", "out-of-memory",
        "truncated", "noncanonical", "unaligned-field", "trailing-data", "interrupted"};
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "invalid-status";
}

static size_t width(size_t value) {
    size_t bits = 0;
    while (value) { bits++; value >>= 1; }
    return bits;
}

static bool add(size_t *total, size_t value) {
    if (value > SIZE_MAX - *total) return false;
    *total += value;
    return true;
}

static bool frame_size(size_t payload, size_t *out) {
    *out = payload;
    return add(out, 2 * width(payload) + 1);
}

CettaBinaryStatus cetta_binary_measure(const CettaBinaryField *fields, size_t count,
    size_t *bits, size_t *bytes) {
    size_t total;
    *bits = *bytes = 0;
    if (!frame_size(width(count), &total)) return CETTA_BINARY_OVERFLOW;
    for (size_t i = 0; i < count; i++) {
        size_t field;
        if (fields[i].size > SIZE_MAX / 8 ||
            !frame_size(8 * fields[i].size, &field) || !add(&total, field))
            return CETTA_BINARY_OVERFLOW;
    }
    *bits = total;
    *bytes = total / 8 + (total % 8 != 0);
    return CETTA_BINARY_OK;
}

static void put_bit(Writer *writer, bool bit) {
    if (bit) writer->data[writer->at / 8] |= (uint8_t)(1u << (7 - writer->at % 8));
    writer->at++;
}

static void put_number(Writer *writer, size_t number, size_t digits) {
    while (digits) { digits--; put_bit(writer, (number >> digits) & 1u); }
}

static void put_header(Writer *writer, size_t payload_bits) {
    size_t digits = width(payload_bits);
    for (size_t i = 0; i < digits; i++) put_bit(writer, true);
    put_bit(writer, false);
    put_number(writer, payload_bits, digits);
}

static void put_byte(Writer *writer, uint8_t byte) {
    size_t index = writer->at / 8, shift = writer->at % 8;
    writer->data[index] |= byte >> shift;
    if (shift) writer->data[index + 1] |= (uint8_t)(byte << (8 - shift));
    writer->at += 8;
}

CettaBinaryStatus cetta_binary_pack(const CettaBinaryField *fields, size_t count,
    CettaBinaryInterrupted interrupted, void *context, uint8_t **out, size_t *size) {
    *out = NULL; *size = 0;
    if (interrupted && interrupted(context)) return CETTA_BINARY_INTERRUPTED;
    size_t bits, bytes;
    CettaBinaryStatus status = cetta_binary_measure(fields, count, &bits, &bytes);
    if (status != CETTA_BINARY_OK) return status;
    uint8_t *data = allocate(bytes, true);
    if (!data) return CETTA_BINARY_NOMEM;
    Writer writer = {data, 0};
    size_t count_bits = width(count);
    put_header(&writer, count_bits);
    put_number(&writer, count, count_bits);
    for (size_t i = 0; i < count; i++) {
        if (interrupted && interrupted(context)) { free(data); return CETTA_BINARY_INTERRUPTED; }
        put_header(&writer, fields[i].size * 8);
        for (size_t k = 0; k < fields[i].size; k++) {
            if (!(k & 4095u) && interrupted && interrupted(context)) {
                free(data); return CETTA_BINARY_INTERRUPTED;
            }
            put_byte(&writer, fields[i].data[k]);
        }
    }
    *out = data; *size = bytes;
    return CETTA_BINARY_OK;
}

static bool get_bit(Reader *reader, bool *out) {
    if (reader->at == reader->bits) return false;
    *out = (reader->data[reader->at / 8] >> (7 - reader->at % 8)) & 1u;
    reader->at++;
    return true;
}

static CettaBinaryStatus get_number(Reader *reader, size_t digits, size_t *out) {
    if (digits > sizeof(size_t) * CHAR_BIT) return CETTA_BINARY_OVERFLOW;
    if (digits > reader->bits - reader->at) return CETTA_BINARY_TRUNCATED;
    size_t value = 0;
    for (size_t k = 0; k < digits; k++) {
        bool bit;
        if (!get_bit(reader, &bit)) return CETTA_BINARY_TRUNCATED;
        if (!k && !bit) return CETTA_BINARY_NONCANONICAL;
        value = (value << 1) | bit;
    }
    *out = value;
    return CETTA_BINARY_OK;
}

static CettaBinaryStatus get_header(Reader *reader, size_t *out) {
    size_t digits = 0;
    bool bit;
    do {
        if (!get_bit(reader, &bit)) return CETTA_BINARY_TRUNCATED;
        if (bit && ++digits > sizeof(size_t) * CHAR_BIT) return CETTA_BINARY_OVERFLOW;
    } while (bit);
    CettaBinaryStatus status = get_number(reader, digits, out);
    if (status == CETTA_BINARY_OK && *out > reader->bits - reader->at)
        return CETTA_BINARY_TRUNCATED;
    return status;
}

static uint8_t get_byte(Reader *reader) {
    size_t index = reader->at / 8, shift = reader->at % 8;
    uint8_t byte = (uint8_t)(reader->data[index] << shift);
    if (shift) byte |= reader->data[index + 1] >> (8 - shift);
    reader->at += 8;
    return byte;
}

void cetta_binary_fields_free(CettaBinaryOwnedField *fields, size_t count) {
    if (fields) for (size_t k = 0; k < count; k++) free(fields[k].data);
    free(fields);
}

CettaBinaryStatus cetta_binary_unpack(const uint8_t *data, size_t size,
    CettaBinaryInterrupted interrupted, void *context,
    CettaBinaryOwnedField **out, size_t *count) {
    *out = NULL; *count = 0;
    if (interrupted && interrupted(context)) return CETTA_BINARY_INTERRUPTED;
    if (size > SIZE_MAX / 8) return CETTA_BINARY_OVERFLOW;
    Reader reader = {data, 0, size * 8};
    size_t count_bits, length;
    CettaBinaryStatus status = get_header(&reader, &count_bits);
    if (status != CETTA_BINARY_OK) return status;
    status = get_number(&reader, count_bits, &length);
    if (status != CETTA_BINARY_OK) return status;
    /* Every field needs at least one header bit. Reject impossible counts
     * before allocating the output table. */
    if (length > reader.bits - reader.at) return CETTA_BINARY_TRUNCATED;
    if (length > SIZE_MAX / sizeof(CettaBinaryOwnedField)) return CETTA_BINARY_OVERFLOW;
    CettaBinaryOwnedField *fields = length ? allocate(length * sizeof(*fields), true) : NULL;
    if (length && !fields) return CETTA_BINARY_NOMEM;
    for (size_t k = 0; k < length; k++) {
        if (interrupted && interrupted(context)) { status = CETTA_BINARY_INTERRUPTED; goto fail; }
        size_t field_bits;
        status = get_header(&reader, &field_bits);
        if (status != CETTA_BINARY_OK) goto fail;
        if (field_bits % 8) { status = CETTA_BINARY_ALIGNMENT; goto fail; }
        fields[k].size = field_bits / 8;
        if (!fields[k].size) continue;
        fields[k].data = allocate(fields[k].size, false);
        if (!fields[k].data) { status = CETTA_BINARY_NOMEM; goto fail; }
        for (size_t i = 0; i < fields[k].size; i++) {
            if (!(i & 4095u) && interrupted && interrupted(context)) {
                status = CETTA_BINARY_INTERRUPTED; goto fail;
            }
            fields[k].data[i] = get_byte(&reader);
        }
    }
    if (reader.bits - reader.at >= 8) { status = CETTA_BINARY_TRAILING; goto fail; }
    while (reader.at < reader.bits) {
        bool bit;
        if (!get_bit(&reader, &bit)) { status = CETTA_BINARY_TRUNCATED; goto fail; }
        if (bit) { status = CETTA_BINARY_TRAILING; goto fail; }
    }
    *out = fields; *count = length;
    return CETTA_BINARY_OK;
fail:
    cetta_binary_fields_free(fields, length);
    return status;
}
