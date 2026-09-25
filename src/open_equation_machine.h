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
 * Anything outside the fragment declines at compile time.  Arithmetic
 * outside int64 runs the shared grounded operations; a raised error ends its
 * branch as a RAISE.  A Space program change between answers stops nothing:
 * calls already entered keep their equations and later calls enter the
 * current ones.  A step the region cannot take exactly stops the cursor
 * with a handoff and publishes nothing further. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"

typedef struct CettaOpenEquationProgram CettaOpenEquationProgram;
typedef struct CettaOpenEquationCursor CettaOpenEquationCursor;
struct PettaProgram;
struct PettaPlanNode;

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
    /* Whether the language profile offers the grounded operation `head`,
     * which the search machine consults before running one directly. */
    bool (*builtin_allowed)(void *context, SymbolId head);
    /* Whether a let binder that only counting operations read takes its
     * producer's count, as the search machine's let/count fusion does. */
    bool count_fusion;
    /* The search machine's own collection head, beside reify and collapse. */
    SymbolId reify_head;
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
/* Whether the entry relation only relays: its equations bind their
 * parameters and hand their goals to the host, so a cursor opened on a call
 * of it would do no work of its own.  A running cursor still calls such a
 * relation in the region, since the call is part of its own execution. */
bool cetta_open_equation_program_entry_relays(
    const CettaOpenEquationProgram *program);

typedef enum {
    CETTA_OPEN_EQUATION_ANSWER = 0,
    CETTA_OPEN_EQUATION_EXHAUSTED,
    /* The region cannot continue exactly; nothing further is published. */
    CETTA_OPEN_EQUATION_HANDOFF,
    /* An operation of the current branch raised the error in `value`, as
     * the machine raises a grounded operation's error; the branch ends and
     * the cursor's remaining alternatives stay. */
    CETTA_OPEN_EQUATION_RAISE,
    /* A body operation outside the fragment: the cursor waits at a host
     * frame for its host to evaluate the goal against its destination
     * (`cetta_open_equation_cursor_host_goal`), and resumes once per
     * answer (`cetta_open_equation_cursor_accept`, then `_continue`).
     * When the goal has no answer left, the host backtracks into the choice
     * below the goal's
     * own choices; that choice's next step drops the host frame.  An answer
     * after which the goal has no choice left drops the frame at once. */
    CETTA_OPEN_EQUATION_HOST,
} CettaOpenEquationStep;

/* A `match` over a space in an equation body is a choice over the rows the
 * space's index offers for the pattern, snapshotted when the match runs
 * (the logical-update view): each row's variables are freshened, the row is
 * unified with the pattern, and the rest of the body runs once per row that
 * unifies.  A space the region cannot read exactly (an overlay, a
 * non-native backend, a named space with a schema) stops the cursor with a
 * handoff. */

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
    /* The run used its activation budget before an answer or exhaustion. */
    CETTA_OPEN_EQUATION_HANDOFF_BUDGET,
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
    /* The space a `match` reads: `reference` is its space operand, `root`
     * the program's space.  NULL when the reference names no space. */
    Space *(*resolve_space)(void *context, Space *root, Arena *arena,
                            Atom *reference);
    /* The host's truth value, in `arena`, as a grounded operation's truth
     * result becomes when the search machine runs the operation. */
    Atom *(*boolean_value)(void *context, Arena *arena, bool value);
    /* The search machine's own path for `(add-atom space payload)` with a
     * ground payload, which it takes once the call's arguments are values:
     * the payload joins `target` and `result_out` is the host's result or
     * error, built in `arena`.  False where the machine would evaluate the
     * call otherwise (an override of the operation, a transaction, a
     * payload that changes the program), before any effect. */
    bool (*admit_ground_atom)(void *context, Space *root, Space *target,
                              Arena *arena, Atom *call, Atom **result_out);
    /* The host's search constrains an equation's destination by the
     * output its body's plan fixes before any effect of the equation, as
     * PeTTa's translation places that output in the left-hand side. */
    bool source_output_constraints;
    /* Only how many answers the cursor publishes is observed: each answer's
     * multiplicity, not its value.  A match that ends the call's own
     * answer, with a data template, then publishes one answer for all its
     * rows, weighted by their count, as the host's count fold does. */
    bool count_only;
    /* A bounded run, for a strategy that restarts the call: at most this
     * many activations (0: no budget), after which the cursor stops with a
     * BUDGET handoff, and no activation deeper than `depth_bound` (0: no
     * bound).  An activation's depth is the number of equation applications
     * on its derivation: the call's own is 1, and a callee, tail calls
     * included, is one deeper than the activation that called it.  An
     * activation past the bound fails and marks the run cut. */
    uint64_t activation_budget;
    uint32_t depth_bound;
    /* The host's decision, at the moment of the call, whether an
     * application headed by a symbol is a call.  A dynamic call whose head's
     * value is a symbol it rejects is data, as the search machine takes it.
     * NULL leaves every such application to the host. */
    bool (*head_callable)(void *context, Space *space, Atom *expression);
    /* Storage the host keeps for the cursor's whole life, released only
     * after the cursor is gone, as the older generation the region shares.
     * A ground value a host goal takes from the region is copied there once
     * and shared from then on, and what an answer brings back from there is
     * shared too.  It never references the host's `answer_arena`.  NULL:
     * every crossing copies. */
    Arena *stable;
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

