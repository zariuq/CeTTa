#define _GNU_SOURCE
#include "shared_transition.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#else
#include <sched.h>
#endif

/* Checked builds stop at the first acquisition outside the lock order, even
 * one that would not have waited. */
#if !defined(NDEBUG) || defined(CETTA_GUARD_ORDER_CHECKS)
#define CETTA_GUARD_CHECKED 1
#else
#define CETTA_GUARD_CHECKED 0
#endif

/* Guard word: a writer holds it, a writer waits (new readers stand back so
 * writers are not starved), some thread sleeps on the word, and the count
 * of readers holding it. */
#define GUARD_WRITER 0x80000000u
#define GUARD_PENDING 0x40000000u
#define GUARD_SLEEPERS 0x20000000u
#define GUARD_READERS 0x1fffffffu

/* Leaf word: free, held, held with sleepers. */
#define LEAF_FREE 0u
#define LEAF_HELD 1u
#define LEAF_CONTENDED 2u

_Thread_local unsigned cetta_shared_transition_scope_depth;
/* Key 1 is the world channel's; spaces and other channels follow it. */
CettaGuard cetta_world_guard = {.word = 0u, .key = 1u};
static _Atomic uint64_t g_guard_next_key = 2u;
static _Atomic unsigned g_scope_threads;

typedef struct {
    CettaGuard *guard;
    uint64_t key;
    CettaGuardMode mode;
} HeldGuard;

static _Thread_local HeldGuard *g_held;
static _Thread_local uint32_t g_held_len, g_held_cap;
static _Thread_local unsigned g_leaf_ranks;
static _Thread_local CettaGuardStats g_stats;
/* How often the calling thread has entered the world channel. */
static _Thread_local uint32_t g_world_depth;

static void guard_fatal(const char *what, uint64_t key, uint64_t held_key) {
    fprintf(stderr,
            "fatal: CeTTa guard order violated: %s (key %llu, held %llu)\n",
            what, (unsigned long long)key, (unsigned long long)held_key);
    abort();
}

static uint64_t guard_now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void word_wait(_Atomic uint32_t *word, uint32_t expected) {
#if defined(__linux__)
    (void)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAIT_PRIVATE, expected,
                  NULL, NULL, 0);
#else
    (void)word;
    (void)expected;
    sched_yield();
#endif
}

static void word_wake_all(_Atomic uint32_t *word) {
#if defined(__linux__)
    (void)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE_PRIVATE, INT_MAX,
                  NULL, NULL, 0);
#else
    (void)word;
#endif
}

static void word_wake_one(_Atomic uint32_t *word) {
#if defined(__linux__)
    (void)syscall(SYS_futex, (uint32_t *)word, FUTEX_WAKE_PRIVATE, 1,
                  NULL, NULL, 0);
#else
    (void)word;
#endif
}

/* ── Scopes ─────────────────────────────────────────────────────────────── */

static pthread_once_t g_stats_report_once = PTHREAD_ONCE_INIT;
static bool g_stats_report;

static void stats_report_read(void) {
    const char *value = getenv("CETTA_GUARD_STATS");
    g_stats_report = value && *value && strcmp(value, "0") != 0;
}

void cetta_shared_transition_scope_enter(void) {
    if (cetta_shared_transition_scope_depth == (unsigned)-1)
        abort();
    if (cetta_shared_transition_scope_depth++ == 0u) {
        memset(&g_stats, 0, sizeof(g_stats));
        atomic_fetch_add_explicit(&g_scope_threads, 1u, memory_order_acq_rel);
    }
}

void cetta_shared_transition_scope_leave(void) {
    if (cetta_shared_transition_scope_depth == 0u || g_held_len != 0u ||
        g_leaf_ranks != 0u || g_world_depth != 0u) {
        abort();
    }
    if (--cetta_shared_transition_scope_depth != 0u)
        return;
    atomic_fetch_sub_explicit(&g_scope_threads, 1u, memory_order_acq_rel);
    pthread_once(&g_stats_report_once, stats_report_read);
    free(g_held);
    g_held = NULL;
    g_held_cap = 0u;
}

