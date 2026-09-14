#include "prime_need.h"
#include "stats.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_PRIME_RECEIPT_PRIMARY_INDEX
#define CETTA_PRIME_RECEIPT_PRIMARY_INDEX 0
#endif

struct PrimeNeedFrame {
    const PrimeNeedFrame *parent;
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
    uint64_t thunk_id;
    uint64_t depth;
    uint64_t authority_id;
    uint64_t evaluator_id;
    uint64_t storage_key;
    uint64_t import_key;
    uint64_t source_occurrence_id;
    uint64_t source_argument_index;
    PrimeNeedCacheState cache_state;
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
    int selected =
        setting && setting[0] == '1' && setting[1] == '\0';
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

static _Atomic uint64_t g_prime_need_next_session = 1u;
static _Atomic uint64_t g_prime_need_next_serial = 1u;
static _Atomic uint64_t g_prime_need_next_thunk = 1u;
static _Atomic uint64_t g_prime_need_next_authority = 1u;
static _Atomic uint64_t g_prime_need_next_storage_key = 1u;
static _Atomic uint64_t g_prime_need_next_receipt_session = 1u;
static _Atomic uint64_t g_prime_need_next_receipt_node = 1u;
static _Atomic uint64_t g_prime_need_next_source_occurrence = 1u;

static void prime_need_reserve_storage_keys_through(uint64_t key) {
    if (key == UINT64_MAX)
        return;
    uint64_t desired = key + 1u;
    uint64_t current = atomic_load_explicit(
        &g_prime_need_next_storage_key, memory_order_relaxed);
    while (current < desired &&
           !atomic_compare_exchange_weak_explicit(
               &g_prime_need_next_storage_key, &current, desired,
               memory_order_relaxed, memory_order_relaxed)) {
    }
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
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    const PrimeNeedReceiptFrame **primary_ancestors;
    size_t primary_ancestor_count;
    uint64_t primary_depth;
#endif
};

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

static bool prime_need_atom_extend_arena_bloom(
    const Atom *atom, uint64_t *bloom,
    uint32_t *min_id, uint32_t *max_id) {
    if (!atom)
        return true;
    if (!bloom || (atom->flags & ATOM_FLAG_ARENA_CLOSED) == 0u)
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
        ((!frame->event.before || atom_graph_is_closed_for_arena(
             frame->owner, frame->event.before)) &&
         (!frame->event.after || atom_graph_is_closed_for_arena(
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
    uint64_t value = atomic_fetch_add_explicit(
        counter, 1u, memory_order_relaxed);
    if (value != 0u)
        return value;
    return atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
static int prime_need_var_id_compare(const void *left, const void *right) {
    VarId l = *(const VarId *)left;
    VarId r = *(const VarId *)right;
    return l < r ? -1 : (l > r ? 1 : 0);
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
        if (copy[i] == VAR_ID_NONE)
            return false;
        if (unique_count == 0u || copy[unique_count - 1u] != copy[i])
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
    return cell->capture_var_ids && ids &&
        memcmp(cell->capture_var_ids, ids, count * sizeof(*ids)) == 0;
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
        frame = frame->parent;
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

static bool prime_need_snapshot_push(Arena *owner,
                                     const PrimeNeedSnapshot *base,
                                     uint64_t serial,
                                     uint64_t thunk_id,
                                     uint64_t authority_id,
                                     uint64_t evaluator_id,
                                     uint64_t storage_key,
                                     uint64_t import_key,
                                     uint64_t source_occurrence_id,
                                     uint64_t source_argument_index,
                                     PrimeNeedCacheState cache_state,
                                     Atom *origin,
                                     Atom *cached,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
                                     bool capture_known,
                                     const VarId *capture_var_ids,
                                     size_t capture_var_count,
#endif
                                     PrimeNeedSnapshot *out) {
    if (!owner || !base || !out || !prime_need_snapshot_present(base) ||
        thunk_id == 0u || authority_id == 0u || !origin)
        return false;
    PrimeNeedFrame *frame = arena_alloc(owner, sizeof(*frame));
    if (!frame)
        return false;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_FRAME_ALLOC);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_SNAPSHOT_FRAME_BYTES,
        (uint64_t)sizeof(*frame));
    frame->parent = base->top;
    frame->owner = owner;
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
        atom_graph_is_closed_for_arena(owner, origin) &&
        (!cached || atom_graph_is_closed_for_arena(owner, cached));
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    const bool capture_closed =
        !capture_var_ids || arena_owns_ptr(owner, capture_var_ids);
#else
    const bool capture_closed = true;
#endif
    const bool prefix_bloom_complete =
        !base->top || base->top->arena_bloom_complete;
    const bool prefix_range_complete =
        !base->top || base->top->arena_range_complete;
    frame->arena_bloom =
        (base->top ? base->top->arena_bloom : 0u) |
        prime_need_arena_bloom_mask(owner) | payload_bloom;
    frame->arena_min_id = 0u;
    frame->arena_max_id = 0u;
    prime_need_arena_range_extend(
        owner->identity,
        &frame->arena_min_id, &frame->arena_max_id);
    if (base->top) {
        prime_need_arena_range_extend(
            base->top->arena_min_id,
            &frame->arena_min_id, &frame->arena_max_id);
        prime_need_arena_range_extend(
            base->top->arena_max_id,
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
        (!base->top || base->closure_owner == owner)
            ? owner : NULL;
    frame->session_id = base->session_id;
    frame->serial = serial ? serial : prime_need_fresh_nonzero(
        &g_prime_need_next_serial);
    frame->thunk_id = thunk_id;
    frame->depth = base->top ? base->top->depth + 1u : 1u;
    frame->authority_id = authority_id;
    frame->evaluator_id = evaluator_id;
    frame->storage_key = storage_key;
    frame->import_key = import_key;
    frame->source_occurrence_id = source_occurrence_id;
    frame->source_argument_index = source_argument_index;
    frame->cache_state = cache_state;
    frame->origin = origin;
    frame->cached = cached;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    frame->capture_known = capture_known;
    frame->capture_var_ids = capture_var_ids;
    frame->capture_var_count = capture_var_count;
#endif
    if (frame->serial == 0u)
        return false;
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
    out->origin = frame->origin;
    out->cached = frame->cached;
    out->authority_id = frame->authority_id;
    out->evaluator_id = frame->evaluator_id;
    out->storage_key = frame->storage_key;
    out->import_key = frame->import_key;
    out->source_occurrence_id = frame->source_occurrence_id;
    out->source_argument_index = frame->source_argument_index;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    out->capture_known = frame->capture_known;
    out->capture_var_ids = frame->capture_var_ids;
    out->capture_var_count = frame->capture_var_count;
#endif
}

bool prime_need_snapshot_lookup(const PrimeNeedSnapshot *snapshot,
                                uint64_t thunk_id,
                                PrimeNeedCellView *out) {
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
        return true;
    }
#endif
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_LOG_FALLBACK);
    for (const PrimeNeedFrame *frame = snapshot->top; frame;
         frame = frame->parent) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_HEAP_LOOKUP_LOG_FRAME);
        if (frame->session_id == snapshot->session_id &&
            frame->thunk_id == thunk_id) {
            prime_need_cell_view_from_frame(frame, out);
            return true;
        }
    }
    return false;
}

static bool prime_need_snapshot_find_storage_key(
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

static bool prime_need_snapshot_find_imported_cell(
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

static bool prime_need_snapshot_allocate_with_storage_key(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t storage_key, uint64_t source_occurrence_id,
    uint64_t source_argument_index,
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
    if (storage_key == 0u || storage_key_in_use) {
        do {
            storage_key = prime_need_fresh_nonzero(
                &g_prime_need_next_storage_key);
        } while (storage_key <= base->max_storage_key &&
                 prime_need_snapshot_find_storage_key(
                     base, storage_key, NULL));
    }
    uint64_t thunk_id = prime_need_fresh_nonzero(&g_prime_need_next_thunk);
    uint64_t authority_id = prime_need_fresh_nonzero(
        &g_prime_need_next_authority);
    if (!prime_need_snapshot_push(
            owner, base, 0u, thunk_id, authority_id, 0u, storage_key,
            import_key, source_occurrence_id, source_argument_index,
            PRIME_NEED_CACHE_EMPTY, term, NULL,
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
        owner, base, term, 0u, 0u, 0u,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        false, NULL, 0u,
#endif
        out, out_thunk_id);
}

uint64_t prime_need_fresh_source_occurrence(void) {
    return prime_need_fresh_nonzero(&g_prime_need_next_source_occurrence);
}

bool prime_need_snapshot_allocate_source_argument(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    if (source_occurrence_id == 0u)
        return false;
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, source_occurrence_id,
        source_argument_index,
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
        owner, base, term, storage_key, 0u, 0u,
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        false, NULL, 0u,
#endif
        out, out_thunk_id);
}

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
bool prime_need_snapshot_allocate_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, 0u, 0u, true,
        capture_var_ids, capture_var_count, out, out_thunk_id);
}

bool prime_need_snapshot_allocate_source_argument_closure(
    Arena *owner, const PrimeNeedSnapshot *base, Atom *term,
    uint64_t source_occurrence_id, uint64_t source_argument_index,
    const VarId *capture_var_ids, size_t capture_var_count,
    PrimeNeedSnapshot *out, uint64_t *out_thunk_id) {
    if (source_occurrence_id == 0u)
        return false;
    return prime_need_snapshot_allocate_with_storage_key(
        owner, base, term, 0u, source_occurrence_id,
        source_argument_index, true, capture_var_ids,
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
        owner, base, term, storage_key, 0u, 0u, true,
        capture_var_ids, capture_var_count, out, out_thunk_id);
}
#endif

static bool prime_need_snapshot_transition(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    PrimeNeedCacheState expected, PrimeNeedCacheState next,
    uint64_t evaluator_id, Atom *cached, PrimeNeedSnapshot *out) {
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
        owner, base, 0u, thunk_id, previous.authority_id,
        next == PRIME_NEED_CACHE_EVALUATING ? evaluator_id : 0u,
        previous.storage_key, previous.import_key,
        previous.source_occurrence_id, previous.source_argument_index,
        next, previous.origin, cached,
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
        PRIME_NEED_CACHE_EVALUATING, evaluator_id, NULL, out);
}

bool prime_need_snapshot_resolve_value(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, Atom *value, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_VALUE, evaluator_id, value, out);
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
        PRIME_NEED_CACHE_STABLE_FAULT, evaluator_id, fault, out);
