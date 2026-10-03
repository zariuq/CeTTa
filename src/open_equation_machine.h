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
    /* Whether a ready call needs no additional typed/dispatch protocol.
     * Used by untyped calls, ready fold adapters and head occurrences;
     * it also detects a first declaration arriving at a live call site. */
    bool (*relation_admitted)(void *context, Space *space, SymbolId head,
                              uint32_t arity);
    /* Whether the compiler may install the complete ordinary call protocol.
     * Engine-owned forms and foreign/tabled/memoized dispatch retain their
     * existing adapters. This authority does not erase any type guard. */
    bool (*call_protocol_admitted)(void *context, Space *space, SymbolId head,
                                   uint32_t arity);
    /* Equation search has already prepared this entry's arguments and
     * installed its type guards. This authority admits the body alone;
     * it does not license a recursive call to skip its call protocol. */
    bool (*body_admitted)(void *context, Space *space, SymbolId head,
                          uint32_t arity);
    /* This entry returns its substituted RHS as data, as CALL_READY_DATA
     * does. Calls made by an evaluated body keep their own occurrence plans. */
    bool entry_body_is_data;
    /* Whether an expression inside an equation head is a relational
     * occurrence, evaluated after the structural match with its value
     * unified with the query subterm, rather than data.  It mirrors the
     * search machine's own classification of equation heads. */
    bool (*head_callable)(void *context, Space *space, Atom *expression);
    /* Whether the language profile offers the grounded operation `head`,
     * which the search machine consults before running one directly. */
    bool (*builtin_allowed)(void *context, SymbolId head);
    /* Present only in profiles whose builtin calls also enumerate authored
     * equations. The result observes an exact head and arity in this space;
     * absence permits native lowering until the program stamp changes. */
    bool (*builtin_equations)(void *context, Space *space, SymbolId head,
                              uint32_t arity);
    /* Whether a goal of `head` with `arity` arguments may answer with goals
     * delayed on its variables (delay_service.h), as a call into the
     * embedded Prolog may.  The region moves its variables between its own
     * slots, so such a goal, and a relation that reaches it, stays with the
     * search machine, which keeps the delayed goals.  NULL: none may. */
    bool (*may_delay)(void *context, SymbolId head, uint32_t arity);
    /* Whether a let binder that only counting operations read takes its
     * producer's count, as the search machine's let/count fusion does. */
    bool count_fusion;
    /* The host's answer materializer beside `collapse`: `reify` where the
     * profile offers it. */
    SymbolId reify_head;
    /* The host's search machine runs the extended profile's `select` and
     * `collect` itself, where the profile offers them; the region then runs
     * them too. */
    bool bounded_collections;
} CettaOpenEquationHost;

/* Term carriers the compiled region interprets. Sharing is visited once;
 * private carriers other than complete internal list cells are rejected. */
bool cetta_open_equation_term_supported(Atom *term);

/* The equations of `head`/`arity` and every relation they call, compiled
 * from the PeTTa program's own occurrence plans when all are in the
 * fragment; NULL declines and names the first reason. */
CettaOpenEquationProgram *cetta_open_equation_program_compile(
    struct PettaProgram *petta, Space *space, SymbolId head, uint32_t arity,
    const CettaOpenEquationHost *host, const char **reason_out);
/* Drop the compiler's reference; each open cursor holds its own. */
void cetta_open_equation_program_retain(CettaOpenEquationProgram *program);
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
    /* A `once` committed to its first answer, or an equation's `(cut)`
     * ran, while host goals made since the once or the equation's call
     * still had choices: the cursor has dropped its own frames above the
     * once or the call, and the host drops its choices above the height
     * `cetta_open_equation_cursor_cut_height` gives, which the host
     * reported for the oldest of those goals.  The choice left newest then
     * resumes the cursor, which continues after the once or the cut.  Only
     * a cursor that has accepted a host answer can report it. */
    CETTA_OPEN_EQUATION_CUT,
    /* An entered collection whose answers carry host constraints has
     * exhausted its producer. Its copied answers and restored caller
     * continue in the host, rather than importing those constraints into
     * the region's store. */
    CETTA_OPEN_EQUATION_COLLECTION,
    /* A pure runtime service failed; the handoff reason distinguishes
     * capacity, stack exhaustion and provider faults from logical failure. */
    CETTA_OPEN_EQUATION_FAULT,
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
    CETTA_OPEN_EQUATION_HANDOFF_SERVICE_ERROR,
    CETTA_OPEN_EQUATION_HANDOFF_STACK,
} CettaOpenEquationHandoff;

