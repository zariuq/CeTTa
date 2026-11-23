#include "space_alias.h"
#include "petta_semantics.h"
#include "rule_binding.h"

#include <stdlib.h>
#include <string.h>

/* ── Internal Hash Maps & Table Representation ──────────────────────────── */

typedef struct {
    SymbolId key;
    AtomId rep;
} SymAliasSlot;

typedef struct {
    AtomId key;
    AtomId rep;
} AtomAliasSlot;

typedef struct {
    SymbolId key;
    uint32_t offset;
    uint32_t count;
} SymClassSlot;

typedef struct {
    SymbolId key;
    uint8_t state; /* 0 = empty, 1 = unaffected, 2 = affected */
} HeadAffectedSlot;

/* A member's class: its slice of `class_members`. */
typedef struct {
    AtomId key;
    uint32_t offset;
    uint32_t count;
} ClassSlot;

typedef struct {
    uint32_t offset;
    uint32_t count;
} SymClassRange;

typedef struct SpaceAliasTable {
    uint64_t built_global_equational_epoch;
    uint64_t built_equational_revision;
    uint64_t built_overlay_equational_epoch;
    uint64_t built_module_dep_epoch;
    uint64_t built_universe_instance;
    uint64_t built_universe_storage_epoch;
    size_t active_edge_count;
    bool has_non_symbol_leaf_alias;
    /* Every edge joins two atoms (symbols or grounded leaves): the quotient
     * is the free term algebra over the classes of atoms, so two terms are
     * equal exactly when one is the other with members swapped for members
     * of their classes. */
    bool atomic_only;

    /* Set of all SymbolIds appearing anywhere in non-reflexive `=e` edges. */
    SymbolId *carrier_syms;
    uint32_t carrier_sym_cap;

    /* Direct SymbolId -> canonical representative AtomId (for aliased symbols). */
    SymAliasSlot *sym_alias;
    uint32_t sym_alias_cap;

    /* Full ground AtomId -> canonical representative AtomId. */
    AtomAliasSlot *atom_alias;
    uint32_t atom_alias_cap;

    /* Equivalence classes of symbols (for aliased rule heads). */
    SymClassSlot *sym_class_slots;
    uint32_t sym_class_cap;
    SymbolId *sym_class_members;
    uint32_t sym_class_members_len;
    SymClassRange *sym_class_ranges;
    uint32_t sym_class_range_len;

    /* Lazy cache for space_alias_head_affected(). */
    uint64_t head_cache_eq_rev;
    uint64_t head_cache_overlay_dep;
    HeadAffectedSlot *head_affected;
    uint32_t head_affected_cap;
    uint32_t head_affected_used;

    /* Every class of two or more members: the printed member first, then the
     * others by structural size and the standard order of terms.  Any
     * member's AtomId finds its class's slice. */
    ClassSlot *class_slots;
    uint32_t class_cap;
    AtomId *class_members;
    uint32_t class_members_len;

    /* Suffix-tracked equation collection cache owned by SpaceTheoryView. */
    SpaceTheoryEquationCache equation_cache;
} SpaceAliasTable;

static inline uint32_t alias_mix_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline uint32_t alias_mix_u64(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return (uint32_t)x;
}

static uint32_t next_pow2_u32(size_t need) {
    uint32_t cap = 16u;
    while (cap < need * 2u && cap < (1u << 30))
        cap <<= 1u;
    return cap;
}

static void *alias_calloc(size_t count, size_t elem_size) {
    size_t bytes = count * elem_size;
    void *ptr = cetta_malloc(bytes ? bytes : 1u);
    if (bytes)
        memset(ptr, 0, bytes);
    return ptr;
}

static void space_alias_clear_head_cache(SpaceAliasTable *t) {
    free(t->head_affected);
    t->head_affected = NULL;
    t->head_affected_cap = 0u;
    t->head_affected_used = 0u;
}

static void space_alias_clear_maps(SpaceAliasTable *t) {
    free(t->carrier_syms);
    free(t->sym_alias);
    free(t->atom_alias);
    free(t->sym_class_slots);
    free(t->sym_class_members);
    free(t->sym_class_ranges);
    free(t->class_slots);
    free(t->class_members);
    t->class_slots = NULL;
    t->class_cap = 0u;
    t->class_members = NULL;
    t->class_members_len = 0u;
    t->atomic_only = false;
    t->carrier_syms = NULL;
    t->carrier_sym_cap = 0u;
    t->sym_alias = NULL;
    t->sym_alias_cap = 0u;
    t->atom_alias = NULL;
    t->atom_alias_cap = 0u;
    t->sym_class_slots = NULL;
    t->sym_class_cap = 0u;
    t->sym_class_members = NULL;
    t->sym_class_members_len = 0u;
    t->sym_class_ranges = NULL;
    t->sym_class_range_len = 0u;
    t->active_edge_count = 0u;
    t->has_non_symbol_leaf_alias = false;
    space_alias_clear_head_cache(t);
}

void space_alias_table_free(SpaceAliasTable *table) {
    if (!table)
        return;
    space_alias_clear_maps(table);
    space_theory_equation_cache_free(&table->equation_cache);
    free(table);
}

/* Compute an overlay-and-dependency equational stamp so overlays detect `=e`
 * changes in any base layer or imported module. */
static uint64_t space_alias_overlay_equational_stamp(const Space *s) {
    if (!s || (!s->overlay_base && s->dep_count == 0u))
        return 0u;
    uint64_t h = UINT64_C(0x9e3779b97f4a7c15);
    const Space *slow = s->overlay_base;
    const Space *fast = slow;
    while (fast && fast->overlay_base) {
        slow = slow->overlay_base;
        fast = fast->overlay_base->overlay_base;
        if (slow == fast)
            return UINT64_MAX;
    }
    for (const Space *layer = s->overlay_base; layer; layer = layer->overlay_base) {
        h ^= space_equational_revision(layer) + UINT64_C(0x9e3779b97f4a7c15) +
             (h << 6) + (h >> 2);
        h ^= space_module_dependency_epoch(layer) + UINT64_C(0x517cc1b727220a95) +
             (h << 6) + (h >> 2);
        if (layer->module_own_view)
            break;
    }
    uint32_t dep_count = space_dependency_count(s);
    for (uint32_t d = 0u; d < dep_count; d++) {
        const Space *dep = space_dependency_at(s, d);
        if (dep) {
            h ^= space_equational_revision(dep) + UINT64_C(0x9e3779b97f4a7c15) +
                 (h << 6) + (h >> 2);
        }
    }
    return h;
}

/* ── Carrier & Map Lookups ──────────────────────────────────────────────── */

static bool carrier_sym_contains(const SpaceAliasTable *t, SymbolId sym) {
    if (!t || !t->carrier_syms || sym == SYMBOL_ID_NONE)
        return false;
    uint32_t mask = t->carrier_sym_cap - 1u;
    for (uint32_t slot = alias_mix_u32(sym) & mask;; slot = (slot + 1u) & mask) {
        SymbolId cur = t->carrier_syms[slot];
        if (cur == sym)
            return true;
        if (cur == SYMBOL_ID_NONE)
            return false;
    }
}

static void carrier_sym_insert(SymbolId *slots, uint32_t cap, SymbolId sym) {
    if (sym == SYMBOL_ID_NONE)
        return;
    uint32_t mask = cap - 1u;
    for (uint32_t slot = alias_mix_u32(sym) & mask;; slot = (slot + 1u) & mask) {
        if (slots[slot] == sym)
            return;
        if (slots[slot] == SYMBOL_ID_NONE) {
            slots[slot] = sym;
            return;
        }
    }
}

static AtomId sym_alias_find(const SpaceAliasTable *t, SymbolId sym) {
    if (!t || !t->sym_alias || sym == SYMBOL_ID_NONE)
        return CETTA_ATOM_ID_NONE;
    uint32_t mask = t->sym_alias_cap - 1u;
    for (uint32_t slot = alias_mix_u32(sym) & mask;; slot = (slot + 1u) & mask) {
        if (t->sym_alias[slot].key == sym)
            return t->sym_alias[slot].rep;
        if (t->sym_alias[slot].key == SYMBOL_ID_NONE)
            return CETTA_ATOM_ID_NONE;
    }
}

static void sym_alias_insert(SymAliasSlot *slots, uint32_t cap,
                             SymbolId sym, AtomId rep) {
    uint32_t mask = cap - 1u;
    for (uint32_t slot = alias_mix_u32(sym) & mask;; slot = (slot + 1u) & mask) {
        if (slots[slot].key == sym || slots[slot].key == SYMBOL_ID_NONE) {
            slots[slot].key = sym;
            slots[slot].rep = rep;
            return;
        }
    }
}

