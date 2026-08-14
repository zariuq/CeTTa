#include "match.h"
#include "stats.h"
#include "term_universe.h"
#include "variant_shape.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Bindings ───────────────────────────────────────────────────────────── */

/* Cycle-safety ownership is intentionally split by representation:
 *
 * - bindings_has_loop rejects cycles in the metavariable substitution graph;
 * - bindings_dereference_limit bounds variable-only graph walks by the size of
 *   that graph, catching recurrence without imposing a semantic depth budget;
 * - MatchPathSet rejects recurrence on the active structural-match path while
 *   permitting shared finite DAG subterms;
 * - the object-binder ABT traversals in abt.c reject cyclic term graphs and
 *   enforce de Bruijn scope, but do not own metavariable substitutions.
 *
 * These guards cover different graphs and are not interchangeable.  Removing
 * one requires a falsifier showing that another guard covers the same graph.
 */

#define BINDINGS_MIN_CAPACITY 8
#define BINDINGS_SEEN_STACK_CAP 32
#define BINDINGS_TEMP_STACK_CAP 32
#define BINDINGS_POOL_CLASS_COUNT 4
#define BINDINGS_MEMO_STACK_CAP 32
#define BINDINGS_MEMO_INDEX_THRESHOLD 16u
#define BINDINGS_LOOKUP_CACHE_MISS UINT32_MAX
#define BINDINGS_LOOKUP_INDEX_THRESHOLD 16u
#define FRESHEN_EPOCH_MEMO_INLINE_CAP 64u
#define VAR_ID_SET_INLINE_CAP 8u

enum {
    BINDINGS_CYCLE_UNKNOWN = 0u,
    BINDINGS_CYCLE_ACYCLIC = 1u,
    BINDINGS_CYCLE_PRESENT = 2u,
};

enum {
    BINDINGS_DERIVED_LEGACY_NONZERO = 1u << 0,
    BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO = 1u << 1,
    BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO = 1u << 2,
};

typedef enum {
    BINDINGS_REACHABILITY_UNKNOWN = 0,
    BINDINGS_REACHABILITY_ABSENT = 1,
    BINDINGS_REACHABILITY_PRESENT = 2,
} BindingsReachability;

typedef struct {
    VarId id;
    uint32_t index_plus_one;
} BindingsLookupIndexSlot;

struct BindingsLookupIndex {
    _Atomic uint32_t references;
    BindingsLookupIndexSlot *slots;
    size_t capacity;
    uint32_t count;
    uint32_t synced_len;
    bool has_duplicates;
};

static __thread int g_bindings_lookup_index_enabled = -1;
static _Atomic uint64_t g_bindings_builder_instance_counter = 1u;

typedef struct BindingPoolBlock {
    struct BindingPoolBlock *next;
} BindingPoolBlock;

typedef struct {
    const Atom *src;
    Atom *dst;
} FreshenEpochMemoSlot;

typedef struct {
    FreshenEpochMemoSlot inline_slots[FRESHEN_EPOCH_MEMO_INLINE_CAP];
    FreshenEpochMemoSlot *slots;
    size_t cap;
    size_t used;
    uint64_t inline_occupied;
} FreshenEpochMemo;

_Static_assert(FRESHEN_EPOCH_MEMO_INLINE_CAP == 64u,
               "inline memo occupancy mask must cover every slot");

static const uint32_t BINDINGS_POOL_CAPS[BINDINGS_POOL_CLASS_COUNT] = {8, 16, 32, 64};
static __thread BindingPoolBlock *g_binding_entry_pools[BINDINGS_POOL_CLASS_COUNT];
static __thread BindingPoolBlock *g_binding_constraint_pools[BINDINGS_POOL_CLASS_COUNT];
static __thread size_t g_binding_entry_active_bytes = 0;
static __thread size_t g_binding_entry_pool_bytes = 0;
static __thread size_t g_binding_entry_retained_bytes = 0;
static __thread size_t g_binding_constraint_active_bytes = 0;
static __thread size_t g_binding_constraint_pool_bytes = 0;
static __thread size_t g_binding_constraint_retained_bytes = 0;

static void bindings_pool_free_all(BindingPoolBlock **pools) {
    for (uint32_t i = 0; i < BINDINGS_POOL_CLASS_COUNT; i++) {
        BindingPoolBlock *block = pools[i];
        while (block) {
            BindingPoolBlock *next = block->next;
            free(block);
            block = next;
        }
        pools[i] = NULL;
    }
}

void bindings_thread_cache_free(void) {
    bindings_pool_free_all(g_binding_entry_pools);
    bindings_pool_free_all(g_binding_constraint_pools);
    g_binding_entry_active_bytes = 0;
    g_binding_entry_pool_bytes = 0;
    g_binding_entry_retained_bytes = 0;
    g_binding_constraint_active_bytes = 0;
    g_binding_constraint_pool_bytes = 0;
    g_binding_constraint_retained_bytes = 0;
}

static void bindings_note_entry_pool_metrics(void) {
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_POOL_BYTES,
                            (uint64_t)g_binding_entry_pool_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_POOL_BYTES_PEAK,
        (uint64_t)g_binding_entry_pool_bytes);
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_RETAINED_BYTES,
                            (uint64_t)g_binding_entry_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_RETAINED_BYTES_PEAK,
        (uint64_t)g_binding_entry_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_ENTRY_ACTIVE_BYTES_PEAK,
        (uint64_t)g_binding_entry_active_bytes);
}

static void bindings_note_constraint_pool_metrics(void) {
    cetta_runtime_stats_set(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_POOL_BYTES,
        (uint64_t)g_binding_constraint_pool_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_POOL_BYTES_PEAK,
        (uint64_t)g_binding_constraint_pool_bytes);
    cetta_runtime_stats_set(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_RETAINED_BYTES,
        (uint64_t)g_binding_constraint_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_RETAINED_BYTES_PEAK,
        (uint64_t)g_binding_constraint_retained_bytes);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_ACTIVE_BYTES_PEAK,
        (uint64_t)g_binding_constraint_active_bytes);
}

size_t bindings_entry_active_bytes(void) {
    return g_binding_entry_active_bytes;
}

size_t bindings_constraint_active_bytes(void) {
    return g_binding_constraint_active_bytes;
}

static inline bool binding_var_eq(VarId lhs, VarId rhs) {
    return lhs == rhs;
}

static bool atom_contains_private_variant_var(const Atom *atom) {
    return atom_has_private_variant_vars(atom);
}

static bool binding_contains_private_variant_slot(const Binding *binding) {
    return binding &&
           (variant_private_var_id(binding->var_id) ||
            atom_contains_private_variant_var(binding->val));
}

static bool constraint_contains_private_variant_slot(
    const BindingConstraint *constraint) {
    return constraint &&
           (atom_contains_private_variant_var(constraint->lhs) ||
            atom_contains_private_variant_var(constraint->rhs));
}

static void bindings_private_counts_slow(
    const Bindings *bindings, uint32_t *entries, uint32_t *constraints) {
    uint32_t entry_count = 0u;
    uint32_t constraint_count = 0u;
    if (bindings) {
        for (uint32_t i = 0u; i < bindings->len; i++)
            entry_count +=
                binding_contains_private_variant_slot(
                    &bindings->entries[i]) ? 1u : 0u;
        for (uint32_t i = 0u; i < bindings->eq_len; i++)
            constraint_count +=
                constraint_contains_private_variant_slot(
                    &bindings->constraints[i]) ? 1u : 0u;
    }
    if (entries)
        *entries = entry_count;
    if (constraints)
        *constraints = constraint_count;
}

static uint32_t bindings_legacy_fallback_count_slow(
    const Bindings *bindings) {
    uint32_t count = 0u;
    if (bindings) {
        for (uint32_t i = 0u; i < bindings->len; i++)
            count += bindings->entries[i].legacy_name_fallback ? 1u : 0u;
    }
    return count;
}

static uint8_t bindings_derived_nonzero(const Bindings *bindings) {
    if (!bindings)
        return 0u;
    uint8_t flags = 0u;
    if (bindings->legacy_fallback_count != 0u)
        flags |= BINDINGS_DERIVED_LEGACY_NONZERO;
    if (bindings->private_entry_count != 0u)
        flags |= BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO;
    if (bindings->private_constraint_count != 0u)
        flags |= BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO;
    return flags;
}

/*
 * A builder trail restores logical lengths.  These counts are accelerators,
 * not logical state, so the compact trail remembers only whether a scan can
 * be necessary and rebuilds exact values on that cold rollback path.
 */
static void bindings_restore_derived_counts(
    Bindings *bindings, uint8_t nonzero) {
    bindings->legacy_fallback_count =
        (nonzero & BINDINGS_DERIVED_LEGACY_NONZERO)
            ? bindings_legacy_fallback_count_slow(bindings)
            : 0u;
    if (nonzero & (BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO |
                   BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO)) {
        uint32_t entries = 0u;
        uint32_t constraints = 0u;
        bindings_private_counts_slow(
            bindings, &entries, &constraints);
        bindings->private_entry_count =
            (nonzero & BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO)
                ? entries
                : 0u;
        bindings->private_constraint_count =
            (nonzero & BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO)
                ? constraints
                : 0u;
    } else {
        bindings->private_entry_count = 0u;
        bindings->private_constraint_count = 0u;
    }
}

static bool bindings_private_audit_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("CETTA_BINDINGS_DERIVED_AUDIT");
        enabled = value && value[0] == '1';
    }
    return enabled != 0;
}

bool bindings_contains_private_variant_slots(const Bindings *b) {
    if (!b)
        return false;
    bool derived =
        b->private_entry_count != 0u ||
        b->private_constraint_count != 0u;
    if (bindings_private_audit_enabled()) {
        uint32_t entries = 0u;
        uint32_t constraints = 0u;
        bindings_private_counts_slow(b, &entries, &constraints);
        assert(entries == b->private_entry_count);
        assert(constraints == b->private_constraint_count);
    }
    return derived;
}

void bindings_assert_no_private_variant_slots(const Bindings *b) {
#ifndef NDEBUG
    assert(!bindings_contains_private_variant_slots(b));
#else
    (void)b;
#endif
}

static int bindings_pool_class(uint32_t cap) {
    for (uint32_t i = 0; i < BINDINGS_POOL_CLASS_COUNT; i++) {
        if (BINDINGS_POOL_CAPS[i] == cap)
            return (int)i;
    }
    return -1;
}

static Binding *bindings_entries_alloc(uint32_t cap) {
    int klass = bindings_pool_class(cap);
    size_t bytes = sizeof(Binding) * cap;
    if (klass >= 0 && g_binding_entry_pools[klass]) {
        BindingPoolBlock *block = g_binding_entry_pools[klass];
        g_binding_entry_pools[klass] = block->next;
        if (g_binding_entry_pool_bytes >= bytes)
            g_binding_entry_pool_bytes -= bytes;
        else
            g_binding_entry_pool_bytes = 0;
        g_binding_entry_active_bytes += bytes;
        bindings_note_entry_pool_metrics();
        return (Binding *)block;
    }
    g_binding_entry_active_bytes += bytes;
    g_binding_entry_retained_bytes += bytes;
    bindings_note_entry_pool_metrics();
    return cetta_malloc(bytes);
}

static void bindings_entries_release(Binding *entries, uint32_t cap) {
    size_t bytes;
    if (!entries) return;
    bytes = sizeof(Binding) * cap;
    int klass = bindings_pool_class(cap);
    if (klass < 0) {
        if (g_binding_entry_active_bytes >= bytes)
            g_binding_entry_active_bytes -= bytes;
        else
            g_binding_entry_active_bytes = 0;
        if (g_binding_entry_retained_bytes >= bytes)
            g_binding_entry_retained_bytes -= bytes;
        else
            g_binding_entry_retained_bytes = 0;
        bindings_note_entry_pool_metrics();
        free(entries);
        return;
    }
    if (g_binding_entry_active_bytes >= bytes)
        g_binding_entry_active_bytes -= bytes;
    else
        g_binding_entry_active_bytes = 0;
    g_binding_entry_pool_bytes += bytes;
    BindingPoolBlock *block = (BindingPoolBlock *)entries;
    block->next = g_binding_entry_pools[klass];
    g_binding_entry_pools[klass] = block;
    bindings_note_entry_pool_metrics();
}

static BindingConstraint *bindings_constraints_alloc(uint32_t cap) {
    int klass = bindings_pool_class(cap);
    size_t bytes = sizeof(BindingConstraint) * cap;
    if (klass >= 0 && g_binding_constraint_pools[klass]) {
        BindingPoolBlock *block = g_binding_constraint_pools[klass];
        g_binding_constraint_pools[klass] = block->next;
        if (g_binding_constraint_pool_bytes >= bytes)
            g_binding_constraint_pool_bytes -= bytes;
        else
            g_binding_constraint_pool_bytes = 0;
        g_binding_constraint_active_bytes += bytes;
        bindings_note_constraint_pool_metrics();
        return (BindingConstraint *)block;
    }
    g_binding_constraint_active_bytes += bytes;
    g_binding_constraint_retained_bytes += bytes;
    bindings_note_constraint_pool_metrics();
    return cetta_malloc(bytes);
}

static void bindings_constraints_release(BindingConstraint *constraints, uint32_t cap) {
    size_t bytes;
    if (!constraints) return;
    bytes = sizeof(BindingConstraint) * cap;
    int klass = bindings_pool_class(cap);
    if (klass < 0) {
        if (g_binding_constraint_active_bytes >= bytes)
            g_binding_constraint_active_bytes -= bytes;
        else
            g_binding_constraint_active_bytes = 0;
        if (g_binding_constraint_retained_bytes >= bytes)
            g_binding_constraint_retained_bytes -= bytes;
        else
            g_binding_constraint_retained_bytes = 0;
        bindings_note_constraint_pool_metrics();
        free(constraints);
        return;
    }
    if (g_binding_constraint_active_bytes >= bytes)
        g_binding_constraint_active_bytes -= bytes;
    else
        g_binding_constraint_active_bytes = 0;
    g_binding_constraint_pool_bytes += bytes;
    BindingPoolBlock *block = (BindingPoolBlock *)constraints;
    block->next = g_binding_constraint_pools[klass];
    g_binding_constraint_pools[klass] = block;
    bindings_note_constraint_pool_metrics();
}

static BindingConstraint *bindings_temp_constraints_alloc(
    uint32_t cap, BindingConstraint *stack, uint32_t stack_cap) {
    if (cap <= stack_cap)
        return stack;
    return bindings_constraints_alloc(cap);
}

static void bindings_temp_constraints_release(BindingConstraint *constraints,
                                              uint32_t cap,
                                              BindingConstraint *stack) {
    if (!constraints || constraints == stack)
        return;
    bindings_constraints_release(constraints, cap);
}

static Atom *bindings_lookup_spelling(Bindings *b, SymbolId spelling);

static size_t bindings_var_id_hash(VarId id) {
    uint64_t x = (uint64_t)id;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (size_t)x;
}

static bool bindings_lookup_index_enabled(void) {
    if (g_bindings_lookup_index_enabled < 0) {
        const char *setting = getenv("CETTA_BINDINGS_LOOKUP_INDEX");
        g_bindings_lookup_index_enabled =
            !(setting && setting[0] == '0') ? 1 : 0;
    }
    return g_bindings_lookup_index_enabled != 0;
}

static BindingsLookupIndex *bindings_lookup_index_alloc(size_t capacity) {
    BindingsLookupIndex *index = cetta_malloc(sizeof(*index));
    atomic_init(&index->references, 1u);
    index->slots =
        cetta_malloc(capacity * sizeof(*index->slots));
    memset(index->slots, 0, capacity * sizeof(*index->slots));
    index->capacity = capacity;
    index->count = 0u;
    index->synced_len = 0u;
    index->has_duplicates = false;
    return index;
}

static void bindings_lookup_index_retain(BindingsLookupIndex *index) {
    if (!index)
        return;
    uint32_t previous = atomic_fetch_add_explicit(
        &index->references, 1u, memory_order_relaxed);
    assert(previous > 0u && previous < UINT32_MAX);
}

static void bindings_lookup_index_release(BindingsLookupIndex *index) {
    if (!index)
        return;
    uint32_t previous = atomic_fetch_sub_explicit(
        &index->references, 1u, memory_order_acq_rel);
    assert(previous > 0u);
    if (previous == 1u) {
        free(index->slots);
        free(index);
    }
}

static size_t bindings_lookup_index_capacity_for_len(uint32_t len) {
    size_t needed = (size_t)len * 2u;
    if (needed < len)
        return 0u;
    size_t capacity = 32u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u)
            return 0u;
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(BindingsLookupIndexSlot))
        return 0u;
    return capacity;
}

static bool bindings_lookup_index_insert_raw(
    BindingsLookupIndex *index, VarId id, uint32_t entry_index) {
    if (!index || id == VAR_ID_NONE || index->capacity == 0u)
        return false;
    size_t mask = index->capacity - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (index->slots[slot].id != VAR_ID_NONE &&
           !binding_var_eq(index->slots[slot].id, id)) {
        slot = (slot + 1u) & mask;
    }
    if (index->slots[slot].id == VAR_ID_NONE) {
        index->slots[slot].id = id;
        index->count++;
    } else if (index->slots[slot].index_plus_one != entry_index + 1u) {
        index->has_duplicates = true;
    }
    index->slots[slot].index_plus_one = entry_index + 1u;
    return true;
}

static bool bindings_lookup_index_rehash(BindingsLookupIndex *index,
                                         size_t capacity) {
    if (!index || capacity < 32u ||
        (capacity & (capacity - 1u)) != 0u ||
        capacity > SIZE_MAX / sizeof(*index->slots)) {
        return false;
    }
    BindingsLookupIndexSlot *old_slots = index->slots;
    size_t old_capacity = index->capacity;
    BindingsLookupIndexSlot *slots =
        cetta_malloc(capacity * sizeof(*slots));
    memset(slots, 0, capacity * sizeof(*slots));
    index->slots = slots;
    index->capacity = capacity;
    index->count = 0u;
    for (size_t i = 0u; i < old_capacity; i++) {
        if (old_slots[i].id == VAR_ID_NONE)
            continue;
        if (!bindings_lookup_index_insert_raw(
                index, old_slots[i].id,
                old_slots[i].index_plus_one - 1u)) {
            free(old_slots);
            return false;
        }
    }
    free(old_slots);
    return true;
}

static BindingsLookupIndex *bindings_lookup_index_build(
    const Bindings *bindings, uint32_t len) {
    size_t capacity = bindings_lookup_index_capacity_for_len(len);
    if (capacity == 0u)
        return NULL;
    BindingsLookupIndex *index =
        bindings_lookup_index_alloc(capacity);
    for (uint32_t i = 0u; i < len; i++) {
        if (!bindings_lookup_index_insert_raw(
                index, bindings->entries[i].var_id, i)) {
            bindings_lookup_index_release(index);
            return NULL;
        }
    }
    index->synced_len = len;
    return index;
}

static bool bindings_lookup_index_detach(Bindings *bindings) {
    BindingsLookupIndex *index = bindings->lookup_index;
    if (!index ||
        atomic_load_explicit(
            &index->references, memory_order_acquire) == 1u) {
        return true;
    }
    BindingsLookupIndex *copy =
        bindings_lookup_index_alloc(index->capacity);
    memcpy(copy->slots, index->slots,
           index->capacity * sizeof(*copy->slots));
    copy->count = index->count;
    copy->synced_len = index->synced_len;
    copy->has_duplicates = index->has_duplicates;
    bindings_lookup_index_release(index);
    bindings->lookup_index = copy;
    return true;
}

static uint32_t bindings_lookup_index_find(
    const BindingsLookupIndex *index, VarId id) {
    if (!index || id == VAR_ID_NONE || index->capacity == 0u)
        return 0u;
    size_t mask = index->capacity - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (index->slots[slot].id != VAR_ID_NONE) {
        if (binding_var_eq(index->slots[slot].id, id))
            return index->slots[slot].index_plus_one;
        slot = (slot + 1u) & mask;
    }
    return 0u;
}

static void bindings_lookup_index_delete_unique(
    BindingsLookupIndex *index, VarId id) {
    if (!index || id == VAR_ID_NONE || index->capacity == 0u)
        return;
    size_t mask = index->capacity - 1u;
    size_t hole = bindings_var_id_hash(id) & mask;
    while (index->slots[hole].id != VAR_ID_NONE &&
           !binding_var_eq(index->slots[hole].id, id)) {
        hole = (hole + 1u) & mask;
    }
    if (index->slots[hole].id == VAR_ID_NONE)
        return;

    size_t scan = (hole + 1u) & mask;
    while (index->slots[scan].id != VAR_ID_NONE) {
        size_t home =
            bindings_var_id_hash(index->slots[scan].id) & mask;
        size_t scan_distance = (scan - home) & mask;
        size_t hole_distance = (hole - home) & mask;
        if (hole_distance < scan_distance) {
            index->slots[hole] = index->slots[scan];
            hole = scan;
        }
        scan = (scan + 1u) & mask;
    }
    memset(&index->slots[hole], 0, sizeof(index->slots[hole]));
    assert(index->count > 0u);
    index->count--;
}

static void bindings_lookup_index_truncate(Bindings *bindings,
                                           uint32_t new_len) {
    BindingsLookupIndex *index = bindings->lookup_index;
    if (!index || index->synced_len <= new_len)
        return;
    if (!bindings_lookup_index_detach(bindings))
        return;
    index = bindings->lookup_index;

    if (index->has_duplicates) {
        BindingsLookupIndex *replacement =
            new_len >= BINDINGS_LOOKUP_INDEX_THRESHOLD
                ? bindings_lookup_index_build(bindings, new_len)
                : NULL;
        bindings_lookup_index_release(index);
        bindings->lookup_index = replacement;
        return;
    }

    for (uint32_t i = index->synced_len; i > new_len; i--) {
        VarId id = bindings->entries[i - 1u].var_id;
        uint32_t found = bindings_lookup_index_find(index, id);
        if (found == i)
            bindings_lookup_index_delete_unique(index, id);
    }
    index->synced_len = new_len;
    if (new_len < BINDINGS_LOOKUP_INDEX_THRESHOLD) {
        bindings_lookup_index_release(index);
        bindings->lookup_index = NULL;
    }
}

static BindingsLookupIndex *bindings_lookup_index_sync(Bindings *bindings) {
    if (!bindings_lookup_index_enabled() ||
        bindings->len < BINDINGS_LOOKUP_INDEX_THRESHOLD) {
        return NULL;
    }
    if (!bindings->lookup_index) {
        bindings->lookup_index =
            bindings_lookup_index_build(bindings, bindings->len);
        return bindings->lookup_index;
    }
    BindingsLookupIndex *index = bindings->lookup_index;
    if (index->synced_len > bindings->len) {
        bindings_lookup_index_truncate(bindings, bindings->len);
        index = bindings->lookup_index;
        if (!index)
            return NULL;
    }
    if (index->synced_len == bindings->len)
        return index;
    if (!bindings_lookup_index_detach(bindings))
        return NULL;
    index = bindings->lookup_index;
    size_t required =
        bindings_lookup_index_capacity_for_len(bindings->len);
    if (required == 0u)
        return NULL;
    if (required > index->capacity &&
        !bindings_lookup_index_rehash(index, required)) {
        return NULL;
    }
    for (uint32_t i = index->synced_len; i < bindings->len; i++) {
        if (!bindings_lookup_index_insert_raw(
                index, bindings->entries[i].var_id, i)) {
            bindings_lookup_index_release(index);
            bindings->lookup_index = NULL;
            return NULL;
        }
    }
    index->synced_len = bindings->len;
    return index;
}

static inline BindingsLookupIndex *bindings_lookup_index_current(
        Bindings *bindings) {
    BindingsLookupIndex *index = bindings->lookup_index;
    if (index && index->synced_len == bindings->len)
        return index;
    /* Appends deliberately leave a shared index at its existing prefix.
     * Synchronizing the suffix only when a lookup needs it avoids a
     * copy-on-write mutation on every binding write. */
    return bindings_lookup_index_sync(bindings);
}

#ifdef CETTA_TEST_HOOKS
void bindings_lookup_index_test_clear(Bindings *bindings) {
    if (!bindings)
        return;
    bindings_lookup_index_release(bindings->lookup_index);
    bindings->lookup_index = NULL;
}
#endif

static inline void bindings_lookup_cache_reset(Bindings *b) {
    b->lookup_cache_count = 0;
    b->lookup_cache_next = 0;
}

static inline void bindings_lookup_cache_note(Bindings *b, VarId var_id,
                                              uint32_t index) {
    for (uint32_t i = 0; i < b->lookup_cache_count; i++) {
        if (binding_var_eq(b->lookup_cache_ids[i], var_id)) {
            b->lookup_cache_indices[i] = index;
            return;
        }
    }
    uint32_t slot =
        b->lookup_cache_count < CETTA_BINDINGS_LOOKUP_CACHE_SLOTS
        ? b->lookup_cache_count++
        : b->lookup_cache_next;
    b->lookup_cache_ids[slot] = var_id;
    b->lookup_cache_indices[slot] = index;
    if (b->lookup_cache_count == CETTA_BINDINGS_LOOKUP_CACHE_SLOTS) {
        b->lookup_cache_next = (uint8_t)(
            (slot + 1u) % CETTA_BINDINGS_LOOKUP_CACHE_SLOTS);
    }
}

static int32_t bindings_lookup_index(Bindings *b, VarId var_id) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP);
    for (uint32_t i = 0; i < b->lookup_cache_count; i++) {
        uint32_t idx = b->lookup_cache_indices[i];
        if (binding_var_eq(b->lookup_cache_ids[i], var_id) &&
            idx == BINDINGS_LOOKUP_CACHE_MISS) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_CACHE_HIT);
            return -1;
        }
        if (idx < b->len &&
            binding_var_eq(b->lookup_cache_ids[i], var_id) &&
            binding_var_eq(b->entries[idx].var_id, var_id)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_CACHE_HIT);
            return (int32_t)idx;
        }
    }
    BindingsLookupIndex *index = bindings_lookup_index_current(b);
    if (index) {
        uint32_t index_plus_one =
            bindings_lookup_index_find(index, var_id);
        if (index_plus_one > 0u) {
            uint32_t idx = index_plus_one - 1u;
            if (idx < b->len &&
                binding_var_eq(b->entries[idx].var_id, var_id)) {
                bindings_lookup_cache_note(b, var_id, idx);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_CACHE_MISS);
                return (int32_t)idx;
            }
            bindings_lookup_index_release(b->lookup_index);
            b->lookup_index = NULL;
        } else {
            bindings_lookup_cache_note(
                b, var_id, BINDINGS_LOOKUP_CACHE_MISS);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_CACHE_MISS);
            return -1;
        }
    }
    for (uint32_t i = b->len; i > 0; i--) {
        uint32_t idx = i - 1;
        if (binding_var_eq(b->entries[idx].var_id, var_id)) {
            bindings_lookup_cache_note(b, var_id, idx);
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_CACHE_MISS);
            return (int32_t)idx;
        }
    }
    bindings_lookup_cache_note(b, var_id, BINDINGS_LOOKUP_CACHE_MISS);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_CACHE_MISS);
    return -1;
}

