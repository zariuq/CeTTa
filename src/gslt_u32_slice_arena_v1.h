#ifndef CETTA_GSLT_U32_SLICE_ARENA_V1_H
#define CETTA_GSLT_U32_SLICE_ARENA_V1_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t offset;
    uint32_t len;
} CettaGsltU32SliceV1;

typedef struct {
    uint32_t *items;
    uint32_t len;
    uint32_t cap;
} CettaGsltU32SliceArenaV1;

void cetta_gslt_u32_slice_arena_free_v1(CettaGsltU32SliceArenaV1 *arena);

uint32_t cetta_gslt_u32_slice_arena_watermark_v1(
    const CettaGsltU32SliceArenaV1 *arena);

bool cetta_gslt_u32_slice_arena_reset_v1(
    CettaGsltU32SliceArenaV1 *arena, uint32_t watermark);

bool cetta_gslt_u32_slice_arena_reserve_v1(
    CettaGsltU32SliceArenaV1 *arena, uint32_t count,
    CettaGsltU32SliceV1 *slice_out);

bool cetta_gslt_u32_slice_arena_append_v1(
    CettaGsltU32SliceArenaV1 *arena,
    const uint32_t *items, uint32_t count,
    CettaGsltU32SliceV1 *slice_out);

const uint32_t *cetta_gslt_u32_slice_arena_items_v1(
    const CettaGsltU32SliceArenaV1 *arena, CettaGsltU32SliceV1 slice);

bool cetta_gslt_u32_slice_arena_equal_v1(
    const CettaGsltU32SliceArenaV1 *arena,
    CettaGsltU32SliceV1 left, CettaGsltU32SliceV1 right);

#endif /* CETTA_GSLT_U32_SLICE_ARENA_V1_H */
