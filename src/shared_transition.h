#ifndef CETTA_SHARED_TRANSITION_H
#define CETTA_SHARED_TRANSITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Coordination of concurrent evaluator branches.
 *
 * Branches (hyperpose, the parallel executor) run as threads of one process
 * inside a concurrent scope.  A step that reads spaces R and writes spaces W
 * needs one consistent cut over R and exclusive access to W, and nothing
 * about any other space: steps over disjoint spaces commute
 * (GSLT/Contracts/CoordinationFreedom.lean).  Each space therefore owns a
 * guard, and a physical section takes the guards of exactly the spaces it
 * touches:
 *
 *   - a reader-writer guard per space (CettaGuard).  Readers of one space
 *     share it; a writer excludes them.  A section over several spaces takes
 *     every guard it needs before it reads anything, so it observes them at
 *     one instant: pairwise cuts do not suffice once three spaces are read
 *     (MarkedReadCuts).
 *   - leaf locks (CettaLeafLock) for process-global structures that are
 *     monotone or keyed caches: interning, decode caches, program caches.
 *     Presence-only updates need no semantic coordination (CALM); a leaf lock
 *     only makes one physical update indivisible.
 *
 * Lock order, checked on every acquisition inside a concurrent scope:
 *   1. space guards, in ascending key order (keys are unique and never
 *      reused; a space takes its key when initialized);
 *   2. leaf locks, in ascending rank, one per rank at a time; while any leaf
 *      lock is held no space guard may be taken, and a leaf holder never
 *      calls out, evaluates, or enters a foreign runtime;
 *   3. the embedded Prolog engine and every other foreign lock are entered
 *      only while this thread holds no guard and no leaf lock
 *      (cetta_guard_any_held is false).
 *
 * A section whose guard set is found by reading (a space handle read out of
 * a space, an overlay base, module dependencies) takes the guards it knows
 * in key order, reads, then takes the guards it discovered.  When a
 * discovered key lies below one the section already holds, the section
 * releases its own guards and takes the whole set again in order; its
 * caller then revalidates what it read with the spaces' version counters
 * before using it (CETTA_GUARD_REACQUIRED).  A nested section never takes a
 * guard below one an enclosing section holds: checked builds stop at the
 * first such acquisition, and other builds stop instead of waiting when the
 * guard is not free at once.
 *
 * Outside a concurrent scope every guard and leaf lock is inert: one
 * thread-local load and a branch, never a call.
 */

/* ── Concurrent scopes ──────────────────────────────────────────────────── */

void cetta_shared_transition_scope_enter(void);
void cetta_shared_transition_scope_leave(void);
bool cetta_shared_transition_scope_active(void);
/* Whether any thread of the process is inside a concurrent scope.  Storage
 * that readers of other threads may hold (a grown table's old copy) is kept
 * until this is false. */
bool cetta_shared_transition_any_scope_active(void);

/* The calling thread's depth of concurrent-evaluation scopes.  The scoped
 * guards read it inline, so outside a scope a guard is one thread-local
 * load and a branch. */
extern _Thread_local unsigned cetta_shared_transition_scope_depth;

/* Whether this thread may share runtime objects with another, read inline by
 * every guard and leaf lock.  A build with CETTA_COORDINATION_OFF compiles
 * every bracket out: it measures what the brackets cost a single thread. */
#ifdef CETTA_COORDINATION_OFF
#define CETTA_COORDINATION_ACTIVE() 0
#else
#define CETTA_COORDINATION_ACTIVE()                                       \
    __builtin_expect(cetta_shared_transition_scope_depth != 0u, 0)
#endif

/* ── Channel guards ─────────────────────────────────────────────────────── */

typedef enum {
    CETTA_GUARD_READ = 1u,
    CETTA_GUARD_WRITE = 2u,
} CettaGuardMode;

/* A reader-writer guard over one channel's state.  All-zero is a valid
 * free guard; its key is assigned on first use when not set at
 * initialization.  A copy of a free guard is a free guard. */
typedef struct CettaGuard {
    _Atomic uint32_t word;
    _Atomic uint64_t key;
} CettaGuard;

typedef struct {
    CettaGuard *guard;
    CettaGuardMode mode;
} CettaGuardRequest;

/* The guards one bracket took, released together at its end.  Sections
 * nest; the thread's held stack is popped back to `base` on leave. */
typedef struct {
    uint32_t base;
    bool held;
    /* The section holds the world channel, which is counted per thread
     * rather than stacked: a transaction holds it from begin to commit
     * across sections that open and close in between. */
    bool world;
    /* The section pushed space guards onto the thread's held stack. */
    bool spaces;
} CettaGuardSection;

typedef enum {
    /* Every requested guard is held; nothing this section held was let go. */
    CETTA_GUARD_ACQUIRED = 0,
    /* The section let its own guards go to take a lower key in order: what
     * it read before must be revalidated against version counters. */
    CETTA_GUARD_REACQUIRED = 1,
} CettaGuardAcquire;

/* A fresh key for a guard that orders it after every guard keyed before. */
void cetta_guard_init(CettaGuard *guard);
uint64_t cetta_guard_key(CettaGuard *guard);

/* Begin a section on the calling thread (inside a concurrent scope). */
void cetta_guard_section_begin(CettaGuardSection *section);
/* Take every requested guard not already held by this thread in a mode at
 * least as strong.  Requests may repeat a guard; the strongest mode wins. */
CettaGuardAcquire cetta_guard_section_acquire(
    CettaGuardSection *section, const CettaGuardRequest *requests,
    size_t count);
void cetta_guard_section_end(CettaGuardSection *section);

