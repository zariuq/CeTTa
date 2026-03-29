#ifndef CETTA_GROUNDED_H
#define CETTA_GROUNDED_H

#include "atom.h"

/* Try to dispatch a grounded operation.
   If head is a known grounded op and args are valid, returns result atom.
   Otherwise returns NULL (not a grounded op). */
Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs);

/* Check if a symbol is a known grounded op head (by SymbolId). */
bool is_grounded_op(SymbolId id);

/* Shared fold/reduce binder substitution helper. It substitutes the
   accumulator/item variables and freshens the remaining variables so the step
   expression can be reused safely across evaluator and grounded folds. */
Atom *cetta_fold_bind_step_atom(Arena *a, Atom *atom,
                                SymbolId acc_spelling, Atom *acc_val,
                                SymbolId item_spelling, Atom *item_val);

#endif /* CETTA_GROUNDED_H */
