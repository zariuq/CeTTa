#ifndef CETTA_TELEGRAM_SCHEDULER_H
#define CETTA_TELEGRAM_SCHEDULER_H
#include "telegram_agent.h"
#include "durable_service.h"

typedef struct CettaTelegramScheduler CettaTelegramScheduler;
typedef struct {
    CettaTelegramAgent agent;
    size_t pending_inputs, pending_effects, watch_bytes;
    void *context;
    /* Host thread, metadata only. No recursive step/free/store access. */
    void (*fault)(void *context, const char *component, const char *key, CettaDurableStatus status);
} CettaTelegramSchedulerConfig;
typedef struct {
    uint64_t evaluations, accepted, blocked, admissions, published, faults;
} CettaTelegramSchedulerStats;

/* One evaluator-thread owner for this source. Service/store/config outlive it.
 * Rebuilds pending output from immutable intents; no live config/admin writers.
 * Limits are explicit backpressure, not silent eviction of unresolved work.
 * Construction/step may publish worker tasks and admit HTTP through service. */
CettaDurableStatus cetta_telegram_scheduler_new(CettaDurableStore *store,
    CettaDurableService *service, const CettaTelegramSchedulerConfig *config,
    CettaTelegramScheduler **out);
/* Bounded number (1..64) of candidate visits, alternating input and output.
 * Metadata watches skip unchanged blocked work. Call service_step separately
 * even when no cognitive worker is connected. Storage/queue faults latch. */
CettaDurableStatus cetta_telegram_scheduler_step(CettaTelegramScheduler *scheduler,
                                               unsigned visits);
void cetta_telegram_scheduler_stats(const CettaTelegramScheduler *scheduler,
                                  CettaTelegramSchedulerStats *out);
void cetta_telegram_scheduler_free(CettaTelegramScheduler *scheduler);
#endif
