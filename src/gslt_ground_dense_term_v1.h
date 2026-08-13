#ifndef CETTA_GSLT_GROUND_DENSE_TERM_V1_H
#define CETTA_GSLT_GROUND_DENSE_TERM_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_GSLT_GROUND_DENSE_OK_V1,
    CETTA_GSLT_GROUND_DENSE_MISMATCH_V1,
    CETTA_GSLT_GROUND_DENSE_INVALID_V1,
    CETTA_GSLT_GROUND_DENSE_RESOURCE_V1,
    /* The direct view is outside the locally available dynamic fragment.
     * No semantic mismatch has been established; invoke the general view
     * matcher without using the dense workspace. */
    CETTA_GSLT_GROUND_DENSE_DEFER_V1,
} CettaGsltGroundDenseStatusV1;

/* Resolve one variable of an immutable source view.  OK must return a closed
 * target.  DEFER retains the source semantics but asks the caller to use its
 * general matcher.  MISMATCH is not a valid resolver result. */
typedef CettaGsltGroundDenseStatusV1
    (*CettaGsltGroundDenseViewResolveV1)(
        void *context, Atom *source_variable, Atom **target_out);

typedef struct {
    void *impl;
} CettaGsltGroundDenseTermProgramV1;

typedef struct {
    void *impl;
} CettaGsltGroundDenseWorkspaceV1;

typedef struct {
    uint64_t match_nodes;
    uint64_t rigid_subtrees_compared;
    uint64_t slot_writes;
    uint64_t slot_compares;
    uint64_t expression_materializations;
    uint64_t rigid_subtrees_reused;
    uint64_t workspace_growths;
    uint64_t view_nodes;
    uint64_t view_variable_resolutions;
    uint64_t view_deferrals;
} CettaGsltGroundDenseStatsV1;

void cetta_gslt_ground_dense_term_program_init_v1(
    CettaGsltGroundDenseTermProgramV1 *program);
void cetta_gslt_ground_dense_term_program_free_v1(
    CettaGsltGroundDenseTermProgramV1 *program);

/*
 * Compile an immutable Atom pattern into bounded dense variable slots.
 * Variable IDs must occupy the half-open interval
 * [first_variable, first_variable + variable_width).  Preparation is
 * transactional: failure leaves an existing program unchanged.  The source
 * graph is borrowed and must outlive the compiled program.
 */
bool cetta_gslt_ground_dense_term_compile_v1(
    CettaGsltGroundDenseTermProgramV1 *program,
    Atom *source,
    VarId first_variable,
    uint32_t variable_width,
    char *error_buf,
    size_t error_buf_size);

uint32_t cetta_gslt_ground_dense_term_width_v1(
    const CettaGsltGroundDenseTermProgramV1 *program);
uint32_t cetta_gslt_ground_dense_term_node_count_v1(
    const CettaGsltGroundDenseTermProgramV1 *program);
bool cetta_gslt_ground_dense_term_is_linear_v1(
    const CettaGsltGroundDenseTermProgramV1 *program);

void cetta_gslt_ground_dense_workspace_init_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace);
void cetta_gslt_ground_dense_workspace_free_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace);
void cetta_gslt_ground_dense_workspace_discard_match_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace);

/*
 * Match the compiled pattern against a closed target.  Slot storage and the
 * traversal stack are retained for later calls, while a fresh logical epoch
 * makes every call start with an empty substitution.
 */
CettaGsltGroundDenseStatusV1 cetta_gslt_ground_dense_term_match_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Atom *target,
    CettaGsltGroundDenseStatsV1 *stats);

/* Match the compiled pattern directly against an immutable source term plus
 * its demand resolver.  This traverses source applications without building
 * the complete substituted value.  A variable-bearing source expression
 * captured by one consumer slot, or an unresolved source variable, returns
 * DEFER transactionally so the caller can invoke its general view matcher. */
CettaGsltGroundDenseStatusV1 cetta_gslt_ground_dense_term_match_view_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Atom *source,
    CettaGsltGroundDenseViewResolveV1 resolve,
    void *resolve_context,
    CettaGsltGroundDenseStatsV1 *stats);

/*
 * Instantiate another program of the same slot width from the most recent
 * successful match.  Rigid subtrees are reused; only expressions containing
 * slots are rebuilt in the caller-owned arena.
 */
CettaGsltGroundDenseStatusV1 cetta_gslt_ground_dense_term_instantiate_v1(
    CettaGsltGroundDenseWorkspaceV1 *workspace,
    const CettaGsltGroundDenseTermProgramV1 *program,
    Arena *arena,
    Atom **result_out,
    CettaGsltGroundDenseStatsV1 *stats);

#endif /* CETTA_GSLT_GROUND_DENSE_TERM_V1_H */
