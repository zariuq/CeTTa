#ifndef CETTA_NATIVE_HANDLE_H
#define CETTA_NATIVE_HANDLE_H

#include "atom.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*CettaNativeHandleFreeFn)(void *resource);

typedef struct CettaNativeHandleRegistry CettaNativeHandleRegistry;

typedef struct {
    size_t live;
    size_t slot_capacity;
    size_t index_capacity;
    /* Registry metadata only, excluding resource payloads and stale holders. */
    size_t storage_bytes;
} CettaNativeHandleStats;

struct CettaLibraryContext;

Atom *cetta_native_handle_atom(Arena *a, const char *kind, uint64_t id);
/* Opt in a registry resource to arena-backed holders. Copies to other arenas
 * retain it; the last holder's reset releases it. Explicit close invalidates
 * all aliases early, and context teardown invalidates surviving holders.
 * The visible (NativeHandle kind id) syntax is unchanged, but an identifier
 * manufactured by arithmetic or print/reparse is not an owning reference.
 * Non-enrolled clients keep their explicit-close/context-teardown contract.
 * Resource use must not race explicit close or context teardown. */
Atom *cetta_native_handle_owned_atom(struct CettaLibraryContext *ctx, Arena *a,
                                     const char *kind, uint64_t id);
bool cetta_native_handle_arg(Atom *arg, const char *expected_kind, uint64_t *id_out);

bool cetta_native_handle_alloc(struct CettaLibraryContext *ctx, const char *kind,
                               void *resource, CettaNativeHandleFreeFn free_resource,
                               uint64_t *id_out);
/* Immutable resources can declare a conservative transitive payload footprint
 * for arena pacing. Registry metadata remains separately measured. */
bool cetta_native_handle_alloc_sized(struct CettaLibraryContext *ctx, const char *kind,
    void *resource, CettaNativeHandleFreeFn free_resource, size_t payload_bytes,
    uint64_t *id_out);
void *cetta_native_handle_get(struct CettaLibraryContext *ctx, const char *kind,
                              uint64_t id);
/* Resolve an owning value, requiring its retained identifier to belong to the
 * live slot. Printed/reparsed or arithmetically reconstructed ids cannot grant
 * access. The resource-use concurrency contract above still applies. */
void *cetta_native_handle_get_owned(struct CettaLibraryContext *ctx, Atom *value,
                                    const char *kind);
bool cetta_native_handle_close(struct CettaLibraryContext *ctx, const char *kind,
                               uint64_t id);
/* Counts committed live resources, independently of retained stale holders. */
size_t cetta_native_handle_count(const struct CettaLibraryContext *ctx);
CettaNativeHandleStats cetta_native_handle_stats(const struct CettaLibraryContext *ctx);
void cetta_native_handle_cleanup_all(struct CettaLibraryContext *ctx);

#ifdef CETTA_NATIVE_HANDLE_TEST_HOOKS
/* Fail an owner allocation after n successful owner allocations; -1 disables. */
void cetta_native_handle_test_fail_owner_after(long n);
#endif

#endif /* CETTA_NATIVE_HANDLE_H */
