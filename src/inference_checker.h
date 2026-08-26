#ifndef CETTA_INFERENCE_CHECKER_H
#define CETTA_INFERENCE_CHECKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

/*
 * Native checker for serialized admitted finite inference presentations.
 *
 * The serialized input vocabulary is the language-neutral
 * GPresentation/GRule/GRuleInst vocabulary used by the executable generic
 * inference checker.  A presentation is admitted once, then proof actions are
 * consumed incrementally without retaining an action list or proof tree.
 * GPresentationV1 carries side-condition and conversion fields exactly.
 * Its admitted generic side conditions execute through the registered ABT
 * providers; malformed or unknown conditions fail closed and are never
 * erased.
 */

typedef enum {
    CETTA_INFERENCE_OK = 0,
    CETTA_INFERENCE_INVALID_PRESENTATION,
    CETTA_INFERENCE_MALFORMED_PROOF,
    CETTA_INFERENCE_UNKNOWN_RULE,
    CETTA_INFERENCE_INVALID_ARGUMENTS,
    CETTA_INFERENCE_PREMISE_MISMATCH,
    CETTA_INFERENCE_EMPTY_STACK,
    CETTA_INFERENCE_BAD_REFERENCE,
    CETTA_INFERENCE_FINAL_MISMATCH,
    CETTA_INFERENCE_RESOURCE_LIMIT
} CettaInferenceStatus;

typedef uint32_t CettaInferenceRuleHandle;

#define CETTA_INFERENCE_RULE_HANDLE_NONE UINT32_MAX

typedef struct CettaInferenceChecker CettaInferenceChecker;

typedef struct {
    size_t max_nodes;
    size_t max_depth;
} CettaInferenceReplayLimits;

typedef struct {
    size_t nodes;
    size_t max_depth_observed;
} CettaInferenceReplayStats;

typedef struct {
    const CettaInferenceChecker *checker;
    Arena *arena;
    Atom **stack;
    size_t stack_len;
    size_t stack_capacity;
    Atom **saved;
    size_t saved_len;
    size_t saved_capacity;
} CettaInferenceTrace;

const char *cetta_inference_status_name(CettaInferenceStatus status);

/*
 * Validate and index a canonical GPresentationV1 value.  Legacy
 * GPresentation remains accepted only as the explicitly side-condition-free
 * version-zero carrier.  The checker borrows immutable pattern atoms from
 * presentation, so presentation must outlive it.
 */
CettaInferenceStatus cetta_inference_checker_create(
    Atom *presentation,
    CettaInferenceChecker **out,
    char *error_buf,
    size_t error_buf_size);

void cetta_inference_checker_destroy(CettaInferenceChecker *checker);

size_t cetta_inference_checker_rule_count(
    const CettaInferenceChecker *checker);

/*
 * Extend an admitted presentation without rebuilding it.  Constructor and
 * rule names remain globally unique, and rule handles are stable for the
 * lifetime of the checker.  Added rules use the admitted presentation's wire
 * version and borrow their canonical GRule/GRuleV1 atom, which must therefore
 * outlive the checker just like the initial presentation.
 */
CettaInferenceStatus cetta_inference_checker_add_constructor(
    CettaInferenceChecker *checker,
    const char *name,
    uint64_t arity,
    char *error_buf,
    size_t error_buf_size);

