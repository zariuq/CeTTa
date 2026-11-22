#include "subst_tree.h"
#include "stats.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── SubstNode lifecycle ───────────────────────────────────────────────── */

SubstNode *snode_new(void) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SUBST_NODE_NEW);
    SubstNode *n = cetta_malloc(sizeof(SubstNode));
    if (!n) return NULL;
    memset(n, 0, sizeof(SubstNode));
    return n;
}

void snode_free(SubstNode *n) {
    if (!n) return;
    if (n->sym_hashed) {
        uint32_t cap = n->sym_ht.mask + 1;
        for (uint32_t i = 0; i < cap; i++)
            if (n->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                snode_free(n->sym_ht.entries[i].child);
        free(n->sym_ht.entries);
    } else {
        for (uint32_t i = 0; i < n->nsym; i++) snode_free(n->sym[i].child);
        free(n->sym);
    }
    for (uint32_t i = 0; i < n->nslots; i++) snode_free(n->slots[i].child);
    free(n->slots);
    for (uint32_t i = 0; i < n->nexpr; i++) snode_free(n->expr[i].child);
    free(n->expr);
    if (n->int_hashed) {
        uint32_t cap = n->int_ht.mask + 1;
        for (uint32_t i = 0; i < cap; i++)
            if (n->int_ht.entries[i].child)
                snode_free(n->int_ht.entries[i].child);
        free(n->int_ht.entries);
    } else {
        for (uint32_t i = 0; i < n->nints; i++) snode_free(n->ints[i].child);
        free(n->ints);
    }
    free(n->leaves);
    free(n);
}

static CettaCount snode_transport_stable_coordinates(
        SubstNode *node, const CettaIndex *source_to_target,
        CettaCount source_len, CettaCount *retained) {
    if (!node)
        return 0u;

    CettaIndex write = 0u;
    CettaCount removed = 0u;
    for (CettaIndex read = 0u; read < node->nleaves; read++) {
        CettaIndex source = node->leaves[read];
        if (source >= source_len) {
            fputs("CeTTa: clean substitution index contains an invalid "
                  "occurrence coordinate\n", stderr);
            abort();
        }
        CettaIndex target = source_to_target[source];
        if (target == UINT64_MAX) {
            removed++;
            continue;
        }
        node->leaves[write++] = target;
    }
    node->nleaves = write;
    *retained += write;

    if (node->sym_hashed) {
        uint32_t cap = node->sym_ht.mask + 1u;
        for (uint32_t i = 0u; i < cap; i++) {
            if (node->sym_ht.entries[i].key != SYMBOL_ID_NONE) {
                removed += snode_transport_stable_coordinates(
                    node->sym_ht.entries[i].child, source_to_target,
                    source_len, retained);
            }
        }
    } else {
        for (uint32_t i = 0u; i < node->nsym; i++) {
            removed += snode_transport_stable_coordinates(
                node->sym[i].child, source_to_target, source_len, retained);
        }
    }
    for (uint32_t i = 0u; i < node->nslots; i++) {
        removed += snode_transport_stable_coordinates(
            node->slots[i].child, source_to_target, source_len, retained);
    }
    for (uint32_t i = 0u; i < node->nexpr; i++) {
        removed += snode_transport_stable_coordinates(
            node->expr[i].child, source_to_target, source_len, retained);
    }
    if (node->int_hashed) {
        uint32_t cap = node->int_ht.mask + 1u;
        for (uint32_t i = 0u; i < cap; i++) {
            if (node->int_ht.entries[i].child) {
                removed += snode_transport_stable_coordinates(
                    node->int_ht.entries[i].child, source_to_target,
                    source_len, retained);
            }
        }
    } else {
        for (uint32_t i = 0u; i < node->nints; i++) {
            removed += snode_transport_stable_coordinates(
                node->ints[i].child, source_to_target, source_len, retained);
        }
    }
    return removed;
}

/* ── Symbol hash table helpers ─────────────────────────────────────────── */

static inline uint32_t sym_hash(SymbolId key) {
    return (uint32_t)((uint64_t)key * 2654435761u);
}

static bool sym_ht_init(SymHashTable *ht, uint32_t min_cap) {
    uint32_t cap = 32;
    if (min_cap > UINT32_MAX / 2u) {
        fputs("CeTTa: symbol substitution index capacity exhausted\n", stderr);
        abort();
    }
    uint32_t target = min_cap * 2u;
    while (cap < target) {
        if (cap > UINT32_MAX / 2u) {
            fputs("CeTTa: symbol substitution index capacity exhausted\n",
                  stderr);
            abort();
        }
        cap *= 2u;
    }
    SymHashEntry *entries = cetta_malloc(sizeof(SymHashEntry) * cap);
    if (!entries) return false;
    ht->entries = entries;
    ht->mask = cap - 1;
    ht->count = 0;
    for (uint32_t i = 0; i < cap; i++)
        ht->entries[i].key = SYMBOL_ID_NONE;
    return true;
}

static SubstNode *sym_ht_get(SymHashTable *ht, SymbolId key) {
    uint32_t idx = sym_hash(key) & ht->mask;
    for (;;) {
        if (ht->entries[idx].key == key)
            return ht->entries[idx].child;
        if (ht->entries[idx].key == SYMBOL_ID_NONE)
            return NULL;
        idx = (idx + 1) & ht->mask;
    }
}

static bool sym_ht_put(SymHashTable *ht, SymbolId key, SubstNode *child) {
    /* Resize if load factor > 0.7 */
    if ((uint64_t)ht->count * 10u >
        (uint64_t)(ht->mask + 1u) * 7u) {
        uint32_t old_cap = ht->mask + 1;
        SymHashEntry *old = ht->entries;
        if (old_cap > UINT32_MAX / 2u) {
            fputs("CeTTa: symbol substitution index capacity exhausted\n",
                  stderr);
            abort();
        }
        uint32_t new_cap = old_cap * 2;
        SymHashEntry *entries = cetta_malloc(sizeof(SymHashEntry) * new_cap);
        if (!entries) return false;
        ht->entries = entries;
        ht->mask = new_cap - 1;
        for (uint32_t i = 0; i < new_cap; i++)
            ht->entries[i].key = SYMBOL_ID_NONE;
        ht->count = 0;
        for (uint32_t i = 0; i < old_cap; i++) {
            if (old[i].key != SYMBOL_ID_NONE) {
                sym_ht_put(ht, old[i].key, old[i].child);
            }
        }
        free(old);
    }
    uint32_t idx = sym_hash(key) & ht->mask;
    while (ht->entries[idx].key != SYMBOL_ID_NONE) {
        if (ht->entries[idx].key == key) {
            ht->entries[idx].child = child;
            return true;
        }
        idx = (idx + 1) & ht->mask;
    }
    ht->entries[idx].key = key;
    ht->entries[idx].child = child;
    ht->count++;
    return true;
}

static void snode_sym_promote(SubstNode *n) {
    SubstSymBranch *old_sym = n->sym;
    uint32_t count = n->nsym;
    if (!sym_ht_init(&n->sym_ht, count + 16)) return;
    for (uint32_t i = 0; i < count; i++)
        sym_ht_put(&n->sym_ht, old_sym[i].key, old_sym[i].child);
    free(old_sym);
    n->sym = NULL;
    n->csym = 0;
    n->sym_hashed = true;
}

/* ── Int hash table helpers (mirror of the symbol table above) ─────────────
 * The integer-child branch of a node fans out to one child per distinct int
 * key.  A discrimination tree over a family like (friend sam 0..N) piles N
 * distinct ints onto one node; a linear array scan there is O(n) per insert
 * and O(n^2) to build.  Like symbols, promote to an open-addressing table
 * once the fan-out crosses SNODE_HASH_THRESHOLD.  Occupancy is the child
 * pointer (every int64 value is a valid key, so there is no reserved empty
 * key); the full-width finalizer spreads both sequential and high-bit-varying
 * integers before the low mask bits choose a slot. */

static inline uint32_t int_hash(int64_t key) {
    /* Fold a full-width SplitMix64 finalizer.  Truncating the old
       multiplicative hash discarded the high 32 bits entirely, so keys
       separated by 2^32 formed one linear-probe cluster. */
    uint64_t x = (uint64_t)key;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (uint32_t)(x ^ (x >> 32));
}

static bool int_ht_init(IntHashTable *ht, uint32_t min_cap) {
    uint32_t cap = 32;
    if (min_cap > UINT32_MAX / 2u) {
        fputs("CeTTa: integer substitution index capacity exhausted\n", stderr);
        abort();
    }
    uint32_t target = min_cap * 2u;
    while (cap < target) {
        if (cap > UINT32_MAX / 2u) {
            fputs("CeTTa: integer substitution index capacity exhausted\n",
                  stderr);
            abort();
        }
        cap *= 2u;
    }
    IntHashEntry *entries = cetta_malloc(sizeof(IntHashEntry) * cap);
    if (!entries) return false;
    ht->entries = entries;
    ht->mask = cap - 1;
    ht->count = 0;
    for (uint32_t i = 0; i < cap; i++)
        ht->entries[i].child = NULL;
    return true;
}

static SubstNode *int_ht_get(IntHashTable *ht, int64_t key) {
    uint32_t idx = int_hash(key) & ht->mask;
    for (;;) {
        if (!ht->entries[idx].child)
            return NULL;
        if (ht->entries[idx].val == key)
            return ht->entries[idx].child;
        idx = (idx + 1) & ht->mask;
    }
}

static bool int_ht_put(IntHashTable *ht, int64_t key, SubstNode *child) {
    /* Resize if load factor > 0.7 */
    if ((uint64_t)ht->count * 10u >
        (uint64_t)(ht->mask + 1u) * 7u) {
        uint32_t old_cap = ht->mask + 1;
        IntHashEntry *old = ht->entries;
        if (old_cap > UINT32_MAX / 2u) {
            fputs("CeTTa: integer substitution index capacity exhausted\n",
                  stderr);
            abort();
        }
        uint32_t new_cap = old_cap * 2;
        IntHashEntry *entries = cetta_malloc(sizeof(IntHashEntry) * new_cap);
        if (!entries) return false;
        ht->entries = entries;
        ht->mask = new_cap - 1;
        for (uint32_t i = 0; i < new_cap; i++)
            ht->entries[i].child = NULL;
        ht->count = 0;
        for (uint32_t i = 0; i < old_cap; i++) {
            if (old[i].child) {
                int_ht_put(ht, old[i].val, old[i].child);
            }
        }
        free(old);
    }
    uint32_t idx = int_hash(key) & ht->mask;
    while (ht->entries[idx].child) {
        if (ht->entries[idx].val == key) {
            ht->entries[idx].child = child;
            return true;
        }
        idx = (idx + 1) & ht->mask;
    }
    ht->entries[idx].val = key;
    ht->entries[idx].child = child;
    ht->count++;
    return true;
}

static void snode_int_promote(SubstNode *n) {
    uint32_t count = n->nints;
    if (!int_ht_init(&n->int_ht, count + 16)) return;
    for (uint32_t i = 0; i < count; i++)
        int_ht_put(&n->int_ht, n->ints[i].val, n->ints[i].child);
    free(n->ints);
    n->ints = NULL;
    n->cints = 0;
    n->int_hashed = true;
}

/* ── Branch helpers ────────────────────────────────────────────────────── */

static SubstNode *snode_get_sym(SubstNode *n, SymbolId key) {
    if (n->sym_hashed) {
        SubstNode *existing = sym_ht_get(&n->sym_ht, key);
        if (existing) return existing;
        SubstNode *child = snode_new();
        if (!child) return NULL;
        if (!sym_ht_put(&n->sym_ht, key, child)) { snode_free(child); return NULL; }
        n->nsym++;
        return child;
    }
    for (uint32_t i = 0; i < n->nsym; i++)
        if (n->sym[i].key == key)
            return n->sym[i].child;
    if (n->nsym >= n->csym) {
        uint32_t capacity = n->csym ? n->csym * 2u : 4u;
        void *grown = cetta_realloc(n->sym, sizeof(n->sym[0]) * capacity);
        if (!grown) return NULL;
        n->sym = grown; n->csym = capacity;
    }
    SubstNode *child = snode_new();
    if (!child) return NULL;
    n->sym[n->nsym].key = key;
    n->sym[n->nsym].child = child;
    n->nsym++;
    if (n->nsym > SNODE_HASH_THRESHOLD)
        snode_sym_promote(n);
    return child;
}

static SubstNode *snode_get_slot(SubstNode *n, uint32_t edge) {
    for (uint32_t i = 0; i < n->nslots; i++)
        if (n->slots[i].edge == edge) return n->slots[i].child;
    if (n->nslots >= n->cslots) {
        uint32_t capacity = n->cslots ? n->cslots * 2u : 4u;
        void *grown = cetta_realloc(n->slots, sizeof(n->slots[0]) * capacity);
        if (!grown) return NULL;
        n->slots = grown; n->cslots = capacity;
    }
    SubstNode *child = snode_new();
    if (!child) return NULL;
    n->slots[n->nslots].edge = edge;
    n->slots[n->nslots].child = child;
    n->nslots++;
    return child;
}

/* A row's variables by slot: the number of a variable is the order of its
 * first occurrence, in the order the walk reads the row.  A row's first
 * variables are kept inline, so most insertions allocate nothing. */
enum { SNODE_SLOT_NAMES_INLINE = 16u };

typedef struct {
    VarId *vars;            /* inline_vars until a row has more variables */
    uint32_t len, cap;
    VarId inline_vars[SNODE_SLOT_NAMES_INLINE];
} SnodeSlotNames;

static void snode_slot_names_init(SnodeSlotNames *names) {
    names->vars = names->inline_vars;
    names->len = 0u;
    names->cap = SNODE_SLOT_NAMES_INLINE;
}

static void snode_slot_names_free(SnodeSlotNames *names) {
    if (names->vars != names->inline_vars)
        free(names->vars);
    snode_slot_names_init(names);
}

/* The edge of an occurrence of VAR_ID: the slot k again if the row has
 * numbered it k, else a new slot, which numbers it next. */
static bool snode_slot_edge(SnodeSlotNames *names, VarId var_id,
                            uint32_t *edge) {
    for (uint32_t k = 0u; k < names->len; k++) {
        if (var_id_eq(names->vars[k], var_id)) {
            *edge = k + 1u;
            return true;
        }
    }
    if (names->len == names->cap) {
        uint32_t capacity = names->cap * 2u;
        bool inline_names = names->vars == names->inline_vars;
        VarId *grown = cetta_realloc(inline_names ? NULL : names->vars,
                                     sizeof(*grown) * capacity);
        if (!grown)
            return false;
        if (inline_names)
            memcpy(grown, names->inline_vars, sizeof(*grown) * names->len);
        names->vars = grown;
        names->cap = capacity;
    }
    names->vars[names->len++] = var_id;
    *edge = SNODE_SLOT_NEW;
    return true;
}

static SubstNode *snode_get_var(SubstNode *n, SnodeSlotNames *names,
                                VarId var_id) {
    uint32_t edge = SNODE_SLOT_NEW;
    return snode_slot_edge(names, var_id, &edge) ? snode_get_slot(n, edge) : NULL;
}

static SubstNode *snode_get_expr(SubstNode *n, CettaExprLen arity) {
    for (uint32_t i = 0; i < n->nexpr; i++)
        if (n->expr[i].arity == arity) return n->expr[i].child;
    if (n->nexpr >= n->cexpr) {
        uint32_t capacity = n->cexpr ? n->cexpr * 2u : 4u;
        void *grown = cetta_realloc(n->expr, sizeof(n->expr[0]) * capacity);
        if (!grown) return NULL;
        n->expr = grown; n->cexpr = capacity;
    }
    SubstNode *child = snode_new();
    if (!child) return NULL;
    n->expr[n->nexpr].arity = arity;
    n->expr[n->nexpr].child = child;
    n->nexpr++;
    return child;
}

static SubstNode *snode_get_int(SubstNode *n, int64_t val) {
    if (n->int_hashed) {
        SubstNode *existing = int_ht_get(&n->int_ht, val);
        if (existing) return existing;
        SubstNode *child = snode_new();
        if (!child) return NULL;
        if (!int_ht_put(&n->int_ht, val, child)) { snode_free(child); return NULL; }
        n->nints++;
        return child;
    }
    for (uint32_t i = 0; i < n->nints; i++)
        if (n->ints[i].val == val) return n->ints[i].child;
    if (n->nints >= n->cints) {
        uint32_t capacity = n->cints ? n->cints * 2u : 4u;
        void *grown = cetta_realloc(n->ints, sizeof(n->ints[0]) * capacity);
        if (!grown) return NULL;
        n->ints = grown; n->cints = capacity;
    }
    SubstNode *child = snode_new();
    if (!child) return NULL;
    n->ints[n->nints].val = val;
    n->ints[n->nints].child = child;
    n->nints++;
    if (n->nints > SNODE_HASH_THRESHOLD)
        snode_int_promote(n);
    return child;
}

static void snode_add_leaf(SubstNode *n, CettaIndex idx) {
    if (!n) return;
    if (n->nleaves >= n->cleaves) {
        uint32_t capacity = n->cleaves ? n->cleaves * 2u : 4u;
        void *grown = cetta_realloc(n->leaves, sizeof(n->leaves[0]) * capacity);
        if (!grown) return;
        n->leaves = grown; n->cleaves = capacity;
    }
    n->leaves[n->nleaves++] = idx;
}

/* Set once any bigint or rational is filed under its text. */
static _Atomic bool g_st_exact_number_keys;

/* ── Insertion ─────────────────────────────────────────────────────────── */

/* The node for a value this tree does not discriminate: a placeholder edge,
 * which the verifying match, not the tree, decides. */
static SubstNode *snode_get_grounded_placeholder(SubstNode *node) {
    return snode_get_slot(node, SNODE_SLOT_PLACEHOLDER);
}

static SubstNode *snode_insert_atom(SubstNode *node, Atom *a, unsigned depth,
                                    CettaIndexExpansionBudget *budget,
                                    SnodeSlotNames *names) {
    if (!node || !a) return NULL;
    if (a->kind == ATOM_EXPR &&
        !cetta_index_expand_expression(budget, depth, a->expr.len))
        return snode_get_grounded_placeholder(node);
    switch (a->kind) {
    case ATOM_SYMBOL: return snode_get_sym(node, a->sym_id);
    case ATOM_VAR:
        return snode_get_var(node, names, a->var_id);
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT) return snode_get_int(node, a->ground.ival);
        if (a->ground.gkind == GV_STRING || a->ground.gkind == GV_BIGINT ||
            a->ground.gkind == GV_RATIONAL) {
            if (a->ground.gkind != GV_STRING)
                atomic_store_explicit(&g_st_exact_number_keys, true,
                                      memory_order_release);
            SymbolId string_id = a->ground.gkind == GV_STRING
                ? symbol_intern_bytes(g_symbols,
                                      (const uint8_t *)a->ground.sval,
                                      a->ground.slen)
                : symbol_intern_cstr(g_symbols,
                                     a->ground.gkind == GV_BIGINT
                                         ? atom_bigint_cstr(a)
                                         : atom_rational_cstr(a));
            return snode_get_sym(node, string_id);
        }
        return snode_get_grounded_placeholder(node);
    case ATOM_EXPR: {
        /* A list pattern meets lists of many lengths: like an unindexed
         * grounded value it is a placeholder here, and the verifying match
         * applies its rest. */
        if (atom_is_list_rest(a))
            return snode_get_grounded_placeholder(node);
        SubstNode *cur = snode_get_expr(node, a->expr.len);
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            cur = snode_insert_atom(cur, a->expr.elems[i], depth + 1u, budget,
                                    names);
        return cur;
    }
    }
    return node;
}

