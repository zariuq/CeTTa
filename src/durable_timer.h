#ifndef CETTA_DURABLE_TIMER_H
#define CETTA_DURABLE_TIMER_H
#include "durable_store.h"
#include "atom.h"

typedef struct { int64_t utc_ms; uint64_t monotonic_ms; } CettaClockSample;
typedef struct { char after_key[256]; } CettaTimerCursor;
typedef struct {
    size_t emitted, faults;
    int64_t next_deadline_ms; /* -1 when no future deadline was found */
    bool more_due;
} CettaTimerTick;
typedef void (*CettaTimerFault)(void *context, const char *outbox_key, CettaDurableStatus status);

/* Pure native channel validator for name "timer.after", handler version "1".
 * Payload: (timer:at 1 utc-deadline-ms period-ms policy grace-ms value).
 * Period 0 is one-shot. Policy is FireOnce, SkipMissed or CatchUpAll. UTC
 * deadlines must derive from a recorded clock observation in the reaction.
 * Payload and reply are closed data, each at most 64 KiB. */
bool cetta_timer_validate(void *unused, const Atom *payload, const Atom *reply);

/* Host/evaluator thread only; no evaluator runs here. Accepted immutable
 * timer intents are the registrations. A tick commits each timer's cursor
 * advance (or terminal outcome) and its inbox observations together.
 * FireOnce coalesces due occurrences; SkipMissed records a summary of those
 * later than grace_ms; CatchUpAll emits each, up to 8 per timer per tick.
 * max_events is 1..64; round-robin admission bounds catch-up and avoids one
 * old periodic timer monopolizing the service. Cursor is a scheduling hint,
 * not durable authority. Fault callbacks run outside transactions.
 * A forward wall jump makes timers overdue; a backward jump delays firing
 * until the sampled UTC deadline is reached. Used clock samples are recorded.
 * Storage failure stops admission, without advancing the failing timer. */
CettaDurableStatus cetta_timer_tick(CettaDurableStore *store, CettaTimerCursor *cursor,
    int64_t now_ms, size_t max_events, CettaTimerFault fault, void *context, CettaTimerTick *out);
/* Cancellation and firing race on the same observed cursor. A prior fire is
 * preserved; cancellation records its own terminal observation. Native only. */
CettaDurableStatus cetta_timer_cancel(CettaDurableStore *store, const char *outbox_key,
                                    int64_t now_ms);
bool cetta_clock_sample(CettaClockSample *out);
/* Monotonic wait derived from a sampled UTC deadline. The service resamples
 * UTC at least once per second to notice clock changes and newly accepted work.
 * This helper caps waits at 1000 ms; it performs no sleeping or clock reads. */
uint32_t cetta_timer_wait_ms(int64_t deadline_ms, const CettaClockSample *sample,
                           uint64_t monotonic_now_ms);
#endif
