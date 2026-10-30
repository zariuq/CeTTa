#include "prime_need.h"
#include "stats.h"
#include "native_cost.h"
#include "identity_counter.h"
#include <assert.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS
#include <stdio.h>
#endif

#ifndef CETTA_PRIME_RECEIPT_PRIMARY_INDEX
#define CETTA_PRIME_RECEIPT_PRIMARY_INDEX 0
#endif

struct PrimeNeedFrame {
    const PrimeNeedFrame *parent;
    /* An ancestor at depth with its lowest set bit cleared. This backedge
     * stays on the parent path and bounds unindexed lineage walks. */
    const PrimeNeedFrame *lineage_skip;
    /* The newest frame at or below this one that records a named cell
     * (prime_need_snapshot_allocate_named).  Named cells are few and are
     * looked up again and again from deep in a long history: following this
     * chain visits only their frames. */
    const PrimeNeedFrame *named_top;
    Arena *owner;
    /* Cached common owner for the complete parent path.  NULL denotes a path
     * whose immutable prefix lives in a longer-lived arena. */
    Arena *closure_owner;
    /* Conservative summary of every arena that owns storage reachable from
     * this path.  When complete, a missing bit proves exclusion in O(1);
     * collisions fall back to the exact graph audit. */
    uint64_t arena_bloom;
    uint32_t arena_min_id;
    uint32_t arena_max_id;
    bool arena_range_complete;
    bool arena_bloom_complete;
    uint64_t session_id;
    uint64_t serial;
    uint64_t update_id;
    uint64_t thunk_id;
    uint64_t depth;
    uint64_t authority_id;
    uint64_t evaluator_id;
    uint64_t storage_key;
    uint64_t import_key;
    uint64_t source_occurrence_id;
    uint64_t source_argument_index;
    PrimeNeedSourceRole source_role;
    PrimeNeedCacheState cache_state;
    PrimeNeedValueRole value_role;
    /* Optional direct producer of this VALUE update, in the same session.
     * Both identities are zero for ordinary results. The update identity
     * distinguishes sibling completions of the same producer cell. */
    uint64_t value_producer_thunk;
    uint64_t value_producer_update;
    Atom *origin;
    Atom *cached;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    bool capture_known;
    const VarId *capture_var_ids;
    size_t capture_var_count;
#endif
#if CETTA_PRIME_NEED_HEAP_INDEX
    /* Exact persistent-index roots for the snapshot ending at this frame.
     * Retaining them on the immutable frame lets suffix promotion restart
     * from a longer-lived prefix without rebuilding or copying that prefix. */
    const PrimeNeedHeapIndexNode *heap_index;
    const PrimeNeedHeapIndexNode *lineage_index;
#endif
};

#if CETTA_PRIME_NEED_HEAP_INDEX
enum {
    PRIME_NEED_HEAP_INDEX_BITS_PER_LEVEL = 4u,
    PRIME_NEED_HEAP_INDEX_LEVELS =
        64u / PRIME_NEED_HEAP_INDEX_BITS_PER_LEVEL,
};

struct PrimeNeedHeapIndexNode {
    const PrimeNeedFrame *value;
    uint16_t child_mask;
    uint16_t child_count;
    const PrimeNeedHeapIndexNode *children[];
};

static bool prime_need_heap_index_enabled(void) {
    static _Atomic int enabled = -1;
    int cached = atomic_load_explicit(&enabled, memory_order_relaxed);
    if (cached >= 0)
        return cached != 0;
    const char *setting = getenv("CETTA_PRIME_NEED_HEAP_INDEX");
    /* A compiled persistent index is the ordinary lookup path. Explicit
     * non-1 settings retain the log implementation for comparison. */
    int selected =
        !setting || (setting[0] == '1' && setting[1] == '\0');
    int expected = -1;
    if (!atomic_compare_exchange_strong_explicit(
            &enabled, &expected, selected,
            memory_order_relaxed, memory_order_relaxed))
        selected = expected;
    return selected != 0;
}

static size_t prime_need_popcount16(uint16_t bits) {
    size_t count = 0u;
    while (bits != 0u) {
        bits = (uint16_t)(bits & (uint16_t)(bits - 1u));
        count++;
    }
    return count;
}

static PrimeNeedHeapIndexNode *prime_need_heap_index_node_alloc(
    Arena *owner, uint16_t child_count) {
    if (!owner ||
        (size_t)child_count >
            (SIZE_MAX - sizeof(PrimeNeedHeapIndexNode)) /
                sizeof(const PrimeNeedHeapIndexNode *))
        return NULL;
    size_t bytes = sizeof(PrimeNeedHeapIndexNode) +
        (size_t)child_count * sizeof(const PrimeNeedHeapIndexNode *);
    PrimeNeedHeapIndexNode *node = arena_alloc(owner, bytes);
    if (!node)
        return NULL;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_INDEX_NODE_ALLOC);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_INDEX_NODE_BYTES,
        (uint64_t)bytes);
    node->value = NULL;
    node->child_mask = 0u;
    node->child_count = child_count;
    return node;
}

static bool prime_need_heap_index_insert_at(
    Arena *owner, const PrimeNeedHeapIndexNode *base,
    uint64_t thunk_id, unsigned depth, const PrimeNeedFrame *value,
    const PrimeNeedHeapIndexNode **out) {
    if (!owner || !value || !out ||
        depth > PRIME_NEED_HEAP_INDEX_LEVELS)
        return false;

    if (depth == PRIME_NEED_HEAP_INDEX_LEVELS) {
        PrimeNeedHeapIndexNode *leaf =
            prime_need_heap_index_node_alloc(owner, 0u);
        if (!leaf)
            return false;
        leaf->value = value;
        *out = leaf;
        return true;
    }

    unsigned shift =
        (PRIME_NEED_HEAP_INDEX_LEVELS - depth - 1u) *
        PRIME_NEED_HEAP_INDEX_BITS_PER_LEVEL;
    unsigned slot = (unsigned)((thunk_id >> shift) & UINT64_C(0xf));
    uint16_t bit = (uint16_t)(UINT16_C(1) << slot);
    uint16_t old_mask = base ? base->child_mask : 0u;
    bool replacing = (old_mask & bit) != 0u;
    size_t position = prime_need_popcount16(
        (uint16_t)(old_mask & (uint16_t)(bit - 1u)));
    const PrimeNeedHeapIndexNode *old_child =
        replacing ? base->children[position] : NULL;
    const PrimeNeedHeapIndexNode *new_child = NULL;
    if (!prime_need_heap_index_insert_at(
            owner, old_child, thunk_id, depth + 1u, value, &new_child))
        return false;

    uint16_t new_mask = (uint16_t)(old_mask | bit);
    uint16_t new_count = (uint16_t)prime_need_popcount16(new_mask);
    PrimeNeedHeapIndexNode *node =
        prime_need_heap_index_node_alloc(owner, new_count);
    if (!node)
        return false;
    node->value = base ? base->value : NULL;
    node->child_mask = new_mask;

    size_t old_position = 0u;
    for (size_t new_position = 0u;
         new_position < (size_t)new_count; new_position++) {
        if (new_position == position) {
            node->children[new_position] = new_child;
            if (replacing)
                old_position++;
        } else {
            node->children[new_position] = base->children[old_position++];
        }
    }
    *out = node;
    return true;
}

static const PrimeNeedFrame *prime_need_heap_index_lookup(
    const PrimeNeedHeapIndexNode *root, uint64_t thunk_id,
    size_t *steps_out) {
    const PrimeNeedHeapIndexNode *cursor = root;
    size_t steps = 0u;
    for (unsigned depth = 0u;
         depth < PRIME_NEED_HEAP_INDEX_LEVELS; depth++) {
        if (!cursor)
            goto not_found;
        unsigned shift =
            (PRIME_NEED_HEAP_INDEX_LEVELS - depth - 1u) *
            PRIME_NEED_HEAP_INDEX_BITS_PER_LEVEL;
        unsigned slot =
            (unsigned)((thunk_id >> shift) & UINT64_C(0xf));
        uint16_t bit = (uint16_t)(UINT16_C(1) << slot);
        steps++;
        if ((cursor->child_mask & bit) == 0u)
            goto not_found;
        size_t position = prime_need_popcount16(
            (uint16_t)(cursor->child_mask &
                       (uint16_t)(bit - 1u)));
        cursor = cursor->children[position];
    }
    steps++;
    if (steps_out)
        *steps_out = steps;
    return cursor ? cursor->value : NULL;

not_found:
    if (steps_out)
        *steps_out = steps;
    return NULL;
}
#endif

/* Identity namespaces.  Each is drawn through cetta_identity_try_take, so it
 * issues every identity at most once in a process and never wraps: once
 * exhausted it refuses for good, and zero is the refusal. */
static _Atomic uint64_t g_prime_need_next_session = 1u;
static _Atomic uint64_t g_prime_need_next_serial = 1u;
static _Atomic uint64_t g_prime_need_next_thunk = 1u;
static _Atomic uint64_t g_prime_need_next_authority = 1u;
static _Atomic uint64_t g_prime_need_next_storage_key = 1u;
static _Atomic uint64_t g_prime_need_next_receipt_session = 1u;
static _Atomic uint64_t g_prime_need_next_receipt_node = 1u;
static _Atomic uint64_t g_prime_need_next_source_occurrence = 1u;
static _Atomic uint64_t g_prime_need_next_evaluator = 1u;
static _Atomic uint64_t g_prime_need_next_resample_scope = 1u;

static _Atomic(PrimeNeedIdentityRefusalObserver)
    g_prime_need_identity_refusal_observer = NULL;

void prime_need_identity_set_refusal_observer(
    PrimeNeedIdentityRefusalObserver observer) {
    if (atomic_load_explicit(&g_prime_need_identity_refusal_observer,
                             memory_order_acquire) != observer)
        atomic_store_explicit(&g_prime_need_identity_refusal_observer,
                              observer, memory_order_release);
}

/* Every refusal passes through here exactly once and returns zero. */
static uint64_t prime_need_identity_refused(void) {
    PrimeNeedIdentityRefusalObserver observer = atomic_load_explicit(
        &g_prime_need_identity_refusal_observer, memory_order_acquire);
    if (observer)
        observer();
    return 0u;
}

/* Moves `counter` beyond `key`, so the namespace never issues it later;
 * reserving UINT64_MAX exhausts the namespace.  The counter only grows, and
 * the compare-and-swap means no draw that starts after this returns can
 * issue `key` or anything below it.  A draw that finished earlier may
 * already hold `key`: a persisted key is checked against its snapshot
 * before use. */
static void prime_need_identity_reserve_through(_Atomic uint64_t *counter,
                                                uint64_t key) {
    uint64_t desired = key == UINT64_MAX ? UINT64_MAX : key + 1u;
    uint64_t current = atomic_load_explicit(counter, memory_order_relaxed);
    while (current < desired &&
           !atomic_compare_exchange_weak_explicit(
               counter, &current, desired,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void prime_need_reserve_storage_keys_through(uint64_t key) {
    prime_need_identity_reserve_through(&g_prime_need_next_storage_key, key);
}

struct PrimeNeedReceiptFrame {
    const PrimeNeedReceiptFrame *left;
    const PrimeNeedReceiptFrame *right;
    Arena *owner;
    /* Cached common owner for the complete reachable sub-DAG.  NULL denotes
     * a cross-arena join. */
    Arena *closure_owner;
    uint64_t arena_bloom;
    uint32_t arena_min_id;
    uint32_t arena_max_id;
    bool arena_range_complete;
    bool arena_bloom_complete;
    uint64_t session_id;
    /* Stable across arena promotion; pointer identity is storage only. */
    uint64_t identity_id;
    uint64_t event_id;
    uint64_t depth;
    bool has_event;
    PrimeNeedReceiptEvent event;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    /* Allocation inventory only; never a causal edge. Mutable solely by
     * this frame's private recording slice before quiescent publication. */
    struct PrimeNeedReceiptFrame *recording_next;
    bool recording_referenced;
#endif
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    const PrimeNeedReceiptFrame **primary_ancestors;
    size_t primary_ancestor_count;
    uint64_t primary_depth;
#endif
};

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
struct PrimeNeedRecordingSlice {
    PrimeNeedRecording *group;
    Arena arena;
    size_t node_quantum, byte_quantum;
    bool growing;
    size_t node_limit, byte_limit, nodes, charge;
    _Atomic uint64_t omitted;
    _Atomic bool incomplete, overflow;
    PrimeNeedReceiptFrame *first, *last;
    /* A quiescent nested publication can adopt an existing root without
     * moving its allocation inventory or manufacturing an event. */
    const PrimeNeedReceiptFrame *published_root;
};

struct PrimeNeedRecording {
    _Atomic size_t references;
    _Atomic size_t payload_references;
    _Atomic bool forgotten;
    _Atomic size_t owner_links;
    atomic_flag budget_lock;
    PrimeNeedRecordingLimits limits;
    uint64_t session_id;
    size_t slice_count, reserved_nodes, reserved_bytes;
    uint64_t publication_revision;
    size_t allocation_bytes, metadata_bound;
    bool sealed;
    /* A prior recorder is retained as a carrier control block, not as notes. */
    PrimeNeedRecording *previous;
    PrimeNeedRecordingSlice slices[];
};

struct PrimeNeedRecordingCopy {
    PrimeNeedRecording *source, *image;
    AtomDeepCopySession *session;
    PrimeNeedRecordingStatus captured;
    size_t restored_links;
    bool attempted, prepared;
    bool source_forgotten;
};
#endif

typedef struct {
    const void **slots;
    size_t cap;
    size_t used;
} PrimeNeedPointerSet;

struct PrimeNeedArenaAudit {
    const Arena *forbidden;
    PrimeNeedPointerSet snapshot_frames;
    PrimeNeedPointerSet receipt_frames;
};

static uint64_t prime_need_arena_id_bloom_mask(uint32_t identity) {
    if (identity == 0u)
        return 0u;
    uint64_t value = identity;
    value ^= value >> 16u;
    value *= UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 29u;
    return (UINT64_C(1) << (value & 63u)) |
           (UINT64_C(1) << ((value >> 6u) & 63u));
}

static uint64_t prime_need_arena_bloom_mask(const Arena *arena) {
    return arena ? prime_need_arena_id_bloom_mask(arena->identity) : 0u;
}

static void prime_need_arena_range_extend(
    uint32_t identity, uint32_t *min_id, uint32_t *max_id) {
    if (identity == 0u || !min_id || !max_id)
        return;
    if (*min_id == 0u || identity < *min_id)
        *min_id = identity;
    if (identity > *max_id)
        *max_id = identity;
}

static bool prime_need_arena_range_excludes(
    uint32_t min_id, uint32_t max_id, bool complete,
    const Arena *forbidden) {
    return complete && forbidden && forbidden->identity != 0u &&
           min_id != 0u &&
           (forbidden->identity < min_id ||
            forbidden->identity > max_id);
}

/* The arena summaries read atom closure facts, which describe an atom's
 * children, not the element vector a suffix view shares.  In an arena with
 * an older generation a view may share that generation's vector, so such an
 * arena owns no Need storage; no evaluator arena that does has one. */
static bool prime_need_owner_admissible(const Arena *owner) {
    return owner && owner->older_identity == 0u;
}

static bool prime_need_atom_extend_arena_bloom(
    const Atom *atom, uint64_t *bloom,
    uint32_t *min_id, uint32_t *max_id) {
    if (!atom)
        return true;
    if (!bloom || !atom_graph_has_owner_closure(atom))
        return false;
    *bloom |= prime_need_arena_id_bloom_mask(atom->arena_id);
    prime_need_arena_range_extend(atom->arena_id, min_id, max_id);
    return true;
}

static bool prime_need_atom_extend_arena_range(
    const Atom *atom, uint32_t *min_id, uint32_t *max_id) {
    if (!atom)
        return true;
    uint32_t atom_min_id = 0u;
    uint32_t atom_max_id = 0u;
    if (!atom_graph_arena_id_bounds(
            atom, &atom_min_id, &atom_max_id))
        return false;
    prime_need_arena_range_extend(atom_min_id, min_id, max_id);
    prime_need_arena_range_extend(atom_max_id, min_id, max_id);
    return true;
}

static bool prime_need_arena_bloom_excludes(
    uint64_t bloom, bool complete, const Arena *forbidden) {
    const uint64_t mask = prime_need_arena_bloom_mask(forbidden);
    return complete && mask != 0u && (bloom & mask) != mask;
}

static void prime_need_receipt_refresh_arena_summary(
    PrimeNeedReceiptFrame *frame) {
    if (!frame || !frame->owner)
        return;
    const PrimeNeedReceiptFrame *left = frame->left;
    const PrimeNeedReceiptFrame *right = frame->right;
    frame->arena_bloom = prime_need_arena_bloom_mask(frame->owner) |
        (left ? left->arena_bloom : 0u) |
        (right ? right->arena_bloom : 0u);
    frame->arena_min_id = 0u;
    frame->arena_max_id = 0u;
    prime_need_arena_range_extend(
        frame->owner->identity,
        &frame->arena_min_id, &frame->arena_max_id);
    if (left) {
        prime_need_arena_range_extend(
            left->arena_min_id,
            &frame->arena_min_id, &frame->arena_max_id);
        prime_need_arena_range_extend(
            left->arena_max_id,
            &frame->arena_min_id, &frame->arena_max_id);
    }
    if (right) {
        prime_need_arena_range_extend(
            right->arena_min_id,
            &frame->arena_min_id, &frame->arena_max_id);
        prime_need_arena_range_extend(
            right->arena_max_id,
            &frame->arena_min_id, &frame->arena_max_id);
    }
    const bool payload_bloom_complete =
        !frame->has_event ||
        (prime_need_atom_extend_arena_bloom(
             frame->event.before, &frame->arena_bloom,
             &frame->arena_min_id, &frame->arena_max_id) &&
         prime_need_atom_extend_arena_bloom(
             frame->event.after, &frame->arena_bloom,
             &frame->arena_min_id, &frame->arena_max_id) &&
         !frame->event.state_cell);
    frame->arena_bloom_complete =
        payload_bloom_complete &&
        (!left || left->arena_bloom_complete) &&
        (!right || right->arena_bloom_complete);
    const bool payload_range_complete =
        !frame->has_event ||
        (prime_need_atom_extend_arena_range(
             frame->event.before,
             &frame->arena_min_id, &frame->arena_max_id) &&
         prime_need_atom_extend_arena_range(
             frame->event.after,
             &frame->arena_min_id, &frame->arena_max_id) &&
         !frame->event.state_cell);
    frame->arena_range_complete =
        payload_range_complete &&
        (!left || left->arena_range_complete) &&
        (!right || right->arena_range_complete);
    const bool payload_owner_closed =
        !frame->has_event ||
        ((!frame->event.before || atom_graph_is_owner_closed_for_arena(
             frame->owner, frame->event.before)) &&
         (!frame->event.after || atom_graph_is_owner_closed_for_arena(
             frame->owner, frame->event.after)) &&
         !frame->event.state_cell);
    frame->closure_owner =
        payload_owner_closed &&
        (!left || left->closure_owner == frame->owner) &&
        (!right || right->closure_owner == frame->owner)
            ? frame->owner : NULL;
}

static size_t prime_need_pointer_hash(const void *ptr) {
    uintptr_t bits = (uintptr_t)ptr;
    bits >>= 4u;
    bits ^= bits >> 17u;
    bits *= (uintptr_t)UINT64_C(0x9e3779b97f4a7c15);
    return (size_t)(bits ^ (bits >> 29u));
}

static bool prime_need_pointer_set_reserve(
    PrimeNeedPointerSet *set, size_t needed) {
    if (!set)
        return false;
    size_t cap = set->cap ? set->cap : 16u;
    while (needed > cap / 2u) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (cap == set->cap)
        return true;
    const void **slots = calloc(cap, sizeof(*slots));
    if (!slots)
        return false;
    size_t mask = cap - 1u;
    for (size_t i = 0u; i < set->cap; i++) {
        const void *item = set->slots[i];
        if (!item)
            continue;
        size_t slot = prime_need_pointer_hash(item) & mask;
        while (slots[slot])
            slot = (slot + 1u) & mask;
        slots[slot] = item;
    }
    free(set->slots);
    set->slots = slots;
    set->cap = cap;
    return true;
}

static bool prime_need_pointer_set_insert(
    PrimeNeedPointerSet *set, const void *ptr, bool *inserted) {
    if (!set || !ptr || !inserted ||
        !prime_need_pointer_set_reserve(set, set->used + 1u))
        return false;
    size_t mask = set->cap - 1u;
    size_t slot = prime_need_pointer_hash(ptr) & mask;
    while (set->slots[slot]) {
        if (set->slots[slot] == ptr) {
            *inserted = false;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    set->slots[slot] = ptr;
    set->used++;
    *inserted = true;
    return true;
}

PrimeNeedArenaAudit *prime_need_arena_audit_new(
    const Arena *forbidden) {
    if (!forbidden)
        return NULL;
    PrimeNeedArenaAudit *audit = calloc(1u, sizeof(*audit));
    if (!audit)
        return NULL;
    audit->forbidden = forbidden;
    return audit;
}

bool prime_need_arena_audit_snapshot(
    PrimeNeedArenaAudit *audit, const PrimeNeedSnapshot *snapshot) {
    if (!audit || !snapshot)
        return false;
    if (snapshot->session_id == 0u)
        return true;
    if (snapshot->top &&
        (!snapshot->owner ||
         snapshot->owner != snapshot->top->owner ||
         snapshot->closure_owner != snapshot->top->closure_owner ||
         snapshot->owner == audit->forbidden))
        return false;
    if (snapshot->top && snapshot->closure_owner)
        return snapshot->closure_owner != audit->forbidden;
    if (snapshot->top && prime_need_arena_range_excludes(
            snapshot->top->arena_min_id,
            snapshot->top->arena_max_id,
            snapshot->top->arena_range_complete,
            audit->forbidden))
        return true;
    if (snapshot->top && prime_need_arena_bloom_excludes(
            snapshot->top->arena_bloom,
            snapshot->top->arena_bloom_complete,
            audit->forbidden))
        return true;
#if CETTA_PRIME_NEED_HEAP_INDEX
    if (snapshot->heap_index &&
        arena_owns_ptr(audit->forbidden, snapshot->heap_index))
        return false;
    if (snapshot->lineage_index &&
        arena_owns_ptr(audit->forbidden, snapshot->lineage_index))
        return false;
#endif
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        bool inserted = false;
        if (!prime_need_pointer_set_insert(
                &audit->snapshot_frames, frame, &inserted))
            return false;
        if (!inserted)
            break;
        if (frame->closure_owner)
            return frame->closure_owner != audit->forbidden;
        if (prime_need_arena_range_excludes(
                frame->arena_min_id, frame->arena_max_id,
                frame->arena_range_complete, audit->forbidden))
            return true;
        if (prime_need_arena_bloom_excludes(
                frame->arena_bloom, frame->arena_bloom_complete,
                audit->forbidden))
            return true;
        if (!frame->owner || frame->owner == audit->forbidden ||
            arena_owns_atom(audit->forbidden, frame->origin) ||
            arena_owns_atom(audit->forbidden, frame->cached))
            return false;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        if (frame->capture_var_ids &&
            arena_owns_ptr(
                audit->forbidden, frame->capture_var_ids))
            return false;
#endif
    }
    return true;
}

bool prime_need_arena_audit_branch_state(
    PrimeNeedArenaAudit *audit, const PrimeNeedBranchState *receipt) {
    if (!audit || !receipt)
        return false;
    if (receipt->session_id == 0u || !receipt->owner)
        return true;
    /* The initialized carrier is the identity of the event-DAG algebra.  Its
     * owner records where future events should be allocated, but with no top
     * frame it retains no allocation from that arena and therefore excludes
     * every forbidden arena.  Event-bearing carriers retain the exact owner
     * and transitive-frame audit below. */
    if (!prime_need_branch_state_has_events(receipt))
        return true;
    if (receipt->owner == audit->forbidden ||
        (receipt->top && receipt->owner != receipt->top->owner))
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_AUDIT);
    if (receipt->top && receipt->top->closure_owner)
        return receipt->top->closure_owner != audit->forbidden;
    if (receipt->top && prime_need_arena_range_excludes(
            receipt->top->arena_min_id,
            receipt->top->arena_max_id,
            receipt->top->arena_range_complete,
            audit->forbidden))
        return true;
    if (receipt->top && prime_need_arena_bloom_excludes(
            receipt->top->arena_bloom,
            receipt->top->arena_bloom_complete,
            audit->forbidden))
        return true;

    const PrimeNeedReceiptFrame **work = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    if (receipt->top) {
        cap = 16u;
        work = malloc(cap * sizeof(*work));
        if (!work)
            return false;
        work[len++] = receipt->top;
    }
    bool excludes = true;
    while (excludes && len > 0u) {
        const PrimeNeedReceiptFrame *frame = work[--len];
        bool inserted = false;
        if (!prime_need_pointer_set_insert(
                &audit->receipt_frames, frame, &inserted)) {
            excludes = false;
            break;
        }
        if (!inserted)
            continue;
        if (frame->closure_owner) {
            excludes = frame->closure_owner != audit->forbidden;
            continue;
        }
        if (prime_need_arena_range_excludes(
                frame->arena_min_id, frame->arena_max_id,
                frame->arena_range_complete, audit->forbidden))
            continue;
        if (prime_need_arena_bloom_excludes(
                frame->arena_bloom, frame->arena_bloom_complete,
                audit->forbidden))
            continue;
        excludes = frame->owner &&
            frame->owner != audit->forbidden &&
            !arena_owns_atom(
                audit->forbidden, frame->event.before) &&
            !arena_owns_atom(
                audit->forbidden, frame->event.after) &&
            !arena_owns_ptr(audit->forbidden, frame->event.state_cell);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
        excludes =
            excludes &&
            !arena_owns_ptr(
                audit->forbidden, frame->primary_ancestors);
#endif
        if (!excludes)
            break;
        size_t additions =
            (frame->left ? 1u : 0u) + (frame->right ? 1u : 0u);
        if (additions > SIZE_MAX - len) {
            excludes = false;
            break;
        }
        size_t needed = len + additions;
        if (needed > cap) {
            size_t next = cap ? cap : 16u;
            while (next < needed) {
                if (next > SIZE_MAX / 2u) {
                    excludes = false;
                    break;
                }
                next *= 2u;
            }
            if (!excludes)
                break;
            const PrimeNeedReceiptFrame **grown =
                realloc(work, next * sizeof(*grown));
            if (!grown) {
                excludes = false;
                break;
            }
            work = grown;
            cap = next;
        }
        if (frame->left)
            work[len++] = frame->left;
        if (frame->right)
            work[len++] = frame->right;
    }
    free(work);
    return excludes;
}

void prime_need_arena_audit_free(PrimeNeedArenaAudit *audit) {
    if (!audit)
        return;
    free(audit->snapshot_frames.slots);
    free(audit->receipt_frames.slots);
    free(audit);
}

#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
static void prime_need_receipt_build_primary_index(
    Arena *owner, PrimeNeedReceiptFrame *frame) {
    frame->primary_ancestors = NULL;
    frame->primary_ancestor_count = 0u;
    frame->primary_depth = 0u;
    if (!owner || !frame->left ||
        frame->left->primary_depth == UINT64_MAX)
        return;

    frame->primary_depth = frame->left->primary_depth + 1u;
    size_t level_count = 0u;
    for (uint64_t distance = frame->primary_depth;
         distance != 0u; distance >>= 1u)
        level_count++;
    if (level_count == 0u ||
        level_count > SIZE_MAX / sizeof(*frame->primary_ancestors))
        return;

    const PrimeNeedReceiptFrame **ancestors = arena_alloc(
        owner, level_count * sizeof(*ancestors));
    if (!ancestors)
        return;
    ancestors[0] = frame->left;
    size_t built = 1u;
    for (size_t level = 1u; level < level_count; level++) {
        const PrimeNeedReceiptFrame *half = ancestors[level - 1u];
        if (!half || half->primary_ancestor_count < level)
            break;
        ancestors[level] = half->primary_ancestors[level - 1u];
        if (!ancestors[level])
            break;
        built++;
    }
    frame->primary_ancestors = ancestors;
    frame->primary_ancestor_count = built;
}

static bool prime_need_receipt_primary_reaches(
    const PrimeNeedReceiptFrame *top,
    const PrimeNeedReceiptFrame *target,
    size_t *steps_out) {
    size_t steps = 0u;
    if (!top || !target ||
        top->primary_depth < target->primary_depth)
        goto not_found;

    const PrimeNeedReceiptFrame *cursor = top;
    uint64_t distance =
        top->primary_depth - target->primary_depth;
    size_t level = 0u;
    while (distance != 0u) {
        if ((distance & 1u) != 0u) {
            if (!cursor || cursor->primary_ancestor_count <= level)
                goto not_found;
            cursor = cursor->primary_ancestors[level];
            steps++;
        }
        distance >>= 1u;
        level++;
    }
    steps++;
    if (steps_out)
        *steps_out = steps;
    return cursor && cursor->identity_id == target->identity_id;

not_found:
    if (steps_out)
        *steps_out = steps;
    return false;
}
#endif

static uint64_t prime_need_fresh_nonzero(_Atomic uint64_t *counter) {
    uint64_t identity = cetta_identity_try_take(counter);
    return identity != 0u ? identity : prime_need_identity_refused();
}

/* Fresh thunk identities stay below PRIME_NEED_NAMED_CELL_BIT: the upper
 * half belongs to the identities callers choose
 * (prime_need_snapshot_allocate_named).  Reaching the half exhausts the
 * fresh namespace for good, as reaching UINT64_MAX does elsewhere. */
static uint64_t prime_need_fresh_thunk_id(void) {
    uint64_t thunk_id = prime_need_fresh_nonzero(&g_prime_need_next_thunk);
    if ((thunk_id & PRIME_NEED_NAMED_CELL_BIT) == 0u)
        return thunk_id;
    prime_need_identity_reserve_through(&g_prime_need_next_thunk, UINT64_MAX);
    return prime_need_identity_refused();
}

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
static int prime_need_var_id_compare(const void *left, const void *right) {
    VarId l = *(const VarId *)left;
    VarId r = *(const VarId *)right;
    return var_id_cmp(l, r);
}

static bool prime_need_capture_copy(
    Arena *owner, const VarId *ids, size_t count,
    const VarId **out_ids, size_t *out_count) {
    if (!owner || !out_ids || !out_count || (count != 0u && !ids) ||
        count > SIZE_MAX / sizeof(*ids))
        return false;
    *out_ids = NULL;
    *out_count = 0u;
    if (count == 0u)
        return true;
    VarId *copy = arena_alloc(owner, count * sizeof(*copy));
    if (!copy)
        return false;
    memcpy(copy, ids, count * sizeof(*copy));
    qsort(copy, count, sizeof(*copy), prime_need_var_id_compare);
    size_t unique_count = 0u;
    for (size_t i = 0u; i < count; i++) {
        if (var_id_is_none(copy[i]))
            return false;
        if (unique_count == 0u || !var_id_eq(copy[unique_count - 1u], copy[i]))
            copy[unique_count++] = copy[i];
    }
    *out_ids = copy;
    *out_count = unique_count;
    return true;
}

static bool prime_need_capture_equal(
    const PrimeNeedCellView *cell,
    const VarId *ids, size_t count) {
    if (!cell || !cell->capture_known || cell->capture_var_count != count)
        return false;
    if (count == 0u)
        return true;
    if (!cell->capture_var_ids || !ids) return false;
    for (size_t i = 0u; i < count; i++)
        if (!var_id_eq(cell->capture_var_ids[i], ids[i])) return false;
    return true;
}
#endif

void prime_need_snapshot_init(PrimeNeedSnapshot *snapshot) {
    if (!snapshot)
        return;
    snapshot->top = NULL;
    snapshot->session_id = 0u;
    snapshot->max_storage_key = 0u;
    snapshot->owner = NULL;
    snapshot->closure_owner = NULL;
#if CETTA_PRIME_NEED_HEAP_INDEX
    snapshot->heap_index = NULL;
    snapshot->lineage_index = NULL;
#endif
}

bool prime_need_snapshot_present(const PrimeNeedSnapshot *snapshot) {
    return snapshot && snapshot->session_id != 0u;
}

bool prime_need_snapshot_begin(PrimeNeedSnapshot *snapshot) {
    if (!snapshot)
        return false;
    if (prime_need_snapshot_present(snapshot))
        return true;
    snapshot->top = NULL;
    snapshot->session_id = prime_need_fresh_nonzero(
        &g_prime_need_next_session);
    snapshot->max_storage_key = 0u;
    snapshot->owner = NULL;
    snapshot->closure_owner = NULL;
#if CETTA_PRIME_NEED_HEAP_INDEX
    snapshot->heap_index = NULL;
    snapshot->lineage_index = NULL;
#endif
    return snapshot->session_id != 0u;
}

static const PrimeNeedFrame *prime_need_frame_at_depth(
    const PrimeNeedFrame *frame, uint64_t depth, size_t *steps_out) {
    size_t steps = 0u;
    while (frame && frame->depth > depth) {
        frame = frame->lineage_skip && frame->lineage_skip->depth >= depth
            ? frame->lineage_skip : frame->parent;
        steps++;
    }
    if (steps_out)
        *steps_out = steps;
    return frame && frame->depth == depth ? frame : NULL;
}

bool prime_need_snapshot_is_ancestor(const PrimeNeedSnapshot *ancestor,
                                     const PrimeNeedSnapshot *descendant) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_ANCESTOR_QUERY);
    if (!ancestor || !descendant)
        return false;
    if (!prime_need_snapshot_present(ancestor))
        return true;
    if (!prime_need_snapshot_present(descendant) ||
        ancestor->session_id != descendant->session_id)
        return false;
    if (!ancestor->top)
        return true;
    if (ancestor->top == descendant->top)
        return true;
    if (!descendant->top ||
        ancestor->top->depth > descendant->top->depth)
        return false;
    const PrimeNeedFrame *at_depth = NULL;
#if CETTA_PRIME_NEED_HEAP_INDEX
    if (prime_need_heap_index_enabled() && descendant->lineage_index) {
        size_t steps = 0u;
        at_depth = prime_need_heap_index_lookup(
            descendant->lineage_index, ancestor->top->depth, &steps);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_ANCESTOR_INDEX_STEP,
            (uint64_t)steps);
    } else
#endif
    {
        size_t steps = 0u;
        at_depth = prime_need_frame_at_depth(
            descendant->top, ancestor->top->depth, &steps);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_ANCESTOR_LOG_STEP,
            (uint64_t)steps);
    }
    return at_depth && at_depth->serial == ancestor->top->serial;
}

static void prime_need_snapshot_view_at(const PrimeNeedSnapshot *source,
                                        const PrimeNeedFrame *top,
                                        PrimeNeedSnapshot *out) {
    *out = *source;
    out->top = top;
    if (top) {
        out->owner = top->owner;
        out->closure_owner = top->closure_owner;
#if CETTA_PRIME_NEED_HEAP_INDEX
        out->heap_index = top->heap_index;
        out->lineage_index = top->lineage_index;
#endif
    } else {
        out->owner = NULL;
        out->closure_owner = NULL;
#if CETTA_PRIME_NEED_HEAP_INDEX
        out->heap_index = NULL;
        out->lineage_index = NULL;
#endif
    }
}

bool prime_need_snapshot_common_ancestor(const PrimeNeedSnapshot *left,
                                         const PrimeNeedSnapshot *right,
                                         PrimeNeedSnapshot *out) {
    if (!left || !right || !out || !prime_need_snapshot_present(left) ||
        !prime_need_snapshot_present(right) ||
        left->session_id != right->session_id)
        return false;
    const PrimeNeedFrame *l = left->top;
    const PrimeNeedFrame *r = right->top;
    while (l && r && l->depth > r->depth)
        l = l->parent;
    while (l && r && r->depth > l->depth)
        r = r->parent;
    /* Promotion copies frames and keeps their serials: equal serials at
     * equal depth are the same history. */
    while (l && r && l->serial != r->serial) {
        l = l->parent;
        r = r->parent;
    }
    prime_need_snapshot_view_at(left, l && r ? l : NULL, out);
    if (right->max_storage_key > out->max_storage_key)
        out->max_storage_key = right->max_storage_key;
    return true;
}


typedef struct {
    uint64_t *slots;
    size_t cap;
    size_t used;
} PrimeNeedThunkSet;

/* 1 when `thunk_id` was added, 0 when it was there, -1 without memory. */
static int prime_need_thunk_set_insert(PrimeNeedThunkSet *set,
                                       uint64_t thunk_id) {
    if ((set->used + 1u) * 2u > set->cap) {
        size_t cap = set->cap ? set->cap * 2u : 64u;
        uint64_t *slots = calloc(cap, sizeof(*slots));
        if (!slots)
            return -1;
        for (size_t i = 0u; i < set->cap; i++) {
            uint64_t id = set->slots[i];
            if (id == 0u)
                continue;
            size_t at = (size_t)((id * UINT64_C(0x9E3779B97F4A7C15)) >> 7) &
                        (cap - 1u);
            while (slots[at] != 0u)
                at = (at + 1u) & (cap - 1u);
            slots[at] = id;
        }
        free(set->slots);
        set->slots = slots;
        set->cap = cap;
    }
    size_t at = (size_t)((thunk_id * UINT64_C(0x9E3779B97F4A7C15)) >> 7) &
                (set->cap - 1u);
    while (set->slots[at] != 0u) {
        if (set->slots[at] == thunk_id)
            return 0;
        at = (at + 1u) & (set->cap - 1u);
    }
    set->slots[at] = thunk_id;
    set->used++;
    return 1;
}

bool prime_need_snapshot_settled_ancestor(const PrimeNeedSnapshot *base,
                                          const PrimeNeedSnapshot *snapshot,
                                          PrimeNeedSnapshot *out) {
    if (!base || !snapshot || !out ||
        !prime_need_snapshot_present(snapshot) ||
        !prime_need_snapshot_is_ancestor(base, snapshot))
        return false;
    uint64_t floor = prime_need_snapshot_present(base) && base->top
        ? base->top->depth : 0u;
    const PrimeNeedFrame *top = snapshot->top;
    for (;;) {
        /* The newest record of each cell above `base`.  A cell still
         * evaluating there cuts the history below the frame where its
         * evaluation began; then the shorter history is examined again. */
        PrimeNeedThunkSet seen = {0};
        const PrimeNeedFrame *cut = NULL;
        bool ok = true;
        for (const PrimeNeedFrame *frame = top;
             frame && frame->depth > floor; frame = frame->parent) {
            int added = prime_need_thunk_set_insert(&seen, frame->thunk_id);
            if (added < 0) {
                ok = false;
                break;
            }
            if (added > 0 && frame->cache_state == PRIME_NEED_CACHE_EVALUATING)
                cut = frame;
        }
        free(seen.slots);
        if (!ok)
            return false;
        if (!cut)
            break;
        top = cut->parent;
    }
    prime_need_snapshot_view_at(snapshot, top, out);
    return true;
}

bool prime_need_snapshot_merge(PrimeNeedSnapshot *dst,
                               const PrimeNeedSnapshot *src) {
    if (!dst || !src || !prime_need_snapshot_present(src))
        return dst != NULL;
    if (prime_need_snapshot_present(dst) &&
        dst->session_id == src->session_id && dst->top == src->top) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_MERGE_IDENTICAL);
        *dst = *src;
        return true;
    }
    if (!prime_need_snapshot_present(dst) ||
        prime_need_snapshot_is_ancestor(dst, src)) {
        *dst = *src;
        return true;
    }
#ifdef CETTA_PRIME_NEED_MUTATION_SIBLING_MERGE
    /* Deliberately invalid mutation: sibling refinements are not ordered. */
    *dst = *src;
    return true;
#endif
    return prime_need_snapshot_is_ancestor(src, dst);
}

static void prime_need_frame_summarize(PrimeNeedFrame *frame, Arena *owner,
    const PrimeNeedFrame *parent, const Arena *prefix_closure,
    Atom *origin, Atom *cached
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    , const VarId *capture_var_ids
#endif
    ) {
    uint64_t payload_bloom = 0u;
    uint32_t payload_min_id = 0u;
    uint32_t payload_max_id = 0u;
    const bool payload_bloom_complete =
        prime_need_atom_extend_arena_bloom(
            origin, &payload_bloom,
            &payload_min_id, &payload_max_id) &&
        prime_need_atom_extend_arena_bloom(
            cached, &payload_bloom,
            &payload_min_id, &payload_max_id);
    uint32_t payload_range_min_id = 0u;
    uint32_t payload_range_max_id = 0u;
    const bool payload_range_complete =
        prime_need_atom_extend_arena_range(
            origin, &payload_range_min_id,
            &payload_range_max_id) &&
        prime_need_atom_extend_arena_range(
            cached, &payload_range_min_id,
            &payload_range_max_id);
    const bool payload_owner_closed =
        atom_graph_is_owner_closed_for_arena(owner, origin) &&
        (!cached || atom_graph_is_owner_closed_for_arena(owner, cached));
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    const bool capture_closed =
        !capture_var_ids || arena_owns_ptr(owner, capture_var_ids);
#else
    const bool capture_closed = true;
#endif
    const bool prefix_bloom_complete =
        !parent || parent->arena_bloom_complete;
    const bool prefix_range_complete =
        !parent || parent->arena_range_complete;
    frame->arena_bloom =
        (parent ? parent->arena_bloom : 0u) |
        prime_need_arena_bloom_mask(owner) | payload_bloom;
    frame->arena_min_id = 0u;
    frame->arena_max_id = 0u;
    prime_need_arena_range_extend(
        owner->identity,
        &frame->arena_min_id, &frame->arena_max_id);
    if (parent) {
        prime_need_arena_range_extend(
            parent->arena_min_id,
            &frame->arena_min_id, &frame->arena_max_id);
        prime_need_arena_range_extend(
            parent->arena_max_id,
            &frame->arena_min_id, &frame->arena_max_id);
    }
    prime_need_arena_range_extend(
        payload_min_id,
        &frame->arena_min_id, &frame->arena_max_id);
    prime_need_arena_range_extend(
        payload_max_id,
        &frame->arena_min_id, &frame->arena_max_id);
    prime_need_arena_range_extend(
        payload_range_min_id,
        &frame->arena_min_id, &frame->arena_max_id);
    prime_need_arena_range_extend(
        payload_range_max_id,
        &frame->arena_min_id, &frame->arena_max_id);
    frame->arena_bloom_complete =
        prefix_bloom_complete && payload_bloom_complete && capture_closed;
    frame->arena_range_complete =
        prefix_range_complete && payload_range_complete && capture_closed;
    frame->closure_owner =
        payload_owner_closed &&
        (!parent || prefix_closure == owner)
            ? owner : NULL;
}

static bool prime_need_snapshot_push(Arena *owner,
                                     const PrimeNeedSnapshot *base,
                                     uint64_t serial,
                                     uint64_t update_id,
                                     uint64_t thunk_id,
                                     uint64_t authority_id,
                                     uint64_t evaluator_id,
                                     uint64_t storage_key,
                                     uint64_t import_key,
                                     uint64_t source_occurrence_id,
                                     uint64_t source_argument_index,
                                     PrimeNeedSourceRole source_role,
                                     PrimeNeedCacheState cache_state,
                                     PrimeNeedValueRole value_role,
                                     uint64_t value_producer_thunk,
                                     uint64_t value_producer_update,
                                     Atom *origin,
                                     Atom *cached,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
                                     bool capture_known,
                                     const VarId *capture_var_ids,
                                     size_t capture_var_count,
#endif
                                     PrimeNeedSnapshot *out) {
    if (!prime_need_owner_admissible(owner) || !base || !out ||
        !prime_need_snapshot_present(base) ||
        thunk_id == 0u || authority_id == 0u || !origin ||
        (source_role != PRIME_NEED_SOURCE_INFERRED &&
         source_role != PRIME_NEED_SOURCE_COMPUTATION) ||
        ((value_producer_thunk == 0u) != (value_producer_update == 0u)) ||
        (value_producer_thunk && (cache_state != PRIME_NEED_CACHE_VALUE ||
            value_producer_thunk == thunk_id)) ||
        (value_role != PRIME_NEED_VALUE_DEMAND_RESULT &&
         value_role != PRIME_NEED_VALUE_READY) ||
        (value_role == PRIME_NEED_VALUE_READY &&
         cache_state != PRIME_NEED_CACHE_VALUE))
        return false;
    /* Refused serial admission precedes allocation and the HEAP_UPDATE. */
    if (serial == 0u) {
        serial = prime_need_fresh_nonzero(&g_prime_need_next_serial);
        if (serial == 0u)
            return false;
    }
    PrimeNeedFrame *frame = arena_alloc(owner, sizeof(*frame));
    if (!frame)
        return false;
    cetta_native_cost_charge(CETTA_COST_HEAP_UPDATE, 1u);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_FRAME_ALLOC);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_FRAME_BYTES,
        (uint64_t)sizeof(*frame));
    frame->parent = base->top;
    frame->named_top = (thunk_id & PRIME_NEED_NAMED_CELL_BIT) != 0u
        ? frame : base->top ? base->top->named_top : NULL;
    frame->owner = owner;
    prime_need_frame_summarize(frame, owner, base->top, base->closure_owner,
        origin, cached
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        , capture_var_ids
#endif
        );
    frame->session_id = base->session_id;
    frame->serial = serial;
    frame->update_id = update_id ? update_id : serial;
    frame->thunk_id = thunk_id;
    frame->depth = base->top ? base->top->depth + 1u : 1u;
    frame->lineage_skip = prime_need_frame_at_depth(
        frame->parent, frame->depth & (frame->depth - 1u), NULL);
    frame->authority_id = authority_id;
    frame->evaluator_id = evaluator_id;
    frame->storage_key = storage_key;
    frame->import_key = import_key;
    frame->source_occurrence_id = source_occurrence_id;
    frame->source_argument_index = source_argument_index;
    frame->source_role = source_role;
    frame->cache_state = cache_state;
    frame->value_role = value_role;
    frame->value_producer_thunk = value_producer_thunk;
    frame->value_producer_update = value_producer_update;
    frame->origin = origin;
    frame->cached = cached;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    frame->capture_known = capture_known;
    frame->capture_var_ids = capture_var_ids;
    frame->capture_var_count = capture_var_count;
#endif
    out->top = frame;
    out->session_id = base->session_id;
    out->max_storage_key =
        storage_key > base->max_storage_key
            ? storage_key
            : base->max_storage_key;
    out->owner = owner;
    out->closure_owner = frame->closure_owner;
#if CETTA_PRIME_NEED_HEAP_INDEX
    out->heap_index = NULL;
    out->lineage_index = NULL;
    if (prime_need_heap_index_enabled()) {
        if (!base->top || base->heap_index) {
            const PrimeNeedHeapIndexNode *index = NULL;
            if (prime_need_heap_index_insert_at(
                    owner, base->heap_index, thunk_id, 0u, frame, &index))
                out->heap_index = index;
        }
        if (!base->top || base->lineage_index) {
            const PrimeNeedHeapIndexNode *index = NULL;
            if (prime_need_heap_index_insert_at(
                    owner, base->lineage_index, frame->depth, 0u,
                    frame, &index))
                out->lineage_index = index;
        }
    }
    frame->heap_index = out->heap_index;
    frame->lineage_index = out->lineage_index;
#endif
    return true;
}

static void prime_need_cell_view_from_frame(
    const PrimeNeedFrame *frame, PrimeNeedCellView *out) {
    out->cache_state = frame->cache_state;
    out->value_role = frame->value_role;
    out->update_id = frame->update_id;
    out->value_producer_thunk = frame->value_producer_thunk;
    out->value_producer_update = frame->value_producer_update;
    out->origin = frame->origin;
    out->cached = frame->cached;
    out->authority_id = frame->authority_id;
    out->evaluator_id = frame->evaluator_id;
    out->storage_key = frame->storage_key;
    out->import_key = frame->import_key;
    out->source_occurrence_id = frame->source_occurrence_id;
    out->source_argument_index = frame->source_argument_index;
    out->source_role = frame->source_role;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    out->capture_known = frame->capture_known;
    out->capture_var_ids = frame->capture_var_ids;
    out->capture_var_count = frame->capture_var_count;
#endif
}

bool prime_need_snapshot_history_begin(const PrimeNeedSnapshot *snapshot,
                                      PrimeNeedHistoryIterator *iterator) {
    if (!iterator)
        return false;
    *iterator = (PrimeNeedHistoryIterator){.invalid = true};
    if (!prime_need_snapshot_present(snapshot))
        return false;
    if (snapshot->top && (snapshot->top->session_id != snapshot->session_id ||
                          snapshot->top->depth == 0u))
        return false;
    *iterator = (PrimeNeedHistoryIterator){
        .next = snapshot->top,
        .session_id = snapshot->session_id,
        .remaining = snapshot->top ? snapshot->top->depth : 0u,
    };
    return true;
}

PrimeNeedHistoryStep prime_need_snapshot_history_next(
    PrimeNeedHistoryIterator *iterator, PrimeNeedHistoryView *view) {
    if (!iterator)
        return PRIME_NEED_HISTORY_INVALID;
    if (!view || iterator->invalid || iterator->session_id == 0u) {
        iterator->invalid = true;
        return PRIME_NEED_HISTORY_INVALID;
    }
    const PrimeNeedFrame *frame = iterator->next;
    if (!frame && iterator->remaining == 0u)
        return PRIME_NEED_HISTORY_DONE;
    if (!frame || iterator->remaining == 0u ||
        frame->session_id != iterator->session_id ||
        frame->depth != iterator->remaining || frame->serial == 0u || frame->update_id == 0u ||
        frame->thunk_id == 0u || frame->authority_id == 0u || !frame->origin ||
        ((frame->parent == NULL) != (iterator->remaining == 1u))) {
        iterator->invalid = true;
        return PRIME_NEED_HISTORY_INVALID;
    }
    PrimeNeedHistoryView observed = {
        .session_id = frame->session_id,
        .serial = frame->serial,
        .thunk_id = frame->thunk_id,
        .depth = frame->depth,
    };
    prime_need_cell_view_from_frame(frame, &observed.cell);
    *view = observed;
    iterator->next = frame->parent;
    iterator->remaining--;
    return PRIME_NEED_HISTORY_ENTRY;
}

typedef enum {
    PRIME_NEED_READ_CELL,
    PRIME_NEED_READ_STORAGE,
    PRIME_NEED_READ_IMPORT,
} PrimeNeedReadKind;

typedef struct {
    PrimeNeedReadKind kind;
    uint64_t key;
    Atom *origin;
    uint64_t thunk_id;
    uint64_t update_id;
} PrimeNeedReadDependency;

struct PrimeNeedReadCapture {
    PrimeNeedSnapshot context;
    PrimeNeedSnapshot entrance;
    PrimeNeedReadDependency *reads;
    size_t length;
    size_t capacity;
    bool complete;
};

static _Thread_local PrimeNeedReadScope *g_prime_need_read_scope;
static _Thread_local bool g_prime_need_read_observing;
static void prime_need_capture_observe(const PrimeNeedSnapshot *,
    PrimeNeedReadKind, uint64_t, Atom *);

static inline __attribute__((always_inline)) bool prime_need_snapshot_lookup_frame_raw(
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id,
    PrimeNeedCellView *out, const PrimeNeedFrame **frame_out) {
    cetta_native_cost_charge(CETTA_COST_HEAP_LOOKUP, 1u);
    if (!snapshot || !out || !prime_need_snapshot_present(snapshot) ||
        thunk_id == 0u)
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_QUERY);
#if CETTA_PRIME_NEED_HEAP_INDEX
    if (prime_need_heap_index_enabled() && snapshot->heap_index) {
        size_t steps = 0u;
        const PrimeNeedFrame *frame = prime_need_heap_index_lookup(
            snapshot->heap_index, thunk_id, &steps);
        cetta_native_cost_charge(CETTA_COST_HEAP_INDEX_VISIT, steps);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_INDEX_STEP,
            (uint64_t)steps);
        if (!frame) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_INDEX_MISS);
            return false;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_INDEX_HIT);
        prime_need_cell_view_from_frame(frame, out);
        if (frame_out)
            *frame_out = frame;
        return true;
    }
#endif
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_LOG_FALLBACK);
    if ((thunk_id & PRIME_NEED_NAMED_CELL_BIT) != 0u) {
        for (const PrimeNeedFrame *frame =
                 snapshot->top ? snapshot->top->named_top : NULL;
             frame;
             frame = frame->parent ? frame->parent->named_top : NULL) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_LOG_FRAME);
            if (frame->session_id == snapshot->session_id &&
                frame->thunk_id == thunk_id) {
                prime_need_cell_view_from_frame(frame, out);
                if (frame_out)
                    *frame_out = frame;
                return true;
            }
        }
        return false;
    }
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        cetta_native_cost_charge(CETTA_COST_HEAP_INDEX_VISIT, 1u);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_LOG_FRAME);
        if (frame->session_id == snapshot->session_id &&
            frame->thunk_id == thunk_id) {
            prime_need_cell_view_from_frame(frame, out);
            if (frame_out)
                *frame_out = frame;
            return true;
        }
    }
    return false;
}

static inline __attribute__((always_inline)) bool prime_need_snapshot_lookup_frame(
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id,
    PrimeNeedCellView *out, const PrimeNeedFrame **frame_out) {
    bool found = prime_need_snapshot_lookup_frame_raw(snapshot, thunk_id, out, frame_out);
    if (g_prime_need_read_scope && !g_prime_need_read_observing)
        prime_need_capture_observe(snapshot, PRIME_NEED_READ_CELL, thunk_id, NULL);
    return found;
}

bool prime_need_snapshot_lookup(const PrimeNeedSnapshot *snapshot,
                                uint64_t thunk_id,
                                PrimeNeedCellView *out) {
    return prime_need_snapshot_lookup_frame(snapshot, thunk_id, out, NULL);
}

bool prime_need_snapshot_suffix_is_private(const PrimeNeedSnapshot *base,
                                            const PrimeNeedSnapshot *snapshot) {
    if (!prime_need_snapshot_present(base) ||
        !prime_need_snapshot_present(snapshot) ||
        base->session_id != snapshot->session_id ||
        !prime_need_snapshot_is_ancestor(base, snapshot))
        return false;
    uint64_t floor = base->top ? base->top->depth : 0u;
    for (const PrimeNeedFrame *frame = snapshot->top;
         frame && frame->depth > floor; frame = frame->parent) {
        if ((frame->thunk_id & PRIME_NEED_NAMED_CELL_BIT) != 0u ||
            frame->import_key != 0u ||
            frame->storage_key <= base->max_storage_key)
            return false;
    }
    return true;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static bool prime_need_frame_completed(const PrimeNeedFrame *frame) {
    return frame && (frame->cache_state == PRIME_NEED_CACHE_VALUE ||
                     frame->cache_state == PRIME_NEED_CACHE_STABLE_FAULT);
}

bool prime_need_snapshot_cached_update_id(
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id, uint64_t *out) {
    if (!out)
        return false;
    PrimeNeedCellView cell;
    const PrimeNeedFrame *frame = NULL;
    if (!prime_need_snapshot_lookup_frame(snapshot, thunk_id, &cell, &frame) ||
        !prime_need_frame_completed(frame))
        return false;
    *out = frame->update_id;
    return true;
}
#endif

static bool prime_need_snapshot_find_storage_key_raw(
    const PrimeNeedSnapshot *snapshot, uint64_t storage_key,
    uint64_t *out_thunk_id) {
    if (!snapshot || storage_key == 0u ||
        !prime_need_snapshot_present(snapshot))
        return false;
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_STORAGE_KEY_SCAN_FRAME);
        if (frame->session_id == snapshot->session_id &&
            frame->storage_key == storage_key) {
            if (out_thunk_id)
                *out_thunk_id = frame->thunk_id;
            return true;
        }
    }
    return false;
}

static bool prime_need_snapshot_find_imported_cell_raw(
    const PrimeNeedSnapshot *snapshot, uint64_t import_key,
    Atom *origin, uint64_t *out_thunk_id) {
    if (!snapshot || import_key == 0u || !origin ||
        !prime_need_snapshot_present(snapshot))
        return false;
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        if (frame->session_id != snapshot->session_id ||
            !atom_eq(frame->origin, origin))
            continue;
        if (frame->storage_key == import_key ||
            frame->import_key == import_key) {
            if (out_thunk_id)
                *out_thunk_id = frame->thunk_id;
            return true;
        }
    }
    return false;
}

static bool prime_need_capture_query(const PrimeNeedSnapshot *snapshot,
    PrimeNeedReadKind kind, uint64_t key, Atom *origin,
    uint64_t *thunk_id, uint64_t *update_id) {
    *thunk_id = 0u;
    *update_id = 0u;
    if (!snapshot || !prime_need_snapshot_present(snapshot) || !key)
        return false;
    if (kind == PRIME_NEED_READ_CELL) {
        PrimeNeedCellView cell;
        if (prime_need_snapshot_lookup_frame_raw(snapshot, key, &cell, NULL)) {
            *thunk_id = key;
            *update_id = cell.update_id;
        }
    } else if (kind == PRIME_NEED_READ_STORAGE) {
        (void)prime_need_snapshot_find_storage_key_raw(snapshot, key, thunk_id);
    } else if (kind == PRIME_NEED_READ_IMPORT && origin) {
        (void)prime_need_snapshot_find_imported_cell_raw(snapshot, key, origin, thunk_id);
    } else return false;
    return true;
}

PrimeNeedReadCapture *prime_need_read_capture_new(const PrimeNeedSnapshot *context,
                                                const PrimeNeedSnapshot *entrance) {
    if (!context || !entrance || !prime_need_snapshot_present(context) ||
        !prime_need_snapshot_is_ancestor(context, entrance)) return NULL;
    PrimeNeedReadCapture *capture = calloc(1u, sizeof(*capture));
    if (!capture) return NULL;
    capture->context = *context;
    capture->entrance = *entrance;
    capture->complete = true;
    return capture;
}

void prime_need_read_capture_free(PrimeNeedReadCapture *capture) {
    if (!capture) return;
    for (PrimeNeedReadScope *scope = g_prime_need_read_scope; scope; scope = scope->previous)
        assert(scope->capture != capture);
    free(capture->reads);
    free(capture);
}

void prime_need_read_capture_storage(const PrimeNeedReadCapture *capture,CettaStorageCensus *census) {
    if(!capture || !cetta_storage_object(census,capture,sizeof(*capture),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,capture->reads,capture->capacity,sizeof(*capture->reads),0u);
    if(capture->length>capture->capacity) {cetta_storage_refuse(census,"NeedReadCaptureExtent");return;}
    prime_need_snapshot_storage_census(&capture->context,census);
    prime_need_snapshot_storage_census(&capture->entrance,census);
}

PrimeNeedReadCapture *prime_need_read_capture_copy(const PrimeNeedReadCapture *capture) {
    if (!capture) return NULL;
    PrimeNeedReadCapture *copy = prime_need_read_capture_new(&capture->context, &capture->entrance);
    if (!copy) return NULL;
    if (capture->length) {
        if (capture->length > SIZE_MAX / sizeof(*copy->reads)) {
            free(copy);
            return NULL;
        }
        copy->reads = malloc(capture->length * sizeof(*copy->reads));
        if (!copy->reads) {
            free(copy);
            return NULL;
        }
        memcpy(copy->reads, capture->reads, capture->length * sizeof(*copy->reads));
    }
    copy->length = copy->capacity = capture->length;
    copy->complete = capture->complete;
    return copy;
}

PrimeNeedReadCapture *prime_need_read_capture_copy_with_session(
    Arena *destination,AtomDeepCopySession *session,const PrimeNeedReadCapture *capture) {
    if (!capture || !atom_deep_copy_session_relocation_targets(session,destination) ||
        atom_deep_copy_session_failed(session) ||
        capture->length>capture->capacity ||
        capture->capacity>SIZE_MAX/sizeof(*capture->reads) ||
        (!!capture->reads != !!capture->capacity) ||
        !prime_need_snapshot_present(&capture->context) ||
        !prime_need_snapshot_is_ancestor(&capture->context,&capture->entrance)) return NULL;
    /* The read coordinates must describe the advertised entrance. Copying
     * malformed support must not turn it into permission for another world. */
    for(size_t i=0u;i<capture->length;i++) {
        const PrimeNeedReadDependency *read=&capture->reads[i];
        uint64_t thunk=0u,update=0u;
        if (!prime_need_capture_query(&capture->entrance,read->kind,read->key,
                read->origin,&thunk,&update) ||
            thunk!=read->thunk_id || update!=read->update_id) return NULL;
    }
    PrimeNeedReadCapture *copy=calloc(1u,sizeof(*copy));
    if (!copy) return NULL;
    copy->context=capture->context;copy->entrance=capture->entrance;
    copy->length=capture->length;copy->capacity=capture->capacity;
    copy->complete=capture->complete;
    if (copy->capacity) {
        copy->reads=malloc(copy->capacity*sizeof(*copy->reads));
        if (!copy->reads) goto fail;
        memcpy(copy->reads,capture->reads,copy->length*sizeof(*copy->reads));
    }
    if (!prime_need_snapshot_copy_with_session(destination,session,&copy->context) ||
        !prime_need_snapshot_copy_with_session(destination,session,&copy->entrance) ||
        !prime_need_snapshot_is_ancestor(&copy->context,&copy->entrance)) goto fail;
    for(size_t i=0u;i<copy->length;i++) {
        PrimeNeedReadDependency *read=&copy->reads[i];
        if (read->origin) {
            read->origin=atom_deep_copy_session_copy(session,read->origin);
            if (!read->origin) goto fail;
        }
        uint64_t thunk=0u,update=0u;
        if (!prime_need_capture_query(&copy->entrance,read->kind,read->key,
                read->origin,&thunk,&update) ||
            thunk!=read->thunk_id || update!=read->update_id) goto fail;
    }
    return copy;
fail:
    prime_need_read_capture_free(copy);
    return NULL;
}

bool prime_need_read_capture_complete(const PrimeNeedReadCapture *capture) {
    return capture && capture->complete;
}

bool prime_need_read_capture_has_cuts(const PrimeNeedReadCapture *capture,
    const PrimeNeedSnapshot *context, const PrimeNeedSnapshot *entrance) {
    return capture && context && entrance && capture->complete &&
        capture->context.top == context->top &&
        capture->context.session_id == context->session_id &&
        capture->context.max_storage_key == context->max_storage_key &&
        capture->entrance.top == entrance->top &&
        capture->entrance.session_id == entrance->session_id &&
        capture->entrance.max_storage_key == entrance->max_storage_key;
}

void prime_need_read_scope_enter(PrimeNeedReadScope *scope, PrimeNeedReadCapture *capture) {
    assert(scope != NULL);
    scope->capture = capture;
    scope->previous = g_prime_need_read_scope;
    scope->resident_boundary=false;
    g_prime_need_read_scope = scope;
}

void prime_need_read_scope_enter_resident(PrimeNeedReadScope *scope,PrimeNeedReadCapture *capture) {
    prime_need_read_scope_enter(scope,capture);scope->resident_boundary=true;
}

void prime_need_read_scope_leave(PrimeNeedReadScope *scope) {
    assert(scope && g_prime_need_read_scope == scope);
    g_prime_need_read_scope = scope->previous;
    scope->capture = NULL;
    scope->previous = NULL;
}

/* A source reads its own evolving cache while it runs. Record the entrance
 * observation for that query, not an impossible demand that EMPTY, EVALUATING
 * and VALUE all hold in the recipient at once. Its produced updates are
 * checked separately during import. Observation failure invalidates reuse
 * evidence; it never changes the result of the source's actual lookup. */
static void prime_need_capture_observe(const PrimeNeedSnapshot *snapshot,
    PrimeNeedReadKind kind, uint64_t key, Atom *origin) {
    bool previous = g_prime_need_read_observing;
    g_prime_need_read_observing = true;
    for (PrimeNeedReadScope *scope = g_prime_need_read_scope; scope;
         scope = scope->resident_boundary ? NULL : scope->previous) {
        PrimeNeedReadCapture *capture = scope->capture;
        if (!capture || !capture->complete) continue;
        if (!snapshot || !key || !prime_need_snapshot_is_ancestor(&capture->entrance, snapshot)) {
            capture->complete = false;
            continue;
        }
        bool present = false;
        for (size_t i = 0u; i < capture->length; i++) {
            const PrimeNeedReadDependency *read = &capture->reads[i];
            if (read->kind == kind && read->key == key &&
                (kind != PRIME_NEED_READ_IMPORT || atom_capture_identical(read->origin, origin))) {
                present = true;
                break;
            }
        }
        if (present) continue;
        PrimeNeedReadDependency read = {.kind = kind, .key = key, .origin = origin};
        if (!prime_need_capture_query(&capture->entrance, kind, key, origin,
                                      &read.thunk_id, &read.update_id)) {
            capture->complete = false;
            continue;
        }
        if (capture->length == capture->capacity) {
            size_t capacity = capture->capacity ? capture->capacity * 2u : 8u;
            if (capacity <= capture->capacity || capacity > SIZE_MAX / sizeof(*capture->reads)) {
                capture->complete = false;
                continue;
            }
            PrimeNeedReadDependency *reads = realloc(capture->reads, capacity * sizeof(*reads));
            if (!reads) {
                capture->complete = false;
                continue;
            }
            capture->reads = reads;
            capture->capacity = capacity;
        }
        capture->reads[capture->length++] = read;
    }
    g_prime_need_read_observing = previous;
}

static bool prime_need_snapshot_find_storage_key(const PrimeNeedSnapshot *snapshot,
    uint64_t storage_key, uint64_t *out_thunk_id) {
    bool found = prime_need_snapshot_find_storage_key_raw(snapshot, storage_key, out_thunk_id);
    if (g_prime_need_read_scope && !g_prime_need_read_observing)
        prime_need_capture_observe(snapshot, PRIME_NEED_READ_STORAGE, storage_key, NULL);
    return found;
}

bool prime_need_snapshot_find_imported_cell(const PrimeNeedSnapshot *snapshot,
    uint64_t import_key, Atom *origin, uint64_t *out_thunk_id) {
    bool found = prime_need_snapshot_find_imported_cell_raw(snapshot, import_key, origin, out_thunk_id);
    if (g_prime_need_read_scope && !g_prime_need_read_observing)
        prime_need_capture_observe(snapshot, PRIME_NEED_READ_IMPORT, import_key, origin);
    return found;
}

/* A recipient may already hold this exact earned update from another reader.
 * That is different from independently producing an equal cached payload. */
static bool prime_need_import_has_earned_update(const PrimeNeedSnapshot *production,
    const PrimeNeedSnapshot *recipient, uint64_t floor, uint64_t thunk_id) {
    PrimeNeedCellView earned, held;
    const PrimeNeedFrame *frame = NULL;
    return thunk_id &&
        prime_need_snapshot_lookup_frame_raw(production, thunk_id, &earned, &frame) &&
        frame->depth > floor &&
        prime_need_snapshot_lookup_frame_raw(recipient, thunk_id, &held, NULL) &&
        earned.update_id == held.update_id;
}

CettaMatchCompletion prime_need_snapshot_import_production_checked(Arena *owner,
    const PrimeNeedReadCapture *capture, const PrimeNeedSnapshot *production,
    uint64_t thunk_id, const PrimeNeedSnapshot *recipient, PrimeNeedSnapshot *out) {
    if (!prime_need_owner_admissible(owner) || !capture || !capture->complete ||
        !production || !recipient || !out || !thunk_id)
        return CETTA_MATCH_INCOMPLETE;
    if (
        !prime_need_snapshot_is_ancestor(&capture->entrance, production) ||
        !prime_need_snapshot_is_ancestor(&capture->context, recipient))
        return CETTA_MATCH_REFUTED;
    uint64_t floor = capture->entrance.top ? capture->entrance.top->depth : 0u;
    PrimeNeedCellView original, completed;
    const PrimeNeedFrame *completion = NULL;
    if (!prime_need_snapshot_lookup_frame_raw(&capture->entrance, thunk_id, &original, NULL) ||
        original.cache_state != PRIME_NEED_CACHE_EMPTY ||
        !prime_need_snapshot_lookup_frame_raw(production, thunk_id, &completed, &completion) ||
        completion->depth <= floor ||
        (completed.cache_state != PRIME_NEED_CACHE_VALUE &&
         completed.cache_state != PRIME_NEED_CACHE_STABLE_FAULT)) return CETTA_MATCH_INCOMPLETE;

    for (size_t i = 0u; i < capture->length; i++) {
        const PrimeNeedReadDependency *read = &capture->reads[i];
        uint64_t actual_thunk, actual_update;
        if (!prime_need_capture_query(recipient, read->kind, read->key, read->origin,
                                     &actual_thunk, &actual_update)) return CETTA_MATCH_INCOMPLETE;
        if (actual_thunk == read->thunk_id && actual_update == read->update_id) continue;
        uint64_t earned_thunk, earned_update;
        if (!prime_need_capture_query(production, read->kind, read->key, read->origin,
                                     &earned_thunk, &earned_update)) return CETTA_MATCH_INCOMPLETE;
        if (actual_thunk != earned_thunk || actual_update != earned_update ||
            !prime_need_import_has_earned_update(production, recipient, floor, actual_thunk))
            return CETTA_MATCH_REFUTED;
    }

    uint64_t extent = production->top->depth - floor;
    if (extent > SIZE_MAX / sizeof(const PrimeNeedFrame *)) return CETTA_MATCH_INCOMPLETE;
    const PrimeNeedFrame **updates = extent ? malloc((size_t)extent * sizeof(*updates)) : NULL;
    if (extent && !updates) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedThunkSet seen = {0};
    size_t count = 0u;
    bool valid = true;
    CettaMatchCompletion refusal = CETTA_MATCH_REFUTED;
    for (const PrimeNeedFrame *frame = production->top;
         valid && frame && frame->depth > floor; frame = frame->parent) {
        int added = prime_need_thunk_set_insert(&seen, frame->thunk_id);
        if (added < 0) { valid = false; refusal = CETTA_MATCH_INCOMPLETE; break; }
        if (!added) continue;
        if (frame->cache_state == PRIME_NEED_CACHE_EVALUATING) {
            valid = false; refusal = CETTA_MATCH_INCOMPLETE; break;
        }
        PrimeNeedCellView held = {0}, entrance = {0};
        bool holds = prime_need_snapshot_lookup_frame_raw(recipient, frame->thunk_id, &held, NULL);
        if (holds && held.update_id == frame->update_id) continue;
        bool existed = prime_need_snapshot_lookup_frame_raw(
            &capture->entrance, frame->thunk_id, &entrance, NULL);
        if (holds != existed || (holds && held.update_id != entrance.update_id)) {
            valid = false;
            break;
        }
        /* Independent branches may each admit a requested persisted key
         * above their entrance watermark. Different thunk identities alone
         * do not make those physical keys distinct. Reuse cannot silently
         * renumber an earned payload's stored references. */
        uint64_t occupied = 0u;
        if (!existed && frame->storage_key &&
            prime_need_snapshot_find_storage_key_raw(recipient, frame->storage_key, &occupied) &&
            occupied != frame->thunk_id) {
            valid = false;
            break;
        }
        updates[count++] = frame;
    }
    free(seen.slots);
    if (!valid) { free(updates); return refusal; }

    PrimeNeedSnapshot imported = *recipient;
    for (size_t i = count; i > 0u; i--) {
        const PrimeNeedFrame *frame = updates[i - 1u];
        PrimeNeedSnapshot next;
        if (!prime_need_snapshot_push(owner, &imported, 0u, frame->update_id,
            frame->thunk_id, frame->authority_id, frame->evaluator_id,
            frame->storage_key, frame->import_key, frame->source_occurrence_id,
            frame->source_argument_index, frame->source_role,
            frame->cache_state, frame->value_role,
            frame->value_producer_thunk, frame->value_producer_update,
            frame->origin, frame->cached,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
            frame->capture_known, frame->capture_var_ids, frame->capture_var_count,
#endif
            &next)) { free(updates); return CETTA_MATCH_INCOMPLETE; }
        imported = next;
    }
    free(updates);
    if (production->max_storage_key > imported.max_storage_key)
        imported.max_storage_key = production->max_storage_key;
    /* A nested production inherits the external support of reused rows. */
    if (g_prime_need_read_scope && !g_prime_need_read_observing)
        for (size_t i = 0u; i < capture->length; i++) {
            PrimeNeedReadDependency read = capture->reads[i];
            prime_need_capture_observe(recipient, read.kind, read.key, read.origin);
        }
    *out = imported;
    return CETTA_MATCH_MATCHED;
}

bool prime_need_snapshot_import_production(Arena *owner,
    const PrimeNeedReadCapture *capture, const PrimeNeedSnapshot *production,
    uint64_t thunk_id, const PrimeNeedSnapshot *recipient, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_import_production_checked(owner, capture, production,
        thunk_id, recipient, out) == CETTA_MATCH_MATCHED;
}

static bool prime_need_snapshot_allocate_with_storage_key(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key, uint64_t source_occurrence_id,
    uint64_t source_argument_index, bool ready,
    PrimeNeedSourceRole source_role,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    bool capture_known, const VarId *capture_var_ids,
    size_t capture_var_count,
#endif
    PrimeNeedSnapshot *out,
    uint64_t *out_thunk_id) {
    if (!owner || !base || !out || !out_thunk_id || !term ||
        !prime_need_snapshot_present(base))
        return false;
    uint64_t import_key = storage_key;
    if (import_key != 0u)
        prime_need_reserve_storage_keys_through(import_key);
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    const VarId *owned_capture_ids = NULL;
    size_t owned_capture_count = 0u;
    if (capture_known &&
        !prime_need_capture_copy(
            owner, capture_var_ids, capture_var_count,
            &owned_capture_ids, &owned_capture_count))
        return false;
#endif
    if (import_key != 0u &&
        prime_need_snapshot_find_imported_cell(
            base, import_key, term, out_thunk_id)) {
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        if (capture_known) {
            PrimeNeedCellView existing;
            if (!prime_need_snapshot_lookup(
                    base, *out_thunk_id, &existing))
                return false;
            if (existing.capture_known &&
                !prime_need_capture_equal(
                    &existing, owned_capture_ids,
                    owned_capture_count))
                return false;
        }
#endif
        *out = *base;
        return true;
    }
    bool storage_key_in_use =
        storage_key != 0u &&
        storage_key <= base->max_storage_key &&
        prime_need_snapshot_find_storage_key(base, storage_key, NULL);
    /* Each identity is drawn only once the one before it was issued.  A
     * refusal returns before anything reachable is built: `out`, the thunk
     * id and `base` are untouched, and identities already drawn are spent
     * rather than issued again. */
    if (storage_key == 0u || storage_key_in_use) {
        do {
            storage_key = prime_need_fresh_nonzero(
                &g_prime_need_next_storage_key);
            if (storage_key == 0u)
                return false;
        } while (storage_key <= base->max_storage_key &&
                 prime_need_snapshot_find_storage_key(
                     base, storage_key, NULL));
    }
    uint64_t thunk_id = prime_need_fresh_thunk_id();
    if (thunk_id == 0u)
        return false;
    uint64_t authority_id = prime_need_fresh_nonzero(
        &g_prime_need_next_authority);
    if (authority_id == 0u)
        return false;
    if (!prime_need_snapshot_push(
            owner, base, 0u, 0u, thunk_id, authority_id, 0u, storage_key,
            import_key, source_occurrence_id, source_argument_index, source_role,
            ready ? PRIME_NEED_CACHE_VALUE : PRIME_NEED_CACHE_EMPTY,
            ready ? PRIME_NEED_VALUE_READY : PRIME_NEED_VALUE_DEMAND_RESULT,
            0u, 0u, term, ready ? term : NULL,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
            capture_known, owned_capture_ids, owned_capture_count,
#endif
            out))
        return false;
    *out_thunk_id = thunk_id;
    return true;
}

bool prime_need_snapshot_allocate(Arena *owner,
                                  const PrimeNeedSnapshot *base,
                                  Atom *term,
                                  PrimeNeedSnapshot *out,
                                  uint64_t *out_thunk_id) {
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, 0u, 0u, false, PRIME_NEED_SOURCE_INFERRED,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        false, NULL, 0u,
#endif
        out, out_thunk_id);
}

uint64_t prime_need_fresh_source_occurrence(void) {
    return prime_need_fresh_nonzero(&g_prime_need_next_source_occurrence);
}

uint64_t prime_need_fresh_evaluator_id(void) {
    return prime_need_fresh_nonzero(&g_prime_need_next_evaluator);
}

uint64_t prime_need_fresh_resample_scope(void) {
    return prime_need_fresh_nonzero(&g_prime_need_next_resample_scope);
}

bool prime_need_snapshot_allocate_source_argument(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    PrimeNeedSourceRole source_role,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    if (source_occurrence_id == 0u)
        return false;
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, source_occurrence_id,
        source_argument_index, false, source_role,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        false, NULL, 0u,
#endif
        out, out_thunk_id);
}

bool prime_need_snapshot_allocate_persisted(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key, PrimeNeedSnapshot *out,
    uint64_t *out_thunk_id) {
    if (storage_key == 0u)
        return false;
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, storage_key, 0u, 0u, false, PRIME_NEED_SOURCE_INFERRED,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        false, NULL, 0u,
#endif
        out, out_thunk_id);
}

bool prime_need_snapshot_allocate_named(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t thunk_id, PrimeNeedSnapshot *out) {
    if (!owner || !base || !out || !term ||
        (thunk_id & PRIME_NEED_NAMED_CELL_BIT) == 0u ||
        !prime_need_snapshot_present(base))
        return false;
    PrimeNeedCellView existing;
    if (prime_need_snapshot_lookup(base, thunk_id, &existing))
        return false;
    uint64_t authority_id = prime_need_fresh_nonzero(
        &g_prime_need_next_authority);
    if (authority_id == 0u)
        return false;
    return prime_need_snapshot_push(
        owner, base, 0u, 0u, thunk_id, authority_id, 0u, 0u, 0u, 0u, 0u,
        PRIME_NEED_SOURCE_INFERRED,
        PRIME_NEED_CACHE_EMPTY, PRIME_NEED_VALUE_DEMAND_RESULT, 0u, 0u, term, NULL,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        /* The term is closed: it captures no variable. */
        true, NULL, 0u,
#endif
        out);
}

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
bool prime_need_snapshot_allocate_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, 0u, 0u, false, PRIME_NEED_SOURCE_INFERRED, true,
        capture_var_ids, capture_var_count, out, out_thunk_id);
}

bool prime_need_snapshot_allocate_source_argument_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    PrimeNeedSourceRole source_role,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    if (source_occurrence_id == 0u)
        return false;
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, source_occurrence_id,
        source_argument_index, false, source_role, true, capture_var_ids,
        capture_var_count, out, out_thunk_id);
}

bool prime_need_snapshot_allocate_persisted_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    if (storage_key == 0u)
        return false;
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, storage_key, 0u, 0u, false, PRIME_NEED_SOURCE_INFERRED, true,
        capture_var_ids, capture_var_count, out, out_thunk_id);
}
bool prime_need_snapshot_allocate_ready_value_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *value,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, value, 0u, 0u, 0u, true, PRIME_NEED_SOURCE_INFERRED, true,
        capture_var_ids, capture_var_count, out, out_thunk_id);
}
#endif

bool prime_need_snapshot_allocate_ready_value(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *value,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, value, 0u, 0u, 0u, true, PRIME_NEED_SOURCE_INFERRED,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        false, NULL, 0u,
#endif
        out, out_thunk_id);
}

static bool prime_need_snapshot_transition(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    PrimeNeedCacheState expected, PrimeNeedCacheState next,
    uint64_t evaluator_id, Atom *cached, PrimeNeedValueRole value_role,
    uint64_t value_producer_thunk, uint64_t value_producer_update,
    PrimeNeedSnapshot *out) {
    PrimeNeedCellView previous;
    if (!owner || !base || !out ||
        !prime_need_snapshot_lookup(base, thunk_id, &previous))
        return false;
    if (previous.cache_state != expected || evaluator_id == 0u)
        return false;
    if (expected == PRIME_NEED_CACHE_EVALUATING &&
        previous.evaluator_id != evaluator_id)
        return false;
    if (next == PRIME_NEED_CACHE_EVALUATING) {
        if (expected != PRIME_NEED_CACHE_EMPTY || cached)
            return false;
    } else if (next == PRIME_NEED_CACHE_VALUE ||
               next == PRIME_NEED_CACHE_STABLE_FAULT) {
        if (expected != PRIME_NEED_CACHE_EVALUATING || !cached)
            return false;
    } else if (next != PRIME_NEED_CACHE_EMPTY || cached ||
               expected != PRIME_NEED_CACHE_EVALUATING) {
        return false;
    }
    return prime_need_snapshot_push(
        owner, base, 0u, 0u, thunk_id, previous.authority_id,
        next == PRIME_NEED_CACHE_EVALUATING ? evaluator_id : 0u,
        previous.storage_key, previous.import_key,
        previous.source_occurrence_id, previous.source_argument_index,
        previous.source_role,
        next, value_role, value_producer_thunk, value_producer_update,
        previous.origin, cached,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        previous.capture_known, previous.capture_var_ids,
        previous.capture_var_count,
#endif
        out);
}

bool prime_need_snapshot_start_evaluation(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EMPTY,
        PRIME_NEED_CACHE_EVALUATING, evaluator_id, NULL,
        PRIME_NEED_VALUE_DEMAND_RESULT, 0u, 0u, out);
}

bool prime_need_snapshot_resolve_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_VALUE, evaluator_id, value,
        PRIME_NEED_VALUE_DEMAND_RESULT, 0u, 0u, out);
}

bool prime_need_snapshot_resolve_forwarded_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, uint64_t producer_thunk_id,
    PrimeNeedSnapshot *out) {
    PrimeNeedCellView producer;
    const PrimeNeedFrame *update = NULL;
    if (!producer_thunk_id || producer_thunk_id == thunk_id ||
        !prime_need_snapshot_lookup_frame(
            base, producer_thunk_id, &producer, &update) ||
        producer.cache_state != PRIME_NEED_CACHE_VALUE ||
        (producer.value_role != PRIME_NEED_VALUE_DEMAND_RESULT &&
         producer.value_role != PRIME_NEED_VALUE_READY))
        return false;
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_VALUE, evaluator_id, value,
        producer.value_role, producer_thunk_id, update->update_id, out);
}

bool prime_need_snapshot_forwarded_value_source(
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id,
    PrimeNeedCellView *source) {
    PrimeNeedCellView consumer, producer;
    const PrimeNeedFrame *consumer_update = NULL, *producer_update = NULL;
    if (!source || !prime_need_snapshot_lookup_frame(
            snapshot, thunk_id, &consumer, &consumer_update) ||
        consumer.cache_state != PRIME_NEED_CACHE_VALUE ||
        !consumer.value_producer_thunk ||
        !prime_need_snapshot_lookup_frame(snapshot, consumer.value_producer_thunk,
            &producer, &producer_update) ||
        producer.cache_state != PRIME_NEED_CACHE_VALUE ||
        producer_update->update_id != consumer.value_producer_update ||
        producer_update->update_id >= consumer_update->update_id)
        return false;
    *source = producer;
    return true;
}

bool prime_need_snapshot_resolve_ready_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_VALUE, evaluator_id, value,
        PRIME_NEED_VALUE_READY, 0u, 0u, out);
}

bool prime_need_snapshot_resolve_stable_fault(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *fault, PrimeNeedSnapshot *out) {
#ifdef CETTA_PRIME_NEED_MUTATION_FAULT_NOT_CACHED
    (void)fault;
    return prime_need_snapshot_retry_evaluation(
        owner, base, thunk_id, evaluator_id, out);
#else
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_STABLE_FAULT, evaluator_id, fault,
        PRIME_NEED_VALUE_DEMAND_RESULT, 0u, 0u, out);
#endif
}

bool prime_need_snapshot_retry_evaluation(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_EMPTY, evaluator_id, NULL,
        PRIME_NEED_VALUE_DEMAND_RESULT, 0u, 0u, out);
}

bool prime_need_snapshot_promote(Arena *dst,
                                 PrimeNeedSnapshot *snapshot) {
    if (!dst || !snapshot || !snapshot->top)
        return dst && snapshot;
    /* Snapshot paths are single-owner by construction.  Publishing back to
     * that same arena is already lifetime-safe and must preserve the O(1)
     * persistent handle rather than copying the complete history. */
    if (snapshot->closure_owner == dst)
        return true;
    AtomDeepCopySession *session = atom_deep_copy_session_new(dst);
    if (!session)
        return false;
    bool ok = prime_need_snapshot_promote_with_session(dst, session, snapshot);
    atom_deep_copy_session_free(session);
    return ok;
}

bool prime_need_snapshot_atoms_satisfy(const PrimeNeedSnapshot *snapshot,
        AtomTreePredicate predicate, void *context) {
    if (!snapshot || !predicate) return false;
    uint64_t remaining = snapshot->top ? snapshot->top->depth : 0u;
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        if (!remaining || frame->depth != remaining ||
            frame->session_id != snapshot->session_id || !frame->origin ||
            !predicate(frame->origin, context) ||
            (frame->cached && !predicate(frame->cached, context))) return false;
        remaining--;
    }
    return remaining == 0u;
}

bool prime_need_snapshot_promote_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedSnapshot *snapshot) {
    if (!snapshot || !atom_deep_copy_session_targets(copy_session, dst))
        return false;
    if (!snapshot->top || snapshot->closure_owner == dst)
        return true;
    uint64_t depth = snapshot->top->depth;
    if (depth > SIZE_MAX / sizeof(const PrimeNeedFrame *))
        return false;
    const PrimeNeedFrame **path = malloc(sizeof(*path) * (size_t)depth);
    if (!path)
        return false;
    const PrimeNeedFrame *cursor = snapshot->top;
    for (uint64_t i = depth; i > 0u; i--) {
        if (!cursor) {
            free(path);
            return false;
        }
        path[i - 1u] = cursor;
        cursor = cursor->parent;
    }
    PrimeNeedSnapshot promoted = {
        .top = NULL,
        .session_id = snapshot->session_id,
        /* A dead suffix may have been elided without releasing its key
         * reservation. Rebuilding live frames cannot lower that watermark. */
        .max_storage_key = snapshot->max_storage_key,
        .owner = NULL,
        .closure_owner = NULL,
#if CETTA_PRIME_NEED_HEAP_INDEX
        .heap_index = NULL,
        .lineage_index = NULL,
#endif
    };
    for (uint64_t i = 0u; i < depth; i++) {
        Atom *origin = atom_deep_copy_session_copy(copy_session, path[i]->origin);
        Atom *cached = path[i]->cached
            ? atom_deep_copy_session_copy(copy_session, path[i]->cached) : NULL;
        if (!origin || (path[i]->cached && !cached)) {
            free(path);
            return false;
        }
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        const VarId *capture_var_ids = NULL;
        size_t capture_var_count = 0u;
        if (path[i]->capture_known &&
            !prime_need_capture_copy(
                dst, path[i]->capture_var_ids,
                path[i]->capture_var_count,
                &capture_var_ids, &capture_var_count)) {
            free(path);
            return false;
        }
#endif
        PrimeNeedSnapshot next;
        if (!prime_need_snapshot_push(
                dst, &promoted, path[i]->serial, path[i]->update_id, path[i]->thunk_id,
                path[i]->authority_id, path[i]->evaluator_id,
                path[i]->storage_key, path[i]->import_key,
                path[i]->source_occurrence_id,
                path[i]->source_argument_index, path[i]->source_role,
                path[i]->cache_state, path[i]->value_role,
                path[i]->value_producer_thunk, path[i]->value_producer_update,
                origin, cached,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
                path[i]->capture_known, capture_var_ids,
                capture_var_count,
#endif
                &next)) {
            free(path);
            return false;
        }
        promoted = next;
    }
    free(path);
    *snapshot = promoted;
    return true;
}

/* These tokens classify physical nodes in the enclosing Atom copy session.
 * Allocation records close index-to-frame backedges; completed records alone
 * permit another carrier to publish a previously prepared frame path. */
static const char prime_need_frame_copy_kind;
static const char prime_need_frame_copy_complete_kind;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
static const char prime_need_capture_copy_kind;
#endif
#if CETTA_PRIME_NEED_HEAP_INDEX
static const char prime_need_index_copy_kind;

static bool prime_need_index_copy(AtomDeepCopySession *session,
    const PrimeNeedHeapIndexNode *source, const Arena *fallback,
    unsigned level, const PrimeNeedHeapIndexNode **out) {
    if (!source) { *out = NULL; return true; }
    const Arena *owner = NULL;
    Arena *image_owner = NULL;
    if (level > PRIME_NEED_HEAP_INDEX_LEVELS ||
        !atom_deep_copy_session_owner_of_span(session, source, sizeof(*source),
            fallback, &owner, &image_owner) || owner == image_owner ||
        source->child_count != prime_need_popcount16(source->child_mask) ||
        (level == PRIME_NEED_HEAP_INDEX_LEVELS && source->child_count))
        return false;
    size_t bytes = sizeof(*source) +
        (size_t)source->child_count * sizeof(*source->children);
    if (!atom_deep_copy_session_owner_of_span(session, source, bytes,
            owner, &owner, &image_owner)) return false;
    PrimeNeedHeapIndexNode *image = atom_deep_copy_session_object_forwarded(
        session, &prime_need_index_copy_kind, source, bytes);
    if (image) {
        if (!arena_owns_ptr(image_owner, image)) return false;
        *out = image;
        return true;
    }
    const PrimeNeedFrame *value = NULL;
    if (source->value) {
        value = atom_deep_copy_session_object_forwarded(session,
            &prime_need_frame_copy_kind, source->value, sizeof(*source->value));
        if (!value) return false;
    }
    image = prime_need_heap_index_node_alloc(image_owner, source->child_count);
    if (!image) return false;
    image->value = value;
    image->child_mask = source->child_mask;
    for (size_t i = 0u; i < source->child_count; i++) {
        if (!source->children[i] || !prime_need_index_copy(session,
                source->children[i], owner, level + 1u, &image->children[i]))
            return false;
    }
    if (!atom_deep_copy_session_object_record(session,
            &prime_need_index_copy_kind, source, image, bytes)) return false;
    *out = image;
    return true;
}
#endif

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
static bool prime_need_capture_copy_owned(AtomDeepCopySession *session,
    const PrimeNeedFrame *source, const VarId **out) {
    if (!source->capture_var_ids) {
        if (source->capture_var_count) return false;
        *out = NULL;
        return true;
    }
    if (!source->capture_var_count ||
        source->capture_var_count > SIZE_MAX / sizeof(VarId)) return false;
    size_t bytes = source->capture_var_count * sizeof(VarId);
    const Arena *owner = NULL;
    Arena *image_owner = NULL;
    if (!atom_deep_copy_session_owner_of_span(session, source->capture_var_ids,
            bytes, source->owner, &owner, &image_owner) || owner == image_owner)
        return false;
    VarId *image = atom_deep_copy_session_object_forwarded(session,
        &prime_need_capture_copy_kind, source->capture_var_ids, bytes);
    if (!image) {
        image = arena_alloc(image_owner, bytes);
        if (!image) return false;
        memcpy(image, source->capture_var_ids, bytes);
        if (!atom_deep_copy_session_object_record(session,
                &prime_need_capture_copy_kind, source->capture_var_ids,
                image, bytes)) return false;
    } else if (!arena_owns_ptr(image_owner, image)) {
        return false;
    }
    *out = image;
    return true;
}
#endif

bool prime_need_snapshot_copy_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedSnapshot *snapshot) {
    if (!snapshot || !atom_deep_copy_session_targets(session, dst)) return false;
    PrimeNeedSnapshot image = *snapshot;
    if ((snapshot->owner && !atom_deep_copy_session_destination_for_owner(
            session, snapshot->owner, &image.owner)) ||
        (snapshot->closure_owner && !atom_deep_copy_session_destination_for_owner(
            session, snapshot->closure_owner, &image.closure_owner))) return false;
    if (!snapshot->top) {
#if CETTA_PRIME_NEED_HEAP_INDEX
        if (snapshot->heap_index || snapshot->lineage_index) return false;
#endif
        *snapshot = image;
        return true;
    }
    const Arena *source_owner = NULL;
    Arena *image_owner = NULL;
    if (!snapshot->session_id || !atom_deep_copy_session_owner_of_span(session,
            snapshot->top, sizeof(*snapshot->top), snapshot->owner,
            &source_owner, &image_owner) || source_owner != snapshot->owner ||
        source_owner == image_owner || !snapshot->top->depth ||
        snapshot->top->depth > SIZE_MAX / sizeof(const PrimeNeedFrame *))
        return false;
    size_t depth = (size_t)snapshot->top->depth;
    const PrimeNeedFrame **path = malloc(depth * sizeof(*path));
    if (!path) return false;
    size_t count = 0u;
    const PrimeNeedFrame *cursor = snapshot->top;
    PrimeNeedFrame *parent = NULL;
    bool ok = false;
    while (cursor) {
        if (count >= depth || !atom_deep_copy_session_owner_of_span(session,
                cursor, sizeof(*cursor), cursor->owner,
                &source_owner, &image_owner) || cursor->owner != source_owner ||
            source_owner == image_owner || cursor->depth != depth - count ||
            cursor->session_id != snapshot->session_id) goto done;
        parent = atom_deep_copy_session_object_forwarded(session,
            &prime_need_frame_copy_complete_kind, cursor, sizeof(*cursor));
        if (parent) {
            if (parent->owner != image_owner || !arena_owns_ptr(image_owner, parent))
                goto done;
            break;
        }
        /* A previous refused preparation must not expose an unfinished shell. */
        if (atom_deep_copy_session_object_forwarded(session,
                &prime_need_frame_copy_kind, cursor, sizeof(*cursor))) goto done;
        path[count++] = cursor;
        cursor = cursor->parent;
    }
    if (!cursor && count != depth) goto done;
    for (size_t i = count; i > 0u; i--) {
        const PrimeNeedFrame *source = path[i - 1u];
        if (!atom_deep_copy_session_destination_for_owner(session,
                source->owner, &image_owner) || image_owner == source->owner)
            goto done;
        Atom *origin = atom_deep_copy_session_copy(session, source->origin);
        Atom *cached = source->cached
            ? atom_deep_copy_session_copy(session, source->cached) : NULL;
        if (!origin || (source->cached && !cached)) goto done;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        const VarId *capture = NULL;
        if (!prime_need_capture_copy_owned(session, source, &capture)) goto done;
#endif
        PrimeNeedFrame *frame = arena_alloc(image_owner, sizeof(*frame));
        if (!frame) goto done;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_FRAME_ALLOC);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_FRAME_BYTES,
            (uint64_t)sizeof(*frame));
        *frame = *source;
        frame->parent = parent;
        frame->lineage_skip = prime_need_frame_at_depth(
            parent, frame->depth & (frame->depth - 1u), NULL);
        frame->named_top = NULL;
        frame->owner = image_owner;
        frame->origin = origin;
        frame->cached = cached;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        frame->capture_var_ids = capture;
#endif
#if CETTA_PRIME_NEED_HEAP_INDEX
        frame->heap_index = NULL;
        frame->lineage_index = NULL;
#endif
        prime_need_frame_summarize(frame, image_owner, parent,
            parent ? parent->closure_owner : NULL, origin, cached
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
            , capture
#endif
            );
        if (!atom_deep_copy_session_object_record(session,
                &prime_need_frame_copy_kind, source, frame, sizeof(*frame))) goto done;
        parent = frame;
    }
    /* The named-cell chain is a stored backedge, transported through the
     * same frame relation before any completed image is published. */
    for (size_t i = count; i > 0u; i--) {
        const PrimeNeedFrame *source = path[i - 1u];
        PrimeNeedFrame *frame = atom_deep_copy_session_object_forwarded(session,
            &prime_need_frame_copy_kind, source, sizeof(*source));
        if (!frame) goto done;
        if (source->named_top) {
            frame->named_top = atom_deep_copy_session_object_forwarded(session,
                &prime_need_frame_copy_kind, source->named_top, sizeof(*source));
            if (!frame->named_top) goto done;
        }
    }
#if CETTA_PRIME_NEED_HEAP_INDEX
    /* All value backedges have images before the persistent indexes are copied.
     * Do not regenerate a missing index from current process configuration. */
    for (size_t i = count; i > 0u; i--) {
        const PrimeNeedFrame *source = path[i - 1u];
        PrimeNeedFrame *frame = atom_deep_copy_session_object_forwarded(session,
            &prime_need_frame_copy_kind, source, sizeof(*source));
        if (!frame || !prime_need_index_copy(session, source->heap_index,
                source->owner, 0u, &frame->heap_index) ||
            !prime_need_index_copy(session, source->lineage_index,
                source->owner, 0u, &frame->lineage_index)) goto done;
    }
    if (!prime_need_index_copy(session, snapshot->heap_index, snapshot->owner,
            0u, &image.heap_index) ||
        !prime_need_index_copy(session, snapshot->lineage_index, snapshot->owner,
            0u, &image.lineage_index)) goto done;
#endif
    for (size_t i = count; i > 0u; i--) {
        const PrimeNeedFrame *source = path[i - 1u];
        PrimeNeedFrame *frame = atom_deep_copy_session_object_forwarded(session,
            &prime_need_frame_copy_kind, source, sizeof(*source));
        if (!atom_deep_copy_session_object_record(session,
                &prime_need_frame_copy_complete_kind, source, frame, sizeof(*source)))
            goto done;
    }
    image.top = parent;
    image.closure_owner = parent->closure_owner;
    *snapshot = image;
    ok = true;
done:
    free(path);
    return ok;
}

bool prime_need_snapshot_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedSnapshot *snapshot) {
    if (!atom_deep_copy_session_targets(copy_session, dst) ||
        !forbidden || !snapshot ||
        dst == forbidden)
        return false;
    if (!prime_need_snapshot_present(snapshot) || !snapshot->top)
        return true;
    if (snapshot->closure_owner &&
        snapshot->closure_owner != forbidden)
        return true;
    if (snapshot->top->owner != forbidden)
        return prime_need_snapshot_excludes_arena(
            snapshot, forbidden);

    const PrimeNeedFrame *cursor = snapshot->top;
    size_t suffix_len = 0u;
    while (cursor && cursor->owner == forbidden) {
        if (suffix_len == SIZE_MAX)
            return false;
        suffix_len++;
        cursor = cursor->parent;
    }
    if (suffix_len == 0u)
        return false;
    /* A retained prefix may itself span several older regions.  Its
     * compositional summary normally proves exclusion immediately; a Bloom
     * collision or unclassified payload falls back to the exact walk. */
    if (cursor &&
        !prime_need_arena_range_excludes(
            cursor->arena_min_id, cursor->arena_max_id,
            cursor->arena_range_complete, forbidden) &&
        !prime_need_arena_bloom_excludes(
            cursor->arena_bloom,
            cursor->arena_bloom_complete,
            forbidden)) {
        PrimeNeedSnapshot prefix = {
            .top = cursor,
            .session_id = snapshot->session_id,
            .max_storage_key = snapshot->max_storage_key,
            .owner = cursor->owner,
            .closure_owner = cursor->closure_owner,
#if CETTA_PRIME_NEED_HEAP_INDEX
            .heap_index = cursor->heap_index,
            .lineage_index = cursor->lineage_index,
#endif
        };
        if (!prime_need_snapshot_excludes_arena(
                &prefix, forbidden))
            return false;
    }

    if (suffix_len > SIZE_MAX / sizeof(const PrimeNeedFrame *))
        return false;
    const PrimeNeedFrame **path = malloc(sizeof(*path) * suffix_len);
    if (!path)
        return false;
    const PrimeNeedFrame *item = snapshot->top;
    for (size_t i = suffix_len; i > 0u; i--) {
        path[i - 1u] = item;
        item = item->parent;
    }

    PrimeNeedSnapshot promoted = {
        .top = cursor,
        .session_id = snapshot->session_id,
        /* Keeping the full maximum while replaying the suffix preserves the
         * exact final bound and is a safe over-approximation for the private
         * temporary base handle below. */
        .max_storage_key = snapshot->max_storage_key,
        .owner = cursor ? cursor->owner : NULL,
        .closure_owner = cursor ? cursor->closure_owner : NULL,
#if CETTA_PRIME_NEED_HEAP_INDEX
        .heap_index = cursor ? cursor->heap_index : NULL,
        .lineage_index = cursor ? cursor->lineage_index : NULL,
#endif
    };
    bool ok = true;
    for (size_t i = 0u; ok && i < suffix_len; i++) {
        Atom *origin = atom_deep_copy_session_copy(
            copy_session, path[i]->origin);
        Atom *cached = path[i]->cached
            ? atom_deep_copy_session_copy(copy_session, path[i]->cached)
            : NULL;
        if (!origin || (path[i]->cached && !cached)) {
            ok = false;
            break;
        }
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        const VarId *capture_var_ids = NULL;
        size_t capture_var_count = 0u;
        if (path[i]->capture_known &&
            !prime_need_capture_copy(
                dst, path[i]->capture_var_ids,
                path[i]->capture_var_count,
                &capture_var_ids, &capture_var_count)) {
            ok = false;
            break;
        }
#endif
        PrimeNeedSnapshot next;
        if (!prime_need_snapshot_push(
                dst, &promoted, path[i]->serial, path[i]->update_id, path[i]->thunk_id,
                path[i]->authority_id, path[i]->evaluator_id,
                path[i]->storage_key, path[i]->import_key,
                path[i]->source_occurrence_id,
                path[i]->source_argument_index, path[i]->source_role,
                path[i]->cache_state, path[i]->value_role,
                path[i]->value_producer_thunk, path[i]->value_producer_update,
                origin, cached,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
                path[i]->capture_known, capture_var_ids,
                capture_var_count,
#endif
                &next)) {
            ok = false;
            break;
        }
        promoted = next;
    }
    free(path);
    if (!ok || !prime_need_snapshot_excludes_arena(
                   &promoted, forbidden)) {
        return false;
    }
    promoted.max_storage_key = snapshot->max_storage_key;
    *snapshot = promoted;
    return true;
}

bool prime_need_snapshot_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedSnapshot *snapshot) {
    if (!dst || !forbidden || !snapshot || dst == forbidden)
        return false;
    ArenaMark dst_mark = arena_mark(dst);
    AtomDeepCopySession *copy_session =
        atom_deep_copy_session_new(dst);
    if (!copy_session)
        return false;
    bool promoted = prime_need_snapshot_promote_suffix_with_session(
        dst, forbidden, copy_session, snapshot);
    atom_deep_copy_session_free(copy_session);
    if (!promoted)
        arena_reset(dst, dst_mark);
    return promoted;
}

bool prime_need_snapshot_excludes_arena(
    const PrimeNeedSnapshot *snapshot, const Arena *forbidden) {
    PrimeNeedArenaAudit *audit =
        prime_need_arena_audit_new(forbidden);
    if (!audit)
        return false;
    bool excludes = prime_need_arena_audit_snapshot(
        audit, snapshot);
    prime_need_arena_audit_free(audit);
    return excludes;
}

bool prime_need_snapshot_owner_excludes_arena(
    const PrimeNeedSnapshot *snapshot, const Arena *forbidden) {
    if (!snapshot || !forbidden)
        return false;
    if (!prime_need_snapshot_present(snapshot) || !snapshot->top)
        return true;
    return snapshot->closure_owner &&
           snapshot->closure_owner != forbidden;
}

Atom *prime_need_ref(Arena *arena, const PrimeNeedSnapshot *snapshot,
                     uint64_t thunk_id) {
    PrimeNeedCellView cell;
    if (!arena || !snapshot || !prime_need_snapshot_present(snapshot) ||
        !prime_need_snapshot_lookup(snapshot, thunk_id, &cell))
        return NULL;
    return atom_prime_need_capability(
        arena, snapshot->session_id, thunk_id, cell.authority_id);
}

bool prime_need_ref_parse_rights(const Atom *atom, uint64_t *session_id,
                                 uint64_t *thunk_id,
                                 uint64_t *authority_id,
                                 uint32_t *rights) {
    const CettaPrimeNeedCapability *capability =
        atom_prime_need_capability_value(atom);
    if (!capability || capability->session_id == 0u ||
        capability->thunk_id == 0u || capability->authority_id == 0u ||
        capability->rights == 0u ||
        (capability->rights & ~CETTA_PRIME_NEED_RIGHT_ALL) != 0u)
        return false;
    if (session_id)
        *session_id = capability->session_id;
    if (thunk_id)
        *thunk_id = capability->thunk_id;
    if (authority_id)
        *authority_id = capability->authority_id;
    if (rights)
        *rights = capability->rights;
    return true;
}

bool prime_need_ref_parse(const Atom *atom, uint64_t *session_id,
                          uint64_t *thunk_id, uint64_t *authority_id) {
    return prime_need_ref_parse_rights(atom, session_id, thunk_id,
                                       authority_id, NULL);
}

bool prime_need_ref_belongs_to(const Atom *atom,
                               const PrimeNeedSnapshot *snapshot,
                               uint64_t *thunk_id) {
    uint64_t session = 0u;
    uint64_t id = 0u;
    uint64_t authority = 0u;
    PrimeNeedCellView cell;
    if (!snapshot ||
        !prime_need_ref_parse(atom, &session, &id, &authority) ||
        session != snapshot->session_id)
        return false;
    if (!prime_need_snapshot_lookup(snapshot, id, &cell) ||
        cell.authority_id != authority)
        return false;
    if (thunk_id)
        *thunk_id = id;
    return true;
}

bool prime_need_ref_belongs_to_with_rights(
    const Atom *atom, const PrimeNeedSnapshot *snapshot,
    uint32_t required_rights, uint64_t *thunk_id) {
    uint64_t session = 0u;
    uint64_t id = 0u;
    uint64_t authority = 0u;
    uint32_t rights = 0u;
    PrimeNeedCellView cell;
    if (!snapshot || required_rights == 0u ||
        (required_rights & ~CETTA_PRIME_NEED_RIGHT_ALL) != 0u ||
        !prime_need_ref_parse_rights(atom, &session, &id, &authority,
                                     &rights) ||
        session != snapshot->session_id ||
        (rights & required_rights) != required_rights)
        return false;
    if (!prime_need_snapshot_lookup(snapshot, id, &cell) ||
        cell.authority_id != authority)
        return false;
    if (thunk_id)
        *thunk_id = id;
    return true;
}

Atom *prime_need_ref_restrict(Arena *arena, const Atom *atom,
                              const PrimeNeedSnapshot *snapshot,
                              uint32_t rights) {
    uint64_t session = 0u;
    uint64_t thunk = 0u;
    uint64_t authority = 0u;
    uint32_t existing = 0u;
    if (!arena || !snapshot || rights == 0u ||
        (rights & ~CETTA_PRIME_NEED_RIGHT_ALL) != 0u ||
        !prime_need_ref_parse_rights(atom, &session, &thunk, &authority,
                                     &existing) ||
        session != snapshot->session_id ||
        (rights & existing) != rights ||
        !prime_need_ref_belongs_to(atom, snapshot, NULL))
        return NULL;
    return atom_prime_need_capability_with_rights(
        arena, session, thunk, authority, rights);
}

typedef struct PrimeNeedReaderLineage {
    _Atomic size_t references;
    size_t depth;
    struct PrimeNeedReaderLineage *left, *right, *retiring;
    /* A suffix import adopts this cut, not the producer's whole context. */
    struct PrimeNeedReaderLineage *import_entrance, *import_production;
} PrimeNeedReaderLineage;

#define PRIME_NEED_POINTER_INVENTORY(Name, prefix, Element) \
typedef struct { \
    const Element **items; \
    size_t len; \
    size_t cap; \
    const Element **seen; \
    size_t seen_cap; \
} Name; \
 \
static void prefix##_free(Name *frames) { \
    if (!frames) \
        return; \
    free(frames->items); \
    free(frames->seen); \
    frames->items = NULL; \
    frames->len = 0u; \
    frames->cap = 0u; \
    frames->seen = NULL; \
    frames->seen_cap = 0u; \
} \
 \
 \
static bool prefix##_contains( \
    const Name *frames, \
    const Element *frame) { \
    if (!frames || !frame) \
        return false; \
    if (frames->seen && frames->seen_cap > 0u) { \
        size_t mask = frames->seen_cap - 1u; \
        size_t slot = prime_need_pointer_hash(frame) & mask; \
        while (frames->seen[slot]) { \
            if (frames->seen[slot] == frame) \
                return true; \
            slot = (slot + 1u) & mask; \
        } \
        return false; \
    } \
    for (size_t i = 0u; i < frames->len; i++) \
        if (frames->items[i] == frame) \
            return true; \
    return false; \
} \
 \
static bool prefix##_reserve_seen( \
    Name *frames, size_t count) { \
    if (!frames) \
        return false; \
    size_t needed = 16u; \
    while (needed / 2u < count) { \
        if (needed > SIZE_MAX / 2u) \
            return false; \
        needed *= 2u; \
    } \
    if (frames->seen_cap >= needed) \
        return true; \
    if (needed > SIZE_MAX / sizeof(*frames->seen)) \
        return false; \
    const Element **seen = calloc( \
        needed, sizeof(*seen)); \
    if (!seen) \
        return false; \
    size_t mask = needed - 1u; \
    for (size_t i = 0u; i < frames->len; i++) { \
        size_t slot = \
            prime_need_pointer_hash(frames->items[i]) & mask; \
        while (seen[slot]) \
            slot = (slot + 1u) & mask; \
        seen[slot] = frames->items[i]; \
    } \
    free(frames->seen); \
    frames->seen = seen; \
    frames->seen_cap = needed; \
    return true; \
} \
 \
static bool prefix##_push( \
    Name *frames, \
    const Element *frame) { \
    if (!frames || !frame) \
        return false; \
    if (!prefix##_reserve_seen( \
            frames, frames->len + 1u)) \
        return false; \
    if (prefix##_contains(frames, frame)) \
        return true; \
    if (frames->len == frames->cap) { \
        size_t next = frames->cap ? frames->cap * 2u : 16u; \
        if (next < frames->cap || \
            next > SIZE_MAX / sizeof(*frames->items)) \
            return false; \
        const Element **items = realloc( \
            frames->items, next * sizeof(*frames->items)); \
        if (!items) \
            return false; \
        frames->items = items; \
        frames->cap = next; \
    } \
    size_t mask = frames->seen_cap - 1u; \
    size_t slot = prime_need_pointer_hash(frame) & mask; \
    while (frames->seen[slot]) \
        slot = (slot + 1u) & mask; \
    frames->seen[slot] = frame; \
    frames->items[frames->len++] = frame; \
    return true; \
}

PRIME_NEED_POINTER_INVENTORY(PrimeNeedReceiptFrames, prime_need_receipt_frames, PrimeNeedReceiptFrame)
PRIME_NEED_POINTER_INVENTORY(PrimeNeedReaderLineages, prime_need_reader_lineages, PrimeNeedReaderLineage)
#undef PRIME_NEED_POINTER_INVENTORY


typedef struct {
    uint64_t key;
    PrimeNeedReaderValue reader;
} PrimeNeedReaderEntry;

struct PrimeNeedReaderContext {
    _Atomic size_t references;
    size_t length;
    size_t capacity;
    PrimeNeedReaderLineage *lineage;
    PrimeNeedReaderEntry entries[];
};

static void prime_need_reader_reference_retain(_Atomic size_t *references) {
    size_t count = atomic_load_explicit(references, memory_order_relaxed);
    for (;;) {
        if (!count || count == SIZE_MAX) abort();
        if (atomic_compare_exchange_weak_explicit(references, &count, count + 1u,
                memory_order_relaxed, memory_order_relaxed)) return;
    }
}

static bool prime_need_reader_reference_release(_Atomic size_t *references) {
    size_t count = atomic_load_explicit(references, memory_order_relaxed);
    for (;;) {
        if (!count) abort();
        if (atomic_compare_exchange_weak_explicit(references, &count, count - 1u,
                memory_order_acq_rel, memory_order_relaxed)) return count == 1u;
    }
}

static PrimeNeedReaderLineage *prime_need_reader_lineage_new(
    PrimeNeedReaderLineage *left, PrimeNeedReaderLineage *right) {
    size_t depth = left ? left->depth : 0u;
    if (right && right->depth > depth) depth = right->depth;
    if (depth == SIZE_MAX) return NULL;
    PrimeNeedReaderLineage *node = malloc(sizeof(*node));
    if (!node) return NULL;
    if (left == right) right = NULL;
    *node = (PrimeNeedReaderLineage){.references=1u, .depth=depth + 1u,
        .left=left, .right=right};
    if (left) prime_need_reader_reference_retain(&left->references);
    if (right) prime_need_reader_reference_retain(&right->references);
    return node;
}

/* Retirement uses only the nodes whose last reference was dropped. Version
 * ancestry holds no reader payload, and a deep lineage needs no C recursion. */
static void prime_need_reader_lineage_release(PrimeNeedReaderLineage *lineage) {
    if (!lineage || !prime_need_reader_reference_release(&lineage->references)) return;
    PrimeNeedReaderLineage *pending = lineage;
    while (pending) {
        PrimeNeedReaderLineage *node = pending;
        pending = node->retiring;
        PrimeNeedReaderLineage *parents[] = {node->left, node->right,
            node->import_entrance, node->import_production};
        for (size_t i=0u;i<4u;i++) if (parents[i] &&
            prime_need_reader_reference_release(&parents[i]->references)) {
            parents[i]->retiring = pending;
            pending = parents[i];
        }
        free(node);
    }
}

static CettaMatchCompletion prime_need_reader_lineage_ancestor(
    const PrimeNeedReaderLineage *ancestor, const PrimeNeedReaderLineage *descendant) {
    if (ancestor == descendant) return CETTA_MATCH_MATCHED;
    if (!ancestor || !descendant || ancestor->depth >= descendant->depth)
        return CETTA_MATCH_REFUTED;
    PrimeNeedReaderLineages nodes = {0};
    CettaMatchCompletion result = CETTA_MATCH_INCOMPLETE;
    if (!prime_need_reader_lineages_push(&nodes, descendant)) goto done;
    for (size_t i=0u;i<nodes.len;i++) {
        const PrimeNeedReaderLineage *node=nodes.items[i];
        const PrimeNeedReaderLineage *parents[]={node->left,node->right};
        for (size_t j=0u;j<2u;j++) {
            if (parents[j]==ancestor) {result=CETTA_MATCH_MATCHED;goto done;}
            if (parents[j] && parents[j]->depth>ancestor->depth &&
                !prime_need_reader_lineages_push(&nodes,parents[j])) goto done;
        }
    }
    result=CETTA_MATCH_REFUTED;
done:
    prime_need_reader_lineages_free(&nodes);
    return result;
}

static CettaMatchCompletion prime_need_reader_lineage_holds_import(
    const PrimeNeedReaderLineage *entrance, const PrimeNeedReaderLineage *production,
    const PrimeNeedReaderLineage *recipient) {
    PrimeNeedReaderLineages nodes={0};
    CettaMatchCompletion result=CETTA_MATCH_INCOMPLETE;
    if (!recipient) return CETTA_MATCH_REFUTED;
    if (!prime_need_reader_lineages_push(&nodes,recipient)) goto done;
    for (size_t i=0u;i<nodes.len;i++) {
        const PrimeNeedReaderLineage *node=nodes.items[i];
        if (node->import_entrance==entrance && node->import_production==production) {
            result=CETTA_MATCH_MATCHED;goto done;
        }
        if ((node->left && !prime_need_reader_lineages_push(&nodes,node->left)) ||
            (node->right && !prime_need_reader_lineages_push(&nodes,node->right))) goto done;
    }
    result=CETTA_MATCH_REFUTED;
done:
    prime_need_reader_lineages_free(&nodes);
    return result;
}

void prime_need_reader_context_retain(PrimeNeedReaderContext *context) {
    if (context) prime_need_reader_reference_retain(&context->references);
}

void prime_need_reader_context_release(PrimeNeedReaderContext *context) {
    if (!context || !prime_need_reader_reference_release(&context->references)) return;
    for (size_t i=0u;i<context->length;i++) {
        PrimeNeedReaderValue value=context->entries[i].reader;
        if (value.value) value.operations->release(value.value);
    }
    prime_need_reader_lineage_release(context->lineage);
    free(context);
}

bool prime_need_reader_context_lookup(const PrimeNeedReaderContext *context,
    uint64_t key, PrimeNeedReaderValue *out) {
    if (!context || !key || !out) return false;
    for (size_t i=0u;i<context->length;i++) {
        if (context->entries[i].key>key) break;
        if (context->entries[i].key==key && context->entries[i].reader.value) {
            *out=context->entries[i].reader;
            return true;
        }
    }
    return false;
}

CettaMatchCompletion prime_need_reader_context_visit(const PrimeNeedReaderContext *context,
    CettaMatchCompletion (*visit)(uint64_t,const PrimeNeedReaderValue *,void *),void *state) {
    if (!visit) return CETTA_MATCH_INCOMPLETE;
    for (size_t i=0u;context && i<context->length;i++) {
        const PrimeNeedReaderEntry *entry=&context->entries[i];
        if (!entry->reader.value) continue;
        CettaMatchCompletion result=visit(entry->key,&entry->reader,state);
        if (result!=CETTA_MATCH_MATCHED) return result;
    }
    return CETTA_MATCH_MATCHED;
}

static bool prime_need_reader_value_same(PrimeNeedReaderValue a, PrimeNeedReaderValue b) {
    return a.value==b.value && a.operations==b.operations;
}

static PrimeNeedReaderContext *prime_need_reader_context_new(size_t capacity,
    PrimeNeedReaderLineage *left, PrimeNeedReaderLineage *right) {
    if (capacity>(SIZE_MAX-sizeof(PrimeNeedReaderContext))/sizeof(PrimeNeedReaderEntry)) return NULL;
    PrimeNeedReaderContext *context=malloc(sizeof(*context)+capacity*sizeof(*context->entries));
    if (!context) return NULL;
    *context=(PrimeNeedReaderContext){.references=1u,.capacity=capacity};
    context->lineage=prime_need_reader_lineage_new(left,right);
    if (!context->lineage) {free(context);return NULL;}
    return context;
}

static bool prime_need_reader_context_append(PrimeNeedReaderContext *context,
    PrimeNeedReaderEntry entry) {
    if (entry.reader.value && !entry.reader.operations->retain(entry.reader.value)) return false;
    context->entries[context->length++]=entry;
    return true;
}

static void prime_need_reader_context_replace(PrimeNeedReaderContext **out,
    PrimeNeedReaderContext *owned) {
    PrimeNeedReaderContext *old=*out;
    *out=owned;
    prime_need_reader_context_release(old);
}

static CettaMatchCompletion prime_need_reader_context_share(const PrimeNeedReaderContext *source,
    PrimeNeedReaderContext **out) {
    prime_need_reader_context_retain((PrimeNeedReaderContext *)source);
    prime_need_reader_context_replace(out,(PrimeNeedReaderContext *)source);
    return CETTA_MATCH_MATCHED;
}

static const char prime_need_reader_context_copy_kind, prime_need_reader_lineage_copy_kind;

static void prime_need_reader_context_copy_hold(const void *raw) {
    prime_need_reader_context_retain((PrimeNeedReaderContext *)raw);
}
static void prime_need_reader_context_copy_drop(const void *raw) {
    prime_need_reader_context_release((PrimeNeedReaderContext *)raw);
}
static void prime_need_reader_lineage_copy_hold(const void *raw) {
    PrimeNeedReaderLineage *node=(PrimeNeedReaderLineage *)raw;
    prime_need_reader_reference_retain(&node->references);
}
static void prime_need_reader_lineage_copy_drop(const void *raw) {
    prime_need_reader_lineage_release((PrimeNeedReaderLineage *)raw);
}

/* A mapped image is already owned by this session. Re-copying a carrier
 * after an earlier transport must not create a second version of its image. */
static void *prime_need_reader_copy_image(const AtomDeepCopySession *session,
    const void *kind,const void *source,size_t bytes) {
    if (!kind || !source || !bytes) return NULL;
    void *image=atom_deep_copy_session_object_forwarded(session,kind,source,bytes);
    const void *original=atom_deep_copy_session_object_original(session,kind,source,bytes);
    if (image) {
        if (image==source || original ||
            atom_deep_copy_session_object_forwarded(session,kind,image,bytes)) return NULL;
        return image;
    }
    return original && original!=source ? (void *)source : NULL;
}

static bool prime_need_reader_copy_value(AtomDeepCopySession *session,
    PrimeNeedReaderValue source,PrimeNeedReaderValue *out) {
    if (!source.value) {
        if (source.operations) return false;
        *out=(PrimeNeedReaderValue){0};
        return true;
    }
    const PrimeNeedReaderValueOps *ops=source.operations;
    if (!ops || !ops->retain || !ops->release) return false;
    void *image=prime_need_reader_copy_image(session,ops->copy_kind,
        source.value,ops->copy_size);
    if (!image) return false;
    *out=(PrimeNeedReaderValue){.value=image,.operations=ops};
    return true;
}

static size_t prime_need_reader_lineage_copy_index(const PrimeNeedReaderLineages *nodes,
    const size_t *indices,const PrimeNeedReaderLineage *node) {
    size_t slot=prime_need_pointer_hash(node)&(nodes->seen_cap-1u);
    while (nodes->seen[slot] && nodes->seen[slot]!=node)
        slot=(slot+1u)&(nodes->seen_cap-1u);
    return nodes->seen[slot] ? indices[slot] : SIZE_MAX;
}

static PrimeNeedReaderLineage *prime_need_reader_lineage_copy(
    const PrimeNeedReaderLineage *source,AtomDeepCopySession *session) {
    if (!source) return NULL;
    PrimeNeedReaderLineage *known=prime_need_reader_copy_image(session,
        &prime_need_reader_lineage_copy_kind,source,sizeof(*source));
    if (known) return known;
    PrimeNeedReaderLineages nodes={0};
    size_t *indices=NULL,*heads=NULL,*children=NULL,*next=NULL,*queue=NULL;
    unsigned char *waiting=NULL;
    bool copied=false;
    if (!prime_need_reader_lineages_push(&nodes,source)) goto done;
    for (size_t i=0u;i<nodes.len;i++) {
        const PrimeNeedReaderLineage *node=nodes.items[i];
        if (!node->depth || node->retiring ||
            !atomic_load_explicit(&node->references,memory_order_relaxed)) goto done;
        if (prime_need_reader_copy_image(session,&prime_need_reader_lineage_copy_kind,
                node,sizeof(*node))) continue;
        const PrimeNeedReaderLineage *parents[]={node->left,node->right,
            node->import_entrance,node->import_production};
        for (size_t k=0u;k<4u;k++) {
            if (!parents[k]) continue;
            if ((k<2u && parents[k]->depth>=node->depth) ||
                !prime_need_reader_lineages_push(&nodes,parents[k])) goto done;
        }
    }
    if (nodes.len>SIZE_MAX/(4u*sizeof(size_t)) ||
        nodes.seen_cap>SIZE_MAX/sizeof(*indices)) goto done;
    indices=malloc(nodes.seen_cap*sizeof(*indices));
    heads=malloc(nodes.len*sizeof(*heads));
    children=malloc(4u*nodes.len*sizeof(*children));
    next=malloc(4u*nodes.len*sizeof(*next));
    queue=malloc(nodes.len*sizeof(*queue));
    waiting=calloc(nodes.len,sizeof(*waiting));
    if (!indices || !heads || !children || !next || !queue || !waiting) goto done;
    for (size_t i=0u;i<nodes.len;i++) {
        size_t slot=prime_need_pointer_hash(nodes.items[i])&(nodes.seen_cap-1u);
        while (nodes.seen[slot]!=nodes.items[i]) slot=(slot+1u)&(nodes.seen_cap-1u);
        indices[slot]=i;
        heads[i]=SIZE_MAX;
    }
    size_t edges=0u,queued=0u,processed=0u;
    for (size_t i=0u;i<nodes.len;i++) {
        const PrimeNeedReaderLineage *node=nodes.items[i];
        if (prime_need_reader_copy_image(session,&prime_need_reader_lineage_copy_kind,
                node,sizeof(*node))) continue;
        const PrimeNeedReaderLineage *parents[]={node->left,node->right,
            node->import_entrance,node->import_production};
        for (size_t k=0u;k<4u;k++) {
            if (!parents[k] || prime_need_reader_copy_image(session,
                    &prime_need_reader_lineage_copy_kind,parents[k],sizeof(*parents[k]))) continue;
            size_t parent=prime_need_reader_lineage_copy_index(&nodes,indices,parents[k]);
            if (parent==SIZE_MAX) goto done;
            waiting[i]++;
            children[edges]=i;next[edges]=heads[parent];heads[parent]=edges++;
        }
    }
    for (size_t i=0u;i<nodes.len;i++) if (!waiting[i]) queue[queued++]=i;
    while (processed<queued) {
        size_t index=queue[processed++];
        const PrimeNeedReaderLineage *node=nodes.items[index];
        if (prime_need_reader_copy_image(session,&prime_need_reader_lineage_copy_kind,
                node,sizeof(*node))) goto advance;
        PrimeNeedReaderLineage *image=calloc(1u,sizeof(*image));
        if (!image) goto done;
        atomic_init(&image->references,1u);
        image->depth=node->depth;
        const PrimeNeedReaderLineage *parents[]={node->left,node->right,
            node->import_entrance,node->import_production};
        PrimeNeedReaderLineage **targets[]={&image->left,&image->right,
            &image->import_entrance,&image->import_production};
        bool ready=true;
        for (size_t k=0u;ready && k<4u;k++) {
            if (!parents[k]) continue;
            PrimeNeedReaderLineage *parent=prime_need_reader_copy_image(session,
                &prime_need_reader_lineage_copy_kind,parents[k],sizeof(*parents[k]));
            if (!parent) {ready=false;break;}
            prime_need_reader_reference_retain(&parent->references);
            *targets[k]=parent;
        }
        if (ready) ready=atom_deep_copy_session_object_record_retained(session,
            &prime_need_reader_lineage_copy_kind,node,image,sizeof(*node),
            prime_need_reader_lineage_copy_hold,prime_need_reader_lineage_copy_drop);
        prime_need_reader_lineage_release(image);
        if (!ready) goto done;
advance:
        for (size_t edge=heads[index];edge!=SIZE_MAX;edge=next[edge]) {
            size_t child=children[edge];
            if (!waiting[child]) goto done;
            if (!--waiting[child]) queue[queued++]=child;
        }
    }
    copied=processed==nodes.len;
done:
    free(indices);free(heads);free(children);free(next);free(queue);free(waiting);
    prime_need_reader_lineages_free(&nodes);
    return copied ? prime_need_reader_copy_image(session,&prime_need_reader_lineage_copy_kind,
        source,sizeof(*source)) : NULL;
}

typedef struct {
    PrimeNeedReaderValue source, image;
} PrimeNeedReaderPreparedPair;
typedef struct {
    PrimeNeedReaderPreparedPair *entries;
    size_t length, capacity;
    bool failed;
} PrimeNeedReaderPreparation;
static const char prime_need_reader_preparation_kind;

static void prime_need_reader_preparation_release(void *raw) {
    PrimeNeedReaderPreparation *inventory=raw;
    for (size_t i=0u; i<inventory->length; i++) {
        PrimeNeedReaderPreparedPair pair=inventory->entries[i];
        if (pair.image.value && pair.image.operations && pair.image.operations->release)
            pair.image.operations->release(pair.image.value);
        pair.source.operations->release(pair.source.value);
    }
    free(inventory->entries);
    free(inventory);
}
static void prime_need_reader_preparation_storage(const void *raw, CettaStorageCensus *census) {
    const PrimeNeedReaderPreparation *inventory=raw;
    if (!cetta_storage_object(census,inventory,sizeof(*inventory),CETTA_STORAGE_BLOCK)) return;
    cetta_storage_array(census,inventory->entries,inventory->capacity,sizeof(*inventory->entries),0u);
    for (size_t i=0u;i<inventory->length;i++) {
        const PrimeNeedReaderValue values[]={inventory->entries[i].source,inventory->entries[i].image};
        for (size_t j=0u;j<2u;j++) if (values[j].value) {
            if (values[j].operations && values[j].operations->storage)
                values[j].operations->storage(values[j].value,census);
            else cetta_storage_refuse(census,"ReaderPreparedOwnerStorage");
        }
    }
}
static bool prime_need_reader_preparation_failed(const AtomDeepCopySession *session) {
    const PrimeNeedReaderPreparation *inventory=atom_deep_copy_session_inventory(
        session,&prime_need_reader_preparation_kind);
    return inventory && inventory->failed;
}

static CettaMatchCompletion prime_need_reader_preparation_refused(AtomDeepCopySession *session) {
    PrimeNeedReaderPreparation *inventory=atom_deep_copy_session_inventory(
        session,&prime_need_reader_preparation_kind);
    if (inventory) inventory->failed=true;
    return CETTA_MATCH_INCOMPLETE;
}

PrimeNeedReaderContext *prime_need_reader_context_copy_lookup(
    const PrimeNeedReaderContext *source,const AtomDeepCopySession *session) {
    if (!atom_deep_copy_session_private_destination(session) ||
        prime_need_reader_preparation_failed(session) ||
        !source || source->length>source->capacity ||
        source->capacity>(SIZE_MAX-sizeof(*source))/sizeof(*source->entries)) return NULL;
    size_t bytes=sizeof(*source)+source->capacity*sizeof(*source->entries);
    return prime_need_reader_copy_image(session,&prime_need_reader_context_copy_kind,source,bytes);
}

static CettaMatchCompletion prime_need_reader_context_copy_mapped(
    const PrimeNeedReaderContext *source, AtomDeepCopySession *session,
    PrimeNeedReaderContext **out) {
    if (!out || !atom_deep_copy_session_private_destination(session) ||
        prime_need_reader_preparation_failed(session)) return CETTA_MATCH_INCOMPLETE;
    if (!source) return prime_need_reader_context_share(NULL,out);
    if (source->length>source->capacity || !source->lineage ||
        !atomic_load_explicit(&source->references,memory_order_relaxed) ||
        source->capacity>(SIZE_MAX-sizeof(*source))/sizeof(*source->entries))
        return CETTA_MATCH_INCOMPLETE;
    size_t bytes=sizeof(*source)+source->capacity*sizeof(*source->entries);
    PrimeNeedReaderContext *known=prime_need_reader_context_copy_lookup(source,session);
    if (known) return prime_need_reader_context_share(known,out);
    for (size_t i=0u;i<source->length;i++) {
        PrimeNeedReaderValue image;
        if (!source->entries[i].key ||
            (i && source->entries[i-1u].key>=source->entries[i].key) ||
            !prime_need_reader_copy_value(session,source->entries[i].reader,&image))
            return CETTA_MATCH_INCOMPLETE;
    }
    PrimeNeedReaderLineage *lineage=prime_need_reader_lineage_copy(source->lineage,session);
    if (!lineage) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedReaderContext *image=malloc(bytes);
    if (!image) return CETTA_MATCH_INCOMPLETE;
    *image=(PrimeNeedReaderContext){.references=1u,.capacity=source->capacity,.lineage=lineage};
    prime_need_reader_reference_retain(&lineage->references);
    bool ready=true;
    for (size_t i=0u;ready && i<source->length;i++) {
        PrimeNeedReaderEntry entry=source->entries[i];
        ready=prime_need_reader_copy_value(session,entry.reader,&entry.reader) &&
            prime_need_reader_context_append(image,entry);
    }
    if (ready) ready=atom_deep_copy_session_object_record_retained(session,
        &prime_need_reader_context_copy_kind,source,image,bytes,
        prime_need_reader_context_copy_hold,prime_need_reader_context_copy_drop);
    if (!ready) {
        prime_need_reader_context_release(image);
        return CETTA_MATCH_INCOMPLETE;
    }
    prime_need_reader_context_replace(out,image);
    return CETTA_MATCH_MATCHED;
}

CettaMatchCompletion prime_need_reader_context_copy_with_session(
    const PrimeNeedReaderContext *source, Arena *owner,
    AtomDeepCopySession *session, PrimeNeedReaderContext **out) {
    if (!atom_deep_copy_session_targets(session, owner)) return CETTA_MATCH_INCOMPLETE;
    return prime_need_reader_context_copy_mapped(source, session, out);
}

/* Prepared values and their source keys belong to the session, even when
 * a later entry fails. A failed inventory refuses further reader publication;
 * the caller discards the unpublished enclosing copy and all its inventories. */
CettaMatchCompletion prime_need_reader_context_prepare_copy_with_session(
    const PrimeNeedReaderContext *source, AtomDeepCopySession *session,
    PrimeNeedReaderValueCopy prepare, void *state, PrimeNeedReaderContext **out) {
    if (!out || !atom_deep_copy_session_private_destination(session) ||
        prime_need_reader_preparation_failed(session) ||
        (source && (!prepare || !source->lineage ||
         !atomic_load_explicit(&source->references,memory_order_relaxed) ||
         source->length>source->capacity ||
         source->capacity>(SIZE_MAX-sizeof(*source))/sizeof(*source->entries))))
        return prime_need_reader_preparation_refused(session);
    if (!source || prime_need_reader_context_copy_lookup(source,session))
        return prime_need_reader_context_copy_mapped(source,session,out);
    for (size_t i=0u;i<source->length;i++) {
        PrimeNeedReaderEntry entry=source->entries[i];
        const PrimeNeedReaderValueOps *ops=entry.reader.operations;
        if (!entry.key || (i && source->entries[i-1u].key>=entry.key) ||
            (!entry.reader.value && ops) ||
            (entry.reader.value && (!ops || !ops->retain || !ops->release ||
             !ops->copy_kind || !ops->copy_size))) return prime_need_reader_preparation_refused(session);
    }
    PrimeNeedReaderPreparation *inventory=atom_deep_copy_session_inventory(
        session,&prime_need_reader_preparation_kind);
    if (!inventory) {
        inventory=calloc(1u,sizeof(*inventory));
        if (!inventory) return prime_need_reader_preparation_refused(session);
        if (!atom_deep_copy_session_install_inventory(session,&prime_need_reader_preparation_kind,
                inventory,prime_need_reader_preparation_release,prime_need_reader_preparation_storage)) {
            free(inventory);return prime_need_reader_preparation_refused(session);
        }
    }
    if (source->length>SIZE_MAX-inventory->length) return prime_need_reader_preparation_refused(session);
    size_t required=inventory->length+source->length;
    if (required>inventory->capacity) {
        if (required>SIZE_MAX/sizeof(*inventory->entries)) return prime_need_reader_preparation_refused(session);
        PrimeNeedReaderPreparedPair *entries=realloc(inventory->entries,required*sizeof(*entries));
        if (!entries) return prime_need_reader_preparation_refused(session);
        inventory->entries=entries;inventory->capacity=required;
    }
    CettaMatchCompletion result=CETTA_MATCH_INCOMPLETE;
    for (size_t i=0u;i<source->length;i++) {
        PrimeNeedReaderValue value=source->entries[i].reader,image={0};
        if (prime_need_reader_copy_value(session,value,&image)) continue;
        const PrimeNeedReaderValueOps *ops=value.operations;
        if (atom_deep_copy_session_object_original(session,ops->copy_kind,value.value,ops->copy_size) ||
            !ops->retain(value.value)) goto failed;
        PrimeNeedReaderPreparedPair *entry=&inventory->entries[inventory->length++];
        *entry=(PrimeNeedReaderPreparedPair){.source=value};
        /* Failure outputs are unspecified and do not transfer a share. */
        result=prepare(state,&value,&image);
        if (result!=CETTA_MATCH_MATCHED) goto failed;
        result=CETTA_MATCH_INCOMPLETE;
        if (!image.value || image.value==value.value || image.operations!=ops ||
            atom_deep_copy_session_object_forwarded(session,ops->copy_kind,image.value,ops->copy_size) ||
            !atom_deep_copy_session_object_record(session,ops->copy_kind,
                value.value,image.value,ops->copy_size)) {
            if (image.value && image.operations && image.operations->release)
                image.operations->release(image.value);
            goto failed;
        }
        entry->image=image;
    }
    result=prime_need_reader_context_copy_mapped(source,session,out);
    if (result==CETTA_MATCH_MATCHED) return result;
failed:
    inventory->failed=true;
    return result;
}

CettaMatchCompletion prime_need_reader_context_update(const PrimeNeedReaderContext *base,
    uint64_t key, const PrimeNeedReaderValue *value, PrimeNeedReaderContext **out) {
    if (!key || !out || (value && (!value->value || !value->operations ||
            !value->operations->retain || !value->operations->release))) return CETTA_MATCH_INCOMPLETE;
    size_t length=base?base->length:0u, position=0u;
    while (position<length && base->entries[position].key<key) position++;
    bool found=position<length && base->entries[position].key==key;
    PrimeNeedReaderValue selected=value?*value:(PrimeNeedReaderValue){0};
    if ((!found && !value) || (found && prime_need_reader_value_same(
            base->entries[position].reader,selected))) return prime_need_reader_context_share(base,out);
    if (!found && length==SIZE_MAX) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedReaderContext *next=prime_need_reader_context_new(length+!found,
        base?base->lineage:NULL,NULL);
    if (!next) return CETTA_MATCH_INCOMPLETE;
    for (size_t i=0u;i<length+!found;i++) {
        PrimeNeedReaderEntry entry;
        if (i==position) entry=(PrimeNeedReaderEntry){.key=key,.reader=selected};
        else entry=base->entries[i-(i>position && !found)];
        if (!prime_need_reader_context_append(next,entry)) {
            prime_need_reader_context_release(next);return CETTA_MATCH_INCOMPLETE;
        }
    }
    prime_need_reader_context_replace(out,next);
    return CETTA_MATCH_MATCHED;
}

CettaMatchCompletion prime_need_reader_context_clear_if(const PrimeNeedReaderContext *base,
        CettaMatchCompletion (*should_clear)(uint64_t,const PrimeNeedReaderValue *,void *,bool *),
        void *state,PrimeNeedReaderContext **out) {
    if (!should_clear || !out) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedReaderContext *next=NULL;
    for (size_t i=0u;base && i<base->length;i++) {
        PrimeNeedReaderEntry entry=base->entries[i];
        bool clear=false;
        if (entry.reader.value) {
            CettaMatchCompletion result=should_clear(entry.key,&entry.reader,state,&clear);
            if (result!=CETTA_MATCH_MATCHED) {
                prime_need_reader_context_release(next);return result;
            }
        }
        if (clear && !next) {
            next=prime_need_reader_context_new(base->length,base->lineage,NULL);
            if (!next) return CETTA_MATCH_INCOMPLETE;
            for (size_t prefix=0u;prefix<i;prefix++) {
                if (!prime_need_reader_context_append(next,base->entries[prefix])) {
                    prime_need_reader_context_release(next);return CETTA_MATCH_INCOMPLETE;
                }
            }
        }
        if (next) {
            if (clear) entry.reader=(PrimeNeedReaderValue){0};
            if (!prime_need_reader_context_append(next,entry)) {
                prime_need_reader_context_release(next);return CETTA_MATCH_INCOMPLETE;
            }
        }
    }
    if (!next) return prime_need_reader_context_share(base,out);
    prime_need_reader_context_replace(out,next);
    return CETTA_MATCH_MATCHED;
}

bool prime_need_reader_context_values_absent(const PrimeNeedReaderContext *context) {
    for(size_t i=0u;context && i<context->length;i++) if(context->entries[i].reader.value) return false;
    return true;
}

static bool prime_need_reader_value_lives_in(const PrimeNeedReaderValue *value,const Arena *arena) {
    return arena && value->operations && value->operations->lives_in &&
        value->operations->lives_in(value->value,arena);
}

CettaMatchCompletion prime_need_reader_context_can_promote(const PrimeNeedReaderContext *context,
    const PrimeNeedSnapshot *world,const Arena *arena) {
    for(size_t i=0u;context && i<context->length;i++) {
        const PrimeNeedReaderValue *value=&context->entries[i].reader;
        if(!value->value) continue;
        if(prime_need_reader_value_lives_in(value,arena)) continue;
        if(!value->operations || !value->operations->settled) return CETTA_MATCH_REFUTED;
        PrimeNeedReadScope metadata;
        prime_need_read_scope_enter_resident(&metadata,NULL);
        CettaMatchCompletion result=value->operations->settled(value->value,world);
        prime_need_read_scope_leave(&metadata);
        if(result!=CETTA_MATCH_MATCHED) return result;
    }
    return CETTA_MATCH_MATCHED;
}

typedef struct {const Arena *arena;} PrimeNeedReaderPromotion;
static CettaMatchCompletion prime_need_reader_clear_foreign(uint64_t key,
        const PrimeNeedReaderValue *value,void *raw,bool *clear) {
    (void)key;
    const PrimeNeedReaderPromotion *promotion=raw;
    *clear=!prime_need_reader_value_lives_in(value,promotion->arena);
    return CETTA_MATCH_MATCHED;
}

CettaMatchCompletion prime_need_reader_context_promote(const PrimeNeedReaderContext *context,
        const PrimeNeedSnapshot *world,const Arena *arena,PrimeNeedReaderContext **out) {
    if (!out) return CETTA_MATCH_INCOMPLETE;
    CettaMatchCompletion result=prime_need_reader_context_can_promote(context,world,arena);
    if (result!=CETTA_MATCH_MATCHED) return result;
    PrimeNeedReaderPromotion promotion={.arena=arena};
    return prime_need_reader_context_clear_if(context,prime_need_reader_clear_foreign,&promotion,out);
}

CettaMatchCompletion prime_need_reader_context_can_settle(const PrimeNeedReaderContext *context,
    const PrimeNeedSnapshot *world) {
    return prime_need_reader_context_can_promote(context,world,NULL);
}

CettaMatchCompletion prime_need_reader_context_settle(const PrimeNeedReaderContext *context,
    const PrimeNeedSnapshot *world,PrimeNeedReaderContext **out) {
    return prime_need_reader_context_promote(context,world,NULL,out);
}

CettaMatchCompletion prime_need_reader_context_merge(const PrimeNeedReaderContext *left,
    const PrimeNeedReaderContext *right, PrimeNeedReaderContext **out) {
    if (!out) return CETTA_MATCH_INCOMPLETE;
    if (!left || left==right) return prime_need_reader_context_share(right,out);
    if (!right) return prime_need_reader_context_share(left,out);
    CettaMatchCompletion ancestor=prime_need_reader_lineage_ancestor(left->lineage,right->lineage);
    if (ancestor==CETTA_MATCH_MATCHED) return prime_need_reader_context_share(right,out);
    if (ancestor==CETTA_MATCH_INCOMPLETE) return ancestor;
    ancestor=prime_need_reader_lineage_ancestor(right->lineage,left->lineage);
    if (ancestor==CETTA_MATCH_MATCHED) return prime_need_reader_context_share(left,out);
    if (ancestor==CETTA_MATCH_INCOMPLETE) return ancestor;
    size_t a=0u,b=0u;
    while (a<left->length && b<right->length) {
        const PrimeNeedReaderEntry *x=&left->entries[a],*y=&right->entries[b];
        if (x->key<y->key) a++;
        else if (y->key<x->key) b++;
        else {if (!prime_need_reader_value_same(x->reader,y->reader)) return CETTA_MATCH_REFUTED;a++;b++;}
    }
    if (left->length>SIZE_MAX-right->length) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedReaderContext *next=prime_need_reader_context_new(left->length+right->length,
        left->lineage,right->lineage);
    if (!next) return CETTA_MATCH_INCOMPLETE;
    a=b=0u;
    while (a<left->length || b<right->length) {
        PrimeNeedReaderEntry entry;
        if (b==right->length || (a<left->length && left->entries[a].key<right->entries[b].key))
            entry=left->entries[a++];
        else if (a==left->length || right->entries[b].key<left->entries[a].key)
            entry=right->entries[b++];
        else {entry=left->entries[a++];b++;}
        if (!prime_need_reader_context_append(next,entry)) {
            prime_need_reader_context_release(next);return CETTA_MATCH_INCOMPLETE;
        }
    }
    prime_need_reader_context_replace(out,next);
    return CETTA_MATCH_MATCHED;
}

static const PrimeNeedReaderEntry *prime_need_reader_entry(
        const PrimeNeedReaderContext *context,uint64_t key,size_t *position) {
    if (!context) return NULL;
    while (*position<context->length && context->entries[*position].key<key) (*position)++;
    return *position<context->length && context->entries[*position].key==key
        ? &context->entries[*position] : NULL;
}
static bool prime_need_reader_entry_same(const PrimeNeedReaderEntry *a,
    const PrimeNeedReaderEntry *b) {
    return (!a&&!b) || (a&&b&&prime_need_reader_value_same(a->reader,b->reader));
}
CettaMatchCompletion prime_need_reader_context_import(const PrimeNeedReaderContext *entrance,
    const PrimeNeedReaderContext *production,const PrimeNeedReaderContext *recipient,
    PrimeNeedReaderContext **out) {
    if (!out) return CETTA_MATCH_INCOMPLETE;
    if (!production) return entrance?CETTA_MATCH_INCOMPLETE:prime_need_reader_context_share(recipient,out);
    if (production==entrance) return prime_need_reader_context_share(recipient,out);
    if (entrance) {
        CettaMatchCompletion licensed=prime_need_reader_lineage_ancestor(entrance->lineage,production->lineage);
        if (licensed!=CETTA_MATCH_MATCHED) return licensed;
    }
    if (recipient) {
        CettaMatchCompletion held=prime_need_reader_lineage_ancestor(production->lineage,recipient->lineage);
        if (held==CETTA_MATCH_MATCHED) return prime_need_reader_context_share(recipient,out);
        if (held==CETTA_MATCH_INCOMPLETE) return held;
        held=prime_need_reader_lineage_holds_import(entrance?entrance->lineage:NULL,
            production->lineage,recipient->lineage);
        if (held==CETTA_MATCH_MATCHED) return prime_need_reader_context_share(recipient,out);
        if (held==CETTA_MATCH_INCOMPLETE) return held;
    }
    bool changed=false;
    size_t entrance_position=0u,recipient_position=0u;
    for (size_t i=0u;i<production->length;i++) {
        const PrimeNeedReaderEntry *p=&production->entries[i],
            *e=prime_need_reader_entry(entrance,p->key,&entrance_position);
        if (prime_need_reader_entry_same(e,p)) continue;
        changed=true;
        const PrimeNeedReaderEntry *r=prime_need_reader_entry(recipient,p->key,&recipient_position);
        if (r && !prime_need_reader_entry_same(r,e) && !prime_need_reader_entry_same(r,p))
            return CETTA_MATCH_REFUTED;
    }
    if (!changed) return prime_need_reader_context_share(recipient,out);
    size_t length=recipient?recipient->length:0u;
    if (length>SIZE_MAX-production->length) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedReaderContext *next=prime_need_reader_context_new(length+production->length,
        recipient?recipient->lineage:NULL,NULL);
    if (!next) return CETTA_MATCH_INCOMPLETE;
    next->lineage->import_entrance=entrance?entrance->lineage:NULL;
    next->lineage->import_production=production->lineage;
    if (entrance) prime_need_reader_reference_retain(&entrance->lineage->references);
    prime_need_reader_reference_retain(&production->lineage->references);
    size_t a=0u,b=0u;
    entrance_position=0u;
    while (a<length || b<production->length) {
        PrimeNeedReaderEntry entry;
        if (b==production->length || (a<length && recipient->entries[a].key<production->entries[b].key))
            entry=recipient->entries[a++];
        else {
            entry=production->entries[b++];
            bool delta=!prime_need_reader_entry_same(&entry,prime_need_reader_entry(entrance,entry.key,&entrance_position));
            if (a<length && recipient->entries[a].key==entry.key) {
                if (!delta) entry=recipient->entries[a];
                a++;
            } else if (!delta) continue;
        }
        if (!prime_need_reader_context_append(next,entry)) {
            prime_need_reader_context_release(next);return CETTA_MATCH_INCOMPLETE;
        }
    }
    prime_need_reader_context_replace(out,next);
    return CETTA_MATCH_MATCHED;
}

static void prime_need_reader_lineage_storage(const void *raw, CettaStorageCensus *census) {
    const PrimeNeedReaderLineage *lineage=raw;
    if (!lineage || !cetta_storage_object(census,lineage,sizeof(*lineage),CETTA_STORAGE_BLOCK)) return;
    if (lineage->left) cetta_storage_schedule(census,lineage->left,prime_need_reader_lineage_storage);
    if (lineage->right) cetta_storage_schedule(census,lineage->right,prime_need_reader_lineage_storage);
    if (lineage->import_entrance) cetta_storage_schedule(census,lineage->import_entrance,prime_need_reader_lineage_storage);
    if (lineage->import_production) cetta_storage_schedule(census,lineage->import_production,prime_need_reader_lineage_storage);
}

void prime_need_reader_context_storage(const PrimeNeedReaderContext *context, CettaStorageCensus *census) {
    if (!context || !cetta_storage_object(census,context,
            sizeof(*context)+context->capacity*sizeof(*context->entries),CETTA_STORAGE_BLOCK)) return;
    if (context->lineage) cetta_storage_schedule(census,context->lineage,prime_need_reader_lineage_storage);
    for (size_t i=0u;i<context->length;i++) {
        PrimeNeedReaderValue value=context->entries[i].reader;
        if (!value.value) continue;
        if (!value.operations->storage) {cetta_storage_refuse(census,"ReaderContextStorage");return;}
        value.operations->storage(value.value,census);
    }
}

bool prime_need_reader_context_payloads(const PrimeNeedReaderContext *context,
    AtomTreePredicate atoms, bool (*cells)(const StateCell *, void *), void *state) {
    for (size_t i=0u;context && i<context->length;i++) {
        PrimeNeedReaderValue value=context->entries[i].reader;
        if (value.value && (!value.operations->payloads ||
                !value.operations->payloads(value.value,atoms,cells,state))) return false;
    }
    return true;
}

void prime_occurrence_init(PrimeOccurrence *occurrence) {
    *occurrence=(PrimeOccurrence){0};
}
void prime_occurrence_copy(PrimeOccurrence *out, const PrimeOccurrence *source) {
    *out=*source;
    prime_need_reader_context_retain(out->reader_context);
}
void prime_occurrence_assign(PrimeOccurrence *out, const PrimeOccurrence *source) {
    if (out==source) return;
    PrimeNeedReaderContext *old=out->reader_context;
    prime_occurrence_copy(out,source);
    prime_need_reader_context_release(old);
}
void prime_occurrence_clear(PrimeOccurrence *occurrence) {
    PrimeNeedReaderContext *old=occurrence->reader_context;
    prime_occurrence_init(occurrence);
    prime_need_reader_context_release(old);
}

static bool prime_need_receipt_collect_all(
    const PrimeNeedReceiptFrame *top,
    PrimeNeedReceiptFrames *frames) {
    PrimeNeedReceiptFrames work = {0};
    if (!top)
        return true;
    if (!prime_need_receipt_frames_push(&work, top)) {
        prime_need_receipt_frames_free(&work);
        return false;
    }
    for (size_t i = 0u; i < work.len; i++) {
        const PrimeNeedReceiptFrame *frame = work.items[i];
        if (frame->left &&
            !prime_need_receipt_frames_push(&work, frame->left)) {
            prime_need_receipt_frames_free(&work);
            return false;
        }
        if (frame->right &&
            !prime_need_receipt_frames_push(&work, frame->right)) {
            prime_need_receipt_frames_free(&work);
            return false;
        }
    }
    *frames = work;
    return true;
}

bool prime_need_branch_state_payloads_satisfy(const PrimeNeedBranchState *state,
        AtomTreePredicate atoms, PrimeNeedStateCellPredicate cells, void *context) {
    if (!state || !atoms || !cells) return false;
    PrimeNeedReceiptFrames frames = {0};
    if (!prime_need_receipt_collect_all(state->top, &frames)) return false;
    bool ok = true;
    for (size_t i = 0u; ok && i < frames.len; i++) {
        const PrimeNeedReceiptFrame *frame = frames.items[i];
        if (!frame->has_event) continue;
        const PrimeNeedReceiptEvent *event = &frame->event;
        ok = (!event->before || atoms(event->before, context)) &&
            (!event->after || atoms(event->after, context)) &&
            (!event->state_cell || cells(event->state_cell, context));
    }
    prime_need_receipt_frames_free(&frames);
    return ok;
}

static bool prime_need_receipt_reaches_exact(
    const PrimeNeedReceiptFrame *top,
    const PrimeNeedReceiptFrame *target,
    size_t *visited_out) {
    PrimeNeedReceiptFrames work = {0};
    size_t visited = 0u;
    bool found = false;
    if (!top || !target)
        goto done;
    if (!prime_need_receipt_frames_push(&work, top))
        goto done;
    for (size_t i = 0u; i < work.len; i++) {
        const PrimeNeedReceiptFrame *frame = work.items[i];
        visited++;
        if (frame->identity_id == target->identity_id) {
            found = true;
            break;
        }
        if (frame->left &&
            !prime_need_receipt_frames_push(&work, frame->left))
            goto done;
        if (frame->right &&
            !prime_need_receipt_frames_push(&work, frame->right))
            goto done;
    }

done:
    if (visited_out)
        *visited_out = visited;
    prime_need_receipt_frames_free(&work);
    return found;
}

static bool prime_need_receipt_reachable(
    const PrimeNeedReceiptFrame *top,
    const PrimeNeedReceiptFrame *target) {
    size_t visited = 0u;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_QUERY);
    if (!target) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_EMPTY_TARGET_ACCEPT);
        return true;
    }
    if (!top || top->session_id != target->session_id) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_BOUNDARY_REJECT);
        return false;
    }
    if (top->identity_id == target->identity_id) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_SELF_ACCEPT);
        return true;
    }
    /* Every receipt edge points to a frame of strictly smaller depth.
     * Equal-depth distinct frames therefore cannot reach one another, and a
     * direct parent is the overwhelmingly common extension case.  Resolve
     * both without collecting the transitive DAG. */
    if (top->depth <= target->depth) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_DEPTH_REJECT);
        return false;
    }
    if ((top->left &&
         top->left->identity_id == target->identity_id) ||
        (top->right &&
         top->right->identity_id == target->identity_id)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_PARENT_ACCEPT);
        return true;
    }
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    size_t index_steps = 0u;
    bool indexed = prime_need_receipt_primary_reaches(
        top, target, &index_steps);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_INDEX_STEP,
        (uint64_t)index_steps);
#endif
    if (indexed) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_INDEX_ACCEPT);
        return true;
    }
#endif
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_FALLBACK);
    bool found = prime_need_receipt_reaches_exact(
        top, target, &visited);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_REACH_FALLBACK_FRAME,
        (uint64_t)visited);
#endif
    return found;
}

void prime_need_branch_state_init(PrimeNeedBranchState *receipt) {
    if (!receipt)
        return;
    receipt->top = NULL;
    receipt->session_id = 0u;
    receipt->owner = NULL;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    receipt->recording = NULL;
#endif
}

static bool prime_need_carrier_begin(
    Arena *owner, PrimeNeedBranchState *receipt) {
    if (!prime_need_owner_admissible(owner) || !receipt)
        return false;
    if (prime_need_branch_state_present(receipt))
        return receipt->owner == owner;
    uint64_t session_id = prime_need_fresh_nonzero(
        &g_prime_need_next_receipt_session);
    if (session_id == 0u) {
        /* No half-begun carrier: it stays absent. */
        prime_need_branch_state_init(receipt);
        return false;
    }
    receipt->top = NULL;
    receipt->session_id = session_id;
    receipt->owner = owner;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    receipt->recording = NULL;
#endif
    return true;
}

bool prime_need_branch_state_begin(
    Arena *owner, PrimeNeedBranchState *state) {
    return prime_need_carrier_begin(owner, state);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
bool prime_need_receipt_begin(Arena *owner, PrimeNeedReceipt *receipt) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_RECEIPT_BEGIN);
    return prime_need_carrier_begin(owner, receipt);
}
#endif

bool prime_need_branch_state_present(const PrimeNeedBranchState *receipt) {
    return receipt && receipt->session_id != 0u && receipt->owner;
}

bool prime_need_branch_state_is_ancestor(
    const PrimeNeedBranchState *ancestor,
    const PrimeNeedBranchState *descendant) {
    if (!ancestor || !descendant)
        return false;
    if (!prime_need_branch_state_present(ancestor))
        return true;
    if (!prime_need_branch_state_present(descendant) ||
        ancestor->session_id != descendant->session_id)
        return false;
    return prime_need_receipt_reachable(descendant->top, ancestor->top);
}

static bool prime_need_receipt_event_same_identity(
    const PrimeNeedReceiptFrame *left,
    const PrimeNeedReceiptFrame *right) {
    return left && right && left->has_event && right->has_event &&
           left->event_id != 0u && left->event_id == right->event_id;
}

static bool prime_need_receipt_events_conflict(
    const PrimeNeedReceiptFrame *left,
    const PrimeNeedReceiptFrame *right) {
    if (!left || !right || !left->has_event || !right->has_event ||
        prime_need_receipt_event_same_identity(left, right))
        return false;
    const PrimeNeedReceiptEvent *a = &left->event;
    const PrimeNeedReceiptEvent *b = &right->event;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if ((a->kind == PRIME_NEED_RECEIPT_OBSERVE_CELL ||
         a->kind == PRIME_NEED_RECEIPT_INSPECT_ORIGIN) &&
        a->kind == b->kind) {
        return a->need_session_id == b->need_session_id &&
               a->thunk_id == b->thunk_id &&
               !atom_eq(a->after, b->after);
    }
#endif
    bool a_state = a->kind == PRIME_NEED_RECEIPT_READ_STATE ||
                   a->kind == PRIME_NEED_RECEIPT_WRITE_STATE;
    bool b_state = b->kind == PRIME_NEED_RECEIPT_READ_STATE ||
                   b->kind == PRIME_NEED_RECEIPT_WRITE_STATE;
    if (!a_state || !b_state || a->state_cell != b->state_cell)
        return false;
    if (a->kind == PRIME_NEED_RECEIPT_READ_STATE &&
        b->kind == PRIME_NEED_RECEIPT_READ_STATE)
        return !atom_eq(a->after, b->after);
    /* State writes are occurrence-bearing effects.  Two incomparable
     * branches may not silently identify, commute, or union them. */
    return true;
}

static bool prime_need_receipt_collect_events(
    const PrimeNeedReceipt *receipt, PrimeNeedReceiptFrames *events);

/* Physical records describe the selected world; event IDs name the work
 * already performed. A graft may give one event another physical record.
 * Coalescing those records requires agreement of the complete payload. */
static CettaMatchCompletion prime_need_event_payload_compare(
    const PrimeNeedReceiptFrame *left, const PrimeNeedReceiptFrame *right) {
    const PrimeNeedReceiptEvent *a = &left->event, *b = &right->event;
    if (left->event_id != right->event_id || a->event_id != b->event_id ||
        a->kind != b->kind || a->state_cell != b->state_cell
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        || a->need_session_id != b->need_session_id || a->thunk_id != b->thunk_id ||
        a->source_occurrence_id != b->source_occurrence_id ||
        a->source_argument_index != b->source_argument_index ||
        a->rule_occurrence_id != b->rule_occurrence_id ||
        a->producer_update_id != b->producer_update_id ||
        a->producer_completed != b->producer_completed ||
        a->state_event_id != b->state_event_id
#endif
        ) return CETTA_MATCH_REFUTED;
    Atom *x[] = {a->before, a->after}, *y[] = {b->before, b->after};
    for (size_t i = 0u; i < 2u; i++) {
        if (x[i] == y[i]) continue;
        if (!x[i] || !y[i]) return CETTA_MATCH_REFUTED;
        CettaMatchCompletion compared = atom_capture_identical_checked(x[i], y[i]);
        if (compared != CETTA_MATCH_MATCHED) return compared;
    }
    return CETTA_MATCH_MATCHED;
}

static int prime_need_receipt_frame_identity_compare(const void *left, const void *right) {
    const PrimeNeedReceiptFrame *a = *(const PrimeNeedReceiptFrame *const *)left;
    const PrimeNeedReceiptFrame *b = *(const PrimeNeedReceiptFrame *const *)right;
    return a->identity_id < b->identity_id ? -1 : a->identity_id > b->identity_id ? 1 : 0;
}
static int prime_need_receipt_event_compare(const void *, const void *);

typedef struct {
    PrimeNeedReceiptFrames physical;
    const PrimeNeedReceiptFrame **events;
    size_t event_count;
} PrimeNeedStateInventory;

static void prime_need_state_inventory_free(PrimeNeedStateInventory *inventory) {
    prime_need_receipt_frames_free(&inventory->physical);
    free(inventory->events);
    *inventory = (PrimeNeedStateInventory){0};
}

static size_t prime_need_state_node_position(const PrimeNeedStateInventory *inventory,
    uint64_t identity) {
    size_t lo = 0u, hi = inventory->physical.len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        uint64_t key = inventory->physical.items[mid]->identity_id;
        if (key < identity) lo = mid + 1u;
        else hi = mid;
    }
    return lo < inventory->physical.len &&
        inventory->physical.items[lo]->identity_id == identity ? lo : SIZE_MAX;
}

static size_t prime_need_state_event_position(const PrimeNeedStateInventory *inventory,
    uint64_t identity) {
    size_t lo = 0u, hi = inventory->event_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (inventory->events[mid]->event_id < identity) lo = mid + 1u;
        else hi = mid;
    }
    return lo < inventory->event_count && inventory->events[lo]->event_id == identity
        ? lo : SIZE_MAX;
}

/* One or two held roots, with a complete physical inventory. The sorted
 * physical IDs provide a topological order only after checking every edge;
 * they are not an entrance watermark or an order on semantic events. */
static CettaMatchCompletion prime_need_state_inventory(
    const PrimeNeedBranchState *left, const PrimeNeedBranchState *right,
    PrimeNeedStateInventory *out) {
    if (!left || !out || (right && left->session_id != right->session_id))
        return CETTA_MATCH_INCOMPLETE;
    PrimeNeedStateInventory prepared = {0};
    PrimeNeedReceiptFrames extra = {0};
    CettaMatchCompletion result = CETTA_MATCH_INCOMPLETE;
    if (!prime_need_receipt_collect_all(left->top, &prepared.physical) ||
        (right && !prime_need_receipt_collect_all(right->top, &extra))) goto done;
    for (size_t i = 0u; i < extra.len; i++)
        if (!prime_need_receipt_frames_push(&prepared.physical, extra.items[i])) goto done;
    PrimeNeedReceiptFrames *frames = &prepared.physical;
    if (frames->len > SIZE_MAX / sizeof(*prepared.events)) goto done;
    prepared.events = frames->len ? malloc(frames->len * sizeof(*prepared.events)) : NULL;
    if (frames->len && !prepared.events) goto done;
    if (frames->len > 1u) qsort(frames->items, frames->len,
        sizeof(*frames->items), prime_need_receipt_frame_identity_compare);
    size_t physical_count = 0u;
    for (size_t i = 0u; i < frames->len; i++) {
        const PrimeNeedReceiptFrame *frame = frames->items[i];
        if (!frame->identity_id || !frame->depth || frame->session_id != left->session_id ||
            !frame->owner || (frame->has_event != (frame->event_id != 0u)) ||
            frame->event.event_id != frame->event_id) goto done;
        if (frame->has_event) {
            bool state = frame->event.kind == PRIME_NEED_RECEIPT_READ_STATE ||
                         frame->event.kind == PRIME_NEED_RECEIPT_WRITE_STATE;
            if ((unsigned)frame->event.kind > PRIME_NEED_RECEIPT_ADMISSION_PENDING || frame->right ||
                (frame->event_id != frame->identity_id && !state) ||
                (state && (!frame->event.state_cell || !frame->event.after)) ||
                (frame->event.kind == PRIME_NEED_RECEIPT_WRITE_STATE && !frame->event.before)) goto done;
        } else if (!frame->left || !frame->right ||
                   frame->left->identity_id == frame->right->identity_id) goto done;
        const PrimeNeedReceiptFrame *parents[] = {frame->left, frame->right};
        for (size_t j = 0u; j < 2u; j++) if (parents[j] &&
            (parents[j]->session_id != frame->session_id || parents[j]->depth >= frame->depth ||
             parents[j]->identity_id >= frame->identity_id)) goto done;
        if (physical_count && frames->items[physical_count - 1u]->identity_id == frame->identity_id) {
            const PrimeNeedReceiptFrame *same = frames->items[physical_count - 1u];
            if (same->depth != frame->depth || same->has_event != frame->has_event ||
                (same->left ? same->left->identity_id : 0u) !=
                    (frame->left ? frame->left->identity_id : 0u) ||
                (same->right ? same->right->identity_id : 0u) !=
                    (frame->right ? frame->right->identity_id : 0u)) goto done;
            if (frame->has_event) {
                result = prime_need_event_payload_compare(same, frame);
                if (result != CETTA_MATCH_MATCHED) goto done;
                result = CETTA_MATCH_INCOMPLETE;
            }
            continue;
        }
        frames->items[physical_count++] = frame;
        if (frame->has_event) prepared.events[prepared.event_count++] = frame;
    }
    frames->len = physical_count;
    if (prepared.event_count > 1u) qsort(prepared.events, prepared.event_count,
        sizeof(*prepared.events), prime_need_receipt_event_compare);
    size_t unique = 0u;
    for (size_t i = 0u; i < prepared.event_count; i++) {
        const PrimeNeedReceiptFrame *frame = prepared.events[i];
        if (unique && prepared.events[unique - 1u]->event_id == frame->event_id) {
            result = prime_need_event_payload_compare(prepared.events[unique - 1u], frame);
            if (result != CETTA_MATCH_MATCHED) goto done;
        } else prepared.events[unique++] = frame;
    }
    prepared.event_count = unique;
    *out = prepared; prepared = (PrimeNeedStateInventory){0};
    result = CETTA_MATCH_MATCHED;
done:
    prime_need_receipt_frames_free(&extra);
    prime_need_state_inventory_free(&prepared);
    return result;
}

/* Cell-local canonical order is shared by lookup and delivery validation.
 * Physical paths may traverse other cells, but alias closure only identifies
 * observations of this cell. Independent receiving contexts are not a clock. */
typedef struct {
    const PrimeNeedReceiptFrame **events;
    uint64_t *after;
    size_t count, words;
} PrimeNeedStateOrder;

static void prime_need_state_order_free(PrimeNeedStateOrder *order) {
    free(order->events); free(order->after); *order = (PrimeNeedStateOrder){0};
}

static bool prime_need_state_order_after(const PrimeNeedStateOrder *order, size_t a, size_t b) {
    return (order->after[a * order->words + b / 64u] & (UINT64_C(1) << (b % 64u))) != 0u;
}

static size_t prime_need_state_order_position(const PrimeNeedStateOrder *order, uint64_t event) {
    size_t lo = 0u, hi = order->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (order->events[mid]->event_id < event) lo = mid + 1u;
        else hi = mid;
    }
    return lo < order->count && order->events[lo]->event_id == event ? lo : SIZE_MAX;
}

static CettaMatchCompletion prime_need_state_order(
    const PrimeNeedStateInventory *inventory, StateCell *cell, PrimeNeedStateOrder *out) {
    size_t n = inventory->physical.len, k = 0u;
    for (size_t i = 0u; i < inventory->event_count; i++)
        k += inventory->events[i]->event.state_cell == cell &&
             (inventory->events[i]->event.kind == PRIME_NEED_RECEIPT_READ_STATE ||
              inventory->events[i]->event.kind == PRIME_NEED_RECEIPT_WRITE_STATE);
    if (!k) {*out = (PrimeNeedStateOrder){0}; return CETTA_MATCH_MATCHED;}
    if (k > SIZE_MAX / sizeof(PrimeNeedReceiptFrame *) || k > SIZE_MAX - 63u)
        return CETTA_MATCH_INCOMPLETE;
    size_t words = (k + 63u) / 64u;
    if (words > SIZE_MAX / k / sizeof(uint64_t)) return CETTA_MATCH_INCOMPLETE;
    PrimeNeedStateOrder prepared = {.count=k,.words=words};
    prepared.events = malloc(k * sizeof(*prepared.events));
    prepared.after = calloc(k * words, sizeof(*prepared.after));
    bool *reached = n ? malloc(n * sizeof(*reached)) : NULL;
    CettaMatchCompletion result = CETTA_MATCH_INCOMPLETE;
    if (!prepared.events || !prepared.after || (n && !reached)) goto done;
    size_t count = 0u;
    for (size_t i = 0u; i < inventory->event_count; i++) {
        const PrimeNeedReceiptFrame *f = inventory->events[i];
        if (f->event.state_cell == cell && (f->event.kind == PRIME_NEED_RECEIPT_READ_STATE ||
            f->event.kind == PRIME_NEED_RECEIPT_WRITE_STATE)) prepared.events[count++] = f;
    }
    for (size_t a = 0u; a < k; a++) {
        memset(reached, 0, n * sizeof(*reached));
        for (size_t i = 0u; i < n; i++) {
            const PrimeNeedReceiptFrame *f = inventory->physical.items[i];
            const PrimeNeedReceiptFrame *parents[] = {f->left, f->right};
            bool inherited = false;
            for (size_t j = 0u; j < 2u; j++) if (parents[j]) {
                size_t position = prime_need_state_node_position(inventory, parents[j]->identity_id);
                if (position >= i) goto done;
                inherited |= reached[position];
            }
            reached[i] = inherited || (f->has_event && f->event_id == prepared.events[a]->event_id);
            if (inherited && f->has_event && f->event.state_cell == cell) {
                size_t b = prime_need_state_order_position(&prepared,f->event_id);
                if (b != SIZE_MAX) prepared.after[a * words + b / 64u] |= UINT64_C(1) << (b % 64u);
            }
        }
    }
    for (size_t via = 0u; via < k; via++)
        for (size_t a = 0u; a < k; a++)
            if (prime_need_state_order_after(&prepared,a,via))
                for (size_t w = 0u; w < words; w++)
                    prepared.after[a * words + w] |= prepared.after[via * words + w];
    *out = prepared; prepared = (PrimeNeedStateOrder){0}; result = CETTA_MATCH_MATCHED;
done:
    free(reached); prime_need_state_order_free(&prepared); return result;
}

static CettaMatchCompletion prime_need_state_order_value(const PrimeNeedStateOrder *order, Atom **out) {
    Atom *value = NULL;
    for (size_t a = 0u; a < order->count; a++) {
        if (prime_need_state_order_after(order,a,a) &&
            order->events[a]->event.kind != PRIME_NEED_RECEIPT_READ_STATE)
            return CETTA_MATCH_INCOMPLETE;
        bool maximal = true;
        for (size_t b = 0u; b < order->count; b++) {
            if (a == b) continue;
            bool ab = prime_need_state_order_after(order,a,b), ba = prime_need_state_order_after(order,b,a);
            maximal &= !(ab && !ba);
            if (ab != ba) continue;
            /* Equal read-only components are a commuting frontier. A
             * write-containing cycle or incomparable write is not one. */
            if (order->events[a]->event.kind != PRIME_NEED_RECEIPT_READ_STATE ||
                order->events[b]->event.kind != PRIME_NEED_RECEIPT_READ_STATE)
                return ab ? CETTA_MATCH_INCOMPLETE : CETTA_MATCH_REFUTED;
            CettaMatchCompletion equal = atom_capture_identical_checked(
                order->events[a]->event.after,order->events[b]->event.after);
            if (equal != CETTA_MATCH_MATCHED) return equal;
        }
        if (maximal) value = order->events[a]->event.after;
    }
    if (order->count && !value) return CETTA_MATCH_INCOMPLETE;
    *out = value; return CETTA_MATCH_MATCHED;
}

static CettaMatchCompletion prime_need_state_inventory_version(
    const PrimeNeedStateInventory *inventory, StateCell *cell, Atom **out,
    uint64_t *out_write_event_id) {
    PrimeNeedStateOrder order = {0};
    Atom *value = NULL;
    CettaMatchCompletion result = prime_need_state_order(inventory,cell,&order);
    if (result == CETTA_MATCH_MATCHED) result = prime_need_state_order_value(&order,&value);
    if (result == CETTA_MATCH_MATCHED) {
        size_t last = SIZE_MAX;
        if (out_write_event_id) for (size_t i = 0u; i < order.count; i++) {
            if (order.events[i]->event.kind != PRIME_NEED_RECEIPT_WRITE_STATE) continue;
            /* The value check established comparable, acyclic writes. Only
             * their canonical ancestry selects the supplying occurrence. */
            if (last == SIZE_MAX || prime_need_state_order_after(&order,last,i)) last = i;
        }
        if (last != SIZE_MAX && value != order.events[last]->event.after)
            result = atom_capture_identical_checked(value,order.events[last]->event.after);
        if (result == CETTA_MATCH_MATCHED) {
            *out = value;
            if (out_write_event_id)
                *out_write_event_id = last == SIZE_MAX ? 0u : order.events[last]->event_id;
        }
    }
    prime_need_state_order_free(&order); return result;
}

static CettaMatchCompletion prime_need_state_inventory_value(
    const PrimeNeedStateInventory *inventory, StateCell *cell, Atom **out) {
    return prime_need_state_inventory_version(inventory,cell,out,NULL);
}

CettaMatchCompletion prime_need_branch_state_same_writes_checked(
    const PrimeNeedBranchState *left,const PrimeNeedBranchState *right,StateCell *cell) {
    if (!left || !right || !cell) return CETTA_MATCH_INCOMPLETE;
    if (left->session_id!=right->session_id) return CETTA_MATCH_REFUTED;
    PrimeNeedStateInventory inventory[2]={0};
    PrimeNeedStateOrder order[2]={0};
    const PrimeNeedBranchState *states[]={left,right};
    CettaMatchCompletion result=CETTA_MATCH_INCOMPLETE;
    for (size_t side=0u;side<2u;side++) {
        result=prime_need_state_inventory(states[side],NULL,&inventory[side]);
        if (result!=CETTA_MATCH_MATCHED) goto done;
        result=prime_need_state_order(&inventory[side],cell,&order[side]);
        if (result!=CETTA_MATCH_MATCHED) goto done;
        Atom *value=NULL;
        result=prime_need_state_order_value(&order[side],&value);
        if (result!=CETTA_MATCH_MATCHED) goto done;
    }
    size_t position[2]={0u,0u};
    for (;;) {
        for (size_t side=0u;side<2u;side++)
            while (position[side]<order[side].count &&
                order[side].events[position[side]]->event.kind!=PRIME_NEED_RECEIPT_WRITE_STATE)
                position[side]++;
        bool end[2]={position[0]==order[0].count,position[1]==order[1].count};
        if (end[0] || end[1]) {
            result=end[0] && end[1] ? CETTA_MATCH_MATCHED : CETTA_MATCH_REFUTED;
            break;
        }
        result=prime_need_event_payload_compare(order[0].events[position[0]],
            order[1].events[position[1]]);
        if (result!=CETTA_MATCH_MATCHED) goto done;
        position[0]++;position[1]++;
    }
    if (result==CETTA_MATCH_MATCHED)
        for (size_t a=0u;a<order[0].count;a++) {
            if (order[0].events[a]->event.kind!=PRIME_NEED_RECEIPT_WRITE_STATE) continue;
            size_t x=prime_need_state_order_position(&order[1],order[0].events[a]->event_id);
            if (x==SIZE_MAX) {result=CETTA_MATCH_INCOMPLETE;goto done;}
            for (size_t b=0u;b<order[0].count;b++) {
                if (order[0].events[b]->event.kind!=PRIME_NEED_RECEIPT_WRITE_STATE) continue;
                size_t y=prime_need_state_order_position(&order[1],order[0].events[b]->event_id);
                if (y==SIZE_MAX) {result=CETTA_MATCH_INCOMPLETE;goto done;}
                if (prime_need_state_order_after(&order[0],a,b)!=
                    prime_need_state_order_after(&order[1],x,y)) {
                    result=CETTA_MATCH_REFUTED;goto done;
                }
            }
        }
done:
    for (size_t side=0u;side<2u;side++) {
        prime_need_state_order_free(&order[side]);prime_need_state_inventory_free(&inventory[side]);
    }
    return result;
}

static bool prime_need_state_cell_rebased(const PrimeNeedStateInventory *inventory,
    StateCell *cell) {
    for (size_t i = 0u; i < inventory->physical.len; i++) {
        const PrimeNeedReceiptFrame *frame = inventory->physical.items[i];
        if (frame->has_event && frame->identity_id != frame->event_id &&
            (!cell || frame->event.state_cell == cell)) return true;
    }
    return false;
}

static CettaMatchCompletion prime_need_state_inventory_subset(
    const PrimeNeedStateInventory *prefix, const PrimeNeedStateInventory *whole) {
    for (size_t i = 0u; i < prefix->physical.len; i++) {
        const PrimeNeedReceiptFrame *a = prefix->physical.items[i];
        size_t p = prime_need_state_node_position(whole, a->identity_id);
        if (p == SIZE_MAX) return CETTA_MATCH_REFUTED;
        const PrimeNeedReceiptFrame *b = whole->physical.items[p];
        if (a->depth != b->depth || a->has_event != b->has_event || a->event_id != b->event_id ||
            (a->left ? a->left->identity_id : 0u) != (b->left ? b->left->identity_id : 0u) ||
            (a->right ? a->right->identity_id : 0u) != (b->right ? b->right->identity_id : 0u))
            return CETTA_MATCH_REFUTED;
        if (a->has_event) {
            CettaMatchCompletion compared = prime_need_event_payload_compare(a, b);
            if (compared != CETTA_MATCH_MATCHED) return compared;
        }
    }
    return CETTA_MATCH_MATCHED;
}

/* Each input already owns a causally ordered history.  Only occurrences
 * exclusive to opposite branches can be concurrent hazards: comparing a
 * common write against a later event in either history would instead reject
 * that history's existing sequential effects.  Ordered event identities,
 * rather than addresses or equal payloads, retain this distinction after
 * promotion.  These compacted arrays are consumed by the check and freed;
 * their collection membership tables are not used again. */
static void prime_need_receipt_remove_shared_events(
    PrimeNeedReceiptFrames *left, PrimeNeedReceiptFrames *right) {
    size_t l = 0u, r = 0u, left_only = 0u, right_only = 0u;
    while (l < left->len || r < right->len) {
        if (l < left->len && (r == right->len ||
                left->items[l]->event_id < right->items[r]->event_id)) {
            left->items[left_only++] = left->items[l++];
        } else if (r < right->len && (l == left->len ||
                right->items[r]->event_id < left->items[l]->event_id)) {
            right->items[right_only++] = right->items[r++];
        } else {
            uint64_t shared = left->items[l]->event_id;
            while (l < left->len && left->items[l]->event_id == shared) l++;
            while (r < right->len && right->items[r]->event_id == shared) r++;
        }
    }
    left->len = left_only;
    right->len = right_only;
}

bool prime_need_branch_state_compatible(
    const PrimeNeedBranchState *left,
    const PrimeNeedBranchState *right) {
    if (!left || !right)
        return false;
    if (!prime_need_branch_state_present(left) ||
        !prime_need_branch_state_present(right))
        return true;
    if (left->session_id != right->session_id)
        return false;
    if (prime_need_branch_state_is_ancestor(left, right) ||
        prime_need_branch_state_is_ancestor(right, left))
        return true;
    PrimeNeedReceiptFrames left_frames = {0};
    PrimeNeedReceiptFrames right_frames = {0};
    if (!prime_need_receipt_collect_events(left, &left_frames) ||
        !prime_need_receipt_collect_events(right, &right_frames)) {
        prime_need_receipt_frames_free(&left_frames);
        prime_need_receipt_frames_free(&right_frames);
        return false;
    }
    bool rebased = false;
    for (size_t i = 0u; i < left_frames.len; i++)
        rebased |= left_frames.items[i]->identity_id != left_frames.items[i]->event_id;
    for (size_t i = 0u; i < right_frames.len; i++)
        rebased |= right_frames.items[i]->identity_id != right_frames.items[i]->event_id;
    PrimeNeedStateInventory canonical = {0};
    bool compatible = true;
    if (rebased) {
        compatible = prime_need_state_inventory(left, right, &canonical) == CETTA_MATCH_MATCHED;
        for (size_t i = 0u; compatible && i < canonical.event_count; i++) {
            StateCell *cell = canonical.events[i]->event.state_cell;
            if (!cell || !prime_need_state_cell_rebased(&canonical, cell)) continue;
            bool earlier = false;
            for (size_t j = 0u; j < i; j++) earlier |= canonical.events[j]->event.state_cell == cell;
            if (earlier) continue;
            Atom *value = NULL;
            compatible = prime_need_state_inventory_value(&canonical, cell, &value) == CETTA_MATCH_MATCHED;
        }
    }
    prime_need_receipt_remove_shared_events(&left_frames, &right_frames);
    for (size_t i = 0u; compatible && i < left_frames.len; i++) {
        const PrimeNeedReceiptFrame *a = left_frames.items[i];
        if (!a->has_event)
            continue;
        for (size_t j = 0u; j < right_frames.len; j++) {
            const PrimeNeedReceiptFrame *b = right_frames.items[j];
            if (b->has_event &&
                prime_need_receipt_events_conflict(a, b)) {
                compatible = false;
                break;
            }
        }
    }
    prime_need_receipt_frames_free(&left_frames);
    prime_need_receipt_frames_free(&right_frames);
    prime_need_state_inventory_free(&canonical);
    return compatible;
}

static PrimeNeedReceiptFrame *prime_need_receipt_alloc_frame(Arena *owner) {
    PrimeNeedReceiptFrame *frame = prime_need_owner_admissible(owner)
        ? arena_alloc(owner, sizeof(PrimeNeedReceiptFrame)) : NULL;
    if (frame) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_FRAME_ALLOC);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_FRAME_BYTES,
            sizeof(PrimeNeedReceiptFrame));
    }
    return frame;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
enum { PRIME_NEED_RECORDING_BLOCK_BYTES = 64u };

static void prime_need_recording_omit(PrimeNeedRecordingSlice *slice) {
    if (slice->group->sealed) return;
    atomic_store_explicit(&slice->incomplete, true, memory_order_relaxed);
    uint64_t old = atomic_load_explicit(&slice->omitted, memory_order_relaxed);
    while (old != UINT64_MAX && !atomic_compare_exchange_weak_explicit(
            &slice->omitted, &old, old + 1u,
            memory_order_relaxed, memory_order_relaxed)) {}
    if (old == UINT64_MAX)
        atomic_store_explicit(&slice->overflow, true, memory_order_relaxed);
}

_Thread_local const PrimeNeedReceipt *g_prime_need_recording_scope;
static bool prime_need_receipt_merge_retained(
        PrimeNeedReceipt *dst, const PrimeNeedReceipt *src) {
    if ((dst && dst->recording) || (src && src->recording))
        return prime_need_recording_merge(dst, src);
    return prime_need_branch_state_merge(dst, src);
}
_Thread_local PrimeNeedReceiptMerge prime_need_receipt_merge_operation =
    prime_need_receipt_merge_retained;
extern void bindings_prime_observation_policy_changed(void) __attribute__((weak));
extern void prime_need_observation_policy_changed(void) __attribute__((weak));

/* Binding joins depend only on this scope, while the evaluator also responds
 * to cost and answer observers. Refresh the join only at its own boundary. */
static void prime_need_recording_policy_changed(void) {
    prime_need_receipt_merge_operation = g_prime_need_recording_scope
        ? prime_need_recording_merge_scoped : prime_need_receipt_merge_retained;
    if (bindings_prime_observation_policy_changed)
        bindings_prime_observation_policy_changed();
    if (prime_need_observation_policy_changed)
        prime_need_observation_policy_changed();
}

void prime_need_recording_enter(PrimeNeedRecordingScope *scope,
    const PrimeNeedReceipt *slice) {
    assert(scope && slice);
    *scope = (PrimeNeedRecordingScope){g_prime_need_recording_scope, true};
    g_prime_need_recording_scope = slice;
    prime_need_recording_policy_changed();
}

void prime_need_recording_leave(PrimeNeedRecordingScope *scope) {
    if (scope && scope->entered) {
        g_prime_need_recording_scope = scope->previous;
        scope->entered = false;
        prime_need_recording_policy_changed();
    }
}

void prime_need_recording_omit_scope(void) {
    if (g_prime_need_recording_scope && g_prime_need_recording_scope->recording)
        prime_need_recording_omit(g_prime_need_recording_scope->recording);
}

bool prime_need_recording_inherit(PrimeNeedReceipt *out) {
    if (!out)
        return false;
    const PrimeNeedReceipt *active = g_prime_need_recording_scope;
    PrimeNeedRecordingSlice *parent = active && active->recording
        ? active->recording : NULL;
    if (!parent || !parent->group)
        return false;
    return (parent->growing ? prime_need_recording_reserve_growing
                            : prime_need_recording_reserve)(
        parent->group, NULL, parent->node_quantum, parent->byte_quantum, out);
}

static bool prime_need_recording_scoped(const PrimeNeedReceipt *base,
    PrimeNeedReceipt *out) {
    const PrimeNeedReceipt *active = g_prime_need_recording_scope;
    assert(active);
    *out = *active;
    if (!active->recording || !prime_need_recording_readable(active))
        return false;
    /* A base frame from this bounded group extends the slice chain. A base
     * outside the group is not a recording edge: retain the event on the
     * slice's own chain instead of marking every later event omitted. */
    if (base && base->top && base->recording && prime_need_recording_readable(base) &&
        base->recording->group == active->recording->group)
        out->top = base->top;
    return true;
}

bool prime_need_recording_merge_scoped(PrimeNeedReceipt *dst,
    const PrimeNeedReceipt *src) {
    if (!dst) return false;
    PrimeNeedReceipt left, right;
    if (!prime_need_recording_scoped(dst, &left)) {
        *dst = left;
        return true;
    }
    (void)prime_need_recording_scoped(src, &right);
    bool ok = prime_need_recording_merge(&left, &right);
    if (ok) *dst = left;
    return ok;
}

bool prime_need_recording_roots(const PrimeNeedReceipt *receipt,
    PrimeNeedRecordingRootVisitor visit, void *context) {
    if (!receipt || !receipt->recording || !visit) return false;
    if (!prime_need_recording_readable(receipt))
        return prime_need_recording_erased(receipt);
    const PrimeNeedRecordingSlice *slice = receipt->recording;
    bool imported_exposed = slice->published_root &&
        slice->published_root->owner != &slice->arena;
    for (const PrimeNeedReceiptFrame *frame = slice->first;
         imported_exposed && frame; frame = frame->recording_next)
        if (!frame->recording_referenced &&
            prime_need_receipt_reachable(frame, slice->published_root))
            imported_exposed = false;
    if (imported_exposed) {
        PrimeNeedReceipt root = *receipt;
        root.top = slice->published_root;
        if (!visit(&root, context)) return false;
    }
    for (const PrimeNeedReceiptFrame *frame = slice->first; frame;
         frame = frame->recording_next) {
        if (!frame->recording_referenced) {
            PrimeNeedReceipt root = *receipt;
            root.top = frame;
            if (!visit(&root, context)) return false;
        }
    }
    return true;
}

static PrimeNeedRecording *prime_need_recording_allocate(PrimeNeedRecordingLimits limits) {
    if (limits.slices > (SIZE_MAX - sizeof(PrimeNeedRecording)) /
                            sizeof(PrimeNeedRecordingSlice))
        return NULL;
    size_t bytes = sizeof(PrimeNeedRecording) +
        limits.slices * sizeof(PrimeNeedRecordingSlice);
    size_t link_bytes = arena_retained_owner_allocation_bytes();
    if (limits.owner_links > (SIZE_MAX - bytes) / link_bytes)
        return NULL;
    size_t metadata = bytes + limits.owner_links * link_bytes;
    if (limits.payload_bytes > SIZE_MAX - metadata)
        return NULL;
    PrimeNeedRecording *group = calloc(1u, bytes);
    if (!group)
        return NULL;
    atomic_flag_clear_explicit(&group->budget_lock, memory_order_relaxed);
    atomic_init(&group->references, 1u);
    atomic_init(&group->payload_references, 1u);
    atomic_init(&group->forgotten, false);
    atomic_init(&group->owner_links, 0u);
    group->limits = limits;
    group->allocation_bytes = bytes;
    group->metadata_bound = metadata;
    return group;
}

PrimeNeedRecording *prime_need_recording_new(PrimeNeedRecordingLimits limits) {
    PrimeNeedRecording *group = prime_need_recording_allocate(limits);
    if (!group) return NULL;
    group->session_id = prime_need_fresh_nonzero(
        &g_prime_need_next_receipt_session);
    if (!group->session_id) {
        free(group);
        return NULL;
    }
    return group;
}

void prime_need_recording_seal(PrimeNeedRecording *group) {
    if (group) group->sealed = true;
}

void prime_need_recording_forget(PrimeNeedRecording *group) {
    if (!group) return;
    prime_need_recording_seal(group);
    atomic_store_explicit(&group->forgotten, true, memory_order_release);
}

void prime_need_recording_carrier_retain(void *raw) {
    PrimeNeedRecording *group = raw;
    if (group && atomic_fetch_add_explicit(
            &group->references, 1u, memory_order_relaxed) == SIZE_MAX)
        abort();
}

void prime_need_recording_carrier_release(void *raw) {
    PrimeNeedRecording *group = raw;
    while (group && atomic_fetch_sub_explicit(
            &group->references, 1u, memory_order_acq_rel) == 1u) {
        assert(!atomic_load_explicit(&group->payload_references, memory_order_relaxed));
        PrimeNeedRecording *previous = group->previous;
        free(group);
        group = previous;
    }
}

void prime_need_recording_retain(void *raw) {
    PrimeNeedRecording *group = raw;
    if (!group) return;
    prime_need_recording_carrier_retain(group);
    size_t before = atomic_fetch_add_explicit(
        &group->payload_references, 1u, memory_order_relaxed);
    if (!before || before == SIZE_MAX) abort();
}

void prime_need_recording_release(void *raw) {
    PrimeNeedRecording *group = raw;
    if (!group) return;
    size_t before = atomic_fetch_sub_explicit(
        &group->payload_references, 1u, memory_order_acq_rel);
    assert(before);
    if (before == 1u)
        for (size_t i = 0u; i < group->slice_count; i++) {
            PrimeNeedRecordingSlice *slice = &group->slices[i];
            arena_free(&slice->arena);
            slice->nodes = slice->charge = 0u;
            slice->first = slice->last = NULL;
            slice->published_root = NULL;
        }
    prime_need_recording_carrier_release(group);
}

void prime_need_recording_demote(PrimeNeedRecording *group) {
    if (!group) return;
    prime_need_recording_forget(group);
    prime_need_recording_carrier_retain(group);
    prime_need_recording_release(group);
}

void prime_need_recording_adopt_previous(PrimeNeedRecording *group,
    PrimeNeedRecording *previous) {
    assert(group && !group->previous && group != previous);
    group->previous = previous;
}

bool prime_need_recording_readable(const PrimeNeedReceipt *receipt) {
    if (!receipt || !receipt->recording) return false;
    PrimeNeedRecordingSlice *slice = receipt->recording;
    PrimeNeedRecording *group = slice->group;
    if (!group || receipt->session_id != group->session_id ||
        !atomic_load_explicit(&group->payload_references, memory_order_acquire))
        return false;
    /* An explicit reader owns its arena's strong edge. Borrowed carriers
     * cannot recover that permission merely because another reader lives. */
    if (receipt->owner == &slice->arena)
        return !atomic_load_explicit(&group->forgotten, memory_order_acquire);
    return receipt->owner && arena_retains_owner(
        receipt->owner, group, prime_need_recording_release);
}

bool prime_need_recording_same_group(
        const PrimeNeedReceipt *left,const PrimeNeedReceipt *right) {
    return left&&right&&left->recording&&right->recording&&
        left->recording->group==right->recording->group;
}

bool prime_need_recording_erased(const PrimeNeedReceipt *receipt) {
    if (!receipt || !receipt->recording) return false;
    PrimeNeedRecordingSlice *slice = receipt->recording;
    PrimeNeedRecording *group = slice->group;
    return group && receipt->session_id == group->session_id &&
        receipt->owner == &slice->arena &&
        (atomic_load_explicit(&group->forgotten, memory_order_acquire) ||
         !atomic_load_explicit(&group->payload_references, memory_order_acquire));
}

bool prime_need_recording_audit_receipt(PrimeNeedArenaAudit *audit,
    const PrimeNeedReceipt *receipt) {
    if (!audit || !receipt) return false;
    if (!prime_need_recording_readable(receipt))
        return prime_need_recording_erased(receipt);
    if (receipt->owner == audit->forbidden) return false;
    PrimeNeedReceipt allocated = *receipt;
    allocated.owner = receipt->top ? receipt->top->owner : &receipt->recording->arena;
    return prime_need_arena_audit_branch_state(audit, &allocated);
}

bool prime_need_recording_excludes_arena(const PrimeNeedReceipt *receipt,
    const Arena *forbidden) {
    PrimeNeedArenaAudit *audit = prime_need_arena_audit_new(forbidden);
    if (!audit) return false;
    bool excludes = prime_need_recording_audit_receipt(audit, receipt);
    prime_need_arena_audit_free(audit);
    return excludes;
}

void prime_need_recording_carrier_storage(const void *raw, CettaStorageCensus *census) {
    for (const PrimeNeedRecording *group = raw; group; group = group->previous)
        if (!cetta_storage_object(census, group, group->allocation_bytes, CETTA_STORAGE_BLOCK))
            break;
}

void prime_need_recording_storage(const void *raw, CettaStorageCensus *census) {
    const PrimeNeedRecording *group = raw;
    if (!group || !census) return;
    prime_need_recording_carrier_storage(group, census);
    /* A weak visit must not hide payload reached later through a reader. */
    if (!cetta_storage_object(census, &group->payload_references, 0u, CETTA_STORAGE_VISITED))
        return;
    for (size_t i = 0u; i < group->slice_count; i++)
        arena_storage_census(&group->slices[i].arena, census);
}

PrimeNeedRecordingStatus prime_need_recording_status(const PrimeNeedRecording *group) {
    PrimeNeedRecordingStatus status = {0};
    if (!group) {
        status.incomplete = true;
        return status;
    }
    status.reserved_nodes = group->reserved_nodes;
    status.reserved_payload_bytes = group->reserved_bytes;
    status.metadata_bound = group->metadata_bound;
    status.owner_links = atomic_load_explicit(
        &group->owner_links, memory_order_relaxed);
    status.incomplete = !atomic_load_explicit(
        &group->payload_references, memory_order_acquire);
    for (size_t i = 0u; i < group->slice_count; i++) {
        const PrimeNeedRecordingSlice *slice = &group->slices[i];
        status.nodes += slice->nodes;
        status.payload_charge += slice->charge;
        status.incomplete |= slice->incomplete;
        status.overflow |= slice->overflow;
        if (slice->omitted > UINT64_MAX - status.omitted) {
            status.omitted = UINT64_MAX;
            status.overflow = true;
        } else {
            status.omitted += slice->omitted;
        }
    }
    status.retained_bound = status.metadata_bound + status.payload_charge;
    return status;
}

static void prime_need_recording_budget_lock(PrimeNeedRecording *group) {
    while (atomic_flag_test_and_set_explicit(&group->budget_lock,
            memory_order_acquire)) {}
}

static void prime_need_recording_budget_unlock(PrimeNeedRecording *group) {
    atomic_flag_clear_explicit(&group->budget_lock, memory_order_release);
}

/* A parallel episode reuses one slice across steps that a serial history
 * would reserve separately. Grow this slice by its original quantum, taken
 * from the group budget the caller already configured. */
static bool prime_need_recording_extend(PrimeNeedRecordingSlice *slice) {
    if (!slice || !slice->growing || !slice->group ||
        !slice->node_quantum || !slice->byte_quantum)
        return false;
    PrimeNeedRecording *group = slice->group;
    prime_need_recording_budget_lock(group);
    bool ok = slice->node_quantum <= group->limits.nodes - group->reserved_nodes &&
        slice->byte_quantum <= group->limits.payload_bytes - group->reserved_bytes &&
        slice->node_limit <= SIZE_MAX - slice->node_quantum &&
        slice->byte_limit <= SIZE_MAX - slice->byte_quantum;
    if (ok) {
        group->reserved_nodes += slice->node_quantum;
        group->reserved_bytes += slice->byte_quantum;
        slice->node_limit += slice->node_quantum;
        slice->byte_limit += slice->byte_quantum;
    }
    prime_need_recording_budget_unlock(group);
    return ok;
}

static bool prime_need_recording_reserve_mode(PrimeNeedRecording *group,
    const PrimeNeedReceipt *seed, size_t nodes, size_t bytes,
    bool growing, PrimeNeedReceipt *out) {
    if (!group || !out || group->sealed ||
        !atomic_load_explicit(&group->payload_references, memory_order_acquire) ||
        (seed && prime_need_receipt_present(seed) &&
         (!seed->recording || seed->recording->group != group)))
        return false;
    prime_need_recording_budget_lock(group);
    bool ok = group->slice_count < group->limits.slices &&
        nodes <= group->limits.nodes - group->reserved_nodes &&
        bytes <= group->limits.payload_bytes - group->reserved_bytes;
    PrimeNeedRecordingSlice *slice = NULL;
    if (ok) {
        slice = &group->slices[group->slice_count++];
        slice->group = group;
        slice->node_quantum = nodes;
        slice->byte_quantum = bytes;
        slice->growing = growing;
        slice->node_limit = nodes;
        slice->byte_limit = bytes;
        group->reserved_nodes += nodes;
        group->reserved_bytes += bytes;
    }
    if (!ok) {
        prime_need_recording_budget_unlock(group);
        return false;
    }
    atomic_init(&slice->omitted, 0u);
    atomic_init(&slice->incomplete, false);
    atomic_init(&slice->overflow, false);
    arena_init_detached(&slice->arena);
    arena_set_block_capacity(&slice->arena, PRIME_NEED_RECORDING_BLOCK_BYTES);
    prime_need_recording_budget_unlock(group);
    *out = (PrimeNeedReceipt){
        .top = seed ? seed->top : NULL,
        .session_id = group->session_id,
        .owner = &slice->arena,
        .recording = slice,
    };
    return true;
}

bool prime_need_recording_reserve(PrimeNeedRecording *group,
    const PrimeNeedReceipt *seed, size_t nodes, size_t bytes, PrimeNeedReceipt *out) {
    return prime_need_recording_reserve_mode(group, seed, nodes, bytes, false, out);
}

bool prime_need_recording_reserve_growing(PrimeNeedRecording *group,
    const PrimeNeedReceipt *seed, size_t nodes, size_t bytes, PrimeNeedReceipt *out) {
    return prime_need_recording_reserve_mode(group, seed, nodes, bytes, true, out);
}

static bool prime_need_recording_promote_mode(Arena *dst,
    PrimeNeedReceipt *receipt, bool reader) {
    if (!dst || !receipt || !receipt->recording)
        return false;
    if (!prime_need_recording_readable(receipt)) {
        if (!prime_need_recording_erased(receipt)) return false;
        prime_need_receipt_init(receipt);
        return true;
    }
    PrimeNeedRecordingSlice *slice = receipt->recording;
    PrimeNeedRecording *group = slice->group;
    for (size_t i = 0u; i < group->slice_count; i++)
        if (dst == &group->slices[i].arena) {
            receipt->owner = &slice->arena;
            return true;
        }
    reader |= receipt->owner != &slice->arena;
    void (*release)(void *) = reader ? prime_need_recording_release
                                    : prime_need_recording_carrier_release;
    if (arena_retains_owner(dst, group, release)) {
        if (reader) receipt->owner = dst;
        return true;
    }
    size_t links = atomic_load_explicit(&group->owner_links, memory_order_relaxed);
    for (;;) {
        if (links == group->limits.owner_links) {
            prime_need_recording_omit(slice);
            /* This is loss of optional evidence, not failure of promotion
             * of the computation. Other copies of both roots remain valid. */
            prime_need_receipt_init(receipt);
            return true;
        }
        if (atomic_compare_exchange_weak_explicit(&group->owner_links,
                &links, links + 1u, memory_order_relaxed, memory_order_relaxed))
            break;
    }
    if (!arena_retain_owner_with_storage(dst, group,
            reader ? prime_need_recording_retain : prime_need_recording_carrier_retain,
            release, reader ? prime_need_recording_storage : prime_need_recording_carrier_storage))
        return false;
    if (reader) receipt->owner = dst;
    return true;
}

bool prime_need_recording_promote(Arena *dst, PrimeNeedReceipt *receipt) {
    return prime_need_recording_promote_mode(dst, receipt, true);
}

bool prime_need_recording_promote_carrier(Arena *dst, PrimeNeedReceipt *receipt) {
    return prime_need_recording_promote_mode(dst, receipt, false);
}

/* Bound each possible arena allocation by its aligned request plus one
 * complete block of slack and its header. This deliberately overcounts a
 * shared block. The copy service below is used only after this check. */
static bool prime_need_recording_allocation_charge(
    size_t bytes, size_t limit, size_t *charge) {
    size_t overhead = PRIME_NEED_RECORDING_BLOCK_BYTES +
        sizeof(ArenaBlock) - ARENA_BLOCK_SIZE + 7u;
    if (bytes > SIZE_MAX - overhead || bytes + overhead > limit - *charge)
        return false;
    *charge += bytes + overhead;
    return true;
}

static bool prime_need_recording_payload_charge(
    Atom *before, Atom *after, size_t limit, size_t *charge) {
    Atom **pending = NULL;
    size_t count = 0u, capacity = 0u;
    bool ok = false;
    Atom *roots[] = {before, after};
    size_t maximum = limit / sizeof(Atom);
    for (size_t root = 0u; root < 2u; root++) {
        if (!roots[root])
            continue;
        if (!maximum)
            goto done;
        if (!capacity) {
            capacity = maximum < 64u ? maximum : 64u;
            pending = malloc(capacity * sizeof(*pending));
            if (!pending)
                goto done;
        }
        pending[count++] = roots[root];
        while (count) {
            Atom *atom = pending[--count];
            cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE, 1u);
            if (!atom || !prime_need_recording_allocation_charge(
                    sizeof(Atom), limit, charge))
                goto done;
            if (atom->kind == ATOM_SYMBOL)
                continue;
            if (atom->kind == ATOM_VAR) {
                if (atom->name_key || var_id_is_none(atom->var_id) ||
                    !cetta_frame_identity_is_none(var_epoch_suffix(atom->var_id)))
                    goto done;
                continue;
            }
            if (atom->kind == ATOM_GROUNDED) {
                switch (atom->ground.gkind) {
                case GV_INT:
                    if (atom->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID)
                        goto done;
                    break;
                case GV_FLOAT: case GV_BOOL:
                    break;
                case GV_INTERNAL_TAG:
                    if (atom->ground.ival <= 0 ||
                        atom->ground.ival > CETTA_INTERNAL_TAG_PRIME_CODE_BINDER)
                        goto done;
                    break;
                case GV_PRIME_NEED_CAPABILITY:
                    /* The recorded value is an immutable scalar capability,
                     * not a new suspension or permission to execute one. Its
                     * complete copy uses one additional arena allocation. */
                    if (!prime_need_ref_parse_rights(atom, NULL, NULL, NULL, NULL) ||
                        !prime_need_recording_allocation_charge(
                            sizeof(CettaPrimeNeedCapability), limit, charge))
                        goto done;
                    break;
                case GV_STRING:
                    if (!prime_need_recording_allocation_charge(
                            (size_t)atom->ground.slen + 1u, limit, charge))
                        goto done;
                    break;
                default:
                    goto done;
                }
                continue;
            }
            if (atom->kind != ATOM_EXPR ||
                (atom->structural_facts & ATOM_STRUCTURAL_FRONT_SLACK) ||
                atom->expr.len > maximum - count ||
                atom->expr.len > SIZE_MAX / sizeof(Atom *))
                goto done;
            size_t length = (size_t)atom->expr.len;
            size_t bytes = length * sizeof(Atom *);
            /* Canonical deep copy retains its temporary child vector and
             * the final expression vector. DAG sharing only reduces this. */
            if (!prime_need_recording_allocation_charge(bytes, limit, charge) ||
                !prime_need_recording_allocation_charge(bytes, limit, charge))
                goto done;
            if (length > capacity - count) {
                size_t next = capacity;
                while (next < count + length)
                    next = next > maximum / 2u ? maximum : next * 2u;
                Atom **grown = realloc(pending, next * sizeof(*pending));
                if (!grown)
                    goto done;
                pending = grown;
                capacity = next;
            }
            for (size_t i = 0u; i < length; i++)
                pending[count++] = atom->expr.elems[i];
        }
    }
    ok = true;
done:
    free(pending);
    return ok;
}

static bool prime_need_recording_event_fits(const PrimeNeedRecordingSlice *slice,
    PrimeNeedReceiptEvent event, size_t *charge_out) {
    size_t charge = 0u;
    if (slice->nodes >= slice->node_limit || slice->charge > slice->byte_limit)
        return false;
    if (!prime_need_recording_allocation_charge(sizeof(PrimeNeedReceiptFrame),
            slice->byte_limit - slice->charge, &charge) ||
        !prime_need_recording_payload_charge(event.before, event.after,
            slice->byte_limit - slice->charge, &charge))
        return false;
    *charge_out = charge;
    return true;
}

static bool prime_need_recording_append(const PrimeNeedReceipt *base,
    PrimeNeedReceiptEvent event, const PrimeNeedReceiptFrame *right,
    bool has_event, PrimeNeedReceipt *out) {
    PrimeNeedRecordingSlice *slice = base->recording;
    *out = *base;
    if (slice->group->sealed || base->owner != &slice->arena) return true;
    size_t charge = 0u;
    if (slice->incomplete || event.state_cell) {
        prime_need_recording_omit(slice);
        return true;
    }
    while (!prime_need_recording_event_fits(slice, event, &charge)) {
        if (!prime_need_recording_extend(slice)) {
            prime_need_recording_omit(slice);
            return true;
        }
    }
    uint64_t identity = prime_need_fresh_nonzero(&g_prime_need_next_receipt_node);
    if (!identity) {
        prime_need_recording_omit(slice);
        return true;
    }
    slice->charge += charge;
    Atom *before = event.before ? atom_deep_copy(&slice->arena, event.before) : NULL;
    Atom *after = event.after ? atom_deep_copy(&slice->arena, event.after) : NULL;
    if ((event.before && !before) || (event.after && !after)) {
        prime_need_recording_omit(slice);
        return true;
    }
    PrimeNeedReceiptFrame *frame = prime_need_receipt_alloc_frame(&slice->arena);
    *frame = (PrimeNeedReceiptFrame){
        .left = base->top,
        .right = right,
        .owner = &slice->arena,
        .session_id = base->session_id,
        .identity_id = identity,
        .event_id = has_event ? identity : 0u,
        .depth = 1u + (base->top ? base->top->depth : 0u),
        .has_event = has_event,
        .event = event,
    };
    if (right && right->depth >= frame->depth)
        frame->depth = right->depth + 1u;
    frame->event.event_id = frame->event_id;
    frame->event.before = before;
    frame->event.after = after;
    prime_need_receipt_refresh_arena_summary(frame);
    if (slice->last) slice->last->recording_next = frame;
    else slice->first = frame;
    slice->last = frame;
    if (base->top && base->top->owner == &slice->arena)
        ((PrimeNeedReceiptFrame *)base->top)->recording_referenced = true;
    if (right && right->owner == &slice->arena)
        ((PrimeNeedReceiptFrame *)right)->recording_referenced = true;
    slice->nodes++;
    out->top = frame;
    cetta_native_cost_charge(CETTA_COST_RECEIPT_APPEND, 1u);
    return true;
}

static bool prime_need_recording_publication_change(PrimeNeedRecordingSlice *slice) {
    if (slice->group->publication_revision == UINT64_MAX) {
        slice->overflow = true;
        prime_need_recording_omit(slice);
        return false;
    }
    slice->group->publication_revision++;
    return true;
}

static bool prime_need_recording_publish_root(PrimeNeedRecordingSlice *to,
    const PrimeNeedReceipt *active, const PrimeNeedReceiptFrame *root) {
    PrimeNeedReceipt joined = *active;
    joined.top = to->published_root ? to->published_root : to->last;
    if (joined.top && prime_need_receipt_reachable(joined.top, root))
        return true;
    if (!joined.top || prime_need_receipt_reachable(root, joined.top)) {
        if (!prime_need_recording_publication_change(to)) return false;
        to->published_root = root;
        return true;
    }
    const PrimeNeedReceiptFrame *previous = joined.top;
    if (!prime_need_recording_publication_change(to)) return false;
    if (!prime_need_recording_append(&joined, (PrimeNeedReceiptEvent){0},
            root, false, &joined) || joined.top == previous)
        return false;
    to->published_root = joined.top;
    return true;
}

/* After workers join, retain their roots, joining nonempty frontiers through
 * budgeted DAG nodes. Each
 * slice keeps its allocation inventory, arena, charges and writable quota.
 * A failed join leaves the source root available and reports an omission. */
void prime_need_recording_publish(PrimeNeedReceipt *src) {
    const PrimeNeedReceipt *active = g_prime_need_recording_scope;
    if (!src || !src->recording || !active || !active->recording ||
        active->recording == src->recording || !src->recording->group ||
        src->recording->group->sealed ||
        active->recording->group != src->recording->group)
        return;
    PrimeNeedRecordingSlice *to = active->recording;
    if (src->recording->published_root)
        (void)prime_need_recording_publish_root(to, active,
            src->recording->published_root);
    for (PrimeNeedReceiptFrame *frame = src->recording->first; frame;
         frame = frame->recording_next) {
        if (frame->recording_referenced) continue;
        if (prime_need_recording_publish_root(to, active, frame) &&
            prime_need_recording_publication_change(to))
            frame->recording_referenced = true;
    }
}

bool prime_need_recording_merge(PrimeNeedReceipt *dst, const PrimeNeedReceipt *src) {
    if (!dst)
        return false;
    if (dst->recording && !prime_need_recording_readable(dst)) {
        if (!prime_need_recording_erased(dst)) return false;
        prime_need_receipt_init(dst);
    }
    if (src && src->recording && !prime_need_recording_readable(src) &&
        !prime_need_recording_erased(src)) return false;
    if (!src || !prime_need_receipt_present(src))
        return true;
    if (!prime_need_receipt_present(dst)) {
        *dst = *src;
        return true;
    }
    PrimeNeedRecordingSlice *slice = dst->recording ? dst->recording : src->recording;
    if (!dst->recording || !src->recording ||
        dst->recording->group != src->recording->group) {
        prime_need_recording_omit(slice);
        return true;
    }
    if (dst->top == src->top || !src->top)
        return true;
    if (!dst->top) {
        dst->top = src->top;
        return true;
    }
    /* Neither a prefix nor its omissions supplies semantic compatibility.
     * The operational Need snapshot and branch-state carrier decide it. */
    return prime_need_recording_append(dst, (PrimeNeedReceiptEvent){0},
        src->top, false, dst);
}
#endif

static bool prime_need_state_join_nodes(Arena *owner, uint64_t session,
    const PrimeNeedReceiptFrame *left, const PrimeNeedReceiptFrame *right,
    const PrimeNeedReceiptFrame **out) {
    if (!left || !right || left->depth == UINT64_MAX || right->depth == UINT64_MAX)
        return false;
    uint64_t identity_id = prime_need_fresh_nonzero(
        &g_prime_need_next_receipt_node);
    if (identity_id == 0u)
        return false;
    PrimeNeedReceiptFrame *join = prime_need_receipt_alloc_frame(owner);
    if (!join)
        return false;
    *join = (PrimeNeedReceiptFrame){
        .left = left,
        .right = right,
        .owner = owner,
        .session_id = session,
        .identity_id = identity_id,
        .event_id = 0u,
        .depth = (left->depth > right->depth
                      ? left->depth : right->depth) + 1u,
        .has_event = false,
    };
    prime_need_receipt_refresh_arena_summary(join);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    prime_need_receipt_build_primary_index(owner, join);
#endif
    *out = join;
    return true;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
CettaMatchCompletion prime_need_receipt_join_qualified(
        Arena *owner, PrimeNeedReceipt *dst, const PrimeNeedReceipt *src) {
    if (!prime_need_owner_admissible(owner) || !dst)
        return CETTA_MATCH_INCOMPLETE;
    if (!src) return CETTA_MATCH_MATCHED;
    PrimeNeedReceipt left=*dst,right=*src;
    bool scoped=g_prime_need_recording_scope&&g_prime_need_recording_scope->recording&&
        (!left.recording || prime_need_recording_same_group(&left,g_prime_need_recording_scope));
    PrimeNeedRecordingSlice *target=scoped
        ? g_prime_need_recording_scope->recording : left.recording;
    if (right.recording && !prime_need_recording_readable(&right)) {
        if (!prime_need_recording_erased(&right)) return CETTA_MATCH_INCOMPLETE;
        if (target) prime_need_recording_omit(target);
        return CETTA_MATCH_MATCHED;
    }
    if (target && right.recording && target->group!=right.recording->group) {
        prime_need_recording_omit(target);
        return CETTA_MATCH_MATCHED;
    }
    bool merged=true;
    if (scoped)
        merged=prime_need_recording_merge_scoped(&left,&right);
    else if (left.recording || right.recording)
        merged=prime_need_recording_merge(&left,&right);
    else {
        bool left_present=prime_need_receipt_present(&left);
        bool right_present=prime_need_receipt_present(&right);
        if (left_present && right_present && left.session_id!=right.session_id)
            return CETTA_MATCH_REFUTED;
        if (left_present && !prime_need_receipt_promote(owner,&left))
            return CETTA_MATCH_INCOMPLETE;
        if (right_present && !prime_need_receipt_promote(owner,&right))
            return CETTA_MATCH_INCOMPLETE;
        if (!right_present) {
            *dst=left;return CETTA_MATCH_MATCHED;
        }
        if (!left_present || !prime_need_receipt_has_events(&left)) left=right;
        else if (!prime_need_receipt_has_events(&right) ||
                prime_need_branch_state_is_ancestor(&right,&left)) {}
        else if (prime_need_branch_state_is_ancestor(&left,&right)) left=right;
        else {
            const PrimeNeedReceiptFrame *join=NULL;
            merged=prime_need_state_join_nodes(owner,left.session_id,left.top,right.top,&join);
            if (merged) {left.top=join;left.owner=owner;}
        }
    }
    if (!merged || !prime_need_receipt_promote(owner,&left))
        return CETTA_MATCH_INCOMPLETE;
    *dst=left;
    return CETTA_MATCH_MATCHED;
}
#endif

bool prime_need_branch_state_merge_nonempty(
    PrimeNeedBranchState *dst, const PrimeNeedBranchState *src) {
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_MERGE);
    if (!dst || !src || !prime_need_branch_state_has_events(src))
        return false;
    if (!prime_need_branch_state_present(dst)) {
        *dst = *src;
        return true;
    }
    if (dst->session_id == src->session_id && dst->top == src->top) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_MERGE_IDENTICAL);
        *dst = *src;
        return true;
    }
    if (prime_need_branch_state_is_ancestor(dst, src)) {
        *dst = *src;
        return true;
    }
    if (prime_need_branch_state_is_ancestor(src, dst))
        return true;
    if (!prime_need_branch_state_compatible(dst, src))
        return false;
    Arena *owner = dst->owner ? dst->owner : src->owner;
    const PrimeNeedReceiptFrame *join = NULL;
    if (!prime_need_state_join_nodes(owner, dst->session_id, dst->top, src->top, &join))
        return false;
    dst->top = join;
    dst->owner = owner;
    return true;
}

static bool prime_need_receipt_append_with_lifetime(
    Arena *owner, const PrimeNeedReceipt *base,
    PrimeNeedReceiptEvent event, bool branch_local,
    PrimeNeedReceipt *out) {
    if (!out)
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (base && base->recording)
        return prime_need_recording_append(base, event, NULL, true, out);
#endif
    Arena *actual_owner = branch_local && owner
        ? owner : base && base->owner ? base->owner : owner;
    if (!actual_owner)
        return false;
    uint64_t session_id = base && prime_need_branch_state_present(base)
        ? base->session_id
        : prime_need_fresh_nonzero(&g_prime_need_next_receipt_session);
    uint64_t event_id = prime_need_fresh_nonzero(
        &g_prime_need_next_receipt_node);
    if (session_id == 0u || event_id == 0u)
        return false;
    Atom *before = event.before
        ? atom_deep_copy(actual_owner, event.before) : NULL;
    Atom *after = event.after
        ? atom_deep_copy(actual_owner, event.after) : NULL;
    if ((event.before && !before) || (event.after && !after))
        return false;
    PrimeNeedReceiptFrame *frame = prime_need_receipt_alloc_frame(actual_owner);
    if (!frame)
        return false;
    event.event_id = event_id;
    event.before = before;
    event.after = after;
    *frame = (PrimeNeedReceiptFrame){
        .left = base ? base->top : NULL,
        .right = NULL,
        .owner = actual_owner,
        .session_id = session_id,
        .identity_id = event_id,
        .event_id = event_id,
        .depth = base && base->top ? base->top->depth + 1u : 1u,
        .has_event = true,
        .event = event,
    };
    cetta_native_cost_charge(CETTA_COST_RECEIPT_APPEND, 1u);
    prime_need_receipt_refresh_arena_summary(frame);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    prime_need_receipt_build_primary_index(actual_owner, frame);
#endif
    *out = (PrimeNeedReceipt){
        .top = frame,
        .session_id = session_id,
        .owner = actual_owner,
    };
#if CETTA_BUILD_WITH_RUNTIME_STATS && !defined(CETTA_RUNTIME_STATS_NOOP)
    CettaRuntimeCounter event_counter =
        CETTA_RUNTIME_COUNTER_PRIME_NEED_RECEIPT_EVENT_OBSERVE_CELL;
    switch (event.kind) {
    case PRIME_NEED_RECEIPT_OBSERVE_CELL:
        break;
    case PRIME_NEED_RECEIPT_INSPECT_ORIGIN:
        event_counter =
            CETTA_RUNTIME_COUNTER_PRIME_NEED_RECEIPT_EVENT_INSPECT_ORIGIN;
        break;
    case PRIME_NEED_RECEIPT_READ_STATE:
        event_counter =
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_EVENT_READ_STATE;
        break;
    case PRIME_NEED_RECEIPT_WRITE_STATE:
        event_counter =
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_EVENT_WRITE_STATE;
        break;
    case PRIME_NEED_RECEIPT_USE_EQUATION:
        event_counter =
            CETTA_RUNTIME_COUNTER_PRIME_NEED_RECEIPT_EVENT_USE_EQUATION;
        break;
    case PRIME_NEED_RECEIPT_RESAMPLE:
        event_counter =
            CETTA_RUNTIME_COUNTER_PRIME_NEED_RECEIPT_EVENT_RESAMPLE;
        break;
    case PRIME_NEED_RECEIPT_ADMISSION_TRUE:
    case PRIME_NEED_RECEIPT_ADMISSION_FALSE:
    case PRIME_NEED_RECEIPT_ADMISSION_PENDING:
        /* The legacy counters above classify cell, state and equation
         * events. Admission is already charged as a receipt append. */
        return true;
    }
    cetta_runtime_stats_inc(event_counter);
#endif
    return true;
}

static bool prime_need_receipt_append(
    Arena *owner, const PrimeNeedReceipt *base,
    PrimeNeedReceiptEvent event, PrimeNeedReceipt *out) {
    return prime_need_receipt_append_with_lifetime(
        owner, base, event, false, out);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static bool prime_need_observed_append(Arena *owner,
    const PrimeNeedReceipt *base, PrimeNeedReceiptEvent event,
    bool branch_local, PrimeNeedReceipt *out) {
    PrimeNeedReceipt scoped;
    if (g_prime_need_recording_scope) {
        if (!prime_need_recording_scoped(base, &scoped)) {
            if (!out) return false;
            *out = scoped;
            return true;
        }
        base = &scoped;
    }
    return prime_need_receipt_append_with_lifetime(
        owner, base, event, branch_local, out);
}

bool prime_need_receipt_observe_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *outcome,
    PrimeNeedReceipt *out) {
    return prime_need_receipt_observe_source_cell(
        owner, base, need_session_id, thunk_id, 0u, 0u, outcome, out);
}

static bool prime_need_receipt_observe_source_cell_event(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *outcome, bool branch_local, PrimeNeedReceipt *out) {
    if (need_session_id == 0u || thunk_id == 0u || !outcome)
        return false;
    return prime_need_observed_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_OBSERVE_CELL,
            .need_session_id = need_session_id,
            .thunk_id = thunk_id,
            .source_occurrence_id = source_occurrence_id,
            .source_argument_index = source_argument_index,
            .after = outcome,
        },
        branch_local,
        out);
}

bool prime_need_receipt_observe_source_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *outcome, PrimeNeedReceipt *out) {
    return prime_need_receipt_observe_source_cell_event(
        owner, base, need_session_id, thunk_id,
        source_occurrence_id, source_argument_index,
        outcome, false, out);
}

bool prime_need_receipt_observe_source_cell_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    Atom *outcome, PrimeNeedReceipt *out) {
    if (!owner)
        return false;
    return prime_need_receipt_observe_source_cell_event(
        owner, base, need_session_id, thunk_id,
        source_occurrence_id, source_argument_index,
        outcome, true, out);
}

bool prime_need_receipt_observe_cached_cell(
    Arena *owner, const PrimeNeedReceipt *base,
    const PrimeNeedSnapshot *snapshot, uint64_t thunk_id,
    bool completed, bool branch_local, Atom *outcome, PrimeNeedReceipt *out) {
    if (!outcome || (branch_local && !owner))
        return false;
    PrimeNeedCellView cell;
    const PrimeNeedFrame *frame = NULL;
    if (!prime_need_snapshot_lookup_frame(snapshot, thunk_id, &cell, &frame) ||
        !prime_need_frame_completed(frame))
        return false;
    return prime_need_observed_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_OBSERVE_CELL,
            .need_session_id = frame->session_id,
            .thunk_id = thunk_id,
            .source_occurrence_id = frame->source_occurrence_id,
            .source_argument_index = frame->source_argument_index,
            .producer_update_id = frame->update_id,
            .producer_completed = completed,
            .after = outcome,
        }, branch_local, out);
}

static bool prime_need_receipt_inspect_origin_event(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *origin_view,
    bool branch_local, PrimeNeedReceipt *out) {
    if (need_session_id == 0u || thunk_id == 0u || !origin_view)
        return false;
    return prime_need_observed_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_INSPECT_ORIGIN,
            .need_session_id = need_session_id,
            .thunk_id = thunk_id,
            .after = origin_view,
        },
        branch_local,
        out);
}

bool prime_need_receipt_inspect_origin(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *origin_view,
    PrimeNeedReceipt *out) {
    return prime_need_receipt_inspect_origin_event(
        owner, base, need_session_id, thunk_id, origin_view,
        false, out);
}

bool prime_need_receipt_inspect_origin_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *origin_view,
    PrimeNeedReceipt *out) {
    if (!owner)
        return false;
    return prime_need_receipt_inspect_origin_event(
        owner, base, need_session_id, thunk_id, origin_view,
        true, out);
}
#endif

bool prime_need_branch_state_read(
    Arena *owner, const PrimeNeedBranchState *base, StateCell *cell,
    Atom *value, PrimeNeedBranchState *out) {
    if (!cell || !value)
        return false;
    return prime_need_receipt_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_READ_STATE,
            .state_cell = cell,
            .after = value,
        },
        out);
}

bool prime_need_branch_state_write(
    Arena *owner, const PrimeNeedBranchState *base, StateCell *cell,
    Atom *before, Atom *after, PrimeNeedBranchState *out) {
    if (!cell || !before || !after)
        return false;
    return prime_need_receipt_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_WRITE_STATE,
            .state_cell = cell,
            .before = before,
            .after = after,
        },
        out);
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
bool prime_need_receipt_read_state(
    Arena *owner, const PrimeNeedReceipt *base, StateCell *cell,
    Atom *value, PrimeNeedReceipt *out) {
    if (!cell || !value) return false;
    return prime_need_observed_append(owner, base,
        (PrimeNeedReceiptEvent){.kind = PRIME_NEED_RECEIPT_READ_STATE,
            .state_cell = cell, .after = value}, false, out);
}

bool prime_need_receipt_write_state(
    Arena *owner, const PrimeNeedReceipt *base, StateCell *cell,
    Atom *before, Atom *after, PrimeNeedReceipt *out) {
    if (!cell || !before || !after) return false;
    return prime_need_observed_append(owner, base,
        (PrimeNeedReceiptEvent){.kind = PRIME_NEED_RECEIPT_WRITE_STATE,
            .state_cell = cell, .before = before, .after = after}, false, out);
}

bool prime_need_receipt_record_state(Arena *owner,const PrimeNeedReceipt *base,
        const PrimeNeedBranchState *operation,PrimeNeedReceipt *out) {
    if (!out || !operation || !operation->session_id || operation->recording ||
        !operation->top) return false;
    const PrimeNeedReceiptFrame *frame=operation->top;
    const PrimeNeedReceiptEvent *event=&frame->event;
    if (!frame->has_event || frame->session_id!=operation->session_id ||
        !frame->event_id || frame->event_id!=event->event_id ||
        !event->state_cell || !event->after ||
        (event->kind!=PRIME_NEED_RECEIPT_READ_STATE &&
         event->kind!=PRIME_NEED_RECEIPT_WRITE_STATE) ||
        (event->kind==PRIME_NEED_RECEIPT_WRITE_STATE && !event->before))
        return false;
    return prime_need_observed_append(owner,base,
        (PrimeNeedReceiptEvent){.kind=event->kind,
            .state_event_id=event->event_id,.state_cell=event->state_cell,
            .before=event->before,.after=event->after},false,out);
}

static bool prime_need_receipt_use_equation_event(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    Atom *equation, Atom *result, bool branch_local,
    PrimeNeedReceipt *out) {
    if (source_occurrence_id == 0u || rule_occurrence_id == 0u ||
        (equation == NULL) != (result == NULL))
        return false;
    return prime_need_observed_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_USE_EQUATION,
            .source_occurrence_id = source_occurrence_id,
            .rule_occurrence_id = rule_occurrence_id,
            .before = equation,
            .after = result,
        },
        branch_local,
        out);
}

bool prime_need_receipt_use_equation(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    Atom *equation, Atom *result, PrimeNeedReceipt *out) {
    if (!equation || !result)
        return false;
    return prime_need_receipt_use_equation_event(
        owner, base, source_occurrence_id, rule_occurrence_id,
        equation, result, false, out);
}

bool prime_need_receipt_use_equation_ids(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    PrimeNeedReceipt *out) {
    return prime_need_receipt_use_equation_event(
        owner, base, source_occurrence_id, rule_occurrence_id,
        NULL, NULL, false, out);
}

bool prime_need_receipt_use_equation_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    Atom *equation, Atom *result, PrimeNeedReceipt *out) {
    if (!owner || !equation || !result)
        return false;
    return prime_need_receipt_use_equation_event(
        owner, base, source_occurrence_id, rule_occurrence_id,
        equation, result, true, out);
}

bool prime_need_receipt_use_equation_ids_branch_local(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    PrimeNeedReceipt *out) {
    if (!owner)
        return false;
    return prime_need_receipt_use_equation_event(
        owner, base, source_occurrence_id, rule_occurrence_id,
        NULL, NULL, true, out);
}

bool prime_need_receipt_admission(
    Arena *owner, const PrimeNeedReceipt *base,
    PrimeNeedReceiptEventKind kind, uint64_t dependency,
    Atom *coefficient, Atom *verdict, PrimeNeedReceipt *out) {
    if ((kind != PRIME_NEED_RECEIPT_ADMISSION_TRUE &&
         kind != PRIME_NEED_RECEIPT_ADMISSION_FALSE &&
         kind != PRIME_NEED_RECEIPT_ADMISSION_PENDING) ||
        !coefficient || !verdict)
        return false;
    return prime_need_observed_append(owner, base,
        (PrimeNeedReceiptEvent){
            .kind = kind,
            .source_occurrence_id = dependency,
            .before = coefficient,
            .after = verdict,
        }, false, out);
}

static bool prime_need_receipt_resample_event(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    bool branch_local, PrimeNeedReceipt *out) {
    if (!origin)
        return false;
    return prime_need_observed_append(
        owner, base,
        (PrimeNeedReceiptEvent){
            .kind = PRIME_NEED_RECEIPT_RESAMPLE,
            .before = origin,
        },
        branch_local,
        out);
}

bool prime_need_receipt_resample(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    PrimeNeedReceipt *out) {
    return prime_need_receipt_resample_event(
        owner, base, origin, false, out);
}

bool prime_need_receipt_resample_branch_local(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    PrimeNeedReceipt *out) {
    if (!owner)
        return false;
    return prime_need_receipt_resample_event(
        owner, base, origin, true, out);
}
#endif

CettaMatchCompletion prime_need_branch_state_lookup_version_checked(
    const PrimeNeedBranchState *state, StateCell *cell,
    Atom **out_value, uint64_t *out_write_event_id) {
    if (!state || !cell || !out_value || !out_write_event_id) return CETTA_MATCH_INCOMPLETE;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (state->recording) return CETTA_MATCH_INCOMPLETE;
#endif
    if (!prime_need_branch_state_present(state)) {
        if (state->top || state->owner || state->session_id) return CETTA_MATCH_INCOMPLETE;
        *out_value = NULL;
        *out_write_event_id = 0u;
        return CETTA_MATCH_MATCHED;
    }
    PrimeNeedStateInventory inventory = {0};
    CettaMatchCompletion result = prime_need_state_inventory(state,NULL,&inventory);
    for (size_t i = 0u; result == CETTA_MATCH_MATCHED && i < inventory.event_count; i++)
        if (inventory.events[i]->event.kind != PRIME_NEED_RECEIPT_READ_STATE &&
            inventory.events[i]->event.kind != PRIME_NEED_RECEIPT_WRITE_STATE)
            result = CETTA_MATCH_INCOMPLETE;
    if (result == CETTA_MATCH_MATCHED)
        result = prime_need_state_inventory_version(&inventory,cell,out_value,out_write_event_id);
    prime_need_state_inventory_free(&inventory);
    return result;
}

CettaMatchCompletion prime_need_branch_state_holds_write_checked(
        const PrimeNeedBranchState *state,StateCell *cell,
        uint64_t write_event_id,bool *out_held) {
    if (!state||!cell||!write_event_id||!out_held) return CETTA_MATCH_INCOMPLETE;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (state->recording) return CETTA_MATCH_INCOMPLETE;
#endif
    if (!prime_need_branch_state_present(state)) {
        if (state->top||state->owner||state->session_id) return CETTA_MATCH_INCOMPLETE;
        *out_held=false;return CETTA_MATCH_MATCHED;
    }
    PrimeNeedStateInventory inventory={0};
    CettaMatchCompletion result=prime_need_state_inventory(state,NULL,&inventory);
    size_t at=result==CETTA_MATCH_MATCHED
        ? prime_need_state_event_position(&inventory,write_event_id) : SIZE_MAX;
    if (result==CETTA_MATCH_MATCHED && at!=SIZE_MAX) {
        const PrimeNeedReceiptEvent *event=&inventory.events[at]->event;
        if (event->kind!=PRIME_NEED_RECEIPT_WRITE_STATE||event->state_cell!=cell)
            result=CETTA_MATCH_REFUTED;
    }
    if (result==CETTA_MATCH_MATCHED) *out_held=at!=SIZE_MAX;
    prime_need_state_inventory_free(&inventory);
    return result;
}

CettaMatchCompletion prime_need_branch_state_lookup_checked(const PrimeNeedBranchState *state,
    StateCell *cell, Atom **out_value) {
    if (!state || !cell || !out_value) return CETTA_MATCH_INCOMPLETE;
    if (!prime_need_branch_state_present(state)) {
        if (state->top) return CETTA_MATCH_INCOMPLETE;
        *out_value = NULL;
        return CETTA_MATCH_MATCHED;
    }
    PrimeNeedReceiptFrames frames = {0};
    if (!prime_need_receipt_collect_all(state->top, &frames))
        return CETTA_MATCH_INCOMPLETE;
    const PrimeNeedReceiptFrame *latest = NULL;
    bool rebased = false;
    for (size_t i = 0u; i < frames.len; i++) {
        const PrimeNeedReceiptFrame *frame = frames.items[i];
        if (!frame->has_event || frame->event.state_cell != cell ||
            (frame->event.kind != PRIME_NEED_RECEIPT_READ_STATE &&
             frame->event.kind != PRIME_NEED_RECEIPT_WRITE_STATE))
            continue;
        rebased |= frame->identity_id != frame->event_id;
        if (!latest || frame->depth > latest->depth ||
            (frame->depth == latest->depth &&
             frame->event_id > latest->event_id))
            latest = frame;
    }
    prime_need_receipt_frames_free(&frames);
    if (rebased) {
        PrimeNeedStateInventory inventory = {0};
        CettaMatchCompletion result = prime_need_state_inventory(state, NULL, &inventory);
        if (result == CETTA_MATCH_MATCHED)
            result = prime_need_state_inventory_value(&inventory, cell, out_value);
        prime_need_state_inventory_free(&inventory);
        return result;
    }
    if (latest && !latest->event.after) return CETTA_MATCH_INCOMPLETE;
    *out_value = latest ? latest->event.after : NULL;
    return CETTA_MATCH_MATCHED;
}

bool prime_need_branch_state_lookup(const PrimeNeedBranchState *state,
    StateCell *cell, Atom **out_value) {
    return prime_need_branch_state_lookup_checked(state, cell, out_value) == CETTA_MATCH_MATCHED;
}

bool prime_need_branch_state_value(const PrimeNeedBranchState *state,
    StateCell *cell, Atom **out_value) {
    Atom *value = NULL;
    if (!out_value || !prime_need_branch_state_lookup(state,cell,&value) || !value)
        return false;
    *out_value = value;
    return true;
}

static CettaMatchCompletion prime_need_state_inventory_checked(
        const PrimeNeedBranchState *state,PrimeNeedStateInventory *inventory) {
    if(!state || !inventory || !prime_need_branch_state_present(state))
        return CETTA_MATCH_INCOMPLETE;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(state->recording) return CETTA_MATCH_INCOMPLETE;
#endif
    CettaMatchCompletion result=prime_need_state_inventory(state,NULL,inventory);
    for(size_t i=0u;result==CETTA_MATCH_MATCHED && i<inventory->event_count;i++)
        if(inventory->events[i]->event.kind!=PRIME_NEED_RECEIPT_READ_STATE &&
            inventory->events[i]->event.kind!=PRIME_NEED_RECEIPT_WRITE_STATE)
            result=CETTA_MATCH_INCOMPLETE;
    return result;
}

/* When production earned no State suffix, its requester contributes no State
 * constraint to delivery. Validate the receiver on its own terms, including
 * an exactly absent world or a well-formed world from another session. */
static CettaMatchCompletion prime_need_state_preserved_recipient_checked(
        const PrimeNeedBranchState *recipient) {
    if(!recipient) return CETTA_MATCH_INCOMPLETE;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(recipient->recording) return CETTA_MATCH_INCOMPLETE;
#endif
    if(!prime_need_branch_state_present(recipient))
        return recipient->top || recipient->session_id || recipient->owner
            ? CETTA_MATCH_INCOMPLETE : CETTA_MATCH_MATCHED;
    PrimeNeedStateInventory checked={0};
    CettaMatchCompletion result=prime_need_state_inventory_checked(recipient,&checked);
    prime_need_state_inventory_free(&checked);
    return result;
}

CettaMatchCompletion prime_need_branch_state_import_suffix(Arena *owner,
    const PrimeNeedBranchState *common, const PrimeNeedBranchState *entrance,
    const PrimeNeedBranchState *production, const PrimeNeedBranchState *recipient,
    PrimeNeedStateInitialValue initial_value, void *context, PrimeNeedBranchState *out) {
    if (!prime_need_owner_admissible(owner) || !common || !entrance || !production ||
        !recipient || !out) return CETTA_MATCH_INCOMPLETE;
    const PrimeNeedBranchState *states[] = {common, entrance, production, recipient};
    /* A producer that never entered a State world has no State suffix to
     * import. Preserve the receiver's world, including the absent world. */
    bool no_state_source = true;
    for (size_t i = 0u; i < 3u; i++) {
        no_state_source &= !states[i]->top && !states[i]->session_id && !states[i]->owner;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        no_state_source &= !states[i]->recording;
#endif
    }
    if (no_state_source) {
        CettaMatchCompletion valid=
            prime_need_state_preserved_recipient_checked(recipient);
        if(valid!=CETTA_MATCH_MATCHED) return valid;
        *out = *recipient;
        return CETTA_MATCH_MATCHED;
    }
    PrimeNeedStateInventory inv[4] = {0};
    PrimeNeedStateInventory *c = &inv[0], *e = &inv[1], *p = &inv[2], *r = &inv[3];
    CettaMatchCompletion result = CETTA_MATCH_INCOMPLETE;
    bool *missing = NULL, *source_events = NULL, *held = NULL;
    const PrimeNeedReceiptFrame **mapped = NULL;
    for (size_t i = 0u; i < 3u; i++) {
        result = CETTA_MATCH_INCOMPLETE;
        if(states[i]->session_id!=common->session_id) goto done;
        result=prime_need_state_inventory_checked(states[i],&inv[i]);
        if(result!=CETTA_MATCH_MATCHED) goto done;
    }
    result = prime_need_state_inventory_subset(c, e);
    if (result != CETTA_MATCH_MATCHED) goto done;
    result = prime_need_state_inventory_subset(e, p);
    if (result != CETTA_MATCH_MATCHED) goto done;
    /* A static domain may mention State that this row never observed. If the
     * producer earned no physical State node beyond its entrance, preserve the
     * recipient world without requiring the first requester's entrance branch
     * to be its ancestor. Actual read/write suffixes take the checks below. */
    result = prime_need_state_inventory_subset(p, e);
    if (result == CETTA_MATCH_MATCHED) {
        result=prime_need_state_preserved_recipient_checked(recipient);
        if(result==CETTA_MATCH_MATCHED) *out=*recipient;
        goto done;
    }
    if (result == CETTA_MATCH_INCOMPLETE) goto done;
    result=CETTA_MATCH_INCOMPLETE;
    if(recipient->session_id!=common->session_id) goto done;
    result=prime_need_state_inventory_checked(recipient,r);
    if(result!=CETTA_MATCH_MATCHED) goto done;
    result = prime_need_state_inventory_subset(c, r);
    if (result != CETTA_MATCH_MATCHED) goto done;
    size_t n = p->physical.len, k = p->event_count;
    result = CETTA_MATCH_INCOMPLETE;
    if (n > SIZE_MAX / sizeof(*mapped)) goto done;
    mapped = n ? calloc(n, sizeof(*mapped)) : NULL;
    missing = n ? calloc(n, sizeof(*missing)) : NULL;
    source_events = k ? calloc(k, sizeof(*source_events)) : NULL;
    held = k ? calloc(k, sizeof(*held)) : NULL;
    if ((n && (!mapped || !missing)) || (k && (!source_events || !held))) goto done;
    bool any_missing = false;
    for (size_t i = 0u; i < n; i++) {
        const PrimeNeedReceiptFrame *frame = p->physical.items[i];
        if (prime_need_state_node_position(e, frame->identity_id) != SIZE_MAX) {
            mapped[i] = recipient->top;
            continue;
        }
        const PrimeNeedReceiptFrame *parents[] = {frame->left, frame->right};
        for (size_t j = 0u; j < (frame->has_event ? 1u : 2u); j++) {
            if (!parents[j]) {
                if (entrance->top) {result = CETTA_MATCH_REFUTED; goto done;}
                continue;
            }
            size_t parent = prime_need_state_node_position(p, parents[j]->identity_id);
            if (parent >= i) {result = CETTA_MATCH_INCOMPLETE; goto done;}
            if (prime_need_state_node_position(e, parents[j]->identity_id) != SIZE_MAX &&
                (!entrance->top || parents[j]->identity_id != entrance->top->identity_id)) {
                /* A merge with an older side branch did not earn that
                 * branch after this force's actual entrance. */
                result = CETTA_MATCH_REFUTED; goto done;
            }
            missing[i] |= missing[parent];
        }
        if (!frame->has_event) continue;
        size_t event = prime_need_state_event_position(p, frame->event_id);
        source_events[event] = true;
        size_t recipient_event = prime_need_state_event_position(r, frame->event_id);
        held[event] = recipient_event != SIZE_MAX;
        if (held[event]) {
            result = prime_need_event_payload_compare(frame, r->events[recipient_event]);
            if (result != CETTA_MATCH_MATCHED) goto done;
            if (missing[i]) {result = CETTA_MATCH_REFUTED; goto done;}
        } else {missing[i] = true; any_missing = true;}
    }
    /* Reuse one canonical closure per relevant cell. Already delivered
     * events form an ideal, and strict order between its components survives.
     * Later own writes remain after the ideal; old reads are not re-executed. */
    for (size_t i = 0u; i < k; i++) if (source_events[i] && held[i]) {
        StateCell *cell = p->events[i]->event.state_cell;
        bool visited = false;
        for (size_t j = 0u; j < i; j++)
            visited |= source_events[j] && held[j] && p->events[j]->event.state_cell == cell;
        if (visited) continue;
        PrimeNeedStateOrder source_order = {0}, recipient_order = {0};
        result = prime_need_state_order(p,cell,&source_order);
        if (result == CETTA_MATCH_MATCHED) result = prime_need_state_order(r,cell,&recipient_order);
        Atom *ignored = NULL;
        if (result == CETTA_MATCH_MATCHED) result = prime_need_state_order_value(&source_order,&ignored);
        if (result == CETTA_MATCH_MATCHED) result = prime_need_state_order_value(&recipient_order,&ignored);
        for (size_t a = 0u; result == CETTA_MATCH_MATCHED && a < source_order.count; a++) {
            size_t ap = prime_need_state_event_position(p,source_order.events[a]->event_id);
            if (!source_events[ap] || !held[ap]) continue;
            size_t ar = prime_need_state_order_position(&recipient_order,source_order.events[a]->event_id);
            if (ar == SIZE_MAX) {result = CETTA_MATCH_REFUTED; break;}
            for (size_t b = 0u; b < source_order.count; b++) {
                if (!prime_need_state_order_after(&source_order,a,b) ||
                    prime_need_state_order_after(&source_order,b,a)) continue;
                size_t bp = prime_need_state_event_position(p,source_order.events[b]->event_id);
                if (!source_events[bp] || !held[bp]) continue;
                size_t br = prime_need_state_order_position(&recipient_order,source_order.events[b]->event_id);
                if (br == SIZE_MAX || !prime_need_state_order_after(&recipient_order,ar,br)) {
                    result = CETTA_MATCH_REFUTED; break;
                }
            }
        }
        prime_need_state_order_free(&source_order); prime_need_state_order_free(&recipient_order);
        if (result != CETTA_MATCH_MATCHED) goto done;
    }
    if (!any_missing) {*out = *recipient; result = CETTA_MATCH_MATCHED; goto done;}
    for (size_t i = 0u; i < k; i++) if (source_events[i]) {
        StateCell *cell = p->events[i]->event.state_cell;
        /* A pre-entrance write that supplies an observed value must already
         * be held. It cannot be smuggled into the recipient with the row. */
        for (size_t j = 0u; j < e->event_count; j++) {
            const PrimeNeedReceiptFrame *prior = e->events[j];
            if (prior->event.state_cell != cell || prior->event.kind != PRIME_NEED_RECEIPT_WRITE_STATE ||
                prime_need_state_event_position(c, prior->event_id) != SIZE_MAX) continue;
            size_t actual = prime_need_state_event_position(r, prior->event_id);
            if (actual == SIZE_MAX) {result = CETTA_MATCH_REFUTED; goto done;}
            result = prime_need_event_payload_compare(prior, r->events[actual]);
            if (result != CETTA_MATCH_MATCHED) goto done;
        }
        for (size_t j = 0u; j < r->event_count; j++) {
            const PrimeNeedReceiptFrame *other = r->events[j];
            if (other->event.state_cell != cell || other->event.kind != PRIME_NEED_RECEIPT_WRITE_STATE ||
                prime_need_state_event_position(c, other->event_id) != SIZE_MAX ||
                prime_need_state_event_position(e, other->event_id) != SIZE_MAX) continue;
            size_t produced = prime_need_state_event_position(p, other->event_id);
            if (produced != SIZE_MAX && source_events[produced]) continue;
            result = CETTA_MATCH_REFUTED; goto done;
        }
    }
    for (size_t i = 0u; i < n; i++) {
        const PrimeNeedReceiptFrame *frame = p->physical.items[i];
        if (prime_need_state_node_position(e, frame->identity_id) != SIZE_MAX) continue;
        size_t left_index = frame->left ? prime_need_state_node_position(p, frame->left->identity_id) : SIZE_MAX;
        size_t right_index = frame->right ? prime_need_state_node_position(p, frame->right->identity_id) : SIZE_MAX;
        const PrimeNeedReceiptFrame *left = left_index == SIZE_MAX ? recipient->top : mapped[left_index];
        const PrimeNeedReceiptFrame *right = right_index == SIZE_MAX ? recipient->top : mapped[right_index];
        if (!frame->has_event) {
            if (!left || !right || left->identity_id == right->identity_id) {
                mapped[i] = left ? left : right; continue;
            }
            PrimeNeedBranchState a = {.top=left,.session_id=common->session_id,.owner=left->owner};
            PrimeNeedBranchState b = {.top=right,.session_id=common->session_id,.owner=right->owner};
            PrimeNeedStateInventory joined = {0};
            result = prime_need_state_inventory(&a, &b, &joined);
            for (size_t j = 0u; result == CETTA_MATCH_MATCHED && j < k; j++) if (source_events[j]) {
                Atom *value = NULL;
                result = prime_need_state_inventory_value(&joined, p->events[j]->event.state_cell, &value);
            }
            prime_need_state_inventory_free(&joined);
            if (result != CETTA_MATCH_MATCHED) goto done;
            result = CETTA_MATCH_INCOMPLETE;
            if (!prime_need_state_join_nodes(owner, common->session_id, left, right, &mapped[i])) goto done;
            continue;
        }
        size_t event = prime_need_state_event_position(p, frame->event_id);
        if (held[event]) {mapped[i] = recipient->top; continue;}
        PrimeNeedBranchState parent = {.top=left,.session_id=common->session_id,
            .owner=left ? left->owner : recipient->owner};
        Atom *actual = NULL;
        result = prime_need_branch_state_lookup_checked(&parent, frame->event.state_cell, &actual);
        if (result != CETTA_MATCH_MATCHED) goto done;
        if (!actual) {
            result = initial_value ? initial_value(context, frame->event.state_cell, &actual)
                                   : CETTA_MATCH_INCOMPLETE;
            if (result != CETTA_MATCH_MATCHED) goto done;
            if (!actual) {result = CETTA_MATCH_INCOMPLETE; goto done;}
        }
        Atom *expected = frame->event.kind == PRIME_NEED_RECEIPT_READ_STATE
            ? frame->event.after : frame->event.before;
        result = atom_capture_identical_checked(actual, expected);
        if (result != CETTA_MATCH_MATCHED) goto done;
        result = CETTA_MATCH_INCOMPLETE;
        if (left && left->depth == UINT64_MAX) goto done;
        PrimeNeedReceiptEvent copied = frame->event;
        copied.before = copied.before ? atom_deep_copy(owner, copied.before) : NULL;
        copied.after = atom_deep_copy(owner, copied.after);
        if ((frame->event.before && !copied.before) || !copied.after) goto done;
        uint64_t identity = prime_need_fresh_nonzero(&g_prime_need_next_receipt_node);
        if (!identity || (left && identity <= left->identity_id)) goto done;
        PrimeNeedReceiptFrame *node = prime_need_receipt_alloc_frame(owner);
        if (!node) goto done;
        *node = (PrimeNeedReceiptFrame){.left=left,.owner=owner,.session_id=common->session_id,
            .identity_id=identity,.event_id=frame->event_id,.depth=left ? left->depth+1u : 1u,
            .has_event=true,.event=copied};
        prime_need_receipt_refresh_arena_summary(node);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
        prime_need_receipt_build_primary_index(owner, node);
#endif
        mapped[i] = node;
    }
    {
        size_t top = production->top ? prime_need_state_node_position(p, production->top->identity_id) : SIZE_MAX;
        const PrimeNeedReceiptFrame *head = top == SIZE_MAX ? recipient->top : mapped[top];
        *out = (PrimeNeedBranchState){.top=head,.session_id=common->session_id,
            .owner=head ? head->owner : recipient->owner};
        result = CETTA_MATCH_MATCHED;
    }
done:
    free(mapped); free(missing); free(source_events); free(held);
    for (size_t i = 0u; i < 4u; i++) prime_need_state_inventory_free(&inv[i]);
    return result;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
bool prime_need_receipt_state_value(const PrimeNeedReceipt *receipt,
                                    StateCell *cell, Atom **out_value) {
    if (!prime_need_receipt_has_events(receipt)) return false;
    return prime_need_branch_state_value(receipt, cell, out_value);
}
#endif

static int prime_need_receipt_event_compare(const void *left,
                                            const void *right) {
    const PrimeNeedReceiptFrame *const *a = left;
    const PrimeNeedReceiptFrame *const *b = right;
    if ((*a)->event_id < (*b)->event_id)
        return -1;
    if ((*a)->event_id > (*b)->event_id)
        return 1;
    return 0;
}

static bool prime_need_receipt_collect_events(
    const PrimeNeedReceipt *receipt, PrimeNeedReceiptFrames *events) {
    PrimeNeedReceiptFrames all = {0};
    if (!receipt || !events || !prime_need_branch_state_present(receipt))
        return receipt && events;
    if (!prime_need_receipt_collect_all(receipt->top, &all))
        return false;
    for (size_t i = 0u; i < all.len; i++) {
        if (all.items[i]->has_event &&
            !prime_need_receipt_frames_push(events, all.items[i])) {
            prime_need_receipt_frames_free(&all);
            prime_need_receipt_frames_free(events);
            return false;
        }
    }
    prime_need_receipt_frames_free(&all);
    if (events->len > 1u) {
        qsort(events->items, events->len, sizeof(*events->items),
              prime_need_receipt_event_compare);
    }
    /* These arrays are consumed as ordered views; their pointer-membership
     * tables are not used after compaction. */
    size_t unique = 0u;
    for (size_t i = 0u; i < events->len; i++) {
        const PrimeNeedReceiptFrame *frame = events->items[i];
        if (unique && events->items[unique - 1u]->event_id == frame->event_id) {
            if (prime_need_event_payload_compare(events->items[unique - 1u], frame) != CETTA_MATCH_MATCHED) {
                prime_need_receipt_frames_free(events);
                return false;
            }
        } else events->items[unique++] = frame;
    }
    events->len = unique;
    return true;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static size_t prime_need_receipt_graph_find(
    const PrimeNeedReceiptGraph *graph, size_t end, uint64_t identity) {
    size_t begin = 0u;
    while (begin < end) {
        size_t middle = begin + (end - begin) / 2u;
        uint64_t candidate = graph->nodes[middle].identity_id;
        if (candidate == identity)
            return middle;
        if (candidate < identity)
            begin = middle + 1u;
        else
            end = middle;
    }
    return SIZE_MAX;
}

void prime_need_receipt_graph_free(PrimeNeedReceiptGraph *graph) {
    if (!graph)
        return;
    free(graph->nodes);
    prime_need_recording_release(graph->recording_owner);
    *graph = (PrimeNeedReceiptGraph){0};
}

typedef struct {
    uint64_t session, cell, update;
} PrimeNeedReceiptProducer;

static PrimeNeedReceiptProducer prime_need_receipt_producer(
    const PrimeNeedReceiptEvent *event) {
    return (PrimeNeedReceiptProducer){event->need_session_id, event->thunk_id,
                                     event->producer_update_id};
}

static int prime_need_receipt_producer_compare(const void *raw_left,
                                              const void *raw_right) {
    const PrimeNeedReceiptProducer *left = raw_left, *right = raw_right;
    if (left->session != right->session)
        return left->session < right->session ? -1 : 1;
    if (left->cell != right->cell)
        return left->cell < right->cell ? -1 : 1;
    return left->update < right->update ? -1 : left->update > right->update;
}

bool prime_need_receipt_graph_producers(
    const PrimeNeedReceiptGraph *graph, bool *retained) {
    if (!graph || graph->length > SIZE_MAX / sizeof(*graph->nodes) ||
        (graph->length && (!graph->nodes || !retained))) return false;
    /* Short views avoid sorting and allocation. Large views share one
     * producer index instead of repeating a whole-graph scan per delivery. */
    if (graph->length <= 32u) {
        for (size_t i = 0u; i < graph->length; i++) {
            const PrimeNeedReceiptGraphNode *node = &graph->nodes[i];
            const PrimeNeedReceiptEvent *event = &node->event;
            bool found = false;
            if (node->has_event && event->producer_update_id) {
                for (size_t j = 0u; j < graph->length; j++) {
                    const PrimeNeedReceiptGraphNode *candidate = &graph->nodes[j];
                    const PrimeNeedReceiptEvent *producer = &candidate->event;
                    if (candidate->has_event && producer->producer_completed &&
                        producer->producer_update_id == event->producer_update_id &&
                        producer->need_session_id == event->need_session_id &&
                        producer->thunk_id == event->thunk_id) {
                        found = true;
                        break;
                    }
                }
            }
            retained[i] = found;
        }
        return true;
    }
    size_t count = 0u;
    for (size_t i = 0u; i < graph->length; i++) {
        const PrimeNeedReceiptGraphNode *node = &graph->nodes[i];
        if (node->has_event && node->event.producer_completed &&
            node->event.producer_update_id) count++;
    }
    if (count > SIZE_MAX / sizeof(PrimeNeedReceiptProducer)) return false;
    PrimeNeedReceiptProducer *producers = count
        ? malloc(count * sizeof(*producers)) : NULL;
    if (count && !producers) return false;
    size_t written = 0u;
    for (size_t i = 0u; i < graph->length; i++) {
        const PrimeNeedReceiptGraphNode *node = &graph->nodes[i];
        if (node->has_event && node->event.producer_completed &&
            node->event.producer_update_id)
            producers[written++] = prime_need_receipt_producer(&node->event);
    }
    if (count > 1u)
        qsort(producers, count, sizeof(*producers), prime_need_receipt_producer_compare);
    for (size_t i = 0u; i < graph->length; i++) {
        const PrimeNeedReceiptGraphNode *node = &graph->nodes[i];
        PrimeNeedReceiptProducer key = prime_need_receipt_producer(&node->event);
        retained[i] = node->has_event && key.update && count &&
            bsearch(&key, producers, count, sizeof(*producers),
                    prime_need_receipt_producer_compare) != NULL;
    }
    free(producers);
    return true;
}

PrimeNeedReceiptGraphStatus prime_need_receipt_graph_validate(
    const PrimeNeedReceiptGraph *graph) {
    if (!graph)
        return PRIME_NEED_RECEIPT_GRAPH_INVALID;
    if (graph->length == 0u) {
        if (graph->root_id != 0u || graph->event_count != 0u)
            return PRIME_NEED_RECEIPT_GRAPH_INVALID;
        return graph->session_id != 0u
            ? graph->incomplete ? PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE
                                : PRIME_NEED_RECEIPT_GRAPH_COMPLETE
            : PRIME_NEED_RECEIPT_GRAPH_ABSENT;
    }
    if (!graph->nodes || graph->session_id == 0u ||
        graph->length > SIZE_MAX / sizeof(*graph->nodes) ||
        graph->root_id != graph->nodes[graph->length - 1u].identity_id)
        return PRIME_NEED_RECEIPT_GRAPH_INVALID;
    bool *referenced = calloc(graph->length, sizeof(*referenced));
    if (!referenced)
        return PRIME_NEED_RECEIPT_GRAPH_REFUSED;
    PrimeNeedReceiptGraphStatus status = PRIME_NEED_RECEIPT_GRAPH_INVALID;
    size_t events = 0u;
    for (size_t i = 0u; i < graph->length; i++) {
        const PrimeNeedReceiptGraphNode *node = &graph->nodes[i];
        if (node->identity_id == 0u ||
            (i != 0u && graph->nodes[i - 1u].identity_id >= node->identity_id))
            goto done;
        if (node->has_event) {
            if (node->event.event_id != node->identity_id ||
                (unsigned)node->event.kind > PRIME_NEED_RECEIPT_ADMISSION_PENDING ||
                node->right_id != 0u ||
                (node->event.producer_completed && !node->event.producer_update_id) ||
                (node->event.producer_update_id &&
                 (node->event.kind != PRIME_NEED_RECEIPT_OBSERVE_CELL ||
                  !node->event.need_session_id || !node->event.thunk_id)) ||
                (node->event.state_event_id &&
                 (!node->event.state_cell || node->event.state_event_id>=node->event.event_id ||
                  (node->event.kind!=PRIME_NEED_RECEIPT_READ_STATE &&
                   node->event.kind!=PRIME_NEED_RECEIPT_WRITE_STATE))))
                goto done;
            events++;
        } else if (node->event.event_id != 0u ||
                   node->left_id == 0u || node->right_id == 0u ||
                   node->left_id == node->right_id) {
            goto done;
        }
        uint64_t parents[2] = {node->left_id, node->right_id};
        for (size_t j = 0u; j < 2u; j++) {
            if (parents[j] == 0u)
                continue;
            size_t index = prime_need_receipt_graph_find(graph, i, parents[j]);
            if (index == SIZE_MAX)
                goto done;
            referenced[index] = true;
        }
    }
    if (events != graph->event_count)
        goto done;
    /* Every non-root has a later child.  Strict ID order then ensures that
     * every node reaches the sole root, without a second graph traversal. */
    for (size_t i = 0u; i + 1u < graph->length; i++) {
        if (!referenced[i])
            goto done;
    }
    status = graph->incomplete ? PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE
                               : PRIME_NEED_RECEIPT_GRAPH_COMPLETE;
done:
    free(referenced);
    return status;
}


static bool prime_need_receipt_graph_payload_equal(
    const PrimeNeedReceiptGraphNode *left,
    const PrimeNeedReceiptGraphNode *right) {
    if (left->identity_id != right->identity_id ||
        left->left_id != right->left_id || left->right_id != right->right_id ||
        left->has_event != right->has_event)
        return false;
    if (!left->has_event)
        return true;
    const PrimeNeedReceiptEvent *a = &left->event;
    const PrimeNeedReceiptEvent *b = &right->event;
    return a->event_id == b->event_id && a->kind == b->kind &&
           a->need_session_id == b->need_session_id &&
           a->thunk_id == b->thunk_id &&
           a->source_occurrence_id == b->source_occurrence_id &&
           a->source_argument_index == b->source_argument_index &&
           a->rule_occurrence_id == b->rule_occurrence_id &&
           a->producer_update_id == b->producer_update_id &&
           a->producer_completed == b->producer_completed &&
           a->state_event_id == b->state_event_id &&
           a->state_cell == b->state_cell &&
           (a->before == b->before ||
            (a->before && b->before && atom_eq(a->before, b->before))) &&
           (a->after == b->after ||
            (a->after && b->after && atom_eq(a->after, b->after)));
}

/* The caller holds the carrier's metadata owner. A nonzero-to-nonzero CAS
 * competes with the final payload release: storage cannot be resurrected
 * after that release wins. Retain the shell before acquiring payload, and
 * transfer both references to the graph only after checking read permission.
 * This is lifetime protection, not publication/snapshot synchronization. */
static PrimeNeedReceiptGraphStatus prime_need_receipt_graph_hold_recording(
    const PrimeNeedReceipt *receipt, PrimeNeedRecording **out) {
    PrimeNeedRecordingSlice *slice=receipt->recording;
    PrimeNeedRecording *group=slice->group;
    if (!group || receipt->session_id!=group->session_id)
        return PRIME_NEED_RECEIPT_GRAPH_INVALID;
    bool borrowed=receipt->owner==&slice->arena;
    if (!borrowed && (!receipt->owner || !arena_retains_owner(
            receipt->owner,group,prime_need_recording_release)))
        return PRIME_NEED_RECEIPT_GRAPH_INVALID;
    prime_need_recording_carrier_retain(group);
    size_t readers=atomic_load_explicit(&group->payload_references,memory_order_acquire);
    while (readers && readers!=SIZE_MAX) {
        if (atomic_compare_exchange_weak_explicit(&group->payload_references,
                &readers,readers+1u,memory_order_acquire,memory_order_acquire)) {
            if (borrowed && atomic_load_explicit(&group->forgotten,memory_order_acquire)) {
                prime_need_recording_release(group);
                return PRIME_NEED_RECEIPT_GRAPH_ABSENT;
            }
            *out=group;
            return PRIME_NEED_RECEIPT_GRAPH_COMPLETE;
        }
    }
    prime_need_recording_carrier_release(group);
    return readers==SIZE_MAX ? PRIME_NEED_RECEIPT_GRAPH_REFUSED
        : borrowed ? PRIME_NEED_RECEIPT_GRAPH_ABSENT : PRIME_NEED_RECEIPT_GRAPH_INVALID;
}

typedef enum {
    PRIME_NEED_ANCHOR_CACHED_COMPLETION,
    PRIME_NEED_ANCHOR_STATE_WRITE,
} PrimeNeedAnchorKind;

static void prime_need_receipt_anchor_omit(PrimeNeedReceipt *dst) {
    bool scoped=g_prime_need_recording_scope&&g_prime_need_recording_scope->recording&&
        (!dst||!dst->recording||prime_need_recording_same_group(
            dst,g_prime_need_recording_scope));
    PrimeNeedRecordingSlice *target=scoped
        ? g_prime_need_recording_scope->recording : dst ? dst->recording : NULL;
    if (target) prime_need_recording_omit(target);
}

static bool prime_need_receipt_anchor_matches(const PrimeNeedReceiptEvent *event,
        PrimeNeedAnchorKind kind,uint64_t first,uint64_t second,uint64_t third) {
    return kind==PRIME_NEED_ANCHOR_CACHED_COMPLETION
        ? event->kind==PRIME_NEED_RECEIPT_OBSERVE_CELL&&event->producer_completed&&
          event->need_session_id==first&&event->thunk_id==second&&
          event->producer_update_id==third
        : event->kind==PRIME_NEED_RECEIPT_WRITE_STATE&&event->state_event_id==first;
}

static CettaMatchCompletion prime_need_receipt_select_anchor(
        const PrimeNeedReceipt *receipt,PrimeNeedAnchorKind kind,
        uint64_t first,uint64_t second,uint64_t third,
        const PrimeNeedReceiptFrame **selected) {
    PrimeNeedReceiptFrames events={0};
    if (!prime_need_receipt_collect_events(receipt,&events)) return CETTA_MATCH_INCOMPLETE;
    CettaMatchCompletion result=CETTA_MATCH_MATCHED;
    for (size_t i=0u;i<events.len;i++) {
        if (!prime_need_receipt_anchor_matches(
                &events.items[i]->event,kind,first,second,third)) continue;
        if (*selected && (*selected)->event_id!=events.items[i]->event_id) {
            result=CETTA_MATCH_REFUTED;break;
        }
        *selected=events.items[i];
    }
    prime_need_receipt_frames_free(&events);
    return result;
}

static CettaMatchCompletion prime_need_receipt_join_anchor_qualified(
        Arena *owner,PrimeNeedReceipt *dst,const PrimeNeedReceipt *catalogue,
        PrimeNeedAnchorKind kind,uint64_t first,uint64_t second,uint64_t third,
        bool *found) {
    if (!prime_need_owner_admissible(owner)||!dst||!catalogue||!found||!first)
        return CETTA_MATCH_INCOMPLETE;
    const PrimeNeedReceiptFrame *selected=NULL;
    CettaMatchCompletion result=prime_need_receipt_select_anchor(
        dst,kind,first,second,third,&selected);
    if (result!=CETTA_MATCH_MATCHED) return result;
    if (selected) {*found=true;return CETTA_MATCH_MATCHED;}
    PrimeNeedRecording *pin=NULL;
    if (catalogue->recording) {
        PrimeNeedReceiptGraphStatus held=prime_need_receipt_graph_hold_recording(catalogue,&pin);
        if (held==PRIME_NEED_RECEIPT_GRAPH_ABSENT) {
            prime_need_receipt_anchor_omit(dst);*found=false;return CETTA_MATCH_MATCHED;
        }
        if (held!=PRIME_NEED_RECEIPT_GRAPH_COMPLETE) return CETTA_MATCH_INCOMPLETE;
    }
    result=prime_need_receipt_select_anchor(
        catalogue,kind,first,second,third,&selected);
    if (result!=CETTA_MATCH_MATCHED) goto done;
    if (!selected) {
        if (catalogue->recording) {
            prime_need_receipt_anchor_omit(dst);*found=false;result=CETTA_MATCH_MATCHED;
        } else result=CETTA_MATCH_INCOMPLETE;
        goto done;
    }
    {
        PrimeNeedReceipt anchor=*catalogue;anchor.top=selected;
        result=prime_need_receipt_join_qualified(owner,dst,&anchor);
        if (result==CETTA_MATCH_MATCHED) *found=true;
    }
done:
    prime_need_recording_release(pin);
    return result;
}

CettaMatchCompletion prime_need_receipt_join_cached_anchor_qualified(
        Arena *owner,PrimeNeedReceipt *dst,const PrimeNeedReceipt *catalogue,
        uint64_t need_session_id,uint64_t thunk_id,uint64_t update_id,bool *found) {
    if (!need_session_id||!thunk_id||!update_id) return CETTA_MATCH_INCOMPLETE;
    return prime_need_receipt_join_anchor_qualified(owner,dst,catalogue,
        PRIME_NEED_ANCHOR_CACHED_COMPLETION,need_session_id,thunk_id,update_id,found);
}

CettaMatchCompletion prime_need_receipt_join_state_anchor_qualified(
        Arena *owner,PrimeNeedReceipt *dst,const PrimeNeedReceipt *catalogue,
        uint64_t state_event_id,bool *found) {
    return prime_need_receipt_join_anchor_qualified(owner,dst,catalogue,
        PRIME_NEED_ANCHOR_STATE_WRITE,state_event_id,0u,0u,found);
}

PrimeNeedReceiptGraphStatus prime_need_receipt_graph_collect(
    const PrimeNeedReceipt *receipt, PrimeNeedReceiptGraph *out) {
    if (!receipt || !out) return PRIME_NEED_RECEIPT_GRAPH_INVALID;
    PrimeNeedReceipt absent = {0};
    PrimeNeedRecording *recording_owner=NULL;
    if (receipt->recording) {
        PrimeNeedReceiptGraphStatus held=prime_need_receipt_graph_hold_recording(receipt,&recording_owner);
        if (held==PRIME_NEED_RECEIPT_GRAPH_ABSENT) receipt=&absent;
        else if (held!=PRIME_NEED_RECEIPT_GRAPH_COMPLETE) return held;
    }
    if (receipt->top && receipt->session_id == 0u) {
        prime_need_recording_release(recording_owner);
        return PRIME_NEED_RECEIPT_GRAPH_INVALID;
    }
    PrimeNeedReceiptGraph graph = {.session_id = receipt->session_id,.recording_owner=recording_owner};
    if (receipt->recording)
        graph.incomplete = prime_need_recording_status(
            receipt->recording->group).incomplete;
    PrimeNeedReceiptFrames frames = {0};
    PrimeNeedReceiptGraphStatus status = PRIME_NEED_RECEIPT_GRAPH_REFUSED;
    if (!prime_need_receipt_collect_all(receipt->top, &frames))
        goto done;
    if (frames.len != 0u) {
        if (frames.len > SIZE_MAX / sizeof(*graph.nodes))
            goto done;
        graph.nodes = calloc(frames.len, sizeof(*graph.nodes));
        if (!graph.nodes)
            goto done;
        graph.root_id = receipt->top->identity_id;
        qsort(frames.items, frames.len, sizeof(*frames.items),
              prime_need_receipt_frame_identity_compare);
    }
    for (size_t i = 0u; i < frames.len; i++) {
        const PrimeNeedReceiptFrame *frame = frames.items[i];
        if (frame->session_id != graph.session_id ||
            frame->event_id != (frame->has_event ? frame->identity_id : 0u)) {
            status = PRIME_NEED_RECEIPT_GRAPH_INVALID;
            goto done;
        }
        PrimeNeedReceiptGraphNode node = {
            .identity_id = frame->identity_id,
            .left_id = frame->left ? frame->left->identity_id : 0u,
            .right_id = frame->right ? frame->right->identity_id : 0u,
            .has_event = frame->has_event,
            .event = frame->event,
        };
        if (graph.length != 0u &&
            graph.nodes[graph.length - 1u].identity_id == node.identity_id) {
            /* Promotion can leave two storage copies of one inherited node.
             * It cannot manufacture a second occurrence or change its edges. */
            if (!prime_need_receipt_graph_payload_equal(
                    &graph.nodes[graph.length - 1u], &node)) {
                status = PRIME_NEED_RECEIPT_GRAPH_INVALID;
                goto done;
            }
            continue;
        }
        graph.nodes[graph.length++] = node;
        graph.event_count += node.has_event ? 1u : 0u;
    }
    status = prime_need_receipt_graph_validate(&graph);
    if (status == PRIME_NEED_RECEIPT_GRAPH_COMPLETE ||
        status == PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE ||
        status == PRIME_NEED_RECEIPT_GRAPH_ABSENT) {
        prime_need_receipt_graph_free(out);
        *out = graph;
        graph = (PrimeNeedReceiptGraph){0};
    }
done:
    prime_need_receipt_frames_free(&frames);
    prime_need_receipt_graph_free(&graph);
    return status;
}

size_t prime_need_receipt_event_count(const PrimeNeedReceipt *receipt) {
    if (!prime_need_receipt_has_events(receipt)) return 0u;
    PrimeNeedReceiptFrames events = {0};
    if (!prime_need_receipt_collect_events(receipt, &events))
        return 0u;
    size_t count = events.len;
    prime_need_receipt_frames_free(&events);
    return count;
}

bool prime_need_receipt_event_at(const PrimeNeedReceipt *receipt,
                                 size_t index,
                                 PrimeNeedReceiptEvent *out_event) {
    if (!prime_need_receipt_has_events(receipt)) return false;
    PrimeNeedReceiptFrames events = {0};
    if (!out_event ||
        !prime_need_receipt_collect_events(receipt, &events) ||
        index >= events.len) {
        prime_need_receipt_frames_free(&events);
        return false;
    }
    *out_event = events.items[index]->event;
    prime_need_receipt_frames_free(&events);
    return true;
}
#endif

bool prime_need_branch_state_equal(
    const PrimeNeedBranchState *left,
    const PrimeNeedBranchState *right) {
    if (!left || !right)
        return false;
    if (!prime_need_branch_state_present(left) ||
        !prime_need_branch_state_present(right))
        return !prime_need_branch_state_present(left) &&
               !prime_need_branch_state_present(right);
    if (left->session_id != right->session_id)
        return false;
    PrimeNeedReceiptFrames a = {0};
    PrimeNeedReceiptFrames b = {0};
    if (!prime_need_receipt_collect_events(left, &a) ||
        !prime_need_receipt_collect_events(right, &b)) {
        prime_need_receipt_frames_free(&a);
        prime_need_receipt_frames_free(&b);
        return false;
    }
    bool equal = a.len == b.len;
    for (size_t i = 0u; equal && i < a.len; i++)
        equal = prime_need_event_payload_compare(a.items[i], b.items[i]) == CETTA_MATCH_MATCHED;
    prime_need_receipt_frames_free(&a);
    prime_need_receipt_frames_free(&b);
    return equal;
}

typedef struct {
    const PrimeNeedReceiptFrame **keys;
    PrimeNeedReceiptFrame **values;
    size_t cap;
} PrimeNeedReceiptCopyMap;

static bool prime_need_receipt_copy_map_init(
    PrimeNeedReceiptCopyMap *map, size_t count) {
    if (!map)
        return false;
    *map = (PrimeNeedReceiptCopyMap){0};
    size_t cap = 16u;
    while (cap / 2u < count) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / sizeof(*map->keys) ||
        cap > SIZE_MAX / sizeof(*map->values))
        return false;
    map->keys = calloc(cap, sizeof(*map->keys));
    map->values = calloc(cap, sizeof(*map->values));
    if (!map->keys || !map->values) {
        free(map->keys);
        free(map->values);
        *map = (PrimeNeedReceiptCopyMap){0};
        return false;
    }
    map->cap = cap;
    return true;
}

static void prime_need_receipt_copy_map_free(
    PrimeNeedReceiptCopyMap *map) {
    if (!map)
        return;
    free(map->keys);
    free(map->values);
    *map = (PrimeNeedReceiptCopyMap){0};
}

static bool prime_need_receipt_copy_map_put(
    PrimeNeedReceiptCopyMap *map,
    const PrimeNeedReceiptFrame *source,
    PrimeNeedReceiptFrame *copy) {
    if (!map || !map->keys || map->cap == 0u || !source || !copy)
        return false;
    size_t mask = map->cap - 1u;
    size_t slot = prime_need_pointer_hash(source) & mask;
    while (map->keys[slot] && map->keys[slot] != source)
        slot = (slot + 1u) & mask;
    map->keys[slot] = source;
    map->values[slot] = copy;
    return true;
}

static PrimeNeedReceiptFrame *prime_need_receipt_copy_map_get(
    const PrimeNeedReceiptCopyMap *map,
    const PrimeNeedReceiptFrame *source) {
    if (!source)
        return NULL;
    if (!map || !map->keys || map->cap == 0u)
        return NULL;
    size_t mask = map->cap - 1u;
    size_t slot = prime_need_pointer_hash(source) & mask;
    while (map->keys[slot]) {
        if (map->keys[slot] == source)
            return map->values[slot];
        slot = (slot + 1u) & mask;
    }
    return NULL;
}

static int prime_need_receipt_frame_depth_compare(
    const void *left_item, const void *right_item) {
    const PrimeNeedReceiptFrame *left =
        *(const PrimeNeedReceiptFrame *const *)left_item;
    const PrimeNeedReceiptFrame *right =
        *(const PrimeNeedReceiptFrame *const *)right_item;
    if (left->depth < right->depth)
        return -1;
    if (left->depth > right->depth)
        return 1;
    uintptr_t left_address = (uintptr_t)left;
    uintptr_t right_address = (uintptr_t)right;
    return left_address < right_address
        ? -1 : left_address > right_address ? 1 : 0;
}

static bool prime_need_branch_state_promote_internal(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedBranchState *receipt) {
    if (!dst || !receipt)
        return false;
    if (!prime_need_branch_state_present(receipt))
        return true;
    if (!prime_need_branch_state_has_events(receipt)) {
        receipt->owner = dst;
        return true;
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_PROMOTE);
    if (receipt->owner == dst &&
        (!receipt->top || receipt->top->closure_owner == dst))
        return true;
    PrimeNeedReceiptFrames originals = {0};
    if (!prime_need_receipt_collect_all(receipt->top, &originals))
        return false;
    qsort(originals.items, originals.len, sizeof(*originals.items),
          prime_need_receipt_frame_depth_compare);
    PrimeNeedReceiptCopyMap copies;
    if (!prime_need_receipt_copy_map_init(&copies, originals.len)) {
        prime_need_receipt_frames_free(&originals);
        return false;
    }
    bool own_session = !copy_session;
    if (own_session)
        copy_session = atom_deep_copy_session_new(dst);
    if (!copy_session)
        goto fail;
    PrimeNeedReceiptFrame *promoted_top = NULL;
    for (size_t i = 0u; i < originals.len; i++) {
        const PrimeNeedReceiptFrame *source = originals.items[i];
        PrimeNeedReceiptFrame *left =
            prime_need_receipt_copy_map_get(&copies, source->left);
        PrimeNeedReceiptFrame *right =
            prime_need_receipt_copy_map_get(&copies, source->right);
        if ((source->left && !left) || (source->right && !right))
            goto fail;
        PrimeNeedReceiptFrame *copy = prime_need_receipt_alloc_frame(dst);
        if (!copy)
            goto fail;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_PROMOTED_FRAME);
        *copy = *source;
        copy->left = left;
        copy->right = right;
        copy->owner = dst;
        if (source->has_event) {
            copy->event.before = source->event.before
                ? atom_deep_copy_session_copy(copy_session, source->event.before) : NULL;
            copy->event.after = source->event.after
                ? atom_deep_copy_session_copy(copy_session, source->event.after) : NULL;
            if ((source->event.before && !copy->event.before) ||
                (source->event.after && !copy->event.after))
                goto fail;
        }
        prime_need_receipt_refresh_arena_summary(copy);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
        prime_need_receipt_build_primary_index(dst, copy);
#endif
        if (!prime_need_receipt_copy_map_put(&copies, source, copy))
            goto fail;
        if (source == receipt->top)
            promoted_top = copy;
    }
    if (!promoted_top)
        goto fail;
    receipt->top = promoted_top;
    receipt->owner = dst;
    if (own_session)
        atom_deep_copy_session_free(copy_session);
    prime_need_receipt_copy_map_free(&copies);
    prime_need_receipt_frames_free(&originals);
    return true;

fail:
    if (own_session)
        atom_deep_copy_session_free(copy_session);
    prime_need_receipt_copy_map_free(&copies);
    prime_need_receipt_frames_free(&originals);
    return false;
}

bool prime_need_branch_state_promote(
    Arena *dst, PrimeNeedBranchState *receipt) {
    return prime_need_branch_state_promote_internal(dst, NULL, receipt);
}

bool prime_need_branch_state_promote_with_session(
    Arena *dst, AtomDeepCopySession *copy_session, PrimeNeedBranchState *receipt) {
    return atom_deep_copy_session_targets(copy_session, dst) &&
        prime_need_branch_state_promote_internal(dst, copy_session, receipt);
}

static const char prime_need_receipt_copy_kind;
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
static const char prime_need_receipt_index_copy_kind;

static bool prime_need_receipt_index_copy(AtomDeepCopySession *session,
    const PrimeNeedReceiptFrame *source, const PrimeNeedReceiptFrame ***out) {
    *out = NULL;
    if (!source->primary_ancestors)
        return source->primary_ancestor_count == 0u;
    size_t capacity = 0u;
    for (uint64_t depth = source->primary_depth; depth; depth >>= 1u)
        capacity++;
    if (!source->primary_ancestor_count ||
        source->primary_ancestor_count > capacity ||
        capacity > SIZE_MAX / sizeof(*source->primary_ancestors))
        return false;
    size_t bytes = capacity * sizeof(*source->primary_ancestors);
    const Arena *owner = NULL;
    Arena *image_owner = NULL;
    if (!atom_deep_copy_session_owner_of_span(session, source->primary_ancestors,
            bytes, source->owner, &owner, &image_owner) || owner == image_owner)
        return false;
    const PrimeNeedReceiptFrame **image = atom_deep_copy_session_object_forwarded(
        session, &prime_need_receipt_index_copy_kind, source->primary_ancestors, bytes);
    if (image && !arena_owns_ptr(image_owner, image)) return false;
    bool fresh = !image;
    if (fresh) {
        image = arena_alloc(image_owner, bytes);
        if (!image) return false;
        memset(image, 0, bytes);
    }
    for (size_t i = 0u; i < source->primary_ancestor_count; i++) {
        const PrimeNeedReceiptFrame *ancestor = source->primary_ancestors[i];
        if (!ancestor || ancestor->depth >= source->depth ||
            ancestor->session_id != source->session_id) return false;
        const PrimeNeedReceiptFrame *mapped = atom_deep_copy_session_object_forwarded(
            session, &prime_need_receipt_copy_kind, ancestor, sizeof(*ancestor));
        if (!mapped || (!fresh && image[i] != mapped)) return false;
        if (fresh) image[i] = mapped;
    }
    if (fresh && !atom_deep_copy_session_object_record(session,
            &prime_need_receipt_index_copy_kind, source->primary_ancestors,
            image, bytes)) return false;
    *out = image;
    return true;
}
#endif

static bool prime_need_receipt_copy_frames(AtomDeepCopySession *session,
    const PrimeNeedReceiptFrames *originals, uint64_t session_id, bool recorded) {
#if !CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    (void)recorded;
#endif
    for (size_t i = 0u; i < originals->len; i++) {
        const PrimeNeedReceiptFrame *source = originals->items[i];
        const Arena *owner = NULL;
        Arena *image_owner = NULL;
        if (!atom_deep_copy_session_owner_of_span(session, source, sizeof(*source),
                source->owner, &owner, &image_owner) || owner != source->owner ||
            owner == image_owner || !source->identity_id || !source->depth ||
            source->session_id != session_id ||
            (source->left && source->left->depth >= source->depth) ||
            (source->right && source->right->depth >= source->depth)) return false;
        const PrimeNeedReceiptFrame *left = source->left
            ? atom_deep_copy_session_object_forwarded(session,
                &prime_need_receipt_copy_kind, source->left, sizeof(*source->left)) : NULL;
        const PrimeNeedReceiptFrame *right = source->right
            ? atom_deep_copy_session_object_forwarded(session,
                &prime_need_receipt_copy_kind, source->right, sizeof(*source->right)) : NULL;
        if ((source->left && !left) || (source->right && !right)) return false;
        Atom *before = source->event.before
            ? atom_deep_copy_session_copy(session, source->event.before) : NULL;
        Atom *after = source->event.after
            ? atom_deep_copy_session_copy(session, source->event.after) : NULL;
        if ((source->event.before && !before) || (source->event.after && !after)) return false;
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
        const PrimeNeedReceiptFrame **ancestors = NULL;
        if (!prime_need_receipt_index_copy(session, source, &ancestors)) return false;
#endif
        PrimeNeedReceiptFrame *copy = atom_deep_copy_session_object_forwarded(
            session, &prime_need_receipt_copy_kind, source, sizeof(*source));
        if (copy) {
            if (!arena_owns_ptr(image_owner, copy) || copy->owner != image_owner ||
                copy->session_id != source->session_id || copy->identity_id != source->identity_id ||
                copy->event_id != source->event_id || copy->depth != source->depth ||
                copy->has_event != source->has_event || copy->left != left || copy->right != right ||
                copy->event.before != before || copy->event.after != after ||
                copy->event.state_cell != source->event.state_cell) return false;
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
            if (copy->primary_ancestors != ancestors ||
                copy->primary_ancestor_count != source->primary_ancestor_count ||
                copy->primary_depth != source->primary_depth) return false;
#endif
        } else {
            copy = prime_need_receipt_alloc_frame(image_owner);
            if (!copy) return false;
            *copy = *source;
            copy->owner = image_owner;
            copy->left = left;
            copy->right = right;
            copy->event.before = before;
            copy->event.after = after;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            /* Inventory edges are installed only after the entire recorded
             * forest has passed the common metadata/Atom transport. */
            if (!recorded && (source->recording_next || source->recording_referenced))
                return false;
            copy->recording_next = NULL;
#endif
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
            copy->primary_ancestors = ancestors;
#endif
            prime_need_receipt_refresh_arena_summary(copy);
            if (!atom_deep_copy_session_object_record(session,
                    &prime_need_receipt_copy_kind, source, copy, sizeof(*source))) return false;
        }
    }
    return true;
}

bool prime_need_branch_state_copy_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedBranchState *state) {
    if (!state || !atom_deep_copy_session_targets(session, dst)) return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (state->recording) return false;
#endif
    if (!prime_need_branch_state_present(state))
        return !state->top && !state->owner && !state->session_id;
    PrimeNeedBranchState image = *state;
    if (!atom_deep_copy_session_destination_for_owner(session, state->owner,
            &image.owner) || image.owner == state->owner) return false;
    if (!state->top) { *state = image; return true; }
    if (state->top->owner != state->owner) return false;
    PrimeNeedReceiptFrames originals = {0};
    bool ok = false;
    if (!prime_need_receipt_collect_all(state->top, &originals)) goto done;
    qsort(originals.items, originals.len, sizeof(*originals.items),
          prime_need_receipt_frame_depth_compare);
    if (!prime_need_receipt_copy_frames(session, &originals, state->session_id, false))
        goto done;
    image.top = atom_deep_copy_session_object_forwarded(session,
        &prime_need_receipt_copy_kind, state->top, sizeof(*state->top));
    if (!image.top || image.top == state->top) goto done;
    *state = image;
    ok = true;
done:
    prime_need_receipt_frames_free(&originals);
    return ok;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static const char prime_need_recording_copy_kind, prime_need_recording_slice_copy_kind;
static const char prime_need_recording_member_copy_kind, prime_need_recording_owner_copy_kind;
static const char prime_need_recording_prepared_copy_kind;
static char prime_need_recording_copy_mode_kind;
static char prime_need_recording_copy_retain, prime_need_recording_copy_duplicate;
static char prime_need_recording_copy_erase;

static const void *prime_need_recording_copy_policy(const AtomDeepCopySession *session) {
    return atom_deep_copy_session_object_forwarded(session,
        &prime_need_recording_copy_mode_kind, &prime_need_recording_copy_mode_kind,
        sizeof(prime_need_recording_copy_mode_kind));
}

static bool prime_need_recording_copy_select(AtomDeepCopySession *session, char *policy) {
    const void *selected = prime_need_recording_copy_policy(session);
    if (selected) return selected == policy;
    return atom_deep_copy_session_object_record(session,
        &prime_need_recording_copy_mode_kind, &prime_need_recording_copy_mode_kind,
        policy, sizeof(*policy));
}

PrimeNeedRecording *prime_need_receipt_recording_owner(const PrimeNeedReceipt *receipt) {
    return receipt && receipt->recording ? receipt->recording->group : NULL;
}

PrimeNeedRecordingCopy *prime_need_recording_copy_new(PrimeNeedRecording *source) {
    if (source && !atomic_load_explicit(&source->payload_references, memory_order_acquire))
        return NULL;
    if (!source || !source->session_id || source->slice_count > source->limits.slices ||
        (g_prime_need_recording_scope && g_prime_need_recording_scope->recording &&
         g_prime_need_recording_scope->recording->group == source)) return NULL;
    size_t nodes = 0u, bytes = 0u;
    for (size_t i = 0u; i < source->slice_count; i++) {
        const PrimeNeedRecordingSlice *slice = &source->slices[i];
        if (slice->group != source || slice->nodes > slice->node_limit ||
            slice->charge > slice->byte_limit ||
            slice->node_limit > source->limits.nodes - nodes ||
            slice->byte_limit > source->limits.payload_bytes - bytes) return NULL;
        nodes += slice->node_limit;
        bytes += slice->byte_limit;
    }
    size_t links = atomic_load_explicit(&source->owner_links, memory_order_relaxed);
    if (nodes != source->reserved_nodes || bytes != source->reserved_bytes ||
        links > source->limits.owner_links) return NULL;
    PrimeNeedRecordingCopy *copy = calloc(1u, sizeof(*copy));
    if (!copy) return NULL;
    copy->image = prime_need_recording_allocate(source->limits);
    if (!copy->image) { free(copy); return NULL; }
    copy->source = source;
    copy->source_forgotten = atomic_load_explicit(&source->forgotten, memory_order_acquire);
    prime_need_recording_retain(source);
    copy->captured = prime_need_recording_status(source);
    PrimeNeedRecording *image = copy->image;
    image->session_id = source->session_id;
    image->publication_revision = source->publication_revision;
    image->sealed = source->sealed;
    image->reserved_nodes = nodes;
    image->reserved_bytes = bytes;
    atomic_store_explicit(&image->owner_links, links, memory_order_relaxed);
    for (size_t i = 0u; i < source->slice_count; i++) {
        const PrimeNeedRecordingSlice *s = &source->slices[i];
        PrimeNeedRecordingSlice *d = &image->slices[i];
        d->group = image;
        arena_init_detached(&d->arena);
        arena_set_block_capacity(&d->arena, s->arena.block_capacity);
        d->arena.block_growth = s->arena.block_growth;
        arena_set_runtime_kind(&d->arena, s->arena.runtime_kind);
        arena_set_scalar_cache(&d->arena, s->arena.scalar_cache);
        d->node_quantum = s->node_quantum; d->byte_quantum = s->byte_quantum;
        d->growing = s->growing;
        d->node_limit = s->node_limit; d->byte_limit = s->byte_limit;
        d->nodes = s->nodes; d->charge = s->charge;
        atomic_init(&d->omitted, atomic_load_explicit(&s->omitted, memory_order_relaxed));
        atomic_init(&d->incomplete, atomic_load_explicit(&s->incomplete, memory_order_relaxed));
        atomic_init(&d->overflow, atomic_load_explicit(&s->overflow, memory_order_relaxed));
        /* Captured endpoints are never published; preparation replaces them
         * through the same typed metadata map as every receipt carrier. */
        d->first = s->first; d->last = s->last;
        d->published_root = s->published_root;
        image->slice_count++;
    }
    return copy;
}

size_t prime_need_recording_copy_pair_count(const PrimeNeedRecordingCopy *copy) {
    return copy && copy->image ? copy->image->slice_count : 0u;
}

bool prime_need_recording_copy_pair(const PrimeNeedRecordingCopy *copy,
    size_t index, AtomDeepCopyArenaPair *out) {
    if (!copy || !copy->image || copy->attempted || !out ||
        index >= copy->image->slice_count) return false;
    *out = (AtomDeepCopyArenaPair){&copy->source->slices[index].arena,
                                &copy->image->slices[index].arena};
    return true;
}

static bool prime_need_recording_copy_current(const PrimeNeedRecordingCopy *copy) {
    const PrimeNeedRecording *s = copy->source, *d = copy->image;
    if (!s || !d || s->session_id != d->session_id ||
        atomic_load_explicit(&s->forgotten, memory_order_acquire) != copy->source_forgotten ||
        s->publication_revision != d->publication_revision || s->sealed != d->sealed ||
        s->slice_count != d->slice_count ||
        s->reserved_nodes != d->reserved_nodes || s->reserved_bytes != d->reserved_bytes ||
        s->limits.nodes != d->limits.nodes || s->limits.payload_bytes != d->limits.payload_bytes ||
        s->limits.slices != d->limits.slices || s->limits.owner_links != d->limits.owner_links ||
        (g_prime_need_recording_scope && g_prime_need_recording_scope->recording &&
         g_prime_need_recording_scope->recording->group == s)) return false;
    PrimeNeedRecordingStatus now = prime_need_recording_status(s);
    return now.nodes == copy->captured.nodes &&
        now.payload_charge == copy->captured.payload_charge &&
        now.owner_links == copy->captured.owner_links &&
        now.omitted == copy->captured.omitted &&
        now.incomplete == copy->captured.incomplete && now.overflow == copy->captured.overflow;
}

bool prime_need_recording_copy_prepare(PrimeNeedRecordingCopy *copy,
    AtomDeepCopySession *session) {
    if (!copy || !copy->image || !session) return false;
    if (copy->attempted) return copy->prepared && copy->session == session;
    copy->attempted = true;
    if (!prime_need_recording_copy_current(copy)) return false;
    PrimeNeedRecording *source = copy->source, *image = copy->image;
    PrimeNeedReceiptFrames frames = {0};
    bool ok = false;
    for (size_t i = 0u; i < source->slice_count; i++) {
        const PrimeNeedRecordingSlice *s = &source->slices[i];
        const PrimeNeedRecordingSlice *d = &image->slices[i];
        if (!atom_deep_copy_session_maps_arena(session, &s->arena, &d->arena) ||
            s->first != d->first || s->last != d->last ||
            s->published_root != d->published_root || s->nodes != d->nodes ||
            s->charge != d->charge || s->node_limit != d->node_limit ||
            s->byte_limit != d->byte_limit) goto done;
        size_t count = 0u;
        const PrimeNeedReceiptFrame *last = NULL;
        for (const PrimeNeedReceiptFrame *frame = s->first; frame;
             frame = frame->recording_next) {
            if (count == s->nodes || frame->owner != &s->arena ||
                frame->event.state_cell || prime_need_receipt_frames_contains(&frames, frame) ||
                !prime_need_receipt_frames_push(&frames, frame)) goto done;
            count++; last = frame;
        }
        if (count != s->nodes || last != s->last) goto done;
    }
    for (size_t i = 0u; i < frames.len; i++) {
        const PrimeNeedReceiptFrame *frame = frames.items[i];
        if ((frame->left && !prime_need_receipt_frames_contains(&frames, frame->left)) ||
            (frame->right && !prime_need_receipt_frames_contains(&frames, frame->right))) goto done;
    }
    for (size_t i = 0u; i < source->slice_count; i++)
        if (source->slices[i].published_root && !prime_need_receipt_frames_contains(
                &frames, source->slices[i].published_root)) goto done;
    if (frames.len > 1u)
        qsort(frames.items, frames.len, sizeof(*frames.items), prime_need_receipt_frame_depth_compare);
    if (!prime_need_receipt_copy_frames(session, &frames, source->session_id, true) ||
        !atom_deep_copy_session_object_record(session, &prime_need_recording_copy_kind,
            source, image, source->allocation_bytes)) goto done;
    for (size_t i = 0u; i < source->slice_count; i++) {
        const PrimeNeedRecordingSlice *s = &source->slices[i];
        PrimeNeedRecordingSlice *d = &image->slices[i];
        if (!atom_deep_copy_session_object_record(session, &prime_need_recording_slice_copy_kind,
                s, d, sizeof(*s)) ||
            !atom_deep_copy_session_object_record(session, &prime_need_recording_owner_copy_kind,
                &s->arena, d, sizeof(s->arena))) goto done;
        d->published_root = s->published_root
            ? atom_deep_copy_session_object_forwarded(session,
                &prime_need_receipt_copy_kind, s->published_root,
                sizeof(*s->published_root)) : NULL;
        if (s->published_root && !d->published_root) goto done;
        d->first = NULL; d->last = NULL;
        for (const PrimeNeedReceiptFrame *frame = s->first; frame;
             frame = frame->recording_next) {
            PrimeNeedReceiptFrame *mapped = atom_deep_copy_session_object_forwarded(
                session, &prime_need_receipt_copy_kind, frame, sizeof(*frame));
            if (!mapped || mapped->owner != &d->arena ||
                !atom_deep_copy_session_object_record(session,
                    &prime_need_recording_member_copy_kind, frame, mapped, sizeof(*frame))) goto done;
            if (d->last) d->last->recording_next = mapped;
            else d->first = mapped;
            mapped->recording_next = NULL;
            mapped->recording_referenced = frame->recording_referenced;
            d->last = mapped;
        }
    }
    if (!prime_need_recording_copy_current(copy) ||
        !atom_deep_copy_session_object_record(session, &prime_need_recording_prepared_copy_kind,
            source, copy, sizeof(*copy))) goto done;
    copy->session = session;
    copy->prepared = true;
    ok = true;
done:
    prime_need_receipt_frames_free(&frames);
    return ok;
}

bool prime_need_recording_copy_receipt(const PrimeNeedRecordingCopy *copy,
    const PrimeNeedReceipt *source, PrimeNeedReceipt *out) {
    if (!copy || !copy->prepared || !copy->image || !source || !out ||
        !source->recording || source->recording->group != copy->source ||
        source->session_id != copy->source->session_id ||
        !prime_need_recording_readable(source)) return false;
    PrimeNeedRecordingSlice *slice = atom_deep_copy_session_object_forwarded(
        copy->session, &prime_need_recording_slice_copy_kind,
        source->recording, sizeof(*source->recording));
    if (!slice || slice->group != copy->image ||
        !atom_deep_copy_session_maps_arena(copy->session, &source->recording->arena, &slice->arena))
        return false;
    PrimeNeedReceipt image = *source;
    image.owner = &slice->arena; image.recording = slice;
    /* A forwarding hit proves storage identity, not membership in this
     * recording inventory. Only the complete owner walk grants that tag. */
    image.top = source->top ? atom_deep_copy_session_object_forwarded(copy->session,
        &prime_need_recording_member_copy_kind, source->top, sizeof(*source->top)) : NULL;
    if (source->top) {
        PrimeNeedRecordingSlice *root_slice = atom_deep_copy_session_object_forwarded(
            copy->session, &prime_need_recording_owner_copy_kind,
            source->top->owner, sizeof(*source->top->owner));
        /* A seed may refer to another slice of this same group. A separately
         * copied group can retain equal IDs, so IDs do not grant ownership. */
        if (!image.top || image.top->session_id != image.session_id || !root_slice ||
            root_slice->group != copy->image || image.top->owner != &root_slice->arena)
            return false;
    }
    *out = image;
    return true;
}

bool prime_need_recording_copy_restore_holder(PrimeNeedRecordingCopy *copy,
    const Arena *source, Arena *image) {
    if (!copy || !copy->prepared || !copy->image || source == image ||
        !atom_deep_copy_session_maps_arena(copy->session, source, image)) return false;
    bool held = false;
    for (unsigned reader = 0u; reader < 2u; reader++) {
        void (*release)(void *) = reader ? prime_need_recording_release
                                        : prime_need_recording_carrier_release;
        if (!arena_retains_owner(source, copy->source, release)) continue;
        held = true;
        if (arena_retains_owner(image, copy->image, release)) continue;
        if (copy->restored_links == copy->captured.owner_links) return false;
        if (!arena_retain_owner_with_storage(image, copy->image,
                reader ? prime_need_recording_retain : prime_need_recording_carrier_retain,
                release, reader ? prime_need_recording_storage : prime_need_recording_carrier_storage))
            return false;
        copy->restored_links++;
    }
    return held;
}

bool prime_need_recording_copy_enable(AtomDeepCopySession *session) {
    return prime_need_recording_copy_select(session, &prime_need_recording_copy_duplicate);
}

bool prime_need_recording_copy_forget(AtomDeepCopySession *session) {
    return prime_need_recording_copy_select(session, &prime_need_recording_copy_erase);
}

bool prime_need_recording_copy_erases(const AtomDeepCopySession *session) {
    return prime_need_recording_copy_policy(session) == &prime_need_recording_copy_erase;
}

bool prime_need_recording_copy_enclosing_owner(PrimeNeedRecordingCopy *copy,
    const Arena *source, Arena *image) {
    if (!copy || !copy->prepared || !prime_need_recording_copy_current(copy) ||
        !source || !image || source == image ||
        !atom_deep_copy_session_maps_arena(copy->session, source, image)) return false;
    /* The live copy capability is the kind: equal logical recording IDs do
     * not share enclosing-owner authority. The caller supplies the direct
     * group ownership premise; the table checks its physical region pair. */
    return atom_deep_copy_session_object_record(copy->session,
        copy, source, image, sizeof(*source));
}

PrimeNeedRecordingCopy *prime_need_recording_copy_lookup(
    const AtomDeepCopySession *session, const PrimeNeedRecording *source) {
    if (!source || prime_need_recording_copy_policy(session) !=
            &prime_need_recording_copy_duplicate) return NULL;
    PrimeNeedRecordingCopy *copy = atom_deep_copy_session_object_forwarded(session,
        &prime_need_recording_prepared_copy_kind, source, sizeof(*copy));
    return copy && copy->session == session && copy->source == source &&
        copy->prepared && prime_need_recording_copy_current(copy) ? copy : NULL;
}

static bool prime_need_recording_copy_mode(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt, bool carrier) {
    if (!receipt || !receipt->recording || !atom_deep_copy_session_targets(session, dst))
        return false;
    if (!prime_need_recording_readable(receipt)) {
        if (!prime_need_recording_erased(receipt)) return false;
        prime_need_receipt_init(receipt);
        return true;
    }
    bool reader = receipt->owner != &receipt->recording->arena;
    const void *policy = prime_need_recording_copy_policy(session);
    if (!policy) {
        if (!prime_need_recording_copy_select(session, &prime_need_recording_copy_retain))
            return false;
        policy = &prime_need_recording_copy_retain;
    }
    if (policy == &prime_need_recording_copy_erase) {
        prime_need_receipt_init(receipt);
        return true;
    }
    if (policy == &prime_need_recording_copy_retain)
        return carrier ? prime_need_recording_promote_carrier(dst, receipt)
                       : prime_need_recording_promote(dst, receipt);
    if (policy != &prime_need_recording_copy_duplicate) return false;
    PrimeNeedRecordingCopy *copy = prime_need_recording_copy_lookup(
        session, receipt->recording->group);
    if (!copy) return false;
    PrimeNeedReceipt image;
    if (!prime_need_recording_copy_receipt(copy, receipt, &image)) return false;
    const Arena *source = atom_deep_copy_session_object_original(
        session, copy, dst, sizeof(*dst));
    bool owned = source && atom_deep_copy_session_maps_arena(session, source, dst);
    if (!owned) owned = arena_retains_owner(dst, copy->image, prime_need_recording_release);
    if (!owned) owned = arena_retains_owner(dst, copy->image, prime_need_recording_carrier_release);
    for (size_t i = 0u; !owned && i < copy->image->slice_count; i++)
        owned = dst == &copy->image->slices[i].arena;
    if (!owned) return false;
    if (reader && !prime_need_recording_promote(dst, &image)) return false;
    *receipt = image;
    return true;
}

bool prime_need_recording_copy_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt) {
    return prime_need_recording_copy_mode(dst, session, receipt, false);
}

bool prime_need_recording_copy_carrier_with_session(Arena *dst,
    AtomDeepCopySession *session, PrimeNeedReceipt *receipt) {
    return prime_need_recording_copy_mode(dst, session, receipt, true);
}

PrimeNeedRecording *prime_need_recording_copy_take(PrimeNeedRecordingCopy *copy) {
    if (!copy || !copy->prepared) return NULL;
    PrimeNeedRecording *image = copy->image;
    copy->image = NULL;
    return image;
}

void prime_need_recording_copy_free(PrimeNeedRecordingCopy *copy) {
    if (!copy) return;
    prime_need_recording_release(copy->image);
    prime_need_recording_release(copy->source);
    free(copy);
}
#endif

bool prime_need_branch_state_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedBranchState *receipt) {
    if (!atom_deep_copy_session_targets(copy_session, dst) ||
        !forbidden || !receipt ||
        dst == forbidden)
        return false;
    if (!prime_need_branch_state_has_events(receipt)) {
        /* The empty event-DAG has no payload to copy, but its owner is the
         * allocation authority for the first future event.  Rehome that
         * authority before the forbidden region can be reset. */
        return prime_need_branch_state_promote(dst, receipt);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_PROMOTE_SUFFIX);
    if (receipt->top->closure_owner &&
        receipt->top->closure_owner != forbidden)
        return true;
    if (receipt->top->owner != forbidden &&
        prime_need_branch_state_excludes_arena(receipt, forbidden))
        return true;

    PrimeNeedReceiptFrames originals = {0};
    PrimeNeedReceiptFrames suffix = {0};
    if (!prime_need_receipt_frames_push(
            &originals, receipt->top)) {
        prime_need_receipt_frames_free(&originals);
        return false;
    }
    bool shape_ok = true;
    for (size_t i = 0u; shape_ok && i < originals.len; i++) {
        const PrimeNeedReceiptFrame *source = originals.items[i];
        if (!source->owner) {
            shape_ok = false;
            break;
        }
        if (source->closure_owner &&
            source->closure_owner != forbidden)
            continue;
        if (prime_need_arena_range_excludes(
                source->arena_min_id, source->arena_max_id,
                source->arena_range_complete, forbidden))
            continue;
        if (prime_need_arena_bloom_excludes(
                source->arena_bloom,
                source->arena_bloom_complete,
                forbidden))
            continue;
        if (source->owner == forbidden) {
            shape_ok = prime_need_receipt_frames_push(&suffix, source);
        } else if ((source->left &&
                    source->left->owner == forbidden) ||
                   (source->right &&
                    source->right->owner == forbidden)) {
            /* A longer-lived node must never point into its shorter-lived
             * child.  Copying a newest suffix cannot repair that edge. */
            shape_ok = false;
        }
        if (shape_ok && source->left &&
            !prime_need_receipt_frames_push(
                &originals, source->left))
            shape_ok = false;
        if (shape_ok && source->right &&
            !prime_need_receipt_frames_push(
                &originals, source->right))
            shape_ok = false;
    }
    if (!shape_ok || suffix.len == 0u) {
        prime_need_receipt_frames_free(&suffix);
        prime_need_receipt_frames_free(&originals);
        return false;
    }
    qsort(suffix.items, suffix.len, sizeof(*suffix.items),
          prime_need_receipt_frame_depth_compare);

    PrimeNeedReceiptCopyMap copies;
    if (!prime_need_receipt_copy_map_init(&copies, suffix.len)) {
        prime_need_receipt_frames_free(&suffix);
        prime_need_receipt_frames_free(&originals);
        return false;
    }
    bool ok = true;
    for (size_t i = 0u; ok && i < suffix.len; i++) {
        const PrimeNeedReceiptFrame *source = suffix.items[i];
        const PrimeNeedReceiptFrame *left = source->left;
        const PrimeNeedReceiptFrame *right = source->right;
        if (source->left && source->left->owner == forbidden) {
            left = prime_need_receipt_copy_map_get(&copies, source->left);
            if (!left) {
                ok = false;
                break;
            }
        }
        if (source->right && source->right->owner == forbidden) {
            right = prime_need_receipt_copy_map_get(&copies, source->right);
            if (!right) {
                ok = false;
                break;
            }
        }
        PrimeNeedReceiptFrame *copy = prime_need_receipt_alloc_frame(dst);
        if (!copy) {
            ok = false;
            break;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_PROMOTED_SUFFIX_FRAME);
        *copy = *source;
        copy->left = left;
        copy->right = right;
        copy->owner = dst;
        if (source->has_event) {
            copy->event.before = source->event.before
                ? atom_deep_copy_session_copy(
                      copy_session, source->event.before)
                : NULL;
            copy->event.after = source->event.after
                ? atom_deep_copy_session_copy(
                      copy_session, source->event.after)
                : NULL;
            if ((source->event.before && !copy->event.before) ||
                (source->event.after && !copy->event.after)) {
                ok = false;
                break;
            }
        }
        prime_need_receipt_refresh_arena_summary(copy);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
        prime_need_receipt_build_primary_index(dst, copy);
#endif
        ok = prime_need_receipt_copy_map_put(&copies, source, copy);
    }

    PrimeNeedReceipt promoted = *receipt;
    if (ok && receipt->top->owner == forbidden) {
        promoted.top = prime_need_receipt_copy_map_get(
            &copies, receipt->top);
        ok = promoted.top != NULL;
    }
    if (ok) {
        promoted.owner = promoted.top ? promoted.top->owner : dst;
        ok = prime_need_branch_state_excludes_arena(&promoted, forbidden);
    }
    prime_need_receipt_copy_map_free(&copies);
    prime_need_receipt_frames_free(&suffix);
    prime_need_receipt_frames_free(&originals);
    if (!ok)
        return false;
    *receipt = promoted;
    return true;
}

bool prime_need_branch_state_promote_suffix(
    Arena *dst, const Arena *forbidden, PrimeNeedBranchState *receipt) {
    if (!dst || !forbidden || !receipt || dst == forbidden)
        return false;
    ArenaMark dst_mark = arena_mark(dst);
    AtomDeepCopySession *copy_session =
        atom_deep_copy_session_new(dst);
    if (!copy_session)
        return false;
    bool promoted = prime_need_branch_state_promote_suffix_with_session(
        dst, forbidden, copy_session, receipt);
    atom_deep_copy_session_free(copy_session);
    if (!promoted)
        arena_reset(dst, dst_mark);
    return promoted;
}

bool prime_need_branch_state_excludes_arena(
    const PrimeNeedBranchState *receipt, const Arena *forbidden) {
    PrimeNeedArenaAudit *audit =
        prime_need_arena_audit_new(forbidden);
    if (!audit)
        return false;
    bool excludes = prime_need_arena_audit_branch_state(
        audit, receipt);
    prime_need_arena_audit_free(audit);
    return excludes;
}


void prime_need_snapshot_storage_census(const PrimeNeedSnapshot *snapshot, CettaStorageCensus *census) {
    if (!snapshot) return;
    for (const PrimeNeedFrame *frame = snapshot->top; frame; frame = frame->parent) {
        if (!cetta_storage_object(census, frame, 0u, CETTA_STORAGE_VISITED)) break;
        arena_storage_census(frame->owner, census);
        if (frame->closure_owner) { arena_storage_census(frame->closure_owner, census); break; }
    }
}

static void prime_need_receipt_storage_visit(const void *raw, CettaStorageCensus *census);
static void prime_need_receipt_storage_census(const PrimeNeedReceiptFrame *frame, CettaStorageCensus *census) {
    cetta_storage_schedule(census, frame, prime_need_receipt_storage_visit);
}
static void prime_need_receipt_storage_visit(const void *raw, CettaStorageCensus *census) {
    const PrimeNeedReceiptFrame *frame = raw;
    if (!frame || !cetta_storage_object(census, frame, 0u, CETTA_STORAGE_VISITED)) return;
    arena_storage_census(frame->owner, census);
    if (frame->closure_owner) { arena_storage_census(frame->closure_owner, census); return; }
    prime_need_receipt_storage_census(frame->left, census);
    prime_need_receipt_storage_census(frame->right, census);
}

void prime_need_branch_storage_census(const PrimeNeedBranchState *state, CettaStorageCensus *census) {
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (state && state->recording) {
        if (state->owner != &state->recording->arena)
            prime_need_recording_storage(state->recording->group, census);
        else prime_need_recording_carrier_storage(state->recording->group, census);
        return;
    }
#endif
    if (state) prime_need_receipt_storage_census(state->top, census);
}

void prime_occurrence_storage_census(const PrimeOccurrence *occurrence, CettaStorageCensus *census) {
    if (!occurrence) return;
    prime_need_snapshot_storage_census(&occurrence->prime_need, census);
    prime_need_branch_storage_census(&occurrence->branch_state, census);
    arena_storage_census(occurrence->weights.owner, census);
    prime_need_reader_context_storage(occurrence->reader_context, census);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_branch_storage_census(&occurrence->receipt, census);
#endif
}

#if CETTA_PRIME_NEED_IDENTITY_TEST_HOOKS
static _Atomic uint64_t *prime_need_identity_test_counter(
    PrimeNeedIdentityNamespace ns) {
    switch (ns) {
    case PRIME_NEED_IDENTITY_SESSION:
        return &g_prime_need_next_session;
    case PRIME_NEED_IDENTITY_SERIAL:
        return &g_prime_need_next_serial;
    case PRIME_NEED_IDENTITY_THUNK:
        return &g_prime_need_next_thunk;
    case PRIME_NEED_IDENTITY_AUTHORITY:
        return &g_prime_need_next_authority;
    case PRIME_NEED_IDENTITY_STORAGE_KEY:
        return &g_prime_need_next_storage_key;
    case PRIME_NEED_IDENTITY_RECEIPT_SESSION:
        return &g_prime_need_next_receipt_session;
    case PRIME_NEED_IDENTITY_RECEIPT_NODE:
        return &g_prime_need_next_receipt_node;
    case PRIME_NEED_IDENTITY_SOURCE_OCCURRENCE:
        return &g_prime_need_next_source_occurrence;
    case PRIME_NEED_IDENTITY_EVALUATOR:
        return &g_prime_need_next_evaluator;
    case PRIME_NEED_IDENTITY_RESAMPLE_SCOPE:
        return &g_prime_need_next_resample_scope;
    case PRIME_NEED_IDENTITY_NAMESPACE_COUNT:
        break;
    }
    return NULL;
}

const char *prime_need_identity_test_name(PrimeNeedIdentityNamespace ns) {
    static const char *const names[PRIME_NEED_IDENTITY_NAMESPACE_COUNT] = {
        [PRIME_NEED_IDENTITY_SESSION] = "session",
        [PRIME_NEED_IDENTITY_SERIAL] = "serial",
        [PRIME_NEED_IDENTITY_THUNK] = "thunk",
        [PRIME_NEED_IDENTITY_AUTHORITY] = "authority",
        [PRIME_NEED_IDENTITY_STORAGE_KEY] = "storage-key",
        [PRIME_NEED_IDENTITY_RECEIPT_SESSION] = "receipt-session",
        [PRIME_NEED_IDENTITY_RECEIPT_NODE] = "receipt-node",
        [PRIME_NEED_IDENTITY_SOURCE_OCCURRENCE] = "source-occurrence",
        [PRIME_NEED_IDENTITY_EVALUATOR] = "evaluator",
        [PRIME_NEED_IDENTITY_RESAMPLE_SCOPE] = "resample-scope",
    };
    return (unsigned)ns < PRIME_NEED_IDENTITY_NAMESPACE_COUNT
        ? names[ns] : NULL;
}

uint64_t prime_need_identity_test_last(PrimeNeedIdentityNamespace ns) {
    return ns == PRIME_NEED_IDENTITY_THUNK
        ? PRIME_NEED_NAMED_CELL_BIT - 1u
        : UINT64_MAX - 1u;
}

bool prime_need_identity_test_seed(PrimeNeedIdentityNamespace ns,
                                   uint64_t next) {
    _Atomic uint64_t *counter = prime_need_identity_test_counter(ns);
    if (!counter || next == 0u)
        return false;
    atomic_store_explicit(counter, next, memory_order_relaxed);
    return true;
}

uint64_t prime_need_identity_test_next(PrimeNeedIdentityNamespace ns) {
    _Atomic uint64_t *counter = prime_need_identity_test_counter(ns);
    return counter ? atomic_load_explicit(counter, memory_order_relaxed)
                   : 0u;
}

uint64_t prime_need_identity_test_take(PrimeNeedIdentityNamespace ns) {
    switch (ns) {
    case PRIME_NEED_IDENTITY_THUNK:
        return prime_need_fresh_thunk_id();
    case PRIME_NEED_IDENTITY_SOURCE_OCCURRENCE:
        return prime_need_fresh_source_occurrence();
    case PRIME_NEED_IDENTITY_EVALUATOR:
        return prime_need_fresh_evaluator_id();
    case PRIME_NEED_IDENTITY_RESAMPLE_SCOPE:
        return prime_need_fresh_resample_scope();
    default: {
        _Atomic uint64_t *counter = prime_need_identity_test_counter(ns);
        return counter ? prime_need_fresh_nonzero(counter) : 0u;
    }
    }
}

void prime_need_identity_test_reserve_storage_keys_through(uint64_t key) {
    prime_need_reserve_storage_keys_through(key);
}

/* CETTA_TEST_PRIME_NEED_IDENTITY_SEED="thunk=last,serial=exhausted,session=7"
 * places namespaces before main runs, so a whole program can meet an
 * exhausted namespace.  `last` is the namespace's last identity and
 * `exhausted` is UINT64_MAX.  A malformed seed aborts the test binary. */
__attribute__((constructor))
static void prime_need_identity_test_seed_from_environment(void) {
    const char *spec = getenv("CETTA_TEST_PRIME_NEED_IDENTITY_SEED");
    if (!spec)
        return;
    const char *cursor = spec;
    while (*cursor) {
        const char *equals = strchr(cursor, '=');
        if (!equals)
            goto malformed;
        size_t name_len = (size_t)(equals - cursor);
        int found = -1;
        for (int ns = 0; ns < PRIME_NEED_IDENTITY_NAMESPACE_COUNT; ns++) {
            const char *name = prime_need_identity_test_name(
                (PrimeNeedIdentityNamespace)ns);
            if (strlen(name) == name_len &&
                strncmp(cursor, name, name_len) == 0)
                found = ns;
        }
        if (found < 0)
            goto malformed;
        const char *value = equals + 1;
        const char *end = strchr(value, ',');
        if (!end)
            end = value + strlen(value);
        size_t value_len = (size_t)(end - value);
        uint64_t next = 0u;
        if (value_len == 4u && strncmp(value, "last", 4u) == 0) {
            next = prime_need_identity_test_last(
                (PrimeNeedIdentityNamespace)found);
        } else if (value_len == 9u && strncmp(value, "exhausted", 9u) == 0) {
            next = UINT64_MAX;
        } else {
            if (value_len == 0u)
                goto malformed;
            for (const char *digit = value; digit < end; digit++) {
                unsigned d = (unsigned)(*digit - '0');
                if (d > 9u || next > (UINT64_MAX - d) / 10u)
                    goto malformed;
                next = next * 10u + d;
            }
        }
        if (!prime_need_identity_test_seed(
                (PrimeNeedIdentityNamespace)found, next))
            goto malformed;
        cursor = *end ? end + 1 : end;
    }
    return;
malformed:
    fprintf(stderr, "malformed CETTA_TEST_PRIME_NEED_IDENTITY_SEED: %s\n",
            spec);
    abort();
}
#endif

void prime_need_checkpoint_issuance(CettaCheckpointIssuance *out) {
    if(!out)return;
#define NI(role,cell) out->next[role]=atomic_load_explicit(&(cell),memory_order_relaxed)
    NI(CETTA_CHECKPOINT_NEED_SESSION,g_prime_need_next_session);
    NI(CETTA_CHECKPOINT_NEED_SERIAL,g_prime_need_next_serial);
    NI(CETTA_CHECKPOINT_NEED_THUNK,g_prime_need_next_thunk);
    NI(CETTA_CHECKPOINT_NEED_AUTHORITY,g_prime_need_next_authority);
    NI(CETTA_CHECKPOINT_NEED_EVALUATOR,g_prime_need_next_evaluator);
    NI(CETTA_CHECKPOINT_NEED_STORAGE,g_prime_need_next_storage_key);
    NI(CETTA_CHECKPOINT_RECEIPT_SESSION,g_prime_need_next_receipt_session);
    NI(CETTA_CHECKPOINT_RECEIPT_NODE,g_prime_need_next_receipt_node);
    NI(CETTA_CHECKPOINT_SOURCE_OCCURRENCE,g_prime_need_next_source_occurrence);
#undef NI
}

#if CETTA_PRIME_NEED_HEAP_INDEX
/* A renamed thunk may occupy different radix branches. Read the actual
 * initialized tree, checking its full lookup relation against this immutable
 * chronological path; do not rebuild it or call the charged lookup service. */
static bool prime_need_compare_index_tick(const CettaCheckpointCompare *read,
    const char *name,uint64_t value,bool right) {
    return read->physical(read->context,name,right?0u:value,right?value:0u);
}
static const PrimeNeedFrame *prime_need_compare_path_key(const PrimeNeedFrame *top,
    uint64_t key,bool lineage,const CettaCheckpointCompare *read,bool right,bool *ok) {
    uint64_t previous=UINT64_MAX;
    for(const PrimeNeedFrame *f=top;f;f=f->parent) {
        if(!prime_need_compare_index_tick(read,"need.index_path_probe",1u,right)||
            !f->depth||f->depth>=previous){*ok=false;return NULL;}
        previous=f->depth;
        if((lineage?f->depth:f->thunk_id)==key)return f;
    }
    return NULL;
}
static bool prime_need_compare_index_walk(const PrimeNeedHeapIndexNode *node,
    const PrimeNeedFrame *top,unsigned depth,uint64_t key,bool lineage,
    const CettaCheckpointCompare *read,bool right,size_t *leaves) {
    if(!node||depth>PRIME_NEED_HEAP_INDEX_LEVELS||
        !prime_need_compare_index_tick(read,"need.index_node",1u,right))return false;
    if(node->child_count!=prime_need_popcount16(node->child_mask))return false;
    if(depth==PRIME_NEED_HEAP_INDEX_LEVELS) {
        bool ok=true;
        const PrimeNeedFrame *expected=prime_need_compare_path_key(top,key,lineage,read,right,&ok);
        if(!ok||node->child_mask||!expected||node->value!=expected||*leaves==SIZE_MAX)return false;
        (*leaves)++;return true;
    }
    if(node->value||!node->child_count)return false;
    size_t ordinal=0u;
    for(unsigned digit=0u;digit<16u;digit++)if(node->child_mask&(UINT16_C(1)<<digit)) {
        if(!prime_need_compare_index_walk(node->children[ordinal++],top,depth+1u,
            (key<<4u)|digit,lineage,read,right,leaves))return false;
    }
    return ordinal==node->child_count;
}
static bool prime_need_compare_index_valid(const PrimeNeedHeapIndexNode *root,
    const PrimeNeedFrame *top,bool lineage,const CettaCheckpointCompare *read,bool right,
    size_t *entries) {
    *entries=0u;if(!root)return true;
    if(!prime_need_compare_index_walk(root,top,0u,0u,lineage,read,right,entries))return false;
    size_t expected=0u;uint64_t previous=UINT64_MAX;
    for(const PrimeNeedFrame *f=top;f;f=f->parent) {
        if(!prime_need_compare_index_tick(read,"need.index_coverage",1u,right)||
            !f->depth||f->depth>=previous)return false;
        previous=f->depth;
        bool ok=true;uint64_t key=lineage?f->depth:f->thunk_id;
        if(prime_need_compare_path_key(top,key,lineage,read,right,&ok)==f) {
            if(expected==SIZE_MAX)return false;
            expected++;
        }
        if(!ok)return false;
    }
    return expected==*entries;
}
static bool prime_need_compare_indexes(const PrimeNeedHeapIndexNode *a,
    const PrimeNeedFrame *ap,const PrimeNeedHeapIndexNode *b,const PrimeNeedFrame *bp,
    bool lineage,const CettaCheckpointCompare *read) {
    if((!a)!=(!b)||!read->scalar(read->context,"need.index_present",a!=NULL,b!=NULL)||
        !read->physical(read->context,"need.index_storage",(uintptr_t)a,(uintptr_t)b))return false;
    size_t ac=0u,bc=0u;
    return prime_need_compare_index_valid(a,ap,lineage,read,false,&ac)&&
        prime_need_compare_index_valid(b,bp,lineage,read,true,&bc)&&
        read->scalar(read->context,"need.index_entries",ac,bc);
}
#endif

/* The caller holds both complete paths throughout this synchronous read.
 * Read fuel, allocation refusal and the actual external owner admission are
 * supplied by the visitor; this is not a mutation or replay interpreter. */
bool prime_need_snapshot_compare(const PrimeNeedSnapshot *left,
    const PrimeNeedSnapshot *right,const CettaCheckpointCompare *read) {
    if(!left||!right||!cetta_checkpoint_compare_valid(read))return false;
#define PN_SCALAR(name,a,b) do { if(!read->scalar(read->context,name,(uint64_t)(a),(uint64_t)(b)))return false; } while(0)
#define PN_ID(name,role,a,b) do { if(!read->identity(read->context,name,role,(uint64_t)(a),(uint64_t)(b)))return false; } while(0)
#define PN_PTR(name,a,b) do { if(!read->address(read->context,name,a,b))return false; } while(0)
#define PN_ATOM(name,a,b) do { if(!read->atom(read->context,name,a,b))return false; } while(0)
    PN_PTR("need.top",left->top,right->top);
    if(!read->arena(read->context,"need.owner",left->owner,right->owner))return false;
    if(!read->arena(read->context,"need.closure_owner",left->closure_owner,right->closure_owner))return false;
    PN_ID("need.session",CETTA_CHECKPOINT_NEED_SESSION,left->session_id,right->session_id);
    PN_ID("need.max_storage",CETTA_CHECKPOINT_NEED_STORAGE,left->max_storage_key,right->max_storage_key);
#if CETTA_PRIME_NEED_HEAP_INDEX
    if(!prime_need_compare_indexes(left->heap_index,left->top,right->heap_index,right->top,false,read)||
        !prime_need_compare_indexes(left->lineage_index,left->top,right->lineage_index,right->top,true,read))return false;
#endif
    const PrimeNeedFrame *a=left->top,*b=right->top;
    uint64_t preceding_a=UINT64_MAX,preceding_b=UINT64_MAX;
    while(a&&b) {
        /* Depth is maintained by the original constructor. Checking strict
         * descent also makes a malformed cyclic path a refusal. */
        if(!a->depth||!b->depth||a->depth>=preceding_a||b->depth>=preceding_b)return false;
        preceding_a=a->depth;preceding_b=b->depth;
        PN_PTR("need.frame",a,b);PN_PTR("need.parent",a->parent,b->parent);
        /* The constructor selects self for a named cell, otherwise the
         * parent's newest named frame. Compare the actual alias edge too. */
        if(a->named_top!=((a->thunk_id&PRIME_NEED_NAMED_CELL_BIT)?a:
                a->parent?a->parent->named_top:NULL)||
           b->named_top!=((b->thunk_id&PRIME_NEED_NAMED_CELL_BIT)?b:
                b->parent?b->parent->named_top:NULL))return false;
        PN_PTR("need.named_top",a->named_top,b->named_top);
        if(!read->arena(read->context,"need.frame_owner",a->owner,b->owner))return false;
        if(!read->arena(read->context,"need.frame_closure_owner",a->closure_owner,b->closure_owner))return false;
        if(!read->physical(read->context,"need.arena_bloom",a->arena_bloom,b->arena_bloom)||
            !read->physical(read->context,"need.arena_min",a->arena_min_id,b->arena_min_id)||
            !read->physical(read->context,"need.arena_max",a->arena_max_id,b->arena_max_id)||
            !read->physical(read->context,"need.arena_range_complete",a->arena_range_complete,b->arena_range_complete)||
            !read->physical(read->context,"need.arena_bloom_complete",a->arena_bloom_complete,b->arena_bloom_complete))return false;
        PN_SCALAR("need.depth",a->depth,b->depth);
        PN_ID("need.frame_session",CETTA_CHECKPOINT_NEED_SESSION,a->session_id,b->session_id);
        PN_ID("need.serial",CETTA_CHECKPOINT_NEED_SERIAL,a->serial,b->serial);
        PN_ID("need.update",CETTA_CHECKPOINT_NEED_SERIAL,a->update_id,b->update_id);
        PN_ID("need.thunk",CETTA_CHECKPOINT_NEED_THUNK,a->thunk_id,b->thunk_id);
        PN_ID("need.authority",CETTA_CHECKPOINT_NEED_AUTHORITY,a->authority_id,b->authority_id);
        PN_ID("need.evaluator",CETTA_CHECKPOINT_NEED_EVALUATOR,a->evaluator_id,b->evaluator_id);
        PN_ID("need.storage",CETTA_CHECKPOINT_NEED_STORAGE,a->storage_key,b->storage_key);
        PN_ID("need.import",CETTA_CHECKPOINT_NEED_IMPORT,a->import_key,b->import_key);
        PN_ID("need.source_occurrence",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,a->source_occurrence_id,b->source_occurrence_id);
        PN_SCALAR("need.source_argument",a->source_argument_index,b->source_argument_index);
        PN_SCALAR("need.cache_state",a->cache_state,b->cache_state);
        PN_SCALAR("need.source_role",a->source_role,b->source_role);
        PN_SCALAR("need.value_role",a->value_role,b->value_role);
        PN_ID("need.value_producer",CETTA_CHECKPOINT_NEED_THUNK,a->value_producer_thunk,b->value_producer_thunk);
        PN_ID("need.value_producer_update",CETTA_CHECKPOINT_NEED_SERIAL,a->value_producer_update,b->value_producer_update);
        PN_ATOM("need.origin",a->origin,b->origin);PN_ATOM("need.cached",a->cached,b->cached);
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        PN_SCALAR("need.capture_known",a->capture_known,b->capture_known);
        PN_SCALAR("need.capture_count",a->capture_var_count,b->capture_var_count);
        PN_PTR("need.capture_storage",a->capture_var_ids,b->capture_var_ids);
        if(a->capture_var_count!=b->capture_var_count||
            (a->capture_var_count&&(!a->capture_var_ids||!b->capture_var_ids)))return false;
        for(size_t i=0u;i<a->capture_var_count;i++)
            if(!cetta_checkpoint_var(read,"need.capture_variable",a->capture_var_ids[i],b->capture_var_ids[i]))return false;
#endif
#if CETTA_PRIME_NEED_HEAP_INDEX
        if(!prime_need_compare_indexes(a->heap_index,a,b->heap_index,b,false,read)||
            !prime_need_compare_indexes(a->lineage_index,a,b->lineage_index,b,true,read))return false;
#endif
        a=a->parent;b=b->parent;
    }
    PN_PTR("need.path_end",a,b);
    return !a&&!b;
#undef PN_SCALAR
#undef PN_ID
#undef PN_PTR
#undef PN_ATOM
}

static bool prime_need_receipt_frame_compare(const PrimeNeedReceiptFrame *a,
    const PrimeNeedReceiptFrame *b,const CettaCheckpointCompare *read) {
#define RF_S(n,x,y) do { if(!read->scalar(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RF_F(n,x,y) do { if(!read->physical(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RF_P(n,x,y) do { if(!read->address(read->context,n,x,y))return false; } while(0)
#define RF_I(n,r,x,y) do { if(!read->identity(read->context,n,r,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RF_A(n,x,y) do { if(!read->atom(read->context,n,x,y))return false; } while(0)
    if(!a||!b)return !a&&!b;
    RF_P("receipt.frame",a,b);RF_P("receipt.frame_left",a->left,b->left);
    RF_P("receipt.frame_right",a->right,b->right);if(!read->arena(read->context,"receipt.frame_owner",a->owner,b->owner))return false;
    if(!read->arena(read->context,"receipt.frame_closure",a->closure_owner,b->closure_owner))return false;
    RF_F("receipt.arena_bloom",a->arena_bloom,b->arena_bloom);
    RF_F("receipt.arena_min",a->arena_min_id,b->arena_min_id);
    RF_F("receipt.arena_max",a->arena_max_id,b->arena_max_id);
    RF_F("receipt.arena_range_complete",a->arena_range_complete,b->arena_range_complete);
    RF_F("receipt.arena_bloom_complete",a->arena_bloom_complete,b->arena_bloom_complete);
    RF_I("receipt.frame_session",CETTA_CHECKPOINT_RECEIPT_SESSION,a->session_id,b->session_id);
    RF_I("receipt.frame_identity",CETTA_CHECKPOINT_RECEIPT_NODE,a->identity_id,b->identity_id);
    RF_I("receipt.frame_event",CETTA_CHECKPOINT_RECEIPT_NODE,a->event_id,b->event_id);
    RF_S("receipt.frame_depth",a->depth,b->depth);
    RF_S("receipt.frame_has_event",a->has_event,b->has_event);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    RF_P("receipt.allocation_next",a->recording_next,b->recording_next);
    RF_S("receipt.referenced",a->recording_referenced,b->recording_referenced);
#endif
    if(a->has_event!=b->has_event)return false;
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    RF_S("receipt.primary_depth",a->primary_depth,b->primary_depth);
    RF_S("receipt.primary_count",a->primary_ancestor_count,b->primary_ancestor_count);
    if(a->primary_ancestor_count!=b->primary_ancestor_count||
        (a->primary_ancestor_count&&(!a->primary_ancestors||!b->primary_ancestors)))return false;
    for(size_t i=0u;i<a->primary_ancestor_count;i++)
        RF_P("receipt.primary_ancestor",a->primary_ancestors[i],b->primary_ancestors[i]);
#endif
    if(!a->has_event)return true;
    const PrimeNeedReceiptEvent *p=&a->event,*q=&b->event;
    RF_S("receipt.frame_kind",p->kind,q->kind);
    RF_I("receipt.frame_event_id",CETTA_CHECKPOINT_RECEIPT_NODE,p->event_id,q->event_id);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    RF_I("receipt.frame_need",CETTA_CHECKPOINT_NEED_SESSION,p->need_session_id,q->need_session_id);
    RF_I("receipt.frame_thunk",CETTA_CHECKPOINT_NEED_THUNK,p->thunk_id,q->thunk_id);
    RF_I("receipt.frame_occurrence",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,p->source_occurrence_id,q->source_occurrence_id);
    RF_S("receipt.frame_argument",p->source_argument_index,q->source_argument_index);
    RF_I("receipt.frame_rule",CETTA_CHECKPOINT_RULE_OCCURRENCE,p->rule_occurrence_id,q->rule_occurrence_id);
    RF_I("receipt.frame_update",CETTA_CHECKPOINT_NEED_SERIAL,p->producer_update_id,q->producer_update_id);
    RF_S("receipt.frame_completed",p->producer_completed,q->producer_completed);
    RF_I("receipt.frame_state_event",CETTA_CHECKPOINT_RECEIPT_NODE,p->state_event_id,q->state_event_id);
#endif
    RF_P("receipt.frame_state_cell",p->state_cell,q->state_cell);
    RF_A("receipt.frame_before",p->before,q->before);RF_A("receipt.frame_after",p->after,q->after);
    return true;
#undef RF_S
#undef RF_F
#undef RF_P
#undef RF_I
#undef RF_A
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
bool prime_need_recording_compare(const PrimeNeedRecording *a,
    const PrimeNeedRecording *b,const CettaCheckpointCompare *read) {
    if(!cetta_checkpoint_compare_valid(read))return false;
#define RG_S(n,x,y) do { if(!read->scalar(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RG_F(n,x,y) do { if(!read->physical(read->context,n,(uint64_t)(x),(uint64_t)(y)))return false; } while(0)
#define RG_P(n,x,y) do { if(!read->address(read->context,n,x,y))return false; } while(0)
    RG_P("recording.group",a,b);
    if(!a||!b)return !a&&!b;
    size_t ar=atomic_load_explicit(&a->references,memory_order_relaxed);
    size_t br=atomic_load_explicit(&b->references,memory_order_relaxed);
    if(!ar||!br||a->slice_count!=b->slice_count||a->slice_count>a->limits.slices||
        b->slice_count>b->limits.slices)return false;
    RG_F("recording.references",ar,br);
    RG_F("recording.payload_references",
        atomic_load_explicit(&a->payload_references,memory_order_relaxed),
        atomic_load_explicit(&b->payload_references,memory_order_relaxed));
    RG_S("recording.sealed",a->sealed,b->sealed);
    RG_S("recording.owner_links",atomic_load_explicit(&a->owner_links,memory_order_relaxed),
        atomic_load_explicit(&b->owner_links,memory_order_relaxed));
    RG_S("recording.node_limit",a->limits.nodes,b->limits.nodes);
    RG_S("recording.byte_limit",a->limits.payload_bytes,b->limits.payload_bytes);
    RG_S("recording.slice_limit",a->limits.slices,b->limits.slices);
    RG_S("recording.owner_limit",a->limits.owner_links,b->limits.owner_links);
    if(!read->identity(read->context,"recording.session",CETTA_CHECKPOINT_RECEIPT_SESSION,
        a->session_id,b->session_id))return false;
    RG_S("recording.slice_count",a->slice_count,b->slice_count);
    RG_S("recording.publication_revision",a->publication_revision,b->publication_revision);
    RG_S("recording.reserved_nodes",a->reserved_nodes,b->reserved_nodes);
    RG_S("recording.reserved_bytes",a->reserved_bytes,b->reserved_bytes);
    RG_S("recording.allocation_bytes",a->allocation_bytes,b->allocation_bytes);
    RG_S("recording.metadata_bound",a->metadata_bound,b->metadata_bound);
    for(size_t i=0u;i<a->slice_count;i++) {
        const PrimeNeedRecordingSlice *x=&a->slices[i],*y=&b->slices[i];
        if(x->group!=a||y->group!=b||x->nodes!=y->nodes||x->nodes>x->node_limit||
            y->nodes>y->node_limit||x->charge>x->byte_limit||y->charge>y->byte_limit)return false;
        RG_S("recording.slice_index",i,i);RG_P("recording.slice",x,y);
        RG_P("recording.slice_group",x->group,y->group);if(!read->arena(read->context,"recording.slice_arena",&x->arena,&y->arena))return false;
        RG_S("recording.slice_node_quantum",x->node_quantum,y->node_quantum);
        RG_S("recording.slice_byte_quantum",x->byte_quantum,y->byte_quantum);
        RG_S("recording.slice_growing",x->growing,y->growing);
        RG_S("recording.slice_node_limit",x->node_limit,y->node_limit);
        RG_S("recording.slice_byte_limit",x->byte_limit,y->byte_limit);
        RG_S("recording.slice_nodes",x->nodes,y->nodes);RG_S("recording.slice_charge",x->charge,y->charge);
        RG_S("recording.slice_omitted",atomic_load_explicit(&x->omitted,memory_order_relaxed),
            atomic_load_explicit(&y->omitted,memory_order_relaxed));
        RG_S("recording.slice_incomplete",atomic_load_explicit(&x->incomplete,memory_order_relaxed),
            atomic_load_explicit(&y->incomplete,memory_order_relaxed));
        RG_S("recording.slice_overflow",atomic_load_explicit(&x->overflow,memory_order_relaxed),
            atomic_load_explicit(&y->overflow,memory_order_relaxed));
        RG_P("recording.first",x->first,y->first);RG_P("recording.last",x->last,y->last);
        RG_P("recording.published_root",x->published_root,y->published_root);
        const PrimeNeedReceiptFrame *p=x->first,*q=y->first,*pl=NULL,*ql=NULL;
        for(size_t j=0u;j<x->nodes;j++) {
            if(!p||!q||p->owner!=&x->arena||q->owner!=&y->arena||
                !prime_need_receipt_frame_compare(p,q,read))return false;
            pl=p;ql=q;p=p->recording_next;q=q->recording_next;
        }
        if(p||q||pl!=x->last||ql!=y->last)return false;
    }
    return true;
#undef RG_S
#undef RG_F
#undef RG_P
}
#endif

static bool prime_need_compare_service(const CettaCheckpointCompare *read,
    const char *site,size_t work,size_t bytes,size_t *scratch) {
    if(bytes>SIZE_MAX-*scratch || !read->service(read->context,site,work,bytes))return false;
    *scratch+=bytes;return true;
}

static bool prime_need_compare_receipt_push(PrimeNeedReceiptFrames *frames,
    const PrimeNeedReceiptFrame *frame,const CettaCheckpointCompare *read,bool right,size_t *scratch) {
    if(!frame)return true;
    for(size_t i=0u;i<frames->len;i++) {
        if(!read->physical(read->context,"receipt.preflight_member",right?0u:1u,right?1u:0u))return false;
        if(frames->items[i]==frame)return true;
    }
    size_t n=frames->len+1u;
    if(!n||n>SIZE_MAX/128u||n>SIZE_MAX/n||n*n>SIZE_MAX/8u)return false;
    /* The existing unique-frame collector reserves a power-of-two pointer
     * vector and half-full index; this conservative reservation covers its
     * live and replacement arrays, including failed allocation attempts. */
    size_t minimum=n<16u?16u:n;
    if(minimum>SIZE_MAX/(16u*sizeof(void *))||
        !prime_need_compare_service(read,"receipt.collect_push",8u*n*n,
            minimum*16u*sizeof(void *),scratch))return false;
    return prime_need_receipt_frames_push(frames,frame);
}
static bool prime_need_compare_receipt_collect(const PrimeNeedReceiptFrame *top,
    PrimeNeedReceiptFrames *frames,const CettaCheckpointCompare *read,bool right,size_t *scratch) {
    if(!prime_need_compare_receipt_push(frames,top,read,right,scratch))return false;
    for(size_t i=0u;i<frames->len;i++) {
        const PrimeNeedReceiptFrame *frame=frames->items[i];
        if(!read->physical(read->context,"receipt.preflight_frame",right?0u:1u,right?1u:0u)||
            !prime_need_compare_receipt_push(frames,frame->left,read,right,scratch)||
            !prime_need_compare_receipt_push(frames,frame->right,read,right,scratch))return false;
    }
    return true;
}
/* Operational state exists even when causal recording is compiled out.
 * Compare its complete held DAG through the same bounded field visitor;
 * no observation graph is constructed and no source state is modified. */
bool prime_need_branch_state_compare(const PrimeNeedBranchState *left,
    const PrimeNeedBranchState *right,const CettaCheckpointCompare *read) {
    if(!left||!right||!cetta_checkpoint_compare_valid(read))return false;
    if((!left->session_id)!=(!left->owner)||(!right->session_id)!=(!right->owner)||
        (left->top&&!left->session_id)||(right->top&&!right->session_id))return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if(left->recording||right->recording)return false;
#endif
    bool ok=false;size_t scratch=0u;PrimeNeedReceiptFrames a={0},b={0};
    if(!prime_need_compare_receipt_collect(left->top,&a,read,false,&scratch)||
        !prime_need_compare_receipt_collect(right->top,&b,read,true,&scratch)||a.len!=b.len)
        goto done;
    if(!read->address(read->context,"branch_state.top",left->top,right->top)||
        !read->arena(read->context,"branch_state.owner",left->owner,right->owner)||
        !read->identity(read->context,"branch_state.session",CETTA_CHECKPOINT_RECEIPT_SESSION,
            left->session_id,right->session_id)||
        !read->scalar(read->context,"branch_state.frames",a.len,b.len))goto done;
    for(size_t i=0u;i<a.len;i++) {
        const PrimeNeedReceiptFrame *pair[2]={a.items[i],b.items[i]};
        const uint64_t sessions[2]={left->session_id,right->session_id};
        for(size_t side=0u;side<2u;side++) {
            const PrimeNeedReceiptFrame *frame=pair[side];
            if(!frame->identity_id||!frame->depth||frame->session_id!=sessions[side]||
                (frame->has_event!=(frame->event_id!=0u)))goto done;
            if(frame->has_event) {
                if(frame->event.event_id!=frame->event_id||frame->right||
                    (frame->event_id!=frame->identity_id &&
                     frame->event.kind!=PRIME_NEED_RECEIPT_READ_STATE &&
                     frame->event.kind!=PRIME_NEED_RECEIPT_WRITE_STATE)||
                    (unsigned)frame->event.kind>PRIME_NEED_RECEIPT_ADMISSION_PENDING)goto done;
            } else if(frame->event.event_id||!frame->left||!frame->right||
                frame->left->identity_id==frame->right->identity_id)goto done;
            const PrimeNeedReceiptFrame *parents[2]={frame->left,frame->right};
            for(size_t j=0u;j<2u;j++)if(parents[j]&&
                (parents[j]->depth>=frame->depth||
                 parents[j]->identity_id>=frame->identity_id))goto done;
        }
        if(!prime_need_receipt_frame_compare(pair[0],pair[1],read))goto done;
    }
    ok=true;
done:
    prime_need_receipt_frames_free(&a);prime_need_receipt_frames_free(&b);
    if(scratch&&read->service_release)read->service_release(read->context,scratch);
    return ok;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static bool prime_need_compare_graph_service(size_t n,const CettaCheckpointCompare *read,size_t *scratch) {
    /* collect_all(NULL) and graph_collect have no buffer allocation. */
    if(!n)return prime_need_compare_service(read,"receipt.graph_collect_validate",1u,0u,scratch);
    size_t minimum=n<16u?16u:n;
    if(n&&n>SIZE_MAX/n)return false;
    if(n>SIZE_MAX/16u||(n&&n*n>SIZE_MAX/16u)||
        minimum>SIZE_MAX/(32u*sizeof(void *))||n>SIZE_MAX/(2u*sizeof(PrimeNeedReceiptGraphNode)))return false;
    size_t pointers=minimum*32u*sizeof(void *),nodes=n*2u*sizeof(PrimeNeedReceiptGraphNode);
    if(pointers>SIZE_MAX-nodes)return false;
    /* Reuse graph_collect/validate only after the entire reachable native
     * population is held and bounded. Its pointer hash, sort and validator
     * work is bounded here separately from callback field-read fuel. */
    return prime_need_compare_service(read,"receipt.graph_collect_validate",
        n*n*16u,pointers+nodes,scratch);
}

/* graph_collect coalesces promoted storage copies of one receipt identity.
 * Its payload check can otherwise enter atom_eq, whose work is independent
 * of the number of receipt frames. This finite reader admits that service
 * only when duplicate payload roots are already the same physical roots.
 * Separate equal promoted roots require a bounded payload adapter. */
static bool prime_need_compare_duplicate_payloads(const PrimeNeedReceiptFrames *frames,
    const CettaCheckpointCompare *read,bool right) {
    for(size_t i=0u;i<frames->len;i++) {
        const PrimeNeedReceiptFrame *a=frames->items[i];
        for(size_t j=0u;j<i;j++) {
            if(!read->physical(read->context,"receipt.duplicate_payload_probe",
                right?0u:1u,right?1u:0u))return false;
            const PrimeNeedReceiptFrame *b=frames->items[j];
            if(a->identity_id==b->identity_id&&a->has_event&&b->has_event&&
                (a->event.before!=b->event.before||a->event.after!=b->event.after))
                return false;
        }
    }
    return true;
}

bool prime_need_receipt_compare(const PrimeNeedReceipt *left,
    const PrimeNeedReceipt *right,const CettaCheckpointCompare *read) {
    if(!left||!right||!cetta_checkpoint_compare_valid(read))return false;
    PrimeNeedReceipt absent = {0};
    if (left->recording && !prime_need_recording_readable(left)) {
        if (!prime_need_recording_erased(left)) return false;
        left = &absent;
    }
    if (right->recording && !prime_need_recording_readable(right)) {
        if (!prime_need_recording_erased(right)) return false;
        right = &absent;
    }
    bool ok=false;size_t scratch=0u;PrimeNeedReceiptGraph a={0},b={0};
    PrimeNeedReceiptFrames af={0},bf={0};
    PrimeNeedReceiptGraphStatus sa=PRIME_NEED_RECEIPT_GRAPH_REFUSED,sb=PRIME_NEED_RECEIPT_GRAPH_REFUSED;
    if(!prime_need_compare_receipt_collect(left->top,&af,read,false,&scratch)||
        !prime_need_compare_receipt_collect(right->top,&bf,read,true,&scratch)||af.len!=bf.len||
        !prime_need_compare_duplicate_payloads(&af,read,false)||
        !prime_need_compare_duplicate_payloads(&bf,read,true)||
        !prime_need_compare_graph_service(af.len,read,&scratch)||!prime_need_compare_graph_service(bf.len,read,&scratch))goto done;
    atom_hash_read_scope_enter();
    sa=prime_need_receipt_graph_collect(left,&a);
    sb=prime_need_receipt_graph_collect(right,&b);
    atom_hash_read_scope_leave();
#define PR_SCALAR(name,x,y) do { if(!read->scalar(read->context,name,(uint64_t)(x),(uint64_t)(y)))goto done; } while(0)
#define PR_ID(name,role,x,y) do { if(!read->identity(read->context,name,role,(uint64_t)(x),(uint64_t)(y)))goto done; } while(0)
#define PR_PTR(name,x,y) do { if(!read->address(read->context,name,x,y))goto done; } while(0)
#define PR_ATOM(name,x,y) do { if(!read->atom(read->context,name,x,y))goto done; } while(0)
    PR_SCALAR("receipt.status",sa,sb);
    if(sa!=sb||(sa!=PRIME_NEED_RECEIPT_GRAPH_ABSENT&&sa!=PRIME_NEED_RECEIPT_GRAPH_COMPLETE))goto done;
    PR_PTR("receipt.top",left->top,right->top);if(!read->arena(read->context,"receipt.owner",left->owner,right->owner))goto done;
    PR_PTR("receipt.recording",left->recording,right->recording);
    PR_ID("receipt.session",CETTA_CHECKPOINT_RECEIPT_SESSION,a.session_id,b.session_id);
    PR_ID("receipt.root",CETTA_CHECKPOINT_RECEIPT_NODE,a.root_id,b.root_id);
    PR_SCALAR("receipt.length",a.length,b.length);PR_SCALAR("receipt.events",a.event_count,b.event_count);
    PR_SCALAR("receipt.incomplete",a.incomplete,b.incomplete);
    if(a.length!=b.length||a.incomplete||b.incomplete)goto done;
    for(size_t i=0u;i<a.length;i++) {
        const PrimeNeedReceiptGraphNode *x=&a.nodes[i],*y=&b.nodes[i];
        PR_ID("receipt.node",CETTA_CHECKPOINT_RECEIPT_NODE,x->identity_id,y->identity_id);
        PR_ID("receipt.left",CETTA_CHECKPOINT_RECEIPT_NODE,x->left_id,y->left_id);
        PR_ID("receipt.right",CETTA_CHECKPOINT_RECEIPT_NODE,x->right_id,y->right_id);
        PR_SCALAR("receipt.has_event",x->has_event,y->has_event);
        if(x->has_event!=y->has_event)goto done;
        if(!x->has_event)continue;
        const PrimeNeedReceiptEvent *p=&x->event,*q=&y->event;
        PR_SCALAR("receipt.kind",p->kind,q->kind);
        PR_ID("receipt.event",CETTA_CHECKPOINT_RECEIPT_NODE,p->event_id,q->event_id);
        PR_ID("receipt.need_session",CETTA_CHECKPOINT_NEED_SESSION,p->need_session_id,q->need_session_id);
        PR_ID("receipt.thunk",CETTA_CHECKPOINT_NEED_THUNK,p->thunk_id,q->thunk_id);
        PR_ID("receipt.source_occurrence",CETTA_CHECKPOINT_SOURCE_OCCURRENCE,p->source_occurrence_id,q->source_occurrence_id);
        PR_SCALAR("receipt.source_argument",p->source_argument_index,q->source_argument_index);
        PR_ID("receipt.rule",CETTA_CHECKPOINT_RULE_OCCURRENCE,p->rule_occurrence_id,q->rule_occurrence_id);
        PR_ID("receipt.producer_update",CETTA_CHECKPOINT_NEED_SERIAL,p->producer_update_id,q->producer_update_id);
        PR_SCALAR("receipt.producer_completed",p->producer_completed,q->producer_completed);
        PR_ID("receipt.state_event",CETTA_CHECKPOINT_RECEIPT_NODE,p->state_event_id,q->state_event_id);
        PR_PTR("receipt.state_cell",p->state_cell,q->state_cell);
        PR_ATOM("receipt.before",p->before,q->before);PR_ATOM("receipt.after",p->after,q->after);
    }
    for(size_t i=0u;i<af.len;i++)
        if(!prime_need_receipt_frame_compare(af.items[i],bf.items[i],read))goto done;
    ok=true;
done:
    prime_need_receipt_frames_free(&af);prime_need_receipt_frames_free(&bf);
    prime_need_receipt_graph_free(&a);prime_need_receipt_graph_free(&b);
    /* Only local vector/index/graph buffers used these reservations.
     * The callback relation retains source frame/Atom references,
     * never these temporary arrays. Paid work remains consumed. */
    if(scratch&&read->service_release)read->service_release(read->context,scratch);
    return ok;
#undef PR_SCALAR
#undef PR_ID
#undef PR_PTR
#undef PR_ATOM
}
#endif
