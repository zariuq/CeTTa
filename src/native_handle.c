#include "native_handle.h"

#include "library.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Protect registry mutation, holder references and acquired-use receipts.
 * Borrowed resource pointers retain their external use/close exclusion;
 * payload mutation requires a separate admitted operation. */
static pthread_mutex_t native_handle_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct CettaNativeHandleOwner {
    CettaNativeHandleSlot *slot;
    size_t references;
    size_t use_count;
    CettaNativeHandleUse *uses, *exclusive_use;
    void *resource;
    CettaNativeHandleFreeFn free_resource;
    CettaStorageVisitor storage;
} CettaNativeHandleOwner;

static void native_handle_release(void *ptr);

/* Enrolled slots hold a control cell. Keeping the registry layout unchanged
 * also preserves the context-owned contract of non-enrolled native modules.
 * All registry destruction goes through native_handle_detach below. */
static CettaNativeHandleOwner *native_handle_owner(CettaNativeHandleSlot *slot) {
    return slot && slot->free_resource == native_handle_release
        ? slot->resource : NULL;
}

typedef struct {
    void *resource;
    CettaNativeHandleFreeFn free_resource;
    CettaNativeHandleOwner *dead_owner;
} NativeHandleRetirement;

/* The mutex protects extraction. The caller invokes the extracted callback
 * only after unlocking; surviving holders retain an empty closed owner. */
static NativeHandleRetirement native_handle_extract(CettaNativeHandleOwner *owner) {
    assert(!owner->slot);
    NativeHandleRetirement retired = {0};
    if (owner->use_count)
        return retired;
    assert(!owner->uses && !owner->exclusive_use);
    retired.resource = owner->resource;
    retired.free_resource = owner->free_resource;
    owner->resource = NULL;
    owner->free_resource = NULL;
    owner->storage = NULL;
    if (owner->references == 0u)
        retired.dead_owner = owner;
    return retired;
}

static CettaNativeHandleSlot *native_handle_slot(
    CettaLibraryContext *ctx, const char *kind, uint64_t id) {
    for (uint32_t i = 0; ctx && kind && i < ctx->native_handle_len; ++i) {
        CettaNativeHandleSlot *slot = &ctx->native_handles[i];
        if (slot->id == id && slot->resource && slot->kind &&
            strcmp(slot->kind, kind) == 0)
            return slot;
    }
    return NULL;
}

/* Detach before invoking a destructor: destructors may close other handles.
 * Surviving holders must never retain a pointer into a freed context. */
static NativeHandleRetirement native_handle_detach(CettaNativeHandleSlot *slot) {
    NativeHandleRetirement retired = {
        .resource = slot->resource,
        .free_resource = slot->free_resource,
    };
    CettaNativeHandleOwner *owner = native_handle_owner(slot);
    if (owner) {
        owner->slot = NULL;
        retired = native_handle_extract(owner);
    }
    slot->resource = NULL;
    slot->kind = NULL;
    slot->free_resource = NULL;
    slot->storage = NULL;
    return retired;
}

static void native_handle_retire(NativeHandleRetirement retired) {
    if (retired.resource && retired.free_resource)
        retired.free_resource(retired.resource);
    free(retired.dead_owner);
}

static void native_handle_retain(void *ptr) {
    CettaNativeHandleOwner *owner = ptr;
    pthread_mutex_lock(&native_handle_mutex);
    if (owner->references == SIZE_MAX)
        abort();
    ++owner->references;
    pthread_mutex_unlock(&native_handle_mutex);
}

static void native_handle_release(void *ptr) {
    CettaNativeHandleOwner *owner = ptr;
    NativeHandleRetirement retired = {0};
    pthread_mutex_lock(&native_handle_mutex);
    assert(owner->references > 0);
    if (--owner->references == 0) {
        if (owner->slot)
            retired = native_handle_detach(owner->slot);
        else
            retired = native_handle_extract(owner);
    }
    pthread_mutex_unlock(&native_handle_mutex);
    native_handle_retire(retired);
}

static bool native_handle_kind_matches(Atom *arg, const char *expected_kind) {
    if (!arg || !expected_kind) return false;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING) {
        return atom_string_equals_cstr(arg, expected_kind);
    }
    if (arg->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(arg);
        return name && strcmp(name, expected_kind) == 0;
    }
    return false;
}

