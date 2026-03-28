#include "match.h"
#include "stats.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Bindings ───────────────────────────────────────────────────────────── */

#define BINDINGS_RECURSION_LIMIT 512
#define BINDINGS_MIN_CAPACITY 8
#define BINDINGS_SEEN_STACK_CAP 32
#define BINDINGS_TEMP_STACK_CAP 32
#define BINDINGS_POOL_CLASS_COUNT 4
#define BINDINGS_LOOKUP_CACHE_SLOTS 4
#define BINDINGS_LOOKUP_CACHE_MIN_LEN 8
#define BINDINGS_MEMO_STACK_CAP 32

typedef struct BindingPoolBlock {
    struct BindingPoolBlock *next;
} BindingPoolBlock;

static const uint32_t BINDINGS_POOL_CAPS[BINDINGS_POOL_CLASS_COUNT] = {8, 16, 32, 64};
static BindingPoolBlock *g_binding_entry_pools[BINDINGS_POOL_CLASS_COUNT];
static BindingPoolBlock *g_binding_constraint_pools[BINDINGS_POOL_CLASS_COUNT];

static inline bool binding_var_eq(VarId lhs, VarId rhs) {
    return lhs == rhs;
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
    if (klass >= 0 && g_binding_entry_pools[klass]) {
        BindingPoolBlock *block = g_binding_entry_pools[klass];
        g_binding_entry_pools[klass] = block->next;
        return (Binding *)block;
    }
    return cetta_malloc(sizeof(Binding) * cap);
}

static void bindings_entries_release(Binding *entries, uint32_t cap) {
    if (!entries) return;
    int klass = bindings_pool_class(cap);
    if (klass < 0) {
        free(entries);
        return;
    }
    BindingPoolBlock *block = (BindingPoolBlock *)entries;
    block->next = g_binding_entry_pools[klass];
    g_binding_entry_pools[klass] = block;
}

static BindingConstraint *bindings_constraints_alloc(uint32_t cap) {
    int klass = bindings_pool_class(cap);
    if (klass >= 0 && g_binding_constraint_pools[klass]) {
        BindingPoolBlock *block = g_binding_constraint_pools[klass];
        g_binding_constraint_pools[klass] = block->next;
        return (BindingConstraint *)block;
    }
    return cetta_malloc(sizeof(BindingConstraint) * cap);
}

static void bindings_constraints_release(BindingConstraint *constraints, uint32_t cap) {
    if (!constraints) return;
    int klass = bindings_pool_class(cap);
    if (klass < 0) {
        free(constraints);
        return;
    }
    BindingPoolBlock *block = (BindingPoolBlock *)constraints;
    block->next = g_binding_constraint_pools[klass];
    g_binding_constraint_pools[klass] = block;
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
    if (b->len < BINDINGS_LOOKUP_CACHE_MIN_LEN) {
        for (uint32_t i = 0; i < b->len; i++) {
            if (binding_var_eq(b->entries[i].var_id, var_id))
                return (int32_t)i;
        }
        return -1;
    }
    for (uint32_t i = 0; i < b->lookup_cache_count; i++) {
        uint32_t idx = b->lookup_cache_indices[i];
        if (idx < b->len &&
            binding_var_eq(b->lookup_cache_ids[i], var_id) &&
            binding_var_eq(b->entries[idx].var_id, var_id))
            return (int32_t)idx;
    }
    for (uint32_t i = 0; i < b->len; i++) {
        if (binding_var_eq(b->entries[i].var_id, var_id)) {
            bindings_lookup_cache_note(b, var_id, i);
            return (int32_t)i;
        }
    }
    return -1;
}

static Atom *bindings_resolve_atom(Bindings *b, Atom *atom, uint32_t depth) {
    if (!atom || depth > BINDINGS_RECURSION_LIMIT) return atom;
    while (atom->kind == ATOM_VAR) {
        Atom *next = bindings_lookup_id(b, atom->var_id);
        if (!next) next = bindings_lookup_spelling(b, atom->sym_id);
        if (!next) return atom;
        if (next == atom) return atom;
        if (next->kind == ATOM_VAR &&
            (binding_var_eq(next->var_id, atom->var_id) ||
             next->sym_id == atom->sym_id))
            return atom;
        atom = next;
        if (++depth > BINDINGS_RECURSION_LIMIT) return atom;
    }
    return atom;
}

