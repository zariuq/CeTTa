#ifndef CETTA_RHOCALC_CORE_H
#define CETTA_RHOCALC_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"
#include "owned_execution.h"

typedef struct CettaLibraryContext CettaLibraryContext;
typedef struct ResultSet ResultSet;

typedef enum {
    RHO_SCHEDULER_CANONICAL = 0,
    RHO_SCHEDULER_ROTATING = 1
} RhoSchedulerPolicy;

typedef enum {
    RHOCALC_SEMANTIC_PROFILE_STRICT_CORE = 0,
    RHOCALC_SEMANTIC_PROFILE_COST = 1,
    RHOCALC_SEMANTIC_PROFILE_RHOMETTA = 2
} RhocalcSemanticProfileId;

typedef struct {
    RhoSchedulerPolicy scheduler_policy;
    uint32_t reduction_limit;
    uint32_t thread_count;
    bool threaded;
} RhoRuntimeProfile;

typedef enum {
    RHOCALC_PAYLOAD_FAILURE_NONE = 0,
    RHOCALC_PAYLOAD_FAILURE_OWNED_EXPORT_ALIASED
} RhocalcPayloadFailure;

typedef enum {
    RHOCALC_PAYLOAD_PENDING,
    RHOCALC_PAYLOAD_DONE,
    RHOCALC_PAYLOAD_FAILED
} RhocalcPayloadStatus;

/* The provider owns the evaluator continuation, not the transactional world.
 * Its calls receive the rebound space/registry with the world's redirects
 * and library selected. The provider scopes its own evaluator registers.
 * A step appends complete result occurrences in their retained logical world;
 * a resource descriptor is read after DONE before promotion. PENDING never
 * means exhaustion.
 * A zero grant is inert. Open, steps and close may have separately declared
 * atomic costs. The enclosing caller supplies current source/authority scope. */
typedef struct RhocalcPayloadProvider {
    void *(*open)(Space *space, Arena *arena, Arena *persistent,
                  Registry *registry, Atom *expression);
    RhocalcPayloadStatus (*step)(void *job, uint64_t allowance,
                                  ResultSet *results);
    void (*close)(void *job);
    /* Optional descriptor after DONE in this exact occurrence's logical world.
     * The provider retains that world and all descriptor storage until close.
     * A successful read is coherent for both slots; false is refusal, never
     * permission to use the cell's physical fallback. The cell identity is
     * unchanged, including the affine check across result occurrences. */
    bool (*result_state)(void *job, CettaCount result_index, StateCell *cell,
                         Atom **value, Atom **content_type);
} RhocalcPayloadProvider;

