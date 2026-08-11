#ifndef CETTA_NIK_RUNTIME_H
#define CETTA_NIK_RUNTIME_H

#include "gslt_horn_runtime.h"
#include "inference_checker.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_NIK_ACCEPTED = 0,
    CETTA_NIK_REJECTED,
    CETTA_NIK_MALFORMED,
    CETTA_NIK_UNSUPPORTED,
    CETTA_NIK_INCOMPLETE,
    CETTA_NIK_FAULT,
} CettaNikOutcome;

typedef struct {
    CettaInferenceReplayLimits replay;
    CettaGsltHornLimits gslt;
    uint64_t max_total_work;
} CettaNikLimits;

typedef struct {
    CettaNikOutcome outcome;
    const char *authority_alias;
    const char *system_id;
    const char *revision;
    const char *authority_digest;
    const char *catalog_digest;
    CettaInferenceStatus native_status;
    CettaGsltHornOutcome reference_outcome;
    CettaGsltHornOutcome compiled_outcome;
    bool native_ran;
    bool reference_ran;
    bool compiled_ran;
    bool native_accepted;
    bool reference_accepted;
    bool compiled_accepted;
    uint64_t native_nodes;
    uint64_t reference_rule_attempts;
    uint64_t compiled_rule_attempts;
    uint64_t total_work;
} CettaNikReceiptV1;

const char *cetta_nik_outcome_name(CettaNikOutcome outcome);

/*
 * Check one closed claim against one named admitted authority.  The same
 * Lean-emitted catalog is replayed by the structural native checker and by
 * both finite-Horn GSLT realizations.  Search is deliberately outside this
 * interface.
 */
CettaNikOutcome cetta_nik_check_v1(
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size);

#endif /* CETTA_NIK_RUNTIME_H */
