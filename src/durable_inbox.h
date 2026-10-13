#ifndef CETTA_DURABLE_INBOX_H
#define CETTA_DURABLE_INBOX_H
#include "durable_store.h"
#include "atom.h"

typedef struct CettaInboxWindow CettaInboxWindow;
typedef enum { INBOX_ROUTED, INBOX_UNSUPPORTED, INBOX_UNAUTHORIZED } CettaInboxDisposition;
typedef struct {
    int64_t id;
    const char *lane; /* host-classified chat/thread, never arbitrary path syntax */
    CettaInboxDisposition disposition;
    const Atom *value; /* screened, validated closed input; no evaluator call here */
} CettaInboxItem;
typedef struct { int64_t revision, next_offset; size_t inserted; } CettaInboxCommit;

/* Native host only. Source is a stable, non-secret bot identity. Source/lane
 * components are 1..64 ASCII letters/digits/._- and never interned as symbols.
 * A window owns the committed cursor observation used to construct one poll.
 * Store outlives windows; serialize each window's calls. */
CettaDurableStatus cetta_inbox_begin(CettaDurableStore *store, const char *source,
                                   CettaInboxWindow **out);
/* Retry admission uses a host clock sample; the untimed begin refuses any
 * scheduled retry. A backward wall jump delays admission; forward jumps make
 * due work eligible. Neither entry advances the cursor. */
CettaDurableStatus cetta_inbox_begin_at(CettaDurableStore *store, const char *source,
                                      int64_t utc_ms, CettaInboxWindow **out);
/* Persist a clock observation paired with this exact pending response before
 * recovery policy sees it. Reopening reuses that sample, never moves the retry
 * deadline by reevaluating at a later time. */
CettaDurableStatus cetta_inbox_poll_clock(CettaInboxWindow *window, int64_t sampled_ms,
                                       int64_t *recorded_ms);
int64_t cetta_inbox_poll_failures(const CettaInboxWindow *window);
/* True only for a pending response with an already recorded hold decision.
 * Do not repeatedly reevaluate it; explicit host repair must release it. */
bool cetta_inbox_poll_held(const CettaInboxWindow *window);
/* Commit the trusted policy result (telegram:retry-at 1 due-ms reason) or
 * (telegram:hold 1 reason). Record response, clock, version and decision in
 * host.poll-control. A retry releases the response without advancing cursor;
 * a hold keeps it. Repeated recovery of that same held response is refused.
 * A successfully classified batch clears retry state and the clock atomically. */
CettaDurableStatus cetta_inbox_resolve_poll(CettaInboxWindow *window,
    const char *program_version, const Atom *decision, int64_t *revision);
int64_t cetta_inbox_offset(const CettaInboxWindow *window);
const char *cetta_inbox_source(const CettaInboxWindow *window);
void cetta_inbox_window_free(CettaInboxWindow *window);
/* Native I/O owner: persist a screened CDV1 poll response against the window's
 * exact cursor and empty host.polls slot. Does not advance the remote cursor.
 * No Atom access; caller serializes this window's lifetime. Success spends it.
 * A pending response prevents beginning another poll for this source. */
CettaDurableStatus cetta_inbox_record_poll(CettaInboxWindow *window,
                                         const void *data, size_t size);
/* Host thread: reopen a recorded poll for bounded trusted classification.
 * The returned record belongs to the window. Commit validates this exact
 * response and removes it atomically with occurrences and cursor advancement,
 * including an empty classified batch. Failed commits preserve the response.
 * Provider schema/offset validation and classification remain host policy. */
CettaDurableStatus cetta_inbox_recover_poll(CettaDurableStore *store, const char *source,
                                         CettaInboxWindow **out);
const CettaDurableRecord *cetta_inbox_poll_response(const CettaInboxWindow *window);
/* Up to 100 items, IDs nonnegative and below INT64_MAX, no contiguity/order
 * assumption. Ledger occurrence, routed inbox reference and cursor commit
 * together. The caller may acknowledge the returned next_offset remotely ONLY
 * on OK. On UNKNOWN reopen and inspect/retry the batch against a fresh window;
 * never infer non-commit. Attempted windows are spent, including empty batches.
 * Dedupe survives consumption: retain host.received while remote replay remains
 * possible. Same identity with different recorded data is CORRUPT, not replace.
 * This API does not parse Telegram or grant worker code poll authority. */
CettaDurableStatus cetta_inbox_commit(CettaInboxWindow *window,
    const CettaInboxItem *items, size_t count, CettaInboxCommit *out);
/* Build the read scope/inbox key needed by the trusted reaction. */
CettaDurableStatus cetta_inbox_keys(const char *source, const char *lane, int64_t id,
                                  char ledger[96], char input[168]);
#endif
