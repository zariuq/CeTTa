#include "seal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Object layout.  Every object starts with the atom-level hold (atom.h), so
 * atom.c can retain, release and display it without linking this file.
 * Facets live inside their brand's allocation: the facet with given rights
 * is one canonical object, so the model's facet key (brand, rights) is
 * pointer identity, and a facet's holders are its brand's holders.
 */

struct CettaSealObject {
    const CettaSealHold *hold;
    uint8_t kind;
};

typedef struct SealBrand SealBrand;

typedef struct {
    struct CettaSealObject base;
    uint8_t rights;
    SealBrand *brand;
} SealFacet;

struct SealBrand {
    atomic_size_t holders;
    uint64_t identity;
    char *nickname;
    SealFacet facets[4];
};

typedef struct {
    struct CettaSealObject base;
    atomic_size_t holders;
    uint64_t identity;
    SealBrand *brand;
    bool has_arena;
    Arena arena;          /* owns `atom` */
    Atom *atom;           /* atom payload, or the public view of a native one */
    bool native;
    CettaSealNativePayload payload;
} SealCapsule;

typedef struct {
    struct CettaSealObject base;
    atomic_size_t holders;
    uint64_t identity;
} SealKey;

/* Identities: one 64-bit counter, capped so a facet's (brand, rights)
 * mixing key stays injective.  It never wraps. */
#define SEAL_IDENTITY_LIMIT (UINT64_MAX >> 2)
static _Atomic uint64_t g_seal_next_identity = 1u;
static atomic_uint_fast64_t g_seal_live_objects = 0u;

#ifdef CETTA_TEST_HOOKS
static _Atomic uint64_t g_seal_fail_countdown = 0u;
void cetta_seal_test_set_next_identity(uint64_t next) {
    atomic_store(&g_seal_next_identity, next);
}
void cetta_seal_test_fail_allocation(uint64_t nth) {
    atomic_store(&g_seal_fail_countdown, nth);
}
uint64_t cetta_seal_test_live_objects(void) {
    return (uint64_t)atomic_load(&g_seal_live_objects);
}
#endif

static void *seal_alloc(size_t size) {
#ifdef CETTA_TEST_HOOKS
    uint64_t countdown = atomic_load(&g_seal_fail_countdown);
    if (countdown != 0u) {
        if (countdown == 1u) {
            atomic_store(&g_seal_fail_countdown, 0u);
            return NULL;
        }
        atomic_store(&g_seal_fail_countdown, countdown - 1u);
    }
#endif
    return calloc(1u, size);
}

static bool seal_next_identity(uint64_t *out) {
    uint64_t current = atomic_load(&g_seal_next_identity);
    do {
        if (current == 0u || current >= SEAL_IDENTITY_LIMIT)
            return false;
    } while (!atomic_compare_exchange_weak(&g_seal_next_identity, &current,
                                           current + 1u));
    *out = current;
    return true;
}

const char *cetta_seal_status_name(CettaSealStatus status) {
    switch (status) {
    case CETTA_SEAL_OK: return "ok";
    case CETTA_SEAL_MISS: return "miss";
    case CETTA_SEAL_WRONG_FACET: return "wrong-facet";
    case CETTA_SEAL_MISSING_RIGHT: return "missing-right";
    case CETTA_SEAL_DENIED: return "denied";
    case CETTA_SEAL_NOT_PORTABLE: return "not-portable";
    case CETTA_SEAL_NOMEM: return "no-memory";
    case CETTA_SEAL_IDENTITY_EXHAUSTED: return "identity-exhausted";
    case CETTA_SEAL_INVALID: return "invalid";
    }
    return "invalid";
}

/* ── holds ────────────────────────────────────────────────────────────── */

static void seal_holder_add(atomic_size_t *holders) {
    size_t before = atomic_fetch_add_explicit(holders, 1u,
                                              memory_order_relaxed);
    if (before == SIZE_MAX)
        abort();
}

static bool seal_holder_drop(atomic_size_t *holders) {
    size_t before = atomic_fetch_sub_explicit(holders, 1u,
                                              memory_order_acq_rel);
    if (before == 0u)
        abort();
    return before == 1u;
}

static void seal_brand_retain(SealBrand *brand) {
    seal_holder_add(&brand->holders);
}

static void seal_brand_release(SealBrand *brand) {
    if (!brand || !seal_holder_drop(&brand->holders))
        return;
    free(brand->nickname);
    free(brand);
    atomic_fetch_sub(&g_seal_live_objects, 1u);
}

