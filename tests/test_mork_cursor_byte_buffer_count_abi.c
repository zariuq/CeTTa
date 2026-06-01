#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rust/cetta-space-bridge/include/mork_space_bridge.h"

_Static_assert(sizeof(((MorkBuffer *)0)->count) == sizeof(uint64_t),
               "MorkBuffer.count is the row-count ABI field and must be u64");

typedef struct MorkCursor MorkCursor;
typedef struct MorkProductCursor MorkProductCursor;
typedef struct MorkOverlayCursor MorkOverlayCursor;

MorkCursor *mork_cursor_new(const MorkSpace *space);
void mork_cursor_free(MorkCursor *cursor);
MorkStatus mork_cursor_child_count(const MorkCursor *cursor);
MorkBuffer mork_cursor_path_bytes(const MorkCursor *cursor);
MorkBuffer mork_cursor_child_bytes(const MorkCursor *cursor);
MorkStatus mork_cursor_depth(const MorkCursor *cursor);
MorkStatus mork_cursor_descend_until(MorkCursor *cursor);

MorkProductCursor *mork_product_cursor_new(const MorkSpace *const *spaces,
                                           size_t count);
void mork_product_cursor_free(MorkProductCursor *cursor);
MorkStatus mork_product_cursor_child_count(const MorkProductCursor *cursor);
MorkBuffer mork_product_cursor_path_bytes(const MorkProductCursor *cursor);
MorkBuffer mork_product_cursor_child_bytes(const MorkProductCursor *cursor);
MorkStatus mork_product_cursor_depth(const MorkProductCursor *cursor);
MorkStatus mork_product_cursor_descend_until(MorkProductCursor *cursor);

MorkOverlayCursor *mork_overlay_cursor_new(const MorkSpace *base,
                                           const MorkSpace *overlay);
void mork_overlay_cursor_free(MorkOverlayCursor *cursor);
MorkStatus mork_overlay_cursor_child_count(const MorkOverlayCursor *cursor);
MorkBuffer mork_overlay_cursor_path_bytes(const MorkOverlayCursor *cursor);
MorkBuffer mork_overlay_cursor_child_bytes(const MorkOverlayCursor *cursor);
MorkStatus mork_overlay_cursor_depth(const MorkOverlayCursor *cursor);
MorkStatus mork_overlay_cursor_descend_until(MorkOverlayCursor *cursor);

static void release_status(MorkStatus status) {
    if (status.message)
        mork_bytes_free(status.message, status.message_len);
}

static void release_buffer(MorkBuffer buffer) {
    if (buffer.data)
        mork_bytes_free(buffer.data, buffer.len);
    if (buffer.message)
        mork_bytes_free(buffer.message, buffer.message_len);
}

static void fail_status(const char *ctx, MorkStatus status) {
    fprintf(stderr, "error: %s: status code %" PRId32, ctx, status.code);
    if (status.message && status.message_len > 0)
        fprintf(stderr, ": %.*s", (int)status.message_len,
                (const char *)status.message);
    fputc('\n', stderr);
    release_status(status);
    exit(1);
}

static uint64_t status_value(const char *ctx, MorkStatus status) {
    uint64_t value = 0;
    if (status.code != MORK_STATUS_OK)
        fail_status(ctx, status);
    value = status.value;
    release_status(status);
    return value;
}

static void require_buffer_count_zero(const char *ctx, MorkBuffer buffer,
                                      size_t expected_len) {
    if (buffer.code != MORK_STATUS_OK) {
        fprintf(stderr, "error: %s: buffer status code %" PRId32, ctx, buffer.code);
        if (buffer.message && buffer.message_len > 0)
            fprintf(stderr, ": %.*s", (int)buffer.message_len,
                    (const char *)buffer.message);
        fputc('\n', stderr);
        release_buffer(buffer);
        exit(1);
    }
    if (buffer.len != expected_len) {
        fprintf(stderr, "error: %s: expected len %zu, got %zu\n",
                ctx, expected_len, buffer.len);
        release_buffer(buffer);
        exit(1);
    }
    if (expected_len > 0 && !buffer.data) {
        fprintf(stderr, "error: %s: non-empty buffer returned null data\n", ctx);
        release_buffer(buffer);
        exit(1);
    }
    if (buffer.count != 0) {
        fprintf(stderr,
                "error: %s: cursor byte buffer count must be 0, got %" PRIu64 "\n",
                ctx, buffer.count);
        release_buffer(buffer);
        exit(1);
    }
    release_buffer(buffer);
}