static size_t bindings_dereference_limit(const Bindings *bindings);

static Atom *bindings_resolve_atom(Bindings *b, Atom *atom) {
    if (!atom) return atom;
    size_t dereferences = 0;
    size_t dereference_limit = bindings_dereference_limit(b);
    while (atom->kind == ATOM_VAR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_RESOLVE);
        Atom *next = bindings_lookup_id(b, atom->var_id);
        if (!next) next = bindings_lookup_spelling(b, atom->sym_id);
        if (!next) return atom;
        if (next == atom) return atom;
        if (next->kind == ATOM_VAR &&
            (binding_var_eq(next->var_id, atom->var_id) ||
             (!next->name_key && !atom->name_key &&
              next->sym_id == atom->sym_id)))
            return atom;
        atom = next;
        if (++dereferences > dereference_limit) return atom;
    }
    return atom;
}

static bool atom_contains_unbound_var(Bindings *b, Atom *atom) {
    atom = bindings_resolve_atom(b, atom);
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (atom_contains_unbound_var(b, atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool atom_eq_under_bindings(Bindings *b, Atom *lhs, Atom *rhs) {
    lhs = bindings_resolve_atom(b, lhs);
    rhs = bindings_resolve_atom(b, rhs);
    if (lhs == rhs) return true;
    if (lhs->kind != rhs->kind) return false;
    switch (lhs->kind) {
    case ATOM_VAR:
        return binding_var_eq(lhs->var_id, rhs->var_id);
    case ATOM_SYMBOL:
        return lhs->sym_id == rhs->sym_id;
    case ATOM_GROUNDED:
        return atom_eq(lhs, rhs);
    case ATOM_EXPR:
        if (lhs->expr.len != rhs->expr.len) return false;
        for (CettaExprIndex i = 0; i < lhs->expr.len; i++) {
            if (!atom_eq_under_bindings(
                    b, lhs->expr.elems[i], rhs->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool constraint_pair_eq(const BindingConstraint *lhs, const BindingConstraint *rhs) {
    return (atom_eq(lhs->lhs, rhs->lhs) && atom_eq(lhs->rhs, rhs->rhs)) ||
           (atom_eq(lhs->lhs, rhs->rhs) && atom_eq(lhs->rhs, rhs->lhs));
}

static bool bindings_reserve_entries(Bindings *b, uint32_t needed) {
    if (needed <= b->cap) return true;
    uint32_t next_cap = b->cap ? b->cap : BINDINGS_MIN_CAPACITY;
    while (next_cap < needed) next_cap *= 2;
    Binding *next = bindings_entries_alloc(next_cap);
    if (b->len > 0)
        memcpy(next, b->entries, sizeof(Binding) * b->len);
    bindings_entries_release(b->entries, b->cap);
    b->entries = next;
    b->cap = next_cap;
    return true;
}

static bool bindings_reserve_constraints(Bindings *b, uint32_t needed) {
    if (needed <= b->eq_cap) return true;
    uint32_t next_cap = b->eq_cap ? b->eq_cap : BINDINGS_MIN_CAPACITY;
    while (next_cap < needed) next_cap *= 2;
    BindingConstraint *next = bindings_constraints_alloc(next_cap);
    if (b->eq_len > 0)
        memcpy(next, b->constraints, sizeof(BindingConstraint) * b->eq_len);
    bindings_constraints_release(b->constraints, b->eq_cap);
    b->constraints = next;
    b->eq_cap = next_cap;
    return true;
}

static bool bindings_store_constraint(Bindings *b, Atom *lhs, Atom *rhs) {
    BindingConstraint next = {.lhs = lhs, .rhs = rhs};
    for (uint32_t i = 0; i < b->eq_len; i++) {
        if (constraint_pair_eq(&b->constraints[i], &next))
            return true;
    }
    if (!bindings_reserve_constraints(b, b->eq_len + 1)) return false;
    b->constraints[b->eq_len++] = next;
    if (constraint_contains_private_variant_slot(&next))
        b->private_constraint_count++;
    return true;
}

static bool bindings_add_inplace_internal(Bindings *b, VarId var_id,
                                          SymbolId spelling, Atom *name_key,
                                          Atom *val,
                                          bool normalize_constraints,
                                          bool legacy_name_fallback);
static bool bindings_add_internal(Bindings *b, VarId var_id, SymbolId spelling,
                                  Atom *name_key, Atom *val,
                                  bool normalize_constraints,
                                  bool legacy_name_fallback);
static bool bindings_add_constraint_inplace_internal(Bindings *b, Atom *lhs,
                                                     Atom *rhs,
                                                     bool normalize_constraints);
static bool bindings_add_constraint_internal(Bindings *b, Atom *lhs, Atom *rhs,
                                             bool normalize_constraints);
static BindingsReachability bindings_value_reaches_var(
    Bindings *bindings, Atom *value, VarId target);

static void bindings_cycle_note_edge(Bindings *bindings, VarId var_id,
                                     Atom *value) {
    if (!bindings ||
        bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC) {
        return;
    }
    if (!atom_has_vars(value)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_CYCLE_GROUND_VALUE);
        return;
    }
    BindingsReachability reaches =
        bindings_value_reaches_var(bindings, value, var_id);
    if (reaches == BINDINGS_REACHABILITY_PRESENT) {
        bindings->cycle_state = BINDINGS_CYCLE_PRESENT;
    } else if (reaches == BINDINGS_REACHABILITY_UNKNOWN) {
        bindings->cycle_state = BINDINGS_CYCLE_UNKNOWN;
    }
}

static bool bindings_normalize_constraints(Bindings *b) {
    if (b->eq_len == 0) return true;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        b->eq_len, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = b->eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = b->constraints[i];
    b->eq_len = 0;
    b->private_constraint_count = 0u;
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_inplace_internal(
                b, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, npending,
                                              pending_stack);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, npending, pending_stack);
    return true;
}

/* --- Prime per-occurrence (prime_ext) accessors -------------------------- *
 * The occurrence is absent (NULL) in pure-HE evaluation; reads then resolve to
 * a shared zero-initialized singleton (top==NULL => "not present"), exactly
 * matching the former zero-inited inline fields.  Mutable views materialize the
 * occurrence lazily -- only Prime evaluation reaches them. */
static const PrimeOccurrence g_prime_occurrence_empty;

const PrimeNeedSnapshot *bindings_need_view(const Bindings *b) {
    return b->prime_ext ? &b->prime_ext->prime_need
                        : &g_prime_occurrence_empty.prime_need;
}

const PrimeNeedBranchState *bindings_branch_state_view(const Bindings *b) {
    return b->prime_ext ? &b->prime_ext->branch_state
                        : &g_prime_occurrence_empty.branch_state;
}

static PrimeOccurrence *bindings_prime_ext_materialize(Bindings *b) {
    if (!b->prime_ext) {
        PrimeOccurrence *ext = cetta_malloc(sizeof(PrimeOccurrence));
        prime_need_snapshot_init(&ext->prime_need);
        prime_need_branch_state_init(&ext->branch_state);
        ext->occurrence_token = 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        prime_need_receipt_init(&ext->receipt);
#endif
        b->prime_ext = ext;
    }
    return b->prime_ext;
}

PrimeNeedSnapshot *bindings_need_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->prime_need;
}

PrimeNeedBranchState *bindings_branch_state_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->branch_state;
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
const PrimeNeedReceipt *bindings_receipt_view(const Bindings *b) {
    return b->prime_ext ? &b->prime_ext->receipt
                        : &g_prime_occurrence_empty.receipt;
}

PrimeNeedReceipt *bindings_receipt_mut(Bindings *b) {
    return &bindings_prime_ext_materialize(b)->receipt;
}
#endif

uint64_t bindings_occurrence_token(const Bindings *b) {
    return b && b->prime_ext ? b->prime_ext->occurrence_token : 0u;
}

bool bindings_refresh_occurrence_token(Bindings *b) {
    if (!b)
        return false;
    uint64_t token = prime_need_fresh_source_occurrence();
    if (token == 0u)
        return false;
    bindings_prime_ext_materialize(b)->occurrence_token = token;
    return true;
}

bool bindings_prime_present(const Bindings *b) {
    if (!b->prime_ext)
        return false;
    return prime_need_snapshot_present(&b->prime_ext->prime_need) ||
           prime_need_branch_state_present(&b->prime_ext->branch_state) ||
           b->prime_ext->occurrence_token != 0u
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
           || prime_need_receipt_present(&b->prime_ext->receipt)
#endif
           ;
}

/* Reset an existing occurrence to empty without freeing it (kept for reuse to
 * avoid alloc churn on hot Prime merge/rollback paths). */
static void prime_occurrence_reset(PrimeOccurrence *ext) {
    prime_need_snapshot_init(&ext->prime_need);
    prime_need_branch_state_init(&ext->branch_state);
    ext->occurrence_token = 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_receipt_init(&ext->receipt);
#endif
}

void bindings_prime_assign(Bindings *dst, const Bindings *src) {
    if (src->prime_ext &&
        (prime_need_snapshot_present(&src->prime_ext->prime_need) ||
         prime_need_branch_state_present(&src->prime_ext->branch_state) ||
         src->prime_ext->occurrence_token != 0u
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
         || prime_need_receipt_present(&src->prime_ext->receipt)
#endif
         )) {
        *bindings_prime_ext_materialize(dst) = *src->prime_ext;
    } else if (dst->prime_ext) {
        prime_occurrence_reset(dst->prime_ext);
    }
}

void bindings_prime_set(Bindings *dst, const PrimeNeedSnapshot *need,
                        const PrimeNeedBranchState *branch_state,
                        uint64_t occurrence_token,
                        const PrimeNeedReceipt *receipt) {
    bool present = (need && prime_need_snapshot_present(need)) ||
                   (branch_state &&
                    prime_need_branch_state_present(branch_state)) ||
                   occurrence_token != 0u;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    present = present ||
              (receipt && prime_need_receipt_present(receipt));
#else
    (void)receipt;
#endif
    if (present) {
        PrimeOccurrence *ext = bindings_prime_ext_materialize(dst);
        if (need) ext->prime_need = *need;
        else prime_need_snapshot_init(&ext->prime_need);
        if (branch_state) ext->branch_state = *branch_state;
        else prime_need_branch_state_init(&ext->branch_state);
        ext->occurrence_token = occurrence_token;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (receipt) ext->receipt = *receipt;
        else prime_need_receipt_init(&ext->receipt);
#endif
    } else if (dst->prime_ext) {
        prime_occurrence_reset(dst->prime_ext);
    }
}

void bindings_init(Bindings *b) {
    b->entries = NULL;
    b->len = 0;
    b->cap = 0;
    b->constraints = NULL;
    b->eq_len = 0;
    b->eq_cap = 0;
    b->legacy_fallback_count = 0u;
    b->private_entry_count = 0u;
    b->private_constraint_count = 0u;
    b->cycle_state = BINDINGS_CYCLE_ACYCLIC;
    b->lookup_index = NULL;
    b->prime_ext = NULL;
    bindings_lookup_cache_reset(b);
}

void bindings_free(Bindings *b) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_FREE);
    if (b->cap > 0 || b->eq_cap > 0)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_FREE_NONEMPTY);
    if (b->cap > 0)
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_BINDINGS_RELEASED_ENTRY_CAPACITY,
                                b->cap);
    if (b->eq_cap > 0)
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_BINDINGS_RELEASED_CONSTRAINT_CAPACITY,
            b->eq_cap);
    bindings_entries_release(b->entries, b->cap);
    bindings_constraints_release(b->constraints, b->eq_cap);
    bindings_lookup_index_release(b->lookup_index);
    b->lookup_index = NULL;
    if (b->prime_ext) {
        free(b->prime_ext);
        b->prime_ext = NULL;
    }
    bindings_init(b);
}

bool bindings_clone(Bindings *dst, const Bindings *src) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_CLONE);
    bindings_init(dst);
    if (src->len > 0) {
        if (!bindings_reserve_entries(dst, src->len)) return false;
        memcpy(dst->entries, src->entries, sizeof(Binding) * src->len);
        dst->len = src->len;
    }
    if (src->eq_len > 0) {
        if (!bindings_reserve_constraints(dst, src->eq_len)) {
            bindings_free(dst);
            return false;
        }
        memcpy(dst->constraints, src->constraints,
               sizeof(BindingConstraint) * src->eq_len);
        dst->eq_len = src->eq_len;
    }
    if (src->lookup_cache_count > 0) {
        dst->lookup_cache_count = src->lookup_cache_count;
        dst->lookup_cache_next = src->lookup_cache_next;
        for (uint32_t i = 0; i < src->lookup_cache_count; i++) {
            dst->lookup_cache_ids[i] = src->lookup_cache_ids[i];
            dst->lookup_cache_indices[i] = src->lookup_cache_indices[i];
        }
    }
    if (src->lookup_index) {
        bindings_lookup_index_retain(src->lookup_index);
        dst->lookup_index = src->lookup_index;
    }
    dst->private_entry_count = src->private_entry_count;
    dst->private_constraint_count = src->private_constraint_count;
    dst->legacy_fallback_count = src->legacy_fallback_count;
    dst->cycle_state = src->cycle_state;
    bindings_prime_assign(dst, src);
    return true;
}

static bool binding_prefix_item_equal(const Binding *left,
                                      const Binding *right) {
    if (!left || !right ||
        left->var_id != right->var_id ||
        left->spelling != right->spelling ||
        left->legacy_name_fallback != right->legacy_name_fallback) {
        return false;
    }
    if ((left->name_key || right->name_key) &&
        (!left->name_key || !right->name_key ||
         !atom_eq(left->name_key, right->name_key))) {
        return false;
    }
    return left->val && right->val && atom_eq(left->val, right->val);
}

bool bindings_factor_prefix(Bindings *full, const Bindings *base,
                            bool *factored,
                            uint64_t *logical_items_elided) {
    if (factored)
        *factored = false;
    if (logical_items_elided)
        *logical_items_elided = 0u;
    if (!full || !base || full == base || !factored)
        return false;
    if ((base->len == 0u && base->eq_len == 0u) ||
        full->len < base->len || full->eq_len < base->eq_len) {
        return true;
    }
    for (uint32_t i = 0u; i < base->len; i++) {
        if (!binding_prefix_item_equal(
                &full->entries[i], &base->entries[i])) {
            return true;
        }
    }
    for (uint32_t i = 0u; i < base->eq_len; i++) {
        if (!constraint_pair_eq(
                &full->constraints[i], &base->constraints[i])) {
            return true;
        }
    }

    Bindings suffix;
    bindings_init(&suffix);
    uint32_t suffix_len = full->len - base->len;
    uint32_t suffix_eq_len = full->eq_len - base->eq_len;
    if (suffix_len > 0u &&
        !bindings_reserve_entries(&suffix, suffix_len)) {
        bindings_free(&suffix);
        return false;
    }
    if (suffix_eq_len > 0u &&
        !bindings_reserve_constraints(&suffix, suffix_eq_len)) {
        bindings_free(&suffix);
        return false;
    }
    for (uint32_t i = base->len; i < full->len; i++) {
        Binding item = full->entries[i];
        suffix.entries[suffix.len++] = item;
        if (item.legacy_name_fallback)
            suffix.legacy_fallback_count++;
        if (binding_contains_private_variant_slot(&item))
            suffix.private_entry_count++;
    }
    for (uint32_t i = base->eq_len; i < full->eq_len; i++) {
        BindingConstraint item = full->constraints[i];
        suffix.constraints[suffix.eq_len++] = item;
        if (constraint_contains_private_variant_slot(&item))
            suffix.private_constraint_count++;
    }
    suffix.cycle_state =
        suffix.len == 0u || full->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;

    bool same_need =
        prime_need_snapshot_is_ancestor(bindings_need_view(base),
                                        bindings_need_view(full)) &&
        prime_need_snapshot_is_ancestor(bindings_need_view(full),
                                        bindings_need_view(base));
    bool same_branch_state =
        prime_need_branch_state_equal(bindings_branch_state_view(base),
                                      bindings_branch_state_view(full));
    bool same_occurrence =
        bindings_occurrence_token(base) ==
        bindings_occurrence_token(full);
    bool same_receipt = true;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    same_receipt = prime_need_receipt_equal(
        bindings_receipt_view(base), bindings_receipt_view(full));
#endif
    if (!same_need || !same_branch_state || !same_receipt ||
        !same_occurrence)
        bindings_prime_assign(&suffix, full);

    uint64_t elided = (uint64_t)base->len + (uint64_t)base->eq_len;
    bindings_replace(full, &suffix);
    *factored = true;
    if (logical_items_elided)
        *logical_items_elided = elided;
    return true;
}

bool bindings_copy(Bindings *dst, const Bindings *src) {
    if (dst == src) return true;
    Bindings tmp;
    if (!bindings_clone(&tmp, src)) return false;
    bindings_free(dst);
    *dst = tmp;
    return true;
}

bool bindings_promote_logical_atoms_with_session(
    Bindings *bindings, AtomDeepCopySession *session) {
    if (!bindings)
        return true;
    if (!session)
        return false;
    for (uint32_t i = 0; i < bindings->len; i++) {
        Atom *promoted_name_key = bindings->entries[i].name_key
            ? atom_deep_copy_session_copy(
                  session, bindings->entries[i].name_key)
            : NULL;
        Atom *promoted = bindings->entries[i].val
            ? atom_deep_copy_session_copy(
                  session, bindings->entries[i].val)
            : NULL;
        if ((bindings->entries[i].name_key && !promoted_name_key) ||
            (bindings->entries[i].val && !promoted))
            return false;
        bindings->entries[i].name_key = promoted_name_key;
        bindings->entries[i].val = promoted;
    }
    for (uint32_t i = 0; i < bindings->eq_len; i++) {
        Atom *lhs = bindings->constraints[i].lhs
            ? atom_deep_copy_session_copy(
                  session, bindings->constraints[i].lhs)
            : NULL;
        Atom *rhs = bindings->constraints[i].rhs
            ? atom_deep_copy_session_copy(
                  session, bindings->constraints[i].rhs)
            : NULL;
        if ((bindings->constraints[i].lhs && !lhs) ||
            (bindings->constraints[i].rhs && !rhs)) {
            return false;
        }
        bindings->constraints[i].lhs = lhs;
        bindings->constraints[i].rhs = rhs;
    }
    bindings->lookup_cache_count = 0;
    bindings->lookup_cache_next = 0;
    return true;
}

bool bindings_logical_atoms_closed_for_arena(
    const Bindings *bindings, const Arena *arena) {
    if (!bindings || !arena)
        return bindings == NULL;
    for (uint32_t i = 0u; i < bindings->len; i++) {
        const Binding *entry = &bindings->entries[i];
        if ((entry->name_key &&
             !atom_graph_is_closed_for_arena(arena, entry->name_key)) ||
            (entry->val &&
             !atom_graph_is_closed_for_arena(arena, entry->val))) {
            return false;
        }
    }
    for (uint32_t i = 0u; i < bindings->eq_len; i++) {
        const BindingConstraint *constraint = &bindings->constraints[i];
        if ((constraint->lhs &&
             !atom_graph_is_closed_for_arena(arena, constraint->lhs)) ||
            (constraint->rhs &&
             !atom_graph_is_closed_for_arena(arena, constraint->rhs))) {
            return false;
        }
    }
    return true;
}

bool bindings_promote_logical_atoms_to_arena(Bindings *bindings,
                                             Arena *dst) {
    if (!bindings || !dst)
        return true;
    AtomDeepCopySession *session =
        atom_deep_copy_session_new(dst);
    if (!session)
        return false;
    bool promoted =
        bindings_promote_logical_atoms_with_session(bindings, session);
    atom_deep_copy_session_free(session);
    return promoted;
}

bool bindings_promote_atoms_to_arena(Bindings *bindings, Arena *dst) {
    if (!bindings_promote_logical_atoms_to_arena(bindings, dst))
        return false;
    if (!bindings->prime_ext)
        return true;
    if (!prime_need_snapshot_promote(
            dst, &bindings->prime_ext->prime_need) ||
        !prime_need_branch_state_promote(
            dst, &bindings->prime_ext->branch_state))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_receipt_promote(dst, &bindings->prime_ext->receipt))
        return false;
#endif
    return true;
}

void bindings_move(Bindings *dst, Bindings *src) {
    *dst = *src;
    bindings_init(src);
}

void bindings_replace(Bindings *dst, Bindings *src) {
    bindings_free(dst);
    bindings_move(dst, src);
}

bool bindings_remove_entry_at(Bindings *bindings, uint32_t index) {
    if (!bindings || index >= bindings->len)
        return false;
    if (binding_contains_private_variant_slot(
            &bindings->entries[index])) {
        assert(bindings->private_entry_count > 0u);
        bindings->private_entry_count--;
    }
    if (bindings->entries[index].legacy_name_fallback) {
        assert(bindings->legacy_fallback_count > 0u);
        bindings->legacy_fallback_count--;
    }
    for (uint32_t i = index + 1u; i < bindings->len; i++)
        bindings->entries[i - 1u] = bindings->entries[i];
    bindings->len--;
    if (bindings->cycle_state != BINDINGS_CYCLE_ACYCLIC)
        bindings->cycle_state = BINDINGS_CYCLE_UNKNOWN;
    bindings_lookup_cache_reset(bindings);
    bindings_lookup_index_release(bindings->lookup_index);
    bindings->lookup_index = NULL;
    return true;
}

void bindings_invalidate_after_key_rewrite(Bindings *bindings) {
    if (!bindings)
        return;
    bindings->cycle_state = bindings->len == 0u
        ? BINDINGS_CYCLE_ACYCLIC
        : BINDINGS_CYCLE_UNKNOWN;
    bindings_private_counts_slow(
        bindings, &bindings->private_entry_count,
        &bindings->private_constraint_count);
    bindings->legacy_fallback_count =
        bindings_legacy_fallback_count_slow(bindings);
    bindings_lookup_cache_reset(bindings);
    bindings_lookup_index_release(bindings->lookup_index);
    bindings->lookup_index = NULL;
}

Atom *bindings_lookup_id(Bindings *b, VarId var_id) {
    int32_t idx = bindings_lookup_index(b, var_id);
    return idx >= 0 ? b->entries[idx].val : NULL;
}

Atom *bindings_lookup_var(Bindings *b, Atom *var) {
    return bindings_lookup_id(b, var->var_id);
}

Atom *binding_variable_atom(Arena *a, const Binding *binding) {
    if (!a || !binding || binding->legacy_name_fallback)
        return NULL;
    return atom_var_with_presentation(
        a, binding->spelling, binding->name_key, binding->var_id);
}

Atom *bindings_resolve_atom_preview(Bindings *b, Atom *atom) {
    return bindings_resolve_atom(b, atom);
}

static Atom *bindings_lookup_spelling(Bindings *b, SymbolId spelling) {
    if (b->legacy_fallback_count == 0u)
        return NULL;
    if (bindings_private_audit_enabled()) {
        assert(
            b->legacy_fallback_count ==
            bindings_legacy_fallback_count_slow(b));
    }
    for (uint32_t i = 0; i < b->len; i++) {
        if (!b->entries[i].legacy_name_fallback)
            continue;
        if (b->entries[i].spelling == spelling)
            return b->entries[i].val;
    }
    return NULL;
}

char *arena_tagged_var_name(Arena *a, const char *name, uint32_t suffix) {
    size_t name_len = strlen(name);
    size_t needed = name_len + 1 + 10 + 1;
    char *buf = arena_alloc(a, needed);
    snprintf(buf, needed, "%s#%u", name, suffix);
    return buf;
}

static Atom *epoch_var_atom(Arena *a, Atom *var, uint32_t epoch) {
    return atom_var_like(a, var, var_epoch_id(var->var_id, epoch));
}

static bool bindings_add_inplace_internal(Bindings *b, VarId var_id,
                                          SymbolId spelling, Atom *name_key,
                                          Atom *val,
                                          bool normalize_constraints,
                                          bool legacy_name_fallback) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_ADD);
    if (val->kind == ATOM_VAR && binding_var_eq(var_id, val->var_id)) {
        return true;
    }
    if (val->kind == ATOM_VAR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_ADD_GUARD);
        Atom *other = bindings_lookup_id(b, val->var_id);
        if (other && other->kind == ATOM_VAR && binding_var_eq(other->var_id, var_id)) {
            return true;
        }
    }
    /* Check for existing binding */
    int32_t existing_idx = bindings_lookup_index(b, var_id);
    Atom *existing = existing_idx >= 0 ? b->entries[existing_idx].val : NULL;
    if (existing) {
        Atom *existing_key = b->entries[existing_idx].name_key;
        if ((existing_key || name_key) &&
            (!existing_key || !name_key || !atom_eq(existing_key, name_key)))
            return false;
        /* Already bound — unify structurally instead of demanding
           literal equality, so repeated higher-order type constraints
           can refine earlier variable bindings. */
        bool ok = (existing == val || atom_eq(existing, val));
        if (!ok) {
            Bindings probe;
            if (!bindings_clone(&probe, b))
                return false;
            ok = match_atoms(existing, val, &probe);
            if (!ok) {
                bindings_free(&probe);
                return false;
            }
            bindings_replace(b, &probe);
        }
        if (!ok) {
            return false;
        }
        if (legacy_name_fallback && existing_idx >= 0 &&
            !b->entries[existing_idx].legacy_name_fallback) {
            b->entries[existing_idx].legacy_name_fallback = true;
            b->legacy_fallback_count++;
            b->cycle_state = BINDINGS_CYCLE_UNKNOWN;
        }
        if (normalize_constraints && !bindings_normalize_constraints(b))
            return false;
        return true;
    }
    if (!bindings_reserve_entries(b, b->len + 1)) {
        return false;
    }
    if (legacy_name_fallback)
        b->cycle_state = BINDINGS_CYCLE_UNKNOWN;
    else
        bindings_cycle_note_edge(b, var_id, val);
    b->entries[b->len].var_id = var_id;
    b->entries[b->len].spelling = spelling;
    b->entries[b->len].name_key = name_key;
    b->entries[b->len].val = val;
    b->entries[b->len].legacy_name_fallback = legacy_name_fallback;
    if (legacy_name_fallback)
        b->legacy_fallback_count++;
    if (binding_contains_private_variant_slot(&b->entries[b->len]))
        b->private_entry_count++;
    uint32_t added_index = b->len;
    b->len++;
    bindings_lookup_cache_note(b, var_id, added_index);
    if (normalize_constraints && !bindings_normalize_constraints(b))
        return false;
    return true;
}