static SubstNode *snode_insert_atom_id(SubstNode *node,
                                       const TermUniverse *universe,
                                       AtomId atom_id, unsigned depth,
                                       CettaIndexExpansionBudget *budget,
                                       SnodeSlotNames *names) {
    if (!node || !universe || atom_id == CETTA_ATOM_ID_NONE ||
        !tu_hdr(universe, atom_id)) {
        return NULL;
    }

    if (tu_kind(universe, atom_id) == ATOM_EXPR &&
        !cetta_index_expand_expression(budget, depth, tu_arity(universe, atom_id)))
        return snode_get_grounded_placeholder(node);

    switch (tu_kind(universe, atom_id)) {
    case ATOM_SYMBOL:
        return snode_get_sym(node, tu_sym(universe, atom_id));
    case ATOM_VAR:
        return snode_get_var(node, names, tu_var_id(universe, atom_id));
    case ATOM_GROUNDED:
        if (tu_ground_kind(universe, atom_id) == GV_INT)
            return snode_get_int(node, tu_int(universe, atom_id));
        if (tu_ground_kind(universe, atom_id) == GV_STRING ||
            tu_ground_kind(universe, atom_id) == GV_BIGINT ||
            tu_ground_kind(universe, atom_id) == GV_RATIONAL) {
            if (tu_ground_kind(universe, atom_id) != GV_STRING)
                atomic_store_explicit(&g_st_exact_number_keys, true,
                                      memory_order_release);
            /* The key a string has in the atom paths: its bytes, NUL included. */
            SymbolId string_id = tu_ground_kind(universe, atom_id) == GV_STRING
                ? symbol_intern_bytes(
                      g_symbols,
                      (const uint8_t *)tu_string_cstr(universe, atom_id),
                      tu_string_len(universe, atom_id))
                : symbol_intern_cstr(
                      g_symbols,
                      tu_ground_kind(universe, atom_id) == GV_BIGINT
                          ? tu_bigint_cstr(universe, atom_id)
                          : tu_rational_cstr(universe, atom_id));
            return snode_get_sym(node, string_id);
        }
        return snode_get_grounded_placeholder(node);
    case ATOM_EXPR: {
        if (tu_arity(universe, atom_id) > 0u &&
            tu_internal_tag(universe, tu_child(universe, atom_id, 0u)) ==
                CETTA_INTERNAL_TAG_LIST_REST)
            return snode_get_grounded_placeholder(node);
        SubstNode *cur = snode_get_expr(node, tu_arity(universe, atom_id));
        for (CettaExprIndex i = 0; i < tu_arity(universe, atom_id); i++) {
            AtomId child_id = tu_child(universe, atom_id, i);
            cur = snode_insert_atom_id(cur, universe, child_id, depth + 1u,
                                       budget, names);
            if (!cur)
                return NULL;
        }
        return cur;
    }
    }
    return node;
}