static AtomId atom_alias_find(const SpaceAliasTable *t, AtomId id) {
    if (!t || !t->atom_alias || id == CETTA_ATOM_ID_NONE)
        return CETTA_ATOM_ID_NONE;
    uint32_t mask = t->atom_alias_cap - 1u;
    for (uint32_t slot = alias_mix_u64(id) & mask;; slot = (slot + 1u) & mask) {
        if (t->atom_alias[slot].key == id)
            return t->atom_alias[slot].rep;
        if (t->atom_alias[slot].key == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
    }
}

static void atom_alias_insert(AtomAliasSlot *slots, uint32_t cap,
                              AtomId key, AtomId rep) {
    if (key == CETTA_ATOM_ID_NONE)
        return;
    uint32_t mask = cap - 1u;
    for (uint32_t slot = alias_mix_u64(key) & mask;; slot = (slot + 1u) & mask) {
        if (slots[slot].key == key || slots[slot].key == CETTA_ATOM_ID_NONE) {
            slots[slot].key = key;
            slots[slot].rep = rep;
            return;
        }
    }
}

static const SymClassSlot *sym_class_find(const SpaceAliasTable *t,
                                          SymbolId sym) {
    if (!t || !t->sym_class_slots || sym == SYMBOL_ID_NONE)
        return NULL;
    uint32_t mask = t->sym_class_cap - 1u;
    for (uint32_t slot = alias_mix_u32(sym) & mask;; slot = (slot + 1u) & mask) {
        if (t->sym_class_slots[slot].key == sym)
            return &t->sym_class_slots[slot];
        if (t->sym_class_slots[slot].key == SYMBOL_ID_NONE)
            return NULL;
    }
}

static const ClassSlot *class_find(const SpaceAliasTable *t, AtomId id) {
    if (!t || !t->class_slots || id == CETTA_ATOM_ID_NONE)
        return NULL;
    uint32_t mask = t->class_cap - 1u;
    for (uint32_t slot = alias_mix_u64(id) & mask;; slot = (slot + 1u) & mask) {
        if (t->class_slots[slot].key == id)
            return &t->class_slots[slot];
        if (t->class_slots[slot].key == CETTA_ATOM_ID_NONE)
            return NULL;
    }
}

/* ── Union-Find & Congruence Closure Builder ────────────────────────────── */

typedef struct {
    AtomId id;
    uint32_t parent;
    uint32_t size;       /* structural node count for size-minimizing rep */
    uint32_t arity;      /* 0 for leaf, expr.len for ATOM_EXPR */
    uint32_t *children;  /* node indices of expr children */
    SymbolId sym;        /* SYMBOL_ID_NONE unless ATOM_SYMBOL */
    bool is_grounded;
} AliasNode;

typedef struct {
    AtomId key;
    uint32_t node_idx; /* +1 encoded so 0 means empty */
} BuildNodeMapSlot;

typedef struct {
    TermUniverse *universe;
    AliasNode *nodes;
    uint32_t node_len;
    uint32_t node_cap;
    BuildNodeMapSlot *map;
    uint32_t map_cap;
} AliasBuilder;

static void alias_builder_free(AliasBuilder *b) {
    for (uint32_t i = 0u; i < b->node_len; i++)
        free(b->nodes[i].children);
    free(b->nodes);
    free(b->map);
}

static uint32_t alias_uf_find(AliasNode *nodes, uint32_t x) {
    while (nodes[x].parent != x) {
        nodes[x].parent = nodes[nodes[x].parent].parent;
        x = nodes[x].parent;
    }
    return x;
}

static bool alias_uf_union(AliasNode *nodes, uint32_t a, uint32_t b) {
    a = alias_uf_find(nodes, a);
    b = alias_uf_find(nodes, b);
    if (a == b)
        return false;
    if (a < b)
        nodes[b].parent = a;
    else
        nodes[a].parent = b;
    return true;
}

static bool alias_builder_map_grow(AliasBuilder *b) {
    uint32_t next_cap = b->map_cap ? b->map_cap * 2u : 64u;
    BuildNodeMapSlot *next = alias_calloc(next_cap, sizeof(*next));
    if (!next)
        return false;
    uint32_t mask = next_cap - 1u;
    for (uint32_t i = 0u; i < b->map_cap; i++) {
        if (b->map[i].node_idx == 0u)
            continue;
        for (uint32_t slot = alias_mix_u64(b->map[i].key) & mask;;
             slot = (slot + 1u) & mask) {
            if (next[slot].node_idx == 0u) {
                next[slot] = b->map[i];
                break;
            }
        }
    }
    free(b->map);
    b->map = next;
    b->map_cap = next_cap;
    return true;
}

static uint32_t alias_builder_find_id(const AliasBuilder *b, AtomId id) {
    if (!b->map || id == CETTA_ATOM_ID_NONE)
        return UINT32_MAX;
    uint32_t mask = b->map_cap - 1u;
    for (uint32_t slot = alias_mix_u64(id) & mask;; slot = (slot + 1u) & mask) {
        if (b->map[slot].node_idx == 0u)
            return UINT32_MAX;
        if (b->map[slot].key == id)
            return b->map[slot].node_idx - 1u;
    }
}

static uint32_t alias_builder_intern_term(AliasBuilder *b, Atom *atom,
                                          uint32_t depth) {
    if (!atom || depth > 64u)
        return UINT32_MAX;
    AtomId id = term_universe_store_atom_id(b->universe, NULL, atom);
    if (id == CETTA_ATOM_ID_NONE)
        return UINT32_MAX;
    uint32_t existing = alias_builder_find_id(b, id);
    if (existing != UINT32_MAX)
        return existing;

    uint32_t arity = 0u;
    uint32_t *children = NULL;
    uint32_t size = 1u;
    if (atom->kind == ATOM_EXPR && atom->expr.len > 0u) {
        arity = (uint32_t)atom->expr.len;
        children = cetta_malloc(sizeof(*children) * (size_t)arity);
        if (!children)
            return UINT32_MAX;
        for (uint32_t i = 0u; i < arity; i++) {
            uint32_t child_idx =
                alias_builder_intern_term(b, atom->expr.elems[i], depth + 1u);
            if (child_idx == UINT32_MAX) {
                free(children);
                return UINT32_MAX;
            }
            children[i] = child_idx;
            size += b->nodes[child_idx].size;
        }
        /* Re-check in case recursive interning already registered `id`. */
        existing = alias_builder_find_id(b, id);
        if (existing != UINT32_MAX) {
            free(children);
            return existing;
        }
    }

    if (b->node_len == b->node_cap) {
        uint32_t next_cap = b->node_cap ? b->node_cap * 2u : 32u;
        AliasNode *grown =
            cetta_realloc(b->nodes, sizeof(*grown) * (size_t)next_cap);
        if (!grown) {
            free(children);
            return UINT32_MAX;
        }
        b->nodes = grown;
        b->node_cap = next_cap;
    }
    if ((b->node_len + 1u) * 2u >= b->map_cap) {
        if (!alias_builder_map_grow(b)) {
            free(children);
            return UINT32_MAX;
        }
    }
    uint32_t idx = b->node_len++;
    b->nodes[idx] = (AliasNode){
        .id = id,
        .parent = idx,
        .size = size,
        .arity = arity,
        .children = children,
        .sym = atom->kind == ATOM_SYMBOL ? atom->sym_id : SYMBOL_ID_NONE,
        .is_grounded = atom->kind == ATOM_GROUNDED,
    };
    uint32_t mask = b->map_cap - 1u;
    for (uint32_t slot = alias_mix_u64(id) & mask;; slot = (slot + 1u) & mask) {
        if (b->map[slot].node_idx == 0u) {
            b->map[slot].key = id;
            b->map[slot].node_idx = idx + 1u;
            break;
        }
    }
    return idx;
}

static void collect_term_symbols(const Atom *atom, SymbolId **syms,
                                 uint32_t *len, uint32_t *cap,
                                 bool *has_grounded, uint32_t depth) {
    if (!atom || depth > 64u)
        return;
    if (atom->kind == ATOM_SYMBOL) {
        if (*len == *cap) {
            uint32_t next = *cap ? *cap * 2u : 16u;
            *syms = cetta_realloc(*syms, sizeof(**syms) * (size_t)next);
            *cap = next;
        }
        (*syms)[(*len)++] = atom->sym_id;
        return;
    }
    if (atom->kind == ATOM_GROUNDED) {
        *has_grounded = true;
        return;
    }
    if (atom->kind == ATOM_EXPR) {
        for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
            collect_term_symbols(atom->expr.elems[i], syms, len, cap,
                                 has_grounded, depth + 1u);
        }
    }
}

/* One equation between neighbours of an alias row.  PREFER says which side a
 * preference edge points to: none for `=e`, the right for `=e>`, the left
 * for `=e<`. */
typedef enum {
    ALIAS_PREFER_NONE = 0,
    ALIAS_PREFER_RIGHT,
    ALIAS_PREFER_LEFT,
} AliasPrefer;

typedef struct {
    Atom *lhs;
    Atom *rhs;
    AliasPrefer prefer;
} AliasEdgePair;

/* Order two members for printing: the smaller first, then by the standard
 * order of terms; never by AtomId, which follows interning order. */
static int alias_member_order(TermUniverse *u, const AliasNode *a,
                              const AliasNode *b) {
    if (a->size != b->size)
        return a->size < b->size ? -1 : 1;
    Atom *x = term_universe_get_atom(u, a->id);
    Atom *y = term_universe_get_atom(u, b->id);
    int ordering = 0;
    if (x && y && petta_semantics_term_compare(x, y, &ordering))
        return ordering < 0 ? -1 : ordering > 0 ? 1 : 0;
    return 0;
}

/* The members of the sink components of the preference edges: the members no
 * other member is preferred over without being preferred back.  Iterative
 * Tarjan over the nodes; marks SINK[i] for every such member. */
