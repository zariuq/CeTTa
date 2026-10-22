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

/* Allocation granularity only: neither the registry nor the index has a
 * configured capacity ceiling. Slot addresses remain stable while occupied. */
#define NATIVE_HANDLE_SEGMENT_SIZE 64u

typedef struct NativeHandleSegment NativeHandleSegment;
typedef struct CettaNativeHandleSlot {
    uint64_t id;
    const char *kind;
    void *resource;
    CettaNativeHandleFreeFn free_resource;
    size_t payload_bytes;
    NativeHandleSegment *segment;
    struct CettaNativeHandleSlot *next_free;
} CettaNativeHandleSlot;

struct NativeHandleSegment {
    CettaNativeHandleRegistry *registry;
    NativeHandleSegment *previous, *next;
    NativeHandleSegment *free_previous, *free_next;
    CettaNativeHandleSlot *free_slots;
    size_t available;
    CettaNativeHandleSlot slots[NATIVE_HANDLE_SEGMENT_SIZE];
};

typedef struct { uint64_t id; CettaNativeHandleSlot *slot; } NativeHandleIndexEntry;

struct CettaNativeHandleRegistry {
    NativeHandleSegment *segments, *free_segments;
    CettaNativeHandleSlot *only_slot;
    size_t segment_count;
    NativeHandleIndexEntry *index;
    size_t index_capacity, live;
    bool closing;
};

typedef struct {
    CettaNativeHandleSlot *slot;
    size_t references;
    void *resource;
    CettaNativeHandleFreeFn free_resource;
    size_t payload_bytes;
} CettaNativeHandleOwner;

static void native_handle_release(void *ptr);

/* Enrolled slots hold a control cell. Non-enrolled modules keep their
 * context-owned contract. All destruction goes through detach below. */
static CettaNativeHandleOwner *native_handle_owner(CettaNativeHandleSlot *slot) {
    return slot && slot->free_resource == native_handle_release
        ? slot->resource : NULL;
}

typedef struct {
    void *resource;
    CettaNativeHandleFreeFn free_resource;
    CettaNativeHandleOwner *dead_owner;
} NativeHandleRetirement;

static size_t native_handle_hash(uint64_t id) {
    id ^= id >> 30;
    id *= UINT64_C(0xbf58476d1ce4e5b9);
    id ^= id >> 27;
    id *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(id ^ (id >> 31));
}

static size_t native_handle_index_at(CettaNativeHandleRegistry *registry, uint64_t id) {
    size_t i = native_handle_hash(id) & (registry->index_capacity - 1);
    while (registry->index[i].slot && registry->index[i].id != id)
        i = (i + 1) & (registry->index_capacity - 1);
    return i;
}

static bool native_handle_index_resize(CettaNativeHandleRegistry *registry, size_t capacity) {
    if (capacity > SIZE_MAX / sizeof(*registry->index)) return false;
    NativeHandleIndexEntry *index = calloc(capacity, sizeof(*index));
    if (!index) return false;
    for (size_t i = 0; i < registry->index_capacity; ++i) if (registry->index[i].slot) {
        size_t j = native_handle_hash(registry->index[i].id) & (capacity - 1);
        while (index[j].slot) j = (j + 1) & (capacity - 1);
        index[j] = registry->index[i];
    }
    free(registry->index);
    registry->index = index;
    registry->index_capacity = capacity;
    return true;
}

static bool native_handle_index_reserve(CettaNativeHandleRegistry *registry) {
    if (registry->live < registry->index_capacity / 2) return true;
    size_t capacity = registry->index_capacity ? registry->index_capacity * 2 : 16;
    return capacity >= registry->index_capacity && native_handle_index_resize(registry, capacity);
}

static void native_handle_index_trim(CettaNativeHandleRegistry *registry) {
    if (!registry->live) {
        free(registry->index);
        registry->index = NULL;
        registry->index_capacity = 0;
    } else if (registry->live <= registry->index_capacity / 8) {
        size_t capacity = registry->index_capacity;
        while (capacity > 16 && registry->live <= capacity / 8) capacity /= 2;
        if (capacity < registry->index_capacity)
            (void)native_handle_index_resize(registry, capacity);
    }
}

/* Backward-shift deletion keeps every probe chain valid without tombstones.
 * Only index entries move; holders still point at the same segmented slots. */
static void native_handle_index_remove(CettaNativeHandleRegistry *registry, uint64_t id) {
    size_t hole = native_handle_index_at(registry, id);
    assert(registry->index[hole].slot);
    size_t mask = registry->index_capacity - 1;
    for (size_t i = (hole + 1) & mask; registry->index[i].slot; i = (i + 1) & mask) {
        size_t home = native_handle_hash(registry->index[i].id) & mask;
        if (((hole - home) & mask) < ((i - home) & mask)) {
            registry->index[hole] = registry->index[i];
            hole = i;
        }
    }
    registry->index[hole] = (NativeHandleIndexEntry){0};
    --registry->live;
    registry->only_slot = NULL;
    if (registry->live == 1) {
        for (size_t i = 0; i < registry->index_capacity; ++i)
            if (registry->index[i].slot) { registry->only_slot = registry->index[i].slot; break; }
    }
}