/* ── SubstTree (head-partitioned) ──────────────────────────────────────── */

void stree_init(SubstTree *t) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        stree_bucket_init(&t->buckets[i]);
    stree_bucket_init(&t->wildcard);
}

void stree_free(SubstTree *t) {
    for (uint32_t i = 0; i < STREE_BUCKETS; i++)
        stree_bucket_free(&t->buckets[i]);
    stree_bucket_free(&t->wildcard);
}

static CettaCount stree_bucket_transport_stable_coordinates(
        SubstBucket *bucket, const CettaIndex *source_to_target,
        CettaCount source_len) {
    if (!bucket || !bucket->root) {
        if (bucket)
            bucket->count = 0u;
        return 0u;
    }
    CettaCount retained = 0u;
    CettaCount removed = snode_transport_stable_coordinates(
        bucket->root, source_to_target, source_len, &retained);
    bucket->count = retained;
    return removed;
}

CettaCount stree_transport_stable_coordinates(
        SubstTree *tree, const CettaIndex *source_to_target,
        CettaCount source_len) {
    if (!tree || (!source_to_target && source_len != 0u))
        return 0u;
    CettaCount removed = 0u;
    for (uint32_t i = 0u; i < STREE_BUCKETS; i++) {
        removed += stree_bucket_transport_stable_coordinates(
            &tree->buckets[i], source_to_target, source_len);
    }
    removed += stree_bucket_transport_stable_coordinates(
        &tree->wildcard, source_to_target, source_len);
    return removed;
}