#endif
}

bool prime_need_snapshot_retry_evaluation(
    Arena *owner, const PrimeNeedSnapshot *base, uint64_t thunk_id,
    uint64_t evaluator_id, PrimeNeedSnapshot *out) {
    return prime_need_snapshot_transition(
        owner, base, thunk_id, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_EMPTY, evaluator_id, NULL, out);
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
        .max_storage_key = 0u,
        .owner = NULL,
        .closure_owner = NULL,
#if CETTA_PRIME_NEED_HEAP_INDEX
        .heap_index = NULL,
        .lineage_index = NULL,
#endif
    };
    for (uint64_t i = 0u; i < depth; i++) {
        Atom *origin = atom_deep_copy(dst, path[i]->origin);
        Atom *cached = path[i]->cached
            ? atom_deep_copy(dst, path[i]->cached) : NULL;
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
                dst, &promoted, path[i]->serial, path[i]->thunk_id,
                path[i]->authority_id, path[i]->evaluator_id,
                path[i]->storage_key, path[i]->import_key,
                path[i]->source_occurrence_id,
                path[i]->source_argument_index,
                path[i]->cache_state, origin, cached,
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

bool prime_need_snapshot_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedSnapshot *snapshot) {
    if (!dst || !forbidden || !copy_session || !snapshot ||
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
                dst, &promoted, path[i]->serial, path[i]->thunk_id,
                path[i]->authority_id, path[i]->evaluator_id,
                path[i]->storage_key, path[i]->import_key,
                path[i]->source_occurrence_id,
                path[i]->source_argument_index,
                path[i]->cache_state, origin, cached,
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

typedef struct {
    const PrimeNeedReceiptFrame **items;
    size_t len;
    size_t cap;
    const PrimeNeedReceiptFrame **seen;
    size_t seen_cap;
} PrimeNeedReceiptFrames;

static void prime_need_receipt_frames_free(PrimeNeedReceiptFrames *frames) {
    if (!frames)
        return;
    free(frames->items);
    free(frames->seen);
    frames->items = NULL;
    frames->len = 0u;
    frames->cap = 0u;
    frames->seen = NULL;
    frames->seen_cap = 0u;
}

static size_t prime_need_receipt_frame_hash(
    const PrimeNeedReceiptFrame *frame) {
    uintptr_t value = (uintptr_t)frame;
    value ^= value >> 17u;
    value *= (uintptr_t)0xed5ad4bbu;
    value ^= value >> 11u;
    return (size_t)value;
}

static bool prime_need_receipt_frames_contains(
    const PrimeNeedReceiptFrames *frames,
    const PrimeNeedReceiptFrame *frame) {
    if (!frames || !frame)
        return false;
    if (frames->seen && frames->seen_cap > 0u) {
        size_t mask = frames->seen_cap - 1u;
        size_t slot = prime_need_receipt_frame_hash(frame) & mask;
        while (frames->seen[slot]) {
            if (frames->seen[slot] == frame)
                return true;
            slot = (slot + 1u) & mask;
        }
        return false;
    }
    for (size_t i = 0u; i < frames->len; i++)
        if (frames->items[i] == frame)
            return true;
    return false;
}

static bool prime_need_receipt_frames_reserve_seen(
    PrimeNeedReceiptFrames *frames, size_t count) {
    if (!frames)
        return false;
    size_t needed = 16u;
    while (needed / 2u < count) {
        if (needed > SIZE_MAX / 2u)
            return false;
        needed *= 2u;
    }
    if (frames->seen_cap >= needed)
        return true;
    if (needed > SIZE_MAX / sizeof(*frames->seen))
        return false;
    const PrimeNeedReceiptFrame **seen = calloc(
        needed, sizeof(*seen));
    if (!seen)
        return false;
    size_t mask = needed - 1u;
    for (size_t i = 0u; i < frames->len; i++) {
        size_t slot =
            prime_need_receipt_frame_hash(frames->items[i]) & mask;
        while (seen[slot])
            slot = (slot + 1u) & mask;
        seen[slot] = frames->items[i];
    }
    free(frames->seen);
    frames->seen = seen;
    frames->seen_cap = needed;
    return true;
}

static bool prime_need_receipt_frames_push(
    PrimeNeedReceiptFrames *frames,
    const PrimeNeedReceiptFrame *frame) {
    if (!frames || !frame)
        return false;
    if (!prime_need_receipt_frames_reserve_seen(
            frames, frames->len + 1u))
        return false;
    if (prime_need_receipt_frames_contains(frames, frame))
        return true;
    if (frames->len == frames->cap) {
        size_t next = frames->cap ? frames->cap * 2u : 16u;
        if (next < frames->cap ||
            next > SIZE_MAX / sizeof(*frames->items))
            return false;
        const PrimeNeedReceiptFrame **items = realloc(
            frames->items, next * sizeof(*frames->items));
        if (!items)
            return false;
        frames->items = items;
        frames->cap = next;
    }
    size_t mask = frames->seen_cap - 1u;
    size_t slot = prime_need_receipt_frame_hash(frame) & mask;
    while (frames->seen[slot])
        slot = (slot + 1u) & mask;
    frames->seen[slot] = frame;
    frames->items[frames->len++] = frame;
    return true;
}

static bool prime_need_receipt_collect_all(
    const PrimeNeedReceiptFrame *top,
    PrimeNeedReceiptFrames *frames) {
    PrimeNeedReceiptFrames work = {0};
    if (!top)
        return true;
    if (!prime_need_receipt_frames_push(&work, top))
        return false;
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
}

static bool prime_need_carrier_begin(
    Arena *owner, PrimeNeedBranchState *receipt) {
    if (!owner || !receipt)
        return false;
    if (prime_need_branch_state_present(receipt))
        return receipt->owner == owner;
    receipt->top = NULL;
    receipt->session_id = prime_need_fresh_nonzero(
        &g_prime_need_next_receipt_session);
    receipt->owner = owner;
    return receipt->session_id != 0u;
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
    if (!prime_need_receipt_collect_all(left->top, &left_frames) ||
        !prime_need_receipt_collect_all(right->top, &right_frames)) {
        prime_need_receipt_frames_free(&left_frames);
        prime_need_receipt_frames_free(&right_frames);
        return false;
    }
    bool compatible = true;
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
    return compatible;
}

static PrimeNeedReceiptFrame *prime_need_receipt_alloc_frame(Arena *owner) {
    PrimeNeedReceiptFrame *frame =
        owner ? arena_alloc(owner, sizeof(PrimeNeedReceiptFrame)) : NULL;
    if (frame) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_FRAME_ALLOC);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_PRIME_NEED_CARRIER_FRAME_BYTES,
            sizeof(PrimeNeedReceiptFrame));
    }
    return frame;
}

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
    uint64_t identity_id = prime_need_fresh_nonzero(
        &g_prime_need_next_receipt_node);
    if (identity_id == 0u)
        return false;
    PrimeNeedReceiptFrame *join = prime_need_receipt_alloc_frame(owner);
    if (!join)
        return false;
    *join = (PrimeNeedReceiptFrame){
        .left = dst->top,
        .right = src->top,
        .owner = owner,
        .session_id = dst->session_id,
        .identity_id = identity_id,
        .event_id = 0u,
        .depth = (dst->top->depth > src->top->depth
                      ? dst->top->depth : src->top->depth) + 1u,
        .has_event = false,
    };
    prime_need_receipt_refresh_arena_summary(join);