static bool bindings_add_internal(Bindings *b, VarId var_id, SymbolId spelling,
                                  Atom *name_key, Atom *val,
                                  bool normalize_constraints,
                                  bool legacy_name_fallback) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(&next, var_id, spelling, name_key, val,
                                       normalize_constraints,
                                       legacy_name_fallback)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_id(Bindings *b, VarId var_id, SymbolId spelling, Atom *val) {
    return bindings_add_internal(
        b, var_id, spelling, NULL, val, true, false);
}

bool bindings_add_id_acyclic(Bindings *b, VarId var_id, SymbolId spelling,
                             Atom *val) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(&next, var_id, spelling, NULL, val,
                                       true, false) ||
        bindings_has_loop(&next)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_var(Bindings *b, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    return bindings_add_internal(
        b, var->var_id, var->sym_id, var->name_key, val, true, false);
}

bool bindings_add_var_acyclic(Bindings *b, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(
            &next, var->var_id, var->sym_id, var->name_key,
            val, true, false) ||
        bindings_has_loop(&next)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

static bool bindings_add_constraint_inplace_internal(Bindings *b, Atom *lhs,
                                                     Atom *rhs,
                                                     bool normalize_constraints) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_ADD);
    lhs = bindings_resolve_atom(b, lhs);
    rhs = bindings_resolve_atom(b, rhs);

    if (atom_eq_under_bindings(b, lhs, rhs)) {
        return true;
    }
    if (lhs->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, lhs->var_id, lhs->sym_id, lhs->name_key,
                rhs, false, false)) {
            return false;
        }
    } else if (rhs->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, rhs->var_id, rhs->sym_id, rhs->name_key,
                lhs, false, false)) {
            return false;
        }
    } else if (!atom_contains_unbound_var(b, lhs) &&
               !atom_contains_unbound_var(b, rhs)) {
        return false;
    } else if (!bindings_store_constraint(b, lhs, rhs)) {
        return false;
    }

    if (normalize_constraints && !bindings_normalize_constraints(b)) {
        return false;
    }
    return true;
}

static bool bindings_add_constraint_internal(Bindings *b, Atom *lhs, Atom *rhs,
                                             bool normalize_constraints) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_constraint_inplace_internal(
            &next, lhs, rhs, normalize_constraints)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_constraint(Bindings *b, Atom *lhs, Atom *rhs) {
    return bindings_add_constraint_internal(b, lhs, rhs, true);
}

static bool bindings_merged_occurrence_token(
    const Bindings *dst, const Bindings *src, uint64_t *out) {
    if (!out)
        return false;
    uint64_t left = bindings_occurrence_token(dst);
    uint64_t right = bindings_occurrence_token(src);
    if (left == 0u || left == right) {
        *out = right;
        return true;
    }
    if (right == 0u) {
        *out = left;
        return true;
    }
    *out = prime_need_fresh_source_occurrence();
    return *out != 0u;
}

static bool bindings_try_merge_inplace(Bindings *dst, const Bindings *src) {
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
    /* Only touch Prime state (and materialize dst's occurrence) when either
     * side carries any: HE merges stay allocation-free. */
    if (bindings_prime_present(dst) || bindings_prime_present(src)) {
        uint64_t occurrence_token = 0u;
        if (!bindings_merged_occurrence_token(
                dst, src, &occurrence_token))
            return false;
        if (!prime_need_snapshot_merge(bindings_need_mut(dst),
                                       bindings_need_view(src)))
            return false;
        if (!prime_need_branch_state_merge(
                bindings_branch_state_mut(dst),
                bindings_branch_state_view(src)))
            return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (!prime_need_receipt_merge(bindings_receipt_mut(dst),
                                      bindings_receipt_view(src)))
            return false;
#endif
        bindings_prime_ext_materialize(dst)->occurrence_token =
            occurrence_token;
    }
    uint32_t pending_cap = dst->eq_len + src->eq_len + 1;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        pending_cap, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = 0;
    for (uint32_t i = 0; i < dst->eq_len; i++)
        pending[npending++] = dst->constraints[i];
    for (uint32_t i = 0; i < src->eq_len; i++) {
        pending[npending++] = src->constraints[i];
    }
    dst->eq_len = 0;
    dst->private_constraint_count = 0u;
    for (uint32_t i = 0; i < src->len; i++) {
        if (!bindings_add_inplace_internal(dst, src->entries[i].var_id,
                                           src->entries[i].spelling,
                                           src->entries[i].name_key,
                                           src->entries[i].val,
                                           false,
                                           src->entries[i].legacy_name_fallback)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            return false;
        }
    }
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_inplace_internal(
                dst, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, pending_cap, pending_stack);
    if (!bindings_normalize_constraints(dst)) {
        return false;
    }
    return true;
}

bool bindings_try_merge(Bindings *dst, const Bindings *src) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    Bindings merged;
    if (!bindings_clone(&merged, dst)) return false;
    if (!bindings_try_merge_inplace(&merged, src)) {
        bindings_free(&merged);
        return false;
    }
    bindings_replace(dst, &merged);
    return true;
}

bool bindings_try_merge_live(Bindings *dst, const Bindings *src) {
    if (!src || (src->len == 0 && src->eq_len == 0 &&
                 !bindings_prime_present(src)))
        return true;
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
    if (dst && dst->len == 0 && dst->eq_len == 0 &&
        !bindings_prime_present(dst)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
        Bindings cloned;
        if (!bindings_clone(&cloned, src))
            return false;
        bindings_replace(dst, &cloned);
        return true;
    }

    BindingsBuilder builder;
    bindings_builder_init_owned(&builder, dst);
    bool ok = bindings_builder_try_merge(&builder, src);
    bindings_builder_take(&builder, dst);
    return ok;
}

bool bindings_clone_merge(Bindings *dst, const Bindings *base,
                          const Bindings *extra) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    bindings_init(dst);
    if (!bindings_clone(dst, base))
        return false;
    if (!bindings_try_merge_inplace(dst, extra)) {
        bindings_free(dst);
        return false;
    }
    return true;
}

static bool bindings_seen_var(const VarId *seen, uint32_t len, VarId var_id) {
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_BINDINGS_SEEN_SCAN, len);
    for (uint32_t i = 0; i < len; i++) {
        if (binding_var_eq(seen[i], var_id)) return true;
    }
    return false;
}

typedef struct {
    VarId *ids;
    uint32_t cap;
    bool heap;
} BindingApplySeen;

static inline void bindings_apply_seen_init(
    BindingApplySeen *seen, VarId *ids, uint32_t cap) {
    seen->ids = ids;
    seen->cap = cap;
    seen->heap = false;
}

static void bindings_apply_seen_release(
    BindingApplySeen *seen) {
    if (!seen || !seen->heap)
        return;
    free(seen->ids);
    seen->ids = NULL;
    seen->cap = 0u;
    seen->heap = false;
}

static bool bindings_apply_seen_reserve(
    BindingApplySeen *seen, uint32_t used, uint32_t needed) {
    if (!seen)
        return false;
    if (needed <= seen->cap)
        return true;
    uint32_t next = seen->cap ? seen->cap : 1u;
    while (next < needed) {
        if (next > UINT32_MAX / 2u) {
            next = needed;
            break;
        }
        next *= 2u;
    }
    VarId *grown = cetta_malloc(sizeof(*grown) * (size_t)next);
    if (used > 0u)
        memcpy(grown, seen->ids, sizeof(*grown) * (size_t)used);
    if (seen->heap)
        free(seen->ids);
    seen->ids = grown;
    seen->cap = next;
    seen->heap = true;
    return true;
}

typedef struct {
    VarId id;
    uint32_t index_plus_one;
} BindingApplyMemoSlot;

typedef struct {
    VarId *ids;
    Atom **vals;
    uint32_t len;
    uint32_t cap;
    bool heap;
    BindingApplyMemoSlot *slots;
    size_t slot_cap;
} BindingApplyMemo;

static inline void bindings_apply_memo_init(BindingApplyMemo *memo, VarId *ids,
                                            Atom **vals, uint32_t cap) {
    memo->ids = ids;
    memo->vals = vals;
    memo->len = 0;
    memo->cap = cap;
    memo->heap = false;
    memo->slots = NULL;
    memo->slot_cap = 0u;
}

static void bindings_apply_memo_release(BindingApplyMemo *memo) {
    if (!memo)
        return;
    if (memo->heap) {
        free(memo->ids);
        free(memo->vals);
    }
    free(memo->slots);
    memo->ids = NULL;
    memo->vals = NULL;
    memo->len = 0;
    memo->cap = 0;
    memo->heap = false;
    memo->slots = NULL;
    memo->slot_cap = 0u;
}

static bool bindings_apply_memo_reserve(BindingApplyMemo *memo, uint32_t needed) {
    if (needed <= memo->cap)
        return true;
    uint32_t new_cap = memo->cap ? memo->cap : 1;
    while (new_cap < needed)
        new_cap *= 2;
    VarId *new_ids = cetta_malloc(sizeof(VarId) * new_cap);
    Atom **new_vals = cetta_malloc(sizeof(Atom *) * new_cap);
    if (memo->len > 0) {
        memcpy(new_ids, memo->ids, sizeof(VarId) * memo->len);
        memcpy(new_vals, memo->vals, sizeof(Atom *) * memo->len);
    }
    if (memo->heap) {
        free(memo->ids);
        free(memo->vals);
    }
    memo->ids = new_ids;
    memo->vals = new_vals;
    memo->cap = new_cap;
    memo->heap = true;
    return true;
}

static bool bindings_apply_memo_index_insert(
    BindingApplyMemoSlot *slots, size_t slot_cap,
    VarId id, uint32_t index) {
    if (!slots || slot_cap == 0u || id == VAR_ID_NONE)
        return false;
    size_t mask = slot_cap - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (slots[slot].id != VAR_ID_NONE &&
           !binding_var_eq(slots[slot].id, id)) {
        slot = (slot + 1u) & mask;
    }
    slots[slot].id = id;
    slots[slot].index_plus_one = index + 1u;
    return true;
}

static bool bindings_apply_memo_index_build(
    BindingApplyMemo *memo, uint32_t needed) {
    if (!memo)
        return false;
    size_t minimum = (size_t)needed * 2u;
    if (minimum < needed)
        return false;
    size_t slot_cap = 32u;
    while (slot_cap < minimum) {
        if (slot_cap > SIZE_MAX / 2u)
            return false;
        slot_cap *= 2u;
    }
    if (slot_cap > SIZE_MAX / sizeof(*memo->slots))
        return false;
    BindingApplyMemoSlot *slots =
        cetta_malloc(slot_cap * sizeof(*slots));
    memset(slots, 0, slot_cap * sizeof(*slots));
    for (uint32_t i = 0u; i < memo->len; i++) {
        if (!bindings_apply_memo_index_insert(
                slots, slot_cap, memo->ids[i], i)) {
            free(slots);
            return false;
        }
    }
    free(memo->slots);
    memo->slots = slots;
    memo->slot_cap = slot_cap;
    return true;
}

static uint32_t bindings_apply_memo_index_find(
    const BindingApplyMemo *memo, VarId id) {
    if (!memo || !memo->slots || memo->slot_cap == 0u ||
        id == VAR_ID_NONE)
        return 0u;
    size_t mask = memo->slot_cap - 1u;
    size_t slot = bindings_var_id_hash(id) & mask;
    while (memo->slots[slot].id != VAR_ID_NONE) {
        if (binding_var_eq(memo->slots[slot].id, id))
            return memo->slots[slot].index_plus_one;
        slot = (slot + 1u) & mask;
    }
    return 0u;
}

static Atom *bindings_apply_memo_lookup(BindingApplyMemo *memo, VarId id) {
    if (memo->slots) {
        uint32_t found = bindings_apply_memo_index_find(memo, id);
        return found > 0u ? memo->vals[found - 1u] : NULL;
    }
    for (uint32_t i = memo->len; i > 0; i--) {
        if (binding_var_eq(memo->ids[i - 1], id))
            return memo->vals[i - 1];
    }
    return NULL;
}

static bool bindings_apply_memo_store(BindingApplyMemo *memo, VarId id, Atom *val) {
    if (memo->slots) {
        uint32_t found = bindings_apply_memo_index_find(memo, id);
        if (found > 0u) {
            memo->vals[found - 1u] = val;
            return true;
        }
    } else {
        for (uint32_t i = 0; i < memo->len; i++) {
            if (binding_var_eq(memo->ids[i], id)) {
                memo->vals[i] = val;
                return true;
            }
        }
    }
    if (!bindings_apply_memo_reserve(memo, memo->len + 1))
        return false;
    if (memo->slots && (size_t)(memo->len + 1u) * 2u > memo->slot_cap &&
        !bindings_apply_memo_index_build(memo, memo->len + 1u))
        return false;
    memo->ids[memo->len] = id;
    memo->vals[memo->len] = val;
    if (memo->slots && !bindings_apply_memo_index_insert(
            memo->slots, memo->slot_cap, id, memo->len))
        return false;
    memo->len++;
    if (!memo->slots && memo->len >= BINDINGS_MEMO_INDEX_THRESHOLD &&
        !bindings_apply_memo_index_build(memo, memo->len))
        return false;
    return true;
}

static Atom *bindings_apply_seen_with_rewrite(Bindings *b, Arena *a, Atom *atom,
                                              BindingApplySeen *seen,
                                              uint32_t seen_len,
                                              bool track_cycles,
                                              BindingApplyMemo *memo,
                                              BindingsRewriteVarFn rewrite_var,
                                              void *rewrite_ctx) {
    if (!atom_has_vars(atom))
        return atom;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_REWRITE_NODE_VISIT);
    switch (atom->kind) {
    case ATOM_VAR: {
        Atom *memoized = bindings_apply_memo_lookup(memo, atom->var_id);
        if (memoized) return memoized;
        if (track_cycles &&
            bindings_seen_var(seen->ids, seen_len, atom->var_id)) {
            Atom *result = rewrite_var ? rewrite_var(a, atom, rewrite_ctx) : atom;
            if (result)
                bindings_apply_memo_store(memo, atom->var_id, result);
            return result;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_APPLY);
        Atom *val = bindings_lookup_id(b, atom->var_id);
        if (!val) val = bindings_lookup_spelling(b, atom->sym_id);
        if (!val) {
            Atom *result = rewrite_var ? rewrite_var(a, atom, rewrite_ctx) : atom;
            if (result)
                bindings_apply_memo_store(memo, atom->var_id, result);
            return result;
        }
        uint32_t next_seen_len = seen_len;
        if (track_cycles) {
            if (!bindings_apply_seen_reserve(
                    seen, seen_len, seen_len + 1u))
                return NULL;
            seen->ids[seen_len] = atom->var_id;
            next_seen_len++;
        }
        Atom *result = bindings_apply_seen_with_rewrite(
            b, a, val, seen, next_seen_len, track_cycles,
            memo, rewrite_var, rewrite_ctx);
        if (result)
            bindings_apply_memo_store(memo, atom->var_id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom *draft = NULL;
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? bindings_apply_seen_with_rewrite(b, a, child,
                                                   seen, seen_len,
                                                   track_cycles, memo,
                                                   rewrite_var, rewrite_ctx)
                : child;
            if (!next)
                return NULL;
            if (!new_elems && next != atom->expr.elems[i]) {
                draft = atom_expr_builder_begin(a, atom->expr.len);
                if (!draft)
                    return NULL;
                new_elems = draft->expr.elems;
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems) return atom;
        return atom_expr_builder_finish(a, draft);
    }
    default:
        return atom;
    }
}

Atom *bindings_apply_rewrite_vars(Bindings *b, Arena *a, Atom *atom,
                                  BindingsRewriteVarFn rewrite_var,
                                  void *rewrite_ctx) {
    if (!b || !a || !atom)
        return NULL;
    if (b->len == 0 && !rewrite_var)
        return atom;
    /* Ground-term sharing: a variable-free atom is canonical and immutable --
     * no binding can rewrite it -- so return it shared instead of walking and
     * deep-copying it once per result.  This is the same shared-return contract
     * the b->len==0 case above already relies on (callers must treat the result
     * as read-only); it only widens it to the (bindings present, atom ground)
     * case, e.g. a constant match template applied under a nonempty binding.
     * Only when no custom rewrite hook is installed (a hook may transform
     * non-variable atoms). */
    if (!rewrite_var && !atom_has_vars(atom))
        return atom;
    VarId seen_stack[BINDINGS_SEEN_STACK_CAP];
    VarId memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    BindingApplySeen seen;
    bindings_apply_seen_init(
        &seen, seen_stack, BINDINGS_SEEN_STACK_CAP);
    BindingApplyMemo memo;
    bindings_apply_memo_init(&memo, memo_id_stack, memo_val_stack,
                             BINDINGS_MEMO_STACK_CAP);
    /* Every binding mutation updates the cached `cycle_state` summary.  In an
     * acyclic graph, active-path membership cannot affect the result, so do
     * not build or scan that transient structure.  Unknown and witnessed-
     * cyclic environments retain the independent general guard below. */
    bool track_cycles =
        b->cycle_state != BINDINGS_CYCLE_ACYCLIC ||
        b->legacy_fallback_count != 0u;
    Atom *result = bindings_apply_seen_with_rewrite(
        b, a, atom, &seen, 0, track_cycles, &memo,
        rewrite_var, rewrite_ctx);
    bindings_apply_memo_release(&memo);
    bindings_apply_seen_release(&seen);
    return result;
}

Atom *bindings_apply(Bindings *b, Arena *a, Atom *atom) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY);
    return bindings_apply_rewrite_vars(b, a, atom, NULL, NULL);
}

static size_t bindings_dereference_limit(const Bindings *bindings);

static Atom *bindings_lookup_id_since(Bindings *b, VarId id,
                                      uint32_t first_entry) {
    int32_t index = bindings_lookup_index(b, id);

    if (index < 0 || (uint32_t)index < first_entry)
        return NULL;
    return b->entries[(uint32_t)index].val;
}

void bindings_dense_epoch_frame_init(BindingsDenseEpochFrame *frame) {
    if (frame)
        memset(frame, 0, sizeof(*frame));
}

void bindings_dense_epoch_frame_free(BindingsDenseEpochFrame *frame) {
    if (!frame)
        return;
    free(frame->values);
    free(frame->slot_stamps);
    memset(frame, 0, sizeof(*frame));
}

static void bindings_dense_epoch_frame_begin_generation(
        BindingsDenseEpochFrame *frame) {
    if (frame->slot_generation == UINT32_MAX) {
        if (frame->cap > 0u) {
            memset(frame->slot_stamps, 0,
                   sizeof(*frame->slot_stamps) * (size_t)frame->cap);
        }
        frame->slot_generation = 1u;
    } else {
        frame->slot_generation++;
        if (frame->slot_generation == 0u)
            frame->slot_generation = 1u;
    }
}

