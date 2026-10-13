#ifndef CETTA_LANGUAGE_DEF_PATTERN_ADMISSION_V1_H
#define CETTA_LANGUAGE_DEF_PATTERN_ADMISSION_V1_H

#include "language_def_core_v1.h"

/* Closed Pattern admission against the authoritative LanguageDef. This is
 * the locally nameless, typed boundary, not the ordinary Atom carrier codec.
 * Binding parameter representation follows WellSorted.parameterType? and
 * MatchesParameterRepresentation. Display names remain declaration metadata.
 * No host equations, rewrite rules or explicit substitutions are executed.
 *
 * The boundary follows WellSorted.checkClosedTerm: no free variables,
 * canonical binder metadata, no schema substitutions or collection rests,
 * and declared, well-scoped typing. Admission asks whether a derivation
 * exists; it does not select or merge declaration occurrences. */
typedef enum {
    CETTA_LD_PA_V1_OK,
    CETTA_LD_PA_V1_BAD_ARGUMENT,
    CETTA_LD_PA_V1_MALFORMED,
    CETTA_LD_PA_V1_UNKNOWN_TYPE,
    CETTA_LD_PA_V1_UNKNOWN_CONSTRUCTOR,
    CETTA_LD_PA_V1_ARITY_MISMATCH,
    CETTA_LD_PA_V1_TYPE_MISMATCH,
    CETTA_LD_PA_V1_SCOPE_MISMATCH,
    CETTA_LD_PA_V1_REPRESENTATION_MISMATCH,
    CETTA_LD_PA_V1_UNRESOLVED_PROFILE,
    CETTA_LD_PA_V1_RESOURCE_LIMIT
} CettaLdPatternAdmissionV1Status;

bool cetta_language_def_pattern_v1_admit(
    const CettaLanguageDefCoreV1 *language, const char *category,
    const CettaLdPatternV1 *pattern, uint32_t depth_limit, uint64_t work_limit,
    CettaLdPatternAdmissionV1Status *status, char *error, size_t error_size);

const char *cetta_ld_pattern_admission_v1_status_name(CettaLdPatternAdmissionV1Status status);

#endif
