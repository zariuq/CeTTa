#ifndef CETTA_PETTA_SEARCH_MACHINE_H
#define CETTA_PETTA_SEARCH_MACHINE_H

#include "eval.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "search_machine.h"

/*
 * PeTTa's relational control is represented by an explicit heap machine.
 * The machine owns clause choice, continuation order, rollback, and answer
 * projection.  Shared evaluator operations enter only through the host
 * callbacks below; they never own a PeTTa choice point.
 */

typedef enum {
    PETTA_MACHINE_HOST_NONE = 0,
    PETTA_MACHINE_HOST_STRICT_APPLICATION,
    PETTA_MACHINE_HOST_READY_APPLICATION,
    /*
     * The host explicitly supersedes a machine-native form for a selected
     * semantic profile.  Unlike ordinary READY_APPLICATION, this mode is
     * consulted before the machine's direct-form dispatch.
     */
    PETTA_MACHINE_HOST_READY_OVERRIDE,
    PETTA_MACHINE_HOST_STRICT_RELATIONAL_EXTENSION,
    /*
     * The shared host owns the intrinsic cases, while explicit PeTTa
     * equations extend the same relation.  Host answers are enumerated
     * first; ordinary backtracking then reaches explicit relation clauses.
     */
    PETTA_MACHINE_HOST_READY_RELATIONAL_EXTENSION,
} PettaMachineHostMode;

typedef enum {
    PETTA_MACHINE_FOLD_NOT_APPLICABLE = 0,
    PETTA_MACHINE_FOLD_VALUE,
    PETTA_MACHINE_FOLD_INTERRUPTED,
} PettaMachineFoldResult;

typedef enum {
    PETTA_MACHINE_BOUNDARY_ACCEPTED = 0,
    PETTA_MACHINE_BOUNDARY_REFUTED,
    PETTA_MACHINE_BOUNDARY_FAULT,
} PettaMachineBoundaryResult;

/* Language-owned analyses are installed explicitly on a machine instance.
 * The machine must not consult ambient profile state to decide whether a
 * semantic analysis participates in evaluation. */
typedef enum {
    PETTA_MACHINE_ANALYSIS_NONE = 0u,
    PETTA_MACHINE_ANALYSIS_TYPE_OBLIGATIONS = 1u << 0,
} PettaMachineAnalysisCapability;

/* Exact, provider-defined mutable authority consulted by semantic analyses
 * in addition to the root Space.  The machine compares this vector but does
 * not interpret its components. */
#define PETTA_MACHINE_AUTHORITY_WORD_CAPACITY 8u
typedef struct {
    uint64_t words[PETTA_MACHINE_AUTHORITY_WORD_CAPACITY];
    uint8_t length;
} PettaMachineAuthorityToken;

/* A collection producer lends each fully evaluated item to a synchronous,
 * side-effect-free consumer.  The consumer must not retain item beyond the
 * callback.  A declined producer invalidates all consumer state accumulated
 * by that attempt, so callers use a private transactional accumulator. */
typedef bool (*PettaMachineBorrowedItemConsumer)(
    void *context, Atom *item);

