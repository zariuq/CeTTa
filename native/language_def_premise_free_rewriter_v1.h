#ifndef CETTA_LANGUAGE_DEF_PREMISE_FREE_REWRITER_V1_H
#define CETTA_LANGUAGE_DEF_PREMISE_FREE_REWRITER_V1_H

#include "operational_language_def_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Executable profile for finite, premise-free LanguageDef rewrite systems.
 *
 * This profile consumes constructor declarations and PApp/FVar rewrite
 * patterns from the supplied five-field wire.  It performs deterministic
 * root-before-children contextual rewriting and retains the ordered identity
 * of every rule occurrence.  Equations, premises, binders, collections, and
 * builtin pattern literals are rejected rather than interpreted implicitly.
 *
 * The source LanguageDef must outlive the compiled program.  Rule names are
 * retained only as provenance; matching and execution depend on rule content.
 */

typedef enum {
    CETTA_LD_PFR_V1_OK = 0,
    CETTA_LD_PFR_V1_BAD_ARGUMENT,
    CETTA_LD_PFR_V1_MALFORMED_PRESENTATION,
    CETTA_LD_PFR_V1_UNSUPPORTED_PRESENTATION,
    CETTA_LD_PFR_V1_MALFORMED_TERM,
    CETTA_LD_PFR_V1_STEP_LIMIT,
    CETTA_LD_PFR_V1_DEPTH_LIMIT,
    CETTA_LD_PFR_V1_ALLOCATION_FAILURE,
    CETTA_LD_PFR_V1_INTERNAL_FAILURE
} CettaLdPfrV1Status;

typedef struct {
    void *constructors;
    uint32_t constructor_len;
    void *rules;
    uint32_t rule_len;
} CettaLdPfrV1Program;

typedef struct {
    CettaOpLangV1SExpr *normal_form;
    uint32_t *rule_indices;
    uint32_t rule_len;
} CettaLdPfrV1Result;

void cetta_ld_pfr_v1_program_init(CettaLdPfrV1Program *program);
void cetta_ld_pfr_v1_program_free(CettaLdPfrV1Program *program);

/*
 * Compile the structurally supplied presentation.  Replacement is atomic:
 * failure leaves an existing value in out unchanged.
 */
bool cetta_ld_pfr_v1_compile(
    CettaLdPfrV1Program *out,
    const CettaOperationalLanguageDefV1 *language,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size);

void cetta_ld_pfr_v1_result_init(CettaLdPfrV1Result *result);
void cetta_ld_pfr_v1_result_free(CettaLdPfrV1Result *result);

/*
 * Normalize one closed constructor term.  The strategy is deterministic
 * root-before-children contextual rewriting in presentation order.
 * Replacement is atomic on every failure, including resource limits.
 */
bool cetta_ld_pfr_v1_normalize(
    const CettaLdPfrV1Program *program,
    const CettaOpLangV1SExpr *term,
    uint32_t step_limit,
    CettaLdPfrV1Result *out,
    CettaLdPfrV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_ld_pfr_v1_term_equal(
    const CettaOpLangV1SExpr *left,
    const CettaOpLangV1SExpr *right);

bool cetta_ld_pfr_v1_rule_name(
    const CettaLdPfrV1Program *program,
    uint32_t rule_index,
    const uint8_t **name_bytes,
    uint32_t *name_len);

const char *cetta_ld_pfr_v1_status_name(CettaLdPfrV1Status status);

#endif /* CETTA_LANGUAGE_DEF_PREMISE_FREE_REWRITER_V1_H */