static void alias_preference_sinks(uint32_t n, const uint32_t *from,
                                   const uint32_t *to, size_t m, bool *sink) {
    uint32_t *start = alias_calloc((size_t)n + 1u, sizeof(*start));
    uint32_t *adj = cetta_malloc(sizeof(*adj) * (m ? m : 1u));
    for (size_t e = 0u; e < m; e++)
        start[from[e] + 1u]++;
    for (uint32_t i = 0u; i < n; i++)
        start[i + 1u] += start[i];
    uint32_t *fill = cetta_malloc(sizeof(*fill) * (n ? n : 1u));
    for (uint32_t i = 0u; i < n; i++)
        fill[i] = start[i];
    for (size_t e = 0u; e < m; e++)
        adj[fill[from[e]]++] = to[e];
    free(fill);

    uint32_t *index = cetta_malloc(sizeof(*index) * (n ? n : 1u));
    uint32_t *low = cetta_malloc(sizeof(*low) * (n ? n : 1u));
    uint32_t *comp = cetta_malloc(sizeof(*comp) * (n ? n : 1u));
    bool *on_stack = alias_calloc(n ? n : 1u, sizeof(*on_stack));
    uint32_t *stack = cetta_malloc(sizeof(*stack) * (n ? n : 1u));
    uint32_t *call = cetta_malloc(sizeof(*call) * (n ? n : 1u));
    uint32_t *edge_at = cetta_malloc(sizeof(*edge_at) * (n ? n : 1u));
    for (uint32_t i = 0u; i < n; i++)
        index[i] = UINT32_MAX;
    uint32_t next_index = 0u, sp = 0u, comps = 0u;
    for (uint32_t root = 0u; root < n; root++) {
        if (index[root] != UINT32_MAX)
            continue;
        uint32_t depth = 0u;
        call[depth++] = root;
        index[root] = low[root] = next_index++;
        edge_at[root] = start[root];
        stack[sp++] = root;
        on_stack[root] = true;
        while (depth) {
            uint32_t v = call[depth - 1u];
            if (edge_at[v] < start[v + 1u]) {
                uint32_t w = adj[edge_at[v]++];
                if (index[w] == UINT32_MAX) {
                    index[w] = low[w] = next_index++;
                    edge_at[w] = start[w];
                    stack[sp++] = w;
                    on_stack[w] = true;
                    call[depth++] = w;
                } else if (on_stack[w] && index[w] < low[v]) {
                    low[v] = index[w];
                }
                continue;
            }
            if (low[v] == index[v]) {
                uint32_t w;
                do {
                    w = stack[--sp];
                    on_stack[w] = false;
                    comp[w] = comps;
                } while (w != v);
                comps++;
            }
            depth--;
            if (depth && low[v] < low[call[depth - 1u]])
                low[call[depth - 1u]] = low[v];
        }
    }
    bool *leaves = alias_calloc(comps ? comps : 1u, sizeof(*leaves));
    for (size_t e = 0u; e < m; e++)
        if (comp[from[e]] != comp[to[e]])
            leaves[comp[from[e]]] = true;
    for (uint32_t i = 0u; i < n; i++)
        sink[i] = !leaves[comp[i]];
    free(leaves);
    free(edge_at);
    free(call);
    free(stack);
    free(on_stack);
    free(comp);
    free(low);
    free(index);
    free(adj);
    free(start);
}

static void space_alias_append_equation_edges(Atom *atom, AliasEdgePair **edges,
                                              size_t *edge_len, size_t *edge_cap) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len < 3u ||
        !atom->expr.elems[0] || atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        space_theory_classify_head_arity(atom->expr.elems[0]->sym_id,
                                         atom->expr.len) !=
            SPACE_THEORY_ATOM_EQUATION)
        return;
    SymbolId form = atom->expr.elems[0]->sym_id;
    AliasPrefer prefer = form == g_builtin_syms.equals_e_right
        ? ALIAS_PREFER_RIGHT
        : form == g_builtin_syms.equals_e_left ? ALIAS_PREFER_LEFT
                                               : ALIAS_PREFER_NONE;
    /* (=e a1 ... an) equates each member with its right neighbour. */
    for (CettaExprIndex k = 1u; k + 1u < atom->expr.len; k++) {
        Atom *lhs = atom->expr.elems[k];
        Atom *rhs = atom->expr.elems[k + 1u];
        if (!lhs || !rhs || atom_has_vars(lhs) || atom_has_vars(rhs) ||
            atom_eq(lhs, rhs))
            continue;
        if (*edge_len == *edge_cap) {
            size_t next = *edge_cap ? *edge_cap * 2u : 8u;
            *edges = cetta_realloc(*edges, sizeof(**edges) * next);
            *edge_cap = next;
        }
        (*edges)[(*edge_len)++] = (AliasEdgePair){lhs, rhs, prefer};
    }
}