typedef struct {
    /* Equation context. An explicit registry &self keeps its original role
     * through resource rebinding; only an absent binding uses this space. */
    Space *space;
    Registry *registry;
    Arena *persistent_arena;
    CettaLibraryContext *library_context;
    /* Optional invocation-owned diagnostic, separate from language results.
     * Workers may report a failure; the caller reads it after they join. */
    _Atomic(RhocalcPayloadFailure) *payload_failure;
    /* NULL keeps the synchronous evaluator. A supplied table and the runtime
     * owners it uses remain live through the run; it grants no authority. */
    const RhocalcPayloadProvider *payload_provider;
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

typedef struct RhocalcRun RhocalcRun;

typedef enum {
    RHOCALC_RUN_PENDING,
    RHOCALC_RUN_DONE,
    RHOCALC_RUN_FAILED
} RhocalcRunStatus;

typedef struct {
    uint64_t driver_steps;
    /* Details of the retained canonical cursor only. Atomic/rotating
     * selection does not report its frontier work through these fields. */
    uint64_t selector_steps;
    uint64_t matching_pairs;
    uint64_t body_candidates;
    uint64_t completed_scans;
    uint64_t comparison_component_probes;
    uint64_t comparison_text_probes;
    uint64_t preparation_steps;
    uint64_t channel_scan_probes;
    /* Retained preparation, including private export normalization. It includes memo
     * probes and retained table growth. Normalization also includes key actions;
     * these overlapping diagnostics must not be added as a total invoice.
     * Payload rendering and allocation retain their separate atomic prices. */
    uint64_t validation_steps, normalization_steps, key_steps, key_bytes;
    /* Semantic-task transitions and the actual delegated binding-walk actions.
     * Both overlap driver/selector transitions; normalization remains in its
     * existing counters. These are work currencies, not elapsed-time bounds. */
    uint64_t substitution_steps, substitution_apply_steps, substitution_memo_steps;
    uint64_t construction_children, construction_bytes;
    /* Subsets of the phase totals above, never extra invoice units. Physical
     * hash placement can change these when an equivalent graph is relocated;
     * exact split accounts require the same prepared representation. */
    uint64_t validation_memo_steps, normalization_memo_steps, selector_memo_steps;
    /* Final canonical reaction construction. These overlap driver/selector
     * and preparation totals; copy/merge actions and child writes overlap
     * build actions. Bytes describe requested expression storage, not RSS. */
    uint64_t reaction_build_steps, reaction_copy_steps, reaction_merge_steps;
    uint64_t reaction_children, reaction_bytes;
    /* Copy-session actions within driver_steps. Opening/finalizing transfers
     * and retiring a generation have their separate atomic service prices. */
    uint64_t compaction_copy_steps, export_copy_steps;
    uint32_t reductions_taken;
} RhocalcRunAccount;

/* Address in the normalized source process after source_reductions commits.
 * This describes an actual committed canonical COMM, not authority, an entire
 * trace, or a provisional winner. Body and candidate ordinals retain duplicate
 * result occurrences. */
typedef struct {
    uint32_t source_reductions;
    uint32_t send_component, recv_component, body_index;
    uint64_t candidate_ordinal;
} RhocalcCommAddress;

/* Sequential strict-core/Rhometta execution with an owned candidate cursor.
 * Opening copies the input and the context record; the caller retains the
 * result arena, symbol environment, input's grounded resource owners and the
 * context's referenced runtime owners until close. Context copying grants no
 * new authority. The context's optional
 * payload_failure pointer is not retained: read the run-owned diagnostic below.
 * Synchronous-result correspondence requires an unchanged referenced world,
 * registry and library for the run. Opening does not snapshot these services
 * or replace the caller's current-authority/publication checks.
 *
 * Poll allowance counts driver transitions, including retained grammar and
 * initial normalization actions and canonical selector probes. Initial
 * normalization retains child traversal, flattening, process/name key bytes,
 * alpha lookup, stable sorting and wide expression assembly. Source/role
 * memo lookup, table clearing and collision rehashing retain their positions;
 * completed results share syntax storage without removing occurrences.
 * Hash-probe work depends on all prepared node addresses, including private
 * normalized nodes. Exact split accounts require the same prepared graph and
 * provider choices, not just equivalent original inputs. Memo subcounts expose
 * these physical actions without subtracting them from the actual invoice.
 * Component keys, endpoint merge sorting, channel-group scans, body-row
 * preparation, component merging and contextual reaction-key comparison also
 * retain their inner positions. A completed key carries its binder displacement;
 * quotation does not change its parent's counter. A comparison retains one
 * side's reply while the other is pending. Selected-body copying/remapping
 * completes before replacing the previous winner. Final canonical successor
 * construction retains its body copy, row merge and child assembly; only a
 * completed root becomes the next process. A normalized variable-free body
 * preserves its syntax sharing under literal substitution. Semantic substitution
 * retains scoped capture queries, alpha-renaming, normalized-name replies,
 * matched-only drop exposure, child assembly and the existing binding walker
 * for raw payload data. Each deferred result is classified before normalization;
 * definite nonprocess data is wrapped unchanged, while refused checking fails
 * the invocation. The actual normalized reply then determines substitution.
 * Deferred payloads still run and retain every result occurrence.
 * Compaction and export retain the existing graph-copy session; the old
 * generation remains live until compaction commits. Canonical export keeps
 * that copied graph; other orderings use the retained normalizer. Payload atom
 * rendering, scalar/name construction, allocation and cleanup retain atomic
 * services, including interning constructors, generation
 * retirement and control-owner destruction after a completed transfer.
 * The eager-copy opener retains its initial copy cost; the held
 * opener below avoids that copy with a stronger input-lifetime contract.
 * Payload transactions
 * retain setup/evaluation/promotion phases; an explicit provider can retain
 * evaluator and return traversal inside evaluation. Its own atomic services
 * remain separately priced, and the default evaluator is synchronous.
 * Driver counts are NOT a bound on their work or wall-clock cost; closing
 * also retains its separate cleanup cost.
 * Synchronous interning profiles keep their admitted atomic constructor;
 * incremental draft assembly requires a noninterning arena.
 * Rotating selection is one atomic frontier transition. Threaded runs continue
 * to use the existing synchronous/threaded entry points.
 *
 * Zero allowance is inert. No provisional result is exported. A result is
 * available only after DONE. Its atom storage outlives close in the result
 * arena; grounded references retain their existing ownership contract. Their
 * owners, including persistent_arena for escaping payload spaces/states, must
 * outlive all result use, not merely the run. Export does not acquire them.
 * Runs may be interleaved on one thread; each owns its error and payload status.
 * Concurrent use of a run, or moving it between evaluator threads, is invalid.
 * Advancement executes the payload handler. Closing retires owned cursor state;
 * it does not roll back effects already performed by that handler. */
RhocalcRun *rhocalc_run_open(Arena *arena, Atom *proc,
                            const RhoRuntimeProfile *profile,
                            const RhocalcEvalContext *eval_context);
/* Fixed-control opening without an input copy. In addition to the contracts
 * above, the caller holds the COMPLETE immutable input graph, its arenas and
 * resource/intern owners until close. Holding only the root arena is not
 * sufficient. The service uses this when its invocation already holds that
 * graph. Zero polls neither validate nor normalize borrowed syntax. */
RhocalcRun *rhocalc_run_open_held(Arena *arena, Atom *proc,
                                 const RhoRuntimeProfile *profile,
                                 const RhocalcEvalContext *eval_context);
RhocalcRunStatus rhocalc_run_poll(RhocalcRun *run, uint64_t allowance,
                                  uint64_t *used);
bool rhocalc_run_result(const RhocalcRun *run, RhoReductionResult *out);
RhocalcRunAccount rhocalc_run_account(const RhocalcRun *run);
bool rhocalc_run_last_comm(const RhocalcRun *run, RhocalcCommAddress *out);
RhocalcPayloadFailure rhocalc_run_payload_failure(const RhocalcRun *run);
const char *rhocalc_run_error(const RhocalcRun *run);
/* Cancel and release owned storage, retaining its exact cleanup position.
 * Zero allowance is inert. Normalizer keys and frames retire individually;
 * other owner/copy/payload/arena services currently request atomic permission.
 * ATOMIC keeps the run and its remaining owners intact. A successful final
 * action clears *run; the caller holds context/resource dependencies until
 * then. A retiring run cannot be advanced or publish a new result. Units
 * count cleanup actions, not allocator work or a physical latency bound. */
CettaOwnedReleaseStep rhocalc_run_release_poll(RhocalcRun **run,
    uint64_t allowance, bool allow_atomic, uint64_t *used);
void rhocalc_run_close(RhocalcRun *run);

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
                                      uint64_t fuel,
                                      uint64_t search_budget);
