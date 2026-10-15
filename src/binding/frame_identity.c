#include "frame_identity.h"
#include "atom.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    /* Generation and reference count change atomically together.  Testing a
     * generation separately from retaining would itself admit an ABA race. */
    _Atomic uint64_t state;
    _Atomic uint32_t next_slot;
    uint32_t next_free;
} FrameIdentityCell;

/* Cells live in segments allocated when their first handle is issued.  A
 * published segment is never moved or freed, so a reader holding a handle
 * reaches its cell without a lock. */
enum {
    FRAME_IDENTITY_SEGMENT_BITS = 16u,
    FRAME_IDENTITY_SEGMENT_CELLS = 1u << FRAME_IDENTITY_SEGMENT_BITS,
    FRAME_IDENTITY_SEGMENTS =
        (CETTA_FRAME_HANDLE_LIMIT + FRAME_IDENTITY_SEGMENT_CELLS - 1u) /
        FRAME_IDENTITY_SEGMENT_CELLS,
};

static FrameIdentityCell *_Atomic g_frame_identity_segments[FRAME_IDENTITY_SEGMENTS];
static pthread_mutex_t g_frame_identity_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_frame_identity_next_handle = 1u;
static uint32_t g_frame_identity_handle_limit = CETTA_FRAME_HANDLE_LIMIT;
static uint32_t g_frame_identity_free_handle;
static _Atomic uint64_t g_frame_identity_exhaustions;

/* The cell of an issued handle, or NULL when its segment was never
 * allocated (the handle was never issued). */
static FrameIdentityCell *frame_identity_cell(uint32_t handle) {
    if (handle == 0u || handle >= CETTA_FRAME_HANDLE_LIMIT)
        return NULL;
    FrameIdentityCell *segment = atomic_load_explicit(
        &g_frame_identity_segments[handle >> FRAME_IDENTITY_SEGMENT_BITS],
        memory_order_acquire);
    return segment
        ? &segment[handle & (FRAME_IDENTITY_SEGMENT_CELLS - 1u)] : NULL;
}

/* Called with the mutex held, before `handle` is issued for the first time. */
static bool frame_identity_segment_ensure(uint32_t handle) {
    _Atomic(FrameIdentityCell *) *slot =
        &g_frame_identity_segments[handle >> FRAME_IDENTITY_SEGMENT_BITS];
    if (atomic_load_explicit(slot, memory_order_relaxed))
        return true;
    FrameIdentityCell *segment =
        calloc(FRAME_IDENTITY_SEGMENT_CELLS, sizeof(*segment));
    if (!segment)
        return false;
    atomic_store_explicit(slot, segment, memory_order_release);
    return true;
}

static uint32_t frame_identity_generation_max(uint32_t handle) {
    return handle < CETTA_FRAME_CLASS1_FIRST ? CETTA_FRAME_GENERATION_MAX
         : handle < CETTA_FRAME_CLASS2_FIRST ? UINT32_C(0x3e)
         : handle < CETTA_FRAME_CLASS3_FIRST ? UINT32_C(0x6)
         : UINT32_C(0x2);
}

static CettaFrameIdentity frame_identity_spell(uint32_t handle,
                                               uint32_t generation) {
    if (handle < CETTA_FRAME_CLASS1_FIRST)
        return (generation << CETTA_FRAME_HANDLE_BITS) | handle;
    if (handle < CETTA_FRAME_CLASS2_FIRST)
        return (UINT32_C(2) << 30) | (generation << 24) |
               (handle - CETTA_FRAME_CLASS1_FIRST);
    if (handle < CETTA_FRAME_CLASS3_FIRST)
        return (UINT32_C(6) << 29) | (generation << 26) |
               (handle - CETTA_FRAME_CLASS2_FIRST);
    return (UINT32_C(14) << 28) | (generation << 26) |
           (handle - CETTA_FRAME_CLASS3_FIRST);
}

/* Free handles kept by the thread that released them, so minting and the
 * last release of an activation's identity take no lock.  A handle here has
 * no references; its next mint advances the generation exactly as a handle
 * from the shared list does.  A thread returns its handles when it exits. */
enum { FRAME_IDENTITY_LOCAL_HANDLES = 64u };

typedef struct {
    uint32_t len;
    uint32_t handles[FRAME_IDENTITY_LOCAL_HANDLES];
} FrameIdentityLocalHandles;

static _Thread_local FrameIdentityLocalHandles g_local_handles;
static _Thread_local bool g_local_handles_registered;
static pthread_key_t g_local_handles_key;
static pthread_once_t g_local_handles_once = PTHREAD_ONCE_INIT;