static uint32_t bindings_dense_epoch_frame_lower_bound(
        const BindingsDenseEpochFrame *frame, VarId source_id) {
    uint32_t low = 0u;
    uint32_t high = frame ? frame->len : 0u;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (frame->source_ids[middle] < source_id)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

static bool bindings_dense_epoch_frame_lookup(
        const BindingsDenseEpochFrame *frame, VarId source_id,
        Atom **value_out, bool *present_out) {
    if (value_out)
        *value_out = NULL;
    if (present_out)
        *present_out = false;
    if (!frame || !value_out || !present_out ||
        !frame->source_ids || !frame->source_variables ||
        source_id == VAR_ID_NONE) {
        return false;
    }
    uint32_t index = bindings_dense_epoch_frame_lower_bound(
        frame, source_id);
    if (index >= frame->len || frame->source_ids[index] != source_id)
        return false;
    *present_out =
        frame->slot_stamps[index] == frame->slot_generation;
    *value_out = *present_out ? frame->values[index] : NULL;
    return true;
}

static void bindings_dense_epoch_frame_scan(
        BindingsDenseEpochFrame *frame, const Bindings *bindings,
        uint32_t begin) {
    for (uint32_t entry_index = begin;
         entry_index < bindings->len; entry_index++) {
        const Binding *entry = &bindings->entries[entry_index];
        if (var_epoch_suffix(entry->var_id) != frame->epoch)
            continue;
        VarId source_id = (VarId)var_base_id(entry->var_id);
        uint32_t slot = bindings_dense_epoch_frame_lower_bound(
            frame, source_id);
        if (slot >= frame->len || frame->source_ids[slot] != source_id)
            continue;
        frame->values[slot] = entry->val;
        frame->slot_stamps[slot] = frame->slot_generation;
    }
    frame->scanned_len = bindings->len;
}

bool bindings_dense_epoch_frame_prepare(
        BindingsDenseEpochFrame *frame,
        BindingsBuilder *builder,
        const VarId *source_ids, Atom *const *source_variables,
        uint32_t variable_count, uint32_t epoch,
        uint32_t first_entry) {
    const Bindings *bindings = builder ? &builder->current : NULL;
    if (!frame || !builder || !bindings || builder->instance_id == 0u ||
        epoch == 0u ||
        builder->growth_count == UINT64_MAX ||
        builder->rollback_count == UINT64_MAX ||
        first_entry > bindings->len ||
        (variable_count > 0u && (!source_ids || !source_variables))) {
        return false;
    }
    for (uint32_t index = 0u; index < variable_count; index++) {
        Atom *variable = source_variables[index];
        if (!variable || variable->kind != ATOM_VAR ||
            variable->var_id != source_ids[index] ||
            var_epoch_suffix(source_ids[index]) != 0u ||
            (index > 0u && source_ids[index - 1u] >= source_ids[index])) {
            return false;
        }
    }
    if (variable_count > frame->cap) {
        size_t values_width = sizeof(*frame->values);
        size_t stamps_width = sizeof(*frame->slot_stamps);
        if ((size_t)variable_count > SIZE_MAX / values_width ||
            (size_t)variable_count > SIZE_MAX / stamps_width) {
            return false;
        }
        uint32_t old_cap = frame->cap;
        frame->values = frame->values
            ? cetta_realloc(
                  frame->values, values_width * (size_t)variable_count)
            : cetta_malloc(values_width * (size_t)variable_count);
        frame->slot_stamps = frame->slot_stamps
            ? cetta_realloc(
                  frame->slot_stamps,
                  stamps_width * (size_t)variable_count)
            : cetta_malloc(stamps_width * (size_t)variable_count);
        memset(frame->slot_stamps + old_cap, 0,
               stamps_width * (size_t)(variable_count - old_cap));
        frame->cap = variable_count;
    }
    frame->builder = builder;
    frame->builder_instance = builder->instance_id;
    frame->source_ids = source_ids;
    frame->source_variables = source_variables;
    frame->binding_growth = builder->growth_count;
    frame->binding_rollbacks = builder->rollback_count;
    frame->len = variable_count;
    frame->epoch = epoch;
    frame->first_entry = first_entry;
    frame->scanned_len = first_entry;
    bindings_dense_epoch_frame_begin_generation(frame);
    bindings_dense_epoch_frame_scan(frame, bindings, first_entry);
    return true;
}

bool bindings_dense_epoch_frame_refresh(
        BindingsDenseEpochFrame *frame,
        BindingsBuilder *builder) {
    const Bindings *bindings = builder ? &builder->current : NULL;
    if (!frame || !builder || !bindings ||
        frame->builder != builder ||
        frame->builder_instance == 0u ||
        frame->builder_instance != builder->instance_id ||
        frame->epoch == 0u || frame->len > frame->cap ||
        frame->binding_growth == UINT64_MAX ||
        frame->binding_rollbacks == UINT64_MAX ||
        builder->growth_count == UINT64_MAX ||
        builder->rollback_count == UINT64_MAX ||
        frame->binding_growth > builder->growth_count ||
        frame->binding_rollbacks != builder->rollback_count ||
        frame->first_entry > frame->scanned_len ||
        frame->scanned_len > bindings->len ||
        (frame->binding_growth == builder->growth_count &&
         frame->scanned_len != bindings->len) ||
        (frame->len > 0u &&
         (!frame->source_ids || !frame->source_variables ||
          !frame->values || !frame->slot_stamps))) {
        return false;
    }
    bindings_dense_epoch_frame_scan(
        frame, bindings, frame->scanned_len);
    frame->binding_growth = builder->growth_count;
    frame->binding_rollbacks = builder->rollback_count;
    return true;
}

static bool bindings_dense_epoch_frame_is_current(
        const BindingsDenseEpochFrame *frame,
        const BindingsBuilder *builder) {
    if (!frame || !builder || frame->builder != builder ||
        frame->builder_instance == 0u ||
        frame->builder_instance != builder->instance_id ||
        frame->epoch == 0u || frame->len > frame->cap ||
        frame->binding_growth == UINT64_MAX ||
        frame->binding_rollbacks == UINT64_MAX ||
        frame->binding_growth != builder->growth_count ||
        frame->binding_rollbacks != builder->rollback_count ||
        frame->first_entry > frame->scanned_len ||
        frame->scanned_len != builder->current.len ||
        (frame->len > 0u &&
         (!frame->source_ids || !frame->source_variables ||
          !frame->values || !frame->slot_stamps))) {
        return false;
    }
    return true;
}

bool bindings_resolve_epoch_view_ground(
        const Bindings *bindings, const Atom *source_variable,
        uint32_t epoch, uint32_t first_entry, Atom **ground_out) {
    Atom *value;
    size_t dereferences = 0u;
    size_t dereference_limit;

    if (ground_out)
        *ground_out = NULL;
    if (!bindings || !source_variable || !ground_out ||
        source_variable->kind != ATOM_VAR ||
        first_entry > bindings->len)
        return false;
    value = bindings_lookup_id_since(
        (Bindings *)bindings,
        var_epoch_id(source_variable->var_id, epoch), first_entry);
    dereference_limit = bindings_dereference_limit(bindings);
    while (value && value->kind == ATOM_VAR) {
        Atom *next;

        if (++dereferences > dereference_limit)
            return true;
        next = bindings_lookup_id(
            (Bindings *)bindings, value->var_id);
        if (!next)
            return true;
        value = next;
    }
    if (value && !atom_has_vars(value))
        *ground_out = value;
    return true;
}

static Atom *bindings_apply_seen_epoch(Bindings *b, Arena *a, Atom *atom,
                                       uint32_t epoch, bool original_side,
                                       uint32_t first_entry,
                                       bool resolve_outer,
                                       const BindingsDenseEpochFrame *frame,
                                       BindingApplySeen *seen,
                                       uint32_t seen_len,
                                       bool track_cycles,
                                       BindingApplyMemo *local_memo,
                                       BindingApplyMemo *outer_memo) {
    if (!atom_has_vars(atom))
        return atom;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EPOCH_NODE_VISIT);
    switch (atom->kind) {
    case ATOM_VAR: {
        bool outer_lookup = resolve_outer && !original_side;
        uint32_t lookup_first = outer_lookup ? 0u : first_entry;
        BindingApplyMemo *memo = outer_lookup ? outer_memo : local_memo;
        VarId lookup_id = original_side
            ? var_epoch_id(atom->var_id, epoch) : atom->var_id;
        Atom *memoized = bindings_apply_memo_lookup(memo, lookup_id);
        if (memoized) return memoized;
        if (track_cycles &&
            bindings_seen_var(seen->ids, seen_len, lookup_id)) {
            Atom *result = original_side ? epoch_var_atom(a, atom, epoch) : atom;
            bindings_apply_memo_store(memo, lookup_id, result);
            return result;
        }
        Atom *val = NULL;
        bool dense_present = false;
        bool dense_known = original_side && frame &&
            bindings_dense_epoch_frame_lookup(
                frame, atom->var_id, &val, &dense_present);
        if (!dense_known)
            val = bindings_lookup_id_since(b, lookup_id, lookup_first);
        else if (!dense_present)
            val = NULL;
        if (!val && outer_lookup)
            val = bindings_lookup_spelling(b, atom->sym_id);
        if (!val) {
            Atom *result = original_side ? epoch_var_atom(a, atom, epoch) : atom;
            bindings_apply_memo_store(memo, lookup_id, result);
            return result;
        }
        if (track_cycles) {
            if (!bindings_apply_seen_reserve(
                    seen, seen_len, seen_len + 1u))
                return NULL;
            seen->ids[seen_len] = lookup_id;
        }
        Atom *result = bindings_apply_seen_epoch(
            b, a, val, epoch, false, first_entry, resolve_outer,
            frame,
            seen, seen_len + (track_cycles ? 1u : 0u), track_cycles,
            local_memo, outer_memo);
        bindings_apply_memo_store(memo, lookup_id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom *draft = NULL;
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? bindings_apply_seen_epoch(
                      b, a, child, epoch, original_side,
                      first_entry, resolve_outer, frame, seen, seen_len,
                      track_cycles, local_memo, outer_memo)
                : child;
            if (!new_elems && next != atom->expr.elems[i]) {
                draft = atom_expr_builder_begin(a, atom->expr.len);
                if (!draft)
                    return NULL;
                new_elems = draft->expr.elems;
                for (uint32_t j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems) return atom;
        return atom_expr_builder_finish(a, draft);
    }
    default:
        return atom;
    }
}

static Atom *bindings_apply_epoch_from(
        Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
        uint32_t first_entry, bool resolve_outer,
        const BindingsDenseEpochFrame *frame, bool original_side,
        VarId initial_seen_id) {
    if (!b || !a || !atom || first_entry > b->len)
        return NULL;
    VarId seen_stack[BINDINGS_SEEN_STACK_CAP];
    VarId local_memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *local_memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    VarId outer_memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *outer_memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    BindingApplySeen seen;
    bindings_apply_seen_init(
        &seen, seen_stack, BINDINGS_SEEN_STACK_CAP);
    BindingApplyMemo local_memo;
    BindingApplyMemo outer_memo;
    bindings_apply_memo_init(
        &local_memo, local_memo_id_stack, local_memo_val_stack,
        BINDINGS_MEMO_STACK_CAP);
    bindings_apply_memo_init(
        &outer_memo, outer_memo_id_stack, outer_memo_val_stack,
                             BINDINGS_MEMO_STACK_CAP);
    /* The builder maintains an exact acyclicity summary for the current
     * substitution graph.  Epoch rewriting changes only the input variable
     * identifier used for the first lookup; the same graph contains every
     * traversed binding edge. */
    bool track_cycles =
        b->cycle_state != BINDINGS_CYCLE_ACYCLIC ||
        b->legacy_fallback_count != 0u;
    uint32_t initial_seen_len = 0u;
    if (track_cycles && initial_seen_id != VAR_ID_NONE) {
        if (!bindings_apply_seen_reserve(&seen, 0u, 1u)) {
            bindings_apply_memo_release(&outer_memo);
            bindings_apply_memo_release(&local_memo);
            bindings_apply_seen_release(&seen);
            return NULL;
        }
        seen.ids[initial_seen_len++] = initial_seen_id;
    }
    Atom *result = bindings_apply_seen_epoch(
        b, a, atom, epoch, original_side, first_entry, resolve_outer,
        frame,
        &seen, initial_seen_len, track_cycles,
        &local_memo, &outer_memo);
    bindings_apply_memo_release(&outer_memo);
    bindings_apply_memo_release(&local_memo);
    bindings_apply_seen_release(&seen);
    return result;
}

static Atom *bindings_apply_epoch_view(Bindings *b, Arena *a, Atom *atom,
                                       uint32_t epoch,
                                       uint32_t first_entry,
                                       bool resolve_outer,
                                       const BindingsDenseEpochFrame *frame) {
    return bindings_apply_epoch_from(
        b, a, atom, epoch, first_entry, resolve_outer,
        frame, true, VAR_ID_NONE);
}

Atom *bindings_apply_epoch_since(Bindings *b, Arena *a, Atom *atom,
                                 uint32_t epoch, uint32_t first_entry) {
    return bindings_apply_epoch_view(
        b, a, atom, epoch, first_entry, false, NULL);
}

Atom *bindings_apply_epoch_then_all(Bindings *b, Arena *a, Atom *atom,
                                    uint32_t epoch,
                                    uint32_t first_entry) {
    return bindings_apply_epoch_view(
        b, a, atom, epoch, first_entry, true, NULL);
}

Atom *bindings_apply_dense_epoch_frame_then_all(
        BindingsBuilder *builder, Arena *a, Atom *atom,
        const BindingsDenseEpochFrame *frame) {
    if (!bindings_dense_epoch_frame_is_current(frame, builder))
        return NULL;
    Bindings *b = &builder->current;
    return bindings_apply_epoch_view(
        b, a, atom, frame->epoch, frame->first_entry, true, frame);
}

Atom *bindings_apply_dense_epoch_frame_slot_then_all(
        BindingsBuilder *builder, Arena *a,
        const BindingsDenseEpochFrame *frame,
        Atom *source_variable, uint32_t slot) {
    if (!a || !bindings_dense_epoch_frame_is_current(frame, builder) ||
        slot >= frame->len ||
        frame->epoch == 0u ||
        frame->first_entry > builder->current.len ||
        !source_variable || source_variable->kind != ATOM_VAR ||
        !frame->source_ids || !frame->source_variables ||
        !frame->slot_stamps || !frame->values) {
        return NULL;
    }
    Bindings *b = &builder->current;
    Atom *template_variable = frame->source_variables[slot];
    VarId source_id = frame->source_ids[slot];
    if (source_variable->var_id != source_id ||
        !template_variable || template_variable->kind != ATOM_VAR ||
        template_variable->var_id != source_id)
        return NULL;
    if (frame->slot_stamps[slot] != frame->slot_generation)
        return epoch_var_atom(a, source_variable, frame->epoch);
    Atom *value = frame->values[slot];
    if (!value)
        return NULL;
    return bindings_apply_epoch_from(
        b, a, value, frame->epoch, frame->first_entry, true,
        frame, false, var_epoch_id(source_id, frame->epoch));
}

Atom *bindings_resolve_dense_epoch_frame_slot_root(
        BindingsBuilder *builder, Arena *a,
        const BindingsDenseEpochFrame *frame,
        Atom *source_variable, uint32_t slot) {
    if (!a || !bindings_dense_epoch_frame_is_current(frame, builder) ||
        slot >= frame->len ||
        frame->epoch == 0u ||
        frame->first_entry > builder->current.len ||
        !source_variable || source_variable->kind != ATOM_VAR ||
        !frame->source_ids || !frame->source_variables ||
        !frame->slot_stamps || !frame->values) {
        return NULL;
    }
    Bindings *b = &builder->current;
    Atom *template_variable = frame->source_variables[slot];
    VarId source_id = frame->source_ids[slot];
    if (source_variable->var_id != source_id ||
        !template_variable || template_variable->kind != ATOM_VAR ||
        template_variable->var_id != source_id)
        return NULL;
    if (frame->slot_stamps[slot] != frame->slot_generation)
        return epoch_var_atom(a, source_variable, frame->epoch);
    Atom *value = frame->values[slot];
    return value ? bindings_resolve_atom_preview(b, value) : NULL;
}

Atom *bindings_apply_epoch(Bindings *b, Arena *a, Atom *atom,
                           uint32_t epoch) {
    return bindings_apply_epoch_since(b, a, atom, epoch, 0u);
}

static void freshen_epoch_memo_clear(FreshenEpochMemoSlot *slots, size_t cap) {
    for (size_t i = 0; i < cap; i++) {
        slots[i].src = NULL;
        slots[i].dst = NULL;
    }
}

static void freshen_epoch_memo_init(FreshenEpochMemo *memo) {
    if (!memo)
        return;
    memo->slots = memo->inline_slots;
    memo->cap = FRESHEN_EPOCH_MEMO_INLINE_CAP;
    memo->used = 0;
    memo->inline_occupied = 0u;
}

static void freshen_epoch_memo_free(FreshenEpochMemo *memo) {
    if (!memo)
        return;
    if (memo->slots != memo->inline_slots)
        free(memo->slots);
    memo->slots = memo->inline_slots;
    memo->cap = FRESHEN_EPOCH_MEMO_INLINE_CAP;
    memo->used = 0;
    memo->inline_occupied = 0u;
}

static inline bool freshen_epoch_memo_slot_occupied(
    const FreshenEpochMemo *memo, size_t pos) {
    if (memo->slots == memo->inline_slots)
        return (memo->inline_occupied & (UINT64_C(1) << pos)) != 0u;
    return memo->slots[pos].src != NULL;
}

static inline void freshen_epoch_memo_mark_occupied(
    FreshenEpochMemo *memo, size_t pos) {
    if (memo->slots == memo->inline_slots)
        memo->inline_occupied |= UINT64_C(1) << pos;
}

static size_t freshen_epoch_memo_hash(const Atom *src) {
    uintptr_t x = (uintptr_t)src;
    x >>= 4;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)x;
}

static Atom *freshen_epoch_memo_lookup(FreshenEpochMemo *memo,
                                       const Atom *src) {
    if (!memo || !src || memo->cap == 0)
        return NULL;
    size_t mask = memo->cap - 1u;
    size_t pos = freshen_epoch_memo_hash(src) & mask;
    for (;;) {
        FreshenEpochMemoSlot *slot = &memo->slots[pos];
        if (!freshen_epoch_memo_slot_occupied(memo, pos))
            return NULL;
        if (slot->src == src)
            return slot->dst;
        pos = (pos + 1u) & mask;
    }
}

static bool freshen_epoch_memo_grow(FreshenEpochMemo *memo) {
    if (!memo)
        return false;
    size_t old_cap = memo->cap;
    size_t new_cap = old_cap ? old_cap * 2u : FRESHEN_EPOCH_MEMO_INLINE_CAP;
    FreshenEpochMemoSlot *old_slots = memo->slots;
    bool old_inline = old_slots == memo->inline_slots;
    uint64_t old_inline_occupied = memo->inline_occupied;
    FreshenEpochMemoSlot *new_slots =
        cetta_malloc(sizeof(FreshenEpochMemoSlot) * new_cap);
    freshen_epoch_memo_clear(new_slots, new_cap);
    memo->slots = new_slots;
    memo->cap = new_cap;
    memo->used = 0;
    memo->inline_occupied = 0u;
    for (size_t i = 0; i < old_cap; i++) {
        if (old_inline &&
            (old_inline_occupied & (UINT64_C(1) << i)) == 0u) {
            continue;
        }
        const Atom *src = old_slots[i].src;
        Atom *dst = old_slots[i].dst;
        if (!old_inline && !src)
            continue;
        size_t mask = memo->cap - 1u;
        size_t pos = freshen_epoch_memo_hash(src) & mask;
        while (memo->slots[pos].src)
            pos = (pos + 1u) & mask;
        memo->slots[pos].src = src;
        memo->slots[pos].dst = dst;
        memo->used++;
    }
    if (old_slots != memo->inline_slots)
        free(old_slots);
    return true;
}

static bool freshen_epoch_memo_store(FreshenEpochMemo *memo,
                                     const Atom *src, Atom *dst) {
    if (!memo || !src || !dst)
        return true;
    if ((memo->used + 1u) * 4u >= memo->cap * 3u &&
        !freshen_epoch_memo_grow(memo)) {
        return false;
    }
    size_t mask = memo->cap - 1u;
    size_t pos = freshen_epoch_memo_hash(src) & mask;
    for (;;) {
        FreshenEpochMemoSlot *slot = &memo->slots[pos];
        if (!freshen_epoch_memo_slot_occupied(memo, pos)) {
            slot->src = src;
            slot->dst = dst;
            freshen_epoch_memo_mark_occupied(memo, pos);
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

static Atom *atom_freshen_epoch_impl(Arena *a, Atom *atom, uint32_t epoch,
                                     FreshenEpochMemo *memo) {
    if (!a || !atom)
        return NULL;
    if (!atom_has_vars(atom))
        return atom;
    Atom *memoized = freshen_epoch_memo_lookup(memo, atom);
    if (memoized)
        return memoized;
    Atom *out = NULL;
    switch (atom->kind) {
    case ATOM_VAR:
        out = epoch_var_atom(a, atom, epoch);
        break;
    case ATOM_EXPR: {
        Atom *draft = NULL;
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? atom_freshen_epoch_impl(a, child, epoch, memo)
                : child;
            if (!next)
                return NULL;
            if (!new_elems && next != atom->expr.elems[i]) {
                draft = atom_expr_builder_begin(a, atom->expr.len);
                if (!draft)
                    return NULL;
                new_elems = draft->expr.elems;
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        out = new_elems ? atom_expr_builder_finish(a, draft) : atom;
        break;
    }
    default:
        out = atom;
        break;
    }
    if (!freshen_epoch_memo_store(memo, atom, out))
        return NULL;
    return out;
}

Atom *atom_freshen_epoch(Arena *a, Atom *atom, uint32_t epoch) {
    FreshenEpochMemo memo;
    freshen_epoch_memo_init(&memo);
    Atom *out = atom_freshen_epoch_impl(a, atom, epoch, &memo);
    freshen_epoch_memo_free(&memo);
    return out;
}

Atom *bindings_to_atom(Arena *a, const Bindings *b) {
    Atom **assigns = NULL;
    if (b->len > 0) {
        assigns = arena_alloc(a, sizeof(Atom *) * b->len);
        for (uint32_t i = 0; i < b->len; i++) {
            Atom *key = b->entries[i].legacy_name_fallback
                ? atom_symbol_id(a, b->entries[i].spelling)
                : binding_variable_atom(a, &b->entries[i]);
            if (!key)
                return NULL;
            assigns[i] = atom_expr2(a,
                key,
                b->entries[i].val);
        }
    }
    Atom **equalities = NULL;
    if (b->eq_len > 0) {
        equalities = arena_alloc(a, sizeof(Atom *) * b->eq_len);
        for (uint32_t i = 0; i < b->eq_len; i++) {
            equalities[i] = atom_expr2(a,
                b->constraints[i].lhs,
                b->constraints[i].rhs);
        }
    }
    return atom_expr3(a,
        atom_symbol_id(a, g_builtin_syms.bindings),
        atom_expr(a, assigns, b->len),
        atom_expr(a, equalities, b->eq_len));
}

bool bindings_from_atom(Atom *atom, Bindings *out) {
    bindings_init(out);
    if (atom->kind != ATOM_EXPR || atom->expr.len != 3 ||
        !atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.bindings)) {
        return false;
    }

    Atom *assigns = atom->expr.elems[1];
    Atom *equalities = atom->expr.elems[2];
    if (assigns->kind != ATOM_EXPR || equalities->kind != ATOM_EXPR) {
        return false;
    }

    for (CettaExprIndex i = 0; i < assigns->expr.len; i++) {
        Atom *assign = assigns->expr.elems[i];
        if (assign->kind != ATOM_EXPR || assign->expr.len != 2) {
            bindings_free(out);
            return false;
        }
        Atom *key = assign->expr.elems[0];
        VarId id = VAR_ID_NONE;
        SymbolId spelling = SYMBOL_ID_NONE;
        Atom *name_key = NULL;
        bool legacy_name_fallback = false;
        if (key->kind == ATOM_VAR) {
            id = key->var_id;
            spelling = key->sym_id;
            name_key = key->name_key;
        } else if (key->kind == ATOM_SYMBOL) {
            id = g_var_intern ? var_intern(g_var_intern, key->sym_id)
                              : fresh_var_id();
            spelling = key->sym_id;
            legacy_name_fallback = true;
        } else {
            bindings_free(out);
            return false;
        }
        if (!bindings_add_inplace_internal(out, id, spelling, name_key,
                                           assign->expr.elems[1], true,
                                           legacy_name_fallback)) {
            bindings_free(out);
            return false;
        }
    }

    for (CettaExprIndex i = 0; i < equalities->expr.len; i++) {
        Atom *pair = equalities->expr.elems[i];
        if (pair->kind != ATOM_EXPR || pair->expr.len != 2) {
            bindings_free(out);
            return false;
        }
        if (!bindings_add_constraint(out, pair->expr.elems[0], pair->expr.elems[1])) {
            bindings_free(out);
            return false;
        }
    }

    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_PARSE);
    if (bindings_has_loop(out)) {
        bindings_free(out);
        return false;
    }
    return true;
}

void binding_set_init(BindingSet *bs) {
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

void binding_set_free(BindingSet *bs) {
    for (CettaIndex i = 0; i < bs->len; i++)
        bindings_free(&bs->items[i]);
    free(bs->items);
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

static CettaCount binding_set_capacity_limit(void) {
    size_t limit = SIZE_MAX / sizeof(Bindings);
    if ((uint64_t)limit > CETTA_BINDING_SET_MAX_ROWS)
        return CETTA_BINDING_SET_MAX_ROWS;
    return (CettaCount)limit;
}

static bool binding_set_ensure_one(BindingSet *bs) {
    CettaCount limit;
    CettaCount need;
    CettaCount next;
    if (!bs)
        return false;
    limit = binding_set_capacity_limit();
    if (bs->len >= limit)
        return false;
    if (bs->len < bs->cap)
        return true;
    need = bs->len + 1u;
    next = bs->cap ? bs->cap * 2u : 8u;
    if (next <= bs->cap || next < need)
        next = need;
    if (next > limit)
        next = limit;
    if (next < need)
        return false;
    bs->items = cetta_realloc(bs->items, sizeof(Bindings) * (size_t)next);
    bs->cap = next;
    return true;
}

bool binding_set_push(BindingSet *bs, const Bindings *b) {
    if (!b || !binding_set_ensure_one(bs))
        return false;
    if (!bindings_clone(&bs->items[bs->len], b))
        return false;
    bs->len++;
    return true;
}

bool binding_set_push_move(BindingSet *bs, Bindings *b) {
    if (!b || !binding_set_ensure_one(bs))
        return false;
    bindings_move(&bs->items[bs->len], b);
    bs->len++;
    return true;
}

/* ── BindingsBuilder (branch-local speculative bindings) ───────────────── */

/*
 * INVARIANT: the builder only owns speculative branch-local edits.
 * No trail entry may reference atoms published into an OutcomeSet or any
 * other cross-branch accumulator. Freezing happens only at the
 * speculation -> publication boundary.
 *
 * This first tranche intentionally supports fresh binder growth rather than
 * arbitrary in-place unification updates. Positive example: dependent
 * telescope binders added while exploring one function-argument branch.
 * Negative example: trailing through published result environments.
 */

static bool bindings_builder_trail_reserve(BindingsBuilder *bb, uint32_t needed) {
    if (needed <= bb->trail_cap)
        return true;
    uint32_t next_cap = bb->trail_cap;
    if (next_cap == 0u)
        next_cap = 8u;
    else if (next_cap > UINT32_MAX / 2u)
        next_cap = needed;
    else
        next_cap *= 2u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            next_cap = needed;
        else
            next_cap *= 2u;
    }
    if ((size_t)next_cap >
        SIZE_MAX / sizeof(BindingsBuilderTrailEntry)) {
        return false;
    }
    bb->trail = cetta_realloc(bb->trail,
                              sizeof(BindingsBuilderTrailEntry) *
                                  (size_t)next_cap);
    bb->trail_cap = next_cap;
    return true;
}

static bool bindings_builder_prime_trail_reserve(
    BindingsBuilder *bb, uint32_t needed) {
    if (needed <= bb->prime_trail_cap)
        return true;
    uint32_t next_cap = bb->prime_trail_cap;
    if (next_cap == 0u)
        next_cap = 8u;
    else if (next_cap > UINT32_MAX / 2u)
        next_cap = needed;
    else
        next_cap *= 2u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u)
            next_cap = needed;
        else
            next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / sizeof(*bb->prime_trail))
        return false;
    bb->prime_trail = cetta_realloc(
        bb->prime_trail, sizeof(*bb->prime_trail) * (size_t)next_cap);
    bb->prime_trail_cap = next_cap;
    return true;
}

static const PrimeOccurrence *bindings_builder_checkpoint_prime(
    const BindingsBuilder *bb, const BindingsBuilderTrailEntry *entry) {
    if (!entry->prime_state_present)
        return NULL;
    if (entry->prime_state_mark >= bb->prime_trail_len)
        return NULL;
    return &bb->prime_trail[entry->prime_state_mark];
}

static bool bindings_builder_snapshot(BindingsBuilder *bb) {
    if (!bb || bb->trail_len == UINT32_MAX ||
        !bindings_builder_trail_reserve(bb, bb->trail_len + 1u)) {
        return false;
    }
    bool prime_present = bindings_prime_present(&bb->current);
    if (prime_present &&
        (bb->prime_trail_len == UINT32_MAX ||
         !bindings_builder_prime_trail_reserve(
             bb, bb->prime_trail_len + 1u))) {
        return false;
    }
    bb->trail[bb->trail_len++] = (BindingsBuilderTrailEntry){
        .len = bb->current.len,
        .eq_len = bb->current.eq_len,
        .prime_state_mark = bb->prime_trail_len,
        .cycle_state = bb->current.cycle_state,
        .derived_nonzero = bindings_derived_nonzero(&bb->current),
        .prime_state_present = prime_present,
    };
    if (prime_present)
        bb->prime_trail[bb->prime_trail_len++] = *bb->current.prime_ext;
    return true;
}

static void bindings_builder_discard_latest_snapshot(BindingsBuilder *bb) {
    assert(bb && bb->trail_len > 0u);
    const BindingsBuilderTrailEntry *entry =
        &bb->trail[bb->trail_len - 1u];
    assert(entry->prime_state_mark <= bb->prime_trail_len);
    bb->prime_trail_len = entry->prime_state_mark;
    bb->trail_len--;
}

/* An activation frame may outlive one logical use of a builder.  Address plus
 * counters is not enough after free/reinit at the same address, so each
 * builder incarnation receives a never-recycled identity.  Exhaustion merely
 * disables revision-bound accelerators; generic binding semantics continue. */
static uint64_t bindings_builder_next_instance_id(void) {
    uint64_t current = atomic_load_explicit(
        &g_bindings_builder_instance_counter, memory_order_relaxed);
    while (current != 0u && current != UINT64_MAX) {
        uint64_t next = current + 1u;
        if (atomic_compare_exchange_weak_explicit(
                &g_bindings_builder_instance_counter, &current, next,
                memory_order_relaxed, memory_order_relaxed)) {
            return current;
        }
    }
    return 0u;
}

bool bindings_builder_init(BindingsBuilder *bb, const Bindings *base) {
    if (!bb)
        return false;
    bindings_init(&bb->current);
    bb->instance_id = bindings_builder_next_instance_id();
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    if (!base)
        return true;
    if (!bindings_clone(&bb->current, base)) {
        free(bb->trail);
        bb->trail = NULL;
        bb->trail_len = 0;
        bb->trail_cap = 0;
        free(bb->prime_trail);
        bb->prime_trail = NULL;
        bb->prime_trail_len = 0;
        bb->prime_trail_cap = 0;
        bb->instance_id = 0u;
        bindings_free(&bb->current);
        return false;
    }
    return true;
}

void bindings_builder_init_owned(BindingsBuilder *bb, Bindings *owned) {
    bb->current = *owned;
    bb->instance_id = bindings_builder_next_instance_id();
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bindings_init(owned);
}

void bindings_builder_free(BindingsBuilder *bb) {
    free(bb->trail);
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    free(bb->prime_trail);
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bb->instance_id = 0u;
    bindings_free(&bb->current);
}

uint32_t bindings_builder_save(const BindingsBuilder *bb) {
    return bb->trail_len;
}

void bindings_builder_rollback(BindingsBuilder *bb, uint32_t mark) {
    uint32_t old_len = bb->current.len;
    uint8_t restored_derived_nonzero = 0u;
    bool restored = false;
    while (bb->trail_len > mark) {
        BindingsBuilderTrailEntry *entry = &bb->trail[--bb->trail_len];
        const PrimeOccurrence *prime =
            bindings_builder_checkpoint_prime(bb, entry);
        bb->current.len = entry->len;
        bb->current.eq_len = entry->eq_len;
        bb->current.cycle_state = entry->cycle_state;
        restored_derived_nonzero = entry->derived_nonzero;
        restored = true;
        bindings_prime_set(
            &bb->current,
            prime ? &prime->prime_need : NULL,
            prime ? &prime->branch_state : NULL,
            prime ? prime->occurrence_token : 0u,
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
            prime ? &prime->receipt : NULL
#else
            NULL
#endif
            );
        bb->prime_trail_len = entry->prime_state_mark;
    }
    if (restored) {
        bindings_restore_derived_counts(
            &bb->current, restored_derived_nonzero);
        if (bb->rollback_count != UINT64_MAX)
            bb->rollback_count++;
    }
    /*
     * The inline cache is only an accelerator and its payload is not
     * trailed.  Clearing it is both cheaper and safer than restoring stale
     * count metadata.  The full index removes only rolled-back suffix entries.
     */
    bindings_lookup_cache_reset(&bb->current);
    if (bb->current.len < old_len)
        bindings_lookup_index_truncate(&bb->current, bb->current.len);
}

void bindings_builder_commit(BindingsBuilder *bb) {
    bb->trail_len = 0;
    bb->prime_trail_len = 0;
}

bool bindings_builder_prime_present(const BindingsBuilder *bb) {
    return bb &&
        (bindings_prime_present(&bb->current) || bb->prime_trail_len > 0u);
}

bool bindings_builder_prepare_fresh_entries(
    BindingsBuilder *bb, uint32_t additional_entries) {
    uint32_t entry_capacity;
    uint32_t trail_capacity;
    uint32_t prime_capacity = 0u;

    if (!bb)
        return false;
    if (additional_entries == 0u)
        return true;
    if (additional_entries > UINT32_MAX - bb->current.len ||
        additional_entries > UINT32_MAX - bb->trail_len)
        return false;
    entry_capacity = bb->current.len + additional_entries;
    trail_capacity = bb->trail_len + additional_entries;
    bool prime_present = bindings_prime_present(&bb->current);
    if (prime_present) {
        if (additional_entries > UINT32_MAX - bb->prime_trail_len)
            return false;
        prime_capacity = bb->prime_trail_len + additional_entries;
    }
    return bindings_reserve_entries(&bb->current, entry_capacity) &&
        bindings_builder_trail_reserve(bb, trail_capacity) &&
        (!prime_present ||
         bindings_builder_prime_trail_reserve(bb, prime_capacity));
}

static bool bindings_builder_add_constraint_internal(BindingsBuilder *bb,
                                                     Atom *lhs, Atom *rhs,
                                                     bool normalize_constraints);

static bool bindings_builder_add_id_internal(BindingsBuilder *bb, VarId var_id,
                                             SymbolId spelling, Atom *name_key,
                                             Atom *val,
                                             bool legacy_name_fallback) {
    if (!bb)
        return false;
    if (val->kind == ATOM_VAR && binding_var_eq(var_id, val->var_id))
        return true;
    if (val->kind == ATOM_VAR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_ADD_GUARD);
        Atom *other = bindings_lookup_id(&bb->current, val->var_id);
        if (other && other->kind == ATOM_VAR && binding_var_eq(other->var_id, var_id))
            return true;
    }

    int32_t existing_idx = bindings_lookup_index(&bb->current, var_id);
    if (existing_idx >= 0) {
        Atom *existing_key = bb->current.entries[existing_idx].name_key;
        if ((existing_key || name_key) &&
            (!existing_key || !name_key || !atom_eq(existing_key, name_key)))
            return false;
        Atom *existing = bb->current.entries[existing_idx].val;
        if (legacy_name_fallback &&
            !bb->current.entries[existing_idx].legacy_name_fallback) {
            return false;
        }
        if (existing == val || atom_eq(existing, val))
            return true;
        uint32_t mark = bindings_builder_save(bb);
        if (match_atoms_builder(existing, val, bb))
            return true;
        bindings_builder_rollback(bb, mark);
        return false;
    }

    if (!bindings_builder_snapshot(bb))
        return false;

    if (!bindings_reserve_entries(&bb->current, bb->current.len + 1)) {
        bindings_builder_discard_latest_snapshot(bb);
        return false;
    }
    if (legacy_name_fallback)
        bb->current.cycle_state = BINDINGS_CYCLE_UNKNOWN;
    else
        bindings_cycle_note_edge(&bb->current, var_id, val);
    bb->current.entries[bb->current.len].var_id = var_id;
    bb->current.entries[bb->current.len].spelling = spelling;
    bb->current.entries[bb->current.len].name_key = name_key;
    bb->current.entries[bb->current.len].val = val;
    bb->current.entries[bb->current.len].legacy_name_fallback = legacy_name_fallback;
    if (legacy_name_fallback)
        bb->current.legacy_fallback_count++;
    if (binding_contains_private_variant_slot(
            &bb->current.entries[bb->current.len])) {
        bb->current.private_entry_count++;
    }
    uint32_t added_index = bb->current.len;
    bb->current.len++;
    if (bb->growth_count != UINT64_MAX)
        bb->growth_count++;
    bindings_lookup_cache_note(&bb->current, var_id, added_index);
    return true;
}

bool bindings_builder_add_id_fresh(BindingsBuilder *bb, VarId var_id,
                                   SymbolId spelling, Atom *val) {
    return bindings_builder_add_id_internal(
        bb, var_id, spelling, NULL, val, false);
}

bool bindings_builder_add_var_fresh(BindingsBuilder *bb, Atom *var, Atom *val) {
    if (!var || var->kind != ATOM_VAR)
        return false;
    return bindings_builder_add_id_internal(
        bb, var->var_id, var->sym_id, var->name_key, val, false);
}

static bool bindings_builder_store_constraint(BindingsBuilder *bb,
                                              Atom *lhs, Atom *rhs) {
    BindingConstraint next = {.lhs = lhs, .rhs = rhs};
    for (uint32_t i = 0; i < bb->current.eq_len; i++) {
        if (constraint_pair_eq(&bb->current.constraints[i], &next))
            return true;
    }
    if (!bindings_builder_snapshot(bb))
        return false;
    if (!bindings_reserve_constraints(&bb->current, bb->current.eq_len + 1)) {
        bindings_builder_discard_latest_snapshot(bb);
        return false;
    }
    bb->current.constraints[bb->current.eq_len++] = next;
    if (bb->growth_count != UINT64_MAX)
        bb->growth_count++;
    if (constraint_contains_private_variant_slot(&next))
        bb->current.private_constraint_count++;
    return true;
}

static bool bindings_builder_add_constraint_internal(BindingsBuilder *bb,
                                                     Atom *lhs, Atom *rhs,
                                                     bool normalize_constraints);

static bool bindings_builder_normalize_constraints(BindingsBuilder *bb) {
    if (bb->current.eq_len == 0)
        return true;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        bb->current.eq_len, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = bb->current.eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = bb->current.constraints[i];
    bb->current.eq_len = 0;
    bb->current.private_constraint_count = 0u;
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_builder_add_constraint_internal(
                bb, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, npending,
                                              pending_stack);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, npending, pending_stack);
    return true;
}

