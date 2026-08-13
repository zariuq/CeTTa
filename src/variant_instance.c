#include "variant_instance.h"
#include "stats.h"

#include <stdlib.h>
#include <string.h>

struct VariantInstanceStorage {
    Atom **slot_vals;
    uint32_t slot_count;
};

typedef struct {
    const VariantInstance *instance;
} VariantInstanceMaterializeCtx;

typedef struct {
    uint32_t ordinal_base;
} VariantInstanceRebaseCtx;

static VariantInstanceStorage *variant_instance_storage(const VariantInstance *instance) {
    return instance ? instance->storage : NULL;
}

static VariantInstanceStorage *variant_instance_storage_new(void) {
    VariantInstanceStorage *storage = cetta_malloc(sizeof(*storage));
    storage->slot_vals = NULL;
    storage->slot_count = 0;
    return storage;
}

static bool variant_instance_reserve_slots(VariantInstanceStorage *storage,
                                           uint32_t needed) {
    if (!storage)
        return false;
    if (needed <= storage->slot_count)
        return true;
    Atom **next = cetta_realloc(storage->slot_vals, sizeof(Atom *) * needed);
    if (!next)
        return false;
    for (uint32_t i = storage->slot_count; i < needed; i++)
        next[i] = NULL;
    storage->slot_vals = next;
    storage->slot_count = needed;
    return true;
}

static Atom *variant_instance_rewrite_slot_var(Arena *dst, Atom *src_var,
                                               void *ctx) {
    VariantInstanceMaterializeCtx *materialize = ctx;
    VariantInstanceStorage *storage =
        materialize ? variant_instance_storage(materialize->instance) : NULL;
    if (!variant_private_var_id(src_var->var_id))
        return src_var;
    uint32_t ordinal = variant_shape_slot_ordinal(src_var->var_id);
    if (!storage || ordinal >= storage->slot_count)
        return src_var;
    Atom *slot_val = storage->slot_vals[ordinal];
    return slot_val ? slot_val : src_var;
}

static Atom *variant_instance_rewrite_rebased_slot_var(Arena *dst, Atom *src_var,
                                                       void *ctx) {
    VariantInstanceRebaseCtx *rebase = ctx;
    if (!variant_private_var_id(src_var->var_id))
        return src_var;
    return atom_var_like(dst, src_var,
                         variant_shape_slot_id(
                             rebase->ordinal_base +
                             variant_shape_slot_ordinal(src_var->var_id)));
}

void variant_instance_init(VariantInstance *instance) {
    if (!instance)
        return;
    instance->storage = NULL;
}

void variant_instance_free(VariantInstance *instance) {
    VariantInstanceStorage *storage;
    if (!instance)
        return;
    storage = instance->storage;
    if (!storage)
        return;
    free(storage->slot_vals);
    free(storage);
    instance->storage = NULL;
}

void variant_instance_move(VariantInstance *dst, VariantInstance *src) {
    if (!dst || !src)
        return;
    *dst = *src;
    variant_instance_init(src);
}

bool variant_instance_present(const VariantInstance *instance) {
    VariantInstanceStorage *storage = variant_instance_storage(instance);
    return storage && storage->slot_count > 0;
}

bool variant_instance_clone(VariantInstance *dst, const VariantInstance *src) {
    VariantInstanceStorage *src_storage;
    if (!dst || !src)
        return false;
    src_storage = variant_instance_storage(src);
    VariantInstance next;
    variant_instance_init(&next);
    if (src_storage) {
        next.storage = variant_instance_storage_new();
    }
    if (src_storage && !variant_instance_reserve_slots(next.storage, src_storage->slot_count)) {
        variant_instance_free(&next);
        return false;
    }
    if (src_storage && src_storage->slot_count > 0) {
        memcpy(next.storage->slot_vals, src_storage->slot_vals,
               sizeof(Atom *) * src_storage->slot_count);
    }
    variant_instance_free(dst);
    *dst = next;
    return true;
}

bool variant_instance_promote_atoms_to_arena(Arena *dst,
                                             VariantInstance *instance) {
    if (!dst)
        return true;
    AtomDeepCopySession *session = atom_deep_copy_session_new(dst);
    if (!session)
        return false;
    bool promoted =
        variant_instance_promote_atoms_with_session(session, instance);
    atom_deep_copy_session_free(session);
    return promoted;
}

bool variant_instance_promote_atoms_with_session(
    AtomDeepCopySession *session, VariantInstance *instance) {
    VariantInstanceStorage *storage = variant_instance_storage(instance);
    if (!session || !storage)
        return true;
    for (uint32_t i = 0; i < storage->slot_count; i++) {
        Atom *slot_val = storage->slot_vals[i];
        if (!slot_val) {
            storage->slot_vals[i] = NULL;
            continue;
        }
        Atom *promoted =
            atom_deep_copy_session_copy(session, slot_val);
        if (!promoted)
            return false;
        storage->slot_vals[i] = promoted;
    }
    return true;
}

