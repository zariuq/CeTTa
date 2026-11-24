/* tcalc_cetta_module.h - tcalc as a module of MeTTa, (import! &self tcalc);
 * see tcalc_cetta_module.c and lib/tcalc.metta. */
#ifndef CETTA_TCALC_CETTA_MODULE_H
#define CETTA_TCALC_CETTA_MODULE_H

#include "atom.h"
#include "call_outcome.h"
#include "session.h"
#include "space.h"

#include <stdbool.h>
#include <stdint.h>

struct CettaLibraryContext;

typedef enum {
    CETTA_TCALC_OBSERVE_MULTISET = 0,            /* Exact MeTTa answer multiset (explicit) */
    CETTA_TCALC_OBSERVE_PRESENCE = 1,            /* Presence-only set store (e.g. unique) */
    CETTA_TCALC_OBSERVE_MULTISET_AUTO_EARLY = 2, /* Evaluator route before prepared_pure */
    CETTA_TCALC_OBSERVE_MULTISET_AUTO = 3,       /* Evaluator route after prepared_pure */
} CettaTcalcObservation;

/* Pre-initialize the evaluator-route mode from a startup environment scan so
 * evaluation never rescans the process environment on the first user call. */
void cetta_tcalc_set_eval_mode_from_startup(int mode) __attribute__((weak));

/* True only when CETTA_TCALC_EVAL=always (2) was requested. */
bool cetta_tcalc_eval_mode_is_always(void) __attribute__((weak));

/* Zero-allocation syntactic eligibility check for AUTO / AUTO_EARLY: returns
 * false when HEAD in SPACE cannot trigger compiled evaluation in the requested
 * mode (e.g. effectful, reserved, equation-free, or non-branching under EARLY),
 * letting the evaluator skip argument substitution and cache lookup cleanly. */
bool cetta_tcalc_space_head_auto_eligible(Space *space, SymbolId head,
                                          CettaLanguageId language_id,
                                          bool early)
    __attribute__((weak));

/* Try evaluating CALL against SPACE using cached demand-driven tcalc rules
 * guarded by per-head version counters.  Returns true and populates *OUT with
 * exact answers for OBSERVATION in LANGUAGE_ID when CALL is in the compiled
 * fragment; returns false when CALL must run on the interpreter. */
bool cetta_tcalc_space_try_eval(struct CettaLibraryContext *ctx, Space *space,
                                Arena *a, Atom *call,
                                CettaLanguageId language_id,
                                CettaTcalcObservation observation,
                                CettaCallOutcome *out)
    __attribute__((weak));

/* The module's operations, heads __cetta_lib_tcalc_load, _outcomes, _step,
 * _query, _space_eval, _space_program, and _space_cache_stats.  False for a
 * head it does not know. */
bool cetta_tcalc_module_call(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                             Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);

#endif
