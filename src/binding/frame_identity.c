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

static FrameIdentityCell g_frame_identities[CETTA_FRAME_HANDLE_MASK + 1u];
static pthread_mutex_t g_frame_identity_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_frame_identity_next_handle = 1u;
static uint32_t g_frame_identity_free_handle;

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
        g_frame_identities[handle].next_free = g_frame_identity_free_handle;
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
                g_frame_identities[handle].next_free;
        } else if (g_frame_identity_next_handle <= CETTA_FRAME_HANDLE_MASK) {
            handle = g_frame_identity_next_handle++;
        } else {
            pthread_mutex_unlock(&g_frame_identity_mutex);
            return false;
        }
        pthread_mutex_unlock(&g_frame_identity_mutex);
    }
    FrameIdentityCell *cell = &g_frame_identities[handle];
    uint64_t previous = atomic_load_explicit(&cell->state, memory_order_relaxed);
    uint32_t generation = (uint32_t)(previous >> 32u) + 1u;
    if ((uint32_t)previous != 0u || generation > CETTA_FRAME_GENERATION_MAX)
        abort();
    atomic_store_explicit(&cell->next_slot, 0u, memory_order_relaxed);
    atomic_store_explicit(&cell->state,
        ((uint64_t)generation << 32u) | UINT64_C(1), memory_order_release);
    *identity_out = (generation << CETTA_FRAME_HANDLE_BITS) | handle;
    return true;
}

bool cetta_frame_identity_retain(CettaFrameIdentity identity) {
    uint32_t handle = cetta_frame_handle(identity);
    uint32_t generation = cetta_frame_generation(identity);
    if (!handle || !generation || generation > CETTA_FRAME_GENERATION_MAX)
        return false;
    FrameIdentityCell *cell = &g_frame_identities[handle];
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
    FrameIdentityCell *cell = &g_frame_identities[cetta_frame_handle(identity)];
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
    uint32_t handle = cetta_frame_handle(identity);
    uint32_t generation = cetta_frame_generation(identity);
    if (!count_out || !handle || !generation ||
        generation > CETTA_FRAME_GENERATION_MAX)
        return false;
    FrameIdentityCell *cell = &g_frame_identities[handle];
    uint64_t state = atomic_load_explicit(&cell->state, memory_order_acquire);
    if ((uint32_t)(state >> 32u) != generation || !(uint32_t)state)
        return false;
    *count_out = atomic_load_explicit(&cell->next_slot, memory_order_relaxed);
    return true;
}

void cetta_frame_identity_release(CettaFrameIdentity identity) {
    uint32_t handle = cetta_frame_handle(identity);
    uint32_t generation = cetta_frame_generation(identity);
    if (!handle || !generation || generation > CETTA_FRAME_GENERATION_MAX)
        abort();
    FrameIdentityCell *cell = &g_frame_identities[handle];
    uint64_t state = atomic_load_explicit(&cell->state, memory_order_acquire);
    for (;;) {
        if ((uint32_t)(state >> 32u) != generation || !(uint32_t)state)
            abort();
        if (atomic_compare_exchange_weak_explicit(&cell->state, &state, state - 1u,
                memory_order_acq_rel, memory_order_relaxed))
            break;
    }
    if ((uint32_t)state != 1u || generation == CETTA_FRAME_GENERATION_MAX)
        return;
    FrameIdentityLocalHandles *local = frame_identity_local_handles();
    if (local && local->len < FRAME_IDENTITY_LOCAL_HANDLES) {
        local->handles[local->len++] = handle;
        return;
    }
    pthread_mutex_lock(&g_frame_identity_mutex);
    cell->next_free = g_frame_identity_free_handle;
    g_frame_identity_free_handle = handle;
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
    fputs("fatal: contextual frame identity space exhausted\n", stderr);
    abort();
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