static bool bindings_builder_add_constraint_internal(BindingsBuilder *bb,
                                                     Atom *lhs, Atom *rhs,
                                                     bool normalize_constraints) {
    Bindings *current = &bb->current;
    lhs = bindings_resolve_atom(current, lhs);
    rhs = bindings_resolve_atom(current, rhs);

    if (atom_eq_under_bindings(current, lhs, rhs))
        return true;
    if (lhs->kind == ATOM_VAR) {
        if (!bindings_builder_add_id_internal(bb, lhs->var_id, lhs->sym_id,
                                              lhs->name_key, rhs, false)) {
            return false;
        }
    } else if (rhs->kind == ATOM_VAR) {
        if (!bindings_builder_add_id_internal(bb, rhs->var_id, rhs->sym_id,
                                              rhs->name_key, lhs, false)) {
            return false;
        }
    } else if (!atom_contains_unbound_var(current, lhs) &&
               !atom_contains_unbound_var(current, rhs)) {
        return false;
    } else if (!bindings_builder_store_constraint(bb, lhs, rhs)) {
        return false;
    }

    if (normalize_constraints && !bindings_builder_normalize_constraints(bb))
        return false;
    return true;
}

bool bindings_builder_try_merge(BindingsBuilder *bb, const Bindings *src) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_MERGE);
    if (!bb || !src)
        return true;
    if (src->len == 0 && src->eq_len == 0 && !bindings_prime_present(src))
        return true;
    bindings_assert_no_private_variant_slots(&bb->current);
    bindings_assert_no_private_variant_slots(src);

    uint32_t mark = bindings_builder_save(bb);
    if (!bindings_builder_snapshot(bb))
        return false;
    if (bindings_prime_present(&bb->current) || bindings_prime_present(src)) {
        uint64_t occurrence_token = 0u;
        if (!bindings_merged_occurrence_token(
                &bb->current, src, &occurrence_token)) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
        if (!prime_need_snapshot_merge(bindings_need_mut(&bb->current),
                                       bindings_need_view(src))) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
        if (!prime_need_branch_state_merge(
                bindings_branch_state_mut(&bb->current),
                bindings_branch_state_view(src))) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        if (!prime_need_receipt_merge(
                bindings_receipt_mut(&bb->current),
                bindings_receipt_view(src))) {
            bindings_builder_rollback(bb, mark);
            return false;
        }
#endif
        bindings_prime_ext_materialize(&bb->current)->occurrence_token =
            occurrence_token;
    }
    uint32_t pending_cap = bb->current.eq_len + src->eq_len + 1;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        pending_cap, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = 0;
    for (uint32_t i = 0; i < bb->current.eq_len; i++)
        pending[npending++] = bb->current.constraints[i];
    for (uint32_t i = 0; i < src->eq_len; i++)
        pending[npending++] = src->constraints[i];

    bb->current.eq_len = 0;
    bb->current.private_constraint_count = 0u;
    for (uint32_t i = 0; i < src->len; i++) {
        if (!bindings_builder_add_id_internal(bb, src->entries[i].var_id,
                                              src->entries[i].spelling,
                                              src->entries[i].name_key,
                                              src->entries[i].val,
                                              src->entries[i].legacy_name_fallback)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            bindings_builder_rollback(bb, mark);
            return false;
        }
    }
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_builder_add_constraint_internal(
                bb, pending[i].lhs, pending[i].rhs, false)) {
            bindings_temp_constraints_release(pending, pending_cap,
                                              pending_stack);
            bindings_builder_rollback(bb, mark);
            return false;
        }
    }
    bindings_temp_constraints_release(pending, pending_cap, pending_stack);
    if (!bindings_builder_normalize_constraints(bb)) {
        bindings_builder_rollback(bb, mark);
        return false;
    }
    return true;
}

const Bindings *bindings_builder_bindings(const BindingsBuilder *bb) {
    return &bb->current;
}

void bindings_builder_take(BindingsBuilder *bb, Bindings *out) {
    bindings_move(out, &bb->current);
    free(bb->trail);
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    free(bb->prime_trail);
    bb->prime_trail = NULL;
    bb->prime_trail_len = 0;
    bb->prime_trail_cap = 0;
    bb->growth_count = 0u;
    bb->rollback_count = 0u;
    bb->instance_id = 0u;
}

/* ── Variable renaming (standardization apart) ─────────────────────────── */

/* The packed VarId ABI has a 32-bit suffix.  Keep the reservation cursor one
 * bit wider so reaching the end is distinguishable from wrapping to zero. */
static _Atomic uint64_t g_var_counter = 1;
#define FRESH_VAR_SUFFIX_BLOCK_SIZE 4096u

typedef struct {
    uint64_t next;
    uint32_t remaining;
} FreshVarSuffixBlockCache;

static __thread FreshVarSuffixBlockCache g_fresh_var_suffix_block_cache = {0};

typedef struct {
    VarId inline_items[VAR_ID_SET_INLINE_CAP];
    VarId *items;
    uint32_t len;
    uint32_t cap;
} VarIdSet;

typedef struct {
    VarId id;
    Atom *mapped;
} RenameVarEntry;

typedef struct {
    RenameVarEntry *items;
    uint32_t len;
    uint32_t cap;
} RenameVarMap;

bool fresh_var_suffix_try(uint32_t *suffix_out) {
    if (!suffix_out)
        return false;

    if (g_fresh_var_suffix_block_cache.remaining > 0u) {
        uint64_t suffix = g_fresh_var_suffix_block_cache.next++;

        g_fresh_var_suffix_block_cache.remaining--;
        if (suffix == 0u || suffix > UINT32_MAX) {
            g_fresh_var_suffix_block_cache.remaining = 0u;
            return false;
        }
        *suffix_out = (uint32_t)suffix;
        return true;
    }

    for (;;) {
        uint64_t start = atomic_load_explicit(
            &g_var_counter, memory_order_relaxed);
        uint64_t available;
        uint64_t block_size;
        uint64_t after;

        /* Epoch zero means "no standardization-apart tag".  UINT32_MAX + 1
         * is the exhausted sentinel in the wider reservation cursor. */
        if (start == 0u || start > UINT32_MAX)
            return false;
        available = (uint64_t)UINT32_MAX - start + 1u;
        block_size = available < FRESH_VAR_SUFFIX_BLOCK_SIZE
            ? available : FRESH_VAR_SUFFIX_BLOCK_SIZE;
        after = start + block_size;
        if (!atomic_compare_exchange_weak_explicit(
                &g_var_counter, &start, after,
                memory_order_relaxed, memory_order_relaxed))
            continue;
        g_fresh_var_suffix_block_cache.next = start;
        g_fresh_var_suffix_block_cache.remaining = (uint32_t)block_size;
        return fresh_var_suffix_try(suffix_out);
    }
}

uint32_t fresh_var_suffix(void) {
    uint32_t suffix;

    if (fresh_var_suffix_try(&suffix))
        return suffix;
    fputs("fatal: fresh-variable suffix space exhausted\n", stderr);
    abort();
}

#ifdef CETTA_TEST_HOOKS
void fresh_var_suffix_test_reset(uint64_t next_suffix) {
    atomic_store_explicit(
        &g_var_counter, next_suffix, memory_order_relaxed);
    g_fresh_var_suffix_block_cache.next = 0u;
    g_fresh_var_suffix_block_cache.remaining = 0u;
}
#endif

static void var_id_set_init(VarIdSet *set) {
    set->items = set->inline_items;
    set->len = 0;
    set->cap = VAR_ID_SET_INLINE_CAP;
}

static void var_id_set_free(VarIdSet *set) {
    if (set->items != set->inline_items)
        free(set->items);
    set->items = set->inline_items;
    set->len = 0;
    set->cap = VAR_ID_SET_INLINE_CAP;
}

static bool var_id_set_contains(const VarIdSet *set, VarId id) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i] == id)
            return true;
    }
    return false;
}

static bool var_id_set_add(VarIdSet *set, VarId id) {
    if (var_id_set_contains(set, id))
        return true;
    if (set->len >= set->cap) {
        uint32_t next_cap = set->cap ? set->cap * 2u : 8u;
        if (next_cap < set->cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*set->items))
            return false;
        if (set->items == set->inline_items) {
            VarId *items = cetta_malloc(
                sizeof(*items) * (size_t)next_cap);
            memcpy(items, set->inline_items,
                   sizeof(*items) * (size_t)set->len);
            set->items = items;
        } else {
            set->items = cetta_realloc(
                set->items, sizeof(*set->items) * (size_t)next_cap);
        }
        set->cap = next_cap;
    }
    set->items[set->len++] = id;
    return true;
}

typedef enum {
    RENAME_WALK_ENTER,
    RENAME_WALK_LEAVE,
} RenameWalkKind;

typedef struct {
    RenameWalkKind kind;
    Atom *atom;
} RenameWalkTask;

typedef struct {
    RenameWalkTask inline_tasks[64];
    RenameWalkTask *tasks;
    size_t len;
    size_t cap;
} RenameWalkStack;

static void rename_walk_stack_init(RenameWalkStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void rename_walk_stack_free(RenameWalkStack *stack) {
    if (stack->tasks != stack->inline_tasks)
        free(stack->tasks);
}

static bool rename_walk_stack_push(RenameWalkStack *stack,
                                   RenameWalkTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            RenameWalkTask *next = cetta_malloc(
                sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks,
                   sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

/* These addresses are private traversal states, never runtime atoms. */
static Atom g_rename_walk_active;
static Atom g_rename_walk_complete;

/* Hash-stable atoms are immutable published graphs assembled only from
 * hash-stable children. They cannot acquire a back edge after publication,
 * so variable collection needs neither active/complete states nor a memo. */
static bool collect_var_ids_hash_stable(Atom *root, VarIdSet *set) {
    RenameWalkStack stack;
    rename_walk_stack_init(&stack);
    if (!rename_walk_stack_push(
            &stack, (RenameWalkTask){RENAME_WALK_ENTER, root}))
        goto fail;
    while (stack.len > 0u) {
        Atom *atom = stack.tasks[--stack.len].atom;
        if (!atom)
            goto fail;
        if (!atom_has_vars(atom))
            continue;
        if (atom->kind == ATOM_VAR) {
            if (!var_id_set_add(set, atom->var_id))
                goto fail;
            continue;
        }
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--) {
            if (!rename_walk_stack_push(
                    &stack,
                    (RenameWalkTask){RENAME_WALK_ENTER,
                                     atom->expr.elems[i - 1u]}))
                goto fail;
        }
    }
    rename_walk_stack_free(&stack);
    return true;

fail:
    rename_walk_stack_free(&stack);
    return false;
}

static bool collect_var_ids(Atom *root, VarIdSet *set) {
    if (!root || !set)
        return false;
    if ((root->flags & ATOM_FLAG_HASH_STABLE) != 0u)
        return collect_var_ids_hash_stable(root, set);
    RenameWalkStack stack;
    FreshenEpochMemo states;
    rename_walk_stack_init(&stack);
    freshen_epoch_memo_init(&states);
    if (!rename_walk_stack_push(
            &stack, (RenameWalkTask){RENAME_WALK_ENTER, root}))
        goto fail;
    while (stack.len > 0u) {
        RenameWalkTask task = stack.tasks[--stack.len];
        Atom *atom = task.atom;
        if (!atom)
            goto fail;
        if (!atom_has_vars(atom))
            continue;
        if (task.kind == RENAME_WALK_LEAVE) {
            if (!freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        Atom *state = freshen_epoch_memo_lookup(&states, atom);
        if (state == &g_rename_walk_active)
            goto fail;
        if (state == &g_rename_walk_complete)
            continue;
        if (!freshen_epoch_memo_store(
                &states, atom, &g_rename_walk_active))
            goto fail;
        if (atom->kind == ATOM_VAR) {
            if (!var_id_set_add(set, atom->var_id) ||
                !freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        if (atom->kind != ATOM_EXPR) {
            if (!freshen_epoch_memo_store(
                    &states, atom, &g_rename_walk_complete))
                goto fail;
            continue;
        }
        if (!rename_walk_stack_push(
                &stack, (RenameWalkTask){RENAME_WALK_LEAVE, atom}))
            goto fail;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--)
            if (!rename_walk_stack_push(
                    &stack,
                    (RenameWalkTask){RENAME_WALK_ENTER,
                                     atom->expr.elems[i - 1u]}))
                goto fail;
    }
    freshen_epoch_memo_free(&states);
    rename_walk_stack_free(&stack);
    return true;

fail:
    freshen_epoch_memo_free(&states);
    rename_walk_stack_free(&stack);
    return false;
}

/*
 * If the current substitution graph is acyclic, adding x -> value can create
 * a cycle exactly when `value` already reaches x.  Explore only that reachable
 * slice.  This is the incremental occurs-check counterpart of
 * bindings_has_loop's full-graph oracle.
 */
static BindingsReachability bindings_value_reaches_var(
    Bindings *bindings, Atom *value, VarId target) {
    if (!bindings || !value || target == VAR_ID_NONE)
        return BINDINGS_REACHABILITY_UNKNOWN;

    /* The caller maintains an acyclic substitution graph.  Follow its common
     * variable-only spine directly; no visited set is needed until an
     * expression introduces branching. */
    while (value->kind == ATOM_VAR) {
        if (binding_var_eq(value->var_id, target))
            return BINDINGS_REACHABILITY_PRESENT;
        value = bindings_lookup_id(bindings, value->var_id);
        if (!value)
            return BINDINGS_REACHABILITY_ABSENT;
    }
    if (!atom_has_vars(value))
        return BINDINGS_REACHABILITY_ABSENT;

    VarIdSet reachable;
    var_id_set_init(&reachable);
    if (!collect_var_ids(value, &reachable)) {
        var_id_set_free(&reachable);
        return BINDINGS_REACHABILITY_UNKNOWN;
    }

    for (uint32_t cursor = 0u; cursor < reachable.len; cursor++) {
        VarId current = reachable.items[cursor];
        if (binding_var_eq(current, target)) {
            var_id_set_free(&reachable);
            return BINDINGS_REACHABILITY_PRESENT;
        }
        int32_t index = bindings_lookup_index(bindings, current);
        if (index < 0)
            continue;
        Atom *next = bindings->entries[(uint32_t)index].val;
        if (next && next->kind == ATOM_VAR &&
            binding_var_eq(next->var_id, current)) {
            continue;
        }
        if (next && atom_has_vars(next) &&
            !collect_var_ids(next, &reachable)) {
            var_id_set_free(&reachable);
            return BINDINGS_REACHABILITY_UNKNOWN;
        }
    }

    var_id_set_free(&reachable);
    return BINDINGS_REACHABILITY_ABSENT;
}

/*
 * Logical-environment projection
 * --------------------------------
 *
 * A long-lived explicit machine cannot retain every fresh clause variable
 * ever encountered.  At a semantic safe point it needs the transitive closure
 * of the variables still named by its continuation.  This is deliberately a
 * property of Bindings rather than of any one evaluator.
 *
 * The work set is hashed: a variable chain of length n is projected in O(n)
 * expected work instead of repeatedly rescanning an n-entry environment.
 */
typedef struct {
    VarId *slots;
    size_t slot_cap;
    size_t slot_len;
    VarId *work;
    size_t work_len;
    size_t work_cap;
    size_t work_next;
} BindingsReachableVars;

typedef struct {
    VarId id;
    uint32_t index_plus_one;
} BindingsReachableIndexSlot;

static void bindings_reachable_vars_init(BindingsReachableVars *vars) {
    memset(vars, 0, sizeof(*vars));
}

static void bindings_reachable_vars_free(BindingsReachableVars *vars) {
    free(vars->slots);
    free(vars->work);
    bindings_reachable_vars_init(vars);
}

static bool bindings_reachable_vars_rehash(
    BindingsReachableVars *vars, size_t requested_cap) {
    size_t cap = 16u;
    while (cap < requested_cap) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / sizeof(*vars->slots))
        return false;
    VarId *slots = cetta_malloc(cap * sizeof(*slots));
    memset(slots, 0, cap * sizeof(*slots));
    for (size_t i = 0u; i < vars->slot_cap; i++) {
        VarId id = vars->slots[i];
        if (id == VAR_ID_NONE)
            continue;
        size_t slot = bindings_var_id_hash(id) & (cap - 1u);
        while (slots[slot] != VAR_ID_NONE)
            slot = (slot + 1u) & (cap - 1u);
        slots[slot] = id;
    }
    free(vars->slots);
    vars->slots = slots;
    vars->slot_cap = cap;
    return true;
}

static bool bindings_reachable_vars_contains(
    const BindingsReachableVars *vars, VarId id) {
    if (!vars || id == VAR_ID_NONE || vars->slot_cap == 0u)
        return false;
    size_t slot =
        bindings_var_id_hash(id) & (vars->slot_cap - 1u);
    while (vars->slots[slot] != VAR_ID_NONE) {
        if (vars->slots[slot] == id)
            return true;
        slot = (slot + 1u) & (vars->slot_cap - 1u);
    }
    return false;
}

static bool bindings_reachable_vars_add(
    BindingsReachableVars *vars, VarId id) {
    if (!vars || id == VAR_ID_NONE)
        return false;
    if (bindings_reachable_vars_contains(vars, id))
        return true;
    if (vars->slot_cap == 0u ||
        vars->slot_len + 1u >= vars->slot_cap / 2u) {
        size_t requested = vars->slot_cap
            ? vars->slot_cap * 2u : 16u;
        if (requested < vars->slot_cap ||
            !bindings_reachable_vars_rehash(vars, requested)) {
            return false;
        }
    }
    if (vars->work_len == vars->work_cap) {
        size_t next = vars->work_cap ? vars->work_cap * 2u : 16u;
        if (next < vars->work_cap ||
            next > SIZE_MAX / sizeof(*vars->work)) {
            return false;
        }
        vars->work = cetta_realloc(
            vars->work, next * sizeof(*vars->work));
        vars->work_cap = next;
    }
    size_t slot =
        bindings_var_id_hash(id) & (vars->slot_cap - 1u);
    while (vars->slots[slot] != VAR_ID_NONE)
        slot = (slot + 1u) & (vars->slot_cap - 1u);
    vars->slots[slot] = id;
    vars->slot_len++;
    vars->work[vars->work_len++] = id;
    return true;
}

static bool bindings_reachable_vars_add_atom(
    BindingsReachableVars *vars, Atom *atom) {
    if (!atom || !atom_has_vars(atom))
        return true;
    VarIdSet found;
    var_id_set_init(&found);
    if (!collect_var_ids(atom, &found)) {
        var_id_set_free(&found);
        return false;
    }
    for (uint32_t i = 0u; i < found.len; i++) {
        if (!bindings_reachable_vars_add(vars, found.items[i])) {
            var_id_set_free(&found);
            return false;
        }
    }
    var_id_set_free(&found);
    return true;
}

static bool bindings_reachable_vars_add_epoch_root(
    BindingsReachableVars *vars,
    const BindingsEpochRoot *root) {
    if (!vars || !root || !root->atom || root->epoch == 0u)
        return false;
    if (!atom_has_vars(root->atom))
        return true;
    if (root->variable_support) {
        CettaTermVariableSupportIterator iterator;
        cetta_term_variable_support_iterator_init(
            &iterator, root->variable_support);
        uint32_t base_id = 0u;
        while (cetta_term_variable_support_iterator_next(
                   &iterator, &base_id)) {
            if (!bindings_reachable_vars_add(
                    vars, var_epoch_id((VarId)base_id,
                                       root->epoch))) {
                return false;
            }
        }
        return true;
    }
    VarIdSet found;
    var_id_set_init(&found);
    if (!collect_var_ids(root->atom, &found)) {
        var_id_set_free(&found);
        return false;
    }
    for (uint32_t i = 0u; i < found.len; i++) {
        if (!bindings_reachable_vars_add(
                vars, var_epoch_id(found.items[i], root->epoch))) {
            var_id_set_free(&found);
            return false;
        }
    }
    var_id_set_free(&found);
    return true;
}

static bool bindings_reachable_atom_intersects(
    const BindingsReachableVars *vars, Atom *atom, bool *intersects) {
    *intersects = false;
    if (!atom || !atom_has_vars(atom))
        return true;
    VarIdSet found;
    var_id_set_init(&found);
    if (!collect_var_ids(atom, &found)) {
        var_id_set_free(&found);
        return false;
    }
    for (uint32_t i = 0u; i < found.len; i++) {
        if (bindings_reachable_vars_contains(vars, found.items[i])) {
            *intersects = true;
            break;
        }
    }
    var_id_set_free(&found);
    return true;
}

static bool bindings_reachable_index_build(
    const Bindings *src, BindingsReachableIndexSlot **slots_out,
    size_t *cap_out) {
    *slots_out = NULL;
    *cap_out = 0u;
    if (!src || src->len == 0u)
        return true;
    size_t needed = (size_t)src->len * 2u;
    if (needed < src->len)
        return false;
    size_t cap = 16u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u)
            return false;
        cap *= 2u;
    }
    if (cap > SIZE_MAX / sizeof(**slots_out))
        return false;
    BindingsReachableIndexSlot *slots =
        cetta_malloc(cap * sizeof(*slots));
    memset(slots, 0, cap * sizeof(*slots));
    for (uint32_t i = 0u; i < src->len; i++) {
        VarId id = src->entries[i].var_id;
        if (id == VAR_ID_NONE) {
            free(slots);
            return false;
        }
        size_t slot = bindings_var_id_hash(id) & (cap - 1u);
        while (slots[slot].id != VAR_ID_NONE &&
               slots[slot].id != id) {
            slot = (slot + 1u) & (cap - 1u);
        }
        /* Match lookup scans newest-to-oldest, so duplicates map to newest. */
        slots[slot].id = id;
        slots[slot].index_plus_one = i + 1u;
    }
    *slots_out = slots;
    *cap_out = cap;
    return true;
}

static uint32_t bindings_reachable_index_lookup(
    const BindingsReachableIndexSlot *slots, size_t cap, VarId id) {
    if (!slots || cap == 0u || id == VAR_ID_NONE)
        return 0u;
    size_t slot = bindings_var_id_hash(id) & (cap - 1u);
    while (slots[slot].id != VAR_ID_NONE) {
        if (slots[slot].id == id)
            return slots[slot].index_plus_one;
        slot = (slot + 1u) & (cap - 1u);
    }
    return 0u;
}

static int bindings_reachable_entry_index_compare(
    const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

/*
 * Project a modern unconstrained environment by following only the variables
 * reachable from the roots through the maintained VarId index.  The first
 * call may synchronize that derived index in O(|environment|); subsequent
 * projections visit O(|reachable bindings|) entries.  Sorting selected entry
 * positions restores their authoritative relative order.
 *
 * The dense projector below remains the semantic fallback for legacy spelling
 * lookup, constraints, compactor selection maps, small environments, and an
 * explicitly disabled lookup index.
 */
static bool bindings_project_reachable_sparse(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    Bindings *dst) {
    if (!src || !dst || src == dst ||
        (root_count > 0u && !roots) ||
        (epoch_root_count > 0u && !epoch_roots) ||
        src->legacy_fallback_count != 0u ||
        src->eq_len != 0u ||
        src->len < BINDINGS_LOOKUP_INDEX_THRESHOLD ||
        !bindings_lookup_index_enabled()) {
        return false;
    }

    Bindings *indexed = (Bindings *)src;
    BindingsLookupIndex *lookup =
        bindings_lookup_index_current(indexed);
    if (!lookup)
        return false;

    BindingsReachableVars live;
    bindings_reachable_vars_init(&live);
    uint32_t *selected = NULL;
    size_t selected_len = 0u;
    size_t selected_cap = 0u;
    bindings_init(dst);

    for (size_t i = 0u; i < root_count; i++) {
        if (!bindings_reachable_vars_add_atom(&live, roots[i]))
            goto fail;
    }
    for (size_t i = 0u; i < epoch_root_count; i++) {
        if (!bindings_reachable_vars_add_epoch_root(
                &live, &epoch_roots[i])) {
            goto fail;
        }
    }
    while (live.work_next < live.work_len) {
        VarId id = live.work[live.work_next++];
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_SPARSE_INDEX_LOOKUP);
        uint32_t index_plus_one =
            bindings_lookup_index_find(lookup, id);
        if (index_plus_one == 0u)
            continue;
        uint32_t entry_index = index_plus_one - 1u;
        if (entry_index >= src->len ||
            !binding_var_eq(
                src->entries[entry_index].var_id, id)) {
            goto fail;
        }
        if (selected_len == selected_cap) {
            size_t next = selected_cap ? selected_cap * 2u : 16u;
            if (next < selected_cap ||
                next > SIZE_MAX / sizeof(*selected)) {
                goto fail;
            }
            selected = cetta_realloc(
                selected, next * sizeof(*selected));
            selected_cap = next;
        }
        selected[selected_len++] = entry_index;
        if (!bindings_reachable_vars_add_atom(
                &live, src->entries[entry_index].name_key) ||
            !bindings_reachable_vars_add_atom(
                &live, src->entries[entry_index].val)) {
            goto fail;
        }
    }

    if (selected_len > 1u) {
        qsort(
            selected, selected_len, sizeof(*selected),
            bindings_reachable_entry_index_compare);
    }
    if (selected_len > UINT32_MAX ||
        (selected_len > 0u &&
         !bindings_reserve_entries(
             dst, (uint32_t)selected_len))) {
        goto fail;
    }
    for (size_t i = 0u; i < selected_len; i++) {
        const Binding *binding =
            &src->entries[selected[i]];
        dst->entries[dst->len++] = *binding;
        if (binding_contains_private_variant_slot(binding))
            dst->private_entry_count++;
    }
    dst->cycle_state =
        src->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;
    bindings_prime_assign(dst, src);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_SPARSE);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_SPARSE_ENTRY,
        selected_len);
    free(selected);
    bindings_reachable_vars_free(&live);
    return true;

fail:
    free(selected);
    bindings_reachable_vars_free(&live);
    bindings_free(dst);
    bindings_init(dst);
    return false;
}

