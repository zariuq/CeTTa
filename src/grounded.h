#ifndef CETTA_GROUNDED_H
#define CETTA_GROUNDED_H

#include "atom.h"

/* Try to dispatch a grounded operation.
   If head is a known grounded op and args are valid, returns result atom.
   Otherwise returns NULL (not a grounded op). */
Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs);

/* Evaluate the allocation-free scalar subset of truth-valued grounded
 * operations.  True means `truth_out` is the exact result; false means the
 * caller must use grounded_dispatch, which remains authoritative for every
 * unsupported operator, payload, arity, or dialect-specific override. */
bool grounded_try_plain_scalar_truth(Atom *head, Atom **args,
                                     uint32_t nargs, bool *truth_out);

/* Select the values carrying the greatest numeric keys while retaining source
   occurrence order and preferring earlier occurrences at a boundary tie.
   The two expression-backed sequences must have equal length.  False leaves
   the caller responsible for its ordinary fallback semantics. */
bool grounded_retain_top_k_numeric_projection(
    Arena *arena, Atom *keys, Atom *values, int64_t requested,
    Atom **result_out);

/* Check if a symbol is a known grounded op head (by SymbolId). */
bool is_grounded_op(SymbolId id);

/* Capability: ops admitted into TYPE-LEVEL conversion (normalize_type_expr
   and the he-prime checked normalizer).  Deterministic, effect-free, and
   independent of live mutable state.  A positive list, not a blocklist: an op
   absent here is left un-dispatched inside a type, so new grounded ops never
   silently become type-runnable. */
bool grounded_op_is_type_pure(SymbolId id);

/* Shared fold/reduce binder substitution helper. It substitutes the
   accumulator/item variables and freshens the remaining variables so the step
   expression can be reused safely across evaluator and grounded folds. */
Atom *cetta_fold_bind_step_atom(Arena *a, Atom *atom,
                                Atom *acc_var, Atom *acc_val,
                                Atom *item_var, Atom *item_val);

#endif /* CETTA_GROUNDED_H */