static MorkSpace *new_edge_space(const char *ctx) {
    static const char facts[] = "(edge a b)\n(edge a c)";
    MorkSpace *space = mork_space_new();
    if (!space) {
        fprintf(stderr, "error: %s: mork_space_new returned null\n", ctx);
        exit(1);
    }
    uint64_t added = status_value(ctx, mork_space_add_sexpr(
                                           space,
                                           (const uint8_t *)facts,
                                           strlen(facts)));
    if (added != 2) {
        fprintf(stderr, "error: %s: expected 2 added rows, got %" PRIu64 "\n",
                ctx, added);
        mork_space_free(space);
        exit(1);
    }
    return space;
}

static void require_descended(const char *ctx, uint64_t moved) {
    if (moved != 1) {
        fprintf(stderr, "error: %s: descend_until did not move\n", ctx);
        exit(1);
    }
}

static void test_single_cursor_buffers(void) {
    MorkSpace *space = new_edge_space("single cursor add");
    MorkCursor *cursor = mork_cursor_new(space);
    if (!cursor) {
        fprintf(stderr, "error: single cursor: mork_cursor_new returned null\n");
        mork_space_free(space);
        exit(1);
    }

    require_buffer_count_zero("mork_cursor_child_bytes",
                              mork_cursor_child_bytes(cursor),
                              (size_t)status_value(
                                  "mork_cursor_child_count",
                                  mork_cursor_child_count(cursor)));

    require_descended("mork_cursor_descend_until",
                      status_value("mork_cursor_descend_until",
                                   mork_cursor_descend_until(cursor)));
    require_buffer_count_zero("mork_cursor_path_bytes",
                              mork_cursor_path_bytes(cursor),
                              (size_t)status_value("mork_cursor_depth",
                                                   mork_cursor_depth(cursor)));

    mork_cursor_free(cursor);
    mork_space_free(space);
}

static void test_product_cursor_buffers(void) {
    MorkSpace *lhs = new_edge_space("product cursor lhs add");
    MorkSpace *rhs = new_edge_space("product cursor rhs add");
    const MorkSpace *spaces[2] = { lhs, rhs };
    MorkProductCursor *cursor = mork_product_cursor_new(spaces, 2);
    if (!cursor) {
        fprintf(stderr, "error: product cursor: mork_product_cursor_new returned null\n");
        mork_space_free(lhs);
        mork_space_free(rhs);
        exit(1);
    }

    require_buffer_count_zero("mork_product_cursor_child_bytes",
                              mork_product_cursor_child_bytes(cursor),
                              (size_t)status_value(
                                  "mork_product_cursor_child_count",
                                  mork_product_cursor_child_count(cursor)));

    require_descended("mork_product_cursor_descend_until",
                      status_value("mork_product_cursor_descend_until",
                                   mork_product_cursor_descend_until(cursor)));
    require_buffer_count_zero("mork_product_cursor_path_bytes",
                              mork_product_cursor_path_bytes(cursor),
                              (size_t)status_value(
                                  "mork_product_cursor_depth",
                                  mork_product_cursor_depth(cursor)));

    mork_product_cursor_free(cursor);
    mork_space_free(lhs);
    mork_space_free(rhs);
}

static void test_overlay_cursor_buffers(void) {
    MorkSpace *base = new_edge_space("overlay cursor base add");
    MorkSpace *overlay = new_edge_space("overlay cursor overlay add");
    MorkOverlayCursor *cursor = mork_overlay_cursor_new(base, overlay);
    if (!cursor) {
        fprintf(stderr, "error: overlay cursor: mork_overlay_cursor_new returned null\n");
        mork_space_free(base);
        mork_space_free(overlay);
        exit(1);
    }

    require_buffer_count_zero("mork_overlay_cursor_child_bytes",
                              mork_overlay_cursor_child_bytes(cursor),
                              (size_t)status_value(
                                  "mork_overlay_cursor_child_count",
                                  mork_overlay_cursor_child_count(cursor)));

    require_descended("mork_overlay_cursor_descend_until",
                      status_value("mork_overlay_cursor_descend_until",
                                   mork_overlay_cursor_descend_until(cursor)));
    require_buffer_count_zero("mork_overlay_cursor_path_bytes",
                              mork_overlay_cursor_path_bytes(cursor),
                              (size_t)status_value(
                                  "mork_overlay_cursor_depth",
                                  mork_overlay_cursor_depth(cursor)));

    mork_overlay_cursor_free(cursor);
    mork_space_free(base);
    mork_space_free(overlay);
}

int main(void) {
    test_single_cursor_buffers();
    test_product_cursor_buffers();
    test_overlay_cursor_buffers();
    puts("PASS: raw MORK cursor byte buffers keep len as bytes and count as zero");
    return 0;
}
