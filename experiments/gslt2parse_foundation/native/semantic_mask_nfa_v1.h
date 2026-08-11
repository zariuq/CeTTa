#ifndef CETTA_GSLT2PARSE_SEMANTIC_MASK_NFA_V1_H
#define CETTA_GSLT2PARSE_SEMANTIC_MASK_NFA_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "regular_span_nfa_v1.h"

typedef enum {
    PPSEMANTIC_MASK_V1_DROP = 0,
    PPSEMANTIC_MASK_V1_COPY = 1,
    PPSEMANTIC_MASK_V1_WRAPPER = 2
} PPSemanticMaskV1ActionKind;

typedef struct {
    PPSemanticMaskV1ActionKind kind;
    Atom *wrapper_label;
    char *wrapper_canonical;
} PPSemanticMaskV1Action;

typedef struct {
    Atom *definition;
    char *definition_canonical;
    Atom *label;
    char *label_canonical;
    uint32_t tag;
} PPSemanticMaskV1RootLink;

/*
 * An annotated semantic-mask answer set and its exact recognition erasure.
 * edge_actions is parallel to erased.nfa.edges; zero-width edges carry
 * UINT32_MAX, while consuming edges index actions.
 */
typedef struct {
    RSNFAV1Plan erased;
    PPSemanticMaskV1Action *actions;
    uint32_t *edge_actions;
    uint32_t action_len;
    PPSemanticMaskV1RootLink *root_links;
    uint32_t root_link_len;
    Arena transformed_arena;
} PPSemanticMaskNfaV1Plan;

/*
 * An action-local deterministic realization of a semantic-mask NFA.  It is
 * available only when every active consuming edge agrees on the action for
 * the current scalar.  Functionality with delayed output is a strictly
 * larger fragment and therefore remains an explicit build outcome rather
 * than being approximated here.
 */
typedef struct {
    RSDFAV1Program dfa;
    uint32_t *transition_actions;
    uint32_t transition_action_len;
    uint32_t action_len;
} PPSemanticMaskDfaV1Program;

typedef enum {
    PPSEMANTIC_MASK_DFA_V1_REJECTED = 0,
    PPSEMANTIC_MASK_DFA_V1_ACCEPTED = 1,
    PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT = 2
} PPSemanticMaskDfaV1RunOutcome;

typedef struct {
    PPSemanticMaskDfaV1RunOutcome outcome;
    uint32_t action_len;
    uint64_t work_item_len;
} PPSemanticMaskDfaV1RunResult;

/*
 * One reverse witness for a deterministic recognition transition.  Given the
 * selected NFA state after consuming a scalar, it recovers a compatible NFA
 * state before that scalar and the unique semantic action emitted there.
 */
typedef struct {
    uint32_t target_nfa_state;
    uint32_t source_nfa_state;
    uint32_t action;
} PPSemanticMaskTraceV1Backstep;

/*
 * A deterministic realization of every functional, synchronous semantic-mask
 * NFA, including masks whose output requires finite lookahead.  The DFA first
 * recognizes the complete scalar sequence.  A reverse pass then selects one
 * accepting NFA path; exact functionality guarantees that its action sequence
 * is the only possible sequence for that input.
 */
typedef struct {
    RSDFAV1Program dfa;
    /*
     * A transition is action-local when every reverse witness carries the
     * same action.  UINT32_MAX marks transitions that still require exact
     * delayed-output reconstruction.  This is derived from backsteps and
     * revalidated structurally; it is never a second semantic authority.
     */
    uint32_t *transition_actions;
    uint32_t transition_action_len;
    uint32_t action_local_transition_len;
    uint32_t *backstep_offsets;
    PPSemanticMaskTraceV1Backstep *backsteps;
    uint32_t *accept_nfa_states;
    uint32_t backstep_len;
    uint32_t nfa_state_len;
    uint32_t action_len;
    uint32_t functionality_pair_len;
    uint64_t functionality_work_item_len;
} PPSemanticMaskTraceDfaV1Program;

