#ifndef CETTA_NATIVE_FOLD_H
#define CETTA_NATIVE_FOLD_H

#include "atom.h"
#include "space.h"

/*
 * Native folds over a flat match bag: PeTTa's (foldall function generator
 * initial) with the generator (match space pattern template), whichever
 * executor runs it (the evaluator's fold, the search machine).
 *
 * The step, a builtin operation or one pure equation, is decoded into the
 * integer fold language and classified (fold_algebra.h).  The class fixes
 * the consumer's observation contract, and the contract alone decides which
 * producer route may feed it:
 *  - every fold admits an ordered scan of one pattern's integer column;
 *  - a constant item (or an item the step ignores) is a run of one step,
 *    taken as a power when the contract is an affine or commutative fold
 *    and iterated otherwise;
 *  - a commutative fold may read an unordered join through its moments.
 * Anything else declines and the caller's ordinary fold runs.  So does a
 * zero divisor or an intermediate that cannot continue exactly: nothing is
 * published before the fold completes.
 */
typedef enum {
    CETTA_NATIVE_FOLD_NOT_ADMITTED = 0,
    CETTA_NATIVE_FOLD_DECLINED,
    CETTA_NATIVE_FOLD_ORDERED_SCAN,
    CETTA_NATIVE_FOLD_RUN,
    CETTA_NATIVE_FOLD_MOMENTS,
} CettaNativeFoldRoute;

/* On a scan, run or moments route, *answer is the fold's one value,
 * allocated in `arena`.  The route taken is counted. */
CettaNativeFoldRoute cetta_native_foldall(
    Space *space, Arena *arena, Atom *function, Atom *generator,
    Atom *initial, Atom **answer);

/* Whether `function` is an admitted step that ignores its item and has a
 * value for every number of steps from `initial`: a fold of it observes
 * only how many answers its generator gives, which the caller may count
 * by running the generator itself.  Decided before anything runs. */
bool cetta_native_fold_counts_answers(Space *space, Arena *arena,
                                      Atom *function, Atom *initial);
/* That step taken `count` times from `initial` into *answer; false only
 * when `function` is not such a step or allocation fails. */
bool cetta_native_fold_repeat(Space *space, Arena *arena, Atom *function,
                              Atom *initial, uint64_t count, Atom **answer);

#endif /* CETTA_NATIVE_FOLD_H */
