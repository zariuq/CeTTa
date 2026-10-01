#ifndef CETTA_DELAY_SERVICE_H
#define CETTA_DELAY_SERVICE_H

#include "atom.h"
#include "match.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * A machine branch's delayed goals (ConstraintPropagation, "A
 * dependency-and-suspension service").  A goal is suspended on unbound
 * variables; binding any of them wakes it, once, onto the woken queue, and
 * the machine runs the woken goals before its next step.  Goals are the
 * clients': the service stores a client's goal term and the variables it
 * waits on, and never reads the term.
 *
 * Every change is recorded with the binding trail's position at the time,
 * so rolling the bindings back to a mark rolls the service back with them:
 * bindings, suspended goals and woken goals are one state with one history
 * (rollback_exec; split_logs_keep_stale_wakeups is what a separate history
 * would leave behind).  A branch with no suspended goal has no service and
 * pays nothing (bind_unwatched).
 */

typedef enum {
    /* The residual goals of one connected component of SWI-Prolog's
     * delayed goals and attributes (petta_libpl). */
    CETTA_DELAY_CLIENT_PROLOG = 1,
} CettaDelayClient;

typedef struct {
    CettaDelayClient client;
    /* The client's goal, owned by the service. */
    Atom *goal;
    /* The variables it waits on. */
    VarId *vars;
    uint32_t var_len;
    /* Suspended, and not yet woken. */
    bool suspended;
} CettaDelayGoal;

typedef struct CettaDelayService CettaDelayService;

typedef enum {
    CETTA_DELAY_SYNC_READY,
    CETTA_DELAY_SYNC_FAILED,
    CETTA_DELAY_SYNC_RAISED,
    CETTA_DELAY_SYNC_CAPACITY,
} CettaDelaySyncStatus;

/* One retained solver client belongs to the branch, alongside ordinary
 * delayed goals. Its history uses the binding trail's marks. clone() copies
 * current state into an independent owner with a fresh history; rollback,
 * commit and rebase follow the same protocol as the goal table.
 *
 * A watched native write queues work through bound(). sync() drains that
 * work against the native bindings and publishes only its consequences in
 * delta. READY is quiescent; FAILED is logical inconsistency; RAISED carries
 * the exact exception. Native allocation/protocol failure is separate.
 *
 * roots() visits owned native terms needed by current and rewindable state.
 * goals() materializes current conditional goals only at an observation or
 * foreign boundary. Those views live until the next client operation. They
 * use the same variable identities as native bindings, without transferring
 * a mutable solver handle to a sibling. A client may not reenter this
 * service from its callbacks. */
typedef struct {
    CettaDelayClient client;
    void *(*clone)(const void *state);
    void (*free)(void *state);
    /* Release engine-local handles while retaining exact current and
     * rewindable states. Called by the owning thread before worker clones. */
    bool (*detach)(void *state);
    bool (*frontier)(void *state, const uint32_t *kept, uint32_t count);
    void (*rollback)(void *state, uint32_t mark);
    void (*commit)(void *state);
    void (*rebase)(void *state, const uint32_t *kept, uint32_t count);
    bool (*watches)(const void *state, VarId var);
    bool (*bound)(void *state, uint32_t mark, VarId var);
    bool (*withdraw)(void *state, uint32_t mark, VarId var);
    bool (*reset)(void *state, uint32_t mark);
    bool (*pending)(const void *state);
    CettaDelaySyncStatus (*sync)(void *state, uint32_t mark, const Bindings *bindings,
                                 Arena *arena, Bindings *delta, Atom **raised);
    bool (*roots)(const void *state,
                   bool (*visit)(void *context, Atom *root), void *context);
    bool (*goals)(const void *state,
                   bool (*visit)(void *context, const CettaDelayGoal *goal),
                   void *context);
} CettaDelayOwnerOps;

/* Ownership transfers on success only. Installing over an existing owner
 * is refused; replacement must be an explicit client history operation. */
bool cetta_delay_owner_install(CettaDelayService *service,
                               const CettaDelayOwnerOps *ops, void *state);
void *cetta_delay_owner(const CettaDelayService *service, CettaDelayClient client);
bool cetta_delay_holds(const CettaDelayService *service);
bool cetta_delay_owner_detach(const CettaDelayService *service);
bool cetta_delay_owner_frontier(const CettaDelayService *service,
                                const uint32_t *kept, uint32_t count);
bool cetta_delay_owner_pending(const CettaDelayService *service);
CettaDelaySyncStatus cetta_delay_owner_sync(
    CettaDelayService *service, uint32_t mark, const Bindings *bindings, Arena *arena,
    Bindings *delta, Atom **raised);
