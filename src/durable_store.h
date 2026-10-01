#ifndef CETTA_DURABLE_STORE_H
#define CETTA_DURABLE_STORE_H

#include <stddef.h>
#include <stdint.h>

/* One local owner, one commit domain, many named spaces. Keys identify
 * occurrences, so equal payloads can coexist. Values are opaque, versioned
 * data, never runtime pointers. All returned memory belongs to the caller. */
typedef struct CettaDurableStore CettaDurableStore;

typedef enum {
    DURABLE_OK, DURABLE_INVALID, DURABLE_BUSY, DURABLE_CONFLICT,
    DURABLE_PRECONDITION, DURABLE_LIMIT, DURABLE_NOMEM,
    DURABLE_IO, DURABLE_CORRUPT, DURABLE_VERSION,
    DURABLE_UNKNOWN, DURABLE_POISONED
} CettaDurableStatus;

typedef struct {
    size_t record_bytes, batch_bytes, live_bytes, history_bytes;
    size_t records, operations;
    uint32_t database_pages; /* 4096-byte pages, plus bounded WAL overhead */
} CettaDurableLimits;

typedef enum { DURABLE_INSERT = 1, DURABLE_REPLACE = 2, DURABLE_REMOVE = 3 }
    CettaDurableOpKind;

typedef struct {
    CettaDurableOpKind kind;
    const char *space, *key;
    const void *data;
    size_t size;
} CettaDurableOp;

typedef struct {
    char *space, *key;
    unsigned char *data;
    size_t size;
    int64_t revision, position;
} CettaDurableRecord;

typedef struct {
    char epoch[33];
    int64_t revision;
    CettaDurableRecord *records;
    size_t count;
} CettaDurableSnapshot;

typedef struct {
    int64_t live_bytes, records, history_bytes, database_pages;
    int limits_exceeded; /* readable and reclaimable; admission is restricted */
} CettaDurableUsage;

typedef enum { DURABLE_KEY, DURABLE_PREFIX, DURABLE_SPACE } CettaDurableScopeKind;
typedef struct {
    CettaDurableScopeKind kind;
    const char *space, *key; /* prefix may be empty; space scope ignores key */
} CettaDurableScope;
typedef struct CettaDurableObservation CettaDurableObservation;
typedef struct CettaDurableWatch CettaDurableWatch;

CettaDurableLimits cetta_durable_default_limits(void);
const char *cetta_durable_status_name(CettaDurableStatus status);

/* Opens a local regular file and holds an exclusive OS lock until close.
 * Schema versions fail closed. The caller supplies a private directory;
 * SQLite sidecars must remain on the same local filesystem. */
CettaDurableStatus cetta_durable_open(const char *path,
    const CettaDurableLimits *limits, CettaDurableStore **out);
void cetta_durable_close(CettaDurableStore *store);

/* The file lock excludes other processes; this guard also excludes a second
 * operational runtime sharing this handle. Attach before recovery/startup;
 * detach only after all owner threads have stopped, before closing the store.
 * A native identity, never a serialized/evaluator capability. */
CettaDurableStatus cetta_durable_attach_runtime(CettaDurableStore *store, const void *owner);
CettaDurableStatus cetta_durable_detach_runtime(CettaDurableStore *store, const void *owner);

/* Copies a consistent snapshot, releasing the connection before returning.
 * NULL space reads all named spaces. Even filtered reads carry the global
 * revision: committing with it validates absence and range reads too. */
CettaDurableStatus cetta_durable_snapshot(CettaDurableStore *store,
    const char *space, CettaDurableSnapshot *out);
void cetta_durable_snapshot_free(CettaDurableSnapshot *snapshot);
CettaDurableStatus cetta_durable_usage(CettaDurableStore *store, CettaDurableUsage *out);

/* Read all declared scopes in one snapshot, without retaining a transaction.
 * Observations own immutable views. Empty key/prefix/space views are absence
 * dependencies. No hashing, tombstones or global-revision conflicts are used.
 * At commit, the current keys and their revisions must match each view exactly.
 * A transient insert followed by removal preserves an absence dependency.
 * Every written key must be covered by a declared scope. The host must also
 * declare every read that influenced the decision, including negative reads. */
CettaDurableStatus cetta_durable_observe(CettaDurableStore *store,
    const CettaDurableScope *scopes, size_t count, CettaDurableObservation **out);
const CettaDurableSnapshot *cetta_durable_observation_view(
    const CettaDurableObservation *observation, size_t index);
void cetta_durable_observation_free(CettaDurableObservation *observation);
/* Metadata-only dependency watch: owns scope/key/revision copies, no values.
 * Not a commit capability. OK means unchanged, CONFLICT means re-observe.
 * Checking is a short read transaction; unrelated writes do not wake it.
 * max_bytes bounds retained allocation, including repeated/overlapping scopes. */
CettaDurableStatus cetta_durable_watch_new(const CettaDurableObservation *observation,
    size_t max_bytes, CettaDurableWatch **out);
CettaDurableStatus cetta_durable_watch_current(CettaDurableStore *store,
    const CettaDurableWatch *watch);
size_t cetta_durable_watch_bytes(const CettaDurableWatch *watch);
void cetta_durable_watch_free(CettaDurableWatch *watch);
CettaDurableStatus cetta_durable_commit_observed(CettaDurableStore *store,
    const CettaDurableObservation *observation, const CettaDurableOp *ops,
    size_t count, int64_t *published_revision);

/* Commits all operations, their log, and the new revision atomically.
 * expected_epoch/revision come from the evaluated snapshot. No evaluation or
 * network operations occur here. Each (space,key) may occur once per batch.
 * INSERT requires absence; REPLACE and REMOVE require presence. This API has
 * no blind upsert. A failed commit never returns a published revision.
 * After an interrupted/unknown acknowledgment, inspect durable state; never
 * infer that a missing acknowledgment means a transition did not commit. */
CettaDurableStatus cetta_durable_commit(CettaDurableStore *store,
    const char *expected_epoch, int64_t expected_revision,
    const CettaDurableOp *ops, size_t count, int64_t *published_revision);

/* Atomically folds the log into a checkpoint without changing the revision.
 * Live records (including unresolved work) are never pruned. Checkpointing
 * does not grant semantic permission to remove records. */
CettaDurableStatus cetta_durable_checkpoint(CettaDurableStore *store);

/* Reconstructs a snapshot from checkpoint + committed deltas. Useful for
 * rebuilding query indexes; does not execute an application continuation. */
CettaDurableStatus cetta_durable_recover(CettaDurableStore *store,
    CettaDurableSnapshot *out);

#endif
