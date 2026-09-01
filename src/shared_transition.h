#ifndef CETTA_SHARED_TRANSITION_H
#define CETTA_SHARED_TRANSITION_H

#include <stdbool.h>

/*
 * A concurrent execution scope may expose identity-bearing runtime objects to
 * more than one evaluator.  The physical-transition domain supplies one
 * serial realization of each indivisible representation update.  It does not
 * impose a language policy, make compound operations atomic, or assert that
 * effects commute.
 */
void cetta_shared_transition_scope_enter(void);
void cetta_shared_transition_scope_leave(void);
bool cetta_shared_transition_scope_active(void);
bool cetta_shared_transition_guard_held_by_current_thread(void);

typedef struct {
    bool held;
} CettaSharedTransitionGuard;

void cetta_shared_transition_guard_enter(CettaSharedTransitionGuard *guard);
void cetta_shared_transition_guard_leave(CettaSharedTransitionGuard *guard);

/* Bracket one physical operation on a resource shared by concurrent
 * evaluator branches.  Cleanup attributes make release part of every normal
 * C exit path, including early returns.  The guard is deliberately inert
 * outside a concurrent-evaluation scope: synchronization is a realization
 * capability, never a language-level permission check. */
#define CETTA_SCOPED_SHARED_TRANSITION(name)                              \
    __attribute__((cleanup(cetta_shared_transition_guard_leave)))         \
    CettaSharedTransitionGuard name = {0};                                \
    cetta_shared_transition_guard_enter(&(name))

#endif
