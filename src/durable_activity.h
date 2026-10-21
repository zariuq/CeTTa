#ifndef CETTA_DURABLE_ACTIVITY_H
#define CETTA_DURABLE_ACTIVITY_H
#include "durable_host.h"
#include "durable_worker.h"

typedef struct CettaActivityRuntime CettaActivityRuntime;
typedef enum { ACTIVITY_WORK, ACTIVITY_NOTICE, ACTIVITY_TIMER, ACTIVITY_CANCEL }
    CettaActivityKind;
typedef struct {
    const CettaHostProgram *program; /* immutable durable:activity, activity/1 */
    const char *worker;
    int listener; /* listening CWP1 seqpacket socket; owned on success */
    uid_t worker_uid;
    int fuel;
    size_t ready_capacity;       /* retained candidates */
    size_t worker_peer_capacity; /* live CWP1 connections */
    /* Zero quantum selects the synchronous HE extended policy. A positive
     * finite quantum selects a self-contained Prime default policy and its
     * retained host planner. Work purse/preparation/results have explicit
     * limits; fuel applies only to the synchronous policy. */
    CettaHostPlanLimits planning;
} CettaActivityConfig;
typedef struct {
    uint64_t steps, accepted, published, blocked, timer_events, worker_requests;
    uint64_t max_step_us;
    uint64_t plan_polls, plan_work;
    uint64_t plan_setup, plan_setup_bytes;
    uint64_t plan_preparation, plan_transport;
    uint64_t plan_receipt_events, plan_receipt_units;
    CettaOwnedTaskAccount plan_protocol;
    uint64_t slot_admission, slot_preparation, slot_execution, slot_delivery;
    uint64_t slot_teardown, slot_arrivals, slot_bookkeeping, slot_units;
    uint64_t max_slot_units;
    size_t ready, watch_bytes;
} CettaActivityStats;
typedef struct {
    uint64_t admission, preparation, execution, delivery;
    uint64_t teardown, arrivals, bookkeeping, total;
    bool complete;
} CettaActivitySlotAccount;

/* Trusted host intake, not a worker/proposal entry. IDs identify occurrences
 * permanently; identical resubmission is OK even after inbox consumption.
 * An owner+generation identifies one activity. Body is inert UTF-8 data;
 * external workers receive no scope, actor, program or channel authority.
 * TIMER uses recorded absolute due/period milliseconds; other kinds require
 * zero due/period. CANCEL revokes the wait, not remote execution. */
CettaDurableStatus cetta_activity_record(CettaDurableStore *store,
    const char *id, const char *owner, int64_t generation, CettaActivityKind kind,
    const char *body, int64_t due_ms, int64_t period_ms);

/* One native host/evaluator thread. Store/program/config outlive the runtime.
 * Attaches as its store's sole runtime and rebuilds ready work from the journal.
 * Workers may overlap outside this process. No evaluator/Atom sharing across
 * threads, and no execution of proposals received through CWP1. */
CettaDurableStatus cetta_activity_new(CettaDurableStore *store,
    const CettaActivityConfig *config, CettaActivityRuntime **out);
/* Pump IPC without waiting, admit one timer occurrence, publish one accepted
 * worker request and visit 1..64 input candidates in persistent FIFO rotation.
 * A blocked/foreign occurrence cannot consume another activity's completion.
 * Prime planning polls its retained task once per candidate visit. Captured
 * snapshots may finish stale; acceptance alone checks current observations.
 * Exhausted total work parks the task until its observations change or the
 * runtime is retired, without refilling its purse on an unchanged snapshot.
 * Queue capacity is backpressure: excess work remains durable. Storage faults
 * latch. Whole-step duration is measured; fuel/visits are NOT a wall-time cap.
 * The embedding event loop owns sleeping/wakes, not an activity's await. */
CettaDurableStatus cetta_activity_step(CettaActivityRuntime *runtime, unsigned visits);
/* The same service turn with its complete logical phase account. Native store,
 * socket and allocator instruction/time realization remains a trusted service
 * boundary; their finite configured capacities bound the loops counted here. */
CettaDurableStatus cetta_activity_step_accounted(CettaActivityRuntime *runtime,
    unsigned visits, CettaActivitySlotAccount *account);
/* Wait on the worker's listener/connected peers, an optional native intake fd,
 * and the next absolute timer deadline. Does not consume a hint or evaluate.
 * Outstanding unvisited candidates force a recheck. Caller reads intake and
 * calls step after any wake/timeout. max_wait_ms is 0..1000. */
CettaDurableStatus cetta_activity_wait(CettaActivityRuntime *runtime,
    int intake_fd, unsigned max_wait_ms);
/* Also include a host-owned output fd's write readiness. The host retains
 * at most its configured output buffers, without blocking activity progress. */
CettaDurableStatus cetta_activity_wait_io(CettaActivityRuntime *runtime,
    int intake_fd, int output_fd, unsigned max_wait_ms);
void cetta_activity_stats(const CettaActivityRuntime *runtime, CettaActivityStats *out);
void cetta_activity_free(CettaActivityRuntime *runtime);

/* Exposed for embedding/testing the existing host acceptance boundary.
 * Routing derives solely from native envelopes and immutable worker origins.
 * A selected COMPLETE transition still needs cetta_host_accept. No matching
 * transition means keep the input. With retained planning the ticket is
 * admitted but not evaluated; callers service its borrowed host task first.
 * Worker payloads remain string data. */
CettaDurableStatus cetta_activity_decide(CettaDurableStore *store,
    const CettaActivityConfig *config, const char *input_key, CettaHostDecision **out);
#endif