static bool bindings_project_reachable_selected(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    Bindings *dst, bool **keep_entries_out,
    bool **keep_constraints_out) {
    bool return_selection =
        keep_entries_out != NULL && keep_constraints_out != NULL;
    if ((keep_entries_out == NULL) !=
            (keep_constraints_out == NULL) ||
        !dst || src == dst || (root_count > 0u && !roots)) {
        return false;
    }
    if (epoch_root_count > 0u && !epoch_roots)
        return false;
    if (return_selection) {
        *keep_entries_out = NULL;
        *keep_constraints_out = NULL;
    }
    bindings_init(dst);
    if (!src)
        return true;
    if (!return_selection &&
        src->legacy_fallback_count == 0u &&
        src->eq_len == 0u &&
        src->len >= BINDINGS_LOOKUP_INDEX_THRESHOLD &&
        bindings_lookup_index_enabled()) {
        return bindings_project_reachable_sparse(
            src, roots, root_count,
            epoch_roots, epoch_root_count, dst);
    }
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_PROJECT_DENSE);

    BindingsReachableVars live;
    BindingsReachableIndexSlot *index_slots = NULL;
    size_t index_cap = 0u;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    bool has_legacy = false;
    bindings_reachable_vars_init(&live);

    if (!bindings_reachable_index_build(
            src, &index_slots, &index_cap)) {
        goto fail;
    }
    if (src->len > 0u) {
        keep_entries = cetta_malloc(
            (size_t)src->len * sizeof(*keep_entries));
        memset(keep_entries, 0,
               (size_t)src->len * sizeof(*keep_entries));
    }
    if (src->eq_len > 0u) {
        keep_constraints = cetta_malloc(
            (size_t)src->eq_len * sizeof(*keep_constraints));
        memset(keep_constraints, 0,
               (size_t)src->eq_len * sizeof(*keep_constraints));
    }
    for (size_t i = 0u; i < root_count; i++) {
        if (!bindings_reachable_vars_add_atom(&live, roots[i]))
            goto fail;
    }
    for (size_t i = 0u; i < epoch_root_count; i++) {
        if (!bindings_reachable_vars_add_epoch_root(
                &live, &epoch_roots[i])) {
            goto fail;
        }
    }

    /*
     * Legacy bindings are keyed by spelling and can therefore be reached by
     * a variable whose VarId is absent from the table.  Keep them all rather
     * than silently changing that compatibility behavior.
     */
    for (uint32_t i = 0u; i < src->len; i++) {
        if (!src->entries[i].legacy_name_fallback)
            continue;
        has_legacy = true;
        keep_entries[i] = true;
        if (!bindings_reachable_vars_add(
                &live, src->entries[i].var_id) ||
            !bindings_reachable_vars_add_atom(
                &live, src->entries[i].name_key) ||
            !bindings_reachable_vars_add_atom(
                &live, src->entries[i].val)) {
            goto fail;
        }
    }

    for (;;) {
        while (live.work_next < live.work_len) {
            VarId id = live.work[live.work_next++];
            uint32_t index_plus_one =
                bindings_reachable_index_lookup(
                    index_slots, index_cap, id);
            if (index_plus_one == 0u)
                continue;
            uint32_t index = index_plus_one - 1u;
            if (keep_entries[index])
                continue;
            keep_entries[index] = true;
            if (!bindings_reachable_vars_add_atom(
                    &live, src->entries[index].name_key) ||
                !bindings_reachable_vars_add_atom(
                    &live, src->entries[index].val)) {
                goto fail;
            }
        }

        bool added_constraint = false;
        for (uint32_t i = 0u; i < src->eq_len; i++) {
            if (keep_constraints[i])
                continue;
            bool lhs_live = false;
            bool rhs_live = false;
            bool ground =
                !atom_has_vars(src->constraints[i].lhs) &&
                !atom_has_vars(src->constraints[i].rhs);
            if (!ground && !has_legacy) {
                if (!bindings_reachable_atom_intersects(
                        &live, src->constraints[i].lhs, &lhs_live) ||
                    !bindings_reachable_atom_intersects(
                        &live, src->constraints[i].rhs, &rhs_live)) {
                    goto fail;
                }
            }
            if (!ground && !has_legacy && !lhs_live && !rhs_live)
                continue;
            keep_constraints[i] = true;
            added_constraint = true;
            if (!bindings_reachable_vars_add_atom(
                    &live, src->constraints[i].lhs) ||
                !bindings_reachable_vars_add_atom(
                    &live, src->constraints[i].rhs)) {
                goto fail;
            }
        }
        if (live.work_next == live.work_len && !added_constraint)
            break;
    }

    uint32_t kept_entries = 0u;
    for (uint32_t i = 0u; i < src->len; i++)
        kept_entries += keep_entries[i] ? 1u : 0u;
    uint32_t kept_constraints = 0u;
    for (uint32_t i = 0u; i < src->eq_len; i++)
        kept_constraints += keep_constraints[i] ? 1u : 0u;
    if (kept_entries > 0u &&
        !bindings_reserve_entries(dst, kept_entries)) {
        goto fail_dst;
    }
    if (kept_constraints > 0u &&
        !bindings_reserve_constraints(dst, kept_constraints)) {
        goto fail_dst;
    }
    for (uint32_t i = 0u; i < src->len; i++) {
        if (keep_entries[i]) {
            dst->entries[dst->len++] = src->entries[i];
            if (src->entries[i].legacy_name_fallback)
                dst->legacy_fallback_count++;
            if (binding_contains_private_variant_slot(
                    &src->entries[i])) {
                dst->private_entry_count++;
            }
        }
    }
    for (uint32_t i = 0u; i < src->eq_len; i++) {
        if (keep_constraints[i]) {
            dst->constraints[dst->eq_len++] = src->constraints[i];
            if (constraint_contains_private_variant_slot(
                    &src->constraints[i])) {
                dst->private_constraint_count++;
            }
        }
    }
    dst->cycle_state =
        src->cycle_state == BINDINGS_CYCLE_ACYCLIC
            ? BINDINGS_CYCLE_ACYCLIC
            : BINDINGS_CYCLE_UNKNOWN;
    bindings_prime_assign(dst, src);

    free(index_slots);
    if (return_selection) {
        *keep_entries_out = keep_entries;
        *keep_constraints_out = keep_constraints;
    } else {
        free(keep_entries);
        free(keep_constraints);
    }
    bindings_reachable_vars_free(&live);
    return true;

fail_dst:
    bindings_free(dst);
    bindings_init(dst);
fail:
    free(index_slots);
    free(keep_entries);
    free(keep_constraints);
    bindings_reachable_vars_free(&live);
    return false;
}

bool bindings_project_reachable_with_epoch_roots_and_entry_marks(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count, uint32_t *entry_marks,
    size_t entry_mark_count, Bindings *dst) {
    if (!dst ||
        (epoch_root_count > 0u && !epoch_roots) ||
        (entry_mark_count > 0u && (!src || !entry_marks)) ||
        entry_mark_count > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }
    for (size_t index = 0u; index < entry_mark_count; index++) {
        if (entry_marks[index] > src->len)
            return false;
    }

    /* Selection maps exist only to translate entry-prefix marks.  Asking for
     * them when there are no marks forces the dense projector and defeats the
     * maintained VarId index for every ordinary host/child projection. */
    if (entry_mark_count == 0u) {
        return bindings_project_reachable_selected(
            src, roots, root_count,
            epoch_roots, epoch_root_count, dst,
            NULL, NULL);
    }

    Bindings projected;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    if (!bindings_project_reachable_selected(
            src, roots, root_count,
            epoch_roots, epoch_root_count, &projected,
            &keep_entries, &keep_constraints)) {
        return false;
    }

    uint32_t *next_marks = entry_mark_count
        ? malloc(entry_mark_count * sizeof(*next_marks)) : NULL;
    if (entry_mark_count > 0u && !next_marks) {
        bindings_free(&projected);
        free(keep_entries);
        free(keep_constraints);
        return false;
    }
    for (size_t mark_index = 0u;
         mark_index < entry_mark_count; mark_index++) {
        uint32_t retained = 0u;
        for (uint32_t entry = 0u;
             entry < entry_marks[mark_index]; entry++) {
            if (keep_entries[entry])
                retained++;
        }
        next_marks[mark_index] = retained;
    }

    *dst = projected;
    if (entry_mark_count > 0u) {
        memcpy(entry_marks, next_marks,
               entry_mark_count * sizeof(*entry_marks));
    }
    free(next_marks);
    free(keep_entries);
    free(keep_constraints);
    return true;
}

bool bindings_project_reachable_with_entry_marks(
    const Bindings *src, Atom *const *roots, size_t root_count,
    uint32_t *entry_marks, size_t entry_mark_count,
    Bindings *dst) {
    return bindings_project_reachable_with_epoch_roots_and_entry_marks(
        src, roots, root_count, NULL, 0u,
        entry_marks, entry_mark_count, dst);
}

bool bindings_project_reachable_with_epoch_roots(
    const Bindings *src, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count, Bindings *dst) {
    return bindings_project_reachable_with_epoch_roots_and_entry_marks(
        src, roots, root_count, epoch_roots, epoch_root_count,
        NULL, 0u, dst);
}
bool bindings_project_reachable(
    const Bindings *src, Atom *const *roots, size_t root_count,
    Bindings *dst) {
    return bindings_project_reachable_with_entry_marks(
        src, roots, root_count, NULL, 0u, dst);
}

static int bindings_checkpoint_mark_compare(
    const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static size_t bindings_checkpoint_mark_find(
    const uint32_t *marks, size_t count, uint32_t target) {
    size_t low = 0u;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (marks[middle] < target)
            low = middle + 1u;
        else
            high = middle;
    }
    return low;
}

bool bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint32_t *entry_marks, size_t entry_mark_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    if (discarded_logical_items)
        *discarded_logical_items = 0u;
    if (discarded_trail_entries)
        *discarded_trail_entries = 0u;
    if (!bb || (root_count > 0u && !roots) ||
        (epoch_root_count > 0u && !epoch_roots) ||
        (checkpoint_count > 0u && !checkpoint_marks) ||
        (entry_mark_count > 0u && !entry_marks) ||
        checkpoint_count > UINT32_MAX ||
        entry_mark_count > SIZE_MAX / sizeof(uint32_t)) {
        return false;
    }

    uint32_t old_trail_len = bb->trail_len;
    for (size_t i = 0u; i < checkpoint_count; i++) {
        if (checkpoint_marks[i] > old_trail_len)
            return false;
    }
    for (size_t i = 0u; i < entry_mark_count; i++) {
        if (entry_marks[i] > bb->current.len)
            return false;
    }

    Bindings projected;
    bool *keep_entries = NULL;
    bool *keep_constraints = NULL;
    if (!bindings_project_reachable_selected(
            &bb->current, roots, root_count,
            epoch_roots, epoch_root_count, &projected,
            &keep_entries, &keep_constraints)) {
        return false;
    }

    size_t entry_prefix_count = (size_t)bb->current.len + 1u;
    size_t constraint_prefix_count =
        (size_t)bb->current.eq_len + 1u;
    if (entry_prefix_count > SIZE_MAX / sizeof(uint32_t) ||
        constraint_prefix_count > SIZE_MAX / sizeof(uint32_t)) {
        bindings_free(&projected);
        free(keep_entries);
        free(keep_constraints);
        return false;
    }

    uint32_t *entry_prefix =
        cetta_malloc(entry_prefix_count * sizeof(*entry_prefix));
    uint32_t *legacy_prefix =
        cetta_malloc(entry_prefix_count * sizeof(*legacy_prefix));
    uint32_t *private_entry_prefix =
        cetta_malloc(entry_prefix_count *
                     sizeof(*private_entry_prefix));
    uint32_t *constraint_prefix =
        cetta_malloc(constraint_prefix_count *
                     sizeof(*constraint_prefix));
    uint32_t *private_constraint_prefix =
        cetta_malloc(constraint_prefix_count *
                     sizeof(*private_constraint_prefix));
    entry_prefix[0] = 0u;
    legacy_prefix[0] = 0u;
    private_entry_prefix[0] = 0u;
    for (uint32_t i = 0u; i < bb->current.len; i++) {
        bool keep = keep_entries[i];
        entry_prefix[i + 1u] =
            entry_prefix[i] + (keep ? 1u : 0u);
        legacy_prefix[i + 1u] =
            legacy_prefix[i] +
            (keep && bb->current.entries[i].legacy_name_fallback
                 ? 1u : 0u);
        private_entry_prefix[i + 1u] =
            private_entry_prefix[i] +
            (keep && binding_contains_private_variant_slot(
                         &bb->current.entries[i])
                 ? 1u : 0u);
    }
    constraint_prefix[0] = 0u;
    private_constraint_prefix[0] = 0u;
    for (uint32_t i = 0u; i < bb->current.eq_len; i++) {
        bool keep = keep_constraints[i];
        constraint_prefix[i + 1u] =
            constraint_prefix[i] + (keep ? 1u : 0u);
        private_constraint_prefix[i + 1u] =
            private_constraint_prefix[i] +
            (keep && constraint_contains_private_variant_slot(
                         &bb->current.constraints[i])
                 ? 1u : 0u);
    }

    uint32_t *sorted_marks = NULL;
    uint32_t *next_marks = NULL;
    uint32_t *next_entry_marks = NULL;
    BindingsBuilderTrailEntry *next_trail = NULL;
    PrimeOccurrence *next_prime_trail = NULL;
    uint32_t next_prime_len = 0u;
    uint32_t next_prime_cap = 0u;
    size_t unique_count = 0u;
    if (checkpoint_count > 0u) {
        sorted_marks = cetta_malloc(
            checkpoint_count * sizeof(*sorted_marks));
        next_marks = cetta_malloc(
            checkpoint_count * sizeof(*next_marks));
        memcpy(sorted_marks, checkpoint_marks,
               checkpoint_count * sizeof(*sorted_marks));
        qsort(sorted_marks, checkpoint_count,
              sizeof(*sorted_marks),
              bindings_checkpoint_mark_compare);
        for (size_t i = 0u; i < checkpoint_count; i++) {
            if (unique_count == 0u ||
                sorted_marks[i] !=
                    sorted_marks[unique_count - 1u]) {
                sorted_marks[unique_count++] = sorted_marks[i];
            }
        }
        next_trail = cetta_malloc(
            unique_count * sizeof(*next_trail));
        for (size_t i = 0u; i < unique_count; i++) {
            uint32_t mark = sorted_marks[i];
            bool prime_present =
                mark == old_trail_len
                    ? bindings_prime_present(&bb->current)
                    : bb->trail[mark].prime_state_present != 0u;
            if (prime_present)
                next_prime_cap++;
        }
        if (next_prime_cap > 0u) {
            next_prime_trail = cetta_malloc(
                (size_t)next_prime_cap * sizeof(*next_prime_trail));
        }
    }
    if (entry_mark_count > 0u) {
        next_entry_marks = malloc(
            entry_mark_count * sizeof(*next_entry_marks));
        if (!next_entry_marks) {
            bindings_free(&projected);
            free(keep_entries);
            free(keep_constraints);
            free(entry_prefix);
            free(legacy_prefix);
            free(private_entry_prefix);
            free(constraint_prefix);
            free(private_constraint_prefix);
            free(sorted_marks);
            free(next_marks);
            free(next_trail);
            free(next_prime_trail);
            return false;
        }
        for (size_t i = 0u; i < entry_mark_count; i++)
            next_entry_marks[i] = entry_prefix[entry_marks[i]];
    }

    bool valid = true;
    for (size_t i = 0u; i < unique_count; i++) {
        uint32_t mark = sorted_marks[i];
        BindingsBuilderTrailEntry state;
        const PrimeOccurrence *prime = NULL;
        if (mark == old_trail_len) {
            state = (BindingsBuilderTrailEntry){
                .len = bb->current.len,
                .eq_len = bb->current.eq_len,
                .cycle_state = bb->current.cycle_state,
                .derived_nonzero =
                    bindings_derived_nonzero(&bb->current),
            };
            if (bindings_prime_present(&bb->current))
                prime = bb->current.prime_ext;
        } else {
            state = bb->trail[mark];
            prime = bindings_builder_checkpoint_prime(bb, &state);
            if (state.prime_state_present && !prime) {
                valid = false;
                break;
            }
        }
        if (state.len > bb->current.len ||
            state.eq_len > bb->current.eq_len) {
            valid = false;
            break;
        }
        uint32_t original_len = state.len;
        uint32_t original_eq_len = state.eq_len;
        state.len = entry_prefix[state.len];
        state.eq_len = constraint_prefix[state.eq_len];
        state.derived_nonzero = 0u;
        if (legacy_prefix[original_len] != 0u) {
            state.derived_nonzero |=
                BINDINGS_DERIVED_LEGACY_NONZERO;
        }
        if (private_entry_prefix[original_len] != 0u) {
            state.derived_nonzero |=
                BINDINGS_DERIVED_PRIVATE_ENTRY_NONZERO;
        }
        if (private_constraint_prefix[original_eq_len] != 0u) {
            state.derived_nonzero |=
                BINDINGS_DERIVED_PRIVATE_CONSTRAINT_NONZERO;
        }
        state.cycle_state =
            state.len == 0u ||
                    state.cycle_state == BINDINGS_CYCLE_ACYCLIC
                ? BINDINGS_CYCLE_ACYCLIC
                : BINDINGS_CYCLE_UNKNOWN;
        state.prime_state_mark = next_prime_len;
        state.prime_state_present = prime != NULL;
        if (prime) {
            if (next_prime_len >= next_prime_cap) {
                valid = false;
                break;
            }
            next_prime_trail[next_prime_len++] = *prime;
        }
        next_trail[i] = state;
    }
    if (valid) {
        for (size_t i = 0u; i < checkpoint_count; i++) {
            size_t mapped = bindings_checkpoint_mark_find(
                sorted_marks, unique_count, checkpoint_marks[i]);
            if (mapped >= unique_count ||
                sorted_marks[mapped] != checkpoint_marks[i] ||
                mapped > UINT32_MAX) {
                valid = false;
                break;
            }
            next_marks[i] = (uint32_t)mapped;
        }
    }
    if (!valid) {
        bindings_free(&projected);
        free(keep_entries);
        free(keep_constraints);
        free(entry_prefix);
        free(legacy_prefix);
        free(private_entry_prefix);
        free(constraint_prefix);
        free(private_constraint_prefix);
        free(sorted_marks);
        free(next_marks);
        free(next_entry_marks);
        free(next_trail);
        free(next_prime_trail);
        return false;
    }

    uint64_t old_logical_items =
        (uint64_t)bb->current.len + bb->current.eq_len;
    uint64_t next_logical_items =
        (uint64_t)projected.len + projected.eq_len;
    bindings_replace(&bb->current, &projected);
    if (bb->rollback_count != UINT64_MAX)
        bb->rollback_count++;
    free(bb->trail);
    free(bb->prime_trail);
    bb->trail = next_trail;
    bb->trail_len = (uint32_t)unique_count;
    bb->trail_cap = (uint32_t)unique_count;
    bb->prime_trail = next_prime_trail;
    bb->prime_trail_len = next_prime_len;
    bb->prime_trail_cap = next_prime_cap;
    if (checkpoint_count > 0u) {
        memcpy(checkpoint_marks, next_marks,
               checkpoint_count * sizeof(*checkpoint_marks));
    }
    if (entry_mark_count > 0u) {
        memcpy(entry_marks, next_entry_marks,
               entry_mark_count * sizeof(*entry_marks));
    }
    if (discarded_logical_items &&
        old_logical_items > next_logical_items) {
        *discarded_logical_items =
            old_logical_items - next_logical_items;
    }
    if (discarded_trail_entries &&
        old_trail_len > unique_count) {
        *discarded_trail_entries =
            old_trail_len - unique_count;
    }

    free(keep_entries);
    free(keep_constraints);
    free(entry_prefix);
    free(legacy_prefix);
    free(private_entry_prefix);
    free(constraint_prefix);
    free(private_constraint_prefix);
    free(sorted_marks);
    free(next_marks);
    free(next_entry_marks);
    return true;
}

bool bindings_builder_compact_reachable_with_entry_marks(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint32_t *entry_marks, size_t entry_mark_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    return bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
        bb, roots, root_count, NULL, 0u,
        checkpoint_marks, checkpoint_count,
        entry_marks, entry_mark_count,
        discarded_logical_items, discarded_trail_entries);
}

bool bindings_builder_compact_reachable_with_epoch_roots(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    const BindingsEpochRoot *epoch_roots,
    size_t epoch_root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    return bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
        bb, roots, root_count, epoch_roots, epoch_root_count,
        checkpoint_marks, checkpoint_count, NULL, 0u,
        discarded_logical_items, discarded_trail_entries);
}
bool bindings_builder_compact_reachable(
    BindingsBuilder *bb, Atom *const *roots, size_t root_count,
    uint32_t *checkpoint_marks, size_t checkpoint_count,
    uint64_t *discarded_logical_items,
    uint64_t *discarded_trail_entries) {
    return bindings_builder_compact_reachable_with_entry_marks(
        bb, roots, root_count,
        checkpoint_marks, checkpoint_count, NULL, 0u,
        discarded_logical_items, discarded_trail_entries);
}

static void rename_var_map_init(RenameVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void rename_var_map_free(RenameVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static Atom *rename_var_map_lookup(RenameVarMap *map, VarId id) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].id == id)
            return map->items[i].mapped;
    }
    return NULL;
}

static Atom *rename_var_map_add_fresh(RenameVarMap *map, Arena *a, Atom *var) {
    uint32_t suffix = fresh_var_suffix();
    Atom *fresh = atom_var_like(a, var, var_epoch_id(var->var_id, suffix));
    if (map->len >= map->cap) {
        uint32_t next_cap = map->cap ? map->cap * 2u : 8u;
        if (next_cap < map->cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*map->items))
            return NULL;
        map->items = cetta_realloc(
            map->items, sizeof(*map->items) * (size_t)next_cap);
        map->cap = next_cap;
    }
    map->items[map->len].id = var->var_id;
    map->items[map->len].mapped = fresh;
    map->len++;
    return fresh;
}

typedef enum {
    RENAME_VARS_VISIT,
    RENAME_VARS_BUILD,
} RenameVarsTaskKind;

typedef struct {
    RenameVarsTaskKind kind;
    Atom *source;
    Atom **destination;
    Atom **children;
} RenameVarsTask;

typedef struct {
    RenameVarsTask inline_tasks[64];
    RenameVarsTask *tasks;
    size_t len;
    size_t cap;
} RenameVarsTaskStack;

static void rename_vars_task_stack_init(RenameVarsTaskStack *stack) {
    stack->tasks = stack->inline_tasks;
    stack->len = 0u;
    stack->cap = sizeof(stack->inline_tasks) / sizeof(stack->inline_tasks[0]);
}

static void rename_vars_task_stack_free(RenameVarsTaskStack *stack) {
    if (stack->tasks != stack->inline_tasks)
        free(stack->tasks);
}

