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
 * over the program's numbering.  With clingo loadable, __cetta_lib_asp_ground
 * grounds a program text into such statements and __cetta_lib_asp_clingo_models
 * answers (clingo-models ((A ...) ...)) by clingo's solver; without it they
 * are not this library's to answer. */
bool cetta_asp_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