static void frame_identity_return_local_handles(void *raw) {
    FrameIdentityLocalHandles *local = raw;
    if (!local || local->len == 0u)
        return;
    pthread_mutex_lock(&g_frame_identity_mutex);
    while (local->len > 0u) {
        uint32_t handle = local->handles[--local->len];
        frame_identity_cell(handle)->next_free = g_frame_identity_free_handle;
        g_frame_identity_free_handle = handle;
    }
    pthread_mutex_unlock(&g_frame_identity_mutex);
}

static void frame_identity_local_key_create(void) {
    if (pthread_key_create(&g_local_handles_key,
                           frame_identity_return_local_handles) != 0)
        abort();
}

static FrameIdentityLocalHandles *frame_identity_local_handles(void) {
    if (!g_local_handles_registered) {
        pthread_once(&g_local_handles_once, frame_identity_local_key_create);
        if (pthread_setspecific(g_local_handles_key, &g_local_handles) != 0)
            return NULL;
        g_local_handles_registered = true;
    }
    return &g_local_handles;
}

bool cetta_frame_identity_acquire(CettaFrameIdentity *identity_out) {
    if (!identity_out)
        return false;
    FrameIdentityLocalHandles *local = frame_identity_local_handles();
    uint32_t handle = 0u;
    if (local && local->len > 0u) {
        handle = local->handles[--local->len];
    } else {
        pthread_mutex_lock(&g_frame_identity_mutex);
        handle = g_frame_identity_free_handle;
        if (handle) {
            g_frame_identity_free_handle =
                frame_identity_cell(handle)->next_free;
        } else if (g_frame_identity_next_handle < g_frame_identity_handle_limit &&
                   frame_identity_segment_ensure(g_frame_identity_next_handle)) {
            handle = g_frame_identity_next_handle++;
        } else {
            pthread_mutex_unlock(&g_frame_identity_mutex);
            atomic_fetch_add_explicit(&g_frame_identity_exhaustions, 1u,
                                      memory_order_relaxed);
            return false;
        }
        pthread_mutex_unlock(&g_frame_identity_mutex);
    }
    FrameIdentityCell *cell = frame_identity_cell(handle);
    uint64_t previous = atomic_load_explicit(&cell->state, memory_order_relaxed);
    uint32_t generation = (uint32_t)(previous >> 32u) + 1u;
    if ((uint32_t)previous != 0u ||
        generation > frame_identity_generation_max(handle))
        abort();
    atomic_store_explicit(&cell->next_slot, 0u, memory_order_relaxed);
    atomic_store_explicit(&cell->state,
        ((uint64_t)generation << 32u) | UINT64_C(1), memory_order_release);
    *identity_out = frame_identity_spell(handle, generation);
    return true;
}

uint64_t cetta_frame_identity_exhaustions(void) {
    return atomic_load_explicit(&g_frame_identity_exhaustions,
                                memory_order_relaxed);
}

void cetta_frame_identity_limit_handles(uint32_t count) {
    pthread_mutex_lock(&g_frame_identity_mutex);
    uint32_t limit = count >= CETTA_FRAME_HANDLE_LIMIT - 1u
        ? CETTA_FRAME_HANDLE_LIMIT : count + 1u;
    g_frame_identity_handle_limit = limit;
    pthread_mutex_unlock(&g_frame_identity_mutex);
}

/* The cell an identity names while that incarnation may still be live. */
static FrameIdentityCell *frame_identity_cell_of(CettaFrameIdentity identity,
                                                 uint32_t *generation_out) {
    CettaFrameIdentityParts parts;
    if (!cetta_frame_identity_parts(identity, &parts))
        return NULL;
    *generation_out = parts.generation;
    return frame_identity_cell(parts.handle);
}

bool cetta_frame_identity_retain(CettaFrameIdentity identity) {
    uint32_t generation = 0u;
    FrameIdentityCell *cell = frame_identity_cell_of(identity, &generation);
    if (!cell)
        return false;
    uint64_t state = atomic_load_explicit(&cell->state, memory_order_acquire);
    for (;;) {
        uint32_t references = (uint32_t)state;
        if ((uint32_t)(state >> 32u) != generation || !references)
            return false;
        if (references == UINT32_MAX)
            abort();
        if (atomic_compare_exchange_weak_explicit(&cell->state, &state, state + 1u,
                memory_order_acquire, memory_order_relaxed))
            return true;
    }
}

bool cetta_frame_identity_new_slot(CettaFrameIdentity identity,
        uint32_t inventory_size, uint32_t *slot_out) {
    if (!slot_out || !cetta_frame_identity_retain(identity))
        return false;
    uint32_t generation = 0u;
    FrameIdentityCell *cell = frame_identity_cell_of(identity, &generation);
    uint32_t current = atomic_load_explicit(&cell->next_slot, memory_order_relaxed);
    bool allocated = false;
    for (;;) {
        uint32_t previous = current < inventory_size ? inventory_size : current;
        if (previous == UINT32_MAX)
            break;
        if (atomic_compare_exchange_weak_explicit(
                &cell->next_slot, &current, previous + 1u,
                memory_order_relaxed, memory_order_relaxed)) {
            *slot_out = previous + 1u;
            allocated = true;
            break;
        }
    }
    cetta_frame_identity_release(identity);
    return allocated;
}