static void facet_retain(void *object) {
    seal_brand_retain(((SealFacet *)object)->brand);
}

static void facet_release(void *object) {
    seal_brand_release(((SealFacet *)object)->brand);
}

static void capsule_retain(void *object) {
    seal_holder_add(&((SealCapsule *)object)->holders);
}

static void capsule_release(void *object) {
    SealCapsule *capsule = object;
    if (!capsule || !seal_holder_drop(&capsule->holders))
        return;
    /* Releasing the payload may release nested objects; nothing here reads
     * a space or state cell the payload names. */
    if (capsule->native && capsule->payload.release)
        capsule->payload.release(capsule->payload.resource);
    if (capsule->has_arena)
        arena_free(&capsule->arena);
    seal_brand_release(capsule->brand);
    free(capsule);
    atomic_fetch_sub(&g_seal_live_objects, 1u);
}

static void key_retain(void *object) {
    seal_holder_add(&((SealKey *)object)->holders);
}

static void key_release(void *object) {
    SealKey *key = object;
    if (!key || !seal_holder_drop(&key->holders))
        return;
    free(key);
    atomic_fetch_sub(&g_seal_live_objects, 1u);
}

/* ── observers ────────────────────────────────────────────────────────── */

static size_t seal_display_into(char *buffer, size_t capacity,
                                const char *head, const char *nickname) {
    int written = nickname && nickname[0]
        ? snprintf(buffer, capacity, "(%s %s)", head, nickname)
        : snprintf(buffer, capacity, "(%s)", head);
    return written < 0 ? 0u : (size_t)written;
}

static const char *facet_head(uint8_t rights) {
    switch (rights & CETTA_SEAL_RIGHTS_BOTH) {
    case CETTA_SEAL_RIGHTS_BOTH: return "Seal";
    case CETTA_SEAL_RIGHT_SEAL: return "Sealer";
    case CETTA_SEAL_RIGHT_UNSEAL: return "Unsealer";
    default: return "SealFacet";
    }
}

static size_t facet_display(const void *object, char *buffer, size_t capacity) {
    const SealFacet *facet = object;
    return seal_display_into(buffer, capacity, facet_head(facet->rights),
                             facet->brand->nickname);
}

static size_t capsule_display(const void *object, char *buffer,
                              size_t capacity) {
    const SealCapsule *capsule = object;
    return seal_display_into(buffer, capacity, "Sealed",
                             capsule->brand->nickname);
}

static size_t key_display(const void *object, char *buffer, size_t capacity) {
    (void)object;
    return seal_display_into(buffer, capacity, "Key", NULL);
}

static uint64_t facet_identity(const void *object) {
    const SealFacet *facet = object;
    return (facet->brand->identity << 2) | facet->rights;
}

static uint64_t capsule_identity(const void *object) {
    return ((const SealCapsule *)object)->identity << 2;
}

static uint64_t key_identity(const void *object) {
    return ((const SealKey *)object)->identity << 2;
}

static const char *facet_type(const void *object) {
    (void)object;
    return "SealFacet";
}

static const char *capsule_type(const void *object) {
    (void)object;
    return "Sealed";
}

static const char *key_type(const void *object) {
    (void)object;
    return "Key";
}

static const CettaSealHold g_facet_hold = {
    .retain = facet_retain, .release = facet_release,
    .display = facet_display, .identity = facet_identity,
    .type_name = facet_type,
};
static const CettaSealHold g_capsule_hold = {
    .retain = capsule_retain, .release = capsule_release,
    .display = capsule_display, .identity = capsule_identity,
    .type_name = capsule_type,
};
static const CettaSealHold g_key_hold = {
    .retain = key_retain, .release = key_release,
    .display = key_display, .identity = key_identity,
    .type_name = key_type,
};

static bool seal_is_ours(const CettaSealObject *object) {
    return object && (object->hold == &g_facet_hold ||
                      object->hold == &g_capsule_hold ||
                      object->hold == &g_key_hold);
}

static SealFacet *as_facet(CettaSealObject *object) {
    return object && object->hold == &g_facet_hold ? (SealFacet *)object
                                                   : NULL;
}

static SealCapsule *as_capsule(CettaSealObject *object) {
    return object && object->hold == &g_capsule_hold ? (SealCapsule *)object
                                                     : NULL;
}

/* ── creation ─────────────────────────────────────────────────────────── */

