#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "answer_bank.h"
#include "call_outcome.h"
#include "atom.h"
#include "eval_completion.h"
#include "native_handle.h"
#include "search_machine.h"
#include "space.h"
#include "term_canon.h"
#include "variant_instance.h"
typedef struct CettaLibraryContext CettaLibraryContext;
/* Cooperatively poll the active execution's interruption at a blocking
 * native boundary. A true result must release owned host work and unwind. */
bool eval_native_interrupted(void);
/* Opaque revision of a live owned retained computation; NULL if not owned. */
Atom *eval_current_work_epoch(Arena *arena, Atom *handle);
struct CettaPettaTokenSpaceClauseRegistry;

/*
 * The body of a canonical one-place PeTTa callable with its parameter
 * replaced by `parameter`.  Binding `parameter` to an argument and evaluating
 * the body applies the callable to that argument, which stays a value;
 * relational control remains with the caller.  NULL means the atom is not a
 * one-place callable or its ABT substitution could not be constructed.
 */
Atom *cetta_petta_open_callable(
    Arena *arena, Atom *callable, Atom *parameter);

struct CettaPettaTokenSpaceClauseRegistry *
cetta_petta_token_space_clause_registry_new(void);
void cetta_petta_token_space_clause_registry_free(
    struct CettaPettaTokenSpaceClauseRegistry *registry);

/* ── Outcome: the unified result type for all evaluator functions ───────── */
/* Every evaluator function returns a set of outcomes (atom + bindings).
   This replaces the former split between ResultSet and ResultBindSet.
   "Plain" evaluation is just projection: outcome.atom. */

typedef enum {
    CETTA_OUTCOME_INLINE = 0,
    CETTA_OUTCOME_ANSWER_REF = 1,
} CettaOutcomeKind;

typedef struct {
    const AnswerBank *bank;
    AnswerRef ref;
    CettaVarMap goal_instantiation;
} OutcomeAnswerRef;

typedef struct Outcome {
    CettaOutcomeKind kind;
    /* The producer's return demand, independent of Atom spelling. A failed
     * interpretation returns EXPRESSION; an earned value stays completed. */
    CettaCallResultForm result_form;
    Atom *atom;
    Atom *materialized_atom;
    Bindings env;
    VariantInstance variant;
    OutcomeAnswerRef answer_ref;
    /* A conditional answer's delayed goals (CETTA_CALL_SUSPENDED); NULL for
     * an answer that holds outright. */
    Atom *delayed;
} Outcome;

typedef struct OutcomeSet {
    Outcome *items;
    CettaCount len, cap;
    Arena *payload_owner;
    Outcome inline_items[1];
} OutcomeSet;

void outcome_set_init(OutcomeSet *os);
void outcome_set_init_with_owner(OutcomeSet *os, Arena *owner);
void outcome_set_set_owner(OutcomeSet *os, Arena *owner);
void outcome_set_add(OutcomeSet *os, Atom *atom, const Bindings *env);
/* The newest answer holds only while `delayed` holds (CETTA_CALL_SUSPENDED). */
void outcome_set_delay_last(OutcomeSet *os, Atom *delayed);
/* True only after both the value and its active context are retained. */
bool outcome_set_add_move(OutcomeSet *os, Atom *atom, Bindings *env);
void outcome_set_free(OutcomeSet *os);
/* Owned arrays, bindings, variants and payload arenas of a quiescent bag.
 * The enclosing OutcomeSet header belongs to its caller. */
void outcome_set_storage_census(const OutcomeSet *os, CettaStorageCensus *census);
/* Copy a quiescent inline occurrence bag for an owned continuation. One
 * enclosing Atom session preserves sharing between every visible, cached,
 * delayed, logical, variant and Prime occurrence payload. Materialization
 * state, order, multiplicity and set capacity are retained without evaluation.
 * A non-NULL payload owner must map to arena in that session. An absent owner
 * remains absent. AnswerBank references refuse until their owner is transported.
 * destination must be initialized; refusal leaves it and source unchanged.
 * Discard the unpublished session/arenas after refusal. External resources and
 * identity-mapped regions retain their separate caller lifetime contracts. */
bool outcome_set_copy_to_arena(const OutcomeSet *source, OutcomeSet *destination,
    Arena *arena, AtomDeepCopySession *atoms);
/* Same physical occurrence copy, within a still-retained source episode.
 * Reader handles remain qualified by that episode instead of being erased or
 * treated as independently checkpointed resources. */
bool outcome_set_copy_to_episode(const OutcomeSet *source,OutcomeSet *destination,
    Arena *arena,AtomDeepCopySession *atoms);

