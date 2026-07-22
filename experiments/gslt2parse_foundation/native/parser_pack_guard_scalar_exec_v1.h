#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARD_SCALAR_EXEC_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARD_SCALAR_EXEC_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_guard_ref_v1.h"
#include "regular_span_nfa_v1.h"

typedef struct {
    uint32_t dfa_state_limit;
    uint32_t dfa_transition_limit;
    uint64_t scan_work_limit;
    uint32_t scan_token_limit;
    uint32_t witness_work_limit;
    uint32_t parse_work_limit;
    uint32_t replay_depth;
    uint32_t result_limit;
} PPGuardScalarExecV1Limits;

/*
 * Reusable adapter for a ParserPack whose positive guards were lowered to a
 * tagged regular NFA.  The v1 adapter is intentionally limited to packs with
 * scalar/class terminals and closed-reference guard bodies; lexical-plan
 * composition remains a distinct layer.  Guard tag atoms are borrowed from
 * guard_nfa and must outlive this adapter.
 */
typedef struct {
    RSDFAV1Plan dfa;
    PPNativeV1TerminalPlan terminals;
    CettaLpNativeGllPrepared gll;
    CettaLpNativeGlrPrepared glr;
    PPNativeV1PreparedStartFamily guard_witness_parsers;
    Atom *start_state;
    uint32_t start_state_id;
    uint32_t parser_table_build_count;
    uint32_t guard_witness_parser_family_build_count;
    uint32_t guard_witness_prepared_start_len;
    Atom *const *guard_tags;
    uint32_t guard_tag_len;
    uint32_t *excluded_tags;
    char base_pack_digest[65];
    char guard_plan_digest[65];
} PPGuardScalarExecV1Plan;

void ppguard_scalar_exec_v1_plan_init(PPGuardScalarExecV1Plan *plan);
void ppguard_scalar_exec_v1_plan_free(PPGuardScalarExecV1Plan *plan);

bool ppguard_scalar_exec_v1_plan_build(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPGuardPlanV1 *guard_plan,
    const RSNFAV1Plan *guard_nfa,
    const PPGuardScalarExecV1Limits *limits,
    PPGuardScalarExecV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    RSDFAV1ScanResult scan;
    RSDFAV1ParserLattice scalar_lattice;
    PPGuardRelationV1 gll_relation;
    PPGuardRelationV1 glr_relation;
    PPGuardRefV1Receipt gll_witness;
    PPGuardRefV1Receipt glr_witness;
    PPNativeV1Result gll;
    PPNativeV1Result glr;
    PPGuardPlanV1Receipt gll_receipt;
    PPGuardPlanV1Receipt glr_receipt;
} PPGuardScalarExecV1Result;

void ppguard_scalar_exec_v1_result_init(PPGuardScalarExecV1Result *result);
void ppguard_scalar_exec_v1_result_free(PPGuardScalarExecV1Result *result);

/*
 * Consume an already-decoded scalar view without rereading source bytes.
 * Recognition, guard realization, semantic replay, and complete forest
 * equality are checked independently through both native parser kernels.
 */
bool ppguard_scalar_exec_v1_run(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardScalarExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardScalarExecV1Limits *limits,
    PPGuardScalarExecV1Result *out,
    char *error_buf,
    size_t error_buf_size);

#endif