static void space_alias_rebuild_locked(Space *s, SpaceAliasTable *t,
                                       uint64_t global_eq_epoch,
                                       uint64_t eq_rev, uint64_t overlay_stamp,
                                       uint64_t mod_dep_epoch) {
    space_alias_clear_maps(t);
    t->built_global_equational_epoch = global_eq_epoch;
    t->built_equational_revision = eq_rev;
    t->built_overlay_equational_epoch = overlay_stamp;
    t->built_module_dep_epoch = mod_dep_epoch;
    uint64_t u_inst = s->native.universe ? s->native.universe->instance_id : 0u;
    uint64_t u_epoch = s->native.universe ? s->native.universe->storage_epoch : 0u;
    t->built_universe_instance = u_inst;
    t->built_universe_storage_epoch = u_epoch;

    if (!s->native.universe)
        return;

    AliasEdgePair *edges = NULL;
    size_t edge_len = 0u, edge_cap = 0u;

    /* Collect active equation atoms through SpaceTheoryView (preserving
     * O(delta) suffix catch-up on plain native spaces). */
    SpaceTheoryView view = space_theory_view_open(s);
    Atom **equation_atoms = NULL;
    size_t equation_count = 0u;
    if (space_theory_view_collect_equations(&view, &t->equation_cache,
                                            &equation_atoms, &equation_count)) {
        for (size_t i = 0u; i < equation_count; i++) {
            space_alias_append_equation_edges(equation_atoms[i], &edges,
                                              &edge_len, &edge_cap);
        }
        free(equation_atoms);
    }

    if (edge_len == 0u) {
        /* When no active equations remain, the table stays inactive even when
         * equational_revision > 0. */
        free(edges);
        return;
    }

    AliasBuilder builder = {.universe = s->native.universe};
    SymbolId *raw_carrier = NULL;
    uint32_t raw_carrier_len = 0u, raw_carrier_cap = 0u;
    bool has_grounded = false;

    uint32_t *pref_from = cetta_malloc(sizeof(*pref_from) * edge_len);
    uint32_t *pref_to = cetta_malloc(sizeof(*pref_to) * edge_len);
    size_t pref_len = 0u;
    for (size_t i = 0u; i < edge_len; i++) {
        uint32_t l_idx = alias_builder_intern_term(&builder, edges[i].lhs, 0u);
        uint32_t r_idx = alias_builder_intern_term(&builder, edges[i].rhs, 0u);
        if (l_idx == UINT32_MAX || r_idx == UINT32_MAX)
            continue;
        if (edges[i].prefer == ALIAS_PREFER_RIGHT) {
            pref_from[pref_len] = l_idx;
            pref_to[pref_len++] = r_idx;
        } else if (edges[i].prefer == ALIAS_PREFER_LEFT) {
            pref_from[pref_len] = r_idx;
            pref_to[pref_len++] = l_idx;
        }
        alias_uf_union(builder.nodes, l_idx, r_idx);
        collect_term_symbols(edges[i].lhs, &raw_carrier, &raw_carrier_len,
                             &raw_carrier_cap, &has_grounded, 0u);
        collect_term_symbols(edges[i].rhs, &raw_carrier, &raw_carrier_len,
                             &raw_carrier_cap, &has_grounded, 0u);
    }
    free(edges);

    if (builder.node_len == 0u) {
        free(pref_from);
        free(pref_to);
        free(raw_carrier);
        alias_builder_free(&builder);
        return;
    }

    /* Congruence closure fixpoint over compound nodes. */
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t i = 0u; i < builder.node_len; i++) {
            if (builder.nodes[i].arity == 0u)
                continue;
            for (uint32_t j = i + 1u; j < builder.node_len; j++) {
                if (builder.nodes[j].arity != builder.nodes[i].arity)
                    continue;
                if (alias_uf_find(builder.nodes, i) ==
                    alias_uf_find(builder.nodes, j))
                    continue;
                bool same_children = true;
                for (uint32_t k = 0u; k < builder.nodes[i].arity; k++) {
                    if (alias_uf_find(builder.nodes,
                                      builder.nodes[i].children[k]) !=
                        alias_uf_find(builder.nodes,
                                      builder.nodes[j].children[k])) {
                        same_children = false;
                        break;
                    }
                }
                if (same_children) {
                    alias_uf_union(builder.nodes, i, j);
                    changed = true;
                }
            }
        }
    }

    /* The printed member of each class: among the members of the sink
     * components of the preference edges, the smallest, then the first in
     * the standard order of terms.  Without preference edges every member is
     * its own sink.  Nothing depends on AtomIds, row order or unrelated rows. */
    bool *sink = alias_calloc(builder.node_len, sizeof(*sink));
    alias_preference_sinks(builder.node_len, pref_from, pref_to, pref_len, sink);
    free(pref_from);
    free(pref_to);
    uint32_t *class_rep = cetta_malloc(sizeof(*class_rep) * builder.node_len);
    uint32_t *class_count = alias_calloc(builder.node_len, sizeof(*class_count));
    for (uint32_t i = 0u; i < builder.node_len; i++)
        class_rep[i] = UINT32_MAX;
    bool atomic_only = true;
    for (uint32_t i = 0u; i < builder.node_len; i++) {
        uint32_t root = alias_uf_find(builder.nodes, i);
        class_count[root]++;
        if (builder.nodes[i].arity != 0u)
            atomic_only = false;
        if (!sink[i])
            continue;
        uint32_t best = class_rep[root];
        if (best == UINT32_MAX ||
            alias_member_order(builder.universe, &builder.nodes[i],
                               &builder.nodes[best]) < 0) {
            class_rep[root] = i;
        }
    }
    free(sink);

    /* Count non-trivial class members. */
    size_t nontrivial_members = 0u;
    size_t sym_alias_count = 0u;
    for (uint32_t i = 0u; i < builder.node_len; i++) {
        uint32_t root = alias_uf_find(builder.nodes, i);
        if (class_count[root] < 2u)
            continue;
        nontrivial_members++;
        if (builder.nodes[i].sym != SYMBOL_ID_NONE)
            sym_alias_count++;
    }

    if (nontrivial_members == 0u) {
        free(class_rep);
        free(class_count);
        free(raw_carrier);
        alias_builder_free(&builder);
        return;
    }

    t->active_edge_count = edge_len;
    t->has_non_symbol_leaf_alias = has_grounded;
    t->atomic_only = atomic_only;

    /* Populate carrier_syms set. */
    if (raw_carrier_len > 0u) {
        t->carrier_sym_cap = next_pow2_u32(raw_carrier_len);
        t->carrier_syms = alias_calloc(t->carrier_sym_cap, sizeof(SymbolId));
        for (uint32_t i = 0u; i < raw_carrier_len; i++)
            carrier_sym_insert(t->carrier_syms, t->carrier_sym_cap, raw_carrier[i]);
    }
    free(raw_carrier);

    /* Populate sym_alias and atom_alias maps. */
    if (sym_alias_count > 0u) {
        t->sym_alias_cap = next_pow2_u32(sym_alias_count);
        t->sym_alias = alias_calloc(t->sym_alias_cap, sizeof(*t->sym_alias));
    }
    /* Reserve capacity in atom_alias for base members plus up to 4 rounds of
     * child-normalized compound keys (guarantees <= 50% load factor). */
    t->atom_alias_cap = next_pow2_u32(nontrivial_members * 5u + 8u);
    t->atom_alias = alias_calloc(t->atom_alias_cap, sizeof(*t->atom_alias));
    for (uint32_t i = 0u; i < t->atom_alias_cap; i++)
        t->atom_alias[i].key = CETTA_ATOM_ID_NONE;

    for (uint32_t i = 0u; i < builder.node_len; i++) {
        uint32_t root = alias_uf_find(builder.nodes, i);
        if (class_count[root] < 2u)
            continue;
        AtomId rep_id = builder.nodes[class_rep[root]].id;
        atom_alias_insert(t->atom_alias, t->atom_alias_cap,
                          builder.nodes[i].id, rep_id);
        if (builder.nodes[i].sym != SYMBOL_ID_NONE) {
            sym_alias_insert(t->sym_alias, t->sym_alias_cap,
                             builder.nodes[i].sym, rep_id);
        }
    }

    /* Also register child-normalized compound keys in atom_alias so one
     * bottom-up pass reaches the canonical representative in a single step
     * (e.g. (=e Fido fido) + (=e (Pet fido) SpecialPet) -> (Pet Fido) -> SpecialPet). */
    Arena scratch;
    arena_init(&scratch);
    for (uint32_t round = 0u; round < 4u; round++) {
        bool added_any = false;
        for (uint32_t i = 0u; i < builder.node_len; i++) {
            uint32_t root = alias_uf_find(builder.nodes, i);
            if (class_count[root] < 2u || builder.nodes[i].arity == 0u)
                continue;
            AtomId rep_id = builder.nodes[class_rep[root]].id;
            uint32_t arity = builder.nodes[i].arity;
            Atom **parts = arena_alloc(&scratch, sizeof(*parts) * (size_t)arity);
            if (!parts)
                break;
            bool ok = true;
            for (uint32_t k = 0u; k < arity; k++) {
                uint32_t c_root = alias_uf_find(builder.nodes,
                                                builder.nodes[i].children[k]);
                AtomId c_id = class_count[c_root] >= 2u
                    ? builder.nodes[class_rep[c_root]].id
                    : builder.nodes[builder.nodes[i].children[k]].id;
                Atom *c_atom = term_universe_get_atom(s->native.universe, c_id);
                if (!c_atom) {
                    ok = false;
                    break;
                }
                parts[k] = c_atom;
            }
            if (!ok)
                continue;
            Atom *rebuilt = atom_expr(&scratch, parts, (CettaExprLen)arity);
            if (!rebuilt)
                continue;
            AtomId rebuilt_id =
                term_universe_store_atom_id(s->native.universe, NULL, rebuilt);
            if (rebuilt_id != CETTA_ATOM_ID_NONE &&
                atom_alias_find(t, rebuilt_id) == CETTA_ATOM_ID_NONE) {
                atom_alias_insert(t->atom_alias, t->atom_alias_cap,
                                  rebuilt_id, rep_id);
                added_any = true;
            }
        }
        if (!added_any)
            break;
    }
    arena_free(&scratch);

    /* Build symbol equivalence classes for aliased rule heads (e.g. (=e Pet Companion)). */
    if (sym_alias_count > 0u) {
        t->sym_class_members = cetta_malloc(sizeof(SymbolId) * sym_alias_count);
        t->sym_class_ranges = cetta_malloc(sizeof(*t->sym_class_ranges) *
                                           sym_alias_count);
        t->sym_class_cap = next_pow2_u32(sym_alias_count);
        t->sym_class_slots = alias_calloc(t->sym_class_cap,
                                          sizeof(*t->sym_class_slots));
        uint32_t cursor = 0u;
        uint32_t range_len = 0u;
        for (uint32_t root = 0u; root < builder.node_len; root++) {
            if (class_count[root] < 2u ||
                alias_uf_find(builder.nodes, root) != root)
                continue;
            uint32_t start = cursor;
            for (uint32_t i = 0u; i < builder.node_len; i++) {
                if (builder.nodes[i].sym != SYMBOL_ID_NONE &&
                    alias_uf_find(builder.nodes, i) == root) {
                    t->sym_class_members[cursor++] = builder.nodes[i].sym;
                }
            }
            uint32_t count = cursor - start;
            if (count >= 2u) {
                t->sym_class_ranges[range_len++] = (SymClassRange){
                    .offset = start,
                    .count = count,
                };
                uint32_t mask = t->sym_class_cap - 1u;
                for (uint32_t m = start; m < cursor; m++) {
                    SymbolId sym = t->sym_class_members[m];
                    for (uint32_t slot = alias_mix_u32(sym) & mask;;
                         slot = (slot + 1u) & mask) {
                        if (t->sym_class_slots[slot].key == SYMBOL_ID_NONE ||
                            t->sym_class_slots[slot].key == sym) {
                            t->sym_class_slots[slot] = (SymClassSlot){
                                .key = sym,
                                .offset = start,
                                .count = count,
                            };
                            break;
                        }
                    }
                }
            } else {
                cursor = start;
            }
        }
        t->sym_class_members_len = cursor;
        t->sym_class_range_len = range_len;
    }

    /* Every class's members: the printed member first, then the others by
     * size and the standard order of terms (eq:class lists them so). */
    t->class_members = cetta_malloc(sizeof(AtomId) * nontrivial_members);
    t->class_cap = next_pow2_u32(nontrivial_members * 2u + 2u);
    t->class_slots = alias_calloc(t->class_cap, sizeof(*t->class_slots));
    for (uint32_t i = 0u; i < t->class_cap; i++)
        t->class_slots[i].key = CETTA_ATOM_ID_NONE;
    {
        uint32_t cursor = 0u;
        uint32_t *order = cetta_malloc(sizeof(*order) * builder.node_len);
        for (uint32_t root = 0u; root < builder.node_len; root++) {
            if (class_count[root] < 2u ||
                alias_uf_find(builder.nodes, root) != root)
                continue;
            uint32_t len = 0u;
            for (uint32_t i = 0u; i < builder.node_len; i++)
                if (i != class_rep[root] &&
                    alias_uf_find(builder.nodes, i) == root)
                    order[len++] = i;
            for (uint32_t i = 1u; i < len; i++) {
                uint32_t v = order[i], j = i;
                while (j > 0u &&
                       alias_member_order(builder.universe, &builder.nodes[v],
                                          &builder.nodes[order[j - 1u]]) < 0) {
                    order[j] = order[j - 1u];
                    j--;
                }
                order[j] = v;
            }
            uint32_t start = cursor;
            t->class_members[cursor++] = builder.nodes[class_rep[root]].id;
            for (uint32_t i = 0u; i < len; i++)
                t->class_members[cursor++] = builder.nodes[order[i]].id;
            uint32_t mask = t->class_cap - 1u;
            for (uint32_t m = start; m < cursor; m++) {
                AtomId id = t->class_members[m];
                for (uint32_t slot = alias_mix_u64(id) & mask;;
                     slot = (slot + 1u) & mask) {
                    if (t->class_slots[slot].key == CETTA_ATOM_ID_NONE ||
                        t->class_slots[slot].key == id) {
                        t->class_slots[slot] = (ClassSlot){
                            .key = id, .offset = start, .count = cursor - start};
                        break;
                    }
                }
            }
        }
        free(order);
        t->class_members_len = cursor;
    }

    free(class_rep);
    free(class_count);
    alias_builder_free(&builder);
}

