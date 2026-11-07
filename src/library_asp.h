#ifndef CETTA_LIBRARY_ASP_H
#define CETTA_LIBRARY_ASP_H
#include "call_outcome.h"
struct CettaLibraryContext;
/* The asp library's native operations: a ground program is an expression of
 * statements over ground atoms, (rule H (P ...) (N ...)),
 * (choice A (P ...) (N ...)) and (constraint (P ...) (N ...)).  __cetta_lib_asp_solve
 * answers (model (A ...)) or (unsat (certificate STEP ...)); and
 * __cetta_lib_asp_models answers (models ((A ...) ...) (certificate STEP ...)),
 * every stable model once, with the certificate that no further one exists.
 * A step is (rup ID (LIT ...) (HINT ...)) or (loop ID (LIT ...) (ATOM ...) ATOM)
 * over the program's numbering. */
bool cetta_asp_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
/* The clingo library's native operations, with clingo loadable:
 * __cetta_lib_clingo_ground grounds a program text into the statements above;
 * __cetta_lib_clingo_models and __cetta_lib_clingo_solve answer as the asp
 * library does, with (reference clingo) in place of its certificate.  Without
 * clingo they are not this library's to answer. */
bool cetta_clingo_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
