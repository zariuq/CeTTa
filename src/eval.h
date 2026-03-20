#ifndef CETTA_EVAL_H
#define CETTA_EVAL_H

#include "atom.h"
#include "space.h"

/* ── Result Set ─────────────────────────────────────────────────────────── */

typedef struct {
    Atom **items;
    uint32_t len, cap;
} ResultSet;

void result_set_init(ResultSet *rs);
void result_set_add(ResultSet *rs, Atom *atom);
void result_set_free(ResultSet *rs);

/* ── Evaluation ─────────────────────────────────────────────────────────── */

/* Evaluate a top-level !-expression. Returns results in rs.
   This is the full HE-style evaluation with recursive rewriting. */
void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs);
void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs);

/* Internal: evaluate an atom fully (recursive).
   type is the expected type (NULL means %Undefined%). */
void metta_eval(Space *s, Arena *a, Atom *atom, Atom *type, int fuel, ResultSet *rs);

/* ── Result with bindings (for interpret_tuple threading) ──────────────── */

typedef struct {
    Atom *atom;
    Bindings bindings;
} ResultWithBindings;

typedef struct {
    ResultWithBindings *items;
    uint32_t len, cap;
} ResultBindSet;

void rb_set_init(ResultBindSet *rbs);
void rb_set_add(ResultBindSet *rbs, Atom *atom, Bindings *b);
void rb_set_free(ResultBindSet *rbs);

#endif /* CETTA_EVAL_H */