static void native_handle_free_segment_add(CettaNativeHandleRegistry *registry, NativeHandleSegment *segment) {
    segment->free_previous = NULL;
    segment->free_next = registry->free_segments;
    if (registry->free_segments) registry->free_segments->free_previous = segment;
    registry->free_segments = segment;
}

static void native_handle_free_segment_remove(CettaNativeHandleRegistry *registry, NativeHandleSegment *segment) {
    if (segment->free_previous) segment->free_previous->free_next = segment->free_next;
    else registry->free_segments = segment->free_next;
    if (segment->free_next) segment->free_next->free_previous = segment->free_previous;
    segment->free_previous = segment->free_next = NULL;
}

static bool native_handle_segment_add(CettaNativeHandleRegistry *registry) {
    NativeHandleSegment *segment = calloc(1, sizeof(*segment));
    if (!segment) return false;
    segment->registry = registry;
    segment->next = registry->segments;
    if (registry->segments) registry->segments->previous = segment;
    registry->segments = segment;
    ++registry->segment_count;
    for (size_t i = 0; i < NATIVE_HANDLE_SEGMENT_SIZE; ++i) {
        segment->slots[i].segment = segment;
        segment->slots[i].next_free = segment->free_slots;
        segment->free_slots = &segment->slots[i];
    }
    segment->available = NATIVE_HANDLE_SEGMENT_SIZE;
    native_handle_free_segment_add(registry, segment);
    return true;
}

static CettaNativeHandleSlot *native_handle_slot(
    const CettaLibraryContext *ctx, const char *kind, uint64_t id) {
    CettaNativeHandleRegistry *registry = ctx ? ctx->native_handle_registry : NULL;
    if (!registry || !registry->index_capacity || !kind || !id) return NULL;
    /* A one-resource context needs no hash computation or probe. */
    CettaNativeHandleSlot *slot = registry->live == 1 ? registry->only_slot
        : registry->index[native_handle_index_at(registry, id)].slot;
    if (slot && slot->id != id) return NULL;
    return slot && strcmp(slot->kind, kind) == 0 ? slot : NULL;
}

/* Detach before invoking a destructor: destructors may close other handles.
 * Surviving holders must never retain a pointer into a freed context. */
static NativeHandleRetirement native_handle_detach(CettaNativeHandleSlot *slot) {
    if (!slot->resource) return (NativeHandleRetirement){0};
    NativeHandleSegment *segment = slot->segment;
    CettaNativeHandleRegistry *registry = segment->registry;
    NativeHandleRetirement retired = {
        .resource = slot->resource,
        .free_resource = slot->free_resource,
    };
    CettaNativeHandleOwner *owner = native_handle_owner(slot);
    if (owner) {
        retired.resource = owner->resource;
        retired.free_resource = owner->free_resource;
        owner->resource = NULL;
        owner->payload_bytes = 0;
        owner->slot = NULL;
        if (owner->references == 0)
            retired.dead_owner = owner;
    }
    slot->resource = NULL;
    slot->kind = NULL;
    slot->free_resource = NULL;
    native_handle_index_remove(registry, slot->id);
    slot->next_free = segment->free_slots;
    segment->free_slots = slot;
    if (!segment->available++) native_handle_free_segment_add(registry, segment);
    if (!registry->closing) {
        if (segment->available == NATIVE_HANDLE_SEGMENT_SIZE && registry->segment_count > 1) {
            native_handle_free_segment_remove(registry, segment);
            if (segment->previous) segment->previous->next = segment->next;
            else registry->segments = segment->next;
            if (segment->next) segment->next->previous = segment->previous;
            --registry->segment_count;
            free(segment);
        }
        native_handle_index_trim(registry);
    }
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

static size_t native_handle_payload_bytes(void *ptr) {
    CettaNativeHandleOwner *owner = ptr;
    pthread_mutex_lock(&native_handle_mutex);
    size_t bytes = owner->resource ? owner->payload_bytes : 0;
    pthread_mutex_unlock(&native_handle_mutex);
    return bytes;
}

#ifdef CETTA_NATIVE_HANDLE_TEST_HOOKS
static long native_handle_owner_fail_after = -1;
void cetta_native_handle_test_fail_owner_after(long n) {
    pthread_mutex_lock(&native_handle_mutex);
    native_handle_owner_fail_after = n;
    pthread_mutex_unlock(&native_handle_mutex);
}
#endif

static CettaNativeHandleOwner *native_handle_owner_alloc(void) {
#ifdef CETTA_NATIVE_HANDLE_TEST_HOOKS
    if (native_handle_owner_fail_after == 0) {
        native_handle_owner_fail_after = -1;
        return NULL;
    }
    if (native_handle_owner_fail_after > 0) --native_handle_owner_fail_after;
#endif
    return malloc(sizeof(CettaNativeHandleOwner));
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
        owner = native_handle_owner_alloc();
        if (!owner) { pthread_mutex_unlock(&native_handle_mutex); return NULL; }
        *owner = (CettaNativeHandleOwner){
            .slot = slot,
            .resource = slot->resource,
            .free_resource = slot->free_resource,
            .payload_bytes = slot->payload_bytes,
        };
        slot->resource = owner;
        slot->free_resource = native_handle_release;
    }
    /* Reserve across allocation, which registers its own arena reference. */
    if (owner->references == SIZE_MAX)
        abort();
    ++owner->references;
    bool tracked_payload = owner->payload_bytes != 0;
    pthread_mutex_unlock(&native_handle_mutex);
    Atom *identifier = atom_native_handle_identifier_tracked(
        a, (int64_t)id, owner, native_handle_retain, native_handle_release,
        tracked_payload ? native_handle_payload_bytes : NULL);
    native_handle_release(owner);
    if (!identifier) return NULL;
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
    return cetta_native_handle_alloc_sized(ctx, kind, resource, free_resource, 0, id_out);
}

