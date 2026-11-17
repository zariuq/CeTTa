#ifndef TCALC_NATIVE_QUERY_H
#define TCALC_NATIVE_QUERY_H
#include "native_match_view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Distinct occurrence ids remain distinct even when their values are equal.
 * All values and their lexical/ownership contexts belong to one pinned view.
 * This component enumerates ordered candidate keys; symmetry quotienting,
 * guards, scheduling and store commit belong to the transform protocol. */
typedef struct { uint64_t occurrence; NmHostValue value; } NmQueryRow;
typedef struct {
    NmTermView terms;
    void *pin;
    bool (*valid)(void *pin);
} NmQueryView;
typedef struct {
    NmImportLimits import;
    size_t pairs, scan, memo;
} NmQueryLimits;

typedef enum {
    NM_QUERY_ANSWER, NM_QUERY_END, NM_QUERY_YIELD, NM_QUERY_EXHAUSTED,
    NM_QUERY_DEFER, NM_QUERY_RESOURCE, NM_QUERY_INVALIDATED, NM_QUERY_FAULT, NM_QUERY_INTERRUPTED
} NmQueryStatus;
typedef struct NmQuery NmQuery;
typedef struct {
    uint64_t candidates, unifier_calls, fuel_used;
    uint64_t inspections, children, bindings;
} NmQueryStats;
typedef enum { NM_QUERY_PROPOSED, NM_QUERY_IMPORTED, NM_QUERY_CORE_RESULT } NmQueryEventKind;
typedef struct {
    NmQueryEventKind kind;
    uint64_t candidate;
    const uint64_t *selected;
    size_t count;
    bool allowed;
    NmViewStatus imported;
    NmUnifyResult result;
    NmConstraintTruth truth;
} NmQueryEvent;
/* Optional observation only: event vectors are borrowed for the callback.
 * A trace callback must not reenter the query or release its borrowed owners. */
typedef void (*NmQueryTrace)(void *context, const NmQueryEvent *event);
typedef struct {
    const uint64_t *selected;
    size_t count, takes;
    const NmPreparedMatch *prepared;
    const NmUnifyWork *work;
    NmUnifyResult result;
} NmQueryAnswer;

/* Borrows program, source owners and view pin until free; copies the row and
 * pattern vectors. The first `takes` patterns select distinct occurrences;
 * reads may repeat each other but may not select a taken occurrence. A NULL
 * valid callback asserts an immutable view. Duplicate occurrence ids are a
 * setup fault, not duplicate answers. Success returns YIELD with *out set;
 * on failure *out is NULL. Callbacks enforce their own pin validity before
 * dereferencing source storage; valid is an additional protocol check. */
NmQueryStatus nm_query_open(const NmUnifyProgram *program, NmExecution execution,
                            NmQueryView view, const NmHostValue *patterns, size_t count,
                            size_t takes, const NmQueryRow *rows, size_t row_count,
                            NmQueryLimits limits, uint64_t fuel_budget, uint64_t fuel_used,
                            NmQuery **out, char *error, size_t error_size);

/* A quantum bounds complete candidate proposals, including selections ruled
 * out by occurrence exclusivity. It is not a CPU-instruction or allocation
 * bound. Native fuel is a separate cumulative receipt. YIELD preserves the
 * cursor and permits another next call. END and all error/exhaustion statuses
 * are terminal. A returned answer is borrowed until the next call or free.
 * No call publishes bindings or mutates a source row. */
NmQueryStatus nm_query_next(NmQuery *query, uint64_t quantum, NmQueryAnswer *answer,
                            char *error, size_t error_size);
NmQueryStats nm_query_stats(const NmQuery *query);
void nm_query_set_trace(NmQuery *query, NmQueryTrace trace, void *context);
void nm_query_free(NmQuery *query);

#ifdef __cplusplus
}
#endif
#endif
