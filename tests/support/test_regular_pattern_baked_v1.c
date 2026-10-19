#include "native/regular_pattern_v1.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t cetta_regular_pattern_baked_example_v1[];
extern const size_t cetta_regular_pattern_baked_example_v1_len;

int main(void) {
    CettaRegularPatternV1 *first = NULL, *second = NULL;
    char error[256] = {0}; unsigned checks = 0;
    if (!cetta_regular_pattern_v1_restore(cetta_regular_pattern_baked_example_v1,
            cetta_regular_pattern_baked_example_v1_len, &first, error, sizeof(error)) ||
        !cetta_regular_pattern_v1_restore(cetta_regular_pattern_baked_example_v1,
            cetta_regular_pattern_baked_example_v1_len, &second, error, sizeof(error))) goto fail;
    cetta_regular_pattern_v1_release(first); first = NULL;
    const char *inputs[] = {"a", "abbb", "x"};
    for (unsigned i = 0; i < 3u; ++i) {
        RSDFAV1ByteToken token; RSDFAV1ByteCursorScanResult result;
        if (!rsdfa_v1_program_scan_byte_cursor_longest_prevalidated(cetta_regular_pattern_v1_program(second),
                (const uint8_t *)inputs[i], (uint32_t)strlen(inputs[i]), 0u, 1000u, &token, 1u,
                &result, error, sizeof(error)) || result.outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED ||
            result.accept_len != (i < 2u ? 1u : 0u) || (i < 2u && token.end_byte != strlen(inputs[i]))) goto fail;
        ++checks;
    }
    if (cetta_regular_pattern_v1_restore(cetta_regular_pattern_baked_example_v1,
        cetta_regular_pattern_baked_example_v1_len - 1u, &first, error, sizeof(error))) goto fail;
    ++checks;
    cetta_regular_pattern_v1_release(second);
    printf("(RegularPatternBakedV1Summary %u 0)\n", checks); return 0;
fail:
    fprintf(stderr, "baked pattern: %s\n", error);
    cetta_regular_pattern_v1_release(first); cetta_regular_pattern_v1_release(second);
    return 1;
}