/* An owned copy of a quiescent Prime search checkpoint. NEW accepts an owning
 * prime-search residual from the current library context and reserves its
 * existing lease until PREPARE or FREE. A busy or unsupported input is never
 * consumed. Source and consulted external Space/program/registry resources
 * remain alive under the same stable, exclusive episode. The preparation is
 * enrolled in the context's existing cleanup registry: context teardown
 * retires its image before dependencies and invalidates the caller-owned
 * shell. The caller must still FREE that shell. Explicit close and context
 * teardown are detected, but must not race these calls.
 *
 * Add every ARENA_PAIR_AT entry (ARENA_PAIR_COUNT entries) to the enclosing
 * injective Atom session before PREPARE, together with each separately
 * prepared recording-group pair and every admitted borrowed external region.
 * Alternatively, select prime_need_recording_copy_forget on that session
 * before PREPARE: bounded recording carriers and the image's recording policy
 * are erased instead of copied. Other observers and the source stay owned by
 * their existing holders. Paid cost accounts and operational state remain.
 * ARENA_PAIR returns the first, control-storage pair. PREPARE copies the
 * complete supported frame/frontier/world/licence packet through that session;
 * it does not run an evaluator transition. Refusal releases the reservation
 * and leaves the original usable. Discard the unpublished session on refusal.
 * On success, release the session before retiring its source pointer keys.
 *
 * PUBLISH gives the image a fresh owning residual handle. Handle-capacity
 * refusal keeps the prepared image for retry, even after source retirement.
 * Publication does not validate source revisions: ordinary resume performs
 * its existing checks and retains an invalidated packet. It does not certify
 * an executable result or replay. Captured logical IDs and paid work stay
 * exact; future fresh IDs still come from the ambient allocator. Copy expense
 * is separate from the copied evaluator account.
 *
 * This profile declines nested/registered children, worker/resource/foreign
 * owners and compiled cache owners. Preserving a recording requires its
 * separately prepared capability in the same session. An owned top episode additionally
 * requires its actual storage holder, initialized private request/region
 * arenas, inactive region stack and no external answer observer. With the Prime stack
 * disabled, NEW returns UNSUPPORTED without consuming its input. Ordinary
 * execution remains available. No new language form is introduced. */
typedef struct CettaPrimeCheckpointCopy CettaPrimeCheckpointCopy;
CettaPrimeCheckpointCopy *cetta_prime_checkpoint_copy_new(
    CettaLibraryContext *context, Atom *residual, CettaContinuationStatus *status);
/* The reserved variant keeps the source lease after successful PREPARE.
 * After releasing the Atom copy session, REPLACE transfers the image into
 * that source's existing native-handle owner under a sole exclusive use.
 * All owning handle aliases keep their identity and resolve the image; the
 * old payload retires without running it. Other acquired uses refuse and
 * retain the staged image for retry. Source close invalidates replacement.
 * FREE abandons the image and releases the reservation. This variant cannot
 * PUBLISH a second producer. External borrowed users remain excluded under
 * the existing stable episode contract. It has the same admission limits as
 * the copier above; this is not concurrent shared-producer migration. */
CettaPrimeCheckpointCopy *cetta_prime_checkpoint_copy_new_reserved(
    CettaLibraryContext *context, Atom *residual, CettaContinuationStatus *status);
bool cetta_prime_checkpoint_copy_arena_pair(
    const CettaPrimeCheckpointCopy *copy, AtomDeepCopyArenaPair *pair);
size_t cetta_prime_checkpoint_copy_arena_pair_count(const CettaPrimeCheckpointCopy *copy);
bool cetta_prime_checkpoint_copy_arena_pair_at(const CettaPrimeCheckpointCopy *copy,
    size_t index, AtomDeepCopyArenaPair *pair);
CettaContinuationStatus cetta_prime_checkpoint_copy_prepare(
    CettaPrimeCheckpointCopy *copy, AtomDeepCopySession *atoms);
CettaContinuationStatus cetta_prime_checkpoint_copy_publish(
    CettaPrimeCheckpointCopy *copy, Arena *arena, Atom **residual);
CettaContinuationStatus cetta_prime_checkpoint_copy_replace(
    CettaPrimeCheckpointCopy *copy, CettaNativeHandleUse *source_use);
const char *cetta_prime_checkpoint_copy_refusal(const CettaPrimeCheckpointCopy *copy);
void cetta_prime_checkpoint_copy_free(CettaPrimeCheckpointCopy *copy);
/* Stage an independent outcome vector through one graph relocation session.
 * The caller owns both vectors until it commits or abandons the staged copy. */
bool outcome_set_relocate_clone(OutcomeSet *dst, const OutcomeSet *src,
                               Arena *owner, AtomDeepCopySession *session);

/* A retained execution of the existing HE canonical frames. Input syntax,
 * complete bindings and captured Need/branch/weight/history carriers are
 * promoted into its storage; source spaces, registry, library
 * context, namespace overrides, cancellation flags, observer hooks and their
 * contexts, and identity-bearing foreign resources are borrowed and must
 * remain live until destruction. This requirement is a
 * caller contract, not an ownership check supplied by this interface.
 *
 * Operations belong to the creating thread. Native callbacks are atomic at
 * this boundary. Advancing may commit their effects; inspection cannot.
 * Source queries retain their existing current-revision semantics.
 *
 * Construction and activation require an ordinary HE caller outside an
 * active transactional payload, including its redirect setup and promotion.
 * On a temporary caller conflict, begin returns NULL, advance returns BUSY,
 * and destroy returns false. They leave the caller's transaction and the
 * retained execution unchanged; the execution can resume after payload
 * cleanup. No cross-region publication permission is supplied here. */