bool cetta_shared_transition_scope_active(void) {
    return cetta_shared_transition_scope_depth != 0u;
}

bool cetta_shared_transition_any_scope_active(void) {
    return atomic_load_explicit(&g_scope_threads, memory_order_acquire) != 0u;
}

CettaGuardStats cetta_guard_stats_current_thread(void) {
    return g_stats;
}

void cetta_guard_stats_report(const CettaGuardStats *before) {
    pthread_once(&g_stats_report_once, stats_report_read);
    if (!g_stats_report || !before)
        return;
    fprintf(stderr,
            "guard-stats reads=%llu writes=%llu contended=%llu wait_ns=%llu "
            "reacquired=%llu leaf=%llu leaf_contended=%llu leaf_wait_ns=%llu "
            "world=%llu world_contended=%llu world_wait_ns=%llu\n",
            (unsigned long long)(g_stats.read_acquisitions - before->read_acquisitions),
            (unsigned long long)(g_stats.write_acquisitions - before->write_acquisitions),
            (unsigned long long)(g_stats.contended - before->contended),
            (unsigned long long)(g_stats.wait_ns - before->wait_ns),
            (unsigned long long)(g_stats.reacquired - before->reacquired),
            (unsigned long long)(g_stats.leaf_acquisitions - before->leaf_acquisitions),
            (unsigned long long)(g_stats.leaf_contended - before->leaf_contended),
            (unsigned long long)(g_stats.leaf_wait_ns - before->leaf_wait_ns),
            (unsigned long long)(g_stats.world_acquisitions - before->world_acquisitions),
            (unsigned long long)(g_stats.world_contended - before->world_contended),
            (unsigned long long)(g_stats.world_wait_ns - before->world_wait_ns));
}

/* ── Guard words ────────────────────────────────────────────────────────── */

void cetta_guard_init(CettaGuard *guard) {
    atomic_store_explicit(&guard->word, 0u, memory_order_relaxed);
    atomic_store_explicit(
        &guard->key,
        atomic_fetch_add_explicit(&g_guard_next_key, 1u, memory_order_relaxed),
        memory_order_relaxed);
}

uint64_t cetta_guard_key(CettaGuard *guard) {
    uint64_t key = atomic_load_explicit(&guard->key, memory_order_relaxed);
    if (key != 0u)
        return key;
    uint64_t fresh =
        atomic_fetch_add_explicit(&g_guard_next_key, 1u, memory_order_relaxed);
    if (atomic_compare_exchange_strong_explicit(
            &guard->key, &key, fresh, memory_order_relaxed,
            memory_order_relaxed))
        return fresh;
    return key;
}

static bool word_try_read(_Atomic uint32_t *lock) {
    uint32_t word = atomic_load_explicit(lock, memory_order_relaxed);
    while (!(word & (GUARD_WRITER | GUARD_PENDING))) {
        if ((word & GUARD_READERS) == GUARD_READERS)
            abort();
        if (atomic_compare_exchange_weak_explicit(
                lock, &word, word + 1u, memory_order_acquire,
                memory_order_relaxed))
            return true;
    }
    return false;
}

static bool word_try_write(_Atomic uint32_t *lock) {
    uint32_t word = atomic_load_explicit(lock, memory_order_relaxed);
    while (!(word & (GUARD_WRITER | GUARD_READERS))) {
        if (atomic_compare_exchange_weak_explicit(
                lock, &word, GUARD_WRITER | (word & GUARD_SLEEPERS),
                memory_order_acquire, memory_order_relaxed))
            return true;
    }
    return false;
}

static void word_lock_read_slow(_Atomic uint32_t *lock, uint64_t *contended,
                                uint64_t *wait_ns) {
    uint64_t start = guard_now_ns();
    for (;;) {
        uint32_t word = atomic_load_explicit(lock, memory_order_relaxed);
        if (!(word & (GUARD_WRITER | GUARD_PENDING))) {
            if (atomic_compare_exchange_weak_explicit(
                    lock, &word, word + 1u, memory_order_acquire,
                    memory_order_relaxed))
                break;
            continue;
        }
        if (!(word & GUARD_SLEEPERS) &&
            !atomic_compare_exchange_weak_explicit(
                lock, &word, word | GUARD_SLEEPERS,
                memory_order_relaxed, memory_order_relaxed))
            continue;
        word_wait(lock, word | GUARD_SLEEPERS);
    }
    (*contended)++;
    *wait_ns += guard_now_ns() - start;
}

