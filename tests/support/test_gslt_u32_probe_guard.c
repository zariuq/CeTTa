#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_calls, fail_allocation;
static void *checked_malloc(size_t size) {
    if (++allocation_calls == fail_allocation)
        return NULL;
    return malloc(size);
}
static void *checked_calloc(size_t count, size_t size) {
    if (++allocation_calls == fail_allocation)
        return NULL;
    return calloc(count, size);
}
#define malloc checked_malloc
#define calloc checked_calloc
#define CETTA_GSLT_INDEX_TEST_HOOKS 1
#include "../../src/gslt_u32_index_v1.c"
#undef malloc
#undef calloc

static uint32_t inverse_odd(uint32_t value) {
    uint32_t inverse = value;
    for (unsigned i = 0u; i < 5u; i++)
        inverse *= 2u - value * inverse;
    return inverse;
}

static uint32_t inverse_hash(uint32_t hash) {
    uint32_t key = hash ^ (hash >> 16u);
    key *= inverse_odd(UINT32_C(0x846ca68b));
    key = key ^ (key >> 15u) ^ (key >> 30u);
    key *= inverse_odd(UINT32_C(0x7feb352d));
    key ^= key >> 16u;
    assert(u32_index_key_hash(key) == hash);
    return key;
}

static uint32_t collision_key(uint32_t number, uint32_t salt) {
    return inverse_hash(number << 16u) ^ salt;
}

static void fill(CettaGsltU32IndexV1 *map, uint32_t count) {
    cetta_gslt_u32_index_init_v1(map);
    /* Establish the runtime salt before deriving adversarial keys. */
    assert(cetta_gslt_u32_index_insert_unique_v1(map, UINT32_MAX, 19u) ==
           CETTA_GSLT_U32_INDEX_INSERTED_V1);
    cetta_gslt_u32_index_reset_v1(map);
    for (uint32_t i = 1u; i <= count; i++)
        assert(cetta_gslt_u32_index_insert_unique_v1(map,
            collision_key(i, map->placement_salt), i ^ 0xabcdu) ==
            CETTA_GSLT_U32_INDEX_INSERTED_V1);
}

static void contents(const CettaGsltU32IndexV1 *map, uint32_t count) {
    assert(map->len == count && cetta_gslt_u32_index_validate_v1(map));
    for (uint32_t i = 1u; i <= count; i++) {
        uint32_t value;
        test_front_probes = test_tree_probes = 0u;
        assert(cetta_gslt_u32_index_find_v1(map,
            collision_key(i, map->placement_salt), &value));
        assert(value == (i ^ 0xabcdu));
        assert(test_front_probes <= 32u && test_tree_probes <= 65u);
    }
}

static void scaling(void) {
    const uint32_t sizes[] = {256u, 1024u, 4096u, 16384u};
    for (size_t trial = 0u; trial < 4u; trial++) {
        CettaGsltU32IndexV1 map;
        fill(&map, sizes[trial]);
        contents(&map, sizes[trial]);
        assert(map.overflow_len == map.len - 32u);
        uint32_t value;
        test_front_probes = test_tree_probes = 0u;
        assert(!cetta_gslt_u32_index_find_v1(&map,
            collision_key(sizes[trial] + 1u, map.placement_salt), &value));
        assert(test_front_probes <= 32u && test_tree_probes <= 65u);
        printf("collision n=%u cap=%u overflow=%u height=%u miss=%llu+%llu\n",
            map.len, map.cap, map.overflow_len, u32_index_height(map.overflow),
            (unsigned long long)test_front_probes,
            (unsigned long long)test_tree_probes);
        cetta_gslt_u32_index_free_v1(&map);
    }
}

static void faults(uint32_t count) {
    unsigned refused = 0u, completed = 0u;
    for (size_t fail = 1u; fail <= 20u; fail++) {
        CettaGsltU32IndexV1 map;
        fill(&map, count);
        CettaGsltU32IndexV1 before = map;
        allocation_calls = 0u;
        fail_allocation = fail;
        CettaGsltU32IndexInsertResultV1 result =
            cetta_gslt_u32_index_insert_unique_v1(&map,
                collision_key(count + 1u, map.placement_salt), (count + 1u) ^ 0xabcdu);
        fail_allocation = 0u;
        if (result == CETTA_GSLT_U32_INDEX_RESOURCE_V1) {
            assert(memcmp(&map, &before, sizeof(map)) == 0);
            contents(&map, count);
            refused++;
        } else {
            assert(result == CETTA_GSLT_U32_INDEX_INSERTED_V1);
            contents(&map, count + 1u);
            completed++;
        }
        cetta_gslt_u32_index_free_v1(&map);
    }
    assert(refused != 0u && completed != 0u);
    printf("faults count=%u refused=%u completed=%u\n", count, refused, completed);
}

