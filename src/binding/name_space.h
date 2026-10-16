#ifndef CETTA_BINDING_NAME_SPACE_H
#define CETTA_BINDING_NAME_SPACE_H

/* Reclaimable names.
 *
 * A name is a 32-bit index that a runtime object (an arena, an activation)
 * owns for as long as it lives.  A name is handed out again only after its
 * last owner has released it, so the names taken at any moment are at most
 * the objects alive at that moment: a name space bounds what lives at once,
 * never how many objects a process may create over its lifetime.
 *
 * Each name's cell keeps a 64-bit generation that only grows, and every
 * incarnation of the name starts above the generations of the ones before.
 * A reference that does not own its name (a cache key, a weak handle) keeps
 * the generation it saw and compares it before trusting the name: a later
 * incarnation always has a larger generation.  At one increment per
 * nanosecond a 64-bit generation lasts 584 years; a name whose next
 * generation would pass 2^64 - 1 is retired, never reissued, so a generation
 * is never repeated even then.
 *
 * Owners are counted.  Whoever holds a name must own it, or hold it only
 * with its generation; that invariant is what makes reuse sound.  In
 * quarantine mode (CETTA_NAME_QUARANTINE=1 in the environment) released
 * names are never reused, so a stale name always meets a dead cell and a
 * check that looks a name up finds whatever still holds a released name.
 *
 * Shared by the arena and activation name spaces; each client keeps its own
 * CettaNameSpace and its own thread-local CettaNameLocal cache. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CETTA_NAME_CHUNK_BITS = 16,
    CETTA_NAME_CHUNK_CELLS = 1 << CETTA_NAME_CHUNK_BITS,
    CETTA_NAME_CHUNK_COUNT = 1 << (32 - CETTA_NAME_CHUNK_BITS),
    /* Names a thread keeps for its next acquisitions, so taking and
     * returning a name takes no lock. */
    CETTA_NAME_LOCAL_NAMES = 64,
};

typedef struct {
    /* The current incarnation's generation, or, while free, the least
     * generation the next incarnation may start at. */
    _Atomic uint64_t generation;
    /* The least generation the next incarnation must start above or at:
     * every release raises it to what its owner used, so the owner that
     * releases last need not be the one that used the most. */
    _Atomic uint64_t floor;
    /* Owners of the current incarnation; zero while free. */
    _Atomic uint32_t owners;
    /* One word for the client (an activation's slot cursor). */
    _Atomic uint32_t aux;
    /* The next free name while this one is on the shared free list. */
    uint32_t next_free;
    /* Flags for the client, written by the acquiring thread. */
    uint32_t client_flags;
} CettaNameCell;

typedef struct CettaNameSpace {
    const char *what;
    /* Names are handed out from [first, limit); zero is never a name. */
    uint32_t first;
    uint32_t limit;
    pthread_mutex_t mutex;
    pthread_once_t once;
    pthread_key_t local_key;
    bool quarantine;
    /* Under mutex: the next never-used name and the shared free list. */
    uint32_t next_fresh;
    uint32_t free_head;
    /* Acquisitions refused because every name was owned. */
    _Atomic uint64_t exhaustions;
    /* Releases of a name nobody owned (a client's double release). */
    _Atomic uint64_t stale_releases;
    /* Names with no generation left, never handed out again. */
    _Atomic uint64_t retired;
    _Atomic(CettaNameCell *) chunks[CETTA_NAME_CHUNK_COUNT];
} CettaNameSpace;

#define CETTA_NAME_SPACE_INITIALIZER(what_, first_, limit_)                    \
    {                                                                          \
        .what = (what_), .first = (first_), .limit = (limit_),                 \
        .mutex = PTHREAD_MUTEX_INITIALIZER, .once = PTHREAD_ONCE_INIT,         \
        .next_fresh = (first_),                                                \
    }

/* A thread's cache of free names of one space. */
typedef struct {
    CettaNameSpace *space;
    bool registered;
    uint32_t len;
    uint32_t names[CETTA_NAME_LOCAL_NAMES];
} CettaNameLocal;

static inline CettaNameCell *cetta_name_cell(CettaNameSpace *ns,
                                             uint32_t name) {
    CettaNameCell *chunk = atomic_load_explicit(
        &ns->chunks[name >> CETTA_NAME_CHUNK_BITS], memory_order_acquire);
    return chunk ? &chunk[name & (CETTA_NAME_CHUNK_CELLS - 1)] : NULL;
}

/* Return a thread's cached names to the shared list (thread exit). */
static inline void cetta_name_local_flush(CettaNameLocal *local) {
    if (!local || !local->space || local->len == 0u)
        return;
    CettaNameSpace *ns = local->space;
    pthread_mutex_lock(&ns->mutex);
    while (local->len > 0u) {
        uint32_t name = local->names[--local->len];
        cetta_name_cell(ns, name)->next_free = ns->free_head;
        ns->free_head = name;
    }
    pthread_mutex_unlock(&ns->mutex);
}