static void word_lock_write_slow(_Atomic uint32_t *lock, uint64_t *contended,
                                 uint64_t *wait_ns) {
    uint64_t start = guard_now_ns();
    for (;;) {
        uint32_t word = atomic_load_explicit(lock, memory_order_relaxed);
        if (!(word & (GUARD_WRITER | GUARD_READERS))) {
            if (atomic_compare_exchange_weak_explicit(
                    lock, &word, GUARD_WRITER | (word & GUARD_SLEEPERS),
                    memory_order_acquire, memory_order_relaxed))
                break;
            continue;
        }
        uint32_t marked = word | GUARD_PENDING | GUARD_SLEEPERS;
        if (marked != word &&
            !atomic_compare_exchange_weak_explicit(
                lock, &word, marked, memory_order_relaxed,
                memory_order_relaxed))
            continue;
        word_wait(lock, marked);
    }
    (*contended)++;
    *wait_ns += guard_now_ns() - start;
}

static void word_unlock(_Atomic uint32_t *lock, CettaGuardMode mode) {
    if (mode == CETTA_GUARD_WRITE) {
        uint32_t word = atomic_exchange_explicit(lock, 0u,
                                                 memory_order_release);
        if (!(word & GUARD_WRITER))
            abort();
        if (word & GUARD_SLEEPERS)
            word_wake_all(lock);
        return;
    }
    uint32_t word = atomic_fetch_sub_explicit(lock, 1u, memory_order_release);
    if ((word & GUARD_READERS) == 0u)
        abort();
    if ((word & GUARD_READERS) == 1u && (word & GUARD_SLEEPERS)) {
        uint32_t cleared = atomic_fetch_and_explicit(
            lock, ~GUARD_SLEEPERS, memory_order_relaxed);
        if (cleared & GUARD_SLEEPERS)
            word_wake_all(lock);
    }
}

static void guard_lock(CettaGuard *guard, CettaGuardMode mode) {
    if (guard == &cetta_world_guard) {
        g_stats.world_acquisitions++;
        if (!word_try_write(&guard->word))
            word_lock_write_slow(&guard->word, &g_stats.world_contended,
                                 &g_stats.world_wait_ns);
        return;
    }
    if (mode == CETTA_GUARD_WRITE) {
        g_stats.write_acquisitions++;
        if (!word_try_write(&guard->word))
            word_lock_write_slow(&guard->word, &g_stats.contended,
                                 &g_stats.wait_ns);
    } else {
        g_stats.read_acquisitions++;
        if (!word_try_read(&guard->word))
            word_lock_read_slow(&guard->word, &g_stats.contended,
                                &g_stats.wait_ns);
    }
}

#if !CETTA_GUARD_CHECKED
static bool guard_try_lock(CettaGuard *guard, CettaGuardMode mode) {
    bool taken = mode == CETTA_GUARD_WRITE ? word_try_write(&guard->word)
                                           : word_try_read(&guard->word);
    if (taken) {
        if (mode == CETTA_GUARD_WRITE)
            g_stats.write_acquisitions++;
        else
            g_stats.read_acquisitions++;
    }
    return taken;
}
#endif

static void guard_unlock(CettaGuard *guard, CettaGuardMode mode) {
    word_unlock(&guard->word, mode);
}

/* ── The held stack ─────────────────────────────────────────────────────── */

static void held_push(CettaGuard *guard, uint64_t key, CettaGuardMode mode) {
    if (g_held_len == g_held_cap) {
        uint32_t cap = g_held_cap ? g_held_cap * 2u : 16u;
        HeldGuard *grown = realloc(g_held, (size_t)cap * sizeof(*grown));
        if (!grown) {
            fputs("fatal: CeTTa guard stack exhausted\n", stderr);
            abort();
        }
        g_held = grown;
        g_held_cap = cap;
    }
    g_held[g_held_len++] = (HeldGuard){.guard = guard, .key = key, .mode = mode};
}