static char *seal_strdup(const char *text) {
    size_t length = text ? strlen(text) : 0u;
    char *copy = seal_alloc(length + 1u);
    if (copy && length)
        memcpy(copy, text, length);
    return copy;
}

CettaSealObject *cetta_seal_brand_new(const char *nickname,
                                      CettaSealStatus *status) {
    CettaSealStatus ignored;
    if (!status)
        status = &ignored;
    SealBrand *brand = seal_alloc(sizeof(*brand));
    char *name = brand ? seal_strdup(nickname) : NULL;
    if (!brand || !name) {
        free(name);
        free(brand);
        *status = CETTA_SEAL_NOMEM;
        return NULL;
    }
    uint64_t identity = 0;
    if (!seal_next_identity(&identity)) {
        free(name);
        free(brand);
        *status = CETTA_SEAL_IDENTITY_EXHAUSTED;
        return NULL;
    }
    atomic_init(&brand->holders, 1u);
    brand->identity = identity;
    brand->nickname = name;
    for (uint8_t rights = 0; rights < 4u; ++rights) {
        brand->facets[rights].base.hold = &g_facet_hold;
        brand->facets[rights].base.kind = CETTA_SEAL_OBJECT_FACET;
        brand->facets[rights].rights = rights;
        brand->facets[rights].brand = brand;
    }
    atomic_fetch_add(&g_seal_live_objects, 1u);
    *status = CETTA_SEAL_OK;
    return &brand->facets[CETTA_SEAL_RIGHTS_BOTH].base;
}

CettaSealObject *cetta_seal_brand_get(CettaSealBrandCell *cell,
                                      CettaSealStatus *status) {
    CettaSealStatus ignored;
    if (!status) status = &ignored;
    *status = CETTA_SEAL_OK;
    if (!cell || !cell->nickname) { *status = CETTA_SEAL_INVALID; return NULL; }
    CettaSealObject *brand = atomic_load_explicit(&cell->current, memory_order_acquire);
    if (brand) return brand;
    if (pthread_mutex_lock(&cell->gate)) { *status = CETTA_SEAL_INVALID; return NULL; }
    brand = atomic_load_explicit(&cell->current, memory_order_relaxed);
    if (!brand) {
        brand = cetta_seal_brand_new(cell->nickname, status);
        if (brand) atomic_store_explicit(&cell->current, brand, memory_order_release);
    }
    pthread_mutex_unlock(&cell->gate);
    return brand;
}

CettaSealObject *cetta_seal_attenuate(CettaSealObject *object,
                                      uint32_t wanted) {
    SealFacet *facet = as_facet(object);
    if (!facet)
        return NULL;
    uint8_t rights = (uint8_t)(facet->rights & wanted & CETTA_SEAL_RIGHTS_BOTH);
    seal_brand_retain(facet->brand);
    return &facet->brand->facets[rights].base;
}

CettaSealObject *cetta_seal_key_new(CettaSealStatus *status) {
    CettaSealStatus ignored;
    if (!status)
        status = &ignored;
    SealKey *key = seal_alloc(sizeof(*key));
    if (!key) {
        *status = CETTA_SEAL_NOMEM;
        return NULL;
    }
    uint64_t identity = 0;
    if (!seal_next_identity(&identity)) {
        free(key);
        *status = CETTA_SEAL_IDENTITY_EXHAUSTED;
        return NULL;
    }
    key->base.hold = &g_key_hold;
    key->base.kind = CETTA_SEAL_OBJECT_KEY;
    atomic_init(&key->holders, 1u);
    key->identity = identity;
    atomic_fetch_add(&g_seal_live_objects, 1u);
    *status = CETTA_SEAL_OK;
    return &key->base;
}

void cetta_seal_retain(CettaSealObject *object) {
    if (seal_is_ours(object))
        object->hold->retain(object);
}

void cetta_seal_release(CettaSealObject *object) {
    if (seal_is_ours(object))
        object->hold->release(object);
}

/* The seal right is checked before anything is allocated. */
static CettaSealStatus seal_facet_for_sealing(CettaSealObject *object,
                                              SealFacet **out) {
    SealFacet *facet = as_facet(object);
    if (!facet)
        return CETTA_SEAL_WRONG_FACET;
    if ((facet->rights & CETTA_SEAL_RIGHT_SEAL) == 0u)
        return CETTA_SEAL_MISSING_RIGHT;
    *out = facet;
    return CETTA_SEAL_OK;
}