typedef enum {
    CETTA_OPEN_TYPE_DEFER = 0,
    CETTA_OPEN_TYPE_ANSWERS,
    CETTA_OPEN_TYPE_PROVED,
    CETTA_OPEN_TYPE_CAPACITY,
    CETTA_OPEN_TYPE_FAULT,
    CETTA_OPEN_TYPE_STACK,
} CettaOpenTypeResult;

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
     * on entry.  The program comes retained for the cursor, which keeps
     * that reference or releases it. */
    CettaOpenEquationProgram *(*current)(void *context, Space *space,
                                         SymbolId head, uint32_t arity,
                                         bool body_is_data);
    /* Full calls that can enter an ordinary body without a typed protocol.
     * Dynamic applications consult this before using a checked-body entry. */
    bool (*relation_admitted)(void *context, Space *space, SymbolId head,
                              uint32_t arity);
    /* A complete pure guard through the existing intrinsic service, under
     * the current profile and classifier admission. Answers refine only
     * `required`, retain order and duplicates, and belong to `arena`; the
     * pointer array is caller-owned. PROVED leaves all bindings unchanged.
     * DEFER has performed no classifier effects. */
    CettaOpenTypeResult (*type_guard)(void *context, Space *space, Arena *arena,
                                      Atom *value, Atom *required,
                                      Atom ***answers, uint32_t *count);
    void *type_context;
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
    /* The search machine's own path for PeTTa's named state over values:
     * `(get-state name)` reads the value the name holds, and
     * `(change-state! name value)` makes it hold `value`; `result_out` is
     * the host's answer, built in `arena`.  False where the machine would
     * do otherwise (an unset name, which raises; a transaction; a value
     * with an unbound cell; an override of the operation), before any
     * effect. */
    bool (*named_state)(void *context, Space *root, Arena *arena, Atom *call,
                        Atom **result_out);
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
    /* Whether the language profile offers the grounded operation `head`,
     * which the search machine consults before running one directly; the
     * region consults it before running one named by a value.  NULL
     * leaves every such application to the host. */
    bool (*builtin_allowed)(void *context, SymbolId head);
    /* SWI-PeTTa's fun/1 for a head beyond its builtins: whether the
     * program's own space defines it (petta_program_function_registered),
     * which get-metatype reports.  NULL leaves such a symbol to the host. */
    bool (*function_registered)(void *context, SymbolId head);
    /* Whether an error raised inside a call dispatched at run time fails
     * that path alone, as the host's own dispatches recover
     * (DispatchErrorScope); the region's dispatches then recover too. */
    bool dispatch_recovers;
    bool raises_abort_collections;
    /* Storage the host keeps for the cursor's whole life, released only
     * after the cursor is gone, as the older generation the region shares.
     * A ground value a host goal takes from the region is copied there once
     * and shared from then on, and what an answer brings back from there is
     * shared too.  It never references the host's `answer_arena`.  NULL:
     * every crossing copies. */
    Arena *stable;
} CettaOpenEquationRuntime;

/* Install the owning machine's pure guard service before the cursor runs.
 * Its context must outlive the cursor, as does the machine's host context. */
void cetta_open_equation_cursor_set_type_guard(
    CettaOpenEquationCursor *cursor, void *context,
    CettaOpenTypeResult (*guard)(void *, Space *, Arena *, Atom *, Atom *,
        Atom ***, uint32_t *));
void cetta_open_equation_cursor_set_collection_raise_policy(
    CettaOpenEquationCursor *cursor, bool abort_collections);

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
    /* The subject and incoming required type, already values. These run
     * the machine's existing guard continuations and preserve refinements. */
    CETTA_OPEN_EQUATION_HOST_TYPE_ACCEPT,
    CETTA_OPEN_EQUATION_HOST_TYPE_MATCH,
    /* An ordinary call whose argument protocol has already completed. */
    CETTA_OPEN_EQUATION_HOST_READY_CALL,
    CETTA_OPEN_EQUATION_HOST_READY_DATA,
    CETTA_OPEN_EQUATION_HOST_GROUNDED,
    /* Cold continuation export emits these directly into the host's goals. */
    CETTA_OPEN_EQUATION_HOST_UNIFY,
    CETTA_OPEN_EQUATION_HOST_RECOVER,
    CETTA_OPEN_EQUATION_HOST_FAIL,
    CETTA_OPEN_EQUATION_HOST_READY_IF,
    CETTA_OPEN_EQUATION_HOST_READY_CASE,
    CETTA_OPEN_EQUATION_HOST_COMMIT_PREFIX,
    CETTA_OPEN_EQUATION_HOST_RELEASE_OWNER,
    CETTA_OPEN_EQUATION_HOST_YIELD,
    CETTA_OPEN_EQUATION_HOST_ABORT_COLLECTION,
    CETTA_OPEN_EQUATION_HOST_READY_MATCH,
    CETTA_OPEN_EQUATION_HOST_READY_MATCH_ROW,
    CETTA_OPEN_EQUATION_HOST_READY_FOLD,
} CettaOpenEquationHostMode;

/* The goal of the newest HOST step and its destination, built in the answer
 * arena over `vars`: one variable per unbound cell they mention, a call
 * variable standing for itself.  The goal is its source occurrence with the
 * values of the variables it reads in their places, and `plan` is that
 * occurrence's plan: the host evaluates the goal under it, so a value in a
 * variable's place is taken as a value and never evaluated again.  `mode`
 * says how the host takes it; `recovers`, that the region issued it inside
 * a call it dispatched at run time, whose handler the goal runs under.
 * Valid until the cursor's next step. */
