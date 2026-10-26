#ifndef CETTA_TRANSFORM_CORE_H
#define CETTA_TRANSFORM_CORE_H

#include "atom.h"
#include "space.h"

#include <stddef.h>

/* tf:fire (lib/petta/lib_transform.metta) is one firing of a transform: it
 * commits the first kernel candidate of the sources, in proposal order, whose
 * footprint validates, removes its takes and answers (tf:fired INSTANCES).
 * This realization proposes the same rows in the same order, directly over
 * the spaces' occurrence cursors and one trail-backed binding environment,
 * and stops at the first validating candidate when no `let` conjunct can run
 * code.  It validates the footprint by the multiplicity each taken atom
 * needs, and removes the takes through the caller's remove-atom and add-atom in the
 * library's order, so the store it leaves and its answer are the authored
 * firing's.
 *
 * It is admitted only while the live equations of every operation the
 * library defines, and the declarations of `transform`, are the library's
 * own; otherwise it declines and the authored definition runs.  A call whose
 * conjuncts it cannot read the library's way also declines. */

typedef struct {
    void *context;
    /* The space a conjunct's space reference names.  False for an operand
     * that names no space; true with *space_out NULL for a location that
     * holds no space yet, which reads as empty. */
    bool (*resolve_space)(void *context, Atom *space_ref, Space **space_out);
    /* (remove-atom SPACE ATOM) and (add-atom SPACE ATOM) through the caller's
     * contracts: NULL, or the error the builtin answers. */
    Atom *(*remove_atom)(void *context, Atom *space_ref, Atom *atom);
    Atom *(*add_atom)(void *context, Atom *space_ref, Atom *atom);
    /* Every result of evaluating a `let` conjunct's code, in order.  Its
     * variables' values arrive quoted, as the library passes them.  False
     * with *error_out set when evaluation answers an error. */
    bool (*evaluate)(void *context, Arena *a, Atom *expression,
                     Atom ***results_out, size_t *len_out, Atom **error_out);
} CettaTransformHost;

typedef enum {
    CETTA_TRANSFORM_DECLINED = 0, /* the authored definition runs */
    CETTA_TRANSFORM_FIRED,        /* *answer_out is (tf:fired INSTANCES),
                                   * or the count tf:fire-into added */
    CETTA_TRANSFORM_NONE,         /* no candidate validates: no answer */
    CETTA_TRANSFORM_ERROR,        /* *answer_out is the error */
} CettaTransformOutcome;

/* Read-only: whether the live presentation is the library at
 * `library_path`.  The verdict is cached per program revision. */
bool cetta_transform_presentation_admitted(Space *program,
                                           const char *library_path);

/* `call` is (tf:fire SPACE CONJUNCTS TEMPLATE), or (tf:fire-into SPACE
 * CONJUNCTS TEMPLATE DESTINATION), whose instances are added to DESTINATION
 * after the commit and whose answer is how many, with its arguments as data.
 * Nothing is evaluated or removed before the call is admitted. */
CettaTransformOutcome cetta_transform_fire(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);

#endif /* CETTA_TRANSFORM_CORE_H */