#if CETTA_PRIME_RECEIPT_PRIMARY_INDEX
    prime_need_receipt_build_primary_index(owner, join);
#endif
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
    return prime_need_receipt_append_with_lifetime(
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

static bool prime_need_receipt_inspect_origin_event(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t need_session_id, uint64_t thunk_id, Atom *origin_view,
    bool branch_local, PrimeNeedReceipt *out) {
    if (need_session_id == 0u || thunk_id == 0u || !origin_view)
        return false;
    return prime_need_receipt_append_with_lifetime(
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
    return prime_need_branch_state_read(owner, base, cell, value, out);
}

bool prime_need_receipt_write_state(
    Arena *owner, const PrimeNeedReceipt *base, StateCell *cell,
    Atom *before, Atom *after, PrimeNeedReceipt *out) {
    return prime_need_branch_state_write(
        owner, base, cell, before, after, out);
}

static bool prime_need_receipt_use_equation_event(
    Arena *owner, const PrimeNeedReceipt *base,
    uint64_t source_occurrence_id, uint64_t rule_occurrence_id,
    Atom *equation, Atom *result, bool branch_local,
    PrimeNeedReceipt *out) {
    if (source_occurrence_id == 0u || rule_occurrence_id == 0u ||
        (equation == NULL) != (result == NULL))
        return false;
    return prime_need_receipt_append_with_lifetime(
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

static bool prime_need_receipt_resample_event(
    Arena *owner, const PrimeNeedReceipt *base, Atom *origin,
    bool branch_local, PrimeNeedReceipt *out) {
    if (!origin)
        return false;
    return prime_need_receipt_append_with_lifetime(
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

bool prime_need_branch_state_value(const PrimeNeedBranchState *state,
                                   StateCell *cell, Atom **out_value) {
    if (!state || !cell || !out_value ||
        !prime_need_branch_state_present(state))
        return false;
    PrimeNeedReceiptFrames frames = {0};
    if (!prime_need_receipt_collect_all(state->top, &frames))
        return false;
    const PrimeNeedReceiptFrame *latest = NULL;
    for (size_t i = 0u; i < frames.len; i++) {
        const PrimeNeedReceiptFrame *frame = frames.items[i];
        if (!frame->has_event || frame->event.state_cell != cell ||
            (frame->event.kind != PRIME_NEED_RECEIPT_READ_STATE &&
             frame->event.kind != PRIME_NEED_RECEIPT_WRITE_STATE))
            continue;
        if (!latest || frame->depth > latest->depth ||
            (frame->depth == latest->depth &&
             frame->event_id > latest->event_id))
            latest = frame;
    }
    prime_need_receipt_frames_free(&frames);
    if (!latest || !latest->event.after)
        return false;
    *out_value = latest->event.after;
    return true;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
bool prime_need_receipt_state_value(const PrimeNeedReceipt *receipt,
                                    StateCell *cell, Atom **out_value) {
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
    return true;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
size_t prime_need_receipt_event_count(const PrimeNeedReceipt *receipt) {
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
        equal = a.items[i]->event_id == b.items[i]->event_id;
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
    size_t slot = prime_need_receipt_frame_hash(source) & mask;
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
    size_t slot = prime_need_receipt_frame_hash(source) & mask;
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

bool prime_need_branch_state_promote(
    Arena *dst, PrimeNeedBranchState *receipt) {
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
                ? atom_deep_copy(dst, source->event.before) : NULL;
            copy->event.after = source->event.after
                ? atom_deep_copy(dst, source->event.after) : NULL;
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
    prime_need_receipt_copy_map_free(&copies);
    prime_need_receipt_frames_free(&originals);
    return true;

fail:
    prime_need_receipt_copy_map_free(&copies);
    prime_need_receipt_frames_free(&originals);
    return false;
}

bool prime_need_branch_state_promote_suffix_with_session(
    Arena *dst, const Arena *forbidden,
    AtomDeepCopySession *copy_session, PrimeNeedBranchState *receipt) {
    if (!dst || !forbidden || !copy_session || !receipt ||
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
