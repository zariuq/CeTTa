#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mork_space_bridge_runtime.h"

enum {
    CTBR_MAGIC = 0x43544252u,
    CTBR_VERSION = 4u,
    CTBR_EXACT_ROWS_FLAGS = 0u,
    BRIDGE_EXPR_TAG_ARITY = 0x00u,
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

static void fail_bridge(const char *ctx) {
    fprintf(stderr, "error: %s: %s\n", ctx, cetta_mork_bridge_last_error());
    assert(0);
}

static void add_text(CettaMorkSpaceHandle *space, const char *text) {
    uint64_t added = 0;
    if (!cetta_mork_bridge_space_add_sexpr(
            space, (const uint8_t *)text, strlen(text), &added)) {
        fail_bridge("cetta_mork_bridge_space_add_sexpr");
    }
    assert(added == 1);
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

int main(void) {
    test_ground_counted_exact_rows();
    test_variable_rows_require_opening_context();
    printf("PASS: mork bridge contextual exact rows\n");
    return 0;
}
