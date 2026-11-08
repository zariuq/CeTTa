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
 * It is admitted only while the live equations and declarations are the library's own,
 * every directive is data the authored rounds read the same way (a flat
 * query, templates whose heads are constants other than `=` and `:`), and the
 * facts are not the program's own space.  Otherwise it declines, and the
 * authored definition runs. Hosts must retain ordinary matching when caller
 * constraints or delayed goals can affect a query's rows. */

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

/* The library's held-argument presentation. This permits a host boundary,
 * not execution: the bound call must still pass the admission below. */
bool cetta_exec_saturation_presentation_admitted(Space *program);

/* Read-only call admission. No argument is evaluated and no fact is
 * published on a decline. */
bool cetta_exec_saturation_admitted(Space *program, Atom *call, Space *facts);

/* A directive the authored rounds read as data: (exec KEY (, P ...) (, T ...))
 * with a flat query of one or more patterns, each read by the space's own
 * conjunction query as `match` reads it, and templates whose heads are
 * constants other than `=` and `:`, so a published fact is never a
 * definition.  The parts point into the directive. */
typedef struct {
    Atom *key;
    Atom **patterns;
    CettaExprLen npatterns;
    Atom **templates;
    CettaExprLen ntemplates;
} CettaExecDirective;

bool cetta_exec_directive_read(Atom *directive, CettaExecDirective *out);

/* A row of a directive's query: VISIT answers false to stop the walk. */
typedef bool (*CettaExecRowVisit)(void *context,
                                  const CettaExecDirective *directive,
                                  Bindings *row);

/* Every row of the directive's query over `facts` when `delta` is NULL;
 * otherwise every row that reads at least one atom of `delta`, each query
 * position in turn reading `delta` and the other patterns `facts`, so a row
 * reading several atoms of `delta` is visited once per such position.
 * False when a visit stopped the walk or memory ran out. */
bool cetta_exec_directive_rows(Space *facts, Space *delta, Arena *a,
                               const CettaExecDirective *directive,
                               CettaExecRowVisit visit, void *context);

/* `call` is `(exec:saturate SPACE DIRECTIVES)` with its arguments as data;
 * `program` holds the live definitions and `facts` is the space SPACE
 * resolves to. */
CettaExecSaturationOutcome cetta_exec_saturate(
    Space *program, Arena *a, Atom *call, Space *facts,
    const CettaExecSaturationHost *host, Atom **answer_out);

#endif /* CETTA_EXEC_SATURATION_H */
