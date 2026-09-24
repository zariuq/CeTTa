#ifndef CETTA_OPEN_EQUATION_MACHINE_H
#define CETTA_OPEN_EQUATION_MACHINE_H

/* Compiled open equation sets: PeTTa relations whose calls may carry
 * unbound variables, run as a first-order machine over an exclusive region.
 *
 * A program holds the equations of every relation reachable from an entry
 * relation, each compiled to
 *  - a head program: two-sided unification of the call's arguments with
 *    the equation's parameters, writing the parameters' variables into the
 *    activation's slots; a relational occurrence in a head (a callable
 *    subterm, as in `(= (f (g $x)) ...)`) keeps its query subterm in a slot
 *    and runs as a call after the whole structural match, in source order,
 *    its value unified with that subterm, and
 *  - a body in administrative normal form: calls with a destination,
 *    arithmetic primitives, comparison tests, pattern binds, tail calls and
 *    `empty` (the defunctionalized machine of DefunctionalizedEquationBodies:
 *    a return frame is a resume point with its captured slots).  Outputs
 *    pass by destination, as in PeTTa's translation: an activation unifies
 *    its body's exposed output term with the caller's destination during
 *    head matching, a let's pattern meets its value's exposed term before
 *    the value's goals run, and an if's output meets a branch's exposed term
 *    before the branch runs, so a demanded constructor prunes the search.
 *
 * A cursor enumerates one call's answers in PeTTa's order: equations in
 * authored order, depth first.  Its store is an exclusive region: variables
 * are cells in the cursor, a binding of a cell older than the newest
 * alternative is trailed, and trying an alternative restores the region to
 * the alternative's marks.  Every binding is occurs-checked, as CeTTa's
 * matcher does.  An answer is published with the values of the call's own
 * variables; unbound region variables become fresh variables of the
 * answer.
 *
 * Anything outside the fragment declines at compile time; a step that the
 * region cannot take exactly at run time (arithmetic on an unbound value, a
 * result outside int64) stops the cursor with a handoff and publishes
 * nothing further.  A Space program change between answers is a resumable
 * handoff: the cursor adopts the current program for new calls. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"

typedef struct CettaOpenEquationProgram CettaOpenEquationProgram;
typedef struct CettaOpenEquationCursor CettaOpenEquationCursor;
struct PettaProgram;

/* The host authorities the compiler consults. */
typedef struct {
    void *context;
    /* Whether the region may own calls of `head`/`arity`: tabled
     * relations, translator rules, memoized relations and relations with a
     * declared function type (whose calls PeTTa checks and whose arguments
     * may stay unevaluated) stay with the host. */
    bool (*relation_admitted)(void *context, Space *space, SymbolId head,
                              uint32_t arity);
    /* Whether an expression inside an equation head is a relational
     * occurrence, evaluated after the structural match with its value
     * unified with the query subterm, rather than data.  It mirrors the
     * search machine's own classification of equation heads. */
    bool (*head_callable)(void *context, Space *space, Atom *expression);
} CettaOpenEquationHost;

/* The equations of `head`/`arity` and every relation they call, compiled
 * from the PeTTa program's own occurrence plans when all are in the
 * fragment; NULL declines and names the first reason. */
CettaOpenEquationProgram *cetta_open_equation_program_compile(
    struct PettaProgram *petta, Space *space, SymbolId head, uint32_t arity,
    const CettaOpenEquationHost *host, const char **reason_out);
/* Drop the compiler's reference; each open cursor holds its own. */
void cetta_open_equation_program_release(CettaOpenEquationProgram *program);
/* The space revision the program was compiled from is still current. */
bool cetta_open_equation_program_is_current(
    const CettaOpenEquationProgram *program);
/* The relations the program holds, in first-entry order: the entry relation
 * first, then every relation its equations call. */
uint32_t cetta_open_equation_program_relation_count(
    const CettaOpenEquationProgram *program);
bool cetta_open_equation_program_relation(
    const CettaOpenEquationProgram *program, uint32_t index,
    SymbolId *head_out, uint32_t *arity_out);