static const HeldGuard *held_find(const CettaGuard *guard) {
    for (uint32_t i = 0u; i < g_held_len; i++)
        if (g_held[i].guard == guard)
            return &g_held[i];
    return NULL;
}

static uint64_t held_max_key(uint32_t from, uint32_t to) {
    uint64_t max = 0u;
    for (uint32_t i = from; i < to; i++)
        if (g_held[i].key > max)
            max = g_held[i].key;
    return max;
}

bool cetta_guard_held(const CettaGuard *guard, CettaGuardMode mode) {
    const HeldGuard *held = held_find(guard);
    return held && held->mode >= mode;
}

bool cetta_guard_any_held(void) {
    return g_held_len != 0u || g_leaf_ranks != 0u || g_world_depth != 0u;
}

void cetta_guard_section_begin(CettaGuardSection *section) {
    section->base = g_held_len;
    section->world = false;
    section->spaces = false;
    section->held = true;
}

void cetta_guard_section_end(CettaGuardSection *section) {
    if (!section->held)
        return;
    section->held = false;
    if (section->world) {
        section->world = false;
        if (g_world_depth == 0u)
            guard_fatal("world channel released more often than taken", 1u, 0u);
        if (--g_world_depth == 0u)
            word_unlock(&cetta_world_guard.word, CETTA_GUARD_WRITE);
    }
    if (!section->spaces)
        return;
    section->spaces = false;
    if (g_held_len < section->base)
        guard_fatal("sections released out of nesting order", 0u, 0u);
    while (g_held_len > section->base) {
        HeldGuard top = g_held[--g_held_len];
        guard_unlock(top.guard, top.mode);
    }
}

typedef struct {
    CettaGuard *guard;
    uint64_t key;
    CettaGuardMode mode;
} PendingGuard;

static int pending_compare(const void *left, const void *right) {
    const PendingGuard *a = left, *b = right;
    return a->key < b->key ? -1 : a->key > b->key ? 1 : 0;
}

/* Sort by key and merge repeated guards into their strongest mode. */
static size_t pending_normalize(PendingGuard *items, size_t count) {
    qsort(items, count, sizeof(*items), pending_compare);
    size_t out = 0u;
    for (size_t i = 0u; i < count; i++) {
        if (out && items[out - 1u].guard == items[i].guard) {
            if (items[i].mode > items[out - 1u].mode)
                items[out - 1u].mode = items[i].mode;
            continue;
        }
        items[out++] = items[i];
    }
    return out;
}

