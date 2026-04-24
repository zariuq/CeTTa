#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mork_space_bridge_runtime.h"

enum {
    CTBR_MAGIC = 0x43544252u,
    CTBR_VERSION = 4u,
    CTBR_EXACT_ROWS_FLAGS = 0u,
    CTBR_QUERY_ROWS_FLAGS = 0u,
    CTBR_OPEN_REF_EXACT = 0u,
    CTBR_OPEN_REF_QUERY_SLOT = 1u,
    BRIDGE_EXPR_TAG_ARITY = 0x00u,
    BRIDGE_EXPR_TAG_NEWVAR = 0x02u,
};

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

static void fail_bridge(const char *ctx) {
    fprintf(stderr, "error: %s: %s\n", ctx, cetta_mork_bridge_last_error());
    assert(0);
}

static bool error_contains(const char *needle) {
    const char *err = cetta_mork_bridge_last_error();
    return err != NULL && strstr(err, needle) != NULL;
}

static void add_text(CettaMorkSpaceHandle *space, const char *text) {
    uint64_t added = 0;
    if (!cetta_mork_bridge_space_add_sexpr(
            space, (const uint8_t *)text, strlen(text), &added)) {
        fail_bridge("cetta_mork_bridge_space_add_sexpr");
    }
    assert(added == 1);
}

static void add_contextual_edge_xx(CettaMorkSpaceHandle *space) {
    uint64_t changed = 0;
    const uint8_t expr[] = {
        0x03,                         /* (edge $slot0 $slot0) */
        0xC4, 'e', 'd', 'g', 'e',
        0xC0,
        0x80,
    };
    const uint8_t context[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x12, 0x34,
        0x00, 0x00, 0x00, 0x02,
        '$', 'x',
    };

    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context, sizeof(context), &changed));
    assert(changed == 1);
}

static void assert_contextual_exact_dump_succeeds(CettaMorkSpaceHandle *space) {
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;

    if (!cetta_mork_bridge_space_dump_contextual_exact_rows(space, &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_dump_contextual_exact_rows");
    assert(packet != NULL);
    assert(len > 0);
    assert(rows == 1);
    cetta_mork_bridge_bytes_free(packet, len);
}

static void assert_contextual_exact_dump_missing_context(CettaMorkSpaceHandle *space) {
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;

    assert(!cetta_mork_bridge_space_dump_contextual_exact_rows(space, &packet, &len, &rows));
    assert(error_contains("opening context"));
    assert(packet == NULL);
    assert(len == 0);
    assert(rows == 0);
}

static void test_ground_counted_exact_rows(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    uint32_t expr_len = 0;

    assert(space != NULL);
    add_text(space, "(dup a)");
    add_text(space, "(dup a)");
    add_text(space, "(dup a)");

    if (!cetta_mork_bridge_space_dump_contextual_exact_rows(space, &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_dump_contextual_exact_rows");

    assert(rows == 1);
    assert(packet != NULL);
    assert(len >= 37);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == CTBR_VERSION);
    assert(read_u16_be(packet, 6) == CTBR_EXACT_ROWS_FLAGS);
    assert(read_u32_be(packet, 8) == 1);
    assert(read_u32_be(packet, 12) == 1);
    assert(read_u32_be(packet, 16) == 0);
    assert(read_u32_be(packet, 20) == 0);
    assert(read_u32_be(packet, 24) == 0);
    assert(read_u32_be(packet, 28) == 3);
    expr_len = read_u32_be(packet, 32);
    assert(len == 36u + (size_t)expr_len);
    assert(packet[36] == BRIDGE_EXPR_TAG_ARITY);

    cetta_mork_bridge_bytes_free(packet, len);
    cetta_mork_bridge_space_free(space);
}

static void test_variable_rows_require_opening_context(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    const char *err = NULL;

    assert(space != NULL);
    add_text(space, "(edge $x $x)");

    assert(!cetta_mork_bridge_space_dump_contextual_exact_rows(space, &packet, &len, &rows));
    err = cetta_mork_bridge_last_error();
    assert(err != NULL);
    assert(strstr(err, "opening context") != NULL);
    assert(packet == NULL);
    assert(len == 0);
    assert(rows == 0);

    cetta_mork_bridge_space_free(space);
}

static void test_variable_rows_with_opening_context(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    size_t off = 0;
    uint32_t expr_len = 0;
    uint64_t added = 0;
    const uint8_t expr[] = {
        0x03,                         /* (edge $x $x) */
        0xC4, 'e', 'd', 'g', 'e',
        0xC0,
        0x80,
    };
    const uint8_t context[] = {
        0x00, 0x00, 0x00, 0x01,       /* one entry */
        0x00, 0x00,                   /* slot 0 */
        CTBR_OPEN_REF_EXACT, 0x00,    /* exact, reserved */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x12, 0x34,       /* VarId */
        0x00, 0x00, 0x00, 0x02,       /* spelling length */
        '$', 'x',
    };

    assert(space != NULL);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context, sizeof(context), &added));
    assert(added == 1);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context, sizeof(context), &added));
    assert(added == 1);

    if (!cetta_mork_bridge_space_dump_contextual_exact_rows(space, &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_dump_contextual_exact_rows");

    assert(rows == 1);
    assert(packet != NULL);
    assert(len >= 70);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == CTBR_VERSION);
    assert(read_u16_be(packet, 6) == CTBR_QUERY_ROWS_FLAGS);
    assert(read_u32_be(packet, 8) == 1);
    assert(read_u32_be(packet, 12) == 1);

    off = 16;
    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(packet[off++] == CTBR_OPEN_REF_EXACT);
    assert(packet[off++] == 0);
    assert(read_u64_be(packet, off) == 0x1234u); off += 8;
    assert(read_u32_be(packet, off) == 2); off += 4;
    assert(packet[off++] == '$');
    assert(packet[off++] == 'x');

    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 2); off += 4;
    expr_len = read_u32_be(packet, off); off += 4;
    assert(off + expr_len == len);
    assert(packet[off] == BRIDGE_EXPR_TAG_ARITY);

    cetta_mork_bridge_bytes_free(packet, len);
    cetta_mork_bridge_space_free(space);
}