Atom *cetta_native_handle_atom(Arena *a, const char *kind, uint64_t id) {
    return atom_expr(a, (Atom *[]){
        atom_symbol(a, "NativeHandle"),
        atom_string(a, kind),
        atom_int(a, (int64_t)id)
    }, 3);
}

static void native_handle_storage(const void *raw, CettaStorageCensus *census) {
    const CettaNativeHandleOwner *owner = raw;
    if (!cetta_storage_object(census, owner, sizeof(*owner), CETTA_STORAGE_BLOCK)) return;
    if (!owner->resource) return;
    if (owner->storage) owner->storage(owner->resource, census);
    else cetta_storage_refuse(census, "OpaqueNativeHandleResource");
}

Atom *cetta_native_handle_owned_atom(CettaLibraryContext *ctx, Arena *a,
                                     const char *kind, uint64_t id) {
    if (!ctx || !a || !kind)
        return NULL;
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx, kind, id);
    if (!slot) {
        pthread_mutex_unlock(&native_handle_mutex);
        return NULL;
    }
    CettaNativeHandleOwner *owner = native_handle_owner(slot);
    if (!owner) {
        owner = cetta_malloc(sizeof(*owner));
        *owner = (CettaNativeHandleOwner){
            .slot = slot,
            .resource = slot->resource,
            .free_resource = slot->free_resource,
            .storage = slot->storage,
        };
        slot->resource = owner;
        slot->free_resource = native_handle_release;
    }
    /* Reserve across allocation, which registers its own arena reference. */
    if (owner->references == SIZE_MAX)
        abort();
    ++owner->references;
    pthread_mutex_unlock(&native_handle_mutex);
    Atom *identifier = atom_native_handle_identifier_with_storage(
        a, (int64_t)id, owner, native_handle_retain, native_handle_release, native_handle_storage);
    native_handle_release(owner);
    return atom_expr(a, (Atom *[]){atom_symbol(a, "NativeHandle"),
                                  atom_string(a, kind), identifier}, 3);
}

bool cetta_native_handle_arg(Atom *arg, const char *expected_kind, uint64_t *id_out) {
    if (!arg || !id_out || arg->kind != ATOM_EXPR || arg->expr.len != 3) return false;
    if (!atom_is_symbol_id(arg->expr.elems[0], g_builtin_syms.native_handle)) return false;
    if (!native_handle_kind_matches(arg->expr.elems[1], expected_kind)) return false;
    if (arg->expr.elems[2]->kind != ATOM_GROUNDED ||
        arg->expr.elems[2]->ground.gkind != GV_INT) {
        return false;
    }
    *id_out = (uint64_t)arg->expr.elems[2]->ground.ival;
    return true;
}

bool cetta_native_handle_alloc(struct CettaLibraryContext *ctx, const char *kind,
                               void *resource, CettaNativeHandleFreeFn free_resource,
                               uint64_t *id_out) {
    int slot = -1;
    if (!ctx || !kind || !resource || !id_out) return false;
    pthread_mutex_lock(&native_handle_mutex);
    if (ctx->native_handle_next_id == 0) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    for (uint32_t i = 0; i < ctx->native_handle_len; i++) {
        if (ctx->native_handles[i].resource == NULL) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        if (ctx->native_handle_len >= CETTA_MAX_NATIVE_HANDLES) {
            pthread_mutex_unlock(&native_handle_mutex);
            return false;
        }
        slot = (int)ctx->native_handle_len++;
    }
    ctx->native_handles[slot].id = ctx->native_handle_next_id++;
    ctx->native_handles[slot].kind = kind;
    ctx->native_handles[slot].resource = resource;
    ctx->native_handles[slot].free_resource = free_resource;
    ctx->native_handles[slot].storage = NULL;
    *id_out = ctx->native_handles[slot].id;
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
}

bool cetta_native_handle_set_storage(CettaLibraryContext *ctx, const char *kind,
        uint64_t id, CettaStorageVisitor storage) {
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx, kind, id);
    if (slot) {
        slot->storage = storage;
        CettaNativeHandleOwner *owner = native_handle_owner(slot);
        if (owner) owner->storage = storage;
    }
    pthread_mutex_unlock(&native_handle_mutex);
    return slot != NULL;
}

void *cetta_native_handle_get(struct CettaLibraryContext *ctx, const char *kind,
                              uint64_t id) {
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx, kind, id);
    CettaNativeHandleOwner *owner = native_handle_owner(slot);
    void *resource = owner ? owner->resource : slot ? slot->resource : NULL;
    pthread_mutex_unlock(&native_handle_mutex);
    return resource;
}

