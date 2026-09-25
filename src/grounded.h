#ifndef CETTA_GROUNDED_H
#define CETTA_GROUNDED_H

#include "atom.h"

/* A compact carrier for the closed Int/Float/Bool fragment.  This is a
 * representation of an already admitted grounded value, never a new public
 * Atom kind.  Values may remain in this carrier only inside a deterministic
 * region with no intermediate representation observer. */
typedef enum {
    CETTA_PLAIN_SCALAR_INT = 0,
    CETTA_PLAIN_SCALAR_FLOAT,
    CETTA_PLAIN_SCALAR_BOOL,
} CettaPlainScalarKind;

typedef struct {
    CettaPlainScalarKind kind;
    union {
        int64_t integer;
        double floating;
        bool boolean;
    } as;
} CettaPlainScalar;

/* Try to dispatch a grounded operation.
   If head is a known grounded op and args are valid, returns result atom.
   Otherwise returns NULL (not a grounded op). */
Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs);

/* Interpret a binary numeric dispatch result at the PeTTa effect boundary.
 * These operators produce numbers/booleans or raise; they never return an
 * arbitrary argument as data. False makes no claim about other operators.
 * In particular, an Error-shaped value returned by list access is not a
 * raised error. HE/Prime keep their existing result interpretation. */
bool grounded_numeric_error_is_raised(Atom *head, uint32_t nargs);

static inline bool grounded_result_is_raised_numeric_error(
        Atom *head, uint32_t nargs, Atom *result) {
    /* Scalar results cannot carry an Error constructor. Keep their common
     * path local while sharing the operator/dialect policy for errors. */
    return result && result->kind == ATOM_EXPR && atom_is_error(result) &&
           grounded_numeric_error_is_raised(head, nargs);
}

/* Evaluate the allocation-free scalar subset of truth-valued grounded
 * operations.  True means `truth_out` is the exact result; false means the
 * caller must use grounded_dispatch, which remains authoritative for every
 * unsupported operator, payload, arity, or dialect-specific override. */
bool grounded_try_plain_scalar_truth(Atom *head, Atom **args,
                                     uint32_t nargs, bool *truth_out);

/* Observe or publish the compact scalar carrier at an explicit
 * representation boundary. */
bool grounded_plain_scalar_from_atom(
    const Atom *atom, CettaPlainScalar *value_out);
Atom *grounded_plain_scalar_materialize(
    Arena *arena, const CettaPlainScalar *value);

/* Interpret one admitted scalar operation without allocating intermediate
 * Atoms.  False is an exact specialization refusal: the ordinary grounded
 * dispatcher remains authoritative for overflow, unsupported payloads,
 * dialect-owned spellings, and operations outside this fragment. */
bool grounded_try_plain_scalar_operation(
    Atom *head, const CettaPlainScalar *arguments, uint32_t nargs,
    CettaPlainScalar *value_out);

/* Classify the source spelling and arity of the closed scalar-tree fragment.
   This is a syntactic admission fact only: callers must still establish that
   every leaf is a plain scalar and use one of the exact evaluators below. */
bool grounded_is_plain_scalar_tree_operator(
    Atom *head, uint32_t nargs);

/* Evaluate the positive, effect-free scalar arithmetic fragment used by
 * bounded source-derived execution segments.  True means `value_out` is the
 * exact ordinary grounded result.  False leaves the caller responsible for
 * canonical evaluation.  Dialect-owned spellings, non-scalar operands,
 * division, errors, and results outside Int/Float are deliberately refused. */
bool grounded_try_plain_scalar_arithmetic(
    Arena *arena, Atom *head, Atom **args, uint32_t nargs,
    Atom **value_out);

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
   silently become type-runnable.  Deliberately absent: space mutation
   (add-atom/remove-atom, mork ops), I/O (println!/trace!/print-alternatives!),
   foreign calls (py-*), evaluator-coupled folds, parsing, `size` (reads live
   mutable space state), and every __cetta_lib_ op (semantics not audited).
   Everything here is a pure function of its argument atoms. */
/* Operators only CeTTa's own dialects define (integer floor division and
 * numeric-eq): PeTTa and Hyperon, which he-compat follows, keep them data.
 * is_grounded_op decides them per dialect. */
static inline bool grounded_op_is_cetta_only(SymbolId id) {
    return id == g_builtin_syms.op_floor_div ||
           id == g_builtin_syms.numeric_eq;
}

static inline bool grounded_op_is_type_pure(SymbolId id) {
    return (symbol_flags(g_symbols, id) &
            CETTA_SYMBOL_FLAG_TYPE_PURE_GROUNDED_OP) != 0u;
}

/* Shared fold/reduce binder substitution helper. It substitutes the
   accumulator/item variables and freshens the remaining variables so the step
   expression can be reused safely across evaluator and grounded folds. */
Atom *cetta_fold_bind_step_atom(Arena *a, Atom *atom,
                                Atom *acc_var, Atom *acc_val,
                                Atom *item_var, Atom *item_val);

#endif /* CETTA_GROUNDED_H */
