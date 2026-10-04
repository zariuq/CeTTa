#ifndef CETTA_HTTP_WORKER_H
#define CETTA_HTTP_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Native bytes only: no arena atoms or evaluator callbacks cross this API.
 * One thread owns all easy handles and runs prepare/observe. The multi handle
 * is touched elsewhere only through curl_multi_wakeup. */
typedef struct CettaHttpWorker CettaHttpWorker;

typedef struct {
    size_t jobs, request_bytes, response_bytes;
} CettaHttpWorkerLimits;

typedef enum {
    HTTP_WORKER_OK, HTTP_WORKER_INVALID, HTTP_WORKER_FULL,
    HTTP_WORKER_DUPLICATE, HTTP_WORKER_CLOSED, HTTP_WORKER_NOMEM,
    HTTP_WORKER_UNAVAILABLE
} CettaHttpWorkerStatus;

typedef struct {
    uint64_t id;                     /* transport handle, not a durable ID */
    const char *method, *url;
    const char *const *headers;      /* complete header lines */
    size_t header_count;
    const void *body;
    size_t body_size;
    uint32_t timeout_ms;
    size_t max_response_bytes;
    bool follow_redirects;
    /* Host-declared repeat safety, never inferred from the HTTP method.
     * False (default) uses a fresh connection and closes it after the transfer
     * to prevent curl's implicit resend on a dead pooled connection. */
    bool idempotent;
    /* Native host configuration; NULL keeps curl defaults. Empty proxy disables
     * environment proxy discovery. ca_file selects a CA trust bundle;
     * certificate and hostname verification remain enabled. */
    const char *proxy, *ca_file;
} CettaHttpRequest;

typedef struct {
    uint64_t id;
    bool started;                   /* may have reached the remote peer */
    bool cancelled;
    bool response_too_large;
    bool response_budget_exceeded;
    bool allocation_failed;
    int transport_code;             /* CURLcode; no token-bearing error text */
    long status;
    bool request_size_known;
    long request_size;              /* libcurl diagnostic, not a retry permit */
    unsigned char *body;
    size_t body_size;
} CettaHttpResult;

typedef enum {
    HTTP_PREPARE_READY,             /* claim is durable: networking may start */
    HTTP_PREPARE_DEFER,             /* no network; retry after a bounded wait */
    HTTP_PREPARE_DROP               /* no network; observe a not-started result */
} CettaHttpPrepare;

typedef enum { HTTP_RECORD_FULL, HTTP_RECORD_MINIMAL } CettaHttpRecordMode;
typedef enum {
    HTTP_RECORD_ACK,                /* the supplied fact is durable */
    HTTP_RECORD_RETRY,              /* keep bytes; retry recording, never HTTP */
    HTTP_RECORD_USE_MINIMAL         /* full result cannot be recorded */
} CettaHttpRecord;

typedef enum {
    HTTP_CANCEL_UNKNOWN,            /* absent, including ACKed/retired: consult journal */
    HTTP_CANCEL_NOT_STARTED,        /* owner will not start this request */
    HTTP_CANCEL_REQUESTED,          /* may already have executed remotely */
    HTTP_CANCEL_TOO_LATE            /* completed; preserve its observation */
} CettaHttpCancel;

typedef struct {
    void *context;
    /* Hooks must be bounded, must not evaluate application code or perform
     * network I/O, and must not join/free this worker from its owner thread. */
    CettaHttpPrepare (*prepare)(void *context, uint64_t id);
    /* MINIMAL supplies metadata with body=NULL/body_size=0. In this mode ACK records
     * an explicit outcome-unrecordable fact, never fabricate remote failure
     * or success. Original bytes remain owned until ACK or worker shutdown. */
    CettaHttpRecord (*observe)(void *context, const CettaHttpResult *result,
                              CettaHttpRecordMode mode);
    /* Required with hooks: signal degraded health outside the failing store.
     * After the retry budget is exhausted the job parks, retaining its bytes
     * and blocking new starts until resume_recording or shutdown. Runs unlocked.
     * It may call resume_recording; never blindly resume an unrepaired failure. */
    void (*recording_stalled)(void *context, uint64_t id,
                             CettaHttpRecordMode mode, unsigned attempts);
    unsigned record_attempts;       /* per mode/resume, 0 selects default 5 */
} CettaHttpWorkerHooks;

CettaHttpWorkerLimits cetta_http_worker_default_limits(void);
CettaHttpWorkerStatus cetta_http_worker_new(const CettaHttpWorkerLimits *limits,
    const CettaHttpWorkerHooks *hooks, CettaHttpWorker **out);

/* Copies request bytes on admission. The handle becomes usable after OK.
 * Outstanding jobs and owned request/body
 * buffers are bounded, including completed but unacknowledged results. Curl,
 * TLS and resolver internals have additional implementation-owned memory. */
CettaHttpWorkerStatus cetta_http_worker_submit(CettaHttpWorker *worker,
    const CettaHttpRequest *request);
CettaHttpCancel cetta_http_worker_cancel(CettaHttpWorker *worker, uint64_t id);
/* Resume a parked recording at FULL after storage repair, using its retained
 * body. The hook may request MINIMAL again. Does not restart HTTP. */
bool cetta_http_worker_resume_recording(CettaHttpWorker *worker, uint64_t id);

/* Ephemeral clients may abandon a result. This operation is refused on a
 * durable worker (one with hooks). Cancellation there must remain observable. */
bool cetta_http_worker_abandon(CettaHttpWorker *worker, uint64_t id);

/* Only available without observe hooks. Transfers the result's body to the
 * caller, which frees it with cetta_http_result_free. */
bool cetta_http_worker_take(CettaHttpWorker *worker, CettaHttpResult *out);
void cetta_http_result_free(CettaHttpResult *result);

/* Wait for a completion/progress notification or timeout, using a monotonic
 * condition variable. Does not run the transport or evaluate any MeTTa. */
uint64_t cetta_http_worker_wait(CettaHttpWorker *worker,
    uint64_t observed_generation, uint32_t timeout_ms);

/* Stop admission, abort active transfers, and join the owner. Every remaining
 * durable result is offered to observe once more. Return the number it could
 * not record; their persisted attempts must recover as uncertain. Call only
 * after excluding concurrent API users. Hooks/context live until this returns. */
size_t cetta_http_worker_free(CettaHttpWorker *worker);
#endif