typedef struct {
    void *context;
    uint32_t analysis_capabilities;
    /* Wall-clock sampling is opt-in so normal evaluation pays no clock cost. */
    bool measure_stats;
    /*
     * Called immediately before a machine transition.  Returning false
     * suspends without consuming the pending goal, so the same machine can
     * resume when its caller restores a budget or clears cancellation.
     */
    bool (*permit_transition)(void *context);
    PettaMachineHostMode (*classify)(
        void *context, Space *space, Atom *expression);
    Space *(*resolve_space)(
        void *context, Space *root_space, Arena *arena,
        Atom *reference);
    bool (*evaluate)(
        void *context, Space *space, Arena *arena, Atom *expression,
        const Bindings *environment, OutcomeSet *outcomes);
    /* Arguments have reached their translated values.  A selected semantic
     * profile may now enforce only those boundary facts consumed by its
     * committed cardinality proof. */
    PettaMachineBoundaryResult (*validate_ready_call)(
        void *context, Space *space, Atom *call,
        char *diagnostic, size_t diagnostic_size);
    /* Snapshot every non-Space mutable authority consulted by an installed
     * analysis.  The returned vector is an exact dependency receipt. */
    bool (*semantic_authority_token)(
        void *context, PettaMachineAuthorityToken *token);
    /* A generated determinate-fold program may own a lexical fold without
     * constructing one goal and accumulator variable per input item. */
    PettaMachineFoldResult (*foldl_single_result)(
        void *context, Space *space, Arena *arena,
        Atom *items, Atom *initial,
        Atom *accumulator_binder, Atom *item_binder,
        Atom *step_expression, const Bindings *environment,
        Atom **result_out);
    /* Pull a determinate, effect-free collection without materializing its
     * spine.  The callback is the consumer algebra; length is only its first
     * use.  NOT_APPLICABLE leaves canonical evaluation authoritative. */
    PettaMachineFoldResult (*pull_collection_single_result)(
        void *context, Space *space, Atom *producer,
        const Bindings *environment,
        PettaMachineBorrowedItemConsumer consume_item,
        void *consumer_context);
    /*
     * Named-state arguments have already been evaluated by the PeTTa
     * machine.  The host owns only registry lookup/mutation; it must not
     * recursively reinterpret the ready value through another evaluator.
     * Outcome environments may contain only substitutions learned by the
     * operation.  The machine retains the input environment; legacy hosts
     * that return it again are reduced to an exact-prefix delta before use.
     */
    bool (*named_state)(
        void *context, Space *space, Arena *arena, PeTTaForm form,
        Atom *name, Atom *value,
        const Bindings *environment, OutcomeSet *outcomes);
    PettaSpecializeResult (*prepare_call)(
        void *context, Space *space, Arena *result_arena,
        Atom *call, Atom **prepared_call);
    /* Execute a closed, revision-pinned determinate pure call through the
     * shared generated machine.  NULL declines to canonical clause search. */
    Atom *(*execute_prepared_pure_call)(
        void *context, Space *space, Arena *result_arena,
        Atom *prepared_call);
    PeTTaNamedArity (*foreign_named_arity)(
        void *context, SymbolId head, CettaExprLen supplied);
    /* Same classification including plan-time auto-resolved engine names;
     * consulted only for occurrences whose compiled plan is a call. */
    PeTTaNamedArity (*foreign_named_arity_resolved)(
        void *context, SymbolId head, CettaExprLen supplied);
    /* Engine-probing variant for call-by-construction sites (symbol-space
     * match): may register the name as auto-resolved on first proof. */
    PeTTaNamedArity (*foreign_named_arity_resolving)(
        void *context, SymbolId head, CettaExprLen supplied);
    bool (*foreign_call)(
        void *context, Arena *arena,
        Atom *expression, Atom *expected,
        const Bindings *environment, OutcomeSet *outcomes,
        bool *recognized);
    bool (*clause_snapshot)(
        void *context, Space *space, SymbolId head,
        PettaClauseCandidate **candidates,
        size_t *candidate_count,
        PettaClauseSnapshotStats *stats);
    bool (*translator_rule_contains)(
        void *context, SymbolId head);
    bool (*translator_rule_set)(
        void *context, SymbolId head, bool enabled);
    bool (*tabled_relation_contains)(
        void *context, SymbolId head, CettaExprLen arity);
    /*
     * A declared table is an optimization request.  If this callback
     * cannot prove the current relation effect-free, the machine executes
     * the ordinary relation instead of caching or replaying effects.
     */
    bool (*tabled_relation_admissible)(
        void *context, Space *space,
        SymbolId head, CettaExprLen arity);
    bool (*tabled_relation_set)(
        void *context, SymbolId head, CettaExprLen arity,
        bool enabled);
    /*
     * Transactions are explicit machine delimiters.  The host owns the
     * mutable-resource snapshot, while the machine owns relational control:
     * the first successful body path commits and body exhaustion rolls back.
     * A begun transaction must remain valid across machine suspension.
     */
    bool (*transaction_begin)(
        void *context, Space *space, Arena *arena,
        void **transaction, Space **transaction_space);
    bool (*transaction_commit)(
        void *context, void *transaction);
    void (*transaction_rollback)(
        void *context, void *transaction);
    bool (*mutex_acquire)(
        void *context, Arena *arena, Atom *name,
        void **mutex);
    void (*mutex_release)(
        void *context, void *mutex);
} PettaMachineHost;

typedef enum {
    PETTA_MACHINE_STEP_ANSWER = 0,
    PETTA_MACHINE_STEP_EXHAUSTED,
    PETTA_MACHINE_STEP_INVALIDATED,
    PETTA_MACHINE_STEP_CAPACITY,
    PETTA_MACHINE_STEP_HOST_ERROR,
    PETTA_MACHINE_STEP_SUSPENDED,
} PettaMachineStep;

typedef struct PettaMachineImpl PettaMachineImpl;

typedef struct {
    PettaMachineImpl *impl;
} PettaMachine;

bool petta_machine_init(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const Bindings *base_environment,
    const PettaMachineHost *host);

bool petta_machine_init_with_plan(
    PettaMachine *machine, Space *space, Arena *answer_arena,
    Atom *query, const PettaPlanNode *plan,
    const Bindings *base_environment,
    const PettaMachineHost *host);

PettaMachineStep petta_machine_next(
    PettaMachine *machine, Atom **answer, Bindings *environment);

/* A native typecheck-v2 refutation is semantic, not ordinary search
 * exhaustion.  The evaluator reads this diagnostic only after the machine
 * terminates and turns it into the profile's process-level rejection. */