uint32_t stree_head_hash(SymbolId id) {
    uint32_t mixed = (uint32_t)((uint64_t)id * 2654435761u);
    return mixed % STREE_BUCKETS;
}

static SymbolId atom_head_sym(Atom *a) {
    if (a->kind == ATOM_SYMBOL) return a->sym_id;
    if (a->kind == ATOM_EXPR && a->expr.len > 0 &&
        a->expr.elems[0]->kind == ATOM_SYMBOL)
        return a->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

void stree_bucket_init(SubstBucket *bucket) {
    bucket->root = NULL;
    bucket->count = 0;
}

void stree_bucket_free(SubstBucket *bucket) {
    snode_free(bucket->root);
    bucket->root = NULL;
    bucket->count = 0;
}

/* A row is filed by its position alone, whether or not it has variables:
 * its consumer renames it apart for each use, so the tree holds no frame
 * identity for it. */
void stree_bucket_insert(SubstBucket *bucket, Atom *atom, CettaIndex atom_idx) {
    if (!bucket->root) bucket->root = snode_new();
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    SnodeSlotNames names;
    snode_slot_names_init(&names);
    SubstNode *leaf = snode_insert_atom(bucket->root, atom, 0u, &budget, &names);
    snode_slot_names_free(&names);
    snode_add_leaf(leaf, atom_idx);
    bucket->count++;
}

bool stree_bucket_insert_id(SubstBucket *bucket, const TermUniverse *universe,
                            AtomId atom_id, CettaIndex atom_idx) {
    if (!bucket || !universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!bucket->root)
        bucket->root = snode_new();
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    SnodeSlotNames names;
    snode_slot_names_init(&names);
    SubstNode *leaf = snode_insert_atom_id(bucket->root, universe, atom_id, 0u,
                                           &budget, &names);
    snode_slot_names_free(&names);
    if (!leaf)
        return false;
    snode_add_leaf(leaf, atom_idx);
    bucket->count++;
    return true;
}

void stree_insert(SubstTree *t, Atom *atom, CettaIndex atom_idx) {
    SymbolId head = atom_head_sym(atom);
    SubstBucket *bucket = head != SYMBOL_ID_NONE
                        ? &t->buckets[stree_head_hash(head)]
                        : &t->wildcard;
    stree_bucket_insert(bucket, atom, atom_idx);
}

bool stree_insert_id(SubstTree *t, const TermUniverse *universe,
                     AtomId atom_id, CettaIndex atom_idx) {
    if (!t || !universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    SymbolId head = tu_head_sym(universe, atom_id);
    SubstBucket *bucket = head != SYMBOL_ID_NONE
                        ? &t->buckets[stree_head_hash(head)]
                        : &t->wildcard;
    return stree_bucket_insert_id(bucket, universe, atom_id, atom_idx);
}

/* The leaf node of an atom already in the tree: the insertion walk, which
 * finds every node of an indexed atom and allocates none. */
static SubstNode *stree_leaf_node_id(SubstTree *t, const TermUniverse *universe,
                                     AtomId atom_id, SubstBucket **bucket_out) {
    if (!t || !universe || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    SymbolId head = tu_head_sym(universe, atom_id);
    SubstBucket *bucket = head != SYMBOL_ID_NONE
                        ? &t->buckets[stree_head_hash(head)]
                        : &t->wildcard;
    if (!bucket->root)
        return NULL;
    *bucket_out = bucket;
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    SnodeSlotNames names;
    snode_slot_names_init(&names);
    SubstNode *leaf = snode_insert_atom_id(bucket->root, universe, atom_id, 0u,
                                           &budget, &names);
    snode_slot_names_free(&names);
    return leaf;
}

bool stree_remove_id(SubstTree *t, const TermUniverse *universe,
                     AtomId atom_id, CettaIndex idx) {
    SubstBucket *bucket = NULL;
    SubstNode *leaf = stree_leaf_node_id(t, universe, atom_id, &bucket);
    for (CettaIndex i = 0u; leaf && i < leaf->nleaves; i++) {
        if (leaf->leaves[i] != idx)
            continue;
        leaf->leaves[i] = leaf->leaves[--leaf->nleaves];
        bucket->count--;
        return true;
    }
    return false;
}

bool stree_relabel_id(SubstTree *t, const TermUniverse *universe,
                      AtomId atom_id, CettaIndex from, CettaIndex to) {
    SubstBucket *bucket = NULL;
    SubstNode *leaf = stree_leaf_node_id(t, universe, atom_id, &bucket);
    for (CettaIndex i = 0u; leaf && i < leaf->nleaves; i++) {
        if (leaf->leaves[i] != from)
            continue;
        leaf->leaves[i] = to;
        return true;
    }
    return false;
}

/* ── Result Set ────────────────────────────────────────────────────────── */

void smset_init(SubstMatchSet *s) {
    memset(s->inline_items, 0, sizeof(s->inline_items));
    s->items = s->inline_items;
    s->len = 0;
    s->cap = 1;
}

void smset_free(SubstMatchSet *s) {
    for (CettaIndex i = 0; i < s->len; i++) {
        if (s->items[i].owns_identity)
            cetta_frame_identity_release(s->items[i].epoch);
        bindings_free(&s->items[i].bindings);
    }
    if (s->items != s->inline_items)
        free(s->items);
    memset(s->inline_items, 0, sizeof(s->inline_items));
    s->items = s->inline_items;
    s->len = 0;
    s->cap = 1;
}

/* ── Retrieval ─────────────────────────────────────────────────────────── *
 *
 * The walk follows the flattened query through the tree.  A symbol, an
 * integer or an arity must agree with the indexed term; a query variable
 * skips one indexed subterm and keeps nothing; a placeholder meets any query
 * subterm.  A new slot keeps the query subterm it meets, and the slot met
 * again unifies that subterm with the one it meets now, in a table of the
 * query's own variables: every equation the walk imposes holds under any
 * unifier of the query with the row renamed apart, so the walk loses no row
 * that unifies.  The leaves the walk reaches are the selected rows.  The
 * table only decides the walk: its bindings never leave it, since every
 * consumer matches each selected row again, renamed apart for that use (the
 * complete check, occurs included).                                      */

/* The positions a walk selects: the caller's growable vector. */
typedef struct {
    CettaIndex **items;
    CettaIndex *len;
    CettaIndex *cap;
} StreeRows;

/* Flat token for depth-first query encoding */
typedef struct {
    enum { FT_SYM, FT_VAR, FT_EXPR, FT_INT, FT_GROUNDED_OTHER } kind;
    union { SymbolId sym_id; CettaExprLen arity; int64_t ival; };
    VarId var_id;
    Atom *original;
} FlatToken;

/* A walk: the flattened query, the query subterm each slot numbered on the
 * current path first met (NULL where it met none, under a query variable),
 * and the table that unifies what a slot meets again, which holds the
 * query's variables only. */
enum { STREE_WALK_INLINE_SLOTS = 16u };

typedef struct {
    FlatToken *flat;
    CettaIndex nflat;
    Atom **slot_value;      /* slot_inline until a path numbers more slots */
    uint32_t nslot, cslot;
    Atom *slot_inline[STREE_WALK_INLINE_SLOTS];
    BindingsBuilder bb;
    Arena *a;
    StreeRows out;
} StreeWalk;

/* Forward declarations for mutual recursion */
static void st_flat_walk(SubstNode *node, CettaIndex idx, StreeWalk *w);
static void st_flat_skip(SubstNode *node, CettaIndex remaining,
                         CettaIndex resume_idx, StreeWalk *w);

/* Collect leaves: the rows filed under a node the whole query reached. */
static void st_collect(SubstNode *node, StreeRows *out) {
    if (node->nleaves == 0u)
        return;
    if (node->nleaves > *out->cap - *out->len) {
        CettaIndex need = *out->len + node->nleaves;
        CettaIndex next_cap = *out->cap ? *out->cap : 16u;
        while (next_cap < need)
            next_cap *= 2u;
        size_t next_bytes = sizeof(CettaIndex) * (size_t)next_cap;
        CettaIndex *next = cetta_realloc(*out->items, next_bytes);
        if (!next)
            return;
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_QUERY_SUBST_MATCHSET_HEAP_BYTES,
            (uint64_t)next_bytes);
        *out->items = next;
        *out->cap = next_cap;
    }
    memcpy(*out->items + *out->len, node->leaves,
           sizeof(CettaIndex) * (size_t)node->nleaves);
    *out->len += node->nleaves;
}

/* ── Flat-sequence retrieval ──────────────────────────────────────────── */

static bool query_within_budget(const Atom *query, unsigned depth,
                                 CettaIndexExpansionBudget *budget) {
    if (!query)
        return false;
    if (query->kind != ATOM_EXPR || atom_is_list_rest(query))
        return true;
    if (!cetta_index_expand_expression(budget, depth, query->expr.len))
        return false;
    for (CettaExprIndex i = 0u; i < query->expr.len; i++)
        if (!query_within_budget(query->expr.elems[i], depth + 1u, budget))
            return false;
    return true;
}

bool stree_query_within_expansion_budget(const Atom *query) {
    CettaIndexExpansionBudget budget = cetta_index_expansion_budget();
    return query_within_budget(query, 0u, &budget);
}

static bool flat_token_count(Atom *a, CettaIndex *count) {
    if (!a || !count)
        return false;
    if (*count == UINT64_MAX)
        return false;
    (*count)++;
    if (a->kind != ATOM_EXPR || atom_is_list_rest(a))
        return true;
    for (CettaExprIndex i = 0; i < a->expr.len; i++) {
        if (!flat_token_count(a->expr.elems[i], count))
            return false;
    }
    return true;
}

/* Flatten query atom depth-first into tokens */
static CettaIndex flatten_atom(Atom *a, FlatToken *buf, CettaIndex pos) {
    switch (a->kind) {
    case ATOM_SYMBOL:
        buf[pos] = (FlatToken){.kind = FT_SYM, .sym_id = a->sym_id, .original = a};
        return pos + 1;
    case ATOM_VAR:
        buf[pos] = (FlatToken){.kind = FT_VAR, .sym_id = a->sym_id,
                               .var_id = a->var_id, .original = a};
        return pos + 1;
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT) {
            buf[pos] = (FlatToken){.kind = FT_INT, .ival = a->ground.ival, .original = a};
            return pos + 1;
        }
        if (a->ground.gkind == GV_STRING || a->ground.gkind == GV_BIGINT ||
            a->ground.gkind == GV_RATIONAL) {
            SymbolId string_id = a->ground.gkind == GV_STRING
                ? symbol_intern_bytes(g_symbols,
                                      (const uint8_t *)a->ground.sval,
                                      a->ground.slen)
                : symbol_intern_cstr(g_symbols,
                                     a->ground.gkind == GV_BIGINT
                                         ? atom_bigint_cstr(a)
                                         : atom_rational_cstr(a));
            buf[pos] = (FlatToken){.kind = FT_SYM, .sym_id = string_id, .original = a};
            return pos + 1;
        }
        buf[pos] = (FlatToken){.kind = FT_GROUNDED_OTHER, .original = a};
        return pos + 1;
    case ATOM_EXPR:
        /* A list pattern in a query may meet any indexed term, as a query
         * variable does; the verifying match applies its rest. */
        if (atom_is_list_rest(a)) {
            buf[pos] = (FlatToken){.kind = FT_VAR, .var_id = VAR_ID_NONE,
                                   .original = a};
            return pos + 1;
        }
        buf[pos] = (FlatToken){.kind = FT_EXPR, .arity = a->expr.len, .original = a};
        pos++;
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            pos = flatten_atom(a->expr.elems[i], buf, pos);
        return pos;
    }
    return pos;
}

/* Float facts sit on a placeholder edge.  An integer fact sits on its
 * integer branch, so an HE float query also follows every integer key
 * that promotes equal to it.  PeTTa stays on the placeholder edge only. */
static void st_follow_he_float_ints(SubstNode *node, double fval,
                                    CettaIndex idx, StreeWalk *w) {
    int64_t exact = 0;
    CettaHeFloatIntBranches mode = cetta_he_float_int_branches(fval, &exact);
    if (!node || mode == CETTA_HE_FLOAT_INTS_NONE)
        return;
    if (mode == CETTA_HE_FLOAT_INTS_ONE) {
        SubstNode *match = NULL;
        if (node->int_hashed) {
            match = int_ht_get(&node->int_ht, exact);
        } else {
            for (uint32_t i = 0; i < node->nints; i++) {
                if (node->ints[i].val == exact) {
                    match = node->ints[i].child;
                    break;
                }
            }
        }
        if (match)
            st_flat_walk(match, idx + 1, w);
        return;
    }
    if (node->int_hashed) {
        uint32_t cap = node->int_ht.mask + 1u;
        for (uint32_t i = 0u; i < cap; i++) {
            SubstNode *child = node->int_ht.entries[i].child;
            if (!child)
                continue;
            if (cetta_he_promoted_kind_equal(
                    GV_FLOAT, 0, fval, GV_INT,
                    node->int_ht.entries[i].val, 0.0))
                st_flat_walk(child, idx + 1, w);
        }
        return;
    }
    for (uint32_t i = 0u; i < node->nints; i++) {
        if (cetta_he_promoted_kind_equal(
                GV_FLOAT, 0, fval, GV_INT, node->ints[i].val, 0.0))
            st_flat_walk(node->ints[i].child, idx + 1, w);
    }
}

/* A bigint or rational fact sits on the symbol branch of its text, so an HE
 * float query also follows the branch of its own exact value, if one exists. */
static void st_follow_he_float_exact_key(SubstNode *node, double fval,
                                         CettaIndex idx, StreeWalk *w) {
    if (!atomic_load_explicit(&g_st_exact_number_keys, memory_order_acquire) ||
        (node->nsym == 0u && !node->sym_hashed))
        return;
    char *text = cetta_he_float_exact_key_text(fval);
    if (!text)
        return;
    SymbolId key = symbol_lookup_cstr(g_symbols, text);
    free(text);
    if (key == SYMBOL_ID_NONE)
        return;
    if (node->sym_hashed) {
        SubstNode *match = sym_ht_get(&node->sym_ht, key);
        if (match)
            st_flat_walk(match, idx + 1, w);
        return;
    }
    for (uint32_t i = 0; i < node->nsym; i++)
        if (node->sym[i].key == key)
            st_flat_walk(node->sym[i].child, idx + 1, w);
}

/* Number the next slot on this path, which first met VALUE. */
static bool st_push_slot(StreeWalk *w, Atom *value) {
    if (w->nslot == w->cslot) {
        uint32_t capacity = w->cslot * 2u;
        bool inline_table = w->slot_value == w->slot_inline;
        Atom **grown = cetta_realloc(inline_table ? NULL : w->slot_value,
                                     sizeof(*grown) * capacity);
        if (!grown)
            return false;
        if (inline_table)
            memcpy(grown, w->slot_inline, sizeof(*grown) * w->nslot);
        w->slot_value = grown;
        w->cslot = capacity;
    }
    w->slot_value[w->nslot++] = value;
    return true;
}

/* Follow the slot edge EDGE to CHILD where it meets VALUE, the query subterm
 * at this position (NULL under a query variable), and walk on from NEXT. */
static void st_follow_slot(SubstNode *child, uint32_t edge, Atom *value,
                           CettaIndex next, StreeWalk *w) {
    if (edge == SNODE_SLOT_PLACEHOLDER) {
        st_flat_walk(child, next, w);
        return;
    }
    if (edge == SNODE_SLOT_NEW) {
        if (!st_push_slot(w, value))
            return;
        st_flat_walk(child, next, w);
        w->nslot--;
        return;
    }
    uint32_t k = edge - 1u;
    if (k >= w->nslot)
        return;
    Atom *first = w->slot_value[k];
    if (!first || !value) {
        /* Nothing to compare yet: the slot keeps what it meets first. */
        w->slot_value[k] = first ? first : value;
        st_flat_walk(child, next, w);
        w->slot_value[k] = first;
        return;
    }
#ifdef STREE_MUTANT_IGNORE_REPEATED_SLOTS
    /* The unit test's mutant: a walk that never compares a repeated slot
     * selects every row that unifies and more; the test must tell. */
    st_flat_walk(child, next, w);
#else
    uint32_t mark = bindings_builder_save(&w->bb);
    if (match_atoms_builder(first, value, &w->bb, w->a))
        st_flat_walk(child, next, w);
    bindings_builder_rollback(&w->bb, mark);
#endif
}

/* The index of the token after the subterm that starts at IDX. */
static CettaIndex st_subterm_end(const StreeWalk *w, CettaIndex idx) {
    CettaIndex end = idx + 1;
    CettaIndex depth = w->flat[idx].kind == FT_EXPR ? w->flat[idx].arity : 0u;
    while (depth > 0 && end < w->nflat) {
        if (w->flat[end].kind == FT_EXPR) depth += w->flat[end].arity;
        depth--;
        end++;
    }
    return end;
}

/* Walk the flat query through the tree from IDX.  When IDX reaches the end,
 * the whole query has met the node: collect its leaves. */
static void st_flat_walk(SubstNode *node, CettaIndex idx, StreeWalk *w) {
    if (!node) return;
    if (idx == w->nflat) {
        st_collect(node, &w->out);
        return;
    }

    FlatToken *tok = &w->flat[idx];

    switch (tok->kind) {
    case FT_SYM:
        if (node->sym_hashed) {
            SubstNode *match = sym_ht_get(&node->sym_ht, tok->sym_id);
            if (match)
                st_flat_walk(match, idx + 1, w);
        } else {
            for (uint32_t i = 0; i < node->nsym; i++)
                if (node->sym[i].key == tok->sym_id)
                    st_flat_walk(node->sym[i].child, idx + 1, w);
        }
        for (uint32_t i = 0; i < node->nslots; i++)
            st_follow_slot(node->slots[i].child, node->slots[i].edge,
                           tok->original, idx + 1, w);
        break;

    case FT_VAR:
        /* A query variable meets any indexed subterm: skip one, keep
         * nothing, and resume the walk at idx+1. */
        if (node->sym_hashed) {
            uint32_t cap = node->sym_ht.mask + 1;
            for (uint32_t i = 0; i < cap; i++)
                if (node->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                    st_flat_walk(node->sym_ht.entries[i].child, idx + 1, w);
        } else {
            for (uint32_t i = 0; i < node->nsym; i++)
                st_flat_walk(node->sym[i].child, idx + 1, w);
        }
        for (uint32_t i = 0; i < node->nslots; i++)
            st_follow_slot(node->slots[i].child, node->slots[i].edge,
                           NULL, idx + 1, w);
        if (node->int_hashed) {
            uint32_t cap = node->int_ht.mask + 1;
            for (uint32_t i = 0; i < cap; i++)
                if (node->int_ht.entries[i].child)
                    st_flat_walk(node->int_ht.entries[i].child, idx + 1, w);
        } else {
            for (uint32_t i = 0; i < node->nints; i++)
                st_flat_walk(node->ints[i].child, idx + 1, w);
        }
        /* An expression: skip its arity's subterms after the arity branch. */
        for (uint32_t i = 0; i < node->nexpr; i++)
            st_flat_skip(node->expr[i].child, node->expr[i].arity, idx + 1, w);
        break;

    case FT_INT:
        if (node->int_hashed) {
            SubstNode *match = int_ht_get(&node->int_ht, tok->ival);
            if (match)
                st_flat_walk(match, idx + 1, w);
        } else {
            for (uint32_t i = 0; i < node->nints; i++)
                if (node->ints[i].val == tok->ival)
                    st_flat_walk(node->ints[i].child, idx + 1, w);
        }
        for (uint32_t i = 0; i < node->nslots; i++)
            st_follow_slot(node->slots[i].child, node->slots[i].edge,
                           tok->original, idx + 1, w);
        break;

    case FT_EXPR:
        /* The arity must agree; its subterms follow in the flat sequence. */
        for (uint32_t i = 0; i < node->nexpr; i++)
            if (node->expr[i].arity == tok->arity)
                st_flat_walk(node->expr[i].child, idx + 1, w);
        /* A slot meets the whole expression: skip its tokens. */
        if (node->nslots) {
            CettaIndex end = st_subterm_end(w, idx);
            for (uint32_t i = 0; i < node->nslots; i++)
                st_follow_slot(node->slots[i].child, node->slots[i].edge,
                               tok->original, end, w);
        }
        break;

    case FT_GROUNDED_OTHER:
        /* A value the tree does not discriminate meets the slots. */
        for (uint32_t i = 0; i < node->nslots; i++)
            st_follow_slot(node->slots[i].child, node->slots[i].edge,
                           tok->original, idx + 1, w);
        if (tok->original &&
            tok->original->kind == ATOM_GROUNDED &&
            tok->original->ground.gkind == GV_FLOAT)
        {
            st_follow_he_float_ints(node, tok->original->ground.fval, idx, w);
            st_follow_he_float_exact_key(node, tok->original->ground.fval,
                                         idx, w);
        }
        break;
    }
}

/* Skip REMAINING indexed subterms under a query variable, then resume the
 * walk at RESUME_IDX.  Slots numbered inside them met no query subterm. */
static void st_flat_skip(SubstNode *node, CettaIndex remaining,
                         CettaIndex resume_idx, StreeWalk *w) {
    if (!node) return;
    if (remaining == 0) {
        st_flat_walk(node, resume_idx, w);
        return;
    }
    if (node->sym_hashed) {
        uint32_t cap = node->sym_ht.mask + 1;
        for (uint32_t i = 0; i < cap; i++)
            if (node->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                st_flat_skip(node->sym_ht.entries[i].child, remaining - 1,
                             resume_idx, w);
    } else {
        for (uint32_t i = 0; i < node->nsym; i++)
            st_flat_skip(node->sym[i].child, remaining - 1, resume_idx, w);
    }
    for (uint32_t i = 0; i < node->nslots; i++) {
        if (node->slots[i].edge == SNODE_SLOT_NEW) {
            if (!st_push_slot(w, NULL))
                continue;
            st_flat_skip(node->slots[i].child, remaining - 1, resume_idx, w);
            w->nslot--;
        } else {
            st_flat_skip(node->slots[i].child, remaining - 1, resume_idx, w);
        }
    }
    if (node->int_hashed) {
        uint32_t cap = node->int_ht.mask + 1;
        for (uint32_t i = 0; i < cap; i++)
            if (node->int_ht.entries[i].child)
                st_flat_skip(node->int_ht.entries[i].child, remaining - 1,
                             resume_idx, w);
    } else {
        for (uint32_t i = 0; i < node->nints; i++)
            st_flat_skip(node->ints[i].child, remaining - 1, resume_idx, w);
    }
    for (uint32_t i = 0; i < node->nexpr; i++)
        st_flat_skip(node->expr[i].child, remaining - 1 + node->expr[i].arity,
                     resume_idx, w);
}

/* ── Bucket-level query ────────────────────────────────────────────────── */

void stree_query_bucket(SubstBucket *bucket, Arena *a, Atom *query,
                        CettaIndex **rows, CettaIndex *nrows,
                        CettaIndex *crows) {
    if (!bucket->root || bucket->count == 0) return;
    CettaIndex nflat = 0;
    if (!flat_token_count(query, &nflat) || nflat == 0)
        return;
    size_t flat_bytes = sizeof(FlatToken) * (size_t)(nflat ? nflat : 1);
    FlatToken *flat = cetta_malloc(flat_bytes);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_SUBST_FLAT_HEAP_BYTES,
                            (uint64_t)flat_bytes);
    flatten_atom(query, flat, 0);
    StreeWalk w = {
        .flat = flat,
        .nflat = nflat,
        .cslot = STREE_WALK_INLINE_SLOTS,
        .a = a,
        .out = {.items = rows, .len = nrows, .cap = crows},
    };
    w.slot_value = w.slot_inline;
    if (!bindings_builder_init(&w.bb, NULL)) {
        free(flat);
        return;
    }
    st_flat_walk(bucket->root, 0, &w);
    bindings_builder_free(&w.bb);
    if (w.slot_value != w.slot_inline)
        free(w.slot_value);
    free(flat);
}
