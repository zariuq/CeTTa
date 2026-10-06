#ifndef CETTA_PETTA_SEARCH_MACHINE_H
#define CETTA_PETTA_SEARCH_MACHINE_H

#include "delay_service.h"
#include "call_outcome.h"
#include "eval.h"
#include "match_decision.h"
#include "nik_direct_authority.h"
#include "open_equation_machine.h"
#include "petta_analysis.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "prepared_pure_machine.h"
#include "search_machine.h"

/*
 * The relational operational fragment is represented by an explicit heap
 * machine.  The machine owns equation choice, continuation order, rollback, and
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
     * first; ordinary backtracking then reaches explicit relation equations.
     */
    PETTA_MACHINE_HOST_READY_RELATIONAL_EXTENSION,
} PettaMachineHostMode;

typedef enum {
    PETTA_MACHINE_BUILTIN_EQUATIONS_NONE = 0,
    /* This profile permits a later authored alternative. No equation is
     * installed yet, so direct dispatch remains valid at the call itself. */
    PETTA_MACHINE_BUILTIN_EQUATIONS_AVAILABLE,
    PETTA_MACHINE_BUILTIN_EQUATIONS_APPEND,
    PETTA_MACHINE_BUILTIN_EQUATIONS_OWNED,
    PETTA_MACHINE_BUILTIN_EQUATIONS_PROTECTED,
} PettaMachineBuiltinEquations;

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

typedef enum {
    /* Ordinary PeTTa/SWI tables assume a static world.  A completed variant
     * table remains authoritative until an explicit table-control operation
     * clears it; unrelated source mutation does not update it implicitly. */
    PETTA_TABLE_MUTATION_STATIC_WORLD = 0,
    /* CeTTa's extended policy treats a table as a derived view of one Space
     * revision.  Any intervening mutation refuses replay and forces a fresh
     * table before the next root evaluation. */
    PETTA_TABLE_MUTATION_REVISION_GUARDED,
} PettaTableMutationPolicy;

PettaMachineTable *petta_machine_table_new(void);
void petta_machine_table_free(PettaMachineTable *table);

/*
 * Compiled match decisions over the equations of a relation, kept for a
 * session.  Each entry states the equation projection, the host semantics
 * and the callability authority it was compiled under, and is used only
 * while they are current.  A root machine borrows the repository when no
 * other root holds it, and the machines it starts share its lease; a root
 * that finds it held compiles into a repository of its own.
 */
typedef struct PettaMatchDecisionRepository PettaMatchDecisionRepository;

PettaMatchDecisionRepository *petta_match_decision_repository_new(void);
void petta_match_decision_repository_free(
    PettaMatchDecisionRepository *repository);
/* Whether a root machine holds the repository now. */
bool petta_match_decision_repository_leased(
    const PettaMatchDecisionRepository *repository);
