#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "answer_bank.h"
#include "atom.h"
#include "eval_completion.h"
#include "space.h"
#include "term_canon.h"
#include "variant_instance.h"
typedef struct CettaLibraryContext CettaLibraryContext;
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
void outcome_set_add_move(OutcomeSet *os, Atom *atom, Bindings *env);
void outcome_set_free(OutcomeSet *os);
/* Stage an independent outcome vector through one graph relocation session.
 * The caller owns both vectors until it commits or abandons the staged copy. */
bool outcome_set_relocate_clone(OutcomeSet *dst, const OutcomeSet *src,
                               Arena *owner, AtomDeepCopySession *session);

/* A retained execution of the existing HE canonical frames. Structural
 * input and logical bindings are copied; source spaces, registry, library
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
} EvalOutcome;

typedef enum {
    CETTA_EVAL_ZERO_COMPLETE = 0,
    CETTA_EVAL_NONZERO,
    CETTA_EVAL_ZERO_PENDING,
} CettaEvalZeroStatus;

typedef void (*CettaPrimeNeedAnswerObserver)(
    Atom *answer, const PrimeNeedReceipt *receipt, void *context);
struct PettaPlanNode;

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

/* Each entry owns its mutable query banks and compiled scratch. Reentrant
 * entry suspends that owner; returning restores it. Newly appended structural
 * results are published in `a` before the invocation's storage is released.
 * Identity-bearing foreign resources retain their own lifetime contracts. */
void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_one_step(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs);
void eval_top_with_registry_petta_plan_outcome(
    Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr,
    const struct PettaPlanNode *plan, EvalOutcome *outcome);
void eval_top_with_registry_outcome(
    Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr,
    EvalOutcome *outcome, CettaPrimeNeedAnswerObserver observer,
    void *observer_context);
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
bool eval_current_uses_rust_he_compat_semantics(void);
bool eval_current_builtin_allowed(const char *name) __attribute__((weak));
bool eval_current_pattern_module_enabled(void) __attribute__((weak));
uint32_t eval_current_pattern_control_arity(SymbolId head) __attribute__((weak));
SymbolId eval_current_pattern_head(SymbolId head) __attribute__((weak));
Space *eval_resolve_space_read(Space *space) __attribute__((weak));
bool eval_current_profile_enables_dependent_telescope(void) __attribute__((weak));
CettaLanguageId eval_current_language_id(void) __attribute__((weak));
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
/* In Prime, why `name` is a name Prime interprets itself (a grounded
   operation, a builtin of its profile, a judgment, or a form of the
   evaluator), so that a program may not define or declare it; NULL for any
   other name, and in every other language. */
const char *cetta_prime_reserved_name_reason(Atom *name);
/* In Prime, `(Error atom (reserved-name N reason))` when `atom` is an
   equation or a type declaration for such a name N; NULL otherwise. */
Atom *cetta_prime_reserved_definition_error(Arena *a, Atom *atom);
/* Whether `space` is the space the program runs in (the root space of the
   current evaluation), where those names may not be defined. */
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
uint64_t eval_set_payload_owner_epoch(uint64_t epoch);
uint64_t eval_payload_owner_epoch(void);
uint64_t eval_next_payload_owner_epoch(void);
bool eval_payload_redirects_begin(Arena *owner);
void eval_payload_redirects_end(void);
bool eval_payload_note_space_redirect(Space *orig, Space *redirect);
bool eval_payload_note_state_redirect(StateCell *orig, StateCell *redirect);
bool eval_payload_track_scratch_space(Space *space);
bool eval_payload_track_scratch_state(StateCell *cell);
/* Track a `new-space`-created space (persistent-arena struct) so its heap
 * internals are released at session teardown; eval_cleanup_owned_new_spaces
 * frees the ones not bound in the registry (registry-bound ones are freed by
 * the registry teardown pass). */
void eval_track_new_space(Space *space);
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