typedef struct CettaHeExecution CettaHeExecution;
typedef enum {
    CETTA_HE_ENTRY_BIND = 0,
    CETTA_HE_ENTRY_EVALUATE,
    CETTA_HE_ENTRY_CALL,
} CettaHeEntry;
typedef enum {
    CETTA_HE_ADVANCED = 0,
    CETTA_HE_CLOSED,
    CETTA_HE_INTERRUPTED,
    CETTA_HE_BUSY,
} CettaHeAdvance;
typedef struct {
    /* Borrowed, exact ordered occurrences; valid until advance or destroy. */
    const OutcomeSet *outcomes;
    size_t frames;
    unsigned top_phase;
    uint64_t advances;
    uint64_t collections;
    CettaEvalCompletion completion;
    bool closed;
    bool interrupted;
} CettaHeExecutionView;

CettaHeExecution *eval_he_owned_begin(Space *space, Registry *registry,
    CettaHeEntry entry, Atom *atom, Atom *type, int fuel,
    const Bindings *bindings, bool preserve_bindings, bool typed);
CettaHeAdvance eval_he_owned_advance(CettaHeExecution *execution);
/* Stop pending work; already committed effects are retained. */
bool eval_he_owned_cancel(CettaHeExecution *execution);
/* Inert inspection neither evaluates nor consumes an occurrence. */
bool eval_he_owned_inspect(const CettaHeExecution *execution,
                          CettaHeExecutionView *view);
/* False for an active owner, another thread, or a payload conflict; no
 * resource is released. */
bool eval_he_owned_destroy(CettaHeExecution *execution);

/* ── ResultSet: public API for top-level results (atoms only) ──────────── */
/* This is the user-facing result type. Internally, the evaluator works
   with OutcomeSet; ResultSet is produced by dropping bindings at the end. */

typedef struct ResultSet {
    Atom **items;
    CettaCount len, cap;
    Atom *inline_items[1];
} ResultSet;

void result_set_init(ResultSet *rs);
void result_set_add(ResultSet *rs, Atom *atom);
void result_set_free(ResultSet *rs);

/* Evaluation coverage is separate from the occurrence frontier.  Existing
   callers that only need HE-compatible answers continue to use metta_eval;
   callers making universal or no-more-occurrences claims use EvalOutcome. */
typedef struct EvalOutcome {
    /* Ordered occurrence bag.  Prime retains both ordinary values and
       Error-headed fault occurrences here; compatibility projections may be
       applied only after this exact carrier has been produced. */
    ResultSet results;
    CettaEvalCompletion completion;
    bool budget_limited;
    uint64_t budget_initial;
    uint64_t budget_remaining;
    uint64_t steps_spent;
    uint64_t effect_denials;
    /* PeTTa: whether the evaluator knows which of the directive's Error
       answers were raised and not caught, and whether any was. */
    bool petta_raise_known;
    bool petta_raised_error;
    /* Input: the fuel bounds the depth of each evaluation path, as the
       evaluation without an outcome does, rather than the total number of
       steps; exhausting it on any path still marks the outcome incomplete. */
    bool depth_fuel;
    /* Input: the caller turns an incomplete outcome into its own explicit
       answer, so the incompleteness is not also reported to the enclosing
       evaluation. */
    bool completion_consumed;
    /* Owns retained top-level results or an Observation; released by free. */
    struct CettaPrimeObservationReturn *prime_observation;
} EvalOutcome;

typedef enum {
    CETTA_EVAL_ZERO_COMPLETE = 0,
    CETTA_EVAL_NONZERO,
    CETTA_EVAL_ZERO_PENDING,
} CettaEvalZeroStatus;

typedef void (*CettaPrimeNeedAnswerObserver)(
    Atom *answer, const PrimeNeedReceipt *receipt, void *context);
struct PettaPlanNode;


typedef struct CettaPrimeObservationReturn CettaPrimeObservationReturn;
typedef struct {
    CettaContinuationStatus status;
    bool started, initialized, unpublished, normalized, committed;
    CettaCount pending;
    Atom *holder, *projected;
    /* An owned region may retain an irrecoverable conversion/protocol
     * refusal. CAPACITY alone does not distinguish it from an append retry.
     * These fields describe the held top operation, not nested borrowed
     * controllers, and grant neither execution nor lifetime authority. */
    const char *return_refusal;
    bool producer_complete, return_materialized;
    size_t accepted_rows, copied_rows;
    uint64_t remaining_allowance;
    /* Explicitly sliced ordinary return transport. Traversal decisions are
     * separate from controller work and from prices of native copy services. */
    bool copy_pending;
    uint64_t copy_traversal_steps;
    bool preparation_pending;
    uint64_t preparation_steps;
} CettaPrimeObservationState;

