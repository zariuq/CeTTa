#define _GNU_SOURCE
#include "atom.h"
#include "string_literal.h"
#include "lang.h"
#include "petta_numeric.h"
#include "stats.h"
#include "binding/name_space.h"
#include "generated/cetta_execution_contracts.generated.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

/* The rational-term module (term_graph.c).  A link unit without it builds
 * no graph, so no GV_TERM_GRAPH atom reaches its code. */
const CettaTermGraphRef *term_graph_ref(const CettaTermGraph *graph,
                                        uint32_t node) __attribute__((weak));
void term_graph_retain(void *graph) __attribute__((weak));
void term_graph_release(void *graph) __attribute__((weak));
bool term_graph_infinite(const CettaTermGraph *graph, uint32_t node)
    __attribute__((weak));
bool term_graph_value_eq(Atom *left, Atom *right,
                         bool (*leaf_eq)(Atom *, Atom *)) __attribute__((weak));
uint32_t term_graph_view_hash(Atom *atom,
                              uint32_t (*leaf_hash)(Atom *))
    __attribute__((weak));
void term_graph_print(const CettaTermGraphRef *ref, FILE *out,
                      bool petta) __attribute__((weak));
Atom *term_graph_open_value(Arena *arena, Atom *atom) __attribute__((weak));

#define CETTA_ATOM_DEEP_COPY_MEMO_INLINE_CAP 64u

#ifndef CETTA_STRUCTURAL_NAME_TRANSPORT_MUTATION
#define CETTA_STRUCTURAL_NAME_TRANSPORT_MUTATION 0
#endif

#ifndef CETTA_PRIME_CONTEXT_MUTATION
#define CETTA_PRIME_CONTEXT_MUTATION 0
#endif

struct CettaBigInt {
    /* Arithmetic values stay in GMP limb form.  Decimal text is an output/
     * hashing cache, materialized at most once instead of on every numeric
     * operation. */
    _Atomic(char *) text;
#if CETTA_BUILD_WITH_GMP
    mpz_t value;
    bool has_value;
#endif
};

struct CettaRational {
    const char *text;
#if CETTA_BUILD_WITH_GMP
    mpq_t value;
    bool has_value;
#endif
};

struct ArenaFinalizer {
    void (*fn)(void *);
    void *ptr;
    struct ArenaFinalizer *next;
};

/* One hold per distinct (owner, release) pair.  The holds are finalizers on
 * the arena's chain; `holds` lists them in acquisition order and `slots`
 * indexes them, with the same discipline as the frame-identity set below: a
 * reset pops holds newest first, so the popped hold is always the last record
 * and survivors' probe chains never pass through it. */
struct ArenaRetainedOwnerSet {
    ArenaFinalizer **holds;   /* acquisition order                    */
    uint32_t *slots;          /* index slot -> record position + 1    */
    uint32_t *index;          /* record position -> its index slot    */
    uint32_t len;
    uint32_t cap;
    uint32_t slot_cap;        /* power of two, or zero                */
    uint32_t slot_mask;
};

typedef struct {
    Atom atom;
    void *owner;
    void (*retain)(void *);
    void (*release)(void *);
    size_t (*footprint)(void *);
} NativeHandleIdentifier;

/* Large byte values travel through many publication arenas. Retain their
 * immutable storage instead of copying every byte at each boundary. Small
 * strings remain inline arena allocations; neither form changes Atom's ABI. */
typedef struct {
    _Atomic(size_t) references;
    size_t length;
    char bytes[];
} AtomStringStorage;

typedef struct {
    Atom atom;
    AtomStringStorage *storage;
} RetainedStringAtom;

enum { ATOM_STRING_RETAINED_MIN_BYTES = 4096u };

static bool arena_retained_owner_set_find(const ArenaRetainedOwnerSet *set,
                                          const void *owner,
                                          void (*release)(void *));

static void atom_string_storage_retain(void *ptr) {
    AtomStringStorage *storage = ptr;
    size_t before = atomic_fetch_add_explicit(&storage->references, 1u,
                                              memory_order_relaxed);
    if (before == SIZE_MAX)
        abort();
}

static void atom_string_storage_release(void *ptr) {
    AtomStringStorage *storage = ptr;
    if (atomic_fetch_sub_explicit(&storage->references, 1u,
                                  memory_order_acq_rel) == 1u)
        free(storage);
}

static Atom *atom_string_retain_storage(Arena *arena, const Atom *value,
                                        AtomStringStorage *storage) {
    bool held = arena_retained_owner_set_find(arena->retained_owners, storage,
                                              atom_string_storage_release);
    if (!arena_retain_owner(arena, storage, atom_string_storage_retain,
                            atom_string_storage_release))
        return NULL;
    /* Count each reachable buffer once in this arena's allocation pressure,
     * including a restored survivor. Sharing across arenas is conservative
     * pressure accounting, not a claim of unique physical allocation. */
    if (!held)
        arena_account_external_bytes(arena, sizeof(*storage) + storage->length + 1u);
    RetainedStringAtom *copy = arena_alloc(arena, sizeof(*copy));
    atom_header_copy(&copy->atom, value);
    copy->atom.arena_id = arena->identity;
    copy->atom.name_key = NULL;
    copy->atom.structural_facts |= ATOM_STRUCTURAL_STRING_STORAGE;
    copy->atom.ground.sval = storage->bytes;
    copy->storage = storage;
    return &copy->atom;
}

static CettaBigInt *cetta_bigint_clone_owned(const CettaBigInt *src);
static void cetta_bigint_free_owned(CettaBigInt *big);
static CettaRational *cetta_rational_clone_owned(const CettaRational *src);
static void cetta_rational_free_owned(CettaRational *rat);
static Atom *atom_prime_context_deep_copy(
    Arena *dst, const CettaPrimeContext *context);
static _Atomic(uint64_t) g_hashcons_reclamation_epoch = 1u;

typedef struct {
    const Atom *src;
    Atom *dst;
} AtomDeepCopyMemoSlot;

typedef struct {
    Atom **source_end;
    Atom *longest;
    Atom *recent;
} AtomDeepCopyBufferSlot;

/* The storage a session copies out of that dies when the session ends: for
 * each dying arena, its atoms outside the kept ranges, which are the storage
 * that arena keeps below the caller's mark.  A copied atom of a dying arena
 * records its copy in place (ATOM_ARENA_ID_FORWARDED, and its name key the
 * copy) rather than in the session's table.  Both lists grow as needed: any
 * number of dying arenas, and of blocks kept below a mark. */
typedef struct {
    uint32_t identity;
    uint32_t kept_len;
    uintptr_t *kept;          /* kept_len pairs: start, end */
} AtomDeepCopyDyingArena;

typedef struct {
    uint32_t len;
    uint32_t cap;
    AtomDeepCopyDyingArena *arenas;
} AtomDeepCopyRegion;

/* The arena id of an atom whose copy it records; never an arena's. */
#define ATOM_ARENA_ID_FORWARDED UINT32_MAX
/* The identity of a freed arena struct: never an atom's.  Allocating from
 * it is fatal until arena_init opens it again. */
#define ARENA_ID_DEAD (UINT32_MAX - 1u)

/* Arena identities are reclaimable names (binding/name_space.h): an arena
 * owns its name from arena_init to arena_free, and a younger arena linked
 * to it as its older generation owns it too, so the name is reused only
 * once neither can name the arena's atoms.  An identity's generation is the
 * arena's reset epoch, which continues across incarnations of the name, so
 * an (identity, reset epoch) pair never repeats. */
static CettaNameSpace g_arena_names =
    CETTA_NAME_SPACE_INITIALIZER("arena identities", 1u, ARENA_ID_DEAD);
static _Thread_local CettaNameLocal g_arena_name_local;
CETTA_NAME_SPACE_ONCE_FUNCTION(arena_names_once, g_arena_names)

typedef struct {
    AtomDeepCopyMemoSlot inline_slots[CETTA_ATOM_DEEP_COPY_MEMO_INLINE_CAP];
    AtomDeepCopyMemoSlot *slots;
    size_t cap;
    size_t used;
    AtomDeepCopyBufferSlot *buffers;
    size_t buffer_cap;
    size_t buffer_used;
    /* The dying region whose atoms record their copies in place; NULL when
     * every copy is recorded here. */
    const AtomDeepCopyRegion *region;
    /* Whether copies drop HE's evaluated marks (a space storing syntax). */
    bool strip_he_marks;
    const uint32_t *loan_arenas;
    size_t loan_arena_count;
} AtomDeepCopyMemo;

static _Thread_local unsigned g_atom_deep_copy_strip_he_marks;

void atom_deep_copy_strip_he_marks_enter(void) {
    g_atom_deep_copy_strip_he_marks++;
}

void atom_deep_copy_strip_he_marks_leave(void) {
    if (g_atom_deep_copy_strip_he_marks)
        g_atom_deep_copy_strip_he_marks--;
}

/* Whether copying `source` keeps HE's evaluated mark: the copy of a value
 * is the same value, built unshared (never a list buffer view). */
static inline bool atom_deep_copy_keeps_he_mark(const AtomDeepCopyMemo *memo,
                                                const Atom *source) {
    return !memo->strip_he_marks &&
           (source->flags & ATOM_FLAG_HE_EVALUATED) != 0u;
}

/* Whether a copy may share `source` (a settled subtree or a list buffer)
 * rather than rebuild it: a copy that drops evaluated marks rebuilds what
 * may hold one. */
static inline bool atom_deep_copy_may_share(const AtomDeepCopyMemo *memo,
                                            const Atom *source) {
    return !atom_deep_copy_keeps_he_mark(memo, source) &&
           !(memo->strip_he_marks && atom_may_hold_he_mark(source));
}

/* A temporary loan deliberately does not use the older-generation ownership
 * test: a later ordinary publication must still materialize this graph. */
static __attribute__((noinline, cold)) bool atom_deep_copy_may_loan(
        const AtomDeepCopyMemo *memo, const Atom *source) {
    if (!memo->loan_arena_count || !source->arena_id ||
        !(source->flags & ATOM_FLAG_ARENA_CLOSED) ||
        (source->flags & (ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                          ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE)) ||
        !atom_deep_copy_may_share(memo, source))
        return false;
    for (size_t index = 0u; index < memo->loan_arena_count; index++)
        if (source->arena_id == memo->loan_arenas[index])
            return true;
    return false;
}

typedef struct AtomDeepCopyInventory {
    const void *kind;
    void *value;
    AtomDeepCopyInventoryRelease release;
    struct AtomDeepCopyInventory *next;
} AtomDeepCopyInventory;

struct AtomDeepCopySession {
    Arena *dst;
    AtomDeepCopyMemo memo;
    AtomDeepCopyResolver resolver;
    void *resolver_context;
    AtomDeepCopyLeafMap map;
    void *map_context;
    bool failed;
    bool started;
    AtomDeepCopyRegion region;
    AtomDeepCopyInventory *inventories;
};

bool atom_deep_copy_session_retain_frame(
        AtomDeepCopySession *session, CettaFrameIdentity identity) {
    return session && (cetta_frame_identity_is_none(identity) ||
        arena_retain_frame_identity(session->dst, identity));
}

/* ── Arena ──────────────────────────────────────────────────────────────── */

_Noreturn void cetta_oom(size_t size) {
    fprintf(stderr, "fatal: out of memory allocating %zu bytes\n", size);
    abort();
}

static bool arena_runtime_counter_lane(CettaArenaRuntimeKind kind,
                                       CettaRuntimeCounter *alloc_counter,
                                       CettaRuntimeCounter *live_peak_counter,
                                       CettaRuntimeCounter *reserved_peak_counter) {
    switch (kind) {
    case CETTA_ARENA_RUNTIME_KIND_PERSISTENT:
        if (alloc_counter)
            *alloc_counter = CETTA_RUNTIME_COUNTER_PERSISTENT_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter = CETTA_RUNTIME_COUNTER_PERSISTENT_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter = CETTA_RUNTIME_COUNTER_PERSISTENT_ARENA_RESERVED_BYTES_PEAK;
        return true;
    case CETTA_ARENA_RUNTIME_KIND_EVAL:
        if (alloc_counter)
            *alloc_counter = CETTA_RUNTIME_COUNTER_EVAL_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter = CETTA_RUNTIME_COUNTER_EVAL_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter = CETTA_RUNTIME_COUNTER_EVAL_ARENA_RESERVED_BYTES_PEAK;
        return true;
    case CETTA_ARENA_RUNTIME_KIND_SCRATCH:
        if (alloc_counter)
            *alloc_counter = CETTA_RUNTIME_COUNTER_SCRATCH_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter = CETTA_RUNTIME_COUNTER_SCRATCH_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter = CETTA_RUNTIME_COUNTER_SCRATCH_ARENA_RESERVED_BYTES_PEAK;
        return true;
    case CETTA_ARENA_RUNTIME_KIND_SURVIVOR:
        if (alloc_counter)
            *alloc_counter =
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_ALLOC_BYTES;
        if (live_peak_counter)
            *live_peak_counter =
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_LIVE_BYTES_PEAK;
        if (reserved_peak_counter)
            *reserved_peak_counter =
                CETTA_RUNTIME_COUNTER_QUERY_EPISODE_SURVIVOR_ARENA_RESERVED_BYTES_PEAK;
        return true;
    default:
        return false;
    }
}

static void arena_runtime_note_usage(const Arena *a) {
    CettaRuntimeCounter live_peak_counter;
    CettaRuntimeCounter reserved_peak_counter;

    if (!a ||
        !arena_runtime_counter_lane(a->runtime_kind, NULL, &live_peak_counter,
                                    &reserved_peak_counter))
        return;
    cetta_runtime_stats_update_max(
        live_peak_counter, (uint64_t)arena_accounted_live_bytes(a));
    cetta_runtime_stats_update_max(reserved_peak_counter,
                                   (uint64_t)a->reserved_bytes);
}

static void arena_runtime_note_spare(const Arena *a) {
    if (!a)
        return;
    cetta_runtime_stats_update_max(CETTA_RUNTIME_COUNTER_ARENA_SPARE_BLOCKS_PEAK,
                                   (uint64_t)a->spare_block_count);
    cetta_runtime_stats_update_max(CETTA_RUNTIME_COUNTER_ARENA_SPARE_BYTES_PEAK,
                                   (uint64_t)a->spare_bytes);
}

static void arena_runtime_note_alloc(const Arena *a, size_t size) {
    CettaRuntimeCounter alloc_counter;
    CettaRuntimeCounter live_peak_counter;
    CettaRuntimeCounter reserved_peak_counter;

    if (!a ||
        !arena_runtime_counter_lane(a->runtime_kind, &alloc_counter,
                                    &live_peak_counter,
                                    &reserved_peak_counter))
        return;
    if (a->runtime_kind == CETTA_ARENA_RUNTIME_KIND_SURVIVOR) {
        /* Record the aggregate and its unique semantic role under the same
         * lock.  This is the executable conservation boundary: every
         * survivor byte belongs to exactly one role, including OTHER. */
        cetta_runtime_stats_note_survivor_allocation((uint64_t)size);
    } else {
        cetta_runtime_stats_add(alloc_counter, (uint64_t)size);
    }
    cetta_runtime_stats_update_max(
        live_peak_counter, (uint64_t)arena_accounted_live_bytes(a));
    cetta_runtime_stats_update_max(reserved_peak_counter,
                                   (uint64_t)a->reserved_bytes);
}

/* ── Resource ledger (runtime statistics only) ─────────────────────────
 * An arena created while statistics are enabled is charged, under its
 * ledger tag, one live arena and the bytes of every block it holds (spare
 * blocks included), until it is freed.  Each charge is returned by the
 * arena that made it, so the gauges return to zero with the arenas. */

static CettaArenaLedgerTag arena_ledger_tag_for_kind(
        CettaArenaRuntimeKind kind) {
    switch (kind) {
    case CETTA_ARENA_RUNTIME_KIND_PERSISTENT: return CETTA_ARENA_LEDGER_PERSISTENT;
    case CETTA_ARENA_RUNTIME_KIND_EVAL: return CETTA_ARENA_LEDGER_EVAL;
    case CETTA_ARENA_RUNTIME_KIND_SCRATCH: return CETTA_ARENA_LEDGER_SCRATCH;
    case CETTA_ARENA_RUNTIME_KIND_SURVIVOR: return CETTA_ARENA_LEDGER_SURVIVOR;
    default: return CETTA_ARENA_LEDGER_OTHER;
    }
}

static void arena_ledger_charge(CettaArenaLedgerTag tag, int64_t arenas,
                                int64_t bytes) {
    if (tag == CETTA_ARENA_LEDGER_NONE)
        return;
    CettaRuntimeCounter base = (CettaRuntimeCounter)(
        CETTA_RUNTIME_COUNTER_ARENA_LEDGER_OTHER_LIVE +
        4u * ((uint32_t)tag - (uint32_t)CETTA_ARENA_LEDGER_OTHER));
    if (arenas)
        cetta_runtime_stats_gauge_add(base, (CettaRuntimeCounter)(base + 1u),
                                      arenas);
    if (bytes)
        cetta_runtime_stats_gauge_add((CettaRuntimeCounter)(base + 2u),
                                      (CettaRuntimeCounter)(base + 3u),
                                      bytes);
}

static void arena_ledger_open(Arena *a) {
    a->ledger_tag_named = false;
    a->ledger_tag = cetta_runtime_stats_is_enabled()
        ? (uint8_t)arena_ledger_tag_for_kind(a->runtime_kind)
        : (uint8_t)CETTA_ARENA_LEDGER_NONE;
    arena_ledger_charge((CettaArenaLedgerTag)a->ledger_tag, 1, 0);
}

static void arena_ledger_close(Arena *a) {
    arena_ledger_charge((CettaArenaLedgerTag)a->ledger_tag, -1,
                        -(int64_t)a->reserved_bytes);
    a->ledger_tag = (uint8_t)CETTA_ARENA_LEDGER_NONE;
}

static void arena_ledger_retag(Arena *a, CettaArenaLedgerTag tag) {
    if (a->ledger_tag == CETTA_ARENA_LEDGER_NONE || a->ledger_tag == tag)
        return;
    arena_ledger_charge((CettaArenaLedgerTag)a->ledger_tag, -1,
                        -(int64_t)a->reserved_bytes);
    a->ledger_tag = (uint8_t)tag;
    arena_ledger_charge(tag, 1, (int64_t)a->reserved_bytes);
}

static void arena_ledger_stock(int64_t bytes) {
    cetta_runtime_stats_gauge_add(CETTA_RUNTIME_COUNTER_ARENA_STOCK_BYTES,
                                  CETTA_RUNTIME_COUNTER_ARENA_STOCK_BYTES_PEAK,
                                  bytes);
}

void *cetta_malloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) cetta_oom(size);
    return ptr;
}

void *cetta_realloc(void *ptr, size_t size) {
    void *out = realloc(ptr, size == 0 ? 1 : size);
    if (!out) cetta_oom(size);
    return out;
}

/* Short-lived arenas, such as a table generator's machine, return their
 * blocks as fast as they take them.  A thread keeps a few full-size blocks
 * of recycling arenas for the next one: given back to the allocator, they
 * would let it trim the heap top and grow it again around every such
 * lifetime.  Other arenas leave the allocator's own reuse undisturbed. */
#define ARENA_THREAD_STOCK_BLOCKS 32u

#if defined(__SANITIZE_ADDRESS__)
#define ARENA_STOCK_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ARENA_STOCK_ASAN 1
#endif
#endif
#ifdef ARENA_STOCK_ASAN
#include <sanitizer/asan_interface.h>
#define ARENA_STOCK_POISON(block) \
    ASAN_POISON_MEMORY_REGION((block)->data, (block)->capacity)
#define ARENA_STOCK_UNPOISON(block) \
    ASAN_UNPOISON_MEMORY_REGION((block)->data, (block)->capacity)
#else
#define ARENA_STOCK_POISON(block) ((void)(block))
#define ARENA_STOCK_UNPOISON(block) ((void)(block))
#endif

static _Thread_local ArenaBlock *g_arena_stock;
static _Thread_local uint32_t g_arena_stock_len;
static _Thread_local bool g_arena_stock_registered;
static pthread_key_t g_arena_stock_key;
static pthread_once_t g_arena_stock_once = PTHREAD_ONCE_INIT;

static void arena_stock_release(void *unused) {
    (void)unused;
    ArenaBlock *block = g_arena_stock;
    g_arena_stock = NULL;
    g_arena_stock_len = 0u;
    while (block) {
        ArenaBlock *next = block->next;
        ARENA_STOCK_UNPOISON(block);
        arena_ledger_stock(-(int64_t)block->capacity);
        free(block);
        block = next;
    }
}

static void arena_stock_key_init(void) {
    (void)pthread_key_create(&g_arena_stock_key, arena_stock_release);
}

/* A thread's stock is released when the thread exits. */
static void arena_retire_block(ArenaBlock *block, bool recycle) {
    if (!recycle || block->capacity != ARENA_BLOCK_SIZE ||
        g_arena_stock_len >= ARENA_THREAD_STOCK_BLOCKS) {
        free(block);
        return;
    }
    if (!g_arena_stock_registered) {
        (void)pthread_once(&g_arena_stock_once, arena_stock_key_init);
        (void)pthread_setspecific(g_arena_stock_key, (void *)1);
        g_arena_stock_registered = true;
    }
    block->next = g_arena_stock;
    block->used = 0;
    ARENA_STOCK_POISON(block);
    g_arena_stock = block;
    g_arena_stock_len++;
    arena_ledger_stock((int64_t)block->capacity);
}

static ArenaBlock *arena_stock_take(void) {
    ArenaBlock *block = g_arena_stock;
    if (!block)
        return NULL;
    ARENA_STOCK_UNPOISON(block);
    g_arena_stock = block->next;
    g_arena_stock_len--;
    block->next = NULL;
    block->used = 0;
    arena_ledger_stock(-(int64_t)block->capacity);
    return block;
}

/* A new block, from the thread's stock when it is full-size.  Out of line,
 * so the claim path of small arenas stays as short as before. */
static __attribute__((noinline)) ArenaBlock *arena_block_alloc(
    size_t capacity, bool recycle) {
    ArenaBlock *block = recycle && capacity == ARENA_BLOCK_SIZE
        ? arena_stock_take() : NULL;
    if (block)
        return block;
    block = cetta_malloc(sizeof(ArenaBlock) - ARENA_BLOCK_SIZE + capacity);
    block->capacity = capacity;
    block->used = 0;
    block->next = NULL;
    return block;
}

static void arena_free_block_list(ArenaBlock *head, bool recycle) {
    while (head) {
        ArenaBlock *next = head->next;
        arena_retire_block(head, recycle);
        head = next;
    }
}

static ArenaBlock *arena_take_spare_block(Arena *a, size_t min_capacity) {
    ArenaBlock **link = &a->spare;
    while (*link) {
        ArenaBlock *block = *link;
        if (block->capacity >= min_capacity) {
            *link = block->next;
            block->next = NULL;
            block->used = 0;
            a->spare_bytes -= block->capacity;
            a->spare_block_count--;
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ARENA_SPARE_HIT);
            arena_runtime_note_spare(a);
            return block;
        }
        link = &block->next;
    }
    return NULL;
}

/* An arena's storage ends with arena_free; only arena_init opens it again.
 * Every allocation from a freed arena reaches a block claim first, since
 * freeing drops all its blocks, so the check costs nothing elsewhere. */
static __attribute__((cold, noinline)) void arena_used_after_free(void) {
    fputs("fatal: arena used after arena_free without arena_init\n", stderr);
    abort();
}

static ArenaBlock *arena_claim_block(Arena *a, size_t min_capacity) {
    if (__builtin_expect(a->identity == ARENA_ID_DEAD, 0))
        arena_used_after_free();
    ArenaBlock *block = arena_take_spare_block(a, min_capacity);
    if (block)
        return block;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ARENA_SPARE_MISS);

    size_t capacity = a->block_capacity ? a->block_capacity
                                        : ARENA_BLOCK_SIZE;
    if (a->block_growth && a->reserved_bytes > capacity)
        capacity = a->reserved_bytes < ARENA_BLOCK_SIZE
            ? a->reserved_bytes : ARENA_BLOCK_SIZE;
    if (min_capacity > capacity)
        capacity = min_capacity;
    block = arena_block_alloc(capacity, a->recycle_blocks);
    a->reserved_bytes += block->capacity;
    a->block_count++;
    arena_ledger_charge((CettaArenaLedgerTag)a->ledger_tag, 0,
                        (int64_t)block->capacity);
    arena_runtime_note_usage(a);
    return block;
}

static void arena_push_spare_list(Arena *a, ArenaBlock *blocks) {
    if (!a || !blocks)
        return;
    ArenaBlock *tail = blocks;
    uint32_t recycled_blocks = 0;
    size_t recycled_bytes = 0;
    while (tail) {
        recycled_blocks++;
        recycled_bytes += tail->capacity;
        if (!tail->next)
            break;
        tail = tail->next;
    }
    tail->next = a->spare;
    a->spare = blocks;
    a->spare_block_count += recycled_blocks;
    a->spare_bytes += recycled_bytes;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ARENA_SPARE_RECYCLE_BLOCK,
                            recycled_blocks);
    arena_runtime_note_spare(a);
}

static bool arena_block_list_owns_ptr(const ArenaBlock *head, const void *ptr) {
    const char *p = (const char *)ptr;
    for (const ArenaBlock *b = head; b; b = b->next) {
        const char *lo = b->data;
        const char *hi = b->data + b->capacity;
        if (p >= lo && p < hi)
            return true;
    }
    return false;
}

bool arena_owns_ptr(const Arena *a, const void *ptr) {
    if (!a || !ptr)
        return false;
    return arena_block_list_owns_ptr(a->head, ptr) ||
           arena_block_list_owns_ptr(a->spare, ptr);
}

bool arena_owns_atom(const Arena *a, const Atom *atom) {
    return a && atom && a->identity != 0u &&
           atom->arena_id == a->identity;
}

#if CETTA_PROVENANCE_ASSERT
typedef struct ProvenanceArenaNode {
    const Arena *arena;
    struct ProvenanceArenaNode *next;
} ProvenanceArenaNode;

static pthread_mutex_t g_provenance_arena_mutex = PTHREAD_MUTEX_INITIALIZER;
static ProvenanceArenaNode *g_provenance_arenas = NULL;

static const char *arena_runtime_kind_name(CettaArenaRuntimeKind kind) {
    switch (kind) {
    case CETTA_ARENA_RUNTIME_KIND_PERSISTENT: return "PERSISTENT";
    case CETTA_ARENA_RUNTIME_KIND_EVAL: return "EVAL";
    case CETTA_ARENA_RUNTIME_KIND_SCRATCH: return "SCRATCH";
    case CETTA_ARENA_RUNTIME_KIND_SURVIVOR: return "SURVIVOR";
    default: return "OTHER";
    }
}

static bool provenance_tracks_kind(CettaArenaRuntimeKind kind) {
    return kind == CETTA_ARENA_RUNTIME_KIND_EVAL ||
           kind == CETTA_ARENA_RUNTIME_KIND_SURVIVOR;
}

static void provenance_unregister_arena(const Arena *arena) {
    if (!arena)
        return;
    pthread_mutex_lock(&g_provenance_arena_mutex);
    ProvenanceArenaNode **link = &g_provenance_arenas;
    while (*link) {
        ProvenanceArenaNode *node = *link;
        if (node->arena == arena) {
            *link = node->next;
            free(node);
            pthread_mutex_unlock(&g_provenance_arena_mutex);
            return;
        }
        link = &node->next;
    }
    pthread_mutex_unlock(&g_provenance_arena_mutex);
}

static void provenance_register_arena(const Arena *arena) {
    if (!arena || !provenance_tracks_kind(arena->runtime_kind))
        return;
    pthread_mutex_lock(&g_provenance_arena_mutex);
    for (ProvenanceArenaNode *node = g_provenance_arenas; node;
         node = node->next) {
        if (node->arena == arena) {
            pthread_mutex_unlock(&g_provenance_arena_mutex);
            return;
        }
    }
    ProvenanceArenaNode *node = cetta_malloc(sizeof(*node));
    node->arena = arena;
    node->next = g_provenance_arenas;
    g_provenance_arenas = node;
    pthread_mutex_unlock(&g_provenance_arena_mutex);
}

typedef struct {
    const Arena *arena;
    uint32_t older_identity;
} ProvenanceOwners;

static const Arena *provenance_transient_owner_of(const void *ptr,
                                                  ProvenanceOwners allowed_owner) {
    const Arena *owner = NULL;
    if (!ptr)
        return NULL;
    pthread_mutex_lock(&g_provenance_arena_mutex);
    for (ProvenanceArenaNode *node = g_provenance_arenas; node;
         node = node->next) {
        const Arena *arena = node->arena;
        if (arena == allowed_owner.arena ||
            (allowed_owner.older_identity != 0u &&
             arena->identity == allowed_owner.older_identity))
            continue;
        if (arena_owns_ptr(arena, ptr)) {
            owner = arena;
            break;
        }
    }
    pthread_mutex_unlock(&g_provenance_arena_mutex);
    return owner;
}

static void provenance_fail(Atom *root, const char *site, const char *what,
                            const void *ptr, const Arena *owner) {
    fprintf(stderr,
            "fatal: CETTA_PROVENANCE_ASSERT: %s retains %s pointer %p "
            "from %s arena\n",
            site ? site : "<unknown-site>", what ? what : "unknown",
            ptr, owner ? arena_runtime_kind_name(owner->runtime_kind) : "unknown");
    if (root) {
        fprintf(stderr, "  root atom: ");
        atom_print(root, stderr);
        fputc('\n', stderr);
    }
    abort();
}

static void provenance_check_ptr(Atom *root, const char *site,
                                 ProvenanceOwners allowed_owner,
                                 const char *what, const void *ptr) {
    const Arena *owner = provenance_transient_owner_of(ptr, allowed_owner);
    if (owner)
        provenance_fail(root, site, what, ptr, owner);
}

typedef struct {
    const void **slots;
    size_t cap;
    size_t used;
} ProvenancePointerSet;

static size_t provenance_pointer_hash(const void *ptr) {
    uintptr_t bits = (uintptr_t)ptr;
    bits >>= 4u;
    bits ^= bits >> 17u;
    bits *= (uintptr_t)UINT64_C(0x9e3779b97f4a7c15);
    return (size_t)(bits ^ (bits >> 29u));
}

static void provenance_pointer_set_reserve(
    ProvenancePointerSet *set, size_t needed) {
    size_t cap = set->cap ? set->cap : 64u;
    while (needed > cap / 2u) {
        if (cap > SIZE_MAX / 2u)
            cetta_oom(SIZE_MAX);
        cap *= 2u;
    }
    if (cap == set->cap)
        return;

    const void **slots = cetta_malloc(cap * sizeof(*slots));
    memset(slots, 0, cap * sizeof(*slots));
    size_t mask = cap - 1u;
    for (size_t i = 0u; i < set->cap; i++) {
        const void *item = set->slots[i];
        if (!item)
            continue;
        size_t slot = provenance_pointer_hash(item) & mask;
        while (slots[slot])
            slot = (slot + 1u) & mask;
        slots[slot] = item;
    }
    free(set->slots);
    set->slots = slots;
    set->cap = cap;
}

static bool provenance_pointer_set_insert(
    ProvenancePointerSet *set, const void *ptr) {
    provenance_pointer_set_reserve(set, set->used + 1u);
    size_t mask = set->cap - 1u;
    size_t slot = provenance_pointer_hash(ptr) & mask;
    while (set->slots[slot]) {
        if (set->slots[slot] == ptr)
            return false;
        slot = (slot + 1u) & mask;
    }
    set->slots[slot] = ptr;
    set->used++;
    return true;
}

static void provenance_atom_stack_push(
    Atom ***stack_io, size_t *length_io, size_t *capacity_io,
    Atom **inline_stack, size_t inline_capacity, Atom *atom) {
    if (!atom)
        return;
    if (*length_io == *capacity_io) {
        size_t next_capacity = *capacity_io * 2u;
        if (next_capacity < *capacity_io ||
            next_capacity > SIZE_MAX / sizeof(Atom *))
            cetta_oom(SIZE_MAX);
        if (*stack_io == inline_stack) {
            Atom **grown = cetta_malloc(
                next_capacity * sizeof(*grown));
            memcpy(grown, inline_stack,
                   inline_capacity * sizeof(*grown));
            *stack_io = grown;
        } else {
            *stack_io = cetta_realloc(
                *stack_io, next_capacity * sizeof(**stack_io));
        }
        *capacity_io = next_capacity;
    }
    (*stack_io)[(*length_io)++] = atom;
}

static void provenance_check_atom(Atom *root, Atom *atom, const char *site,
                                  ProvenanceOwners allowed_owner,
                                  uint32_t depth) {
    (void)depth;
    if (!atom)
        return;

    Atom *inline_stack[64];
    Atom **stack = inline_stack;
    size_t length = 0u;
    size_t capacity = sizeof(inline_stack) / sizeof(inline_stack[0]);
    ProvenancePointerSet seen = {0};
    provenance_atom_stack_push(
        &stack, &length, &capacity, inline_stack,
        sizeof(inline_stack) / sizeof(inline_stack[0]), atom);

    while (length > 0u) {
        Atom *current = stack[--length];
        if (!provenance_pointer_set_insert(&seen, current))
            continue;

        provenance_check_ptr(
            root, site, allowed_owner, "Atom", current);
        switch (current->kind) {
        case ATOM_SYMBOL:
            break;
        case ATOM_VAR:
            provenance_atom_stack_push(
                &stack, &length, &capacity, inline_stack,
                sizeof(inline_stack) / sizeof(inline_stack[0]),
                current->name_key);
            break;
        case ATOM_GROUNDED:
            switch (current->ground.gkind) {
        case GV_STRING:
            provenance_check_ptr(root, site, allowed_owner, "string payload",
                                 current->ground.sval);
            break;
        case GV_BIGINT:
            provenance_check_ptr(root, site, allowed_owner, "bigint payload",
                                 current->ground.bigint);
            if (current->ground.bigint) {
                provenance_check_ptr(root, site, allowed_owner, "bigint text",
                                     atomic_load_explicit(
                                         &current->ground.bigint->text,
                                         memory_order_acquire));
            }
            break;
        case GV_RATIONAL:
            provenance_check_ptr(root, site, allowed_owner, "rational payload",
                                 current->ground.rational);
            if (current->ground.rational) {
                provenance_check_ptr(root, site, allowed_owner, "rational text",
                                     current->ground.rational->text);
            }
            break;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
            provenance_check_ptr(root, site, allowed_owner, "grounded handle",
                                 current->ground.ptr);
            break;
        case GV_TERM_GRAPH: {
            /* The arena retains the sealed graph; its node record and graph
             * are stable handles, not a recursively unfolded atom tree. */
            const CettaTermGraphRef *ref = current->ground.ptr;
            provenance_check_ptr(root, site, allowed_owner,
                                 "rational term node", ref);
            if (ref)
                provenance_check_ptr(root, site, allowed_owner,
                                     "rational term graph", ref->graph);
            break;
        }
        case GV_INTERNAL_TAG:
            break;
        case GV_STRUCTURAL:
        case GV_SEAL:
            /* A seal object is a heap record retained by the arena's hold,
             * not storage of any arena. */
            break;
        case GV_PRIME_NEED_CAPABILITY:
            provenance_check_ptr(root, site, allowed_owner,
                                 "Prime Need capability",
                                 current->ground.prime_need_capability);
            break;
        case GV_PRIME_CONTEXT: {
            const CettaPrimeContext *frame =
                current->ground.prime_context;
            while (frame) {
                if (!provenance_pointer_set_insert(&seen, frame))
                    break;
                provenance_check_ptr(root, site, allowed_owner,
                                     "Prime context frame", frame);
                provenance_atom_stack_push(
                    &stack, &length, &capacity, inline_stack,
                    sizeof(inline_stack) / sizeof(inline_stack[0]),
                    frame->value);
                provenance_atom_stack_push(
                    &stack, &length, &capacity, inline_stack,
                    sizeof(inline_stack) / sizeof(inline_stack[0]),
                    frame->key);
                frame = frame->parent;
            }
            break;
        }
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
            break;
            }
            break;
        case ATOM_EXPR:
            provenance_check_ptr(
                root, site, allowed_owner, "expr elems",
                current->expr.elems);
            for (CettaExprIndex i = current->expr.len; i > 0u; i--) {
                provenance_atom_stack_push(
                    &stack, &length, &capacity, inline_stack,
                    sizeof(inline_stack) / sizeof(inline_stack[0]),
                    current->expr.elems[i - 1u]);
            }
            break;
        }
    }
    free(seen.slots);
    if (stack != inline_stack)
        free(stack);
}

void cetta_provenance_assert_not_transient(Atom *atom, const char *site) {
    cetta_provenance_assert_not_transient_except(atom, site, NULL);
}

void cetta_provenance_assert_not_transient_except(Atom *atom, const char *site,
                                                 const Arena *allowed_owner) {
    provenance_check_atom(atom, atom, site,
                          (ProvenanceOwners){.arena = allowed_owner}, 0);
}

void cetta_provenance_assert_state_cell_not_transient(const StateCell *cell,
                                                     const char *site) {
    if (!cell)
        return;
    provenance_check_ptr(NULL, site, (ProvenanceOwners){0}, "StateCell", cell);
    provenance_check_atom(cell->value, cell->value, site, (ProvenanceOwners){0}, 0);
    provenance_check_atom(cell->content_type, cell->content_type, site,
                          (ProvenanceOwners){0}, 0);
}
#else
static void provenance_unregister_arena(const Arena *arena) {
    (void)arena;
}

static void provenance_register_arena(const Arena *arena) {
    (void)arena;
}
#endif

/* Copy elision admits the destination's explicitly linked older generation.
 * Publication diagnostics above still admit only their named arena: a
 * borrowing licence must not become a persistent ownership certificate. */
static inline void provenance_assert_settled(Atom *atom, const char *site,
                                             const Arena *destination) {
#if CETTA_PROVENANCE_ASSERT
    provenance_check_atom(atom, atom, site,
        (ProvenanceOwners){destination, destination->older_identity}, 0);
#else
    (void)atom;
    (void)site;
    (void)destination;
#endif
}

enum {
    HASHCONS_SMALL_INT_MIN = -128,
    HASHCONS_SMALL_INT_MAX = 4095,
    HASHCONS_SMALL_INT_COUNT =
        HASHCONS_SMALL_INT_MAX - HASHCONS_SMALL_INT_MIN + 1,
};

enum {
    ARENA_SYMBOL_CACHE_LOG2_CAPACITY = 6u,
    ARENA_SYMBOL_CACHE_CAPACITY =
        1u << ARENA_SYMBOL_CACHE_LOG2_CAPACITY,
};

_Static_assert(ARENA_SYMBOL_CACHE_CAPACITY <= 64u,
               "arena symbol cache occupancy fits one word");

enum {
    ARENA_INT_CACHE_MIN = -16,
    ARENA_INT_CACHE_CAPACITY = 256,
};

struct ArenaSymbolCache {
    Atom *atoms[ARENA_SYMBOL_CACHE_CAPACITY];
    uint64_t epochs[ARENA_SYMBOL_CACHE_CAPACITY];
    uint64_t occupied;
    uint64_t symbol_table_instance;
    /* An arena with a scalar cache keeps its canonical scalars here instead,
     * in storage its own resets never reclaim: one atom per symbol slot and
     * per small integer, so the owner stays bounded while the scalars
     * outlive every per-call reset of the arena they are handed out by. */
    Atom *ints[ARENA_INT_CACHE_CAPACITY];
    Arena owner;
    bool owner_ready;
};

static size_t arena_symbol_cache_storage_bytes(
    const ArenaSymbolCache *cache) {
    return cache ? sizeof(*cache) : 0u;
}

static void arena_symbol_cache_free(Arena *a) {
    if (!a || !a->symbol_cache)
        return;
    if (a->symbol_cache->owner_ready)
        arena_free(&a->symbol_cache->owner);
    free(a->symbol_cache);
    a->symbol_cache = NULL;
    a->symbol_cache_bytes = 0u;
}

/* Name an arena for a new incarnation: its identity, and a reset epoch
 * above every epoch an earlier incarnation of that name used. */
static void arena_take_name(Arena *a) {
    uint32_t name = 0u;
    uint64_t generation = 0u;
    if (!cetta_name_acquire(&g_arena_names, &g_arena_name_local,
                            arena_names_once, &name, &generation)) {
        fputs("fatal: resource exhausted: every arena identity names a live "
              "arena\n", stderr);
        abort();
    }
    a->identity = name;
    a->reset_epoch = generation;
}

void arena_init(Arena *a) {
    a->head = NULL;
    a->spare = NULL;
    a->hashcons = g_hashcons;
    a->symbol_cache = NULL;
    a->symbol_cache_bytes = 0u;
    a->live_bytes = 0;
    a->external_bytes = 0;
    a->reserved_bytes = 0;
    a->spare_bytes = 0;
    a->block_count = 0;
    a->spare_block_count = 0;
    a->runtime_kind = CETTA_ARENA_RUNTIME_KIND_OTHER;
    arena_take_name(a);
    a->older_identity = 0u;
    a->block_capacity = 0u;
    a->finalizers = NULL;
    a->retained_owners = NULL;
    a->frame_identities = NULL;
    a->scalar_cache = false;
    a->own_symbols = false;
    a->recycle_blocks = false;
    a->block_growth = false;
    arena_ledger_open(a);
}

void arena_init_detached(Arena *a) {
    arena_init(a);
    a->hashcons = NULL;
}

void arena_set_block_capacity(Arena *a, size_t capacity) {
    a->block_capacity = capacity > ARENA_BLOCK_SIZE ? 0u
                                                    : (uint32_t)capacity;
    a->block_growth = false;
}

void arena_set_block_growth(Arena *a, size_t first) {
    arena_set_block_capacity(a, first);
    a->block_growth = a->block_capacity != 0u &&
        a->block_capacity < ARENA_BLOCK_SIZE;
}

static void arena_run_finalizers_until(Arena *a, ArenaFinalizer *stop) {
    while (a->finalizers != stop) {
        ArenaFinalizer *node = a->finalizers;
        a->finalizers = node->next;
        ArenaRetainedOwnerSet *owners = a->retained_owners;
        if (owners && owners->len &&
            node == owners->holds[owners->len - 1u]) {
            owners->len--;
            owners->slots[owners->index[owners->len]] = 0u;
        }
        node->fn(node->ptr);
        free(node);
    }
}

static void arena_register_finalizer(Arena *a, void (*fn)(void *), void *ptr) {
    ArenaFinalizer *node = cetta_malloc(sizeof(ArenaFinalizer));
    node->fn = fn;
    node->ptr = ptr;
    node->next = a->finalizers;
    a->finalizers = node;
    arena_account_external_bytes(a, sizeof(*node));
}

enum {
    ARENA_RETAINED_OWNER_SET_MIN_CAPACITY = 8u,
    ARENA_RETAINED_OWNER_SET_MAX_CAPACITY = UINT32_C(0x80000000),
};

static size_t arena_retained_owner_set_bytes(const ArenaRetainedOwnerSet *set) {
    if (!set)
        return 0u;
    return sizeof(*set) + (size_t)set->cap * sizeof(*set->holds) +
           (size_t)set->cap * sizeof(*set->index) +
           (size_t)set->slot_cap * sizeof(*set->slots);
}

static uint32_t arena_retained_owner_slot(const ArenaRetainedOwnerSet *set,
                                          const void *owner,
                                          void (*release)(void *)) {
    uint64_t mixed = (uint64_t)(uintptr_t)owner ^
        ((uint64_t)(uintptr_t)release << 17u ^
         (uint64_t)(uintptr_t)release >> 47u);
    mixed ^= mixed >> 33u;
    mixed *= UINT64_C(0xff51afd7ed558ccd);
    mixed ^= mixed >> 29u;
    return (uint32_t)mixed & set->slot_mask;
}

static bool arena_retained_owner_set_index_reserve(ArenaRetainedOwnerSet *set,
                                                   uint32_t needed) {
    if (needed > ARENA_RETAINED_OWNER_SET_MAX_CAPACITY / 2u)
        return false;
    uint32_t wanted = ARENA_RETAINED_OWNER_SET_MIN_CAPACITY;
    while (wanted < needed * 2u) {
        if (wanted > ARENA_RETAINED_OWNER_SET_MAX_CAPACITY / 2u)
            return false;
        wanted *= 2u;
    }
    if (set->slot_cap >= wanted)
        return true;
    free(set->slots);
    set->slots = cetta_malloc((size_t)wanted * sizeof(*set->slots));
    memset(set->slots, 0, (size_t)wanted * sizeof(*set->slots));
    set->slot_cap = wanted;
    set->slot_mask = wanted - 1u;
    /* Reinsert in acquisition order, which keeps every probe chain made of
     * records older than the record it leads to. */
    for (uint32_t i = 0u; i < set->len; i++) {
        uint32_t slot = arena_retained_owner_slot(
            set, set->holds[i]->ptr, set->holds[i]->fn);
        while (set->slots[slot])
            slot = (slot + 1u) & set->slot_mask;
        set->slots[slot] = i + 1u;
        set->index[i] = slot;
    }
    return true;
}

static bool arena_retained_owner_set_records_reserve(ArenaRetainedOwnerSet *set,
                                                     uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t capacity = set->cap ? set->cap
                                 : ARENA_RETAINED_OWNER_SET_MIN_CAPACITY;
    while (capacity < needed) {
        if (capacity > ARENA_RETAINED_OWNER_SET_MAX_CAPACITY / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity != 0u &&
        SIZE_MAX / (size_t)capacity < sizeof(*set->holds))
        return false;
    set->holds = cetta_realloc(set->holds, (size_t)capacity * sizeof(*set->holds));
    set->index = cetta_realloc(set->index, (size_t)capacity * sizeof(*set->index));
    set->cap = capacity;
    return true;
}

/* The newest hold is the likeliest repeat (one owner retained by a run of
 * borrows), so it is checked before the index. */
static bool arena_retained_owner_set_find(const ArenaRetainedOwnerSet *set,
                                          const void *owner,
                                          void (*release)(void *)) {
    if (!set || set->len == 0u)
        return false;
    const ArenaFinalizer *newest = set->holds[set->len - 1u];
    if (newest->ptr == owner && newest->fn == release)
        return true;
    if (!set->slot_cap)
        return false;
    uint32_t slot = arena_retained_owner_slot(set, owner, release);
    for (uint32_t entry; (entry = set->slots[slot]) != 0u;
         slot = (slot + 1u) & set->slot_mask) {
        const ArenaFinalizer *held = set->holds[entry - 1u];
        if (held->ptr == owner && held->fn == release)
            return true;
    }
    return false;
}

bool arena_retain_owner(Arena *a, void *owner,
                        void (*retain)(void *), void (*release)(void *)) {
    if (!a || !owner || !retain || !release)
        return false;
    if (!a->retained_owners) {
        a->retained_owners = cetta_malloc(sizeof(*a->retained_owners));
        *a->retained_owners = (ArenaRetainedOwnerSet){0};
        arena_account_external_bytes(
            a, arena_retained_owner_set_bytes(a->retained_owners));
    }
    ArenaRetainedOwnerSet *set = a->retained_owners;
    if (arena_retained_owner_set_find(set, owner, release))
        return true;
    size_t before = arena_retained_owner_set_bytes(set);
    if (set->len >= ARENA_RETAINED_OWNER_SET_MAX_CAPACITY / 2u ||
        !arena_retained_owner_set_records_reserve(set, set->len + 1u) ||
        !arena_retained_owner_set_index_reserve(set, set->len + 1u))
        return false;
    ArenaFinalizer *held = cetta_malloc(sizeof(*held));
    retain(owner);
    *held = (ArenaFinalizer){.fn = release, .ptr = owner, .next = a->finalizers};
    a->finalizers = held;
    uint32_t position = set->len;
    uint32_t slot = arena_retained_owner_slot(set, owner, release);
    while (set->slots[slot])
        slot = (slot + 1u) & set->slot_mask;
    set->slots[slot] = position + 1u;
    set->holds[position] = held;
    set->index[position] = slot;
    set->len = position + 1u;
    arena_account_external_bytes(
        a, arena_retained_owner_set_bytes(set) - before + sizeof(*held));
    return true;
}

/* Called after every hold has been popped from the finalizer chain. */
static void arena_release_retained_owner_set(Arena *a) {
    ArenaRetainedOwnerSet *set = a->retained_owners;
    if (!set)
        return;
    size_t bytes = arena_retained_owner_set_bytes(set);
    free(set->holds);
    free(set->slots);
    free(set->index);
    free(set);
    a->retained_owners = NULL;
    a->external_bytes = a->external_bytes >= bytes ? a->external_bytes - bytes
                                                   : 0u;
}

/* ── Arena frame-identity ownership ─────────────────────────────────────── */
/*
 * One hold per distinct full generational identity reachable from this arena's
 * live allocation region.  Syntax construction consults the set for every
 * variable occurrence, so a term mentioning one activation's variable a
 * thousand times performs one retain and keeps one record.
 *
 * Records are appended in acquisition order and never permuted, so a partial
 * reset can drop exactly the suffix acquired after its mark.  The index is an
 * Threading contract: an Arena is owned by one thread at a time, by the same
 * lifetime confinement as the rest of its allocation fields.  Arena access is
 * not synchronized internally — mutual exclusion is the caller's
 * responsibility.  The retain/release it performs calls the global identity
 * allocator, which keeps per-cell atomics; those atomics make the borrow
 * correct across threads.  No GIL is read or held by the set.
 */
struct ArenaFrameIdentitySet {
    CettaFrameIdentity *identities;   /* acquisition order                */
    uint32_t *slots;                  /* index -> record position         */
    uint32_t *index;                  /* record position -> its index slot */
    uint32_t len;                     /* distinct live identities         */
    uint32_t cap;                     /* capacity of `identities`         */
    uint32_t slot_cap;                /* power of two, or zero            */
    uint32_t slot_mask;
};

enum {
    ARENA_FRAME_IDENTITY_SET_MIN_CAPACITY = 8u,
    ARENA_FRAME_IDENTITY_SET_MAX_CAPACITY = UINT32_C(0x80000000),
};

static size_t arena_frame_identity_set_bytes(const ArenaFrameIdentitySet *set);

static ArenaFrameIdentitySet *arena_frame_identity_set_alloc(void) {
    ArenaFrameIdentitySet *set = cetta_malloc(sizeof(*set));
    *set = (ArenaFrameIdentitySet){0};
    return set;
}

static void arena_frame_identity_set_dispose(ArenaFrameIdentitySet *set) {
    if (!set)
        return;
    for (uint32_t i = 0u; i < set->len; i++)
        cetta_frame_identity_release(set->identities[i]);
    free(set->identities);
    free(set->slots);
    free(set->index);
    free(set);
}

/* Audit finalizer ordering: on reset, identity holds are released *before*
 * per-arena object finalizers (BigInt, rational, etc.) run.  Those finalizers
 * clear GMP payloads; they do not read frame identities.  This ordering is
 * safe — and it is the only order available, because finalizer cleanup is
 * the last stop before arena teardown.
 */

/* Storage retained by the set itself.  The arena must account for this
 * wherever the set lives: allocation policies consult `external_bytes`
 * and floors into `live_bytes`.  Capacity that survives a partial reset
 * is still charged; only a full `arena_free` releases the object. */
static size_t arena_frame_identity_set_bytes(const ArenaFrameIdentitySet *set) {
    if (!set)
        return 0u;
    return sizeof(*set) + (size_t)set->cap * sizeof(*set->identities) +
           (size_t)set->cap * sizeof(*set->index) +
           (size_t)set->slot_cap * sizeof(*set->slots);
}

/* Charge the arena for identity-set storage.  The arena already accounts for
 * the set as part of `external_bytes` when it is created and freed, recording
 * the retained block so partial-reset capacity does not masquerade as free
 * space.  This helper only handles shape changes after creation. */
static void arena_frame_identity_set_account_growth(Arena *a,
                                                    size_t previous_bytes) {
    if (!a || !a->frame_identities)
        return;
    size_t now = arena_frame_identity_set_bytes(a->frame_identities);
    if (now >= previous_bytes) {
        if (now > previous_bytes)
        arena_account_external_bytes(a, now - previous_bytes);
    } else {
        a->external_bytes -= (previous_bytes - now);
    }
}

/* Index slots hold `position + 1` so that zero means "empty". */
static uint32_t arena_frame_identity_index_slot(
        const ArenaFrameIdentitySet *set, CettaFrameIdentity identity) {
    uint64_t mixed = identity.word;
    mixed ^= mixed >> 33u;
    mixed *= UINT64_C(0xff51afd7ed558ccd);
    mixed ^= mixed >> 29u;
    return (uint32_t)mixed & set->slot_mask;
}

static bool arena_frame_identity_set_index_reserve(ArenaFrameIdentitySet *set,
                                                   uint32_t needed) {
    uint32_t wanted = ARENA_FRAME_IDENTITY_SET_MIN_CAPACITY;
    while (wanted < needed * 2u) {
        if (wanted > ARENA_FRAME_IDENTITY_SET_MAX_CAPACITY / 2u)
            return false;
        wanted *= 2u;
    }
    if (set->slot_cap >= wanted)
        return true;
    free(set->slots);
    set->slots = cetta_malloc((size_t)wanted * sizeof(*set->slots));
    memset(set->slots, 0, (size_t)wanted * sizeof(*set->slots));
    set->slot_cap = wanted;
    set->slot_mask = wanted - 1u;
    for (uint32_t i = 0u; i < set->len; i++) {
        uint32_t slot = arena_frame_identity_index_slot(set, set->identities[i]);
        while (set->slots[slot])
            slot = (slot + 1u) & set->slot_mask;
        set->slots[slot] = i + 1u;
        set->index[i] = slot;
    }
    return true;
}

static bool arena_frame_identity_set_records_reserve(ArenaFrameIdentitySet *set,
                                                     uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t capacity = set->cap ? set->cap
                                 : ARENA_FRAME_IDENTITY_SET_MIN_CAPACITY;
    while (capacity < needed) {
        if (capacity > ARENA_FRAME_IDENTITY_SET_MAX_CAPACITY / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity != 0u &&
        (SIZE_MAX / (size_t)capacity < sizeof(*set->identities) ||
         SIZE_MAX / (size_t)capacity < sizeof(*set->index)))
        return false;
    CettaFrameIdentity *identities = cetta_realloc(
        set->identities, (size_t)capacity * sizeof(*identities));
    uint32_t *index = cetta_realloc(
        set->index, (size_t)capacity * sizeof(*index));
    set->identities = identities;
    set->index = index;
    set->cap = capacity;
    return true;
}

/* The most recently acquired identity is a likely repeat, so probe the tail
 * before consulting the index.  Repeated occurrences of one variable, which
 * is the common shape in generated activation syntax, are then found without
 * touching the index at all. */
static bool arena_frame_identity_set_find(const ArenaFrameIdentitySet *set,
                                          CettaFrameIdentity identity,
                                          uint32_t *position_out) {
    if (!set || set->len == 0u)
        return false;
    if (cetta_frame_identity_eq(set->identities[set->len - 1u], identity)) {
        if (position_out)
            *position_out = set->len - 1u;
        return true;
    }
    if (!set->slot_cap)
        return false;
    uint32_t slot = arena_frame_identity_index_slot(set, identity);
    for (;;) {
        uint32_t entry = set->slots[slot];
        if (!entry)
            return false;
        uint32_t position = entry - 1u;
        if (cetta_frame_identity_eq(set->identities[position], identity)) {
            if (position_out)
                *position_out = position;
            return true;
        }
        slot = (slot + 1u) & set->slot_mask;
    }
}

/* Claim one hold.  Failure to retain leaves the set untouched, so a stale
 * identity can never become cached membership. */
static bool arena_frame_identity_set_acquire(Arena *a,
                                             CettaFrameIdentity identity) {
    if (cetta_frame_identity_is_none(identity))
        return true;
    if (!a->frame_identities) {
        a->frame_identities = arena_frame_identity_set_alloc();
        /* The object itself is now charged; no identities yet to account. */
        arena_account_external_bytes(
            a, arena_frame_identity_set_bytes(a->frame_identities));
    }
    ArenaFrameIdentitySet *set = a->frame_identities;
    if (arena_frame_identity_set_find(set, identity, NULL))
        return true;
    size_t before;
    if (!a->frame_identities)
        before = 0u;
    else
        before = arena_frame_identity_set_bytes(set);
    if (set->len == UINT32_MAX ||
        !arena_frame_identity_set_records_reserve(set, set->len + 1u) ||
        !arena_frame_identity_set_index_reserve(set, set->len + 1u))
        return false;
    if (!cetta_frame_identity_retain(identity))
        return false;
    uint32_t position = set->len;
    uint32_t slot = arena_frame_identity_index_slot(set, identity);
    while (set->slots[slot])
        slot = (slot + 1u) & set->slot_mask;
    set->slots[slot] = position + 1u;
    set->identities[position] = identity;
    set->index[position] = slot;
    set->len = position + 1u;
    arena_frame_identity_set_account_growth(a, before);
    return true;
}

static void arena_frame_identity_set_truncate(Arena *a, uint32_t length) {
    ArenaFrameIdentitySet *set = a->frame_identities;
    if (!set || length >= set->len)
        return;
    for (uint32_t i = length; i < set->len; i++) {
        uint32_t slot = set->index[i];
        set->slots[slot] = 0u;
        cetta_frame_identity_release(set->identities[i]);
    }
    set->len = length;
    /* Survivors keep their slots and `index` positions unchanged.  Their probe
     * chains cannot have depended on any removed slot, because insertion
     * appends records and `index_reserve` reinserts the entire prefix in
     * acquisition order: every probe visited only slots inserted earlier.
     * A removed record was inserted later than any record that survives,
     * so no surviving chain skipped through it. */
}

static void arena_release_frame_identities(Arena *a) {
    if (!a || !a->frame_identities)
        return;
    ArenaFrameIdentitySet *set = a->frame_identities;
    size_t bytes = arena_frame_identity_set_bytes(set);
    arena_frame_identity_set_dispose(a->frame_identities);
    a->frame_identities = NULL;
    if (a->external_bytes >= bytes)
        a->external_bytes -= bytes;
    else
        a->external_bytes = 0u;
}

bool arena_retain_frame_identity(Arena *a, CettaFrameIdentity identity) {
    return a && arena_frame_identity_set_acquire(a, identity);
}

static void arena_invalidate_allocations(
    Arena *a, CettaGsltLifetimeTransition transition) {
    if (!a)
        return;
    if (!cetta_gslt_lifetime_invalidates_allocations(transition)) {
        fputs("fatal: non-invalidating arena transition used as invalidation\n",
              stderr);
        abort();
    }
    if (a->reset_epoch == UINT64_MAX) {
        fputs("fatal: arena allocation epoch space exhausted\n", stderr);
        abort();
    }
    a->reset_epoch++;
}

/* End the incarnation: release the older generation this arena kept named
 * and its own name, whose next incarnation starts above the epochs this one
 * used.  The struct is left dead until arena_init opens it again. */
static void arena_release_names(Arena *a) {
    if (a->older_identity != 0u) {
        cetta_name_release(&g_arena_names, &g_arena_name_local,
                           arena_names_once, a->older_identity, 0u);
        a->older_identity = 0u;
    }
    if (a->identity != 0u && a->identity != ARENA_ID_DEAD) {
        /* The next incarnation starts above every epoch this one used; with
         * none left, the name is retired. */
        uint64_t next = a->reset_epoch == UINT64_MAX ? UINT64_MAX
                                                     : a->reset_epoch + 1u;
        cetta_name_release(&g_arena_names, &g_arena_name_local,
                           arena_names_once, a->identity, next);
        a->identity = ARENA_ID_DEAD;
    }
}

void arena_free(Arena *a) {
    if (!a)
        return;
    arena_invalidate_allocations(a, CETTA_GSLT_LIFETIME_ARENA_FREE);
    provenance_unregister_arena(a);
    arena_run_finalizers_until(a, NULL);
    arena_free_block_list(a->head, a->recycle_blocks);
    arena_free_block_list(a->spare, a->recycle_blocks);
    arena_symbol_cache_free(a);
    arena_release_frame_identities(a);
    arena_release_retained_owner_set(a);
    arena_ledger_close(a);
    arena_release_names(a);
    a->head = NULL;
    a->spare = NULL;
    a->live_bytes = 0;
    a->external_bytes = 0;
    a->reserved_bytes = 0;
    a->spare_bytes = 0;
    a->block_count = 0;
    a->spare_block_count = 0;
    a->finalizers = NULL;
}

void arena_reserve(Arena *a, size_t size) {
    if (!a || size == 0)
        return;
    size = (size + 7) & ~(size_t)7;
    size_t available =
        (a->head && a->head->capacity > a->head->used)
            ? (a->head->capacity - a->head->used)
            : 0;
    if (available >= size)
        return;

    ArenaBlock *b = arena_claim_block(a, size);
    b->next = a->head;
    a->head = b;
}

void arena_set_hashcons(Arena *a, HashConsTable *hc) {
    if (!a) return;
    a->hashcons = hc;
}

void arena_set_scalar_cache(Arena *a, bool enabled) {
    if (a)
        a->scalar_cache = enabled;
}

void arena_set_own_symbols(Arena *a, bool enabled) {
    if (a)
        a->own_symbols = enabled;
}

void arena_set_block_recycling(Arena *a, bool enabled) {
    if (a)
        a->recycle_blocks = enabled;
}

void arena_set_runtime_kind(Arena *a, CettaArenaRuntimeKind kind) {
    if (!a) return;
    provenance_unregister_arena(a);
    a->runtime_kind = kind;
    provenance_register_arena(a);
    if (!a->ledger_tag_named)
        arena_ledger_retag(a, arena_ledger_tag_for_kind(kind));
    arena_runtime_note_usage(a);
}

void arena_set_ledger_tag(Arena *a, CettaArenaLedgerTag tag) {
    if (!a || tag == CETTA_ARENA_LEDGER_NONE)
        return;
    a->ledger_tag_named = true;
    arena_ledger_retag(a, tag);
}

void arena_set_older_generation(Arena *young, const Arena *older) {
    if (!young)
        return;
    uint32_t linked = older && older != young &&
        older->older_identity == 0u && older->identity != 0u &&
        older->identity != ARENA_ID_DEAD ? older->identity : 0u;
    if (linked == young->older_identity)
        return;
    /* The link owns the older arena's name, so the name cannot pass to
     * another arena while this one still takes its atoms as settled. */
    if (linked)
        cetta_name_retain(&g_arena_names, linked);
    if (young->older_identity)
        cetta_name_release(&g_arena_names, &g_arena_name_local,
                           arena_names_once, young->older_identity, 0u);
    young->older_identity = linked;
}

extern inline size_t arena_accounted_live_bytes(const Arena *a);
extern inline size_t arena_mark_accounted_live_bytes(ArenaMark mark);

void arena_account_external_bytes(Arena *a, size_t size) {
    if (!a || size == 0u)
        return;
    if (a->external_bytes > SIZE_MAX - size)
        a->external_bytes = SIZE_MAX;
    else
        a->external_bytes += size;
    arena_runtime_note_usage(a);
}

static size_t arena_ownership_index_bytes(const Arena *a) {
    return arena_retained_owner_set_bytes(a->retained_owners) +
        arena_frame_identity_set_bytes(a->frame_identities);
}

/* A mark restores allocations with the region's lifetime.  Index capacity
 * has the arena's lifetime instead and remains charged across rollback. */
static size_t arena_mark_external_bytes(const Arena *a, ArenaMark mark) {
    size_t transient = mark.external_bytes >= mark.ownership_index_bytes
        ? mark.external_bytes - mark.ownership_index_bytes : 0u;
    size_t indexes = arena_ownership_index_bytes(a);
    return transient > SIZE_MAX - indexes ? SIZE_MAX : transient + indexes;
}

ArenaMark arena_mark(const Arena *a) {
    ArenaMark mark = {0};
    if (!a) {
        return mark;
    }
    mark.head = a->head;
    mark.used = a->head ? a->head->used : 0;
    mark.live_bytes = a->live_bytes;
    mark.external_bytes = a->external_bytes;
    mark.ownership_index_bytes = arena_ownership_index_bytes(a);
    mark.symbol_cache_bytes = a->symbol_cache_bytes;
    mark.reserved_bytes = a->reserved_bytes;
    mark.block_count = a->block_count;
    mark.finalizers = a->finalizers;
    mark.frame_identity_len = a->frame_identities
        ? a->frame_identities->len : 0u;
    return mark;
}

bool arena_at_mark(const Arena *a, ArenaMark mark) {
    if (!a)
        return true;
    return a->head == mark.head &&
        (a->head ? a->head->used : 0u) == mark.used &&
        a->live_bytes == mark.live_bytes &&
        a->external_bytes == arena_mark_external_bytes(a, mark) &&
        a->finalizers == mark.finalizers &&
        (a->frame_identities ? a->frame_identities->len : 0u) ==
            mark.frame_identity_len;
}

void arena_release_spare(Arena *a, size_t keep_bytes) {
    if (!a)
        return;
    size_t kept = 0;
    ArenaBlock **link = &a->spare;
    while (*link) {
        ArenaBlock *block = *link;
        if (kept < keep_bytes) {
            kept += block->capacity;
            link = &block->next;
            continue;
        }
        *link = block->next;
        a->spare_bytes -= block->capacity;
        a->spare_block_count--;
        a->reserved_bytes -= block->capacity;
        a->block_count--;
        arena_ledger_charge((CettaArenaLedgerTag)a->ledger_tag, 0,
                            -(int64_t)block->capacity);
        arena_retire_block(block, a->recycle_blocks);
    }
    arena_runtime_note_usage(a);
    arena_runtime_note_spare(a);
}

void arena_reset(Arena *a, ArenaMark mark) {
    if (!a) return;
    arena_invalidate_allocations(a, CETTA_GSLT_LIFETIME_ARENA_RESET);
    arena_frame_identity_set_truncate(a, mark.frame_identity_len);
    arena_run_finalizers_until(a, mark.finalizers);
    ArenaBlock *recycled = NULL;
    while (a->head && a->head != mark.head) {
        ArenaBlock *next = a->head->next;
        a->head->used = 0;
        a->head->next = recycled;
        recycled = a->head;
        a->head = next;
    }
    arena_push_spare_list(a, recycled);
    if (a->head && a->head->used > mark.used) {
        a->head->used = mark.used;
    }
    a->live_bytes = mark.live_bytes;
    a->external_bytes = arena_mark_external_bytes(a, mark);
    arena_runtime_note_usage(a);
}

/* ── Hash-Consing ──────────────────────────────────────────────────────── */

_Thread_local HashConsTable *g_hashcons = NULL;

typedef enum {
    ATOM_COMPARE_STRUCTURAL,
    ATOM_COMPARE_VALUE,
    ATOM_COMPARE_INTERN
} AtomCompareMode;

static bool atom_compare_driver(Atom *left, Atom *right,
                                AtomCompareMode mode, bool all_numbers);

static bool atom_is_hash_stable(const Atom *atom);
static uint32_t atom_number_value_hash(const Atom *atom);

static uint32_t atom_hash_leaf_compute(Atom *a) {
    if (!a) return 0;
    /* A rational term hashes by its unfolding, as atom_eq compares it. */
    if (term_graph_view_hash && atom_structural_has_rational(a))
        return term_graph_view_hash(a, atom_hash);
    uint32_t h = 5381;
    h = ((h << 5) + h) ^ (uint32_t)a->kind;
    switch (a->kind) {
    case ATOM_SYMBOL: {
        uint64_t sh = symbol_hash_value(g_symbols, a->sym_id);
        h = ((h << 5) + h) ^ (uint32_t)(sh & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(sh >> 32);
        break;
    }
    case ATOM_VAR:
        /* A variable without an epoch hashes as when ids were one word:
         * its base, then the base's high half with the epoch folded in. */
        h = ((h << 5) + h) ^ (uint32_t)(a->var_id.base & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)((a->var_id.base >> 32) ^
                                        a->var_id.epoch.word ^
                                        (a->var_id.epoch.word >> 32));
        break;
    case ATOM_GROUNDED:
        h = ((h << 5) + h) ^ (uint32_t)a->ground.gkind;
        switch (a->ground.gkind) {
        case GV_INT: h = ((h << 5) + h) ^ (uint32_t)(a->ground.ival & 0xFFFFFFFF); break;
        case GV_FLOAT: { union { double d; uint64_t u; } conv; conv.d = a->ground.fval;
                         h = ((h << 5) + h) ^ (uint32_t)(conv.u & 0xFFFFFFFF); break; }
        case GV_BOOL: h = ((h << 5) + h) ^ (uint32_t)a->ground.bval; break;
        case GV_STRING: {
            const char *p = a->ground.sval;
            for (uint32_t i = 0; i < a->ground.slen; i++)
                h = ((h << 5) + h) ^ (uint32_t)p[i];
            break;
        }
        case GV_BIGINT: {
            for (const char *p = atom_bigint_cstr(a); p && *p; p++)
                h = ((h << 5) + h) ^ (uint32_t)*p;
            break;
        }
        case GV_RATIONAL: {
            for (const char *p = atom_rational_cstr(a); p && *p; p++)
                h = ((h << 5) + h) ^ (uint32_t)*p;
            break;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            break; /* mutable/contextual — don't hash-cons */
        case GV_SEAL: {
            /* Identity equality: copies of one object hash alike, distinct
             * objects (even of equal payload) usually differ. */
            uint64_t key = atom_seal_identity(a);
            h = ((h << 5) + h) ^ (uint32_t)(key & 0xFFFFFFFFu);
            h = ((h << 5) + h) ^ (uint32_t)(key >> 32);
            break;
        }
        case GV_STRUCTURAL: {
            uint64_t key = atom_structural_hash(a);
            h = ((h << 5) + h) ^ (uint32_t)key;
            h = ((h << 5) + h) ^ (uint32_t)(key >> 32);
            break;
        }
        case GV_INTERNAL_TAG:
            h = ((h << 5) + h) ^ (uint32_t)(a->ground.ival & 0xFFFFFFFF);
            break;
        }
        break;
    case ATOM_EXPR:
        break;
    }
    return h;
}

typedef struct {
    Atom *node;
    CettaExprIndex next;
    uint32_t hash;
} AtomHashFrame;

static uint32_t atom_hash_expr_seed(const Atom *atom, bool value_mode) {
    uint32_t h = ((5381u << 5) + 5381u) ^ (uint32_t)ATOM_EXPR;
    h = ((h << 5) + h) ^ (uint32_t)atom->expr.len;
    if (!value_mode)
        h = ((h << 5) + h) ^ (uint32_t)(atom->expr.len >> 32);
    return h;
}

static inline void atom_hash_cache_store(Atom *atom, uint32_t hash) {
    __atomic_store_n(&atom->hash_cache, hash, __ATOMIC_RELAXED);
}

/* Return a result for a leaf or an already indexed expression.  The root of
 * atom_hash_compute deliberately ignores its cache: closed-arena admission
 * uses that recomputation to check that the cached hash is still truthful. */
static inline __attribute__((always_inline)) bool atom_hash_resolved(
    Atom *atom, bool value_mode, bool ignore_cache, uint32_t *result) {
    if (!atom) {
        *result = 0u;
        return true;
    }
    if (value_mode) {
        if (atom_is_number(atom)) {
            *result = atom_number_value_hash(atom);
            return true;
        }
        if (term_graph_view_hash && atom_structural_has_rational(atom)) {
            *result = term_graph_view_hash(atom, atom_value_hash);
            return true;
        }
        if (atom->kind != ATOM_EXPR) {
            *result = atom_hash(atom);
            return true;
        }
        return false;
    }
    if (!ignore_cache) {
        uint32_t cached = atom_hash_cache_load(atom);
        if (cached != 0u) {
            *result = cached;
            return true;
        }
    }
    if (atom->kind == ATOM_EXPR &&
        !(term_graph_view_hash && atom_structural_has_rational(atom)))
        return false;
    *result = ATOM_HASH_SPELL(atom_hash_leaf_compute(atom));
    if (!ignore_cache && atom_is_hash_stable(atom))
        atom_hash_cache_store(atom, *result);
    return true;
}

static uint32_t atom_hash_fold(Atom *root, bool value_mode,
                               bool ignore_root_cache) {
    uint32_t result = 0u;
    if (atom_hash_resolved(root, value_mode, ignore_root_cache, &result))
        return result;
    AtomHashFrame inline_frames[32];
    AtomHashFrame *frames = inline_frames;
    size_t len = 1u, capacity = 32u;
    frames[0] = (AtomHashFrame){root, 0u,
                               atom_hash_expr_seed(root, value_mode)};
    while (len) {
        AtomHashFrame *frame = &frames[len - 1u];
        if (frame->next < frame->node->expr.len) {
            Atom *child = frame->node->expr.elems[frame->next];
            if (atom_hash_resolved(child, value_mode, false, &result)) {
                frame->hash = ((frame->hash << 5) + frame->hash) ^ result;
                frame->next++;
                continue;
            }
            if (len == capacity) {
                size_t grown = capacity * 2u;
                if (grown < capacity ||
                    grown > SIZE_MAX / sizeof(*frames))
                    cetta_oom(SIZE_MAX);
                if (frames == inline_frames) {
                    frames = cetta_malloc(grown * sizeof(*frames));
                    memcpy(frames, inline_frames, len * sizeof(*frames));
                } else {
                    frames = cetta_realloc(frames, grown * sizeof(*frames));
                }
                capacity = grown;
            }
            frames[len++] = (AtomHashFrame){
                child, 0u, atom_hash_expr_seed(child, value_mode)};
            continue;
        }
        result = ATOM_HASH_SPELL(atom_hash_expr_finish(frame->hash));
        if (!value_mode && len > 1u && atom_is_hash_stable(frame->node))
            atom_hash_cache_store(frame->node, result);
        len--;
        if (len) {
            frame = &frames[len - 1u];
            frame->hash = ((frame->hash << 5) + frame->hash) ^ result;
            frame->next++;
        }
    }
    if (frames != inline_frames)
        free(frames);
    return result;
}

static uint32_t atom_hash_compute(Atom *atom) {
    if (!atom)
        return 0u;
    if (atom->kind != ATOM_EXPR &&
        !(term_graph_view_hash && atom_structural_has_rational(atom)))
        return ATOM_HASH_SPELL(atom_hash_leaf_compute(atom));
    if (atom->kind == ATOM_EXPR &&
        !(term_graph_view_hash && atom_structural_has_rational(atom))) {
        uint32_t hash = atom_hash_expr_seed(atom, false);
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            uint32_t child_hash;
            if (!atom_hash_resolved(atom->expr.elems[i], false, false,
                                    &child_hash))
                return atom_hash_fold(atom, false, true);
            hash = ((hash << 5) + hash) ^ child_hash;
        }
        return ATOM_HASH_SPELL(atom_hash_expr_finish(hash));
    }
    return atom_hash_fold(atom, false, true);
}

uint32_t atom_hash(Atom *a) {
    if (!a)
        return 0;
    uint32_t h = atom_hash_cache_load(a);
    if (h != 0u)
        return h;
    h = atom_hash_compute(a);
    if (atom_is_hash_stable(a))
        atom_hash_cache_store(a, h);
    return h;
}

bool atom_eq_fast(Atom *a, Atom *b) {
    if (a == b) return true;
    /* If both are hash-consed, pointer equality is authoritative for immutable atoms */
    return atom_eq(a, b);
}

static void hashcons_init_capacity(HashConsTable *hc, uint32_t capacity) {
    hc->frame_identities = (CettaFrameIdentityScope){0};
    hc->size = capacity;
    hc->used = 0;
    hc->symbol_cache = NULL;
    hc->symbol_cache_size = 0u;
    hc->small_int_cache = NULL;
    hc->bool_cache[0] = NULL;
    hc->bool_cache[1] = NULL;
    hc->lookup_count = 0;
    hc->lookup_probes = 0;
    hc->maximum_lookup_probe = 0;
    hc->table = calloc(hc->size, sizeof(Atom *));
    if (!hc->table)
        cetta_oom(sizeof(Atom *) * hc->size);
}

void hashcons_init(HashConsTable *hc) {
    hashcons_init_capacity(hc, HASHCONS_TABLE_SIZE);
}

void hashcons_init_compact(HashConsTable *hc) {
    hashcons_init_capacity(hc, 64u);
}

uint64_t hashcons_reclamation_epoch(void) {
    return atomic_load_explicit(&g_hashcons_reclamation_epoch,
                                memory_order_acquire);
}

static void hashcons_note_reclamation(void) {
    uint64_t epoch = hashcons_reclamation_epoch();
    while (epoch != UINT64_MAX &&
           !atomic_compare_exchange_weak_explicit(
               &g_hashcons_reclamation_epoch, &epoch, epoch + 1u,
               memory_order_acq_rel, memory_order_acquire)) {
    }
}

void hashcons_free(HashConsTable *hc) {
    if (!hc || !hc->table) return;
    /* Invalidate borrowed pointer keys before allocations can be reused. */
    hashcons_note_reclamation();
    for (uint32_t i = 0; i < hc->size; i++) {
        Atom *atom = hc->table[i];
        if (!atom) continue;
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_STRING)
            free((void *)atom->ground.sval);
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_BIGINT)
            cetta_bigint_free_owned(atom->ground.bigint);
        if (atom->kind == ATOM_GROUNDED &&
            atom->ground.gkind == GV_RATIONAL)
            cetta_rational_free_owned(atom->ground.rational);
        if (atom->kind == ATOM_EXPR)
            free(atom->expr.elems);
        free(atom);
    }
    free(hc->table);
    free(hc->symbol_cache);
    free(hc->small_int_cache);
    cetta_frame_identity_scope_clear(&hc->frame_identities);
    hc->table = NULL;
    hc->size = hc->used = 0;
    hc->symbol_cache = NULL;
    hc->symbol_cache_size = 0u;
    hc->small_int_cache = NULL;
    hc->bool_cache[0] = NULL;
    hc->bool_cache[1] = NULL;
    hc->lookup_count = 0;
    hc->lookup_probes = 0;
    hc->maximum_lookup_probe = 0;
}

static bool atom_is_hash_stable(const Atom *atom) {
    return atom && (atom->flags & ATOM_FLAG_HASH_STABLE) != 0;
}

static bool atom_can_hashcons(const Atom *atom) {
    return atom && (atom->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) != 0;
}

static uint32_t atom_hash_flags_for_eligible_leaf(void);
static uint32_t atom_flags_for_grounded_kind(GroundedKind gkind);
static uint32_t atom_flags_for_internal_tag(int64_t tag);
static uint32_t atom_structural_facts_for_internal_tag(int64_t tag);
static uint32_t atom_flags_for_symbol_id(SymbolId sym_id);
static inline __attribute__((always_inline)) uint32_t
atom_flags_from_children(uint32_t arena_id, uint32_t older_id, Atom **elems,
                         CettaExprLen len, uint32_t *structural_facts_out);
static VarId atom_single_variable_id_from_children(
    Atom **elems, CettaExprLen len);

/*
 * A hash-cons entry owns its root allocation but retains expression children
 * and variable name keys by pointer.  Admit a graph only when every retained
 * pointer is already globally owned and transitively closed.  Recomputing the
 * expression summary at the publication identity also prevents a stale or
 * hand-built root header from laundering an inadmissible graph.
 */
static bool atom_hashcons_graph_admitted(const Atom *atom) {
    if (!atom_can_hashcons(atom))
        return false;
    uint32_t expected = 0u;
    uint32_t expected_structural_facts = ATOM_STRUCTURAL_FACTS_VALID;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        expected = atom_flags_for_symbol_id(atom->sym_id);
        expected_structural_facts |= ATOM_STRUCTURAL_SYMBOLIC;
        break;
    case ATOM_VAR:
        if (var_id_is_none(atom->var_id) ||
            (atom->name_key &&
             (atom->name_key->arena_id != 0u ||
              (atom->name_key->flags & ATOM_FLAG_ARENA_CLOSED) == 0u))) {
            return false;
        }
        expected = ATOM_FLAG_HAS_VARS |
                   atom_hash_flags_for_eligible_leaf() |
                   atom_var_bloom_for_id(atom->var_id) |
                   (atom_var_id_is_private_variant(atom->var_id)
                        ? ATOM_FLAG_HAS_PRIVATE_VARIANT_VAR : 0u);
        break;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INTERNAL_TAG) {
            expected = atom_flags_for_internal_tag(atom->ground.ival);
            expected_structural_facts =
                atom_structural_facts_for_internal_tag(atom->ground.ival);
            break;
        }
        expected = atom_flags_for_grounded_kind(atom->ground.gkind);
        /* Interning copies a string's bytes into independent global storage;
         * its arena-retention representation is not an admissibility change. */
        if (atom->ground.gkind == GV_STRING)
            expected_structural_facts |= atom->structural_facts &
                ATOM_STRUCTURAL_STRING_STORAGE;
        if (atom->ground.gkind == GV_FLOAT && isnan(atom->ground.fval))
            expected_structural_facts |= ATOM_STRUCTURAL_HAS_NAN;
        break;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems ? atom->expr.elems[i] : NULL;
            if (!child || child->arena_id != 0u ||
                (child->flags & ATOM_FLAG_ARENA_CLOSED) == 0u) {
                return false;
            }
        }
        expected = atom_flags_from_children(
            0u, 0u, atom->expr.elems, atom->expr.len,
            &expected_structural_facts);
        if (!var_id_eq(atom->var_id, atom_single_variable_id_from_children(
                atom->expr.elems, atom->expr.len)))
            return false;
        break;
    }
    if (atom->flags != expected)
        return false;
    if (atom->structural_facts != expected_structural_facts)
        return false;
    uint32_t cached = atom_hash_cache_load(atom);
    return cached == 0u || cached == atom_hash_compute((Atom *)atom);
}

/*
 * atom_hash remains the compact cross-substrate hash contract.  The hash-cons
 * table needs an independent index hash because distinct recursive term
 * families can share that 32-bit value.  Eligible expression children are
 * already immutable global hash-cons entries (enforced at publication) in
 * this table's ownership domain or an outliving one, so their pointers remain
 * stable canonical ids.  Mixing those ids gives O(arity) lookup without
 * recursively re-hashing a deep term.  Exact atom_eq remains authoritative.
 */
static inline uint64_t hashcons_index_mix(uint64_t h, uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    h ^= value;
    h = ((h << 27) | (h >> 37)) * UINT64_C(5) +
        UINT64_C(0x52dce729);
    return h;
}

static uint64_t hashcons_index_mix_span(
    uint64_t h, const uint8_t *bytes, size_t len) {
    uint64_t span = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; i++) {
        span ^= bytes[i];
        span *= UINT64_C(1099511628211);
    }
    h = hashcons_index_mix(h, len);
    return hashcons_index_mix(h, span);
}

static inline uint64_t hashcons_index_finalize(uint64_t h) {
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    h *= UINT64_C(0xc4ceb9fe1a85ec53);
    h ^= h >> 33;
    return h;
}

static uint64_t hashcons_slot_hash(Atom *atom) {
    uint64_t h = UINT64_C(0x243f6a8885a308d3);
    h = hashcons_index_mix(h, atom->kind);
    h = hashcons_index_mix(h, atom_hash(atom));
    switch (atom->kind) {
    case ATOM_SYMBOL:
        h = hashcons_index_mix(h, atom->sym_id);
        break;
    case ATOM_VAR:
        h = hashcons_index_mix(h, var_id_mix(atom->var_id));
        h = hashcons_index_mix(h, atom->sym_id);
        h = hashcons_index_mix(
            h, (uint64_t)(uintptr_t)atom->name_key);
        break;
    case ATOM_GROUNDED:
        h = hashcons_index_mix(h, atom->ground.gkind);
        switch (atom->ground.gkind) {
        case GV_INT:
            h = hashcons_index_mix(h, (uint64_t)atom->ground.ival);
            break;
        case GV_FLOAT: {
            uint64_t bits = 0;
            memcpy(&bits, &atom->ground.fval, sizeof(bits));
            h = hashcons_index_mix(h, bits);
            break;
        }
        case GV_BOOL:
            h = hashcons_index_mix(
                h, atom->ground.bval ? 1u : 0u);
            break;
        case GV_STRING:
            h = hashcons_index_mix_span(
                h, (const uint8_t *)atom->ground.sval,
                atom->ground.slen);
            break;
        case GV_BIGINT: {
            const char *text = atom_bigint_cstr(atom);
            h = hashcons_index_mix_span(
                h, (const uint8_t *)text, strlen(text));
            break;
        }
        case GV_RATIONAL: {
            const char *text = atom_rational_cstr(atom);
            h = hashcons_index_mix_span(
                h, (const uint8_t *)text, strlen(text));
            break;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
        case GV_STRUCTURAL:
        case GV_SEAL:
            h = hashcons_index_mix(
                h, (uint64_t)(uintptr_t)atom->ground.ptr);
            break;
        }
        break;
    case ATOM_EXPR:
        h = hashcons_index_mix(h, atom->expr.len);
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            h = hashcons_index_mix(
                h, (uint64_t)(uintptr_t)atom->expr.elems[i]);
        }
        break;
    }
    return hashcons_index_finalize(h);
}

typedef struct {
    Atom *node;
    CettaExprIndex next;
    uint64_t hash;
} AtomSpreadFrame;

typedef struct {
    const Atom *node;
    uint64_t hash;
} AtomSpreadMemoEntry;

/* Only nested expressions need the map; ordinary flat keys allocate none. */
static AtomSpreadMemoEntry *atom_spread_memo_slot(
        AtomSpreadMemoEntry *entries, size_t capacity, const Atom *node) {
    size_t index = (size_t)hashcons_index_finalize((uintptr_t)node) & (capacity - 1u);
    while (entries[index].node && entries[index].node != node)
        index = (index + 1u) & (capacity - 1u);
    return &entries[index];
}

static bool atom_spread_infinite_node(const Atom *atom, void *context) {
    (void)context;
    const CettaTermGraphRef *ref = atom_term_graph_ref(atom);
    return ref && term_graph_infinite &&
        term_graph_infinite(ref->graph, ref->node);
}

bool atom_spread_hash(Atom *atom, uint64_t *out) {
    if (!atom || !out ||
        !(atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) ||
        atom_has_identity_grounded(atom))
        return false;
    const uint64_t seed = UINT64_C(0x243f6a8885a308d3);
    if (atom_structural_has_rational(atom)) {
        /* An infinite unfolding cannot equal a finite key. Keep its existing
         * bisimulation hash, including open forms; finite graph views decline
         * because their ordinary representatives use the finite-tree fold. */
        if (!atom_graph_any(atom, atom_spread_infinite_node, NULL))
            return false;
        *out = hashcons_index_finalize(hashcons_index_mix(seed, atom_hash(atom)));
        return true;
    }
    if (atom->kind != ATOM_EXPR) {
        *out = hashcons_index_finalize(
            hashcons_index_mix(seed, atom_hash(atom)));
        return true;
    }
    AtomSpreadFrame inline_frames[32];
    AtomSpreadFrame *frames = inline_frames;
    size_t cap = sizeof(inline_frames) / sizeof(inline_frames[0]);
    size_t len = 0u;
    AtomSpreadMemoEntry inline_memo[32];
    AtomSpreadMemoEntry *memo = NULL;
    size_t memo_cap = 0u, memo_len = 0u;
    frames[len++] = (AtomSpreadFrame){
        atom, 0u, hashcons_index_mix(seed, atom->expr.len)};
    for (;;) {
        AtomSpreadFrame *top = &frames[len - 1u];
        if (top->next < top->node->expr.len) {
            Atom *child = top->node->expr.elems[top->next++];
            if (child->kind != ATOM_EXPR) {
                top->hash = hashcons_index_mix(top->hash, atom_hash(child));
                continue;
            }
            if (!memo) {
                memset(inline_memo, 0, sizeof(inline_memo));
                memo = inline_memo;
                memo_cap = sizeof(inline_memo) / sizeof(inline_memo[0]);
            }
            AtomSpreadMemoEntry *known = atom_spread_memo_slot(memo, memo_cap, child);
            if (known->node) {
                top->hash = hashcons_index_mix(top->hash, known->hash);
                continue;
            }
            if (len == cap) {
                size_t grown = cap * 2u;
                if (frames == inline_frames) {
                    frames = cetta_malloc(sizeof(*frames) * grown);
                    memcpy(frames, inline_frames, sizeof(inline_frames));
                } else {
                    frames = cetta_realloc(frames, sizeof(*frames) * grown);
                }
                cap = grown;
            }
            frames[len++] = (AtomSpreadFrame){
                child, 0u, hashcons_index_mix(seed, child->expr.len)};
            continue;
        }
        uint64_t done = hashcons_index_finalize(top->hash);
        if (--len == 0u) {
            if (frames != inline_frames)
                free(frames);
            if (memo && memo != inline_memo)
                free(memo);
            *out = done;
            return true;
        }
        if ((memo_len + 1u) * 2u >= memo_cap) {
            size_t capacity = memo_cap * 2u;
            if (capacity <= memo_cap || capacity > SIZE_MAX / sizeof(*memo))
                cetta_oom(SIZE_MAX);
            AtomSpreadMemoEntry *grown = cetta_malloc(capacity * sizeof(*grown));
            memset(grown, 0, capacity * sizeof(*grown));
            for (size_t i = 0u; i < memo_cap; i++)
                if (memo[i].node)
                    *atom_spread_memo_slot(grown, capacity, memo[i].node) = memo[i];
            if (memo != inline_memo)
                free(memo);
            memo = grown;
            memo_cap = capacity;
        }
        *atom_spread_memo_slot(memo, memo_cap, top->node) =
            (AtomSpreadMemoEntry){top->node, done};
        memo_len++;
        frames[len - 1u].hash = hashcons_index_mix(frames[len - 1u].hash, done);
    }
}

/* Interning keeps every distinct term: a float by its bits, so 0.0 and -0.0
 * stay two atoms, and never a numeric promotion across kinds; a variable by
 * its id, its spelling and its structural name, so each keeps its own name.
 * Decide only comparisons that cannot invoke an opaque service: a flat
 * expression may retry through the full driver without replaying a callback.
 * Negative means the full comparison is needed. */
static inline int atom_intern_leaf_compare(Atom *a, Atom *b) {
    if (a == b)
        return 1;
    if (!a || !b || a->kind != b->kind)
        return 0;
    switch (a->kind) {
    case ATOM_SYMBOL:
        return a->sym_id == b->sym_id;
    case ATOM_VAR:
        return var_id_eq(a->var_id, b->var_id) && a->sym_id == b->sym_id &&
               a->name_key == b->name_key;
    case ATOM_GROUNDED:
        if (a->ground.gkind != b->ground.gkind)
            return 0;
        if (a->ground.gkind == GV_INT)
            return a->ground.ival == b->ground.ival;
        if (a->ground.gkind == GV_FLOAT)
            return memcmp(&a->ground.fval, &b->ground.fval,
                          sizeof(a->ground.fval)) == 0;
        break;
    case ATOM_EXPR:
        break;
    }
    return -1;
}

static bool atom_intern_identical(Atom *a, Atom *b) {
    int leaf = atom_intern_leaf_compare(a, b);
    if (leaf >= 0)
        return leaf != 0;
    if (a->kind != ATOM_EXPR)
        return atom_eq(a, b);
    if (a->expr.len != b->expr.len)
        return false;
    for (CettaExprIndex i = 0u; i < a->expr.len; i++) {
        leaf = atom_intern_leaf_compare(a->expr.elems[i], b->expr.elems[i]);
        if (leaf < 0)
            return atom_compare_driver(a, b, ATOM_COMPARE_INTERN, false);
        if (!leaf)
            return false;
    }
    return true;
}

static uint32_t hashcons_find_slot(HashConsTable *hc, Atom *atom, bool *found) {
    uint32_t h = (uint32_t)(hashcons_slot_hash(atom) % hc->size);
    hc->lookup_count++;
    for (uint32_t i = 0; i < hc->size; i++) {
        hc->lookup_probes++;
        if ((uint64_t)i + 1u > hc->maximum_lookup_probe)
            hc->maximum_lookup_probe = (uint64_t)i + 1u;
        uint32_t idx = (h + i) % hc->size;
        if (!hc->table[idx]) {
            *found = false;
            return idx;
        }
        if (atom_intern_identical(hc->table[idx], atom)) {
            *found = true;
            return idx;
        }
    }
    *found = false;
    return hc->size;
}

static Atom *hashcons_leaf_cache_get(HashConsTable *hc, const Atom *atom) {
    Atom *cached = NULL;
    if (!hc || !atom)
        return NULL;
    if (atom->kind == ATOM_SYMBOL) {
        if (atom->sym_id < hc->symbol_cache_size)
            cached = hc->symbol_cache[atom->sym_id];
    } else if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_INT &&
            atom->ground.ival >= HASHCONS_SMALL_INT_MIN &&
            atom->ground.ival <= HASHCONS_SMALL_INT_MAX &&
            hc->small_int_cache) {
            cached = hc->small_int_cache[
                (size_t)(atom->ground.ival - HASHCONS_SMALL_INT_MIN)];
        } else if (atom->ground.gkind == GV_BOOL) {
            cached = hc->bool_cache[atom->ground.bval ? 1u : 0u];
        }
    }
    if (cached)
        hc->lookup_count++;
    return cached;
}

static void hashcons_leaf_cache_store(HashConsTable *hc, Atom *atom) {
    if (!hc || !atom)
        return;
    if (atom->kind == ATOM_SYMBOL) {
        if (atom->sym_id >= hc->symbol_cache_size) {
            uint32_t next = hc->symbol_cache_size
                ? hc->symbol_cache_size : 64u;
            while (next <= atom->sym_id) {
                if (next > UINT32_MAX / 2u)
                    return;
                next *= 2u;
            }
            Atom **grown = cetta_realloc(
                hc->symbol_cache, (size_t)next * sizeof(*grown));
            memset(grown + hc->symbol_cache_size, 0,
                   (size_t)(next - hc->symbol_cache_size) * sizeof(*grown));
            hc->symbol_cache = grown;
            hc->symbol_cache_size = next;
        }
        hc->symbol_cache[atom->sym_id] = atom;
    } else if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_INT &&
            atom->ground.ival >= HASHCONS_SMALL_INT_MIN &&
            atom->ground.ival <= HASHCONS_SMALL_INT_MAX) {
            if (!hc->small_int_cache) {
                hc->small_int_cache = cetta_malloc(
                    HASHCONS_SMALL_INT_COUNT *
                    sizeof(*hc->small_int_cache));
                memset(hc->small_int_cache, 0,
                       HASHCONS_SMALL_INT_COUNT *
                       sizeof(*hc->small_int_cache));
            }
            hc->small_int_cache[
                (size_t)(atom->ground.ival - HASHCONS_SMALL_INT_MIN)] = atom;
        } else if (atom->ground.gkind == GV_BOOL) {
            hc->bool_cache[atom->ground.bval ? 1u : 0u] = atom;
        }
    }
}

static void hashcons_grow(HashConsTable *hc) {
    Atom **old_table = hc->table;
    uint32_t old_size = hc->size;
    hc->size *= 2;
    hc->used = 0;
    hc->table = cetta_malloc(sizeof(Atom *) * hc->size);
    memset(hc->table, 0, sizeof(Atom *) * hc->size);
    for (uint32_t i = 0; i < old_size; i++) {
        Atom *atom = old_table[i];
        if (!atom) continue;
        bool found = false;
        uint32_t slot = hashcons_find_slot(hc, atom, &found);
        hc->table[slot] = atom;
        hc->used++;
    }
    free(old_table);
}

static Atom *hashcons_intern_admitted(HashConsTable *hc, Atom *atom);

static Atom *hashcons_alloc_owned(HashConsTable *hc, const Atom *atom) {
    Atom *owned = cetta_malloc(sizeof(Atom));
    atom_header_copy(owned, atom);
    owned->arena_id = 0u;
    if (atom->kind == ATOM_VAR)
        (void)cetta_frame_identity_scope_retain(
            &hc->frame_identities, var_epoch_suffix(atom->var_id));
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING) {
        size_t len = atom->ground.slen;
        char *bytes = malloc(len + 1u);
        if (!bytes)
            cetta_oom(len + 1u);
        memcpy(bytes, atom->ground.sval, len);
        bytes[len] = '\0';
        owned->ground.sval = bytes;
        owned->structural_facts &= ~ATOM_STRUCTURAL_STRING_STORAGE;
    } else if (atom->kind == ATOM_GROUNDED &&
               atom->ground.gkind == GV_BIGINT) {
        owned->ground.bigint = cetta_bigint_clone_owned(atom->ground.bigint);
    } else if (atom->kind == ATOM_GROUNDED &&
               atom->ground.gkind == GV_RATIONAL) {
        owned->ground.rational =
            cetta_rational_clone_owned(atom->ground.rational);
    } else if (atom->kind == ATOM_EXPR) {
        if (atom->expr.len == 0) {
            owned->expr.elems = NULL;
        } else {
            if (!cetta_expr_len_mul_fits_size(atom->expr.len, sizeof(Atom *)))
                cetta_oom(SIZE_MAX);
            owned->expr.elems =
                cetta_malloc((size_t)atom->expr.len * sizeof(Atom *));
            memcpy(owned->expr.elems, atom->expr.elems,
                   (size_t)atom->expr.len * sizeof(Atom *));
        }
    }
    return owned;
}

/* The symbol atom a table published for `sym_id`, counted as the leaf-cache
 * hit interning would record, or NULL. */
static Atom *hashcons_published_symbol(HashConsTable *hc, SymbolId sym_id) {
    if (!hc || sym_id >= hc->symbol_cache_size)
        return NULL;
    Atom *published = hc->symbol_cache[sym_id];
    if (!published)
        return NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_ATTEMPT);
    hc->lookup_count++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_HIT);
    return published;
}

/* The table's representative of `atom`, inserting an owned copy when it has
 * none; NULL when the table cannot hold another.  The input itself is never
 * the answer: constructors pass temporary headers here, and a miss sends
 * them to their own arena instead of publishing the temporary. */
static Atom *hashcons_intern_admitted(HashConsTable *hc, Atom *atom) {
    if (!hc || !atom)
        return NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_ATTEMPT);
    Atom *cached = hashcons_leaf_cache_get(hc, atom);
    if (cached) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_HIT);
        return cached;
    }
    bool found = false;
    uint32_t slot = hashcons_find_slot(hc, atom, &found);
    if (found) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_HIT);
        return hc->table[slot];
    }
    if (slot >= hc->size)
        return NULL;
    Atom *owned = hashcons_alloc_owned(hc, atom);
    hc->table[slot] = owned;
    hc->used++;
    hashcons_leaf_cache_store(hc, owned);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASHCONS_INSERT);
    if (hc->used * 10 > hc->size * 7)
        hashcons_grow(hc);
    return owned;
}

Atom *hashcons_get(HashConsTable *hc, Atom *atom) {
    if (!hc || !atom || !atom_hashcons_graph_admitted(atom))
        return atom;
    /* The caller owns `atom`, so a miss answers with it. */
    Atom *shared = hashcons_intern_admitted(hc, atom);
    return shared ? shared : atom;
}

/* ── Variables / cached literal lookups ────────────────────────────────── */

VarInternTable *g_var_intern = NULL;

#define VAR_INTERN_CHUNK_BITS 14u
#define VAR_INTERN_CHUNK_SIZE (1u << VAR_INTERN_CHUNK_BITS)
#define VAR_INTERN_CHUNK_MASK (VAR_INTERN_CHUNK_SIZE - 1u)
#define VAR_INTERN_CHUNK_COUNT (1u << (32u - VAR_INTERN_CHUNK_BITS))

/* Symbol ids of C string literals, per thread, keyed by the literal's
 * address. A small two-way level answers most lookups; the open
 * addressing level behind it keeps every literal once interned, so two
 * literals the linker happens to place in one direct-mapped slot cost a
 * probe rather than repeated interning. Two alternating literals sharing
 * a slot remain resident together rather than evicting each other. */
#define SYMBOL_LITERAL_DIRECT_SIZE 64u

typedef struct {
    const char *literal;
    SymbolId id;
} SymbolLiteralCacheEntry;

typedef struct {
    const SymbolTable *table;
    uint64_t instance_id;
    SymbolLiteralCacheEntry direct[SYMBOL_LITERAL_DIRECT_SIZE][2];
    SymbolLiteralCacheEntry *entries;
    uint32_t cap;
    uint32_t len;
} SymbolLiteralCache;

static __thread SymbolLiteralCache g_symbol_literal_cache;
static pthread_key_t g_symbol_literal_cache_key;
static pthread_once_t g_symbol_literal_cache_key_once = PTHREAD_ONCE_INIT;

/* Bases are 64-bit words counted from one.  The cursor's all-ones value is
 * the exhausted sentinel; at a billion mints a second it lasts centuries. */
static _Atomic uint64_t g_var_base_counter = 1;
#define VAR_ID_BLOCK_SIZE 4096u

typedef struct {
    uint64_t next;
    uint32_t remaining;
} VarIdBlockCache;

static __thread VarIdBlockCache g_var_base_block_cache = {0};

static bool fresh_var_base_try(uint64_t *base_out) {
    if (!base_out)
        return false;

    if (g_var_base_block_cache.remaining > 0u) {
        *base_out = g_var_base_block_cache.next++;
        g_var_base_block_cache.remaining--;
        return true;
    }

    for (;;) {
        uint64_t start = atomic_load_explicit(
            &g_var_base_counter, memory_order_relaxed);
        uint64_t available;
        uint64_t block_size;
        uint64_t after;

        /* Base zero means "no variable"; all ones is the exhausted cursor. */
        if (start == 0u || start == UINT64_MAX)
            return false;
        available = UINT64_MAX - start;
        block_size = available < VAR_ID_BLOCK_SIZE
            ? available : VAR_ID_BLOCK_SIZE;
        after = start + block_size;
        if (!atomic_compare_exchange_weak_explicit(
                &g_var_base_counter, &start, after,
                memory_order_relaxed, memory_order_relaxed))
            continue;
        g_var_base_block_cache.next = start;
        g_var_base_block_cache.remaining = (uint32_t)block_size;
        return fresh_var_base_try(base_out);
    }
}

bool fresh_var_id_try(VarId *id_out) {
    uint64_t base;

    if (!id_out || !fresh_var_base_try(&base))
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_FRESH_VAR_BASE_MINT);
    *id_out = var_id_from_base(base);
    return true;
}

VarId fresh_var_id(void) {
    VarId id;

    if (fresh_var_id_try(&id))
        return id;
    fputs("fatal: fresh-variable identity space exhausted\n", stderr);
    abort();
}

#ifdef CETTA_TEST_HOOKS
void fresh_var_id_test_reset(uint64_t next_base) {
    atomic_store_explicit(
        &g_var_base_counter, next_base, memory_order_relaxed);
    g_var_base_block_cache.next = 0u;
    g_var_base_block_cache.remaining = 0u;
}

/* Observations for the arena frame-identity ownership tests.  The record
 * count is the number of distinct full identities the arena currently holds,
 * which is the quantity that must scale with distinct identities rather than
 * with variable occurrences. */
uint32_t arena_frame_identity_count_test(const Arena *a) {
    return a && a->frame_identities ? a->frame_identities->len : 0u;
}

void arena_identity_test_start_fresh_at(uint32_t next) {
    pthread_mutex_lock(&g_arena_names.mutex);
    if (next >= g_arena_names.first && next <= g_arena_names.limit)
        g_arena_names.next_fresh = next;
    pthread_mutex_unlock(&g_arena_names.mutex);
}

uint64_t arena_identity_test_stale_releases(void) {
    return atomic_load_explicit(&g_arena_names.stale_releases,
                                memory_order_relaxed);
}

uint32_t arena_frame_identity_capacity_test(const Arena *a) {
    return a && a->frame_identities ? a->frame_identities->cap : 0u;
}

bool arena_frame_identity_owned_test(const Arena *a,
                                     CettaFrameIdentity identity) {
    return arena_frame_identity_set_find(a ? a->frame_identities : NULL,
                                         identity, NULL);
}
#endif

/* Keep externally linkable definitions for already-compiled callers while
 * source users receive the identical inline definitions from atom.h. */
extern inline uint64_t var_base_id(VarId id);
extern inline CettaFrameIdentity var_epoch_suffix(VarId id);
extern inline VarId var_epoch_id(VarId id, CettaFrameIdentity epoch);

struct VarInternChunk {
    struct VarInternChunk *next;
    VarInternEntry entries[VAR_INTERN_CHUNK_SIZE];
};

void var_intern_init(VarInternTable *t) {
    if (!t) return;
    t->owned_chunks = NULL;
    /* Zeroed pages: each slot holds no chunk until one is made, and the
     * table occupies no page it has not used. */
    t->chunks = calloc(VAR_INTERN_CHUNK_COUNT, sizeof(*t->chunks));
    if (!t->chunks)
        cetta_oom(sizeof(*t->chunks) * VAR_INTERN_CHUNK_COUNT);
    if (pthread_mutex_init(&t->write_mutex, NULL) != 0) {
        fprintf(stderr, "fatal: failed to initialize variable intern mutex\n");
        abort();
    }
}

void var_intern_free(VarInternTable *t) {
    if (!t) return;
    while (t->owned_chunks) {
        struct VarInternChunk *chunk = t->owned_chunks;
        t->owned_chunks = chunk->next;
        free(chunk);
    }
    free(t->chunks);
    pthread_mutex_destroy(&t->write_mutex);
    t->chunks = NULL;
}

static VarInternEntry *var_intern_chunk_load(const VarInternTable *t,
                                             SymbolId spelling) {
    if (!t || !t->chunks) return NULL;
    return atomic_load_explicit(&t->chunks[spelling >> VAR_INTERN_CHUNK_BITS],
                                memory_order_acquire);
}

static VarInternEntry *var_intern_ensure_chunk_locked(VarInternTable *t,
                                                      SymbolId spelling) {
    uint32_t chunk_index = spelling >> VAR_INTERN_CHUNK_BITS;
    VarInternEntry *chunk =
        atomic_load_explicit(&t->chunks[chunk_index], memory_order_relaxed);
    if (chunk) return chunk;

    struct VarInternChunk *owner = cetta_malloc(sizeof(*owner));
    chunk = owner->entries;
    for (uint32_t i = 0; i < VAR_INTERN_CHUNK_SIZE; i++) {
        atomic_init(&chunk[i], 0u);
    }
    owner->next = t->owned_chunks;
    t->owned_chunks = owner;
    atomic_store_explicit(&t->chunks[chunk_index], chunk, memory_order_release);
    return chunk;
}

VarId var_intern(VarInternTable *t, SymbolId spelling) {
    VarInternEntry *chunk;
    uint32_t offset;
    uint64_t base;
    VarId out;

    if (!t || spelling == SYMBOL_ID_NONE) return fresh_var_id();

    offset = spelling & VAR_INTERN_CHUNK_MASK;
    chunk = var_intern_chunk_load(t, spelling);
    if (chunk) {
        base = atomic_load_explicit(&chunk[offset], memory_order_acquire);
        if (base != 0u) return var_id_from_base(base);
    }

    pthread_mutex_lock(&t->write_mutex);
    chunk = var_intern_ensure_chunk_locked(t, spelling);
    base = atomic_load_explicit(&chunk[offset], memory_order_relaxed);
    if (base != 0u) {
        out = var_id_from_base(base);
    } else {
        out = fresh_var_id();
        atomic_store_explicit(&chunk[offset], var_base_id(out),
                              memory_order_release);
    }
    pthread_mutex_unlock(&t->write_mutex);
    return out;
}

static void symbol_literal_cache_release(void *entries) {
    free(entries);
}

static void symbol_literal_cache_key_create(void) {
    (void)pthread_key_create(
        &g_symbol_literal_cache_key, symbol_literal_cache_release);
}

static uint32_t symbol_literal_cache_slot(const char *name, uint32_t mask) {
    uint64_t key = (uint64_t)(uintptr_t)name * UINT64_C(0x9e3779b97f4a7c15);
    return (uint32_t)(key >> 32) & mask;
}

static bool symbol_literal_cache_grow(SymbolLiteralCache *cache) {
    uint32_t cap = cache->cap ? cache->cap * 2u : 256u;
    if (cap < cache->cap)
        return false;
    SymbolLiteralCacheEntry *entries = calloc(cap, sizeof(*entries));
    if (!entries)
        return false;
    for (uint32_t i = 0u; i < cache->cap; i++) {
        const char *literal = cache->entries[i].literal;
        if (!literal)
            continue;
        uint32_t slot = symbol_literal_cache_slot(literal, cap - 1u);
        while (entries[slot].literal)
            slot = (slot + 1u) & (cap - 1u);
        entries[slot] = cache->entries[i];
    }
    if (!cache->entries) {
        pthread_once(&g_symbol_literal_cache_key_once,
                     symbol_literal_cache_key_create);
    }
    free(cache->entries);
    cache->entries = entries;
    cache->cap = cap;
    (void)pthread_setspecific(g_symbol_literal_cache_key, entries);
    return true;
}

static SymbolId symbol_cached_literal_slow(const char *name);

static inline SymbolId symbol_cached_literal(const char *name) {
    SymbolLiteralCache *cache = &g_symbol_literal_cache;
    /* Strings have byte alignment; their low address bits distinguish
       nearby literals rather than unused pointer-alignment padding. */
    const SymbolLiteralCacheEntry *direct = cache->direct[
        (uintptr_t)name % SYMBOL_LITERAL_DIRECT_SIZE];
    if (name && cache->table == g_symbols && g_symbols &&
        cache->instance_id == symbol_table_instance_id(g_symbols)) {
        if (direct[0].literal == name)
            return direct[0].id;
        if (direct[1].literal == name)
            return direct[1].id;
    }
    return symbol_cached_literal_slow(name);
}

static __attribute__((noinline)) SymbolId symbol_cached_literal_slow(
        const char *name) {
    if (!g_symbols || !name || !*name) return SYMBOL_ID_NONE;

    SymbolLiteralCache *cache = &g_symbol_literal_cache;
    uint64_t instance_id = symbol_table_instance_id(g_symbols);
    if (cache->table != g_symbols || cache->instance_id != instance_id) {
        memset(cache->direct, 0, sizeof(cache->direct));
        if (cache->entries)
            memset(cache->entries, 0, cache->cap * sizeof(*cache->entries));
        cache->len = 0u;
        cache->table = g_symbols;
        cache->instance_id = instance_id;
    }
    SymbolLiteralCacheEntry *direct = cache->direct[
        (uintptr_t)name % SYMBOL_LITERAL_DIRECT_SIZE];
    if (direct[0].literal == name)
        return direct[0].id;
    if (direct[1].literal == name)
        return direct[1].id;
    if (cache->cap) {
        uint32_t mask = cache->cap - 1u;
        for (uint32_t slot = symbol_literal_cache_slot(name, mask);
             cache->entries[slot].literal; slot = (slot + 1u) & mask) {
            if (cache->entries[slot].literal == name) {
                direct[1] = direct[0];
                direct[0] = cache->entries[slot];
                return direct[0].id;
            }
        }
    }

    SymbolId id = symbol_intern_cstr(g_symbols, name);
    if (id == SYMBOL_ID_NONE)
        return id;
    direct[1] = direct[0];
    direct[0] = (SymbolLiteralCacheEntry){name, id};
    if ((cache->len + 1u) * 2u > cache->cap &&
        !symbol_literal_cache_grow(cache))
        return id;
    uint32_t mask = cache->cap - 1u;
    uint32_t slot = symbol_literal_cache_slot(name, mask);
    while (cache->entries[slot].literal)
        slot = (slot + 1u) & mask;
    cache->entries[slot] = (SymbolLiteralCacheEntry){name, id};
    cache->len++;
    return id;
}

/* ── Arena ──────────────────────────────────────────────────────────────── */

/* A new head block for an allocation that does not fit.  Out of line, so
 * the bump path of arena_alloc stays short whatever claiming involves. */
static __attribute__((noinline)) void arena_grow(Arena *a, size_t size) {
    ArenaBlock *b = arena_claim_block(a, size);
    b->next = a->head;
    a->head = b;
}

void *arena_alloc(Arena *a, size_t size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;
    if (!a->head || a->head->used + size > a->head->capacity)
        arena_grow(a, size);
    void *ptr = a->head->data + a->head->used;
    a->head->used += size;
    a->live_bytes += size;
    arena_runtime_note_alloc(a, size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s) + 1;
    char *dst = arena_alloc(a, len);
    memcpy(dst, s, len);
    return dst;
}

char *cetta_bigint_canonicalize_owned(const char *text) {
    if (!text || !*text)
        return NULL;
    const unsigned char *p = (const unsigned char *)text;
    bool neg = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    if (!isdigit(*p))
        return NULL;
    while (*p == '0')
        p++;
    const unsigned char *digits = p;
    while (isdigit(*p))
        p++;
    if (*p != '\0')
        return NULL;
    if (digits == p) {
        char *zero = cetta_malloc(2);
        zero[0] = '0';
        zero[1] = '\0';
        return zero;
    }
    size_t ndigits = (size_t)(p - digits);
    char *out = cetta_malloc(ndigits + (neg ? 2u : 1u));
    size_t pos = 0;
    if (neg)
        out[pos++] = '-';
    memcpy(out + pos, digits, ndigits);
    out[pos + ndigits] = '\0';
    return out;
}

bool cetta_bigint_text_fits_i64(const char *text, int64_t *out) {
    if (!text)
        return false;
    char *endp = NULL;
    errno = 0;
    long long value = strtoll(text, &endp, 10);
    if (errno != 0 || !endp || *endp != '\0')
        return false;
    if (out)
        *out = (int64_t)value;
    return true;
}

int cetta_bigint_compare_cstr(const char *lhs, const char *rhs) {
    char *lc = cetta_bigint_canonicalize_owned(lhs);
    char *rc = cetta_bigint_canonicalize_owned(rhs);
    if (!lc || !rc) {
        const unsigned char *lp = (const unsigned char *)(lhs ? lhs : "");
        const unsigned char *rp = (const unsigned char *)(rhs ? rhs : "");
        while (*lp && *lp == *rp) {
            lp++;
            rp++;
        }
        int result = (*lp > *rp) - (*lp < *rp);
        free(lc);
        free(rc);
        return result;
    }

    bool lneg = lc[0] == '-';
    bool rneg = rc[0] == '-';
    int result = 0;
    if (lneg != rneg) {
        result = lneg ? -1 : 1;
    } else {
        const char *ld = lneg ? lc + 1 : lc;
        const char *rd = rneg ? rc + 1 : rc;
        size_t llen = strlen(ld);
        size_t rlen = strlen(rd);
        if (llen != rlen) {
            result = llen < rlen ? -1 : 1;
        } else {
            int raw = memcmp(ld, rd, llen);
            result = raw;
            if (result < 0)
                result = -1;
            else if (result > 0)
                result = 1;
        }
        if (lneg)
            result = -result;
    }

    free(lc);
    free(rc);
    return result;
}

static void cetta_bigint_clear_arena(void *ptr) {
    CettaBigInt *big = ptr;
    if (!big)
        return;
    free(atomic_load_explicit(&big->text, memory_order_acquire));
    atomic_store_explicit(&big->text, NULL, memory_order_release);
#if CETTA_BUILD_WITH_GMP
    if (big->has_value) {
        mpz_clear(big->value);
        big->has_value = false;
    }
#endif
}

static CettaBigInt *cetta_bigint_new_arena(Arena *a, const char *canonical) {
    CettaBigInt *big = arena_alloc(a, sizeof(CettaBigInt));
    char *text = strdup(canonical);
    if (!text)
        cetta_oom(strlen(canonical) + 1u);
    arena_account_external_bytes(a, strlen(canonical) + 1u);
    atomic_init(&big->text, text);
#if CETTA_BUILD_WITH_GMP
    mpz_init_set_str(big->value, canonical, 10);
    big->has_value = true;
    arena_account_external_bytes(
        a, mpz_size(big->value) * sizeof(mp_limb_t));
#endif
    arena_register_finalizer(a, cetta_bigint_clear_arena, big);
    return big;
}

#if CETTA_BUILD_WITH_GMP
static CettaBigInt *cetta_bigint_new_arena_from_mpz(
    Arena *a, const mpz_t value) {
    CettaBigInt *big = arena_alloc(a, sizeof(CettaBigInt));
    atomic_init(&big->text, NULL);
    mpz_init_set(big->value, value);
    big->has_value = true;
    arena_account_external_bytes(
        a, mpz_size(big->value) * sizeof(mp_limb_t));
    arena_register_finalizer(a, cetta_bigint_clear_arena, big);
    return big;
}

static CettaBigInt *cetta_bigint_new_arena_take_mpz(
    Arena *a, mpz_t value) {
    CettaBigInt *big = arena_alloc(a, sizeof(CettaBigInt));
    atomic_init(&big->text, NULL);
    mpz_init(big->value);
    mpz_swap(big->value, value);
    big->has_value = true;
    arena_account_external_bytes(
        a, mpz_size(big->value) * sizeof(mp_limb_t));
    arena_register_finalizer(a, cetta_bigint_clear_arena, big);
    return big;
}
#endif

static CettaBigInt *cetta_bigint_new_owned_from_text(const char *canonical) {
    CettaBigInt *big = cetta_malloc(sizeof(CettaBigInt));
    char *text = strdup(canonical);
    if (!text)
        cetta_oom(strlen(canonical) + 1);
    atomic_init(&big->text, text);
#if CETTA_BUILD_WITH_GMP
    mpz_init_set_str(big->value, canonical, 10);
    big->has_value = true;
#endif
    return big;
}

#if CETTA_BUILD_WITH_GMP
static CettaBigInt *cetta_bigint_new_owned_from_mpz(const char *canonical,
                                                   const mpz_t value) {
    CettaBigInt *big = cetta_malloc(sizeof(CettaBigInt));
    char *text = canonical ? strdup(canonical) : NULL;
    if (canonical && !text)
        cetta_oom(strlen(canonical) + 1u);
    atomic_init(&big->text, text);
    mpz_init_set(big->value, value);
    big->has_value = true;
    return big;
}
#endif

static CettaBigInt *cetta_bigint_clone_owned(const CettaBigInt *src) {
    if (!src)
        return NULL;
#if CETTA_BUILD_WITH_GMP
    if (src->has_value) {
        const char *text = atomic_load_explicit(
            &src->text, memory_order_acquire);
        return cetta_bigint_new_owned_from_mpz(text, src->value);
    }
#endif
    const char *text = atomic_load_explicit(
        &src->text, memory_order_acquire);
    return text ? cetta_bigint_new_owned_from_text(text) : NULL;
}

static void cetta_bigint_free_owned(CettaBigInt *big) {
    if (!big)
        return;
#if CETTA_BUILD_WITH_GMP
    if (big->has_value)
        mpz_clear(big->value);
#endif
    free(atomic_load_explicit(&big->text, memory_order_acquire));
    free(big);
}

#if CETTA_BUILD_WITH_GMP
static void gmp_free_string(char *text) {
    if (!text)
        return;
    void (*free_func)(void *, size_t) = NULL;
    mp_get_memory_functions(NULL, NULL, &free_func);
    free_func(text, strlen(text) + 1);
}

static bool mpq_set_canonical_text(mpq_t out, const char *text) {
    const char *slash = text ? strchr(text, '/') : NULL;
    if (!slash || strchr(slash + 1, '/'))
        return false;
    size_t nlen = (size_t)(slash - text);
    size_t dlen = strlen(slash + 1);
    if (nlen == 0 || dlen == 0)
        return false;

    char *num_text = cetta_malloc(nlen + 1);
    char *den_text = cetta_malloc(dlen + 1);
    memcpy(num_text, text, nlen);
    num_text[nlen] = '\0';
    memcpy(den_text, slash + 1, dlen + 1);

    mpz_t num, den;
    mpz_inits(num, den, NULL);
    bool ok = mpz_set_str(num, num_text, 10) == 0 &&
              mpz_set_str(den, den_text, 10) == 0 &&
              mpz_sgn(den) != 0;
    if (ok) {
        mpq_set_num(out, num);
        mpq_set_den(out, den);
        mpq_canonicalize(out);
    }
    mpz_clears(num, den, NULL);
    free(num_text);
    free(den_text);
    return ok;
}
#endif

char *cetta_rational_canonicalize_owned(const char *text) {
#if CETTA_BUILD_WITH_GMP
    if (!text || !*text)
        return NULL;
    mpq_t q;
    mpq_init(q);
    if (!mpq_set_canonical_text(q, text)) {
        mpq_clear(q);
        return NULL;
    }
    char *num = mpz_get_str(NULL, 10, mpq_numref(q));
    char *den = mpz_get_str(NULL, 10, mpq_denref(q));
    size_t nlen = strlen(num);
    size_t dlen = strlen(den);
    char *out = cetta_malloc(nlen + dlen + 2);
    memcpy(out, num, nlen);
    out[nlen] = '/';
    memcpy(out + nlen + 1, den, dlen + 1);
    gmp_free_string(num);
    gmp_free_string(den);
    mpq_clear(q);
    return out;
#else
    (void)text;
    return NULL;
#endif
}

int cetta_rational_compare_cstr(const char *lhs, const char *rhs) {
    char *lc = cetta_rational_canonicalize_owned(lhs);
    char *rc = cetta_rational_canonicalize_owned(rhs);
    if (!lc || !rc) {
        const unsigned char *lp = (const unsigned char *)(lhs ? lhs : "");
        const unsigned char *rp = (const unsigned char *)(rhs ? rhs : "");
        while (*lp && *lp == *rp) {
            lp++;
            rp++;
        }
        int result = (*lp > *rp) - (*lp < *rp);
        free(lc);
        free(rc);
        return result;
    }
    const unsigned char *lp = (const unsigned char *)lc;
    const unsigned char *rp = (const unsigned char *)rc;
    while (*lp && *lp == *rp) {
        lp++;
        rp++;
    }
    int result = (*lp > *rp) - (*lp < *rp);
    free(lc);
    free(rc);
    return result;
}

#if CETTA_BUILD_WITH_GMP
static void cetta_rational_clear_value(void *ptr) {
    CettaRational *rat = ptr;
    if (rat && rat->has_value) {
        mpq_clear(rat->value);
        rat->has_value = false;
    }
}
#endif

static CettaRational *cetta_rational_new_arena(Arena *a, const char *canonical)
    __attribute__((unused));
static CettaRational *cetta_rational_new_arena(Arena *a, const char *canonical) {
    CettaRational *rat = arena_alloc(a, sizeof(CettaRational));
    rat->text = arena_strdup(a, canonical);
#if CETTA_BUILD_WITH_GMP
    mpq_init(rat->value);
    mpq_set_canonical_text(rat->value, canonical);
    rat->has_value = true;
    arena_register_finalizer(a, cetta_rational_clear_value, rat);
#endif
    return rat;
}

static CettaRational *cetta_rational_new_owned_from_text(const char *canonical) {
    CettaRational *rat = cetta_malloc(sizeof(CettaRational));
    rat->text = strdup(canonical);
    if (!rat->text)
        cetta_oom(strlen(canonical) + 1);
#if CETTA_BUILD_WITH_GMP
    mpq_init(rat->value);
    mpq_set_canonical_text(rat->value, canonical);
    rat->has_value = true;
#endif
    return rat;
}

#if CETTA_BUILD_WITH_GMP
static CettaRational *cetta_rational_new_owned_from_mpq(const char *canonical,
                                                       const mpq_t value) {
    CettaRational *rat = cetta_malloc(sizeof(CettaRational));
    rat->text = strdup(canonical);
    if (!rat->text)
        cetta_oom(strlen(canonical) + 1);
    mpq_init(rat->value);
    mpq_set(rat->value, value);
    rat->has_value = true;
    return rat;
}
#endif

static CettaRational *cetta_rational_clone_owned(const CettaRational *src) {
    if (!src)
        return NULL;
#if CETTA_BUILD_WITH_GMP
    if (src->has_value)
        return cetta_rational_new_owned_from_mpq(src->text, src->value);
#endif
    return cetta_rational_new_owned_from_text(src->text);
}

static void cetta_rational_free_owned(CettaRational *rat) {
    if (!rat)
        return;
#if CETTA_BUILD_WITH_GMP
    if (rat->has_value)
        mpq_clear(rat->value);
#endif
    free((void *)rat->text);
    free(rat);
}

/* ── Constructors ───────────────────────────────────────────────────────── */

static bool atom_grounded_default_arena_owned(GroundedKind kind) {
    switch (kind) {
#define CETTA_ARENA_OWNED_GROUNDED_CASE(grounded_kind) \
    case grounded_kind: \
        return true;
        CETTA_GSLT_ARENA_OWNED_GROUNDED_KIND_ROWS(
            CETTA_ARENA_OWNED_GROUNDED_CASE)
#undef CETTA_ARENA_OWNED_GROUNDED_CASE
    default:
        return false;
    }
}

static void arena_symbol_cache_refresh_table(
    ArenaSymbolCache *cache) {
    if (!cache)
        return;
    uint64_t instance = symbol_table_instance_id(g_symbols);
    if (cache->symbol_table_instance == instance)
        return;
    cache->occupied = 0u;
    cache->symbol_table_instance = instance;
}

static uint32_t arena_symbol_cache_slot(SymbolId sym_id) {
    uint32_t mixed = sym_id * UINT32_C(2654435761);
    return mixed >> (32u - ARENA_SYMBOL_CACHE_LOG2_CAPACITY);
}

static ArenaSymbolCache *arena_symbol_cache_ensure(Arena *a) {
    if (!a)
        return NULL;
    if (!a->symbol_cache) {
        ArenaSymbolCache *cache = cetta_malloc(sizeof(*cache));
        memset(cache, 0, sizeof(*cache));
        cache->occupied = 0u;
        cache->symbol_table_instance =
            symbol_table_instance_id(g_symbols);
        a->symbol_cache = cache;
        a->symbol_cache_bytes = arena_symbol_cache_storage_bytes(cache);
        arena_runtime_note_usage(a);
    }
    arena_symbol_cache_refresh_table(a->symbol_cache);
    return a->symbol_cache;
}

static Atom *arena_symbol_cache_get(Arena *a, SymbolId sym_id) {
    if (!a || !a->symbol_cache)
        return NULL;
    ArenaSymbolCache *cache = a->symbol_cache;
    arena_symbol_cache_refresh_table(cache);
    uint32_t slot = arena_symbol_cache_slot(sym_id);
    uint64_t occupied = UINT64_C(1) << slot;
    if ((cache->occupied & occupied) == 0u ||
        (!a->scalar_cache && cache->epochs[slot] != a->reset_epoch))
        return NULL;
    Atom *atom = cache->atoms[slot];
    if (!atom || atom->kind != ATOM_SYMBOL || atom->sym_id != sym_id)
        return NULL;
    return atom;
}

static bool arena_symbol_cache_is_active(const Arena *a) {
    static __thread bool initialized = false;
    static __thread bool enabled = true;
    if (!initialized) {
        const char *setting = getenv("CETTA_ARENA_SYMBOL_CACHE");
        enabled = !(setting && setting[0] == '0' && setting[1] == '\0');
        initialized = true;
    }
    return enabled && a && !a->hashcons &&
        (a->runtime_kind == CETTA_ARENA_RUNTIME_KIND_EVAL ||
         a->runtime_kind == CETTA_ARENA_RUNTIME_KIND_SURVIVOR ||
         a->scalar_cache);
}

static void arena_symbol_cache_store(
    Arena *a, SymbolId sym_id, Atom *atom) {
    ArenaSymbolCache *cache = arena_symbol_cache_ensure(a);
    if (!cache || !atom)
        return;
    uint32_t slot = arena_symbol_cache_slot(sym_id);
    cache->atoms[slot] = atom;
    cache->epochs[slot] = a->reset_epoch;
    cache->occupied |= UINT64_C(1) << slot;
}

static Atom *atom_maybe_hashcons(Arena *a, const Atom *temp) {
    if (!a || !a->hashcons || !atom_hashcons_graph_admitted(temp))
        return NULL;
    if (temp->kind == ATOM_GROUNDED &&
        atom_grounded_default_arena_owned(temp->ground.gkind))
        return NULL;
    return hashcons_intern_admitted(a->hashcons, (Atom *)temp);
}

static uint32_t atom_hash_flags_for_eligible_leaf(void) {
    return ATOM_FLAG_HASH_STABLE | ATOM_FLAG_HASHCONS_ELIGIBLE |
           ATOM_FLAG_ARENA_CLOSED;
}

bool atom_grounded_kind_is_term_stable(GroundedKind gkind) {
    switch (gkind) {
    case GV_INT:
    case GV_FLOAT:
    case GV_BOOL:
    case GV_STRING:
    case GV_BIGINT:
    case GV_RATIONAL:
        return true;
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_FOREIGN:
    case GV_TERM_GRAPH:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
    case GV_INTERNAL_TAG:
    case GV_STRUCTURAL:
    case GV_SEAL:
        return false;
    }
    return false;
}

static uint32_t atom_flags_for_grounded_kind(GroundedKind gkind) {
    uint32_t flags = atom_grounded_kind_is_term_stable(gkind)
        ? atom_hash_flags_for_eligible_leaf() : 0u;
    if (gkind == GV_INTERNAL_TAG)
        flags |= ATOM_FLAG_ARENA_CLOSED;
    if (cetta_gslt_identity_bearing_grounded_kind(gkind))
        flags |= ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                 ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE;
    if (gkind == GV_SEAL)
        flags |= ATOM_FLAG_HAS_IDENTITY_GROUNDED;
    return flags;
}

static uint32_t atom_structural_facts_for_grounded_kind(
        GroundedKind gkind) {
    return ATOM_STRUCTURAL_FACTS_VALID |
           (gkind == GV_INTERNAL_TAG
                ? ATOM_STRUCTURAL_HAS_INTERNAL_TAG : 0u) |
           (gkind == GV_STATE || gkind == GV_TERM_GRAPH
                ? ATOM_STRUCTURAL_HAS_NAN : 0u) |
           (gkind == GV_TERM_GRAPH ? ATOM_STRUCTURAL_HAS_RATIONAL : 0u);
}

/* Lists and nominal callable constructors are immutable structural data.
 * A callable's identity is retained as a child; captures supply their own
 * retention facts. Other machine tags stay out of the hash-cons. */
static uint32_t atom_flags_for_internal_tag(int64_t tag) {
    return cetta_internal_tag_is_term_stable(tag)
        ? atom_hash_flags_for_eligible_leaf()
        : atom_flags_for_grounded_kind(GV_INTERNAL_TAG);
}

static uint32_t atom_structural_facts_for_internal_tag(int64_t tag) {
    return atom_structural_facts_for_grounded_kind(GV_INTERNAL_TAG) |
           (cetta_internal_tag_is_list(tag) ? ATOM_STRUCTURAL_HAS_LIST : 0u) |
           (tag == (int64_t)CETTA_INTERNAL_TAG_LIST_REST
                ? ATOM_STRUCTURAL_HAS_OPEN_LIST : 0u) |
           (tag == (int64_t)CETTA_INTERNAL_TAG_PETTA_OPEN_CONS
                ? ATOM_STRUCTURAL_HAS_LIST_CARRIER : 0u) |
           (cetta_internal_tag_is_callable(tag) ||
            tag == (int64_t)CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND
                ? ATOM_STRUCTURAL_HAS_PETTA_NONLIST : 0u);
}

bool atom_grounded_is_term_stable(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED)
        return false;
    if (atom->ground.gkind == GV_INTERNAL_TAG)
        return cetta_internal_tag_is_term_stable(atom->ground.ival);
    return atom_grounded_kind_is_term_stable(atom->ground.gkind);
}

static uint32_t atom_flags_for_symbol_id(SymbolId sym_id) {
    uint32_t flags = atom_hash_flags_for_eligible_leaf();
    const char *bytes = symbol_bytes(g_symbols, sym_id);
    if (bytes && bytes[0] == '&')
        flags |= ATOM_FLAG_HAS_REGISTRY_REFS;
    if (cetta_gslt_thread_local_resource_symbol(sym_id, &g_builtin_syms))
        flags |= ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE;
    return flags;
}

/* Whether an atom's graph is closed across its own arena's generations:
 * closed in its arena, or closed across that arena and its older one. */
static inline bool atom_generation_closed(const Atom *atom) {
    const uint32_t generation = ATOM_STRUCTURAL_FACTS_VALID |
                                ATOM_STRUCTURAL_GENERATION_CLOSED;
    return (atom->flags & ATOM_FLAG_ARENA_CLOSED) != 0u ||
           (atom->structural_facts & generation) == generation;
}

/* Whether arena `arena_id`, whose older generation is `older_id`, may
 * reference `child`: globally owned and closed, or closed across the
 * generations of the arena or of its older one. */
static inline bool atom_generation_admits(uint32_t arena_id,
                                          uint32_t older_id,
                                          const Atom *child) {
    if (child->arena_id == 0u)
        return (child->flags & ATOM_FLAG_ARENA_CLOSED) != 0u;
    return (child->arena_id == arena_id || child->arena_id == older_id) &&
           atom_generation_closed(child);
}

bool atom_graph_is_closed_for_arena(const Arena *arena,
                                    const Atom *atom) {
    return arena && atom &&
           (atom->flags & ATOM_FLAG_ARENA_CLOSED) != 0u &&
           (atom->arena_id == 0u ||
            atom->arena_id == arena->identity);
}

bool atom_graph_is_owner_closed_for_arena(const Arena *arena,
                                          const Atom *atom) {
    return arena && atom_graph_has_owner_closure(atom) &&
           (atom->arena_id == 0u || atom->arena_id == arena->identity);
}

/* The settled test for a copy's inner loop, where both are present and the
 * arena is initialized, so its identity is not zero. */
static inline bool atom_settled_in(const Arena *arena, const Atom *atom) {
    uint32_t id = atom->arena_id;
    if (id == arena->identity)
        return atom_generation_closed(atom);
    return arena->older_identity != 0u && id == arena->older_identity &&
           atom_generation_closed(atom);
}

bool atom_settled_for_arena(const Arena *arena, const Atom *atom) {
    return arena && atom && arena->identity != 0u &&
           atom_settled_in(arena, atom);
}

bool atom_graph_arena_id_bounds(const Atom *atom,
                                uint32_t *min_id,
                                uint32_t *max_id) {
    if (!atom || !min_id || !max_id)
        return false;
    *min_id = 0u;
    *max_id = 0u;
    const Atom *inline_stack[64];
    const Atom **stack = inline_stack;
    size_t length = 1u;
    size_t capacity = sizeof(inline_stack) / sizeof(inline_stack[0]);
    stack[0] = atom;
    bool complete = true;
    while (complete && length > 0u) {
        const Atom *node = stack[--length];
        if (!node) {
            complete = false;
            break;
        }
        if (node->arena_id != 0u) {
            if (*min_id == 0u || node->arena_id < *min_id)
                *min_id = node->arena_id;
            if (node->arena_id > *max_id)
                *max_id = node->arena_id;
        }
        if (atom_graph_has_owner_closure(node))
            continue;
        size_t additions = 0u;
        if (node->kind == ATOM_EXPR) {
            additions = node->expr.len;
        } else if (node->kind == ATOM_VAR && node->name_key) {
            additions = 1u;
        } else {
            /* A non-closed leaf owns or references storage whose lifetime is
             * not represented by Atom arena identities. */
            complete = false;
            break;
        }
        if (additions > SIZE_MAX - length) {
            complete = false;
            break;
        }
        const size_t needed = length + additions;
        if (needed > capacity) {
            size_t next_capacity = capacity;
            while (next_capacity < needed) {
                if (next_capacity > SIZE_MAX / 2u) {
                    next_capacity = needed;
                    break;
                }
                next_capacity *= 2u;
            }
            if (next_capacity > SIZE_MAX / sizeof(*stack)) {
                complete = false;
                break;
            }
            if (stack == inline_stack) {
                const Atom **grown = cetta_malloc(
                    next_capacity * sizeof(*grown));
                memcpy((void *)grown, inline_stack,
                       length * sizeof(*grown));
                stack = grown;
            } else {
                stack = cetta_realloc(
                    (void *)stack, next_capacity * sizeof(*stack));
            }
            capacity = next_capacity;
        }
        if (node->kind == ATOM_VAR) {
            stack[length++] = node->name_key;
        } else {
            for (CettaExprIndex i = 0u; i < node->expr.len; i++)
                stack[length++] = node->expr.elems[i];
        }
    }
    if (stack != inline_stack)
        free((void *)stack);
    return complete;
}

/* An expression's summary folds its children's flags: the properties it
 * inherits by disjunction, the ones every child must have by conjunction, and
 * arena closure by conjunction over the children closed for its arena.  Its
 * head then adjusts the fold. */
static const uint32_t ATOM_SUMMARY_INHERITED =
    ATOM_FLAG_HAS_VARS |
    ATOM_FLAG_HAS_REGISTRY_REFS |
    ATOM_FLAG_HAS_PRIVATE_VARIANT_VAR |
    ATOM_FLAG_HAS_IDENTITY_GROUNDED |
    ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE |
    ATOM_FLAG_VAR_BLOOM_MASK;
static const uint32_t ATOM_SUMMARY_REQUIRED =
    ATOM_FLAG_HASH_STABLE |
    ATOM_FLAG_HASHCONS_ELIGIBLE;

/* The symbol `resolve-name` of the current symbol table, looked up once per
 * table: every expression's summary asks whether its head is it. */
static __thread struct {
    const SymbolTable *table;
    uint64_t instance_id;
    SymbolId id;
} g_resolve_name_symbol;

static __attribute__((noinline)) SymbolId atom_resolve_name_symbol_slow(void) {
    SymbolId id = symbol_cached_literal("resolve-name");
    g_resolve_name_symbol.table = g_symbols;
    g_resolve_name_symbol.instance_id =
        g_symbols ? symbol_table_instance_id(g_symbols) : 0u;
    g_resolve_name_symbol.id = id;
    return id;
}

static inline SymbolId atom_resolve_name_symbol(void) {
    if (__builtin_expect(g_symbols != NULL &&
                             g_resolve_name_symbol.table == g_symbols &&
                             g_resolve_name_symbol.instance_id ==
                                 symbol_table_instance_id(g_symbols),
                         1))
        return g_resolve_name_symbol.id;
    return atom_resolve_name_symbol_slow();
}

/* Whether `head` adjusts the summary of the expression it heads. */
static bool atom_summary_head_adjusts(Atom *head) {
    return head && head->kind == ATOM_SYMBOL &&
        (head->sym_id == atom_resolve_name_symbol() ||
         head->sym_id == g_builtin_syms.native_handle);
}

/* The summary `flags` of an expression headed by `head`: a name to resolve
 * carries a registry reference, and a native handle is contextual, so never
 * interned. */
static uint32_t atom_summary_head_adjust(Atom *head, uint32_t flags) {
    if (!head || head->kind != ATOM_SYMBOL)
        return flags;
    if (head->sym_id == atom_resolve_name_symbol())
        flags |= ATOM_FLAG_HAS_REGISTRY_REFS;
    if (head->sym_id == g_builtin_syms.native_handle)
        flags &= ~ATOM_FLAG_HASHCONS_ELIGIBLE;
    return flags;
}

/* The summary of an expression's children as a fold in progress: the
 * disjunction and conjunction of their flags and of their facts, whether
 * each is closed for the arena the expression is built in, or admitted by
 * its generations, and whether one is missing. */
typedef struct {
    uint32_t any_flags;
    uint32_t all_flags;
    uint32_t any_facts;
    uint32_t all_facts;
    uint32_t arena_id;
    uint32_t older_id;
    bool closed;
    bool owner_closed;
    bool generation_closed;
    bool missing;
} AtomChildrenSummary;

/* The summary fold, in three parts: begin, add each child, finish.  The
 * inherited and required flag sets are disjoint, and each part of the fold
 * is a plain disjunction or conjunction, so one pass over the children and
 * one combination after it compute what folding them one by one computes.
 * `generational` is a constant at each use: an arena without an older
 * generation folds exactly the one-arena summary and pays nothing for the
 * generation bit. */
static inline __attribute__((always_inline)) void atom_summary_begin_ids(
    AtomChildrenSummary *summary, uint32_t arena_id, uint32_t older_id,
    bool generational) {
    *summary = (AtomChildrenSummary){
        .all_flags = UINT32_MAX,
        .all_facts = UINT32_MAX,
        .arena_id = arena_id,
        .older_id = older_id,
        .closed = true,
        .owner_closed = true,
        /* The generation bit folds like the arena bit, over the arena and
         * its older generation. */
        .generation_closed = generational,
    };
}

static inline __attribute__((always_inline)) void atom_summary_add_ids(
    AtomChildrenSummary *summary, bool generational, const Atom *child) {
    if (!child) {
        summary->missing = true;
        return;
    }
    const uint32_t child_flags = child->flags;
    const uint32_t child_facts = child->structural_facts;
    summary->any_flags |= child_flags;
    summary->all_flags &= child_flags;
    summary->any_facts |= child_facts;
    summary->all_facts &= child_facts;
    if ((child_flags & ATOM_FLAG_ARENA_CLOSED) == 0u ||
        (child->arena_id != 0u && child->arena_id != summary->arena_id)) {
        summary->closed = false;
        if (!atom_graph_has_owner_closure(child) ||
            (child->arena_id != 0u && child->arena_id != summary->arena_id))
            summary->owner_closed = false;
        /* A child closed in this arena is admitted by the generation bit
         * too, so only a child that fails the arena bit is asked. */
        if (generational && summary->generation_closed &&
            !atom_generation_admits(summary->arena_id, summary->older_id,
                                    child))
            summary->generation_closed = false;
    }
}

/* The flags and facts of an expression headed by `head` (NULL for none)
 * whose children's summary is complete. */
static inline __attribute__((always_inline)) uint32_t atom_summary_finish(
    const AtomChildrenSummary *summary, bool generational, Atom *head,
    uint32_t *structural_facts_out) {
    const uint32_t inherited = ATOM_SUMMARY_INHERITED;
    const uint32_t required = ATOM_SUMMARY_REQUIRED;
    uint32_t flags = ATOM_FLAG_HASH_STABLE | ATOM_FLAG_HASHCONS_ELIGIBLE |
                     ATOM_FLAG_ARENA_CLOSED;
    flags |= summary->any_flags & inherited;
    flags &= summary->all_flags | ~required;
    if (!summary->closed)
        flags &= ~ATOM_FLAG_ARENA_CLOSED;
    uint32_t structural_facts =
        (summary->all_facts & ATOM_STRUCTURAL_FACTS_VALID)
        ? ATOM_STRUCTURAL_FACTS_VALID |
              (summary->all_facts & ATOM_STRUCTURAL_SYMBOLIC) |
              (summary->any_facts & ~(ATOM_STRUCTURAL_FACTS_VALID |
                                      ATOM_STRUCTURAL_GENERATION_CLOSED |
                                      ATOM_STRUCTURAL_OWNER_CLOSED |
                                      ATOM_STRUCTURAL_FRONT_SLACK |
                                      ATOM_STRUCTURAL_STRING_STORAGE |
                                      ATOM_STRUCTURAL_SYMBOLIC))
        : 0u;
    bool generation_closed = summary->generation_closed;
    if (summary->missing) {
        flags &= ~(ATOM_FLAG_HASH_STABLE | ATOM_FLAG_HASHCONS_ELIGIBLE |
                   ATOM_FLAG_ARENA_CLOSED);
        structural_facts = 0u;
        generation_closed = false;
    }
    flags = atom_summary_head_adjust(head, flags);
    if (generational && generation_closed &&
        (flags & ATOM_FLAG_ARENA_CLOSED) == 0u &&
        (structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u)
        structural_facts |= ATOM_STRUCTURAL_GENERATION_CLOSED;
    if (!summary->missing && !summary->closed && summary->owner_closed &&
        (structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u)
        structural_facts |= ATOM_STRUCTURAL_OWNER_CLOSED;
    if (structural_facts_out)
        *structural_facts_out = structural_facts;
    return flags;
}

/* The fold of an expression's summary from its children. */
static inline __attribute__((always_inline)) uint32_t atom_flags_fold(
    uint32_t arena_id, uint32_t older_id, bool generational, Atom **elems,
    CettaExprLen len, uint32_t *structural_facts_out) {
    AtomChildrenSummary summary;
    atom_summary_begin_ids(&summary, arena_id, older_id, generational);
    for (CettaExprIndex i = 0; i < len; i++)
        atom_summary_add_ids(&summary, generational, elems ? elems[i] : NULL);
    return atom_summary_finish(&summary, generational,
                               len > 0 && elems ? elems[0] : NULL,
                               structural_facts_out);
}

static inline __attribute__((always_inline)) uint32_t
atom_flags_from_children(uint32_t arena_id, uint32_t older_id, Atom **elems,
                         CettaExprLen len, uint32_t *structural_facts_out) {
    return older_id != 0u
        ? atom_flags_fold(arena_id, older_id, true, elems, len,
                          structural_facts_out)
        : atom_flags_fold(arena_id, 0u, false, elems, len,
                          structural_facts_out);
}

static VarId atom_single_variable_id_from_children(
        Atom **elems, CettaExprLen len) {
    VarId single = VAR_ID_NONE;
    for (CettaExprIndex i = 0u; i < len; i++) {
        const Atom *child = elems ? elems[i] : NULL;
        if (!child || (child->flags & ATOM_FLAG_HAS_VARS) == 0u)
            continue;
        VarId child_single = atom_single_variable_id(child);
        if (var_id_is_none(child_single))
            return VAR_ID_NONE;
        if (var_id_is_none(single))
            single = child_single;
        else if (!var_id_eq(single, child_single))
            return VAR_ID_NONE;
    }
    return single;
}

/* The single variable of an expression whose children's summary is `flags`:
 * none when no child has variables, which the summary already says. */
static inline VarId atom_single_variable_id_of(
        uint32_t flags, Atom **elems, CettaExprLen len) {
    return (flags & ATOM_FLAG_HAS_VARS) != 0u
        ? atom_single_variable_id_from_children(elems, len) : VAR_ID_NONE;
}

/* The never-reset storage of an arena's canonical scalars, or NULL. */
static Arena *arena_scalar_owner(Arena *a) {
    if (!a || !a->scalar_cache || a->hashcons)
        return NULL;
    ArenaSymbolCache *cache = arena_symbol_cache_ensure(a);
    if (!cache)
        return NULL;
    if (!cache->owner_ready) {
        arena_init(&cache->owner);
        arena_set_hashcons(&cache->owner, NULL);
        arena_set_runtime_kind(&cache->owner, a->runtime_kind);
        cache->owner_ready = true;
    }
    return &cache->owner;
}

void atom_init_symbol(Atom *target, SymbolId sym_id) {
    *target = (Atom){
        .kind = ATOM_SYMBOL,
        .flags = atom_flags_for_symbol_id(sym_id),
        .var_id = VAR_ID_NONE,
        .sym_id = sym_id,
        .structural_facts = ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_SYMBOLIC,
    };
}

Atom *atom_symbol_id(Arena *a, SymbolId sym_id) {
    /* A symbol's admission depends on its id alone, so the canonical atom a
     * hash-cons table already published for the id is the one interning
     * would return. */
    if (a && a->hashcons) {
        Atom *published = hashcons_published_symbol(a->hashcons, sym_id);
        if (published)
            return published;
    }
    if (arena_symbol_cache_is_active(a)) {
        Atom *cached = arena_symbol_cache_get(a, sym_id);
        if (cached)
            return cached;
    }
    Atom temp;
    atom_init_symbol(&temp, sym_id);
    temp.arena_id = a->identity;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    /* A canonical symbol goes to the scalar owner only into an empty slot,
     * so a colliding symbol cannot grow the never-reset storage. */
    Arena *owner = NULL;
    if (a->scalar_cache && a->symbol_cache) {
        uint32_t slot = arena_symbol_cache_slot(sym_id);
        if ((a->symbol_cache->occupied & (UINT64_C(1) << slot)) == 0u)
            owner = arena_scalar_owner(a);
    } else if (a->scalar_cache && !a->symbol_cache) {
        owner = arena_scalar_owner(a);
    }
    Arena *home = owner ? owner : a;
    temp.arena_id = home->identity;
    Atom *at = arena_alloc(home, sizeof(Atom));
    *at = temp;
    if (arena_symbol_cache_is_active(a) && (owner || !a->scalar_cache))
        arena_symbol_cache_store(a, sym_id, at);
    return at;
}

/* One atom per symbol of the current table, owned by the table rather than
 * an arena (arena identity 0, as the shared list tags), with its hash fixed
 * when made, so every thread reads it and none writes it.  For symbols that
 * code compiled into a program hands to the terms it builds: those terms may
 * outlive the program. */
Atom *atom_symbol_canonical(SymbolId sym_id) {
    if (!g_symbols || sym_id == SYMBOL_ID_NONE)
        return NULL;
    Atom *atom = symbol_canonical_atom(g_symbols, sym_id);
    if (atom)
        return atom;
    Atom *made = cetta_malloc(sizeof(*made));
    atom_init_symbol(made, sym_id);
    if (atom_is_hash_stable(made))
        made->hash_cache = atom_hash_compute(made);
    atom = symbol_publish_canonical_atom(g_symbols, sym_id, made);
    if (atom != made)
        free(made);
    return atom;
}

static _Atomic(Atom *) g_atom_unit_canonical;

/* The empty expression's one atom for the process, owned by no arena, as
 * atom_symbol_canonical's are owned by the symbol table. */
Atom *atom_unit_canonical(void) {
    Atom *atom = atomic_load_explicit(&g_atom_unit_canonical,
                                      memory_order_acquire);
    if (atom)
        return atom;
    uint32_t structural_facts = 0u;
    uint32_t flags = atom_flags_from_children(0u, 0u, NULL, 0u,
                                              &structural_facts);
    Atom *made = cetta_malloc(sizeof(*made));
    *made = (Atom){
        .kind = ATOM_EXPR,
        .flags = flags,
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = 0u,
        .name_key = NULL,
        .structural_facts = structural_facts,
        .expr = {.elems = NULL, .len = 0u},
    };
    if (atom_is_hash_stable(made))
        made->hash_cache = atom_hash_compute(made);
    if (atomic_compare_exchange_strong_explicit(
            &g_atom_unit_canonical, &atom, made,
            memory_order_acq_rel, memory_order_acquire))
        return made;
    free(made);
    return atom;
}

Atom *atom_symbol(Arena *a, const char *name) {
    return atom_symbol_id(a, symbol_intern_cstr(g_symbols, name));
}

Atom *atom_symbol_literal(Arena *a, const char *name) {
    return atom_symbol_id(a, symbol_cached_literal(name));
}

Atom *atom_var_with_spelling(Arena *a, SymbolId spelling, VarId id) {
    Atom temp = {0};
    temp.kind = ATOM_VAR;
    temp.var_id = var_id_is_none(id) ? fresh_var_id() : id;
    temp.flags = ATOM_FLAG_HAS_VARS | atom_hash_flags_for_eligible_leaf() |
                 atom_var_bloom_for_id(temp.var_id) |
                 (atom_var_id_is_private_variant(temp.var_id)
                      ? ATOM_FLAG_HAS_PRIVATE_VARIANT_VAR
                      : 0u);
    temp.sym_id = spelling;
    temp.arena_id = a->identity;
    temp.name_key = NULL;
    temp.hash_cache = 0;
    temp.structural_facts = ATOM_STRUCTURAL_FACTS_VALID;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    (void)arena_retain_frame_identity(a, var_epoch_suffix(temp.var_id));
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

static Atom *atom_var_with_spelling_and_name_key(
    Arena *a, SymbolId spelling, Atom *name_key, VarId id) {
    if (!a || !name_key) return NULL;
    Atom temp = {0};
    temp.kind = ATOM_VAR;
    temp.var_id = var_id_is_none(id) ? fresh_var_id() : id;
    temp.flags = ATOM_FLAG_HAS_VARS | atom_hash_flags_for_eligible_leaf() |
                 atom_var_bloom_for_id(temp.var_id) |
                 (atom_var_id_is_private_variant(temp.var_id)
                      ? ATOM_FLAG_HAS_PRIVATE_VARIANT_VAR
                      : 0u);
    if (!atom_graph_is_closed_for_arena(a, name_key))
        temp.flags &= ~ATOM_FLAG_ARENA_CLOSED;
    temp.sym_id = spelling;
    temp.arena_id = a->identity;
    temp.name_key = name_key;
    temp.hash_cache = 0;
    temp.structural_facts = ATOM_STRUCTURAL_FACTS_VALID;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    (void)arena_retain_frame_identity(a, var_epoch_suffix(temp.var_id));
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_var_with_name_key(Arena *a, Atom *name_key, VarId id) {
    return atom_var_with_spelling_and_name_key(
        a, SYMBOL_ID_NONE, name_key, id);
}

Atom *atom_var_with_presentation(Arena *a, SymbolId spelling,
                                 Atom *name_key, VarId id) {
    if (!name_key)
        return atom_var_with_spelling(a, spelling, id);
    if (CETTA_STRUCTURAL_NAME_TRANSPORT_MUTATION == 1)
        return atom_var_with_spelling(a, SYMBOL_ID_NONE, id);
    Atom *key = atom_deep_copy(a, name_key);
    return key
        ? atom_var_with_spelling_and_name_key(a, spelling, key, id)
        : NULL;
}

Atom *atom_var_like(Arena *a, Atom *source, VarId id) {
    if (!source || source->kind != ATOM_VAR) return NULL;
    return atom_var_with_presentation(
        a, source->sym_id, source->name_key, id);
}

Atom *atom_var_with_id(Arena *a, const char *name, VarId id) {
    return atom_var_with_spelling(a, symbol_intern_cstr(g_symbols, name), id);
}

Atom *atom_var_with_literal(Arena *a, const char *name, VarId id) {
    return atom_var_with_spelling(a, symbol_cached_literal(name), id);
}

Atom *atom_var(Arena *a, const char *name) {
    SymbolId spelling = symbol_intern_cstr(g_symbols, name);
    VarId id = g_var_intern ? var_intern(g_var_intern, spelling) : fresh_var_id();
    Atom *at = atom_var_with_spelling(a, spelling, id);
    return at;
}

Atom *atom_int(Arena *a, int64_t val) {
    bool cached = a && a->scalar_cache && !a->hashcons &&
        val >= ARENA_INT_CACHE_MIN &&
        val < ARENA_INT_CACHE_MIN + ARENA_INT_CACHE_CAPACITY;
    size_t int_slot = cached ? (size_t)(val - ARENA_INT_CACHE_MIN) : 0u;
    if (cached && a->symbol_cache && a->symbol_cache->ints[int_slot])
        return a->symbol_cache->ints[int_slot];
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = atom_flags_for_grounded_kind(GV_INT);
    temp.var_id = VAR_ID_NONE;
    temp.arena_id = a->identity;
    temp.hash_cache = 0;
    temp.structural_facts =
        atom_structural_facts_for_grounded_kind(GV_INT);
    temp.ground.gkind = GV_INT;
    temp.ground.ival = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Arena *owner = cached ? arena_scalar_owner(a) : NULL;
    Arena *home = owner ? owner : a;
    temp.arena_id = home->identity;
    Atom *at = arena_alloc(home, sizeof(Atom));
    *at = temp;
    if (owner)
        a->symbol_cache->ints[int_slot] = at;
    return at;
}

Atom *atom_native_handle_identifier(Arena *a, int64_t id, void *owner,
                                    void (*retain)(void *),
                                    void (*release)(void *)) {
    return atom_native_handle_identifier_tracked(a, id, owner, retain, release, NULL);
}

Atom *atom_native_handle_identifier_tracked(Arena *a, int64_t id, void *owner,
    void (*retain)(void *), void (*release)(void *), size_t (*footprint)(void *)) {
    if (!a || !owner || !retain || !release)
        return NULL;
    static _Thread_local int arena_holds = -1;
    if (arena_holds < 0) {
        const char *reference = getenv("CETTA_NATIVE_HANDLE_HOLDS_REFERENCE");
        arena_holds = !(reference && reference[0] != '\0' && reference[0] != '0');
    }
    if (arena_holds) {
        bool held = arena_retained_owner_set_find(a->retained_owners, owner, release);
        if (!arena_retain_owner(a, owner, retain, release)) return NULL;
        if (!held && footprint) arena_account_external_bytes(a, footprint(owner));
    } else {
        retain(owner);
        arena_register_finalizer(a, release, owner);
        if (footprint) arena_account_external_bytes(a, footprint(owner));
    }
    NativeHandleIdentifier *value = arena_alloc(a, sizeof(*value));
    *value = (NativeHandleIdentifier){
        .atom = {
            .kind = ATOM_GROUNDED,
            /* Context-local ownership cannot enter a global hash-cons table
             * or a pointer-free term encoding as an ordinary integer. */
            .flags = ATOM_FLAG_HAS_REGISTRY_REFS |
                     ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                     ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE,
            .arena_id = a->identity,
            .structural_facts = ATOM_STRUCTURAL_FACTS_VALID |
                                ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID |
                                ATOM_STRUCTURAL_OWNER_CLOSED,
            .ground = {.gkind = GV_INT, .ival = id},
        },
        .owner = owner,
        .retain = retain,
        .release = release,
        .footprint = footprint,
    };
    return &value->atom;
}

bool atom_native_handle_identifier_matches(const Atom *atom, const void *owner,
                                           void (*retain)(void *),
                                           void (*release)(void *)) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_INT ||
        !(atom->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID))
        return false;
    const NativeHandleIdentifier *value = (const NativeHandleIdentifier *)atom;
    return value->owner == owner && value->retain == retain && value->release == release;
}

Atom *atom_int_copy(Arena *a, const Atom *source) {
    if (!a || !source || source->kind != ATOM_GROUNDED ||
        source->ground.gkind != GV_INT)
        return NULL;
#ifndef CETTA_NATIVE_HANDLE_COPY_DROP_OWNER_MUTATION
    if (source->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) {
        /* Within one arena the source precedes this copy: a reset that
         * invalidates the source also invalidates the later use. Reuse its
         * holder; only an escape to another arena acquires a new holder. */
        if (source->arena_id == a->identity)
            return (Atom *)source;
        const NativeHandleIdentifier *value =
            (const NativeHandleIdentifier *)source;
        return atom_native_handle_identifier_tracked(a, source->ground.ival,
            value->owner, value->retain, value->release, value->footprint);
    }
#endif
    return atom_int(a, source->ground.ival);
}

/* A grounded leaf of this kind in this arena with every field written: the
 * fields it does not name are zero, and name_key in particular is NULL, as on
 * every atom but a variable.  A collection reads that field as its mark and
 * as its forwarding copy. */
static inline Atom atom_grounded_leaf(const Arena *a, GroundedKind gkind) {
    return (Atom){
        .kind = ATOM_GROUNDED,
        .flags = atom_flags_for_grounded_kind(gkind),
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = a->identity,
        .structural_facts = atom_structural_facts_for_grounded_kind(gkind),
        .ground = {.gkind = gkind},
    };
}

Atom *atom_bigint(Arena *a, const char *val) {
    char *canonical = cetta_bigint_canonicalize_owned(val);
    if (!canonical)
        return NULL;
    int64_t small = 0;
    if (cetta_bigint_text_fits_i64(canonical, &small)) {
        free(canonical);
        return atom_int(a, small);
    }

    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = atom_flags_for_grounded_kind(GV_BIGINT);
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.structural_facts =
        atom_structural_facts_for_grounded_kind(GV_BIGINT);
    temp.ground.gkind = GV_BIGINT;
    CettaBigInt temp_big = {0};
    atomic_init(&temp_big.text, canonical);
    temp.ground.bigint = &temp_big;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) {
        free(canonical);
        return shared;
    }
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    at->arena_id = a->identity;
    at->ground.bigint = cetta_bigint_new_arena(a, canonical);
    free(canonical);
    return at;
}

const char *atom_bigint_cstr(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BIGINT || !atom->ground.bigint)
        return NULL;
    CettaBigInt *big = atom->ground.bigint;
    char *text = atomic_load_explicit(&big->text, memory_order_acquire);
#if CETTA_BUILD_WITH_GMP
    if (!text && big->has_value) {
        char *raw = mpz_get_str(NULL, 10, big->value);
        char *materialized = raw ? strdup(raw) : NULL;
        gmp_free_string(raw);
        if (!materialized)
            return NULL;
        char *expected = NULL;
        if (!atomic_compare_exchange_strong_explicit(
                &big->text, &expected, materialized,
                memory_order_release, memory_order_acquire)) {
            free(materialized);
            text = expected;
        } else {
            text = materialized;
        }
    }
#endif
    return text;
}

Atom *atom_bigint_copy(Arena *a, const Atom *atom) {
    if (!a || !atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BIGINT || !atom->ground.bigint)
        return NULL;
#if CETTA_BUILD_WITH_GMP
    if (atom->ground.bigint->has_value)
        return atom_bigint_from_mpz(a, atom->ground.bigint->value);
#endif
    const char *text = atom_bigint_cstr(atom);
    return text ? atom_bigint(a, text) : NULL;
}

#if CETTA_BUILD_WITH_GMP
bool atom_bigint_get_mpz(const Atom *atom, mpz_t out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BIGINT || !atom->ground.bigint)
        return false;
    CettaBigInt *big = atom->ground.bigint;
    if (big->has_value) {
        mpz_set(out, big->value);
        return true;
    }
    const char *text = atom_bigint_cstr(atom);
    return text && mpz_set_str(out, text, 10) == 0;
}

mpz_srcptr atom_bigint_mpz_view(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_BIGINT || !atom->ground.bigint ||
        !atom->ground.bigint->has_value) {
        return NULL;
    }
    return atom->ground.bigint->value;
}

Atom *atom_bigint_from_mpz(Arena *a, const mpz_t value) {
    if (!a)
        return NULL;
    if (mpz_fits_slong_p(value)) {
        long small = mpz_get_si(value);
        int64_t narrowed = (int64_t)small;
        if ((long)narrowed == small)
            return atom_int(a, (int64_t)small);
    }
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_BIGINT);
    /* Arithmetic values are deliberately not hash-consed.  A growing numeric
     * loop produces unique leaves; interning them forces decimal hashing and
     * retains the entire history. */
    at->ground.bigint = cetta_bigint_new_arena_from_mpz(a, value);
    return at;
}

Atom *atom_bigint_take_mpz(Arena *a, mpz_t value) {
    if (!a)
        return NULL;
    if (mpz_fits_slong_p(value)) {
        long small = mpz_get_si(value);
        int64_t narrowed = (int64_t)small;
        if ((long)narrowed == small)
            return atom_int(a, narrowed);
    }
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_BIGINT);
    at->ground.bigint = cetta_bigint_new_arena_take_mpz(a, value);
    return at;
}
#endif

Atom *atom_rational(Arena *a, const char *val) {
#if CETTA_BUILD_WITH_GMP
    if (!a || !val)
        return NULL;
    mpq_t q;
    mpq_init(q);
    bool ok = mpq_set_canonical_text(q, val);
    if (!ok) {
        mpq_clear(q);
        return NULL;
    }
    Atom *out = atom_rational_from_mpq(a, q);
    mpq_clear(q);
    return out;
#else
    (void)a;
    (void)val;
    return NULL;
#endif
}

const char *atom_rational_cstr(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_RATIONAL || !atom->ground.rational)
        return NULL;
    return atom->ground.rational->text;
}

#if CETTA_BUILD_WITH_GMP
bool atom_rational_get_mpq(const Atom *atom, mpq_t out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_RATIONAL || !atom->ground.rational)
        return false;
    CettaRational *rat = atom->ground.rational;
    if (rat->has_value) {
        mpq_set(out, rat->value);
        return true;
    }
    return mpq_set_canonical_text(out, rat->text);
}

Atom *atom_rational_from_mpq(Arena *a, const mpq_t value) {
    mpq_t q;
    mpq_init(q);
    mpq_set(q, value);
    mpq_canonicalize(q);
    if (mpz_cmp_ui(mpq_denref(q), 1u) == 0) {
        Atom *out = atom_bigint_from_mpz(a, mpq_numref(q));
        mpq_clear(q);
        return out;
    }

    char *num = mpz_get_str(NULL, 10, mpq_numref(q));
    char *den = mpz_get_str(NULL, 10, mpq_denref(q));
    size_t nlen = strlen(num);
    size_t dlen = strlen(den);
    char *text = cetta_malloc(nlen + dlen + 2);
    memcpy(text, num, nlen);
    text[nlen] = '/';
    memcpy(text + nlen + 1, den, dlen + 1);

    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = atom_flags_for_grounded_kind(GV_RATIONAL);
    temp.var_id = VAR_ID_NONE;
    temp.hash_cache = 0;
    temp.structural_facts =
        atom_structural_facts_for_grounded_kind(GV_RATIONAL);
    temp.ground.gkind = GV_RATIONAL;
    CettaRational temp_rat = {0};
    temp_rat.text = text;
    mpq_init(temp_rat.value);
    mpq_set(temp_rat.value, q);
    temp_rat.has_value = true;
    temp.ground.rational = &temp_rat;

    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) {
        mpq_clear(temp_rat.value);
        free(text);
        gmp_free_string(num);
        gmp_free_string(den);
        mpq_clear(q);
        return shared;
    }

    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    at->arena_id = a->identity;
    at->ground.rational = cetta_rational_new_arena(a, text);
    mpq_set(at->ground.rational->value, q);

    mpq_clear(temp_rat.value);
    free(text);
    gmp_free_string(num);
    gmp_free_string(den);
    mpq_clear(q);
    return at;
}
#endif

Atom *atom_space(Arena *a, void *space_ptr) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_SPACE);
    at->ground.ptr = space_ptr;
    return at;
}

Atom *atom_state(Arena *a, StateCell *cell) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_STATE);
    at->ground.ptr = cell;
    return at;
}

Atom *atom_capture(Arena *a, CaptureClosure *closure) {
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_CAPTURE);
    at->ground.ptr = closure;
    return at;
}

/* The arena holds the record once for this atom: a hold registered with the
 * allocation is released by the reset or free that drops the atom. */
static void arena_hold_foreign(Arena *a, CettaForeignValue *value) {
    const CettaForeignHold *hold = (const CettaForeignHold *)value;
    if (!hold || !hold->retain || !hold->release)
        return;
    hold->retain(value);
    arena_register_finalizer(a, hold->release, value);
}

Atom *atom_foreign(Arena *a, CettaForeignValue *value) {
    arena_hold_foreign(a, value);
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_FOREIGN);
    at->ground.ptr = value;
    return at;
}

Atom *atom_seal(Arena *a, void *object) {
    const CettaSealHold *hold = cetta_seal_hold_of(object);
    if (!a || !hold || !hold->retain || !hold->release ||
        !arena_retain_owner(a, object, hold->retain, hold->release))
        return NULL;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = atom_grounded_leaf(a, GV_SEAL);
    at->ground.ptr = object;
    return at;
}

void *atom_seal_object(const Atom *atom) {
    return atom_is_seal(atom) ? atom->ground.ptr : NULL;
}

size_t atom_seal_display_text(const Atom *atom, char *buffer,
                              size_t capacity) {
    void *object = atom_seal_object(atom);
    const CettaSealHold *hold = cetta_seal_hold_of(object);
    if (!hold) {
        if (buffer && capacity)
            buffer[0] = '\0';
        return 0u;
    }
    if (hold->display)
        return hold->display(object, buffer, capacity);
    static const char fallback[] = "(Sealed)";
    if (buffer && capacity) {
        size_t n = sizeof(fallback) - 1u < capacity - 1u
            ? sizeof(fallback) - 1u : capacity - 1u;
        memcpy(buffer, fallback, n);
        buffer[n] = '\0';
    }
    return sizeof(fallback) - 1u;
}

uint64_t atom_seal_identity(const Atom *atom) {
    void *object = atom_seal_object(atom);
    const CettaSealHold *hold = cetta_seal_hold_of(object);
    return hold && hold->identity ? hold->identity(object) : 0u;
}

const char *atom_seal_type_name(const Atom *atom) {
    void *object = atom_seal_object(atom);
    const CettaSealHold *hold = cetta_seal_hold_of(object);
    return hold && hold->type_name ? hold->type_name(object) : NULL;
}

Atom *atom_structural(Arena *arena, void *value) {
    const CettaStructuralClass *type = cetta_structural_class(value);
    if (!arena || !type || !type->type_name || !type->retain || !type->release ||
        !type->equal || !type->compare || !type->hash || !type->display ||
        !type->inspect || !type->retained_bytes) return NULL;
    bool held = arena_retained_owner_set_find(arena->retained_owners, value, type->release);
    if (!arena_retain_owner(arena, value, type->retain, type->release)) return NULL;
    if (!held) arena_account_external_bytes(arena, type->retained_bytes(value));
    Atom *atom = arena_alloc(arena, sizeof(*atom));
    *atom = atom_grounded_leaf(arena, GV_STRUCTURAL);
    atom->ground.ptr = value;
    /* The independent retained owner closes the syntax lifetime. This does
     * not grant admission to scalar codecs, interning or backend transfer. */
    atom->flags |= ATOM_FLAG_ARENA_CLOSED;
    return atom;
}

static const CettaStructuralClass *atom_structural_class(const Atom *atom) {
    return atom_is_structural(atom) ? cetta_structural_class(atom->ground.ptr) : NULL;
}

bool atom_structural_equal(const Atom *left, const Atom *right) {
    const CettaStructuralClass *type = atom_structural_class(left);
    return type && type == atom_structural_class(right) &&
        (left->ground.ptr == right->ground.ptr || type->equal(left->ground.ptr, right->ground.ptr));
}

int atom_structural_compare(const Atom *left, const Atom *right) {
    const CettaStructuralClass *a = atom_structural_class(left), *b = atom_structural_class(right);
    if (!a || !b) return a ? 1 : b ? -1 : 0;
    if (a != b) {
        if (g_symbols) {
            int order = symbol_compare(g_symbols,
                symbol_intern_cstr(g_symbols, a->type_name),
                symbol_intern_cstr(g_symbols, b->type_name));
            if (order) return order < 0 ? -1 : 1;
        }
        return (uintptr_t)a < (uintptr_t)b ? -1 : 1;
    }
    return a->compare(left->ground.ptr, right->ground.ptr);
}

uint64_t atom_structural_hash(const Atom *atom) {
    const CettaStructuralClass *type = atom_structural_class(atom);
    return type ? type->hash(atom->ground.ptr) : 0u;
}

size_t atom_structural_display(const Atom *atom, char *buffer, size_t capacity) {
    const CettaStructuralClass *type = atom_structural_class(atom);
    if (!type) { if (capacity && buffer) buffer[0] = '\0'; return 0u; }
    return type->display(atom->ground.ptr, buffer, capacity);
}

const char *atom_structural_type_name(const Atom *atom) {
    const CettaStructuralClass *type = atom_structural_class(atom);
    return type ? type->type_name : NULL;
}

Atom *atom_structural_inspect(const Atom *atom, Arena *arena) {
    const CettaStructuralClass *type = atom_structural_class(atom);
    return type && arena ? type->inspect(atom->ground.ptr, arena) : NULL;
}

size_t atom_structural_retained_bytes(const Atom *atom) {
    const CettaStructuralClass *type = atom_structural_class(atom);
    return type ? type->retained_bytes(atom->ground.ptr) : 0u;
}

static bool atom_tree_is_seal(const Atom *atom, void *context) {
    (void)context;
    return atom_is_seal(atom);
}

bool atom_contains_seal(const Atom *atom) {
    if (!atom)
        return false;
    /* The inherited identity summary proves absence only for atoms whose
     * constructor computed it; anything else is scanned exactly. */
    if ((atom->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u &&
        (atom->flags & ATOM_FLAG_HAS_IDENTITY_GROUNDED) == 0u)
        return false;
    return atom_tree_any(atom, atom_tree_is_seal, NULL);
}

Atom *atom_bindings_value(Arena *arena, CettaBindingsValue *value) {
    if (!arena || !value || !value->retain || !value->release ||
        !value->observe ||
        !arena_retain_owner(arena, value, value->retain, value->release))
        return NULL;
    Atom *atom = arena_alloc(arena, sizeof(*atom));
    *atom = (Atom){
        .kind = ATOM_GROUNDED,
        /* This opaque leaf retains its independent syntax owner above.
         * Copying it therefore closes the destination's syntax graph; it
         * does not borrow an Atom from the source arena. This says nothing
         * about hash stability or permission to share effects/workers. */
        .flags = atom_flags_for_grounded_kind(GV_BINDINGS) | ATOM_FLAG_ARENA_CLOSED,
        .arena_id = arena->identity,
        .structural_facts = atom_structural_facts_for_grounded_kind(GV_BINDINGS),
        .ground = {.gkind = GV_BINDINGS, .ptr = value},
    };
    return atom;
}

Atom *atom_term_graph(Arena *arena, CettaTermGraph *graph, uint32_t node) {
    const CettaTermGraphRef *ref =
        graph && term_graph_ref ? term_graph_ref(graph, node) : NULL;
    if (!arena || !ref ||
        !arena_retain_owner(arena, graph, term_graph_retain,
                            term_graph_release))
        return NULL;
    Atom *atom = arena_alloc(arena, sizeof(*atom));
    *atom = (Atom){
        .kind = ATOM_GROUNDED,
        .flags = atom_flags_for_grounded_kind(GV_TERM_GRAPH),
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = arena->identity,
        .structural_facts =
            atom_structural_facts_for_grounded_kind(GV_TERM_GRAPH),
        .ground = {.gkind = GV_TERM_GRAPH, .ptr = (void *)ref},
    };
    return atom;
}

const CettaTermGraphRef *atom_term_graph_ref(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_TERM_GRAPH
        ? atom->ground.ptr : NULL;
}

Atom *atom_rational_open(Arena *arena, Atom *atom) {
    return term_graph_open_value && atom_is_rational_value(atom)
        ? term_graph_open_value(arena, atom) : atom;
}

/* atom_hash_compute of a list tag, as a constant: the tags are shared by every
 * list and owned by no arena, so their hash is fixed at compile time. */
#define ATOM_LIST_TAG_HASH_STEP(h, x) \
    ((uint32_t)((uint32_t)(h) * 33u) ^ (uint32_t)(x))
#define ATOM_LIST_TAG_HASH(tag)                                            \
    ATOM_LIST_TAG_HASH_STEP(                                               \
        ATOM_LIST_TAG_HASH_STEP(                                           \
            ATOM_LIST_TAG_HASH_STEP(5381u, ATOM_GROUNDED), GV_INTERNAL_TAG), \
        (tag))
#define ATOM_LIST_TAG_INIT(tag, facts)                                     \
    {                                                                      \
        .kind = ATOM_GROUNDED,                                             \
        .flags = ATOM_FLAG_HASH_STABLE | ATOM_FLAG_HASHCONS_ELIGIBLE |     \
                 ATOM_FLAG_ARENA_CLOSED,                                   \
        .var_id = VAR_ID_NONE,                                             \
        .sym_id = SYMBOL_ID_NONE,                                          \
        .arena_id = 0u,                                                    \
        .name_key = NULL,                                                  \
        .hash_cache = ATOM_HASH_SPELL(ATOM_LIST_TAG_HASH(tag)),            \
        .structural_facts = ATOM_STRUCTURAL_FACTS_VALID |                  \
                            ATOM_STRUCTURAL_HAS_INTERNAL_TAG | (facts),    \
        .ground = {.gkind = GV_INTERNAL_TAG, .ival = (tag)},               \
    }

static Atom g_atom_list_tag =
    ATOM_LIST_TAG_INIT(CETTA_INTERNAL_TAG_LIST, ATOM_STRUCTURAL_HAS_LIST);
static Atom g_atom_list_rest_tag =
    ATOM_LIST_TAG_INIT(CETTA_INTERNAL_TAG_LIST_REST,
                       ATOM_STRUCTURAL_HAS_LIST |
                           ATOM_STRUCTURAL_HAS_OPEN_LIST);

/* [] is shared the same way: (LIST), with its hash fixed like the tags'. */
static Atom *g_atom_empty_list_elems[1] = {&g_atom_list_tag};
static Atom g_atom_empty_list = {
    .kind = ATOM_EXPR,
    .flags = ATOM_FLAG_HASH_STABLE | ATOM_FLAG_HASHCONS_ELIGIBLE |
             ATOM_FLAG_ARENA_CLOSED,
    .var_id = VAR_ID_NONE,
    .sym_id = SYMBOL_ID_NONE,
    .arena_id = 0u,
    .name_key = NULL,
    .hash_cache = ATOM_HASH_SPELL(ATOM_HASH_EXPR_FINISH(ATOM_LIST_TAG_HASH_STEP(
        ATOM_LIST_TAG_HASH_STEP(
            ATOM_LIST_TAG_HASH_STEP(
                ATOM_LIST_TAG_HASH_STEP(5381u, ATOM_EXPR), 1u),
            0u),
        ATOM_HASH_SPELL(ATOM_LIST_TAG_HASH(CETTA_INTERNAL_TAG_LIST))))),
    .structural_facts = ATOM_STRUCTURAL_FACTS_VALID |
                        ATOM_STRUCTURAL_HAS_INTERNAL_TAG |
                        ATOM_STRUCTURAL_HAS_LIST,
    .expr = {.elems = g_atom_empty_list_elems, .len = 1u},
};

Atom *atom_internal_tag(Arena *a, CettaInternalTag tag) {
    if (tag == CETTA_INTERNAL_TAG_LIST)
        return &g_atom_list_tag;
    if (tag == CETTA_INTERNAL_TAG_LIST_REST)
        return &g_atom_list_rest_tag;
    if (!a || tag == 0)
        return NULL;
    Atom *at = arena_alloc(a, sizeof(*at));
    at->kind = ATOM_GROUNDED;
    at->flags = atom_flags_for_grounded_kind(GV_INTERNAL_TAG);
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->arena_id = a->identity;
    at->name_key = NULL;
    at->hash_cache = 0u;
    at->structural_facts = atom_structural_facts_for_internal_tag((int64_t)tag);
    at->ground.gkind = GV_INTERNAL_TAG;
    at->ground.ival = (int64_t)tag;
    return at;
}

Atom *atom_petta_no_result(Arena *a) {
    return atom_internal_tag(a, CETTA_INTERNAL_TAG_PETTA_NO_RESULT);
}

Atom *atom_petta_prolog_compound(Arena *a, Atom *body) {
    if (!a || !body || body->kind != ATOM_EXPR ||
        body->expr.len == 0u ||
        body->expr.elems[0]->kind != ATOM_SYMBOL) {
        return NULL;
    }
    Atom *tag = atom_internal_tag(
        a, CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND);
    return tag ? atom_expr2(a, tag, body) : NULL;
}

bool atom_petta_prolog_compound_body(Atom *atom, Atom **body) {
    if (body)
        *body = NULL;
    if (!atom || !body || atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        atom->expr.elems[0]->kind != ATOM_GROUNDED ||
        atom->expr.elems[0]->ground.gkind != GV_INTERNAL_TAG ||
        atom->expr.elems[0]->ground.ival !=
            (int64_t)CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND) {
        return false;
    }
    *body = atom->expr.elems[1];
    return *body && (*body)->kind == ATOM_EXPR &&
           (*body)->expr.len > 0u &&
           (*body)->expr.elems[0]->kind == ATOM_SYMBOL;
}

/* `prolog:compound` interned once per symbol-table instance: every solve
 * step asks whether its head is a compound carrier, so compare identifiers
 * rather than spellings. */
static SymbolId atom_prolog_compound_symbol(void) {
    static _Thread_local const SymbolTable *table = NULL;
    static _Thread_local uint64_t table_instance_id = 0u;
    static _Thread_local SymbolId symbol = SYMBOL_ID_NONE;
    uint64_t instance = symbol_table_instance_id(g_symbols);
    if (!g_symbols)
        return SYMBOL_ID_NONE;
    if (table != g_symbols || table_instance_id != instance) {
        table = g_symbols;
        table_instance_id = instance;
        symbol = symbol_intern_cstr(g_symbols, "prolog:compound");
    }
    return symbol;
}

bool atom_prolog_compound_body(Atom *atom, Atom **body) {
    if (body)
        *body = NULL;
    if (!atom || !body || atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom->expr.elems[0]->sym_id == SYMBOL_ID_NONE ||
        atom->expr.elems[0]->sym_id != atom_prolog_compound_symbol()) {
        return false;
    }
    *body = atom->expr.elems[1];
    return *body && (*body)->kind == ATOM_EXPR &&
           (*body)->expr.len > 0u &&
           (*body)->expr.elems[0]->kind == ATOM_SYMBOL;
}

Atom *atom_counted_collection(
    Arena *a, int64_t count) {
    if (!a || count < 0)
        return NULL;
    Atom *tag = atom_internal_tag(
        a, CETTA_INTERNAL_TAG_COUNTED_COLLECTION);
    Atom *value = atom_int(a, count);
    return tag && value ? atom_expr2(a, tag, value) : NULL;
}

Atom *atom_list(Arena *a, Atom *const *elems, CettaExprLen len) {
    Atom *tag, *list;
    Atom **parts;
    const CettaExprLen capacity = (CettaExprLen)(SIZE_MAX / sizeof(*parts));
    if (len == 0u)
        return &g_atom_empty_list;
    if (!a || !elems || len >= capacity)
        return NULL;
    tag = atom_internal_tag(a, CETTA_INTERNAL_TAG_LIST);
    if (!tag)
        return NULL;
    parts = cetta_malloc(sizeof(*parts) * (size_t)(len + 1u));
    parts[0] = tag;
    if (len > 0u)
        memcpy(parts + 1, elems, sizeof(*parts) * (size_t)len);
    list = atom_expr(a, parts, len + 1u);
    free(parts);
    return list;
}

Atom *atom_list_with_rest(Arena *a, Atom *const *elems, CettaExprLen len, Atom *rest) {
    Atom *tag, *result;
    Atom **parts;
    CettaExprLen rest_len;
    const CettaExprLen capacity = (CettaExprLen)(SIZE_MAX / sizeof(*parts));
    if (!a || !rest || (!elems && len > 0u) || len > capacity)
        return NULL;
    if (atom_is_list(rest) || atom_is_list_rest(rest)) {
        /* [x... | [y...]] is [x... y...]; [x... | [y... | r]] is [x... y... | r]. */
        rest_len = rest->expr.len - 1u;
        if (rest_len >= capacity || len > capacity - rest_len - 1u)
            return NULL;
        parts = cetta_malloc(sizeof(*parts) * (size_t)(len + rest_len + 1u));
        parts[0] = rest->expr.elems[0];
        if (len > 0u)
            memcpy(parts + 1, elems, sizeof(*parts) * (size_t)len);
        memcpy(parts + 1 + len, rest->expr.elems + 1, sizeof(*parts) * (size_t)rest_len);
        result = atom_expr(a, parts, len + rest_len + 1u);
        free(parts);
        return result;
    }
    if (len == 0u)
        return rest; /* [| r] is r */
    if (len > capacity - 2u)
        return NULL;
    tag = atom_internal_tag(a, CETTA_INTERNAL_TAG_LIST_REST);
    if (!tag)
        return NULL;
    parts = cetta_malloc(sizeof(*parts) * (size_t)(len + 2u));
    parts[0] = tag;
    if (len > 0u)
        memcpy(parts + 1, elems, sizeof(*parts) * (size_t)len);
    parts[len + 1u] = rest;
    result = atom_expr(a, parts, len + 2u);
    free(parts);
    return result;
}

Atom *atom_sequence_like(Arena *a, const Atom *like, Atom *const *elems,
                         CettaExprLen len) {
    if (atom_is_list(like))
        return atom_list(a, elems, len);
    if (!a || (len > 0u && !elems) ||
        !cetta_expr_len_mul_fits_size(len, sizeof(Atom *)))
        return NULL;
    Atom **copy = len ? cetta_malloc(sizeof(*copy) * (size_t)len) : NULL;
    if (len)
        memcpy(copy, elems, sizeof(*copy) * (size_t)len);
    Atom *result = atom_expr(a, copy, len);
    free(copy);
    return result;
}

Atom *atom_list_tail(Arena *a, const Atom *list, CettaExprLen from) {
    if (!atom_is_list(list))
        return NULL;
    if (from > atom_list_len(list))
        from = atom_list_len(list);
    return atom_list(a, atom_list_elems(list) + from, atom_list_len(list) - from);
}

bool atom_counted_collection_count(
    Atom *atom, int64_t *count) {
    if (count)
        *count = 0;
    if (!atom || !count || atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        atom->expr.elems[0]->kind != ATOM_GROUNDED ||
        atom->expr.elems[0]->ground.gkind != GV_INTERNAL_TAG ||
        atom->expr.elems[0]->ground.ival !=
            (int64_t)CETTA_INTERNAL_TAG_COUNTED_COLLECTION ||
        atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT ||
        atom->expr.elems[1]->ground.ival < 0) {
        return false;
    }
    *count = atom->expr.elems[1]->ground.ival;
    return true;
}

Atom *atom_prime_need_capability_with_rights(
    Arena *a, uint64_t session_id, uint64_t thunk_id,
    uint64_t authority_id, uint32_t rights) {
    if (!a || session_id == 0u || thunk_id == 0u || authority_id == 0u ||
        rights == 0u || (rights & ~CETTA_PRIME_NEED_RIGHT_ALL) != 0u)
        return NULL;
    CettaPrimeNeedCapability *capability =
        arena_alloc(a, sizeof(*capability));
    Atom *at = arena_alloc(a, sizeof(*at));
    if (!capability || !at)
        return NULL;
    capability->session_id = session_id;
    capability->thunk_id = thunk_id;
    capability->authority_id = authority_id;
    capability->rights = rights;
    at->kind = ATOM_GROUNDED;
    at->flags = atom_flags_for_grounded_kind(GV_PRIME_NEED_CAPABILITY) |
                ATOM_FLAG_HAS_REGISTRY_REFS |
                ATOM_FLAG_ARENA_CLOSED;
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->arena_id = a->identity;
    at->name_key = NULL;
    at->hash_cache = 0u;
    at->structural_facts = atom_structural_facts_for_grounded_kind(
        GV_PRIME_NEED_CAPABILITY);
    at->ground.gkind = GV_PRIME_NEED_CAPABILITY;
    at->ground.prime_need_capability = capability;
    return at;
}

Atom *atom_prime_need_capability(Arena *a, uint64_t session_id,
                                 uint64_t thunk_id, uint64_t authority_id) {
    return atom_prime_need_capability_with_rights(
        a, session_id, thunk_id, authority_id, CETTA_PRIME_NEED_RIGHT_ALL);
}

const CettaPrimeNeedCapability *atom_prime_need_capability_value(
    const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
                   atom->ground.gkind == GV_PRIME_NEED_CAPABILITY
               ? atom->ground.prime_need_capability
               : NULL;
}

static Atom *atom_prime_context_wrap(Arena *a,
                                     CettaPrimeContext *context) {
    if (!a || !context)
        return NULL;
    Atom *at = arena_alloc(a, sizeof(*at));
    if (!at)
        return NULL;
    at->kind = ATOM_GROUNDED;
    at->flags = atom_flags_for_grounded_kind(GV_PRIME_CONTEXT) |
                (context->flags &
                 (ATOM_FLAG_HAS_VARS | ATOM_FLAG_HAS_REGISTRY_REFS |
                  ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                  ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE));
    at->var_id = VAR_ID_NONE;
    at->sym_id = SYMBOL_ID_NONE;
    at->arena_id = a->identity;
    at->name_key = NULL;
    at->hash_cache = 0u;
    at->structural_facts =
        atom_structural_facts_for_grounded_kind(GV_PRIME_CONTEXT);
    at->ground.gkind = GV_PRIME_CONTEXT;
    at->ground.prime_context = context;
    return at;
}

Atom *atom_prime_context_bind(Arena *a, const CettaPrimeContext *parent,
                              Atom *key, Atom *value) {
    if (!a || !key || !value ||
        (parent && parent->depth == UINT32_MAX))
        return NULL;
    CettaPrimeContext *frame = arena_alloc(a, sizeof(*frame));
    if (!frame)
        return NULL;
    frame->parent = parent;
    frame->key = key;
    frame->value = value;
    frame->depth = parent ? parent->depth + 1u : 1u;
    frame->flags = (parent ? parent->flags : 0u) |
                   (key->flags &
                    (ATOM_FLAG_HAS_VARS | ATOM_FLAG_HAS_REGISTRY_REFS |
                     ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                     ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE)) |
                   (value->flags &
                    (ATOM_FLAG_HAS_VARS | ATOM_FLAG_HAS_REGISTRY_REFS |
                     ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                     ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE));
    return atom_prime_context_wrap(a, frame);
}

const CettaPrimeContext *atom_prime_context_value(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
                   atom->ground.gkind == GV_PRIME_CONTEXT
               ? atom->ground.prime_context
               : NULL;
}

Atom *atom_prime_context_lookup(const CettaPrimeContext *context, Atom *key) {
    if (!key)
        return NULL;
    Atom *oldest = NULL;
    for (const CettaPrimeContext *frame = context; frame;
         frame = frame->parent) {
        if (frame->key && atom_eq(frame->key, key)) {
            if (CETTA_PRIME_CONTEXT_MUTATION == 1) {
                oldest = frame->value;
                continue;
            }
            return frame->value;
        }
    }
    return oldest;
}

uint32_t atom_prime_context_depth(const CettaPrimeContext *context) {
    return context ? context->depth : 0u;
}

static Atom *atom_prime_context_deep_copy(
    Arena *dst, const CettaPrimeContext *context) {
    if (!dst || !context || context->depth == 0u)
        return NULL;
    size_t count = context->depth;
    if (count > SIZE_MAX / sizeof(const CettaPrimeContext *))
        return NULL;
    const CettaPrimeContext **frames = cetta_malloc(
        sizeof(*frames) * count);
    size_t len = 0u;
    for (const CettaPrimeContext *frame = context; frame;
         frame = frame->parent) {
        if (len == count) {
            free(frames);
            return NULL;
        }
        frames[len++] = frame;
    }
    if (len != count) {
        free(frames);
        return NULL;
    }

    CettaPrimeContext *parent = NULL;
    for (size_t i = len; i > 0u; i--) {
        Atom *key = atom_deep_copy(dst, frames[i - 1u]->key);
        Atom *value = atom_deep_copy(dst, frames[i - 1u]->value);
        if (!key || !value) {
            free(frames);
            return NULL;
        }
        CettaPrimeContext *next = arena_alloc(dst, sizeof(*next));
        if (!next) {
            free(frames);
            return NULL;
        }
        next->parent = parent;
        next->key = key;
        next->value = value;
        next->depth = parent ? parent->depth + 1u : 1u;
        next->flags = (parent ? parent->flags : 0u) |
                      (key->flags &
                       (ATOM_FLAG_HAS_VARS | ATOM_FLAG_HAS_REGISTRY_REFS |
                        ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                        ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE)) |
                      (value->flags &
                       (ATOM_FLAG_HAS_VARS | ATOM_FLAG_HAS_REGISTRY_REFS |
                        ATOM_FLAG_HAS_IDENTITY_GROUNDED |
                        ATOM_FLAG_HAS_THREAD_LOCAL_RESOURCE));
        parent = next;
    }
    free(frames);
    return atom_prime_context_wrap(dst, parent);
}

Atom *atom_float(Arena *a, double val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = atom_flags_for_grounded_kind(GV_FLOAT);
    temp.var_id = VAR_ID_NONE;
    temp.arena_id = a->identity;
    temp.hash_cache = 0;
    temp.structural_facts =
        atom_structural_facts_for_grounded_kind(GV_FLOAT) |
        (isnan(val) ? ATOM_STRUCTURAL_HAS_NAN : 0u);
    temp.ground.gkind = GV_FLOAT;
    temp.ground.fval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

static void atom_scalar_leaf(Atom *out, GroundedKind gkind) {
    *out = (Atom){0};
    out->kind = ATOM_GROUNDED;
    out->flags = atom_flags_for_grounded_kind(gkind);
    out->var_id = VAR_ID_NONE;
    out->structural_facts = atom_structural_facts_for_grounded_kind(gkind);
    out->ground.gkind = gkind;
}

void atom_scalar_leaf_int(Atom *out, int64_t value) {
    atom_scalar_leaf(out, GV_INT);
    out->ground.ival = value;
}

void atom_scalar_leaf_float(Atom *out, double value) {
    atom_scalar_leaf(out, GV_FLOAT);
    if (isnan(value))
        out->structural_facts |= ATOM_STRUCTURAL_HAS_NAN;
    out->ground.fval = value;
}

void atom_scalar_leaf_bool(Atom *out, bool value) {
    atom_scalar_leaf(out, GV_BOOL);
    out->ground.bval = value;
}

Atom *atom_bool(Arena *a, bool val) {
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = atom_flags_for_grounded_kind(GV_BOOL);
    temp.var_id = VAR_ID_NONE;
    temp.arena_id = a->identity;
    temp.hash_cache = 0;
    temp.structural_facts =
        atom_structural_facts_for_grounded_kind(GV_BOOL);
    temp.ground.gkind = GV_BOOL;
    temp.ground.bval = val;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = temp;
    return at;
}

Atom *atom_string_n(Arena *a, const char *bytes, size_t len) {
    if (len > CETTA_STRING_BYTES_MAX) {
        fprintf(stderr,
                "fatal: a string of %zu bytes exceeds the %u-byte string limit\n",
                len, CETTA_STRING_BYTES_MAX);
        abort();
    }
    Atom temp = {0};
    temp.kind = ATOM_GROUNDED;
    temp.flags = atom_flags_for_grounded_kind(GV_STRING);
    temp.var_id = VAR_ID_NONE;
    temp.arena_id = a->identity;
    temp.hash_cache = 0;
    temp.structural_facts =
        atom_structural_facts_for_grounded_kind(GV_STRING);
    temp.ground.gkind = GV_STRING;
    temp.ground.slen = (uint32_t)len;
    temp.ground.sval = bytes ? bytes : "";
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    if (len >= ATOM_STRING_RETAINED_MIN_BYTES) {
        if (len > SIZE_MAX - sizeof(AtomStringStorage) - 1u)
            cetta_oom(SIZE_MAX);
        AtomStringStorage *storage = cetta_malloc(sizeof(*storage) + len + 1u);
        atomic_init(&storage->references, 1u);
        storage->length = len;
        memcpy(storage->bytes, bytes, len);
        storage->bytes[len] = '\0';
        Atom *value = atom_string_retain_storage(a, &temp, storage);
        atom_string_storage_release(storage);
        return value;
    }
    Atom *at = arena_alloc(a, sizeof(Atom));
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->arena_id = a->identity;
    at->hash_cache = 0;
    at->structural_facts = temp.structural_facts;
    at->ground.gkind = temp.ground.gkind;
    at->ground.slen = temp.ground.slen;
    at->name_key = NULL;
    char *copy = arena_alloc(a, len + 1u);
    if (len)
        memcpy(copy, bytes, len);
    copy[len] = '\0';
    at->ground.sval = copy;
    return at;
}

Atom *atom_string(Arena *a, const char *val) {
    return atom_string_n(a, val, val ? strlen(val) : 0u);
}

#ifdef CETTA_TEST_HOOKS
size_t atom_string_storage_references_test(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_STRING ||
        !(atom->structural_facts & ATOM_STRUCTURAL_STRING_STORAGE))
        return 0u;
    return atomic_load_explicit(&((const RetainedStringAtom *)atom)->storage->references,
                                memory_order_acquire);
}
#endif

bool atom_expr_allocation_bound(CettaExprLen length, size_t *bytes_out) {
    if (!bytes_out ||
        length > (SIZE_MAX - sizeof(Atom) - 7u) / sizeof(Atom *))
        return false;
    *bytes_out = (sizeof(Atom) + (size_t)length * sizeof(Atom *) + 7u)
        & ~(size_t)7u;
    return true;
}

/* A list pattern whose rest is itself a list or a list pattern is the one list
 * they spell together: [x... | [y...]] is [x..., y...].  Construction keeps
 * that normal form, so substitution into a rest never leaves a nested one. */
static bool atom_list_rest_parts_splice(Atom *const *elems, CettaExprLen len,
                                        uint32_t structural_facts) {
    return (structural_facts & ATOM_STRUCTURAL_HAS_OPEN_LIST) != 0u &&
           len >= 3u && elems &&
           atom_is_internal_tag(elems[0], CETTA_INTERNAL_TAG_LIST_REST) &&
           (atom_is_list(elems[len - 1u]) || atom_is_list_rest(elems[len - 1u]));
}

/* An expression in an arena that interns nothing, with its summary. */
static inline __attribute__((always_inline)) Atom *atom_expr_build(
    Arena *a, Atom **elems, CettaExprLen len, uint32_t flags,
    uint32_t structural_facts, VarId var_id) {
    size_t elems_bytes = (size_t)len * sizeof(Atom *);
    if (elems_bytes > SIZE_MAX - sizeof(Atom))
        cetta_oom(SIZE_MAX);
    Atom *at = arena_alloc(a, sizeof(Atom) + elems_bytes);
    *at = (Atom){
        .kind = ATOM_EXPR,
        .flags = flags,
        .var_id = var_id,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = a->identity,
        .structural_facts = structural_facts,
        .expr = {
            .len = len,
            .elems = elems_bytes ? (Atom **)(at + 1) : NULL,
        },
    };
    if (elems && elems_bytes > 0) {
        Atom **copy = at->expr.elems;
        if (len <= 4u) {
            for (CettaExprLen i = 0; i < len; i++)
                copy[i] = elems[i];
        } else {
            memcpy(copy, elems, elems_bytes);
        }
    }
    return at;
}

/* atom_expr in an arena that interns nothing and has no older generation,
 * at a constant length: the same fold, with the children's summary kept in
 * registers. */
static inline __attribute__((always_inline)) Atom *atom_expr_small(
    Arena *a, Atom **elems, CettaExprLen len) {
    uint32_t structural_facts = 0u;
    uint32_t flags = atom_flags_fold(
        a->identity, 0u, false, elems, len, &structural_facts);
    if (atom_list_rest_parts_splice(elems, len, structural_facts))
        return atom_list_with_rest(a, elems + 1, len - 2u, elems[len - 1u]);
    VarId var_id = atom_single_variable_id_of(flags, elems, len);
    return atom_expr_build(a, elems, len, flags, structural_facts, var_id);
}

void atom_expr_head_init(AtomExprHead *out, const Atom *head) {
    *out = (AtomExprHead){
        .flags = head->flags,
        .structural_facts = head->structural_facts,
        .arena_id = head->arena_id,
        .open = (head->flags & ATOM_FLAG_ARENA_CLOSED) == 0u,
        .adjusts = atom_summary_head_adjusts((Atom *)head),
    };
}

/* atom_expr_small with the head's part of the fold given: beginning the fold
 * from the state adding the head leaves (atom_summary_begin_ids then
 * atom_summary_add_ids of the head) and finishing it with no head to adjust
 * computes what atom_expr_small computes for a head that adjusts nothing. */
static inline __attribute__((always_inline)) Atom *atom_expr_headed_small(
    Arena *a, const AtomExprHead *head, Atom **elems, CettaExprLen len) {
    const bool open = head->open |
        ((head->arena_id != 0u) & (head->arena_id != a->identity));
    AtomChildrenSummary summary = {
        .any_flags = head->flags,
        .all_flags = head->flags,
        .any_facts = head->structural_facts,
        .all_facts = head->structural_facts,
        .arena_id = a->identity,
        .closed = !open,
        .owner_closed = !open ||
            ((head->arena_id == 0u || head->arena_id == a->identity) &&
             (head->structural_facts & (ATOM_STRUCTURAL_FACTS_VALID |
                                       ATOM_STRUCTURAL_OWNER_CLOSED)) ==
                 (ATOM_STRUCTURAL_FACTS_VALID | ATOM_STRUCTURAL_OWNER_CLOSED)),
    };
    for (CettaExprIndex i = 1u; i < len; i++)
        atom_summary_add_ids(&summary, false, elems[i]);
    uint32_t structural_facts = 0u;
    uint32_t flags = atom_summary_finish(
        &summary, false, NULL, &structural_facts);
    if (atom_list_rest_parts_splice(elems, len, structural_facts))
        return atom_list_with_rest(a, elems + 1, len - 2u, elems[len - 1u]);
    VarId var_id = atom_single_variable_id_of(flags, elems, len);
    return atom_expr_build(a, elems, len, flags, structural_facts, var_id);
}

/* atom_expr_headed of elements passed one by one, for the lengths a
 * constructor of up to three arguments has. */
Atom *atom_expr_headed2(Arena *a, const AtomExprHead *head, Atom *head_atom,
                        Atom *first) {
    Atom *elems[2] = {head_atom, first};
    if (!head->adjusts && !a->hashcons && a->older_identity == 0u)
        return atom_expr_headed_small(a, head, elems, 2u);
    return atom_expr(a, elems, 2u);
}

Atom *atom_expr_headed3(Arena *a, const AtomExprHead *head, Atom *head_atom,
                        Atom *first, Atom *second) {
    Atom *elems[3] = {head_atom, first, second};
    if (!head->adjusts && !a->hashcons && a->older_identity == 0u)
        return atom_expr_headed_small(a, head, elems, 3u);
    return atom_expr(a, elems, 3u);
}

Atom *atom_expr_headed4(Arena *a, const AtomExprHead *head, Atom *head_atom,
                        Atom *first, Atom *second, Atom *third) {
    Atom *elems[4] = {head_atom, first, second, third};
    if (!head->adjusts && !a->hashcons && a->older_identity == 0u)
        return atom_expr_headed_small(a, head, elems, 4u);
    return atom_expr(a, elems, 4u);
}

Atom *atom_expr_headed(Arena *a, const AtomExprHead *head, Atom **elems,
                       CettaExprLen len) {
    if (!head->adjusts && !a->hashcons && a->older_identity == 0u &&
        elems) {
        switch (len) {
        case 1u:
            return atom_expr_headed_small(a, head, elems, 1u);
        case 2u:
            return atom_expr_headed_small(a, head, elems, 2u);
        case 3u:
            return atom_expr_headed_small(a, head, elems, 3u);
        case 4u:
            return atom_expr_headed_small(a, head, elems, 4u);
        default:
            break;
        }
    }
    return atom_expr(a, elems, len);
}

bool atom_expr_borrowed_view_init(AtomExprBorrowedView *out,
                                  Atom **elems, CettaExprLen len) {
    if (!out || (len != 0u && !elems))
        return false;
    for (CettaExprIndex index = 0u; index < len; index++)
        if (!elems[index])
            return false;
    uint32_t facts = 0u;
    uint32_t flags = atom_flags_from_children(0u, 0u, elems, len, &facts);
    /* Splicing a LIST/rest representation needs a constructed value. */
    if (atom_list_rest_parts_splice(elems, len, facts))
        return false;
    flags &= ~(ATOM_FLAG_ARENA_CLOSED | ATOM_FLAG_HASHCONS_ELIGIBLE);
    facts &= ~(ATOM_STRUCTURAL_OWNER_CLOSED |
               ATOM_STRUCTURAL_GENERATION_CLOSED | ATOM_STRUCTURAL_FRONT_SLACK);
    out->root = (Atom){
        .kind = ATOM_EXPR, .flags = flags, .structural_facts = facts,
        .var_id = atom_single_variable_id_of(flags, elems, len),
        .sym_id = SYMBOL_ID_NONE, .expr = {.elems = elems, .len = len},
    };
    return true;
}

Atom *atom_expr(Arena *a, Atom **elems, CettaExprLen len) {
    if (!a->hashcons && a->older_identity == 0u && elems) {
        switch (len) {
        case 1u:
            return atom_expr_small(a, elems, 1u);
        case 2u:
            return atom_expr_small(a, elems, 2u);
        case 3u:
            return atom_expr_small(a, elems, 3u);
        case 4u:
            return atom_expr_small(a, elems, 4u);
        default:
            break;
        }
    }
    size_t elems_bytes = 0;
    if (len > 0 && !cetta_expr_len_mul_fits_size(len, sizeof(Atom *)))
        cetta_oom(SIZE_MAX);
    elems_bytes = (size_t)len * sizeof(Atom *);
    uint32_t structural_facts = 0u;
    uint32_t flags = atom_flags_from_children(
        a->identity, a->older_identity, elems, len, &structural_facts);
    if (atom_list_rest_parts_splice(elems, len, structural_facts))
        return atom_list_with_rest(a, elems + 1, len - 2u, elems[len - 1u]);
    VarId var_id = atom_single_variable_id_of(flags, elems, len);
    /* An arena that interns nothing takes the expression as built:
     * atom_maybe_hashcons would decline it. */
    if (!a->hashcons)
        return atom_expr_build(a, elems, len, flags, structural_facts, var_id);
    Atom temp = {0};
    temp.kind = ATOM_EXPR;
    temp.flags = flags;
    temp.structural_facts = structural_facts;
    temp.var_id = var_id;
    temp.arena_id = a->identity;
    temp.hash_cache = 0;
    temp.expr.len = len;
    temp.expr.elems = elems;
    Atom *shared = atom_maybe_hashcons(a, &temp);
    if (shared) return shared;
    if (elems_bytes > SIZE_MAX - sizeof(Atom))
        cetta_oom(SIZE_MAX);
    /* The expression header and its immutable child vector have exactly the
     * same arena lifetime.  Keep them in one allocation so construction and
     * copying do not pay two allocator traversals per expression node. */
    Atom *at = arena_alloc(a, sizeof(Atom) + elems_bytes);
    at->kind = temp.kind;
    at->flags = temp.flags;
    at->var_id = temp.var_id;
    at->sym_id = SYMBOL_ID_NONE;
    at->arena_id = a->identity;
    at->name_key = NULL;
    at->hash_cache = 0;
    at->structural_facts = temp.structural_facts;
    at->expr.len = len;
    at->expr.elems = elems_bytes ? (Atom **)(at + 1) : NULL;
    if (elems && elems_bytes > 0) memcpy(at->expr.elems, elems, elems_bytes);
    return at;
}

/* How far a suffix view looks for its single variable when its list had
 * several (atom_expr_suffix). */
enum { ATOM_SUFFIX_VARIABLE_SCAN = 16u };

/* Whether `child` is closed for arena `arena_id`, as the closure fold asks. */
static bool atom_summary_closed_child(const Atom *child, uint32_t arena_id) {
    return child &&
        (child->flags & ATOM_FLAG_ARENA_CLOSED) != 0u &&
        (child->arena_id == 0u || child->arena_id == arena_id);
}

/* Whether an expression's element vector is storage of the expression's own
 * arena: global storage, a vector allocated with its header, or a buffer
 * with front slack, which only its arena's constructors allocate.  A suffix
 * view shares its list's vector wherever that lives, so a view's header
 * arena says nothing about its vector. */
static bool atom_expr_vector_in_own_arena(const Atom *expression) {
    return expression->arena_id == 0u ||
           expression->expr.elems == (Atom **)(expression + 1) ||
           (expression->structural_facts & ATOM_STRUCTURAL_FRONT_SLACK) != 0u;
}

Atom *atom_expr_suffix(Arena *a, Atom *expression, CettaExprLen offset) {
    if (!a || !expression || expression->kind != ATOM_EXPR ||
        offset > expression->expr.len)
        return NULL;
    CettaExprLen len = expression->expr.len - offset;
    Atom **elems = len > 0u ? expression->expr.elems + offset : NULL;
    /* An arena that interns its expressions takes the interned copy, and
     * storage the suffix could outlive is copied: storage in the arena, in
     * its older generation, or never released is shared. */
    if (len == 0u || a->hashcons ||
        (expression->arena_id != a->identity && expression->arena_id != 0u &&
         (a->older_identity == 0u ||
          expression->arena_id != a->older_identity)))
        return atom_expr(a, elems, len);
    Atom *suffix = arena_alloc(a, sizeof(Atom));
    *suffix = (Atom){
        .kind = ATOM_EXPR,
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = a->identity,
        .expr = {
            .elems = elems,
            .len = len,
        },
    };
    /* The suffix's summary is the list's, bit by bit
     * (SuffixSummary.lean): a bit the departed elements did not hold, or
     * held with every other element, is the list's; a bit a departed element
     * held is the suffix's when a remaining element holds it, and a bit that
     * every element must hold, which a departed element lacked, is the
     * suffix's when no remaining element lacks it.  A scan of the suffix
     * settles only those bits and stops at the first element that settles
     * the last of them, so a walk by suffixes scans each gap between two
     * holders of a bit once.  A list whose own head adjusts its summary, or
     * whose facts are not all known, is folded afresh. */
    uint32_t departed_any = 0u;
    uint32_t departed_lacks = 0u;
    uint32_t departed_facts = 0u;
    uint32_t departed_lacks_facts = 0u;
    bool departed_known = true;
    /* A list in the older generation is closed for that arena, not this
     * one, so the view's closure is folded afresh for this arena. */
    bool closure_inherits =
        expression->arena_id == a->identity || expression->arena_id == 0u;
    bool generation_inherits = true;
    for (CettaExprIndex index = 0u; index < offset; index++) {
        Atom *departed = expression->expr.elems[index];
        if (!departed) {
            departed_known = false;
            break;
        }
        departed_any |= departed->flags;
        departed_lacks |= ~departed->flags;
        departed_facts |= departed->structural_facts;
        departed_lacks_facts |= ~departed->structural_facts;
        closure_inherits = closure_inherits &&
            atom_summary_closed_child(departed, a->identity);
        generation_inherits = generation_inherits &&
            atom_generation_admits(a->identity, a->older_identity, departed);
    }
    /* A symbolic term is one whose every element is: a fact the suffix
     * holds when no remaining element lacks it, as a required flag. */
    const uint32_t facts_all = ATOM_STRUCTURAL_SYMBOLIC;
    const uint32_t facts_any = ~(ATOM_STRUCTURAL_FACTS_VALID |
                                 ATOM_STRUCTURAL_GENERATION_CLOSED |
                                 ATOM_STRUCTURAL_OWNER_CLOSED |
                                 ATOM_STRUCTURAL_FRONT_SLACK | facts_all);
    const uint32_t owner_evidence = ATOM_STRUCTURAL_FACTS_VALID |
                                    ATOM_STRUCTURAL_OWNER_CLOSED;
    if (departed_known &&
        !atom_summary_head_adjusts(expression->expr.elems[0]) &&
        (expression->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u) {
        uint32_t flags = expression->flags & ~ATOM_FLAG_HE_EVALUATED;
        /* A new expression: an evaluated mark is not inherited. */
        uint32_t facts = expression->structural_facts &
                         ~(ATOM_STRUCTURAL_GENERATION_CLOSED |
                           ATOM_STRUCTURAL_OWNER_CLOSED |
                           ATOM_STRUCTURAL_FRONT_SLACK);
        uint32_t find_any = departed_any & flags & ATOM_SUMMARY_INHERITED;
        uint32_t find_facts = departed_facts & facts & facts_any;
        uint32_t find_all = departed_lacks & ~flags & ATOM_SUMMARY_REQUIRED;
        uint32_t find_all_facts = departed_lacks_facts & ~facts & facts_all;
        for (CettaExprIndex index = 0u;
             (find_any | find_facts | find_all | find_all_facts) != 0u &&
             index < len;
             index++) {
            const Atom *child = elems[index];
            find_any &= ~child->flags;
            find_facts &= ~child->structural_facts;
            find_all &= child->flags;
            find_all_facts &= child->structural_facts;
        }
        flags &= ~find_any;
        facts &= ~find_facts;
        flags |= find_all;
        facts |= find_all_facts;
        flags = atom_summary_head_adjust(elems[0], flags);
        if (!closure_inherits) {
            /* A departed child was open for the arena, so closure is folded
             * afresh, up to the first open child that remains.  Each child
             * ends at most one such fold over a walk by suffixes. */
            flags |= ATOM_FLAG_ARENA_CLOSED;
            for (CettaExprIndex index = 0u; index < len; index++) {
                if (!atom_summary_closed_child(elems[index], a->identity)) {
                    flags &= ~ATOM_FLAG_ARENA_CLOSED;
                    break;
                }
            }
        }
        /* The generation bit asks every child the view keeps to be admitted
         * by the view's arena.  When the departed children are, the
         * parent's answer is the view's: a parent in that arena gives its
         * own, and a parent in the older generation, which has none of its
         * own, is admitted exactly when it is closed there.  Otherwise the
         * bit is folded afresh up to the first child not admitted, as the
         * closure bit is. */
        if (a->older_identity != 0u &&
            (flags & ATOM_FLAG_ARENA_CLOSED) == 0u) {
            bool generation = true;
            if (generation_inherits) {
                generation = expression->arena_id == a->identity
                    ? atom_generation_closed(expression)
                    : (expression->flags & ATOM_FLAG_ARENA_CLOSED) != 0u;
            } else {
                for (CettaExprIndex index = 0u; index < len; index++) {
                    if (!atom_generation_admits(
                            a->identity, a->older_identity, elems[index])) {
                        generation = false;
                        break;
                    }
                }
            }
            if (generation)
                facts |= ATOM_STRUCTURAL_GENERATION_CLOSED;
        }
        /* The view shares its list's element vector, which may belong to
         * the older generation even when the list's header is here: a view
         * of a view keeps borrowing it, and dropping children never moves
         * it.  So the evidence is inherited from a list in this arena and
         * recomputed from the remaining children only when the list's
         * vector is its own arena's storage. */
        if ((flags & ATOM_FLAG_ARENA_CLOSED) == 0u &&
            (expression->arena_id == 0u || expression->arena_id == a->identity)) {
            bool owner_closed =
                (expression->structural_facts & owner_evidence) == owner_evidence;
            if (!owner_closed && atom_expr_vector_in_own_arena(expression)) {
                owner_closed = true;
                for (CettaExprIndex index = 0u; index < len; index++) {
                    if (!atom_graph_is_owner_closed_for_arena(a, elems[index])) {
                        owner_closed = false;
                        break;
                    }
                }
            }
            if (owner_closed)
                facts |= ATOM_STRUCTURAL_OWNER_CLOSED;
        }
        suffix->flags = flags;
        suffix->structural_facts = facts;
        /* The suffix's variables are among the list's: the list's single
         * variable is the suffix's when the suffix has one, and a list with
         * several whose departed elements held none leaves several.  Where a
         * departed element held some, the suffix may keep one: a short scan
         * decides it when the suffix is short or shows two variables early,
         * and none otherwise stands for several or unknown, which every
         * reader of the summary treats as a subterm to walk. */
        VarId var_id = VAR_ID_NONE;
        if ((flags & ATOM_FLAG_HAS_VARS) != 0u) {
            var_id = expression->var_id;
            if (var_id_is_none(var_id) &&
                (departed_any & ATOM_FLAG_HAS_VARS) != 0u) {
                VarId single = VAR_ID_NONE;
                bool several = false;
                CettaExprIndex index = 0u;
                for (; index < len && index < ATOM_SUFFIX_VARIABLE_SCAN;
                     index++) {
                    const Atom *child = elems[index];
                    if ((child->flags & ATOM_FLAG_HAS_VARS) == 0u)
                        continue;
                    VarId child_single = atom_single_variable_id(child);
                    if (var_id_is_none(child_single) ||
                        (!var_id_is_none(single) && !var_id_eq(single, child_single))) {
                        several = true;
                        break;
                    }
                    single = child_single;
                }
                var_id = !several && index == len ? single : VAR_ID_NONE;
            }
        }
        suffix->var_id = var_id;
    } else {
        suffix->flags = atom_flags_from_children(
            a->identity, a->older_identity, elems, len,
            &suffix->structural_facts);
        /* The fold sees the children, not the vector they sit in: the
         * evidence stands only where the list vouches for that vector, as
         * in the inherited summary above. */
        if ((expression->arena_id != 0u &&
             expression->arena_id != a->identity) ||
            ((expression->structural_facts & owner_evidence) != owner_evidence &&
             !atom_expr_vector_in_own_arena(expression)))
            suffix->structural_facts &= ~ATOM_STRUCTURAL_OWNER_CLOSED;
        suffix->var_id = atom_single_variable_id_of(
            suffix->flags, elems, len);
    }
    return suffix;
}

Atom *atom_expr_view_rehome(Arena *a, const Atom *view, Atom **elems) {
    if (!a || !view || !elems || view->kind != ATOM_EXPR ||
        view->expr.len == 0u || a->hashcons)
        return NULL;
    Atom *header = arena_alloc(a, sizeof(Atom));
    if (!header)
        return NULL;
    *header = (Atom){
        .kind = ATOM_EXPR,
        /* Relocated storage may contain renamed cells. Keep variable
         * support unknown and do not inherit a hash of the old identities. */
        .flags = view->flags & ~ATOM_FLAG_ARENA_CLOSED,
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = a->identity,
        .hash_cache = 0u,
        .structural_facts = view->structural_facts &
            ~(ATOM_STRUCTURAL_GENERATION_CLOSED | ATOM_STRUCTURAL_OWNER_CLOSED |
              ATOM_STRUCTURAL_FRONT_SLACK),
        .expr = {
            .elems = elems,
            .len = view->expr.len,
        },
    };
    return header;
}

/* The lowest slot of a buffer with front slack.  It is never free, so a view
 * whose next slot down holds it has no room to claim. */
static const Atom atom_front_slack_floor;

/* The singleton-variable summary of two summaries folded together. */
static VarId atom_single_variable_join(const Atom *left, const Atom *right) {
    bool left_vars = (left->flags & ATOM_FLAG_HAS_VARS) != 0u;
    bool right_vars = (right->flags & ATOM_FLAG_HAS_VARS) != 0u;
    if (!left_vars)
        return right_vars ? atom_single_variable_id(right) : VAR_ID_NONE;
    if (!right_vars)
        return atom_single_variable_id(left);
    VarId single = atom_single_variable_id(left);
    return var_id_eq(single, atom_single_variable_id(right)) ? single : VAR_ID_NONE;
}

Atom *atom_expr_prepend(Arena *a, Atom *head, Atom *list) {
    if (!a || !head || !list || list->kind != ATOM_EXPR)
        return NULL;
    CettaExprLen len = list->expr.len;
    if (len == UINT64_MAX)
        cetta_oom(SIZE_MAX);
    CettaExprLen count = len + 1u;
    if (a->hashcons) {
        /* An interning arena shares expressions, so none has room of its
         * own. */
        if (!cetta_expr_len_mul_fits_size(count, sizeof(Atom *)))
            cetta_oom(SIZE_MAX);
        Atom **joined = cetta_malloc((size_t)count * sizeof(Atom *));
        joined[0] = head;
        if (len > 0u)
            memcpy(joined + 1u, list->expr.elems, (size_t)len * sizeof(Atom *));
        Atom *interned = atom_expr(a, joined, count);
        free(joined);
        return interned;
    }

    /* The list starts at its buffer's front exactly when the slot below it
     * is free (PrefixBuffer.claimable_iff_free_below); claiming it lowers the
     * front, and no view of the buffer ever reads that slot again. */
    Atom **elems = NULL;
    if ((list->structural_facts & ATOM_STRUCTURAL_FRONT_SLACK) != 0u &&
        list->arena_id == a->identity && len > 0u &&
        list->expr.elems[-1] == NULL) {
        elems = list->expr.elems - 1;
        elems[0] = head;
    } else {
        /* A fresh buffer: the floor, as many free slots as the new list is
         * long, then the new list (PrefixBuffer.fresh_packed). */
        if (count > (UINT64_MAX - 1u) / 2u ||
            !cetta_expr_len_mul_fits_size(2u * count + 1u, sizeof(Atom *)))
            cetta_oom(SIZE_MAX);
        Atom **buffer = arena_alloc(
            a, (size_t)(2u * count + 1u) * sizeof(Atom *));
        buffer[0] = (Atom *)&atom_front_slack_floor;
        memset(buffer + 1u, 0, (size_t)count * sizeof(Atom *));
        elems = buffer + 1u + count;
        elems[0] = head;
        if (len > 0u)
            memcpy(elems + 1u, list->expr.elems, (size_t)len * sizeof(Atom *));
    }

    Atom *at = arena_alloc(a, sizeof(Atom));
    *at = (Atom){
        .kind = ATOM_EXPR,
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = a->identity,
        .expr = {
            .elems = elems,
            .len = count,
        },
    };
    const uint32_t generation = ATOM_STRUCTURAL_FACTS_VALID |
                                ATOM_STRUCTURAL_GENERATION_CLOSED;
    bool list_folded = len > 0u && list->arena_id == a->identity &&
        !atom_summary_head_adjusts(list->expr.elems[0]) &&
        (list->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u &&
        (head->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) != 0u;
    if (list_folded) {
        /* The list's summary is its fold, since its head adjusts nothing;
         * the new head steps onto it and then adjusts it
         * (TailSummary.summary_prepend). */
        uint32_t flags = list->flags & ~ATOM_FLAG_HE_EVALUATED;
        /* A new expression: an evaluated mark is not inherited. */
        flags |= head->flags & ATOM_SUMMARY_INHERITED;
        flags &= head->flags | ~ATOM_SUMMARY_REQUIRED;
        bool head_closed = atom_summary_closed_child(head, a->identity);
        if (!head_closed)
            flags &= ~ATOM_FLAG_ARENA_CLOSED;
        flags = atom_summary_head_adjust(head, flags);
        /* The facts are the list's and the head's, as a fold gives them:
         * a string's storage stays with the string, and the term is
         * symbolic only when the head and every element of the list are. */
        uint32_t facts =
            (list->structural_facts & ~(ATOM_STRUCTURAL_GENERATION_CLOSED |
                                        ATOM_STRUCTURAL_OWNER_CLOSED |
                                        ATOM_STRUCTURAL_FRONT_SLACK)) |
            (head->structural_facts & ~(ATOM_STRUCTURAL_FACTS_VALID |
                                        ATOM_STRUCTURAL_GENERATION_CLOSED |
                                        ATOM_STRUCTURAL_OWNER_CLOSED |
                                        ATOM_STRUCTURAL_FRONT_SLACK |
                                        ATOM_STRUCTURAL_STRING_STORAGE));
        if ((list->structural_facts & head->structural_facts &
             ATOM_STRUCTURAL_SYMBOLIC) == 0u)
            facts &= ~ATOM_STRUCTURAL_SYMBOLIC;
        if ((flags & ATOM_FLAG_ARENA_CLOSED) == 0u &&
            atom_graph_is_owner_closed_for_arena(a, list) &&
            atom_graph_is_owner_closed_for_arena(a, head))
            facts |= ATOM_STRUCTURAL_OWNER_CLOSED;
        /* Every child is admitted by the generations when the list's are,
         * which its closure or its own generation bit says, and the head is
         * too. */
        if (a->older_identity != 0u &&
            (flags & ATOM_FLAG_ARENA_CLOSED) == 0u &&
            ((list->flags & ATOM_FLAG_ARENA_CLOSED) != 0u ||
             (list->structural_facts & generation) == generation) &&
            (head_closed ||
             atom_generation_admits(a->identity, a->older_identity, head)))
            facts |= ATOM_STRUCTURAL_GENERATION_CLOSED;
        at->flags = flags;
        at->structural_facts = facts | ATOM_STRUCTURAL_FRONT_SLACK;
        at->var_id = atom_single_variable_join(head, list);
    } else {
        uint32_t facts = 0u;
        at->flags = atom_flags_from_children(
            a->identity, a->older_identity, elems, count, &facts);
        at->structural_facts = facts | ATOM_STRUCTURAL_FRONT_SLACK;
        at->var_id = atom_single_variable_id_of(at->flags, elems, count);
    }
    return at;
}

Atom *atom_expr_builder_begin(Arena *a, CettaExprLen len) {
    if (!a)
        return NULL;
    if (len > 0 && !cetta_expr_len_mul_fits_size(len, sizeof(Atom *)))
        cetta_oom(SIZE_MAX);
    size_t elems_bytes = (size_t)len * sizeof(Atom *);
    if (elems_bytes > SIZE_MAX - sizeof(Atom))
        cetta_oom(SIZE_MAX);
    Atom *draft = arena_alloc(a, sizeof(Atom) + elems_bytes);
    *draft = (Atom){
        .kind = ATOM_EXPR,
        .var_id = VAR_ID_NONE,
        .sym_id = SYMBOL_ID_NONE,
        .arena_id = a->identity,
        .expr = {
            .elems = elems_bytes ? (Atom **)(draft + 1) : NULL,
            .len = len,
        },
    };
    return draft;
}

Atom *atom_expr_builder_finish(Arena *a, Atom *draft) {
    if (!a || !draft || draft->kind != ATOM_EXPR ||
        draft->arena_id != a->identity)
        return NULL;
    draft->flags = atom_flags_from_children(
        a->identity, a->older_identity, draft->expr.elems, draft->expr.len,
        &draft->structural_facts);
    if (atom_list_rest_parts_splice(draft->expr.elems, draft->expr.len,
                                    draft->structural_facts))
        return atom_list_with_rest(a, draft->expr.elems + 1,
                                   draft->expr.len - 2u,
                                   draft->expr.elems[draft->expr.len - 1u]);
    draft->var_id = atom_single_variable_id_of(
        draft->flags, draft->expr.elems, draft->expr.len);
    draft->hash_cache = 0u;
    Atom *shared = atom_maybe_hashcons(a, draft);
    return shared ? shared : draft;
}

Atom *atom_expr_shared(Arena *a, Atom **elems, CettaExprLen len) {
    return atom_expr(a, elems, len);
}

/* An expression of `elems` carrying HE's evaluated mark.  It is never
 * interned: it is built outside the hash-cons table, interning admits only a
 * node whose flags are exactly those its children determine, which the mark
 * is not, and an interned node's children are global, which a marked node is
 * not.  Its other summaries are its children's, as for any expression. */
static Atom *atom_he_evaluated_build(Arena *a, Atom **elems,
                                     CettaExprLen len) {
    uint32_t structural_facts = 0u;
    uint32_t flags = atom_flags_from_children(
        a->identity, a->older_identity, elems, len, &structural_facts);
    VarId var_id = atom_single_variable_id_of(flags, elems, len);
    if (structural_facts & ATOM_STRUCTURAL_FACTS_VALID)
        structural_facts |= ATOM_STRUCTURAL_HAS_HE_EVALUATED;
    return atom_expr_build(a, elems, len, flags | ATOM_FLAG_HE_EVALUATED,
                           structural_facts, var_id);
}

Atom *atom_he_evaluated_copy(Arena *a, Atom *expression) {
    if (!a || !expression || expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        (expression->flags & ATOM_FLAG_HE_EVALUATED) != 0u)
        return expression;
    return atom_he_evaluated_build(
        a, expression->expr.elems, expression->expr.len);
}

Atom *atom_expr2(Arena *a, Atom *a1, Atom *a2) {
    Atom *elems[2] = {a1, a2};
    return atom_expr(a, elems, 2);
}

Atom *atom_expr3(Arena *a, Atom *a1, Atom *a2, Atom *a3) {
    Atom *elems[3] = {a1, a2, a3};
    return atom_expr(a, elems, 3);
}

/* ── Special atoms ──────────────────────────────────────────────────────── */

Atom *atom_empty(Arena *a) { return atom_symbol_id(a, g_builtin_syms.empty); }
Atom *atom_unit(Arena *a)  { return atom_expr(a, NULL, 0); }
Atom *atom_true(Arena *a)  { return atom_bool(a, true); }
Atom *atom_false(Arena *a) { return atom_bool(a, false); }

/* ── Type system atoms ──────────────────────────────────────────────────── */

Atom *atom_undefined_type(Arena *a) { return atom_symbol_id(a, g_builtin_syms.undefined_type); }
Atom *atom_atom_type(Arena *a)      { return atom_symbol_id(a, g_builtin_syms.atom); }
Atom *atom_symbol_type(Arena *a)    { return atom_symbol_id(a, g_builtin_syms.symbol); }
Atom *atom_variable_type(Arena *a)  { return atom_symbol_id(a, g_builtin_syms.variable); }
Atom *atom_expression_type(Arena *a){ return atom_symbol_id(a, g_builtin_syms.expression); }
Atom *atom_grounded_type(Arena *a)  { return atom_symbol_id(a, g_builtin_syms.grounded); }
Atom *atom_list_type(Arena *a)      { return atom_symbol_id(a, g_builtin_syms.list_type); }

SymbolId atom_meta_type_id(const Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:   return g_builtin_syms.symbol;
    case ATOM_VAR:      return g_builtin_syms.variable;
    case ATOM_GROUNDED: return g_builtin_syms.grounded;
    case ATOM_EXPR:
        return atom_is_list(atom) || atom_is_list_rest(atom)
                   ? g_builtin_syms.list_type : g_builtin_syms.expression;
    }
    return g_builtin_syms.undefined_type;
}

Atom *get_meta_type(Arena *a, Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:   return atom_symbol_type(a);
    case ATOM_VAR:      return atom_variable_type(a);
    case ATOM_GROUNDED: return atom_grounded_type(a);
    case ATOM_EXPR:
        return atom_is_list(atom) || atom_is_list_rest(atom)
                   ? atom_list_type(a) : atom_expression_type(a);
    }
    return atom_undefined_type(a);
}

bool atom_is_meta_type(Atom *type) {
    return atom_is_symbol_id(type, g_builtin_syms.atom) ||
           atom_is_symbol_id(type, g_builtin_syms.symbol) ||
           atom_is_symbol_id(type, g_builtin_syms.variable) ||
           atom_is_symbol_id(type, g_builtin_syms.expression) ||
           atom_is_symbol_id(type, g_builtin_syms.grounded);
}

bool atom_meta_type_accepts(Arena *a, Atom *formal, Atom *actual) {
    if (atom_is_symbol_id(formal, g_builtin_syms.atom))
        return true;
    if (!atom_is_meta_type(formal))
        return false;
    /* The sequence operations read a list as they read an expression, so a
     * list may stand where an Expression is taken.  Not the converse. */
    if (atom_is_symbol_id(formal, g_builtin_syms.expression) &&
        atom_is_list(actual))
        return true;
    return atom_eq(formal, get_meta_type(a, actual));
}

Atom *atom_error(Arena *a, Atom *source, Atom *message) {
    return atom_expr3(a, atom_symbol_id(a, g_builtin_syms.error), source, message);
}

/* ── Predicates ─────────────────────────────────────────────────────────── */

bool atom_is_symbol(Atom *a, const char *name) {
    return a && a->kind == ATOM_SYMBOL && symbol_eq_cstr(g_symbols, a->sym_id, name);
}

bool atom_is_empty(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.empty);
}

bool atom_is_error(Atom *a) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol_id(a->expr.elems[0], g_builtin_syms.error);
}

bool atom_is_empty_or_error(Atom *a) {
    return atom_is_empty(a) || atom_is_error(a);
}

bool atom_is_var(Atom *a)  { return a->kind == ATOM_VAR; }
bool atom_is_expr(Atom *a) { return a->kind == ATOM_EXPR; }

bool atom_is_symbol_id(Atom *a, SymbolId id) {
    return a && a->kind == ATOM_SYMBOL && a->sym_id == id;
}

const char *atom_name_cstr(Atom *a) {
    if (!a) return "";
    if (a->kind == ATOM_SYMBOL ||
        (a->kind == ATOM_VAR && !a->name_key))
        return symbol_bytes(g_symbols, a->sym_id);
    return "";
}

SymbolId atom_head_symbol_id(const Atom *a) {
    if (!a) return SYMBOL_ID_NONE;
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

static size_t atom_visit_slot(const Atom *atom, size_t cap) {
    uintptr_t key = (uintptr_t)atom;
    key ^= key >> 33u;
    key *= (uintptr_t)UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 29u;
    return (size_t)key & (cap - 1u);
}

bool atom_visit_set_add(AtomVisitSet *set, const Atom *atom, bool *fault) {
    if ((set->len + 1u) * 2u > set->cap) {
        size_t cap = set->cap ? set->cap * 2u : 64u;
        const Atom **slots = calloc(cap, sizeof(*slots));
        if (!slots) {
            *fault = true;
            return false;
        }
        for (size_t i = 0u; i < set->cap; i++) {
            if (!set->slots[i])
                continue;
            size_t slot = atom_visit_slot(set->slots[i], cap);
            while (slots[slot])
                slot = (slot + 1u) & (cap - 1u);
            slots[slot] = set->slots[i];
        }
        free((void *)set->slots);
        set->slots = slots;
        set->cap = cap;
    }
    size_t slot = atom_visit_slot(atom, set->cap);
    while (set->slots[slot]) {
        if (set->slots[slot] == atom)
            return false;
        slot = (slot + 1u) & (set->cap - 1u);
    }
    set->slots[slot] = atom;
    set->len++;
    return true;
}

void atom_visit_set_free(AtomVisitSet *set) {
    free((void *)set->slots);
    *set = (AtomVisitSet){0};
}

static inline __attribute__((always_inline)) bool atom_walk_any(
    const Atom *root, AtomTreePredicate predicate, AtomTreePredicate barrier,
    void *context, bool unique, size_t allowance, bool *complete) {
    enum { INLINE_CAPACITY = 64 };
    const Atom *inline_stack[INLINE_CAPACITY];
    const Atom **stack = inline_stack;
    size_t capacity = INLINE_CAPACITY;
    size_t length = 0u;
    AtomVisitSet seen = {0};
    bool fault = false;
    bool found = false;

    if (complete)
        *complete = true;
    if (!root)
        return false;
    if (!predicate) {
        if (complete)
            *complete = false;
        return false;
    }
    stack[length++] = root;
    while (length > 0u && !found) {
        const Atom *node = stack[--length];
        if (!node)
            continue;
        /* A subexpression met again was answered where it was first met;
         * where the set cannot grow the walk goes on as a tree walk. */
        if (unique && node != root && node->kind == ATOM_EXPR && node->expr.len &&
            !fault && !atom_visit_set_add(&seen, node, &fault) && !fault)
            continue;
        if (complete && fault)
            goto incomplete;
        if (barrier && barrier(node, context))
            continue;
        if (complete) {
            if (!allowance)
                goto incomplete;
            allowance--;
        }
        if (predicate(node, context)) {
            found = true;
            break;
        }
        if (node->kind != ATOM_EXPR || node->expr.len == 0u)
            continue;
        if (node->expr.len > (CettaExprLen)(SIZE_MAX - length)) {
            if (complete)
                goto incomplete;
            cetta_oom(SIZE_MAX);
        }
        size_t needed = length + (size_t)node->expr.len;
        if (needed > capacity) {
            size_t next_capacity = capacity;
            while (next_capacity < needed) {
                if (next_capacity > SIZE_MAX / 2u) {
                    next_capacity = needed;
                    break;
                }
                next_capacity *= 2u;
            }
            if (next_capacity > SIZE_MAX / sizeof(*stack)) {
                if (complete)
                    goto incomplete;
                cetta_oom(SIZE_MAX);
            }
            if (stack == inline_stack) {
                const Atom **grown = complete
                    ? malloc(next_capacity * sizeof(*grown))
                    : cetta_malloc(next_capacity * sizeof(*grown));
                if (!grown)
                    goto incomplete;
                memcpy((void *)grown, inline_stack,
                       length * sizeof(*grown));
                stack = grown;
            } else {
                const Atom **grown = complete
                    ? realloc((void *)stack, next_capacity * sizeof(*stack))
                    : cetta_realloc((void *)stack, next_capacity * sizeof(*stack));
                if (!grown)
                    goto incomplete;
                stack = grown;
            }
            capacity = next_capacity;
        }
        for (CettaExprIndex i = node->expr.len; i > 0u; i--)
            stack[length++] = node->expr.elems[i - 1u];
    }
    goto finished;
incomplete:
    if (complete)
        *complete = false;
finished:
    if (stack != inline_stack)
        free((void *)stack);
    if (unique)
        atom_visit_set_free(&seen);
    return found;
}


bool atom_tree_any(const Atom *root, AtomTreePredicate predicate, void *context) {
    return atom_walk_any(root, predicate, NULL, context, false, SIZE_MAX, NULL);
}

bool atom_graph_any(const Atom *root, AtomTreePredicate predicate, void *context) {
    return atom_walk_any(root, predicate, NULL, context, true, SIZE_MAX, NULL);
}

bool atom_tree_any_pruned(const Atom *root, AtomTreePredicate predicate,
                          AtomTreePredicate barrier, void *context) {
    return atom_walk_any(root, predicate, barrier, context, false, SIZE_MAX, NULL);
}

bool atom_graph_any_pruned(const Atom *root, AtomTreePredicate predicate,
                           AtomTreePredicate barrier, void *context) {
    return atom_walk_any(root, predicate, barrier, context, true, SIZE_MAX, NULL);
}

bool atom_graph_any_checked(const Atom *root, AtomTreePredicate predicate,
                            AtomTreePredicate barrier, void *context,
                            size_t allowance, bool *complete) {
    assert(complete);
    return atom_walk_any(root, predicate, barrier, context, true, allowance, complete);
}

/* ── Comparison ─────────────────────────────────────────────────────────── */

CettaLanguageId eval_current_language_id(void) __attribute__((weak));
bool eval_current_uses_rust_he_compat_semantics(void) __attribute__((weak));

/* The HE lane outside he-compat, where numbers compare by exact value. */
static bool atom_he_exact_semantics(void) {
    return eval_current_language_id &&
           eval_current_language_id() == CETTA_LANGUAGE_HE &&
           !(eval_current_uses_rust_he_compat_semantics &&
             eval_current_uses_rust_he_compat_semantics());
}

static bool cetta_number_kind_as_float(int kind, int64_t ival, double fval,
                                       double *out) {
    if (!out)
        return false;
    if (kind == GV_INT) {
        *out = (double)ival;
        return true;
    }
    if (kind == GV_FLOAT) {
        *out = fval;
        return true;
    }
    return false;
}

static bool float_is_int64(double value, int64_t integer) {
    if (!(value >= -0x1p63 && value < 0x1p63))
        return false;
    int64_t truncated = (int64_t)value;
    return truncated == integer && (double)truncated == value;
}

bool cetta_he_promoted_kind_equal(int left_kind, int64_t left_int,
                                  double left_float, int right_kind,
                                  int64_t right_int, double right_float) {
    double left, right;
    if (!eval_current_language_id ||
        eval_current_language_id() != CETTA_LANGUAGE_HE ||
        left_kind == right_kind)
        return false;
    if (!cetta_number_kind_as_float(left_kind, left_int, left_float, &left) ||
        !cetta_number_kind_as_float(right_kind, right_int, right_float, &right))
        return false;
    /* Hyperon compares the integer rounded to a float, so it finds
     * 9007199254740993 equal to 9007199254740992.0; he-compat keeps that.
     * Otherwise an integer equals only the float of exactly its value. */
    if (eval_current_uses_rust_he_compat_semantics &&
        eval_current_uses_rust_he_compat_semantics())
        return left == right;
    return left_kind == GV_INT
        ? float_is_int64(right_float, left_int)
        : float_is_int64(left_float, right_int);
}

static bool atom_numbers_value_eq(Atom *a, Atom *b);

/* Numbers of different kinds, in the HE lane.  Outside he-compat they are
 * equal when their exact values are, whatever the kinds: 1/2 equals 0.5 and
 * a bigint equals the float of its value.  he-compat keeps Hyperon's rule,
 * which knows only integers and floats. */
bool cetta_he_promoted_numbers_equal(const Atom *left, const Atom *right) {
    if (!left || !right ||
        left->kind != ATOM_GROUNDED || right->kind != ATOM_GROUNDED)
        return false;
    if (atom_he_exact_semantics() &&
        atom_is_number(left) && atom_is_number(right))
        return atom_numbers_value_eq((Atom *)left, (Atom *)right);
    return cetta_he_promoted_kind_equal(
        left->ground.gkind, left->ground.ival, left->ground.fval,
        right->ground.gkind, right->ground.ival, right->ground.fval);
}

bool cetta_he_float_exact_int(double value, int64_t *out) {
    if (!out || !eval_current_language_id ||
        eval_current_language_id() != CETTA_LANGUAGE_HE ||
        !isfinite(value) ||
        value >= 0x1p63 || value < -0x1p63)
        return false;
    int64_t truncated = (int64_t)value;
    if ((double)truncated != value)
        return false;
    *out = truncated;
    return true;
}

CettaHeFloatIntBranches cetta_he_float_int_branches(double value,
                                                    int64_t *out) {
    int64_t exact = 0;
    /* INT64_MAX rounds up to 2^63 as a double.  That query has integer
     * candidates even though the query itself cannot be cast to int64. */
    if (value == 0x1p63 && eval_current_language_id &&
        eval_current_language_id() == CETTA_LANGUAGE_HE) {
        if (out)
            *out = INT64_MAX;
        return CETTA_HE_FLOAT_INTS_SCAN;
    }
    if (!cetta_he_float_exact_int(value, &exact))
        return CETTA_HE_FLOAT_INTS_NONE;
    if (out)
        *out = exact;
    /* At and above 2^53 a finite float is shared by more than one int64. */
    if (fabs(value) >= 0x1p53)
        return CETTA_HE_FLOAT_INTS_SCAN;
    return CETTA_HE_FLOAT_INTS_ONE;
}

/* Two atoms that share subterms are graphs, and walking them as trees visits
 * a shared pair once per path to it, exponentially often in the depth of the
 * sharing.  Past a few steps an equality walk therefore remembers the
 * expression pairs it has begun.  The walk stops at the first difference and
 * an expression cannot contain itself, so a pair met again was found equal;
 * its first meeting compares it, so a NaN still differs from itself.  A state
 * cell's value is compared by a walk of its own.  The memory is a
 * direct-mapped cache that forgets on a collision and allocates nothing. */
#define ATOM_EQ_MEMO_AFTER 32u
#define ATOM_EQ_MEMO_SLOTS 1024u

typedef struct {
    const Atom *left;
    const Atom *right;
    uint64_t walk;
} AtomEqMemoSlot;

static _Thread_local AtomEqMemoSlot g_atom_eq_memo[ATOM_EQ_MEMO_SLOTS];
static _Thread_local uint64_t g_atom_eq_walks;

typedef struct {
    bool all_numbers;
    uint32_t steps;
    uint64_t id;
} AtomEqWalk;

static bool atom_eq_memo_met(AtomEqWalk *walk, const Atom *a, const Atom *b) {
    if (walk->steps < ATOM_EQ_MEMO_AFTER) {
        walk->steps++;
        return false;
    }
    if (!walk->id)
        walk->id = ++g_atom_eq_walks;
    uint64_t key = ((uint64_t)(uintptr_t)a * UINT64_C(0x9E3779B97F4A7C15)) ^
                   (uint64_t)(uintptr_t)b;
    AtomEqMemoSlot *slot =
        &g_atom_eq_memo[(key >> 32) & (ATOM_EQ_MEMO_SLOTS - 1u)];
    if (slot->walk == walk->id && slot->left == a && slot->right == b)
        return true;
    *slot = (AtomEqMemoSlot){a, b, walk->id};
    return false;
}

/* A state cell can hold itself, so two cells compare by the values they
 * hold coinductively: a pair of cells met again on the way down is taken as
 * equal, as cyclic terms compare by their unfolding, and the comparison ends.
 * A cell is equal to itself whatever it holds, as a Hyperon state is, except
 * in the HE lane outside he-compat, where a cell holding a NaN is not. */
typedef struct {
    const StateCell *left;
    const StateCell *right;
} StateCellPair;

static _Thread_local StateCellPair *g_state_cell_pairs;
static _Thread_local uint32_t g_state_cell_pairs_len;
static _Thread_local uint32_t g_state_cell_pairs_cap;

static bool atom_numbers_value_eq(Atom *a, Atom *b);
static bool atom_petta_leaf_eq(Atom *a, Atom *b);

/* PeTTa compares floats as Prolog terms, so 0.0 and -0.0 differ and a NaN is
 * itself; HE compares their IEEE values.  The two rules agree except on the
 * two zeros and on a NaN, which alone ask the lane. */
static inline bool atom_float_eq(double x, double y) {
    bool same_bits = memcmp(&x, &y, sizeof(x)) == 0;
    if (same_bits ? x == x : x != y)
        return same_bits;
    return eval_current_language_id &&
            eval_current_language_id() == CETTA_LANGUAGE_PETTA
        ? same_bits : x == y;
}

/* The comparisons equality decides without a walk, with the walk's answer:
 * an atom that holds no NaN is equal to itself in every lane, atoms of
 * different kinds differ, and symbols, variables and integers compare by
 * identity or value.  False when the pair needs the walk. */
static inline __attribute__((always_inline)) bool atom_eq_decided(
    const Atom *a, const Atom *b, bool *equal) {
    if (a == b && !atom_structural_may_have_nan(a)) {
        *equal = true;
        return true;
    }
    if (a->kind != b->kind) {
        /* A rational term is equal to its open forms. */
        if (atom_structural_has_rational(a) ||
            atom_structural_has_rational(b))
            return false;
        *equal = false;
        return true;
    }
    switch (a->kind) {
    case ATOM_SYMBOL:
        *equal = a->sym_id == b->sym_id;
        return true;
    case ATOM_VAR:
        *equal = var_id_eq(a->var_id, b->var_id);
        return true;
    case ATOM_GROUNDED:
        if (a->ground.gkind != GV_INT || b->ground.gkind != GV_INT)
            return false;
        *equal = a->ground.ival == b->ground.ival;
        return true;
    case ATOM_EXPR:
        if (a->expr.len == b->expr.len ||
            atom_structural_has_rational(a) ||
            atom_structural_has_rational(b))
            return false;
        *equal = false;
        return true;
    }
    return false;
}

/* Most comparisons visit only symbols, variables, integers and a few
 * expression levels. Try those with a fixed, bounded stack; unusual leaves,
 * rational views and deeper terms go through the complete heap-framed
 * driver. The trial cannot make a speculative equality decision. */
static int atom_eq_small_tree(Atom *left, Atom *right) {
    typedef struct {
        Atom *left;
        Atom *right;
        CettaExprIndex next;
    } Frame;
    Frame frames[16];
    Frame *top = frames;
    for (;;) {
        if (left != right || atom_structural_may_have_nan(left)) {
            if (left->kind != right->kind) {
                if (atom_structural_has_rational(left) ||
                    atom_structural_has_rational(right))
                    return -1;
                return 0;
            }
            switch (left->kind) {
            case ATOM_SYMBOL:
                if (left->sym_id != right->sym_id)
                    return 0;
                break;
            case ATOM_VAR:
                if (!var_id_eq(left->var_id, right->var_id))
                    return 0;
                break;
            case ATOM_GROUNDED:
                if (left->ground.gkind != GV_INT || right->ground.gkind != GV_INT)
                    return -1;
                if (left->ground.ival != right->ground.ival)
                    return 0;
                break;
            case ATOM_EXPR:
                if (atom_structural_has_rational(left) ||
                    atom_structural_has_rational(right))
                    return -1;
                if (left->expr.len != right->expr.len)
                    return 0;
                if (top == frames + 16u)
                    return -1;
                *top++ = (Frame){left, right, 0u};
                break;
            }
        }
        while (top != frames && top[-1].next == top[-1].left->expr.len)
            top--;
        if (top == frames)
            return 1;
        Frame *frame = top - 1;
        CettaExprIndex index = frame->next++;
        left = frame->left->expr.elems[index];
        right = frame->right->expr.elems[index];
    }
}

/* An expression with decidable immediate children needs no saved parent.
 * Declining at a compound or unfamiliar child leaves the full walk authoritative. */
static inline int atom_eq_flat_tree(Atom *left, Atom *right) {
    if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
        atom_structural_has_rational(left) ||
        atom_structural_has_rational(right))
        return -1;
    if (left->expr.len != right->expr.len)
        return 0;
    for (CettaExprIndex index = 0u; index < left->expr.len; index++) {
        bool equal = false;
        if (!atom_eq_decided(left->expr.elems[index], right->expr.elems[index], &equal))
            return -1;
        if (!equal)
            return 0;
    }
    return 1;
}

/* The ordinary pointer/leaf fast path needs no frame of its own.  Small trees
 * compare at once; beyond them, symbolic atoms whose structural hashes are
 * already known differ when the hashes do, whatever their depth. */
static __attribute__((noinline)) bool atom_eq_walk(Atom *a, Atom *b) {
    int small = atom_eq_flat_tree(a, b);
    if (small >= 0)
        return small != 0;
    if ((a->structural_facts & b->structural_facts &
         ATOM_STRUCTURAL_SYMBOLIC) != 0u) {
        uint32_t left_hash = atom_hash_cache_load(a);
        uint32_t right_hash = atom_hash_cache_load(b);
        if (left_hash != 0u && right_hash != 0u && left_hash != right_hash)
            return false;
    }
    small = atom_eq_small_tree(a, b);
    if (small >= 0)
        return small != 0;
    return atom_compare_driver(a, b, ATOM_COMPARE_STRUCTURAL, false);
}

bool atom_eq(Atom *a, Atom *b) {
    bool equal = false;
    if (atom_eq_decided(a, b, &equal))
        return equal;
    return atom_eq_walk(a, b);
}

/* The leaves of a rational term's unfolding are atomic, so they compare as
 * atoms. */
static bool atom_eq_leaf(Atom *a, Atom *b) {
    return atom_eq(a, b);
}

typedef enum {
    ATOM_COMPARE_PAIR,
    ATOM_COMPARE_EXPR,
    ATOM_COMPARE_CONTEXT,
    ATOM_COMPARE_STATE_END
} AtomCompareTask;

typedef struct {
    Atom *left;
    Atom *right;
    const CettaPrimeContext *context_left;
    const CettaPrimeContext *context_right;
    CettaExprIndex next;
    size_t scope;
    AtomCompareMode mode;
    AtomCompareTask task;
    unsigned stage;
    bool owns_scope;
} AtomCompareFrame;

typedef struct {
    AtomCompareFrame inline_frames[16];
    AtomCompareFrame *frames;
    size_t len;
    size_t capacity;
    AtomEqWalk inline_scopes[16];
    AtomEqWalk *scopes;
    size_t scope_len;
    size_t scope_capacity;
} AtomCompareStack;

static inline __attribute__((always_inline)) void atom_compare_push(
                              AtomCompareStack *work, Atom *left, Atom *right,
                              AtomCompareMode mode, size_t scope,
                              bool fresh_scope) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATOM_COMPARE_PAIR);
    if (fresh_scope) {
        if (work->scope_len == work->scope_capacity) {
            size_t grown = work->scope_capacity * 2u;
            if (grown < work->scope_capacity ||
                grown > SIZE_MAX / sizeof(*work->scopes))
                cetta_oom(SIZE_MAX);
            if (work->scopes == work->inline_scopes) {
                work->scopes = cetta_malloc(grown * sizeof(*work->scopes));
                memcpy(work->scopes, work->inline_scopes,
                       work->scope_len * sizeof(*work->scopes));
            } else {
                work->scopes = cetta_realloc(
                    work->scopes, grown * sizeof(*work->scopes));
            }
            work->scope_capacity = grown;
        }
        scope = work->scope_len++;
        work->scopes[scope] = (AtomEqWalk){0};
    }
    if (work->len == work->capacity) {
        size_t grown = work->capacity * 2u;
        if (grown < work->capacity ||
            grown > SIZE_MAX / sizeof(*work->frames))
            cetta_oom(SIZE_MAX);
        if (work->frames == work->inline_frames) {
            work->frames = cetta_malloc(grown * sizeof(*work->frames));
            memcpy(work->frames, work->inline_frames,
                   work->len * sizeof(*work->frames));
        } else {
            work->frames = cetta_realloc(
                work->frames, grown * sizeof(*work->frames));
        }
        work->capacity = grown;
    }
    work->frames[work->len++] = (AtomCompareFrame){
        .left = left, .right = right, .scope = scope, .mode = mode,
        .task = ATOM_COMPARE_PAIR, .owns_scope = fresh_scope};
}

/* Comparisons and context/state returns share one explicit continuation
 * stack.  A nested state value or context key gets the fresh equality-memo
 * scope that its former atom_eq call had.  Active state pairs remain visible
 * to reentrant grounded callbacks, but are unwound at this call's border. */
static bool atom_compare_driver(Atom *left, Atom *right,
                                AtomCompareMode mode, bool all_numbers) {
    AtomCompareStack work;
    work.frames = work.inline_frames;
    work.len = 0u;
    work.capacity = 16u;
    work.scopes = work.inline_scopes;
    work.scope_capacity = 16u;
    work.scope_len = 1u;
    work.scopes[0] = (AtomEqWalk){.all_numbers = all_numbers};
    uint32_t state_base = g_state_cell_pairs_len;
    atom_compare_push(&work, left, right, mode, 0u, false);
    while (work.len) {
        AtomCompareFrame *frame = &work.frames[work.len - 1u];
        Atom *a = frame->left, *b = frame->right;
        if (frame->task == ATOM_COMPARE_EXPR) {
            if (frame->next == a->expr.len)
                goto completed;
            CettaExprIndex index = frame->next++;
            atom_compare_push(&work, a->expr.elems[index],
                              b->expr.elems[index], frame->mode,
                              frame->scope, false);
            continue;
        }
        if (frame->task == ATOM_COMPARE_CONTEXT) {
            const CettaPrimeContext *ca = frame->context_left;
            const CettaPrimeContext *cb = frame->context_right;
            if (!ca || !cb) {
                if (ca != cb)
                    goto comparison_failed;
                goto completed;
            }
            if (frame->stage == 0u) {
                frame->stage = 1u;
                atom_compare_push(&work, ca->key, cb->key,
                                  ATOM_COMPARE_STRUCTURAL, 0u, true);
                continue;
            }
            if (frame->stage == 1u) {
                frame->stage = 2u;
                atom_compare_push(&work, ca->value, cb->value,
                                  ATOM_COMPARE_STRUCTURAL, 0u, true);
                continue;
            }
            frame->context_left = ca->parent;
            frame->context_right = cb->parent;
            frame->stage = 0u;
            continue;
        }
        if (frame->task == ATOM_COMPARE_STATE_END)
            goto completed;

        if (frame->mode == ATOM_COMPARE_INTERN) {
            int leaf = atom_intern_leaf_compare(a, b);
            if (leaf >= 0) {
                if (!leaf)
                    goto comparison_failed;
                goto completed;
            }
            if (a->kind != ATOM_EXPR) {
                if (!atom_eq(a, b))
                    goto comparison_failed;
                goto completed;
            }
            if (a->expr.len != b->expr.len)
                goto comparison_failed;
            frame->task = ATOM_COMPARE_EXPR;
            continue;
        }

        if (frame->mode == ATOM_COMPARE_VALUE) {
            if (atom_is_number(a) && atom_is_number(b)) {
                bool equal;
                if (a->ground.gkind == GV_FLOAT &&
                    b->ground.gkind == GV_FLOAT)
                    equal = a->ground.fval == b->ground.fval ||
                            (all_numbers && isnan(a->ground.fval) &&
                             isnan(b->ground.fval));
                else
                    equal = all_numbers ? atom_numbers_value_eq(a, b)
                                        : atom_eq(a, b);
                if (!equal)
                    goto comparison_failed;
                goto completed;
            }
            if (term_graph_value_eq &&
                (atom_structural_has_rational(a) ||
                 atom_structural_has_rational(b))) {
                if (!term_graph_value_eq(a, b, all_numbers
                        ? atom_petta_leaf_eq : atom_eq_leaf))
                    goto comparison_failed;
                goto completed;
            }
            if (a->kind != ATOM_EXPR || b->kind != ATOM_EXPR) {
                if (!atom_eq(a, b))
                    goto comparison_failed;
                goto completed;
            }
            if (a->expr.len != b->expr.len)
                goto comparison_failed;
            if (a == b &&
                (all_numbers || !atom_structural_may_have_nan(a)))
                goto completed;
            if (atom_eq_memo_met(&work.scopes[frame->scope], a, b))
                goto completed;
            frame->task = ATOM_COMPARE_EXPR;
            continue;
        }

        bool equal = false;
        if (atom_eq_decided(a, b, &equal)) {
            if (!equal)
                goto comparison_failed;
            goto completed;
        }
        if (a == b &&
            (!atom_structural_may_have_nan(a) || !eval_current_language_id ||
             eval_current_language_id() != CETTA_LANGUAGE_HE))
            goto completed;
        if (term_graph_value_eq &&
            (atom_structural_has_rational(a) ||
             atom_structural_has_rational(b))) {
            if (!term_graph_value_eq(a, b, atom_eq_leaf))
                goto comparison_failed;
            goto completed;
        }
        if (a->kind != b->kind)
            goto comparison_failed;
        switch (a->kind) {
        case ATOM_SYMBOL:
            equal = a->sym_id == b->sym_id;
            break;
        case ATOM_VAR:
            equal = var_id_eq(a->var_id, b->var_id);
            break;
        case ATOM_EXPR:
            if (a->expr.len != b->expr.len)
                goto comparison_failed;
            if (atom_eq_memo_met(&work.scopes[frame->scope], a, b))
                goto completed;
            frame->task = ATOM_COMPARE_EXPR;
            continue;
        case ATOM_GROUNDED:
            if (a->ground.gkind != b->ground.gkind) {
                equal = cetta_he_promoted_numbers_equal(a, b);
                break;
            }
            switch (a->ground.gkind) {
            case GV_INT: equal = a->ground.ival == b->ground.ival; break;
            case GV_FLOAT:
                equal = atom_float_eq(a->ground.fval, b->ground.fval);
                break;
            case GV_BOOL: equal = a->ground.bval == b->ground.bval; break;
            case GV_STRING:
                equal = a->ground.slen == b->ground.slen &&
                        memcmp(a->ground.sval, b->ground.sval,
                               a->ground.slen) == 0;
                break;
            case GV_BIGINT:
                equal = cetta_bigint_compare_cstr(atom_bigint_cstr(a),
                                                 atom_bigint_cstr(b)) == 0;
                break;
            case GV_RATIONAL:
                equal = cetta_rational_compare_cstr(atom_rational_cstr(a),
                                                    atom_rational_cstr(b)) == 0;
                break;
            case GV_SPACE:
            case GV_CAPTURE:
            case GV_FOREIGN:
            case GV_TERM_GRAPH:
            case GV_SEAL:
                equal = a->ground.ptr == b->ground.ptr;
                break;
            case GV_STRUCTURAL:
                equal = atom_structural_equal(a, b);
                break;
            case GV_BINDINGS: {
                const CettaBindingsValue *va = a->ground.ptr;
                const CettaBindingsValue *vb = b->ground.ptr;
                equal = va == vb || (va && vb && va->equal &&
                                     va->equal(va, vb));
                break;
            }
            case GV_INTERNAL_TAG:
                equal = a->ground.ival == b->ground.ival;
                break;
            case GV_PRIME_NEED_CAPABILITY: {
                const CettaPrimeNeedCapability *ca =
                    a->ground.prime_need_capability;
                const CettaPrimeNeedCapability *cb =
                    b->ground.prime_need_capability;
                equal = ca && cb && ca->session_id == cb->session_id &&
                        ca->thunk_id == cb->thunk_id &&
                        ca->authority_id == cb->authority_id &&
                        ca->rights == cb->rights;
                break;
            }
            case GV_PRIME_CONTEXT: {
                const CettaPrimeContext *ca = a->ground.prime_context;
                const CettaPrimeContext *cb = b->ground.prime_context;
                if (ca == cb)
                    goto completed;
                if (!ca || !cb || ca->depth != cb->depth)
                    goto comparison_failed;
                frame->context_left = ca;
                frame->context_right = cb;
                frame->stage = 0u;
                frame->task = ATOM_COMPARE_CONTEXT;
                continue;
            }
            case GV_STATE: {
                const StateCell *ca = a->ground.ptr;
                const StateCell *cb = b->ground.ptr;
                if (ca == cb &&
                    (!ca || !atom_structural_may_have_nan(ca->value) ||
                     !atom_he_exact_semantics()))
                    goto completed;
                if (!ca || !cb)
                    goto comparison_failed;
                for (uint32_t i = 0u; i < g_state_cell_pairs_len; i++) {
                    if (g_state_cell_pairs[i].left == ca &&
                        g_state_cell_pairs[i].right == cb)
                        goto completed;
                }
                if (g_state_cell_pairs_len == g_state_cell_pairs_cap) {
                    uint32_t cap = 8u;
                    size_t bytes;
                    if ((g_state_cell_pairs_cap &&
                         __builtin_mul_overflow(g_state_cell_pairs_cap, 2u,
                                                &cap)) ||
                        __builtin_mul_overflow(cap, sizeof(*g_state_cell_pairs),
                                               &bytes))
                        cetta_oom(SIZE_MAX);
                    g_state_cell_pairs = cetta_realloc(
                        g_state_cell_pairs, bytes);
                    g_state_cell_pairs_cap = cap;
                }
                g_state_cell_pairs[g_state_cell_pairs_len++] =
                    (StateCellPair){ca, cb};
                frame->task = ATOM_COMPARE_STATE_END;
                atom_compare_push(&work, ca->value, cb->value,
                                  ATOM_COMPARE_STRUCTURAL, 0u, true);
                continue;
            }
            }
            break;
        }
        if (!equal)
            goto comparison_failed;
completed:
        if (frame->task == ATOM_COMPARE_STATE_END)
            g_state_cell_pairs_len--;
        if (frame->owns_scope)
            work.scope_len--;
        work.len--;
    }
    g_state_cell_pairs_len = state_base;
    if (work.frames != work.inline_frames)
        free(work.frames);
    if (work.scopes != work.inline_scopes)
        free(work.scopes);
    return true;

comparison_failed:
    g_state_cell_pairs_len = state_base;
    if (work.frames != work.inline_frames)
        free(work.frames);
    if (work.scopes != work.inline_scopes)
        free(work.scopes);
    return false;
}

bool atom_is_number(const Atom *atom) {
    if (atom->kind != ATOM_GROUNDED)
        return false;
    switch (atom->ground.gkind) {
    case GV_INT:
    case GV_FLOAT:
    case GV_BIGINT:
    case GV_RATIONAL:
        return true;
    default:
        return false;
    }
}

#if CETTA_BUILD_WITH_GMP
static bool atom_number_get_mpq(const Atom *atom, mpq_t out) {
    switch (atom->ground.gkind) {
    case GV_INT: {
        char text[32];
        snprintf(text, sizeof(text), "%" PRId64, atom->ground.ival);
        return mpq_set_str(out, text, 10) == 0;
    }
    case GV_FLOAT:
        if (!isfinite(atom->ground.fval))
            return false;
        mpq_set_d(out, atom->ground.fval);
        return true;
    case GV_BIGINT: {
        mpz_t integer;
        mpz_init(integer);
        bool ok = atom_bigint_get_mpz(atom, integer);
        if (ok)
            mpq_set_z(out, integer);
        mpz_clear(integer);
        return ok;
    }
    case GV_RATIONAL:
        return atom_rational_get_mpq(atom, out);
    default:
        return false;
    }
}
#endif

/* Numbers of any representation, by exact value.  A NaN equals nothing. */
static bool atom_numbers_value_eq(Atom *a, Atom *b) {
    GroundedKind left = a->ground.gkind;
    GroundedKind right = b->ground.gkind;
    if (left == GV_FLOAT && right == GV_FLOAT)
        return a->ground.fval == b->ground.fval;
    if (left == right)
        return atom_eq(a, b);
    if (left == GV_INT && right == GV_FLOAT)
        return float_is_int64(b->ground.fval, a->ground.ival);
    if (left == GV_FLOAT && right == GV_INT)
        return float_is_int64(a->ground.fval, b->ground.ival);
#if CETTA_BUILD_WITH_GMP
    mpq_t x, y;
    mpq_inits(x, y, NULL);
    bool equal = atom_number_get_mpq(a, x) &&
                 atom_number_get_mpq(b, y) && mpq_equal(x, y);
    mpq_clears(x, y, NULL);
    return equal;
#else
    return false;
#endif
}

/* PeTTa's == on two leaves of a term: numbers by value, every NaN one
 * value; any other leaf as itself. */
static bool atom_petta_leaf_eq(Atom *a, Atom *b) {
    if (atom_is_number(a) && atom_is_number(b)) {
        if (a->ground.gkind == GV_FLOAT && b->ground.gkind == GV_FLOAT)
            return a->ground.fval == b->ground.fval ||
                   (isnan(a->ground.fval) && isnan(b->ground.fval));
        return atom_numbers_value_eq(a, b);
    }
    return atom_eq(a, b);
}

/* all_numbers: every representation compares by value, as PeTTa's == does,
 * and every NaN equals every NaN and nothing else (PeTTaLeafEquality).
 * Otherwise numbers compare as the HE lane's atoms do (atom_eq), except that
 * a NaN is never equal to itself. */
static uint32_t value_hash_mix(uint32_t h, uint32_t v) {
    return ((h << 5) + h) ^ v;
}

/* A number hashed by its value as a double, with the two zeros folded.  Two
 * numbers of equal value always convert to the same double, whatever their
 * kinds, so they hash alike; numbers that only round alike merely collide. */
static uint32_t atom_number_value_hash(const Atom *atom) {
    double value = 0.0;
    switch (atom->ground.gkind) {
    case GV_INT:
        value = (double)atom->ground.ival;
        break;
    case GV_FLOAT:
        value = atom->ground.fval;
        break;
    case GV_BIGINT:
#if CETTA_BUILD_WITH_GMP
        if (atom->ground.bigint && atom->ground.bigint->has_value) {
            value = mpz_get_d(atom->ground.bigint->value);
            break;
        }
#endif
        value = strtod(atom_bigint_cstr(atom), NULL);
        break;
    case GV_RATIONAL:
#if CETTA_BUILD_WITH_GMP
        if (atom->ground.rational && atom->ground.rational->has_value) {
            value = mpq_get_d(atom->ground.rational->value);
            break;
        }
#endif
        {
            const char *text = atom_rational_cstr(atom);
            char *slash = NULL;
            double numerator = text ? strtod(text, &slash) : 0.0;
            double denominator = slash && *slash == '/'
                ? strtod(slash + 1, NULL) : 1.0;
            value = numerator / denominator;
        }
        break;
    default:
        return atom_hash((Atom *)atom);
    }
    uint32_t h = value_hash_mix(5381u, (uint32_t)ATOM_GROUNDED);
    if (isnan(value))
        return value_hash_mix(h, 3u);
    if (value == 0.0)
        value = 0.0;
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return value_hash_mix(value_hash_mix(h, (uint32_t)bits),
                          (uint32_t)(bits >> 32));
}

char *cetta_he_float_exact_key_text(double value) {
#if CETTA_BUILD_WITH_GMP
    if (!atom_he_exact_semantics() || !isfinite(value) ||
        (value >= -0x1p63 && value < 0x1p63 &&
         (double)(int64_t)value == value))
        return NULL;
    mpq_t exact;
    mpq_init(exact);
    mpq_set_d(exact, value);
    char *text = mpq_get_str(NULL, 10, exact);
    mpq_clear(exact);
    size_t length = strlen(text);
    char *copy = cetta_malloc(length + 1u);
    memcpy(copy, text, length + 1u);
    gmp_free_string(text);
    return copy;
#else
    (void)value;
    return NULL;
#endif
}

uint32_t atom_value_hash(Atom *a) {
    return atom_hash_fold(a, true, false);
}

static __attribute__((noinline)) bool atom_value_eq_walk(
    Atom *a, Atom *b, bool all_numbers) {
    return atom_compare_driver(a, b, ATOM_COMPARE_VALUE, all_numbers);
}

bool atom_value_eq(Atom *a, Atom *b) {
    /* Two integers are equal exactly when their values are, in every lane;
     * so is an atom that holds no NaN to itself, and in PeTTa, where every
     * NaN equals every NaN, any atom to itself. */
    if (a == b && !atom_structural_may_have_nan(a))
        return true;
    if (a->kind == ATOM_GROUNDED && b->kind == ATOM_GROUNDED &&
        a->ground.gkind == GV_INT && b->ground.gkind == GV_INT)
        return a->ground.ival == b->ground.ival;
    CettaLanguageId language = eval_current_language_id
        ? eval_current_language_id() : CETTA_LANGUAGE_HE;
    if (language != CETTA_LANGUAGE_PETTA && language != CETTA_LANGUAGE_HE)
        return atom_eq(a, b);
    if (a == b && language == CETTA_LANGUAGE_PETTA)
        return true;
    return atom_value_eq_walk(a, b, language == CETTA_LANGUAGE_PETTA);
}

/* ── Printing ───────────────────────────────────────────────────────────── */

/* ── Deep copy ──────────────────────────────────────────────────────────── */

static void atom_deep_copy_memo_clear(AtomDeepCopyMemoSlot *slots,
                                      size_t cap) {
    for (size_t i = 0; i < cap; i++) {
        slots[i].src = NULL;
        slots[i].dst = NULL;
    }
}

static void atom_deep_copy_memo_init(AtomDeepCopyMemo *memo) {
    if (!memo)
        return;
    memo->slots = memo->inline_slots;
    memo->cap = CETTA_ATOM_DEEP_COPY_MEMO_INLINE_CAP;
    memo->used = 0;
    memo->buffers = NULL;
    memo->buffer_cap = 0u;
    memo->buffer_used = 0u;
    memo->region = NULL;
    memo->strip_he_marks = g_atom_deep_copy_strip_he_marks != 0u;
    memo->loan_arenas = NULL;
    memo->loan_arena_count = 0u;
    atom_deep_copy_memo_clear(memo->slots, memo->cap);
}

static void atom_deep_copy_memo_free(AtomDeepCopyMemo *memo) {
    if (!memo)
        return;
    if (memo->slots != memo->inline_slots) {
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
            memo->cap * sizeof(*memo->slots));
        free(memo->slots);
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
        memo->buffer_cap * sizeof(*memo->buffers));
    free(memo->buffers);
    memo->buffers = NULL;
    memo->buffer_cap = 0u;
    memo->buffer_used = 0u;
    memo->slots = memo->inline_slots;
    memo->cap = CETTA_ATOM_DEEP_COPY_MEMO_INLINE_CAP;
    memo->used = 0;
}

static size_t atom_deep_copy_memo_hash(const void *src) {
    uint64_t x = (uint64_t)(uintptr_t)src;
    x >>= 4;
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    return (size_t)x;
}

/* Prefix-buffer views have distinct Atom headers but a common end of their
 * element storage. Forwarding headers alone expands all retained prefixes
 * quadratically during a collection. This table keeps the copied extent;
 * a longer prefix extends it geometrically and a shorter one shares it.
 * Source storage is immutable for the lifetime of a copy session. */
static AtomDeepCopyBufferSlot *atom_deep_copy_buffer_lookup(
    AtomDeepCopyMemo *memo, Atom **source_end) {
    if (!memo->buffer_cap)
        return NULL;
    size_t mask = memo->buffer_cap - 1u;
    size_t pos = atom_deep_copy_memo_hash(source_end) & mask;
    while (memo->buffers[pos].source_end) {
        if (memo->buffers[pos].source_end == source_end)
            return &memo->buffers[pos];
        pos = (pos + 1u) & mask;
    }
    return NULL;
}

static void atom_deep_copy_buffer_store(
    AtomDeepCopyMemo *memo, Atom *source, Atom *copy) {
    Atom **end = source->expr.elems + source->expr.len;
    AtomDeepCopyBufferSlot *known = atom_deep_copy_buffer_lookup(memo, end);
    if (known) {
        if (copy->expr.len > known->longest->expr.len)
            known->longest = copy;
        known->recent = copy;
        return;
    }
    if ((memo->buffer_used + 1u) * 4u >= memo->buffer_cap * 3u) {
        size_t cap = memo->buffer_cap ? memo->buffer_cap * 2u : 16u;
        if (cap < memo->buffer_cap || cap > SIZE_MAX / sizeof(*memo->buffers))
            cetta_oom(SIZE_MAX);
        AtomDeepCopyBufferSlot *slots = cetta_malloc(cap * sizeof(*slots));
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES,
            cap * sizeof(*slots));
        memset(slots, 0, cap * sizeof(*slots));
        for (size_t i = 0u; i < memo->buffer_cap; i++) {
            AtomDeepCopyBufferSlot old = memo->buffers[i];
            if (!old.source_end)
                continue;
            size_t pos = atom_deep_copy_memo_hash(old.source_end) & (cap - 1u);
            while (slots[pos].source_end)
                pos = (pos + 1u) & (cap - 1u);
            slots[pos] = old;
        }
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
            memo->buffer_cap * sizeof(*memo->buffers));
        free(memo->buffers);
        memo->buffers = slots;
        memo->buffer_cap = cap;
    }
    size_t pos = atom_deep_copy_memo_hash(end) & (memo->buffer_cap - 1u);
    while (memo->buffers[pos].source_end)
        pos = (pos + 1u) & (memo->buffer_cap - 1u);
    memo->buffers[pos] = (AtomDeepCopyBufferSlot){end, copy, copy};
    memo->buffer_used++;
}

static bool atom_deep_copy_buffer_enabled(
    const Arena *dst, const Atom *source, bool share) {
    return !share && !dst->hashcons && source->expr.len > 0u &&
        (source->structural_facts & ATOM_STRUCTURAL_FRONT_SLACK) != 0u;
}

static Atom *atom_deep_copy_buffer_view(
    Arena *dst, Atom *source, bool share, AtomDeepCopyMemo *memo) {
    if (!atom_deep_copy_buffer_enabled(dst, source, share))
        return NULL;
    AtomDeepCopyBufferSlot *slot = atom_deep_copy_buffer_lookup(
        memo, source->expr.elems + source->expr.len);
    if (!slot || slot->longest->expr.len < source->expr.len)
        return NULL;
    Atom *parent = slot->recent->expr.len >= source->expr.len
        ? slot->recent : slot->longest;
    Atom *view = atom_expr_suffix(
        dst, parent, parent->expr.len - source->expr.len);
    /* The copied buffer has a floor, so inspecting the preceding slot is
     * safe for every nonempty view, even when that slot is already used. */
    view->structural_facts |= ATOM_STRUCTURAL_FRONT_SLACK;
    slot->recent = view;
    return view;
}

static Atom *atom_deep_copy_memo_lookup(const AtomDeepCopyMemo *memo,
                                        const Atom *src) {
    if (!memo || !src || memo->cap == 0)
        return NULL;
    size_t mask = memo->cap - 1u;
    size_t pos = atom_deep_copy_memo_hash(src) & mask;
    for (;;) {
        const AtomDeepCopyMemoSlot *slot = &memo->slots[pos];
        if (!slot->src)
            return NULL;
        if (slot->src == src)
            return slot->dst;
        pos = (pos + 1u) & mask;
    }
}

static bool atom_deep_copy_memo_grow(AtomDeepCopyMemo *memo) {
    if (!memo)
        return false;
    size_t old_cap = memo->cap;
    size_t new_cap = old_cap ? old_cap * 2u : CETTA_ATOM_DEEP_COPY_MEMO_INLINE_CAP;
    AtomDeepCopyMemoSlot *old_slots = memo->slots;
    AtomDeepCopyMemoSlot *new_slots =
        cetta_malloc(sizeof(AtomDeepCopyMemoSlot) * new_cap);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES,
        sizeof(*new_slots) * new_cap);
    atom_deep_copy_memo_clear(new_slots, new_cap);
    memo->slots = new_slots;
    memo->cap = new_cap;
    memo->used = 0;
    for (size_t i = 0; i < old_cap; i++) {
        const Atom *src = old_slots[i].src;
        Atom *dst = old_slots[i].dst;
        if (!src)
            continue;
        size_t mask = memo->cap - 1u;
        size_t pos = atom_deep_copy_memo_hash(src) & mask;
        while (memo->slots[pos].src)
            pos = (pos + 1u) & mask;
        memo->slots[pos].src = src;
        memo->slots[pos].dst = dst;
        memo->used++;
    }
    if (old_slots != memo->inline_slots) {
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
            sizeof(*old_slots) * old_cap);
        free(old_slots);
    }
    return true;
}

static bool atom_deep_copy_memo_store(AtomDeepCopyMemo *memo,
                                      const Atom *src, Atom *dst) {
    if (!memo || !src || !dst)
        return true;
    if ((memo->used + 1u) * 4u >= memo->cap * 3u &&
        !atom_deep_copy_memo_grow(memo)) {
        return false;
    }
    size_t mask = memo->cap - 1u;
    size_t pos = atom_deep_copy_memo_hash(src) & mask;
    for (;;) {
        AtomDeepCopyMemoSlot *slot = &memo->slots[pos];
        if (!slot->src) {
            slot->src = src;
            slot->dst = dst;
            memo->used++;
            return true;
        }
        if (slot->src == src) {
            slot->dst = dst;
            return true;
        }
        pos = (pos + 1u) & mask;
    }
}

static inline bool atom_deep_copy_region_dying(
    const AtomDeepCopyRegion *region, const Atom *atom) {
    for (uint32_t a = 0u; a < region->len; a++) {
        const AtomDeepCopyDyingArena *dying = &region->arenas[a];
        if (atom->arena_id != dying->identity)
            continue;
        uintptr_t address = (uintptr_t)atom;
        for (uint32_t i = 0u; i < dying->kept_len; i++) {
            if (address >= dying->kept[2u * i] &&
                address < dying->kept[2u * i + 1u])
                return false;
        }
        return true;
    }
    return false;
}

/* The copy already made of `src` in this episode, or NULL. */
static inline Atom *atom_deep_copy_lookup(const AtomDeepCopyMemo *memo,
                                          Atom *src) {
    const AtomDeepCopyRegion *region = memo->region;
    if (region) {
        if (src->arena_id == ATOM_ARENA_ID_FORWARDED)
            return src->name_key;
        if (atom_deep_copy_region_dying(region, src))
            return NULL;
    }
    return atom_deep_copy_memo_lookup(memo, src);
}

/* Record `dst` as the copy of `src`: in place when `src` dies with the
 * episode's region, otherwise in the table. */
static inline bool atom_deep_copy_record(AtomDeepCopyMemo *memo, Atom *src,
                                         Atom *dst) {
    const AtomDeepCopyRegion *region = memo->region;
    if (region && src && dst && atom_deep_copy_region_dying(region, src)) {
        src->arena_id = ATOM_ARENA_ID_FORWARDED;
        src->name_key = dst;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_ATOM_DEEP_COPY_RECORDED_IN_PLACE);
        return true;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_ATOM_DEEP_COPY_RECORDED_IN_TABLE);
    return atom_deep_copy_memo_store(memo, src, dst);
}

static Atom *atom_deep_copy_leaf(Arena *dst, Atom *src, bool share) {
    Atom *out = NULL;

    switch (src->kind) {
    case ATOM_SYMBOL:
        /* A symbol carries nothing a copy could own, so a copy takes the
         * table's one atom for it.  An arena that interns takes its
         * published atom instead, which the terms it interns are made of;
         * one with a symbol cache already holds one atom per symbol; and
         * one may keep symbols of its own. */
        out = dst->hashcons || dst->own_symbols ||
                arena_symbol_cache_is_active(dst)
            ? NULL : atom_symbol_canonical(src->sym_id);
        if (!out)
            out = atom_symbol_id(dst, src->sym_id);
        break;
    case ATOM_VAR:
        out = atom_var_like(dst, src, src->var_id);
        break;
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            out = atom_int_copy(dst, src);
            if (share && g_hashcons)
                out = hashcons_get(g_hashcons, out);
            break;
        case GV_FLOAT:
            out = share && g_hashcons ? hashcons_get(g_hashcons, atom_float(dst, src->ground.fval))
                                      : atom_float(dst, src->ground.fval);
            break;
        case GV_BOOL:
            out = share && g_hashcons ? hashcons_get(g_hashcons, atom_bool(dst, src->ground.bval))
                                      : atom_bool(dst, src->ground.bval);
            break;
        case GV_STRING:
            if ((!share || !g_hashcons) && !dst->hashcons &&
                (src->structural_facts & ATOM_STRUCTURAL_STRING_STORAGE)) {
                out = atom_string_retain_storage(dst, src,
                    ((const RetainedStringAtom *)src)->storage);
                break;
            }
            out = share && g_hashcons
                ? hashcons_get(g_hashcons,
                               atom_string_n(dst, src->ground.sval,
                                             src->ground.slen))
                : atom_string_n(dst, src->ground.sval, src->ground.slen);
            break;
        case GV_BIGINT:
            out = atom_bigint_copy(dst, src);
            break;
        case GV_RATIONAL:
            out = share && g_hashcons ? hashcons_get(g_hashcons, atom_rational(dst, atom_rational_cstr(src)))
                                      : atom_rational(dst, atom_rational_cstr(src));
            break;
        case GV_SPACE:
            out = atom_space(dst, src->ground.ptr);
            break;
        case GV_STATE:
            out = atom_state(dst, (StateCell *)src->ground.ptr);
            break;
        case GV_CAPTURE:
            out = atom_capture(dst, (CaptureClosure *)src->ground.ptr);
            break;
        case GV_BINDINGS:
            out = atom_bindings_value(dst, src->ground.ptr);
            break;
        case GV_FOREIGN:
            out = atom_foreign(dst, (CettaForeignValue *)src->ground.ptr);
            break;
        case GV_STRUCTURAL:
            out = atom_structural(dst, src->ground.ptr);
            break;
        case GV_SEAL:
            /* A copied reference keeps the object's identity. */
            out = atom_seal(dst, src->ground.ptr);
            break;
        case GV_TERM_GRAPH: {
            /* The graph is immutable: the copy shares it. */
            const CettaTermGraphRef *ref = src->ground.ptr;
            out = atom_term_graph(dst, ref->graph, ref->node);
            break;
        }
        case GV_INTERNAL_TAG:
            out = atom_internal_tag(
                dst, (CettaInternalTag)src->ground.ival);
            break;
        case GV_PRIME_NEED_CAPABILITY: {
            const CettaPrimeNeedCapability *capability =
                src->ground.prime_need_capability;
            out = capability
                      ? atom_prime_need_capability_with_rights(
                            dst, capability->session_id,
                            capability->thunk_id, capability->authority_id,
                            capability->rights)
                      : NULL;
            break;
        }
        case GV_PRIME_CONTEXT:
            out = atom_prime_context_deep_copy(
                dst, src->ground.prime_context);
            break;
        }
        break;
    case ATOM_EXPR:
        break;
    }
    if (!out)
        out = atom_symbol(dst, "?");
    return out;
}

typedef struct {
    Atom *src;
    Atom *memo_source;
    Atom **elems;
    CettaExprIndex next;
    CettaExprLen prefix_len;
    Atom *tail;
} AtomDeepCopyFrame;

static void atom_deep_copy_stack_free(
        AtomDeepCopyFrame *stack, const AtomDeepCopyFrame *inline_stack,
        size_t capacity) {
    if (stack == inline_stack)
        return;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
        capacity * sizeof(*stack));
    free(stack);
}

static Atom *atom_deep_copy_impl(
    Arena *dst, Atom *src, bool share, AtomDeepCopyMemo *memo,
    AtomDeepCopyResolver resolver, void *resolver_context,
    AtomDeepCopyLeafMap map, void *map_context,
    AtomDeepCopyNodeView view, void *view_context) {
    AtomDeepCopyFrame inline_stack[16];
    AtomDeepCopyFrame *stack = inline_stack;
    size_t stack_len = 0;
    size_t stack_cap = sizeof(inline_stack) / sizeof(*inline_stack);
    Atom *result = NULL;
    Atom *memoized;

    if (!dst || !src)
        return NULL;
    if (resolver && !(src = resolver(resolver_context, src)))
        return NULL;
    memoized = atom_deep_copy_lookup(memo, src);
    if (memoized)
        return memoized;
    if (!resolver && !map && !view && memo->loan_arena_count && atom_deep_copy_may_loan(memo, src))
        return atom_deep_copy_record(memo, src, src) ? src : NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_NODE);
    Atom *memo_source = src;
    bool complete = false;
    if (view && (!view(dst, src, &src, &complete, view_context) || !src))
        return NULL;
    if (complete)
        return atom_deep_copy_record(memo, memo_source, src) ? src : NULL;
    if (src->kind != ATOM_EXPR) {
        result = map ? map(dst, src, map_context)
                     : atom_deep_copy_leaf(dst, src, share);
        if (!result)
            return NULL;
        if (!atom_deep_copy_record(memo, memo_source, result))
            return NULL;
        return result;
    }
    result = view || !atom_deep_copy_may_share(memo, src)
        ? NULL : atom_deep_copy_buffer_view(dst, src, share, memo);
    if (result) {
        if (!atom_deep_copy_record(memo, src, result))
            return NULL;
        return result;
    }

#define PUSH_COPY_FRAME(source_atom, memo_atom) do { \
    Atom *copy_source = (source_atom); \
    Atom *copy_tail = NULL; \
    if (!view && atom_deep_copy_may_share(memo, copy_source) && \
        atom_deep_copy_buffer_enabled(dst, copy_source, share)) { \
        AtomDeepCopyBufferSlot *buffer = atom_deep_copy_buffer_lookup( \
            memo, copy_source->expr.elems + copy_source->expr.len); \
        if (buffer && buffer->longest->expr.len < copy_source->expr.len) \
            copy_tail = buffer->longest; \
    } \
    CettaExprLen prefix_len = copy_source->expr.len - \
        (copy_tail ? copy_tail->expr.len : 0u); \
    if (copy_source->expr.len > 0 && \
        !cetta_expr_len_mul_fits_size(copy_source->expr.len, sizeof(Atom *))) \
        cetta_oom(SIZE_MAX); \
    if (stack_len == stack_cap) { \
        size_t next_cap = stack_cap * 2u; \
        if (next_cap < stack_cap || \
            next_cap > SIZE_MAX / sizeof(AtomDeepCopyFrame)) \
            cetta_oom(SIZE_MAX); \
        AtomDeepCopyFrame *grown; \
        if (stack == inline_stack) { \
            grown = cetta_malloc(next_cap * sizeof(*grown)); \
            memcpy(grown, stack, stack_len * sizeof(*grown)); \
        } else { \
            grown = cetta_realloc(stack, next_cap * sizeof(*grown)); \
        } \
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES, \
            next_cap * sizeof(AtomDeepCopyFrame)); \
        if (stack != inline_stack) \
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES, \
                stack_cap * sizeof(AtomDeepCopyFrame)); \
        stack = grown; \
        stack_cap = next_cap; \
    } \
    stack[stack_len++] = (AtomDeepCopyFrame) { \
        .src = copy_source, \
        .memo_source = (memo_atom), \
        .elems = prefix_len \
            ? arena_alloc(dst, (size_t)prefix_len * sizeof(Atom *)) \
            : NULL, \
        .next = 0, \
        .prefix_len = prefix_len, \
        .tail = copy_tail, \
    }; \
} while (0)

    PUSH_COPY_FRAME(src, memo_source);
    while (stack_len > 0) {
        AtomDeepCopyFrame *frame = &stack[stack_len - 1u];

        if (frame->next < frame->prefix_len) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_EDGE_VISIT);
            Atom *child = frame->src->expr.elems[frame->next];
            if (resolver &&
                !(child = resolver(resolver_context, child))) {
                atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                return NULL;
            }
            Atom *child_copy = atom_deep_copy_lookup(memo, child);

            if (child_copy) {
                frame->elems[frame->next++] = child_copy;
                continue;
            }
            if (!resolver && !map && !view && memo->loan_arena_count && atom_deep_copy_may_loan(memo, child)) {
                if (!atom_deep_copy_record(memo, child, child)) {
                    atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                    return NULL;
                }
                frame->elems[frame->next++] = child;
                continue;
            }
            if (!map && !view &&
                !(memo->strip_he_marks && atom_may_hold_he_mark(child)) &&
                atom_settled_in(dst, child)) {
                if (!atom_deep_copy_memo_store(memo, child, child)) {
                    atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                    return NULL;
                }
                provenance_assert_settled(
                    child,
                    "atom_deep_copy_impl.destination-closed-child",
                    dst);
                frame = &stack[stack_len - 1u];
                frame->elems[frame->next++] = child;
                continue;
            }
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_NODE);
            Atom *memo_child = child;
            bool child_complete = false;
            if (view && (!view(dst, child, &child, &child_complete, view_context) || !child)) {
                atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                return NULL;
            }
            if (child_complete) {
                if (!atom_deep_copy_record(memo, memo_child, child)) {
                    atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                    return NULL;
                }
                frame->elems[frame->next++] = child;
                continue;
            }
            if (child->kind == ATOM_EXPR) {
                child_copy = view || !atom_deep_copy_may_share(memo, child)
                    ? NULL : atom_deep_copy_buffer_view(dst, child, share, memo);
                if (child_copy) {
                    if (!atom_deep_copy_record(memo, child,
                                               child_copy)) {
                        atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                        return NULL;
                    }
                    frame->elems[frame->next++] = child_copy;
                    continue;
                }
                PUSH_COPY_FRAME(child, memo_child);
                continue;
            }
            child_copy = map ? map(dst, child, map_context)
                             : atom_deep_copy_leaf(dst, child, share);
            if (!child_copy) {
                atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                return NULL;
            }
            if (!atom_deep_copy_record(memo, memo_child, child_copy)) {
                atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
                return NULL;
            }
            frame = &stack[stack_len - 1u];
            frame->elems[frame->next++] = child_copy;
            continue;
        }

        if (atom_deep_copy_keeps_he_mark(memo, frame->src)) {
            result = atom_he_evaluated_build(
                dst, frame->elems, frame->src->expr.len);
        } else if (!view && atom_deep_copy_may_share(memo, frame->src) &&
                   atom_deep_copy_buffer_enabled(dst, frame->src, share)) {
            if (frame->tail) {
                result = frame->tail;
                for (CettaExprLen i = frame->prefix_len; i > 0u; i--)
                    result = atom_expr_prepend(dst, frame->elems[i - 1u], result);
            } else {
                Atom *tail = atom_expr(dst, frame->elems + 1u,
                                       frame->prefix_len - 1u);
                result = atom_expr_prepend(dst, frame->elems[0], tail);
            }
            atom_deep_copy_buffer_store(memo, frame->src, result);
        } else {
            result = share
                ? atom_expr_shared(dst, frame->elems, frame->src->expr.len)
                : atom_expr(dst, frame->elems, frame->src->expr.len);
        }
        if (!atom_deep_copy_record(memo, frame->memo_source, result)) {
            atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
            return NULL;
        }
        stack_len--;
        if (stack_len > 0) {
            frame = &stack[stack_len - 1u];
            frame->elems[frame->next++] = result;
        }
    }
    atom_deep_copy_stack_free(stack, inline_stack, stack_cap);
#undef PUSH_COPY_FRAME
    return result;
}

Atom *atom_deep_copy_mapped(Arena *dst, Atom *src,
                           AtomDeepCopyLeafMap map, void *context) {
    AtomDeepCopyMemo memo;
    atom_deep_copy_memo_init(&memo);
    Atom *out = atom_deep_copy_impl(dst, src, false, &memo, NULL, NULL,
                                    map, context, NULL, NULL);
    atom_deep_copy_memo_free(&memo);
    return out;
}

Atom *atom_deep_copy_observed(Arena *dst, Atom *src,
                             AtomDeepCopyNodeView view, void *context) {
    if (!dst || !src || !view)
        return NULL;
    AtomDeepCopyMemo memo;
    atom_deep_copy_memo_init(&memo);
    Atom *out = atom_deep_copy_impl(dst, src, false, &memo, NULL, NULL,
                                   NULL, NULL, view, context);
    atom_deep_copy_memo_free(&memo);
    return out;
}

Atom *atom_deep_copy(Arena *dst, Atom *src) {
    if (!dst || !src)
        return NULL;
    if (src->kind != ATOM_EXPR)
        return atom_deep_copy_leaf(dst, src, false);
    AtomDeepCopyMemo memo;
    atom_deep_copy_memo_init(&memo);
    Atom *out = atom_deep_copy_impl(
        dst, src, false, &memo, NULL, NULL, NULL, NULL, NULL, NULL);
    atom_deep_copy_memo_free(&memo);
    return out;
}

Atom *atom_deep_copy_single(Arena *dst, Atom *src) {
    if (!dst || !src)
        return NULL;
    if (atom_settled_for_arena(dst, src)) {
        provenance_assert_settled(
            src, "atom_deep_copy_single.destination-closed", dst);
        return src;
    }
    return atom_deep_copy(dst, src);
}

AtomDeepCopySession *atom_deep_copy_session_new(Arena *dst) {
    if (!dst)
        return NULL;
    AtomDeepCopySession *session = cetta_malloc(sizeof(*session));
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES,
        sizeof(*session));
    session->dst = dst;
    session->resolver = NULL;
    session->resolver_context = NULL;
    session->map = NULL;
    session->map_context = NULL;
    session->failed = false;
    session->started = false;
    session->region = (AtomDeepCopyRegion){0};
    session->inventories = NULL;
    atom_deep_copy_memo_init(&session->memo);
    return session;
}

bool atom_deep_copy_session_loan_arenas(AtomDeepCopySession *session,
        const uint32_t *identities, size_t count) {
    if (!session || session->failed || session->started || session->resolver ||
        session->map || (count && !identities))
        return false;
    for (size_t index = 0u; index < count; index++) {
        if (!identities[index] || identities[index] == ARENA_ID_DEAD)
            return false;
        for (uint32_t region = 0u; region < session->region.len; region++)
            if (identities[index] == session->region.arenas[region].identity)
                return false;
    }
    session->memo.loan_arenas = identities;
    session->memo.loan_arena_count = count;
    return true;
}

AtomDeepCopySession *atom_deep_copy_session_new_mapped(
        Arena *dst, AtomDeepCopyLeafMap map, void *context) {
    if (!map)
        return NULL;
    AtomDeepCopySession *session = atom_deep_copy_session_new(dst);
    if (session) {
        session->map = map;
        session->map_context = context;
    }
    return session;
}

/* Add `arena`'s storage above `mark` to the storage dying with the session
 * (a null mark: the whole arena).  An arena already dying keeps its first
 * mark. */
bool atom_deep_copy_session_forward_region(AtomDeepCopySession *session,
                                           const Arena *arena,
                                           ArenaMark mark) {
    if (!session || session->map || !arena || arena->identity == 0u)
        return false;
    for (size_t index = 0u; index < session->memo.loan_arena_count; index++)
        if (session->memo.loan_arenas[index] == arena->identity)
            return false;
    AtomDeepCopyRegion *region = &session->region;
    for (uint32_t a = 0u; a < region->len; a++) {
        if (region->arenas[a].identity == arena->identity)
            return false;
    }
    uint32_t kept_len = 0u;
    if (mark.head) {
        kept_len = 1u;
        for (const ArenaBlock *block = mark.head->next; block;
             block = block->next) {
            if (kept_len == UINT32_MAX)
                return false;
            kept_len++;
        }
    }
    uintptr_t *kept = NULL;
    if (kept_len) {
        if (SIZE_MAX / (size_t)kept_len < 2u * sizeof(*kept))
            return false;
        kept = malloc((size_t)kept_len * 2u * sizeof(*kept));
        if (!kept)
            return false;
        kept[0] = (uintptr_t)mark.head->data;
        kept[1] = (uintptr_t)mark.head->data + mark.used;
        uint32_t i = 1u;
        for (const ArenaBlock *block = mark.head->next; block;
             block = block->next, i++) {
            kept[2u * i] = (uintptr_t)block->data;
            kept[2u * i + 1u] = (uintptr_t)block->data + block->used;
        }
    }
    if (region->len == region->cap) {
        uint32_t cap = region->cap ? region->cap * 2u : 2u;
        AtomDeepCopyDyingArena *grown = cap > region->cap
            ? realloc(region->arenas, (size_t)cap * sizeof(*grown)) : NULL;
        if (!grown) {
            free(kept);
            return false;
        }
        region->arenas = grown;
        region->cap = cap;
    }
    region->arenas[region->len++] = (AtomDeepCopyDyingArena){
        .identity = arena->identity, .kept_len = kept_len, .kept = kept,
    };
    session->memo.region = region;
    return true;
}

void atom_deep_copy_session_set_resolver(
    AtomDeepCopySession *session, AtomDeepCopyResolver resolver,
    void *context) {
    if (!session || session->map ||
        (session->memo.loan_arena_count && session->started))
        return;
    session->memo.loan_arenas = NULL;
    session->memo.loan_arena_count = 0u;
    session->resolver = resolver;
    session->resolver_context = context;
}

Atom *atom_deep_copy_session_copy(AtomDeepCopySession *session, Atom *src) {
    if (!session || !session->dst || session->failed)
        return NULL;
    if (!src) {
        session->failed = true;
        return NULL;
    }
    session->started = true;
    if (session->resolver &&
        !(src = session->resolver(session->resolver_context, src))) {
        session->failed = true;
        return NULL;
    }
    Atom *forwarded = atom_deep_copy_lookup(&session->memo, src);
    if (forwarded)
        return forwarded;
    if (!session->resolver && !session->map && session->memo.loan_arena_count &&
        atom_deep_copy_may_loan(&session->memo, src))
        return atom_deep_copy_memo_store(&session->memo, src, src) ? src : NULL;
    /*
     * Arena ownership proves where the root allocation lives, not where its
     * transitive children live.  A destination-owned expression can still
     * reference a temporary-arena child.  Elision therefore additionally
     * requires the compositional arena-closure proof established by atom
     * constructors.  The per-episode forwarding table remains authoritative
     * for every graph copied during this episode.
     */
    if (!session->map && atom_settled_for_arena(session->dst, src)) {
        if (!atom_deep_copy_memo_store(&session->memo, src, src))
            return NULL;
        provenance_assert_settled(
            src, "atom_deep_copy_session_copy.destination-closed",
            session->dst);
        return src;
    }
    Atom *out = atom_deep_copy_impl(
        session->dst, src, false, &session->memo,
        session->resolver, session->resolver_context,
        session->map, session->map_context, NULL, NULL);
    if (!out)
        session->failed = true;
    return out;
}

Atom *atom_deep_copy_session_forwarded(
    const AtomDeepCopySession *session, const Atom *src) {
    if (!session || !src)
        return NULL;
    return atom_deep_copy_lookup(&session->memo, (Atom *)src);
}

bool atom_deep_copy_session_settled(
    const AtomDeepCopySession *session, const Atom *atom) {
    return session && atom && !session->failed &&
           !session->resolver && !session->map &&
           atom_settled_for_arena(session->dst, atom);
}

void *atom_deep_copy_session_inventory(const AtomDeepCopySession *session,
                                      const void *kind) {
    for (AtomDeepCopyInventory *entry = session ? session->inventories : NULL;
         entry; entry = entry->next)
        if (entry->kind == kind) return entry->value;
    return NULL;
}

bool atom_deep_copy_session_failed(const AtomDeepCopySession *session) {
    return !session || session->failed;
}

bool atom_deep_copy_session_install_inventory(AtomDeepCopySession *session,
    const void *kind, void *inventory, AtomDeepCopyInventoryRelease release) {
    if (!session || !kind || !inventory || !release ||
        atom_deep_copy_session_inventory(session, kind))
        return false;
    AtomDeepCopyInventory *entry = cetta_malloc(sizeof(*entry));
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_ALLOC_BYTES,
                           sizeof(*entry));
    *entry = (AtomDeepCopyInventory){kind, inventory, release, session->inventories};
    session->inventories = entry;
    return true;
}

void atom_deep_copy_session_free(AtomDeepCopySession *session) {
    if (!session)
        return;
    while (session->inventories) {
        AtomDeepCopyInventory *entry = session->inventories;
        session->inventories = entry->next;
        entry->release(entry->value);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
                               sizeof(*entry));
        free(entry);
    }
    for (uint32_t a = 0u; a < session->region.len; a++)
        free(session->region.arenas[a].kept);
    free(session->region.arenas);
    atom_deep_copy_memo_free(&session->memo);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_ATOM_TRANSPORT_METADATA_FREE_BYTES,
        sizeof(*session));
    free(session);
}

Atom *atom_deep_copy_shared(Arena *dst, Atom *src) {
    if (!dst || !src)
        return NULL;
    if (src->kind != ATOM_EXPR)
        return atom_deep_copy_leaf(dst, src, true);
    AtomDeepCopyMemo memo;
    atom_deep_copy_memo_init(&memo);
    Atom *out = atom_deep_copy_impl(
        dst, src, true, &memo, NULL, NULL, NULL, NULL, NULL, NULL);
    atom_deep_copy_memo_free(&memo);
    return out;
}

/* ── Printing ───────────────────────────────────────────────────────────── */

int cetta_format_float(char *buf, size_t size, double value) {
    if (isnan(value)) {
        return snprintf(buf, size, "NaN");
    } else if (isinf(value)) {
        return snprintf(buf, size, "%s", signbit(value) ? "-inf" : "inf");
    } else if (isfinite(value) && floor(value) == value) {
        if (fabs(value) < 9007199254740992.0)
            return snprintf(buf, size, "%.1f", value);
        return snprintf(buf, size, "%.16e", value);
    } else {
        return snprintf(buf, size, "%.16g", value);
    }
}

static void atom_print_float(double value, FILE *out) {
    char buf[64];
    int printed = cetta_format_float(buf, sizeof(buf), value);
    if (printed > 0)
        fputs(buf, out);
}

typedef struct {
    VarId variable;
    size_t ordinal;
    bool occupied;
} PettaPrintVariableSlot;

typedef struct {
    PettaPrintVariableSlot *slots;
    size_t capacity;
    size_t length;
} PettaPrintVariables;

static size_t petta_print_variable_hash(VarId variable) {
    uint64_t mixed = var_id_mix(variable);
    mixed ^= mixed >> 30u;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27u;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31u;
    return (size_t)mixed;
}

static void petta_print_variables_grow(
    PettaPrintVariables *variables) {
    size_t previous_capacity = variables->capacity;
    size_t next_capacity =
        previous_capacity ? previous_capacity * 2u : 16u;
    if (next_capacity <= previous_capacity ||
        next_capacity >
            SIZE_MAX / sizeof(PettaPrintVariableSlot)) {
        cetta_oom(SIZE_MAX);
    }
    PettaPrintVariableSlot *next = cetta_malloc(
        sizeof(*next) * next_capacity);
    memset(next, 0, sizeof(*next) * next_capacity);
    for (size_t index = 0u;
         index < previous_capacity; index++) {
        PettaPrintVariableSlot slot = variables->slots[index];
        if (!slot.occupied)
            continue;
        size_t target =
            petta_print_variable_hash(slot.variable) &
            (next_capacity - 1u);
        while (next[target].occupied)
            target = (target + 1u) & (next_capacity - 1u);
        next[target] = slot;
    }
    free(variables->slots);
    variables->slots = next;
    variables->capacity = next_capacity;
}

static size_t petta_print_variable_ordinal(
    PettaPrintVariables *variables, VarId variable) {
    if (variables->capacity == 0u ||
        variables->length >= variables->capacity / 2u) {
        petta_print_variables_grow(variables);
    }
    size_t slot =
        petta_print_variable_hash(variable) &
        (variables->capacity - 1u);
    while (variables->slots[slot].occupied) {
        if (var_id_eq(variables->slots[slot].variable, variable))
            return variables->slots[slot].ordinal;
        slot = (slot + 1u) & (variables->capacity - 1u);
    }
    size_t ordinal = variables->length++;
    variables->slots[slot] = (PettaPrintVariableSlot){
        .variable = variable,
        .ordinal = ordinal,
        .occupied = true,
    };
    return ordinal;
}

typedef enum {
    ATOM_PRINT_ACTION_ATOM = 0,
    ATOM_PRINT_ACTION_CHAR,
} AtomPrintActionKind;

typedef struct {
    AtomPrintActionKind kind;
    Atom *atom;
    int character;
} AtomPrintAction;

typedef struct {
    AtomPrintAction *items;
    size_t length;
    size_t capacity;
} AtomPrintStack;

static void atom_print_stack_push(
    AtomPrintStack *stack, AtomPrintAction action) {
    if (stack->length == stack->capacity) {
        size_t next_capacity =
            stack->capacity ? stack->capacity * 2u : 64u;
        if (next_capacity <= stack->capacity ||
            next_capacity > SIZE_MAX / sizeof(*stack->items)) {
            cetta_oom(SIZE_MAX);
        }
        stack->items = cetta_realloc(
            stack->items,
            sizeof(*stack->items) * next_capacity);
        stack->capacity = next_capacity;
    }
    stack->items[stack->length++] = action;
}

static void atom_print_stack_push_atom(
    AtomPrintStack *stack, Atom *atom) {
    atom_print_stack_push(
        stack,
        (AtomPrintAction){
            .kind = ATOM_PRINT_ACTION_ATOM,
            .atom = atom,
        });
}

static void atom_print_stack_push_char(
    AtomPrintStack *stack, int character) {
    atom_print_stack_push(
        stack,
        (AtomPrintAction){
            .kind = ATOM_PRINT_ACTION_CHAR,
            .character = character,
        });
}

static pthread_once_t petta_output_locale_once = PTHREAD_ONCE_INIT;
static locale_t petta_output_locale;

static void petta_output_locale_init(void) {
    petta_output_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
}

static bool petta_float_same_bits(double left, double right) {
    uint64_t left_bits = 0u;
    uint64_t right_bits = 0u;
    memcpy(&left_bits, &left, sizeof(left_bits));
    memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

int cetta_petta_number_format_float(
    char *buffer, size_t size, double value) {
    if (!buffer || size == 0u)
        return -1;
    if (isnan(value))
        return snprintf(buffer, size, "1.5NaN");
    if (isinf(value))
        return snprintf(
            buffer, size, "%s", signbit(value) ? "-1.0Inf" : "1.0Inf");

    pthread_once(&petta_output_locale_once, petta_output_locale_init);
    if (!petta_output_locale)
        return -1;
    locale_t previous = uselocale(petta_output_locale);
    /* The shortest significant digits that read back to the same bits. */
    char scientific[64];
    int scientific_length = -1;
    for (int fractional = 0; fractional <= 16; fractional++) {
        int length = snprintf(
            scientific, sizeof(scientific), "%.*e", fractional, value);
        if (length <= 0 || (size_t)length >= sizeof(scientific))
            continue;
        char *end = NULL;
        double parsed = strtod_l(scientific, &end, petta_output_locale);
        if (end && *end == '\0' && petta_float_same_bits(parsed, value)) {
            scientific_length = length;
            break;
        }
    }
    if (scientific_length < 0)
        scientific_length = snprintf(
            scientific, sizeof(scientific), "%.16e", value);
    (void)uselocale(previous);
    char *exponent_text = scientific_length > 0
        ? strchr(scientific, 'e') : NULL;
    if (!exponent_text)
        return -1;

    bool negative = scientific[0] == '-';
    char digits[32];
    size_t digit_count = 0u;
    for (const char *cursor = scientific + (negative ? 1 : 0);
         cursor < exponent_text; cursor++) {
        if (*cursor >= '0' && *cursor <= '9' &&
            digit_count + 1u < sizeof(digits))
            digits[digit_count++] = *cursor;
    }
    while (digit_count > 1u && digits[digit_count - 1u] == '0')
        digit_count--;
    digits[digit_count] = '\0';
    int point = atoi(exponent_text + 1) + 1;

    /* SWI-Prolog's layout: positional unless it needs four or more zeros
     * of padding between the digits and the decimal point. */
    const char *sign = negative ? "-" : "";
    int count = (int)digit_count;
    if (point <= 0 && point >= -3) {
        return snprintf(buffer, size, "%s0.%.*s%s",
                        sign, -point, "000", digits);
    }
    if (point > 0 && count > point) {
        return snprintf(buffer, size, "%s%.*s.%s",
                        sign, point, digits, digits + point);
    }
    if (point > 0 && point - count <= 3) {
        return snprintf(buffer, size, "%s%s%.*s.0",
                        sign, digits, point - count, "000");
    }
    return snprintf(buffer, size, "%s%c.%se%c%02d",
                    sign, digits[0],
                    count > 1 ? digits + 1 : "0",
                    point - 1 < 0 ? '-' : '+',
                    abs(point - 1));
}

bool cetta_petta_number_format_fraction(
    char *buffer, size_t size, double value, size_t precision) {
    if (!buffer || size == 0u || !isfinite(value) ||
        precision >= size || precision > (size_t)INT_MAX ||
        precision > SIZE_MAX - 4u) {
        return false;
    }
    if (precision == 0u) {
        buffer[0] = '\0';
        return true;
    }
    pthread_once(&petta_output_locale_once, petta_output_locale_init);
    if (!petta_output_locale)
        return false;
    char *rendered = malloc(precision + 4u);
    if (!rendered)
        return false;
    double integral = 0.0;
    double fractional = fabs(modf(value, &integral));
    locale_t previous = uselocale(petta_output_locale);
    int written = snprintf(
        rendered, precision + 4u, "%.*f",
        (int)precision, fractional);
    (void)uselocale(previous);
    char *point = strchr(rendered, '.');
    bool valid = written > 0 && point &&
        strlen(point + 1u) == precision;
    if (valid) {
        memcpy(buffer, point + 1u, precision);
        buffer[precision] = '\0';
    }
    free(rendered);
    return valid;
}

bool cetta_petta_number_print(const Atom *atom, FILE *output) {
    if (!atom || !output || atom->kind != ATOM_GROUNDED)
        return false;
    switch (atom->ground.gkind) {
    case GV_INT:
        fprintf(output, "%" PRId64, atom->ground.ival);
        return true;
    case GV_BIGINT:
        fputs(atom_bigint_cstr(atom), output);
        return true;
    case GV_RATIONAL: {
        const char *text = atom_rational_cstr(atom);
        for (; text && *text; text++)
            fputc(*text == '/' ? 'r' : *text, output);
        return text != NULL;
    }
    case GV_FLOAT: {
        char buffer[128];
        int written = cetta_petta_number_format_float(
            buffer, sizeof(buffer), atom->ground.fval);
        if (written <= 0)
            return false;
        fputs(buffer, output);
        return true;
    }
    default:
        return false;
    }
}

char *cetta_petta_number_to_string(Arena *arena, const Atom *atom) {
    if (!arena || !atom || atom->kind != ATOM_GROUNDED)
        return NULL;
    switch (atom->ground.gkind) {
    case GV_INT: {
        char buffer[64];
        int written = snprintf(
            buffer, sizeof(buffer), "%" PRId64, atom->ground.ival);
        return written > 0 && (size_t)written < sizeof(buffer)
            ? arena_strdup(arena, buffer) : NULL;
    }
    case GV_BIGINT:
        return arena_strdup(arena, atom_bigint_cstr(atom));
    case GV_RATIONAL: {
        const char *text = atom_rational_cstr(atom);
        if (!text)
            return NULL;
        char *result = arena_strdup(arena, text);
        char *separator = strchr(result, '/');
        if (separator)
            *separator = 'r';
        return result;
    }
    case GV_FLOAT: {
        char buffer[128];
        int written = cetta_petta_number_format_float(
            buffer, sizeof(buffer), atom->ground.fval);
        return written > 0 && (size_t)written < sizeof(buffer)
            ? arena_strdup(arena, buffer) : NULL;
    }
    default:
        return NULL;
    }
}

static bool g_atom_print_raw_string_bytes = false;
static _Thread_local bool g_atom_print_c_text = false;

void atom_print_set_raw_string_bytes(bool raw) {
    g_atom_print_raw_string_bytes = raw;
}

bool atom_print_raw_string_bytes(void) {
    return g_atom_print_raw_string_bytes;
}

bool atom_print_set_c_text(bool on) {
    bool previous = g_atom_print_c_text;
    g_atom_print_c_text = on;
    return previous;
}

static void atom_print_emit_file(void *context, const char *bytes,
                                 size_t len) {
    fwrite(bytes, 1, len, (FILE *)context);
}

static void atom_print_string_bytes(FILE *out, const char *text, size_t len,
                                    bool petta) {
    CettaStringLiteralBytes mode =
        !(petta || g_atom_print_raw_string_bytes) ? CETTA_STRING_LITERAL_ESCAPED
        : g_atom_print_c_text ? CETTA_STRING_LITERAL_RAW_TEXT
                              : CETTA_STRING_LITERAL_RAW;
    cetta_string_literal_escape(text, len, mode, petta, atom_print_emit_file,
                                out);
}

static void atom_print_mode(
    Atom *root, FILE *out, bool petta,
    PettaPrintVariables *variables) {
    AtomPrintStack stack = {0};
    atom_print_stack_push_atom(&stack, root);

    while (stack.length > 0u) {
        AtomPrintAction action = stack.items[--stack.length];
        if (action.kind == ATOM_PRINT_ACTION_CHAR) {
            fputc(action.character, out);
            continue;
        }

        Atom *a = action.atom;
        switch (a->kind) {
    case ATOM_SYMBOL:
        fputs(atom_name_cstr(a), out);
        break;
    case ATOM_VAR:
        if (petta && variables) {
            fprintf(
                out, "$V%zu",
                petta_print_variable_ordinal(
                    variables, a->var_id));
        } else if (a->name_key &&
                   a->name_key->kind == ATOM_GROUNDED &&
                   a->name_key->ground.gkind == GV_INTERNAL_TAG &&
                   a->name_key->ground.ival ==
                       (int64_t)CETTA_INTERNAL_TAG_PRIME_LEXICAL_SLOT) {
            fprintf(out, "$%s", a->sym_id == SYMBOL_ID_NONE
                ? "__prime_let" : symbol_bytes(g_symbols, a->sym_id));
        } else if (a->name_key) {
            fputs("$@", out);
            atom_print_stack_push_atom(&stack, a->name_key);
        } else {
            fprintf(out, "$%s", atom_name_cstr(a));
        }
        if (!petta) {
            CettaFrameIdentity epoch = var_epoch_suffix(a->var_id);
            if (!cetta_frame_identity_is_none(epoch))
                fprintf(out, "#%" PRIu64, epoch.word);
        }
        break;
    case ATOM_GROUNDED:
        switch (a->ground.gkind) {
        case GV_INT:    fprintf(out, "%ld", (long)a->ground.ival); break;
        case GV_FLOAT:
            if (petta) {
                (void)cetta_petta_number_print(a, out);
            } else {
                atom_print_float(a->ground.fval, out);
            }
            break;
        case GV_BOOL:   fputs(a->ground.bval ? "True" : "False", out); break;
        case GV_BIGINT:
            if (petta)
                (void)cetta_petta_number_print(a, out);
            else
                fputs(atom_bigint_cstr(a), out);
            break;
        case GV_RATIONAL:
            if (petta)
                (void)cetta_petta_number_print(a, out);
            else
                fputs(atom_rational_cstr(a), out);
            break;
        case GV_STRING: {
            /* PeTTa's swrite escapes only quotes and backslashes; its
             * strings keep their newlines, and its reader takes them back. */
            fputc('"', out);
            atom_print_string_bytes(out, a->ground.sval, a->ground.slen,
                                    petta);
            fputc('"', out);
            break;
        }
        case GV_SPACE: {
            /* An HE module space prints by its name, as upstream's does. */
            const char *module_name = space_module_display_name
                ? space_module_display_name(a->ground.ptr) : NULL;
            if (module_name)
                fprintf(out, "ModuleSpace(GroundingSpace-%s)", module_name);
            else
                fprintf(out, "<space %p>", a->ground.ptr);
            break;
        }
        case GV_STATE: {
            StateCell *cell = (StateCell *)a->ground.ptr;
            fputs("(State ", out);
            atom_print_stack_push_char(&stack, ')');
            atom_print_stack_push_atom(&stack, cell->value);
            break;
        }
        case GV_CAPTURE:
            fputs("capture", out);
            break;
        case GV_BINDINGS: {
            CettaBindingsValue *value = a->ground.ptr;
            Arena observation;
            arena_init_detached(&observation);
            Atom *projection = value->observe(&observation, value);
            if (projection)
                atom_print_mode(projection, out, petta, variables);
            arena_free(&observation);
            break;
        }
        case GV_TERM_GRAPH:
            if (term_graph_print)
                term_graph_print(a->ground.ptr, out, petta);
            break;
        case GV_FOREIGN: {
            const CettaForeignHold *hold = a->ground.ptr;
            if (petta && hold && hold->print_petta)
                hold->print_petta(a->ground.ptr, out);
            else
                fprintf(out, "<foreign %p>", a->ground.ptr);
            break;
        }
        case GV_STRUCTURAL: {
            char text[256];
            size_t length = atom_structural_display(a, text, sizeof(text));
            if (length < sizeof(text)) fputs(text, out);
            else {
                char *long_text = cetta_malloc(length + 1u);
                atom_structural_display(a, long_text, length + 1u);
                fputs(long_text, out);
                free(long_text);
            }
            break;
        }
        case GV_SEAL: {
            /* `(Sealed NICK)` and the like: no address, counter or payload. */
            char text[256];
            size_t length = atom_seal_display_text(a, text, sizeof(text));
            if (length < sizeof(text)) {
                fputs(text, out);
            } else {
                char *long_text = cetta_malloc(length + 1u);
                atom_seal_display_text(a, long_text, length + 1u);
                fputs(long_text, out);
                free(long_text);
            }
            break;
        }
        case GV_INTERNAL_TAG:
            fprintf(out, "<internal-tag %ld>", (long)a->ground.ival);
            break;
        case GV_PRIME_NEED_CAPABILITY:
            fputs("<prime-need-capability>", out);
            break;
        case GV_PRIME_CONTEXT:
            fprintf(out, "<context %u>",
                    atom_prime_context_depth(a->ground.prime_context));
            break;
        }
        break;
    case ATOM_EXPR:
        if (petta) {
            int64_t identity = 0;
            if (atom_petta_callable_identity(a, &identity)) {
                fprintf(out, "lambda_%llu", (unsigned long long)identity);
                break;
            }
        }
        if (atom_is_petta_partial(a)) {
            fputs("(partial ", out);
            atom_print_stack_push_char(&stack, ')');
            atom_print_stack_push_atom(&stack, a->expr.elems[2]);
            atom_print_stack_push_char(&stack, ' ');
            atom_print_stack_push_atom(&stack, a->expr.elems[1]);
            break;
        }
        if (atom_is_list(a) || atom_is_list_rest(a)) {
            /* [x1 x2 ... xn] and [x1 ... xk | rest] */
            bool rest = atom_is_list_rest(a);
            fputc('[', out);
            atom_print_stack_push_char(&stack, ']');
            for (CettaExprIndex i = a->expr.len; i > 1u; i--) {
                atom_print_stack_push_atom(&stack, a->expr.elems[i - 1u]);
                if (i > 2u) {
                    if (rest && i == a->expr.len) {
                        atom_print_stack_push_char(&stack, ' ');
                        atom_print_stack_push_char(&stack, '|');
                        atom_print_stack_push_char(&stack, ' ');
                    } else {
                        atom_print_stack_push_char(&stack, ' ');
                    }
                }
            }
            break;
        }
        if (petta) {
            Atom *compound_body = NULL;
            if (atom_petta_prolog_compound_body(
                    a, &compound_body)) {
                atom_print_stack_push_atom(
                    &stack, compound_body);
                break;
            }
        }
        fputc('(', out);
        atom_print_stack_push_char(&stack, ')');
        for (CettaExprIndex i = a->expr.len; i > 0u; i--) {
            atom_print_stack_push_atom(
                &stack, a->expr.elems[i - 1u]);
            if (i > 1u)
                atom_print_stack_push_char(&stack, ' ');
        }
        break;
        }
    }
    free(stack.items);
}

void atom_print(Atom *a, FILE *out) {
    atom_print_mode(a, out, false, NULL);
}

void atom_print_petta(Atom *a, FILE *out) {
    PettaPrintVariables variables = {0};
    atom_print_mode(a, out, true, &variables);
    free(variables.slots);
}

/* The printed text of an atom and its length.  As a C string (`c_text`) it
 * cannot hold NUL, so NUL a lane writes raw is written \x00 there. */
static char *atom_print_to_text(Arena *a, Atom *atom, bool petta, bool c_text,
                                bool source_names, size_t *len_out) {
    char *buffer = NULL;
    size_t length = 0u;
    FILE *memory = open_memstream(&buffer, &length);
    if (len_out)
        *len_out = 0u;
    if (!memory)
        return arena_strdup(a, "");
    bool previous = atom_print_set_c_text(c_text);
    if (source_names)
        atom_print_mode(atom, memory, petta, NULL);
    else if (petta)
        atom_print_petta(atom, memory);
    else
        atom_print(atom, memory);
    fclose(memory);
    atom_print_set_c_text(previous);
    char *result = arena_alloc(a, length + 1u);
    if (length)
        memcpy(result, buffer, length);
    result[length] = '\0';
    free(buffer);
    if (len_out)
        *len_out = length;
    return result;
}

char *atom_to_parseable_string(Arena *a, Atom *atom) {
    return atom_print_to_text(a, atom, false, true, false, NULL);
}

char *atom_to_parseable_string_petta(Arena *a, Atom *atom) {
    return atom_print_to_text(a, atom, true, true, false, NULL);
}

char *atom_to_parseable_bytes(Arena *a, Atom *atom, bool petta,
                              size_t *len_out) {
    return atom_print_to_text(a, atom, petta, false, false, len_out);
}

char *atom_to_source_bytes(Arena *a, Atom *atom, bool petta,
                          size_t *len_out) {
    return atom_print_to_text(a, atom, petta, false, true, len_out);
}

char *atom_to_string(Arena *a, Atom *atom) {
    if (atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        size_t len = atom->ground.slen;
        char *copy = arena_alloc(a, len + 1u);
        if (len)
            memcpy(copy, atom->ground.sval, len);
        copy[len] = '\0';
        return copy;
    }
    return atom_to_parseable_string(a, atom);
}
