#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "answer_bank.h"
#include "atom.h"
#include "space.h"
#include "term_canon.h"
#include "variant_instance.h"
typedef struct CettaLibraryContext CettaLibraryContext;
struct CettaPettaTokenSpaceClauseRegistry;

/*
 * Instantiate a canonical PeTTa callable after its arguments have reached
 * values.  The returned atom is the next unevaluated computation; relational
 * control remains with the caller.  NULL means the callable representation
 * or its ABT substitution could not be constructed.
 */
Atom *cetta_petta_apply_ready_callable(
    Arena *arena, Atom *callable, Atom **arguments,
    CettaExprLen argument_count);

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
void outcome_set_add_move(OutcomeSet *os, Atom *atom, Bindings *env);
void outcome_set_free(OutcomeSet *os);

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
typedef enum {
    CETTA_EVAL_COMPLETE = 0,
    CETTA_EVAL_INCOMPLETE_FUEL,
    CETTA_EVAL_INCOMPLETE_CANCELLED,
    CETTA_EVAL_INCOMPLETE_STACK,
    CETTA_EVAL_INCOMPLETE_CAPACITY,
} CettaEvalCompletion;

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

/* ── Evaluation (public API) ───────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_one_step(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs);
void eval_top_with_registry_petta_plan(
    Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr,
    const struct PettaPlanNode *plan, ResultSet *rs);
void eval_top_with_registry_outcome(
    Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr,
    EvalOutcome *outcome, CettaPrimeNeedAnswerObserver observer,
    void *observer_context);
void eval_release_temporary_spaces(void);
void eval_reset_form_gc_survivor(void);
void eval_set_default_fuel(int fuel);
int eval_get_default_fuel(void);
int eval_current_effective_fuel_limit(void);
bool eval_current_prefer_rationals(void);
bool eval_current_uses_rust_he_compat_semantics(void);
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
Arena *eval_current_persistent_arena(void);
Space *eval_space_snapshot_clone(Space *src, Arena *a);

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
void cetta_petta_erase_typecheck_marks_document(
    TermUniverse *universe, AtomId *atom_ids, int atom_count);
bool cetta_petta_source_head_resolves_in_engine(SymbolId head, CettaExprLen nargs);
bool cetta_petta_source_head_has_runtime_meaning(
    Space *space, SymbolId head, CettaExprLen nargs);

#endif /* CETTA_EVAL_H */
