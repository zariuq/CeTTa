#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "answer_bank.h"
#include "atom.h"
#include "eval_completion.h"
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
/* Open a consecutive callable prefix to free parameter variables, in
 * outer-to-inner order. Argument values remain in the caller's bindings. */
Atom *cetta_petta_open_callable_parameters(
    Arena *arena, Atom *callable, Atom *const *parameters, CettaExprLen count);

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
int eval_get_default_fuel(void);
int eval_current_effective_fuel_limit(void);
bool eval_current_prefer_rationals(void);
bool eval_current_uses_rust_he_compat_semantics(void);
bool eval_current_builtin_allowed(const char *name) __attribute__((weak));
bool eval_current_pattern_module_enabled(void) __attribute__((weak));
uint32_t eval_current_pattern_control_arity(SymbolId head) __attribute__((weak));
uint32_t eval_current_pattern_operation_arity(SymbolId head) __attribute__((weak));
uint32_t eval_current_native_observer_mask(void) __attribute__((weak));
SymbolId eval_current_pattern_head(SymbolId head) __attribute__((weak));
Space *eval_resolve_space_read(Space *space) __attribute__((weak));
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
