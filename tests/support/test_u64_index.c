#include "u64_index.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    CettaU64Index index;
    cetta_u64_index_init(&index);
    uint64_t keys[] = {0, UINT64_MAX, UINT64_C(0x100000001), UINT64_C(0x200000001),
        UINT64_C(0x100000002), UINT64_C(0xffffffff00000000)};
    for (uint32_t i = 0; i < sizeof(keys) / sizeof(*keys); i++)
        assert(cetta_u64_index_insert(&index, keys[i], i) == CETTA_GSLT_U32_INDEX_INSERTED_V1);
    for (uint32_t i = 0; i < sizeof(keys) / sizeof(*keys); i++) {
        uint32_t value;
        assert(cetta_u64_index_find(&index, keys[i], &value) && value == i);
        assert(cetta_u64_index_insert(&index, keys[i], 99) == CETTA_GSLT_U32_INDEX_DUPLICATE_V1);
        assert(cetta_u64_index_find(&index, keys[i], &value) && value == i);
    }
    for (uint32_t i = 0; i < 4096; i++) {
        uint64_t key = ((uint64_t)(i + 10u) << 32) | (i % 17u);
        assert(cetta_u64_index_insert(&index, key, i) == CETTA_GSLT_U32_INDEX_INSERTED_V1);
    }
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t value;
        uint64_t key = ((uint64_t)(i + 10u) << 32) | (i % 17u);
        assert(cetta_u64_index_find(&index, key, &value) && value == i);
        assert(!cetta_u64_index_find(&index, key ^ UINT64_C(0x80000000), &value));
    }
    cetta_u64_index_free(&index);
    uint32_t value;
    assert(!cetta_u64_index_find(&index, 0, &value));
    assert(cetta_u64_index_insert(&index, 0, UINT32_MAX) == CETTA_GSLT_U32_INDEX_INSERTED_V1);
    assert(cetta_u64_index_find(&index, 0, &value) && value == UINT32_MAX);
    cetta_u64_index_free(&index);
    puts("u64 index: exact high/low separation, duplicates, growth and reuse passed");
}
