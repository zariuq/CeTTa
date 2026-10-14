#ifndef CETTA_NATIVE_HANDLE_H
#define CETTA_NATIVE_HANDLE_H

#include "atom.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*CettaNativeHandleFreeFn)(void *resource);

typedef struct {
    uint64_t id;
    const char *kind;
    void *resource;
    CettaNativeHandleFreeFn free_resource;
    CettaStorageVisitor storage;
} CettaNativeHandleSlot;

struct CettaLibraryContext;

Atom *cetta_native_handle_atom(Arena *a, const char *kind, uint64_t id);
/* Opt in a registry resource to arena-backed holders. Copies to other arenas
 * retain it; the last holder's reset releases it. Explicit close invalidates
 * all aliases early, and context teardown invalidates surviving holders.
 * The visible (NativeHandle kind id) syntax is unchanged, but an identifier
 * manufactured by arithmetic or print/reparse is not an owning reference.
 * Non-enrolled clients keep their explicit-close/context-teardown contract.
 * Borrowed resource use must not race explicit close or context teardown.
 * Acquired uses below allow close to revoke the slot while retaining payload. */
Atom *cetta_native_handle_owned_atom(struct CettaLibraryContext *ctx, Arena *a,
                                     const char *kind, uint64_t id);
bool cetta_native_handle_arg(Atom *arg, const char *expected_kind, uint64_t *id_out);

bool cetta_native_handle_alloc(struct CettaLibraryContext *ctx, const char *kind,
                               void *resource, CettaNativeHandleFreeFn free_resource,
                               uint64_t *id_out);
/* The resource remains quiescent while its ownership is inspected. */
bool cetta_native_handle_set_storage(struct CettaLibraryContext *ctx, const char *kind,
    uint64_t id, CettaStorageVisitor storage);
void *cetta_native_handle_get(struct CettaLibraryContext *ctx, const char *kind,
                              uint64_t id);
/* Resolve only a holder belonging to this exact live registry owner. Equal
 * numeric IDs in another context, textual reconstructions and detached
 * holders refuse. The returned resource is borrowed under the same exclusive
 * use/close/context-lifetime contract as get. */
void *cetta_native_handle_get_owned(struct CettaLibraryContext *ctx,
    const char *kind, Atom *handle);
bool cetta_native_handle_close(struct CettaLibraryContext *ctx, const char *kind,
                               uint64_t id);
void cetta_native_handle_cleanup_all(struct CettaLibraryContext *ctx);

/* An acquired use keeps the resource alive after logical close. A lifetime
 * use grants no permission to read or mutate concurrently. Exclusive uses
 * exclude other cooperating exclusive users; borrowed get callers still need
 * their existing external exclusion. Neither mode pins library dependencies.
 * Keep the context and transitive services alive through use and finalization.
 *
 * Zero-initialize a record before first acquisition. Its address is part of
 * the issued ownership receipt: do not copy, reset or relocate an active record
 * except through use_move. The caller synchronizes access to that record and
 * keeps the input Atom's receiving arena alive through acquire. */
typedef enum {
    CETTA_NATIVE_HANDLE_USE_LIFETIME,
    CETTA_NATIVE_HANDLE_USE_EXCLUSIVE
} CettaNativeHandleUseMode;

struct CettaNativeHandleOwner;
typedef struct CettaNativeHandleUse {
    struct CettaNativeHandleUse *self;
    struct CettaNativeHandleOwner *owner;
    struct CettaNativeHandleUse *previous, *next;
    void *resource;
    CettaNativeHandleUseMode mode;
} CettaNativeHandleUse;

bool cetta_native_handle_acquire_owned(struct CettaLibraryContext *ctx,
    const char *kind, Atom *handle, CettaNativeHandleUseMode mode,
    CettaNativeHandleUse *use);
/* Valid only while this exact use remains held. Currency is a snapshot, not
 * fresh execution authority or protection against a subsequent close. */
void *cetta_native_handle_use_resource(const CettaNativeHandleUse *use);
bool cetta_native_handle_use_is_current(const CettaNativeHandleUse *use);
bool cetta_native_handle_use_move(CettaNativeHandleUse *destination,
    CettaNativeHandleUse *source);
bool cetta_native_handle_use_release(CettaNativeHandleUse *use);
/* Visits the owning edge only. The containing allocation accounts for the
 * use record's bytes; the resource and ownership graph remain quiescent. */
void cetta_native_handle_use_storage(const void *use, CettaStorageCensus *census);

/* Relinquish exclusive operation admission while preserving this exact
 * lifetime receipt. Only an exclusive record can be demoted. Neither this
 * operation nor a lifetime fork changes the owner's slot currency. */
bool cetta_native_handle_use_demote(CettaNativeHandleUse *use);
/* Issue a lifetime-only receipt from an actually held source, including a
 * source whose slot has closed. The destination is distinct and empty; both
 * records remain stable and externally synchronized. This is not a new
 * execution admission, context pin or resumable continuation transfer. */
bool cetta_native_handle_use_fork_lifetime(CettaNativeHandleUse *destination,
    const CettaNativeHandleUse *source);

#ifdef CETTA_NATIVE_HANDLE_TEST_HOOKS
/* Invokes the production reserve arithmetic without forging live owners. */
bool cetta_native_handle_test_reserve_use_count(size_t *count);
#endif

#endif /* CETTA_NATIVE_HANDLE_H */
