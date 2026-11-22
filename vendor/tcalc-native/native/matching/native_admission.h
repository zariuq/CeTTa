#ifndef TCALC_NATIVE_ADMISSION_H
#define TCALC_NATIVE_ADMISSION_H
#include "native_query.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { NM_ASK_BEFORE_TELLS, NM_ASK_AFTER_TELLS } NmAsk;
typedef enum {
    NM_ADMISSION_YIELD, NM_ADMITTED, NM_GUARD_OPEN, NM_GUARD_REFUTED,
    NM_TELL_REFUSED, NM_ADMISSION_EXHAUSTED, NM_ADMISSION_RESOURCE,
    NM_ADMISSION_INTERRUPTED, NM_ADMISSION_INVALIDATED, NM_ADMISSION_DEFER,
    NM_ADMISSION_FAULT
} NmAdmissionStatus;
typedef struct NmAdmission NmAdmission;
typedef struct {
    uint64_t activations, fuel_used;
    size_t tells_done, guards_done, snapshots, peak_depth;
    size_t last_index;
    bool last_is_guard, has_query, has_core;
    NmQueryStatus last_query;
    NmConstraintTruth truth;
    NmUnifyResult core;
    NmQueryStats work;
} NmAdmissionReceipt;
typedef struct {
    size_t index;
    bool guard;
    NmQueryEvent query;
} NmAdmissionEvent;
/* Observation only; events are borrowed for the callback. Do not reenter the
 * admission or release its source owners from an observer. */
typedef void (*NmAdmissionTrace)(void *context,const NmAdmissionEvent *event);

/* The equational admission fragment, after premise matching: tells extend a
 * private first-order context; equality guards require ENTAILED in the ask
 * policy's context. OPEN guards are never assumed. The arrays are copied;
 * initial is retained. Host syntax owners, program and pin remain borrowed.
 * This operation neither constructs output rows nor commits a host store.
 * Success returns YIELD and *out; failure returns *out NULL. */
NmAdmissionStatus nm_admission_open(const NmUnifyProgram *program,NmExecution execution,
                                    NmSolution *initial,NmAsk ask,
                                    const NmHostConstraint *tells,size_t ntells,
                                    const NmHostConstraint *guards,size_t nguards,
                                    NmQueryLimits limits,void *pin,bool (*valid)(void *),
                                    uint64_t fuel_budget,uint64_t fuel_used,
                                    NmAdmission **out,char *error,size_t error_size);
/* One quantum unit activates one tell or guard. Each activation pays one fuel
 * unit in addition to the unifier's receipt. Failed work retains its consumed
 * fuel and completed-prefix counts. All effects remain private. */
NmAdmissionStatus nm_admission_next(NmAdmission *admission,uint64_t quantum,
                                    char *error,size_t error_size);
NmAdmissionReceipt nm_admission_receipt(const NmAdmission *admission);
/* Borrowed final context, only after ADMITTED and while the view remains valid.
 * The host must revalidate its own view/commit token before publication. */
const NmSolution *nm_admission_result(const NmAdmission *admission);
/* Transfer the admitted context's owned reference. NULL on failure, after a
 * previous transfer, or when the view is invalid. The admission is still
 * released normally. Host syntax owners remain borrowed. */
NmSolution *nm_admission_take_result(NmAdmission *admission);
void nm_admission_set_trace(NmAdmission *admission,NmAdmissionTrace trace,void *context);
void nm_admission_free(NmAdmission *admission);

#ifdef __cplusplus
}
#endif
#endif
