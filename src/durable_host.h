#ifndef CETTA_DURABLE_HOST_H
#define CETTA_DURABLE_HOST_H
#include "durable_store.h"
#include "eval.h"

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
void cetta_host_decision_free(CettaHostDecision *decision);
/* Receipt key remains available after UNKNOWN for recovery inspection. */
const char *cetta_host_decision_key(const CettaHostDecision *decision);
/* View 0 = input, 1 = actor, 2+i = space grant i. Only these views may inform
 * this decision. Build fresh in-memory query spaces from them, without cached
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
 * DURABLE_OK means evaluation ran, not that it completed or can be accepted. */
CettaDurableStatus cetta_host_evaluate(CettaHostDecision *decision,
    const CettaHostProgram *program, Atom *expression, int fuel);
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