Atom *rhocalc_cost_causal_trace_parallel_expr(Arena *arena, Atom *proc,
                                              uint32_t thread_count);
Atom *rhocalc_cost_causal_prefix_parallel_expr(
    Arena *arena, Atom *proc, uint64_t fuel,
    uint64_t search_budget, uint32_t thread_count);
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

/* Project the payloads of top-level inert rho:val components from a residual.
 * The returned expression preserves their occurrence order. */
Atom *rhocalc_observe_top_level_values(Arena *arena, Atom *residual);
/* The same ordered observation as the synchronous entry, retaining one
 * traversal position per poll transition. The caller holds the immutable
 * residual and result arena until close. Zero allowance is inert; allocation
 * and final expression construction have separate atomic costs. */
typedef struct RhocalcValueObservation RhocalcValueObservation;
RhocalcValueObservation *rhocalc_values_open(Arena *arena, Atom *residual);
RhocalcRunStatus rhocalc_values_poll(RhocalcValueObservation *observation,
    uint64_t allowance, uint64_t *used);
Atom *rhocalc_values_result(const RhocalcValueObservation *observation);
void rhocalc_values_close(RhocalcValueObservation *observation);

#ifdef CETTA_TEST_HOOKS
bool rhocalc_payload_map_capacity_selftest(void);
bool rhocalc_abt_substitution_correspondence_selftest(Arena *arena);
bool rhocalc_atom_text_key_correspondence_selftest(Arena *arena);
#endif

#endif /* CETTA_RHOCALC_CORE_H */
