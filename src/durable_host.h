#ifndef CETTA_DURABLE_HOST_H
#define CETTA_DURABLE_HOST_H
#include "durable_store.h"
#include "eval.h"
#include "native_cost.h"
#include "owned_execution.h"

/* Native coordination boundary; deliberately not an evaluator builtin.
 * A ticket owns the exact observation and authority supplied by the host.
 * Grant indexes in data select this authority; they cannot manufacture it. */
typedef struct CettaHostDecision CettaHostDecision;
typedef struct {
    const char *version;
    /* Trusted, immutable program only; never append observation/worker data.
     * The context contains its loaded pure libraries, not a previous decision's
     * registry or spaces. host:record and host:record-version facts are reserved
     * for the fresh projection. No concurrent use. Both outlive decisions. */
    const Space *space;
    CettaLibraryContext *context;
} CettaHostProgram;
typedef struct {
    CettaDurableScope scope;
    bool writable;
} CettaHostSpaceGrant;
typedef struct {
    const char *name, *version;
    /* Pure, bounded validation. Must enforce method/routing permissions for
     * transport channels. Context is immutable and outlives the decision. */
    bool (*validate)(void *context, const Atom *payload, const Atom *reply);
    void *context;
} CettaHostChannelGrant;
typedef struct {
    const char *input_key, *actor_key, *program_version;
    const CettaHostSpaceGrant *spaces;
    size_t space_count;
    const CettaHostChannelGrant *channels;
    size_t channel_count;
} CettaHostDecisionSpec;
typedef struct {
    char epoch[33], commit_key[33];
    int64_t revision;
    size_t effects;
} CettaHostCommit;

/* Host-supplied specs only, never decoded from a worker proposal. Reads input
 * and actor plus all supplied scopes in one snapshot. Private absence scopes
 * reserve this transition's receipt/outbox without a global revision fence.
 * Store must outlive its decisions; each decision has one caller at a time. */
CettaDurableStatus cetta_host_begin(CettaDurableStore *store,
    const CettaHostDecisionSpec *spec, CettaHostDecision **out);
/* Use when immutable grant/program policy was derived from durable records.
 * The host builds that policy from required, then retains its dependencies in
 * the decision's complete commit/watch observation. A changed policy record
 * refuses begin or acceptance even while the old policy context remains live.
 * required must be non-NULL and may be freed after successful begin; policy
 * contexts still outlive the decision. Hidden dependency views do not add
 * space/channel grants or host:record facts. No per-planning-poll validation. */
CettaDurableStatus cetta_host_begin_with_dependencies(CettaDurableStore *store,
    const CettaHostDecisionSpec *spec, const CettaDurableObservation *required,
    CettaHostDecision **out);
void cetta_host_decision_free(CettaHostDecision *decision);
/* Receipt key remains available after UNKNOWN for recovery inspection. */
const char *cetta_host_decision_key(const CettaHostDecision *decision);
/* View 0 = input, 1 = actor, 2+i = space grant i. Only these views may inform
 * the evaluator's data projection. Policy contexts may additionally depend on
 * the observation supplied to begin_with_dependencies. Build fresh query spaces
 * from these views, without cached
 * state. LLM/time/random observations must first have been recorded as input. */
const CettaDurableSnapshot *cetta_host_view(const CettaHostDecision *decision, size_t index);

/* Build fresh spaces and a private registry from this ticket's exact views,
 * projecting (host:record view-index "key" value) and the corresponding
 * (host:record-version view-index "key" revision position). Version facts
 * permit pure policies to pair recorded worker results with their task or
 * detect stale captured state; they do not grant additional read authority.
 * Run the speculative gate and retain the outcome on this ticket. Previous
 * results are discarded even if this call fails. Expression is host-owned
 * trusted code; worker proposals must enter through recorded input data.
 * This synchronous speculative entry uses HE. Prime uses plan_open and its
 * independently admitted retained source profile. DURABLE_OK means evaluation
 * ran, not that it completed or can be accepted. */
CettaDurableStatus cetta_host_evaluate(CettaHostDecision *decision,
    const CettaHostProgram *program, Atom *expression, int fuel);