static void test_contextual_exact_remove_keeps_requested_identity(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    size_t off = 0;
    uint32_t expr_len = 0;
    uint64_t changed = 0;
    const uint8_t expr[] = {
        0x03,                         /* (edge $slot0 $slot0) */
        0xC4, 'e', 'd', 'g', 'e',
        0xC0,
        0x80,
    };
    const uint8_t context_x[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x12, 0x34,
        0x00, 0x00, 0x00, 0x02,
        '$', 'x',
    };
    const uint8_t context_y[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x56, 0x78,
        0x00, 0x00, 0x00, 0x02,
        '$', 'y',
    };

    assert(space != NULL);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context_x, sizeof(context_x), &changed));
    assert(changed == 1);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context_y, sizeof(context_y), &changed));
    assert(changed == 1);
    assert(cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context_x, sizeof(context_x), &changed));
    assert(changed == 1);
    assert(cetta_mork_bridge_space_remove_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context_x, sizeof(context_x), &changed));
    assert(changed == 0);

    if (!cetta_mork_bridge_space_dump_contextual_exact_rows(space, &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_dump_contextual_exact_rows");

    assert(rows == 1);
    assert(packet != NULL);
    assert(read_u32_be(packet, 8) == 1);
    assert(read_u32_be(packet, 12) == 1);

    off = 16;
    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(packet[off++] == CTBR_OPEN_REF_EXACT);
    assert(packet[off++] == 0);
    assert(read_u64_be(packet, off) == 0x5678u); off += 8;
    assert(read_u32_be(packet, off) == 2); off += 4;
    assert(packet[off++] == '$');
    assert(packet[off++] == 'y');

    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 1); off += 4;
    expr_len = read_u32_be(packet, off); off += 4;
    assert(off + expr_len == len);
    assert(packet[off] == BRIDGE_EXPR_TAG_ARITY);

    cetta_mork_bridge_bytes_free(packet, len);
    cetta_mork_bridge_space_free(space);
}

