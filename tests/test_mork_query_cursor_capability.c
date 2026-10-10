#include <assert.h>
#include <stdio.h>

#include "mork_space_bridge_runtime.h"

void mork_bytes_free(uint8_t *bytes, size_t len) {
    (void)bytes;
    (void)len;
    assert(!"an absent scalar-count export must not allocate an error packet");
}

#if CETTA_TEST_QUERY_CURSOR_PRESENT
CettaMorkQueryCursorHandle *mork_query_cursor_new_query_only_v2(
    CettaMorkSpaceHandle *space, const uint8_t *pattern, size_t len) {
    (void)space;
    (void)pattern;
    (void)len;
    assert(!"capability detection must not attempt a query");
    return NULL;
}
#endif

int main(void) {
    assert(cetta_mork_bridge_supports_query_only_v2_cursor() ==
           (CETTA_TEST_QUERY_CURSOR_PRESENT != 0));
    uint64_t count = 99u;
    unsigned char opaque_handle = 0u;
    assert(!cetta_mork_bridge_space_count_expr_bytes(
        (const CettaMorkSpaceHandle *)&opaque_handle, NULL, 0u, &count));
    assert(count == 0u);
    puts("PASS: query cursor capability distinguishes present and absent constructors");
    return 0;
}
