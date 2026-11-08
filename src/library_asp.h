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
/* Whether `head` is the clingo library's grounder, whose program is made to
 * be solved by later calls: a long-lived value (library.h). */
bool cetta_clingo_result_long_lived(SymbolId head);
/* The sat library's native operations, on the same solver: a clause list
 * of (clause (P ...) (N ...)) over atoms.  __cetta_lib_sat_solve answers
 * (model (A ...)) or (unsat (certificate STEP ...)), an LRAT proof over the
 * atoms numbered in order of first occurrence; __cetta_lib_sat_models answers
 * (models ((A ...) ...) (certificate STEP ...)). */
bool cetta_sat_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
/* The smt library's native operation: __cetta_lib_smt_solve over the sat
 * library's clauses, whose (eq S T) atoms and predicate applications the
 * theory of equality with uninterpreted functions reads. */
bool cetta_smt_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
/* The ipasir library's native operations, with an IPASIR solver loadable:
 * __cetta_lib_ipasir_solve and __cetta_lib_ipasir_models answer as the sat
 * library does, with (reference SIGNATURE) in place of its certificate.
 * Without one they are not this library's to answer. */
bool cetta_ipasir_call(struct CettaLibraryContext *context, Arena *arena,
    Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);
#endif