static SpaceAliasTable *space_alias_ensure_locked(Space *s) {
    if (!s || !space_may_have_equational_aliases(s))
        return NULL;
    uint64_t global_eq_epoch = atomic_load_explicit(
        &cetta_space_global_equational_epoch, memory_order_acquire);
    uint64_t mod_dep_epoch = space_module_dependency_epoch(s);
    uint64_t u_inst = s->native.universe ? s->native.universe->instance_id : 0u;
    uint64_t u_epoch = s->native.universe ? s->native.universe->storage_epoch : 0u;
    SpaceAliasTable *t = s->alias_table;
    if (t &&
        t->built_global_equational_epoch == global_eq_epoch &&
        t->built_module_dep_epoch == mod_dep_epoch &&
        t->built_universe_instance == u_inst &&
        t->built_universe_storage_epoch == u_epoch &&
        (!s->overlay_base ||
         t->built_overlay_equational_epoch ==
             space_alias_overlay_equational_stamp(s))) {
        return t;
    }
    uint64_t eq_rev = space_equational_revision(s);
    uint64_t overlay_stamp = space_alias_overlay_equational_stamp(s);
    if (t &&
        t->built_equational_revision == eq_rev &&
        t->built_overlay_equational_epoch == overlay_stamp &&
        t->built_module_dep_epoch == mod_dep_epoch &&
        t->built_universe_instance == u_inst &&
        t->built_universe_storage_epoch == u_epoch) {
        t->built_global_equational_epoch = global_eq_epoch;
        return t;
    }
    if (!t) {
        t = alias_calloc(1u, sizeof(*t));
        if (!t)
            return NULL;
        s->alias_table = t;
    }
    space_alias_rebuild_locked(s, t, global_eq_epoch, eq_rev, overlay_stamp,
                               mod_dep_epoch);
    return t;
}

bool space_has_active_equations_slow(Space *s) {
    if (!s)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    SPACE_MAINTAIN(maint, s);
    SpaceAliasTable *t = space_alias_ensure_locked(s);
    return t && t->active_edge_count > 0u;
}

/* ── Carrier & Normalization Queries ────────────────────────────────────── */

static bool term_touches_carrier_locked(const Space *s,
                                        const SpaceAliasTable *t,
                                        const Atom *term, uint32_t depth) {
    if (!term || !t || t->active_edge_count == 0u || depth > 64u)
        return false;
    if (term->kind == ATOM_SYMBOL)
        return carrier_sym_contains(t, term->sym_id);
    if (term->kind == ATOM_GROUNDED) {
        if (!t->has_non_symbol_leaf_alias || !s->native.universe)
            return false;
        AtomId id = term_universe_lookup_atom_id(s->native.universe,
                                                 (Atom *)term);
        return id != CETTA_ATOM_ID_NONE &&
               atom_alias_find(t, id) != CETTA_ATOM_ID_NONE;
    }
    if (term->kind == ATOM_EXPR) {
        for (CettaExprIndex i = 0u; i < term->expr.len; i++) {
            if (term_touches_carrier_locked(s, t, term->expr.elems[i],
                                            depth + 1u))
                return true;
        }
    }
    return false;
}

static Atom *normalize_atom_locked(const Space *s, const SpaceAliasTable *t,
                                   Arena *arena, Atom *term, uint32_t depth) {
    if (!term || !t || t->active_edge_count == 0u || depth > 64u)
        return term;
    if (term->kind == ATOM_VAR)
        return term;
    if (term->kind == ATOM_SYMBOL) {
        AtomId rep = sym_alias_find(t, term->sym_id);
        if (rep == CETTA_ATOM_ID_NONE)
            return term;
        Atom *rep_atom = term_universe_get_atom(s->native.universe, rep);
        return rep_atom ? rep_atom : term;
    }
    if (term->kind == ATOM_GROUNDED) {
        if (!t->has_non_symbol_leaf_alias || !s->native.universe)
            return term;
        AtomId id = term_universe_lookup_atom_id(s->native.universe, term);
        AtomId rep = atom_alias_find(t, id);
        if (rep == CETTA_ATOM_ID_NONE)
            return term;
        Atom *rep_atom = term_universe_get_atom(s->native.universe, rep);
        return rep_atom ? rep_atom : term;
    }
    if (term->kind == ATOM_EXPR) {
        CettaExprLen len = term->expr.len;
        if (len == 0u)
            return term;
        Atom *inline_elems[16];
        Atom **elems = len <= 16u
            ? inline_elems
            : arena_alloc(arena, sizeof(*elems) * (size_t)len);
        if (!elems)
            return term;
        bool child_changed = false;
        for (CettaExprIndex i = 0u; i < len; i++) {
            Atom *orig = term->expr.elems[i];
            Atom *norm = normalize_atom_locked(s, t, arena, orig, depth + 1u);
            elems[i] = norm;
            if (norm != orig)
                child_changed = true;
        }
        Atom *candidate = child_changed ? atom_expr(arena, elems, len) : term;
        if (!candidate)
            return term;
        if (!atom_has_vars(candidate) && s->native.universe) {
            AtomId id = term_universe_lookup_atom_id(s->native.universe,
                                                     candidate);
            AtomId rep = atom_alias_find(t, id);
            if (rep != CETTA_ATOM_ID_NONE) {
                Atom *rep_atom = term_universe_get_atom(s->native.universe, rep);
                if (rep_atom)
                    return rep_atom;
            }
        }
        return candidate;
    }
    return term;
}

bool space_alias_head_in_carrier_slow(Space *s, SymbolId head) {
    if (!space_may_have_equational_aliases(s) || head == SYMBOL_ID_NONE)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    SPACE_MAINTAIN(maint, s);
    SpaceAliasTable *t = space_alias_ensure_locked(s);
    return t && t->active_edge_count > 0u && carrier_sym_contains(t, head);
}

bool space_alias_term_touches_carrier_slow(Space *s, const Atom *term) {
    if (!space_may_have_equational_aliases(s) || !term)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    SPACE_MAINTAIN(maint, s);
    SpaceAliasTable *t = space_alias_ensure_locked(s);
    if (!t || t->active_edge_count == 0u)
        return false;
    return term_touches_carrier_locked(s, t, term, 0u);
}

Atom *space_alias_normalize_atom(Space *s, Arena *arena, Atom *term) {
    if (!space_may_have_equational_aliases(s) || !arena || !term)
        return term;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    SPACE_MAINTAIN(maint, s);
    SpaceAliasTable *t = space_alias_ensure_locked(s);
    if (!t || t->active_edge_count == 0u ||
        !term_touches_carrier_locked(s, t, term, 0u))
        return term;
    return normalize_atom_locked(s, t, arena, term, 0u);
}

bool space_alias_same(Space *s, Arena *arena, Atom *left, Atom *right) {
    if (!left || !right)
        return left == right;
    if (atom_eq(left, right))
        return true;
    if (!space_may_have_equational_aliases(s) || !arena)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    SPACE_MAINTAIN(maint, s);
    SpaceAliasTable *t = space_alias_ensure_locked(s);
    if (!t || t->active_edge_count == 0u)
        return false;
    Atom *nl = term_touches_carrier_locked(s, t, left, 0u)
        ? normalize_atom_locked(s, t, arena, left, 0u) : left;
    Atom *nr = term_touches_carrier_locked(s, t, right, 0u)
        ? normalize_atom_locked(s, t, arena, right, 0u) : right;
    return atom_eq(nl, nr);
}

/* ── Affected-Head Cache ────────────────────────────────────────────────── */

static uint8_t head_affected_lookup(const SpaceAliasTable *t, SymbolId head) {
    if (!t || !t->head_affected || head == SYMBOL_ID_NONE)
        return 0u;
    uint32_t mask = t->head_affected_cap - 1u;
    for (uint32_t slot = alias_mix_u32(head) & mask;; slot = (slot + 1u) & mask) {
        if (t->head_affected[slot].key == head)
            return t->head_affected[slot].state;
        if (t->head_affected[slot].key == SYMBOL_ID_NONE)
            return 0u;
    }
}

