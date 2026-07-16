#ifndef CETTA_RHOCALC_CORE_H
#define CETTA_RHOCALC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"

typedef struct CettaLibraryContext CettaLibraryContext;

typedef enum {
    RHO_SCHEDULER_CANONICAL = 0,
    RHO_SCHEDULER_ROTATING = 1
} RhoSchedulerPolicy;

typedef enum {
    RHOCALC_SEMANTIC_PROFILE_STRICT_CORE = 0,
    RHOCALC_SEMANTIC_PROFILE_COST = 1
} RhocalcSemanticProfileId;

typedef struct {
    RhoSchedulerPolicy scheduler_policy;
    uint32_t reduction_limit;
    uint32_t thread_count;
    bool threaded;
} RhoRuntimeProfile;

typedef struct {
    Space *space;
    Registry *registry;
    Arena *persistent_arena;
    CettaLibraryContext *library_context;
} RhocalcEvalContext;

typedef enum {
    RHOCALC_REDUCTION_QUIESCENT = 0,
    RHOCALC_REDUCTION_LIMIT_EXHAUSTED
} RhoReductionStatus;

typedef struct {
    Atom *residual;
    uint32_t reductions_taken;
    RhoReductionStatus status;
} RhoReductionResult;

const char *rhocalc_semantic_profile_name(RhocalcSemanticProfileId profile);
bool rhocalc_process_well_formed_with_semantic_profile(
    Atom *proc, RhocalcSemanticProfileId semantic_profile);
bool rhocalc_process_well_formed(Atom *proc);
bool rhocalc_process_well_formed_with_eval_payloads(Atom *proc);
const char *rhocalc_last_validation_error(void);
Atom *rhocalc_successor_frontier_expr_with_semantic_profile(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile);
Atom *rhocalc_successor_frontier_expr_with_eval_context(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context);
Atom *rhocalc_successor_frontier_expr_with_semantic_profile_and_eval_context(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhocalcEvalContext *eval_context);
Atom *rhocalc_quiescent_frontier_expr(Arena *arena, Atom *proc);
Atom *rhocalc_quiescent_frontier_expr_with_eval_context(
    Arena *arena, Atom *proc, const RhocalcEvalContext *eval_context);
Atom *rhocalc_successor_frontier_expr(Arena *arena, Atom *proc);
Atom *rhocalc_cost_step_frontier_expr(Arena *arena, Atom *proc);
Atom *rhocalc_cost_causal_trace_expr(Arena *arena, Atom *proc);
Atom *rhocalc_cost_causal_prefix_expr(Arena *arena, Atom *proc,
                                      uint64_t fuel);
bool rhocalc_reduce_to_quiescence_with_semantic_profile(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhoRuntimeProfile *profile, RhoReductionResult *out);
bool rhocalc_reduce_to_quiescence_with_eval_context(
    Arena *arena, Atom *proc, const RhoRuntimeProfile *profile,
    const RhocalcEvalContext *eval_context, RhoReductionResult *out);
bool rhocalc_reduce_to_quiescence_with_semantic_profile_and_eval_context(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhoRuntimeProfile *profile, const RhocalcEvalContext *eval_context,
    RhoReductionResult *out);
bool rhocalc_reduce_to_quiescence_with_profile(Arena *arena, Atom *proc,
                                               const RhoRuntimeProfile *profile,
                                               RhoReductionResult *out);
bool rhocalc_reduce_to_quiescence(Arena *arena, Atom *proc,
                                  uint32_t reduction_limit, RhoReductionResult *out);

#ifdef CETTA_TEST_HOOKS
bool rhocalc_payload_map_capacity_selftest(void);
#endif

#endif /* CETTA_RHOCALC_CORE_H */
