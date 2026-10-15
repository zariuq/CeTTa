#include "owned_return_image.h"

#include <assert.h>
#include <stdlib.h>

struct CettaOwnedReturnImage {
    CettaOwnedReturn returned;
    size_t references;
    Arena syntax;
    Bindings environment;
};

_Static_assert(offsetof(CettaOwnedReturnImage, returned) == 0u,
               "owned return view must begin its image");

typedef struct {
    Atom *source_key;
    Atom *variable;
} PendingNameKey;

typedef struct {
    const CettaOwnedReturnImagePolicy *policy;
    AtomDeepCopySession *session;
    PendingNameKey *keys;
    size_t key_count;
    size_t key_capacity;
} ReturnCopy;

/* Inspect every operational root, including superseded immutable updates.
 * The enclosing copy memo also owns these roots, so aliases across the
 * visible value, logical bindings and the captured world stay aliases. */
static bool capture_return_root(const Atom *root, void *context) {
    ReturnCopy *copy = context;
    return atom_deep_copy_session_copy(copy->session, (Atom *)root) != NULL;
}

/* A receipt can name a mutable cell without an Atom carrying its owner.
 * The leaf-owner policy does not describe such a bare resource identity. */
static bool refuse_unqualified_state_cell(const StateCell *cell, void *context) {
    (void)cell;
    (void)context;
    return false;
}

static bool remember_name_key(ReturnCopy *copy, Atom *source, Atom *variable) {
    if (copy->key_count == copy->key_capacity) {
        size_t capacity = copy->key_capacity ? copy->key_capacity * 2u : 16u;
        if (capacity < copy->key_capacity || capacity > SIZE_MAX / sizeof(*copy->keys))
            return false;
        PendingNameKey *keys = realloc(copy->keys, capacity * sizeof(*keys));
        if (!keys)
            return false;
        copy->keys = keys;
        copy->key_capacity = capacity;
    }
    copy->keys[copy->key_count++] = (PendingNameKey){source->name_key, variable};
    return true;
}

static Atom *capture_return_leaf(Arena *arena, Atom *leaf, void *context) {
    ReturnCopy *copy = context;
    if (leaf->kind == ATOM_SYMBOL)
        return atom_symbol_copy(arena, leaf);
    if (leaf->kind == ATOM_VAR) {
        Atom *variable = atom_var_with_spelling(arena, atom_symbol_current_id(leaf), leaf->var_id);
        if (leaf->name_key) {
            /* Name keys are Atom roots even though a variable is a syntax
             * leaf. Queue them in this same episode instead of invoking an
             * unmapped or recursively nested Atom copy. Until filled, the
             * variable and its enclosing terms keep conservative closure
             * and hash-cons facts. Nothing is published during construction. */
            variable->flags &= ~(ATOM_FLAG_ARENA_CLOSED | ATOM_FLAG_HASHCONS_ELIGIBLE);
            if (!remember_name_key(copy, leaf, variable))
                return NULL;
        }
        return variable;
    }
    if (leaf->kind != ATOM_GROUNDED)
        return NULL;
    switch (leaf->ground.gkind) {
    case GV_INT: return atom_int_copy(arena, leaf);
    case GV_FLOAT: return atom_float(arena, leaf->ground.fval);
    case GV_BOOL: return atom_bool(arena, leaf->ground.bval);
    case GV_STRING: return atom_string_n(arena, leaf->ground.sval, leaf->ground.slen);
    case GV_BIGINT: return atom_bigint_copy(arena, leaf);
    case GV_RATIONAL: return atom_rational(arena, atom_rational_cstr(leaf));
    case GV_INTERNAL_TAG: return atom_internal_tag(arena, (CettaInternalTag)leaf->ground.ival);
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_FOREIGN:
    case GV_TERM_GRAPH:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
        break;
    default:
        return NULL;
    }
    CettaReturnLeafOwner hold = {0};
    const CettaOwnedReturnImagePolicy *policy = copy->policy;
    if (!policy || !policy->describe_leaf_owner ||
        !policy->describe_leaf_owner(policy->context, leaf, &hold) ||
        !hold.owner || !hold.retain || !hold.release ||
        !arena_retain_owner(arena, hold.owner, hold.retain, hold.release))
        return NULL;
    Atom *owned = atom_deep_copy(arena, leaf);
    return owned && owned->kind == leaf->kind &&
        owned->ground.gkind == leaf->ground.gkind ? owned : NULL;
}