bool cetta_native_handle_close(struct CettaLibraryContext *ctx, const char *kind,
                               uint64_t id) {
    NativeHandleRetirement retired = {0};
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx, kind, id);
    bool found = slot != NULL;
    if (slot)
        retired = native_handle_detach(slot);
    pthread_mutex_unlock(&native_handle_mutex);
    native_handle_retire(retired);
    return found;
}

void *cetta_native_handle_get_owned(CettaLibraryContext *ctx, const char *kind,
        Atom *handle) {
    uint64_t id = 0u;
    if (!cetta_native_handle_arg(handle, kind, &id)) return NULL;
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx, kind, id);
    CettaNativeHandleOwner *owner = native_handle_owner(slot);
    void *resource = owner && atom_native_handle_identifier_has_owner(
        handle->expr.elems[2], owner) ? owner->resource : NULL;
    pthread_mutex_unlock(&native_handle_mutex);
    return resource;
}

void cetta_native_handle_cleanup_all(struct CettaLibraryContext *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->native_handle_len; i++) {
        pthread_mutex_lock(&native_handle_mutex);
        NativeHandleRetirement retired =
            native_handle_detach(&ctx->native_handles[i]);
        ctx->native_handles[i].id = 0;
        pthread_mutex_unlock(&native_handle_mutex);
        native_handle_retire(retired);
    }
    pthread_mutex_lock(&native_handle_mutex);
    ctx->native_handle_len = 0;
    pthread_mutex_unlock(&native_handle_mutex);
}

static bool native_handle_use_empty(const CettaNativeHandleUse *use) {
    return use && !use->self && !use->owner && !use->previous && !use->next &&
        !use->resource && use->mode == CETTA_NATIVE_HANDLE_USE_LIFETIME;
}

/* The self check precedes every owner dereference: a by-value copy cannot
 * authenticate even after the real record released and retired its owner.
 * Active records and their neighbors stay at stable caller-owned addresses. */
static bool native_handle_use_owned(const CettaNativeHandleUse *use) {
    if (!use || use->self != use || !use->owner || !use->resource)
        return false;
    CettaNativeHandleOwner *owner = use->owner;
    if (!owner->use_count || owner->resource != use->resource ||
        (use->previous ? use->previous->owner != owner || use->previous->next != use
                       : owner->uses != use) ||
        (use->next && (use->next->owner != owner || use->next->previous != use)))
        return false;
    if (use->mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE)
        return owner->exclusive_use == use;
    return use->mode == CETTA_NATIVE_HANDLE_USE_LIFETIME && owner->exclusive_use != use;
}

static bool native_handle_reserve_use_count(size_t *count) {
    if (*count == SIZE_MAX)
        return false;
    ++*count;
    return true;
}

#ifdef CETTA_NATIVE_HANDLE_TEST_HOOKS
bool cetta_native_handle_test_reserve_use_count(size_t *count) {
    return count && native_handle_reserve_use_count(count);
}
#endif

bool cetta_native_handle_acquire_owned(CettaLibraryContext *ctx,
        const char *kind, Atom *handle, CettaNativeHandleUseMode mode,
        CettaNativeHandleUse *use) {
    uint64_t id = 0u;
    if (!native_handle_use_empty(use) ||
        (mode != CETTA_NATIVE_HANDLE_USE_LIFETIME && mode != CETTA_NATIVE_HANDLE_USE_EXCLUSIVE) ||
        !cetta_native_handle_arg(handle, kind, &id))
        return false;
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx, kind, id);
    CettaNativeHandleOwner *owner = native_handle_owner(slot);
    if (!owner || !owner->resource ||
        !atom_native_handle_identifier_has_owner(handle->expr.elems[2], owner) ||
        (mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE && owner->exclusive_use) ||
        !native_handle_reserve_use_count(&owner->use_count)) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    *use = (CettaNativeHandleUse){.self=use, .owner=owner,
        .next=owner->uses, .resource=owner->resource, .mode=mode};
    if (use->next)
        use->next->previous = use;
    owner->uses = use;
    if (mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE)
        owner->exclusive_use = use;
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
}

void *cetta_native_handle_use_resource(const CettaNativeHandleUse *use) {
    pthread_mutex_lock(&native_handle_mutex);
    void *resource = native_handle_use_owned(use) ? use->resource : NULL;
    pthread_mutex_unlock(&native_handle_mutex);
    return resource;
}