void petta_machine_table_reset(PettaMachineTable *table);
bool petta_machine_table_set_mutation_policy(
    PettaMachineTable *table, PettaTableMutationPolicy policy);
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
    /* The semantic consumer contract remains separate from its derived
     * control plan: changing the contraction algebra cannot manufacture
     * scheduling or branch-storage authority.  NULL preserves exact ordered
     * answers for standalone embeddings. */
    const CettaObservationContract *observation_contract;
    /* Scope-entry observation plan supplied by the evaluator.  The machine
     * may use it only to preserve a delimiter across externalized branches;
     * it grants neither storage nor batching authority.  NULL keeps nested
     * observation boundaries inline. */
    const CettaControlPlan *control_plan;
    /* Request suspension only at an exactly externalizable relational
     * choice.  Deterministic stretches remain inside the machine; this avoids
     * polling the continuation hub after every transition. */
    bool externalize_equation_choices;
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
    /* PeTTa's translation places exposed result structure before ordered
     * equation effects. Other dialects require their own phase law. */
    bool source_output_constraints;
    /* Structural equation matching and literal binding patterns are separate
     * from evaluating a computation. An eager structural adapter must not
     * execute a relation embedded in a pattern. */
    bool structural_equation_heads;
    bool exact_equation_arity;
    bool literal_binding_patterns;
    /* A completed call with no applicable equation may remain syntax. A
     * matched equation with no returns still completes with no returns. */
    bool unmatched_call_is_value;
    bool empty_result_is_failure;
    bool retain_query_variable_identity;
    /* An eager tuple completes an argument before starting the next one.
     * A fault occurrence returns from this application, rather than becoming
     * a field in its constructed value. */
    bool eager_argument_faults;
    /* PeTTa's collections are findall: an error raised while collecting
     * aborts the collection and propagates, as an exception does in
     * SWI-PeTTa.  Other dialects keep a raised error as an occurrence. */
    bool raises_abort_collections;
    /* PeTTa runs every dispatch it decides at run time -- an application
     * whose head is a variable or an expression, `reduce`, a list native's
     * element call -- under an implicit handler: an error its body raises
     * fails that path alone, and every other alternative remains
     * (DispatchErrorScope.recover_add).  Argument evaluation is outside it. */
    bool dispatch_recovers_branch_locally;
    /* PeTTa reads a special form as syntax only where it is written: a head
     * reached at run time is a value, applied to evaluated arguments
     * (petta_semantics_runtime_head). */
    bool forms_are_written_syntax;
    /* Optional query union uses the ordinary ordered choice trail.  A pipe
     * in a base-profile pattern remains literal row syntax. */
    bool additive_match_queries;
    /* A `once` whose body is one call to the open tier takes its witness
     * from a portfolio of depth-first search and iterative deepening. */
    bool first_witness_portfolio;
    /* The language's answer-traversal materializer beside `collapse`:
     * `reify` where the profile offers it, and `collapse` in plain PeTTa,
     * which has no `reify`.  A zero field means `collapse`, for standalone
     * machine clients that omit it. */
    SymbolId reify_head;
    /* PeTTa, whose extended profile offers `select` and `collect`: where the
     * profile allows them they are the machine's own controls, the first
     * answers of a body and all of them; otherwise they are the host's. */
    bool bounded_collections;
    /* Explicit dependent domains belong to the active language profile,
     * not to the shared declaration cache or residual-type analysis. */
    bool dependent_type_domains;
    /*
     * Called immediately before a machine transition.  Returning false
     * suspends without consuming the pending goal, so the same machine can
     * resume when its caller restores a budget or clears cancellation.
     */
    bool (*permit_transition)(void *context);
    /* True only when compiled, bounded pure instructions do not consume a
     * finite transition purse.  A metered host leaves this false so equation
     * selection retains the canonical transition boundary. */
    bool unlimited_transition_budget;
    PettaMachineHostMode (*classify)(
        void *context, Space *space, Atom *expression);
    /* Direct grounded execution is a physical realization of a language
     * call.  The host therefore retains profile-owned admission for each
     * shared opcode; absence preserves the standalone machine's unrestricted
     * embedding contract. */
    bool (*builtin_allowed)(void *context, SymbolId head);
    /* Optional language-owned application of an already completed primitive.
     * Numeric register instructions retain their common direct path; other
     * calls use the host's domain and result contracts. */
    bool (*grounded_call)(void *context, Space *space, Arena *arena,
        Atom *head, Atom **arguments, uint32_t argument_count,
        CettaCallOutcome *outcome);
    /* Authority for authored equations sharing an intrinsic head. APPEND
     * enumerates the intrinsic first; OWNED selects this space's equations;
     * PROTECTED retains rows as data without executing them. */
    PettaMachineBuiltinEquations (*builtin_equations)(
        void *context, Space *space, SymbolId head, CettaExprLen arity);
    /* Exact mutable authority for deciding whether an expression root is
     * callable.  A missing token disables reuse of derived callability
     * judgments; it never makes an unknown host registry look immutable. */
    bool (*callability_authority_token)(
        void *context, PettaMachineAuthorityToken *token);
    /* The observer and fields are borrowed for this read-only callback. */
    PettaMachineSpaceQueryAdmission (*admit_space_query)(
        void *context, Space *space,
        SymbolId head, const CettaGsltTermCursorV1 *arguments,
        CettaExprLen arity, CettaGsltTermCursorObserverV1 observer);
    /* The typed counterpart retains one lexical context per argument.
     * It is used after strict argument evaluation has produced explicit
     * closure values but before a whole call is materialized. */
    PettaMachineSpaceQueryAdmission (*admit_space_query_values)(
        void *context, Space *space,
        SymbolId head, Bindings *environment,
        const BindingValue *arguments, CettaExprLen arity);
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
    /* Evaluate a goal the machine hands to the host, adding its answers to
     * `outcomes`; `end` says how the evaluation ended (call_outcome.h):
     * FAILURE once its answers are given, RAISED with an error the host
     * evaluation raised and did not catch, whose answers are then dropped,
     * or INTERRUPTED when cancellation or an exit request stopped it. */
    bool (*evaluate)(
        void *context, Space *space, Arena *arena, Atom *expression,
        const Bindings *environment, OutcomeSet *outcomes,
        CettaCallOutcome *end, const CettaDelayView *delay);
    /* Preserve the translation-stage classification of an authored source
     * occurrence when evaluation crosses a host-owned boundary.  The plan is
     * positional metadata for `expression`; NULL means that this boundary
     * has no earlier translation event to transport.  Hosts which do not
     * model staged callability may omit this callback and use `evaluate`. */
    bool (*evaluate_planned)(
        void *context, Space *space, Arena *arena, Atom *expression,
        const PettaPlanNode *plan,
        const Bindings *environment, OutcomeSet *outcomes,
        CettaCallOutcome *end, const CettaDelayView *delay);
    /* A strict application whose arguments the machine has computed.  The
     * host applies its operation to those values without evaluating them
     * again, and returns false, adding nothing, when the operation needs
     * the ordinary evaluator; the call then goes to evaluate as before.
     * `end` is how an applied operation ended, as for `evaluate`: RAISED
     * with its error, which adds no answer.  Optional. */
    bool (*apply_ready_values)(
        void *context, Space *space, Arena *arena, Atom *expression,
        const Bindings *environment, OutcomeSet *outcomes,
        CettaCallOutcome *end);
    /* Create a new translation event at an explicit forcing boundary such as
     * PeTTa `eval`.  A returned plan fixes callability for that occurrence;
     * NULL declines because the host could not establish the event. */
    const PettaPlanNode *(*translate_source)(
        void *context, Space *space, Atom *expression);
    /* Enumerate the intrinsic answers of the language's `get-type`
     * relation after its subject has reached the ready-value boundary.
     * The returned pointer array is caller-owned; every Atom is owned by
     * `arena`. A NULL target is a fresh type; a non-NULL target is the
     * current required-type operand, not a post-enumeration filter.
     * Explicit user equations remain ordinary later relation
     * equations and are not included by this service. */
    bool (*get_type)(
        void *context, Space *space, Arena *arena, Atom *value, Atom *target,
        Atom ***types, uint32_t *count, CettaEvalCompletion *completion);
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
    /* Sequence a closed numeric producer directly into a lexical fold.
     * A completed value is ready for unification. An expression result is
     * the already-materialized undelivered suffix and its prior accumulator;
     * the machine resumes its existing fold continuation over those values. */
    bool (*foldl_producer)(
        void *context, Space *space, Arena *arena,
        Atom *items, Atom *initial, Atom *accumulator_binder, Atom *item_binder,
        Atom *body, const Bindings *environment, CettaCallOutcome *result);
    /* A generated determinate-map program may own a lexical map without
     * allocating one result variable and one relational goal per item.
     * NOT_APPLICABLE preserves the complete relational product. */
    PettaMachineFoldResult (*map_single_result)(
        void *context, Space *space, Arena *arena,
        Atom *items, Atom *item_binder, Atom *body_expression,
        const Bindings *environment, Atom **result_out);
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
        const Bindings *environment, OutcomeSet *outcomes,
        CettaCallOutcome *end);
    /*
     * Ground `add-atom` after the space argument is a value and the payload
     * has been substituted.  The host owns storage, typing, and program
     * observation through the shared admit authority.  Returning false
     * declines to ordinary host evaluation.  On true, `*result` is the
     * language success value or an error atom.
     */
    bool (*admit_ground_atom)(
        void *context, Space *space, Arena *arena,
        Atom *call, Atom **result);
    PettaSpecializeResult (*prepare_call)(
        void *context, Space *space, Arena *result_arena,
        Atom *call, Atom **prepared_call);
    /* Execute a closed, revision-pinned determinate pure call through the
     * shared generated machine.  NULL declines to canonical equation search. */
    Atom *(*execute_prepared_pure_call)(
        void *context, Space *space, Arena *result_arena,
        Atom *prepared_call);
    /* A revision-keyed program fact: the relation has already declined
     * prepared compilation, so no closed call of it executes there.  Read
     * before a call is proved closed or materialized; false means only
     * that no decline is known. */
    bool (*prepared_pure_call_declined)(
        void *context, Space *space, SymbolId head, CettaExprLen arity);
    /* Open a revision-pinned enumeration of the finite-answer fragment of a
     * closed pure call, in equation-occurrence order.  NULL declines to
     * canonical equation search.  `scratch` may hold admission temporaries;
     * the machine admits each relation the enumeration enters. */
    CettaPreparedPureAnswerCursor *(*open_answer_cursor)(
        void *context, Space *space, Arena *scratch, Atom *prepared_call,
        CettaPreparedPureHeadAdmissionFn head_admission,
        void *head_admission_context);
    /* A revision-keyed program fact: the relation has declined answer
     * compilation.  False means only that no decline is known. */
    bool (*answer_cursor_declined)(
        void *context, Space *space, SymbolId head, CettaExprLen arity);
    /* Every host authority an answer producer's enumeration depends on
     * besides the Space program: callability, transaction state, and the
     * relation-dispatch registrations.  Equal tokens at two times mean the
     * producer's remaining answers are the ones equation search would find. */
    bool (*answer_authority_token)(
        void *context, PettaMachineAuthorityToken *token);
    /* Open a revision-pinned enumeration of a call whose arguments may hold
     * unbound variables, over the compiled open equation tier, with the
     * consumer's `expected` value as its destination.  Each answer reports
     * the call's value and the values of `query_vars` (every variable of the
     * call and of `expected`), built in `answer_arena`.  With
     * `source_output_constraints` an equation's statically known output
     * meets the destination before its effects, as in this machine.  NULL
     * declines to canonical equation search. The call is already prepared:
     * its argument demands and guards have run. With evaluate_result=false
     * the equation RHS is returned as substituted data instead of evaluated. */
    CettaOpenEquationCursor *(*open_relation_cursor)(
        void *context, Space *space, Arena *answer_arena,
        Arena *stable_arena, Atom *call,
        Atom *expected, Atom *const *query_vars, uint32_t query_var_count,
        bool evaluate_result, bool source_output_constraints, bool dispatch_recovers,
        bool count_only, uint64_t activation_budget, uint32_t depth_bound);
    /* A revision-keyed program fact: the relation has declined open
     * compilation.  False means only that no decline is known. */
    bool (*open_relation_declined)(
        void *context, Space *space, SymbolId head, CettaExprLen arity,
        bool evaluate_result);
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
    /* Whether the foreign engine defines the predicate name/arity now,
     * registering nothing.  NULL: it defines none. */
    bool (*foreign_predicate_defined)(
        void *context, SymbolId name, CettaExprLen arity);
    /* `end`: how a recognized call ended (call_outcome.h), FAILURE after
     * its answers or RAISED with its error. */
    /* `delay`: the machine's delayed goals, which the call carries in and
     * reads back (delay_service.h). */
    bool (*extension_call)(
        void *context, Arena *arena,
        Atom *expression, Atom *expected,
        const Bindings *environment, OutcomeSet *outcomes,
        bool *recognized, CettaCallOutcome *end,
        const CettaDelayView *delay);
    bool (*candidate_snapshot_lease)(
        void *context, Space *space, SymbolId head,
        PettaCandidateSnapshotLease *lease,
        PettaCandidateSnapshotStats *stats);
    /* Optional evidence interpretation for a relational call.  The first
     * callback creates one branch-independent call occurrence; the second
     * appends evidence for a successfully matched equation to a branch-local
     * delta.  That delta joins the same rollback trail as substitutions.
     * A NULL pair means that equation execution has no evidence side channel. */
    uint64_t (*begin_relation_call)(
        void *context, SpaceReadToken read, Atom *query);
    bool (*record_equation_use)(
        void *context, Arena *owner, uint64_t call_occurrence,
        const PettaEquationCandidate *candidate, Atom *result,
        const Bindings *environment, Bindings *evidence_delta);
    /* True only when record_equation_use observes the structural result payload.
     * A host that supplies record_equation_use but omits this callback is treated
     * conservatively as payload-observing.  When false, the machine may fuse
     * activation and outer-environment substitution before recording the
     * occurrence IDs; the callback must then ignore the result payload. */
    bool (*equation_result_payload_observed)(void *context);
    /* A delayed equation-body view may outlive the immediate match step.
     * The host must therefore prove the defining relation effect-free at
     * the pinned Space revision; absence or refusal selects materialization. */
    bool (*equation_activation_relation_admissible)(
        void *context, Space *space,
        SymbolId head, CettaExprLen arity);
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
    /* Optional session storage for the compiled match decisions of the
     * root machine's space. */
    PettaMatchDecisionRepository *match_decisions;
    bool (*memoized_relation_contains)(
        void *context, SymbolId head, CettaExprLen arity);
    PettaMemoAggregateMode (*memoized_relation_aggregate)(
        void *context, SymbolId head, CettaExprLen arity);
    uint32_t (*memoized_relation_float_precision)(
        void *context, SymbolId head, CettaExprLen arity);
    uint32_t (*memoized_relation_answer_limit)(
        void *context, SymbolId head, CettaExprLen arity);
    uint64_t (*memoized_relation_size_limit_bytes)(
        void *context, SymbolId head, CettaExprLen arity);
    void (*memoized_relation_observed)(
        void *context, SymbolId head, CettaExprLen arity,
        bool cache_hit);
    void (*memoized_relation_bypassed)(
        void *context, SymbolId head, CettaExprLen arity);
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
    /* Variables the caller reads from every answer, (v...): each is visible,
     * and its binding exported, whether or not the query binds it
     * lexically.  A hyperpose branch runs apart from its caller, whose
     * variables its let patterns bind. */
    Atom *export_variables;
    /* Observe a ready value's intrinsic metatype without interpreting it.
     * A true return with NULL output means no metatype; false is a service
     * fault. The output belongs to arena. Authored equations remain on the
     * ordinary relation path; NULL callback keeps generic host dispatch. */
    bool (*get_metatype)(
        void *context, Space *space, Arena *arena, Atom *value, Atom **type);
    /* Compile/instantiate declaration facts, not relational type answers.
     * Atoms and literal modes belong to arena; free the returned call array.
     * A NULL service uses the same uncached compilation. */
    bool (*type_calls)(
        void *context, Space *space, Arena *arena, Atom *head,
        CettaExprLen supplied, PettaTypeCall **calls, uint32_t *count,
        bool *hold_body);
} PettaMachineHost;

