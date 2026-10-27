#ifndef CETTA_RELATIONAL_CURSOR_H
#define CETTA_RELATIONAL_CURSOR_H

#include "relational_plan.h"

typedef struct CettaRelationalCountCursor CettaRelationalCountCursor;

typedef enum {
    CETTA_RELATIONAL_COUNT_PAUSED = 0,
    CETTA_RELATIONAL_COUNT_COMPLETE,
    CETTA_RELATIONAL_COUNT_INVALIDATED,
    CETTA_RELATIONAL_COUNT_OVERFLOW,
    /* An enumeration's visitor stopped it. */
    CETTA_RELATIONAL_COUNT_STOPPED,
} CettaRelationalCountStatus;

/* Each cursor owns its navigation and unpublished accumulator, and retains
 * the immutable plan. Open/clone failures leave *out unchanged. Neither
 * operation retains the Space, query atoms, scratch arena or seed bindings.
 * Private state has one range per factor, one iterator/saved parent range
 * per variable-factor incidence, and one level per retained variable. */
bool cetta_relational_count_cursor_open(
    const CettaRelationalPlan *plan, const Space *live_space,
    CettaRelationalCountCursor **out);
bool cetta_relational_count_cursor_clone(
    const CettaRelationalCountCursor *source, const Space *live_space,
    CettaRelationalCountCursor **out);
void cetta_relational_count_cursor_free(CettaRelationalCountCursor *cursor);

/* The quantum counts navigation transitions, not dialect evaluation fuel.
 * One transition may inspect a variable's factors, perform a logarithmic
 * range seek, or multiply the leaf's factor lengths. Zero retains progress.
 * Revalidation precedes and follows each slice; an invalidated cursor cannot
 * resume. USED receives only transitions actually taken in this slice. */
CettaRelationalCountStatus cetta_relational_count_cursor_advance(
    CettaRelationalCountCursor *cursor, const Space *live_space,
    uint64_t quantum, uint64_t *used);
CettaRelationalCountStatus cetta_relational_count_cursor_status(
    const CettaRelationalCountCursor *cursor);

/* Only a complete, still valid cursor publishes a count/work receipt.
 * Paused, stale and overflowed states leave both outputs unchanged. */
bool cetta_relational_count_cursor_result(
    const CettaRelationalCountCursor *cursor, const Space *live_space,
    uint64_t *count, CettaRelationalWork *work);
bool cetta_relational_count_cursor_storage(
    const CettaRelationalCountCursor *cursor, CettaContinuationStorage *storage);

/* The controller uses the existing provider-erased continuation interface.
 * The endpoint borrows a live Space for validation and a cursor slot for
 * successful restore. Captured payloads own no endpoint or Space pointer.
 * A failed restore consumes neither the payload nor the target cursor. */
typedef struct {
    CettaRelationalCountCursor **cursor;
    const Space *space;
} CettaRelationalCountEndpoint;

CettaContinuationMachine cetta_relational_count_continuation_machine(
    CettaRelationalCountEndpoint *endpoint);

#endif