bool cetta_native_handle_use_is_current(const CettaNativeHandleUse *use) {
    pthread_mutex_lock(&native_handle_mutex);
    bool current = native_handle_use_owned(use) && use->owner->slot;
    pthread_mutex_unlock(&native_handle_mutex);
    return current;
}

bool cetta_native_handle_use_exchange(CettaLibraryContext *ctx,
        const char *kind, Atom *handle, CettaNativeHandleUse *use,
        void *replacement, void **previous) {
    uint64_t id = 0u;
    if (!replacement || !previous || (use && previous == &use->resource) ||
        !cetta_native_handle_arg(handle,kind,&id)) return false;
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleSlot *slot = native_handle_slot(ctx,kind,id);
    if (!native_handle_use_owned(use) ||
        use->mode != CETTA_NATIVE_HANDLE_USE_EXCLUSIVE || !use->owner->slot ||
        native_handle_owner(slot) != use->owner ||
        !atom_native_handle_identifier_has_owner(handle->expr.elems[2],use->owner) ||
        use->owner->use_count != 1u || replacement == use->resource) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    void *old = use->resource;
    use->owner->resource = replacement;
    use->resource = replacement;
    *previous = old;
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
}

bool cetta_native_handle_use_move(CettaNativeHandleUse *destination,
        CettaNativeHandleUse *source) {
    pthread_mutex_lock(&native_handle_mutex);
    if (destination == source || !native_handle_use_empty(destination) ||
        !native_handle_use_owned(source)) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    *destination = *source;
    destination->self = destination;
    if (destination->previous)
        destination->previous->next = destination;
    else
        destination->owner->uses = destination;
    if (destination->next)
        destination->next->previous = destination;
    if (destination->mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE)
        destination->owner->exclusive_use = destination;
    *source = (CettaNativeHandleUse){0};
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
}

bool cetta_native_handle_use_release(CettaNativeHandleUse *use) {
    NativeHandleRetirement retired = {0};
    pthread_mutex_lock(&native_handle_mutex);
    if (!native_handle_use_owned(use)) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    CettaNativeHandleOwner *owner = use->owner;
    if (use->previous)
        use->previous->next = use->next;
    else
        owner->uses = use->next;
    if (use->next)
        use->next->previous = use->previous;
    if (use->mode == CETTA_NATIVE_HANDLE_USE_EXCLUSIVE)
        owner->exclusive_use = NULL;
    --owner->use_count;
    *use = (CettaNativeHandleUse){0};
    if (!owner->slot)
        retired = native_handle_extract(owner);
    pthread_mutex_unlock(&native_handle_mutex);
    native_handle_retire(retired);
    return true;
}

void cetta_native_handle_use_storage(const void *raw, CettaStorageCensus *census) {
    const CettaNativeHandleUse *use = raw;
    if (!use || native_handle_use_empty(use))
        return;
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleOwner *owner = native_handle_use_owned(use) ? use->owner : NULL;
    pthread_mutex_unlock(&native_handle_mutex);
    if (!owner) {
        cetta_storage_refuse(census, "InvalidNativeHandleUse");
        return;
    }
    native_handle_storage(owner, census);
}

bool cetta_native_handle_use_demote(CettaNativeHandleUse *use) {
    pthread_mutex_lock(&native_handle_mutex);
    if (!native_handle_use_owned(use) ||
        use->mode != CETTA_NATIVE_HANDLE_USE_EXCLUSIVE) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    use->owner->exclusive_use = NULL;
    use->mode = CETTA_NATIVE_HANDLE_USE_LIFETIME;
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
}

bool cetta_native_handle_use_fork_lifetime(CettaNativeHandleUse *destination,
        const CettaNativeHandleUse *source) {
    pthread_mutex_lock(&native_handle_mutex);
    if (destination == source || !native_handle_use_empty(destination) ||
        !native_handle_use_owned(source) ||
        !native_handle_reserve_use_count(&source->owner->use_count)) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    CettaNativeHandleOwner *owner = source->owner;
    *destination = (CettaNativeHandleUse){.self=destination, .owner=owner,
        .next=owner->uses, .resource=source->resource,
        .mode=CETTA_NATIVE_HANDLE_USE_LIFETIME};
    if (destination->next)
        destination->next->previous = destination;
    owner->uses = destination;
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
}