static inline void cetta_name_local_destroy(void *raw) {
    cetta_name_local_flush((CettaNameLocal *)raw);
}

/* One-time setup of a space: its thread-exit key and quarantine mode.
 * pthread_once takes no argument, so each space passes a function that
 * calls this with itself (see CETTA_NAME_SPACE_ONCE_FUNCTION). */
static inline void cetta_name_space_setup(CettaNameSpace *ns) {
    if (pthread_key_create(&ns->local_key, cetta_name_local_destroy) != 0)
        abort();
    const char *quarantine = getenv("CETTA_NAME_QUARANTINE");
    ns->quarantine = quarantine && quarantine[0] && strcmp(quarantine, "0") != 0;
}

#define CETTA_NAME_SPACE_ONCE_FUNCTION(fn, space)                              \
    static void fn(void) { cetta_name_space_setup(&(space)); }

static inline CettaNameLocal *cetta_name_local_attach(
        CettaNameSpace *ns, CettaNameLocal *local, void (*once_fn)(void)) {
    (void)pthread_once(&ns->once, once_fn);
    if (!local)
        return NULL;
    if (!local->registered) {
        local->space = ns;
        if (pthread_setspecific(ns->local_key, local) != 0)
            return NULL;
        local->registered = true;
    }
    return local;
}

/* Under ns->mutex: a name never handed out before, with its cell. */
static inline uint32_t cetta_name_fresh_locked(CettaNameSpace *ns) {
    if (ns->next_fresh >= ns->limit)
        return 0u;
    uint32_t name = ns->next_fresh;
    uint32_t chunk = name >> CETTA_NAME_CHUNK_BITS;
    if (!atomic_load_explicit(&ns->chunks[chunk], memory_order_relaxed)) {
        CettaNameCell *cells = calloc(CETTA_NAME_CHUNK_CELLS, sizeof(*cells));
        if (!cells)
            return 0u;
        atomic_store_explicit(&ns->chunks[chunk], cells, memory_order_release);
    }
    /* Generations start at one; only a last release changes one after. */
    atomic_store_explicit(&cetta_name_cell(ns, name)->generation, 1u,
                          memory_order_relaxed);
    ns->next_fresh++;
    return name;
}

/* Take a name with one owner.  Its generation is at least one and above
 * every earlier incarnation's.  False only when every name is owned,
 * retired, or held free in another thread's cache (at most
 * CETTA_NAME_LOCAL_NAMES per thread). */
static inline bool cetta_name_acquire(CettaNameSpace *ns, CettaNameLocal *local,
                                      void (*once_fn)(void), uint32_t *name_out,
                                      uint64_t *generation_out) {
    local = cetta_name_local_attach(ns, local, once_fn);
    uint32_t name = 0u;
    if (local && local->len > 0u) {
        name = local->names[--local->len];
    } else {
        pthread_mutex_lock(&ns->mutex);
        name = ns->free_head;
        if (name)
            ns->free_head = cetta_name_cell(ns, name)->next_free;
        else
            name = cetta_name_fresh_locked(ns);
        pthread_mutex_unlock(&ns->mutex);
        if (!name) {
            atomic_fetch_add_explicit(&ns->exhaustions, 1u,
                                      memory_order_relaxed);
            return false;
        }
    }
    CettaNameCell *cell = cetta_name_cell(ns, name);
    if (atomic_exchange_explicit(&cell->owners, 1u, memory_order_acq_rel) != 0u) {
        fprintf(stderr, "fatal: %s: a free name had an owner\n", ns->what);
        abort();
    }
    uint64_t generation = atomic_load_explicit(&cell->generation,
                                               memory_order_relaxed);
    if (name_out)
        *name_out = name;
    if (generation_out)
        *generation_out = generation;
    return true;
}

static inline void cetta_name_owner_overflow(CettaNameSpace *ns) {
    fprintf(stderr, "fatal: resource exhausted: %s: owners of one name\n",
            ns->what);
    abort();
}

/* Another owner of a name its caller already owns or holds alive. */
static inline void cetta_name_retain(CettaNameSpace *ns, uint32_t name) {
    CettaNameCell *cell = name ? cetta_name_cell(ns, name) : NULL;
    uint32_t owners = cell
        ? atomic_load_explicit(&cell->owners, memory_order_relaxed) : 0u;
    do {
        if (owners == 0u) {
            fprintf(stderr, "fatal: %s: retained a released name\n", ns->what);
            abort();
        }
        if (owners == UINT32_MAX)
            cetta_name_owner_overflow(ns);
    } while (!atomic_compare_exchange_weak_explicit(
        &cell->owners, &owners, owners + 1u, memory_order_relaxed,
        memory_order_relaxed));
}