bool cetta_open_equation_cursor_host_goal(
    const CettaOpenEquationCursor *cursor, Atom **goal_out,
    Atom **destination_out, Atom *const **vars_out,
    uint32_t *var_count_out, const struct PettaPlanNode **plan_out,
    CettaOpenEquationHostMode *mode_out, bool *recovers_out);
/* A terminal host goal needs no cursor continuation or alternatives. Its
 * exported query values share fresh variables with the goal and destination;
 * install these bindings before transferring the goal to the host. The normal
 * accept/continue protocol remains available if the host declines transfer.
 * Exported plans and literals may borrow program storage: retain every program
 * below before closing the cursor while such exports remain reachable. */
bool cetta_open_equation_cursor_host_terminal(
    const CettaOpenEquationCursor *cursor, Atom ***query_values_out);
/* Whether the current host goal's entered caller chain has a
 * remainder that can be exported without replaying argument producers.
 * This is a cold lowering into the host's existing goals, in execution
 * order. Equal cells and first-store slots share variables throughout the
 * remainder and query values. The already executed host goal is not emitted.
 * Native state restores its HOST retry mark; canonical bindings stay live.
 * The host keeps the cursor for older retry alternatives, schedules no
 * OPEN_RESUME for this branch, and retains its program versions. */
bool cetta_open_equation_cursor_host_continuation_supported(
    const CettaOpenEquationCursor *cursor);
bool cetta_open_equation_cursor_export_host_continuation(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom *const *host_vars, uint32_t host_count,
    bool (*emit)(void *context, CettaOpenEquationHostMode mode,
                 Atom *goal, Atom *destination,
                 const struct PettaPlanNode *plan,
                 uint32_t first_branch_len, uint32_t second_branch_len),
    void *context, Atom ***query_values_out);
/* Error scopes must be visible while a host goal runs, even when its answer
 * will resume natively. These markers neither promote a collection nor run
 * its continuation. The host removes them at successful completion. */
bool cetta_open_equation_cursor_export_host_receivers(
    CettaOpenEquationCursor *cursor,
    bool (*emit)(void *context, CettaOpenEquationHostMode mode,
                 uint32_t collection_frame, uint32_t host_height),
    void *context);
/* Finish a transferred collection under a fresh alias map after its
 * producer has rolled back. Components accompany the copied answer list;
 * the host installs them before running the emitted caller continuation. */
bool cetta_open_equation_cursor_export_collection(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    bool (*emit)(void *context, CettaOpenEquationHostMode mode,
                 Atom *goal, Atom *destination,
                 const struct PettaPlanNode *plan,
                 uint32_t first_branch_len, uint32_t second_branch_len),
    void *context, Atom **components_out, Atom ***query_values_out);
/* Append a jointly freshened answer and its delayed components to an
 * entered collection. The receipt names its retained frame and the host
 * choice height beneath its producer. Reaching a bound reports CUT. */
bool cetta_open_equation_cursor_collect_answer(
    CettaOpenEquationCursor *cursor, uint32_t frame, uint32_t saved_height,
    Atom *answer, Atom *components, bool *cut_out);
/* Abandon the named collection and its producer alternatives on a control
 * raise, preserving all older scopes and reporting their host boundary. */
bool cetta_open_equation_cursor_abort_collection(
    CettaOpenEquationCursor *cursor, uint32_t frame, uint32_t saved_height,
    uint32_t *height_out);
/* Borrowed program versions, starting at zero; NULL ends the sequence. */
CettaOpenEquationProgram *cetta_open_equation_cursor_program_at(
    const CettaOpenEquationCursor *cursor, uint32_t index);
/* The host's choice height beneath the goal now awaited: the choices the
 * goal makes lie above it.  A once that commits through the goal's frame
 * reports it (CETTA_OPEN_EQUATION_CUT). */
void cetta_open_equation_cursor_host_height(CettaOpenEquationCursor *cursor,
                                            uint32_t height);
/* Commit a transferred once/cut prefix without restoring its cells or
 * creating an OEM resumption. The exported canonical suffix stays live. */
bool cetta_open_equation_cursor_commit_prefix(CettaOpenEquationCursor *cursor,
    uint32_t floor, uint32_t saved_height, uint32_t *height_out);
/* The height a CETTA_OPEN_EQUATION_CUT step reported. */
uint32_t cetta_open_equation_cursor_cut_height(
    const CettaOpenEquationCursor *cursor);
/* A control raise crossed a transferred producer: discard host choices
 * above this height without retrying, before ordinary error handling. */
uint32_t cetta_open_equation_cursor_raise_height(
    const CettaOpenEquationCursor *cursor);

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
/* Whether this region has a checked positive, effect-free search body.
 * False selects exact continuation, not a different evaluator. */
bool cetta_open_equation_cursor_can_prune_depth(
    const CettaOpenEquationCursor *cursor);

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