/* Optional finite return-service slices, independent of source-controller
 * credit and accepted-return count. Unlimited phases have their limit flag
 * clear. Preparation counts a raw-row conversion, retained Need-reifier
 * transitions and immutable-graph node/edge inspections. Scoped substitution,
 * AnswerRef merge, lookup, constructors, allocation and cleanup have separate
 * primitive prices. */
typedef struct {
    uint64_t preparation, transport;
    bool limit_preparation, limit_transport;
} CettaPrimeReturnSlice;
typedef struct {
    uint64_t preparation, transport;
} CettaPrimeReturnWork;
/* The state is a borrowed view. The outcome and its result arena, library,
 * registry and source Space must remain live through retry or disposal.
 * A pending row is retried without invoking its producer again. */
bool eval_outcome_observation_state(const struct EvalOutcome *outcome,
                                  CettaPrimeObservationState *state);
CettaContinuationStatus eval_outcome_resume_observation(
    struct EvalOutcome *outcome, uint64_t allowance);
/* Opt-in ordinary owned-root transport. Controller allowance adds to its
 * existing retained work bank; unused earlier credit is still available.
 * copy_allowance independently bounds graph traversal
 * decisions for this invocation. A copy pause retains the exact pending
 * answer and returns DEFERRED. A fully copied answer may retry a refused
 * result slot with zero copy allowance. Scalar/memo/allocator/validation and
 * cleanup services have separate costs; this is not a whole-slot time bound.
 * Select an owned root with zero initial allowance to slice its first return.
 * copy_used, when supplied, reports this call's exact traversal debit even
 * when completion releases the observation. Other observation profiles refuse
 * without changing their residual or spending copy allowance. */
CettaContinuationStatus eval_outcome_resume_observation_with_transport(
    struct EvalOutcome *outcome, uint64_t allowance, uint64_t copy_allowance,
    uint64_t *copy_used);
/* NULL selects unlimited return preparation/transport. A zero preparation
 * slice retains an unprepared row; a zero transport slice may still publish
 * an already copied row. No partial term is delivered. Work, if requested,
 * reports this call's actual phase debits even when completion retires its
 * owner. Neither phase allowance is a whole-slot instruction/time bound. */
CettaContinuationStatus eval_outcome_resume_observation_slice(
    struct EvalOutcome *outcome, uint64_t allowance,
    const CettaPrimeReturnSlice *slice, CettaPrimeReturnWork *work);

typedef enum {
    CETTA_PRIME_OWNED_REGION_READ_ONLY_SNAPSHOT_V1 = 1
} CettaPrimeOwnedRegionProfile;
/* Explicit private snapshot semantics for retained native select/once,
 * cost:run and raw native weight forms. A raw weight entry acquires its
 * source with zero initial producer work; resume grants advance that call.
 * Grant exhaustion returns DEFERRED without constructing an Observation.
 * A closed call returns its separate occurrences (including zero results);
 * a genuinely open source cut returns one Observation with its complete
 * residual. Closed return rows preserve their environment until projection.
 * Finite demand keeps the complete residual; cost observation is optional
 * for select/once. Ordinary evaluation is unchanged.
 * As with the ordinary Atom API, the term retains the binding identities
 * chosen when it was formed. Authored text is elaborated at reader ingress;
 * capture does not select a scope policy or re-elaborate existing terms.
 * Source roots/services remain held and quiescent through capture and the
 * synchronous original-registry enrollment/acquisition. The result arena
 * must be detached from hashcons/older generations and stays externally
 * owned through retry/disposal. Owned execution selects its own term
 * universe and detached allocation, restoring caller selections on exit.
 * After admission the
 * caller may detach and retire its context; the original residual remains
 * revoked after close. Unsupported admission performs no producer work or
 * handle issuance, so the caller may choose ordinary execution instead.
 * Inspection counts graph reads/probes and preparation-service invocations;
 * byte_limit bounds acceptance of the complete preparation census, not RSS
 * or an allocator ceiling. Existing fatal allocator paths remain fatal. */
CettaContinuationStatus eval_prime_owned_region_outcome(
    Space *source, Arena *output, Arena *persistent, Registry *registry,
    Atom *expression, CettaPrimeOwnedRegionProfile profile,
    size_t inspection_limit, size_t byte_limit, EvalOutcome *outcome);
/* The complete outcome and receiving arena remain held and quiescent.
 * Counts the actual return/operation/region graph; finish the census's
 * queued visitors before reading completeness or bytes. The caller pays
 * census probes in its enclosing inspection account. Borrowed observations
 * explicitly refuse rather than implying ownership of their services. */
void eval_prime_owned_region_storage_census(const EvalOutcome *outcome,
    CettaStorageCensus *census);

void eval_outcome_init(EvalOutcome *outcome);
void eval_outcome_free(EvalOutcome *outcome);
/* Derived, deliberately lossy projections of the exact occurrence bag.  The
   projected ResultSets borrow their Atom payloads from `outcome`; callers own
   only the ResultSet arrays and release them with result_set_free. */
