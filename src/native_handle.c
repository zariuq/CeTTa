#include "native_handle.h"

#include "library.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Protect registry mutation and holder accounting, not operations performed
 * through a borrowed resource pointer. Explicit close still requires callers
 * to exclude concurrent use, as it does for context-owned handles. */
static pthread_mutex_t native_handle_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    CettaNativeHandleSlot *slot;
    size_t references;
    void *resource;
    CettaNativeHandleFreeFn free_resource;
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
        retired.resource = owner->resource;
        retired.free_resource = owner->free_resource;
        owner->resource = NULL;
        owner->slot = NULL;
        if (owner->references == 0)
            retired.dead_owner = owner;
    }
    slot->resource = NULL;
    slot->kind = NULL;
    slot->free_resource = NULL;
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
            retired.dead_owner = owner;
    }
    pthread_mutex_unlock(&native_handle_mutex);
    native_handle_retire(retired);
}

static bool native_handle_kind_matches(Atom *arg, const char *expected_kind) {
    if (!arg || !expected_kind) return false;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING) {
        return strcmp(arg->ground.sval, expected_kind) == 0;
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
        };
        slot->resource = owner;
        slot->free_resource = native_handle_release;
    }
    /* Reserve across allocation, which registers its own arena reference. */
    if (owner->references == SIZE_MAX)
        abort();
    ++owner->references;
    pthread_mutex_unlock(&native_handle_mutex);
    Atom *identifier = atom_native_handle_identifier(
        a, (int64_t)id, owner, native_handle_retain, native_handle_release);
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
    *id_out = ctx->native_handles[slot].id;
    pthread_mutex_unlock(&native_handle_mutex);
    return true;
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
