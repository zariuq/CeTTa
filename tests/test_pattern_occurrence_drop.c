#include "space.h"

/* Link-only mutation: lose the occurrence immediately after every returned
 * one, whichever reader takes it, the atom's or the id's. The ordinary
 * cursor test must reject this stream transformation. */
extern SpaceOccurrenceCursorStep __real_space_occurrence_cursor_next(
    SpaceOccurrenceCursor *, CettaIndex *, Atom **);
extern SpaceOccurrenceCursorStep __real_space_occurrence_cursor_next_id(
    SpaceOccurrenceCursor *, CettaIndex *, AtomId *);

SpaceOccurrenceCursorStep __wrap_space_occurrence_cursor_next(
    SpaceOccurrenceCursor *cursor, CettaIndex *index, Atom **atom) {
    SpaceOccurrenceCursorStep result = __real_space_occurrence_cursor_next(cursor, index, atom);
    if (result == SPACE_OCCURRENCE_CURSOR_ITEM) {
        CettaIndex dropped;
        (void)__real_space_occurrence_cursor_next(cursor, &dropped, NULL);
    }
    return result;
}

SpaceOccurrenceCursorStep __wrap_space_occurrence_cursor_next_id(
    SpaceOccurrenceCursor *cursor, CettaIndex *index, AtomId *id) {
    SpaceOccurrenceCursorStep result =
        __real_space_occurrence_cursor_next_id(cursor, index, id);
    if (result == SPACE_OCCURRENCE_CURSOR_ITEM) {
        CettaIndex dropped;
        (void)__real_space_occurrence_cursor_next_id(cursor, &dropped, NULL);
    }
    return result;
}