static void endpoints(void) {
    const uint32_t targets[] = {0u, UINT32_MAX};
    const uint32_t salts[] = {0u, 1u, UINT32_C(0xf9ae72b3), UINT32_MAX};
    for (size_t seed = 0u; seed < 4u; seed++) {
        for (size_t which = 0u; which < 2u; which++) {
            CettaGsltU32IndexV1 map;
            fill(&map, 0u);
            map.placement_salt = salts[seed];
            uint32_t target = targets[which];
            uint32_t hash = u32_index_key_hash(target ^ map.placement_salt);
            for (uint32_t i = 1u; i <= 32u; i++) {
                uint32_t wanted = (((hash >> 16u) + i) << 16u) | (hash & 65535u);
                uint32_t key = inverse_hash(wanted) ^ map.placement_salt;
                assert(key != target);
                assert(cetta_gslt_u32_index_insert_unique_v1(&map, key, i) ==
                       CETTA_GSLT_U32_INDEX_INSERTED_V1);
            }
            assert(cetta_gslt_u32_index_insert_unique_v1(&map, target, target) ==
                   CETTA_GSLT_U32_INDEX_INSERTED_V1);
            assert(map.overflow_len == 1u);
            uint32_t value;
            assert(cetta_gslt_u32_index_find_v1(&map, target, &value) && value == target);
            assert(cetta_gslt_u32_index_insert_unique_v1(&map, target, 17u) ==
                   CETTA_GSLT_U32_INDEX_DUPLICATE_V1);
            assert(cetta_gslt_u32_index_find_v1(&map, target, &value) && value == target);
            assert(cetta_gslt_u32_index_validate_v1(&map));
            cetta_gslt_u32_index_reset_v1(&map);
            assert(!map.overflow && !map.overflow_len &&
                   cetta_gslt_u32_index_validate_v1(&map));
            cetta_gslt_u32_index_free_v1(&map);
        }
    }
    puts("PASS: zero/maximum words survive overflow, duplicate refusal and reset under four placements");
}

static void first_allocation_faults(void) {
    for (size_t fail = 1u; fail <= 3u; fail++) {
        CettaGsltU32IndexV1 map;
        cetta_gslt_u32_index_init_v1(&map);
        CettaGsltU32IndexV1 before = map;
        allocation_calls = 0u;
        fail_allocation = fail;
        assert(cetta_gslt_u32_index_insert_unique_v1(&map, 0u, 0u) ==
               CETTA_GSLT_U32_INDEX_RESOURCE_V1);
        fail_allocation = 0u;
        assert(memcmp(&map, &before, sizeof(map)) == 0 &&
               cetta_gslt_u32_index_validate_v1(&map));
        cetta_gslt_u32_index_free_v1(&map);
    }
}

static uint32_t random_state = 0x7182ac51u;
static uint32_t random_word(void) {
    random_state ^= random_state << 13u;
    random_state ^= random_state >> 17u;
    return random_state ^= random_state << 5u;
}

static void randomized(void) {
    uint32_t expected[32768];
    unsigned char present[32768] = {0};
    CettaGsltU32IndexV1 map;
    fill(&map, 0u);
    for (unsigned iteration = 0u; iteration < 400000u; iteration++) {
        if (iteration % 4096u == 0u) {
            cetta_gslt_u32_index_reset_v1(&map);
            memset(present, 0, sizeof(present));
        }
        uint32_t logical = random_word() & 32767u;
        uint32_t key = collision_key(logical, map.placement_salt);
        uint32_t value = random_word(), observed;
        if (value % 3u) {
            CettaGsltU32IndexInsertResultV1 result =
                cetta_gslt_u32_index_insert_unique_v1(&map, key, value);
            if (present[logical])
                assert(result == CETTA_GSLT_U32_INDEX_DUPLICATE_V1);
            else {
                assert(result == CETTA_GSLT_U32_INDEX_INSERTED_V1);
                present[logical] = 1u;
                expected[logical] = value;
            }
        }
        test_front_probes = test_tree_probes = 0u;
        assert(cetta_gslt_u32_index_find_v1(&map, key, &observed) == (present[logical] != 0u));
        if (present[logical])
            assert(observed == expected[logical]);
        assert(test_front_probes <= 32u && test_tree_probes <= 65u);
        if (iteration % 1009u == 0u)
            assert(cetta_gslt_u32_index_validate_v1(&map));
    }
    cetta_gslt_u32_index_free_v1(&map);
    puts("PASS: 400000 colliding-key model operations, exact values and bounded probes");
}

int main(void) {
    first_allocation_faults();
    endpoints();
    scaling();
    faults(32u);
    faults(44u);
    randomized();
    return 0;
}
