#ifndef CETTA_NATIVE_DISK_STORE_H
#define CETTA_NATIVE_DISK_STORE_H

#include "native_disk_index.h"

/* Generic publication owner over native_disk_index and native_journal.
 * Application records, indexes, receipts and policy remain MeTTa data/logic.
 * An existing private directory is one commit domain. No SQL is used.
 *
 * Locks are nonblocking and held only during snapshot opening, validation and
 * publication. No callback or external effect runs while a lock is held.
 * Use a separate handle per concurrent caller; views outlive their handle.
 */
typedef struct CettaDiskStore CettaDiskStore;
typedef struct CettaDiskStoreView CettaDiskStoreView;
typedef enum {
    CETTA_STORE_OK, CETTA_STORE_ABSENT, CETTA_STORE_INVALID, CETTA_STORE_BUSY,
    CETTA_STORE_CONFLICT, CETTA_STORE_IO, CETTA_STORE_CORRUPT, CETTA_STORE_NOMEM,
    CETTA_STORE_LIMIT, CETTA_STORE_UNKNOWN, CETTA_STORE_VERSION,
    CETTA_STORE_RECOVERY_REQUIRED, CETTA_STORE_OWNERSHIP
} CettaDiskStoreStatus;
typedef struct { size_t key_bytes, value_bytes; } CettaDiskStoreLimits;
typedef enum { CETTA_STORE_POINT, CETTA_STORE_RANGE } CettaDiskStoreScopeKind;
typedef struct {
    CettaDiskStoreScopeKind kind;
    CettaDiskBytes lower, upper;
    bool has_lower, has_upper; /* POINT uses lower as its exact key. */
} CettaDiskStoreScope;

/* The directory and its parent entry must already exist durably. This owner
 * initializes the store inside it; it never creates parent directories.
 * Limits describe individual format items, not corpus or cache capacity.
 * On an existing store, the persisted limits apply. Creation is explicit;
 * an empty or damaged published manifest is corruption, never a new store.
 */
CettaDiskStoreLimits cetta_disk_store_limits(void);
CettaDiskStoreStatus cetta_disk_store_open(const char *directory, bool create,
    const CettaDiskStoreLimits *limits, CettaDiskStore **out);
void cetta_disk_store_close(CettaDiskStore *store);
/* The caller holds its exclusive owner use. A lost post-publication reply
 * resource must use the same write gate as an uncertain filesystem reply. */
void cetta_disk_store_require_recovery(CettaDiskStore *store);
CettaDiskStoreStatus cetta_disk_store_snapshot(CettaDiskStore *store, CettaDiskStoreView **out);
/* Check a live, non-poisoned publisher against a captured view's physical
 * domain and epoch, without publishing or requiring the latest revision.
 * This does not replace atomic read/write validation at a later commit. */
CettaDiskStoreStatus cetta_disk_store_check_view(CettaDiskStore *store,
    const CettaDiskStoreView *view);
/* Successful snapshots confirm first-seen roots durably, including a root
 * published by a writer that died before its final synchronization. */
void cetta_disk_store_view_close(CettaDiskStoreView *view);
const CettaDiskRoot *cetta_disk_store_view_root(const CettaDiskStoreView *view);
uint64_t cetta_disk_store_view_revision(const CettaDiskStoreView *view);
const unsigned char *cetta_disk_store_view_epoch(const CettaDiskStoreView *view);

/* Initial conservative validation: epoch, domain and revision must match.
 * A snapshot is a read view, not a one-use protocol capability. Higher layers
 * own observation handles and durable application receipts. Accepted no-ops
 * also advance revision. All operations become visible through one root.
 * UNKNOWN poisons this handle for writing until recover verifies/synchronizes
 * the published root. It never authorizes replaying an external effect.
 */
CettaDiskStoreStatus cetta_disk_store_commit(CettaDiskStore *store,
    const CettaDiskStoreView *expected, const CettaDiskOp *operations, size_t count,
    CettaDiskStoreView **committed);
/* Validate every declared positive/negative read and write scope against the
 * captured root, and cover every operation by a write scope. Point and
 * [lower,upper) comparisons stream exact keys/values; no lossy projection or
 * whole-store revision is used. Callers must declare all influencing reads.
 */
CettaDiskStoreStatus cetta_disk_store_commit_scoped(CettaDiskStore *store,
    const CettaDiskStoreView *expected,
    const CettaDiskStoreScope *reads, size_t read_count,
    const CettaDiskStoreScope *writes, size_t write_count,
    const CettaDiskOp *operations, size_t count, CettaDiskStoreView **committed);
CettaDiskStoreStatus cetta_disk_store_recover(CettaDiskStore *store, CettaDiskStoreView **out);

/* Copy outside the writer lock. A changed root rejects only this maintenance
 * attempt. Readers keep old fds. Garbage collection preserves the current
 * generation and live builders; an old reader keeps an unlinked inode alive.
 */
CettaDiskStoreStatus cetta_disk_store_compact(CettaDiskStore *store, CettaDiskStoreView **out);
CettaDiskStoreStatus cetta_disk_store_collect(CettaDiskStore *store, uint64_t *removed);

#endif
