#include "variant_shape.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    CettaVarMap *src_to_slot;
    CettaVarMap *slot_to_src;
    const CettaVariantShapeOptions *options;
} VariantCanonCtx;

typedef struct {
    const CettaVarMap *goal_instantiation;
    CettaVarMap *local_slots;
} VariantMaterializeCtx;

static const char *variant_shape_slot_name_or_default(const CettaVariantShapeOptions *options) {
    if (options && options->slot_name && options->slot_name[0] != '\0')
        return options->slot_name;
    return "$_slot";
}

VarId variant_shape_slot_id(uint32_t ordinal) {
    return ((VarId)ordinal << 32) | (VarId)ordinal;
}

void variant_shape_init(VariantShape *shape) {
    if (!shape)
        return;
    shape->skeleton = NULL;
    bindings_init(&shape->slot_env);
}

void variant_shape_free(VariantShape *shape) {
    if (!shape)
        return;
    bindings_free(&shape->slot_env);
    shape->skeleton = NULL;
}

void variant_bank_init(VariantBank *bank, CettaVariantShapeOptions options) {
    if (!bank)
        return;
    arena_init(&bank->arena);
    hashcons_init(&bank->hashcons);
    bank->options = options;
    arena_set_hashcons(&bank->arena, &bank->hashcons);
}

void variant_bank_free(VariantBank *bank) {
    if (!bank)
        return;
    hashcons_free(&bank->hashcons);
    arena_free(&bank->arena);
    memset(bank, 0, sizeof(*bank));
}

static Atom *variant_shape_create_slot_var(Arena *dst, Atom *src_var,
                                           uint32_t ordinal, void *ctx) {
    const CettaVariantShapeOptions *options = ctx;
    VarId canonical_id = variant_shape_slot_id(ordinal);
    const char *slot_name = variant_shape_slot_name_or_default(options);

    switch (options ? options->slot_policy : CETTA_VARIANT_SLOT_FIXED_SPELLING) {
    case CETTA_VARIANT_SLOT_SOURCE_SPELLING:
        return atom_var_with_spelling(dst, src_var->sym_id, canonical_id);
    case CETTA_VARIANT_SLOT_ORDINAL_NAME: {
        char name[64];
        snprintf(name, sizeof(name), "%s%u", slot_name, ordinal);
        return atom_var_with_id(dst, name, canonical_id);
    }
    case CETTA_VARIANT_SLOT_FIXED_SPELLING:
    default:
        return atom_var_with_id(dst, slot_name, canonical_id);
    }
}

static Atom *variant_shape_rewrite_canonical_var(Arena *dst, Atom *src_var, void *ctx) {
    VariantCanonCtx *canon = ctx;
    Atom *slot = cetta_var_map_get_or_add(canon->src_to_slot, dst, src_var,
                                          variant_shape_create_slot_var,
                                          (void *)canon->options);
    if (!slot)
        return NULL;
    if (canon->slot_to_src &&
        !cetta_var_map_add(canon->slot_to_src, slot->var_id, src_var)) {
        return NULL;
    }
    return slot;
}

Atom *variant_shape_canonicalize_atom(Arena *dst, Atom *src,
                                      CettaVarMap *src_to_slot,
                                      CettaVarMap *slot_to_src,
                                      const CettaVariantShapeOptions *options) {
    if (!dst || !src || !src_to_slot || !options)
        return NULL;
    VariantCanonCtx ctx = {
        .src_to_slot = src_to_slot,
        .slot_to_src = slot_to_src,
        .options = options,
    };
    return cetta_atom_rewrite_vars(dst, src,
                                   variant_shape_rewrite_canonical_var,
                                   &ctx, options->share_immutable);
}

static Atom *variant_shape_create_materialized_var(Arena *dst, Atom *src_var,
                                                   uint32_t ordinal, void *ctx) {
    (void)ordinal;
    (void)ctx;
    return atom_var_with_spelling(dst, src_var->sym_id, fresh_var_id());
}

static Atom *variant_shape_rewrite_materialized_var(Arena *dst, Atom *src_var,
                                                    void *ctx) {
    VariantMaterializeCtx *materialize = ctx;
    Atom *goal_var = cetta_var_map_lookup(materialize->goal_instantiation,
                                          src_var->var_id);
    if (goal_var)
        return goal_var;
    return cetta_var_map_get_or_add(materialize->local_slots, dst, src_var,
                                    variant_shape_create_materialized_var,
                                    NULL);
}