static void test_contextual_query_rows_open_exact_value_refs(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    size_t off = 0;
    uint32_t expr_len = 0;
    uint64_t changed = 0;
    const char *query = "(edge $x)";
    const uint8_t expr[] = {
        0x02,                         /* (edge $stored) */
        0xC4, 'e', 'd', 'g', 'e',
        0xC0,
    };
    const uint8_t context[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x9A, 0xBC,
        0x00, 0x00, 0x00, 0x02,
        '$', 'v',
    };

    assert(space != NULL);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context, sizeof(context), &changed));
    assert(changed == 1);

    if (!cetta_mork_bridge_space_query_contextual_rows(
            space, (const uint8_t *)query, strlen(query), &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_query_contextual_rows");

    assert(rows == 1);
    assert(packet != NULL);
    assert(len >= 61);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == CTBR_VERSION);
    assert(read_u16_be(packet, 6) == CTBR_QUERY_ROWS_FLAGS);
    assert(read_u32_be(packet, 8) == 1);
    assert(read_u32_be(packet, 12) == 1);

    off = 16;
    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(packet[off++] == CTBR_OPEN_REF_EXACT);
    assert(packet[off++] == 0);
    assert(read_u64_be(packet, off) == 0x9ABCu); off += 8;
    assert(read_u32_be(packet, off) == 2); off += 4;
    assert(packet[off++] == '$');
    assert(packet[off++] == 'v');

    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 0); off += 4;
    expr_len = read_u32_be(packet, off); off += 4;
    assert(expr_len == 1);
    assert(off + expr_len == len);
    assert(packet[off] == BRIDGE_EXPR_TAG_NEWVAR);

    cetta_mork_bridge_bytes_free(packet, len);
    cetta_mork_bridge_space_free(space);
}

static void test_contextual_query_rows_split_exact_presentations(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    size_t off = 0;
    uint32_t expr_len = 0;
    uint64_t changed = 0;
    const char *query = "(edge $x)";
    const uint8_t expr[] = {
        0x02,                         /* (edge $slot0) */
        0xC4, 'e', 'd', 'g', 'e',
        0xC0,
    };
    const uint8_t context_a[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x11, 0x11,
        0x00, 0x00, 0x00, 0x02,
        '$', 'a',
    };
    const uint8_t context_b[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x22, 0x22,
        0x00, 0x00, 0x00, 0x02,
        '$', 'b',
    };

    assert(space != NULL);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context_a, sizeof(context_a), &changed));
    assert(changed == 1);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context_b, sizeof(context_b), &changed));
    assert(changed == 1);

    if (!cetta_mork_bridge_space_query_contextual_rows(
            space, (const uint8_t *)query, strlen(query), &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_query_contextual_rows");

    assert(rows == 2);
    assert(packet != NULL);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == CTBR_VERSION);
    assert(read_u16_be(packet, 6) == CTBR_QUERY_ROWS_FLAGS);
    assert(read_u32_be(packet, 8) == 2);
    assert(read_u32_be(packet, 12) == 2);

    off = 16;
    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(packet[off++] == CTBR_OPEN_REF_EXACT);
    assert(packet[off++] == 0);
    assert(read_u64_be(packet, off) == 0x1111u); off += 8;
    assert(read_u32_be(packet, off) == 2); off += 4;
    assert(packet[off++] == '$');
    assert(packet[off++] == 'a');

    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(packet[off++] == CTBR_OPEN_REF_EXACT);
    assert(packet[off++] == 0);
    assert(read_u64_be(packet, off) == 0x2222u); off += 8;
    assert(read_u32_be(packet, off) == 2); off += 4;
    assert(packet[off++] == '$');
    assert(packet[off++] == 'b');

    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(read_u32_be(packet, off) == 0); off += 4;
    assert(read_u32_be(packet, off) == 0); off += 4;
    expr_len = read_u32_be(packet, off); off += 4;
    assert(expr_len == 1);
    assert(packet[off++] == BRIDGE_EXPR_TAG_NEWVAR);

    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u16_be(packet, off) == 0); off += 2;
    assert(read_u32_be(packet, off) == 1); off += 4;
    assert(read_u32_be(packet, off) == 0); off += 4;
    expr_len = read_u32_be(packet, off); off += 4;
    assert(expr_len == 1);
    assert(packet[off++] == BRIDGE_EXPR_TAG_NEWVAR);
    assert(off == len);

    cetta_mork_bridge_bytes_free(packet, len);
    cetta_mork_bridge_space_free(space);
}