typedef struct {
    uint64_t quantum, work_limit;
    size_t preparation_inspections, preparation_bytes, results;
    /* Optional retained return-phase slicing. Clear limit flags select the
     * ordinary unlimited phase with a zero numeric field. A set flag admits
     * zero as an inert slice and requires a value below UINT64_MAX. Source
     * preparation and destination transport retain independent residuals. */
    CettaPrimeReturnSlice return_slice;
    /* Optional caller-paid parent. Admission reserves work_limit; the owned
     * provider retains that lease and refunds its unused balance on disposal.
     * NULL preserves the independently selected ordinary planning path. */
    CettaWorkCredit *credit;
} CettaHostPlanLimits;
typedef struct {
    uint64_t inspections, arena_bytes;
} CettaHostSetupWork;
/* Explicit retained Prime planning over the decision's fresh captured views.
 * Source &self changes are confined to that private snapshot; external I/O
 * and capabilities are not admitted. No durable mutation occurs during
 * planning. Commit revalidates the original host observations and grants.
 * quantum bounds native controller work per poll, work_limit the complete
 * non-refilling purse; neither is a semantic firing or CPU-time bound.
 * Preparation and retained result limits are separate. Zero total work is
 * inert; quantum must be positive and finite. No synchronous fallback.
 * Previous evaluation/ready membership is disposed even on admission failure. */
CettaDurableStatus cetta_host_plan_open(CettaHostDecision *,
    const CettaHostProgram *, Atom *, const CettaHostPlanLimits *);
/* Borrow the stable task for admission/service through the existing ready
 * queue. The decision owns it. Do not change its fields or cancel it directly;
 * use plan_cancel/decision_free so final work and results remain inspectable.
 * Decisions, original intern contexts and trusted host program outlive use. */
CettaOwnedTask *cetta_host_plan_task(CettaHostDecision *);
/* Cumulative structural return work of the retained plan. Preparation is
 * owned by the source adapter; transport is owned by this host destination.
 * Scalar work, allocation and cleanup retain their separate prices. */
bool cetta_host_plan_return_work(
    const CettaHostDecision *, CettaPrimeReturnWork *);
typedef enum {
    CETTA_HOST_PLAN_ADMISSION_READY,
    CETTA_HOST_PLAN_ADMISSION_PAUSED,
    CETTA_HOST_PLAN_ADMISSION_REFUSED,
    CETTA_HOST_PLAN_ADMISSION_INVALIDATED,
    CETTA_HOST_PLAN_PRODUCER,
    CETTA_HOST_PLAN_SOURCE_WORK,
    CETTA_HOST_PLAN_PREPARATION,
    CETTA_HOST_PLAN_CONSUMER,
    CETTA_HOST_PLAN_TRANSPORT,
    CETTA_HOST_PLAN_FINAL_CURRENT,
    CETTA_HOST_PLAN_RETIREMENT,
    CETTA_HOST_PLAN_SETUP,
    CETTA_HOST_PLAN_CLEANUP_WORK,
    CETTA_HOST_PLAN_CLEANUP_POLL,
    CETTA_HOST_PLAN_RETURN_CLEANUP,
    CETTA_HOST_PLAN_DECISION_RELEASE,
} CettaHostPlanTag;
typedef struct CettaHostPlanOccurrence {
    CettaHostPlanTag tag;
    CettaNativeCostEvent event;
} CettaHostPlanOccurrence;
#define CETTA_HOST_PLAN_RECEIPT_CAPACITY 12u
typedef struct CettaHostPlanReceipt {
    CettaHostPlanOccurrence occurrences[CETTA_HOST_PLAN_RECEIPT_CAPACITY];
    CettaOwnedExecutionStep step;
    uint8_t count;
    bool initial_pending,complete;
    bool cleanup;
    CettaOwnedReleaseStep cleanup_status;
} CettaHostPlanReceipt;
typedef struct CettaHostPlanReceiptSummary {
    CettaOwnedExecutionStep step;
    CettaOwnedTaskAdmission last_admission;
    uint64_t admission,producer,consumer,final_is_current,retirements;
    uint64_t setup_work,source_work,total;
    uint64_t cleanup_polls,cleanup_work,return_cleanup,decision_release;
    CettaPrimeReturnWork returns;
    bool initial_pending;
    bool cleanup;
    CettaOwnedReleaseStep cleanup_status;
} CettaHostPlanReceiptSummary;
typedef struct {
    CettaOwnedVisit visit;
    CettaOwnedTaskAccount protocol;
    uint64_t source_work;
    /* Fresh program/snapshot projection precedes the retained provider. Its
     * structural work is emitted once as the first ordered receipt event;
     * destination-arena bytes stay a separate physical/storage observation. */
    CettaHostSetupWork setup;
    CettaPrimeReturnWork returns;
    CettaOwnedReleaseStep cleanup_status;
    uint64_t return_cleanup,decision_release;
    /* Issued synchronously at callback completion, before the next protocol
     * action. A provider/consumer delta is one explicit settlement occurrence;
     * this order is not reconstructed later from the aggregate fields. */
    CettaHostPlanReceipt receipt;
} CettaHostPlanPollAccount;
/* One retained host poll with its exact exposed phase deltas. The provider's
 * other primitive/allocation costs and the enclosing activity's arrival,
 * queue, publication and teardown work remain separately accounted. */