Atom *variant_shape_materialize_atom(Arena *dst, Atom *src,
                                     const CettaVarMap *goal_instantiation,
                                     CettaVarMap *local_slots) {
    if (!dst || !src || !goal_instantiation || !local_slots)
        return NULL;
    VariantMaterializeCtx ctx = {
        .goal_instantiation = goal_instantiation,
        .local_slots = local_slots,
    };
    return cetta_atom_rewrite_vars(dst, src,
                                   variant_shape_rewrite_materialized_var,
                                   &ctx, false);
}

static bool variant_shape_build_slot_env(const CettaVarMap *src_to_slot,
                                         const CettaVarMap *slot_to_src,
                                         Bindings *slot_env) {
    if (!src_to_slot || !slot_to_src || !slot_env)
        return false;
    for (uint32_t i = 0; i < src_to_slot->len; i++) {
        Atom *slot = src_to_slot->items[i].mapped_var;
        Atom *live_var = cetta_var_map_lookup(slot_to_src, slot->var_id);
        if (!slot || !live_var ||
            !bindings_add_id(slot_env, slot->var_id, slot->sym_id, live_var)) {
            return false;
        }
    }
    return true;
}

bool variant_shape_canonicalize_bindings(Arena *dst, const Bindings *src,
                                         CettaVarMap *src_to_slot,
                                         const CettaVariantShapeOptions *options,
                                         Bindings *out) {
    if (!dst || !src_to_slot || !options || !out)
        return false;
    bindings_init(out);
    if (!src)
        return true;
    for (uint32_t i = 0; i < src->len; i++) {
        Atom binding_var = {
            .kind = ATOM_VAR,
            .sym_id = src->entries[i].spelling,
            .var_id = src->entries[i].var_id,
        };
        Atom *slot_var =
            cetta_var_map_get_or_add(src_to_slot, dst, &binding_var,
                                     variant_shape_create_slot_var,
                                     (void *)options);
        Atom *slot_val = variant_shape_canonicalize_atom(dst, src->entries[i].val,
                                                         src_to_slot, NULL,
                                                         options);
        if (!slot_var || !slot_val ||
            !bindings_add_id(out, slot_var->var_id, slot_var->sym_id, slot_val)) {
            bindings_free(out);
            return false;
        }
        out->entries[out->len - 1].legacy_name_fallback =
            src->entries[i].legacy_name_fallback;
    }
    for (uint32_t i = 0; i < src->eq_len; i++) {
        Atom *lhs = variant_shape_canonicalize_atom(dst, src->constraints[i].lhs,
                                                    src_to_slot, NULL,
                                                    options);
        Atom *rhs = variant_shape_canonicalize_atom(dst, src->constraints[i].rhs,
                                                    src_to_slot, NULL,
                                                    options);
        if (!lhs || !rhs || !bindings_add_constraint(out, lhs, rhs)) {
            bindings_free(out);
            return false;
        }
    }
    out->lookup_cache_count = 0;
    out->lookup_cache_next = 0;
    return true;
}

bool variant_shape_materialize_bindings(Arena *dst, const Bindings *src,
                                        const CettaVarMap *goal_instantiation,
                                        CettaVarMap *local_slots,
                                        Bindings *out) {
    if (!dst || !goal_instantiation || !local_slots || !out)
        return false;
    bindings_init(out);
    if (!src)
        return true;
    for (uint32_t i = 0; i < src->len; i++) {
        Atom binding_var = {
            .kind = ATOM_VAR,
            .sym_id = src->entries[i].spelling,
            .var_id = src->entries[i].var_id,
        };
        Atom *goal_var = cetta_var_map_lookup(goal_instantiation,
                                              src->entries[i].var_id);
        if (!goal_var) {
            goal_var = variant_shape_materialize_atom(dst, &binding_var,
                                                      goal_instantiation,
                                                      local_slots);
        }
        Atom *materialized_val = variant_shape_materialize_atom(dst,
                                                                src->entries[i].val,
                                                                goal_instantiation,
                                                                local_slots);
        if (!goal_var || !materialized_val ||
            !bindings_add_id(out, goal_var->var_id, goal_var->sym_id,
                             materialized_val)) {
            bindings_free(out);
            return false;
        }
        out->entries[out->len - 1].legacy_name_fallback =
            src->entries[i].legacy_name_fallback;
    }
    for (uint32_t i = 0; i < src->eq_len; i++) {
        Atom *lhs = variant_shape_materialize_atom(dst, src->constraints[i].lhs,
                                                   goal_instantiation,
                                                   local_slots);
        Atom *rhs = variant_shape_materialize_atom(dst, src->constraints[i].rhs,
                                                   goal_instantiation,
                                                   local_slots);
        if (!lhs || !rhs || !bindings_add_constraint(out, lhs, rhs)) {
            bindings_free(out);
            return false;
        }
    }
    out->lookup_cache_count = 0;
    out->lookup_cache_next = 0;
    return true;
}

