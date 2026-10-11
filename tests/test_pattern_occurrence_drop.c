#include "space.h"

/* Link-only mutation: lose the occurrence immediately after every returned
 * one. The ordinary cursor test must reject this stream transformation. */
extern SpaceOccurrenceCursorStep __real_space_occurrence_cursor_next(
    SpaceOccurrenceCursor *, CettaIndex *);

SpaceOccurrenceCursorStep __wrap_space_occurrence_cursor_next(
    SpaceOccurrenceCursor *cursor, CettaIndex *index) {
    SpaceOccurrenceCursorStep result = __real_space_occurrence_cursor_next(cursor, index);
    if (result == SPACE_OCCURRENCE_CURSOR_ITEM) {
        CettaIndex dropped;
        (void)__real_space_occurrence_cursor_next(cursor, &dropped);
    }
    return result;
}
