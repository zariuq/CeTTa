#include "native_handle.h"

#include "library.h"
#include <string.h>

static bool native_handle_kind_matches(Atom *arg, const char *expected_kind) {
    if (!arg || !expected_kind) return false;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING) {
        return strcmp(arg->ground.sval, expected_kind) == 0;
    }
    if (arg->kind == ATOM_SYMBOL) {
        return strcmp(arg->name, expected_kind) == 0;
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

bool cetta_native_handle_arg(Atom *arg, const char *expected_kind, uint64_t *id_out) {
    if (!arg || !id_out || arg->kind != ATOM_EXPR || arg->expr.len != 3) return false;
    if (!atom_is_symbol(arg->expr.elems[0], "NativeHandle")) return false;
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
    for (uint32_t i = 0; i < ctx->native_handle_len; i++) {
        if (ctx->native_handles[i].resource == NULL) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        if (ctx->native_handle_len >= CETTA_MAX_NATIVE_HANDLES) return false;
        slot = (int)ctx->native_handle_len++;
    }
    if (ctx->native_handles[slot].id == 0) {
        ctx->native_handles[slot].id = ctx->native_handle_next_id++;
    }
    ctx->native_handles[slot].kind = kind;
    ctx->native_handles[slot].resource = resource;
    ctx->native_handles[slot].free_resource = free_resource;
    *id_out = ctx->native_handles[slot].id;
    return true;
}

void *cetta_native_handle_get(struct CettaLibraryContext *ctx, const char *kind,
                              uint64_t id) {
    if (!ctx || !kind) return NULL;
    for (uint32_t i = 0; i < ctx->native_handle_len; i++) {
        if (ctx->native_handles[i].id == id &&
            ctx->native_handles[i].resource != NULL &&
            ctx->native_handles[i].kind != NULL &&
            strcmp(ctx->native_handles[i].kind, kind) == 0) {
            return ctx->native_handles[i].resource;
        }
    }
    return NULL;
}

bool cetta_native_handle_close(struct CettaLibraryContext *ctx, const char *kind,
                               uint64_t id) {
    if (!ctx || !kind) return false;
    for (uint32_t i = 0; i < ctx->native_handle_len; i++) {
        if (ctx->native_handles[i].id == id &&
            ctx->native_handles[i].resource != NULL &&
            ctx->native_handles[i].kind != NULL &&
            strcmp(ctx->native_handles[i].kind, kind) == 0) {
            if (ctx->native_handles[i].free_resource) {
                ctx->native_handles[i].free_resource(ctx->native_handles[i].resource);
            }
            ctx->native_handles[i].resource = NULL;
            ctx->native_handles[i].kind = NULL;
            ctx->native_handles[i].free_resource = NULL;
            return true;
        }
    }
    return false;
}

void cetta_native_handle_cleanup_all(struct CettaLibraryContext *ctx) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->native_handle_len; i++) {
        if (ctx->native_handles[i].resource != NULL) {
            if (ctx->native_handles[i].free_resource) {
                ctx->native_handles[i].free_resource(ctx->native_handles[i].resource);
            }
            ctx->native_handles[i].resource = NULL;
            ctx->native_handles[i].kind = NULL;
            ctx->native_handles[i].free_resource = NULL;
        }
        ctx->native_handles[i].id = 0;
    }
    ctx->native_handle_len = 0;
}
