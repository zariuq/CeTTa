#ifndef CETTA_DURABLE_WORKER_H
#define CETTA_DURABLE_WORKER_H
#include "durable_store.h"
#include <stdbool.h>
#include <sys/types.h>

#define CETTA_WORKER_BODY_MAX 65536u
#define CETTA_WORKER_ID_MAX 64u
#define CETTA_WORKER_PACKET_MAX (6u+CETTA_WORKER_ID_MAX+CETTA_WORKER_BODY_MAX)
typedef enum {
    WORKER_NEXT=1, WORKER_RESULT=2, WORKER_RECEIPT=3, WORKER_SUBMIT=4,
    WORKER_WAIT=5, WORKER_NEXT_CONTROL=6,
    WORKER_IDLE=64, WORKER_TASK, WORKER_STORED, WORKER_PENDING, WORKER_UNKNOWN,
    WORKER_CONFLICT, WORKER_INVALID, WORKER_UNAVAILABLE, WORKER_LIMIT
} CettaWorkerCode;
typedef struct CettaWorkerEndpoint CettaWorkerEndpoint;

/* Native host mechanism, not a worker/evaluator capability. Immutable tasks
 * hold bounded UTF-8 observation data (typically JSON). Worker and ID are
 * 1..64 ASCII letters/digits/._-. Reusing an ID requires byte-identical data.
 * Unanswered tasks remain durable; there is no per-worker task-count ceiling.
 * Storage admission follows the configured byte/page budget.
 * IDs must never be reused, even after any explicit retention/compaction.
 * The caller owns program/version/read-scope pairing inside that observation.
 * Publication does not consume input or accept an external effect. */
CettaDurableStatus cetta_worker_publish(CettaDurableStore *store,
    const char *worker, const char *id, const void *observation, size_t size);

/* Owns listener on success only. It must be a listening AF_UNIX/SOCK_SEQPACKET
 * socket, e.g. inherited from the OS supervisor. Exactly one endpoint owner
 * may service it. Connections are bound to this host-configured worker; peer
 * credentials must match uid. Store outlives endpoint. Single-thread calls,
 * no evaluator/Atom operations and no credentials in this protocol.
 * No bind/unlink, networking beyond this local descriptor, or supervision. */
CettaDurableStatus cetta_worker_endpoint_new(CettaDurableStore *store,
    int listener, uid_t uid, const char *worker, CettaWorkerEndpoint **out);
/* Select a finite peer-table bound for a responsive shared service. Zero
 * preserves the generic grow-as-needed profile. Excess connections are
 * accepted and closed without disturbing existing peers. */
CettaDurableStatus cetta_worker_endpoint_new_limited(CettaDurableStore *store,
    int listener, uid_t uid, const char *worker, size_t max_peers,
    CettaWorkerEndpoint **out);
void cetta_worker_endpoint_free(CettaWorkerEndpoint *endpoint);
/* Slots visited by one endpoint step; finite when constructed with a limit. */
size_t cetta_worker_endpoint_slots(const CettaWorkerEndpoint *endpoint);
/* Round-robin pump over expandable peer storage, one request per peer per call.
 * timeout 0..1000 ms. A slow/non-reading peer does not block other peers.
 * Idle or stalled peers close after 30 s of monotonic time. Return storage
 * faults to host health policy; never report STORED after an unknown commit.
 * Disconnection is not cancellation of any durable task/result/effect. */
CettaDurableStatus cetta_worker_endpoint_step(CettaWorkerEndpoint *endpoint,
    unsigned timeout_ms, size_t *requests);
/* Event-loop wait only: observe listener/peer readiness plus an optional host
 * intake fd. No I/O, storage, close or evaluator work; step handles the wake.
 * Readiness remains pending until consumed, including arrivals before poll.
 * No concurrent endpoint calls. timeout is 0..1000 ms; -1 omits intake. */
CettaDurableStatus cetta_worker_endpoint_wait(CettaWorkerEndpoint *endpoint,
    int intake_fd, unsigned timeout_ms);
/* As above, also wait for a host-owned output fd to become writable.
 * -1 omits either descriptor. The caller owns nonblocking output buffering. */
CettaDurableStatus cetta_worker_endpoint_wait_io(CettaWorkerEndpoint *endpoint,
    int intake_fd, int output_fd, unsigned timeout_ms);

/* Packet: "CWP1", code byte, ID-length byte, ID bytes, remaining body bytes.
 * One packet per seqpacket message. NEXT has no ID/body; RESULT requires both;
 * RECEIPT has ID only. Replies use the same framing. TASK carries ID+observation;
 * other replies have no body. UTF-8 is checked before storage, no NUL allowed.
 * STORED means the result is durably recorded as input, NOT that its proposed
 * action was accepted. The service's trusted gated program makes that choice.
 * Retry RESULT with the same ID+bytes or query RECEIPT after a lost reply.
 * Fetching does not consume a task: an unanswered task is returned after restart.
 * The result ledger survives inbox consumption and binds the task revision.
 *
 * NEXT prioritizes host-classified controls, then ordinary work, FIFO within
 * each class. NEXT_CONTROL reads only the control class. Both use indexed
 * reads of at most one ready row, not a snapshot of the entire backlog.
 * NEXT with an ID returns the next pending task after that pending task, so
 * a client can read a batch before answering any of it. UNKNOWN means the ID
 * is not pending (answered meanwhile); start again from NEXT without an ID.
 * SUBMIT records an unsolicited observation under a client-chosen key: an
 * immutable host.worker-submissions record and its host.inbox occurrence
 * commit together. Resending identical bytes returns STORED again, even after
 * the service consumed it; different bytes under one key are a CONFLICT.
 * Submissions remain durable without a task-count ceiling. Like RESULT, STORED
 * means recorded input, not accepted actions. Keys must never be reused.
 */
#endif