/* The frames of a cursor form a stack that host goals divide: the frames
 * above a host frame were made after one answer of its goal, and belong to
 * the host's choice made then, above the goal's own choices.  `_next_above`
 * runs only the frames at depth `base` and above, and is EXHAUSTED when none
 * remain there; `_pending_above` says whether any remain.  Plain `_next`
 * and `_pending` are depth 0. */
CettaOpenEquationStep cetta_open_equation_cursor_next_above(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom *const *query_vars,
    Atom **value_out, Atom ***query_values_out);
bool cetta_open_equation_cursor_pending_above(
    const CettaOpenEquationCursor *cursor, uint32_t base);

/* The multiplicity of the answer just published: 1, or under
 * `runtime.count_only` the number of rows a terminal match counted, which
 * sets `folded_out`. */
uint64_t cetta_open_equation_cursor_answer_weight(
    const CettaOpenEquationCursor *cursor, bool *folded_out);

/* How the host takes a HOST step's goal. */
typedef enum {
    /* Evaluate it under its plan. */
    CETTA_OPEN_EQUATION_HOST_SOLVE = 0,
    /* It produces a let binder that only counting operations read: evaluate
     * it as a counted collection, as the host's own let/count fusion does. */
    CETTA_OPEN_EQUATION_HOST_COUNTED,
    /* It is a dynamic call whose elements the region evaluated: apply its
     * head's value to the others, or keep them as data, as the host's own
     * application of evaluated elements decides. */
    CETTA_OPEN_EQUATION_HOST_APPLY,
} CettaOpenEquationHostMode;

/* The goal of the newest HOST step and its destination, built in the answer
 * arena over `vars`: one variable per unbound cell they mention, a call
 * variable standing for itself.  The goal is its source occurrence with the
 * values of the variables it reads in their places, and `plan` is that
 * occurrence's plan: the host evaluates the goal under it, so a value in a
 * variable's place is taken as a value and never evaluated again.  `mode`
 * says how the host takes it.  Valid until the cursor's next step. */
bool cetta_open_equation_cursor_host_goal(
    const CettaOpenEquationCursor *cursor, Atom **goal_out,
    Atom **destination_out, Atom *const **vars_out,
    uint32_t *var_count_out, const struct PettaPlanNode **plan_out,
    CettaOpenEquationHostMode *mode_out);

/* Accept one answer of the newest host frame's goal into the region:
 * `host_values` are the values of the variables its HOST step reported
 * (`host_vars`, which the host passes back because it may have moved them).
 * The cursor copies what it keeps, so it holds nothing of the host's store
 * afterwards.  Frames made from here on start at depth `*base_out`.  With
 * `last` the goal has no answer after this one: its frame leaves the
 * cursor, and the answer's frames join those from depth `last_base`, the
 * frames of the choice that waited for the goal.  False rejects the answer:
 * it does not unify with the cells (the handoff stays NONE), or the cursor
 * cannot take it (the handoff says why). */
bool cetta_open_equation_cursor_accept(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom *const *host_vars, Atom *const *host_values, uint32_t host_count,
    bool last, uint32_t last_base, uint32_t *base_out);
/* Run on from an accepted answer: the frames at depth `base` and above. */
CettaOpenEquationStep cetta_open_equation_cursor_continue(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars, uint32_t base,
    Atom **value_out, Atom ***query_values_out);

/* The cursor borrows the ground parts of its call's arguments from the
 * host's store, which holds them for as long as the cursor may run.  Before
 * the host releases or moves the atoms of `arena`, the cursor takes copies
 * of those it borrows from it.  False only when out of memory. */
bool cetta_open_equation_cursor_detach(CettaOpenEquationCursor *cursor,
                                       const Arena *arena);

CettaOpenEquationHandoff cetta_open_equation_cursor_handoff(
    const CettaOpenEquationCursor *cursor);
/* Whether the depth bound has failed an activation whose head matched: a
 * bounded run that is exhausted and was never cut has seen every answer. */
bool cetta_open_equation_cursor_bound_cut(
    const CettaOpenEquationCursor *cursor);
/* Allow the cursor `budget` activations in all (0: no budget).  A cursor
 * that stopped with a BUDGET handoff continues where it stopped. */
void cetta_open_equation_cursor_set_budget(CettaOpenEquationCursor *cursor,
                                           uint64_t budget);
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
    uint64_t match_candidates;
    uint64_t host_goals;
} CettaOpenEquationStats;
void cetta_open_equation_cursor_stats(const CettaOpenEquationCursor *cursor,
                                      CettaOpenEquationStats *stats_out);

#endif