void eval_outcome_project_values(const EvalOutcome *outcome, ResultSet *values);
void eval_outcome_project_faults(const EvalOutcome *outcome, ResultSet *faults);
CettaCount eval_outcome_value_count(const EvalOutcome *outcome);
CettaCount eval_outcome_fault_count(const EvalOutcome *outcome);
CettaEvalZeroStatus eval_outcome_zero_status(const EvalOutcome *outcome);
const char *eval_completion_reason(CettaEvalCompletion completion);
uint64_t eval_current_c_stack_budget_bytes(void);
typedef struct {
    uintptr_t anchor;
    uint64_t budget_bytes;
} CettaEvalCStackBoundary;
/* Capture a service boundary and a budget that leaves room to unwind and
 * publish an evaluator fault before reaching the thread's guard page. */
void eval_c_stack_boundary_capture(CettaEvalCStackBoundary *boundary);

/* ── Evaluation (public API) ───────────────────────────────────────────── */

struct CettaPreparedPureProgram;
/* Library consumers of already evaluated values share the ordinary closed
 * call's dialect, purity, observer and source-currency admission. NULL means
 * run the authored consumer; it never means the callback returned no answer. */
struct CettaPreparedPureProgram *eval_prepare_pure_value_call(
    Space *space, Arena *owner, Atom *call);

/* Exact unary/binary lexical applications over ready values share the pure
 * expression machine used by the relational callable consumer. The owner
 * retains opened source syntax until the program is freed. Execute through
 * program_execute_controlled with those one/two values, not a closed-call
 * rebind. Refusal leaves the ordinary callable application authoritative. */
struct CettaPreparedPureProgram *eval_prepare_pure_lexical_values(
    Space *space, Arena *owner, Atom *callable, uint32_t arity);

/* Each entry owns its mutable query banks and compiled scratch. Reentrant
 * entry suspends that owner; returning restores it. Newly appended structural
 * results are published in `a` before the invocation's storage is released.
 * Identity-bearing foreign resources retain their own lifetime contracts. */
void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_one_step(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs);
/* The same top evaluator with explicit completion. A partial result prefix
 * does not certify exhaustion after fuel, cancellation or service refusal. */
CettaEvalCompletion eval_top_with_registry_completion(Space *space,Arena *arena,Arena *persistent,
    Registry *registry,Atom *expression,ResultSet *results);
void eval_top_with_registry_petta_plan_outcome(
    Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr,
    const struct PettaPlanNode *plan, EvalOutcome *outcome);
void eval_top_with_registry_outcome(
    Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr,
    EvalOutcome *outcome, CettaPrimeNeedAnswerObserver observer,
    void *observer_context);

/* Evaluate a proposal in a host-owned HE context. Load trusted program modules
 * before calling; registry/query spaces must be fresh in-memory projections of
 * the exact observation being validated. No live handles or cached state may
 * be supplied. Context must not be concurrently used by another caller.
 * Nested rho evaluation inherits the policy and budget. fuel_limit must be
 * positive. The host must reject any incomplete outcome or effect_denials > 0,
 * then validate closed output before committing a selected proposal. This API
 * performs no durable commitment. Supply an initialized, empty outcome. */
void eval_top_speculative(CettaLibraryContext *context, Space *space,
    Arena *arena, Arena *persistent, Registry *registry, Atom *expression,
    int fuel_limit, EvalOutcome *outcome);
/* Re-entrant callback used only by the optional PeTTa/libpl adapter. */
bool eval_petta_from_lib_prolog(Arena *a, Atom *expr, ResultSet *results);
/* Reclaim completed top-level temporaries. During a callback into an active
 * evaluator this defers cleanup to that caller's completion boundary. */
void eval_release_temporary_spaces(void);
void eval_reset_form_gc_survivor(void);
void eval_set_default_fuel(int fuel);
/* The symbols whose type declarations are written in HE (the standard
 * library's).  In Prime an `Atom` position of such a declaration holds its
 * argument, as in HE; elsewhere in Prime `Atom` is the top type. */
void eval_prime_set_he_signature_heads(const SymbolId *heads, size_t count);
/* Register the HE-authored declarations of a just-loaded library space. */
void eval_prime_set_he_signature_space(Space *library);
/* Heads the Prime evaluator owns for the relational plan (eval.c), and
 * among them `lift`, which the relational machine hands to the evaluator. */
bool cetta_prime_host_intrinsic_head(SymbolId head);
bool cetta_prime_lift_head(SymbolId head);
/* `scope:profile`, a scope declaration evaluated as a query, in Prime. */
bool cetta_prime_scope_declaration_head(SymbolId head);
/* `unquote` (`*`) and its second stage, in Prime: the evaluator opens code
 * itself, elaborating the term it forms. */