CettaDurableStatus cetta_host_plan_poll_accounted(
    CettaHostDecision *, bool poll, CettaHostPlanPollAccount *);
/* Validate and project the host-issued ordered occurrences. Each event keeps
 * the existing native identity, origin, occurrence, kind and units; the tag
 * states which poll action issued that event. Aggregates are derived views. */
bool cetta_host_plan_receipt_summarize(
    const CettaHostPlanReceipt *,CettaHostPlanReceiptSummary *);
bool cetta_host_plan_receipt_total(
    const CettaHostPlanReceipt *, uint64_t *total);
/* Cancel planning without consuming durable input or attempting commitment.
 * Completed data stays readable; the outcome becomes incomplete/cancelled. */
void cetta_host_plan_cancel(CettaHostDecision *);
/* Retained cancellation through the same task cleanup provider. Zero is inert.
 * First positive cancellation snapshots earned work, marks the outcome
 * cancelled and removes producer membership. Pending source and destination
 * copy storage remain owned by this decision. A pending destination copy is
 * retired before its source owner; its destructor/reset is an indivisible
 * service requiring atomic work-placement permission. The initial stop keeps
 * the source alive even when the caller also grants indivisible cleanup.
 * Allowance counts cleanup units; poll and queue work have separate receipts.
 * A NULL account selects the unobserved path, with no receipt construction.
 * This retains completed data and never attempts durable publication. */
CettaOwnedReleaseStep cetta_host_plan_cancel_poll(CettaHostDecision *,
    uint64_t allowance, bool allow_atomic, uint64_t *used,
    CettaHostPlanPollAccount *);
/* Compose plan cancellation with final evaluation/decision disposal. Final
 * disposal is still indivisible and needs its own unit and atomic grant.
 * Pending replies retain *owner and all its contexts. COMPLETE nulls *owner;
 * the caller must then stop using borrowed task/outcome references. */
CettaOwnedReleaseStep cetta_host_decision_release_poll(CettaHostDecision **owner,
    uint64_t allowance, bool allow_atomic, uint64_t *used,
    CettaHostPlanPollAccount *);
/* Borrowed inspection only; invalidated by re-evaluation or decision free. */
const EvalOutcome *cetta_host_outcome(const CettaHostDecision *decision);
/* Wake a blocked decision only after its dependencies change. Copies metadata
 * only, so the decision and evaluation can be freed immediately afterward. */
CettaDurableStatus cetta_host_watch(const CettaHostDecision *decision,
    size_t max_bytes, CettaDurableWatch **out);

/* Accept only this ticket's stored evaluation. Incomplete/denied evaluation
 * is rejected. The host selects one alternative and records its index.
 * Shape: (host:transition continuation (writes...) (effects...))
 *   (host:put space-grant "key" value) | (host:remove space-grant "key")
 *   (host:send channel-grant payload reply-continuation)
 * All data must be closed and bounded. Acceptance atomically consumes input,
 * replaces actor continuation, changes granted state, appends immutable intents
 * and a commit receipt. The ticket is spent once a commit is attempted, including
 * UNKNOWN; recover from the journal instead of guessing or repeating effects.
 * No evaluation, policy callback or HTTP runs inside the storage transaction. */
CettaDurableStatus cetta_host_accept(CettaHostDecision *decision,
    size_t selected, CettaHostCommit *commit);
#endif
