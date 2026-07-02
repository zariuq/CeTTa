#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "answer_bank.h"
#include "atom.h"
#include "space.h"
#include "term_canon.h"
#include "variant_instance.h"
typedef struct CettaLibraryContext CettaLibraryContext;

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

/* ── Evaluation (public API) ───────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_one_step(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs);
void eval_release_temporary_spaces(void);
void eval_reset_form_gc_survivor(void);
void eval_set_default_fuel(int fuel);
int eval_get_default_fuel(void);
int eval_current_effective_fuel_limit(void);
bool eval_current_prefer_rationals(void);
bool eval_current_uses_rust_he_compat_semantics(void);
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

/* Internal: evaluate an atom fully (recursive).
   type is the expected type (NULL means %Undefined%). */
void metta_eval(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, ResultSet *rs);

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

#endif /* CETTA_EVAL_H */