bool variant_instance_atoms_closed_for_arena(
    const VariantInstance *instance, const Arena *arena) {
    VariantInstanceStorage *storage = variant_instance_storage(instance);
    if (!arena)
        return false;
    if (!storage)
        return true;
    for (uint32_t i = 0u; i < storage->slot_count; i++) {
        Atom *value = storage->slot_vals[i];
        if (value && !atom_graph_is_closed_for_arena(arena, value))
            return false;
    }
    return true;
}

bool variant_instance_from_shape(VariantInstance *out, const VariantShape *shape) {
    VariantInstanceStorage *storage;
    if (!out || !shape)
        return false;
    VariantInstance next;
    variant_instance_init(&next);
    next.storage = variant_instance_storage_new();
    if (shape->slot_env.eq_len != 0) {
        variant_instance_free(&next);
        return false;
    }
    storage = next.storage;
    for (uint32_t i = 0; i < shape->slot_env.len; i++) {
        const Binding *entry = &shape->slot_env.entries[i];
        if (!variant_private_var_id(entry->var_id)) {
            variant_instance_free(&next);
            return false;
        }
        uint32_t ordinal = variant_shape_slot_ordinal(entry->var_id);
        if (!variant_instance_reserve_slots(storage, ordinal + 1)) {
            variant_instance_free(&next);
            return false;
        }
        storage->slot_vals[ordinal] = entry->val;
    }
    variant_instance_free(out);
    *out = next;
    return true;
}

bool variant_instance_append_rebased(Arena *dst, VariantInstance *instance,
                                     Atom **out_skeleton,
                                     Atom *skeleton,
                                     const VariantInstance *child) {
    VariantInstanceStorage *dst_storage;
    VariantInstanceStorage *child_storage;
    uint32_t base;

    if (!dst || !instance || !out_skeleton || !skeleton || !child)
        return false;
    child_storage = variant_instance_storage(child);
    if (!child_storage || child_storage->slot_count == 0) {
        *out_skeleton = skeleton;
        return true;
    }
    if (!instance->storage)
        instance->storage = variant_instance_storage_new();
    dst_storage = instance->storage;
    base = dst_storage->slot_count;
    if (!variant_instance_reserve_slots(dst_storage, base + child_storage->slot_count))
        return false;
    for (uint32_t i = 0; i < child_storage->slot_count; i++)
        dst_storage->slot_vals[base + i] = child_storage->slot_vals[i];
    VariantInstanceRebaseCtx ctx = {
        .ordinal_base = base,
    };
    *out_skeleton = cetta_atom_rewrite_vars(dst, skeleton,
                                            variant_instance_rewrite_rebased_slot_var,
                                            &ctx, false);
    return *out_skeleton != NULL;
}

bool variant_instance_sink_env(Arena *dst, VariantInstance *out,
                               const VariantInstance *src,
                               const Bindings *env) {
    VariantInstanceStorage *src_storage;
    VariantInstanceStorage *next_storage;
    if (!dst || !out || !src)
        return false;
    src_storage = variant_instance_storage(src);
    if (!env || (env->len == 0 && env->eq_len == 0))
        return variant_instance_clone(out, src);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_SLOT_SINK);

    VariantInstance next;
    variant_instance_init(&next);
    if (!src_storage)
        return variant_instance_clone(out, src);
    next.storage = variant_instance_storage_new();
    next_storage = next.storage;
    if (!variant_instance_reserve_slots(next_storage, src_storage->slot_count)) {
        variant_instance_free(&next);
        return false;
    }
    for (uint32_t i = 0; i < src_storage->slot_count; i++) {
        Atom *slot_val = src_storage->slot_vals[i];
        if (!slot_val) {
            next_storage->slot_vals[i] = NULL;
            continue;
        }
        if (!atom_has_vars(slot_val)) {
            next_storage->slot_vals[i] = slot_val;
            continue;
        }
        next_storage->slot_vals[i] = bindings_apply((Bindings *)env, dst, slot_val);
        if (!next_storage->slot_vals[i]) {
            variant_instance_free(&next);
            return false;
        }
    }
    variant_instance_free(out);
    *out = next;
    return true;
}

Atom *variant_instance_materialize(Arena *dst, Atom *skeleton,
                                   const VariantInstance *instance) {
    VariantInstanceStorage *storage = variant_instance_storage(instance);
    if (!dst || !skeleton || !storage || storage->slot_count == 0)
        return skeleton;
    VariantInstanceMaterializeCtx ctx = {
        .instance = instance,
    };
    return cetta_atom_rewrite_vars(dst, skeleton,
                                   variant_instance_rewrite_slot_var,
                                   &ctx, false);
}

Atom *variant_instance_peek_private_var(const VariantInstance *instance,
                                        Atom *var) {
    VariantInstanceStorage *storage = variant_instance_storage(instance);
    if (!storage || !var || var->kind != ATOM_VAR ||
        !variant_private_var_id(var->var_id)) {
        return NULL;
    }
    uint32_t ordinal = variant_shape_slot_ordinal(var->var_id);
    if (ordinal >= storage->slot_count)
        return NULL;
    return storage->slot_vals[ordinal];
}
