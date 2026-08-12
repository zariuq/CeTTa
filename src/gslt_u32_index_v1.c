#include "gslt_u32_index_v1.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { CETTA_GSLT_U32_INDEX_INITIAL_CAPACITY_V1 = 16u };

static uint32_t cetta_gslt_u32_index_hash_v1(uint32_t value) {
    value ^= value >> 16u;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15u;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16u;
    return value;
}

void cetta_gslt_u32_index_init_v1(CettaGsltU32IndexV1 *index) {
    if (index)
        memset(index, 0, sizeof(*index));
}

void cetta_gslt_u32_index_free_v1(CettaGsltU32IndexV1 *index) {
    if (!index)
        return;
    free(index->keys);
    free(index->values);
    free(index->occupied);
    cetta_gslt_u32_index_init_v1(index);
}

void cetta_gslt_u32_index_reset_v1(CettaGsltU32IndexV1 *index) {
    if (!index)
        return;
    if (index->occupied && index->cap > 0u)
        memset(index->occupied, 0, (size_t)index->cap);
    index->len = 0u;
}

static bool cetta_gslt_u32_index_shape_valid_v1(
    const CettaGsltU32IndexV1 *index) {
    if (!index)
        return false;
    if (index->cap == 0u)
        return index->len == 0u && !index->keys && !index->values &&
               !index->occupied;
    return index->cap >= CETTA_GSLT_U32_INDEX_INITIAL_CAPACITY_V1 &&
           (index->cap & (index->cap - 1u)) == 0u &&
           index->len <= index->cap && index->keys && index->values &&
           index->occupied;
}

static bool cetta_gslt_u32_index_locate_v1(
    const CettaGsltU32IndexV1 *index, uint32_t key,
    uint32_t *slot_out, bool *found_out) {
    uint32_t mask;
    uint32_t slot;
    uint32_t probes;

    if (!index || !slot_out || !found_out || index->cap == 0u)
        return false;
    mask = index->cap - 1u;
    slot = cetta_gslt_u32_index_hash_v1(key) & mask;
    for (probes = 0u; probes < index->cap; probes++) {
        if (!index->occupied[slot]) {
            *slot_out = slot;
            *found_out = false;
            return true;
        }
        if (index->keys[slot] == key) {
            *slot_out = slot;
            *found_out = true;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    return false;
}

static bool cetta_gslt_u32_index_rehash_v1(
    CettaGsltU32IndexV1 *index, uint32_t cap) {
    CettaGsltU32IndexV1 grown;
    uint32_t old_slot;

    if (!index || cap < CETTA_GSLT_U32_INDEX_INITIAL_CAPACITY_V1 ||
        (cap & (cap - 1u)) != 0u ||
        (size_t)cap > SIZE_MAX / sizeof(uint32_t))
        return false;
    cetta_gslt_u32_index_init_v1(&grown);
    grown.keys = malloc((size_t)cap * sizeof(*grown.keys));
    grown.values = malloc((size_t)cap * sizeof(*grown.values));
    grown.occupied = calloc((size_t)cap, sizeof(*grown.occupied));
    if (!grown.keys || !grown.values || !grown.occupied) {
        cetta_gslt_u32_index_free_v1(&grown);
        return false;
    }
    grown.cap = cap;
    for (old_slot = 0u; old_slot < index->cap; old_slot++) {
        uint32_t new_slot;
        bool found;
        if (!index->occupied[old_slot])
            continue;
        if (!cetta_gslt_u32_index_locate_v1(
                &grown, index->keys[old_slot], &new_slot, &found) || found) {
            cetta_gslt_u32_index_free_v1(&grown);
            return false;
        }
        grown.keys[new_slot] = index->keys[old_slot];
        grown.values[new_slot] = index->values[old_slot];
        grown.occupied[new_slot] = 1u;
        grown.len++;
    }
    free(index->keys);
    free(index->values);
    free(index->occupied);
    *index = grown;
    return true;
}

static bool cetta_gslt_u32_index_prepare_insert_v1(
    CettaGsltU32IndexV1 *index) {
    uint32_t next_cap;

    if (!cetta_gslt_u32_index_shape_valid_v1(index) ||
        index->len == UINT32_MAX)
        return false;
    if (index->cap != 0u &&
        (uint64_t)(index->len + 1u) * UINT64_C(10) <=
            (uint64_t)index->cap * UINT64_C(7))
        return true;
    if (index->cap == 0u)
        next_cap = CETTA_GSLT_U32_INDEX_INITIAL_CAPACITY_V1;
    else {
        if (index->cap > UINT32_MAX / 2u)
            return false;
        next_cap = index->cap * 2u;
    }
    return cetta_gslt_u32_index_rehash_v1(index, next_cap);
}

CettaGsltU32IndexInsertResultV1 cetta_gslt_u32_index_insert_unique_v1(
    CettaGsltU32IndexV1 *index, uint32_t key, uint32_t value) {
    uint32_t slot;
    bool found;

    if (!cetta_gslt_u32_index_shape_valid_v1(index))
        return CETTA_GSLT_U32_INDEX_INVALID_V1;
    if (index->cap != 0u &&
        !cetta_gslt_u32_index_locate_v1(index, key, &slot, &found))
        return CETTA_GSLT_U32_INDEX_INVALID_V1;
    if (index->cap != 0u && found)
        return CETTA_GSLT_U32_INDEX_DUPLICATE_V1;
    if (!cetta_gslt_u32_index_prepare_insert_v1(index))
        return CETTA_GSLT_U32_INDEX_RESOURCE_V1;
    if (!cetta_gslt_u32_index_locate_v1(index, key, &slot, &found) || found)
        return CETTA_GSLT_U32_INDEX_INVALID_V1;
    index->keys[slot] = key;
    index->values[slot] = value;
    index->occupied[slot] = 1u;
    index->len++;
    return CETTA_GSLT_U32_INDEX_INSERTED_V1;
}

bool cetta_gslt_u32_index_find_v1(
    const CettaGsltU32IndexV1 *index, uint32_t key, uint32_t *value_out) {
    uint32_t slot;
    bool found;

    if (!value_out || !cetta_gslt_u32_index_shape_valid_v1(index) ||
        index->cap == 0u ||
        !cetta_gslt_u32_index_locate_v1(index, key, &slot, &found) || !found)
        return false;
    *value_out = index->values[slot];
    return true;
}

bool cetta_gslt_u32_index_validate_v1(const CettaGsltU32IndexV1 *index) {
    uint32_t slot;
    uint32_t occupied_len = 0u;

    if (!cetta_gslt_u32_index_shape_valid_v1(index))
        return false;
    for (slot = 0u; slot < index->cap; slot++) {
        uint32_t located;
        bool found;
        if (!index->occupied[slot])
            continue;
        occupied_len++;
        if (!cetta_gslt_u32_index_locate_v1(
                index, index->keys[slot], &located, &found) ||
            !found || located != slot)
            return false;
    }
    return occupied_len == index->len;
}
