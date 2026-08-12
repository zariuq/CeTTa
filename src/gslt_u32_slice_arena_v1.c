#include "gslt_u32_slice_arena_v1.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void cetta_gslt_u32_slice_arena_free_v1(CettaGsltU32SliceArenaV1 *arena) {
    if (!arena)
        return;
    free(arena->items);
    memset(arena, 0, sizeof(*arena));
}

uint32_t cetta_gslt_u32_slice_arena_watermark_v1(
    const CettaGsltU32SliceArenaV1 *arena) {
    return arena ? arena->len : 0u;
}

bool cetta_gslt_u32_slice_arena_reset_v1(
    CettaGsltU32SliceArenaV1 *arena, uint32_t watermark) {
    if (!arena || watermark > arena->len)
        return false;
    arena->len = watermark;
    return true;
}

static bool u32_slice_arena_grow(CettaGsltU32SliceArenaV1 *arena,
                                 uint32_t needed) {
    uint32_t next;
    uint32_t *grown;

    if (needed <= arena->cap)
        return true;
    next = arena->cap ? arena->cap : 16u;
    while (next < needed) {
        if (next > UINT32_MAX / 2u) {
            next = UINT32_MAX;
            break;
        }
        next *= 2u;
    }
    if (next < needed || (size_t)next > SIZE_MAX / sizeof(*grown))
        return false;
    grown = realloc(arena->items, (size_t)next * sizeof(*grown));
    if (!grown)
        return false;
    arena->items = grown;
    arena->cap = next;
    return true;
}

bool cetta_gslt_u32_slice_arena_reserve_v1(
    CettaGsltU32SliceArenaV1 *arena, uint32_t count,
    CettaGsltU32SliceV1 *slice_out) {
    uint32_t needed;

    if (!arena || !slice_out || count == 0u ||
        count > UINT32_MAX - arena->len)
        return false;
    needed = arena->len + count;
    if (!u32_slice_arena_grow(arena, needed))
        return false;
    *slice_out = (CettaGsltU32SliceV1){arena->len, count};
    arena->len = needed;
    return true;
}

bool cetta_gslt_u32_slice_arena_append_v1(
    CettaGsltU32SliceArenaV1 *arena,
    const uint32_t *items, uint32_t count,
    CettaGsltU32SliceV1 *slice_out) {
    CettaGsltU32SliceV1 slice;

    if (!items || !cetta_gslt_u32_slice_arena_reserve_v1(
                      arena, count, &slice))
        return false;
    memcpy(&arena->items[slice.offset], items,
           (size_t)count * sizeof(*items));
    *slice_out = slice;
    return true;
}

const uint32_t *cetta_gslt_u32_slice_arena_items_v1(
    const CettaGsltU32SliceArenaV1 *arena, CettaGsltU32SliceV1 slice) {
    if (!arena || !arena->items || slice.len == 0u ||
        slice.offset > arena->len || slice.len > arena->len - slice.offset)
        return NULL;
    return &arena->items[slice.offset];
}

bool cetta_gslt_u32_slice_arena_equal_v1(
    const CettaGsltU32SliceArenaV1 *arena,
    CettaGsltU32SliceV1 left, CettaGsltU32SliceV1 right) {
    const uint32_t *left_items;
    const uint32_t *right_items;

    if (left.len != right.len)
        return false;
    left_items = cetta_gslt_u32_slice_arena_items_v1(arena, left);
    right_items = cetta_gslt_u32_slice_arena_items_v1(arena, right);
    return left_items && right_items &&
           memcmp(left_items, right_items,
                  (size_t)left.len * sizeof(*left_items)) == 0;
}
