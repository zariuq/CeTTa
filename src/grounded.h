#ifndef CETTA_GROUNDED_H
#define CETTA_GROUNDED_H

#include "atom.h"

/* Try to dispatch a grounded operation.
   If head is a known grounded op and args are valid, returns result atom.
   Otherwise returns NULL (not a grounded op). */
Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs);

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