/* Whether the calling thread holds `guard` in `mode` or stronger. */
bool cetta_guard_held(const CettaGuard *guard, CettaGuardMode mode);
/* Whether the calling thread holds any guard or leaf lock: the condition
 * under which no foreign runtime may be entered. */
bool cetta_guard_any_held(void);

static inline void cetta_guard_section_close(CettaGuardSection *section) {
    if (__builtin_expect(section->held, 0))
        cetta_guard_section_end(section);
}

/* The world channel: effects on nothing a space holds (the module registry,
 * files and processes, foreign runtimes, state cells) and the isolation of
 * a transaction's whole run.  Its key precedes every space's, so a section
 * takes it before any space guard.  It serializes world operations among
 * themselves and never a space section. */
extern CettaGuard cetta_world_guard;
void cetta_world_section_enter(CettaGuardSection *section);

static inline void cetta_world_section_open(CettaGuardSection *section) {
    if (CETTA_COORDINATION_ACTIVE())
        cetta_world_section_enter(section);
}

#define CETTA_SCOPED_WORLD(name)                                          \
    __attribute__((cleanup(cetta_guard_section_close)))                   \
    CettaGuardSection name = {0};                                         \
    cetta_world_section_open(&(name))

/* ── Leaf locks ─────────────────────────────────────────────────────────── */

/* Ranks order leaf locks among themselves: a thread takes a leaf only above
 * every rank it already holds. */
typedef enum {
    CETTA_LEAF_RANK_CACHE = 1u,     /* caches keyed by a space or program */
    /* a space's derived state, built by a reader under its read guard */
    CETTA_LEAF_RANK_SPACE_MAINTENANCE = 2u,
    CETTA_LEAF_RANK_REGISTRY = 3u,  /* process registries of live objects */
    CETTA_LEAF_RANK_UNIVERSE = 4u,  /* a term universe's insertions */
} CettaLeafRank;

/* All-zero is a free leaf lock. */
typedef struct {
    _Atomic uint32_t word;
} CettaLeafLock;

typedef struct {
    CettaLeafLock *lock;
    uint8_t rank;
    bool held;
} CettaLeafSection;

void cetta_leaf_section_enter(CettaLeafSection *section, CettaLeafLock *lock,
                              CettaLeafRank rank);
void cetta_leaf_section_leave(CettaLeafSection *section);

static inline void cetta_leaf_section_open(CettaLeafSection *section,
                                           CettaLeafLock *lock,
                                           CettaLeafRank rank) {
    section->held = false;
    if (CETTA_COORDINATION_ACTIVE())
        cetta_leaf_section_enter(section, lock, rank);
}

static inline void cetta_leaf_section_close(CettaLeafSection *section) {
    if (__builtin_expect(section->held, 0))
        cetta_leaf_section_leave(section);
}

/* Bracket one physical update of a process-global structure.  Release is
 * part of every normal C exit path through the cleanup attribute. */
#define CETTA_SCOPED_LEAF(name, lock, rank)                              \
    __attribute__((cleanup(cetta_leaf_section_close)))                    \
    CettaLeafSection name;                                                \
    cetta_leaf_section_open(&(name), (lock), (rank))

/* A reader-writer leaf lock for a structure whose own operations call one
 * another: the holder re-enters it at no cost, never more strongly than it
 * first took it.  All-zero is free. */
typedef struct {
    _Atomic uint32_t word;
} CettaLeafRwLock;

typedef struct {
    CettaLeafRwLock *lock;
    uint8_t rank;
    bool held;
} CettaLeafRwSection;

void cetta_leaf_rw_section_enter(CettaLeafRwSection *section,
                                 CettaLeafRwLock *lock, CettaGuardMode mode,
                                 CettaLeafRank rank);
void cetta_leaf_rw_section_leave(CettaLeafRwSection *section);
bool cetta_leaf_rw_held(const CettaLeafRwLock *lock, CettaGuardMode mode);

static inline void cetta_leaf_rw_section_open(CettaLeafRwSection *section,
                                              CettaLeafRwLock *lock,
                                              CettaGuardMode mode,
                                              CettaLeafRank rank) {
    section->held = false;
    if (CETTA_COORDINATION_ACTIVE())
        cetta_leaf_rw_section_enter(section, lock, mode, rank);
}

static inline void cetta_leaf_rw_section_close(CettaLeafRwSection *section) {
    if (__builtin_expect(section->held, 0))
        cetta_leaf_rw_section_leave(section);
}

#define CETTA_SCOPED_LEAF_RW(name, lock, mode, rank)                      \
    __attribute__((cleanup(cetta_leaf_rw_section_close)))                 \
    CettaLeafRwSection name;                                              \
    cetta_leaf_rw_section_open(&(name), (lock), (mode), (rank))

/* ── Coordination counters ──────────────────────────────────────────────── */

/* What the calling thread's acquisitions cost since it entered its
 * outermost concurrent scope.  Counted, not timed, except the waits. */
typedef struct {
    uint64_t read_acquisitions;
    uint64_t write_acquisitions;
    uint64_t contended;          /* guard acquisitions that waited */
    uint64_t wait_ns;            /* time spent waiting for guards */
    uint64_t reacquired;         /* sections that took their set again */
    uint64_t leaf_acquisitions;
    uint64_t leaf_contended;
    uint64_t leaf_wait_ns;
    /* The world channel's, counted apart from the spaces'. */
    uint64_t world_acquisitions;
    uint64_t world_contended;
    uint64_t world_wait_ns;
} CettaGuardStats;

CettaGuardStats cetta_guard_stats_current_thread(void);
/* With CETTA_GUARD_STATS set, one line on stderr: what the calling thread's
 * acquisitions cost since `before` was read (one branch's run). */
void cetta_guard_stats_report(const CettaGuardStats *before);

#endif