const char *petta_machine_typecheck_diagnostic(
    const PettaMachine *machine);
int petta_machine_typecheck_exit_code(const PettaMachine *machine);

void petta_machine_destroy(PettaMachine *machine);

/*
 * Introspection used by complexity gates.  These counters describe semantic
 * machine work rather than wall time.
 */
typedef struct {
    uint64_t transitions;
    uint64_t solve_goal_transitions;
    uint64_t call_goal_transitions;
    uint64_t unify_goal_transitions;
    uint64_t collection_goal_transitions;
    uint64_t control_goal_transitions;
    uint64_t host_goal_transitions;
    uint64_t other_goal_transitions;
    uint64_t clause_snapshot_calls;
    uint64_t clause_snapshot_cache_hits;
    uint64_t clause_snapshot_live_occurrences;
    uint64_t clause_snapshot_records_examined;
    uint64_t clause_snapshot_equality_checks;
    uint64_t clause_snapshot_alpha_checks;
    uint64_t clause_snapshot_candidates;
    uint64_t clause_candidates;
    uint64_t clause_candidates_shape_pruned;
    uint64_t clause_match_attempts;
    uint64_t clause_match_allocated_bytes;
    uint64_t match_candidates;
    uint64_t unification_calls;
    uint64_t unification_failures;
    uint64_t unification_binding_writes;
    uint64_t unification_allocated_bytes;
    uint64_t clause_binding_merge_calls;
    uint64_t clause_binding_merge_source_items;
    uint64_t clause_binding_merge_logical_writes;
    uint64_t clause_binding_merge_failures;
    uint64_t outcome_binding_merge_calls;
    uint64_t outcome_binding_merge_source_items;
    uint64_t outcome_binding_merge_logical_writes;
    uint64_t outcome_binding_merge_failures;
    uint64_t outcome_prefix_factor_attempts;
    uint64_t outcome_prefix_factor_successes;
    uint64_t outcome_prefix_logical_items_elided;
    uint64_t outcome_prefix_residual_items;
    uint64_t binding_apply_calls;
    uint64_t binding_apply_rewrites;
    uint64_t binding_apply_allocated_bytes;
    uint64_t atom_copy_calls;
    uint64_t atom_copy_allocated_bytes;
    uint64_t atom_freshen_calls;
    uint64_t atom_freshen_allocated_bytes;
    uint64_t specializer_prepare_calls;
    uint64_t specializer_prepare_filtered;
    uint64_t specializer_prepare_relevance_bounded;
    uint64_t specializer_prepare_rewritten;
    uint64_t specializer_prepare_unchanged;
    uint64_t specializer_prepare_elapsed_ns;
    uint64_t choice_resumes;
    uint64_t choice_continuation_snapshots;
    uint64_t choice_continuation_items_copied;
    uint64_t choice_continuation_items_trailed;
    uint64_t deterministic_clause_choices_elided;
    uint64_t singleton_outcome_choices_elided;
    uint64_t rollbacks;
    uint64_t answers;
    uint64_t deterministic_heap_collections;
    uint64_t deterministic_minor_heap_collections;
    uint64_t deterministic_major_heap_collections;
    uint64_t deterministic_goal_roots_scanned;
    uint64_t deterministic_heap_bytes_promoted;
    uint64_t deterministic_heap_bytes_reclaimed;
    uint64_t deterministic_binding_entries_discarded;
    uint64_t choice_binding_collections;
    uint64_t choice_binding_items_discarded;
    uint64_t choice_trail_entries_discarded;
    uint64_t choice_heap_resets;
    uint64_t choice_heap_bytes_reclaimed;
    uint64_t table_lookups;
    uint64_t table_hits;
    uint64_t table_generator_rounds;
    uint64_t table_scc_completions;
    uint64_t table_answer_replays;
    uint64_t count_aggregate_answers;
    uint64_t count_aggregate_boundary_copies_avoided;
    uint64_t count_aggregate_match_folds;
    uint64_t count_aggregate_match_answers;
    uint64_t count_aggregate_let_fusions;
    uint64_t host_environment_entries_observed;
    uint64_t host_environment_entries_forwarded;
    size_t maximum_goal_depth;
    size_t maximum_choice_depth;
    size_t maximum_choice_continuation_trail;
    size_t maximum_nursery_live_bytes;
    size_t maximum_tenured_live_bytes;
    size_t maximum_heap_live_bytes;
    size_t maximum_binding_entries;
    size_t maximum_host_environment_entries_forwarded;
    uint64_t active_elapsed_ns;
    uint64_t time_to_first_answer_ns;
    uint64_t first_answer_transition;
} PettaMachineStats;

bool petta_machine_stats(
    const PettaMachine *machine, PettaMachineStats *stats);

#endif /* CETTA_PETTA_SEARCH_MACHINE_H */