CettaOwnedReturnImage *cetta_owned_return_image_capture(
    const CettaOwnedReturn *returned,
    const CettaOwnedReturnImagePolicy *policy) {
    if (!returned)
        return NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (returned->environment && bindings_receipt_view(returned->environment)->recording)
        return NULL;
#endif
    CettaOwnedReturnImage *image = calloc(1u, sizeof(*image));
    if (!image)
        return NULL;
    image->references = 1u;
    image->returned = *returned;
    arena_init_detached(&image->syntax);
    bindings_init(&image->environment);
    ReturnCopy copy = {.policy = policy};
    AtomDeepCopySession *session = atom_deep_copy_session_new_mapped(
        &image->syntax, capture_return_leaf, &copy);
    copy.session = session;
    bool ok = session != NULL;
    if (ok && returned->outcome.term) {
        image->returned.outcome.term = atom_deep_copy_session_copy(
            session, returned->outcome.term);
        ok = image->returned.outcome.term != NULL;
    }
    if (ok && returned->outcome.delayed) {
        image->returned.outcome.delayed = atom_deep_copy_session_copy(
            session, returned->outcome.delayed);
        ok = image->returned.outcome.delayed != NULL;
    }
    if (ok && returned->environment) {
        ok = bindings_payloads_satisfy(returned->environment,
                 capture_return_root, refuse_unqualified_state_cell, &copy) &&
             bindings_copy(&image->environment, returned->environment) &&
             bindings_copy_owned_atoms_with_session(
                 &image->environment, &image->syntax, session);
        image->returned.environment = &image->environment;
    }
    for (size_t index = 0u; ok && index < copy.key_count; ++index) {
        PendingNameKey key = copy.keys[index];
        Atom *owned = atom_deep_copy_session_copy(session, key.source_key);
        if (!owned)
            ok = false;
        else
            key.variable->name_key = owned;
    }
    atom_deep_copy_session_free(session);
    free(copy.keys);
    if (!ok) {
        cetta_owned_return_image_release(image);
        return NULL;
    }
    /* Every logical Atom root has moved and every opaque leaf acquired its
     * independent lifetime. Ancestor images are no longer syntax owners. */
    bindings_owners_release(image->environment.owners);
    image->environment.owners = NULL;
    return image;
}

const CettaOwnedReturn *cetta_owned_return_image_view(
    const CettaOwnedReturnImage *image) {
    return image ? &image->returned : NULL;
}

bool cetta_owned_return_image_retain(CettaOwnedReturnImage *image) {
    if (!image || image->references == SIZE_MAX)
        return false;
    assert(image->references != 0u);
    ++image->references;
    return true;
}

void cetta_owned_return_image_release(void *raw) {
    CettaOwnedReturnImage *image = raw;
    if (!image)
        return;
    assert(image->references != 0u);
    if (--image->references)
        return;
    bindings_free(&image->environment);
    arena_free(&image->syntax);
    free(image);
}

CettaOwnedReturnImage *cetta_owned_return_image_from_view(
    const CettaOwnedReturn *view) {
    return (CettaOwnedReturnImage *)view;
}

size_t cetta_owned_return_image_syntax_bytes(const CettaOwnedReturnImage *image) {
    if (!image)
        return 0u;
    size_t syntax = arena_accounted_live_bytes(&image->syntax);
    return syntax > SIZE_MAX - sizeof(*image) ? SIZE_MAX : syntax + sizeof(*image);
}
