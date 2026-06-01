#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mork_space_bridge_runtime.h"

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

static size_t count_expr_rows(const uint8_t *packet, size_t len) {
    size_t off = 0;
    size_t rows = 0;
    while (off < len) {
        uint32_t expr_len = 0;
        assert(off + 4u <= len);
        expr_len = read_u32_be(packet, off);
        off += 4u;
        assert(off + expr_len <= len);
        off += expr_len;
        rows++;
    }
    return rows;
}

static void release_packet(uint8_t *packet, size_t len) {
    if (packet)
        cetta_mork_bridge_bytes_free(packet, len);
}

static void expect_batch(CettaMorkCursorHandle *cursor,
                         uint64_t max_rows,
                         uint64_t expected_rows,
                         bool expect_data) {
    uint8_t *packet = NULL;
    size_t len = 0;
    uint64_t rows = 0;
    bool ok = cetta_mork_bridge_cursor_next_expr_rows(
        cursor, max_rows, 4096, &packet, &len, &rows);
    if (!ok)
        fail_bridge("cetta_mork_bridge_cursor_next_expr_rows");

    assert(rows == expected_rows);
    assert(count_expr_rows(packet, len) == (size_t)expected_rows);
    if (expect_data) {
        assert(packet != NULL);
        assert(len > 0);
    } else {
        assert(packet == NULL);
        assert(len == 0);
    }
    release_packet(packet, len);
}

static void exercise_cursor(CettaMorkSpaceHandle *space) {
    CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
    assert(cursor != NULL);

    expect_batch(cursor, 2, 2, true);
    expect_batch(cursor, 2, 2, true);
    expect_batch(cursor, 2, 1, true);
    expect_batch(cursor, 2, 0, false);

    cetta_mork_bridge_cursor_free(cursor);
}

static void expect_dump_packet(CettaMorkSpaceHandle *space,
                               uint8_t **out_packet,
                               size_t *out_len,
                               uint64_t *out_rows) {
    bool ok = cetta_mork_bridge_space_dump_expr_rows(
        space, out_packet, out_len, out_rows);
    if (!ok)
        fail_bridge("cetta_mork_bridge_space_dump_expr_rows");
}

static void expect_cursor_packet(CettaMorkSpaceHandle *space,
                                 uint64_t max_rows,
                                 uint8_t **out_packet,
                                 size_t *out_len,
                                 uint64_t *out_rows) {
    CettaMorkCursorHandle *cursor = cetta_mork_bridge_cursor_new(space);
    uint8_t *packet = NULL;
    size_t len = 0;
    size_t cap = 0;
    uint64_t rows = 0;

    assert(cursor != NULL);
    for (;;) {
        uint8_t *batch = NULL;
        size_t batch_len = 0;
        uint64_t batch_rows = 0;
        bool ok = cetta_mork_bridge_cursor_next_expr_rows(
            cursor, max_rows, 4096, &batch, &batch_len, &batch_rows);
        if (!ok)
            fail_bridge("cetta_mork_bridge_cursor_next_expr_rows");
        assert(count_expr_rows(batch, batch_len) == (size_t)batch_rows);
        if (batch_rows == 0) {
            assert(batch == NULL);
            assert(batch_len == 0);
            break;
        }
        if (len + batch_len > cap) {
            size_t next = cap ? cap * 2u : 64u;
            while (next < len + batch_len)
                next *= 2u;
            packet = realloc(packet, next);
            assert(packet != NULL);
            cap = next;
        }
        memcpy(packet + len, batch, batch_len);
        len += batch_len;
        rows += batch_rows;
        release_packet(batch, batch_len);
    }

    cetta_mork_bridge_cursor_free(cursor);
    *out_packet = packet;
    *out_len = len;
    *out_rows = rows;
}

static void exercise_mixed_raw_cursor_regression(void) {
    static const char mixed_facts[] =
        "(edge a b)\n"
        "(nest (pair a (pair b c)) (text \"x y\"))\n"
        "(FList (FSDepth 0) (Cons (\"formula\" \"x\") Nil))\n"
        "\"solo\"";
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
    uint8_t *dump_packet = NULL;
    uint8_t *cursor_packet = NULL;
    size_t dump_len = 0;
    size_t cursor_len = 0;
    uint64_t dump_rows = 0;
    uint64_t cursor_rows = 0;
    uint64_t added = 0;

    assert(space != NULL);
    if (!cetta_mork_bridge_space_add_sexpr(
            space, (const uint8_t *)mixed_facts, strlen(mixed_facts), &added))
        fail_bridge("cetta_mork_bridge_space_add_sexpr mixed");
    assert(added == 4);

    expect_dump_packet(space, &dump_packet, &dump_len, &dump_rows);
    expect_cursor_packet(space, 1, &cursor_packet, &cursor_len, &cursor_rows);
    assert(dump_rows == 4);
    assert(cursor_rows == dump_rows);
    assert(cursor_len == dump_len);
    assert(memcmp(cursor_packet, dump_packet, dump_len) == 0);

    free(cursor_packet);
    release_packet(dump_packet, dump_len);
    cetta_mork_bridge_space_free(space);
}

int main(void) {
    static const char facts[] =
        "(edge a b)\n"
        "(edge a c)\n"
        "(edge b d)\n"
        "(edge c d)\n"
        "(edge d e)";
    CettaMorkSpaceHandle *space = NULL;
    uint64_t added = 0;

    space = cetta_mork_bridge_space_new();
    assert(space != NULL);
    if (!cetta_mork_bridge_space_add_sexpr(
            space, (const uint8_t *)facts, strlen(facts), &added))
        fail_bridge("cetta_mork_bridge_space_add_sexpr");
    assert(added == 5);
    exercise_cursor(space);
    cetta_mork_bridge_space_free(space);

    space = cetta_mork_bridge_space_new_pathmap();
    assert(space != NULL);
    if (!cetta_mork_bridge_space_add_sexpr(
            space, (const uint8_t *)facts, strlen(facts), &added))
        fail_bridge("cetta_mork_bridge_space_add_sexpr");
    assert(added == 5);
    exercise_cursor(space);
    cetta_mork_bridge_space_free(space);

    exercise_mixed_raw_cursor_regression();
    printf("PASS: MORK cursor expr-row streaming returns bounded row batches\n");
    return 0;
}