static void head_affected_store(SpaceAliasTable *t, SymbolId head,
                                uint8_t state) {
    if ((t->head_affected_used + 1u) * 2u >= t->head_affected_cap) {
        uint32_t next_cap = t->head_affected_cap ? t->head_affected_cap * 2u : 32u;
        HeadAffectedSlot *next = alias_calloc(next_cap, sizeof(*next));
        uint32_t mask = next_cap - 1u;
        for (uint32_t i = 0u; i < t->head_affected_cap; i++) {
            if (t->head_affected[i].key == SYMBOL_ID_NONE)
                continue;
            for (uint32_t slot = alias_mix_u32(t->head_affected[i].key) & mask;;
                 slot = (slot + 1u) & mask) {
                if (next[slot].key == SYMBOL_ID_NONE) {
                    next[slot] = t->head_affected[i];
                    break;
                }
            }
        }
        free(t->head_affected);
        t->head_affected = next;
        t->head_affected_cap = next_cap;
    }
    uint32_t mask = t->head_affected_cap - 1u;
    for (uint32_t slot = alias_mix_u32(head) & mask;; slot = (slot + 1u) & mask) {
        if (t->head_affected[slot].key == SYMBOL_ID_NONE) {
            t->head_affected[slot] = (HeadAffectedSlot){head, state};
            t->head_affected_used++;
            return;
        }
        if (t->head_affected[slot].key == head) {
            t->head_affected[slot].state = state;
            return;
        }
    }
}

bool space_alias_head_affected_slow(Space *s, SymbolId head) {
    if (!space_may_have_equational_aliases(s) || head == SYMBOL_ID_NONE)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    {
        SPACE_MAINTAIN(maint, s);
        SpaceAliasTable *t = space_alias_ensure_locked(s);
        if (!t || t->active_edge_count == 0u)
            return false;
        if (carrier_sym_contains(t, head))
            return true;
        uint64_t eq_rev = space_equation_revision(s);
        uint64_t ov_dep = s->overlay_base ? space_overlay_dependency_epoch(s) : 0u;
        if (t->head_cache_eq_rev != eq_rev ||
            t->head_cache_overlay_dep != ov_dep) {
            space_alias_clear_head_cache(t);
            t->head_cache_eq_rev = eq_rev;
            t->head_cache_overlay_dep = ov_dep;
        } else {
            uint8_t cached = head_affected_lookup(t, head);
            if (cached != 0u)
                return cached == 2u;
        }
    }

    /* Inspect equations of `head` outside the maintenance lock (cursor takes
     * its own index maintenance if needed). */
    bool affected = false;
    SpaceEquationCursor cursor;
    unsigned forms = CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL;
    if (space_rule_cursor_init(s, head, forms, &cursor)) {
        for (;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
            if (step == SPACE_EQUATION_CURSOR_END)
                break;
            SpaceEquationOccurrence occ = {0};
            if (step != SPACE_EQUATION_CURSOR_ITEM ||
                !space_rule_occurrence_resolve(id, forms, &occ)) {
                affected = true;
                break;
            }
            if (occ.lhs && space_alias_term_touches_carrier(s, occ.lhs)) {
                affected = true;
                break;
            }
        }
    }

    {
        SPACE_MAINTAIN(maint, s);
        SpaceAliasTable *t = space_alias_ensure_locked(s);
        if (t && t->active_edge_count > 0u) {
            uint64_t eq_rev = space_equation_revision(s);
            uint64_t ov_dep =
                s->overlay_base ? space_overlay_dependency_epoch(s) : 0u;
            if (t->head_cache_eq_rev == eq_rev &&
                t->head_cache_overlay_dep == ov_dep) {
                head_affected_store(t, head, affected ? 2u : 1u);
            }
        }
    }
    return affected;
}

bool space_alias_collect_symbol_class(Space *s, SymbolId head,
                                      SymbolId **out_syms, size_t *out_count) {
    if (out_syms)
        *out_syms = NULL;
    if (out_count)
        *out_count = 0u;
    if (!s || head == SYMBOL_ID_NONE || !out_syms || !out_count ||
        !space_may_have_equational_aliases(s))
        return true;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    SPACE_MAINTAIN(maint, s);
    SpaceAliasTable *t = space_alias_ensure_locked(s);
    const SymClassSlot *cls =
        (t && t->active_edge_count > 0u) ? sym_class_find(t, head) : NULL;
    if (!cls || cls->count < 2u)
        return true;
    SymbolId *copy = cetta_malloc(sizeof(*copy) * (size_t)cls->count);
    memcpy(copy, t->sym_class_members + cls->offset,
           sizeof(*copy) * (size_t)cls->count);
    *out_syms = copy;
    *out_count = cls->count;
    return true;
}

bool space_alias_visit_symbol_classes(Space *s,
                                      SpaceAliasSymbolClassVisitor visitor,
                                      void *ctx) {
    if (!s || !visitor || !space_may_have_equational_aliases(s))
        return true;
    SymbolId *members = NULL;
    SymClassRange *ranges = NULL;
    uint32_t member_len = 0u;
    uint32_t range_len = 0u;
    {
        CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
        SPACE_MAINTAIN(maint, s);
        SpaceAliasTable *t = space_alias_ensure_locked(s);
        if (!t || t->active_edge_count == 0u || t->sym_class_range_len == 0u)
            return true;
        member_len = t->sym_class_members_len;
        range_len = t->sym_class_range_len;
        members = cetta_malloc(sizeof(*members) * (size_t)member_len);
        ranges = cetta_malloc(sizeof(*ranges) * (size_t)range_len);
        memcpy(members, t->sym_class_members,
               sizeof(*members) * (size_t)member_len);
        memcpy(ranges, t->sym_class_ranges,
               sizeof(*ranges) * (size_t)range_len);
    }
    bool ok = true;
    for (uint32_t i = 0u; i < range_len; i++) {
        if (!visitor(members + ranges[i].offset, ranges[i].count, ctx)) {
            ok = false;
            break;
        }
    }
    free(ranges);
    free(members);
    return ok;
}

bool space_alias_head_has_rules(Space *s, SymbolId head,
                                unsigned admitted_forms) {
    if (!s || head == SYMBOL_ID_NONE)
        return false;
    unsigned base_forms = admitted_forms & ~CETTA_RULE_SYNTAX_EQUATIONAL_ALIAS;
    if (space_rules_may_match_known_head(s, head, base_forms))
        return true;
    if (!(admitted_forms & CETTA_RULE_SYNTAX_EQUATIONAL_ALIAS) ||
        !space_may_have_equational_aliases(s))
        return false;

    SymbolId inline_members[16];
    SymbolId *members = NULL;
    uint32_t count = 0u;
    CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
    {
        SPACE_MAINTAIN(maint, s);
        SpaceAliasTable *t = space_alias_ensure_locked(s);
        const SymClassSlot *cls =
            (t && t->active_edge_count > 0u) ? sym_class_find(t, head) : NULL;
        if (!cls || cls->count < 2u)
            return false;
        count = cls->count;
        members = count <= 16u
            ? inline_members
            : cetta_malloc(sizeof(*members) * (size_t)count);
        memcpy(members, t->sym_class_members + cls->offset,
               sizeof(*members) * (size_t)count);
    }
    bool found = false;
    for (uint32_t i = 0u; i < count; i++) {
        if (members[i] == head)
            continue;
        if (space_rules_may_match_known_head(s, members[i], base_forms)) {
            found = true;
            break;
        }
    }
    if (members != inline_members)
        free(members);
    return found;
}

/* ── Structural Pattern Match Modulo =e ─────────────────────────────────── */

static bool match_pattern_rec(Space *s, Arena *arena, Atom *pattern,
                              Atom *target, Bindings *env, bool bidirectional,
                              uint32_t depth) {
    if (!pattern || !target || depth > 64u)
        return false;
    if (pattern->kind == ATOM_VAR) {
        BindingValue existing = bindings_lookup_value_id(env, pattern->var_id);
        if (existing.skeleton) {
            Atom *resolved = bindings_apply_value(env, arena, existing);
            Atom *rhs = bindings_apply_if_vars(env, arena, target);
            return match_pattern_rec(s, arena, resolved, rhs, env,
                                     bidirectional, depth + 1u);
        }
        /* Bind pattern variable to the ORIGINAL raw target term. */
        return bindings_add_var(env, pattern, target);
    }
    if (bidirectional && target->kind == ATOM_VAR) {
        BindingValue existing = bindings_lookup_value_id(env, target->var_id);
        if (existing.skeleton) {
            Atom *resolved = bindings_apply_value(env, arena, existing);
            Atom *lhs = bindings_apply_if_vars(env, arena, pattern);
            return match_pattern_rec(s, arena, lhs, resolved, env,
                                     bidirectional, depth + 1u);
        }
        return bindings_add_var(env, target, pattern);
    }
    if (!atom_has_vars(pattern) && !atom_has_vars(target))
        return space_alias_same(s, arena, pattern, target);
    if (pattern->kind == ATOM_EXPR && target->kind == ATOM_EXPR &&
        pattern->expr.len == target->expr.len) {
        for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
            if (!match_pattern_rec(s, arena, pattern->expr.elems[i],
                                   target->expr.elems[i], env, bidirectional,
                                   depth + 1u))
                return false;
        }
        return true;
    }
    return false;
}

bool space_alias_match_pattern(Space *s, Arena *arena, Atom *pattern,
                               Atom *target, Bindings *env) {
    if (!s || !arena || !pattern || !target || !env)
        return false;
    return match_pattern_rec(s, arena, pattern, target, env, true, 0u);
}