typedef enum {
    CETTA_OPEN_EQUATION_ANSWER = 0,
    CETTA_OPEN_EQUATION_EXHAUSTED,
    /* The region cannot continue exactly; nothing further is published. */
    CETTA_OPEN_EQUATION_HANDOFF,
    /* An operation of the current branch raised the error in `value`, as
     * the machine raises a grounded operation's error; the branch ends and
     * the cursor's remaining alternatives stay. */
    CETTA_OPEN_EQUATION_RAISE,
} CettaOpenEquationStep;

/* Why a cursor stopped with a handoff. */
typedef enum {
    CETTA_OPEN_EQUATION_HANDOFF_NONE = 0,
    /* The host asked the cursor to stop. */
    CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT,
    /* An operation the region cannot run: a call made after a change whose
     * relation's current definition is not admitted to the tier, or a
     * grounded operation with no result. */
    CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED,
    /* Allocation or depth limits. */
    CETTA_OPEN_EQUATION_HANDOFF_CAPACITY,
} CettaOpenEquationHandoff;

/* The host's services to a running cursor. */
typedef struct {
    void *context;
    /* True asks the cursor to stop between alternatives (INTERRUPT). */
    bool (*interrupt)(void *context);
    /* The current program entered at relation (head, arity) in `space`,
     * or NULL when that relation's current definition is not admitted to
     * the tier.  A call made after the Space program or the host's
     * authorities changed enters through it; calls already entered keep the
     * equations they were entered with, and their callers keep their own
     * code, as each call of equation search takes its candidate snapshot
     * on entry.  The cursor retains what it keeps. */
    CettaOpenEquationProgram *(*current)(void *context, Space *space,
                                         SymbolId head, uint32_t arity);
} CettaOpenEquationRuntime;

/* Enumerate the answers of `(head args...)` against the consumer's
 * `expected` value (NULL: an unconstrained destination).  The arguments and
 * the expected value may contain variables: `query_vars[0,
 * query_var_count)` are all of them, and each answer reports their values; a
 * variable an answer leaves unbound reports itself.  The cursor keeps no
 * pointer to them: each call to `_next` passes the same variables, which
 * their owner may have moved.  The expected value is the call's destination, so a demanded
 * constructor bounds the search exactly as in equation search.  Answers and
 * their bindings are built in `answer_arena`. */
CettaOpenEquationCursor *cetta_open_equation_cursor_open(
    CettaOpenEquationProgram *program, Arena *answer_arena,
    Atom *const *args, uint32_t arity, Atom *expected,
    Atom *const *query_vars, uint32_t query_var_count,
    const CettaOpenEquationRuntime *runtime);

/* The next answer: its value and the value of every query variable, in the
 * order they were given (a variable left unbound reports itself, as the
 * atom in `query_vars`). */
CettaOpenEquationStep cetta_open_equation_cursor_next(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom **value_out, Atom ***query_values_out);

/* Whether an alternative remains after the last answer: false means the
 * enumeration is exhausted. */
bool cetta_open_equation_cursor_pending(
    const CettaOpenEquationCursor *cursor);

CettaOpenEquationHandoff cetta_open_equation_cursor_handoff(
    const CettaOpenEquationCursor *cursor);
/* The host's authorities changed (a translator rule, table, memo, type
 * declaration or registration): calls made from now on enter through
 * `runtime.current`. */
void cetta_open_equation_cursor_note_authority_change(
    CettaOpenEquationCursor *cursor);

void cetta_open_equation_cursor_close(CettaOpenEquationCursor *cursor);

/* Counters for gates and measurements. */
typedef struct {
    uint64_t activations;
    uint64_t head_failures;
    uint64_t answers;
    uint64_t trail_writes;
    uint64_t collections;
} CettaOpenEquationStats;
void cetta_open_equation_cursor_stats(const CettaOpenEquationCursor *cursor,
                                      CettaOpenEquationStats *stats_out);

#endif