static bool atom_contains_unbound_var(Bindings *b, Atom *atom, uint32_t depth) {
    atom = bindings_resolve_atom(b, atom, depth);
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (atom_contains_unbound_var(b, atom->expr.elems[i], depth + 1))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool atom_eq_under_bindings(Bindings *b, Atom *lhs, Atom *rhs, uint32_t depth) {
    lhs = bindings_resolve_atom(b, lhs, depth);
    rhs = bindings_resolve_atom(b, rhs, depth);
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
        for (uint32_t i = 0; i < lhs->expr.len; i++) {
            if (!atom_eq_under_bindings(b, lhs->expr.elems[i], rhs->expr.elems[i], depth + 1))
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
                                          SymbolId spelling, Atom *val,
                                          bool normalize_constraints,
                                          bool legacy_name_fallback);
static bool bindings_add_internal(Bindings *b, VarId var_id, SymbolId spelling,
                                  Atom *val, bool normalize_constraints,
                                  bool legacy_name_fallback);
static bool bindings_add_constraint_inplace_internal(Bindings *b, Atom *lhs,
                                                     Atom *rhs,
                                                     bool normalize_constraints);
static bool bindings_add_constraint_internal(Bindings *b, Atom *lhs, Atom *rhs,
                                             bool normalize_constraints);

static bool bindings_normalize_constraints(Bindings *b) {
    if (b->eq_len == 0) return true;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = b->eq_len <= BINDINGS_TEMP_STACK_CAP
        ? pending_stack
        : cetta_malloc(sizeof(BindingConstraint) * b->eq_len);
    uint32_t npending = b->eq_len;
    for (uint32_t i = 0; i < npending; i++)
        pending[i] = b->constraints[i];
    b->eq_len = 0;
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_inplace_internal(
                b, pending[i].lhs, pending[i].rhs, false)) {
            if (pending != pending_stack)
                free(pending);
            return false;
        }
    }
    if (pending != pending_stack)
        free(pending);
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
    if (src->len >= BINDINGS_LOOKUP_CACHE_MIN_LEN) {
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
    return atom_var_with_spelling(a, var->sym_id, var_epoch_id(var->var_id, epoch));
}

static bool bindings_add_inplace_internal(Bindings *b, VarId var_id,
                                          SymbolId spelling, Atom *val,
                                          bool normalize_constraints,
                                          bool legacy_name_fallback) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_ADD);
    if (val->kind == ATOM_VAR && binding_var_eq(var_id, val->var_id)) {
        return true;
    }
    if (val->kind == ATOM_VAR) {
        Atom *other = bindings_lookup_id(b, val->var_id);
        if (other && other->kind == ATOM_VAR && binding_var_eq(other->var_id, var_id)) {
            return true;
        }
    }
    /* Check for existing binding */
    int32_t existing_idx = bindings_lookup_index(b, var_id);
    Atom *existing = existing_idx >= 0 ? b->entries[existing_idx].val : NULL;
    if (existing) {
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
    b->entries[b->len].val = val;
    b->entries[b->len].legacy_name_fallback = legacy_name_fallback;
    if (b->len + 1 >= BINDINGS_LOOKUP_CACHE_MIN_LEN)
        bindings_lookup_cache_note(b, var_id, b->len);
    b->len++;
    if (normalize_constraints && !bindings_normalize_constraints(b))
        return false;
    return true;
}

static bool bindings_add_internal(Bindings *b, VarId var_id, SymbolId spelling,
                                  Atom *val, bool normalize_constraints,
                                  bool legacy_name_fallback) {
    Bindings next;
    if (!bindings_clone(&next, b))
        return false;
    if (!bindings_add_inplace_internal(&next, var_id, spelling, val,
                                       normalize_constraints,
                                       legacy_name_fallback)) {
        bindings_free(&next);
        return false;
    }
    bindings_replace(b, &next);
    return true;
}

