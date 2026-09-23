#include "term_canon.h"

#include <stdlib.h>
#include <string.h>

static Atom *frame_syntax_create_slot(
        Arena *dst, Atom *source, uint32_t ordinal, void *context) {
    (void)context;
    return ordinal ? atom_var_like(dst, source, (VarId)ordinal) : NULL;
}

static Atom *frame_syntax_rewrite_variable(Arena *dst, Atom *source, void *context) {
    CettaVarMap *inventory = context;
    if (inventory->len == UINT32_MAX &&
        !cetta_var_map_lookup(inventory, source->var_id))
        return NULL;
    return cetta_var_map_get_or_add(
        inventory, dst, source, frame_syntax_create_slot, NULL);
}

Atom *cetta_compile_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory) {
    return inventory ? cetta_atom_rewrite_vars(
        dst, src, frame_syntax_rewrite_variable, inventory, true) : NULL;
}

typedef struct {
    CettaVarMap *inventory;
    CettaFrameIdentity identity;
} FrameSyntaxImport;

static Atom *frame_syntax_import_variable(Arena *dst, Atom *source, void *context) {
    FrameSyntaxImport *import = context;
    if (var_epoch_suffix(source->var_id) != 0u)
        return atom_deep_copy_shared(dst, source);
    Atom *slot = frame_syntax_rewrite_variable(dst, source, import->inventory);
    return slot ? atom_var_like(dst, source, var_epoch_id(slot->var_id, import->identity))
                : NULL;
}

Atom *cetta_import_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory,
                              CettaFrameIdentity identity) {
    FrameSyntaxImport import = {inventory, identity};
    return inventory && identity ? cetta_atom_rewrite_vars(
        dst, src, frame_syntax_import_variable, &import, true) : NULL;
}

static Atom *frame_instantiate_create_slot(
        Arena *dst, Atom *source, uint32_t ordinal, void *context) {
    (void)ordinal;
    CettaFrameIdentity identity = *(CettaFrameIdentity *)context;
    uint32_t slot;
    return cetta_frame_identity_new_slot(identity, 0u, &slot)
        ? atom_var_like(dst, source, var_epoch_id(slot, identity)) : NULL;
}

static Atom *frame_instantiate_variable(
        Arena *dst, Atom *source, void *context) {
    FrameSyntaxImport *instance = context;
    return cetta_var_map_get_or_add(instance->inventory, dst, source,
        frame_instantiate_create_slot, &instance->identity);
}

bool cetta_instantiate_frame_terms(Arena *dst, Atom **terms, size_t count) {
    if (!dst || (count && !terms))
        return false;
    CETTA_FRAME_IDENTITY_SCOPE(owner);
    CettaFrameIdentity identity;
    if (!cetta_frame_identity_scope_try(&owner, &identity))
        return false;
    CettaVarMap inventory;
    cetta_var_map_init(&inventory);
    FrameSyntaxImport instance = {&inventory, identity};
    bool ok = true;
    for (size_t i = 0u; ok && i < count; i++) {
        terms[i] = cetta_atom_rewrite_vars(
            dst, terms[i], frame_instantiate_variable, &instance, true);
        ok = terms[i] != NULL;
    }
    cetta_var_map_free(&inventory);
    return ok;
}

Atom *cetta_instantiate_frame_syntax(Arena *dst, Atom *source) {
    return cetta_instantiate_frame_terms(dst, &source, 1u) ? source : NULL;
}

void cetta_var_map_init(CettaVarMap *map) {
    if (!map)
        return;
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

void cetta_var_map_free(CettaVarMap *map) {
    if (!map)
        return;
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

bool cetta_var_map_reserve(CettaVarMap *map, uint32_t needed) {
    if (!map)
        return false;
    if (needed <= map->cap)
        return true;
    uint32_t next_cap = map->cap ? map->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    map->items = map->items
        ? cetta_realloc(map->items, sizeof(CettaVarMapEntry) * next_cap)
        : cetta_malloc(sizeof(CettaVarMapEntry) * next_cap);
    map->cap = next_cap;
    return true;
}

Atom *cetta_var_map_lookup(const CettaVarMap *map, VarId source_id) {
    if (!map)
        return NULL;
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].source_id == source_id)
            return map->items[i].mapped_var;
    }
    return NULL;
}

bool cetta_var_map_add(CettaVarMap *map, VarId source_id, Atom *mapped_var) {
    Atom *existing = cetta_var_map_lookup(map, source_id);
    if (existing)
        return existing == mapped_var;
    if (!map || !mapped_var || !cetta_var_map_reserve(map, map->len + 1))
        return false;
    map->items[map->len].source_id = source_id;
    map->items[map->len].mapped_var = mapped_var;
    map->len++;
    return true;
}

bool cetta_var_map_clone(CettaVarMap *dst, const CettaVarMap *src) {
    cetta_var_map_init(dst);
    if (!src || src->len == 0)
        return true;
    if (!cetta_var_map_reserve(dst, src->len))
        return false;
    memcpy(dst->items, src->items, sizeof(CettaVarMapEntry) * src->len);
    dst->len = src->len;
    return true;
}

bool cetta_var_map_clone_live(Arena *dst, CettaVarMap *out,
                              const CettaVarMap *src) {
    cetta_var_map_init(out);
    if (!src || src->len == 0)
        return true;
    if (!dst || !cetta_var_map_reserve(out, src->len))
        return false;
    for (uint32_t i = 0; i < src->len; i++) {
        Atom *mapped = src->items[i].mapped_var
            ? atom_deep_copy(dst, src->items[i].mapped_var)
            : NULL;
        if (src->items[i].mapped_var && !mapped) {
            cetta_var_map_free(out);
            return false;
        }
        out->items[i].source_id = src->items[i].source_id;
        out->items[i].mapped_var = mapped;
    }
    out->len = src->len;
    return true;
}

Atom *cetta_var_map_get_or_add(CettaVarMap *map, Arena *dst, Atom *src_var,
                               CettaVarMapCreateFn create_var, void *ctx) {
    if (!map || !dst || !src_var || src_var->kind != ATOM_VAR || !create_var)
        return NULL;
    Atom *existing = cetta_var_map_lookup(map, src_var->var_id);
    if (existing)
        return existing;
    Atom *created = create_var(dst, src_var, map->len + 1, ctx);
    if (!created || !cetta_var_map_add(map, src_var->var_id, created))
        return NULL;
    return created;
}

Atom *cetta_atom_rewrite_vars(Arena *dst, Atom *src,
                              CettaAtomRewriteVarFn rewrite_var, void *ctx,
                              bool share_immutable) {
    if (!dst || !src || !rewrite_var)
        return NULL;
    switch (src->kind) {
    case ATOM_VAR:
        return rewrite_var(dst, src, ctx);
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(dst, sizeof(Atom *) * src->expr.len);
        for (CettaExprIndex i = 0; i < src->expr.len; i++) {
            elems[i] = cetta_atom_rewrite_vars(dst, src->expr.elems[i],
                                               rewrite_var, ctx,
                                               share_immutable);
            if (!elems[i])
                return NULL;
        }
        return atom_expr(dst, elems, src->expr.len);
    }
    default:
        return share_immutable ? atom_deep_copy_shared(dst, src)
                               : atom_deep_copy(dst, src);
    }
}