static inline void cetta_name_raise_floor(CettaNameCell *cell,
                                          uint64_t at_least) {
    uint64_t floor = atomic_load_explicit(&cell->floor, memory_order_relaxed);
    while (floor < at_least &&
           !atomic_compare_exchange_weak_explicit(
               &cell->floor, &floor, at_least, memory_order_release,
               memory_order_relaxed)) {
    }
}

/* Drop one owner, recording that the next incarnation must start at
 * `next_generation` or later.  The last owner starts the next generation
 * above both this incarnation's and every owner's watermark and frees the
 * name for reuse; a name with no generation left is retired instead.  A
 * release of a name nobody owns changes nothing (counted; fatal in
 * quarantine mode). */
static inline void cetta_name_release(CettaNameSpace *ns, CettaNameLocal *local,
                                      void (*once_fn)(void), uint32_t name,
                                      uint64_t next_generation) {
    CettaNameCell *cell = name ? cetta_name_cell(ns, name) : NULL;
    if (!cell)
        return;
    if (next_generation)
        cetta_name_raise_floor(cell, next_generation);
    uint32_t owners = atomic_load_explicit(&cell->owners, memory_order_acquire);
    do {
        if (owners == 0u) {
            atomic_fetch_add_explicit(&ns->stale_releases, 1u,
                                      memory_order_relaxed);
            if (ns->quarantine) {
                fprintf(stderr, "fatal: %s: released a free name\n", ns->what);
                abort();
            }
            return;
        }
    } while (!atomic_compare_exchange_weak_explicit(
        &cell->owners, &owners, owners - 1u, memory_order_acq_rel,
        memory_order_acquire));
    if (owners != 1u)
        return;
    uint64_t generation = atomic_load_explicit(&cell->generation,
                                               memory_order_relaxed);
    uint64_t floor = atomic_load_explicit(&cell->floor, memory_order_acquire);
    if (generation == UINT64_MAX || floor == UINT64_MAX) {
        /* No generation is left above this incarnation's: retire the name. */
        atomic_store_explicit(&cell->generation, UINT64_MAX,
                              memory_order_release);
        atomic_fetch_add_explicit(&ns->retired, 1u, memory_order_relaxed);
        return;
    }
    uint64_t next = generation + 1u;
    if (floor > next)
        next = floor;
    atomic_store_explicit(&cell->generation, next, memory_order_release);
    local = cetta_name_local_attach(ns, local, once_fn);
    if (ns->quarantine)
        return;
    if (local && local->len < CETTA_NAME_LOCAL_NAMES) {
        local->names[local->len++] = name;
        return;
    }
    pthread_mutex_lock(&ns->mutex);
    cell->next_free = ns->free_head;
    ns->free_head = name;
    pthread_mutex_unlock(&ns->mutex);
}

/* Own a name held only weakly, if it is still the incarnation of
 * `generation`.  The generation changes only once the owners have reached
 * zero, so an owner count raised on a live incarnation pins it, and the
 * generation read after raising it names the pinned incarnation.  A pin on
 * a later incarnation is released through cetta_name_release: when its other
 * owners left meanwhile, that release is the last and recycles the name. */
static inline bool cetta_name_retain_weak(CettaNameSpace *ns,
                                          CettaNameLocal *local,
                                          void (*once_fn)(void), uint32_t name,
                                          uint64_t generation) {
    CettaNameCell *cell = name ? cetta_name_cell(ns, name) : NULL;
    if (!cell)
        return false;
    uint32_t owners = atomic_load_explicit(&cell->owners, memory_order_acquire);
    do {
        if (owners == 0u)
            return false;
        if (owners == UINT32_MAX)
            cetta_name_owner_overflow(ns);
    } while (!atomic_compare_exchange_weak_explicit(
        &cell->owners, &owners, owners + 1u, memory_order_acquire,
        memory_order_acquire));
#ifdef CETTA_NAME_SPACE_TEST_AFTER_WEAK_PIN
    CETTA_NAME_SPACE_TEST_AFTER_WEAK_PIN();
#endif
    if (atomic_load_explicit(&cell->generation, memory_order_acquire) ==
        generation)
        return true;
    cetta_name_release(ns, local, once_fn, name, 0u);
    return false;
}

static inline uint64_t cetta_name_generation(CettaNameSpace *ns,
                                             uint32_t name) {
    CettaNameCell *cell = name ? cetta_name_cell(ns, name) : NULL;
    return cell ? atomic_load_explicit(&cell->generation, memory_order_acquire)
                : 0u;
}

static inline bool cetta_name_is_owned(CettaNameSpace *ns, uint32_t name) {
    CettaNameCell *cell = name ? cetta_name_cell(ns, name) : NULL;
    return cell &&
        atomic_load_explicit(&cell->owners, memory_order_acquire) != 0u;
}

#endif
