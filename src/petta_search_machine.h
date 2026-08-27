#ifndef CETTA_PETTA_SEARCH_MACHINE_H
#define CETTA_PETTA_SEARCH_MACHINE_H

#include "eval.h"
#include "match_decision.h"
#include "nik_direct_authority.h"
#include "petta_analysis.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "search_machine.h"

/*
 * The relational operational fragment is represented by an explicit heap
 * machine.  The machine owns clause choice, continuation order, rollback, and
 * answer projection; language-owned Need, effect, and observation semantics
 * enter only through the host callbacks below.  PeTTa and Prime therefore
 * share this mechanism without either dialect naming its admission boundary.
 */

typedef enum {
    PETTA_MACHINE_HOST_NONE = 0,
    PETTA_MACHINE_HOST_STRICT_APPLICATION,
    /* Evaluate only argument zero.  Space operations use this mode because
     * their space expression is strict while atom payloads remain syntax. */
    PETTA_MACHINE_HOST_STRICT_FIRST_APPLICATION,
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

/* Admission concerns only the ordinary relational control path for a
 * revision-pinned Space and a known query head/arity.  It does not assert
 * determinism and never suppresses equation alternatives. */
typedef enum {
    PETTA_MACHINE_SPACE_QUERY_DEFER = 0,
    PETTA_MACHINE_SPACE_QUERY_ADMITTED,
    PETTA_MACHINE_SPACE_QUERY_INVALIDATED,
} PettaMachineSpaceQueryAdmission;

/* A narrower judgment than Space-query execution admission: this licenses
 * bypassing only higher-order call specialization.  Table, translator,
 * foreign, and ordinary equation dispatch remain independently authoritative
 * later in the ready-call transition. */
typedef enum {
    PETTA_MACHINE_QUERY_SPECIALIZATION_DEFER = 0,
    PETTA_MACHINE_QUERY_SPECIALIZATION_BYPASS,
    PETTA_MACHINE_QUERY_SPECIALIZATION_INVALIDATED,
} PettaMachineQuerySpecializationAdmission;

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
#define PETTA_MACHINE_AUTHORITY_WORD_CAPACITY \
    CETTA_NIK_DIRECT_AUTHORITY_TOKEN_WORD_CAPACITY
typedef CettaNikDirectAuthorityTokenV1 PettaMachineAuthorityToken;

/* Optional language-owned analysis provider.  Semantic authority identity,
 * physical realization identity, revision, ABI, and policy are part of every
 * admitted-judgment key before mutable authority words are appended.  A new
 * realization therefore cannot reuse its predecessor's facts until an
 * explicit qualification step permits that replacement.
 */
typedef struct {
    const CettaNikDirectAuthorityV1 *authority;
    uint32_t capabilities;
    uint32_t (*policy_identity)(void *host_context);
    bool (*mutable_authority_token)(
        void *host_context, PettaMachineAuthorityToken *token);
    bool (*judge_value)(
        void *host_context, Space *space, Arena *scratch,
        Atom *value, Atom *requirement,
        PettaAnalysisCallableFn callable, void *callable_context,
        PettaAnalysisResult *result);
    bool (*type_has_runtime_classifier)(
        void *host_context, Space *space, Atom *requirement);
    Atom *(*error_atom)(
        void *host_context, Arena *arena, Atom *source,
        int exit_code, const char *diagnostic);
    const char *(*reason_name)(
        void *host_context, PettaAnalysisReason reason);
    PettaMachineBoundaryResult (*validate_ready_call)(
        void *host_context, Space *space, Atom *call,
        char *diagnostic, size_t diagnostic_size);
} PettaAnalysisService;

/*
 * Session-owned answer table for opt-in PeTTa tabling and memoization.
 * The representation is private to the machine; a root machine borrows the
 * table only when no other root is using it, while recursive generator
 * machines share that same lease.  This keeps cached answers alive across
 * top-level requests without making an evaluator arena their owner.
 */
typedef struct PettaMachineTable PettaMachineTable;

typedef enum {
    PETTA_MEMO_AGGREGATE_NONE = 0,
    PETTA_MEMO_AGGREGATE_MIN,
    PETTA_MEMO_AGGREGATE_MAX,
    PETTA_MEMO_AGGREGATE_SUM,
    PETTA_MEMO_AGGREGATE_COUNT,
} PettaMemoAggregateMode;

typedef enum {
    PETTA_MEMO_RETENTION_WTINYLFU = 0,
    PETTA_MEMO_RETENTION_LRU,
} PettaMemoRetentionPolicy;

PettaMachineTable *petta_machine_table_new(void);
void petta_machine_table_free(PettaMachineTable *table);
void petta_machine_table_reset(PettaMachineTable *table);
/* Enforce cache-only retention after a root machine has released its lease.
 * Completed non-memo SLG entries are not charged to these library controls.
 * On allocation failure the table is cleared, preserving answers by forcing
 * ordinary recomputation on the next call. */
bool petta_machine_table_maintain(
    PettaMachineTable *table,
    PettaMemoRetentionPolicy policy,
    uint32_t unique_limit,
    uint64_t size_limit_bytes);

/* A collection producer lends each fully evaluated item to a synchronous,
 * side-effect-free consumer.  The consumer must not retain item beyond the
 * callback.  A declined producer invalidates all consumer state accumulated
 * by that attempt, so callers use a private transactional accumulator. */
typedef bool (*PettaMachineBorrowedItemConsumer)(
    void *context, Atom *item);

typedef struct {
    void *context;
    const PettaAnalysisService *analysis;
    /* The host contributes one component to the shared branch-capture
     * capacity.  The relational backend combines it with its internal state
     * profile and accepts an owned continuation only at multi-shot capacity. */
    CettaBranchAdmission (*admit_branch_capture)(
        void *context, Space *space);
    /* Candidate programs are meaningful only within this exact semantic
     * world.  Storage revision is tracked independently by SpaceReadToken. */
    CettaMatchDecisionSemanticIdentity match_decision_semantics;
    /* Wall-clock sampling is opt-in so normal evaluation pays no clock cost. */
    bool measure_stats;
    /* Quotation is presentation-owned.  PeTTa evaluates `(quote x)` to `x`,
     * while Prime keeps the quoted expression as inert first-class data. */
    bool quote_is_inert_data;
    /* Language-owned canonical answer-traversal materializer.  Runtime
     * dialects supply `reify`; a zero field retains the historical PeTTa
     * spelling for standalone machine clients that omit this field. */
    SymbolId reify_head;
    /*
     * Called immediately before a machine transition.  Returning false
     * suspends without consuming the pending goal, so the same machine can
     * resume when its caller restores a budget or clears cancellation.
     */
    bool (*permit_transition)(void *context);
    PettaMachineHostMode (*classify)(
        void *context, Space *space, Atom *expression);
    /* Exact mutable authority for deciding whether an expression root is
     * callable.  A missing token disables reuse of derived callability
     * judgments; it never makes an unknown host registry look immutable. */
    bool (*callability_authority_token)(
        void *context, PettaMachineAuthorityToken *token);
    PettaMachineQuerySpecializationAdmission
        (*admit_query_without_specialization)(
            void *context, Space *space,
            SymbolId head, Atom *const *arguments,
            CettaExprLen arity);
    PettaMachineSpaceQueryAdmission (*admit_space_query)(
        void *context, Space *space,
        SymbolId head, Atom *const *arguments,
        CettaExprLen arity);
    Space *(*resolve_space)(
        void *context, Space *root_space, Arena *arena,
        Atom *reference);
    /* Resolve a language-owned value reference only after its authored
     * occurrence reaches a SOLVE boundary.  A successful lookup returns a
     * machine-owned value in `resolved`; an unbound reference succeeds with
     * NULL so the original atom remains ordinary data.  Planning and
     * classification never invoke this service and therefore cannot add
     * demand merely to discover an optimization. */
    bool (*resolve_value_reference)(
        void *context, Arena *arena, Atom *reference,
        Atom **resolved);
    /* Some languages assign reference meaning even to an occurrence whose
     * source plan otherwise classifies it as a value.  Others preserve such
     * an authored occurrence as data.  The host owns that language policy;
     * the search machine still resolves only at a SOLVE boundary. */
    bool resolve_value_references_in_value_role;
    bool (*evaluate)(
        void *context, Space *space, Arena *arena, Atom *expression,
        const Bindings *environment, OutcomeSet *outcomes);
    /* Enumerate the intrinsic answers of the language's `get-type`
     * relation after its subject has reached the ready-value boundary.
     * The returned pointer array is caller-owned; every Atom is owned by
     * `arena`.  Explicit user equations remain ordinary later relation
     * clauses and are not included by this service. */
    bool (*get_type)(
        void *context, Space *space, Arena *arena, Atom *value,
        Atom ***types, uint32_t *count);
    /* Construct the active language's public Boolean datum.  Search owns the
     * truth relation; spelling and representation remain language-owned. */
    Atom *(*boolean_value)(
        void *context, Arena *arena, bool value);
    /* Profile-owned data constructors are fixed when the machine is built;
     * the machine never consults ambient session state while executing. */
    bool (*is_data_constructor)(
        void *context, SymbolId head, CettaExprLen arity);
    /* An executable source or residual occurrence has been resolved under
     * the current branch environment but has not executed.  A transactional
     * host may refuse it and restart through its canonical evaluator.
     * Residual static calls are included because specialization must not
     * erase the authority boundary of a dynamic source head. */
    bool (*prepare_resolved_call)(
        void *context, Space *space, Arena *arena,
        Atom *resolved_call, Atom **prepared_call);
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
    /* Native opt-in capabilities whose names are not part of the core
     * PeTTa presentation.  Returning known=false leaves the occurrence
     * available to ordinary equations, data, or an optional foreign
     * extension. */
    PeTTaNamedArity (*native_named_arity)(
        void *context, SymbolId head, CettaExprLen supplied);
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
    bool (*extension_call)(
        void *context, Arena *arena,
        Atom *expression, Atom *expected,
        const Bindings *environment, OutcomeSet *outcomes,
        bool *recognized);
    bool (*clause_snapshot_lease)(
        void *context, Space *space, SymbolId head,
        PettaClauseSnapshotLease *lease,
        PettaClauseSnapshotStats *stats);
    /* Optional evidence interpretation for a relational call.  The first
     * callback creates one branch-independent call occurrence; the second
     * appends evidence for a successfully matched clause to a branch-local
     * delta.  That delta joins the same rollback trail as substitutions.
     * A NULL pair means that clause execution has no evidence side channel. */
    uint64_t (*begin_relation_call)(
        void *context, SpaceReadToken read, Atom *query);
    bool (*record_clause_use)(
        void *context, Arena *owner, uint64_t call_occurrence,
        const PettaClauseCandidate *candidate, Atom *result,
        const Bindings *environment, Bindings *evidence_delta);
    /* True only when record_clause_use observes the structural result payload.
     * A host that supplies record_clause_use but omits this callback is treated
     * conservatively as payload-observing.  When false, the machine may fuse
     * activation and outer-environment substitution before recording the
     * occurrence IDs; the callback must then ignore the result payload. */
    bool (*clause_result_payload_observed)(void *context);
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
    /* A shared table is an optional physical realization.  The language
     * still decides which relations are memoized through the callbacks
     * below; merely supplying storage cannot make a call cacheable. */
    PettaMachineTable *shared_table;
    bool (*memoized_relation_contains)(
        void *context, SymbolId head, CettaExprLen arity);
    PettaMemoAggregateMode (*memoized_relation_aggregate)(
        void *context, SymbolId head, CettaExprLen arity);
    uint32_t (*memoized_relation_float_precision)(
        void *context, SymbolId head, CettaExprLen arity);
    uint32_t (*memoized_relation_answer_limit)(
        void *context, SymbolId head, CettaExprLen arity);
    void (*memoized_relation_observed)(
        void *context, SymbolId head, CettaExprLen arity,
        bool cache_hit);
    void (*memoized_relation_truncated)(
        void *context, SymbolId head, CettaExprLen arity);
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
    PETTA_MACHINE_STEP_DECLINED,
    PETTA_MACHINE_STEP_INVALIDATED,
    PETTA_MACHINE_STEP_CAPACITY,
    PETTA_MACHINE_STEP_HOST_ERROR,
    PETTA_MACHINE_STEP_SUSPENDED,
} PettaMachineStep;

typedef struct PettaMachineImpl PettaMachineImpl;

typedef struct {
    PettaMachineImpl *impl;
} PettaMachine;

/* Relational-control adapter for the evaluator-neutral continuation seam.
 * Capture is observational and restore is same-machine, authority-pinned, and
 * consuming.  The current physical realization is a fully owned image; that
 * contingent representation is receipted separately from multi-shot capacity.
 * Richer effects decline rather than selecting another controller. */
CettaContinuationMachine petta_machine_continuation_machine(
    PettaMachine *machine);

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
    uint64_t clause_snapshot_pointer_identity_hits;
    uint64_t clause_snapshot_equality_checks;
    uint64_t clause_snapshot_alpha_checks;
    uint64_t clause_snapshot_candidates;
    uint64_t clause_snapshot_candidates_copied;
    uint64_t clause_candidates;
    uint64_t clause_candidates_shape_pruned;
    uint64_t match_decision_compilations;
    uint64_t match_decision_cache_hits;
    uint64_t match_decision_runs;
    uint64_t match_decision_clause_inputs;
    uint64_t match_decision_clause_survivors;
    uint64_t match_decision_key_index_build_probes;
    uint64_t match_decision_key_index_select_probes;
    uint64_t match_decision_generic_key_policy_scans;
    uint64_t match_decision_linear_fallbacks;
    uint64_t match_decision_unavailable_path_fallbacks;
    uint64_t match_decision_invalidations;
    uint64_t clause_match_attempts;
    /* Candidate occurrences whose branch goals were successfully installed.
     * This is a control event, not a claim that the branch later produced an
     * answer.  On a completed run, attempts minus scheduled branches is the
     * exact number rejected before entering the branch. */
    uint64_t clause_branches_scheduled;
    uint64_t clause_match_allocated_bytes;
    uint64_t match_candidates;
    uint64_t match_candidate_epoch_views;
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
    uint64_t binding_apply_environment_entries;
    uint64_t binding_apply_epoch_calls;
    uint64_t binding_apply_epoch_suffix_entries;
    uint64_t solve_expression_apply_calls;
    uint64_t solve_expression_apply_allocated_bytes;
    uint64_t solve_expression_open_template_admitted_calls;
    uint64_t solve_expression_open_template_admitted_allocated_bytes;
    uint64_t solve_expected_apply_calls;
    uint64_t solve_expected_apply_allocated_bytes;
    uint64_t solve_expected_open_template_admitted_calls;
    uint64_t solve_expected_open_template_admitted_allocated_bytes;
    uint64_t activation_materialization_calls;
    uint64_t activation_materialization_allocated_bytes;
    uint64_t activation_open_template_admitted_calls;
    uint64_t activation_open_template_admitted_allocated_bytes;
    uint64_t constructor_slot_frame_entries;
    uint64_t constructor_slot_frame_direct_unifications;
    uint64_t pure_grounded_slot_frame_entries;
    uint64_t pure_grounded_slot_frame_direct_dispatches;
    uint64_t relation_slot_frame_entries;
    uint64_t relation_slot_operands_reused;
    uint64_t atom_copy_calls;
    uint64_t atom_copy_allocated_bytes;
    uint64_t atom_copy_query_calls;
    uint64_t atom_copy_query_allocated_bytes;
    uint64_t atom_copy_answer_calls;
    uint64_t atom_copy_answer_allocated_bytes;
    uint64_t atom_copy_visible_variable_calls;
    uint64_t atom_copy_visible_variable_allocated_bytes;
    uint64_t atom_copy_visible_value_calls;
    uint64_t atom_copy_visible_value_allocated_bytes;
    uint64_t atom_copy_error_calls;
    uint64_t atom_copy_error_allocated_bytes;
    uint64_t atom_freshen_calls;
    uint64_t atom_freshen_allocated_bytes;
    uint64_t specializer_prepare_calls;
    uint64_t specializer_prepare_filtered;
    uint64_t specializer_prepare_relation_filtered;
    uint64_t specializer_prepare_relevance_bounded;
    uint64_t specializer_prepare_rewritten;
    uint64_t specializer_prepare_unchanged;
    uint64_t specializer_prepare_capacity_declines;
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
    uint64_t deterministic_heap_collection_elapsed_ns;
    uint64_t deterministic_minor_heap_collection_elapsed_ns;
    uint64_t deterministic_major_heap_collection_elapsed_ns;
    uint64_t deterministic_goal_roots_scanned;
    uint64_t deterministic_heap_bytes_promoted;
    uint64_t deterministic_minor_heap_bytes_promoted;
    uint64_t deterministic_major_heap_bytes_promoted;
    uint64_t deterministic_root_atom_bytes_promoted;
    uint64_t deterministic_query_atom_bytes_promoted;
    uint64_t deterministic_visible_atom_bytes_promoted;
    uint64_t deterministic_type_atom_bytes_promoted;
    uint64_t deterministic_goal_atom_bytes_promoted;
    uint64_t deterministic_goal_first_bytes_promoted;
    uint64_t deterministic_goal_second_bytes_promoted;
    uint64_t deterministic_goal_third_bytes_promoted;
    uint64_t deterministic_goal_fourth_bytes_promoted;
    uint64_t deterministic_binding_atom_bytes_promoted;
    uint64_t deterministic_heap_bytes_reclaimed;
    uint64_t deterministic_binding_entries_discarded;
    uint64_t choice_binding_collections;
    uint64_t choice_binding_items_discarded;
    uint64_t choice_trail_entries_discarded;
    uint64_t choice_nursery_evacuations;
    uint64_t choice_nursery_evacuation_elapsed_ns;
    uint64_t choice_nursery_goal_roots_scanned;
    uint64_t choice_nursery_bytes_evacuated;
    uint64_t choice_nursery_bytes_reclaimed;
    uint64_t choice_heap_resets;
    uint64_t choice_heap_bytes_reclaimed;
    uint64_t owned_continuation_capture_attempts;
    uint64_t owned_continuation_captures;
    uint64_t owned_continuation_capture_deferred;
    uint64_t owned_continuation_capture_unsupported;
    uint64_t owned_continuation_capture_invalidated;
    uint64_t owned_continuation_restores;
    uint64_t owned_continuation_restore_invalidated;
    uint64_t owned_continuation_atom_bytes_captured;
    uint64_t owned_continuation_vector_bytes_captured;
    uint64_t owned_continuation_expansion_attempts;
    uint64_t owned_continuation_expansions;
    uint64_t owned_continuation_expansion_successors;
    uint64_t owned_continuation_expansion_deferred;
    uint64_t owned_continuation_expansion_unsupported;
    uint64_t owned_continuation_expansion_invalidated;
    uint64_t owned_continuation_expansion_capacity;
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
    uint64_t match_existence_observer_folds;
    uint64_t child_machine_init_attempts;
    uint64_t child_machine_init_successes;
    uint64_t child_machine_projected_entries;
    uint64_t child_machine_projection_elapsed_ns;
    uint64_t child_machine_init_elapsed_ns;
    uint64_t child_machine_destroy_calls;
    uint64_t child_machine_destroy_elapsed_ns;
    uint64_t host_environment_entries_observed;
    uint64_t host_environment_entries_forwarded;
    size_t maximum_goal_depth;
    size_t maximum_choice_depth;
    size_t maximum_choice_continuation_trail;
    size_t maximum_nursery_live_bytes;
    size_t maximum_tenured_live_bytes;
    size_t maximum_heap_live_bytes;
    size_t maximum_binding_entries;
    size_t maximum_binding_apply_environment_entries;
    size_t maximum_binding_apply_epoch_suffix_entries;
    size_t maximum_host_environment_entries_forwarded;
    uint64_t active_elapsed_ns;
    uint64_t time_to_first_answer_ns;
    uint64_t first_answer_transition;
} PettaMachineStats;

bool petta_machine_stats(
    const PettaMachine *machine, PettaMachineStats *stats);

#endif /* CETTA_PETTA_SEARCH_MACHINE_H */