static CettaSealStatus capsule_new(SealFacet *facet, SealCapsule **out) {
    SealCapsule *capsule = seal_alloc(sizeof(*capsule));
    if (!capsule)
        return CETTA_SEAL_NOMEM;
    uint64_t identity = 0;
    if (!seal_next_identity(&identity)) {
        free(capsule);
        return CETTA_SEAL_IDENTITY_EXHAUSTED;
    }
    capsule->base.hold = &g_capsule_hold;
    capsule->base.kind = CETTA_SEAL_OBJECT_CAPSULE;
    atomic_init(&capsule->holders, 1u);
    capsule->identity = identity;
    capsule->brand = facet->brand;
    seal_brand_retain(facet->brand);
    atomic_fetch_add(&g_seal_live_objects, 1u);
    *out = capsule;
    return CETTA_SEAL_OK;
}

static bool capsule_copy_atom(SealCapsule *capsule, Atom *atom) {
    arena_init_detached(&capsule->arena);
    capsule->has_arena = true;
    capsule->atom = atom_deep_copy(&capsule->arena, atom);
    return capsule->atom != NULL;
}

CettaSealStatus cetta_seal_atom(CettaSealObject *object, Atom *payload,
                                CettaSealObject **out) {
    SealFacet *facet = NULL;
    if (!out || !payload)
        return CETTA_SEAL_INVALID;
    *out = NULL;
    CettaSealStatus status = seal_facet_for_sealing(object, &facet);
    if (status != CETTA_SEAL_OK)
        return status;
    SealCapsule *capsule = NULL;
    status = capsule_new(facet, &capsule);
    if (status != CETTA_SEAL_OK)
        return status;
    if (!capsule_copy_atom(capsule, payload)) {
        capsule_release(capsule);
        return CETTA_SEAL_NOMEM;
    }
    *out = &capsule->base;
    return CETTA_SEAL_OK;
}

CettaSealStatus cetta_seal_native_with_view(
    CettaSealObject *object, const CettaSealNativePayload *payload,
    Atom *view, CettaSealObject **out) {
    SealFacet *facet = NULL;
    if (!out || !payload || !payload->kind)
        return CETTA_SEAL_INVALID;
    *out = NULL;
    CettaSealStatus status = seal_facet_for_sealing(object, &facet);
    if (status != CETTA_SEAL_OK)
        return status;
    SealCapsule *capsule = NULL;
    status = capsule_new(facet, &capsule);
    if (status != CETTA_SEAL_OK)
        return status;
    if (view && !capsule_copy_atom(capsule, view)) {
        capsule_release(capsule);
        return CETTA_SEAL_NOMEM;
    }
    /* The payload is retained only once the capsule exists, so a failure
     * above leaves the caller's resource untouched. */
    capsule->native = true;
    capsule->payload = *payload;
    if (payload->retain)
        payload->retain(payload->resource);
    *out = &capsule->base;
    return CETTA_SEAL_OK;
}

CettaSealStatus cetta_seal_native(CettaSealObject *facet,
                                  const CettaSealNativePayload *payload,
                                  CettaSealObject **out) {
    return cetta_seal_native_with_view(facet, payload, NULL, out);
}

/* ── opening ──────────────────────────────────────────────────────────── */

static CettaSealStatus seal_open(CettaSealObject *object,
                                 CettaSealObject *box, SealCapsule **out) {
    SealFacet *facet = as_facet(object);
    if (!facet)
        return CETTA_SEAL_WRONG_FACET;
    if ((facet->rights & CETTA_SEAL_RIGHT_UNSEAL) == 0u)
        return CETTA_SEAL_MISSING_RIGHT;
    SealCapsule *capsule = as_capsule(box);
    if (!capsule || capsule->brand != facet->brand)
        return CETTA_SEAL_MISS;
    *out = capsule;
    return CETTA_SEAL_OK;
}

CettaSealStatus cetta_seal_unseal_atom(CettaSealObject *facet,
                                       CettaSealObject *box, Arena *dst,
                                       Atom **out) {
    SealCapsule *capsule = NULL;
    if (!out || !dst)
        return CETTA_SEAL_INVALID;
    *out = NULL;
    CettaSealStatus status = seal_open(facet, box, &capsule);
    if (status != CETTA_SEAL_OK)
        return status;
    if (capsule->native)
        return CETTA_SEAL_MISS;
    /* The retained payload, unchanged: its variables keep their identities
     * and nothing is evaluated.  The copy gives the caller's arena its own
     * holds on whatever the payload retains. */
    *out = atom_deep_copy(dst, capsule->atom);
    return *out ? CETTA_SEAL_OK : CETTA_SEAL_NOMEM;
}

