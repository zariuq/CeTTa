#include "shared_transition.h"

#include <pthread.h>
#include <stdlib.h>

static pthread_mutex_t g_shared_transition_mutex =
    PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned g_shared_transition_scope_depth;
static _Thread_local unsigned g_shared_transition_lock_depth;

void cetta_shared_transition_scope_enter(void) {
    if (g_shared_transition_scope_depth == (unsigned)-1)
        abort();
    g_shared_transition_scope_depth++;
}

void cetta_shared_transition_scope_leave(void) {
    if (g_shared_transition_scope_depth == 0u ||
        g_shared_transition_lock_depth != 0u) {
        abort();
    }
    g_shared_transition_scope_depth--;
}

bool cetta_shared_transition_scope_active(void) {
    return g_shared_transition_scope_depth != 0u;
}

bool cetta_shared_transition_guard_held_by_current_thread(void) {
    return g_shared_transition_lock_depth != 0u;
}

void cetta_shared_transition_guard_enter(
    CettaSharedTransitionGuard *guard) {
    if (!guard)
        return;
    guard->held = false;
    if (!cetta_shared_transition_scope_active())
        return;
    if (g_shared_transition_lock_depth == (unsigned)-1)
        abort();
    if (g_shared_transition_lock_depth++ == 0u &&
        pthread_mutex_lock(&g_shared_transition_mutex) != 0) {
        abort();
    }
    guard->held = true;
}

void cetta_shared_transition_guard_leave(
    CettaSharedTransitionGuard *guard) {
    if (!guard || !guard->held)
        return;
    if (g_shared_transition_lock_depth == 0u)
        abort();
    guard->held = false;
    g_shared_transition_lock_depth--;
    if (g_shared_transition_lock_depth == 0u &&
        pthread_mutex_unlock(&g_shared_transition_mutex) != 0) {
        abort();
    }
}
