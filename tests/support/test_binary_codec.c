#include "binary_codec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t checks;
#define CHECK(condition) do { assert(condition); checks++; } while (0)

static int cancel(void *unused) { (void)unused; return 1; }

static void canonical(const uint8_t *input, size_t size) {
    CettaBinaryOwnedField *decoded = NULL;
    size_t count = 0;
    CettaBinaryStatus status = cetta_binary_unpack(input, size, NULL, NULL, &decoded, &count);
    if (status != CETTA_BINARY_OK) {
        CHECK(decoded == NULL && count == 0);
        return;
    }
    CettaBinaryField *fields = count ? calloc(count, sizeof(*fields)) : NULL;
    CHECK(!count || fields);
    for (size_t i = 0; i < count; i++) fields[i] = (CettaBinaryField){decoded[i].data, decoded[i].size};
    uint8_t *encoded = NULL;
    size_t encoded_size = 0;
    CHECK(cetta_binary_pack(fields, count, NULL, NULL, &encoded, &encoded_size) == CETTA_BINARY_OK);
    CHECK(encoded_size == size && !memcmp(encoded, input, size));
    free(encoded); free(fields); cetta_binary_fields_free(decoded, count);
}

static void roundtrip(const CettaBinaryField *fields, size_t count) {
    uint8_t *encoded = NULL;
    size_t size = 0, measured = 0, bits = 0;
    CHECK(cetta_binary_measure(fields, count, &bits, &measured) == CETTA_BINARY_OK);
    CHECK(bits <= measured * 8 && measured * 8 < bits + 8);
    CHECK(cetta_binary_pack(fields, count, NULL, NULL, &encoded, &size) == CETTA_BINARY_OK);
    CHECK(size == measured);
    CettaBinaryOwnedField *decoded = NULL;
    size_t decoded_count = 0;
    CHECK(cetta_binary_unpack(encoded, size, NULL, NULL, &decoded, &decoded_count) == CETTA_BINARY_OK);
    CHECK(decoded_count == count);
    for (size_t i = 0; i < count; i++) {
        CHECK(decoded[i].size == fields[i].size);
        CHECK(!fields[i].size || !memcmp(decoded[i].data, fields[i].data, fields[i].size));
    }
    cetta_binary_fields_free(decoded, decoded_count);
    canonical(encoded, size);
    if (size) {
        CHECK(cetta_binary_unpack(encoded, size - 1, NULL, NULL, &decoded, &decoded_count) != CETTA_BINARY_OK);
        CHECK(decoded == NULL && decoded_count == 0);
    }
    free(encoded);
}

int main(void) {
    uint8_t values[513];
    for (size_t i = 0; i < sizeof(values); i++) values[i] = (uint8_t)(i * 137u);
    roundtrip(NULL, 0);
    CettaBinaryField fields[5] = {{NULL, 0}, {values, 1}, {values, 17}, {values, 256}, {values, 513}};
    for (size_t n = 1; n <= 5; n++) roundtrip(fields, n);
    uint8_t *data = NULL;
    size_t size = 0;
    CHECK(cetta_binary_pack(NULL, 0, NULL, NULL, &data, &size) == CETTA_BINARY_OK);
    CHECK(size == 1 && data[0] == 0);
    free(data);
    CHECK(cetta_binary_pack(fields, 1, NULL, NULL, &data, &size) == CETTA_BINARY_OK);
    CHECK(size == 1 && data[0] == 0xb0);
    free(data);

    size_t bits, bytes;
    CettaBinaryField enormous = {NULL, SIZE_MAX};
    CHECK(cetta_binary_measure(&enormous, 1, &bits, &bytes) == CETTA_BINARY_OVERFLOW);
    CHECK(bits == 0 && bytes == 0);
    CHECK(cetta_binary_pack(fields, 2, cancel, NULL, &data, &size) == CETTA_BINARY_INTERRUPTED);
    CHECK(data == NULL && size == 0);
    CHECK(cetta_binary_pack(fields, 2, NULL, NULL, &data, &size) == CETTA_BINARY_OK);
    CettaBinaryOwnedField *decoded = NULL;
    size_t count = 0;
    CHECK(cetta_binary_unpack(data, size, cancel, NULL, &decoded, &count) == CETTA_BINARY_INTERRUPTED);
    CHECK(decoded == NULL && count == 0);
    free(data);

    const uint8_t noncanonical[] = {0x80};
    const uint8_t zero_count_digit[] = {0xa0};
    const uint8_t padding[] = {0x01};
    const uint8_t trailing[] = {0x00, 0x00};
    const uint8_t unaligned[] = {0xbb};
    CHECK(cetta_binary_unpack(noncanonical, 1, NULL, NULL, &decoded, &count) == CETTA_BINARY_NONCANONICAL);
    CHECK(cetta_binary_unpack(zero_count_digit, 1, NULL, NULL, &decoded, &count) == CETTA_BINARY_NONCANONICAL);
    CHECK(cetta_binary_unpack(padding, 1, NULL, NULL, &decoded, &count) == CETTA_BINARY_TRAILING);
    CHECK(cetta_binary_unpack(trailing, 2, NULL, NULL, &decoded, &count) == CETTA_BINARY_TRAILING);
    CHECK(cetta_binary_unpack(unaligned, 1, NULL, NULL, &decoded, &count) == CETTA_BINARY_ALIGNMENT);
    CHECK(cetta_binary_pack(NULL, 0, cancel, NULL, &data, &size) == CETTA_BINARY_INTERRUPTED);
#ifdef CETTA_BINARY_TEST_HOOKS
    cetta_binary_test_fail_alloc_after(0);
    CHECK(cetta_binary_pack(fields, 5, NULL, NULL, &data, &size) == CETTA_BINARY_NOMEM);
    CHECK(data == NULL && size == 0);
    cetta_binary_test_fail_alloc_after(-1);
    CHECK(cetta_binary_pack(fields, 5, NULL, NULL, &data, &size) == CETTA_BINARY_OK);
    for (long fail_after = 0; fail_after < 5; fail_after++) {
        cetta_binary_test_fail_alloc_after(fail_after);
        CHECK(cetta_binary_unpack(data, size, NULL, NULL, &decoded, &count) == CETTA_BINARY_NOMEM);
        CHECK(decoded == NULL && count == 0);
    }
    cetta_binary_test_fail_alloc_after(-1);
    CHECK(cetta_binary_unpack(data, size, NULL, NULL, &decoded, &count) == CETTA_BINARY_OK);
    cetta_binary_fields_free(decoded, count);
    free(data);
#endif
    for (unsigned i = 0; i < 256; i++) { uint8_t input = (uint8_t)i; canonical(&input, 1); }
    uint32_t seed = 19;
    for (size_t n = 0; n < 1024; n++) {
        uint8_t input[16];
        for (size_t i = 0; i < sizeof(input); i++) {
            seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
            input[i] = (uint8_t)(seed >> 24);
        }
        canonical(input, 1 + n % sizeof(input));
    }
    printf("binary codec: %zu checks passed\n", checks);
    return 0;
}