CettaGuardAcquire cetta_guard_section_acquire(
    CettaGuardSection *section, const CettaGuardRequest *requests,
    size_t count) {
    if (!section->held)
        cetta_guard_section_begin(section);
    PendingGuard local[16];
    PendingGuard *needed = count <= 16u ? local : malloc(count * sizeof(*needed));
    if (!needed) {
        fputs("fatal: CeTTa guard request exhausted memory\n", stderr);
        abort();
    }
    size_t need = 0u;
    bool own_upgrade = false;
    for (size_t i = 0u; i < count; i++) {
        CettaGuard *guard = requests[i].guard;
        if (!guard)
            continue;
        const HeldGuard *held = held_find(guard);
        if (held && held->mode >= requests[i].mode)
            continue;
        if (held) {
            /* A read already held is strengthened only by the section that
             * took it, by taking its set again. */
            if ((uint32_t)(held - g_held) < section->base)
                guard_fatal("write requested under an enclosing read",
                            held->key, held->key);
            own_upgrade = true;
        }
        needed[need++] = (PendingGuard){
            .guard = guard, .key = cetta_guard_key(guard),
            .mode = requests[i].mode};
    }
    if (need == 0u) {
        if (needed != local)
            free(needed);
        return CETTA_GUARD_ACQUIRED;
    }
    /* A guard already held may be re-entered under a leaf lock; a new one
     * may not be taken there. */
    if (g_leaf_ranks != 0u)
        guard_fatal("space guard taken under a leaf lock", needed[0].key,
                    (uint64_t)g_leaf_ranks);
    need = pending_normalize(needed, need);
    uint64_t outer_max = held_max_key(0u, section->base);
    uint64_t own_max = held_max_key(section->base, g_held_len);
    uint64_t least = needed[0].key;
    CettaGuardAcquire result = CETTA_GUARD_ACQUIRED;

    if (least <= outer_max) {
        /* Below a guard an enclosing section holds: never wait for it. */
#if CETTA_GUARD_CHECKED
        guard_fatal("nested acquisition below an enclosing section's guard",
                    least, outer_max);
#else
        for (size_t i = 0u; i < need; i++) {
            if (!guard_try_lock(needed[i].guard, needed[i].mode))
                guard_fatal("nested acquisition below an enclosing section's "
                            "guard would wait", needed[i].key, outer_max);
            held_push(needed[i].guard, needed[i].key, needed[i].mode);
            section->spaces = true;
        }
        if (needed != local)
            free(needed);
        return CETTA_GUARD_ACQUIRED;
#endif
    }
    if (least <= own_max || own_upgrade) {
        /* A guard discovered below one this section holds: let the
         * section's own guards go and take the whole set in order. */
        size_t own = g_held_len - section->base;
        PendingGuard *all = malloc((own + need) * sizeof(*all));
        if (!all) {
            fputs("fatal: CeTTa guard request exhausted memory\n", stderr);
            abort();
        }
        size_t total = 0u;
        for (uint32_t i = section->base; i < g_held_len; i++)
            all[total++] = (PendingGuard){
                .guard = g_held[i].guard, .key = g_held[i].key,
                .mode = g_held[i].mode};
        for (size_t i = 0u; i < need; i++)
            all[total++] = needed[i];
        while (g_held_len > section->base) {
            HeldGuard top = g_held[--g_held_len];
            guard_unlock(top.guard, top.mode);
        }
        total = pending_normalize(all, total);
        for (size_t i = 0u; i < total; i++) {
            guard_lock(all[i].guard, all[i].mode);
            held_push(all[i].guard, all[i].key, all[i].mode);
            section->spaces = true;
        }
        free(all);
        g_stats.reacquired++;
        result = CETTA_GUARD_REACQUIRED;
    } else {
        for (size_t i = 0u; i < need; i++) {
            guard_lock(needed[i].guard, needed[i].mode);
            held_push(needed[i].guard, needed[i].key, needed[i].mode);
            section->spaces = true;
        }
    }
    if (needed != local)
        free(needed);
    return result;
}

/* ── Leaf locks ─────────────────────────────────────────────────────────── */

void cetta_leaf_section_enter(CettaLeafSection *section, CettaLeafLock *lock,
                              CettaLeafRank rank) {
    section->held = false;
    if (!lock)
        return;
    unsigned bit = 1u << (unsigned)rank;
    if (g_leaf_ranks & ~(bit - 1u))
        guard_fatal("leaf lock taken at or below a held leaf rank",
                    (uint64_t)rank, (uint64_t)g_leaf_ranks);
    g_stats.leaf_acquisitions++;
    uint32_t word = LEAF_FREE;
    if (!atomic_compare_exchange_strong_explicit(
            &lock->word, &word, LEAF_HELD, memory_order_acquire,
            memory_order_relaxed)) {
        uint64_t start = guard_now_ns();
        if (word != LEAF_CONTENDED)
            word = atomic_exchange_explicit(&lock->word, LEAF_CONTENDED,
                                            memory_order_acquire);
        while (word != LEAF_FREE) {
            word_wait(&lock->word, LEAF_CONTENDED);
            word = atomic_exchange_explicit(&lock->word, LEAF_CONTENDED,
                                            memory_order_acquire);
        }
        g_stats.leaf_contended++;
        g_stats.leaf_wait_ns += guard_now_ns() - start;
    }
    g_leaf_ranks |= bit;
    section->lock = lock;
    section->rank = (uint8_t)rank;
    section->held = true;
}

