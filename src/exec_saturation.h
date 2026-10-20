#ifndef CETTA_EXEC_SATURATION_H
#define CETTA_EXEC_SATURATION_H

#include "atom.h"
#include "space.h"

/* exec:saturate (lib/lib_exec.metta) fires a finite set of exec directives
 * over a space in rounds until a round adds nothing.  This realization runs
 * the same rounds semi-naively: after the first, a directive is matched only
 * through the facts the previous round added, at each query position in
 * turn, so a round costs what it can newly derive rather than everything
 * derived so far.  Every proposal is published through the caller's
 * add-atom-nodup contract.  The space it leaves and its answer, (), are those
 * of the authored rounds.
 *
 * It is admitted only while the live definitions are the library's own,
 * every directive is data the authored rounds read the same way (a flat
 * query, templates whose heads are constants other than `=` and `:`), and the
 * facts are not the program's own space.  Otherwise it declines, and the
 * authored definition runs. */

typedef struct {
    void *context;
    /* add-atom-nodup of `payload` into the space `space_ref` names: NULL, or
     * the error the builtin answers.  *stored_out is the atom stored when the
     * space grew, NULL when it already held it or refused it. */
    Atom *(*add_nodup)(void *context, Atom *space_ref, Atom *payload,
                       Atom **stored_out);
} CettaExecSaturationHost;

typedef enum {
    CETTA_EXEC_SATURATION_DECLINED = 0, /* the authored definition runs */
    CETTA_EXEC_SATURATION_DONE,         /* *answer_out is () */
    CETTA_EXEC_SATURATION_ERROR,        /* *answer_out is the error */
} CettaExecSaturationOutcome;

/* `call` is `(exec:saturate SPACE DIRECTIVES)` with its arguments as data;
 * `program` holds the live definitions and `facts` is the space SPACE
 * resolves to. */
CettaExecSaturationOutcome cetta_exec_saturate(
    Space *program, Arena *a, Atom *call, Space *facts,
    const CettaExecSaturationHost *host, Atom **answer_out);

#endif /* CETTA_EXEC_SATURATION_H */
