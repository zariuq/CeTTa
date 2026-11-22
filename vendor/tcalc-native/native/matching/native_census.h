#ifndef TCALC_NATIVE_CENSUS_H
#define TCALC_NATIVE_CENSUS_H
#include "native_query.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NM_CENSUS_YIELD, NM_CENSUS_DONE, NM_CENSUS_EXHAUSTED, NM_CENSUS_RESOURCE,
    NM_CENSUS_INTERRUPTED, NM_CENSUS_INVALIDATED, NM_CENSUS_DEFER, NM_CENSUS_FAULT
} NmCensusStatus;
typedef enum { NM_REGION_ABSENT, NM_REGION_COUNT, NM_REGION_STABLE } NmRegionCheck;
typedef enum { NM_REGION_UNAVAILABLE=0, NM_REGION_ENABLED, NM_REGION_NOT_ENTAILED } NmRegionDecision;
typedef struct NmCensus NmCensus;
typedef struct {
    const uint64_t *certain,*possible;
    size_t certain_count,possible_count;
} NmCensusObservation;
typedef struct {
    NmCensusStatus status;
    bool validated,view_valid;
    size_t certain_count,possible_count;
    uint64_t fuel_used;
    NmQueryStats validation,scan;
    bool last_validation,has_row,has_core;
    uint64_t last_row;
    NmConstraintTruth truth;
    NmUnifyResult core;
} NmCensusReceipt;
typedef struct { bool validation;NmQueryEvent query; } NmCensusEvent;
typedef void (*NmCensusTrace)(void *context,const NmCensusEvent *event);

/* A finite, pinned region under one fixed first-order context. Every row's
 * equation with the pattern is probed independently. Certain rows already
 * agree in that context; possible rows admit an extension. Occurrence
 * multiplicity is retained. Values, program and pin remain borrowed.
 * The row vector is copied; success returns YIELD and *out. */
NmCensusStatus nm_census_open(const NmUnifyProgram *program,NmExecution execution,NmQueryView view,
                              NmHostValue pattern,const NmQueryRow *rows,size_t count,NmQueryLimits limits,
                              uint64_t fuel_budget,uint64_t fuel_used,NmCensus **out,
                              char *error,size_t error_size);
/* A quantum bounds query advances. The pattern and initial substitution are
 * validated first, even for an empty region. All validation and scanning fuel
 * is retained in the receipt; an outer admission charges its own activation. */
NmCensusStatus nm_census_next(NmCensus *census,uint64_t quantum,char *error,size_t error_size);
NmCensusReceipt nm_census_receipt(const NmCensus *census);
/* Borrowed complete observation, only while its source view is still valid. */
const NmCensusObservation *nm_census_observation(const NmCensus *census);
/* ENABLED requires a complete valid scan: no possible row for absence;
 * certain == possible == target for count; certain == possible for a stable
 * region. NOT_ENTAILED does not assert the negation of the underlying claim.
 * Operational failures and unfinished scans always return UNAVAILABLE. */
NmRegionDecision nm_census_check(const NmCensus *census,NmRegionCheck check,uint64_t target);
/* Observation only; the wrapped query event is borrowed for the callback.
 * Do not reenter the census or release its source owners. */
void nm_census_set_trace(NmCensus *census,NmCensusTrace trace,void *context);
void nm_census_free(NmCensus *census);

#ifdef __cplusplus
}
#endif
#endif