static bool rename_vars_task_stack_push(RenameVarsTaskStack *stack,
                                        RenameVarsTask task) {
    if (stack->len == stack->cap) {
        if (stack->cap > SIZE_MAX / 2u ||
            stack->cap * 2u > SIZE_MAX / sizeof(*stack->tasks))
            return false;
        size_t next_cap = stack->cap * 2u;
        if (stack->tasks == stack->inline_tasks) {
            RenameVarsTask *next = cetta_malloc(
                sizeof(*next) * next_cap);
            memcpy(next, stack->inline_tasks,
                   sizeof(*next) * stack->len);
            stack->tasks = next;
        } else {
            stack->tasks = cetta_realloc(
                stack->tasks, sizeof(*stack->tasks) * next_cap);
        }
        stack->cap = next_cap;
    }
    stack->tasks[stack->len++] = task;
    return true;
}

static Atom *rename_vars_listed_iterative(Arena *a, Atom *root,
                                          const VarIdSet *listed,
                                          RenameVarMap *map,
                                          bool rename_listed) {
    Atom *result = NULL;
    RenameVarsTaskStack stack;
    FreshenEpochMemo memo;
    rename_vars_task_stack_init(&stack);
    freshen_epoch_memo_init(&memo);
    if (!rename_vars_task_stack_push(
            &stack,
            (RenameVarsTask){RENAME_VARS_VISIT, root, &result, NULL}))
        goto fail;
    while (stack.len > 0u) {
        RenameVarsTask task = stack.tasks[--stack.len];
        Atom *atom = task.source;
        if (!atom || !task.destination)
            goto fail;
        if (task.kind == RENAME_VARS_BUILD) {
            bool unchanged = true;
            for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
                if (!task.children[i])
                    goto fail;
                if (task.children[i] != atom->expr.elems[i])
                    unchanged = false;
            }
            Atom *built = unchanged
                ? atom
                : atom_expr(a, task.children, atom->expr.len);
            *task.destination = built;
            if (!freshen_epoch_memo_store(&memo, atom, built))
                goto fail;
            continue;
        }
        if (!atom_has_vars(atom)) {
            *task.destination = atom;
            continue;
        }
        Atom *memoized = freshen_epoch_memo_lookup(&memo, atom);
        if (memoized == &g_rename_walk_active)
            goto fail;
        if (memoized) {
            *task.destination = memoized;
            continue;
        }
        if (atom->kind == ATOM_VAR) {
            Atom *renamed = NULL;
            if (var_id_set_contains(listed, atom->var_id) !=
                rename_listed) {
                renamed = atom;
            } else {
                renamed = rename_var_map_lookup(map, atom->var_id);
                if (!renamed)
                    renamed = rename_var_map_add_fresh(map, a, atom);
            }
            if (!renamed ||
                !freshen_epoch_memo_store(&memo, atom, renamed))
                goto fail;
            *task.destination = renamed;
            continue;
        }
        if (atom->kind != ATOM_EXPR) {
            *task.destination = atom;
            if (!freshen_epoch_memo_store(&memo, atom, atom))
                goto fail;
            continue;
        }
        if (!cetta_expr_len_mul_fits_size(
                atom->expr.len, sizeof(Atom *)) ||
            !freshen_epoch_memo_store(
                &memo, atom, &g_rename_walk_active))
            goto fail;
        Atom **children = atom->expr.len
            ? arena_alloc(a, sizeof(*children) * (size_t)atom->expr.len)
            : NULL;
        if (!rename_vars_task_stack_push(
                &stack,
                (RenameVarsTask){RENAME_VARS_BUILD, atom,
                                 task.destination, children}))
            goto fail;
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--)
            if (!rename_vars_task_stack_push(
                    &stack,
                    (RenameVarsTask){RENAME_VARS_VISIT,
                                     atom->expr.elems[i - 1u],
                                     &children[i - 1u], NULL}))
                goto fail;
    }
    freshen_epoch_memo_free(&memo);
    rename_vars_task_stack_free(&stack);
    return result;

fail:
    freshen_epoch_memo_free(&memo);
    rename_vars_task_stack_free(&stack);
    return NULL;
}

Atom *rename_vars(Arena *a, Atom *atom, uint32_t suffix) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RENAME_VARS);
    switch (atom->kind) {
    case ATOM_VAR: {
        return atom_var_like(a, atom, var_epoch_id(atom->var_id, suffix));
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *next = rename_vars(a, atom->expr.elems[i], suffix);
            if (!new_elems && next != atom->expr.elems[i]) {
                new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems) return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *rename_vars_except(Arena *a, Atom *atom, Atom *ignore_spec) {
    if (!a || !atom || !ignore_spec)
        return NULL;
    VarIdSet ignore;
    RenameVarMap map;
    var_id_set_init(&ignore);
    rename_var_map_init(&map);
    Atom *result = collect_var_ids(ignore_spec, &ignore)
        ? rename_vars_listed_iterative(a, atom, &ignore, &map, false)
        : NULL;
    rename_var_map_free(&map);
    var_id_set_free(&ignore);
    return result;
}

/* Complement of rename_vars_except: freshen ONLY the variables occurring in
 * listed_spec, preserving the identity of every other variable (SWI's
 * copy_term/4 sharing contract).  An empty listed_spec is the identity. */
Atom *rename_vars_only(Arena *a, Atom *atom, Atom *listed_spec) {
    if (!a || !atom || !listed_spec)
        return NULL;
    VarIdSet listed;
    RenameVarMap map;
    var_id_set_init(&listed);
    rename_var_map_init(&map);
    Atom *result = collect_var_ids(listed_spec, &listed)
        ? rename_vars_listed_iterative(a, atom, &listed, &map, true)
        : NULL;
    rename_var_map_free(&map);
    var_id_set_free(&listed);
    return result;
}

/* ── One-way pattern matching ───────────────────────────────────────────── */

bool simple_match(Atom *pattern, Atom *target, Bindings *b) {
    /* Variable in pattern binds to target */
    if (pattern->kind == ATOM_VAR) {
        return bindings_add_var(b, pattern, target);
    }

    /* Same kind required */
    if (pattern->kind != target->kind) return false;

    switch (pattern->kind) {
    case ATOM_SYMBOL:
        return pattern->sym_id == target->sym_id;

    case ATOM_GROUNDED:
        if (pattern->ground.gkind != target->ground.gkind) return false;
        switch (pattern->ground.gkind) {
        case GV_INT:    return pattern->ground.ival == target->ground.ival;
        case GV_FLOAT:  return pattern->ground.fval == target->ground.fval;
        case GV_BOOL:   return pattern->ground.bval == target->ground.bval;
        case GV_STRING: return strcmp(pattern->ground.sval, target->ground.sval) == 0;
        case GV_BIGINT:
            return cetta_bigint_compare_cstr(atom_bigint_cstr(pattern),
                                            atom_bigint_cstr(target)) == 0;
        case GV_RATIONAL:
            return cetta_rational_compare_cstr(atom_rational_cstr(pattern),
                                               atom_rational_cstr(target)) == 0;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return pattern->ground.ptr == target->ground.ptr;
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return atom_eq(pattern, target);
        }
        return false;

    case ATOM_EXPR:
        if (pattern->expr.len != target->expr.len) return false;
        for (CettaExprIndex i = 0; i < pattern->expr.len; i++) {
            if (!simple_match(pattern->expr.elems[i], target->expr.elems[i], b))
                return false;
        }
        return true;

    case ATOM_VAR:
        /* Already handled above */
        return false;
    }
    return false;
}

static bool simple_match_builder_rec(Atom *pattern, Atom *target,
                                     BindingsBuilder *bb) {
    if (pattern->kind == ATOM_VAR)
        return bindings_builder_add_var_fresh(bb, pattern, target);

    if (pattern->kind != target->kind)
        return false;

    switch (pattern->kind) {
    case ATOM_SYMBOL:
        return pattern->sym_id == target->sym_id;

    case ATOM_GROUNDED:
        if (pattern->ground.gkind != target->ground.gkind)
            return false;
        switch (pattern->ground.gkind) {
        case GV_INT:    return pattern->ground.ival == target->ground.ival;
        case GV_FLOAT:  return pattern->ground.fval == target->ground.fval;
        case GV_BOOL:   return pattern->ground.bval == target->ground.bval;
        case GV_STRING: return strcmp(pattern->ground.sval, target->ground.sval) == 0;
        case GV_BIGINT:
            return cetta_bigint_compare_cstr(atom_bigint_cstr(pattern),
                                            atom_bigint_cstr(target)) == 0;
        case GV_RATIONAL:
            return cetta_rational_compare_cstr(atom_rational_cstr(pattern),
                                               atom_rational_cstr(target)) == 0;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return pattern->ground.ptr == target->ground.ptr;
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return atom_eq(pattern, target);
        }
        return false;

    case ATOM_EXPR:
        if (pattern->expr.len != target->expr.len)
            return false;
        for (CettaExprIndex i = 0; i < pattern->expr.len; i++) {
            if (!simple_match_builder_rec(pattern->expr.elems[i],
                                          target->expr.elems[i], bb)) {
                return false;
            }
        }
        return true;

    case ATOM_VAR:
        return false;
    }
    return false;
}

bool simple_match_builder(Atom *pattern, Atom *target, BindingsBuilder *bb) {
    return simple_match_builder_rec(pattern, target, bb);
}

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* ── Loop-binding rejection (occurs check) ─────────────────────────────── */

static int32_t bindings_find_entry_index_for_loop(
        const Bindings *b, const Atom *var) {
    if (!b || !var || var->kind != ATOM_VAR)
        return -1;
    for (uint32_t i = b->len; i > 0; i--) {
        uint32_t idx = i - 1;
        if (binding_var_eq(b->entries[idx].var_id, var->var_id))
            return (int32_t)idx;
    }
    if (b->legacy_fallback_count != 0u) {
        /* Keep the same precedence as bindings_lookup_spelling: legacy
         * serialized environments select the first spelling entry. */
        for (uint32_t i = 0u; i < b->len; i++) {
            if (b->entries[i].legacy_name_fallback &&
                b->entries[i].spelling == var->sym_id)
                return (int32_t)i;
        }
    }
    return -1;
}

typedef enum {
    BINDINGS_LOOP_ATOM = 0,
    BINDINGS_LOOP_ENTRY_EXIT = 1,
} BindingsLoopFrameKind;

typedef struct {
    BindingsLoopFrameKind kind;
    Atom *atom;
    uint32_t entry;
} BindingsLoopFrame;

typedef struct {
    BindingsLoopFrame *items;
    size_t len;
    size_t cap;
    BindingsLoopFrame inline_items[32];
} BindingsLoopStack;

static bool bindings_loop_push(BindingsLoopStack *stack,
                               BindingsLoopFrame frame) {
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap * 2u;
        if (next_cap <= stack->cap ||
            next_cap > SIZE_MAX / sizeof(*stack->items))
            return false;
        BindingsLoopFrame *next = cetta_malloc(
            sizeof(*stack->items) * next_cap);
        memcpy(next, stack->items, sizeof(*stack->items) * stack->len);
        if (stack->items != stack->inline_items) free(stack->items);
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = frame;
    return true;
}

static bool bindings_entry_is_trivial_self(const Bindings *b, uint32_t idx) {
    Atom *value = b->entries[idx].val;
    return value->kind == ATOM_VAR &&
           (value->var_id == b->entries[idx].var_id ||
            (b->entries[idx].legacy_name_fallback &&
             !value->name_key &&
             value->sym_id == b->entries[idx].spelling));
}

bool bindings_has_loop(const Bindings *b) {
    if (!b || b->len == 0)
        return false;
    if (b->cycle_state == BINDINGS_CYCLE_ACYCLIC &&
        b->legacy_fallback_count == 0u)
        return false;
    if (b->cycle_state == BINDINGS_CYCLE_PRESENT)
        return true;
    uint8_t state_stack[BINDINGS_SEEN_STACK_CAP];
    uint8_t *state = b->len <= BINDINGS_SEEN_STACK_CAP
        ? state_stack
        : cetta_malloc(sizeof(uint8_t) * b->len);
    memset(state, 0, sizeof(uint8_t) * b->len);
    BindingsLoopStack stack;
    stack.items = stack.inline_items;
    stack.len = 0;
    stack.cap = sizeof stack.inline_items / sizeof stack.inline_items[0];
    for (uint32_t i = 0; i < b->len; i++) {
        if (state[i] != 0) continue;
        if (bindings_entry_is_trivial_self(b, i)) {
            state[i] = 2;
            continue;
        }
        state[i] = 1;
        if (!bindings_loop_push(
                &stack, (BindingsLoopFrame){BINDINGS_LOOP_ENTRY_EXIT,
                                             NULL, i}) ||
            !bindings_loop_push(
                &stack, (BindingsLoopFrame){BINDINGS_LOOP_ATOM,
                                             b->entries[i].val, 0}))
            goto representation_failure;

        while (stack.len > 0) {
            BindingsLoopFrame frame = stack.items[--stack.len];
            if (frame.kind == BINDINGS_LOOP_ENTRY_EXIT) {
                state[frame.entry] = 2;
                continue;
            }
            Atom *atom = frame.atom;
            if (!atom_has_vars(atom)) continue;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_NODE_VISIT);
            if (atom->kind == ATOM_VAR) {
                int32_t found = bindings_find_entry_index_for_loop(b, atom);
                if (found < 0) continue;
                uint32_t entry = (uint32_t)found;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_LOOP_CHECK);
                if (state[entry] == 1) goto loop_found;
                if (state[entry] == 2) continue;
                if (bindings_entry_is_trivial_self(b, entry)) {
                    state[entry] = 2;
                    continue;
                }
                state[entry] = 1;
                if (!bindings_loop_push(
                        &stack,
                        (BindingsLoopFrame){BINDINGS_LOOP_ENTRY_EXIT,
                                             NULL, entry}) ||
                    !bindings_loop_push(
                        &stack,
                        (BindingsLoopFrame){BINDINGS_LOOP_ATOM,
                                             b->entries[entry].val, 0}))
                    goto representation_failure;
                continue;
            }
            if (atom->kind == ATOM_EXPR) {
                for (CettaExprIndex child = atom->expr.len; child > 0; child--)
                    if (!bindings_loop_push(
                            &stack,
                            (BindingsLoopFrame){
                                BINDINGS_LOOP_ATOM,
                                atom->expr.elems[child - 1u], 0}))
                        goto representation_failure;
            }
        }
    }
    if (stack.items != stack.inline_items) free(stack.items);
    if (state != state_stack)
        free(state);
    return false;

loop_found:
representation_failure:
    if (stack.items != stack.inline_items) free(stack.items);
    if (state != state_stack) free(state);
    return true;
}

/* ── Type matching ─────────────────────────────────────────────────────── */

static bool is_named_symbol(Atom *atom, const char *name) {
    return atom_is_symbol(atom, name);
}

static bool is_space_value_type(Atom *atom) {
    return atom &&
           atom->kind == ATOM_EXPR &&
           atom->expr.len == 2 &&
           is_named_symbol(atom->expr.elems[0], "Space");
}

bool match_types_space_kind_equivalent(Atom *actual, Atom *expected) {
    return actual && expected &&
        ((is_named_symbol(actual, "SpaceType") &&
          is_space_value_type(expected)) ||
         (is_named_symbol(expected, "SpaceType") &&
          is_space_value_type(actual)));
}

typedef struct {
    Atom *left;
    Atom *right;
    uint8_t tagged;
    bool active;
} MatchPathSlot;

#define MATCH_PATH_INLINE_CAP 16u

typedef struct {
    MatchPathSlot inline_slots[MATCH_PATH_INLINE_CAP];
    MatchPathSlot *slots;
    size_t cap;
    size_t active;
    size_t tombstones;
} MatchPathSet;

static void match_path_clear(MatchPathSlot *slots, size_t cap) {
    memset(slots, 0, sizeof(*slots) * cap);
}

static void match_path_init(MatchPathSet *path) {
    path->slots = path->inline_slots;
    path->cap = MATCH_PATH_INLINE_CAP;
    path->active = 0;
    path->tombstones = 0;
    match_path_clear(path->slots, path->cap);
}

static void match_path_free(MatchPathSet *path) {
    if (path->slots != path->inline_slots) free(path->slots);
}

static size_t match_path_hash(Atom *left, Atom *right, uint8_t tagged) {
    uintptr_t x = (uintptr_t)left >> 4;
    uintptr_t y = (uintptr_t)right >> 4;
    x ^= y + (uintptr_t)0x9e3779b9u + (x << 6) + (x >> 2);
    x ^= tagged ? (uintptr_t)0x85ebca6bu : 0u;
    x ^= x >> 17;
    x *= (uintptr_t)0xed5ad4bbu;
    x ^= x >> 11;
    return (size_t)x;
}

static bool match_path_rehash(MatchPathSet *path, size_t new_cap) {
    if (new_cap < MATCH_PATH_INLINE_CAP ||
        new_cap > SIZE_MAX / sizeof(*path->slots))
        return false;
    size_t old_cap = path->cap;
    MatchPathSlot *old_slots = path->slots;
    MatchPathSlot *new_slots = cetta_malloc(
        sizeof(*new_slots) * new_cap);
    match_path_clear(new_slots, new_cap);
    path->slots = new_slots;
    path->cap = new_cap;
    size_t old_active = path->active;
    path->active = 0;
    path->tombstones = 0;
    for (size_t i = 0; i < old_cap; i++) {
        MatchPathSlot *old = &old_slots[i];
        if (!old->left || !old->active) continue;
        size_t mask = path->cap - 1u;
        size_t pos = match_path_hash(
            old->left, old->right, old->tagged) & mask;
        while (path->slots[pos].left)
            pos = (pos + 1u) & mask;
        MatchPathSlot *next = &path->slots[pos];
        *next = *old;
        path->active++;
    }
    assert(path->active == old_active);
    if (old_slots != path->inline_slots) free(old_slots);
    return true;
}

/* Return false exactly when this structural comparison recurs on its active
   DFS path. Inactive entries remain as reusable hash slots, so shared finite
   subterms do not count as cycles. */
static bool match_path_enter(MatchPathSet *path, Atom *left, Atom *right,
                             uint8_t tagged) {
    size_t occupied = path->active + path->tombstones;
    if ((occupied + 1u) * 4u >= path->cap * 3u) {
        size_t live_after = path->active + 1u;
        size_t new_cap = path->cap;
        if (live_after * 4u >= path->cap * 3u) {
            if (path->cap > SIZE_MAX / 2u) return false;
            new_cap = path->cap * 2u;
        }
        if (!match_path_rehash(path, new_cap)) return false;
    }

    size_t mask = path->cap - 1u;
    size_t pos = match_path_hash(left, right, tagged) & mask;
    MatchPathSlot *first_tombstone = NULL;
    MatchPathSlot *slot = NULL;
    for (;;) {
        MatchPathSlot *candidate = &path->slots[pos];
        if (!candidate->left) {
            slot = first_tombstone ? first_tombstone : candidate;
            break;
        }
        if (candidate->left == left && candidate->right == right &&
            candidate->tagged == tagged) {
            if (candidate->active) return false;
            slot = candidate;
            break;
        }
        if (!candidate->active && !first_tombstone)
            first_tombstone = candidate;
        pos = (pos + 1u) & mask;
    }
    if (slot->left) path->tombstones--;
    slot->left = left;
    slot->right = right;
    slot->tagged = tagged;
    slot->active = true;
    path->active++;
    return true;
}

static void match_path_leave(MatchPathSet *path, Atom *left, Atom *right,
                             uint8_t tagged) {
    size_t mask = path->cap - 1u;
    size_t pos = match_path_hash(left, right, tagged) & mask;
    for (;;) {
        MatchPathSlot *slot = &path->slots[pos];
        assert(slot->left);
        if (slot->active && slot->left == left && slot->right == right &&
            slot->tagged == tagged) {
            slot->active = false;
            path->active--;
            path->tombstones++;
            return;
        }
        pos = (pos + 1u) & mask;
    }
}

typedef enum {
    DECODED_MATCH_PAIR = 0,
    DECODED_MATCH_EXIT = 1,
} DecodedMatchFrameKind;

typedef struct {
    DecodedMatchFrameKind kind;
    Atom *left;
    Atom *right;
} DecodedMatchPair;

typedef struct {
    DecodedMatchPair *items;
    size_t len;
    size_t cap;
    DecodedMatchPair inline_items[16];
} DecodedMatchWorklist;

static void decoded_match_worklist_init(DecodedMatchWorklist *work) {
    work->items = work->inline_items;
    work->len = 0;
    work->cap = sizeof work->inline_items / sizeof work->inline_items[0];
}

static void decoded_match_worklist_free(DecodedMatchWorklist *work) {
    if (work->items != work->inline_items) free(work->items);
}

static bool decoded_match_push(DecodedMatchWorklist *work,
                               Atom *left, Atom *right) {
    if (work->len == work->cap) {
        size_t next_cap = work->cap * 2u;
        if (next_cap <= work->cap ||
            next_cap > SIZE_MAX / sizeof(*work->items))
            return false;
        DecodedMatchPair *next = cetta_malloc(
            sizeof(*work->items) * next_cap);
        memcpy(next, work->items, sizeof(*work->items) * work->len);
        if (work->items != work->inline_items) free(work->items);
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len++] =
        (DecodedMatchPair){DECODED_MATCH_PAIR, left, right};
    return true;
}

static bool decoded_match_push_exit(DecodedMatchWorklist *work,
                                    Atom *left, Atom *right) {
    if (!decoded_match_push(work, left, right)) return false;
    work->items[work->len - 1u].kind = DECODED_MATCH_EXIT;
    return true;
}

static size_t bindings_dereference_limit(const Bindings *bindings) {
    size_t len = bindings ? (size_t)bindings->len : 0;
    return len > (SIZE_MAX - 2u) / 2u ? SIZE_MAX : len * 2u + 2u;
}

/* Upstream HE treats each nested %Undefined% as an independent wildcard.
   Type matching itself is a finite structural walk, so nesting depth is not a
   semantic budget. The optional builder selects transactional binding without
   changing the relation. */
static bool match_decoded_atoms_worklist(Atom *left, Atom *right,
                                         Bindings *bindings,
                                         BindingsBuilder *builder,
                                         bool undefined_is_wildcard) {
    DecodedMatchWorklist work;
    MatchPathSet path;
    decoded_match_worklist_init(&work);
    match_path_init(&path);
    if (!decoded_match_push(&work, left, right)) goto fail;

    while (work.len > 0) {
        DecodedMatchPair pair = work.items[--work.len];
        if (pair.kind == DECODED_MATCH_EXIT) {
            match_path_leave(&path, pair.left, pair.right, false);
            continue;
        }
        left = pair.left;
        right = pair.right;
        Bindings *current = builder ? &builder->current : bindings;
        /* A variable-only dereference chain cannot be longer than the binding
           table unless it contains a cycle. This is cycle detection derived
           from the graph in hand, not an arbitrary depth cutoff. */
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(current);

retry_pair:
        if (undefined_is_wildcard &&
            (atom_is_symbol_id(left, g_builtin_syms.undefined_type) ||
             atom_is_symbol_id(right, g_builtin_syms.undefined_type)))
            continue;
        if (left->kind == ATOM_VAR) {
            Atom *existing = bindings_lookup_var(current, left);
            if (existing) {
                if (++dereferences > dereference_limit) {
                    goto fail;
                }
                left = existing;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                Atom *right_existing = bindings_lookup_var(current, right);
                if (right_existing) {
                    if (++dereferences > dereference_limit) {
                        goto fail;
                    }
                    right = right_existing;
                    goto retry_pair;
                }
                if (left->var_id == right->var_id) continue;
            }
            bool added = builder
                ? bindings_builder_add_var_fresh(builder, left, right)
                : bindings_add_var(bindings, left, right);
            if (!added) {
                goto fail;
            }
            continue;
        }
        if (right->kind == ATOM_VAR) {
            Atom *existing = bindings_lookup_var(current, right);
            if (existing) {
                if (++dereferences > dereference_limit) {
                    goto fail;
                }
                right = existing;
                goto retry_pair;
            }
            bool added = builder
                ? bindings_builder_add_var_fresh(builder, right, left)
                : bindings_add_var(bindings, right, left);
            if (!added) {
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (left->sym_id != right->sym_id) {
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq(left, right)) {
                goto fail;
            }
            continue;
        }
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len) {
            goto fail;
        }
        if (!match_path_enter(&path, left, right, false) ||
            !decoded_match_push_exit(&work, left, right))
            goto fail;
        /* Push in reverse so binding effects retain the recursive
           implementation's left-to-right traversal order. */
        for (CettaExprIndex i = left->expr.len; i > 0; i--) {
            CettaExprIndex child = i - 1u;
            if (!decoded_match_push(&work, left->expr.elems[child],
                                    right->expr.elems[child])) {
                goto fail;
            }
        }
    }
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    return true;

fail:
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    return false;
}

bool match_types(Atom *actual, Atom *expected, Bindings *b) {
    /* Atom is the expected-side value top. An actual Atom is not evidence for
       an arbitrary concrete expected type. */
    if (atom_is_symbol_id(expected, g_builtin_syms.atom)) return true;
    if (match_types_space_kind_equivalent(actual, expected)) {
        return true;
    }
    return match_decoded_atoms_worklist(actual, expected, b, NULL, true);
}

bool match_types_builder(Atom *actual, Atom *expected, BindingsBuilder *bb) {
    if (atom_is_symbol_id(expected, g_builtin_syms.atom)) return true;
    if (match_types_space_kind_equivalent(actual, expected)) {
        return true;
    }
    return match_decoded_atoms_worklist(actual, expected, NULL, bb, true);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

static bool match_atoms_epoch_worklist(Atom *left, Atom *right,
                                       Bindings *bindings,
                                       BindingsBuilder *builder,
                                       Arena *a, uint32_t epoch);
static bool match_atoms_epoch_rule_local_worklist(
    Atom *left, Atom *right, BindingsBuilder *builder,
    Arena *a, uint32_t epoch);
static bool match_atoms_epoch_view_worklist(
    Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch);
static bool match_atoms_dense_epoch_view_worklist(
    Atom *left, const BindingsDenseEpochFrame *left_frame,
    Atom *right, Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, bool right_original,
    bool prefer_right_rule_slot);
static bool match_atoms_atom_id_epoch_worklist(
    Atom *left, const TermUniverse *candidate_universe, AtomId right_id,
    Bindings *b, Arena *a, uint32_t epoch);

bool match_atoms(Atom *left, Atom *right, Bindings *b) {
    return match_decoded_atoms_worklist(left, right, b, NULL, false);
}

bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb) {
    return match_decoded_atoms_worklist(left, right, NULL, bb, false);
}

/* Leaf-patch view: OFF by default (env CETTA_LEAF_PATCH_VIEW=1 opts in).  When
 * on, an eligible reduction uses the positional bind below instead of the
 * general matcher, so an OFF-vs-ON differential can prove byte-identity across
 * the full suite before any default flip. */