bool bindings_add_id(Bindings *b, VarId var_id, SymbolId spelling, Atom *val) {
    return bindings_add_internal(b, var_id, spelling, val, true, false);
}

bool bindings_add_var(Bindings *b, Atom *var, Atom *val) {
    return bindings_add_id(b, var->var_id, var->sym_id, val);
}

static bool bindings_add_constraint_inplace_internal(Bindings *b, Atom *lhs,
                                                     Atom *rhs,
                                                     bool normalize_constraints) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_CONSTRAINT_ADD);
    lhs = bindings_resolve_atom(b, lhs, 0);
    rhs = bindings_resolve_atom(b, rhs, 0);

    if (atom_eq_under_bindings(b, lhs, rhs, 0)) {
        return true;
    }
    if (lhs->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, lhs->var_id, lhs->sym_id, rhs, false, false)) {
            return false;
        }
    } else if (rhs->kind == ATOM_VAR) {
        if (!bindings_add_inplace_internal(
                b, rhs->var_id, rhs->sym_id, lhs, false, false)) {
            return false;
        }
    } else if (!atom_contains_unbound_var(b, lhs, 0) &&
               !atom_contains_unbound_var(b, rhs, 0)) {
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
    uint32_t pending_cap = dst->eq_len + src->eq_len + 1;
    BindingConstraint pending_stack[BINDINGS_TEMP_STACK_CAP];
    BindingConstraint *pending = pending_cap <= BINDINGS_TEMP_STACK_CAP
        ? pending_stack
        : cetta_malloc(sizeof(BindingConstraint) * pending_cap);
    uint32_t npending = 0;
    for (uint32_t i = 0; i < dst->eq_len; i++)
        pending[npending++] = dst->constraints[i];
    for (uint32_t i = 0; i < src->eq_len; i++) {
        pending[npending++] = src->constraints[i];
    }
    dst->eq_len = 0;
    for (uint32_t i = 0; i < src->len; i++) {
        if (!bindings_add_inplace_internal(dst, src->entries[i].var_id,
                                           src->entries[i].spelling, src->entries[i].val,
                                           false,
                                           src->entries[i].legacy_name_fallback)) {
            if (pending != pending_stack)
                free(pending);
            return false;
        }
    }
    for (uint32_t i = 0; i < npending; i++) {
        if (!bindings_add_constraint_inplace_internal(
                dst, pending[i].lhs, pending[i].rhs, false)) {
            if (pending != pending_stack)
                free(pending);
            return false;
        }
    }
    if (pending != pending_stack)
        free(pending);
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

static Atom *bindings_apply_seen(Bindings *b, Arena *a, Atom *atom,
                                 VarId *seen, uint32_t seen_len,
                                 BindingApplyMemo *memo) {
    switch (atom->kind) {
    case ATOM_VAR: {
        Atom *memoized = bindings_apply_memo_lookup(memo, atom->var_id);
        if (memoized) return memoized;
        if (bindings_seen_var(seen, seen_len, atom->var_id)) {
            bindings_apply_memo_store(memo, atom->var_id, atom);
            return atom;
        }
        Atom *val = bindings_lookup_id(b, atom->var_id);
        if (!val) val = bindings_lookup_spelling(b, atom->sym_id);
        if (!val) {
            bindings_apply_memo_store(memo, atom->var_id, atom);
            return atom;
        }
        seen[seen_len] = atom->var_id;
        Atom *result = bindings_apply_seen(b, a, val, seen, seen_len + 1, memo);
        bindings_apply_memo_store(memo, atom->var_id, result);
        return result;
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            Atom *next = bindings_apply_seen(b, a, atom->expr.elems[i], seen, seen_len, memo);
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

Atom *bindings_apply(Bindings *b, Arena *a, Atom *atom) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY);
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
    Atom *result = bindings_apply_seen(b, a, atom, seen, 0, &memo);
    bindings_apply_memo_release(&memo);
    if (seen != seen_stack)
        free(seen);
    return result;
}

static Atom *bindings_apply_seen_epoch(Bindings *b, Arena *a, Atom *atom, uint32_t epoch,
                                       bool original_side,
                                       VarId *seen, uint32_t seen_len,
                                       BindingApplyMemo *memo) {
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
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            Atom *next = bindings_apply_seen_epoch(
                b, a, atom->expr.elems[i], epoch, original_side, seen, seen_len, memo);
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

Atom *bindings_to_atom(Arena *a, const Bindings *b) {
    Atom **assigns = NULL;
    if (b->len > 0) {
        assigns = arena_alloc(a, sizeof(Atom *) * b->len);
        for (uint32_t i = 0; i < b->len; i++) {
            assigns[i] = atom_expr2(a,
                atom_symbol_id(a, b->entries[i].spelling),
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

    for (uint32_t i = 0; i < assigns->expr.len; i++) {
        Atom *assign = assigns->expr.elems[i];
        if (assign->kind != ATOM_EXPR || assign->expr.len != 2 ||
            assign->expr.elems[0]->kind != ATOM_SYMBOL) {
            bindings_free(out);
            return false;
        }
        VarId id = g_var_intern ? var_intern(g_var_intern, assign->expr.elems[0]->sym_id)
                                : fresh_var_id();
        if (!bindings_add_inplace_internal(out, id, assign->expr.elems[0]->sym_id,
                                           assign->expr.elems[1], true, true)) {
            bindings_free(out);
            return false;
        }
    }

    for (uint32_t i = 0; i < equalities->expr.len; i++) {
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
    for (uint32_t i = 0; i < bs->len; i++)
        bindings_free(&bs->items[i]);
    free(bs->items);
    bs->items = NULL;
    bs->len = 0;
    bs->cap = 0;
}

bool binding_set_push(BindingSet *bs, const Bindings *b) {
    if (bs->len >= bs->cap) {
        bs->cap = bs->cap ? bs->cap * 2 : 8;
        bs->items = cetta_realloc(bs->items, sizeof(Bindings) * bs->cap);
    }
    if (!bindings_clone(&bs->items[bs->len], b))
        return false;
    bs->len++;
    return true;
}

void binding_set_push_move(BindingSet *bs, Bindings *b) {
    if (bs->len >= bs->cap) {
        bs->cap = bs->cap ? bs->cap * 2 : 8;
        bs->items = cetta_realloc(bs->items, sizeof(Bindings) * bs->cap);
    }
    bindings_move(&bs->items[bs->len], b);
    bs->len++;
}

/* ── Variable renaming (standardization apart) ─────────────────────────── */

static uint32_t g_var_counter = 0;

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
    return g_var_counter++;
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

static void var_id_set_add(VarIdSet *set, VarId id) {
    if (var_id_set_contains(set, id))
        return;
    if (set->len >= set->cap) {
        set->cap = set->cap ? set->cap * 2 : 8;
        set->items = cetta_realloc(set->items, sizeof(VarId) * set->cap);
    }
    set->items[set->len++] = id;
}

static void collect_var_ids(Atom *atom, VarIdSet *set) {
    switch (atom->kind) {
    case ATOM_VAR:
        var_id_set_add(set, atom->var_id);
        return;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++)
            collect_var_ids(atom->expr.elems[i], set);
        return;
    default:
        return;
    }
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
    Atom *fresh = atom_var_with_spelling(a, var->sym_id, var_epoch_id(var->var_id, suffix));
    if (map->len >= map->cap) {
        map->cap = map->cap ? map->cap * 2 : 8;
        map->items = cetta_realloc(map->items, sizeof(RenameVarEntry) * map->cap);
    }
    map->items[map->len].id = var->var_id;
    map->items[map->len].mapped = fresh;
    map->len++;
    return fresh;
}

static Atom *rename_vars_except_rec(Arena *a, Atom *atom,
                                    const VarIdSet *ignore,
                                    RenameVarMap *map) {
    switch (atom->kind) {
    case ATOM_VAR: {
        if (var_id_set_contains(ignore, atom->var_id))
            return atom;
        Atom *mapped = rename_var_map_lookup(map, atom->var_id);
        if (mapped)
            return mapped;
        return rename_var_map_add_fresh(map, a, atom);
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            Atom *next = rename_vars_except_rec(a, atom->expr.elems[i], ignore, map);
            if (!new_elems && next != atom->expr.elems[i]) {
                new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                for (uint32_t j = 0; j < i; j++)
                    new_elems[j] = atom->expr.elems[j];
            }
            if (new_elems)
                new_elems[i] = next;
        }
        if (!new_elems)
            return atom;
        return atom_expr(a, new_elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

Atom *rename_vars(Arena *a, Atom *atom, uint32_t suffix) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_RENAME_VARS);
    switch (atom->kind) {
    case ATOM_VAR: {
        return atom_var_with_spelling(a, atom->sym_id, var_epoch_id(atom->var_id, suffix));
    }
    case ATOM_EXPR: {
        Atom **new_elems = NULL;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            Atom *next = rename_vars(a, atom->expr.elems[i], suffix);
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

Atom *rename_vars_except(Arena *a, Atom *atom, Atom *ignore_spec) {
    VarIdSet ignore;
    RenameVarMap map;
    var_id_set_init(&ignore);
    rename_var_map_init(&map);
    collect_var_ids(ignore_spec, &ignore);
    Atom *result = rename_vars_except_rec(a, atom, &ignore, &map);
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
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return pattern->ground.ptr == target->ground.ptr;
        }
        return false;

    case ATOM_EXPR:
        if (pattern->expr.len != target->expr.len) return false;
        for (uint32_t i = 0; i < pattern->expr.len; i++) {
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

/* ── Type matching (from HE spec Matching.lean:188-195) ────────────────── */

/* ── Loop-binding rejection (occurs check) ─────────────────────────────── */

/* Check if following variable bindings from a starting variable
   eventually leads back to that variable (transitive loop detection) */
static bool has_transitive_loop(Bindings *b, VarId start_var,
                                Atom *val, VarId *visited, uint32_t depth) {
    if (depth > BINDINGS_RECURSION_LIMIT) return true; /* safety cap */
    switch (val->kind) {
    case ATOM_VAR:
        if (val->var_id == start_var) return true;
        /* Follow the binding chain */
        if (bindings_seen_var(visited, depth, val->var_id)) return false;
        visited[depth] = val->var_id;
        Atom *next = bindings_lookup_id(b, val->var_id);
        if (next) return has_transitive_loop(b, start_var, next, visited, depth + 1);
        return false;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < val->expr.len; i++)
            if (has_transitive_loop(b, start_var, val->expr.elems[i], visited, depth))
                return true;
        return false;
    default:
        return false;
    }
}

bool bindings_has_loop(Bindings *b) {
    for (uint32_t i = 0; i < b->len; i++) {
        Atom *val = b->entries[i].val;
        if (val->kind == ATOM_VAR && val->var_id == b->entries[i].var_id)
            continue; /* $x = $x is not a loop */
        uint32_t visited_cap = (b->len + b->eq_len + 1);
        if (visited_cap == 0) visited_cap = 1;
        VarId visited_stack[BINDINGS_SEEN_STACK_CAP];
        VarId *visited = visited_cap <= BINDINGS_SEEN_STACK_CAP
            ? visited_stack
            : cetta_malloc(sizeof(VarId) * visited_cap);
        visited[0] = b->entries[i].var_id;
        bool has_loop = has_transitive_loop(b, b->entries[i].var_id, val, visited, 1);
        if (visited != visited_stack)
            free(visited);
        if (has_loop)
            return true;
    }
    return false;
}

/* ── Type matching ─────────────────────────────────────────────────────── */

bool match_types(Atom *type1, Atom *type2, Bindings *b) {
    /* %Undefined% and Atom are wildcards — always match */
    if (atom_is_symbol_id(type1, g_builtin_syms.undefined_type) ||
        atom_is_symbol_id(type1, g_builtin_syms.atom) ||
        atom_is_symbol_id(type2, g_builtin_syms.undefined_type) ||
        atom_is_symbol_id(type2, g_builtin_syms.atom)) {
        return true;
    }
    return match_atoms(type1, type2, b);
}

/* ── Bidirectional matching (match_atoms from HE spec metta.md:577-617) ── */

static bool match_atoms_depth(Atom *left, Atom *right, Bindings *b, int depth);
static bool match_atoms_epoch_depth(Atom *left, Atom *right, Bindings *b, Arena *a,
                                    uint32_t epoch, bool right_original, int depth);

bool match_atoms(Atom *left, Atom *right, Bindings *b) {
    return match_atoms_depth(left, right, b, 64);
}

bool match_atoms_epoch(Atom *left, Atom *right, Bindings *b, Arena *a, uint32_t epoch) {
    return match_atoms_epoch_depth(left, right, b, a, epoch, true, 64);
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
        for (uint32_t i = 0; i < left->expr.len; i++) {
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

static bool match_atoms_depth(Atom *left, Atom *right, Bindings *b, int depth) {
    if (depth <= 0) return false; /* prevent infinite recursion */
    if (left->kind == ATOM_VAR) {
        Atom *existing = bindings_lookup_var(b, left);
        if (existing) return match_atoms_depth(existing, right, b, depth - 1);
        if (right->kind == ATOM_VAR) {
            Atom *right_existing = bindings_lookup_var(b, right);
            if (right_existing) return match_atoms_depth(left, right_existing, b, depth - 1);
            if (left->var_id == right->var_id) return true;
        }
        return bindings_add_var(b, left, right);
    }
    if (right->kind == ATOM_VAR) {
        Atom *existing = bindings_lookup_var(b, right);
        if (existing) return match_atoms_depth(left, existing, b, depth - 1);
        return bindings_add_var(b, right, left);
    }
    /* Symbol/Symbol → must be equal */
    if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
        return left->sym_id == right->sym_id;
    }
    /* Grounded/Grounded → structural equality */
    if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
        return atom_eq(left, right);
    }
    /* Expression/Expression → recursive element-wise */
    if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len) return false;
        for (uint32_t i = 0; i < left->expr.len; i++) {
            if (!match_atoms_depth(left->expr.elems[i], right->expr.elems[i], b, depth - 1))
                return false;
        }
        return true;
    }
    /* Different kinds (non-variable) → no match */
    return false;
}

static bool match_atoms_epoch_depth(Atom *left, Atom *right, Bindings *b, Arena *a,
                                    uint32_t epoch, bool right_original, int depth) {
    if (depth <= 0) return false;
    if (left->kind == ATOM_VAR) {
        Atom *existing = bindings_lookup_var(b, left);
        if (existing)
            return match_atoms_epoch_depth(existing, right, b, a, epoch, right_original, depth - 1);
        if (right->kind == ATOM_VAR) {
            VarId right_id = right_original ? var_epoch_id(right->var_id, epoch) : right->var_id;
            Atom *right_existing = bindings_lookup_id(b, right_id);
            if (right_existing)
                return match_atoms_epoch_depth(left, right_existing, b, a, epoch, false, depth - 1);
            if (left->var_id == right_id) return true;
            return bindings_add_var(b, left,
                                    right_original ? epoch_var_atom(a, right, epoch) : right);
        }
        return bindings_add_var(b, left,
                                right_original ? rename_vars(a, right, epoch) : right);
    }
    if (right->kind == ATOM_VAR) {
        VarId right_id = right_original ? var_epoch_id(right->var_id, epoch) : right->var_id;
        Atom *existing = bindings_lookup_id(b, right_id);
        if (existing)
            return match_atoms_epoch_depth(left, existing, b, a, epoch, false, depth - 1);
        return bindings_add_id(b, right_id, right->sym_id, left);
    }
    if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
        return left->sym_id == right->sym_id;
    }
    if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
        return atom_eq(left, right);
    }
    if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR) {
        if (left->expr.len != right->expr.len) return false;
        for (uint32_t i = 0; i < left->expr.len; i++) {
            if (!match_atoms_epoch_depth(left->expr.elems[i], right->expr.elems[i], b, a,
                                         epoch, right_original, depth - 1))
                return false;
        }
        return true;
    }
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