static void test_contextual_query_rows_emit_query_slot_refs(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;
    size_t off = 0;
    bool found_query_ref = false;
    const char *query = "(pair $x (wrap $y))";
    uint64_t changed = 0;
    const uint8_t expr[] = {
        0x03,                         /* (pair $slot0 $slot0) */
        0xC4, 'p', 'a', 'i', 'r',
        0xC0,
        0x80,
    };
    const uint8_t context[] = {
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00,
        CTBR_OPEN_REF_EXACT, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x77, 0x77,
        0x00, 0x00, 0x00, 0x02,
        '$', 'z',
    };

    assert(space != NULL);
    assert(cetta_mork_bridge_space_add_contextual_exact_expr_bytes(
        space, expr, sizeof(expr), context, sizeof(context), &changed));
    assert(changed == 1);

    if (!cetta_mork_bridge_space_query_contextual_rows(
            space, (const uint8_t *)query, strlen(query), &packet, &len, &rows))
        fail_bridge("cetta_mork_bridge_space_query_contextual_rows");

    assert(packet != NULL);
    assert(rows >= 1);
    assert(len >= 16);
    assert(read_u32_be(packet, 0) == CTBR_MAGIC);
    assert(read_u16_be(packet, 4) == CTBR_VERSION);
    assert(read_u16_be(packet, 6) == CTBR_QUERY_ROWS_FLAGS);

    off = 16;
    {
        uint32_t context_count = read_u32_be(packet, 12);
        for (uint32_t ci = 0; ci < context_count; ci++) {
            uint32_t entry_count = 0;
            assert(off + 8 <= len);
            /* context_id */
            (void)read_u32_be(packet, off);
            off += 4;
            entry_count = read_u32_be(packet, off);
            off += 4;
            for (uint32_t ei = 0; ei < entry_count; ei++) {
                uint8_t kind = 0;
                uint8_t reserved = 0;
                assert(off + 4 <= len);
                /* slot */
                (void)read_u16_be(packet, off);
                off += 2;
                kind = packet[off++];
                reserved = packet[off++];
                assert(reserved == 0);
                if (kind == CTBR_OPEN_REF_EXACT) {
                    uint32_t spelling_len = 0;
                    assert(off + 12 <= len);
                    /* var_id */
                    (void)read_u64_be(packet, off);
                    off += 8;
                    spelling_len = read_u32_be(packet, off);
                    off += 4;
                    assert(off + spelling_len <= len);
                    off += spelling_len;
                } else if (kind == CTBR_OPEN_REF_QUERY_SLOT) {
                    assert(off + 2 <= len);
                    (void)read_u16_be(packet, off);
                    off += 2;
                    found_query_ref = true;
                } else {
                    assert(0 && "unknown contextual open-ref kind");
                }
            }
        }
    }

    assert(found_query_ref);
    assert(off <= len);

    cetta_mork_bridge_bytes_free(packet, len);
    cetta_mork_bridge_space_free(space);
}

static void test_clone_preserves_contextual_exact_rows(void) {
    CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *clone = NULL;

    assert(space != NULL);
    add_contextual_edge_xx(space);
    clone = cetta_mork_bridge_space_clone(space);
    assert(clone != NULL);

    assert_contextual_exact_dump_succeeds(space);
    assert_contextual_exact_dump_succeeds(clone);

    cetta_mork_bridge_space_free(clone);
    cetta_mork_bridge_space_free(space);
}

