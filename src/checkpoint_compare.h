#ifndef CETTA_CHECKPOINT_COMPARE_H
#define CETTA_CHECKPOINT_COMPARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "binding/frame_identity.h"

struct Atom;
struct Arena;
struct Space;

/* Synchronous field comparison for held checkpoint providers. The visitor
 * owns its comparison state and may refuse at any field. It must preserve
 * both providers and return normally. None of these callbacks authenticates
 * an identity map merely because a pair has the same printed payload. */
typedef enum {
    CETTA_CHECKPOINT_NEED_SESSION,
    CETTA_CHECKPOINT_NEED_SERIAL,
    CETTA_CHECKPOINT_NEED_THUNK,
    /* Caller-named high-half cells are not issued by the fresh-thunk counter. */
    CETTA_CHECKPOINT_NEED_NAMED_CELL,
    CETTA_CHECKPOINT_NEED_AUTHORITY,
    CETTA_CHECKPOINT_NEED_EVALUATOR,
    CETTA_CHECKPOINT_NEED_STORAGE,
    CETTA_CHECKPOINT_NEED_IMPORT,
    CETTA_CHECKPOINT_SOURCE_OCCURRENCE,
    CETTA_CHECKPOINT_RECEIPT_SESSION,
    CETTA_CHECKPOINT_RECEIPT_NODE,
    CETTA_CHECKPOINT_RULE_OCCURRENCE,
    CETTA_CHECKPOINT_VAR,
    CETTA_CHECKPOINT_FRAME,
    CETTA_CHECKPOINT_BINDING_OCCURRENCE,
    CETTA_CHECKPOINT_WEIGHT_OCCURRENCE,
    CETTA_CHECKPOINT_WEIGHT_HANDLER,
    CETTA_CHECKPOINT_COST_EVENT,
    CETTA_CHECKPOINT_COST_LEDGER,
    CETTA_CHECKPOINT_ISSUED_STORE,
    CETTA_CHECKPOINT_ISSUED_OCCURRENCE,
    CETTA_CHECKPOINT_ISSUED_LEASE,
    CETTA_CHECKPOINT_ARENA,
    CETTA_CHECKPOINT_NATIVE_HANDLE,
    CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE,
    CETTA_CHECKPOINT_TARGET_SPACE_PREFIX,
    CETTA_CHECKPOINT_IDENTITY_ROLE_COUNT
} CettaCheckpointIdentityRole;

typedef struct {
    uint64_t next[CETTA_CHECKPOINT_IDENTITY_ROLE_COUNT];
} CettaCheckpointIssuance;

typedef enum {
    CETTA_CHECKPOINT_REGISTRY_ATTACHMENT
} CettaCheckpointRetainedAtomRole;

/* Mutable headers can be observed at several event cuts. This role covers
 * their allocation identity only; logical IDs and retained payload/backing
 * edges always use the ordinary persistent comparison callbacks. */
typedef enum {
    CETTA_CHECKPOINT_BINDINGS_CONTAINER,
    CETTA_CHECKPOINT_BINDINGS_OCCURRENCE_CONTAINER
} CettaCheckpointContainerRole;

typedef struct {
    void *context;
    bool (*scalar)(void *, const char *, uint64_t, uint64_t);
    bool (*identity)(void *, const char *, CettaCheckpointIdentityRole,
                     uint64_t, uint64_t);
    bool (*address)(void *, const char *, const void *, const void *);
    /* Optional admitted snapshot relation. Without it, container addresses
     * have the same strict relation as every other checkpoint address. */
    bool (*container)(void *, const char *, CettaCheckpointContainerRole,
                      const void *, const void *);
    /* A provider target or an authenticated retained target-token role.
     * Registry/input Space references keep their ordinary rigid role. The
     * callback must not dereference historical token pointers. It supplies
     * address transport only; the provider separately visits typed IDs. */
    bool (*target)(void *, const char *, const struct Space *, uint64_t,
                    const struct Space *, uint64_t);
    /* An actual provider-owned arena edge. Collect these while owners are
     * held, before dereferencing any queued Atom payload. This callback is
     * distinct from an arbitrary opaque address correspondence. */
    bool (*arena)(void *, const char *, const struct Arena *, const struct Arena *);
    /* Optional owning edge for a previously compared FRAME pair. Both
     * actual scopes remain live and retain their initialized entries through
     * this comparison. Membership is separate from allocator liveness and
     * may differ in physical extent at a recorded-prefix cut. The visitor
     * must not keep these borrowed scope pointers after comparison. */
    bool (*frame_scope)(void *, const char *, const CettaFrameIdentityScope *, uint64_t,
                         const CettaFrameIdentityScope *, uint64_t);
    bool (*atom)(void *, const char *, const struct Atom *, const struct Atom *);
    /* Optional provenance for complete nonexecuting registry snapshots.
     * This does not authorize execution of any reachable capability. */
    bool (*retained_atom)(void *, const char *, CettaCheckpointRetainedAtomRole,
                          const struct Atom *, const struct Atom *);
    /* Physical capacities and reference counts are observed independently.
     * Copies need not use the same allocation shape or number of holders. */
    bool (*physical)(void *, const char *, uint64_t, uint64_t);
    bool (*bytes)(void *, const char *, const void *, size_t,
                  const void *, size_t);
    /* Before a bounded library traversal/allocation. The caller reserves
     * inspection fuel and temporary bytes before returning true and records
     * the attempted service expense even when its allocation later fails.
     * work_bound is an inspection bound, not evaluator Resume allowance. */
    bool (*service)(void *, const char *, size_t work_bound, size_t scratch_bound);
    /* Optional return of this provider's successfully reserved temporary
     * bytes, only after the corresponding buffers have actually been freed.
     * No inspection work is refunded. Cleanup must call it even after a
     * visitor refusal; it cannot allocate, fail or inspect freed payloads.
     * Without it, the visitor keeps its conservative cumulative reservation. */
    void (*service_release)(void *, size_t temporary_bytes);
} CettaCheckpointCompare;

/* Logical identities and address references are separate namespaces. A
 * concrete replay visitor must validate native issuers, zero/sentinel roles,
 * rigid captured prefixes and injectivity before reporting equality. */
static inline bool cetta_checkpoint_compare_valid(
    const CettaCheckpointCompare *read) {
    return read && read->scalar && read->identity && read->address && read->arena && read->atom &&
        read->physical && read->bytes && read->service;
}

static inline bool cetta_checkpoint_container(const CettaCheckpointCompare *read,
    const char *site, CettaCheckpointContainerRole role, const void *left, const void *right) {
    return read->container ? read->container(read->context,site,role,left,right)
                           : read->address(read->context,site,left,right);
}

static inline bool cetta_checkpoint_target(const CettaCheckpointCompare *read,
    const char *site, const struct Space *left, uint64_t left_instance,
    const struct Space *right, uint64_t right_instance) {
    return read->target ? read->target(read->context,site,left,left_instance,
                                      right,right_instance)
                        : read->address(read->context,site,left,right);
}

/* Providers reject unequal extents before walking paired initialized rows,
 * independently of the visitor's treatment of an individual scalar. */
static inline bool cetta_checkpoint_extent(const void *left, size_t length,
    size_t capacity, const void *right, size_t other_length,
    size_t other_capacity) {
    return length == other_length && length <= capacity &&
        other_length <= other_capacity && (!length || (left && right));
}

#endif