void ppsemantic_mask_nfa_v1_plan_init(PPSemanticMaskNfaV1Plan *plan);
void ppsemantic_mask_nfa_v1_plan_free(PPSemanticMaskNfaV1Plan *plan);

bool ppsemantic_mask_nfa_v1_plan_load(
    const PPABIV1Pack *class_pack,
    Atom *const *answer_terms,
    size_t answer_len,
    PPSemanticMaskNfaV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

void ppsemantic_mask_dfa_v1_program_init(
    PPSemanticMaskDfaV1Program *program);
void ppsemantic_mask_dfa_v1_program_free(
    PPSemanticMaskDfaV1Program *program);

bool ppsemantic_mask_dfa_v1_program_validate(
    const PPSemanticMaskDfaV1Program *program,
    char *error_buf,
    size_t error_buf_size);

/*
 * Structural failure returns false.  A valid but non-action-local NFA
 * returns true with ANNOTATION_CONFLICT and leaves out unchanged.
 */
bool ppsemantic_mask_dfa_v1_program_build(
    const PPSemanticMaskNfaV1Plan *mask,
    uint32_t state_limit,
    uint32_t transition_limit,
    PPSemanticMaskDfaV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size);

/*
 * Determinize after applying a caller-supplied quotient to semantic actions.
 * action_quotient is parallel to mask.actions and every entry must be below
 * quotient_action_len.  This is useful when a consumer observes less than
 * the complete semantic value (for example, retained versus discarded source
 * scalars) and therefore can soundly merge otherwise conflicting wrappers.
 */
bool ppsemantic_mask_dfa_v1_program_build_quotient(
    const PPSemanticMaskNfaV1Plan *mask,
    const uint32_t *action_quotient,
    uint32_t quotient_action_len,
    uint32_t state_limit,
    uint32_t transition_limit,
    PPSemanticMaskDfaV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute one exact scalar interval after its enclosing scalar view and this
 * program have been validated.  On acceptance, out_actions receives exactly
 * one semantic action per consumed scalar.
 */
bool ppsemantic_mask_dfa_v1_run_exact_prevalidated(
    const PPSemanticMaskDfaV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    uint32_t tag,
    uint64_t work_limit,
    uint32_t *out_actions,
    uint32_t action_capacity,
    PPSemanticMaskDfaV1RunResult *out,
    char *error_buf,
    size_t error_buf_size);

void ppsemantic_mask_trace_dfa_v1_program_init(
    PPSemanticMaskTraceDfaV1Program *program);
void ppsemantic_mask_trace_dfa_v1_program_free(
    PPSemanticMaskTraceDfaV1Program *program);

bool ppsemantic_mask_trace_dfa_v1_program_validate(
    const PPSemanticMaskTraceDfaV1Program *program,
    char *error_buf,
    size_t error_buf_size);

/*
 * Build a functionality-certified trace carrier.  Functionality is decided
 * exactly for every tag before the carrier is published; bounded outcomes are
 * explicit build failures, never silently accepted approximations.
 */
bool ppsemantic_mask_trace_dfa_v1_program_build(
    const PPSemanticMaskNfaV1Plan *mask,
    uint32_t state_limit,
    uint32_t transition_limit,
    uint32_t functionality_pair_limit,
    uint64_t functionality_work_limit,
    PPSemanticMaskTraceDfaV1Program *out,
    RSDFAV1BuildOutcome *outcome,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute one complete scalar interval.  out_actions may be reused as the
 * forward transition scratch because each entry is overwritten only after its
 * transition witness has been consumed by the reverse pass.
 */
bool ppsemantic_mask_trace_dfa_v1_run_exact_prevalidated(
    const PPSemanticMaskTraceDfaV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    uint32_t tag,
    uint64_t work_limit,
    uint32_t *out_actions,
    uint32_t action_capacity,
    PPSemanticMaskDfaV1RunResult *out,
    char *error_buf,
    size_t error_buf_size);

#endif
