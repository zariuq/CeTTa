#include "gslt_u32_slice_arena_v1.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks_run;
static unsigned checks_failed;

static void expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

static bool slice_equals(const CettaGsltU32SliceArenaV1 *arena,
                         CettaGsltU32SliceV1 slice,
                         const uint32_t *expected, uint32_t expected_len) {
    const uint32_t *items =
        cetta_gslt_u32_slice_arena_items_v1(arena, slice);
    return slice.len == expected_len && items &&
           memcmp(items, expected,
                  (size_t)expected_len * sizeof(*items)) == 0;
}

int main(void) {
    const uint32_t proof_formula[] = {1u, 10u, 11u};
    const uint32_t repeated_formula[] = {1u, 10u, 11u};
    const uint32_t parser_span[] = {4u, 9u};
    const uint32_t scratch[] = {77u, 88u, 99u};
    CettaGsltU32SliceArenaV1 arena = {0};
    CettaGsltU32SliceV1 proof = {0};
    CettaGsltU32SliceV1 repeated = {0};
    CettaGsltU32SliceV1 span = {0};
    CettaGsltU32SliceV1 temporary = {0};
    uint32_t watermark;

    expect(cetta_gslt_u32_slice_arena_append_v1(
               &arena, proof_formula, 3u, &proof),
           "proof formula appends to one flat carrier");
    expect(slice_equals(&arena, proof, proof_formula, 3u),
           "proof formula range reconstructs exactly");
    expect(cetta_gslt_u32_slice_arena_append_v1(
               &arena, parser_span, 2u, &span),
           "parser span uses the same carrier implementation");
    expect(slice_equals(&arena, span, parser_span, 2u),
           "parser span range reconstructs exactly");
    expect(cetta_gslt_u32_slice_arena_append_v1(
               &arena, repeated_formula, 3u, &repeated) &&
               cetta_gslt_u32_slice_arena_equal_v1(
                   &arena, proof, repeated),
           "equal payloads compare independently of offsets");

    watermark = cetta_gslt_u32_slice_arena_watermark_v1(&arena);
    expect(cetta_gslt_u32_slice_arena_append_v1(
               &arena, scratch, 3u, &temporary),
           "transaction scratch appends after the watermark");
    expect(cetta_gslt_u32_slice_arena_reset_v1(&arena, watermark),
           "watermark reset discards transaction scratch");
    expect(slice_equals(&arena, proof, proof_formula, 3u) &&
               slice_equals(&arena, span, parser_span, 2u),
           "pre-transaction handles survive reset");
    expect(cetta_gslt_u32_slice_arena_items_v1(
               &arena, temporary) == NULL,
           "discarded scratch handles fail closed");
    expect(!cetta_gslt_u32_slice_arena_reset_v1(
               &arena, watermark + 1u),
           "reset cannot move beyond the live carrier");
    expect(!cetta_gslt_u32_slice_arena_append_v1(
               &arena, scratch, 0u, &temporary),
           "empty physical slices are rejected");

    arena.len = UINT32_MAX;
    expect(!cetta_gslt_u32_slice_arena_reserve_v1(
               &arena, 1u, &temporary),
           "32-bit range overflow fails closed");
    arena.len = watermark;
    cetta_gslt_u32_slice_arena_free_v1(&arena);

    printf("GsltU32SliceArenaV1Summary checks=%u failures=%u\n",
           checks_run, checks_failed);
    return checks_failed == 0u ? 0 : 1;
}