static void test_structural_algebra_invalidates_contextual_exact_rows(void) {
    CettaMorkSpaceHandle *join_lhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *join_rhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *meet_lhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *meet_rhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *sub_lhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *sub_rhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *restrict_lhs = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *restrict_rhs = cetta_mork_bridge_space_new_pathmap();
    uint64_t size = 0;

    assert(join_lhs != NULL);
    assert(join_rhs != NULL);
    assert(meet_lhs != NULL);
    assert(meet_rhs != NULL);
    assert(sub_lhs != NULL);
    assert(sub_rhs != NULL);
    assert(restrict_lhs != NULL);
    assert(restrict_rhs != NULL);

    add_contextual_edge_xx(join_lhs);
    add_text(join_rhs, "(other row)");
    assert_contextual_exact_dump_succeeds(join_lhs);
    assert(cetta_mork_bridge_space_join_into(join_lhs, join_rhs));
    assert(cetta_mork_bridge_space_size(join_lhs, &size));
    assert(size == 2);
    assert_contextual_exact_dump_missing_context(join_lhs);

    add_contextual_edge_xx(meet_lhs);
    add_text(meet_rhs, "(edge $x $x)");
    assert_contextual_exact_dump_succeeds(meet_lhs);
    assert(cetta_mork_bridge_space_meet_into(meet_lhs, meet_rhs));
    assert(cetta_mork_bridge_space_size(meet_lhs, &size));
    assert(size == 1);
    assert_contextual_exact_dump_missing_context(meet_lhs);

    add_contextual_edge_xx(sub_lhs);
    add_text(sub_rhs, "(other row)");
    assert_contextual_exact_dump_succeeds(sub_lhs);
    assert(cetta_mork_bridge_space_subtract_into(sub_lhs, sub_rhs));
    assert(cetta_mork_bridge_space_size(sub_lhs, &size));
    assert(size == 1);
    assert_contextual_exact_dump_missing_context(sub_lhs);

    add_contextual_edge_xx(restrict_lhs);
    add_text(restrict_rhs, "(edge $x $x)");
    assert_contextual_exact_dump_succeeds(restrict_lhs);
    assert(cetta_mork_bridge_space_restrict_into(restrict_lhs, restrict_rhs));
    assert(cetta_mork_bridge_space_size(restrict_lhs, &size));
    assert(size == 1);
    assert_contextual_exact_dump_missing_context(restrict_lhs);

    cetta_mork_bridge_space_free(restrict_rhs);
    cetta_mork_bridge_space_free(restrict_lhs);
    cetta_mork_bridge_space_free(sub_rhs);
    cetta_mork_bridge_space_free(sub_lhs);
    cetta_mork_bridge_space_free(meet_rhs);
    cetta_mork_bridge_space_free(meet_lhs);
    cetta_mork_bridge_space_free(join_rhs);
    cetta_mork_bridge_space_free(join_lhs);
}