CettaCount space_alias_query_equation_visit(Space *s, Atom *equation,
                                            Atom *query,
                                            unsigned admitted_forms,
                                            Arena *arena,
                                            QueryResultVisitor visitor,
                                            void *ctx) {
    if (!s || !equation || !query || !arena || !visitor)
        return 0u;
    CettaRuleDescriptor rule;
    if (!cetta_rule_view(equation, admitted_forms, &rule))
        return 0u;

    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    CettaFrameIdentity fresh =
        cetta_frame_identity_scope_fresh(&frame_identity_scope);
    Atom *lhs = atom_has_vars(rule.head)
        ? atom_freshen_epoch(arena, rule.head, fresh) : rule.head;
    Atom *rhs = atom_has_vars(rule.body)
        ? atom_freshen_epoch(arena, rule.body, fresh) : rule.body;
    if (!lhs || !rhs)
        return 0u;

    Bindings env;
    bindings_init(&env);
    bool bidirectional = rule.binding == CETTA_RULE_BIND_UNIFY;
    bool matched = match_pattern_rec(s, arena, lhs, query, &env,
                                     bidirectional, 0u);
    CettaCount emitted = 0u;
    if (matched) {
        Atom *result = bindings_apply_if_vars(&env, arena, rhs);
        if (result) {
            (void)visitor(result, &env, ctx);
            emitted = 1u;
        }
    }
    bindings_free(&env);
    return emitted;
}

/* ── Source-Ordered Candidate Collection (Head Aliases + Wildcards) ─────── */

bool space_alias_collect_head_rules(Space *s, SymbolId head,
                                    unsigned admitted_forms,
                                    Atom ***out_equations, size_t *out_count) {
    if (out_equations)
        *out_equations = NULL;
    if (out_count)
        *out_count = 0u;
    if (!s || !out_equations || !out_count)
        return false;

    bool head_aliased = head != SYMBOL_ID_NONE &&
                        space_alias_head_in_carrier(s, head);
    Atom **items = NULL;
    size_t len = 0u, cap = 0u;

    if (!head_aliased) {
        SpaceEquationCursor cursor;
        if (!space_rule_cursor_init(s, head, admitted_forms, &cursor))
            return true;
        for (;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
            if (step == SPACE_EQUATION_CURSOR_END)
                break;
            SpaceEquationOccurrence occ = {0};
            if (step != SPACE_EQUATION_CURSOR_ITEM ||
                !space_rule_occurrence_resolve(id, admitted_forms, &occ) ||
                !occ.equation) {
                free(items);
                return false;
            }
            if (len == cap) {
                size_t next = cap ? cap * 2u : 8u;
                items = cetta_realloc(items, sizeof(*items) * next);
                cap = next;
            }
            items[len++] = occ.equation;
        }
        *out_equations = items;
        *out_count = len;
        return true;
    }

    /* When `head` is itself in the alias carrier (e.g. `(=e Pet Companion)`),
     * scan the composed view in exact source order so rules of every symbol
     * equivalent to `head` (plus wildcard rules) appear in exact authored
     * order with all duplicates preserved. */
    Arena scratch;
    arena_init(&scratch);
    Atom *head_atom = atom_symbol_id(&scratch, head);
    Atom *head_norm = space_alias_normalize_atom(s, &scratch, head_atom);
    CettaCount total = space_view_length64(s);
    for (CettaIndex i = 0u; i < total; i++) {
        Atom *row = space_view_get_at64(s, i);
        CettaRuleDescriptor rule;
        if (!cetta_rule_view(row, admitted_forms, &rule))
            continue;
        bool match_head = false;
        if (rule.head->kind == ATOM_VAR) {
            match_head = true;
        } else if (rule.head->kind == ATOM_EXPR && rule.head->expr.len > 0u) {
            Atom *lhs_head = rule.head->expr.elems[0];
            if (lhs_head->kind == ATOM_VAR) {
                match_head = true;
            } else if (lhs_head->kind == ATOM_SYMBOL) {
                Atom *lhs_head_norm =
                    space_alias_normalize_atom(s, &scratch, lhs_head);
                match_head = atom_eq(lhs_head_norm, head_norm);
            }
        }
        if (!match_head)
            continue;
        if (len == cap) {
            size_t next = cap ? cap * 2u : 8u;
            items = cetta_realloc(items, sizeof(*items) * next);
            cap = next;
        }
        items[len++] = row;
    }
    arena_free(&scratch);
    *out_equations = items;
    *out_count = len;
    return true;
}

/* ── Candidate Rows for eq:match ────────────────────────────────────────── */

/* The rows that can equal an instance of a pattern modulo the classes, as a
 * superset in the space's own order; the caller's modulo match decides each.
 *
 * Over atomic classes the quotient is free, so those rows are exactly the
 * rows matching one of the pattern's expansions: every aliased atom replaced
 * by each member of its class.  Up to ALIAS_EXPANSION_LIMIT expansions are
 * asked of the space's index and merged by position, where positions are the
 * space's own row indices (alias_positions_global).  Otherwise one
 * generalized pattern is asked, whose single cursor keeps the space's order:
 * every subterm the classes can reach replaced by a fresh variable.  No index
 * of this module's own is kept, so a write costs nothing here. */
#define ALIAS_EXPANSION_LIMIT 64u

typedef struct {
    CettaIndex position;
    Atom *row;
} AliasCandidate;

typedef struct {
    AliasCandidate *items;
    size_t len, cap;
} AliasCandidates;

static void alias_candidates_push(AliasCandidates *c, CettaIndex position,
                                  Atom *row) {
    if (c->len == c->cap) {
        c->cap = c->cap ? c->cap * 2u : 16u;
        c->items = cetta_realloc(c->items, sizeof(*c->items) * c->cap);
    }
    c->items[c->len++] = (AliasCandidate){position, row};
}

/* The rows the space's index selects for PATTERN, copied into ARENA.  False
 * when the index declines or the view moves while it is read. */
static bool alias_collect_selected(Space *s, Arena *arena, Atom *pattern,
                                   AliasCandidates *out) {
    if (!s->match_backend.ops)
        return false; /* no cursor execution: the caller reads every row */
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (space_match_cursor_open(s, arena, pattern, &cursor, NULL) !=
        SPACE_MATCH_CURSOR_READY)
        return false;
    bool ok = true;
    for (;;) {
        CettaIndex position = 0u;
        Atom *row = NULL;
        SpaceOccurrenceCursorStep step =
            space_occurrence_cursor_next(&cursor, &position, &row);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        if (step == SPACE_OCCURRENCE_CURSOR_INVALIDATED) {
            ok = false;
            break;
        }
        if (!row)
            continue;
        Atom *copy = atom_deep_copy(arena, row);
        if (!copy) {
            ok = false;
            break;
        }
        alias_candidates_push(out, position, copy);
    }
    space_occurrence_cursor_release(&cursor);
    return ok;
}

static const ClassSlot *alias_atom_class(const Space *s,
                                         const SpaceAliasTable *t,
                                         Atom *atom) {
    if (!atom || (atom->kind != ATOM_SYMBOL && atom->kind != ATOM_GROUNDED))
        return NULL;
    if (atom->kind == ATOM_SYMBOL && !carrier_sym_contains(t, atom->sym_id))
        return NULL;
    if (atom->kind == ATOM_GROUNDED && !t->has_non_symbol_leaf_alias)
        return NULL;
    AtomId id = term_universe_lookup_atom_id(s->native.universe, atom);
    const ClassSlot *slot = class_find(t, id);
    return slot && slot->count >= 2u ? slot : NULL;
}

/* The product of the class sizes of PATTERN's aliased atoms, capped above
 * the expansion limit. */
static uint64_t alias_expansion_count(const Space *s, const SpaceAliasTable *t,
                                      Atom *pattern, uint32_t depth) {
    if (!pattern || depth > 64u)
        return 1u;
    const ClassSlot *slot = alias_atom_class(s, t, pattern);
    if (slot)
        return slot->count;
    if (pattern->kind != ATOM_EXPR)
        return 1u;
    uint64_t product = 1u;
    for (CettaExprIndex i = 0u; i < pattern->expr.len; i++) {
        product *= alias_expansion_count(s, t, pattern->expr.elems[i], depth + 1u);
        if (product > ALIAS_EXPANSION_LIMIT)
            return ALIAS_EXPANSION_LIMIT + 1u;
    }
    return product;
}

/* The expansion numbered *CHOICE (mixed radix over the aliased atoms, in the
 * order alias_expansion_count visits them). */
static Atom *alias_expand(const Space *s, const SpaceAliasTable *t,
                          Arena *arena, Atom *pattern, uint64_t *choice,
                          uint32_t depth) {
    if (!pattern || depth > 64u)
        return pattern;
    const ClassSlot *slot = alias_atom_class(s, t, pattern);
    if (slot) {
        uint32_t pick = (uint32_t)(*choice % slot->count);
        *choice /= slot->count;
        Atom *member = term_universe_get_atom(
            s->native.universe, t->class_members[slot->offset + pick]);
        return member ? member : pattern;
    }
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return pattern;
    CettaExprLen len = pattern->expr.len;
    Atom **elems = arena_alloc(arena, sizeof(*elems) * (size_t)len);
    if (!elems)
        return pattern;
    bool changed = false;
    for (CettaExprIndex i = 0u; i < len; i++) {
        elems[i] = alias_expand(s, t, arena, pattern->expr.elems[i], choice,
                                depth + 1u);
        changed |= elems[i] != pattern->expr.elems[i];
    }
    return changed ? atom_expr(arena, elems, len) : pattern;
}