bool match_leaf_patch_view_enabled(void) {
    static _Thread_local int cached = -1;
    if (cached < 0) {
        const char *v = getenv("CETTA_LEAF_PATCH_VIEW");
        cached = (v && v[0] == '1') ? 1 : 0;
    }
    return cached == 1;
}

/* Positional leaf-patch match: a FLAT LINEAR pattern (lhs = head + distinct
 * variable args, guaranteed linear by the eligibility guard) against a query
 * (head + NON-variable args) reduces to the epoch matcher's right-var binding
 * per position -- epoch_var_atom + bindings_add_var -- with no worklist.  This
 * is licensed by LeafPatchViewKernel.matchP_complete_linear (positional read ==
 * matcher on linear patterns).  Conservative: the whole shape is pre-checked
 * before any binding, and the bindings are built transactionally, so returning
 * false (caller falls back to the general matcher) never leaves a partial
 * binding in b.  Anything outside the shape (nesting, a non-var pattern arg, a
 * variable query arg, or a pre-bound epoched var) falls back. */
bool match_atoms_epoch_positional_linear(Atom *query, Atom *lhs, Bindings *b,
                                         Arena *a, uint32_t epoch) {
    if (!query || !lhs || !b || !a)
        return false;
    if (query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR)
        return false;
    if (lhs->expr.len == 0 || query->expr.len != lhs->expr.len)
        return false;
    Atom *lh = lhs->expr.elems[0];
    Atom *qh = query->expr.elems[0];
    if (!lh || !qh || lh->kind != ATOM_SYMBOL || qh->kind != ATOM_SYMBOL ||
        lh->sym_id != qh->sym_id)
        return false;
    /* Pre-check the whole shape AND linearity before binding anything, so a
     * refusal never leaves a partial binding.  A repeated epoched var here means
     * the pattern is non-linear (an equality constraint the positional bind
     * cannot honour) -- refuse and let the general matcher enforce it. */
    VarId seen[16];
    uint32_t nseen = 0;
    for (CettaExprIndex i = 1; i < lhs->expr.len; i++) {
        Atom *pi = lhs->expr.elems[i];
        Atom *qi = query->expr.elems[i];
        if (!pi || !qi || pi->kind != ATOM_VAR || qi->kind == ATOM_VAR)
            return false;
        VarId eid = var_epoch_id(pi->var_id, epoch);
        if (bindings_lookup_id(b, eid))
            return false; /* epoched var already bound -> matcher dereferences */
        for (uint32_t j = 0; j < nseen; j++)
            if (seen[j] == eid)
                return false; /* repeated variable -> non-linear -> refuse */
        if (nseen >= (sizeof seen / sizeof seen[0]))
            return false; /* arity beyond the small cap -> conservative refuse */
        seen[nseen++] = eid;
    }
    Bindings trial;
    if (!bindings_clone(&trial, b))
        return false;
    for (CettaExprIndex i = 1; i < lhs->expr.len; i++) {
        Atom *pi = lhs->expr.elems[i];
        Atom *qi = query->expr.elems[i];
        Atom *binding_var = epoch_var_atom(a, pi, epoch);
        if (!binding_var || !bindings_add_var(&trial, binding_var, qi)) {
            bindings_free(&trial);
            return false;
        }
    }
    bindings_replace(b, &trial);
    return true;
}

bool match_atoms_epoch_positional_linear_builder(
        Atom *query, Atom *lhs, BindingsBuilder *bb,
        Arena *a, uint32_t epoch) {
    if (!query || !lhs || !bb || !a)
        return false;
    if (query->kind != ATOM_EXPR || lhs->kind != ATOM_EXPR)
        return false;
    if (lhs->expr.len == 0u || query->expr.len != lhs->expr.len)
        return false;
    Atom *lh = lhs->expr.elems[0];
    Atom *qh = query->expr.elems[0];
    if (!lh || !qh || lh->kind != ATOM_SYMBOL || qh->kind != ATOM_SYMBOL ||
        lh->sym_id != qh->sym_id)
        return false;

    /* This realization is deliberately narrower than head-linearity alone.
     * It is the flat, ground-argument leaf view proved equivalent to matching;
     * the generated flow fact selects candidates, while these checks keep the
     * runtime generic and fail closed outside the theorem's domain. */
    VarId seen[16];
    uint32_t nseen = 0u;
    Bindings *current = &bb->current;
    if (current->eq_len != 0u)
        return false;
    for (CettaExprIndex i = 1u; i < lhs->expr.len; i++) {
        Atom *pi = lhs->expr.elems[i];
        Atom *qi = query->expr.elems[i];
        if (!pi || !qi || pi->kind != ATOM_VAR || atom_has_vars(qi))
            return false;
        VarId eid = var_epoch_id(pi->var_id, epoch);
        if (bindings_lookup_id(current, eid))
            return false;
        for (uint32_t j = 0u; j < nseen; j++)
            if (seen[j] == eid)
                return false;
        if (nseen >= (sizeof seen / sizeof seen[0]))
            return false;
        seen[nseen++] = eid;
    }

    for (CettaExprIndex i = 1u; i < lhs->expr.len; i++) {
        Atom *binding_var = epoch_var_atom(a, lhs->expr.elems[i], epoch);
        if (!binding_var || !bindings_builder_add_var_fresh(
                bb, binding_var, query->expr.elems[i]))
            return false;
    }
    return true;
}

bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_worklist(left, right, b, NULL, a, epoch);
}

bool match_atoms_epoch_builder(Atom *left, Atom *right,
                               BindingsBuilder *bb, Arena *a,
                               uint32_t epoch) {
    return match_atoms_epoch_worklist(left, right, NULL, bb, a, epoch);
}

bool match_atoms_epoch_builder_rule_local(
        Atom *left, Atom *right, BindingsBuilder *bb,
        Arena *a, uint32_t epoch) {
    return match_atoms_epoch_rule_local_worklist(
        left, right, bb, a, epoch);
}

bool match_atoms_epoch_view_builder(
        Atom *left_original, uint32_t left_epoch,
        uint32_t left_first_entry, Atom *right_original,
        BindingsBuilder *bb, Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_view_worklist(
        left_original, left_epoch, left_first_entry,
        right_original, NULL, bb, a, right_epoch);
}

bool match_atoms_dense_epoch_view_builder(
        Atom *left_original, const BindingsDenseEpochFrame *left_frame,
        Atom *right_original, BindingsBuilder *bb, Arena *a,
        uint32_t right_epoch) {
    return match_atoms_dense_epoch_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, false);
}

bool match_atoms_dense_epoch_view_builder_current(
        Atom *left_original, const BindingsDenseEpochFrame *left_frame,
        Atom *right, BindingsBuilder *bb, Arena *a) {
    return match_atoms_dense_epoch_view_worklist(
        left_original, left_frame, right,
        NULL, bb, a, 0u, false, false);
}

bool match_atoms_dense_epoch_view_builder_rule_local(
        Atom *left_original, const BindingsDenseEpochFrame *left_frame,
        Atom *right_original, BindingsBuilder *bb, Arena *a,
        uint32_t right_epoch) {
    return match_atoms_dense_epoch_view_worklist(
        left_original, left_frame, right_original,
        NULL, bb, a, right_epoch, true, true);
}

bool match_atoms_atom_id_epoch(Atom *left, const TermUniverse *candidate_universe,
                               AtomId right_id, Bindings *b, Arena *a,
                               uint32_t epoch) {
    return match_atoms_atom_id_epoch_worklist(
        left, candidate_universe, right_id, b, a, epoch);
}

typedef struct {
    VarId left;
    VarId right;
} AlphaPair;

typedef struct {
    AlphaPair *items;
    uint32_t len;
    uint32_t cap;
} AlphaPairSet;

static void alpha_pair_set_init(AlphaPairSet *pairs) {
    pairs->items = NULL;
    pairs->len = 0;
    pairs->cap = 0;
}

static void alpha_pair_set_free(AlphaPairSet *pairs) {
    free(pairs->items);
    pairs->items = NULL;
    pairs->len = 0;
    pairs->cap = 0;
}

static VarId alpha_lookup_left(const AlphaPairSet *pairs, VarId left) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        if (pairs->items[i].left == left)
            return pairs->items[i].right;
    }
    return VAR_ID_NONE;
}

static VarId alpha_lookup_right(const AlphaPairSet *pairs, VarId right) {
    for (uint32_t i = 0; i < pairs->len; i++) {
        if (pairs->items[i].right == right)
            return pairs->items[i].left;
    }
    return VAR_ID_NONE;
}

static bool alpha_add_pair(AlphaPairSet *pairs, VarId left, VarId right) {
    if (pairs->len >= pairs->cap) {
        pairs->cap = pairs->cap ? pairs->cap * 2 : 8;
        pairs->items = cetta_realloc(pairs->items, sizeof(AlphaPair) * pairs->cap);
    }
    pairs->items[pairs->len].left = left;
    pairs->items[pairs->len].right = right;
    pairs->len++;
    return true;
}

static bool atom_alpha_eq_rec(Atom *left, Atom *right, AlphaPairSet *pairs) {
    if (left->kind == ATOM_VAR || right->kind == ATOM_VAR) {
        if (left->kind != ATOM_VAR || right->kind != ATOM_VAR)
            return false;
        VarId mapped_right = alpha_lookup_left(pairs, left->var_id);
        VarId mapped_left = alpha_lookup_right(pairs, right->var_id);
        if (mapped_right || mapped_left) {
            return mapped_right == right->var_id && mapped_left == left->var_id;
        }
        return alpha_add_pair(pairs, left->var_id, right->var_id);
    }

    if (left->kind != right->kind)
        return false;

    switch (left->kind) {
    case ATOM_SYMBOL:
        return left->sym_id == right->sym_id;
    case ATOM_GROUNDED:
        return atom_eq(left, right);
    case ATOM_EXPR:
        if (left->expr.len != right->expr.len)
            return false;
        for (CettaExprIndex i = 0; i < left->expr.len; i++) {
            if (!atom_alpha_eq_rec(left->expr.elems[i], right->expr.elems[i], pairs))
                return false;
        }
        return true;
    case ATOM_VAR:
        return false;
    }
    return false;
}

bool atom_alpha_eq(Atom *left, Atom *right) {
    AlphaPairSet pairs;
    alpha_pair_set_init(&pairs);
    bool ok = atom_alpha_eq_rec(left, right, &pairs);
    alpha_pair_set_free(&pairs);
    return ok;
}

typedef struct {
    bool exit;
    Atom *left;
    Atom *right;
    bool left_original;
    bool right_original;
} EpochMatchPair;

typedef struct {
    EpochMatchPair *items;
    size_t len;
    size_t cap;
    EpochMatchPair inline_items[16];
} EpochMatchWorklist;

static bool epoch_match_push(EpochMatchWorklist *work, Atom *left,
                             bool left_original, Atom *right,
                             bool right_original) {
    if (work->len == work->cap) {
        size_t next_cap = work->cap * 2u;
        if (next_cap <= work->cap ||
            next_cap > SIZE_MAX / sizeof(*work->items))
            return false;
        EpochMatchPair *next = cetta_malloc(
            sizeof(*work->items) * next_cap);
        memcpy(next, work->items, sizeof(*work->items) * work->len);
        if (work->items != work->inline_items) free(work->items);
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len++] =
        (EpochMatchPair){
            false, left, right, left_original, right_original};
    return true;
}

static bool epoch_match_push_exit(EpochMatchWorklist *work, Atom *left,
                                  bool left_original, Atom *right,
                                  bool right_original) {
    if (!epoch_match_push(
            work, left, left_original, right, right_original))
        return false;
    work->items[work->len - 1u].exit = true;
    return true;
}

static bool match_atoms_epoch_views_worklist(
    Atom *left, bool left_original, uint32_t left_epoch,
    uint32_t left_first_entry, Atom *right,
    Bindings *bindings, BindingsBuilder *builder,
    Arena *a, uint32_t right_epoch, bool right_original,
    bool prefer_right_rule_slot,
    const BindingsDenseEpochFrame *left_frame) {
    EpochMatchWorklist work;
    MatchPathSet path;
    Bindings *initial = builder ? &builder->current : bindings;

    if (!left || !right || !initial || !a ||
        (left_original && left_first_entry > initial->len))
        return false;
    work.items = work.inline_items;
    work.len = 0;
    work.cap = sizeof work.inline_items / sizeof work.inline_items[0];
    match_path_init(&path);
    if (!epoch_match_push(
            &work, left, left_original, right, right_original))
        goto fail;

    while (work.len > 0) {
        EpochMatchPair pair = work.items[--work.len];
        uint8_t path_tag =
            (pair.left_original ? UINT8_C(2) : UINT8_C(0)) |
            (pair.right_original ? UINT8_C(1) : UINT8_C(0));
        if (pair.exit) {
            match_path_leave(&path, pair.left, pair.right,
                             path_tag);
            continue;
        }
        left = pair.left;
        right = pair.right;
        left_original = pair.left_original;
        bool right_original = pair.right_original;
        Bindings *current = builder ? &builder->current : bindings;
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(current);

retry_pair:
        if (left->kind == ATOM_VAR) {
            if (left_original) {
                VarId left_id = var_epoch_id(
                    left->var_id, left_epoch);
                Atom *existing = NULL;
                bool dense_present = false;
                bool dense_known = left_frame &&
                    bindings_dense_epoch_frame_lookup(
                        left_frame, left->var_id,
                        &existing, &dense_present);
                if (!dense_known) {
                    existing = bindings_lookup_id_since(
                        current, left_id, left_first_entry);
                } else if (!dense_present) {
                    existing = NULL;
                }

                if (existing) {
                    if (++dereferences > dereference_limit)
                        goto fail;
                    left = existing;
                } else {
                    left = epoch_var_atom(a, left, left_epoch);
                    if (!left)
                        goto fail;
                }
                left_original = false;
                goto retry_pair;
            }
            Atom *existing = bindings_lookup_var(current, left);
            if (existing) {
                if (++dereferences > dereference_limit) goto fail;
                left = existing;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                VarId right_id = right_original
                    ? var_epoch_id(right->var_id, right_epoch)
                    : right->var_id;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
                Atom *right_existing = bindings_lookup_id(current, right_id);
                if (right_existing) {
                    if (++dereferences > dereference_limit) goto fail;
                    right = right_existing;
                    right_original = false;
                    goto retry_pair;
                }
                if (left->var_id == right_id) continue;
                if (prefer_right_rule_slot && right_original) {
                    Atom *binding_var =
                        epoch_var_atom(a, right, right_epoch);
                    bool added = binding_var && (builder
                        ? bindings_builder_add_var_fresh(
                              builder, binding_var, left)
                        : bindings_add_var(
                              bindings, binding_var, left));
                    if (!added) goto fail;
                    continue;
                }
                Atom *value = right_original
                    ? epoch_var_atom(a, right, right_epoch) : right;
                bool added = value && (builder
                    ? bindings_builder_add_var_fresh(builder, left, value)
                    : bindings_add_var(bindings, left, value));
                if (!added) goto fail;
                continue;
            }
            Atom *value = right_original
                ? bindings_apply_epoch(
                    current, a, right, right_epoch) : right;
            bool added = value && (builder
                ? bindings_builder_add_var_fresh(builder, left, value)
                : bindings_add_var(bindings, left, value));
            if (!added) goto fail;
            continue;
        }
        if (right->kind == ATOM_VAR) {
            VarId right_id = right_original
                ? var_epoch_id(right->var_id, right_epoch)
                : right->var_id;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
            Atom *existing = bindings_lookup_id(current, right_id);
            if (existing) {
                if (++dereferences > dereference_limit) goto fail;
                right = existing;
                right_original = false;
                goto retry_pair;
            }
            Atom *binding_var = right_original
                ? epoch_var_atom(a, right, right_epoch) : right;
            Atom *binding_value = left_original
                ? bindings_apply_epoch_view(
                    current, a, left, left_epoch, left_first_entry,
                    true, left_frame)
                : left;
            bool added = binding_var && binding_value && (builder
                ? (prefer_right_rule_slot && right_original
                    ? bindings_builder_add_var_fresh(
                          builder, binding_var, binding_value)
                    : bindings_builder_add_var_fresh(
                          builder, binding_var, binding_value))
                : bindings_add_var(
                    bindings, binding_var, binding_value));
            if (!added) goto fail;
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (left->sym_id != right->sym_id) goto fail;
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq(left, right)) goto fail;
            continue;
        }
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len)
            goto fail;
        path_tag =
            (left_original ? UINT8_C(2) : UINT8_C(0)) |
            (right_original ? UINT8_C(1) : UINT8_C(0));
        if (!match_path_enter(&path, left, right, path_tag) ||
            !epoch_match_push_exit(
                &work, left, left_original, right, right_original))
            goto fail;
        for (CettaExprIndex i = left->expr.len; i > 0; i--) {
            CettaExprIndex child = i - 1u;
            if (!epoch_match_push(
                    &work, left->expr.elems[child], left_original,
                    right->expr.elems[child], right_original))
                goto fail;
        }
    }
    if (work.items != work.inline_items) free(work.items);
    match_path_free(&path);
    return true;

fail:
    if (work.items != work.inline_items) free(work.items);
    match_path_free(&path);
    return false;
}

static bool match_atoms_epoch_worklist(Atom *left, Atom *right,
                                       Bindings *bindings,
                                       BindingsBuilder *builder,
                                       Arena *a, uint32_t epoch) {
    return match_atoms_epoch_views_worklist(
        left, false, 0u, 0u, right, bindings, builder, a, epoch,
        true, false, NULL);
}

static bool match_atoms_epoch_view_worklist(
        Atom *left, uint32_t left_epoch, uint32_t left_first_entry,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
        Arena *a, uint32_t right_epoch) {
    return match_atoms_epoch_views_worklist(
        left, true, left_epoch, left_first_entry,
        right, bindings, builder, a, right_epoch,
        true, false, NULL);
}

static bool match_atoms_dense_epoch_view_worklist(
        Atom *left, const BindingsDenseEpochFrame *left_frame,
        Atom *right, Bindings *bindings, BindingsBuilder *builder,
        Arena *a, uint32_t right_epoch, bool right_original,
        bool prefer_right_rule_slot) {
    if (!bindings_dense_epoch_frame_is_current(
            left_frame, builder))
        return false;
    return match_atoms_epoch_views_worklist(
        left, true, left_frame->epoch, left_frame->first_entry,
        right, bindings, builder, a, right_epoch,
        right_original, prefer_right_rule_slot, left_frame);
}

static bool match_atoms_epoch_rule_local_worklist(
        Atom *left, Atom *right, BindingsBuilder *builder,
        Arena *a, uint32_t epoch) {
    return match_atoms_epoch_views_worklist(
        left, false, 0u, 0u, right, NULL, builder, a, epoch,
        true, true, NULL);
}

typedef struct {
    Atom *left;
    AtomId right_id;
} StoredMatchPair;

typedef struct {
    StoredMatchPair *items;
    size_t len;
    size_t cap;
    StoredMatchPair inline_items[16];
} StoredMatchWorklist;

static bool stored_match_push(StoredMatchWorklist *work, Atom *left,
                              AtomId right_id) {
    if (work->len == work->cap) {
        size_t next_cap = work->cap * 2u;
        if (next_cap <= work->cap ||
            next_cap > SIZE_MAX / sizeof(*work->items))
            return false;
        StoredMatchPair *next = cetta_malloc(
            sizeof(*work->items) * next_cap);
        memcpy(next, work->items, sizeof(*work->items) * work->len);
        if (work->items != work->inline_items) free(work->items);
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len++] = (StoredMatchPair){left, right_id};
    return true;
}

static bool stored_grounded_equal(Atom *left,
                                  const TermUniverse *candidate_universe,
                                  AtomId right_id) {
    if (tu_kind(candidate_universe, right_id) != ATOM_GROUNDED)
        return false;
    switch (left->ground.gkind) {
    case GV_INT:
        return tu_ground_kind(candidate_universe, right_id) == GV_INT &&
               left->ground.ival == tu_int(candidate_universe, right_id);
    case GV_FLOAT:
        return tu_ground_kind(candidate_universe, right_id) == GV_FLOAT &&
               left->ground.fval == tu_float(candidate_universe, right_id);
    case GV_BOOL:
        return tu_ground_kind(candidate_universe, right_id) == GV_BOOL &&
               left->ground.bval == tu_bool(candidate_universe, right_id);
    case GV_STRING: {
        const char *rhs = tu_string_cstr(candidate_universe, right_id);
        return tu_ground_kind(candidate_universe, right_id) == GV_STRING &&
               rhs && strcmp(left->ground.sval, rhs) == 0;
    }
    case GV_BIGINT: {
        const char *rhs = tu_bigint_cstr(candidate_universe, right_id);
        return tu_ground_kind(candidate_universe, right_id) == GV_BIGINT &&
               rhs &&
               cetta_bigint_compare_cstr(atom_bigint_cstr(left), rhs) == 0;
    }
    case GV_RATIONAL: {
        const char *rhs = tu_rational_cstr(candidate_universe, right_id);
        return tu_ground_kind(candidate_universe, right_id) == GV_RATIONAL &&
               rhs &&
               cetta_rational_compare_cstr(atom_rational_cstr(left), rhs) == 0;
    }
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_FOREIGN:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
    case GV_INTERNAL_TAG:
        return false;
    }
    return false;
}

static bool match_atoms_atom_id_epoch_worklist(
    Atom *left, const TermUniverse *candidate_universe, AtomId right_id,
    Bindings *b, Arena *a, uint32_t epoch) {
    if (!left || !candidate_universe || right_id == CETTA_ATOM_ID_NONE)
        return false;
    StoredMatchWorklist work;
    work.items = work.inline_items;
    work.len = 0;
    work.cap = sizeof work.inline_items / sizeof work.inline_items[0];
    if (!stored_match_push(&work, left, right_id)) return false;

    while (work.len > 0) {
        StoredMatchPair pair = work.items[--work.len];
        left = pair.left;
        right_id = pair.right_id;
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(b);

retry_pair:
        if (!tu_hdr(candidate_universe, right_id)) {
            Atom *right = term_universe_get_atom(
                (TermUniverse *)candidate_universe, right_id);
            if (!right || !match_atoms_epoch_worklist(
                    left, right, b, NULL, a, epoch))
                goto fail;
            continue;
        }
        AtomKind right_kind = tu_kind(candidate_universe, right_id);
        if (left->kind == ATOM_VAR) {
            Atom *existing = bindings_lookup_var(b, left);
            if (existing) {
                if (++dereferences > dereference_limit) goto fail;
                left = existing;
                goto retry_pair;
            }
            if (right_kind == ATOM_VAR) {
                VarId right_var_id = var_epoch_id(
                    tu_var_id(candidate_universe, right_id), epoch);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
                Atom *right_existing = bindings_lookup_id(b, right_var_id);
                if (right_existing) {
                    if (!match_decoded_atoms_worklist(
                            left, right_existing, b, NULL, false))
                        goto fail;
                    continue;
                }
                if (left->var_id == right_var_id) continue;
                Atom *value = term_universe_copy_atom_epoch(
                    candidate_universe, a, right_id, epoch);
                if (!value || !bindings_add_var(b, left, value)) goto fail;
                continue;
            }
            Atom *value = !tu_has_vars(candidate_universe, right_id)
                ? (a ? term_universe_copy_atom(candidate_universe, a, right_id)
                     : term_universe_get_atom(
                           (TermUniverse *)candidate_universe, right_id))
                : term_universe_copy_atom_epoch(
                      candidate_universe, a, right_id, epoch);
            if (!value || !bindings_add_var(b, left, value)) goto fail;
            continue;
        }
        if (right_kind == ATOM_VAR) {
            VarId right_var_id = var_epoch_id(
                tu_var_id(candidate_universe, right_id), epoch);
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
            Atom *existing = bindings_lookup_id(b, right_var_id);
            if (existing) {
                if (!match_decoded_atoms_worklist(
                        left, existing, b, NULL, false))
                    goto fail;
                continue;
            }
            Atom *binding_var = term_universe_copy_atom_epoch(
                candidate_universe, a, right_id, epoch);
            if (!binding_var ||
                !bindings_add_var(b, binding_var, left))
                goto fail;
            continue;
        }
        switch (left->kind) {
        case ATOM_SYMBOL:
            if (right_kind != ATOM_SYMBOL ||
                left->sym_id != tu_sym(candidate_universe, right_id))
                goto fail;
            break;
        case ATOM_VAR:
            goto fail;
        case ATOM_GROUNDED:
            if (!stored_grounded_equal(left, candidate_universe, right_id))
                goto fail;
            break;
        case ATOM_EXPR:
            if (right_kind != ATOM_EXPR ||
                left->expr.len != tu_arity(candidate_universe, right_id))
                goto fail;
            for (CettaExprIndex i = left->expr.len; i > 0; i--) {
                CettaExprIndex child = i - 1u;
                if (!stored_match_push(
                        &work, left->expr.elems[child],
                        tu_child(candidate_universe, right_id, child)))
                    goto fail;
            }
            break;
        }
    }
    if (work.items != work.inline_items) free(work.items);
    return true;

fail:
    if (work.items != work.inline_items) free(work.items);
    return false;
}

bool bindings_eq(Bindings *a, Bindings *b) {
    if (!(prime_need_snapshot_is_ancestor(bindings_need_view(a),
                                          bindings_need_view(b)) &&
          prime_need_snapshot_is_ancestor(bindings_need_view(b),
                                          bindings_need_view(a))))
        return false;
    if (!prime_need_branch_state_equal(bindings_branch_state_view(a),
                                       bindings_branch_state_view(b)))
        return false;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (!prime_need_receipt_equal(bindings_receipt_view(a),
                                  bindings_receipt_view(b)))
        return false;
#endif
    if (bindings_occurrence_token(a) != bindings_occurrence_token(b))
        return false;
    if (a->len != b->len) return false;
    if (a->eq_len != b->eq_len) return false;
    for (uint32_t i = 0; i < a->len; i++) {
        Atom *other = bindings_lookup_id(b, a->entries[i].var_id);
        if (!other || !atom_eq(other, a->entries[i].val))
            return false;
    }
    bool matched_stack[BINDINGS_TEMP_STACK_CAP];
    bool *matched = NULL;
    if (b->eq_len > 0) {
        matched = b->eq_len <= BINDINGS_TEMP_STACK_CAP
            ? matched_stack
            : cetta_malloc(sizeof(bool) * b->eq_len);
        memset(matched, 0, sizeof(bool) * b->eq_len);
    }
    for (uint32_t i = 0; i < a->eq_len; i++) {
        bool found = false;
        for (uint32_t j = 0; j < b->eq_len; j++) {
            if (!matched[j] &&
                constraint_pair_eq(&a->constraints[i], &b->constraints[j])) {
                matched[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            if (matched && matched != matched_stack)
                free(matched);
            return false;
        }
    }
    if (matched && matched != matched_stack)
        free(matched);
    return true;
}
