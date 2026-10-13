#ifndef CETTA_DURABLE_WORKER_HOST_H
#define CETTA_DURABLE_WORKER_HOST_H
#include "durable_worker.h"
#include "atom.h"

/* Host/evaluator thread only. Channel worker.request, handler version 1.
 * context is the immutable, host-authorized worker ID string, not a proposal.
 * Payload: (worker:request 1 "worker" "observation"). Reply is closed data.
 * Observations obey the IPC UTF-8/size bounds; replies are at most 64 KiB. */
bool cetta_worker_validate(void *context, const Atom *payload, const Atom *reply);

/* Project one accepted immutable intent into the worker queue. Rechecks name,
 * version and worker authority. The original outbox record participates in the
 * publication transaction's read set. Stable task ID derives from its journal
 * epoch/revision/position, not a process-local handle. Recover by calling again;
 * publication and result receipt are idempotent, including after consumption.
 * Original intent/reply remain durable and host.worker-origins binds them to
 * the task. No input is consumed here and no proposal is evaluated.
 * Keep the intent, origin, task and result until their dependency/retention
 * policy permits removal. A result is recorded input, never an accepted action.
 * The trusted service reaction must match it to its waiting continuation and
 * explicitly decide what to do with a stale draft against current state. */
CettaDurableStatus cetta_worker_register(CettaDurableStore *store,
    const char *outbox_key, const char *authorized_worker, char task_id[65]);
#endif
