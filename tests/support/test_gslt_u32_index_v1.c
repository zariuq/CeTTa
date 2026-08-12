#include "../../src/gslt_u32_index_v1.h"

#include <stdint.h>
#include <stdio.h>

static uint32_t checks;
static uint32_t failures;

static void expect_true(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    CettaGsltU32IndexV1 index;
    uint32_t value = 0u;
    uint32_t key;
    uint32_t retained_cap;
    uint32_t *retained_keys;

    cetta_gslt_u32_index_init_v1(&index);
    expect_true(cetta_gslt_u32_index_validate_v1(&index),
                "empty index validates");
    expect_true(!cetta_gslt_u32_index_find_v1(&index, 4u, &value),
                "empty lookup is absent");
    expect_true(!cetta_gslt_u32_index_find_v1(NULL, 4u, &value),
                "null index lookup fails closed");
    expect_true(!cetta_gslt_u32_index_find_v1(&index, 4u, NULL),
                "null output lookup fails closed");
    expect_true(cetta_gslt_u32_index_insert_unique_v1(&index, 41u, 3u) ==
                    CETTA_GSLT_U32_INDEX_INSERTED_V1,
                "first rule-like identity inserts");
    expect_true(cetta_gslt_u32_index_find_v1(&index, 41u, &value) &&
                    value == 3u,
                "rule-like identity resolves exactly");
    expect_true(cetta_gslt_u32_index_insert_unique_v1(&index, 41u, 99u) ==
                    CETTA_GSLT_U32_INDEX_DUPLICATE_V1,
                "duplicate identity is rejected");
    expect_true(cetta_gslt_u32_index_find_v1(&index, 41u, &value) &&
                    value == 3u && index.len == 1u,
                "duplicate rejection preserves the prior binding");

    expect_true(cetta_gslt_u32_index_insert_unique_v1(
                    &index, UINT32_MAX, UINT32_MAX) ==
                    CETTA_GSLT_U32_INDEX_INSERTED_V1,
                "maximum key and value remain representable");
    expect_true(cetta_gslt_u32_index_insert_unique_v1(&index, 0u, 0u) ==
                    CETTA_GSLT_U32_INDEX_INSERTED_V1,
                "zero key and value remain representable");
    expect_true(cetta_gslt_u32_index_find_v1(
                    &index, UINT32_MAX, &value) && value == UINT32_MAX,
                "maximum entry resolves exactly");
    expect_true(cetta_gslt_u32_index_find_v1(&index, 0u, &value) &&
                    value == 0u,
                "zero entry resolves exactly");

    for (key = 1u; key <= 200u; key++) {
        if (key == 41u)
            continue;
        expect_true(cetta_gslt_u32_index_insert_unique_v1(
                        &index, key, key * 7u) ==
                        CETTA_GSLT_U32_INDEX_INSERTED_V1,
                    "grammar-like inventory insertion succeeds");
    }
    expect_true(index.cap > 16u && index.len == 202u,
                "growing inventory rehashes without losing entries");
    for (key = 1u; key <= 200u; key++) {
        uint32_t expected = key == 41u ? 3u : key * 7u;
        expect_true(cetta_gslt_u32_index_find_v1(&index, key, &value) &&
                        value == expected,
                    "growing inventory lookup remains exact");
    }
    expect_true(!cetta_gslt_u32_index_find_v1(&index, 500u, &value),
                "missing identity stays absent");
    expect_true(cetta_gslt_u32_index_validate_v1(&index),
                "grown index validates");

    retained_cap = index.cap;
    retained_keys = index.keys;
    cetta_gslt_u32_index_reset_v1(&index);
    expect_true(index.len == 0u && index.cap == retained_cap &&
                    index.keys == retained_keys,
                "reset retains storage and clears logical entries");
    expect_true(cetta_gslt_u32_index_validate_v1(&index),
                "reset index validates");
    expect_true(!cetta_gslt_u32_index_find_v1(&index, 41u, &value),
                "reset removes prior observations");
    expect_true(cetta_gslt_u32_index_insert_unique_v1(&index, 9u, 27u) ==
                    CETTA_GSLT_U32_INDEX_INSERTED_V1 &&
                    index.keys == retained_keys,
                "retained storage accepts a new inventory");

    expect_true(cetta_gslt_u32_index_insert_unique_v1(NULL, 1u, 2u) ==
                    CETTA_GSLT_U32_INDEX_INVALID_V1,
                "null insertion fails closed");
    cetta_gslt_u32_index_free_v1(&index);
    expect_true(cetta_gslt_u32_index_validate_v1(&index),
                "free restores the valid empty state");
    cetta_gslt_u32_index_free_v1(NULL);

    printf("GsltU32IndexV1Summary checks=%u failures=%u\n",
           checks, failures);
    return failures == 0u ? 0 : 1;
}
