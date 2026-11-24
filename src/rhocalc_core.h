#ifndef CETTA_RHOCALC_CORE_H
#define CETTA_RHOCALC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"

/* The schedules a run may follow, shared with the tcalc lane
 * (lib/runner_axes.data): canonical or rotating. */
#include "generated/runner_axes.generated.h"

typedef struct CettaLibraryContext CettaLibraryContext;

typedef enum {
    RHOCALC_SEMANTIC_PROFILE_STRICT_CORE = 0,
    RHOCALC_SEMANTIC_PROFILE_COST = 1,
    RHOCALC_SEMANTIC_PROFILE_RHOMETTA = 2
} RhocalcSemanticProfileId;

typedef struct {
    RunSchedule scheduler_policy;
    uint32_t reduction_limit;
    uint32_t thread_count;
    bool threaded;
} RhoRuntimeProfile;

typedef enum {
    RHOCALC_PAYLOAD_FAILURE_NONE = 0,
    RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED
} RhocalcPayloadFailure;

typedef struct {
    Space *space;
    Registry *registry;
    Arena *persistent_arena;
    CettaLibraryContext *library_context;
    /* Optional invocation-owned diagnostic, separate from language results.
     * Workers may report a failure; the caller reads it after they join. */
    _Atomic(RhocalcPayloadFailure) *payload_failure;
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
/* Whether proc is a process of the semantic profile's grammar; when it is
 * not, rhocalc_last_validation_error() says why. */
bool rhocalc_process_well_formed(Atom *proc, RhocalcSemanticProfileId semantic_profile);
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
                                      uint64_t fuel,
                                      uint64_t search_budget);
Atom *rhocalc_cost_causal_trace_parallel_expr(Arena *arena, Atom *proc,
                                              uint32_t thread_count);
Atom *rhocalc_cost_causal_prefix_parallel_expr(
    Arena *arena, Atom *proc, uint64_t fuel,
    uint64_t search_budget, uint32_t thread_count);
/* A term to quiescence under a semantic profile (strict core, cost or
 * rhometta, which needs an evaluation context) and a runtime profile. */
bool rhocalc_reduce_to_quiescence(
    Arena *arena, Atom *proc, RhocalcSemanticProfileId semantic_profile,
    const RhoRuntimeProfile *profile, const RhocalcEvalContext *eval_context,
    RhoReductionResult *out);

/* Project the payloads of top-level inert rho:val components from a residual.
 * The returned expression preserves their occurrence order. */
Atom *rhocalc_observe_top_level_values(Arena *arena, Atom *residual);

/* What a test may watch in a threaded strict-core or rhometta run. */
typedef struct {
    /* Place each COMM's continuation at the right half of the supply of
     * the party that found the match instead of by the two parties'
     * supplies: the placement whose outcome depends on scheduling.  A
     * negative control only. */
    bool finder_placement;
    /* Each task draws a space name when it runs, which DRAWN sees, on the
     * worker that runs it. */
    bool draw_each_task;
    void (*drawn)(void *context, CettaGeneratedName name);
    /* At the end of a run, each live task (waiting on a channel or stuck),
     * with its supply and process, on the thread that ran the reduction. */
    void (*live)(void *context, const CettaNameSupply *supply, Atom *proc);
    void *context;
} RhocalcThreadedTestHooks;

/* The memory of the end of the last threaded run. */
typedef struct {
    uint64_t live_tasks;          /* waiting on a channel or stuck */
    uint64_t live_task_bytes;     /* what their regions hold */
    uint64_t live_task_reserved;  /* the blocks of their regions */
    uint64_t regions_made;        /* regions allocated during the run */
    uint64_t regions_stocked;     /* released regions kept for reuse */
    uint64_t scratch_reserved;    /* the blocks of the workers' scratch */
    uint64_t channels;
} RhocalcThreadedCensus;

#ifdef CETTA_TEST_HOOKS
/* HOOKS for the threaded runs the calling thread runs next, NULL for none,
   and the census of the last one it ran. */
void rhocalc_threaded_test_hooks_set(const RhocalcThreadedTestHooks *hooks);
void rhocalc_threaded_last_census(RhocalcThreadedCensus *out);
bool rhocalc_payload_map_capacity_selftest(void);
bool rhocalc_abt_substitution_correspondence_selftest(Arena *arena);
bool rhocalc_structural_order_selftest(Arena *arena);
#endif

#endif /* CETTA_RHOCALC_CORE_H */