bool cetta_frame_identity_slot_count(CettaFrameIdentity identity,
                                    uint32_t *count_out) {
    uint32_t generation = 0u;
    FrameIdentityCell *cell = count_out
        ? frame_identity_cell_of(identity, &generation) : NULL;
    if (!cell)
        return false;
    uint64_t state = atomic_load_explicit(&cell->state, memory_order_acquire);
    if ((uint32_t)(state >> 32u) != generation || !(uint32_t)state)
        return false;
    *count_out = atomic_load_explicit(&cell->next_slot, memory_order_relaxed);
    return true;
}

void cetta_frame_identity_release(CettaFrameIdentity identity) {
    CettaFrameIdentityParts parts;
    if (!cetta_frame_identity_parts(identity, &parts))
        abort();
    FrameIdentityCell *cell = frame_identity_cell(parts.handle);
    if (!cell)
        abort();
    uint32_t generation = parts.generation;
    uint64_t state = atomic_load_explicit(&cell->state, memory_order_acquire);
    for (;;) {
        if ((uint32_t)(state >> 32u) != generation || !(uint32_t)state)
            abort();
        if (atomic_compare_exchange_weak_explicit(&cell->state, &state, state - 1u,
                memory_order_acq_rel, memory_order_relaxed))
            break;
    }
    /* A handle whose generations are spent is retired: no later identity can
     * spell an incarnation that a stale holder might still present. */
    if ((uint32_t)state != 1u || generation == parts.generation_max)
        return;
    FrameIdentityLocalHandles *local = frame_identity_local_handles();
    if (local && local->len < FRAME_IDENTITY_LOCAL_HANDLES) {
        local->handles[local->len++] = parts.handle;
        return;
    }
    pthread_mutex_lock(&g_frame_identity_mutex);
    cell->next_free = g_frame_identity_free_handle;
    g_frame_identity_free_handle = parts.handle;
    pthread_mutex_unlock(&g_frame_identity_mutex);
}

static bool frame_identity_scope_reserve(CettaFrameIdentityScope *scope) {
    if (!scope)
        return false;
    if (!scope->identities) {
        scope->identities = scope->inline_identities;
        scope->cap = sizeof(scope->inline_identities) / sizeof(scope->inline_identities[0]);
    }
    if (scope->len == scope->cap) {
        if (scope->cap > UINT32_MAX / 2u)
            return false;
        uint32_t capacity = scope->cap * 2u;
        if ((size_t)capacity > SIZE_MAX / sizeof(*scope->identities))
            return false;
        CettaFrameIdentity *grown = cetta_malloc((size_t)capacity * sizeof(*grown));
        memcpy(grown, scope->identities, (size_t)scope->len * sizeof(*grown));
        if (scope->identities != scope->inline_identities)
            free(scope->identities);
        scope->identities = grown;
        scope->cap = capacity;
    }
    return true;
}

bool cetta_frame_identity_scope_try(
        CettaFrameIdentityScope *scope, CettaFrameIdentity *identity_out) {
    if (!identity_out || !frame_identity_scope_reserve(scope))
        return false;
    CettaFrameIdentity identity;
    if (!cetta_frame_identity_acquire(&identity))
        return false;
    scope->identities[scope->len++] = identity;
    *identity_out = identity;
    return true;
}

bool cetta_frame_identity_scope_retain(
        CettaFrameIdentityScope *scope, CettaFrameIdentity identity) {
    if (!scope || !cetta_frame_identity_retain(identity))
        return false;
    if (!frame_identity_scope_reserve(scope)) {
        cetta_frame_identity_release(identity);
        return false;
    }
    scope->identities[scope->len++] = identity;
    return true;
}

CettaFrameIdentity cetta_frame_identity_scope_fresh(CettaFrameIdentityScope *scope) {
    CettaFrameIdentity identity;
    if (cetta_frame_identity_scope_try(scope, &identity))
        return identity;
    /* This entry point cannot return an identity on exhaustion. End the
     * process as a resource failure; no finalization or cleanup is claimed.
     * Recoverable callers use scope_try instead. _Exit also avoids running
     * process-wide teardown from a participating worker. */
    fputs("error: resource exhausted: contextual frame identities\n", stderr);
    (void)fflush(stderr);
    _Exit(2);
}

void cetta_frame_identity_scope_clear(CettaFrameIdentityScope *scope) {
    if (!scope)
        return;
    while (scope->len)
        cetta_frame_identity_release(scope->identities[--scope->len]);
    if (scope->identities != scope->inline_identities)
        free(scope->identities);
    memset(scope, 0, sizeof(*scope));
}