CettaSealStatus cetta_seal_unseal_native(CettaSealObject *facet,
                                         CettaSealObject *box,
                                         const void *kind,
                                         void **resource_out) {
    SealCapsule *capsule = NULL;
    if (!resource_out)
        return CETTA_SEAL_INVALID;
    *resource_out = NULL;
    CettaSealStatus status = seal_open(facet, box, &capsule);
    if (status != CETTA_SEAL_OK)
        return status;
    if (!capsule->native || capsule->payload.kind != kind)
        return CETTA_SEAL_MISS;
    *resource_out = capsule->payload.resource;
    return CETTA_SEAL_OK;
}

CettaSealStatus cetta_seal_unseal_view(CettaSealObject *facet,
                                       CettaSealObject *box,
                                       Atom **view_out) {
    SealCapsule *capsule = NULL;
    if (!view_out)
        return CETTA_SEAL_INVALID;
    *view_out = NULL;
    CettaSealStatus status = seal_open(facet, box, &capsule);
    if (status != CETTA_SEAL_OK)
        return status;
    if (!capsule->native || !capsule->atom)
        return CETTA_SEAL_MISS;
    *view_out = capsule->atom;
    return CETTA_SEAL_OK;
}

/* ── observations ─────────────────────────────────────────────────────── */

CettaSealObjectKind cetta_seal_object_kind(const CettaSealObject *object) {
    return seal_is_ours(object) ? (CettaSealObjectKind)object->kind
                                : CETTA_SEAL_OBJECT_NONE;
}

uint32_t cetta_seal_facet_rights(const CettaSealObject *object) {
    return object && object->hold == &g_facet_hold
        ? ((const SealFacet *)object)->rights : CETTA_SEAL_RIGHTS_NONE;
}

static const SealBrand *seal_brand_of(const CettaSealObject *object) {
    if (!object)
        return NULL;
    if (object->hold == &g_facet_hold)
        return ((const SealFacet *)object)->brand;
    if (object->hold == &g_capsule_hold)
        return ((const SealCapsule *)object)->brand;
    return NULL;
}

bool cetta_seal_same_brand(const CettaSealObject *left,
                           const CettaSealObject *right) {
    const SealBrand *a = seal_brand_of(left);
    return a && a == seal_brand_of(right);
}

const char *cetta_seal_nickname(const CettaSealObject *object) {
    const SealBrand *brand = seal_brand_of(object);
    return brand ? brand->nickname : NULL;
}

Atom *cetta_seal_object_atom(Arena *arena, CettaSealObject *object) {
    return seal_is_ours(object) ? atom_seal(arena, object) : NULL;
}

CettaSealObject *cetta_seal_atom_object(const Atom *atom) {
    CettaSealObject *object = atom_seal_object(atom);
    return seal_is_ours(object) ? object : NULL;
}

/* A portable datum names no runtime identity: no authority object, space,
 * state cell, capture, saved bindings, foreign record or native handle.
 * This is NativeContract's admitDatum for the host's atom codec. */
static bool seal_atom_not_portable(const Atom *atom, void *context) {
    (void)context;
    if (!atom)
        return true;
    if (atom->kind == ATOM_GROUNDED)
        switch (atom->ground.gkind) {
        case GV_INT: case GV_FLOAT: case GV_BOOL: case GV_STRING:
        case GV_BIGINT: case GV_RATIONAL:
            return false;
        case GV_INTERNAL_TAG:
            return !cetta_internal_tag_is_term_stable(atom->ground.ival);
        default:
            return true;
        }
    if (atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
        atom_is_symbol(atom->expr.elems[0], "NativeHandle"))
        return true;
    return false;
}

CettaSealStatus cetta_seal_check_portable(const Atom *atom) {
    if (!atom)
        return CETTA_SEAL_INVALID;
    bool complete = false;
    bool rejected = atom_graph_any_checked(atom, seal_atom_not_portable,
        NULL, NULL, SIZE_MAX, &complete);
    return !complete ? CETTA_SEAL_NOMEM
        : rejected ? CETTA_SEAL_NOT_PORTABLE : CETTA_SEAL_OK;
}
