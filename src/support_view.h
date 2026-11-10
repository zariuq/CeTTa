#ifndef CETTA_SUPPORT_VIEW_H
#define CETTA_SUPPORT_VIEW_H

#include "atom.h"
#include "space.h"

/* support:open, support:assert, support:retract and support:why
 * (lib/petta/lib_support.metta) realized incrementally while the library's
 * presentation is live.  The library matches every directive over the whole
 * view in every round and recomputes the founded atoms on every retraction.
 * This realization matches only the rows that read an atom a change added
 * (exec_saturation.h's semi-naive walk), and keeps the founded atoms by
 * source pointers (native/support/support_v1.h) over the derivations it
 * records.  It writes the library's records into the handle's log space, so
 * the handle, the view and the log are the library's, and so are its
 * answers: the lists `added` and `removed` hold the same atoms, in no
 * particular order, and a derivation `why` answers may differ, being a
 * derivation of the atom from atoms of the view.
 *
 * The incremental state is a cache of what the view and the log hold, valid
 * while both are at the revisions its own last writes left them; otherwise
 * it is read again from them.  A call it cannot realize the library's way
 * (directives exec:saturate would not read, a template variable no query
 * pattern binds, a view or an atom with variables, a view holding an atom
 * twice) declines before it writes anything, and the library's definition
 * runs. */

typedef struct {
    void *context;
    /* The space a space reference names, or NULL. */
    Space *(*resolve_space)(void *context, Atom *space_ref);
    /* add-atom into the space space_ref names: NULL, or the error. */
    Atom *(*add)(void *context, Atom *space_ref, Atom *atom);
    /* remove-atom from the space space_ref names: NULL, or the error. */
    Atom *(*remove)(void *context, Atom *space_ref, Atom *atom);
    /* The value of (new-space), or NULL. */
    Atom *(*new_space)(void *context);
} CettaSupportHost;

typedef enum {
    CETTA_SUPPORT_DECLINED = 0,  /* nothing was written; the library runs */
    CETTA_SUPPORT_ANSWERED,      /* *answer_out is the answer */
    CETTA_SUPPORT_ERROR,         /* *answer_out is the error */
} CettaSupportOutcome;

/* Whether the live presentation is the library at `library_path`; cached
 * per program revision. */
bool cetta_support_presentation_admitted(Space *program,
                                         const char *library_path);

/* support:open, support:assert, support:retract or support:why with its two
 * arguments. */
bool cetta_support_head(SymbolId head, CettaExprLen arity);

/* `call` is one of those calls with its arguments' values. */
CettaSupportOutcome cetta_support_call(Space *program, Arena *a, Atom *call,
                                       const CettaSupportHost *host,
                                       Atom **answer_out);

#endif