bool cetta_native_handle_alloc_sized(struct CettaLibraryContext *ctx, const char *kind,
    void *resource, CettaNativeHandleFreeFn free_resource, size_t payload_bytes,
    uint64_t *id_out) {
    if (!ctx || !kind || !resource || !id_out) return false;
    pthread_mutex_lock(&native_handle_mutex);
    if (ctx->native_handle_next_id == 0) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    CettaNativeHandleRegistry *registry = ctx->native_handle_registry;
    if (!registry) {
        registry = calloc(1, sizeof(*registry));
        if (!registry) { pthread_mutex_unlock(&native_handle_mutex); return false; }
        ctx->native_handle_registry = registry;
    }
    if (registry->closing || !native_handle_index_reserve(registry) ||
        (!registry->free_segments && !native_handle_segment_add(registry))) {
        pthread_mutex_unlock(&native_handle_mutex);
        return false;
    }
    NativeHandleSegment *segment = registry->free_segments;
    CettaNativeHandleSlot *slot = segment->free_slots;
    segment->free_slots = slot->next_free;
    if (!--segment->available) native_handle_free_segment_remove(registry, segment);
    slot->next_free = NULL;
    slot->id = ctx->native_handle_next_id++;
    slot->kind = kind;
    slot->resource = resource;
    slot->free_resource = free_resource;
    slot->payload_bytes = payload_bytes;
    registry->index[native_handle_index_at(registry, slot->id)] =
        (NativeHandleIndexEntry){slot->id, slot};
    ++registry->live;
    registry->only_slot = registry->live == 1 ? slot : NULL;
    *id_out = slot->id;
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

void *cetta_native_handle_get_owned(struct CettaLibraryContext *ctx, Atom *value,
                                    const char *kind) {
    uint64_t id;
    if (!cetta_native_handle_arg(value, kind, &id)) return NULL;
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleOwner *owner = native_handle_owner(native_handle_slot(ctx, kind, id));
    void *resource = owner && atom_native_handle_identifier_matches(
        value->expr.elems[2], owner, native_handle_retain, native_handle_release)
        ? owner->resource : NULL;
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
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleRegistry *registry = ctx->native_handle_registry;
    if (!registry || registry->closing) { pthread_mutex_unlock(&native_handle_mutex); return; }
    registry->closing = true;
    pthread_mutex_unlock(&native_handle_mutex);
    for (NativeHandleSegment *segment = registry->segments; segment; segment = segment->next)
        for (size_t j = 0; j < NATIVE_HANDLE_SEGMENT_SIZE; ++j) {
            pthread_mutex_lock(&native_handle_mutex);
            NativeHandleRetirement retired = native_handle_detach(&segment->slots[j]);
            pthread_mutex_unlock(&native_handle_mutex);
            native_handle_retire(retired);
        }
    pthread_mutex_lock(&native_handle_mutex);
    assert(registry->live == 0);
    ctx->native_handle_registry = NULL;
    pthread_mutex_unlock(&native_handle_mutex);
    while (registry->segments) {
        NativeHandleSegment *segment = registry->segments;
        registry->segments = segment->next;
        free(segment);
    }
    free(registry->index);
    free(registry);
}

size_t cetta_native_handle_count(const struct CettaLibraryContext *ctx) {
    pthread_mutex_lock(&native_handle_mutex);
    size_t count = ctx && ctx->native_handle_registry ? ctx->native_handle_registry->live : 0;
    pthread_mutex_unlock(&native_handle_mutex);
    return count;
}

CettaNativeHandleStats cetta_native_handle_stats(const struct CettaLibraryContext *ctx) {
    CettaNativeHandleStats stats = {0};
    pthread_mutex_lock(&native_handle_mutex);
    CettaNativeHandleRegistry *registry = ctx ? ctx->native_handle_registry : NULL;
    if (registry) {
        stats.live = registry->live;
        stats.slot_capacity = registry->segment_count * NATIVE_HANDLE_SEGMENT_SIZE;
        stats.index_capacity = registry->index_capacity;
        stats.storage_bytes = sizeof(*registry) + registry->segment_count * sizeof(NativeHandleSegment) +
                              registry->index_capacity * sizeof(NativeHandleIndexEntry);
    }
    pthread_mutex_unlock(&native_handle_mutex);
    return stats;
}
