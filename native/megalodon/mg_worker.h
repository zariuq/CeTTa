#ifndef CETTA_MEGALODON_WORKER_H
#define CETTA_MEGALODON_WORKER_H

/*
 * Deep kernel work on a thread with a large stack.
 *
 * Normalization, inference and proof checking recurse on the structure of
 * terms and proofs.  The kernel bounds that recursion with an explicit depth
 * guard (MgCheck.depth_limit, MG_CAPACITY when reached), and the guard is
 * sized for the stack of these workers, not for whatever thread happens to
 * call the library.  A worker stack is reserved virtual memory: only the
 * pages a computation touches are committed.
 *
 * Interruption.  The evaluator's cancellation state belongs to the calling
 * thread.  While the worker runs, the caller keeps polling it and forwards
 * a request to `call->interrupt`, which the kernel budget's interrupt hook
 * (mg_deep_call_interrupted) reads; the caller then waits for the worker to
 * stop at its next poll.  A caller never abandons a running call.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct MgDeepCall MgDeepCall;
struct MgDeepCall {
    void (*run)(MgDeepCall *call);
    void *context;
    atomic_bool interrupt;
};

/* Run call->run(call) on a worker and wait for it.  `poll` (may be NULL) is
 * called by the waiting thread about every millisecond; when it returns
 * true the interrupt is forwarded.  False when no worker could be started:
 * then the call did not run. */
bool mg_deep_call(MgDeepCall *call, bool (*poll)(void *context),
                  void *poll_context);

/* MgBudget.interrupted hook over an MgDeepCall. */
bool mg_deep_call_interrupted(void *call);

/* The stack size of a worker, in bytes. */
size_t mg_deep_call_stack_bytes(void);

/* Stop idle workers (process teardown, leak checks). */
void mg_deep_call_shutdown(void);

#endif /* CETTA_MEGALODON_WORKER_H */