bool variant_shape_from_atom(VariantBank *bank, Atom *src, VariantShape *out) {
    if (!bank || !src || !out)
        return false;

    variant_shape_init(out);

    ArenaMark mark = arena_mark(&bank->arena);
    CettaVarMap src_to_slot;
    CettaVarMap slot_to_src;
    Bindings slot_env;
    cetta_var_map_init(&src_to_slot);
    cetta_var_map_init(&slot_to_src);
    bindings_init(&slot_env);

    Atom *skeleton = variant_shape_canonicalize_atom(&bank->arena, src,
                                                     &src_to_slot,
                                                     &slot_to_src,
                                                     &bank->options);
    if (!skeleton)
        goto fail;

    if (!variant_shape_build_slot_env(&src_to_slot, &slot_to_src, &slot_env))
        goto fail;

    out->skeleton = skeleton;
    bindings_move(&out->slot_env, &slot_env);
    cetta_var_map_free(&src_to_slot);
    cetta_var_map_free(&slot_to_src);
    bindings_free(&slot_env);
    return true;

fail:
    cetta_var_map_free(&src_to_slot);
    cetta_var_map_free(&slot_to_src);
    bindings_free(&slot_env);
    variant_shape_free(out);
    arena_reset(&bank->arena, mark);
    return false;
}

bool variant_shape_from_bound_atom(VariantBank *bank, const Bindings *env,
                                   Atom *src, VariantShape *out) {
    if (!bank || !src || !out)
        return false;

    variant_shape_init(out);

    ArenaMark mark = arena_mark(&bank->arena);
    CettaVarMap src_to_slot;
    CettaVarMap slot_to_src;
    Bindings slot_env;
    Bindings empty_env;
    Bindings *env_mut = (Bindings *)(env ? env : &empty_env);
    VariantCanonCtx ctx = {
        .src_to_slot = &src_to_slot,
        .slot_to_src = &slot_to_src,
        .options = &bank->options,
    };
    cetta_var_map_init(&src_to_slot);
    cetta_var_map_init(&slot_to_src);
    bindings_init(&slot_env);
    bindings_init(&empty_env);

    Atom *skeleton = bindings_apply_rewrite_vars(env_mut, &bank->arena, src,
                                                 variant_shape_rewrite_canonical_var,
                                                 &ctx);
    if (!skeleton)
        goto fail;

    if (!variant_shape_build_slot_env(&src_to_slot, &slot_to_src, &slot_env))
        goto fail;

    out->skeleton = skeleton;
    bindings_move(&out->slot_env, &slot_env);
    cetta_var_map_free(&src_to_slot);
    cetta_var_map_free(&slot_to_src);
    bindings_free(&slot_env);
    bindings_free(&empty_env);
    return true;

fail:
    cetta_var_map_free(&src_to_slot);
    cetta_var_map_free(&slot_to_src);
    bindings_free(&slot_env);
    bindings_free(&empty_env);
    variant_shape_free(out);
    arena_reset(&bank->arena, mark);
    return false;
}

Atom *variant_shape_materialize(Arena *dst, const VariantShape *shape) {
    if (!dst || !shape || !shape->skeleton)
        return NULL;
    return bindings_apply((Bindings *)&shape->slot_env, dst, shape->skeleton);
}