bool cetta_delay_visit_owned_roots(
    const CettaDelayService *service,
    bool (*visit)(void *context, Atom *root), void *context);
/* Export retained conditional goals into the ordinary table at an explicit
 * foreign/answer boundary, then reset the client's current solver epoch.
 * The old epoch remains rewindable until the shared history commits it. */
bool cetta_delay_materialize(CettaDelayService *service, uint32_t mark);

/* What a caller that keeps delayed goals hands a foreign call: its service,
 * NULL while it holds none.  A caller that keeps none passes no view. */
typedef struct {
    const CettaDelayService *service;
    /* Acquire a branch-owned service only when a client needs it. The mark
     * callback reserves an actual binding-history barrier before mutation. */
    CettaDelayService *(*acquire)(void *context);
    uint32_t (*mark)(void *context);
    void *context;
} CettaDelayView;

CettaDelayService *cetta_delay_service_new(void);
void cetta_delay_service_free(CettaDelayService *service);
/* A branch's own copy: the same goals and queue, a fresh history. */
CettaDelayService *cetta_delay_service_clone(const CettaDelayService *service);

/* Whether any goal waits on any variable: the unwatched path's one test. */
bool cetta_delay_watching(const CettaDelayService *service);
bool cetta_delay_serialized(const CettaDelayService *service);
bool cetta_delay_watches(const CettaDelayService *service, VarId var);

/* Suspend `goal` on `vars`, or queue it at once when one of them is
 * already bound (`bound_now`; ConstraintPropagation.table_suspend_bound);
 * `trail_mark` is the binding trail's position of the snapshot taken for
 * this change. */
bool cetta_delay_suspend(CettaDelayService *service, uint32_t trail_mark,
                         CettaDelayClient client, Atom *goal,
                         const VarId *vars, uint32_t var_len,
                         bool bound_now);
/* `var` was bound: every goal suspended on it moves to the woken queue, in
 * the order they were suspended, and leaves the index of every variable it
 * waited on, so it is woken once (table_bind_wakes, table_bind_wf). */
bool cetta_delay_bound(CettaDelayService *service, uint32_t trail_mark,
                       VarId var);
/* Withdraw every goal suspended on `var` without waking it: a client that
 * carried those goals into a call has replaced them with the call's own
 * delayed goals. */
bool cetta_delay_withdraw(CettaDelayService *service, uint32_t trail_mark,
                          VarId var);
/* Take the oldest woken goal; false when the queue is empty. */
bool cetta_delay_take_woken(CettaDelayService *service, uint32_t trail_mark,
                            CettaDelayGoal *goal);
bool cetta_delay_has_woken(const CettaDelayService *service);

/* Undo every change recorded at or after `trail_mark`. */
void cetta_delay_rollback(CettaDelayService *service, uint32_t trail_mark);
/* The binding trail was committed: no change so far can be undone. */
void cetta_delay_commit(CettaDelayService *service);
/* The binding trail was compacted to the checkpoints `kept` (ascending, no
 * repeats), checkpoint `kept[i]` becoming position `i`: a change recorded
 * at mark `t` is now recorded at the last kept checkpoint at or before `t`,
 * so rolling back to a kept checkpoint undoes what it undid before
 * (rollback_rebase), and a change before every kept checkpoint can no
 * longer be undone. */
void cetta_delay_rebase(CettaDelayService *service, const uint32_t *kept,
                        uint32_t kept_len);
/* Visit every goal still to run, suspended or woken and not yet taken: the
 * variables they read must keep their bindings. */
bool cetta_delay_visit_live(const CettaDelayService *service,
                            bool (*visit)(void *context,
                                          const CettaDelayGoal *goal),
                            void *context);
/* The goals suspended on `vars` or, through their variables, on any goal
 * already reached, in the order they were suspended: what a copy of a term
 * over `vars` copies with it.  `*goals` is a malloc'd array (NULL when there
 * is none) of `*count` goals; false when memory ran out. */
bool cetta_delay_reach(const CettaDelayService *service, const VarId *vars,
                       uint32_t var_len, const CettaDelayGoal ***goals,
                       uint32_t *count);

/* The live goals suspended on `var`, for a client that must carry them into
 * a foreign call; returns the total count, writing up to capacity entries.
 * UINT32_MAX signals failed retained-goal materialization or overflow. */
uint32_t cetta_delay_goals_on(const CettaDelayService *service, VarId var,
                              const CettaDelayGoal **goals,
                              uint32_t capacity);

#endif /* CETTA_DELAY_SERVICE_H */