bool cetta_prime_open_code_head(SymbolId head);
int eval_get_default_fuel(void);
int eval_current_effective_fuel_limit(void);
bool eval_current_prefer_rationals(void);
bool eval_current_uses_rust_he_compat_semantics(void) __attribute__((weak));
bool eval_current_builtin_allowed(const char *name) __attribute__((weak));
bool eval_current_pattern_module_enabled(void) __attribute__((weak));
uint32_t eval_current_pattern_control_arity(SymbolId head) __attribute__((weak));
uint32_t eval_current_pattern_operation_arity(SymbolId head) __attribute__((weak));
uint64_t eval_current_native_observer_mask(void) __attribute__((weak));
SymbolId eval_current_pattern_head(SymbolId head) __attribute__((weak));
Space *eval_resolve_space_read(Space *space) __attribute__((weak));
/* Resolve the captured transactional state view. This supplies neither an
 * owner lifetime nor permission to mutate the returned resource. */
StateCell *eval_resolve_state_read(StateCell *cell) __attribute__((weak));
/* Resolve a transactional view and register its live-space dependency with
 * every active retained/native observer. Ordinary execution has a cold
 * no-observer fast path. */
Space *eval_observe_space_read(Space *space) __attribute__((weak));
/* A synchronous borrowed observer of the existing evaluator read hooks.
 * Nested scopes all receive the original tokens. Returning false marks the
 * active evaluation incomplete; it never denotes an empty result. Resource
 * owners outlive the observation and any retained validation of its tokens. */
typedef struct CettaEvalReadScope {
    bool (*observe)(void *context, SpaceReadToken read);
    void *context;
    struct CettaEvalReadScope *previous;
    bool incomplete;
} CettaEvalReadScope;
void eval_read_scope_enter(CettaEvalReadScope *scope);
void eval_read_scope_leave(CettaEvalReadScope *scope);
/* Evaluate the stored code of an interaction request through the ordinary
 * complete-frontier host boundary. The caller owns its request and observer. */
bool eval_interaction_values(Space *space, Arena *arena, Atom *expression,
    Atom ***values_out, size_t *len_out, Atom **error_out);
bool eval_current_profile_enables_dependent_telescope(void) __attribute__((weak));
CettaLanguageId eval_current_language_id(void) __attribute__((weak));
/* Whether the active PeTTa profile checks each step application (the
 * typecheck profiles): native folds have no such evidence. */
bool eval_petta_steps_are_typed(void) __attribute__((weak));
/* The error a minimal instruction answers for arguments outside `expected`:
 * (Error <call> "expected: <expected>, found: <call>"). */
Atom *cetta_call_signature_error(Arena *a, Atom *call, const char *expected);
/* Whether the running PeTTa version is of the typed line (1.1, 1.1.c). */
bool eval_current_petta_typed_line(void) __attribute__((weak));
/* Whether the program extends a classifier PeTTa's typed-call guard
 * evaluates (get-type, get-metatype) with an equation at its one-input arity
 * that can run; a protected default-program definition cannot. */
bool eval_petta_classifier_extended(Space *space, SymbolId classifier)
    __attribute__((weak));
/* The shared profile-aware HE type inference engine. Returned arrays are heap
   allocated and must be freed by the caller; atoms live in `a`. */
uint32_t eval_get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                      Atom ***out_types);
/* The same profile-aware judgment without publishing the subject or result in
   the persistent memo.  Use for nursery-owned machine values whose identities
   are intentionally shorter-lived than the evaluation episode. */
uint32_t eval_get_atom_types_profiled_transient(
    Space *s, Arena *a, Atom *atom, Atom ***out_types);
/* Whether `atom` is a symbol whose types, as the transient judgment reports
   them in `s`, are %Undefined% for as long as the declarations of `s` are
   unchanged: no type annotation in `s` names it, it is not a registry token,
   and its dialect gives it no intrinsic type.  An inference budget can only
   truncate that answer to no types. */
bool eval_symbol_type_undefined(Space *s, Atom *atom);
/* The space an operand denotes, through the registry; NULL otherwise. */
Space *prime_public_space_argument(Arena *arena, Atom *atom);
/* Add a record a scoped judgment published to its theory space, and
 * admit it there. */
bool eval_admit_scoped_record(Space *space, Arena *arena, Atom *record);
/* In Prime, why a name is a protected original or an implementation form
   without an installed editable definition. Standard public bindings may
   be extended or replaced; qualified originals retain their meaning. */
const char *cetta_prime_reserved_name_reason(Atom *name);
/* In Prime, `(Error atom (reserved-name N reason))` when `atom` is an
   equation or a type declaration for such a name N; NULL otherwise. */
Atom *cetta_prime_reserved_definition_error(Arena *a, Atom *atom);
/* Whether `space` is the program's root space, where the public naming
   policy applies. A separately interpreted theory owns its constants. */
bool cetta_prime_space_is_program_space(const Space *space);
uint32_t eval_get_atom_types_profiled_budgeted(
    Space *s, Arena *a, Atom *atom, Atom ***out_types,
    CettaTypeInferenceBudget *budget);
uint32_t eval_get_atom_types_structural_profiled(Space *s, Arena *a,
                                                 Atom *atom,
                                                 Atom ***out_types);
uint32_t eval_get_atom_types_structural_profiled_budgeted(
    Space *s, Arena *a, Atom *atom, Atom ***out_types,
    CettaTypeInferenceBudget *budget);