/* PATTERN with every subterm the classes can reach replaced by a fresh
 * variable: an aliased atom, and over non-atomic classes an expression whose
 * head the classes mention. */
static Atom *alias_generalize(const Space *s, const SpaceAliasTable *t,
                              Arena *arena, Atom *pattern, uint32_t depth) {
    if (!pattern || depth > 64u || pattern->kind == ATOM_VAR)
        return pattern;
    if (pattern->kind == ATOM_SYMBOL || pattern->kind == ATOM_GROUNDED) {
        bool reached = pattern->kind == ATOM_SYMBOL
            ? carrier_sym_contains(t, pattern->sym_id)
            : t->has_non_symbol_leaf_alias &&
                  atom_alias_find(t, term_universe_lookup_atom_id(
                                         s->native.universe, pattern)) !=
                      CETTA_ATOM_ID_NONE;
        return reached ? atom_var_with_id(arena, "eq", fresh_var_id()) : pattern;
    }
    if (pattern->kind != ATOM_EXPR || pattern->expr.len == 0u)
        return pattern;
    Atom *head = pattern->expr.elems[0];
    if (!t->atomic_only && head && head->kind == ATOM_SYMBOL &&
        carrier_sym_contains(t, head->sym_id))
        return atom_var_with_id(arena, "eq", fresh_var_id());
    CettaExprLen len = pattern->expr.len;
    Atom **elems = arena_alloc(arena, sizeof(*elems) * (size_t)len);
    if (!elems)
        return pattern;
    bool changed = false;
    for (CettaExprIndex i = 0u; i < len; i++) {
        elems[i] = alias_generalize(s, t, arena, pattern->expr.elems[i],
                                    depth + 1u);
        changed |= elems[i] != pattern->expr.elems[i];
    }
    return changed ? atom_expr(arena, elems, len) : pattern;
}

/* Whether cursor positions name the space's own rows, comparable across
 * cursors and in the space's order: no facts kept in relations (whose
 * positions count each cursor's own kept facts), no composed members, and no
 * kind that orders its reads. */
static bool alias_positions_global(const Space *s) {
    return !space_is_ordered(s) && space_dense_facts(s) == 0u &&
           !s->overlay_base && space_dependency_count(s) == 0u;
}

static int alias_candidate_position_order(const void *left, const void *right) {
    CettaIndex a = ((const AliasCandidate *)left)->position;
    CettaIndex b = ((const AliasCandidate *)right)->position;
    return a < b ? -1 : a > b;
}

static bool alias_candidates_finish(AliasCandidates *c, Atom ***out_rows,
                                    size_t *out_count) {
    if (c->len == 0u) {
        free(c->items);
        return true;
    }
    Atom **rows = cetta_malloc(sizeof(*rows) * c->len);
    for (size_t i = 0u; i < c->len; i++)
        rows[i] = c->items[i].row;
    *out_rows = rows;
    *out_count = c->len;
    free(c->items);
    return true;
}

/* Every row of the view, as a last resort when the index declines. */
static bool alias_all_rows(Space *s, Arena *arena, Atom ***out_rows,
                           size_t *out_count) {
    CettaCount total = space_view_length64(s);
    if (total == 0u)
        return true;
    Atom **rows = cetta_malloc(sizeof(*rows) * (size_t)total);
    size_t owned = 0;
    for (CettaIndex i = 0u; i < total; i++) {
        Atom *row = space_view_get_at64(s, i);
        if (!row)
            continue;
        Atom *copy = atom_deep_copy(arena, row);
        if (!copy) {
            free(rows);
            return false;
        }
        rows[owned++] = copy;
    }
    *out_rows = rows;
    *out_count = owned;
    return true;
}

static bool alias_candidate_rows(Space *s, Arena *arena, Atom *pattern,
                                 Atom ***out_rows, size_t *out_count) {
    if (out_rows)
        *out_rows = NULL;
    if (out_count)
        *out_count = 0u;
    if (!s || !arena || !pattern || !out_rows || !out_count)
        return false;
    AliasCandidates found = {0};
    if (!space_has_active_equations(s)) {
        /* No classes: the rows the pattern itself selects. */
        if (!alias_collect_selected(s, arena, pattern, &found)) {
            free(found.items);
            return alias_all_rows(s, arena, out_rows, out_count);
        }
        return alias_candidates_finish(&found, out_rows, out_count);
    }
    const SpaceAliasTable *t;
    Atom *selector = NULL;
    uint64_t expansions = 0u;
    {
        CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
        SPACE_MAINTAIN(maint, s);
        t = space_alias_ensure_locked(s);
        if (!t || t->active_edge_count == 0u) {
            selector = pattern;
        } else if (t->atomic_only && alias_positions_global(s) &&
                   (expansions = alias_expansion_count(s, t, pattern, 0u)) <=
                       ALIAS_EXPANSION_LIMIT) {
            selector = NULL;
        } else {
            selector = alias_generalize(s, t, arena, pattern, 0u);
        }
    }
    if (selector) {
        if (!alias_collect_selected(s, arena, selector, &found)) {
            free(found.items);
            return alias_all_rows(s, arena, out_rows, out_count);
        }
        return alias_candidates_finish(&found, out_rows, out_count);
    }
    for (uint64_t e = 0u; e < expansions; e++) {
        Atom *expanded;
        {
            CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
            SPACE_MAINTAIN(maint, s);
            const SpaceAliasTable *now = space_alias_ensure_locked(s);
            uint64_t choice = e;
            expanded = now ? alias_expand(s, now, arena, pattern, &choice, 0u)
                           : pattern;
        }
        if (!alias_collect_selected(s, arena, expanded, &found)) {
            free(found.items);
            return alias_all_rows(s, arena, out_rows, out_count);
        }
    }
    if (expansions > 1u && found.len > 1u) {
        /* One occurrence once, in the space's order. */
        qsort(found.items, found.len, sizeof(*found.items),
              alias_candidate_position_order);
        size_t keep = 0u;
        for (size_t i = 0u; i < found.len; i++)
            if (keep == 0u ||
                found.items[keep - 1u].position != found.items[i].position)
                found.items[keep++] = found.items[i];
        found.len = keep;
    }
    return alias_candidates_finish(&found, out_rows, out_count);
}

bool space_alias_instantiate_matches(Space *s, Arena *arena, Atom *pattern,
                                    Atom *template, Atom ***out,
                                    size_t *out_count) {
    if (out)
        *out = NULL;
    if (out_count)
        *out_count = 0;
    if (!s || !arena || !pattern || !template || !out || !out_count)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(match_cut, s);
    Atom **rows = NULL;
    size_t count = 0;
    if (!alias_candidate_rows(s, arena, pattern, &rows, &count))
        return false;

    /* Candidate rows already belong to arena, including the scan fallback.
     * Compact the vector in place to hold their instantiated templates. */
    size_t found = 0;
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    for (size_t i = 0; i < count; ++i) {
        Atom *row = rows[i];
        if (atom_has_vars(row)) {
            CettaFrameIdentity fresh =
                cetta_frame_identity_scope_fresh(&frame_identity_scope);
            row = atom_freshen_epoch(arena, row, fresh);
            if (!row)
                continue;
        }
        Bindings env;
        bindings_init(&env);
        if (space_alias_match_pattern(s, arena, pattern, row, &env)) {
            Atom *instance = bindings_apply_if_vars(&env, arena, template);
            if (instance)
                rows[found++] = instance;
        }
        bindings_free(&env);
    }
    *out = rows;
    *out_count = found;
    return true;
}

/* ── Class Members for eq:class ─────────────────────────────────────────── */

bool space_alias_class_members(Space *s, Arena *arena, Atom *term,
                               Atom ***out_members, size_t *out_count) {
    if (out_members)
        *out_members = NULL;
    if (out_count)
        *out_count = 0u;
    if (!s || !arena || !term || !out_members || !out_count)
        return false;
    Atom **members = NULL;
    size_t count = 0u;
    if (space_may_have_equational_aliases(s)) {
        CETTA_SCOPED_SPACE_OBSERVE(alias_obs, s);
        SPACE_MAINTAIN(maint, s);
        SpaceAliasTable *t = space_alias_ensure_locked(s);
        if (t && t->active_edge_count > 0u &&
            term_touches_carrier_locked(s, t, term, 0u)) {
            Atom *norm = normalize_atom_locked(s, t, arena, term, 0u);
            AtomId id = term_universe_lookup_atom_id(s->native.universe, norm);
            const ClassSlot *slot = class_find(t, id);
            if (slot) {
                members = cetta_malloc(sizeof(*members) * slot->count);
                for (uint32_t i = 0u; i < slot->count; i++) {
                    Atom *m = term_universe_get_atom(
                        s->native.universe, t->class_members[slot->offset + i]);
                    if (m)
                        members[count++] = m;
                }
            }
        }
    }
    if (count == 0u) {
        free(members);
        members = cetta_malloc(sizeof(*members));
        members[count++] = term;
    }
    *out_members = members;
    *out_count = count;
    return true;
}
