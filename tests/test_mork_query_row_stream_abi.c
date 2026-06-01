#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mork_space_bridge_runtime.h"

enum {
    CTBR_MAGIC = 0x43544252u,
    QUERY_ONLY_V2_VERSION = 5u,
    QUERY_ONLY_V2_FLAGS = 0x0013u,
    MULTI_REF_V3_VERSION = 6u,
    MULTI_REF_V3_FLAGS = 0x001bu,
};

static void fail_bridge(const char *ctx) {
    fprintf(stderr, "error: %s: %s\n", ctx, cetta_mork_bridge_last_error());
    assert(0);
}

static uint32_t read_u32_be(const uint8_t *data, size_t offset) {
    return ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset + 1] << 16) |
           ((uint32_t)data[offset + 2] << 8) |
           (uint32_t)data[offset + 3];
}

static uint16_t read_u16_be(const uint8_t *data, size_t offset) {
    return (uint16_t)(((uint16_t)data[offset] << 8) |
                      (uint16_t)data[offset + 1]);
}

static uint64_t read_u64_be(const uint8_t *data, size_t offset) {
    return ((uint64_t)data[offset] << 56) |
           ((uint64_t)data[offset + 1] << 48) |
           ((uint64_t)data[offset + 2] << 40) |
           ((uint64_t)data[offset + 3] << 32) |
           ((uint64_t)data[offset + 4] << 24) |
           ((uint64_t)data[offset + 5] << 16) |
           ((uint64_t)data[offset + 6] << 8) |
           (uint64_t)data[offset + 7];
}

static size_t count_query_only_rows(const uint8_t *packet, size_t len,
                                    uint64_t expected_rows) {
    size_t off = 16;
    size_t rows = 0;
    assert(len >= 16);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == QUERY_ONLY_V2_VERSION);
    assert(read_u16_be(packet, 6) == QUERY_ONLY_V2_FLAGS);
    assert(read_u64_be(packet, 8) == expected_rows);
    while (off < len) {
        uint32_t ref_count = 0;
        uint32_t binding_count = 0;
        assert(off + 4u <= len);
        ref_count = read_u32_be(packet, off);
        off += 4u + (size_t)ref_count * 4u;
        assert(off + 4u <= len);
        binding_count = read_u32_be(packet, off);
        off += 4u;
        for (uint32_t i = 0; i < binding_count; i++) {
            uint32_t expr_len = 0;
            assert(off + 8u <= len);
            off += 4u;
            expr_len = read_u32_be(packet, off);
            off += 4u;
            assert(off + expr_len <= len);
            off += expr_len;
        }
        rows++;
    }
    return rows;
}

static size_t count_multi_ref_rows(const uint8_t *packet, size_t len,
                                   uint64_t expected_rows,
                                   uint32_t expected_factors) {
    size_t off = 20;
    size_t rows = 0;
    assert(len >= 20);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == MULTI_REF_V3_VERSION);
    assert(read_u16_be(packet, 6) == MULTI_REF_V3_FLAGS);
    assert(read_u32_be(packet, 8) == expected_factors);
    assert(read_u64_be(packet, 12) == expected_rows);
    while (off < len) {
        uint32_t binding_count = 0;
        for (uint32_t i = 0; i < expected_factors; i++) {
            assert(off + 4u <= len);
            assert(read_u32_be(packet, off) > 0);
            off += 4u;
        }
        assert(off + 4u <= len);
        binding_count = read_u32_be(packet, off);
        off += 4u;
        for (uint32_t i = 0; i < binding_count; i++) {
            uint32_t expr_len = 0;
            assert(off + 8u <= len);
            off += 4u;
            expr_len = read_u32_be(packet, off);
            off += 4u;
            assert(off + expr_len <= len);
            off += expr_len;
        }
        rows++;
    }
    return rows;
}

static void release_packet(uint8_t *packet, size_t len) {
    if (packet)
        cetta_mork_bridge_bytes_free(packet, len);
}

static void expect_batch(CettaMorkQueryCursorHandle *cursor,
                         uint64_t expected_rows,
                         bool expect_packet) {
    uint8_t *packet = NULL;
    size_t len = 0;
    uint64_t rows = 0;

    if (!cetta_mork_bridge_query_cursor_next(
            cursor, 2, 4096, &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_query_cursor_next");

    assert(rows == expected_rows);
    if (expect_packet) {
        assert(packet != NULL);
        assert(len >= 16);
        assert(count_query_only_rows(packet, len, expected_rows) ==
               (size_t)expected_rows);
    } else {
        assert(packet == NULL);
        assert(len == 0);
    }
    release_packet(packet, len);
}

static void expect_multi_ref_batch(CettaMorkQueryCursorHandle *cursor,
                                   uint64_t expected_rows,
                                   bool expect_packet) {
    uint8_t *packet = NULL;
    size_t len = 0;
    uint64_t rows = 0;

    if (!cetta_mork_bridge_query_cursor_next(
            cursor, 2, 4096, &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_query_cursor_next multi-ref");

    assert(rows == expected_rows);
    if (expect_packet) {
        assert(packet != NULL);
        assert(len >= 20);
        assert(count_multi_ref_rows(packet, len, expected_rows, 2) ==
               (size_t)expected_rows);
    } else {
        assert(packet == NULL);
        assert(len == 0);
    }
    release_packet(packet, len);
}

int main(void) {
    static const char facts[] =
        "(edge a b)\n"
        "(edge a c)\n"
        "(edge b d)\n"
        "(edge c d)\n"
        "(edge d e)";
    static const char query[] = "(edge $x $y)";
    static const char conj_query[] = "(, (edge $x $y) (edge $y $z))";
    CettaMorkSpaceHandle *space = NULL;
    CettaMorkQueryCursorHandle *cursor = NULL;
    uint64_t added = 0;

    space = cetta_mork_bridge_space_new_pathmap();
    assert(space != NULL);
    if (!cetta_mork_bridge_space_add_sexpr(
            space, (const uint8_t *)facts, strlen(facts), &added))
        fail_bridge("cetta_mork_bridge_space_add_sexpr");
    assert(added == 5);

    if (!cetta_mork_bridge_query_cursor_new_query_only_v2(
            space, (const uint8_t *)query, strlen(query), &cursor))
        fail_bridge("cetta_mork_bridge_query_cursor_new_query_only_v2");
    assert(cursor != NULL);

    expect_batch(cursor, 2, true);
    expect_batch(cursor, 2, true);
    expect_batch(cursor, 1, true);
    expect_batch(cursor, 0, false);

    cetta_mork_bridge_query_cursor_free(cursor);
    cursor = NULL;

    if (!cetta_mork_bridge_query_cursor_new_multi_ref_v3(
            space, (const uint8_t *)conj_query, strlen(conj_query), &cursor))
        fail_bridge("cetta_mork_bridge_query_cursor_new_multi_ref_v3");
    assert(cursor != NULL);

    expect_multi_ref_batch(cursor, 2, true);
    expect_multi_ref_batch(cursor, 2, true);
    expect_multi_ref_batch(cursor, 0, false);

    cetta_mork_bridge_query_cursor_free(cursor);
    cetta_mork_bridge_space_free(space);
    printf("PASS: MORK query cursor streams bounded query-only and multi-ref row batches\n");
    return 0;
}