CettaInferenceStatus cetta_inference_checker_add_rule(
    CettaInferenceChecker *checker,
    Atom *rule,
    CettaInferenceRuleHandle *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Activation is separate from admission so a scoped source layer can make a
 * hypothesis unavailable without invalidating its stable rule handle.  Static
 * and newly admitted rules start active.
 */
CettaInferenceStatus cetta_inference_checker_set_rule_active(
    CettaInferenceChecker *checker,
    CettaInferenceRuleHandle rule,
    bool active,
    char *error_buf,
    size_t error_buf_size);

bool cetta_inference_checker_rule_is_active(
    const CettaInferenceChecker *checker,
    CettaInferenceRuleHandle rule);

CettaInferenceRuleHandle cetta_inference_checker_find_rule(
    const CettaInferenceChecker *checker,
    const char *rule_id);

/*
 * A trace owns only its stack and saved-value index.  Instantiated judgments
 * are allocated in arena and may refer to longer-lived presentation or
 * argument atoms.  The caller may discard the arena after finishing a proof.
 */
void cetta_inference_trace_init(
    CettaInferenceTrace *trace,
    const CettaInferenceChecker *checker,
    Arena *arena);

void cetta_inference_trace_reset(CettaInferenceTrace *trace);

void cetta_inference_trace_free(CettaInferenceTrace *trace);

CettaInferenceStatus cetta_inference_trace_apply(
    CettaInferenceTrace *trace,
    CettaInferenceRuleHandle rule,
    Atom *const *arguments,
    size_t argument_count,
    char *error_buf,
    size_t error_buf_size);

CettaInferenceStatus cetta_inference_trace_apply_named(
    CettaInferenceTrace *trace,
    const char *rule_id,
    Atom *const *arguments,
    size_t argument_count,
    char *error_buf,
    size_t error_buf_size);

CettaInferenceStatus cetta_inference_trace_save(
    CettaInferenceTrace *trace,
    char *error_buf,
    size_t error_buf_size);

CettaInferenceStatus cetta_inference_trace_reference(
    CettaInferenceTrace *trace,
    size_t saved_index,
    char *error_buf,
    size_t error_buf_size);

CettaInferenceStatus cetta_inference_trace_finish(
    const CettaInferenceTrace *trace,
    Atom *goal,
    char *error_buf,
    size_t error_buf_size);

/*
 * Check the exact Lean RawProof wire form:
 *
 *   (GProof (GRuleInst "rule" arguments) children)
 *
 * Children are replayed left-to-right and remain structurally scoped to
 * their parent.  This operation performs no proof search.  A zero limit uses
 * the conservative implementation default for that dimension.
 */
CettaInferenceStatus cetta_inference_check_raw_proof(
    Atom *presentation,
    Atom *goal,
    Atom *proof,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

/*
 * Replay the same RawProof wire form against an already admitted checker.
 * The checker is immutable during replay and may therefore be reused across
 * independent traces.  This is the production path for revisioned authority
 * packages; the presentation-taking helper above remains the one-shot
 * convenience entry point.
 */
CettaInferenceStatus cetta_inference_checker_check_raw_proof(
    const CettaInferenceChecker *checker,
    Atom *goal,
    Atom *proof,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

/*
 * Replay the exact versioned chronological CertificateGSLT carriers:
 *
 *   (GProofDAG 1 nodes root-id target)
 *   (GDNode id (GRuleInst "rule" arguments) references)
 *
 * Version 2 retains the same judgment while sharing every Pattern subtree:
 *
 *   (GProofDAG 2 pattern-nodes nodes root-id target-pattern-id)
 *   (GPatternNode id pattern-key)
 *   (GDNode id (GRuleRefs "rule" argument-pattern-ids) references)
 *
 * Node references are `(GRNode id)` values in canonical LNil/LCons lists.
 * `(GRPremise index)` is part of the general open-article carrier but is
 * rejected here because NIK Check submits a closed article.  Version, target,
 * unique node identity, backward references, ordered premises, and root are
 * all checked.  Version-2 Pattern ids are chronological ordinals; their keys
 * are GPKBVar, GPKFVar, GPKApply, GPKLambda, GPKMultiLambda, GPKSubst, or
 * GPKCollection.  Pattern materialization is untrusted transport: replay is
 * still decided by the same admitted checker.  The admitted checker remains
 * immutable and reusable.
 */
CettaInferenceStatus cetta_inference_checker_check_dag_article(
    const CettaInferenceChecker *checker,
    Atom *goal,
    Atom *article,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

CettaInferenceStatus cetta_inference_check_dag_article(
    Atom *presentation,
    Atom *goal,
    Atom *article,
    CettaInferenceReplayLimits limits,
    CettaInferenceReplayStats *stats,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

/*
 * Compatibility entry for the canonical GaNil/GaCons action encoding used by
 * the executable reference checker.  It still executes through the online
 * trace API; the action list is an input adapter, not retained checker state.
 */
CettaInferenceStatus cetta_inference_check_trace_atoms(
    Atom *presentation,
    Atom *goal,
    Atom *actions,
    Arena *arena,
    char *error_buf,
    size_t error_buf_size);

#endif