void cetta_leaf_section_leave(CettaLeafSection *section) {
    if (!section->held)
        return;
    section->held = false;
    g_leaf_ranks &= ~(1u << section->rank);
    if (atomic_exchange_explicit(&section->lock->word, LEAF_FREE,
                                 memory_order_release) == LEAF_CONTENDED)
        word_wake_one(&section->lock->word);
}

void cetta_world_section_enter(CettaGuardSection *section) {
    if (!section->held)
        cetta_guard_section_begin(section);
    if (section->world)
        return;
    if (g_world_depth == 0u) {
        /* The world channel precedes every space guard. */
        if (g_held_len != 0u || g_leaf_ranks != 0u) {
#if CETTA_GUARD_CHECKED
            guard_fatal("world channel taken under a space guard or leaf lock",
                        1u, held_max_key(0u, g_held_len));
#else
            g_stats.world_acquisitions++;
            if (!word_try_write(&cetta_world_guard.word))
                guard_fatal("world channel under a space guard would wait",
                            1u, held_max_key(0u, g_held_len));
#endif
        } else {
            guard_lock(&cetta_world_guard, CETTA_GUARD_WRITE);
        }
    }
    g_world_depth++;
    section->world = true;
}

/* ── Reader-writer leaf locks ───────────────────────────────────────────── */

/* The reader-writer leaf a thread holds at each rank, and how often it has
 * entered it: a structure's own operations call one another. */
typedef struct {
    CettaLeafRwLock *lock;
    CettaGuardMode mode;
    uint32_t depth;
} HeldLeafRw;

static _Thread_local HeldLeafRw g_leaf_rw[8];

void cetta_leaf_rw_section_enter(CettaLeafRwSection *section,
                                 CettaLeafRwLock *lock, CettaGuardMode mode,
                                 CettaLeafRank rank) {
    section->held = false;
    if (!lock)
        return;
    HeldLeafRw *held = &g_leaf_rw[rank];
    if (held->lock == lock) {
        if (mode > held->mode)
            guard_fatal("leaf write requested under its own read",
                        (uint64_t)rank, (uint64_t)held->mode);
        held->depth++;
        section->lock = lock;
        section->rank = (uint8_t)rank;
        section->held = true;
        return;
    }
    unsigned bit = 1u << (unsigned)rank;
    if (g_leaf_ranks & ~(bit - 1u))
        guard_fatal("leaf lock taken at or below a held leaf rank",
                    (uint64_t)rank, (uint64_t)g_leaf_ranks);
    g_stats.leaf_acquisitions++;
    if (mode == CETTA_GUARD_WRITE) {
        if (!word_try_write(&lock->word))
            word_lock_write_slow(&lock->word, &g_stats.leaf_contended,
                                 &g_stats.leaf_wait_ns);
    } else if (!word_try_read(&lock->word)) {
        word_lock_read_slow(&lock->word, &g_stats.leaf_contended,
                            &g_stats.leaf_wait_ns);
    }
    g_leaf_ranks |= bit;
    *held = (HeldLeafRw){.lock = lock, .mode = mode, .depth = 1u};
    section->lock = lock;
    section->rank = (uint8_t)rank;
    section->held = true;
}

void cetta_leaf_rw_section_leave(CettaLeafRwSection *section) {
    if (!section->held)
        return;
    section->held = false;
    HeldLeafRw *held = &g_leaf_rw[section->rank];
    if (held->lock != section->lock || held->depth == 0u)
        guard_fatal("leaf lock released out of order", section->rank, 0u);
    if (--held->depth != 0u)
        return;
    CettaGuardMode mode = held->mode;
    held->lock = NULL;
    g_leaf_ranks &= ~(1u << section->rank);
    word_unlock(&section->lock->word, mode);
}

bool cetta_leaf_rw_held(const CettaLeafRwLock *lock, CettaGuardMode mode) {
    for (unsigned rank = 0u; rank < 8u; rank++)
        if (g_leaf_rw[rank].lock == lock && g_leaf_rw[rank].mode >= mode)
            return true;
    return false;
}