static void test_logical_row_transfer_between_raw_and_counted_spaces(void) {
    CettaMorkSpaceHandle *raw_src = cetta_mork_bridge_space_new();
    CettaMorkSpaceHandle *counted_dst = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *counted_src = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *raw_dst = cetta_mork_bridge_space_new();
    CettaMorkSpaceHandle *context_src = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *context_dst = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *context_raw_dst = cetta_mork_bridge_space_new();
    CettaMorkSpaceHandle *raw_self = cetta_mork_bridge_space_new();
    CettaMorkSpaceHandle *counted_self = cetta_mork_bridge_space_new_pathmap();
    CettaMorkSpaceHandle *context_self = cetta_mork_bridge_space_new_pathmap();
    uint64_t changed = 0;
    uint64_t size = 0;
    uint8_t *packet = NULL;
    size_t len = 0;
    uint32_t rows = 0;

    assert(raw_src != NULL);
    assert(counted_dst != NULL);
    assert(counted_src != NULL);
    assert(raw_dst != NULL);
    assert(context_src != NULL);
    assert(context_dst != NULL);
    assert(context_raw_dst != NULL);
    assert(raw_self != NULL);
    assert(counted_self != NULL);
    assert(context_self != NULL);

    add_text(raw_src, "(raw edge)");
    add_text(raw_src, "(raw node)");
    assert(cetta_mork_bridge_space_add_logical_rows_from(counted_dst, raw_src, &changed));
    assert(changed == 2);
    assert(cetta_mork_bridge_space_size(counted_dst, &size));
    assert(size == 2);
    if (!cetta_mork_bridge_space_dump_contextual_exact_rows(
            counted_dst, &packet, &len, &rows)) {
        fail_bridge("raw-to-counted contextual exact dump");
    }
    assert(rows == 2);
    cetta_mork_bridge_bytes_free(packet, len);

    add_text(counted_src, "(dup item)");
    add_text(counted_src, "(dup item)");
    add_text(counted_src, "(other item)");
    assert(cetta_mork_bridge_space_add_logical_rows_from(raw_dst, counted_src, &changed));
    assert(changed == 3);
    assert(cetta_mork_bridge_space_size(raw_dst, &size));
    assert(size == 2);
    if (!cetta_mork_bridge_space_dump_expr_rows(raw_dst, &packet, &len, &rows))
        fail_bridge("counted-to-raw expr row dump");
    assert(rows == 2);
    cetta_mork_bridge_bytes_free(packet, len);

    add_contextual_edge_xx(context_src);
    assert(cetta_mork_bridge_space_add_logical_rows_from(
        context_dst, context_src, &changed));
    assert(changed == 1);
    assert_contextual_exact_dump_succeeds(context_dst);

    assert(cetta_mork_bridge_space_add_logical_rows_from(
        context_raw_dst, context_src, &changed));
    assert(changed == 1);
    assert(cetta_mork_bridge_space_size(context_raw_dst, &size));
    assert(size == 1);
    assert(!cetta_mork_bridge_space_dump_contextual_exact_rows(
        context_raw_dst, &packet, &len, &rows));
    assert(error_contains("opening context"));

    add_text(raw_self, "(raw self a)");
    add_text(raw_self, "(raw self b)");
    assert(cetta_mork_bridge_space_add_logical_rows_from(raw_self, raw_self, &changed));
    assert(changed == 2);
    assert(cetta_mork_bridge_space_size(raw_self, &size));
    assert(size == 2);

    add_text(counted_self, "(counted self)");
    add_text(counted_self, "(counted self)");
    assert(cetta_mork_bridge_space_add_logical_rows_from(
        counted_self, counted_self, &changed));
    assert(changed == 2);
    assert(cetta_mork_bridge_space_size(counted_self, &size));
    assert(size == 4);

    add_contextual_edge_xx(context_self);
    assert(cetta_mork_bridge_space_add_logical_rows_from(
        context_self, context_self, &changed));
    assert(changed == 1);
    assert(cetta_mork_bridge_space_size(context_self, &size));
    assert(size == 2);
    assert_contextual_exact_dump_succeeds(context_self);

    cetta_mork_bridge_space_free(context_self);
    cetta_mork_bridge_space_free(counted_self);
    cetta_mork_bridge_space_free(raw_self);
    cetta_mork_bridge_space_free(context_raw_dst);
    cetta_mork_bridge_space_free(context_dst);
    cetta_mork_bridge_space_free(context_src);
    cetta_mork_bridge_space_free(raw_dst);
    cetta_mork_bridge_space_free(counted_src);
    cetta_mork_bridge_space_free(counted_dst);
    cetta_mork_bridge_space_free(raw_src);
}

int main(void) {
    test_ground_counted_exact_rows();
    test_variable_rows_require_opening_context();
    test_variable_rows_with_opening_context();
    test_contextual_exact_remove_keeps_requested_identity();
    test_contextual_query_rows_open_exact_value_refs();
    test_contextual_query_rows_split_exact_presentations();
    test_contextual_query_rows_emit_query_slot_refs();
    test_clone_preserves_contextual_exact_rows();
    test_structural_algebra_invalidates_contextual_exact_rows();
    test_logical_row_transfer_between_raw_and_counted_spaces();
    printf("PASS: mork bridge contextual exact rows\n");
    return 0;
}
