#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "atom.h"
#include "space.h"

typedef struct CettaLibraryContext CettaLibraryContext;

/* ── Outcome: the unified result type for all evaluator functions ───────── */
/* Every evaluator function returns a set of outcomes (atom + bindings).
   This replaces the former split between ResultSet and ResultBindSet.
   "Plain" evaluation is just projection: outcome.atom. */

typedef struct Outcome {
    Atom *atom;
    Bindings env;
} Outcome;

typedef struct OutcomeSet {
    Outcome *items;
    uint32_t len, cap;
} OutcomeSet;

void outcome_set_init(OutcomeSet *os);
void outcome_set_add(OutcomeSet *os, Atom *atom, const Bindings *env);
void outcome_set_free(OutcomeSet *os);

/* ── ResultSet: public API for top-level results (atoms only) ──────────── */
/* This is the user-facing result type. Internally, the evaluator works
   with OutcomeSet; ResultSet is produced by dropping bindings at the end. */

typedef struct ResultSet {
    Atom **items;
    uint32_t len, cap;
} ResultSet;

void result_set_init(ResultSet *rs);
void result_set_add(ResultSet *rs, Atom *atom);
void result_set_free(ResultSet *rs);

/* ── Evaluation (public API) ───────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs);
void eval_release_temporary_spaces(void);
void eval_set_default_fuel(int fuel);
int eval_get_default_fuel(void);
void eval_set_library_context(CettaLibraryContext *ctx);
Registry *eval_current_registry(void);
Arena *eval_current_persistent_arena(void);

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
