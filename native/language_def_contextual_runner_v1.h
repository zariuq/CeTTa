#ifndef CETTA_LANGUAGE_DEF_CONTEXTUAL_RUNNER_V1_H
#define CETTA_LANGUAGE_DEF_CONTEXTUAL_RUNNER_V1_H

#include "language_def_core_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Relational contextual execution for the constructor Pattern profile.
 *
 * The runner implements the ordered-list semantics of LanguageDef rewriteAt:
 * every rewrite row is considered, premises are threaded left-to-right, and
 * every successful branch is retained with its multiplicity.  Congruence
 * premises recursively invoke the same relation at one less unit of
 * contextual fuel.  RelationQuery rows come from an explicit provider and
 * retain provider receipts in the resulting derivation trace.
 *
 * Version 1 deliberately accepts only PApp/FVar patterns.  This covers the
 * current first-order transformation artifacts while keeping binders,
 * substitution, and collection matching fail-closed until their full
 * LanguageDef semantics are implemented.  The runner contains no
 * language-specific constructor or relation names apart from the generic
 * built-in equality relation defined by the LanguageDef engine.
 */

typedef enum {
    CETTA_LD_CR_V1_OK = 0,
    CETTA_LD_CR_V1_BAD_ARGUMENT,
    CETTA_LD_CR_V1_UNSUPPORTED_PROFILE,
    CETTA_LD_CR_V1_PROVIDER_FAILURE,
    CETTA_LD_CR_V1_WORK_LIMIT,
    CETTA_LD_CR_V1_ALLOCATION_FAILURE,
    CETTA_LD_CR_V1_INTERNAL_FAILURE
} CettaLdCrV1Status;

/*
 * Pull one ordered external relation row.  applied_arguments are the query
 * arguments after the current bindings have been substituted.  A successful
 * callback sets present=false at the first index after its finite result.
 * Rows and their Pattern storage remain owned by the provider and need remain
 * valid only until the callback is invoked again.  receipt_id is opaque
 * provenance copied into every result branch that consumes the row.
 */
typedef bool (*CettaLdCrV1RelationProviderFn)(
    void *context,
    const CettaLdTextV1 *relation,
    const CettaLdPatternV1 *applied_arguments,
    uint32_t argument_len,
    uint32_t row_index,
    const CettaLdPatternV1 **row,
    uint32_t *row_len,
    uint64_t *receipt_id,
    bool *present,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    void *context;
    CettaLdCrV1RelationProviderFn query;
} CettaLdCrV1RelationProvider;

typedef enum {
    CETTA_LD_CR_V1_RELATION_BUILTIN = 0,
    CETTA_LD_CR_V1_RELATION_EXTERNAL
} CettaLdCrV1RelationSource;

typedef struct CettaLdCrV1Trace CettaLdCrV1Trace;

typedef struct {
    uint32_t premise_index;
    CettaLdPremiseKindV1 kind;
    union {
        struct {
            CettaLdCrV1Trace *step;
        } congruence;
        struct {
            CettaLdCrV1RelationSource source;
            uint32_t row_index;
            uint64_t receipt_id;
        } relation_query;
    } as;
} CettaLdCrV1PremiseEvidence;

struct CettaLdCrV1Trace {
    uint32_t rule_index;
    CettaLdCrV1PremiseEvidence *premises;
    uint32_t premise_len;
};

typedef struct {
    CettaLdPatternV1 term;
    CettaLdCrV1Trace *trace;
} CettaLdCrV1ResultItem;

typedef struct {
    CettaLdCrV1ResultItem *items;
    uint32_t len;
    /* True exactly when an explored congruence branch reached fuel zero. */
    bool context_fuel_exhausted;
} CettaLdCrV1Results;

typedef struct {
    const CettaLanguageDefCoreV1 *language;
} CettaLdCrV1Program;

void cetta_ld_cr_v1_program_init(CettaLdCrV1Program *program);

/* Atomic replacement: failure leaves an existing program unchanged. */
bool cetta_ld_cr_v1_compile(
    CettaLdCrV1Program *out,
    const CettaLanguageDefCoreV1 *language,
    CettaLdCrV1Status *status,
    char *error_buf,
    size_t error_buf_size);

void cetta_ld_cr_v1_results_init(CettaLdCrV1Results *results);
void cetta_ld_cr_v1_results_free(CettaLdCrV1Results *results);

/*
 * Compute the complete ordered reduct list at the supplied contextual bound.
 * An empty successful result is distinct from a runtime fault.  Fuel zero
 * projects to the same empty list as rewriteAt while also setting the
 * exhaustion observation.  Replacement is atomic on every failure.
 */
bool cetta_ld_cr_v1_reducts(
    const CettaLdCrV1Program *program,
    const CettaLdCrV1RelationProvider *provider,
    uint32_t context_fuel,
    uint64_t work_limit,
    const CettaLdPatternV1 *source,
    CettaLdCrV1Results *out,
    CettaLdCrV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Structural equality for canonical Pattern values. */
bool cetta_ld_cr_v1_pattern_equal(
    const CettaLdPatternV1 *left,
    const CettaLdPatternV1 *right);

const char *cetta_ld_cr_v1_status_name(CettaLdCrV1Status status);

#endif /* CETTA_LANGUAGE_DEF_CONTEXTUAL_RUNNER_V1_H */