uint64_t eval_current_max_rational_digits(void);
uint32_t eval_current_num_threads(void);
void eval_set_library_context(CettaLibraryContext *ctx);
/* Preserve the loaded standards and install editable native delegates. Call
 * after standard type declarations and before loading the user's program. */
bool eval_prime_install_standard_library(Space *space, Arena *arena);
/* Select an existing invocation context without initializing its options.
 * The scoped caller restores the returned context before yielding. */
CettaLibraryContext *eval_swap_library_context(CettaLibraryContext *next);
CettaLibraryContext *eval_current_library_context(void);

/* A Prime typing judgment, or Prime's `get-type`, is in progress.  An entry
 * to HE's type engine made inside one is counted for the census.  Evaluation
 * that a judgment asks for is not part of it: suspend clears the mark and
 * returns it, resume restores it. */
void eval_prime_judgment_enter(void);
void eval_prime_judgment_leave(void);
uint32_t eval_prime_judgment_suspend(void);
void eval_prime_judgment_resume(uint32_t depth);
void eval_prime_judgment_note_he_engine(void);

/* Evaluate one closed request of the hosted HOL Light checker's own
 * operations in its private space.  False, with a reason, when the checker
 * cannot be loaded, is already running, or gives no single answer. */
/* The hosted LF kernel: the same contract as the HOL Light checker above. */
bool eval_prime_lf_guest_evaluate(Arena *a, Atom *request,
                                  Atom **answer_out, Atom **reason_out);
bool eval_prime_hol_guest_evaluate(Arena *a, Atom *request,
                                   Atom **answer_out, Atom **reason_out);
bool eval_prime_hol_guest_running(void);

/* Rhometta deferred payload mode: each payload evaluates against a
 * sibling-isolated transactional snapshot.  The flag marks that context; the
 * owner epoch distinguishes payload-local scratch from shared resources. */
bool eval_set_payload_transactional(bool v);
bool eval_payload_transactional(void);
/* Open a quoted deferred payload with its host's ordinary code-opening
 * semantics. Prime preserves its authored ownership and freshens owned names;
 * opening does not execute the code or grant permission to execute it. */
Atom *eval_open_payload_code(Arena *arena, Atom *quoted);
uint64_t eval_set_payload_owner_epoch(uint64_t epoch);
uint64_t eval_payload_owner_epoch(void);
uint64_t eval_next_payload_owner_epoch(void);
bool eval_payload_redirects_begin(Arena *owner);
void eval_payload_redirects_end(void);
/* A frame retains its parent redirects, scratch resources and complete
 * captured Need environment independently of activation. A validity lease
 * detects retirement/reset of borrowed invocation banks without retaining a
 * frame-bank cycle. Output, State/Space and service owners remain caller-held.
 * Frames and activation scopes stay on their creating thread. */
typedef struct CettaEvalPayloadFrame CettaEvalPayloadFrame;
typedef struct {
    CettaEvalPayloadFrame *previous, *entered;
} CettaEvalPayloadScope;
CettaEvalPayloadFrame *eval_payload_frame_open(Arena *owner);
bool eval_payload_frame_enter(CettaEvalPayloadFrame *frame,
                               CettaEvalPayloadScope *scope);
void eval_payload_frame_leave(CettaEvalPayloadScope *scope);
void eval_payload_frame_close(CettaEvalPayloadFrame *frame);
/* Read the world captured when this selected frame opened. The frame owns
 * its environment; retirement or reset of borrowed invocation banks refuses
 * before reading. External resource/service owners stay caller-held.
 * False is service refusal, never permission for a physical-cell fallback.
 * Neither slot is executed. */
bool eval_payload_frame_source_state(CettaEvalPayloadFrame *frame,Arena *arena,
    StateCell *cell,Atom **value,Atom **content_type);
/* Optional ordinary Prime stack provider for the rho payload protocol. The
 * caller keeps source services selected on every open/step/close. Its grant
 * counts stack/return phases, not the work of atomic native operations. */
struct RhocalcPayloadProvider;
const struct RhocalcPayloadProvider *eval_prime_payload_provider(void);
/* Ordinary synchronous Prime evaluation with per-result logical resource
 * views retained until close, including builds without the stack provider.
 * NULL selects the existing evaluator for other language profiles. */
const struct RhocalcPayloadProvider *eval_payload_synchronous_provider(void);
bool eval_payload_note_space_redirect(Space *orig, Space *redirect);
bool eval_payload_note_state_redirect(StateCell *orig, StateCell *redirect);
bool eval_payload_track_scratch_space(Space *space);
bool eval_payload_track_scratch_state(StateCell *cell);
/* Track a `new-space`-created space (persistent-arena struct) so its heap
 * internals are released at session teardown; eval_cleanup_owned_new_spaces
 * frees the ones not bound in the registry (registry-bound ones are freed by
 * the registry teardown pass). Return false before exposing an unowned
 * initialized shell; cleanup ownership does not publish its contents. */