typedef enum {
    PETTA_MACHINE_STEP_ANSWER = 0,
    PETTA_MACHINE_STEP_EXHAUSTED,
    PETTA_MACHINE_STEP_DECLINED,
    PETTA_MACHINE_STEP_INVALIDATED,
    PETTA_MACHINE_STEP_STACK,
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

/* True exactly when the active machine is at a provider-supported owned
 * relational choice that may be split into independent continuations. */
bool petta_machine_external_branch_ready(const PettaMachine *machine);

/* Whether a let binder that only counting operations read is represented
 * by its producer's count (CETTA_PETTA_LET_COUNT_FUSION: on unless 0, false
 * or off).  Every tier that runs PeTTa equations follows the same switch. */
bool petta_machine_let_count_fusion_enabled(void);

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

/* True when the last answer is an Error that was raised and not caught,
 * rather than an Error value. */
bool petta_machine_last_answer_raised(const PettaMachine *machine);
/* A machine created to evaluate for a caller that delays goals starts from
 * a copy of them, as a child machine does. */
bool petta_machine_inherit_delayed_goals(PettaMachine *machine,
                                         const CettaDelayService *service);
/* The goals the machine delays on an answer's variables (those of the
 * answer, of the values its environment gives, and the visible variables it
 * leaves unbound), as a conditional answer's payload (carried components):
 * the caller withdraws what it delays on those variables and suspends these
 * in their place.  NULL when there is none. */
bool petta_machine_answer_delayed(PettaMachine *machine, Atom *answer,
                                  const Bindings *environment, Arena *arena,
                                  Atom **payload);

void petta_machine_destroy(PettaMachine *machine);
/* Whether CETTA_PETTA_QUERY_TRACE asks to see each query of `head`: the
 * head it names, or every head for `*`. */
bool petta_machine_query_traced(SymbolId head);

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
    uint64_t candidate_snapshot_calls;
    uint64_t candidate_snapshot_cache_hits;
    uint64_t candidate_snapshot_live_occurrences;
    uint64_t candidate_snapshot_records_examined;
    uint64_t candidate_snapshot_pointer_identity_hits;
    uint64_t candidate_snapshot_equality_checks;
    uint64_t candidate_snapshot_alpha_checks;
    uint64_t candidate_snapshot_candidates;
    uint64_t candidate_snapshot_candidates_copied;
    uint64_t equation_candidates;
    uint64_t equation_candidates_shape_pruned;
    uint64_t equation_guard_prune_attempts;
    uint64_t equation_guard_pruned;
    uint64_t equation_guard_retained;
    uint64_t match_decision_compilations;
    uint64_t match_decision_cache_hits;
    uint64_t match_decision_runs;
    uint64_t match_decision_equation_inputs;
    uint64_t match_decision_equation_survivors;
    uint64_t match_decision_key_index_build_probes;
    uint64_t match_decision_key_index_select_probes;
    uint64_t match_decision_generic_key_policy_scans;
    uint64_t match_decision_linear_fallbacks;
    uint64_t match_decision_unavailable_path_fallbacks;
    uint64_t match_decision_discriminator_unbound_skips;
    /* Closed calls built once as ground terms for ground-template equations. */
    uint64_t closed_ground_queries;
    uint64_t match_decision_invalidations;
    uint64_t equation_match_attempts;
    /* Candidate occurrences whose branch goals were successfully installed.
     * This is a control event, not a claim that the branch later produced an
     * answer.  On a completed run, attempts minus scheduled branches is the
     * exact number rejected before entering the branch. */
    uint64_t equation_branches_scheduled;
    uint64_t equation_match_allocated_bytes;
    uint64_t match_candidates;
    uint64_t match_prefix_cursor_reuse;
    uint64_t match_candidate_epoch_views;
    uint64_t unification_calls;
    uint64_t unification_failures;
    uint64_t unification_binding_writes;
    uint64_t unification_allocated_bytes;
    uint64_t equation_binding_merge_calls;
    uint64_t equation_binding_merge_source_items;
    uint64_t equation_binding_merge_logical_writes;
    uint64_t equation_binding_merge_failures;
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
    uint64_t binding_apply_frame_entries;
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
    uint64_t activation_scalar_argument_segment_attempts;
    uint64_t activation_scalar_argument_segment_commits;
    uint64_t activation_scalar_argument_segment_declines;
    uint64_t activation_scalar_argument_segment_operations;
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
    uint64_t choice_continuation_trail_compactions;
    uint64_t choice_continuation_trail_discarded;
    uint64_t deterministic_equation_choices_elided;
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
    uint64_t deterministic_goal_context_bytes_promoted;
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
    uint64_t count_aggregate_match_view_folds;
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
    size_t maximum_binding_apply_frame_entries;
    size_t maximum_host_environment_entries_forwarded;
    uint64_t active_elapsed_ns;
    uint64_t time_to_first_answer_ns;
    uint64_t first_answer_transition;
} PettaMachineStats;

bool petta_machine_stats(
    const PettaMachine *machine, PettaMachineStats *stats);

#endif /* CETTA_PETTA_SEARCH_MACHINE_H */
