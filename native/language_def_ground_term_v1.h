#ifndef CETTA_LANGUAGE_DEF_GROUND_TERM_V1_H
#define CETTA_LANGUAGE_DEF_GROUND_TERM_V1_H

#include "language_def_core_v1.h"
#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Exact ground-term admission for the first-order constructor profile of a
 * decoded LanguageDef.
 *
 * CarrierAst values are ordinary CeTTa expressions whose head is the exact
 * GrammarRule label and whose children inhabit the rule's declared parameter
 * types.  Built-in carriers use the corresponding grounded Atom kind.  The
 * checker rejects unsupported binders, arrows, collections, token carriers,
 * ambiguous declarations, and MeTTa variables; it never assigns them an
 * invented meaning.
 */

typedef enum {
    CETTA_LD_GROUND_TERM_V1_OK = 0,
    CETTA_LD_GROUND_TERM_V1_BAD_ARGUMENT,
    CETTA_LD_GROUND_TERM_V1_UNKNOWN_TYPE,
    CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_TYPE,
    CETTA_LD_GROUND_TERM_V1_UNSUPPORTED_PROFILE,
    CETTA_LD_GROUND_TERM_V1_NON_GROUND_TERM,
    CETTA_LD_GROUND_TERM_V1_UNKNOWN_CONSTRUCTOR,
    CETTA_LD_GROUND_TERM_V1_AMBIGUOUS_CONSTRUCTOR,
    CETTA_LD_GROUND_TERM_V1_ARITY_MISMATCH,
    CETTA_LD_GROUND_TERM_V1_TYPE_MISMATCH,
    CETTA_LD_GROUND_TERM_V1_NONCANONICAL_BUILTIN,
    CETTA_LD_GROUND_TERM_V1_RESOURCE_LIMIT
} CettaLdGroundTermV1Status;

bool cetta_language_def_ground_term_v1_admit(
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    const Atom *term,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/*
 * Check that a declared type belongs to the exact typed Pattern codec
 * profile, even when no concrete value is available (for example, before a
 * relational execution that may return an empty reduct list).
 */
bool cetta_language_def_ground_term_v1_supports_pattern_codec(
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/*
 * Type-directed bridge between an admitted CeTTa ground term and the exact
 * Pattern carrier used by LanguageDef contextual semantics.
 *
 * CarrierAst:
 *   ordinary CeTTa (constructor argument ...) <-> Pattern PApp
 * CarrierBuiltinString:
 *   grounded String <-> a nullary PApp whose head is that string
 * CarrierBuiltinInt:
 *   grounded int64 <-> its canonical decimal nullary PApp
 *
 * This is deliberately not an untyped Atom coercion.  The supplied
 * LanguageDef and expected type select every constructor and every builtin
 * leaf.  Other carriers and noncanonical integer Pattern spellings fail
 * closed.  Ground-term-to-Pattern replacement is atomic.
 */
bool cetta_language_def_ground_term_v1_to_pattern(
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    const Atom *term,
    CettaLdPatternV1 *out,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Returns NULL on rejection.  Any arena allocations remain arena-owned. */
Atom *cetta_language_def_ground_term_v1_from_pattern(
    Arena *arena,
    const CettaLanguageDefCoreV1 *language,
    const char *expected_type,
    const CettaLdPatternV1 *pattern,
    uint32_t depth_limit,
    uint64_t work_limit,
    CettaLdGroundTermV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_ld_ground_term_v1_status_name(
    CettaLdGroundTermV1Status status);

#endif /* CETTA_LANGUAGE_DEF_GROUND_TERM_V1_H */
