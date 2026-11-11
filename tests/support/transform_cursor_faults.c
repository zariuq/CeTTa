#include "space.h"

#include <stdio.h>
#include <stdlib.h>

/* Linked only into the stream-control fault executable.  The third clone is
 * the second p row re-entering its q join level in the focused program. */
bool __real_space_occurrence_cursor_clone(
    const SpaceOccurrenceCursor *source, SpaceOccurrenceCursor *dest);

bool __wrap_space_occurrence_cursor_clone(
    const SpaceOccurrenceCursor *source, SpaceOccurrenceCursor *dest) {
    static size_t calls;
    const char *setting = getenv("CETTA_TEST_TRANSFORM_CURSOR_CLONE_FAIL_AT");
    size_t fail_at = setting ? (size_t)strtoull(setting, NULL, 10) : 0u;
    calls++;
    if (fail_at && calls == fail_at) {
        fprintf(stderr, "transform-cursor-test-clone-refusal %zu\n", calls);
        return false;
    }
    return __real_space_occurrence_cursor_clone(source, dest);
}
