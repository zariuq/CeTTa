#ifndef CETTA_SUPERVISOR_H
#define CETTA_SUPERVISOR_H
#include "durable_store.h"
#include <stdbool.h>

typedef struct CettaSupervisor CettaSupervisor;
typedef struct CettaDispatchPermit CettaDispatchPermit;
typedef enum { SUP_READY, SUP_ACTIVE, SUP_UNCERTAIN, SUP_SUCCEEDED,
    SUP_DEAD_LETTER, SUP_UNRESOLVED } CettaSupervisorPhase;
typedef struct {
    int64_t remaining, generation;
    CettaSupervisorPhase phase;
    bool acknowledged;
    unsigned char *payload, *result;
    size_t payload_bytes, result_bytes;
} CettaSupervisorTask;
typedef enum { SUP_COMPLETE, SUP_RECONCILE } CettaSupervisorReplyKind;

/* IDs are never recycled within a ledger. Its epoch is part of reply identity.
 * One ledger owner excludes competing runtimes. Closing requires exclusion
 * from concurrent library calls; permits retain their invalidated owner. */
CettaDurableStatus cetta_supervisor_open(const char *path,
    const CettaDurableLimits *limits, CettaSupervisor **out);
void cetta_supervisor_close(CettaSupervisor *owner);
const char *cetta_supervisor_epoch(const CettaSupervisor *owner);
CettaDurableStatus cetta_supervisor_submit(CettaSupervisor *owner, const char *id,
    int64_t budget, const void *payload, size_t bytes);
CettaDurableStatus cetta_supervisor_get(CettaSupervisor *owner, const char *id,
    CettaSupervisorTask *out);
void cetta_supervisor_task_free(CettaSupervisorTask *task);
CettaDurableStatus cetta_supervisor_snapshot(CettaSupervisor *owner, CettaDurableSnapshot *out);
CettaDurableStatus cetta_supervisor_task_decode(const CettaDurableRecord *record, CettaSupervisorTask *out);

/* Reserve before returning a permit. Ready tasks are ordered by their last
 * transition, so retries move behind other ready work. Active/uncertain work
 * is skipped. A NULL permit with OK means no ready work, not queue completion.
 * Dispatch only after a successful one-use claim; no effect runs in a commit. */
CettaDurableStatus cetta_supervisor_next(CettaSupervisor *owner,
    CettaDispatchPermit **out);
CettaDurableStatus cetta_dispatch_permit_claim(CettaDispatchPermit *permit, bool *granted);
const char *cetta_dispatch_permit_id(const CettaDispatchPermit *permit);
const char *cetta_dispatch_permit_epoch(const CettaDispatchPermit *permit);
int64_t cetta_dispatch_permit_generation(const CettaDispatchPermit *permit);
const void *cetta_dispatch_permit_payload(const CettaDispatchPermit *permit, size_t *bytes);
void cetta_dispatch_permit_free(CettaDispatchPermit *permit);

/* Retry authorization is an operation-specific obligation of the caller,
 * never inferred from a fault. Reconciliation supplies a justified outcome.
 * Stale, wrong-ledger and out-of-phase replies return OK with accepted=false.
 * UNKNOWN/POISONED require close/reopen and inspection, never blind replay. */
CettaDurableStatus cetta_supervisor_reply(CettaSupervisor *owner,
    const char *epoch, const char *id, int64_t generation,
    CettaSupervisorReplyKind kind, bool succeeded, bool retry_authorized,
    const void *result, size_t bytes, bool *accepted);
CettaDurableStatus cetta_supervisor_uncertain(CettaSupervisor *owner,
    const char *epoch, const char *id, int64_t generation, bool *accepted);
CettaDurableStatus cetta_supervisor_abandon(CettaSupervisor *owner,
    const char *id, bool *accepted);
CettaDurableStatus cetta_supervisor_acknowledge(CettaSupervisor *owner,
    const char *epoch, const char *id, int64_t generation, bool *accepted);
CettaDurableStatus cetta_supervisor_checkpoint(CettaSupervisor *owner);
#endif
