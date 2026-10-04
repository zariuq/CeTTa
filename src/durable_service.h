#ifndef CETTA_DURABLE_SERVICE_H
#define CETTA_DURABLE_SERVICE_H
#include "durable_dispatch.h"
#include "durable_worker.h"
#include "durable_timer.h"
#include "telegram_intake.h"

typedef struct CettaDurableService CettaDurableService;
typedef struct {
    CettaTelegramPoll poll;
    const CettaHostProgram *program;
    const Atom *policy;
    int fuel;
} CettaServiceSource;
typedef struct {
    CettaDispatchConfig dispatch;
    const CettaServiceSource *sources;
    size_t source_count; /* 0..16, one stable source per actual bot */
    int worker_listener;
    uid_t worker_uid;
    const char *worker;
    void *context;
    /* Host-thread diagnostics only, with no raw data or credentials. Called
     * once per held/invalid response revision, or on a fatal service error.
     * No evaluation, networking, recursive step, free or store close here.
     * HTTP owner recording faults use dispatch.degraded separately. */
    void (*fault)(void *context, const char *component, CettaDurableStatus status);
} CettaServiceConfig;
typedef struct {
    uint64_t steps, worker_requests, poll_batches, inputs, recoveries, timer_events, faults;
} CettaServiceStats;

/* Native service lifetime. Store and immutable configuration/programs/policies
 * outlive free. Host thread only; pinned HE extended orchestration, no threaded
 * rho. Construction recovers transport claims before admitting anything.
 * Owns worker_listener only on success. No socket binding, process supervision,
 * live configuration, or worker-supplied authority. */
CettaDurableStatus cetta_service_new(CettaDurableStore *store,
    const CettaServiceConfig *config, CettaDurableService **out);
/* One bounded scheduling turn. Pump IPC, due timers and one poll source in
 * round-robin order. At most max_wait_ms (0..100) is spent waiting on IPC;
 * bounded evaluation/storage work may take longer. Does not invoke cognition.
 * Poll decisions use the source's fixed gated MeTTa policy and durable clock.
 * Invalid/incomplete policy results park that response for this lifetime and
 * report it; a recorded hold survives restart. Other sources continue.
 * Storage/runtime faults latch: reopen/recover instead of spinning/retrying
 * an unknown commit. Return the latched error on subsequent steps. */
CettaDurableStatus cetta_service_step(CettaDurableService *service,
                                     unsigned max_wait_ms);
void cetta_service_stats(const CettaDurableService *service, CettaServiceStats *out);
/* Verify application wiring against this owner's immutable registry: source's
 * credential, worker identity, handler name/version, context and planner.
 * expected.credential may be NULL (use that source's credential). */
bool cetta_service_binding(const CettaDurableService *service, const char *source,
                          const char *worker, const CettaDispatchChannel *expected);
/* Application reactions must use host decision tickets. After a successful
 * commit, notify to refresh timer scheduling. These native admission entries
 * preserve the dispatcher's contract: the trusted application chooses ordering,
 * eligibility and retry policy; the loop never blindly dispatches the outbox.
 * Publish accepted worker intents with cetta_worker_register. */
void cetta_service_changed(CettaDurableService *service);
CettaDurableStatus cetta_service_submit(CettaDurableService *service, const char *outbox_key);
CettaDurableStatus cetta_service_cancel(CettaDurableService *service, const char *outbox_key);
/* Stops IPC, then joins the HTTP owner before the store/credentials may close.
 * Returns the dispatcher's count of unrecorded jobs; claims recover uncertain. */
size_t cetta_service_free(CettaDurableService *service);
#endif