bool eval_track_new_space(Space *space);
void eval_cleanup_owned_new_spaces_for_current_thread(void);
void eval_drain_owned_new_spaces_for_current_thread(void);
void eval_match_decision_cache_free_for_current_thread(void);
void eval_profiled_type_cache_free_for_current_thread(void);
void eval_cleanup_owned_new_spaces(Registry *registry, Space *root);
CettaCount eval_payload_space_redirect_count(void);
bool eval_payload_space_redirect_at(CettaCount idx, Space **orig,
                                    Space **redirect);
CettaCount eval_payload_state_redirect_count(void);
bool eval_payload_state_redirect_at(CettaCount idx, StateCell **orig,
                                    StateCell **redirect);
Registry *eval_current_registry(void);
/* Base PeTTa protects the default program's static primitive predicates.
 * Named owners and extended profiles admit their own equations. */
bool eval_petta_builtin_definition_protected(
    Registry *registry, Space *space, SymbolId head, CettaExprLen input_arity);
Atom *eval_petta_builtin_definition_error(
    Arena *arena, Registry *registry, Space *space, Atom *equation);

Arena *eval_current_persistent_arena(void);
Space *eval_space_snapshot_clone(Space *src, Arena *a);
/* `atom` with each registry token bound in `registry` replaced by its value,
   as HE's tokenizer replaces a token when a module's text is read: the
   module's own `&self` and the tokens its code has bound so far.  `atom`
   itself when nothing is bound. */
Atom *eval_capture_registry_tokens(Registry *registry, Arena *a, Atom *atom);
/* Put the HE whole-word tokens, including &-names, into a newly read form.
 * Inserted values are not traversed or interpreted as further source text.
 * Returns `atom` itself when nothing in it is bound. */
Atom *eval_read_registry_words(Registry *registry, Arena *a, Atom *atom);

/* Nonexecuting Prime judgment view in the active owned world. Inspection
 * follows only permitted immutable origins; explicit promises stay values.
 * A refused traversal leaves the operand opaque. Protocol consumers must
 * validate the resulting shape, and retain the original stored artifact as
 * their admission identity. The caller retains its payload/world lifetime. */
Atom *prime_public_operand_syntax(Arena *arena, Atom *atom);
/* Copy a top-level HE form into its evaluation arena while reading its names.
 * Inserted values are copied without applying the reader to their contents. */
Atom *eval_copy_read_registry_form(Registry *registry, TermUniverse *universe,
                                    Arena *a, AtomId form);
/* The same for a stored form: the id of the form as read, which is `form`
 * itself when nothing in it is bound, without decoding unchanged forms.
 * Changed forms use the existing value ownership/admission path through
 * `storage`. Admission failure returns NONE. */
AtomId eval_read_registry_words_id(Registry *registry, TermUniverse *universe,
                                   Arena *scratch, Arena *storage, AtomId form);

/* Visit each distinct free variable of an executable expression in lexical
 * occurrence order.  Binder policy is shared with evaluator closure/env
 * projection so machines do not publish lexical locals as query answers. */
typedef bool (*CettaFreeVariableVisitor)(
    void *context, VarId var_id, SymbolId spelling,
    Atom *name_key);
bool eval_visit_lexical_free_variables(
    Atom *expression, CettaFreeVariableVisitor visitor,
    void *context);

/* Callability metadata for an extended-profile occurrence program. This is
 * a name classification, not a global builtin or an arity/effect licence. */
bool eval_native_weight_head(SymbolId head);

/* Internal: evaluate an atom fully (recursive).
   type is the expected type (NULL means %Undefined%). */
void metta_eval(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, ResultSet *rs);
/* Evaluate through the same engine as metta_eval while also reporting whether
   the returned frontier is complete for the supplied resource bound. */
void metta_eval_outcome(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                        EvalOutcome *outcome);

/* ── Legacy aliases (transitional, will be removed) ────────────────────── */
/* These exist so that the refactor can proceed incrementally.
   metta_eval_bind and metta_call_bind will be merged into the
   OutcomeSet-based engine. During transition, both old and new types
   are available. */

typedef Outcome ResultWithBindings;
typedef OutcomeSet ResultBindSet;

static inline void rb_set_init(ResultBindSet *rbs) { outcome_set_init(rbs); }
static inline void rb_set_add(ResultBindSet *rbs, Atom *atom, Bindings *b) {
    outcome_set_add(rbs, atom, b);
}
static inline void rb_set_free(ResultBindSet *rbs) { outcome_set_free(rbs); }

bool cetta_petta_profile_admits_arrow_modes(void);
bool cetta_petta_profile_admits_typecheck_ops(void);
bool cetta_petta_profile_admits_native_typecheck_v2(void);
bool cetta_petta_data_op_applies(SymbolId head, CettaExprLen nargs);
void cetta_petta_prepare_document_forms(
    TermUniverse *universe, AtomId *atom_ids, int atom_count);
bool cetta_petta_source_head_resolves_in_engine(SymbolId head, CettaExprLen nargs);
bool cetta_petta_head_names_extension(SymbolId head);
bool cetta_petta_source_head_has_runtime_meaning(
    Space *space, SymbolId head, CettaExprLen nargs);

#endif /* CETTA_EVAL_H */
