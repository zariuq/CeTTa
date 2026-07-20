#include "match.h"
#include "stats.h"
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
#define BINDINGS_LOOKUP_CACHE_SLOTS 4
#define BINDINGS_MEMO_STACK_CAP 32
#define BINDINGS_LOOKUP_CACHE_MISS UINT32_MAX
#define FRESHEN_EPOCH_MEMO_INLINE_CAP 64u

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
} FreshenEpochMemo;

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
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return variant_private_var_id(atom->var_id);
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (atom_contains_private_variant_var(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

bool bindings_contains_private_variant_slots(const Bindings *b) {
    if (!b)
        return false;
    for (uint32_t i = 0; i < b->len; i++) {
        if (variant_private_var_id(b->entries[i].var_id) ||
            atom_contains_private_variant_var(b->entries[i].val)) {
            return true;
        }
    }
    for (uint32_t i = 0; i < b->eq_len; i++) {
        if (atom_contains_private_variant_var(b->constraints[i].lhs) ||
            atom_contains_private_variant_var(b->constraints[i].rhs)) {
            return true;
        }
    }
    return false;
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
    uint32_t slot = b->lookup_cache_count < BINDINGS_LOOKUP_CACHE_SLOTS
        ? b->lookup_cache_count++
        : b->lookup_cache_next;
    b->lookup_cache_ids[slot] = var_id;
    b->lookup_cache_indices[slot] = index;
    if (b->lookup_cache_count == BINDINGS_LOOKUP_CACHE_SLOTS)
        b->lookup_cache_next = (uint8_t)((slot + 1) % BINDINGS_LOOKUP_CACHE_SLOTS);
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

static bool bindings_normalize_constraints(Bindings *b) {
    if (b->eq_len == 0) return true;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = bindings_temp_constraints_alloc(
        b->eq_len, pending_stack, BINDINGS_TEMP_STACK_CAP);
    uint32_t npending = b->eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = b->constraints[i];
    b->eq_len = 0;
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

void bindings_init(Bindings *b) {
    b->entries = NULL;
    b->len = 0;
    b->cap = 0;
    b->constraints = NULL;
    b->eq_len = 0;
    b->eq_cap = 0;
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

bool bindings_promote_atoms_to_arena(Bindings *bindings, Arena *dst) {
    if (!bindings || !dst)
        return true;
    for (uint32_t i = 0; i < bindings->len; i++) {
        Atom *promoted_name_key = atom_deep_copy(
            dst, bindings->entries[i].name_key);
        Atom *promoted = atom_deep_copy(dst, bindings->entries[i].val);
        if ((bindings->entries[i].name_key && !promoted_name_key) ||
            (bindings->entries[i].val && !promoted))
            return false;
        bindings->entries[i].name_key = promoted_name_key;
        bindings->entries[i].val = promoted;
    }
    for (uint32_t i = 0; i < bindings->eq_len; i++) {
        Atom *lhs = atom_deep_copy(dst, bindings->constraints[i].lhs);
        Atom *rhs = atom_deep_copy(dst, bindings->constraints[i].rhs);
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

void bindings_move(Bindings *dst, Bindings *src) {
    *dst = *src;
    bindings_init(src);
}

void bindings_replace(Bindings *dst, Bindings *src) {
    bindings_free(dst);
    bindings_move(dst, src);
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
        if (legacy_name_fallback && existing_idx >= 0)
            b->entries[existing_idx].legacy_name_fallback = true;
        if (normalize_constraints && !bindings_normalize_constraints(b))
            return false;
        return true;
    }
    if (!bindings_reserve_entries(b, b->len + 1)) {
        return false;
    }
    b->entries[b->len].var_id = var_id;
    b->entries[b->len].spelling = spelling;
    b->entries[b->len].name_key = name_key;
    b->entries[b->len].val = val;
    b->entries[b->len].legacy_name_fallback = legacy_name_fallback;
    bindings_lookup_cache_note(b, var_id, b->len);
    b->len++;
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

static bool bindings_try_merge_inplace(Bindings *dst, const Bindings *src) {
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
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
    if (!src || (src->len == 0 && src->eq_len == 0))
        return true;
    bindings_assert_no_private_variant_slots(dst);
    bindings_assert_no_private_variant_slots(src);
    if (dst && dst->len == 0 && dst->eq_len == 0) {
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
    Atom **vals;
    uint32_t len;
    uint32_t cap;
    bool heap;
} BindingApplyMemo;

static inline void bindings_apply_memo_init(BindingApplyMemo *memo, VarId *ids,
                                            Atom **vals, uint32_t cap) {
    memo->ids = ids;
    memo->vals = vals;
    memo->len = 0;
    memo->cap = cap;
    memo->heap = false;
}

static void bindings_apply_memo_release(BindingApplyMemo *memo) {
    if (!memo->heap)
        return;
    free(memo->ids);
    free(memo->vals);
    memo->ids = NULL;
    memo->vals = NULL;
    memo->len = 0;
    memo->cap = 0;
    memo->heap = false;
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

static Atom *bindings_apply_memo_lookup(BindingApplyMemo *memo, VarId id) {
    for (uint32_t i = memo->len; i > 0; i--) {
        if (binding_var_eq(memo->ids[i - 1], id))
            return memo->vals[i - 1];
    }
    return NULL;
}

static bool bindings_apply_memo_store(BindingApplyMemo *memo, VarId id, Atom *val) {
    for (uint32_t i = 0; i < memo->len; i++) {
        if (binding_var_eq(memo->ids[i], id)) {
            memo->vals[i] = val;
            return true;
        }
    }
    if (!bindings_apply_memo_reserve(memo, memo->len + 1))
        return false;
    memo->ids[memo->len] = id;
    memo->vals[memo->len] = val;
    memo->len++;
    return true;
}

static Atom *bindings_apply_seen_with_rewrite(Bindings *b, Arena *a, Atom *atom,
                                              VarId *seen, uint32_t seen_len,
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
        if (bindings_seen_var(seen, seen_len, atom->var_id)) {
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
        seen[seen_len] = atom->var_id;
        Atom *result = bindings_apply_seen_with_rewrite(b, a, val, seen, seen_len + 1,
                                                        memo, rewrite_var, rewrite_ctx);
        if (result)
            bindings_apply_memo_store(memo, atom->var_id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? bindings_apply_seen_with_rewrite(b, a, child,
                                                   seen, seen_len, memo,
                                                   rewrite_var, rewrite_ctx)
                : child;
            if (!next)
                return NULL;
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

Atom *bindings_apply_rewrite_vars(Bindings *b, Arena *a, Atom *atom,
                                  BindingsRewriteVarFn rewrite_var,
                                  void *rewrite_ctx) {
    if (!b || !a || !atom)
        return NULL;
    if (b->len == 0 && !rewrite_var)
        return atom;
    uint32_t seen_cap = b->len ? b->len : 1;
    VarId seen_stack[BINDINGS_SEEN_STACK_CAP];
    VarId memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    VarId *seen = seen_cap <= BINDINGS_SEEN_STACK_CAP
        ? seen_stack
        : cetta_malloc(sizeof(VarId) * seen_cap);
    BindingApplyMemo memo;
    bindings_apply_memo_init(&memo, memo_id_stack, memo_val_stack,
                             BINDINGS_MEMO_STACK_CAP);
    Atom *result = bindings_apply_seen_with_rewrite(b, a, atom, seen, 0, &memo,
                                                    rewrite_var, rewrite_ctx);
    bindings_apply_memo_release(&memo);
    if (seen != seen_stack)
        free(seen);
    return result;
}

Atom *bindings_apply(Bindings *b, Arena *a, Atom *atom) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY);
    return bindings_apply_rewrite_vars(b, a, atom, NULL, NULL);
}

static Atom *bindings_apply_seen_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
                                       bool original_side,
                                       VarId *seen, uint32_t seen_len,
                                       BindingApplyMemo *memo) {
    if (!atom_has_vars(atom))
        return atom;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EPOCH_NODE_VISIT);
    switch (atom->kind) {
    case ATOM_VAR: {
        VarId lookup_id = original_side ? var_epoch_id(atom->var_id, epoch) : atom->var_id;
        Atom *memoized = bindings_apply_memo_lookup(memo, lookup_id);
        if (memoized) return memoized;
        if (bindings_seen_var(seen, seen_len, lookup_id)) {
            Atom *result = original_side ? epoch_var_atom(a, atom, epoch) : atom;
            bindings_apply_memo_store(memo, lookup_id, result);
            return result;
        }
        Atom *val = bindings_lookup_id(b, lookup_id);
        if (!val) {
            Atom *result = original_side ? epoch_var_atom(a, atom, epoch) : atom;
            bindings_apply_memo_store(memo, lookup_id, result);
            return result;
        }
        seen[seen_len] = lookup_id;
        Atom *result = bindings_apply_seen_epoch(
            b, a, val, epoch, false, seen, seen_len + 1, memo);
        bindings_apply_memo_store(memo, lookup_id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? bindings_apply_seen_epoch(b, a, child, epoch, original_side,
                                            seen, seen_len, memo)
                : child;
            if (!new_elems && next != atom->expr.elems[i]) {
                new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                for (uint32_t j = 0; j < i; j++)
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

Atom *bindings_apply_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch) {
    uint32_t seen_cap = b->len ? b->len : 1;
    VarId seen_stack[BINDINGS_SEEN_STACK_CAP];
    VarId memo_id_stack[BINDINGS_MEMO_STACK_CAP];
    Atom *memo_val_stack[BINDINGS_MEMO_STACK_CAP];
    VarId *seen = seen_cap <= BINDINGS_SEEN_STACK_CAP
        ? seen_stack
        : cetta_malloc(sizeof(VarId) * seen_cap);
    BindingApplyMemo memo;
    bindings_apply_memo_init(&memo, memo_id_stack, memo_val_stack,
                             BINDINGS_MEMO_STACK_CAP);
    Atom *result = bindings_apply_seen_epoch(b, a, atom, epoch, true, seen, 0, &memo);
    bindings_apply_memo_release(&memo);
    if (seen != seen_stack)
        free(seen);
    return result;
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
    freshen_epoch_memo_clear(memo->slots, memo->cap);
}

static void freshen_epoch_memo_free(FreshenEpochMemo *memo) {
    if (!memo)
        return;
    if (memo->slots != memo->inline_slots)
        free(memo->slots);
    memo->slots = memo->inline_slots;
    memo->cap = FRESHEN_EPOCH_MEMO_INLINE_CAP;
    memo->used = 0;
    freshen_epoch_memo_clear(memo->slots, memo->cap);
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
        if (!slot->src)
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
    FreshenEpochMemoSlot *new_slots =
        cetta_malloc(sizeof(FreshenEpochMemoSlot) * new_cap);
    freshen_epoch_memo_clear(new_slots, new_cap);
    memo->slots = new_slots;
    memo->cap = new_cap;
    memo->used = 0;
    for (size_t i = 0; i < old_cap; i++) {
        const Atom *src = old_slots[i].src;
        Atom *dst = old_slots[i].dst;
        if (!src)
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
        Atom **new_elems = NULL;
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            Atom *child = atom->expr.elems[i];
            Atom *next = atom_has_vars(child)
                ? atom_freshen_epoch_impl(a, child, epoch, memo)
                : child;
            if (!next)
                return NULL;
            if (!new_elems && next != atom->expr.elems[i]) {
                new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                for (CettaExprIndex j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        out = new_elems ? atom_expr(a, new_elems, atom->expr.len) : atom;
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
    uint32_t next_cap = bb->trail_cap ? bb->trail_cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    bb->trail = cetta_realloc(bb->trail,
                              sizeof(BindingsBuilderTrailEntry) * next_cap);
    bb->trail_cap = next_cap;
    return true;
}

static bool bindings_builder_snapshot(BindingsBuilder *bb) {
    if (!bindings_builder_trail_reserve(bb, bb->trail_len + 1))
        return false;
    bb->trail[bb->trail_len++] = (BindingsBuilderTrailEntry){
        .len = bb->current.len,
        .eq_len = bb->current.eq_len,
        .lookup_cache_count = bb->current.lookup_cache_count,
        .lookup_cache_next = bb->current.lookup_cache_next,
    };
    return true;
}

bool bindings_builder_init(BindingsBuilder *bb, const Bindings *base) {
    bindings_init(&bb->current);
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    if (!base)
        return true;
    if (!bindings_clone(&bb->current, base)) {
        free(bb->trail);
        bb->trail = NULL;
        bb->trail_len = 0;
        bb->trail_cap = 0;
        bindings_free(&bb->current);
        return false;
    }
    return true;
}

void bindings_builder_init_owned(BindingsBuilder *bb, Bindings *owned) {
    bb->current = *owned;
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bindings_init(owned);
}

void bindings_builder_free(BindingsBuilder *bb) {
    free(bb->trail);
    bb->trail = NULL;
    bb->trail_len = 0;
    bb->trail_cap = 0;
    bindings_free(&bb->current);
}

uint32_t bindings_builder_save(const BindingsBuilder *bb) {
    return bb->trail_len;
}

void bindings_builder_rollback(BindingsBuilder *bb, uint32_t mark) {
    while (bb->trail_len > mark) {
        BindingsBuilderTrailEntry *entry = &bb->trail[--bb->trail_len];
        bb->current.len = entry->len;
        bb->current.eq_len = entry->eq_len;
        bb->current.lookup_cache_count = entry->lookup_cache_count;
        bb->current.lookup_cache_next = entry->lookup_cache_next;
    }
}

void bindings_builder_commit(BindingsBuilder *bb) {
    bb->trail_len = 0;
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
        bb->trail_len--;
        return false;
    }
    bb->current.entries[bb->current.len].var_id = var_id;
    bb->current.entries[bb->current.len].spelling = spelling;
    bb->current.entries[bb->current.len].name_key = name_key;
    bb->current.entries[bb->current.len].val = val;
    bb->current.entries[bb->current.len].legacy_name_fallback = legacy_name_fallback;
    bindings_lookup_cache_note(&bb->current, var_id, bb->current.len);
    bb->current.len++;
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
        bb->trail_len--;
        return false;
    }
    bb->current.constraints[bb->current.eq_len++] = next;
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
    if (src->len == 0 && src->eq_len == 0)
        return true;
    bindings_assert_no_private_variant_slots(&bb->current);
    bindings_assert_no_private_variant_slots(src);

    uint32_t mark = bindings_builder_save(bb);
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
}

/* ── Variable renaming (standardization apart) ─────────────────────────── */

static _Atomic uint32_t g_var_counter = 1;
#define FRESH_VAR_SUFFIX_BLOCK_SIZE 4096u

typedef struct {
    uint32_t next;
    uint32_t remaining;
} FreshVarSuffixBlockCache;

static __thread FreshVarSuffixBlockCache g_fresh_var_suffix_block_cache = {0};

typedef struct {
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

uint32_t fresh_var_suffix(void) {
    /* Epoch 0 means "no standardization-apart tag", so fresh suffixes must
       start at 1 and keep skipping 0 on unsigned wraparound. */
    while (g_fresh_var_suffix_block_cache.remaining > 0) {
        uint32_t suffix = g_fresh_var_suffix_block_cache.next++;
        g_fresh_var_suffix_block_cache.remaining--;
        if (suffix != 0)
            return suffix;
    }

    uint32_t start = atomic_fetch_add_explicit(&g_var_counter,
                                               FRESH_VAR_SUFFIX_BLOCK_SIZE,
                                               memory_order_relaxed);
    g_fresh_var_suffix_block_cache.next = start;
    g_fresh_var_suffix_block_cache.remaining = FRESH_VAR_SUFFIX_BLOCK_SIZE;
    return fresh_var_suffix();
}

static void var_id_set_init(VarIdSet *set) {
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static void var_id_set_free(VarIdSet *set) {
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
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
        set->items = cetta_realloc(
            set->items, sizeof(*set->items) * (size_t)next_cap);
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

static bool collect_var_ids(Atom *root, VarIdSet *set) {
    if (!root || !set)
        return false;
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

static Atom *rename_vars_except_iterative(Arena *a, Atom *root,
                                          const VarIdSet *ignore,
                                          RenameVarMap *map) {
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
            if (var_id_set_contains(ignore, atom->var_id)) {
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
        ? rename_vars_except_iterative(a, atom, &ignore, &map)
        : NULL;
    rename_var_map_free(&map);
    var_id_set_free(&ignore);
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

static int32_t bindings_find_entry_index_for_loop(const Bindings *b, VarId var_id) {
    for (uint32_t i = b->len; i > 0; i--) {
        uint32_t idx = i - 1;
        if (binding_var_eq(b->entries[idx].var_id, var_id))
            return (int32_t)idx;
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
           value->var_id == b->entries[idx].var_id;
}

bool bindings_has_loop(const Bindings *b) {
    if (!b || b->len == 0)
        return false;
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
                int32_t found = bindings_find_entry_index_for_loop(
                    b, atom->var_id);
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

typedef struct {
    Atom *left;
    Atom *right;
    bool tagged;
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

static size_t match_path_hash(Atom *left, Atom *right, bool tagged) {
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
                             bool tagged) {
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
                             bool tagged) {
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
    if ((is_named_symbol(actual, "SpaceType") && is_space_value_type(expected)) ||
        (is_named_symbol(expected, "SpaceType") && is_space_value_type(actual))) {
        return true;
    }
    return match_decoded_atoms_worklist(actual, expected, b, NULL, true);
}

bool match_types_builder(Atom *actual, Atom *expected, BindingsBuilder *bb) {
    if (atom_is_symbol_id(expected, g_builtin_syms.atom)) return true;
    if ((is_named_symbol(actual, "SpaceType") && is_space_value_type(expected)) ||
        (is_named_symbol(expected, "SpaceType") && is_space_value_type(actual))) {
        return true;
    }
    return match_decoded_atoms_worklist(actual, expected, NULL, bb, true);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

static bool match_atoms_epoch_worklist(Atom *left, Atom *right, Bindings *b,
                                       Arena *a, uint32_t epoch);
static bool match_atoms_atom_id_epoch_worklist(
    Atom *left, const TermUniverse *candidate_universe, AtomId right_id,
    Bindings *b, Arena *a, uint32_t epoch);

bool match_atoms(Atom *left, Atom *right, Bindings *b) {
    return match_decoded_atoms_worklist(left, right, b, NULL, false);
}

bool match_atoms_builder(Atom *left, Atom *right, BindingsBuilder *bb) {
    return match_decoded_atoms_worklist(left, right, NULL, bb, false);
}

bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_worklist(left, right, b, a, epoch);
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
    bool right_original;
} EpochMatchPair;

typedef struct {
    EpochMatchPair *items;
    size_t len;
    size_t cap;
    EpochMatchPair inline_items[16];
} EpochMatchWorklist;

static bool epoch_match_push(EpochMatchWorklist *work, Atom *left,
                             Atom *right, bool right_original) {
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
        (EpochMatchPair){false, left, right, right_original};
    return true;
}

static bool epoch_match_push_exit(EpochMatchWorklist *work, Atom *left,
                                  Atom *right, bool right_original) {
    if (!epoch_match_push(work, left, right, right_original)) return false;
    work->items[work->len - 1u].exit = true;
    return true;
}

static bool match_atoms_epoch_worklist(Atom *left, Atom *right, Bindings *b,
                                       Arena *a, uint32_t epoch) {
    EpochMatchWorklist work;
    MatchPathSet path;
    work.items = work.inline_items;
    work.len = 0;
    work.cap = sizeof work.inline_items / sizeof work.inline_items[0];
    match_path_init(&path);
    if (!epoch_match_push(&work, left, right, true)) goto fail;

    while (work.len > 0) {
        EpochMatchPair pair = work.items[--work.len];
        if (pair.exit) {
            match_path_leave(&path, pair.left, pair.right,
                             pair.right_original);
            continue;
        }
        left = pair.left;
        right = pair.right;
        bool right_original = pair.right_original;
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(b);

retry_pair:
        if (left->kind == ATOM_VAR) {
            Atom *existing = bindings_lookup_var(b, left);
            if (existing) {
                if (++dereferences > dereference_limit) goto fail;
                left = existing;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                VarId right_id = right_original
                    ? var_epoch_id(right->var_id, epoch) : right->var_id;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
                Atom *right_existing = bindings_lookup_id(b, right_id);
                if (right_existing) {
                    if (++dereferences > dereference_limit) goto fail;
                    right = right_existing;
                    right_original = false;
                    goto retry_pair;
                }
                if (left->var_id == right_id) continue;
                Atom *value = right_original
                    ? epoch_var_atom(a, right, epoch) : right;
                if (!value || !bindings_add_var(b, left, value)) goto fail;
                continue;
            }
            Atom *value = right_original
                ? bindings_apply_epoch(b, a, right, epoch) : right;
            if (!value || !bindings_add_var(b, left, value)) goto fail;
            continue;
        }
        if (right->kind == ATOM_VAR) {
            VarId right_id = right_original
                ? var_epoch_id(right->var_id, epoch) : right->var_id;
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_MATCH);
            Atom *existing = bindings_lookup_id(b, right_id);
            if (existing) {
                if (++dereferences > dereference_limit) goto fail;
                right = existing;
                right_original = false;
                goto retry_pair;
            }
            Atom *binding_var = right_original
                ? epoch_var_atom(a, right, epoch) : right;
            if (!binding_var || !bindings_add_var(b, binding_var, left))
                goto fail;
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
        if (!match_path_enter(&path, left, right, right_original) ||
            !epoch_match_push_exit(
                &work, left, right, right_original))
            goto fail;
        for (CettaExprIndex i = left->expr.len; i > 0; i--) {
            CettaExprIndex child = i - 1u;
            if (!epoch_match_push(&work, left->expr.elems[child],
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
                    left, right, b, a, epoch))
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
