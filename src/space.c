#include <time.h>
#include "space.h"
#include "grounded.h"
#include "search_machine.h"
#include "stats.h"
#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local SpaceMatchBackendError g_space_match_backend_error =
    SPACE_MATCH_BACKEND_ERROR_NONE;
static _Thread_local uint64_t
    g_space_match_backend_packet_materialization_limit_override = 0;
static _Thread_local uint64_t
    g_space_match_backend_contextual_query_slot_limit_override = 0;
static _Thread_local CettaCount g_query_results_capacity_limit_override = 0;
static _Atomic uint64_t g_space_next_instance_id = 1u;
static _Atomic uint64_t g_space_global_mutation_epoch = 0u;

static uint64_t space_fresh_instance_id(void) {
    uint64_t id = atomic_fetch_add_explicit(
        &g_space_next_instance_id, 1u, memory_order_relaxed);
    if (id == 0u || id == UINT64_MAX) {
        fputs("CeTTa: exhausted process-local Space identities\n", stderr);
        abort();
    }
    return id;
}

static uint64_t space_match_backend_atom_id_materialization_limit(void) {
    return (uint64_t)(SIZE_MAX / sizeof(AtomId));
}

const char *space_match_backend_error_name(SpaceMatchBackendError code) {
    switch (code) {
    case SPACE_MATCH_BACKEND_ERROR_NONE:
        return "None";
    case SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE:
        return "NativeSpaceTooLarge";
    case SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE:
        return "PacketTooLarge";
    }
    return "UnknownSpaceMatchBackendError";
}

void space_match_backend_clear_error(void) {
    g_space_match_backend_error = SPACE_MATCH_BACKEND_ERROR_NONE;
}

void space_match_backend_set_error(SpaceMatchBackendError code) {
    g_space_match_backend_error = code;
}

SpaceMatchBackendError space_match_backend_last_error_code(void) {
    return g_space_match_backend_error;
}

const char *space_match_backend_last_error(void) {
    return space_match_backend_error_name(g_space_match_backend_error);
}

uint64_t space_match_backend_native_materialization_limit(void) {
    return space_match_backend_atom_id_materialization_limit();
}

uint64_t space_match_backend_packet_materialization_limit(void) {
    if (g_space_match_backend_packet_materialization_limit_override != 0)
        return g_space_match_backend_packet_materialization_limit_override;
    return space_match_backend_atom_id_materialization_limit();
}

uint64_t space_match_backend_contextual_query_slot_limit(void) {
    if (g_space_match_backend_contextual_query_slot_limit_override != 0)
        return g_space_match_backend_contextual_query_slot_limit_override;
    return UINT16_MAX;
}

void space_match_backend_diag_set_contextual_query_slot_limit_override(
    uint64_t limit) {
    g_space_match_backend_contextual_query_slot_limit_override = limit;
}

void space_match_backend_diag_set_packet_materialization_limit_override(
    uint64_t limit) {
    g_space_match_backend_packet_materialization_limit_override = limit;
}

void space_match_backend_diag_reset(void) {
    g_space_match_backend_packet_materialization_limit_override = 0;
    g_space_match_backend_contextual_query_slot_limit_override = 0;
}

static uint64_t space_match_backend_limit_for_error(SpaceMatchBackendError error) {
    switch (error) {
    case SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE:
        return space_match_backend_native_materialization_limit();
    case SPACE_MATCH_BACKEND_ERROR_PACKET_TOO_LARGE:
        return space_match_backend_packet_materialization_limit();
    case SPACE_MATCH_BACKEND_ERROR_NONE:
        return UINT64_MAX;
    }
    return UINT64_MAX;
}

bool space_match_backend_u32_bound_checked(uint64_t value,
                                           SpaceMatchBackendError error,
                                           uint32_t *out_value) {
    uint64_t limit = space_match_backend_limit_for_error(error);
    if (out_value)
        *out_value = 0;
    if (limit > UINT32_MAX)
        limit = UINT32_MAX;
    if (value > limit) {
        space_match_backend_set_error(error);
        return false;
    }
    if (out_value)
        *out_value = (uint32_t)value;
    space_match_backend_clear_error();
    return true;
}

/* ── Discrimination Trie ────────────────────────────────────────────────── */

DiscNode *disc_node_new(void) {
    DiscNode *n = cetta_malloc(sizeof(DiscNode));
    memset(n, 0, sizeof(DiscNode));
    return n;
}

void disc_node_free(DiscNode *n) {
    if (!n) return;
    if (n->sym_hashed) {
        uint32_t cap = n->sym_ht.mask + 1;
        for (uint32_t i = 0; i < cap; i++)
            if (n->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                disc_node_free(n->sym_ht.entries[i].child);
        free(n->sym_ht.entries);
    } else {
        for (uint32_t i = 0; i < n->nsym; i++) disc_node_free(n->sym[i].child);
        free(n->sym);
    }
    disc_node_free(n->var_child);
    for (uint32_t i = 0; i < n->nexpr; i++) disc_node_free(n->expr[i].child);
    free(n->expr);
    for (uint32_t i = 0; i < n->nints; i++) disc_node_free(n->ints[i].child);
    free(n->ints);
    free(n->leaves);
    free(n);
}

static void disc_add_leaf(DiscNode *n, CettaIndex idx) {
    if (n->nleaves >= n->cleaves) {
        n->cleaves = n->cleaves ? n->cleaves * 2 : 4;
        n->leaves = cetta_realloc(n->leaves, sizeof(CettaIndex) * n->cleaves);
    }
    n->leaves[n->nleaves++] = idx;
}

static inline uint32_t disc_sym_hash(SymbolId key) {
    return (uint32_t)((uint64_t)key * 2654435761u);
}

static void disc_sym_ht_init(DiscSymHashTable *ht, uint32_t min_cap) {
    uint32_t cap = 32;
    while (cap < min_cap * 2)
        cap *= 2;
    ht->entries = cetta_malloc(sizeof(DiscSymHashEntry) * cap);
    ht->mask = cap - 1;
    ht->count = 0;
    for (uint32_t i = 0; i < cap; i++)
        ht->entries[i].key = SYMBOL_ID_NONE;
}

static DiscNode *disc_sym_ht_get(DiscSymHashTable *ht, SymbolId key) {
    uint32_t idx = disc_sym_hash(key) & ht->mask;
    for (;;) {
        if (ht->entries[idx].key == key)
            return ht->entries[idx].child;
        if (ht->entries[idx].key == SYMBOL_ID_NONE)
            return NULL;
        idx = (idx + 1) & ht->mask;
    }
}

static void disc_sym_ht_put(DiscSymHashTable *ht, SymbolId key, DiscNode *child) {
    if (ht->count * 10 > (ht->mask + 1) * 7) {
        uint32_t old_cap = ht->mask + 1;
        DiscSymHashEntry *old = ht->entries;
        uint32_t new_cap = old_cap * 2;
        ht->entries = cetta_malloc(sizeof(DiscSymHashEntry) * new_cap);
        ht->mask = new_cap - 1;
        ht->count = 0;
        for (uint32_t i = 0; i < new_cap; i++)
            ht->entries[i].key = SYMBOL_ID_NONE;
        for (uint32_t i = 0; i < old_cap; i++) {
            if (old[i].key != SYMBOL_ID_NONE)
                disc_sym_ht_put(ht, old[i].key, old[i].child);
        }
        free(old);
    }
    uint32_t idx = disc_sym_hash(key) & ht->mask;
    while (ht->entries[idx].key != SYMBOL_ID_NONE)
        idx = (idx + 1) & ht->mask;
    ht->entries[idx].key = key;
    ht->entries[idx].child = child;
    ht->count++;
}

static void disc_sym_promote(DiscNode *n) {
    DiscSymBranch *old_sym = n->sym;
    uint32_t count = n->nsym;
    disc_sym_ht_init(&n->sym_ht, count + 16);
    for (uint32_t i = 0; i < count; i++)
        disc_sym_ht_put(&n->sym_ht, old_sym[i].key, old_sym[i].child);
    free(old_sym);
    n->sym = NULL;
    n->csym = 0;
    n->sym_hashed = true;
}

static DiscNode *disc_get_sym(DiscNode *n, SymbolId key) {
    if (n->sym_hashed) {
        DiscNode *existing = disc_sym_ht_get(&n->sym_ht, key);
        if (existing) return existing;
        DiscNode *child = disc_node_new();
        disc_sym_ht_put(&n->sym_ht, key, child);
        n->nsym++;
        return child;
    }
    for (uint32_t i = 0; i < n->nsym; i++)
        if (n->sym[i].key == key) return n->sym[i].child;
    if (n->nsym >= n->csym) {
        n->csym = n->csym ? n->csym * 2 : 4;
        n->sym = cetta_realloc(n->sym, sizeof(n->sym[0]) * n->csym);
    }
    DiscNode *child = disc_node_new();
    n->sym[n->nsym].key = key;
    n->sym[n->nsym].child = child;
    n->nsym++;
    if (n->nsym > DISC_HASH_THRESHOLD)
        disc_sym_promote(n);
    return child;
}

static DiscNode *disc_get_var(DiscNode *n) {
    if (!n->var_child) n->var_child = disc_node_new();
    return n->var_child;
}

static DiscNode *disc_get_expr(DiscNode *n, CettaExprLen arity) {
    for (uint32_t i = 0; i < n->nexpr; i++)
        if (n->expr[i].arity == arity) return n->expr[i].child;
    if (n->nexpr >= n->cexpr) {
        n->cexpr = n->cexpr ? n->cexpr * 2 : 4;
        n->expr = cetta_realloc(n->expr, sizeof(n->expr[0]) * n->cexpr);
    }
    DiscNode *child = disc_node_new();
    n->expr[n->nexpr].arity = arity;
    n->expr[n->nexpr].child = child;
    n->nexpr++;
    return child;
}

static DiscNode *disc_get_int(DiscNode *n, int64_t val) {
    for (uint32_t i = 0; i < n->nints; i++)
        if (n->ints[i].val == val) return n->ints[i].child;
    if (n->nints >= n->cints) {
        n->cints = n->cints ? n->cints * 2 : 4;
        n->ints = cetta_realloc(n->ints, sizeof(n->ints[0]) * n->cints);
    }
    DiscNode *child = disc_node_new();
    n->ints[n->nints].val = val;
    n->ints[n->nints].child = child;
    n->nints++;
    return child;
}

/* Insert: walk LHS depth-first, creating trie path */
static DiscNode *disc_insert_atom(DiscNode *node, Atom *a) {
    switch (a->kind) {
    case ATOM_SYMBOL: return disc_get_sym(node, a->sym_id);
    case ATOM_VAR:    return disc_get_var(node);
    case ATOM_GROUNDED:
        if (a->ground.gkind == GV_INT) return disc_get_int(node, a->ground.ival);
        return disc_get_var(node); /* treat other grounded as wildcard for now */
    case ATOM_EXPR: {
        DiscNode *cur = disc_get_expr(node, a->expr.len);
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            cur = disc_insert_atom(cur, a->expr.elems[i]);
        return cur;
    }
    }
    return node;
}

static bool disc_insert_atom_id(DiscNode *node, const TermUniverse *universe,
                                AtomId atom_id, DiscNode **out_leaf) {
    if (!node || !universe || atom_id == CETTA_ATOM_ID_NONE ||
        !tu_hdr(universe, atom_id) || !out_leaf) {
        return false;
    }

    switch (tu_kind(universe, atom_id)) {
    case ATOM_SYMBOL:
        *out_leaf = disc_get_sym(node, tu_sym(universe, atom_id));
        return true;
    case ATOM_VAR:
        *out_leaf = disc_get_var(node);
        return true;
    case ATOM_GROUNDED:
        if (tu_ground_kind(universe, atom_id) == GV_INT) {
            *out_leaf = disc_get_int(node, tu_int(universe, atom_id));
        } else {
            *out_leaf = disc_get_var(node);
        }
        return true;
    case ATOM_EXPR: {
        DiscNode *cur = disc_get_expr(node, tu_arity(universe, atom_id));
        for (CettaExprIndex i = 0; i < tu_arity(universe, atom_id); i++) {
            AtomId child_id = tu_child(universe, atom_id, i);
            if (!disc_insert_atom_id(cur, universe, child_id, &cur))
                return false;
        }
        *out_leaf = cur;
        return true;
    }
    }
    return false;
}

void disc_insert(DiscNode *root, Atom *lhs, CettaIndex eq_idx) {
    DiscNode *leaf = disc_insert_atom(root, lhs);
    disc_add_leaf(leaf, eq_idx);
}

bool disc_insert_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, CettaIndex eq_idx) {
    DiscNode *leaf = NULL;
    if (!disc_insert_atom_id(root, universe, atom_id, &leaf))
        return false;
    disc_add_leaf(leaf, eq_idx);
    return true;
}

/* ── Discrimination Trie Lookup (node-set based) ──────────────────────── */

/* A dynamic set of trie nodes — used during lookup to track all reachable
   positions in the trie after matching a query atom.  The depth-first
   flattening during insertion means that:
     symbol/var/int → 1 trie step
     expression(arity) → 1 step (arity branch) + arity recursive sub-terms
   Lookup must mirror this structure exactly. */

typedef struct {
    DiscNode **nodes;
    uint32_t n, c;
} DiscNodeSet;

static void dns_init(DiscNodeSet *s) { s->nodes = NULL; s->n = 0; s->c = 0; }
static void dns_free(DiscNodeSet *s) { free(s->nodes); s->nodes = NULL; s->n = 0; s->c = 0; }

static void dns_push(DiscNodeSet *s, DiscNode *node) {
    if (!node) return;
    if (s->n >= s->c) {
        s->c = s->c ? s->c * 2 : 8;
        s->nodes = cetta_realloc(s->nodes, sizeof(DiscNode *) * s->c);
    }
    s->nodes[s->n++] = node;
}

/* Forward declarations for mutual recursion */
static void disc_step(DiscNode *node, Atom *q, DiscNodeSet *next);
static void disc_skip_term(DiscNode *node, DiscNodeSet *next);

/* Skip one complete term from the trie.  A query variable can match any
   indexed term, so we must advance past the entire depth-first encoding
   of whatever term appears at this position. */
static void disc_skip_term(DiscNode *node, DiscNodeSet *next) {
    if (!node) return;
    /* Symbol branches: one trie step → child is the continuation */
    if (node->sym_hashed) {
        uint32_t cap = node->sym_ht.mask + 1;
        for (uint32_t i = 0; i < cap; i++)
            if (node->sym_ht.entries[i].key != SYMBOL_ID_NONE)
                dns_push(next, node->sym_ht.entries[i].child);
    } else {
        for (uint32_t i = 0; i < node->nsym; i++)
            dns_push(next, node->sym[i].child);
    }
    /* Variable branches: one trie step */
    dns_push(next, node->var_child);
    /* Int branches: one trie step */
    for (uint32_t i = 0; i < node->nints; i++)
        dns_push(next, node->ints[i].child);
    /* Expression branches: arity tag + arity sub-terms (depth-first) */
    for (uint32_t i = 0; i < node->nexpr; i++) {
        DiscNodeSet cur;
        dns_init(&cur);
        dns_push(&cur, node->expr[i].child);
        for (CettaExprIndex ci = 0; ci < node->expr[i].arity; ci++) {
            DiscNodeSet tmp;
            dns_init(&tmp);
            for (uint32_t ni = 0; ni < cur.n; ni++)
                disc_skip_term(cur.nodes[ni], &tmp);
            dns_free(&cur);
            cur = tmp;
        }
        /* After skipping all sub-terms, cur holds the continuations */
        for (uint32_t ni = 0; ni < cur.n; ni++)
            dns_push(next, cur.nodes[ni]);
        dns_free(&cur);
    }
}

/* Advance through one query atom, collecting all reachable next-nodes.
   Mirrors the depth-first structure of disc_insert_atom exactly. */
static void disc_step(DiscNode *node, Atom *q, DiscNodeSet *next) {
    if (!node) return;
    switch (q->kind) {
    case ATOM_SYMBOL:
        if (node->sym_hashed) {
            DiscNode *child = disc_sym_ht_get(&node->sym_ht, q->sym_id);
            dns_push(next, child);
        } else {
            for (uint32_t i = 0; i < node->nsym; i++)
                if (node->sym[i].key == q->sym_id)
                    dns_push(next, node->sym[i].child);
        }
        /* A variable in the indexed LHS matches any query symbol */
        dns_push(next, node->var_child);
        break;

    case ATOM_VAR:
        /* Query variable matches any indexed term — skip one complete term */
        disc_skip_term(node, next);
        break;

    case ATOM_GROUNDED:
        if (q->ground.gkind == GV_INT) {
            for (uint32_t i = 0; i < node->nints; i++)
                if (node->ints[i].val == q->ground.ival)
                    dns_push(next, node->ints[i].child);
        }
        /* A variable in the indexed LHS matches any grounded value */
        dns_push(next, node->var_child);
        break;

    case ATOM_EXPR:
        /* Match expression by arity, then chain depth-first through children */
        for (uint32_t i = 0; i < node->nexpr; i++) {
            if (node->expr[i].arity == q->expr.len) {
                /* Start with the arity branch's child, then advance through
                   each sub-element of the expression */
                DiscNodeSet cur;
                dns_init(&cur);
                dns_push(&cur, node->expr[i].child);
                for (CettaExprIndex ci = 0; ci < q->expr.len; ci++) {
                    DiscNodeSet tmp;
                    dns_init(&tmp);
                    for (uint32_t ni = 0; ni < cur.n; ni++)
                        disc_step(cur.nodes[ni], q->expr.elems[ci], &tmp);
                    dns_free(&cur);
                    cur = tmp;
                }
                /* After all children: cur holds the terminal nodes */
                for (uint32_t ni = 0; ni < cur.n; ni++)
                    dns_push(next, cur.nodes[ni]);
                dns_free(&cur);
            }
        }
        /* A variable in the indexed LHS matches any expression */
        dns_push(next, node->var_child);
        break;
    }
}

void disc_lookup(DiscNode *root, Atom *query, CettaIndex **out,
                 CettaIndex *nout, CettaIndex *cout) {
    *out = NULL; *nout = 0; *cout = 0;
    /* disc_step from root through the query atom */
    DiscNodeSet final;
    dns_init(&final);
    disc_step(root, query, &final);
    /* Collect equation indices from all terminal nodes' leaves */
    for (uint32_t i = 0; i < final.n; i++) {
        DiscNode *n = final.nodes[i];
        for (CettaIndex j = 0; j < n->nleaves; j++) {
            if (*nout >= *cout) {
                *cout = *cout ? *cout * 2 : 16;
                *out = cetta_realloc(*out, sizeof(CettaIndex) * *cout);
            }
            (*out)[(*nout)++] = n->leaves[j];
        }
    }
    dns_free(&final);
}

/* ── Equation Index ─────────────────────────────────────────────────────── */

static bool __attribute__((unused)) atom_is_eq_subst_safe(Atom *atom);
static bool atom_id_is_eq_subst_safe(const Space *s, AtomId atom_id);
static SymbolId eq_head_symbol(Atom *lhs);
static SymbolId eq_head_symbol_id(const Space *s, AtomId lhs_id);

static void eq_bucket_init(EqBucket *b) {
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
    b->trie = NULL;
    stree_bucket_init(&b->subst);
    b->head = SYMBOL_ID_NONE;
    b->mixed_heads = false;
    b->subst_safe = true;
}

static void eq_bucket_note_head(EqBucket *b, SymbolId head) {
    if (!b || head == SYMBOL_ID_NONE)
        return;
    if (b->head == SYMBOL_ID_NONE) {
        b->head = head;
    } else if (b->head != head) {
        b->mixed_heads = true;
    }
}

static void eq_bucket_add(EqBucket *b, Atom *lhs, CettaIndex atom_idx) {
    CettaIndex idx = b->len;
    SymbolId head = eq_head_symbol(lhs);
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(CettaIndex) * b->cap);
    }
    b->atom_indices[b->len] = atom_idx;
    b->len++;
    eq_bucket_note_head(b, head);
    /* Add to discrimination trie */
    if (!b->trie) b->trie = disc_node_new();
    disc_insert(b->trie, lhs, idx);
    stree_bucket_insert(&b->subst, lhs, idx);
    b->subst_safe = b->subst_safe && atom_is_eq_subst_safe(lhs);
}

static void eq_bucket_add_id(EqBucket *b, const Space *s, AtomId lhs_id,
                             CettaIndex atom_idx) {
    SymbolId head = eq_head_symbol_id(s, lhs_id);
    if (!b || !s || !s->native.universe || lhs_id == CETTA_ATOM_ID_NONE ||
        !tu_hdr(s->native.universe, lhs_id)) {
        Atom *lhs = (s && s->native.universe)
                        ? term_universe_get_atom(s->native.universe, lhs_id)
                        : NULL;
        if (lhs)
            eq_bucket_add(b, lhs, atom_idx);
        return;
    }
    CettaIndex idx = b->len;
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(CettaIndex) * b->cap);
    }
    b->atom_indices[b->len] = atom_idx;
    b->len++;
    eq_bucket_note_head(b, head);
    if (!b->trie)
        b->trie = disc_node_new();
    if (!disc_insert_id(b->trie, s->native.universe, lhs_id, idx)) {
        Atom *lhs = term_universe_get_atom(s->native.universe, lhs_id);
        if (lhs)
            disc_insert(b->trie, lhs, idx);
    }
    if (!stree_bucket_insert_id(&b->subst, s->native.universe, lhs_id, idx)) {
        Atom *lhs = term_universe_get_atom(s->native.universe, lhs_id);
        if (lhs)
            stree_bucket_insert(&b->subst, lhs, idx);
    }
    b->subst_safe = b->subst_safe && atom_id_is_eq_subst_safe(s, lhs_id);
}

static void eq_bucket_free(EqBucket *b) {
    free(b->atom_indices);
    disc_node_free(b->trie); b->trie = NULL;
    stree_bucket_free(&b->subst);
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
    b->head = SYMBOL_ID_NONE;
    b->mixed_heads = false;
    b->subst_safe = true;
}

static uint32_t symbol_hash(SymbolId id) {
    /*
     * Bucket placement must be a function of the symbol, not of when it was
     * interned.  Otherwise loading an unrelated language first renumbers
     * SymbolIds and changes equation/type-index collision work in every other
     * language.  The symbol table already records a spelling-stable 64-bit
     * hash; avalanche its folded value only at the bucket boundary.
     */
    uint64_t stable = symbol_hash_value(g_symbols, id);
    uint32_t mixed = (uint32_t)stable ^ (uint32_t)(stable >> 32);
    mixed ^= mixed >> 16;
    mixed *= 0x85ebca6bu;
    mixed ^= mixed >> 13;
    mixed *= 0xc2b2ae35u;
    mixed ^= mixed >> 16;
    return mixed % EQ_INDEX_BUCKETS;
}

static bool __attribute__((unused)) atom_is_eq_subst_safe(Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        return atom->ground.gkind == GV_INT ||
               atom->ground.gkind == GV_BIGINT ||
               atom->ground.gkind == GV_RATIONAL ||
               atom->ground.gkind == GV_STRING;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!atom_is_eq_subst_safe(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool atom_id_is_eq_subst_safe(const Space *s, AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
    if (!hdr) {
        Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
        return atom ? atom_is_eq_subst_safe(atom) : false;
    }
    switch (tu_kind(s->native.universe, atom_id)) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        return tu_ground_kind(s->native.universe, atom_id) == GV_INT ||
               tu_ground_kind(s->native.universe, atom_id) == GV_BIGINT ||
               tu_ground_kind(s->native.universe, atom_id) == GV_RATIONAL ||
               tu_ground_kind(s->native.universe, atom_id) == GV_STRING;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < tu_arity(s->native.universe, atom_id); i++) {
            if (!atom_id_is_eq_subst_safe(s, tu_child(s->native.universe, atom_id, i)))
                return false;
        }
        return true;
    }
    return false;
}

/* Get the head symbol of an equation LHS for indexing.
   Returns NULL if the head is not a symbol (variable, complex expr). */
static SymbolId eq_head_symbol(Atom *lhs) {
    if (lhs->kind == ATOM_SYMBOL) return lhs->sym_id;
    if (lhs->kind == ATOM_EXPR && lhs->expr.len > 0 &&
        lhs->expr.elems[0]->kind == ATOM_SYMBOL)
        return lhs->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

static SymbolId eq_head_symbol_id(const Space *s, AtomId lhs_id) {
    if (!s || !s->native.universe || lhs_id == CETTA_ATOM_ID_NONE)
        return SYMBOL_ID_NONE;
    return tu_head_sym(s->native.universe, lhs_id);
}

static void eq_index_init(EqIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        eq_bucket_init(&idx->buckets[i]);
    eq_bucket_init(&idx->wildcard);
}

static void eq_index_free(EqIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        eq_bucket_free(&idx->buckets[i]);
    eq_bucket_free(&idx->wildcard);
}

static void eq_index_add(EqIndex *idx, Atom *lhs, CettaIndex atom_idx) {
    SymbolId head = eq_head_symbol(lhs);
    if (head != SYMBOL_ID_NONE) {
        eq_bucket_add(&idx->buckets[symbol_hash(head)], lhs, atom_idx);
    } else {
        eq_bucket_add(&idx->wildcard, lhs, atom_idx);
    }
}

static void eq_index_add_id(EqIndex *idx, const Space *s, AtomId lhs_id,
                            CettaIndex atom_idx) {
    SymbolId head = eq_head_symbol_id(s, lhs_id);
    if (head != SYMBOL_ID_NONE) {
        eq_bucket_add_id(&idx->buckets[symbol_hash(head)], s, lhs_id, atom_idx);
    } else {
        eq_bucket_add_id(&idx->wildcard, s, lhs_id, atom_idx);
    }
}

/* ── Type Annotation Index ──────────────────────────────────────────────── */

static void ty_ann_bucket_init(TypeAnnBucket *b) {
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
}
static void ty_ann_bucket_add(TypeAnnBucket *b, CettaIndex atom_idx) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(CettaIndex) * b->cap);
    }
    b->atom_indices[b->len] = atom_idx;
    b->len++;
}
static void ty_ann_bucket_free(TypeAnnBucket *b) {
    free(b->atom_indices);
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
}

static uint32_t atom_hash_for_index(Atom *a) {
    /* Hash by name for symbols, or by first element for expressions */
    if (a->kind == ATOM_SYMBOL) return symbol_hash(a->sym_id);
    if (a->kind == ATOM_EXPR && a->expr.len > 0 && a->expr.elems[0]->kind == ATOM_SYMBOL)
        return symbol_hash(a->expr.elems[0]->sym_id);
    return 0;
}

static uint32_t atom_hash_for_index_id(const Space *s, AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return 0;
    if (tu_hdr(s->native.universe, atom_id)) {
        if (tu_kind(s->native.universe, atom_id) == ATOM_SYMBOL)
            return symbol_hash(tu_sym(s->native.universe, atom_id));
        if (tu_kind(s->native.universe, atom_id) == ATOM_EXPR &&
            tu_arity(s->native.universe, atom_id) > 0) {
            SymbolId head = tu_head_sym(s->native.universe, atom_id);
            return head == SYMBOL_ID_NONE ? 0u : symbol_hash(head);
        }
        return 0u;
    }
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    return atom ? atom_hash_for_index(atom) : 0u;
}

static void ty_ann_index_init(TypeAnnIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        ty_ann_bucket_init(&idx->buckets[i]);
}
static void ty_ann_index_free(TypeAnnIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        ty_ann_bucket_free(&idx->buckets[i]);
}
static void ty_ann_index_add(TypeAnnIndex *idx, Atom *ann_atom, CettaIndex atom_idx) {
    uint32_t h = atom_hash_for_index(ann_atom);
    ty_ann_bucket_add(&idx->buckets[h], atom_idx);
}

static void ty_ann_index_add_id(TypeAnnIndex *idx, const Space *s,
                                AtomId subject_id, CettaIndex atom_idx) {
    ty_ann_bucket_add(&idx->buckets[atom_hash_for_index_id(s, subject_id)], atom_idx);
}

static void exact_atom_bucket_init(ExactAtomBucket *b) {
    b->indices = NULL;
    b->len = 0;
    b->cap = 0;
}

static void exact_atom_bucket_free(ExactAtomBucket *b) {
    free(b->indices);
    b->indices = NULL;
    b->len = 0;
    b->cap = 0;
}

static void exact_atom_bucket_add(ExactAtomBucket *b, CettaIndex idx) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4;
        b->indices = cetta_realloc(b->indices, sizeof(CettaIndex) * b->cap);
    }
    b->indices[b->len++] = idx;
}

static void exact_index_init(ExactAtomIndex *idx) {
    for (uint32_t i = 0; i < EXACT_INDEX_BUCKETS; i++)
        exact_atom_bucket_init(&idx->buckets[i]);
}

static void exact_index_free(ExactAtomIndex *idx) {
    for (uint32_t i = 0; i < EXACT_INDEX_BUCKETS; i++)
        exact_atom_bucket_free(&idx->buckets[i]);
}

/* Dense AtomId presence bitset -- doctrine-1 exact-membership at the store seam.
   Interned AtomIds are sequential, so a grown bitset gives O(1) unclusterable
   exact-contains, replacing the fixed-bucket structural walk on the hot path. */
static bool id_present_contains(const SpaceNativeStorage *ns, AtomId id) {
    uint64_t bit = (uint64_t)id;
    return ns->id_present && bit < ns->id_present_bits &&
           ((ns->id_present[bit >> 3] >> (bit & 7u)) & 1u);
}

static bool id_present_set(SpaceNativeStorage *ns, AtomId id) {
    uint64_t bit = (uint64_t)id;
    if (bit >= ns->id_present_bits) {
        uint64_t new_bits = ns->id_present_bits ? ns->id_present_bits : 4096u;
        while (new_bits <= bit) {
            if (new_bits > UINT64_MAX / 2u)
                return false;
            new_bits <<= 1;
        }
        uint64_t new_bytes = new_bits >> 3;
        uint64_t old_bytes = ns->id_present_bits >> 3;
        if (new_bytes > SIZE_MAX)
            return false;
        uint8_t *grown =
            (uint8_t *)cetta_realloc(ns->id_present, (size_t)new_bytes);
        if (!grown)
            return false;
        memset(grown + (size_t)old_bytes, 0,
               (size_t)(new_bytes - old_bytes));
        ns->id_present = grown;
        ns->id_present_bits = new_bits;
    }
    ns->id_present[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
    return true;
}

static void id_present_clear(SpaceNativeStorage *ns) {
    if (ns->id_present)
        memset(ns->id_present, 0, (size_t)(ns->id_present_bits >> 3));
}

static void id_present_free(SpaceNativeStorage *ns) {
    free(ns->id_present);
    ns->id_present = NULL;
    ns->id_present_bits = 0;
}

/* Reusable per-thread scratch for canonicalization/interning fallback, reset
   (not freed) per call so the hot dedup path pays no arena init/free churn. */
static _Thread_local Arena g_canon_scratch;
static _Thread_local bool g_canon_scratch_ready = false;
static Arena *canon_scratch(void) {
    if (!g_canon_scratch_ready) {
        arena_init(&g_canon_scratch);
        g_canon_scratch_ready = true;
    }
    return &g_canon_scratch;
}

/* Alpha-canonical AtomId of a STORED atom (maintenance).  Ground atoms are their
   own canonical form (their exact id); non-ground atoms canonicalize their vars
   to first-occurrence ordinals and intern that form, so every alpha-variant of a
   theorem maps to one id.  The presence bitset then answers alpha-aware
   containment as an O(1) id predicate. */
static AtomId space_canonical_id_for_stored(Space *s, AtomId atom_id) {
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    if (!atom)
        return CETTA_ATOM_ID_NONE;
    if (!atom_has_vars(atom))
        return atom_id;
    Arena *scratch = canon_scratch();
    ArenaMark mark = arena_mark(scratch);
    Atom *canonical =
        term_universe_alpha_canonicalize_atom(scratch, atom);
    AtomId cid = canonical
        ? term_universe_store_atom_id(s->native.universe, scratch, canonical)
        : CETTA_ATOM_ID_NONE;
    arena_reset(scratch, mark);
    return cid;
}

/* Alpha-canonical AtomId of a QUERY atom (contains).  Find-only, so a query miss
   stays a miss without growing the universe. */
static AtomId space_canonical_id_for_query(Space *s, Atom *atom) {
    if (!s || !atom || !s->native.universe)
        return CETTA_ATOM_ID_NONE;
    if (!atom_has_vars(atom))
        return term_universe_lookup_atom_id(s->native.universe, atom);
    Arena *scratch = canon_scratch();
    ArenaMark mark = arena_mark(scratch);
    Atom *canon = term_universe_alpha_canonicalize_atom(scratch, atom);
    AtomId cid = canon
        ? term_universe_lookup_atom_id(s->native.universe, canon)
        : CETTA_ATOM_ID_NONE;
    arena_reset(scratch, mark);
    return cid;
}

static bool atom_has_variables(const Atom *atom) {
    if (!atom) return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (atom_has_variables(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool atom_is_exact_indexable(const Atom *atom) {
    if (!atom) return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
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
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!atom_is_exact_indexable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static uint32_t exact_atom_hash(Atom *atom) {
    return atom_hash(atom) % EXACT_INDEX_BUCKETS;
}

static bool atom_id_is_exact_indexable(const Space *s, AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
    if (!hdr)
        return atom_is_exact_indexable(term_universe_get_atom(s->native.universe, atom_id));
    switch (tu_kind(s->native.universe, atom_id)) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        switch ((GroundedKind)hdr->subtag) {
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
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        return false;
    case ATOM_EXPR:
        return !tu_has_vars(s->native.universe, atom_id);
    }
    return false;
}

static uint32_t exact_atom_hash_id(const Space *s, AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return 0;
    const CettaTermHdr *hdr = tu_hdr(s->native.universe, atom_id);
    if (hdr)
        return tu_hash32(s->native.universe, atom_id) % EXACT_INDEX_BUCKETS;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    return atom ? exact_atom_hash(atom) : 0u;
}

static void eq_index_rebuild(Space *s);
static void ty_ann_index_rebuild(Space *s);
static void exact_index_rebuild(Space *s);
static void recompute_has_non_exact_atoms(Space *s);

static TermUniverse g_space_default_universe = {0};
static Arena g_space_default_arena = {0};
static bool g_space_default_universe_ready = false;

static TermUniverse *space_default_universe(void) {
    if (!g_space_default_universe_ready) {
        arena_init(&g_space_default_arena);
        arena_set_runtime_kind(&g_space_default_arena,
                               CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
        term_universe_init(&g_space_default_universe);
        term_universe_set_persistent_arena(&g_space_default_universe,
                                           &g_space_default_arena);
        g_space_default_universe_ready = true;
    }
    return &g_space_default_universe;
}

static bool space_tracks_atom_ids(const Space *s) {
    return s && s->native.universe != NULL;
}

static ImportedBridgeState *space_imported_bridge_state(Space *s) {
    if (!s)
        return NULL;
    switch (s->match_backend.kind) {
    case SPACE_ENGINE_PATHMAP:
        return &s->match_backend.pathmap.bridge;
    case SPACE_ENGINE_MORK:
        return &s->match_backend.mork.bridge;
    default:
        return NULL;
    }
}

static uint32_t space_atom_id_width_bits_for_format(
    TermUniverseStoreFormat format) {
    return term_universe_store_format_atom_id_width_bits(format);
}

static uint32_t space_atom_id_width_bits_for_universe(
    const TermUniverse *universe) {
    return space_atom_id_width_bits_for_format(term_universe_store_format(universe));
}

static size_t space_atom_id_width_bytes_bits(uint32_t bits) {
    return cetta_atom_id_storage_width_bytes_from_bits(bits);
}

static AtomId space_atom_id_storage_load_at(const uint8_t *storage,
                                            uint32_t bits,
                                            CettaIndex idx) {
    size_t width = space_atom_id_width_bytes_bits(bits);
    if (!storage || width == 0)
        return CETTA_ATOM_ID_NONE;
    return cetta_atom_id_storage_load_bits(storage + ((size_t)idx * width),
                                           bits);
}

static bool space_atom_id_storage_store_at(uint8_t *storage,
                                           uint32_t bits,
                                           CettaIndex idx,
                                           AtomId atom_id) {
    size_t width = space_atom_id_width_bytes_bits(bits);
    if (!storage || width == 0)
        return false;
    return cetta_atom_id_storage_store_bits(
        storage + ((size_t)idx * width), bits, atom_id);
}

static bool space_raw_atom_id_storage_rewrite(uint8_t **storage,
                                              uint8_t *storage_bits,
                                              CettaIndex start,
                                              CettaIndex len,
                                              CettaIndex cap,
                                              uint32_t new_bits) {
    uint32_t old_bits = 0;
    uint8_t *next = NULL;
    size_t new_width = space_atom_id_width_bytes_bits(new_bits);
    if (!storage || !storage_bits)
        return false;
    old_bits = *storage_bits;
    if (old_bits == new_bits)
        return true;
    if (new_width == 0)
        return false;
    if (cap == 0 || !*storage) {
        free(*storage);
        *storage = NULL;
        *storage_bits = (uint8_t)new_bits;
        return true;
    }
    if ((size_t)cap > SIZE_MAX / new_width)
        return false;
    next = cetta_malloc((size_t)cap * new_width);
    if (!next)
        return false;
    memset(next, 0, (size_t)cap * new_width);
    for (CettaIndex i = start; i < start + len; i++) {
        AtomId atom_id = space_atom_id_storage_load_at(*storage, old_bits, i);
        if (!space_atom_id_storage_store_at(next, new_bits, i, atom_id)) {
            free(next);
            return false;
        }
    }
    free(*storage);
    *storage = next;
    *storage_bits = (uint8_t)new_bits;
    return true;
}

static bool space_sync_atom_id_storage_width_bits(Space *s,
                                                  uint32_t want_bits) {
    if (!s || !space_tracks_atom_ids(s))
        return true;
    if (want_bits == 0)
        return false;
    if (s->native.atom_id_width_bits == 0) {
        s->native.atom_id_width_bits = (uint8_t)want_bits;
        return true;
    }
    return space_raw_atom_id_storage_rewrite(
        &s->native.atom_ids, &s->native.atom_id_width_bits, s->native.start,
        s->native.len, s->native.cap, want_bits);
}

static bool space_sync_atom_id_storage_width(Space *s) {
    return space_sync_atom_id_storage_width_bits(
        s, s && s->native.universe
               ? space_atom_id_width_bits_for_universe(s->native.universe)
               : 0);
}

static bool space_sync_projected_atom_id_storage_width_bits(Space *s,
                                                            uint32_t want_bits) {
    ImportedBridgeState *st = NULL;
    if (!s)
        return true;
    st = space_imported_bridge_state(s);
    if (!st)
        return true;
    if (want_bits == 0)
        return false;
    if (st->projected_atom_id_width_bits == 0) {
        st->projected_atom_id_width_bits = (uint8_t)want_bits;
        return true;
    }
    return space_raw_atom_id_storage_rewrite(
        &st->projected_atom_ids, &st->projected_atom_id_width_bits, 0,
        st->projected_len, st->projected_len, want_bits);
}

static bool space_term_universe_store_format_observer(
    TermUniverse *universe,
    TermUniverseStoreFormat old_format,
    TermUniverseStoreFormat new_format,
    void *ctx) {
    Space *space = ctx;
    uint32_t want_bits = space_atom_id_width_bits_for_format(new_format);
    (void)old_format;
    if (!space || space->native.universe != universe)
        return true;
    if (!space_sync_atom_id_storage_width_bits(space, want_bits))
        return false;
    return space_sync_projected_atom_id_storage_width_bits(space, want_bits);
}

static void space_attach_to_universe(Space *s, TermUniverse *universe) {
    if (!s || !universe)
        return;
    (void)term_universe_add_store_format_observer(
        universe, space_term_universe_store_format_observer, s);
}

static void space_detach_from_universe(Space *s) {
    if (!s || !s->native.universe)
        return;
    term_universe_remove_store_format_observer(
        s->native.universe, space_term_universe_store_format_observer, s);
}

static void space_note_atom_id_storage_peak(const Space *s) {
    size_t width = 0;
    if (!s || !space_tracks_atom_ids(s))
        return;
    width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
    (void)width;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_SPACE_ATOM_ID_LIVE_BYTES_PEAK,
        (uint64_t)s->native.len * (uint64_t)width);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_SPACE_ATOM_ID_CAPACITY_BYTES_PEAK,
        (uint64_t)s->native.cap * (uint64_t)width);
}

static bool space_has_overlay_base(const Space *s) {
    return s && s->overlay_base;
}

static void space_mark_indexes_dirty(Space *s);
static void space_bump_revision(Space *s);

static CettaCount space_local_length64(const Space *s) {
    return s ? (CettaCount)space_match_backend_logical_len64(s) : 0;
}

static AtomId space_local_get_atom_id_at64(const Space *s, CettaIndex idx) {
    return s ? space_match_backend_get_atom_id_at64(s, idx) : CETTA_ATOM_ID_NONE;
}

static Atom *space_local_get_at64(const Space *s, CettaIndex idx) {
    return s ? space_match_backend_get_at64(s, idx) : NULL;
}

static CettaIndex space_overlay_removed_lower_bound(const Space *s,
                                                    CettaIndex idx) {
    CettaIndex lo = 0;
    CettaIndex hi = space_has_overlay_base(s)
        ? s->overlay_removed_base_len : 0;
    while (lo < hi) {
        CettaIndex mid = lo + (hi - lo) / 2u;
        if (s->overlay_removed_base_indices[mid] < idx)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

static CettaIndex space_overlay_removed_upper_bound(const Space *s,
                                                    CettaIndex idx) {
    CettaIndex lo = 0;
    CettaIndex hi = space_has_overlay_base(s)
        ? s->overlay_removed_base_len : 0;
    while (lo < hi) {
        CettaIndex mid = lo + (hi - lo) / 2u;
        if (s->overlay_removed_base_indices[mid] <= idx)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

static bool space_overlay_base_index_removed(const Space *s, CettaIndex idx) {
    if (!space_has_overlay_base(s))
        return false;
    CettaIndex slot = space_overlay_removed_lower_bound(s, idx);
    return slot < s->overlay_removed_base_len &&
           s->overlay_removed_base_indices[slot] == idx;
}

static void space_overlay_clear_removed_at_or_after(Space *s, CettaIndex limit) {
    if (!space_has_overlay_base(s))
        return;
    s->overlay_removed_base_len =
        space_overlay_removed_lower_bound(s, limit);
}

static void space_overlay_trim_base_tail(Space *s) {
    if (!space_has_overlay_base(s))
        return;
    while (s->overlay_base_visible_len > 0 &&
           s->overlay_removed_base_len > 0) {
        CettaIndex tail = s->overlay_base_visible_len - 1u;
        CettaIndex last = s->overlay_removed_base_len - 1u;
        if (s->overlay_removed_base_indices[last] != tail)
            break;
        s->overlay_removed_base_len = last;
        s->overlay_base_visible_len = tail;
    }
}

static bool space_overlay_mark_base_removed(Space *s, CettaIndex idx) {
    if (!space_has_overlay_base(s) || idx >= s->overlay_base_visible_len)
        return false;
    CettaIndex slot = space_overlay_removed_lower_bound(s, idx);
    if (slot < s->overlay_removed_base_len &&
        s->overlay_removed_base_indices[slot] == idx) {
        return true;
    }
    if (s->overlay_removed_base_len == s->overlay_removed_base_cap) {
        if (s->overlay_removed_base_cap > UINT64_MAX / 2u)
            return false;
        CettaIndex next_cap =
            s->overlay_removed_base_cap ? s->overlay_removed_base_cap * 2u : 8u;
        if (next_cap > SIZE_MAX / sizeof(CettaIndex))
            return false;
        CettaIndex *next = cetta_realloc(
            s->overlay_removed_base_indices, sizeof(CettaIndex) * (size_t)next_cap);
        if (!next)
            return false;
        s->overlay_removed_base_indices = next;
        s->overlay_removed_base_cap = next_cap;
    }
    if (slot < s->overlay_removed_base_len) {
        memmove(&s->overlay_removed_base_indices[slot + 1u],
                &s->overlay_removed_base_indices[slot],
                (size_t)(s->overlay_removed_base_len - slot) *
                    sizeof(CettaIndex));
    }
    s->overlay_removed_base_indices[slot] = idx;
    s->overlay_removed_base_len++;
    if (idx + 1u == s->overlay_base_visible_len)
        space_overlay_trim_base_tail(s);
    space_mark_indexes_dirty(s);
    return true;
}

static CettaCount space_overlay_visible_base_count(const Space *s) {
    if (!space_has_overlay_base(s))
        return 0;
    return s->overlay_base_visible_len - s->overlay_removed_base_len;
}

static bool space_overlay_visible_base_raw_index(const Space *s,
                                                 CettaCount visible_index,
                                                 CettaIndex *out_raw) {
    if (!space_has_overlay_base(s) || !out_raw)
        return false;
    CettaCount visible_count = space_overlay_visible_base_count(s);
    if (visible_index >= visible_count)
        return false;
    CettaCount target = visible_index + 1u;
    CettaIndex lo = 0;
    CettaIndex hi = s->overlay_base_visible_len;
    while (lo < hi) {
        CettaIndex mid = lo + (hi - lo) / 2u;
        CettaIndex removed_through =
            space_overlay_removed_upper_bound(s, mid);
        CettaCount visible_through =
            (CettaCount)(mid + 1u - removed_through);
        if (visible_through >= target)
            hi = mid;
        else
            lo = mid + 1u;
    }
    if (lo >= s->overlay_base_visible_len ||
        space_overlay_base_index_removed(s, lo)) {
        return false;
    }
    *out_raw = lo;
    return true;
}

static bool space_overlay_remove_local_raw_index(Space *s, CettaIndex raw_idx) {
    if (!s)
        return false;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    if (raw_idx >= s->native.len)
        return false;
    if (space_is_ordered(s)) {
        size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        memmove(s->native.atom_ids + ((size_t)raw_idx * width),
                s->native.atom_ids + ((size_t)(raw_idx + 1u) * width),
                (size_t)(s->native.len - raw_idx - 1u) * width);
        s->native.len--;
    } else {
        AtomId tail_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits, s->native.len - 1u);
        s->native.len--;
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits, raw_idx, tail_id);
    }
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

static void space_reserve_linear(Space *s, CettaIndex min_cap) {
    size_t width = 0;
    if (s->native.cap >= min_cap)
        return;
    if (!space_sync_atom_id_storage_width(s))
        return;
    CettaIndex new_cap = s->native.cap ? s->native.cap : 64;
    while (new_cap < min_cap)
        new_cap *= 2;
    width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
    s->native.atom_ids = cetta_realloc(
        s->native.atom_ids, width * (size_t)new_cap);
    s->native.cap = new_cap;
    space_note_atom_id_storage_peak(s);
}

void space_linearize(Space *s) {
    size_t width = 0;
    if (!space_is_queue(s) || s->native.start == 0)
        return;
    if (s->native.len > 0) {
        width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        memmove(s->native.atom_ids,
                s->native.atom_ids + ((size_t)s->native.start * width),
                width * (size_t)s->native.len);
    }
    s->native.start = 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUEUE_COMPACT);
}

static void space_queue_reserve_tail(Space *s, CettaIndex extra) {
    CettaIndex tail = s->native.start + s->native.len;
    if (tail + extra <= s->native.cap)
        return;
    if (s->native.start > 0) {
        space_linearize(s);
        tail = s->native.len;
        if (tail + extra <= s->native.cap)
            return;
    }
    space_reserve_linear(s, s->native.len + extra);
}

const char *space_kind_name(SpaceKind kind) {
    switch (kind) {
    case SPACE_KIND_ATOM:
        return "atom";
    case SPACE_KIND_STACK:
        return "stack";
    case SPACE_KIND_QUEUE:
        return "queue";
    case SPACE_KIND_HASH:
        return "hash";
    }
    return "unknown";
}

bool space_kind_from_name(const char *name, SpaceKind *out) {
    if (!name || !out) return false;
    if (strcmp(name, "atom") == 0) {
        *out = SPACE_KIND_ATOM;
        return true;
    }
    if (strcmp(name, "stack") == 0) {
        *out = SPACE_KIND_STACK;
        return true;
    }
    if (strcmp(name, "queue") == 0) {
        *out = SPACE_KIND_QUEUE;
        return true;
    }
    if (strcmp(name, "hash") == 0) {
        *out = SPACE_KIND_HASH;
        return true;
    }
    return false;
}

bool space_is_ordered(const Space *s) {
    return s && (s->kind == SPACE_KIND_STACK || s->kind == SPACE_KIND_QUEUE);
}

bool space_is_stack(const Space *s) {
    return s && s->kind == SPACE_KIND_STACK;
}

bool space_is_queue(const Space *s) {
    return s && s->kind == SPACE_KIND_QUEUE;
}

bool space_is_hash(const Space *s) {
    return s && s->kind == SPACE_KIND_HASH;
}

static void space_mark_secondary_indexes_dirty(Space *s) {
    if (!s)
        return;
    s->native.eq_idx_dirty = true;
    s->native.ty_idx_dirty = true;
    s->native.exact_idx_dirty = true;
    s->native.has_non_exact_atoms_dirty = true;
}

void space_mark_derived_state_dirty(Space *s) {
    if (!s)
        return;
    space_mark_secondary_indexes_dirty(s);
    /* The presence bitset is add-monotone: incremental add paths keep it current
       and do NOT route through here, so any caller of this helper (removal, bulk
       replace, backend-direct store, external mutation) may have
       invalidated a bit -- flag a recompute-on-next-membership-query. */
    s->native.id_present_dirty = true;
}

static void space_mark_indexes_dirty(Space *s) {
    space_mark_derived_state_dirty(s);
}

void space_discard_native_logical_view(Space *s) {
    if (!s)
        return;
    free(s->native.atom_ids);
    s->native.atom_ids = NULL;
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.has_non_exact_atoms = false;
    space_mark_derived_state_dirty(s);
}

static bool space_store_via_backend_primary(Space *s, AtomId atom_id, Atom *atom) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_id_is_exact_indexable(s, atom_id);
    if (!space_match_backend_store_atom_id_direct(s, atom_id, atom))
        return false;
    space_mark_indexes_dirty(s);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms =
            had_non_exact_atom || !atom_exact_indexable;
        s->native.has_non_exact_atoms_dirty = false;
    }
    space_bump_revision(s);
    return true;
}

static bool space_store_atom_via_backend_primary(Space *s, Atom *atom) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (!s || !atom)
        return false;
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_is_exact_indexable(atom);
    if (!space_match_backend_store_atom_direct(s, atom))
        return false;
    space_mark_indexes_dirty(s);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms =
            had_non_exact_atom || !atom_exact_indexable;
        s->native.has_non_exact_atoms_dirty = false;
    }
    space_bump_revision(s);
    return true;
}

static bool space_remove_via_backend_primary(Space *s, AtomId atom_id) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_id_is_exact_indexable(s, atom_id);
    if (!space_match_backend_remove_atom_id_direct(s, atom_id))
        return false;
    space_mark_indexes_dirty(s);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms = had_non_exact_atom;
        s->native.has_non_exact_atoms_dirty =
            had_non_exact_atom && !atom_exact_indexable;
    }
    space_bump_revision(s);
    return true;
}

static bool space_remove_atom_via_backend_primary(Space *s, Atom *atom) {
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool atom_exact_indexable;
    if (!s || !atom)
        return false;
    keep_pathmap_exact_metadata =
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    atom_exact_indexable = atom_is_exact_indexable(atom);
    if (!space_match_backend_remove_atom_direct(s, atom))
        return false;
    space_mark_indexes_dirty(s);
    if (keep_pathmap_exact_metadata) {
        s->native.has_non_exact_atoms = had_non_exact_atom;
        s->native.has_non_exact_atoms_dirty =
            had_non_exact_atom && !atom_exact_indexable;
    }
    space_bump_revision(s);
    return true;
}

static bool space_truncate_via_backend_primary64(Space *s, CettaCount new_len) {
    if (!s)
        return false;
    if (!space_match_backend_truncate_direct64(s, new_len))
        return false;
    space_mark_indexes_dirty(s);
    space_bump_revision(s);
    return true;
}

static bool space_truncate_via_backend_primary(Space *s, uint32_t new_len) {
    return space_truncate_via_backend_primary64(s, new_len);
}

static void ensure_eq_index(Space *s) {
    if (s && s->native.eq_idx_dirty) {
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return;
        eq_index_rebuild(s);
    }
}

static void ensure_ty_ann_index(Space *s) {
    if (s && s->native.ty_idx_dirty) {
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return;
        ty_ann_index_rebuild(s);
    }
}

static void ensure_exact_index(Space *s) {
    if (s && s->native.exact_idx_dirty)
        exact_index_rebuild(s);
}

static bool ensure_has_non_exact_atoms(Space *s) {
    if (!s)
        return false;
    if (s->native.has_non_exact_atoms_dirty) {
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return false;
        recompute_has_non_exact_atoms(s);
    }
    return !s->native.has_non_exact_atoms_dirty;
}

void space_begin_secondary_index_deferral(Space *s) {
    if (!s)
        return;
    s->native.secondary_index_deferral_depth++;
    /* Deferral postpones only the query-oriented secondary indexes.  It does
       not mutate membership, so invalidating the alpha-presence projection
       here would turn every deferral window into an unnecessary full rescan. */
    space_mark_secondary_indexes_dirty(s);
}

void space_end_secondary_index_deferral(Space *s) {
    if (!s || s->native.secondary_index_deferral_depth == 0)
        return;
    s->native.secondary_index_deferral_depth--;
}

static bool space_sync_exact_membership_from_backend(Space *s) {
    if (!s || !space_engine_uses_pathmap(s->match_backend.kind))
        return true;
    if (!s->native.exact_idx_dirty)
        return true;
    Arena scratch;
    arena_init(&scratch);
    bool ok = space_match_backend_materialize_native_storage(s, &scratch);
    arena_free(&scratch);
    return ok;
}

static void space_native_storage_init_empty(Space *s, TermUniverse *universe) {
    TermUniverse *resolved = NULL;
    if (!s)
        return;
    resolved = universe ? universe : space_default_universe();
    s->native.atom_ids = NULL;
    s->native.atom_id_width_bits =
        (uint8_t)space_atom_id_width_bits_for_universe(resolved);
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.universe = resolved;
    eq_index_init(&s->native.eq_idx);
    ty_ann_index_init(&s->native.ty_idx);
    exact_index_init(&s->native.exact_idx);
    s->native.id_present = NULL;
    s->native.id_present_bits = 0;
    s->native.id_present_synced_len = 0;
    s->native.id_present_dirty = false;
    s->native.eq_idx_dirty = false;
    s->native.ty_idx_dirty = false;
    s->native.exact_idx_dirty = false;
    s->native.has_non_exact_atoms = false;
    s->native.has_non_exact_atoms_dirty = false;
    s->native.secondary_index_deferral_depth = 0;
    s->overlay_base = NULL;
    s->overlay_base_visible_len = 0;
    s->overlay_removed_base_indices = NULL;
    s->overlay_removed_base_len = 0;
    s->overlay_removed_base_cap = 0;
}

static void space_move_storage_and_backend(Space *dst, Space *src) {
    if (!dst || !src)
        return;
    dst->native = src->native;
    dst->kind = src->kind;
    dst->match_backend = src->match_backend;
    dst->overlay_base = src->overlay_base;
    dst->overlay_base_visible_len = src->overlay_base_visible_len;
    dst->overlay_removed_base_indices = src->overlay_removed_base_indices;
    dst->overlay_removed_base_len = src->overlay_removed_base_len;
    dst->overlay_removed_base_cap = src->overlay_removed_base_cap;
    dst->payload_owner_epoch = src->payload_owner_epoch;
    dst->payload_export_owner_epoch = src->payload_export_owner_epoch;
}

static void space_reset_moved_from(Space *s) {
    if (!s)
        return;
    s->kind = SPACE_KIND_ATOM;
    /* Moving the payload ends the old source lifetime.  Re-establish every
       invariant of an empty live Space so callers may safely reuse or free it:
       a fresh identity prevents stale read tokens from reviving, and the
       universe observer keeps AtomId storage width synchronized. */
    s->instance_id = space_fresh_instance_id();
    s->revision = 0;
    space_native_storage_init_empty(s, space_default_universe());
    space_match_backend_init(s);
    space_attach_to_universe(s, s->native.universe);
    s->payload_owner_epoch = 0;
    s->payload_export_owner_epoch = 0;
}

/* ── Space ──────────────────────────────────────────────────────────────── */

void space_init_with_universe(Space *s, TermUniverse *universe) {
    if (!s)
        return;
    s->kind = SPACE_KIND_ATOM;
    space_native_storage_init_empty(s, universe);
    s->instance_id = space_fresh_instance_id();
    s->revision = 0;
    space_match_backend_init(s);
    space_attach_to_universe(s, s->native.universe);
    s->payload_owner_epoch = 0;
    s->payload_export_owner_epoch = 0;
}

void space_init_overlay(Space *s, const Space *base) {
    space_init_with_universe(s, base ? base->native.universe : NULL);
    if (!base)
        return;
    s->kind = base->kind;
    s->overlay_base = base;
    s->overlay_base_visible_len = space_length64(base);
}

void space_init(Space *s) {
    space_init_with_universe(s, NULL);
}

void space_free(Space *s) {
    if (!s)
        return;
    space_detach_from_universe(s);
    free(s->native.atom_ids);
    s->native.atom_ids = NULL;
    s->native.atom_id_width_bits = 0;
    s->native.start = 0;
    s->native.len = 0;
    s->native.cap = 0;
    s->native.universe = space_default_universe();
    s->instance_id = 0;
    s->revision = 0;
    eq_index_free(&s->native.eq_idx);
    ty_ann_index_free(&s->native.ty_idx);
    exact_index_free(&s->native.exact_idx);
    id_present_free(&s->native);
    space_match_backend_free(s);
    s->native.has_non_exact_atoms = false;
    s->native.has_non_exact_atoms_dirty = false;
    s->native.secondary_index_deferral_depth = 0;
    free(s->overlay_removed_base_indices);
    s->overlay_base = NULL;
    s->overlay_base_visible_len = 0;
    s->overlay_removed_base_indices = NULL;
    s->overlay_removed_base_len = 0;
    s->overlay_removed_base_cap = 0;
    s->payload_owner_epoch = 0;
    s->payload_export_owner_epoch = 0;
}

Atom *space_store_atom(Space *s, Arena *fallback, Atom *atom) {
    return term_universe_store_atom(s ? s->native.universe : NULL, fallback, atom);
}

static bool is_equation_atom(Atom *a, Atom **lhs_out, Atom **rhs_out) {
    if (a->kind != ATOM_EXPR || a->expr.len != 3) return false;
    if (!atom_is_symbol_id(a->expr.elems[0], g_builtin_syms.equals)) return false;
    *lhs_out = a->expr.elems[1];
    *rhs_out = a->expr.elems[2];
    return true;
}

SpaceReadToken space_read_token(const Space *s) {
    return (SpaceReadToken){
        .space = s,
        .instance_id = space_instance_id(s),
        .revision = space_revision(s),
    };
}

bool space_read_token_is_current(SpaceReadToken token) {
    return token.space && token.instance_id != 0u &&
           token.instance_id == space_instance_id(token.space) &&
           token.revision == space_revision(token.space);
}

bool space_equation_occurrence_resolve(SpaceEquationOccurrenceId id,
                                       SpaceEquationOccurrence *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!out || !space_read_token_is_current(id.read) ||
        id.logical_index >= space_length64(id.read.space)) {
        return false;
    }
    Atom *equation = space_get_at64(id.read.space, id.logical_index);
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs) ||
        !space_read_token_is_current(id.read)) {
        return false;
    }
    out->id = id;
    out->equation = equation;
    out->lhs = lhs;
    out->rhs = rhs;
    return true;
}

static bool space_equation_cursor_index_matches(
    const SpaceEquationCursor *cursor, CettaIndex logical_index,
    bool wildcard) {
    if (!cursor || !cursor->read.space ||
        logical_index >= space_length64(cursor->read.space)) {
        return false;
    }
    Atom *equation = space_get_at64(cursor->read.space, logical_index);
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs))
        return false;
    SymbolId lhs_head = eq_head_symbol(lhs);
    return wildcard
        ? lhs_head == SYMBOL_ID_NONE
        : lhs_head == cursor->head;
}

static bool space_equation_cursor_peek_bucket(
    SpaceEquationCursor *cursor, const EqBucket *bucket,
    CettaIndex *position, bool wildcard, CettaIndex *logical_index) {
    if (!cursor || !bucket || !position || !logical_index)
        return false;
    while (*position < bucket->len) {
        CettaIndex candidate = bucket->atom_indices[*position];
        if (space_equation_cursor_index_matches(
                cursor, candidate, wildcard)) {
            *logical_index = candidate;
            return true;
        }
        (*position)++;
    }
    return false;
}

bool space_equation_cursor_init(Space *s, SymbolId head,
                                SpaceEquationCursor *cursor) {
    if (cursor)
        memset(cursor, 0, sizeof(*cursor));
    if (!s || !cursor || head == SYMBOL_ID_NONE)
        return false;
    if (!space_has_overlay_base(s))
        ensure_eq_index(s);
    cursor->read = space_read_token(s);
    cursor->head = head;
    cursor->overlay = space_has_overlay_base(s);
    return space_read_token_is_current(cursor->read);
}

SpaceEquationCursorStep space_equation_cursor_next(
    SpaceEquationCursor *cursor, SpaceEquationOccurrenceId *out) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (!cursor || !out || !space_read_token_is_current(cursor->read))
        return SPACE_EQUATION_CURSOR_INVALIDATED;

    Space *s = (Space *)cursor->read.space;
    if (cursor->overlay) {
        CettaCount logical_len = space_length64(s);
        while (cursor->overlay_position < logical_len) {
            CettaIndex logical_index = cursor->overlay_position++;
            Atom *equation = space_get_at64(s, logical_index);
            Atom *lhs = NULL;
            Atom *rhs = NULL;
            if (!equation || !is_equation_atom(equation, &lhs, &rhs))
                continue;
            SymbolId lhs_head = eq_head_symbol(lhs);
            if (lhs_head != SYMBOL_ID_NONE && lhs_head != cursor->head)
                continue;
            out->read = cursor->read;
            out->logical_index = logical_index;
            return SPACE_EQUATION_CURSOR_ITEM;
        }
        return SPACE_EQUATION_CURSOR_END;
    }

    EqBucket *exact =
        &s->native.eq_idx.buckets[symbol_hash(cursor->head)];
    EqBucket *wildcard = &s->native.eq_idx.wildcard;
    CettaIndex exact_index = 0u;
    CettaIndex wildcard_index = 0u;
    bool has_exact = space_equation_cursor_peek_bucket(
        cursor, exact, &cursor->exact_position, false, &exact_index);
    bool has_wildcard = space_equation_cursor_peek_bucket(
        cursor, wildcard, &cursor->wildcard_position, true,
        &wildcard_index);
    if (!has_exact && !has_wildcard)
        return SPACE_EQUATION_CURSOR_END;

    CettaIndex logical_index;
    if (has_exact && (!has_wildcard || exact_index < wildcard_index)) {
        logical_index = exact_index;
        cursor->exact_position++;
    } else if (has_wildcard &&
               (!has_exact || wildcard_index < exact_index)) {
        logical_index = wildcard_index;
        cursor->wildcard_position++;
    } else {
        logical_index = exact_index;
        cursor->exact_position++;
        cursor->wildcard_position++;
    }
    out->read = cursor->read;
    out->logical_index = logical_index;
    return SPACE_EQUATION_CURSOR_ITEM;
}

/* ── Revision-pinned execution-contract analysis ──────────────────────── */

#define SPACE_EFFECT_CACHE_SLOTS 64u

typedef struct {
    SpaceReadToken read;
    SymbolId head;
    CettaGsltQueryEffect effect;
    bool defined;
    bool valid;
} SpaceEffectCacheEntry;

static _Thread_local SpaceEffectCacheEntry
    g_space_effect_cache[SPACE_EFFECT_CACHE_SLOTS];

typedef struct {
    SymbolId head;
    SymbolId *dependencies;
    size_t dependency_len;
    size_t dependency_cap;
    CettaGsltQueryEffect base_effect;
    CettaGsltQueryEffect effect;
    bool defined;
    bool scanned;
} SpaceEffectNode;

typedef struct {
    Space *space;
    SpaceReadToken read;
    SpaceEffectNode *nodes;
    size_t len;
    size_t cap;
    bool failed;
} SpaceEffectGraph;

static CettaGsltQueryEffect space_generated_query_head_effect(
    SymbolId head) {
#define SPACE_QUERY_EFFECT_HEAD(field, effect) \
    if (head == g_builtin_syms.field) return (effect);
    CETTA_GSLT_QUERY_EFFECT_HEAD_ROWS(SPACE_QUERY_EFFECT_HEAD)
#undef SPACE_QUERY_EFFECT_HEAD
    return CETTA_GSLT_QUERY_EFFECT_PURE;
}

static size_t space_effect_cache_slot(const Space *space, SymbolId head) {
    uint64_t key = space_instance_id(space) ^
        ((uint64_t)head * UINT64_C(11400714819323198485));
    return (size_t)(key & (SPACE_EFFECT_CACHE_SLOTS - 1u));
}

void space_execution_analysis_note_mutation(Space *space) {
    if (!space)
        return;
    uint64_t instance = space_instance_id(space);
    for (size_t i = 0u; i < SPACE_EFFECT_CACHE_SLOTS; i++) {
        SpaceEffectCacheEntry *entry = &g_space_effect_cache[i];
        if (entry->valid && entry->read.space == space &&
            entry->read.instance_id == instance) {
            memset(entry, 0, sizeof(*entry));
        }
    }
}

static bool space_effect_graph_reserve(SpaceEffectGraph *graph,
                                       size_t need) {
    if (need <= graph->cap)
        return true;
    size_t next = graph->cap ? graph->cap * 2u : 8u;
    while (next < need) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(*graph->nodes))
        return false;
    SpaceEffectNode *nodes = realloc(
        graph->nodes, sizeof(*nodes) * next);
    if (!nodes)
        return false;
    memset(nodes + graph->cap, 0,
           sizeof(*nodes) * (next - graph->cap));
    graph->nodes = nodes;
    graph->cap = next;
    return true;
}

static bool space_effect_graph_find_or_add(
    SpaceEffectGraph *graph, SymbolId head, size_t *out_index) {
    for (size_t i = 0u; i < graph->len; i++) {
        if (graph->nodes[i].head == head) {
            *out_index = i;
            return true;
        }
    }
    if (!space_effect_graph_reserve(graph, graph->len + 1u))
        return false;
    size_t index = graph->len++;
    graph->nodes[index].head = head;
    graph->nodes[index].base_effect = CETTA_GSLT_QUERY_EFFECT_PURE;
    graph->nodes[index].effect = CETTA_GSLT_QUERY_EFFECT_PURE;
    *out_index = index;
    return true;
}

static bool space_effect_node_add_dependency(
    SpaceEffectGraph *graph, size_t node_index, SymbolId dependency) {
    size_t dependency_index = 0u;
    if (!space_effect_graph_find_or_add(
            graph, dependency, &dependency_index)) {
        return false;
    }
    SpaceEffectNode *node = &graph->nodes[node_index];
    for (size_t i = 0u; i < node->dependency_len; i++)
        if (node->dependencies[i] == dependency)
            return true;
    if (node->dependency_len == node->dependency_cap) {
        size_t next = node->dependency_cap
            ? node->dependency_cap * 2u : 4u;
        if (next > SIZE_MAX / sizeof(*node->dependencies))
            return false;
        SymbolId *dependencies = realloc(
            node->dependencies, sizeof(*dependencies) * next);
        if (!dependencies)
            return false;
        node->dependencies = dependencies;
        node->dependency_cap = next;
    }
    node->dependencies[node->dependency_len++] =
        graph->nodes[dependency_index].head;
    return true;
}

static bool space_effect_scan_rhs(
    SpaceEffectGraph *graph, size_t node_index, Atom *root) {
    Atom *inline_stack[32];
    Atom **stack = inline_stack;
    size_t len = 0u;
    size_t cap = sizeof(inline_stack) / sizeof(inline_stack[0]);

    if (!root)
        return false;
    stack[len++] = root;
    while (len > 0u) {
        Atom *current = stack[--len];
        if (!current || current->kind != ATOM_EXPR ||
            current->expr.len == 0u) {
            continue;
        }
        Atom *head = current->expr.elems[0];
        if (!head || head->kind != ATOM_SYMBOL) {
            graph->nodes[node_index].base_effect =
                CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD;
        } else {
            CettaGsltQueryEffect direct =
                space_generated_query_head_effect(head->sym_id);
            graph->nodes[node_index].base_effect =
                cetta_gslt_query_effect_join(
                    graph->nodes[node_index].base_effect, direct);
            if (direct == CETTA_GSLT_QUERY_EFFECT_PURE &&
                !is_grounded_op(head->sym_id) &&
                space_equations_may_match_known_head(
                    graph->space, head->sym_id) &&
                !space_effect_node_add_dependency(
                    graph, node_index, head->sym_id)) {
                if (stack != inline_stack)
                    free(stack);
                return false;
            }
        }
        for (CettaExprIndex i = 1u; i < current->expr.len; i++) {
            Atom *child = current->expr.elems[i];
            if (!child || child->kind != ATOM_EXPR)
                continue;
            if (len == cap) {
                if (cap > SIZE_MAX / 2u ||
                    cap * 2u > SIZE_MAX / sizeof(*stack)) {
                    if (stack != inline_stack)
                        free(stack);
                    return false;
                }
                size_t next_cap = cap * 2u;
                Atom **next = stack == inline_stack
                    ? malloc(sizeof(*next) * next_cap)
                    : realloc(stack, sizeof(*next) * next_cap);
                if (!next) {
                    if (stack != inline_stack)
                        free(stack);
                    return false;
                }
                if (stack == inline_stack)
                    memcpy(next, inline_stack, sizeof(*next) * len);
                stack = next;
                cap = next_cap;
            }
            stack[len++] = child;
        }
    }
    if (stack != inline_stack)
        free(stack);
    return true;
}

static bool space_effect_scan_node(
    SpaceEffectGraph *graph, size_t node_index) {
    SymbolId head = graph->nodes[node_index].head;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(graph->space, head, &cursor))
        return false;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step =
            space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step != SPACE_EQUATION_CURSOR_ITEM)
            return false;
        SpaceEquationOccurrence occurrence;
        if (!space_equation_occurrence_resolve(id, &occurrence))
            return false;
        graph->nodes[node_index].defined = true;
        if (!space_effect_scan_rhs(
                graph, node_index, occurrence.rhs)) {
            return false;
        }
    }
    graph->nodes[node_index].scanned = true;
    return space_read_token_is_current(graph->read);
}

static void space_effect_graph_free(SpaceEffectGraph *graph) {
    if (!graph)
        return;
    for (size_t i = 0u; i < graph->len; i++)
        free(graph->nodes[i].dependencies);
    free(graph->nodes);
    memset(graph, 0, sizeof(*graph));
}

static bool space_effect_graph_solve(SpaceEffectGraph *graph) {
    for (size_t scan = 0u; scan < graph->len; scan++) {
        if (!space_effect_scan_node(graph, scan))
            return false;
    }
    bool changed;
    do {
        changed = false;
        for (size_t i = 0u; i < graph->len; i++) {
            CettaGsltQueryEffect effect = graph->nodes[i].base_effect;
            for (size_t di = 0u;
                 di < graph->nodes[i].dependency_len; di++) {
                SymbolId dependency = graph->nodes[i].dependencies[di];
                for (size_t ni = 0u; ni < graph->len; ni++) {
                    if (graph->nodes[ni].head == dependency) {
                        effect = cetta_gslt_query_effect_join(
                            effect, graph->nodes[ni].effect);
                        break;
                    }
                }
            }
            if (effect != graph->nodes[i].effect) {
                graph->nodes[i].effect = effect;
                changed = true;
            }
        }
    } while (changed);
    return space_read_token_is_current(graph->read);
}

CettaGsltQueryEffect space_query_effect_for_head(
    Space *space, SymbolId head, bool *out_defined) {
    if (out_defined)
        *out_defined = false;
    if (!space || head == SYMBOL_ID_NONE)
        return CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD;

    CettaGsltQueryEffect direct =
        space_generated_query_head_effect(head);
    if (direct != CETTA_GSLT_QUERY_EFFECT_PURE) {
        if (out_defined)
            *out_defined = true;
        return direct;
    }
    if (is_grounded_op(head))
        return CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL;

    size_t slot_index = space_effect_cache_slot(space, head);
    SpaceEffectCacheEntry *slot =
        &g_space_effect_cache[slot_index];
    if (slot->valid && slot->head == head &&
        space_read_token_is_current(slot->read)) {
        if (out_defined)
            *out_defined = slot->defined;
        return slot->effect;
    }
    if (!space_equations_may_match_known_head(space, head)) {
        slot->read = space_read_token(space);
        slot->head = head;
        slot->effect = CETTA_GSLT_QUERY_EFFECT_INERT_SYMBOL;
        slot->defined = false;
        slot->valid = true;
        return slot->effect;
    }

    SpaceEffectGraph graph = {
        .space = space,
        .read = space_read_token(space),
    };
    size_t root_index = 0u;
    if (!space_effect_graph_find_or_add(&graph, head, &root_index) ||
        !space_effect_graph_solve(&graph)) {
        space_effect_graph_free(&graph);
        return CETTA_GSLT_QUERY_EFFECT_UNCERTAIN_HEAD;
    }

    CettaGsltQueryEffect result = graph.nodes[root_index].effect;
    bool defined = graph.nodes[root_index].defined;
    for (size_t i = 0u; i < graph.len; i++) {
        size_t cache_index = space_effect_cache_slot(
            space, graph.nodes[i].head);
        SpaceEffectCacheEntry *entry =
            &g_space_effect_cache[cache_index];
        entry->read = graph.read;
        entry->head = graph.nodes[i].head;
        entry->effect = graph.nodes[i].effect;
        entry->defined = graph.nodes[i].defined;
        entry->valid = true;
    }
    if (out_defined)
        *out_defined = defined;
    space_effect_graph_free(&graph);
    return result;
}

static bool space_equation_child_ids_at_id(const Space *s, AtomId atom_id,
                                           AtomId *lhs_id_out, AtomId *rhs_id_out) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!tu_hdr(s->native.universe, atom_id) ||
        tu_kind(s->native.universe, atom_id) != ATOM_EXPR ||
        tu_arity(s->native.universe, atom_id) != 3 ||
        tu_head_sym(s->native.universe, atom_id) != g_builtin_syms.equals)
        return false;
    AtomId lhs_id = tu_child(s->native.universe, atom_id, 1);
    AtomId rhs_id = tu_child(s->native.universe, atom_id, 2);
    if (lhs_id == CETTA_ATOM_ID_NONE || rhs_id == CETTA_ATOM_ID_NONE)
        return false;
    *lhs_id_out = lhs_id;
    *rhs_id_out = rhs_id;
    return true;
}

static bool space_equation_children_at_id(const Space *s, AtomId atom_id,
                                          Atom **lhs_out, Atom **rhs_out) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    AtomId lhs_id = CETTA_ATOM_ID_NONE;
    AtomId rhs_id = CETTA_ATOM_ID_NONE;
    if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
        Atom *lhs = term_universe_get_atom(s->native.universe, lhs_id);
        Atom *rhs = term_universe_get_atom(s->native.universe, rhs_id);
        if (lhs && rhs) {
            *lhs_out = lhs;
            *rhs_out = rhs;
            return true;
        }
    }
    if (tu_hdr(s->native.universe, atom_id))
        return false;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    return atom ? is_equation_atom(atom, lhs_out, rhs_out) : false;
}

static bool space_type_annotation_child_ids_at_id(const Space *s,
                                                  AtomId atom_id,
                                                  AtomId *subject_id_out,
                                                  AtomId *type_id_out) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!tu_hdr(s->native.universe, atom_id) ||
        tu_kind(s->native.universe, atom_id) != ATOM_EXPR ||
        tu_arity(s->native.universe, atom_id) != 3 ||
        tu_head_sym(s->native.universe, atom_id) != g_builtin_syms.colon)
        return false;
    AtomId subject_id = tu_child(s->native.universe, atom_id, 1);
    AtomId type_id = tu_child(s->native.universe, atom_id, 2);
    if (subject_id == CETTA_ATOM_ID_NONE || type_id == CETTA_ATOM_ID_NONE)
        return false;
    *subject_id_out = subject_id;
    *type_id_out = type_id;
    return true;
}

static Atom *space_type_annotation_subject_at_id(const Space *s,
                                                 AtomId atom_id) {
    if (!s || !s->native.universe || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    AtomId subject_id = CETTA_ATOM_ID_NONE;
    AtomId type_id = CETTA_ATOM_ID_NONE;
    if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
        if (subject_id != CETTA_ATOM_ID_NONE)
            return term_universe_get_atom(s->native.universe, subject_id);
    }
    if (tu_hdr(s->native.universe, atom_id))
        return NULL;
    Atom *atom = term_universe_get_atom(s->native.universe, atom_id);
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 3 &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.colon))
        return atom->expr.elems[1];
    return NULL;
}

static void eq_index_rebuild(Space *s) {
    space_linearize(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD);
    eq_index_free(&s->native.eq_idx);
    eq_index_init(&s->native.eq_idx);
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
            eq_index_add_id(&s->native.eq_idx, s, lhs_id, i);
            continue;
        }
        Atom *lhs, *rhs;
        if (space_equation_children_at_id(s, atom_id, &lhs, &rhs))
            eq_index_add(&s->native.eq_idx, lhs, i);
    }
    s->native.eq_idx_dirty = false;
}

static void ty_ann_index_rebuild(Space *s) {
    space_linearize(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TY_INDEX_REBUILD);
    ty_ann_index_free(&s->native.ty_idx);
    ty_ann_index_init(&s->native.ty_idx);
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
            ty_ann_index_add_id(&s->native.ty_idx, s, subject_id, i);
            continue;
        }
        Atom *subject = space_type_annotation_subject_at_id(s, atom_id);
        if (subject)
            ty_ann_index_add(&s->native.ty_idx, subject, i);
    }
    s->native.ty_idx_dirty = false;
}

/* Lazily bring the presence bitset up to date with membership.  id_present is a
   projection of the stored atom ids keyed on ALPHA-canonical ids of EVERY atom
   (ground and non-ground), giving add-atom-nodup dedup an O(1) alpha-aware id
   predicate.  Maintenance is DEFERRED to query time (this function) rather than
   done per-add, for two reasons: (1) pure file ingress never forces a lazy blob
   decode -- the canonicalizing read only happens when membership is actually
   queried; (2) a forward-chaining deferral window (which defers the exact-index
   buckets) never triggers a full O(N) rebuild per contains.  A removal / bulk
   mutation sets id_present_dirty -> clear + full resync; otherwise the bitset is
   append-only and we sync just the [synced_len, len) suffix (O(1) amortized in a
   chaining loop, so native add-atom-nodup stays O(N), not O(N^2)).  Uses the
   logical atom-id accessor, so no linearization is required.  Failure leaves
   the projection dirty and forces callers onto the exact/alpha scan; an
   incomplete accelerator must never create a false negative. */
static bool id_present_sync(Space *s) {
    if (!s)
        return false;
    CettaCount logical_len = space_length64(s);
    if (s->native.id_present_dirty ||
        s->native.id_present_synced_len > logical_len) {
        id_present_clear(&s->native);
        s->native.id_present_synced_len = 0;
    }
    for (CettaIndex i = s->native.id_present_synced_len;
         i < logical_len; i++) {
        AtomId stored_id = space_get_atom_id_at64(s, i);
        Atom *stored =
            term_universe_get_atom(s->native.universe, stored_id);
        /* Pointer-backed runtime values do not have lawful structural intern
           keys.  Leave them to the exact/alpha scan; stable queries can still
           use the projection built from the remaining rows. */
        if (stored && !term_universe_atom_is_stable(stored))
            continue;
        AtomId cid = space_canonical_id_for_stored(s, stored_id);
        if (cid == CETTA_ATOM_ID_NONE ||
            !id_present_set(&s->native, cid)) {
            id_present_clear(&s->native);
            s->native.id_present_synced_len = 0;
            s->native.id_present_dirty = true;
            return false;
        }
    }
    s->native.id_present_synced_len = logical_len;
    s->native.id_present_dirty = false;
    return true;
}

static void exact_index_rebuild(Space *s) {
    space_linearize(s);
    exact_index_free(&s->native.exact_idx);
    exact_index_init(&s->native.exact_idx);
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        if (!atom_id_is_exact_indexable(s, atom_id))
            continue;
        exact_atom_bucket_add(
            &s->native.exact_idx.buckets[exact_atom_hash_id(s, atom_id)], i);
    }
    /* id_present is now decoupled from the bucket index -- it is synced lazily at
       membership-query time (id_present_sync), so the bucket rebuild does not
       touch it.  space_linearize preserves logical order, so id_present_synced_len
       stays valid across a bucket rebuild. */
    s->native.exact_idx_dirty = false;
}

static void recompute_has_non_exact_atoms(Space *s) {
    if (!s) return;
    s->native.has_non_exact_atoms = false;
    for (CettaIndex i = 0; i < s->native.len; i++) {
        AtomId atom_id = space_get_atom_id_at64(s, i);
        if (!atom_id_is_exact_indexable(s, atom_id)) {
            s->native.has_non_exact_atoms = true;
            break;
        }
    }
    s->native.has_non_exact_atoms_dirty = false;
}

uint64_t space_global_mutation_epoch(void) {
    return atomic_load_explicit(&g_space_global_mutation_epoch,
                                memory_order_relaxed);
}

static void space_bump_revision(Space *s) {
    if (!s)
        return;
    space_execution_analysis_note_mutation(s);
    if (s->revision == UINT64_MAX) {
        fputs("CeTTa: exhausted Space revision counter\n", stderr);
        abort();
    }
    s->revision++;
    uint64_t prior = atomic_fetch_add_explicit(
        &g_space_global_mutation_epoch, 1u, memory_order_relaxed);
    if (prior == UINT64_MAX) {
        fputs("CeTTa: exhausted global Space mutation epoch\n", stderr);
        abort();
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_REVISION_BUMP);
}

void space_note_external_backend_mutation(Space *s) {
    if (!s)
        return;
    space_mark_derived_state_dirty(s);
    space_bump_revision(s);
}

static bool space_defer_incremental_secondary_indices(const Space *s) {
    return s &&
           (s->match_backend.kind == SPACE_ENGINE_PATHMAP ||
            s->native.secondary_index_deferral_depth > 0);
}

static void space_add_stored_id(Space *s, AtomId atom_id, Atom *backend_atom) {
    bool queue_gap = space_is_queue(s) && s->native.start != 0;
    bool backend_needs_atom =
        !queue_gap && space_match_backend_needs_atom_on_add(s, atom_id);
    bool defer_secondary =
        space_defer_incremental_secondary_indices(s);
    if (backend_needs_atom && space_tracks_atom_ids(s) &&
        atom_id != CETTA_ATOM_ID_NONE) {
        Atom *stored_backend_atom =
            term_universe_get_atom(s->native.universe, atom_id);
        if (stored_backend_atom) {
            backend_atom = stored_backend_atom;
        } else if (!backend_atom) {
            return;
        }
    }
    CettaIndex idx = s->native.len;
    if (space_is_queue(s)) {
        space_queue_reserve_tail(s, 1);
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits,
            s->native.start + s->native.len, atom_id);
    } else {
        space_reserve_linear(s, s->native.len + 1);
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits, s->native.len,
            atom_id);
    }
    s->native.len++;
    space_note_atom_id_storage_peak(s);
    space_bump_revision(s);
    if (queue_gap) {
        space_mark_indexes_dirty(s);
        space_match_backend_note_remove(s);
        return;
    }
    if (defer_secondary) {
        s->native.eq_idx_dirty = true;
        s->native.ty_idx_dirty = true;
        s->native.exact_idx_dirty = true;
        s->native.has_non_exact_atoms_dirty = true;
        /* id_present is synced lazily at query time (append-only suffix via
           id_present_synced_len), so a deferred add needs no per-add work here --
           this is what keeps a forward-chaining loop O(1) per contains AND keeps
           pure ingress free of lazy blob decodes. */
    } else {
        /* Index equations by head symbol */
        if (!s->native.eq_idx_dirty) {
            AtomId lhs_id = CETTA_ATOM_ID_NONE;
            AtomId rhs_id = CETTA_ATOM_ID_NONE;
            if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
                eq_index_add_id(&s->native.eq_idx, s, lhs_id, idx);
            } else {
                Atom *lhs, *rhs;
                if (space_equation_children_at_id(s, atom_id, &lhs, &rhs))
                    eq_index_add(&s->native.eq_idx, lhs, idx);
            }
        }
        /* Index type annotations (: atom type) */
        if (!s->native.ty_idx_dirty) {
            AtomId subject_id = CETTA_ATOM_ID_NONE;
            AtomId type_id = CETTA_ATOM_ID_NONE;
            if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
                ty_ann_index_add_id(&s->native.ty_idx, s, subject_id, idx);
            } else {
                Atom *subject = space_type_annotation_subject_at_id(s, atom_id);
                if (subject)
                    ty_ann_index_add(&s->native.ty_idx, subject, idx);
            }
        }
        if (!s->native.exact_idx_dirty && atom_id_is_exact_indexable(s, atom_id)) {
            exact_atom_bucket_add(
                &s->native.exact_idx.buckets[exact_atom_hash_id(s, atom_id)], idx);
        }
        /* id_present is synced lazily at membership-query time (id_present_sync),
           not per-add, so pure ingress never forces a canonicalizing blob decode. */
        if (!s->native.has_non_exact_atoms_dirty &&
            !atom_id_is_exact_indexable(s, atom_id))
            s->native.has_non_exact_atoms = true;
    }
    /* Match backend owns its own incremental indexing policy. */
    if (backend_needs_atom)
        cetta_provenance_assert_not_transient(backend_atom,
                                             "space.backend.note_add");
    space_match_backend_note_add(s, atom_id,
                                 backend_needs_atom ? backend_atom : NULL, idx);
}

void space_add_atom_id(Space *s, AtomId atom_id) {
    if (!s || !space_tracks_atom_ids(s) || atom_id == CETTA_ATOM_ID_NONE)
        return;
    if (space_store_via_backend_primary(s, atom_id, NULL))
        return;
    space_add_stored_id(s, atom_id, NULL);
}

bool space_add_atom_ids_batch(Space *s, const AtomId *atom_ids,
                              CettaCount atom_count) {
    uint64_t added = 0;
    bool keep_pathmap_exact_metadata;
    bool had_non_exact_atom;
    bool batch_has_non_exact_atom = false;
    SpaceBackendBatchResult batch_result;

    if (!s || (!atom_ids && atom_count != 0))
        return false;
    if (atom_count == 0)
        return true;

    keep_pathmap_exact_metadata =
        !space_has_overlay_base(s) &&
        s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->native.has_non_exact_atoms_dirty;
    had_non_exact_atom =
        keep_pathmap_exact_metadata && s->native.has_non_exact_atoms;
    if (keep_pathmap_exact_metadata) {
        for (CettaCount i = 0; i < atom_count; i++) {
            if (!atom_id_is_exact_indexable(s, atom_ids[i])) {
                batch_has_non_exact_atom = true;
                break;
            }
        }
    }

    batch_result = space_has_overlay_base(s)
        ? SPACE_BACKEND_BATCH_UNSUPPORTED
        : space_match_backend_store_atom_ids_batch_direct(
              s, atom_ids, atom_count, &added);
    if (batch_result == SPACE_BACKEND_BATCH_ERROR)
        return false;
    if (batch_result == SPACE_BACKEND_BATCH_APPLIED) {
        if (added != (uint64_t)atom_count)
            return false;
        space_mark_indexes_dirty(s);
        if (keep_pathmap_exact_metadata) {
            s->native.has_non_exact_atoms =
                had_non_exact_atom || batch_has_non_exact_atom;
            s->native.has_non_exact_atoms_dirty = false;
        }
        space_bump_revision(s);
        return true;
    }

    /* Oracle replay keeps ordered multiplicity and overlay behavior for every
       fragment the backend declines, including variables and wide symbols. */
    for (CettaCount i = 0; i < atom_count; i++)
        space_add_atom_id(s, atom_ids[i]);
    return space_term_universe_last_error_code(s) == TERM_UNIVERSE_ERROR_NONE;
}

void space_add(Space *s, Atom *atom) {
    AtomId atom_id = CETTA_ATOM_ID_NONE;
    if (space_store_atom_via_backend_primary(s, atom))
        return;
    if (space_tracks_atom_ids(s) && atom) {
        atom_id = term_universe_store_atom_id(s->native.universe, NULL, atom);
        if (atom_id == CETTA_ATOM_ID_NONE)
            return;
        if (space_store_via_backend_primary(s, atom_id, atom))
            return;
    }
    space_add_stored_id(s, atom_id, atom);
}

static bool space_term_universe_failed(const Space *s) {
    TermUniverse *universe = s ? s->native.universe : NULL;
    return term_universe_last_error_code(universe) != TERM_UNIVERSE_ERROR_NONE;
}

TermUniverseError space_term_universe_last_error_code(const Space *s) {
    TermUniverse *universe = s ? s->native.universe : NULL;
    return term_universe_last_error_code(universe);
}


static bool space_admit_atom_impl(Space *s, Arena *fallback,
                                  const Arena *source_arena, Atom *atom) {
    if (!s || !atom)
        return false;

    if (space_store_atom_via_backend_primary(s, atom))
        return true;

    if (space_tracks_atom_ids(s)) {
        AtomId atom_id = source_arena
            ? term_universe_store_atom_id_from_source_arena(
                  s->native.universe, fallback, source_arena, atom)
            : term_universe_store_atom_id(
                  s->native.universe, fallback, atom);
        if (atom_id != CETTA_ATOM_ID_NONE) {
            if (space_store_via_backend_primary(s, atom_id, atom))
                return true;
            space_add_stored_id(s, atom_id, atom);
            return true;
        }
        if (space_term_universe_failed(s))
            return false;
        Atom *stored = space_store_atom(s, fallback, atom);
        if (!stored)
            return false;
        AtomId stored_id =
            term_universe_lookup_atom_id(s->native.universe, stored);
        if (stored_id == CETTA_ATOM_ID_NONE)
            return false;
        space_add_atom_id(s, stored_id);
        return true;
    }

    Atom *stored = space_store_atom(s, fallback, atom);
    if (!stored)
        return false;
    CettaIndex len_before = s->native.len;
    space_add(s, stored);
    return s->native.len == len_before + 1;
}

bool space_admit_atom(Space *s, Arena *fallback, Atom *atom) {
    return space_admit_atom_impl(s, fallback, NULL, atom);
}

bool space_admit_atom_from_source_arena(
    Space *s, Arena *fallback, const Arena *source_arena, Atom *atom) {
    if (!term_universe_source_id_memo_enabled())
        return space_admit_atom_impl(s, fallback, NULL, atom);
    return space_admit_atom_impl(s, fallback, source_arena, atom);
}

Space *space_heap_clone_shallow(Space *src) {
    Space *clone;
    CettaCount logical_len = 0;
    uint32_t narrow_len = 0;
    if (!src)
        return NULL;
    logical_len = space_length64(src);
    if (src->native.len != logical_len) {
        if (!space_length_u32_checked(src, &narrow_len))
            return NULL;
        logical_len = narrow_len;
    }
    clone = cetta_malloc(sizeof(Space));
    if (!clone)
        return NULL;
    space_init_with_universe(clone, src ? src->native.universe : NULL);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (CettaIndex i = 0; i < logical_len; i++) {
        AtomId atom_id = space_get_atom_id_at64(src, i);
        Atom *source_atom = NULL;
        if (atom_id != CETTA_ATOM_ID_NONE) {
            space_add_atom_id(clone, atom_id);
            continue;
        }
        source_atom = space_get_at64(src, i);
        if (!source_atom) {
            space_match_backend_set_error(
                SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE);
            space_free(clone);
            free(clone);
            return NULL;
        }
        if (!space_admit_atom(clone, NULL, source_atom))
            space_add(clone, source_atom);
    }
    return clone;
}

void space_replace_contents(Space *dst, Space *src) {
    if (!dst || !src || dst == src)
        return;
    uint64_t dst_instance_id = dst->instance_id;
    uint64_t old_revision = dst->revision;
    uint64_t src_revision = src->revision;
    space_free(dst);
    space_move_storage_and_backend(dst, src);
    space_detach_from_universe(src);
    space_attach_to_universe(dst, dst->native.universe);
    dst->instance_id = dst_instance_id;
    dst->revision = old_revision > src_revision ? old_revision : src_revision;
    space_bump_revision(dst);
    space_reset_moved_from(src);
}

/* ── Query Results ──────────────────────────────────────────────────────── */

void query_results_init(QueryResults *qr) {
    memset(qr->inline_items, 0, sizeof(qr->inline_items));
    qr->items = qr->inline_items;
    qr->len = 0;
    qr->cap = 1;
}

static CettaCount query_results_capacity_limit(void) {
    if (g_query_results_capacity_limit_override != 0)
        return g_query_results_capacity_limit_override;
    return (CettaCount)(SIZE_MAX / sizeof(QueryResult));
}

void query_results_diag_set_capacity_limit_override(CettaCount limit) {
    g_query_results_capacity_limit_override = limit;
}

static void query_results_fatal_capacity(CettaCount len, CettaCount limit,
                                         size_t bytes) {
    fprintf(stderr,
            "fatal: query result capacity exhausted "
            "(len=%" PRIu64 " limit=%" PRIu64 " next-bytes=%zu)\n",
            (uint64_t)len, (uint64_t)limit, bytes);
    abort();
}

static bool query_results_ensure_one(QueryResults *qr) {
    CettaCount limit;
    CettaCount need;
    CettaCount next;
    if (!qr)
        return false;
    limit = query_results_capacity_limit();
    if (qr->len >= limit)
        query_results_fatal_capacity(qr->len, limit, 0u);
    if (qr->len < qr->cap)
        return true;
    need = qr->len + 1u;
    next = qr->cap ? qr->cap * 2u : 8u;
    if (next <= qr->cap || next < need)
        next = need;
    if (next > limit)
        next = limit;
    if (next < need)
        query_results_fatal_capacity(qr->len, limit,
                                     sizeof(QueryResult) * (size_t)need);
    if (qr->items == qr->inline_items) {
        QueryResult *heap = cetta_malloc(sizeof(QueryResult) * (size_t)next);
        if (qr->len > 0)
            memcpy(heap, qr->items, sizeof(QueryResult) * (size_t)qr->len);
        qr->items = heap;
    } else {
        qr->items = cetta_realloc(qr->items, sizeof(QueryResult) * (size_t)next);
    }
    qr->cap = next;
    return true;
}

bool query_results_push(QueryResults *qr, Atom *result, Bindings *b) {
    if (!query_results_ensure_one(qr))
        return false;
    qr->items[qr->len].result = result;
    if (!bindings_clone(&qr->items[qr->len].bindings, b))
        return false;
    qr->len++;
    return true;
}

bool query_results_push_move(QueryResults *qr, Atom *result, Bindings *b) {
    if (!query_results_ensure_one(qr))
        return false;
    qr->items[qr->len].result = result;
    bindings_move(&qr->items[qr->len].bindings, b);
    qr->len++;
    return true;
}

CettaCount query_results_visit(const QueryResults *qr, QueryResultVisitor visitor,
                               void *ctx) {
    CettaCount visited = 0;
    if (!qr || !visitor)
        return 0;
    for (CettaIndex i = 0; i < qr->len; i++) {
        visited++;
        if (!visitor(qr->items[i].result, &qr->items[i].bindings, ctx))
            break;
    }
    return visited;
}

void query_results_free(QueryResults *qr) {
    for (CettaIndex i = 0; i < qr->len; i++)
        bindings_free(&qr->items[i].bindings);
    if (qr->items != qr->inline_items)
        free(qr->items);
    memset(qr->inline_items, 0, sizeof(qr->inline_items));
    qr->items = qr->inline_items;
    qr->len = 0;
    qr->cap = 1;
}

typedef struct {
    VarId var_id;
    SymbolId spelling;
    Atom *name_key;
} QueryVisibleVar;

#define QUERY_VISIBLE_INLINE_CAP 8

typedef struct {
    QueryVisibleVar inline_items[QUERY_VISIBLE_INLINE_CAP];
    QueryVisibleVar *items;
    CettaExprLen len;
    CettaExprLen cap;
} QueryVisibleVarSet;

static void query_visible_var_set_init(QueryVisibleVarSet *set) {
    set->items = set->inline_items;
    set->len = 0;
    set->cap = QUERY_VISIBLE_INLINE_CAP;
}

static void query_visible_var_set_free(QueryVisibleVarSet *set) {
    if (set->items != set->inline_items)
        free(set->items);
    set->items = set->inline_items;
    set->len = 0;
    set->cap = QUERY_VISIBLE_INLINE_CAP;
}

static bool query_visible_var_set_reserve(QueryVisibleVarSet *set,
                                          CettaExprLen needed) {
    if (needed <= set->cap)
        return true;
    CettaExprLen next_cap = set->cap ? set->cap : QUERY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(QueryVisibleVar)))
        return false;
    QueryVisibleVar *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(QueryVisibleVar) * (size_t)next_cap)
        : cetta_realloc(set->items, sizeof(QueryVisibleVar) * (size_t)next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(QueryVisibleVar) * (size_t)set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool query_visible_var_set_add(QueryVisibleVarSet *set, Atom *var) {
    if (!set || !var || var->kind != ATOM_VAR)
        return false;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_VISIBLE_DEDUP_SCAN,
                            set->len);
    for (CettaExprIndex i = 0; i < set->len; i++) {
        if (set->items[i].var_id == var->var_id)
            return true;
    }
    if (!query_visible_var_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].var_id = var->var_id;
    set->items[set->len].spelling = var->sym_id;
    set->items[set->len].name_key = var->name_key;
    set->len++;
    return true;
}

static bool query_visible_var_set_contains(const QueryVisibleVarSet *set,
                                           VarId var_id) {
    for (CettaExprIndex i = 0; set && i < set->len; i++) {
        if (set->items[i].var_id == var_id)
            return true;
    }
    return false;
}

static bool collect_query_visible_vars_rec(Atom *atom, QueryVisibleVarSet *set) {
    if (!atom || !atom_has_vars(atom))
        return true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_VISIBLE_NODE_VISIT);
    if (atom->kind == ATOM_VAR)
        return query_visible_var_set_add(set, atom);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (!collect_query_visible_vars_rec(atom->expr.elems[i], set))
            return false;
    }
    return true;
}

static bool atom_refs_only_query_visible_vars(Atom *atom,
                                              const QueryVisibleVarSet *visible) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR)
        return query_visible_var_set_contains(visible, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (!atom_refs_only_query_visible_vars(atom->expr.elems[i], visible))
            return false;
    }
    return true;
}

static const QueryVisibleVar *query_visible_alias_for_var(
    Atom *atom,
    const QueryVisibleVarSet *visible,
    const Bindings *full) {
    const QueryVisibleVar *found = NULL;
    if (!atom || atom->kind != ATOM_VAR || !visible || !full)
        return NULL;
    for (CettaExprIndex i = 0; i < visible->len; i++) {
        Atom *exact = bindings_lookup_id((Bindings *)full, visible->items[i].var_id);
        if (!exact || exact->kind != ATOM_VAR)
            continue;
        if (exact->var_id != atom->var_id)
            continue;
        if (atom->var_id == visible->items[i].var_id)
            return NULL;
        if (found && found->var_id != visible->items[i].var_id)
            return NULL;
        found = &visible->items[i];
    }
    return found;
}

static Atom *rewrite_query_visible_aliases(Arena *a, Atom *atom,
                                           const QueryVisibleVarSet *visible,
                                           const Bindings *full) {
    if (!atom || !visible || visible->len == 0 || !atom_has_vars(atom))
        return atom;
    if (atom->kind == ATOM_VAR) {
        const QueryVisibleVar *alias =
            query_visible_alias_for_var(atom, visible, full);
        return alias
            ? atom_var_with_presentation(
                  a, alias->spelling, alias->name_key, alias->var_id)
            : atom;
    }
    if (atom->kind != ATOM_EXPR)
        return atom;

    Atom **rewritten = NULL;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        Atom *child = atom->expr.elems[i];
        Atom *next = rewrite_query_visible_aliases(a, child, visible, full);
        if (!rewritten && next != child) {
            rewritten = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            for (CettaExprIndex j = 0; j < i; j++)
                rewritten[j] = atom->expr.elems[j];
        }
        if (rewritten)
            rewritten[i] = next;
    }
    return rewritten ? atom_expr(a, rewritten, atom->expr.len) : atom;
}

static Atom *bindings_apply_without_self_id(Bindings *full, Arena *a,
                                            VarId skip_id, Atom *value) {
    if (!value || !atom_has_vars(value) || !full || full->len == 0)
        return value;
    Bindings reduced;
    if (!bindings_clone(&reduced, full))
        return bindings_apply_if_vars(full, a, value);
    bool removed = false;
    for (uint32_t i = 0; i < reduced.len; i++) {
        if (reduced.entries[i].var_id != skip_id)
            continue;
        removed = bindings_remove_entry_at(&reduced, i);
        break;
    }
    Atom *resolved = removed ? bindings_apply_if_vars(&reduced, a, value)
                             : bindings_apply_if_vars(full, a, value);
    bindings_free(&reduced);
    return resolved;
}

static Atom *bindings_resolve_query_visible_var(Arena *a, const Bindings *full,
                                                const QueryVisibleVar *wanted) {
    Atom *exact = bindings_lookup_id((Bindings *)full, wanted->var_id);
    if (exact) {
        if (!atom_has_vars(exact))
            return exact;
        return bindings_apply_without_self_id((Bindings *)full, a,
                                              wanted->var_id, exact);
    }

    Atom *slot_var = atom_var_with_presentation(
        a, wanted->spelling, wanted->name_key, wanted->var_id);
    if (!slot_var)
        return NULL;
    Atom *resolved = bindings_apply_if_vars(full, a, slot_var);
    if (resolved != slot_var)
        return resolved;

    /* Do NOT fall back to same-base/same-spelling lookup. That fallback
       incorrectly captures outer-scope variables that happen to share the same
       spelling as inner-scope variables during equation evaluation. The exact
       var_id lookup above and bindings_apply_if_vars are the only correct
       resolution paths. */
    return slot_var;
}

static bool project_query_visible_bindings(Arena *a,
                                           const QueryVisibleVarSet *visible,
                                           const Bindings *full,
                                           Bindings *projected) {
    bindings_init(projected);

    if (visible->len == 0) {
        return true;
    }

    for (uint32_t i = 0; i < visible->len; i++) {
        Atom *resolved =
            bindings_resolve_query_visible_var(a, full, &visible->items[i]);
        if (!resolved) {
            bindings_free(projected);
            return false;
        }
        resolved = rewrite_query_visible_aliases(a, resolved, visible, full);
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == visible->items[i].var_id) {
            continue;
        }
        Atom *visible_var = atom_var_with_presentation(
            a, visible->items[i].spelling, visible->items[i].name_key,
            visible->items[i].var_id);
        if (!visible_var ||
            !bindings_add_var(projected, visible_var, resolved)) {
            bindings_free(projected);
            return false;
        }
    }

    for (uint32_t i = 0; i < full->eq_len; i++) {
        Atom *lhs = bindings_apply_if_vars(full, a, full->constraints[i].lhs);
        Atom *rhs = bindings_apply_if_vars(full, a, full->constraints[i].rhs);
        lhs = rewrite_query_visible_aliases(a, lhs, visible, full);
        rhs = rewrite_query_visible_aliases(a, rhs, visible, full);
        if (!atom_refs_only_query_visible_vars(lhs, visible) ||
            !atom_refs_only_query_visible_vars(rhs, visible)) {
            continue;
        }
        if (!bindings_add_constraint(projected, lhs, rhs)) {
            bindings_free(projected);
            return false;
        }
    }
    return true;
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

/* is_equation_atom defined above in Space section */

/* ── Space Registry ─────────────────────────────────────────────────────── */

void registry_init(Registry *r) {
    if (!r) return;
    r->entries = r->inline_entries;
    r->len = 0;
    r->cap = (uint32_t)(sizeof(r->inline_entries) /
                        sizeof(r->inline_entries[0]));
    r->name_keys = name_key_table_new(32u);
    r->index_slots = r->inline_index_slots;
    r->index_cap = (uint32_t)(sizeof(r->inline_index_slots) /
                              sizeof(r->inline_index_slots[0]));
    memset(r->inline_index_slots, 0, sizeof(r->inline_index_slots));
}

void registry_free(Registry *r) {
    if (!r) return;
    if (r->entries && r->entries != r->inline_entries)
        free(r->entries);
    if (r->index_slots && r->index_slots != r->inline_index_slots)
        free(r->index_slots);
    name_key_table_delete(r->name_keys);
    r->entries = r->inline_entries;
    r->len = 0;
    r->cap = (uint32_t)(sizeof(r->inline_entries) /
                        sizeof(r->inline_entries[0]));
    r->name_keys = NULL;
    r->index_slots = r->inline_index_slots;
    r->index_cap = (uint32_t)(sizeof(r->inline_index_slots) /
                              sizeof(r->inline_index_slots[0]));
    memset(r->inline_index_slots, 0, sizeof(r->inline_index_slots));
}

static uint32_t registry_index_hash(SymbolId key, NameId name_id) {
    uint32_t x = key != SYMBOL_ID_NONE
        ? ((uint32_t)key ^ UINT32_C(0x243f6a88))
        : ((uint32_t)name_id ^ UINT32_C(0x9e3779b9));
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    x ^= x >> 16;
    return x;
}

static bool registry_entry_matches(const RegistryEntry *entry,
                                   SymbolId key, NameId name_id) {
    return entry && entry->key == key && entry->name_id == name_id;
}

static uint32_t registry_index_find(const Registry *r, SymbolId key,
                                    NameId name_id) {
    if (!r || !r->index_slots || r->index_cap == 0u)
        return UINT32_MAX;
    uint32_t mask = r->index_cap - 1u;
    uint32_t slot = registry_index_hash(key, name_id) & mask;
    for (uint32_t probes = 0u; probes < r->index_cap; probes++) {
        uint32_t encoded = r->index_slots[slot];
        if (encoded == 0u) return UINT32_MAX;
        uint32_t index = encoded - 1u;
        if (index < r->len &&
            registry_entry_matches(&r->entries[index], key, name_id))
            return index;
        slot = (slot + 1u) & mask;
    }
    return UINT32_MAX;
}

static void registry_index_insert(Registry *r, uint32_t entry_index) {
    RegistryEntry *entry = &r->entries[entry_index];
    uint32_t mask = r->index_cap - 1u;
    uint32_t slot = registry_index_hash(entry->key, entry->name_id) & mask;
    while (r->index_slots[slot] != 0u)
        slot = (slot + 1u) & mask;
    r->index_slots[slot] = entry_index + 1u;
}

static bool registry_index_ensure_capacity(Registry *r,
                                           uint32_t needed_entries) {
    if (!r) return false;
    uint32_t next_cap = r->index_cap ? r->index_cap : 32u;
    while ((uint64_t)needed_entries * 10u > (uint64_t)next_cap * 7u) {
        if (next_cap > UINT32_MAX / 2u) return false;
        next_cap *= 2u;
    }
    if (r->index_slots && r->index_cap == next_cap) return true;
    uint32_t *next = cetta_malloc(sizeof(*next) * (size_t)next_cap);
    memset(next, 0, sizeof(*next) * (size_t)next_cap);
    if (r->index_slots && r->index_slots != r->inline_index_slots)
        free(r->index_slots);
    r->index_slots = next;
    r->index_cap = next_cap;
    for (uint32_t i = 0u; i < r->len; i++)
        registry_index_insert(r, i);
    return true;
}

static bool registry_ensure_capacity(Registry *r, uint32_t min_needed) {
    uint32_t next_cap;
    RegistryEntry *next;
    if (!r)
        return false;
    if (!r->entries) {
        r->entries = r->inline_entries;
        r->cap = (uint32_t)(sizeof(r->inline_entries) /
                            sizeof(r->inline_entries[0]));
    }
    if (r->cap >= min_needed)
        return true;
    next_cap = r->cap ? r->cap : 16u;
    while (next_cap < min_needed) {
        if (next_cap > UINT32_MAX / 2u)
            return false;
        next_cap *= 2u;
    }
    if ((size_t)next_cap > SIZE_MAX / sizeof(RegistryEntry))
        return false;
    if (r->entries == r->inline_entries) {
        next = cetta_malloc(sizeof(RegistryEntry) * (size_t)next_cap);
        if (r->len > 0)
            memcpy(next, r->entries, sizeof(RegistryEntry) * (size_t)r->len);
    } else {
        next = cetta_realloc(r->entries,
                             sizeof(RegistryEntry) * (size_t)next_cap);
    }
    r->entries = next;
    r->cap = next_cap;
    return true;
}

void registry_bind_id(Registry *r, SymbolId key, Atom *value) {
    if (!r || key == SYMBOL_ID_NONE) return;
    uint32_t existing = registry_index_find(r, key, NAME_ID_NONE);
    if (existing != UINT32_MAX) {
        r->entries[existing].value = value;
        return;
    }
    if (!registry_ensure_capacity(r, r->len + 1u) ||
        !registry_index_ensure_capacity(r, r->len + 1u))
        return;
    r->entries[r->len].key = key;
    r->entries[r->len].name_id = NAME_ID_NONE;
    r->entries[r->len].value = value;
    registry_index_insert(r, r->len);
    r->len++;
}

Atom *registry_lookup_id(Registry *r, SymbolId key) {
    if (!r || key == SYMBOL_ID_NONE) return NULL;
    uint32_t index = registry_index_find(r, key, NAME_ID_NONE);
    return index == UINT32_MAX ? NULL : r->entries[index].value;
}

void registry_bind(Registry *r, const char *name, Atom *value) {
    registry_bind_id(r, symbol_intern_cstr(g_symbols, name), value);
}

Atom *registry_lookup(Registry *r, const char *name) {
    return registry_lookup_id(r, symbol_intern_cstr(g_symbols, name));
}

bool registry_bind_name(Registry *r, Atom *name_key, Atom *value) {
    if (!r || !r->name_keys || !name_key) return false;
    NameId id = name_key_intern(r->name_keys, name_key);
    if (id == NAME_ID_NONE) return false;
    uint32_t existing = registry_index_find(r, SYMBOL_ID_NONE, id);
    if (existing != UINT32_MAX) {
        r->entries[existing].value = value;
        return true;
    }
    if (!registry_ensure_capacity(r, r->len + 1u) ||
        !registry_index_ensure_capacity(r, r->len + 1u))
        return false;
    r->entries[r->len].key = SYMBOL_ID_NONE;
    r->entries[r->len].name_id = id;
    r->entries[r->len].value = value;
    registry_index_insert(r, r->len);
    r->len++;
    return true;
}

Atom *registry_lookup_name(Registry *r, Atom *name_key) {
    if (!r || !r->name_keys || !name_key) return NULL;
    NameId id = name_key_find(r->name_keys, name_key);
    if (id == NAME_ID_NONE) return NULL;
    uint32_t index = registry_index_find(r, SYMBOL_ID_NONE, id);
    return index == UINT32_MAX ? NULL : r->entries[index].value;
}

const Atom *registry_entry_name_key(const Registry *r, uint32_t index) {
    if (!r || index >= r->len || !r->name_keys ||
        r->entries[index].key != SYMBOL_ID_NONE ||
        r->entries[index].name_id == NAME_ID_NONE) {
        return NULL;
    }
    return name_key_lookup(r->name_keys, r->entries[index].name_id);
}

bool registry_ref_name_key(Atom *ref, Atom **name_key_out) {
    if (name_key_out) *name_key_out = NULL;
    if (!ref || ref->kind != ATOM_EXPR || ref->expr.len != 2u ||
        !atom_is_symbol(ref->expr.elems[0], "resolve-name")) {
        return false;
    }
    Atom *quoted = ref->expr.elems[1];
    if (!quoted || quoted->kind != ATOM_EXPR || quoted->expr.len != 2u ||
        !atom_is_symbol_id(quoted->expr.elems[0], g_builtin_syms.quote)) {
        return false;
    }
    if (name_key_out) *name_key_out = quoted->expr.elems[1];
    return true;
}

Atom *registry_lookup_ref(Registry *r, Atom *ref) {
    if (!r || !ref) return NULL;
    if (ref->kind == ATOM_SYMBOL)
        return registry_lookup_id(r, ref->sym_id);
    Atom *name_key = NULL;
    return registry_ref_name_key(ref, &name_key)
        ? registry_lookup_name(r, name_key)
        : NULL;
}

Space *resolve_space(Registry *r, Atom *ref) {
    /* Grounded space atom → direct pointer */
    if (ref->kind == ATOM_GROUNDED && ref->ground.gkind == GV_SPACE)
        return (Space *)ref->ground.ptr;
    /* Symbol like &self → registry lookup */
    if (ref->kind == ATOM_SYMBOL || ref->kind == ATOM_EXPR) {
        Atom *val = registry_lookup_ref(r, ref);
        if (val && val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE)
            return (Space *)val->ground.ptr;
    }
    return NULL;
}

bool space_remove(Space *s, Atom *atom) {
    if (!s)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaIndex alpha_index = 0;
        CettaCount alpha_count = 0;
        bool alpha_in_base = false;
        for (CettaCount i = 0; i < base_visible; i++) {
            CettaIndex raw = 0;
            Atom *candidate = NULL;
            if (!space_overlay_visible_base_raw_index(s, i, &raw))
                continue;
            candidate = space_get_at64(s->overlay_base, raw);
            if (candidate && atom_eq(candidate, atom)) {
                if (!space_overlay_mark_base_removed(s, raw))
                    return false;
                space_bump_revision(s);
                return true;
            }
        }
        for (CettaIndex i = 0; i < space_local_length64(s); i++) {
            Atom *candidate = space_local_get_at64(s, i);
            if (candidate && atom_eq(candidate, atom))
                return space_overlay_remove_local_raw_index(s, i);
        }
        for (CettaCount i = 0; i < base_visible; i++) {
            CettaIndex raw = 0;
            Atom *candidate = NULL;
            if (!space_overlay_visible_base_raw_index(s, i, &raw))
                continue;
            candidate = space_get_at64(s->overlay_base, raw);
            if (candidate && atom_alpha_eq(candidate, atom)) {
                alpha_index = raw;
                alpha_in_base = true;
                alpha_count++;
            }
        }
        for (CettaIndex i = 0; i < space_local_length64(s); i++) {
            Atom *candidate = space_local_get_at64(s, i);
            if (candidate && atom_alpha_eq(candidate, atom)) {
                alpha_index = i;
                alpha_in_base = false;
                alpha_count++;
            }
        }
        if (alpha_count != 1u)
            return false;
        if (alpha_in_base) {
            if (!space_overlay_mark_base_removed(s, alpha_index))
                return false;
            space_bump_revision(s);
            return true;
        }
        return space_overlay_remove_local_raw_index(s, alpha_index);
    }
    if (s->native.universe && atom) {
        AtomId atom_id = term_universe_lookup_atom_id(s->native.universe, atom);
        if (atom_id != CETTA_ATOM_ID_NONE &&
            space_remove_via_backend_primary(s, atom_id)) {
            return true;
        }
    }
    if (space_remove_atom_via_backend_primary(s, atom))
        return true;
    if (!atom)
        return false;
    /* A live PathMap bridge is the primary store and must not be bypassed by
       mutating its native projection.  When the bridge has explicitly fallen
       back as unavailable, however, the native shadow is the authoritative
       store; preserve native exact-then-unique-alpha removal semantics there. */
    if (s->match_backend.kind == SPACE_ENGINE_PATHMAP &&
        !s->match_backend.pathmap.bridge.bridge_unavailable)
        return false;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    bool found = false;
    CettaIndex remove_idx = 0;
    for (CettaIndex i = 0; i < s->native.len; i++) {
        Atom *candidate = space_get_at64(s, i);
        if (!candidate)
            continue;
        if (atom_eq(candidate, atom)) {
            remove_idx = i;
            found = true;
            break;
        }
    }
    if (!found) {
        CettaIndex alpha_idx = 0;
        CettaCount alpha_count = 0;
        for (CettaIndex i = 0; i < s->native.len; i++) {
            Atom *candidate = space_get_at64(s, i);
            if (!candidate)
                continue;
            if (atom_alpha_eq(candidate, atom)) {
                alpha_idx = i;
                alpha_count++;
            }
        }
        if (alpha_count == 1) {
            remove_idx = alpha_idx;
            found = true;
        }
    }
    if (!found)
        return false;

    if (space_is_ordered(s)) {
        size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
        memmove(s->native.atom_ids + ((size_t)remove_idx * width),
                s->native.atom_ids + ((size_t)(remove_idx + 1u) * width),
                (size_t)(s->native.len - remove_idx - 1u) * width);
        s->native.len--;
    } else {
        AtomId tail_id = space_atom_id_storage_load_at(
            s->native.atom_ids, s->native.atom_id_width_bits,
            s->native.len - 1u);
        s->native.len--;
        (void)space_atom_id_storage_store_at(
            s->native.atom_ids, s->native.atom_id_width_bits, remove_idx,
            tail_id); /* swap with last */
    }
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

bool space_contains_atom_id(const Space *s, AtomId atom_id) {
    CettaCount logical_len = 0;
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    logical_len = space_length64(s);
    for (CettaIndex i = 0; i < logical_len; i++) {
        if (space_get_atom_id_at64(s, i) == atom_id)
            return true;
    }
    return false;
}

bool space_remove_atom_id(Space *s, AtomId atom_id) {
    if (!s || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount base_visible = space_overlay_visible_base_count(s);
        for (CettaCount i = 0; i < base_visible; i++) {
            CettaIndex raw = 0;
            if (!space_overlay_visible_base_raw_index(s, i, &raw))
                continue;
            if (space_get_atom_id_at64(s->overlay_base, raw) != atom_id)
                continue;
            if (!space_overlay_mark_base_removed(s, raw))
                return false;
            space_bump_revision(s);
            return true;
        }
        for (CettaIndex i = 0; i < space_local_length64(s); i++) {
            if (space_local_get_atom_id_at64(s, i) == atom_id)
                return space_overlay_remove_local_raw_index(s, i);
        }
        return false;
    }
    if (space_remove_via_backend_primary(s, atom_id))
        return true;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    for (CettaIndex i = 0; i < s->native.len; i++) {
        if (space_get_atom_id_at64(s, i) != atom_id)
            continue;
        if (space_is_ordered(s)) {
            size_t width = space_atom_id_width_bytes_bits(s->native.atom_id_width_bits);
            memmove(s->native.atom_ids + ((size_t)i * width),
                    s->native.atom_ids + ((size_t)(i + 1u) * width),
                    (size_t)(s->native.len - i - 1u) * width);
            s->native.len--;
        } else {
            AtomId tail_id = space_atom_id_storage_load_at(
                s->native.atom_ids, s->native.atom_id_width_bits,
                s->native.len - 1u);
            s->native.len--;
            (void)space_atom_id_storage_store_at(
                s->native.atom_ids, s->native.atom_id_width_bits, i,
                tail_id);
        }
        space_mark_indexes_dirty(s);
        space_match_backend_note_remove(s);
        space_bump_revision(s);
        return true;
    }
    return false;
}

bool space_remove_atom_ids_batch(Space *s, const AtomId *atom_ids,
                                 CettaCount atom_count,
                                 CettaCount *out_removed) {
    uint64_t removed = 0;
    SpaceBackendBatchResult batch_result;

    if (out_removed)
        *out_removed = 0;
    if (!s || (!atom_ids && atom_count != 0))
        return false;
    if (atom_count == 0)
        return true;

    batch_result = space_has_overlay_base(s)
        ? SPACE_BACKEND_BATCH_UNSUPPORTED
        : space_match_backend_remove_atom_ids_batch_direct(
              s, atom_ids, atom_count, &removed);
    if (batch_result == SPACE_BACKEND_BATCH_ERROR)
        return false;
    if (batch_result == SPACE_BACKEND_BATCH_APPLIED) {
        if (removed != 0) {
            space_mark_indexes_dirty(s);
            space_bump_revision(s);
        }
        if (out_removed)
            *out_removed = (CettaCount)removed;
        return true;
    }

    for (CettaCount i = 0; i < atom_count; i++) {
        if (space_remove_atom_id(s, atom_ids[i]))
            removed++;
    }
    if (out_removed)
        *out_removed = (CettaCount)removed;
    return true;
}

AtomId space_get_atom_id_at(const Space *s, uint32_t idx) {
    return space_get_atom_id_at64(s, idx);
}

CettaCount space_length64(const Space *s) {
    if (!s)
        return 0;
    if (!space_has_overlay_base(s))
        return (CettaCount)space_match_backend_logical_len64(s);
    return space_overlay_visible_base_count(s) + space_local_length64(s);
}

AtomId space_get_atom_id_at64(const Space *s, CettaIndex idx) {
    if (!s)
        return CETTA_ATOM_ID_NONE;
    if (!space_has_overlay_base(s))
        return space_match_backend_get_atom_id_at64(s, idx);
    CettaCount base_visible = space_overlay_visible_base_count(s);
    if (idx < base_visible) {
        CettaIndex raw = 0;
        if (!space_overlay_visible_base_raw_index(s, idx, &raw))
            return CETTA_ATOM_ID_NONE;
        return space_get_atom_id_at64(s->overlay_base, raw);
    }
    return space_local_get_atom_id_at64(s, idx - base_visible);
}

Atom *space_get_at64(const Space *s, CettaIndex idx) {
    if (!s)
        return NULL;
    if (!space_has_overlay_base(s))
        return space_match_backend_get_at64(s, idx);
    CettaCount base_visible = space_overlay_visible_base_count(s);
    if (idx < base_visible) {
        CettaIndex raw = 0;
        if (!space_overlay_visible_base_raw_index(s, idx, &raw))
            return NULL;
        return space_get_at64(s->overlay_base, raw);
    }
    return space_local_get_at64(s, idx - base_visible);
}

Atom *space_get_at(const Space *s, uint32_t idx) {
    return space_get_at64(s, idx);
}

Atom *space_peek(const Space *s) {
    CettaCount logical_len = space_length64(s);
    if (!s || logical_len == 0)
        return NULL;
    return space_get_at64(s, space_is_queue(s) ? 0 : (logical_len - 1));
}

bool space_pop(Space *s, Atom **out) {
    CettaCount logical_len;
    Atom *top;

    if (!s)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaCount local_len = space_local_length64(s);
        logical_len = base_visible + local_len;
        if (logical_len == 0)
            return false;
        top = space_peek(s);
        if (!top)
            return false;
        if (out)
            *out = top;
        if (local_len > 0)
            return space_overlay_remove_local_raw_index(s, local_len - 1u);
        if (base_visible > 0) {
            CettaIndex raw = 0;
            if (!space_overlay_visible_base_raw_index(s, base_visible - 1u, &raw) ||
                !space_overlay_mark_base_removed(s, raw)) {
                return false;
            }
            space_bump_revision(s);
            return true;
        }
        return false;
    }
    logical_len = space_length64(s);
    if (logical_len == 0)
        return false;
    top = space_peek(s);
    if (!top)
        return false;
    if (out)
        *out = top;
    if (space_truncate_via_backend_primary64(s, logical_len - 1))
        return true;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (space_is_queue(s)) {
        s->native.start++;
        s->native.len--;
        if (s->native.len == 0)
            s->native.start = 0;
    } else {
        s->native.len--;
    }
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

bool space_truncate(Space *s, uint32_t new_len) {
    uint32_t logical_len;

    if (!s)
        return false;
    if (space_has_overlay_base(s))
        return space_truncate64(s, new_len);
    if (!space_length_u32_checked(s, &logical_len))
        return false;
    if (new_len > logical_len)
        return false;
    if (space_truncate_via_backend_primary(s, new_len))
        return true;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (new_len == s->native.len)
        return true;
    s->native.len = new_len;
    if (s->native.len == 0)
        s->native.start = 0;
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

bool space_truncate64(Space *s, CettaCount new_len) {
    CettaCount logical_len;

    if (!s)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaCount local_len = space_local_length64(s);
        logical_len = base_visible + local_len;
        if (new_len > logical_len)
            return false;
        if (new_len >= base_visible) {
            CettaCount want_local = new_len - base_visible;
            if (want_local == local_len)
                return true;
            if (!space_match_backend_materialize_native_storage(s, NULL))
                return false;
            s->native.len = want_local;
            if (s->native.len == 0)
                s->native.start = 0;
            space_mark_indexes_dirty(s);
            space_match_backend_note_remove(s);
            space_bump_revision(s);
            return true;
        }
        if (!space_match_backend_materialize_native_storage(s, NULL))
            return false;
        s->native.len = 0;
        s->native.start = 0;
        if (new_len == 0) {
            s->overlay_base_visible_len = 0;
            s->overlay_removed_base_len = 0;
        } else {
            CettaIndex raw = 0;
            if (!space_overlay_visible_base_raw_index(s, new_len - 1u, &raw))
                return false;
            s->overlay_base_visible_len = raw + 1u;
            space_overlay_clear_removed_at_or_after(s, s->overlay_base_visible_len);
        }
        space_mark_indexes_dirty(s);
        space_match_backend_note_remove(s);
        space_bump_revision(s);
        return true;
    }
    if (space_truncate_via_backend_primary64(s, new_len))
        return true;
    logical_len = space_length64(s);
    if (new_len > logical_len)
        return false;
    if (!space_match_backend_materialize_native_storage(s, NULL))
        return false;
    if (new_len == s->native.len)
        return true;
    s->native.len = new_len;
    if (s->native.len == 0)
        s->native.start = 0;
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

bool space_length_u32_checked(const Space *s, uint32_t *out_len) {
    return space_match_backend_u32_bound_checked(
        space_length64(s), SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE,
        out_len);
}

CettaIndex space_exact_match_indices64(Space *s, Atom *atom, CettaIndex **out) {
    if (out)
        *out = NULL;
    /* Exact index is maintained for ALL space kinds - enable for native too */
    if (!s || !atom || atom_has_variables(atom) || !atom_is_exact_indexable(atom))
        return 0;
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        CettaIndex *matches = cetta_malloc(sizeof(CettaIndex) * (size_t)logical_len);
        CettaIndex n = 0;
        for (CettaIndex i = 0; i < logical_len; i++) {
            Atom *candidate = space_get_at64(s, i);
            if (candidate && atom_eq(candidate, atom))
                matches[n++] = i;
        }
        if (n == 0) {
            free(matches);
            return 0;
        }
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT, n);
        if (out)
            *out = matches;
        else
            free(matches);
        return n;
    }
    if (!space_sync_exact_membership_from_backend(s))
        return 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP);
    if (!s->native.universe)
        return 0;
    AtomId query_id = term_universe_lookup_atom_id(s->native.universe, atom);
    if (query_id == CETTA_ATOM_ID_NONE)
        return 0;
#ifndef NDEBUG
    assert(term_universe_atom_id_eq(s->native.universe, query_id, atom));
#endif
    ensure_exact_index(s);
    ExactAtomBucket *bucket =
        &s->native.exact_idx.buckets[exact_atom_hash_id(s, query_id)];
    if (bucket->len == 0)
        return 0;
    CettaIndex *matches = cetta_malloc(sizeof(CettaIndex) * bucket->len);
    CettaIndex n = 0;
    for (CettaIndex i = 0; i < bucket->len; i++) {
        CettaIndex idx = bucket->indices[i];
        if (idx >= s->native.len)
            continue;
        AtomId candidate_id = space_get_atom_id_at64(s, idx);
#ifndef NDEBUG
        assert(candidate_id != CETTA_ATOM_ID_NONE);
#endif
        if (candidate_id == query_id)
            matches[n++] = idx;
    }
    if (n == 0) {
        free(matches);
        return 0;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT, n);
    if (out)
        *out = matches;
    else
        free(matches);
    return n;
}

uint32_t space_exact_match_indices(Space *s, Atom *atom, uint32_t **out) {
    CettaIndex *wide = NULL;
    CettaIndex n64 = space_exact_match_indices64(s, atom, &wide);
    uint32_t n32 = 0;

    if (out)
        *out = NULL;
    if (!space_match_backend_u32_bound_checked(
            n64, SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE, &n32)) {
        free(wide);
        return 0;
    }
    if (n32 == 0) {
        free(wide);
        return 0;
    }
    uint32_t *narrow = cetta_malloc(sizeof(uint32_t) * n32);
    for (uint32_t i = 0; i < n32; i++) {
        if (!space_match_backend_u32_bound_checked(
                wide[i], SPACE_MATCH_BACKEND_ERROR_NATIVE_SPACE_TOO_LARGE,
                &narrow[i])) {
            free(narrow);
            free(wide);
            return 0;
        }
    }
    free(wide);
    if (out)
        *out = narrow;
    return n32;
}

bool space_contains_exact(Space *s, Atom *atom) {
    /* Doctrine-1 fast path: for a native space an exact-indexable
       atom's membership is an interned-id predicate over the dense presence
       bitset -- O(1), unclusterable -- so add-atom-nodup dedup no longer walks a
       fixed-bucket structural index for the FC-hot ground-theorem case.  Native
       overlays use the same logical-view projection.  Non-native backends and
       var/non-indexable atoms fall through to
       the exact-match path (id_present mirrors exact_idx, so this is equivalent
       to n>0, only cheaper). */
    if (s && atom && !space_engine_uses_pathmap(s->match_backend.kind) &&
        atom_is_exact_indexable(atom)) {
        /* Membership only needs the presence bitset, not the exact-index buckets,
           so bring id_present up to date lazily (append-only suffix sync; full
           resync only after a removal) -- never a full bucket rebuild inside a
           deferral window. */
        if (id_present_sync(s)) {
            AtomId qid = s->native.universe
                ? term_universe_lookup_atom_id(s->native.universe, atom)
                : CETTA_ATOM_ID_NONE;
            return qid != CETTA_ATOM_ID_NONE &&
                   id_present_contains(&s->native, qid);
        }
    }
    CettaIndex *matches = NULL;
    CettaIndex n = space_exact_match_indices64(s, atom, &matches);
    free(matches);
    return n > 0;
}

bool space_contains_canonical(Space *s, Atom *atom, bool *out_applicable) {
    /* Alpha-aware membership for a native space -- O(1) amortized via the
       canonical presence bitset (keyed on first-occurrence-ordinal canonical
       ids). Native overlays project their logical view through the same index.
       add-atom-nodup uses this so non-ground theorem dedup no longer scans
       every atom. *out_applicable is false for non-native backends, where the
       caller keeps the exact + alpha-scan path. */
    if (out_applicable)
        *out_applicable = false;
    if (!s || !atom || space_engine_uses_pathmap(s->match_backend.kind) ||
        !term_universe_atom_is_stable(atom))
        return false;
    /* Alpha-aware membership needs only the presence bitset.  Sync it lazily here
       (append-only suffix; full resync only after a removal) so the forward-
       chaining deferral window stays O(1) per contains instead of rebuilding the
       exact-index buckets each time (the O(N^2) source). */
    if (!id_present_sync(s))
        return false;
    if (out_applicable)
        *out_applicable = true;
    AtomId cid = space_canonical_id_for_query(s, atom);
    return cid != CETTA_ATOM_ID_NONE && id_present_contains(&s->native, cid);
}

bool space_contains_only_exact_atoms(Space *s) {
    if (!s)
        return false;
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = 0; i < logical_len; i++) {
            Atom *atom = space_get_at64(s, i);
            if (!atom || !atom_is_exact_indexable(atom))
                return false;
        }
        return true;
    }
    if (!ensure_has_non_exact_atoms(s))
        return false;
    return !s->native.has_non_exact_atoms;
}

bool space_atom_is_exact_indexable(Atom *atom) {
    return atom_is_exact_indexable(atom);
}

/* ── Type Expression Normalization ───────────────────────────────────────── */

/* Evaluate grounded arithmetic in type expressions using an explicit
   post-order worklist.  Structural depth is an input property, not an
   implicit semantic budget.
   E.g., (VecN String (+ (+ 0 1) 1)) → (VecN String 2) */
typedef struct {
    Atom *input;
    Atom **children;
    CettaExprIndex next_child;
    bool changed;
} TypeNormalizeFrame;

Atom *normalize_type_expr_head(Arena *a, Atom *norm) {
    if (!norm || norm->kind != ATOM_EXPR || norm->expr.len < 2) return norm;
    /* Dispatch only the type-pure capability: type conversion must never run
       an effectful or state-reading grounded op.  Anything outside the
       capability stays un-dispatched (an inert expression in the type). */
    SymbolId op_id = SYMBOL_ID_NONE;
    if (norm->expr.elems[0]->kind == ATOM_SYMBOL)
        op_id = norm->expr.elems[0]->sym_id;
    if (norm->expr.len >= 3 && op_id != SYMBOL_ID_NONE &&
        grounded_op_is_type_pure(op_id)) {
        Atom *result = grounded_dispatch(a, norm->expr.elems[0],
            norm->expr.elems + 1, norm->expr.len - 1);
        if (result) return result;
    }
    return norm;
}

Atom *normalize_type_expr(Arena *a, Atom *ty) {
    if (!ty || ty->kind != ATOM_EXPR || ty->expr.len < 2) return ty;

    size_t cap = 16;
    size_t len = 1;
    TypeNormalizeFrame *stack = cetta_malloc(sizeof(*stack) * cap);
    stack[0] = (TypeNormalizeFrame){.input = ty};

    for (;;) {
        TypeNormalizeFrame *frame = &stack[len - 1u];
        Atom *input = frame->input;
        if (!frame->children) {
            frame->children = arena_alloc(
                a, sizeof(Atom *) * (size_t)input->expr.len);
        }

        if (frame->next_child < input->expr.len) {
            Atom *child = input->expr.elems[frame->next_child];
            if (!child || child->kind != ATOM_EXPR || child->expr.len < 2) {
                frame->children[frame->next_child++] = child;
                continue;
            }
            if (len == cap) {
                if (cap > SIZE_MAX / 2u ||
                    cap * 2u > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    fputs("fatal: type-normalization worklist exceeds "
                          "addressable storage\n", stderr);
                    abort();
                }
                cap *= 2u;
                stack = cetta_realloc(stack, sizeof(*stack) * cap);
            }
            stack[len++] = (TypeNormalizeFrame){.input = child};
            continue;
        }

        Atom *norm = frame->changed
            ? atom_expr(a, frame->children, input->expr.len)
            : input;
        Atom *completed = normalize_type_expr_head(a, norm);
        len--;
        if (len == 0) {
            free(stack);
            return completed;
        }
        frame = &stack[len - 1u];
        CettaExprIndex child_index = frame->next_child++;
        frame->children[child_index] = completed;
        if (completed != frame->input->expr.elems[child_index])
            frame->changed = true;
    }
}

/* ── Type Lookup ─────────────────────────────────────────────────────────── */

static const char *native_handle_kind_name(Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3) return NULL;
    if (!atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.native_handle)) return NULL;
    Atom *kind = atom->expr.elems[1];
    if (kind->kind == ATOM_GROUNDED && kind->ground.gkind == GV_STRING) {
        return kind->ground.sval;
    }
    if (kind->kind == ATOM_SYMBOL) {
        return atom_name_cstr(kind);
    }
    return NULL;
}

static Atom *get_native_handle_type(Arena *a, Atom *atom) {
    const char *kind = native_handle_kind_name(atom);
    if (!kind) return atom_undefined_type(a);
    if (strcmp(kind, "mork-space") == 0) {
        return atom_symbol(a, "MorkSpace");
    }
    return atom_undefined_type(a);
}

Atom *get_grounded_type(Arena *a, Atom *atom) {
    if (atom->kind != ATOM_GROUNDED) return atom_undefined_type(a);
    switch (atom->ground.gkind) {
    case GV_INT:    return atom_symbol(a, "Number");
    case GV_BIGINT: return atom_symbol(a, "Number");
    case GV_RATIONAL: return atom_symbol(a, "Number");
    case GV_FLOAT:  return atom_symbol(a, "Number");
    case GV_BOOL:   return atom_symbol(a, "Bool");
    case GV_STRING: return atom_symbol(a, "String");
    case GV_SPACE: {
        const Space *space = (const Space *)atom->ground.ptr;
        const char *space_type = "atom";
        if (space) {
            if (space_is_stack(space)) {
                space_type = "stack";
            } else if (space_is_queue(space)) {
                space_type = "queue";
            } else if (space_is_hash(space)) {
                space_type = "hash";
            } else if (space->match_backend.kind == SPACE_ENGINE_PATHMAP) {
                space_type = "pathmap";
            } else if (space->match_backend.kind == SPACE_ENGINE_MORK) {
                space_type = "pathmap";
            }
        }
        return atom_expr2(a, atom_symbol(a, "Space"), atom_symbol(a, space_type));
    }
    case GV_FOREIGN:
        return atom_symbol(a, "Foreign");
    case GV_CAPTURE:
        return atom_expr3(a, atom_symbol(a, "->"),
                          atom_atom_type(a), atom_atom_type(a));
    case GV_STATE: {
        StateCell *cell = (StateCell *)atom->ground.ptr;
        if (cell->content_type &&
            !atom_is_symbol_id(cell->content_type, g_builtin_syms.undefined_type))
            return atom_expr2(a, atom_symbol(a, "StateMonad"), cell->content_type);
        return atom_symbol(a, "State");
    }
    case GV_PRIME_NEED_CAPABILITY:
        return atom_symbol(a, "PrimeNeedCapability");
    case GV_PRIME_CONTEXT:
        return atom_symbol(a, "context");
    case GV_INTERNAL_TAG:
        return atom_undefined_type(a);
    }
    return atom_undefined_type(a);
}

static bool type_inference_step(CettaTypeInferenceBudget *budget,
                                uint64_t amount) {
    if (!budget) return true;
    if (!budget->complete) return false;
    if (!budget->steps_limited) return true;
    if (budget->work_steps_observed > UINT64_MAX - amount)
        budget->work_steps_observed = UINT64_MAX;
    else
        budget->work_steps_observed += amount;
    return true;
}

static bool type_inference_can_add(CettaTypeInferenceBudget *budget,
                                   uint32_t count) {
    if (!budget || budget->type_capacity == 0 ||
        count < budget->type_capacity) return true;
    budget->complete = false;
    budget->type_capacity_exhausted = true;
    return false;
}

/* Scan space for (: atom type) annotations. */
static uint32_t get_annotated_types(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types,
                                    CettaTypeInferenceBudget *budget) {
    if (space_has_overlay_base(s)) {
        Atom **types = NULL;
        uint32_t count = 0, cap = 0;
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = 0; i < logical_len; i++) {
            if (!type_inference_step(budget, 1)) break;
            Atom *annotation = space_get_at64(s, i);
            if (!annotation || annotation->kind != ATOM_EXPR ||
                annotation->expr.len != 3)
                continue;
            if (!atom_is_symbol_id(annotation->expr.elems[0], g_builtin_syms.colon))
                continue;
            if (!atom_eq(annotation->expr.elems[1], atom))
                continue;
            if (!type_inference_can_add(budget, count)) break;
            if (count >= cap) {
                cap = cap ? cap * 2u : 4u;
                types = cetta_realloc(types, sizeof(Atom *) * cap);
            }
            types[count++] = atom_freshen_epoch(a, annotation->expr.elems[2],
                                                fresh_var_suffix());
        }
        *out_types = types;
        return count;
    }
    /* Use type annotation index for O(bucket_size) instead of O(N) */
    ensure_ty_ann_index(s);
    uint32_t h = atom_hash_for_index(atom);
    TypeAnnBucket *bucket = &s->native.ty_idx.buckets[h];
    Atom **types = NULL;
    uint32_t count = 0, cap = 0;
    for (CettaIndex i = 0; i < bucket->len; i++) {
        if (!type_inference_step(budget, 1)) break;
        CettaIndex idx = bucket->atom_indices[i];
        if (idx >= s->native.len)
            continue;
        AtomId annotation_id = space_get_atom_id_at64(s, idx);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, annotation_id,
                                                  &subject_id, &type_id)) {
            if (term_universe_atom_id_eq(s->native.universe, subject_id, atom)) {
                Atom *type_copy = term_universe_copy_atom_epoch(
                    s->native.universe, a, type_id, fresh_var_suffix());
                if (type_copy) {
                    if (!type_inference_can_add(budget, count)) break;
                    if (count >= cap) {
                        cap = cap ? cap * 2 : 4;
                        types = cetta_realloc(types, sizeof(Atom *) * cap);
                    }
                    types[count++] = type_copy;
                    continue;
                }
            }
        }
        Atom *annotation = space_get_at(s, idx);
        if (!annotation || annotation->kind != ATOM_EXPR || annotation->expr.len != 3)
            continue;
        if (!atom_is_symbol_id(annotation->expr.elems[0], g_builtin_syms.colon))
            continue;
        if (!atom_eq(annotation->expr.elems[1], atom))
            continue;
        if (!type_inference_can_add(budget, count)) break;
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            types = cetta_realloc(types, sizeof(Atom *) * cap);
        }
        types[count++] = atom_freshen_epoch(a, annotation->expr.elems[2],
                                            fresh_var_suffix());
    }
    *out_types = types;
    return count;
}

uint32_t space_get_declared_types(
    Space *s, Arena *a, Atom *subject, Atom ***out_types) {
    if (!out_types)
        return 0u;
    *out_types = NULL;
    if (!s || !a || !subject)
        return 0u;
    return get_annotated_types(s, a, subject, out_types, NULL);
}

static bool tuple_type_part_keep(Atom *type, bool is_head) {
    if (!type || atom_is_symbol_id(type, g_builtin_syms.undefined_type))
        return false;
    if (is_head && type->kind == ATOM_EXPR && type->expr.len >= 2 &&
        atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow)) {
        return false;
    }
    return true;
}

static uint32_t get_tuple_value_part_types(Space *s, Arena *a, Atom *atom,
                                           bool is_head, Atom ***out_types,
                                           CettaTypeInferenceBudget *budget);
static uint32_t get_atom_types_mode(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types,
                                    bool include_direct_annotations,
                                    CettaTypeInferenceBudget *budget);

static uint32_t get_tuple_value_part_types(Space *s, Arena *a, Atom *atom,
                                           bool is_head, Atom ***out_types,
                                           CettaTypeInferenceBudget *budget) {
    Atom **raw = NULL;
    uint32_t raw_count = 0;

    if (!type_inference_step(budget, 1)) {
        *out_types = NULL;
        return 0;
    }

    switch (atom->kind) {
    case ATOM_VAR:
        *out_types = NULL;
        return 0;
    case ATOM_GROUNDED: {
        Atom *ty = get_grounded_type(a, atom);
        if (tuple_type_part_keep(ty, is_head)) {
            raw = cetta_malloc(sizeof(Atom *));
            raw[0] = ty;
            *out_types = raw;
            return 1;
        }
        *out_types = NULL;
        return 0;
    }
    case ATOM_SYMBOL:
        raw_count = get_annotated_types(s, a, atom, &raw, budget);
        break;
    case ATOM_EXPR:
        raw_count = get_atom_types_mode(s, a, atom, &raw, true, budget);
        break;
    }

    Atom **types = NULL;
    uint32_t count = 0;
    for (uint32_t i = 0; i < raw_count; i++) {
        if (!type_inference_step(budget, 1)) break;
        if (!tuple_type_part_keep(raw[i], is_head))
            continue;
        if (!type_inference_can_add(budget, count)) break;
        types = cetta_realloc(types, sizeof(Atom *) * (count + 1));
        types[count++] = raw[i];
    }
    free(raw);
    *out_types = types;
    return count;
}

typedef struct {
    Atom **items;
    uint32_t len;
} TupleTypeChoices;

static void tuple_type_choices_free(TupleTypeChoices *choices, CettaExprLen len) {
    if (!choices)
        return;
    for (CettaExprIndex i = 0; i < len; i++)
        free(choices[i].items);
    free(choices);
}

static uint32_t infer_tuple_value_types(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types,
                                        CettaTypeInferenceBudget *budget) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        *out_types = NULL;
        return 0;
    }

    CettaExprLen len = atom->expr.len;
    TupleTypeChoices *choices = cetta_malloc(sizeof(TupleTypeChoices) * len);
    memset(choices, 0, sizeof(TupleTypeChoices) * len);
    uint64_t total = 1;

    for (CettaExprIndex i = 0; i < len; i++) {
        choices[i].len = get_tuple_value_part_types(s, a, atom->expr.elems[i],
                                                    i == 0, &choices[i].items,
                                                    budget);
        if (choices[i].len == 0 ||
            total > UINT32_MAX / choices[i].len ||
            total > SIZE_MAX / sizeof(Atom *) / choices[i].len) {
            if (budget && choices[i].len != 0) {
                budget->complete = false;
                budget->type_capacity_exhausted = true;
            }
            tuple_type_choices_free(choices, len);
            *out_types = NULL;
            return 0;
        }
        total *= choices[i].len;
    }

    uint32_t allocation_count = (uint32_t)total;
    if (budget && budget->type_capacity != 0 &&
        allocation_count > budget->type_capacity)
        allocation_count = budget->type_capacity;
    Atom **types = cetta_malloc(sizeof(Atom *) * allocation_count);
    uint32_t count = 0;
    for (uint64_t n = 0; n < total; n++) {
        if (!type_inference_step(budget, 1) ||
            !type_inference_can_add(budget, count)) {
            break;
        }
        uint64_t rem = n;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
        for (CettaExprIndex pos = len; pos > 0; pos--) {
            uint32_t idx = (uint32_t)(rem % choices[pos - 1].len);
            rem /= choices[pos - 1].len;
            elems[pos - 1] = choices[pos - 1].items[idx];
        }
        types[count++] = atom_expr(a, elems, len);
    }

    tuple_type_choices_free(choices, len);
    *out_types = types;
    return count;
}

static uint32_t get_atom_types_mode(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types,
                                    bool include_direct_annotations,
                                    CettaTypeInferenceBudget *budget) {
    uint32_t count = 0;
    Atom **types = NULL;
    if (!type_inference_step(budget, 1)) {
        *out_types = NULL;
        return 0;
    }
    Atom *native_handle_type = get_native_handle_type(a, atom);

    if (!atom_is_symbol_id(native_handle_type, g_builtin_syms.undefined_type)) {
        types = cetta_malloc(sizeof(Atom *));
        types[0] = native_handle_type;
        *out_types = types;
        return 1;
    }

    switch (atom->kind) {
    case ATOM_VAR:
        /* Variables have no type → %Undefined% */
        break;
    case ATOM_GROUNDED: {
        Atom *ty = get_grounded_type(a, atom);
        if (!atom_is_symbol_id(ty, g_builtin_syms.undefined_type)) {
            types = cetta_malloc(sizeof(Atom *));
            types[0] = ty;
            count = 1;
        }
        break;
    }
    case ATOM_SYMBOL:
        count = include_direct_annotations
                    ? get_annotated_types(s, a, atom, &types, budget)
                    : 0;
        break;
    case ATOM_EXPR:
        count = include_direct_annotations
                    ? get_annotated_types(s, a, atom, &types, budget)
                    : 0;
        /* Also try to infer type from operator's function type */
        bool tried_func_type = false;
        if (count == 0 && atom->expr.len >= 2) {
            Atom *op = atom->expr.elems[0];
            Atom **op_types = NULL;
            uint32_t nop = get_annotated_types(s, a, op, &op_types, budget);
            Arena scratch;
            arena_init(&scratch);
            arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            arena_set_hashcons(&scratch, NULL);
            /* Also try recursively inferred types for the operator */
            if (nop == 0 && op->kind == ATOM_EXPR) {
                Atom **recur_types = NULL;
                nop = get_atom_types_mode(s, a, op, &recur_types, true,
                                          budget);
                /* Filter: only keep function types */
                op_types = NULL;
                uint32_t nfunc = 0;
                for (uint32_t ri = 0; ri < nop; ri++) {
                    if (!type_inference_step(budget, 1)) break;
                    if (recur_types[ri]->kind == ATOM_EXPR && recur_types[ri]->expr.len >= 2 &&
                        atom_is_symbol_id(recur_types[ri]->expr.elems[0], g_builtin_syms.arrow)) {
                        if (!type_inference_can_add(budget, nfunc)) break;
                        op_types = cetta_realloc(op_types, sizeof(Atom *) * (nfunc + 1));
                        op_types[nfunc++] = recur_types[ri];
                    }
                }
                free(recur_types);
                nop = nfunc;
            }
            for (uint32_t oi = 0; oi < nop; oi++) {
                if (!type_inference_step(budget, 1)) break;
                Atom *ft = op_types[oi];
                /* Check if it's a function type (-> ...) */
                if (ft->kind == ATOM_EXPR && ft->expr.len >= 2 &&
                    atom_is_symbol_id(ft->expr.elems[0], g_builtin_syms.arrow)) {
                    tried_func_type = true;
                    /* Check arity match */
                    if (ft->expr.len - 2 != atom->expr.len - 1) continue;
                    /* Freshen type vars and try to unify args to get concrete ret type */
                    ArenaMark scratch_mark = arena_mark(&scratch);
                    uint32_t tsuf = fresh_var_suffix();
                    Atom *fresh_ft = atom_freshen_epoch(&scratch, ft, tsuf);
                    Atom *fresh_ret = fresh_ft->expr.elems[fresh_ft->expr.len - 1];
                    Bindings tb;
                    bindings_init(&tb);
                    bool all_ok = true;
                    for (CettaExprIndex ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
                        if (!type_inference_step(budget, 1)) {
                            all_ok = false;
                            break;
                        }
                        /* Apply accumulated bindings to resolve type vars from earlier args */
                        Atom *arg_type_decl =
                            bindings_apply_if_vars(&tb, &scratch, fresh_ft->expr.elems[ai + 1]);
                        if (atom_is_meta_type(arg_type_decl)) {
                            all_ok = atom_meta_type_accepts(a, arg_type_decl,
                                                            atom->expr.elems[ai + 1]);
                            continue;
                        }
                        Atom **atypes = NULL;
                        uint32_t nat = get_atom_types_mode(
                            s, a, atom->expr.elems[ai + 1], &atypes, true,
                            budget);
                        bool found = false;
                        SearchContext trial_context;
                        if (!search_context_init(&trial_context, &tb, &scratch)) {
                            free(atypes);
                            bindings_free(&tb);
                            free(types);
                            free(op_types);
                            arena_free(&scratch);
                            *out_types = NULL;
                            return 0;
                        }
                        for (uint32_t ti = 0; ti < nat; ti++) {
                            if (!type_inference_step(budget, 1)) break;
                            ChoicePoint point = search_context_save(&trial_context);
                            if (match_types_builder(atypes[ti], arg_type_decl,
                                                    search_context_builder(&trial_context))) {
                                Bindings next_tb;
                                bindings_init(&next_tb);
                                search_context_take(&trial_context, &next_tb);
                                bindings_replace(&tb, &next_tb);
                                found = true;
                                break;
                            }
                            search_context_rollback(&trial_context, point);
                        }
                        search_context_free(&trial_context);
                        free(atypes);
                        if (!found) all_ok = false;
                    }
                    if (all_ok) {
                        /* Apply accumulated type bindings to return type,
                           then normalize arithmetic in type expressions */
                        Atom *concrete_ret = normalize_type_expr(
                            &scratch, bindings_apply_if_vars(&tb, &scratch, fresh_ret));
                        if (!type_inference_can_add(budget, count)) {
                            bindings_free(&tb);
                            arena_reset(&scratch, scratch_mark);
                            break;
                        }
                        if (count >= 1) {
                            types = cetta_realloc(types, sizeof(Atom *) * (count + 1));
                        } else {
                            types = cetta_malloc(sizeof(Atom *));
                        }
                        types[count++] = atom_deep_copy(a, concrete_ret);
                    }
                    bindings_free(&tb);
                    arena_reset(&scratch, scratch_mark);
                }
            }
            arena_free(&scratch);
            free(op_types);
            /* If we tried function types but none matched → type error (empty) */
            if (tried_func_type && count == 0 &&
                (!budget || budget->complete)) {
                *out_types = NULL;
                return 0;  /* empty = ill-typed */
            }
        }
        if (count == 0 && atom->expr.len > 0 && !tried_func_type &&
            (!budget || budget->complete))
            count = infer_tuple_value_types(s, a, atom, &types, budget);
        break;
    }

    /* If no types found, return [%Undefined%] */
    if (count == 0 && (!budget || budget->complete)) {
        if (!type_inference_can_add(budget, count)) {
            *out_types = NULL;
            return 0;
        }
        types = cetta_malloc(sizeof(Atom *));
        types[0] = atom_undefined_type(a);
        count = 1;
    }
    *out_types = types;
    return count;
}

uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types) {
    return get_atom_types_mode(s, a, atom, out_types, true, NULL);
}

uint32_t get_atom_types_budgeted(Space *s, Arena *a, Atom *atom,
                                 Atom ***out_types,
                                 CettaTypeInferenceBudget *budget) {
    return get_atom_types_mode(s, a, atom, out_types, true, budget);
}

uint32_t get_atom_types_structural(Space *s, Arena *a, Atom *atom,
                                   Atom ***out_types) {
    /* Structural checking ignores only an expression subject's own top-level
       annotation. Head and argument inference still uses the shared engine. */
    return get_atom_types_mode(s, a, atom, out_types,
                               !(atom && atom->kind == ATOM_EXPR), NULL);
}

uint32_t get_atom_types_structural_budgeted(
    Space *s, Arena *a, Atom *atom, Atom ***out_types,
    CettaTypeInferenceBudget *budget) {
    return get_atom_types_mode(s, a, atom, out_types,
                               !(atom && atom->kind == ATOM_EXPR), budget);
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

typedef struct QueryResultSink {
    QueryResults *results;
    QueryResultVisitor visitor;
    void *visitor_ctx;
    CettaCount emitted;
    bool stop;
} QueryResultSink;

static bool query_equation_emit_stored(Space *s, AtomId lhs_id, AtomId rhs_id,
                                       Atom *query,
                                       const QueryVisibleVarSet *visible,
                                       Arena *a, uint32_t epoch,
                                       const Bindings *seed,
                                       QueryResultSink *sink);
static bool query_equation_emit_decoded_epoch(Atom *lhs, Atom *rhs,
                                              Atom *query,
                                              const QueryVisibleVarSet *visible,
                                              Arena *a, uint32_t epoch,
                                              const Bindings *seed,
                                              QueryResultSink *sink);

static void query_result_sink_init_collect(QueryResultSink *sink,
                                           QueryResults *results) {
    sink->results = results;
    sink->visitor = NULL;
    sink->visitor_ctx = NULL;
    sink->emitted = 0;
    sink->stop = false;
}

static void query_result_sink_init_visit(QueryResultSink *sink,
                                         QueryResultVisitor visitor,
                                         void *ctx) {
    sink->results = NULL;
    sink->visitor = visitor;
    sink->visitor_ctx = ctx;
    sink->emitted = 0;
    sink->stop = false;
}

static bool query_result_sink_emit(QueryResultSink *sink, Atom *result,
                                   Bindings *bindings) {
    if (!sink || sink->stop)
        return false;
    if (sink->results) {
        if (!query_results_push_move(sink->results, result, bindings))
            return false;
        sink->emitted++;
        return true;
    }
    if (!sink->visitor)
        return false;
    sink->emitted++;
    if (!sink->visitor(result, bindings, sink->visitor_ctx)) {
        sink->stop = true;
        return true;
    }
    return true;
}

static void query_bucket_legacy(Space *s, EqBucket *bucket, Atom *query,
                                const QueryVisibleVarSet *visible, Arena *a,
                                QueryResultSink *sink) {
    SymbolId query_head = eq_head_symbol(query);
    if (!sink || sink->stop)
        return;
    if (bucket->trie && bucket->len > 4) {
        CettaIndex *candidates = NULL;
        CettaIndex ncand = 0, ccand = 0;
        CettaIndex considered = 0;
        disc_lookup(bucket->trie, query, &candidates, &ncand, &ccand);
        for (CettaIndex ci = 0; ci < ncand; ci++) {
            CettaIndex i = candidates[ci];
            if (i >= bucket->len) continue;
            CettaIndex atom_idx = bucket->atom_indices[i];
            if (atom_idx >= s->native.len)
                continue;
            AtomId equation_id = space_get_atom_id_at64(s, atom_idx);
            AtomId lhs_id = CETTA_ATOM_ID_NONE;
            AtomId rhs_id = CETTA_ATOM_ID_NONE;
            if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
                SymbolId lhs_head = eq_head_symbol_id(s, lhs_id);
                if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                    lhs_head != query_head) {
                    continue;
                }
                considered++;
                (void)query_equation_emit_stored(
                    s, lhs_id, rhs_id, query, visible, a, fresh_var_suffix(),
                    NULL, sink);
                if (sink->stop)
                    break;
                continue;
            }
            Atom *lhs = NULL, *rhs = NULL;
            if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
                continue;
            SymbolId lhs_head = eq_head_symbol(lhs);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            (void)query_equation_emit_decoded_epoch(
                lhs, rhs, query, visible, a, fresh_var_suffix(), NULL, sink);
            if (sink->stop)
                break;
        }
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                                considered);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
        free(candidates);
        return;
    }

    uint32_t considered = 0;
    for (CettaIndex i = 0; i < bucket->len; i++) {
        CettaIndex atom_idx = bucket->atom_indices[i];
        if (atom_idx >= s->native.len)
            continue;
        AtomId equation_id = space_get_atom_id_at64(s, atom_idx);
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
            SymbolId lhs_head = eq_head_symbol_id(s, lhs_id);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            (void)query_equation_emit_stored(
                s, lhs_id, rhs_id, query, visible, a, fresh_var_suffix(),
                NULL, sink);
            if (sink->stop)
                break;
            continue;
        }
        Atom *lhs = NULL, *rhs = NULL;
        if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
            continue;
        SymbolId lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        (void)query_equation_emit_decoded_epoch(
            lhs, rhs, query, visible, a, fresh_var_suffix(), NULL, sink);
        if (sink->stop)
            break;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
}

static bool query_equation_emit_stored(Space *s, AtomId lhs_id, AtomId rhs_id,
                                       Atom *query,
                                       const QueryVisibleVarSet *visible,
                                       Arena *a, uint32_t epoch,
                                       const Bindings *seed,
                                       QueryResultSink *sink) {
    if (!s || !s->native.universe || lhs_id == CETTA_ATOM_ID_NONE ||
        rhs_id == CETTA_ATOM_ID_NONE || !tu_hdr(s->native.universe, lhs_id) ||
        !tu_hdr(s->native.universe, rhs_id) || !sink) {
        return false;
    }
    Bindings merged;
    bindings_init(&merged);
    if (seed && !bindings_try_merge_live(&merged, seed)) {
        bindings_free(&merged);
        return false;
    }
    bool emitted = false;
    if (match_atoms_atom_id_epoch(query, s->native.universe, lhs_id,
                                  &merged, a, epoch) &&
        !bindings_has_loop(&merged)) {
        Atom *rhs_copy = tu_has_vars(s->native.universe, rhs_id)
            ? term_universe_copy_atom_epoch(s->native.universe, a, rhs_id, epoch)
            : term_universe_copy_atom(s->native.universe, a, rhs_id);
        Atom *result =
            rhs_copy ? bindings_apply_if_vars(&merged, a, rhs_copy) : NULL;
        if (result) {
            result = rewrite_query_visible_aliases(a, result, visible, &merged);
            Bindings projected;
            if (project_query_visible_bindings(a, visible, &merged, &projected)) {
                emitted = query_result_sink_emit(sink, result, &projected);
                bindings_free(&projected);
            }
        }
    }
    bindings_free(&merged);
    return emitted;
}

static bool query_equation_emit_decoded_epoch(Atom *lhs, Atom *rhs,
                                              Atom *query,
                                              const QueryVisibleVarSet *visible,
                                              Arena *a, uint32_t epoch,
                                              const Bindings *seed,
                                              QueryResultSink *sink) {
    if (!lhs || !rhs || !query || !a || !sink)
        return false;
    Bindings merged;
    bindings_init(&merged);
    if (seed && !bindings_try_merge_live(&merged, seed)) {
        bindings_free(&merged);
        return false;
    }
    bool emitted = false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_EQ_DECODED);
    /* Leaf-patch view (OFF by default): for a flat linear pattern with
     * non-variable query args, the positional bind reproduces the matcher's
     * bindings without the worklist; it pre-checks and refuses anything else,
     * falling back to the general matcher on clean state.  OFF-vs-ON is proven
     * byte-identical before any default flip. */
    bool leaf_matched = match_leaf_patch_view_enabled() &&
                        match_atoms_epoch_positional_linear(query, lhs, &merged,
                                                            a, epoch);
    if ((leaf_matched || match_atoms_epoch(query, lhs, &merged, a, epoch)) &&
        !bindings_has_loop(&merged)) {
        Atom *result = bindings_apply_epoch(&merged, a, rhs, epoch);
        result = rewrite_query_visible_aliases(a, result, visible, &merged);
        Bindings projected;
        if (project_query_visible_bindings(a, visible, &merged, &projected)) {
            emitted = query_result_sink_emit(sink, result, &projected);
            bindings_free(&projected);
        }
    }
    bindings_free(&merged);
    return emitted;
}

/* Try matching equations from a bucket against a query.
   Large buckets reuse the substitution-tree epoch path to avoid per-candidate
   rename_vars on both sides of each equation. */
static void query_bucket(Space *s, EqBucket *bucket, Atom *query,
                         const QueryVisibleVarSet *visible, Arena *a,
                         QueryResultSink *sink) {
    SymbolId query_head = eq_head_symbol(query);
    CettaCount emitted_before;
    bool head_bucket_mismatch;
    if (!bucket || bucket->len == 0 || !sink || sink->stop)
        return;
    emitted_before = sink->emitted;
    head_bucket_mismatch =
        query_head != SYMBOL_ID_NONE &&
        (bucket->mixed_heads || bucket->head != query_head);
    if (head_bucket_mismatch || bucket->subst.count <= 4 || !bucket->subst.root ||
        !bucket->subst_safe || !atom_is_eq_subst_safe(query)) {
        query_bucket_legacy(s, bucket, query, visible, a, sink);
        return;
    }
    SubstMatchSet matches;
    uint32_t considered = 0;
    smset_init(&matches);
    stree_query_bucket(&bucket->subst, a, query, NULL, &matches);
    for (CettaIndex mi = 0; mi < matches.len; mi++) {
        const SubstMatch *sm = &matches.items[mi];
        if (sm->atom_idx >= bucket->len)
            continue;
        CettaIndex atom_idx = bucket->atom_indices[sm->atom_idx];
        if (atom_idx >= s->native.len)
            continue;
        AtomId equation_id = space_get_atom_id_at64(s, atom_idx);
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
            SymbolId lhs_head = eq_head_symbol_id(s, lhs_id);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            if (query_equation_emit_stored(s, lhs_id, rhs_id, query, visible, a,
                                           sm->epoch, &sm->bindings, sink)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_EMITTED);
            }
            if (sink->stop)
                break;
            continue;
        }
        Atom *lhs = NULL, *rhs = NULL;
        if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
            continue;
        SymbolId lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        bool emitted = query_equation_emit_decoded_epoch(
            lhs, rhs, query, visible, a, sm->epoch, &sm->bindings, sink);
        if (emitted) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_EMITTED);
        }
        if (!emitted) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_CANDIDATE_FALLBACK);
            (void)query_equation_emit_decoded_epoch(
                lhs, rhs, query, visible, a, fresh_var_suffix(), NULL, sink);
        }
        if (sink->stop)
            break;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_CANDIDATES, considered);
    smset_free(&matches);
    if (!sink->stop && sink->emitted == emitted_before) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_BUCKET_FALLBACK);
        query_bucket_legacy(s, bucket, query, visible, a, sink);
    }
}

static CettaCount query_equations_core_overlay(Space *s, Atom *query, Arena *a,
                                               QueryResultSink *sink) {
    QueryVisibleVarSet visible;
    SymbolId query_head = eq_head_symbol(query);
    CettaCount logical_len;
    CettaCount considered = 0;

    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return 0;
    }

    logical_len = space_length64(s);
    for (CettaIndex i = 0; i < logical_len && !sink->stop; i++) {
        Atom *equation = space_get_at64(s, i);
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        SymbolId lhs_head;
        if (!equation || !is_equation_atom(equation, &lhs, &rhs))
            continue;
        lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        (void)query_equation_emit_decoded_epoch(
            lhs, rhs, query, &visible, a, fresh_var_suffix(), NULL, sink);
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
    query_visible_var_set_free(&visible);
    return sink->emitted;
}

static CettaCount query_equations_core(Space *s, Atom *query, Arena *a,
                                       QueryResultSink *sink) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS);
    if (space_has_overlay_base(s))
        return query_equations_core_overlay(s, query, a, sink);
    ensure_eq_index(s);
    QueryVisibleVarSet visible;
    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return 0;
    }
    /* Use head-symbol index for O(1) lookup instead of O(N) scan.
       This is the key optimization from Vampire's LiteralIndex. */
    SymbolId head = eq_head_symbol(query);
    if (head != SYMBOL_ID_NONE) {
        /* Query has a known head symbol — look up matching bucket */
        query_bucket(s, &s->native.eq_idx.buckets[symbol_hash(head)], query, &visible, a, sink);
    }
    /* Non-symbol-headed queries may still match wildcard equations whose LHS
       head is itself a variable or complex term, but they must not unlock
       every named equation bucket by unifying the head variable with an
       unrelated function symbol. HE treats ($f x) as data unless a wildcard
       equation explicitly matches it. */
    if (!sink->stop)
        query_bucket(s, &s->native.eq_idx.wildcard, query, &visible, a, sink);
    query_visible_var_set_free(&visible);
    return sink->emitted;
}

CettaCount query_equations_visit(Space *s, Atom *query, Arena *a,
                                 QueryResultVisitor visitor, void *ctx) {
    QueryResultSink sink;
    query_result_sink_init_visit(&sink, visitor, ctx);
    return query_equations_core(s, query, a, &sink);
}

/* MAM loop-body view entry guard -- the necessary (not yet sufficient)
 * condition for the deterministic-tail loop lane: the head resolves to EXACTLY
 * ONE equation in a clean single-head bucket, and the space has no overlay base
 * to complicate revision/visibility.  Cheap: one hashed bucket read, no match.
 * Conservative by construction -- any ambiguity (mixed bucket, overlay, zero or
 * many equations) returns false and the general evaluator handles the call.
 * Body-determinism (single-branch, no superpose tail) is verified later when
 * the view is compiled; this predicate only gates measurement + view lookup. */
/* Every ATOM_VAR in the pattern occurs at most once.  A repeated variable is
 * an equality constraint (both occurrences must bind equal terms) that a naive
 * positional leaf-patch cannot honour -- it reads the two leaves independently
 * and would accept terms the real matcher rejects.  Patterns are tiny, so the
 * O(vars^2) dedup is negligible; if a pattern somehow exceeds the small cap we
 * conservatively report non-linear (refuse), never a false linear. */
static bool pattern_vars_unique_rec(const Atom *a, VarId *seen, uint32_t *n,
                                    uint32_t cap) {
    if (!a)
        return true;
    if (a->kind == ATOM_VAR) {
        for (uint32_t i = 0; i < *n; i++)
            if (seen[i] == a->var_id)
                return false; /* repeated variable -> non-linear */
        if (*n >= cap)
            return false; /* too many vars to verify -> conservative refuse */
        seen[(*n)++] = a->var_id;
        return true;
    }
    if (a->kind == ATOM_EXPR) {
        for (CettaExprIndex i = 0; i < a->expr.len; i++)
            if (!pattern_vars_unique_rec(a->expr.elems[i], seen, n, cap))
                return false;
    }
    return true;
}

/* Resolve the single, linear equation a head is eligible for (or NULL).  Shared
 * by the eligibility predicate and the revision-keyed view cache.  Cheap: one
 * hashed bucket read + a tiny linearity walk; the cache calls it once per
 * (head, revision), not per reduction. */
static Atom *space_single_linear_equation_at(Space *s, SymbolId head,
                                             CettaIndex *logical_index) {
    if (logical_index)
        *logical_index = 0u;
    if (!s || head == SYMBOL_ID_NONE || space_has_overlay_base(s))
        return NULL;
    ensure_eq_index(s);
    if (s->native.eq_idx_dirty)
        return NULL;
    /* The general equation query also visits the wildcard-head bucket after
     * the known-head bucket.  A named singleton is therefore not a singleton
     * reduction while any wildcard equation is visible: selecting only the
     * named equation would drop a valid branch. */
    if (s->native.eq_idx.wildcard.len != 0u)
        return NULL;
    const EqBucket *b = &s->native.eq_idx.buckets[symbol_hash(head)];
    if (!(b->head == head && !b->mixed_heads && b->len == 1))
        return NULL;
    CettaIndex idx = b->atom_indices[0];
    if (idx >= s->native.len)
        return NULL;
    Atom *equation = space_get_at64(s, idx);
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs))
        return NULL;
    /* Linearity is required: a repeated LHS variable is an
     * equality constraint the positional leaf-patch cannot honour. */
    VarId seen[64];
    uint32_t nseen = 0;
    if (!pattern_vars_unique_rec(lhs, seen, &nseen, 64u))
        return NULL;
    if (logical_index)
        *logical_index = idx;
    return equation;
}

Atom *space_single_linear_equation(Space *s, SymbolId head) {
    return space_single_linear_equation_at(s, head, NULL);
}

bool space_head_has_single_equation(Space *s, SymbolId head) {
    return space_single_linear_equation(s, head) != NULL;
}

static bool prepared_rhs_is_range_restricted(
    const Atom *atom, const SpacePreparedEquation *plan) {
    if (!atom || !plan)
        return false;
    if (!atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR) {
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            if (plan->registers[i] == atom->var_id)
                return true;
        return false;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
        if (!prepared_rhs_is_range_restricted(atom->expr.elems[i], plan))
            return false;
    return true;
}

static bool prepared_register_head_program(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out,
    CettaGsltRegisterInstruction *instruction_out) {
#define PREPARED_REGISTER_HEAD(field, expected_arity, result_kind, instruction) \
    if (head == g_builtin_syms.field && arity == (expected_arity)) { \
        if (kind_out) \
            *kind_out = (result_kind); \
        if (instruction_out) \
            *instruction_out = (instruction); \
        return true; \
    }
    CETTA_GSLT_REGISTER_HEAD_ROWS(PREPARED_REGISTER_HEAD)
#undef PREPARED_REGISTER_HEAD
    return false;
}

static bool prepared_register_head_kind(
    SymbolId head, CettaExprLen arity,
    CettaGsltRegisterResultKind *kind_out) {
    return prepared_register_head_program(
        head, arity, kind_out, NULL);
}

static bool prepared_register_is_register(
    const SpacePreparedEquation *plan, const Atom *atom) {
    if (!plan || !atom || atom->kind != ATOM_VAR)
        return false;
    for (CettaExprIndex i = 0u; i < plan->arity; i++)
        if (plan->registers[i] == atom->var_id)
            return true;
    return false;
}

static bool prepared_register_template_kind(
    const SpacePreparedEquation *plan, const Atom *atom, uint32_t depth,
    CettaGsltRegisterResultKind *kind_out) {
    if (!plan || !atom || !kind_out || depth > 64u)
        return false;
    if (prepared_register_is_register(plan, atom) ||
        (atom->kind == ATOM_GROUNDED &&
         (atom->ground.gkind == GV_INT ||
          atom->ground.gkind == GV_BIGINT))) {
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u)
        return false;
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_register_head_kind(
            head->sym_id, atom->expr.len - 1u, kind_out)) {
        return false;
    }
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        CettaGsltRegisterResultKind child_kind;
        if (!prepared_register_template_kind(
                plan, atom->expr.elems[i], depth + 1u, &child_kind) ||
            child_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
    }
    return true;
}

static bool prepared_register_exact_integer(const Atom *atom);

/* The recursive register fragment is an exact, pure expression grammar:
 * input registers and exact integer literals; generated register heads;
 * lazy `if`; and calls back to the same revision-pinned singleton equation.
 * The native evaluator below consumes precisely this proof bit and no wider
 * syntax. */
static bool prepared_register_recursive_template_kind(
    const SpacePreparedEquation *plan, const Atom *atom, uint32_t depth,
    CettaGsltRegisterResultKind *kind_out, bool *saw_self_call) {
    if (!plan || !atom || !kind_out || !saw_self_call || depth > 64u)
        return false;
    if (prepared_register_is_register(plan, atom) ||
        prepared_register_exact_integer(atom)) {
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    }
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
        !atom->expr.elems[0] ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    SymbolId head = atom->expr.elems[0]->sym_id;
    CettaExprLen nargs = atom->expr.len - 1u;
    if (head == g_builtin_syms.if_text) {
        if (nargs != 3u)
            return false;
        CettaGsltRegisterResultKind condition_kind;
        CettaGsltRegisterResultKind true_kind;
        CettaGsltRegisterResultKind false_kind;
        if (!prepared_register_recursive_template_kind(
                plan, atom->expr.elems[1], depth + 1u,
                &condition_kind, saw_self_call) ||
            condition_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
            !prepared_register_recursive_template_kind(
                plan, atom->expr.elems[2], depth + 1u,
                &true_kind, saw_self_call) ||
            !prepared_register_recursive_template_kind(
                plan, atom->expr.elems[3], depth + 1u,
                &false_kind, saw_self_call) ||
            true_kind != false_kind ||
            true_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
        *kind_out = true_kind;
        return true;
    }
    if (head == plan->head) {
        if (nargs != plan->arity)
            return false;
        for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
            CettaGsltRegisterResultKind argument_kind;
            if (!prepared_register_recursive_template_kind(
                    plan, atom->expr.elems[i], depth + 1u,
                    &argument_kind, saw_self_call) ||
                argument_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
        }
        *saw_self_call = true;
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return true;
    }
    CettaGsltRegisterResultKind result_kind;
    if (!prepared_register_head_kind(head, nargs, &result_kind))
        return false;
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        CettaGsltRegisterResultKind argument_kind;
        if (!prepared_register_recursive_template_kind(
                plan, atom->expr.elems[i], depth + 1u,
                &argument_kind, saw_self_call) ||
            argument_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
    }
    *kind_out = result_kind;
    return true;
}

static bool prepared_register_tail_shape(
    const SpacePreparedEquation *plan, Atom *atom) {
    if (!plan || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(atom->expr.elems[0], plan->head)) {
        return false;
    }
    for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
        CettaGsltRegisterResultKind kind;
        if (!prepared_register_template_kind(
                plan, atom->expr.elems[i], 0u, &kind) ||
            kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return false;
        }
    }
    return true;
}

static bool prepared_register_compile_guarded_step(
    SpacePreparedEquation *plan) {
    if (!plan || !plan->rhs || plan->rhs->kind != ATOM_EXPR ||
        plan->rhs->expr.len != 4u ||
        !atom_is_symbol_id(plan->rhs->expr.elems[0],
                           g_builtin_syms.if_text)) {
        return false;
    }
    Atom *guard = plan->rhs->expr.elems[1];
    Atom *when_true = plan->rhs->expr.elems[2];
    Atom *when_false = plan->rhs->expr.elems[3];
    CettaGsltRegisterResultKind guard_kind;
    if (!prepared_register_template_kind(
            plan, guard, 0u, &guard_kind) ||
        guard_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN) {
        return false;
    }
    bool true_is_tail = prepared_register_tail_shape(plan, when_true);
    bool false_is_tail = prepared_register_tail_shape(plan, when_false);
    if (true_is_tail == false_is_tail)
        return false;
    Atom *base = true_is_tail ? when_false : when_true;
    CettaGsltRegisterResultKind base_kind;
    if (!prepared_register_template_kind(
            plan, base, 0u, &base_kind) ||
        base_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        return false;
    }
    plan->register_guard = guard;
    plan->register_base = base;
    plan->register_tail = true_is_tail ? when_true : when_false;
    plan->register_base_when_true = !true_is_tail;
    plan->evidence |= CETTA_GSLT_EVIDENCE_REGISTER_GUARD |
                      CETTA_GSLT_EVIDENCE_REGISTER_BASE |
                      CETTA_GSLT_EVIDENCE_REGISTER_TAIL;
    return true;
}

/* A declared arrow signature at the call arity hands the head to the
 * typed-demand discipline; the prepared ground-call lane may not evaluate
 * arguments whose declared parameter types keep them syntax. */
bool space_head_has_arrow_signature(Space *s, SymbolId head,
                                    CettaExprLen arity) {
    CettaCount logical_len = space_length64(s);
    for (CettaIndex i = 0; i < logical_len; i++) {
        Atom *annotation = space_get_at64(s, i);
        if (!annotation || annotation->kind != ATOM_EXPR ||
            annotation->expr.len != 3)
            continue;
        if (!atom_is_symbol_id(annotation->expr.elems[0],
                               g_builtin_syms.colon))
            continue;
        Atom *subject = annotation->expr.elems[1];
        if (!subject || !atom_is_symbol_id(subject, head))
            continue;
        Atom *type = annotation->expr.elems[2];
        if (type && type->kind == ATOM_EXPR &&
            type->expr.len == (CettaExprLen)(arity + 2u) &&
            atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
            return true;
    }
    return false;
}

bool space_prepare_single_equation(Space *s, SymbolId head,
                                   SpacePreparedEquation *out) {
    SpacePreparedEquation plan;
    CettaIndex logical_index = 0u;
    Atom *equation;
    Atom *lhs = NULL;
    Atom *rhs = NULL;

    if (out)
        memset(out, 0, sizeof(*out));
    if (!s || !out || head == SYMBOL_ID_NONE)
        return false;
    equation = space_single_linear_equation_at(s, head, &logical_index);
    if (!equation || !is_equation_atom(equation, &lhs, &rhs) ||
        !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head)) {
        return false;
    }
    CettaExprLen arity = lhs->expr.len - 1u;
    if (arity > SPACE_PREPARED_EQUATION_MAX_REGISTERS)
        return false;

    memset(&plan, 0, sizeof(plan));
    plan.occurrence.read = space_read_token(s);
    plan.occurrence.logical_index = logical_index;
    plan.equation = equation;
    plan.lhs = lhs;
    plan.rhs = rhs;
    plan.head = head;
    plan.arity = arity;
    plan.evidence = CETTA_GSLT_EVIDENCE_SINGLETON_HEAD;
    for (CettaExprIndex i = 0u; i < arity; i++) {
        Atom *argument = lhs->expr.elems[i + 1u];
        if (!argument || argument->kind != ATOM_VAR)
            return false;
        for (CettaExprIndex j = 0u; j < i; j++)
            if (plan.registers[j] == argument->var_id)
                return false;
        plan.registers[i] = argument->var_id;
    }
    plan.evidence |= CETTA_GSLT_EVIDENCE_FLAT_LINEAR_LHS;
    if (!prepared_rhs_is_range_restricted(rhs, &plan))
        return false;
    plan.evidence |= CETTA_GSLT_EVIDENCE_RANGE_RESTRICTED_RHS;
    if (space_read_token_is_current(plan.occurrence.read))
        plan.evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    if (CETTA_GSLT_ACCELERATOR_CALL_POLICY_SUPPORTED(s, head, arity))
        plan.evidence |= CETTA_GSLT_EVIDENCE_CALL_POLICY_SUPPORTED;
    if (!cetta_gslt_prepared_equation_plan_admitted(plan.evidence))
        return false;
    (void)prepared_register_compile_guarded_step(&plan);
    CettaGsltRegisterResultKind recursive_kind;
    bool saw_self_call = false;
    if (prepared_register_recursive_template_kind(
            &plan, plan.rhs, 0u, &recursive_kind, &saw_self_call) &&
        saw_self_call &&
        recursive_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        plan.evidence |= CETTA_GSLT_EVIDENCE_REGISTER_RECURSIVE;
    }
    *out = plan;
    return true;
}

static Atom *prepared_equation_instantiate_rec(
    const SpacePreparedEquation *plan, Atom *source,
    Atom *const *register_values, Arena *arena) {
    if (!source || !plan || !arena)
        return NULL;
    if (!atom_has_vars(source))
        return source;
    if (source->kind == ATOM_VAR) {
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            if (plan->registers[i] == source->var_id)
                return register_values[i];
        return NULL;
    }
    if (source->kind != ATOM_EXPR)
        return source;

    Atom **children = arena_alloc(
        arena, sizeof(*children) * (size_t)source->expr.len);
    if (!children)
        return NULL;
    for (CettaExprIndex i = 0u; i < source->expr.len; i++) {
        children[i] = prepared_equation_instantiate_rec(
            plan, source->expr.elems[i], register_values, arena);
        if (!children[i])
            return NULL;
    }
    return atom_expr(arena, children, source->expr.len);
}

Atom *space_prepared_equation_instantiate_ground(
    const SpacePreparedEquation *plan, Atom *call, Arena *arena) {
    if (!plan || !call || !arena ||
        call->kind != ATOM_EXPR || call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return NULL;
    }
    uint32_t evidence =
        plan->evidence & ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    Atom *register_values[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    bool ground_call = true;
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        Atom *value = call->expr.elems[i + 1u];
        if (!value || atom_has_vars(value))
            ground_call = false;
        register_values[i] = value;
    }
    if (ground_call)
        evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_equation_call_admitted(evidence))
        return NULL;
    return prepared_equation_instantiate_rec(
        plan, plan->rhs, register_values, arena);
}

static bool prepared_register_exact_integer(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           (atom->ground.gkind == GV_INT ||
            atom->ground.gkind == GV_BIGINT);
}

static Atom *prepared_register_execute_expression(
    const SpacePreparedEquation *plan, Atom *source,
    Atom *const *register_values, Arena *arena, uint32_t depth,
    CettaGsltRegisterResultKind *kind_out) {
    if (!plan || !source || !register_values || !arena || !kind_out ||
        depth > 64u) {
        return NULL;
    }
    if (source->kind == ATOM_VAR) {
        for (CettaExprIndex i = 0u; i < plan->arity; i++) {
            if (plan->registers[i] == source->var_id) {
                Atom *value = register_values[i];
                if (!prepared_register_exact_integer(value))
                    return NULL;
                *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
                return value;
            }
        }
        return NULL;
    }
    if (prepared_register_exact_integer(source)) {
        *kind_out = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        return source;
    }
    if (source->kind != ATOM_EXPR || source->expr.len == 0u)
        return NULL;
    Atom *head = source->expr.elems[0];
    CettaExprLen nargs = source->expr.len - 1u;
    CettaGsltRegisterResultKind result_kind;
    if (!head || head->kind != ATOM_SYMBOL ||
        !prepared_register_head_kind(head->sym_id, nargs, &result_kind) ||
        nargs > CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS) {
        return NULL;
    }
    Atom *args[CETTA_GSLT_PREPARED_EQUATION_MAX_REGISTERS];
    for (CettaExprIndex i = 0u; i < nargs; i++) {
        CettaGsltRegisterResultKind child_kind;
        args[i] = prepared_register_execute_expression(
            plan, source->expr.elems[i + 1u], register_values,
            arena, depth + 1u, &child_kind);
        if (!args[i] ||
            child_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return NULL;
        }
    }
    Atom *result = grounded_dispatch(arena, head, args, (uint32_t)nargs);
    if (!result)
        return NULL;
    if ((result_kind == CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER &&
         !prepared_register_exact_integer(result)) ||
        (result_kind == CETTA_GSLT_REGISTER_RESULT_BOOLEAN &&
         !(result->kind == ATOM_GROUNDED &&
           result->ground.gkind == GV_BOOL))) {
        return NULL;
    }
    *kind_out = result_kind;
    return result;
}

SpacePreparedRegisterStep space_prepared_equation_execute_register_step(
    const SpacePreparedEquation *plan, Atom *call, Arena *arena,
    Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!plan || !call || !arena || !result_out ||
        !plan->register_guard || !plan->register_base ||
        !plan->register_tail || call->kind != ATOM_EXPR ||
        call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    uint32_t evidence =
        plan->evidence & ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    Atom *register_values[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        Atom *value = call->expr.elems[i + 1u];
        if (!prepared_register_exact_integer(value))
            return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
        register_values[i] = value;
    }
    evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_register_step_admitted(evidence))
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

    CettaGsltRegisterResultKind guard_kind;
    Atom *guard = prepared_register_execute_expression(
        plan, plan->register_guard, register_values,
        arena, 0u, &guard_kind);
    if (!guard || guard_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    bool take_base = guard->ground.bval == plan->register_base_when_true;
    if (take_base) {
        CettaGsltRegisterResultKind base_kind;
        Atom *base = prepared_register_execute_expression(
            plan, plan->register_base, register_values,
            arena, 0u, &base_kind);
        if (!base || base_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
        *result_out = base;
        return SPACE_PREPARED_REGISTER_VALUE;
    }

    Atom **tail_elems = arena_alloc(
        arena, sizeof(*tail_elems) * (size_t)(plan->arity + 1u));
    tail_elems[0] = plan->register_tail->expr.elems[0];
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        CettaGsltRegisterResultKind argument_kind;
        tail_elems[i + 1u] = prepared_register_execute_expression(
            plan, plan->register_tail->expr.elems[i + 1u],
            register_values, arena, 0u, &argument_kind);
        if (!tail_elems[i + 1u] ||
            argument_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
            return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
        }
    }
    *result_out = atom_expr(arena, tail_elems, plan->arity + 1u);
    return *result_out ? SPACE_PREPARED_REGISTER_TAIL_CALL
                       : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
}

#if CETTA_BUILD_WITH_GMP
#define PREPARED_MPZ_SCRATCH_CAP 192u

typedef struct {
    mpz_t values[PREPARED_MPZ_SCRATCH_CAP];
    size_t initialized;
    size_t used;
} PreparedMpzScratch;

typedef struct {
    mpz_srcptr source;
    mpz_ptr temporary;
} PreparedMpzValue;

static mpz_ptr prepared_mpz_scratch_take(PreparedMpzScratch *scratch) {
    if (!scratch || scratch->used >= PREPARED_MPZ_SCRATCH_CAP)
        return NULL;
    if (scratch->used == scratch->initialized) {
        mpz_init(scratch->values[scratch->initialized]);
        scratch->initialized++;
    }
    return scratch->values[scratch->used++];
}

static void prepared_mpz_scratch_reset(PreparedMpzScratch *scratch) {
    if (scratch)
        scratch->used = 0u;
}

static void prepared_mpz_scratch_clear(PreparedMpzScratch *scratch) {
    if (!scratch)
        return;
    for (size_t i = 0u; i < scratch->initialized; i++)
        mpz_clear(scratch->values[i]);
    scratch->initialized = 0u;
    scratch->used = 0u;
}

static void prepared_mpz_set_i64(mpz_ptr out, int64_t value) {
    uint64_t magnitude = value < 0
        ? (uint64_t)(-(value + 1)) + 1u
        : (uint64_t)value;
    mpz_import(out, 1, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (value < 0)
        mpz_neg(out, out);
}

static bool prepared_mpz_set_atom(mpz_ptr out, const Atom *atom) {
    if (!out || !prepared_register_exact_integer(atom))
        return false;
    if (atom->ground.gkind == GV_INT) {
        prepared_mpz_set_i64(out, atom->ground.ival);
        return true;
    }
    mpz_srcptr source = atom_bigint_mpz_view(atom);
    if (!source)
        return false;
    mpz_set(out, source);
    return true;
}

static bool prepared_mpz_register_index(
    const SpacePreparedEquation *plan, const Atom *atom,
    CettaExprIndex *index_out) {
    if (!plan || !atom || atom->kind != ATOM_VAR)
        return false;
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        if (plan->registers[i] == atom->var_id) {
            if (index_out)
                *index_out = i;
            return true;
        }
    }
    return false;
}

static bool prepared_mpz_eval_integer(
    const SpacePreparedEquation *plan, const Atom *source,
    mpz_t *registers, PreparedMpzScratch *scratch,
    uint32_t depth, PreparedMpzValue *value_out) {
    if (!plan || !source || !registers || !scratch || !value_out ||
        depth > 64u) {
        return false;
    }
    CettaExprIndex register_index = 0u;
    if (prepared_mpz_register_index(plan, source, &register_index)) {
        value_out->source = registers[register_index];
        value_out->temporary = NULL;
        return true;
    }
    if (prepared_register_exact_integer(source)) {
        if (source->ground.gkind == GV_BIGINT) {
            value_out->source = atom_bigint_mpz_view(source);
            value_out->temporary = NULL;
            return value_out->source != NULL;
        }
        mpz_ptr literal = prepared_mpz_scratch_take(scratch);
        if (!literal)
            return false;
        prepared_mpz_set_i64(literal, source->ground.ival);
        value_out->source = literal;
        value_out->temporary = literal;
        return true;
    }
    if (source->kind != ATOM_EXPR || source->expr.len != 3u ||
        !source->expr.elems[0] ||
        source->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    CettaGsltRegisterResultKind result_kind;
    SymbolId head = source->expr.elems[0]->sym_id;
    if (!prepared_register_head_kind(head, 2u, &result_kind) ||
        result_kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        return false;
    }
    PreparedMpzValue left = {0};
    PreparedMpzValue right = {0};
    if (!prepared_mpz_eval_integer(
            plan, source->expr.elems[1], registers, scratch,
            depth + 1u, &left) ||
        !prepared_mpz_eval_integer(
            plan, source->expr.elems[2], registers, scratch,
            depth + 1u, &right)) {
        return false;
    }
    mpz_ptr result = prepared_mpz_scratch_take(scratch);
    if (!result)
        return false;
    if (head == g_builtin_syms.op_plus)
        mpz_add(result, left.source, right.source);
    else if (head == g_builtin_syms.op_minus)
        mpz_sub(result, left.source, right.source);
    else if (head == g_builtin_syms.op_mul)
        mpz_mul(result, left.source, right.source);
    else
        return false;
    value_out->source = result;
    value_out->temporary = result;
    return true;
}

static bool prepared_mpz_eval_guard(
    const SpacePreparedEquation *plan, mpz_t *registers,
    PreparedMpzScratch *scratch, bool *value_out) {
    Atom *guard = plan ? plan->register_guard : NULL;
    if (!guard || guard->kind != ATOM_EXPR || guard->expr.len != 3u ||
        !guard->expr.elems[0] || guard->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    CettaGsltRegisterResultKind result_kind;
    SymbolId head = guard->expr.elems[0]->sym_id;
    if (!prepared_register_head_kind(head, 2u, &result_kind) ||
        result_kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN ||
        head != g_builtin_syms.op_eq) {
        return false;
    }
    prepared_mpz_scratch_reset(scratch);
    PreparedMpzValue left = {0};
    PreparedMpzValue right = {0};
    if (!prepared_mpz_eval_integer(
            plan, guard->expr.elems[1], registers, scratch, 0u, &left) ||
        !prepared_mpz_eval_integer(
            plan, guard->expr.elems[2], registers, scratch, 0u, &right)) {
        return false;
    }
    *value_out = mpz_cmp(left.source, right.source) == 0;
    return true;
}

static Atom *prepared_mpz_materialize_call(
    const SpacePreparedEquation *plan, mpz_t *registers, Arena *arena) {
    if (!plan || !registers || !arena)
        return NULL;
    Atom **elems = arena_alloc(
        arena, sizeof(*elems) * (size_t)(plan->arity + 1u));
    elems[0] = atom_symbol_id(arena, plan->head);
    for (CettaExprIndex i = 0u; i < plan->arity; i++)
        elems[i + 1u] = atom_bigint_from_mpz(arena, registers[i]);
    return atom_expr(arena, elems, plan->arity + 1u);
}

static SpacePreparedRegisterStep prepared_mpz_run_register_loop(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_steps, Atom **result_out) {
    if (!plan || !call || !result_arena || !result_out || max_steps == 0u ||
        !plan->register_guard || !plan->register_base ||
        !plan->register_tail || call->kind != ATOM_EXPR ||
        call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    uint32_t evidence = plan->evidence;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    else
        evidence &= ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_register_step_admitted(evidence))
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

    mpz_t banks[2][SPACE_PREPARED_EQUATION_MAX_REGISTERS];
    for (unsigned bank = 0u; bank < 2u; bank++)
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            mpz_init(banks[bank][i]);
    bool initialized = true;
    for (CettaExprIndex i = 0u; i < plan->arity; i++) {
        if (!prepared_mpz_set_atom(banks[0][i], call->expr.elems[i + 1u])) {
            initialized = false;
            break;
        }
    }
    if (!initialized) {
        for (unsigned bank = 0u; bank < 2u; bank++)
            for (CettaExprIndex i = 0u; i < plan->arity; i++)
                mpz_clear(banks[bank][i]);
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }

    PreparedMpzScratch scratch = {0};
    unsigned active = 0u;
    size_t completed = 0u;
    SpacePreparedRegisterStep outcome = SPACE_PREPARED_REGISTER_TAIL_CALL;
    while (completed < max_steps) {
        if (!space_read_token_is_current(plan->occurrence.read))
            break;
        bool guard = false;
        if (!prepared_mpz_eval_guard(
                plan, banks[active], &scratch, &guard)) {
            outcome = completed == 0u
                ? SPACE_PREPARED_REGISTER_NOT_APPLICABLE
                : SPACE_PREPARED_REGISTER_TAIL_CALL;
            break;
        }
        completed++;
        bool take_base = guard == plan->register_base_when_true;
        if (take_base) {
            prepared_mpz_scratch_reset(&scratch);
            PreparedMpzValue base = {0};
            if (!prepared_mpz_eval_integer(
                    plan, plan->register_base, banks[active],
                    &scratch, 0u, &base)) {
                outcome = SPACE_PREPARED_REGISTER_TAIL_CALL;
                break;
            }
            *result_out = atom_bigint_from_mpz(result_arena, base.source);
            outcome = *result_out
                ? SPACE_PREPARED_REGISTER_VALUE
                : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
            goto prepared_mpz_done;
        }

        unsigned next = active ^ 1u;
        for (CettaExprIndex i = 0u; i < plan->arity; i++) {
            prepared_mpz_scratch_reset(&scratch);
            PreparedMpzValue argument = {0};
            if (!prepared_mpz_eval_integer(
                    plan, plan->register_tail->expr.elems[i + 1u],
                    banks[active], &scratch, 0u, &argument)) {
                outcome = SPACE_PREPARED_REGISTER_TAIL_CALL;
                goto prepared_mpz_materialize;
            }
            if (argument.temporary)
                mpz_swap(banks[next][i], argument.temporary);
            else
                mpz_set(banks[next][i], argument.source);
        }
        active = next;
    }

prepared_mpz_materialize:
    if (outcome != SPACE_PREPARED_REGISTER_NOT_APPLICABLE)
        *result_out = prepared_mpz_materialize_call(
            plan, banks[active], result_arena);
prepared_mpz_done:
    prepared_mpz_scratch_clear(&scratch);
    for (unsigned bank = 0u; bank < 2u; bank++)
        for (CettaExprIndex i = 0u; i < plan->arity; i++)
            mpz_clear(banks[bank][i]);
    return *result_out ? outcome
                       : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
}

typedef struct {
    mpz_t registers[SPACE_PREPARED_EQUATION_MAX_REGISTERS];
} PreparedRecursiveEnvironment;

typedef struct {
    mpz_t integer;
    CettaGsltRegisterResultKind kind;
    bool boolean;
} PreparedRecursiveValue;

typedef enum {
    PREPARED_RECURSIVE_ENTER = 0,
    PREPARED_RECURSIVE_ARGUMENTS,
    PREPARED_RECURSIVE_IF_CONDITION,
    PREPARED_RECURSIVE_IF_BRANCH,
    PREPARED_RECURSIVE_SELF_RESULT,
} PreparedRecursiveFrameState;

typedef struct {
    const Atom *source;
    size_t environment;
    size_t value_base;
    CettaExprIndex next_argument;
    CettaExprLen argument_count;
    SymbolId head;
    CettaGsltRegisterResultKind result_kind;
    CettaGsltRegisterInstruction instruction;
    PreparedRecursiveFrameState state;
} PreparedRecursiveFrame;

typedef struct {
    const SpacePreparedEquation *plan;
    PreparedRecursiveEnvironment **environments;
    size_t environment_len;
    size_t environment_pool_len;
    size_t environment_cap;
    PreparedRecursiveValue **values;
    size_t value_len;
    size_t value_pool_len;
    size_t value_cap;
    PreparedRecursiveFrame *frames;
    size_t frame_len;
    size_t frame_cap;
    size_t calls;
    size_t max_calls;
} PreparedRecursiveMachine;

static bool prepared_recursive_grow_pointer_array(
    void ***items, size_t *capacity, size_t needed) {
    if (!items || !capacity)
        return false;
    if (needed <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 32u;
    while (next < needed) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(**items))
        return false;
    *items = cetta_realloc(*items, sizeof(**items) * next);
    *capacity = next;
    return true;
}

static PreparedRecursiveEnvironment *
prepared_recursive_push_environment(PreparedRecursiveMachine *machine) {
    if (!machine)
        return NULL;
    if (machine->environment_len == machine->environment_pool_len) {
        if (!prepared_recursive_grow_pointer_array(
                (void ***)&machine->environments,
                &machine->environment_cap,
                machine->environment_pool_len + 1u)) {
            return NULL;
        }
        PreparedRecursiveEnvironment *environment =
            cetta_malloc(sizeof(*environment));
        for (CettaExprIndex i = 0u; i < machine->plan->arity; i++)
            mpz_init(environment->registers[i]);
        machine->environments[machine->environment_pool_len++] =
            environment;
    }
    return machine->environments[machine->environment_len++];
}

static PreparedRecursiveValue *
prepared_recursive_push_value(PreparedRecursiveMachine *machine) {
    if (!machine)
        return NULL;
    if (machine->value_len == machine->value_pool_len) {
        if (!prepared_recursive_grow_pointer_array(
                (void ***)&machine->values, &machine->value_cap,
                machine->value_pool_len + 1u)) {
            return NULL;
        }
        PreparedRecursiveValue *value = cetta_malloc(sizeof(*value));
        mpz_init(value->integer);
        value->kind = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
        value->boolean = false;
        machine->values[machine->value_pool_len++] = value;
    }
    return machine->values[machine->value_len++];
}

static bool prepared_recursive_push_integer(
    PreparedRecursiveMachine *machine, mpz_srcptr source) {
    if (!source)
        return false;
    PreparedRecursiveValue *value =
        prepared_recursive_push_value(machine);
    if (!value)
        return false;
    mpz_set(value->integer, source);
    value->kind = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
    value->boolean = false;
    return true;
}

static bool prepared_recursive_push_atom_integer(
    PreparedRecursiveMachine *machine, const Atom *source) {
    PreparedRecursiveValue *value =
        prepared_recursive_push_value(machine);
    if (!value)
        return false;
    if (!prepared_mpz_set_atom(value->integer, source)) {
        machine->value_len--;
        return false;
    }
    value->kind = CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
    value->boolean = false;
    return true;
}

static bool prepared_recursive_push_frame(
    PreparedRecursiveMachine *machine, const Atom *source,
    size_t environment) {
    if (!machine || !source || environment >= machine->environment_len)
        return false;
    if (machine->frame_len == machine->frame_cap) {
        size_t next = machine->frame_cap ? machine->frame_cap * 2u : 64u;
        if (next < machine->frame_cap ||
            next > SIZE_MAX / sizeof(*machine->frames)) {
            return false;
        }
        machine->frames = cetta_realloc(
            machine->frames, sizeof(*machine->frames) * next);
        machine->frame_cap = next;
    }
    PreparedRecursiveFrame *frame =
        &machine->frames[machine->frame_len++];
    memset(frame, 0, sizeof(*frame));
    frame->source = source;
    frame->environment = environment;
    frame->state = PREPARED_RECURSIVE_ENTER;
    return true;
}

static void prepared_recursive_machine_free(
    PreparedRecursiveMachine *machine) {
    if (!machine)
        return;
    for (size_t i = 0u; i < machine->environment_pool_len; i++) {
        PreparedRecursiveEnvironment *environment =
            machine->environments[i];
        for (CettaExprIndex j = 0u; j < machine->plan->arity; j++)
            mpz_clear(environment->registers[j]);
        free(environment);
    }
    for (size_t i = 0u; i < machine->value_pool_len; i++) {
        mpz_clear(machine->values[i]->integer);
        free(machine->values[i]);
    }
    free(machine->environments);
    free(machine->values);
    free(machine->frames);
    memset(machine, 0, sizeof(*machine));
}

static bool prepared_recursive_apply_instruction(
    PreparedRecursiveMachine *machine,
    const PreparedRecursiveFrame *frame) {
    if (!machine || !frame || frame->argument_count != 2u ||
        machine->value_len != frame->value_base + 2u) {
        return false;
    }
    PreparedRecursiveValue *left =
        machine->values[frame->value_base];
    PreparedRecursiveValue *right =
        machine->values[frame->value_base + 1u];
    if (left->kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER ||
        right->kind != CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
        return false;
    }
    bool boolean = false;
    CettaGsltRegisterResultKind result_kind;
    if (!cetta_gslt_register_execute_binary(
            frame->instruction, left->integer, &boolean,
            left->integer, right->integer, &result_kind) ||
        result_kind != frame->result_kind) {
        return false;
    }
    left->kind = result_kind;
    left->boolean = boolean;
    machine->value_len = frame->value_base + 1u;
    return true;
}

static bool prepared_recursive_start_self_call(
    PreparedRecursiveMachine *machine,
    PreparedRecursiveFrame *frame) {
    if (!machine || !frame ||
        frame->argument_count != machine->plan->arity ||
        machine->value_len !=
            frame->value_base + (size_t)frame->argument_count ||
        machine->calls >= machine->max_calls ||
        !space_read_token_is_current(machine->plan->occurrence.read)) {
        return false;
    }
    for (CettaExprIndex i = 0u; i < frame->argument_count; i++)
        if (machine->values[frame->value_base + (size_t)i]->kind !=
            CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER)
            return false;
    PreparedRecursiveEnvironment *environment =
        prepared_recursive_push_environment(machine);
    if (!environment)
        return false;
    for (CettaExprIndex i = 0u; i < frame->argument_count; i++) {
        mpz_set(
            environment->registers[i],
            machine->values[frame->value_base + (size_t)i]->integer);
    }
    machine->value_len = frame->value_base;
    machine->calls++;
    frame->state = PREPARED_RECURSIVE_SELF_RESULT;
    return prepared_recursive_push_frame(
        machine, machine->plan->rhs, machine->environment_len - 1u);
}

static bool prepared_recursive_run(
    PreparedRecursiveMachine *machine) {
    while (machine && machine->frame_len > 0u) {
        PreparedRecursiveFrame *frame =
            &machine->frames[machine->frame_len - 1u];
        if (frame->state == PREPARED_RECURSIVE_ENTER) {
            const Atom *source = frame->source;
            CettaExprIndex register_index = 0u;
            if (prepared_mpz_register_index(
                    machine->plan, source, &register_index)) {
                if (!prepared_recursive_push_integer(
                        machine,
                        machine->environments[frame->environment]
                            ->registers[register_index])) {
                    return false;
                }
                machine->frame_len--;
                continue;
            }
            if (prepared_register_exact_integer(source)) {
                if (!prepared_recursive_push_atom_integer(machine, source))
                    return false;
                machine->frame_len--;
                continue;
            }
            if (source->kind != ATOM_EXPR || source->expr.len == 0u ||
                !source->expr.elems[0] ||
                source->expr.elems[0]->kind != ATOM_SYMBOL) {
                return false;
            }
            frame->head = source->expr.elems[0]->sym_id;
            frame->argument_count = source->expr.len - 1u;
            frame->value_base = machine->value_len;
            if (frame->head == g_builtin_syms.if_text) {
                if (frame->argument_count != 3u)
                    return false;
                frame->state = PREPARED_RECURSIVE_IF_CONDITION;
                if (!prepared_recursive_push_frame(
                        machine, source->expr.elems[1],
                        frame->environment)) {
                    return false;
                }
                continue;
            }
            if (frame->head == machine->plan->head) {
                if (frame->argument_count != machine->plan->arity)
                    return false;
                frame->result_kind =
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
            } else if (!prepared_register_head_program(
                           frame->head, frame->argument_count,
                           &frame->result_kind, &frame->instruction)) {
                return false;
            }
            frame->next_argument = 0u;
            frame->state = PREPARED_RECURSIVE_ARGUMENTS;
            if (frame->argument_count == 0u) {
                if (frame->head != machine->plan->head ||
                    !prepared_recursive_start_self_call(machine, frame)) {
                    return false;
                }
                continue;
            }
            if (!prepared_recursive_push_frame(
                    machine, source->expr.elems[1],
                    frame->environment)) {
                return false;
            }
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_ARGUMENTS) {
            if (machine->value_len !=
                    frame->value_base +
                    (size_t)frame->next_argument + 1u ||
                machine->values[machine->value_len - 1u]->kind !=
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
            frame->next_argument++;
            if (frame->next_argument < frame->argument_count) {
                if (!prepared_recursive_push_frame(
                        machine,
                        frame->source->expr.elems[
                            frame->next_argument + 1u],
                        frame->environment)) {
                    return false;
                }
                continue;
            }
            if (frame->head == machine->plan->head) {
                if (!prepared_recursive_start_self_call(machine, frame))
                    return false;
                continue;
            }
            if (!prepared_recursive_apply_instruction(machine, frame))
                return false;
            machine->frame_len--;
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_IF_CONDITION) {
            if (machine->value_len != frame->value_base + 1u)
                return false;
            PreparedRecursiveValue *condition =
                machine->values[frame->value_base];
            if (condition->kind != CETTA_GSLT_REGISTER_RESULT_BOOLEAN)
                return false;
            const Atom *branch = frame->source->expr.elems[
                condition->boolean ? 2u : 3u];
            machine->value_len = frame->value_base;
            frame->state = PREPARED_RECURSIVE_IF_BRANCH;
            if (!prepared_recursive_push_frame(
                    machine, branch, frame->environment)) {
                return false;
            }
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_IF_BRANCH) {
            if (machine->value_len != frame->value_base + 1u ||
                machine->values[frame->value_base]->kind !=
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
            machine->frame_len--;
            continue;
        }

        if (frame->state == PREPARED_RECURSIVE_SELF_RESULT) {
            if (machine->environment_len != frame->environment + 2u ||
                machine->value_len != frame->value_base + 1u ||
                machine->values[frame->value_base]->kind !=
                    CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER) {
                return false;
            }
            machine->environment_len--;
            machine->frame_len--;
            continue;
        }
        return false;
    }
    return machine && machine->environment_len == 1u &&
           machine->value_len == 1u &&
           machine->values[0]->kind ==
               CETTA_GSLT_REGISTER_RESULT_EXACT_INTEGER;
}

static SpacePreparedRegisterStep prepared_mpz_run_register_recursion(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_calls, Atom **result_out) {
    if (!plan || !call || !result_arena || !result_out ||
        max_calls == 0u || call->kind != ATOM_EXPR ||
        call->expr.len != plan->arity + 1u ||
        !atom_is_symbol_id(call->expr.elems[0], plan->head)) {
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    uint32_t evidence = plan->evidence;
    if (space_read_token_is_current(plan->occurrence.read))
        evidence |= CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    else
        evidence &= ~CETTA_GSLT_EVIDENCE_REVISION_CURRENT;
    evidence |= CETTA_GSLT_EVIDENCE_GROUND_CALL;
    if (!cetta_gslt_prepared_register_recursion_admitted(evidence))
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

    PreparedRecursiveMachine machine = {
        .plan = plan,
        .max_calls = max_calls,
        .calls = 1u,
    };
    PreparedRecursiveEnvironment *initial =
        prepared_recursive_push_environment(&machine);
    bool initialized = initial != NULL;
    for (CettaExprIndex i = 0u; initialized && i < plan->arity; i++) {
        initialized = prepared_mpz_set_atom(
            initial->registers[i], call->expr.elems[i + 1u]);
    }
    bool completed = initialized &&
        prepared_recursive_push_frame(&machine, plan->rhs, 0u) &&
        prepared_recursive_run(&machine);
    if (completed)
        *result_out = atom_bigint_from_mpz(
            result_arena, machine.values[0]->integer);
    prepared_recursive_machine_free(&machine);
    return *result_out ? SPACE_PREPARED_REGISTER_VALUE
                       : SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
}
#endif

/* Execute a bounded run of the same proved register instruction in a private
 * nursery.  GMP builds keep exact numerics in reusable machine registers;
 * the arena semispace below is the representation-independent fallback. */
SpacePreparedRegisterStep space_prepared_equation_run_register_loop(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_steps, Atom **result_out) {
    enum { REGISTER_LOOP_NURSERY_BYTES = 8u * 1024u * 1024u };
    if (result_out)
        *result_out = NULL;
    if (!plan || !call || !result_arena || !result_out || max_steps == 0u)
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;

#if CETTA_BUILD_WITH_GMP
    SpacePreparedRegisterStep mpz_outcome =
        prepared_mpz_run_register_loop(
            plan, call, result_arena, max_steps, result_out);
    if (mpz_outcome != SPACE_PREPARED_REGISTER_NOT_APPLICABLE)
        return mpz_outcome;
#endif

    Arena semispaces[2];
    arena_init(&semispaces[0]);
    arena_init(&semispaces[1]);
    arena_set_hashcons(&semispaces[0], NULL);
    arena_set_hashcons(&semispaces[1], NULL);
    arena_set_runtime_kind(
        &semispaces[0], CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_runtime_kind(
        &semispaces[1], CETTA_ARENA_RUNTIME_KIND_SCRATCH);

    unsigned active = 0u;
    Atom *current = call;
    SpacePreparedRegisterStep outcome =
        SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    size_t completed = 0u;
    while (completed < max_steps) {
        Atom *next = NULL;
        SpacePreparedRegisterStep step =
            space_prepared_equation_execute_register_step(
                plan, current, &semispaces[active], &next);
        if (step == SPACE_PREPARED_REGISTER_NOT_APPLICABLE) {
            /* A revision change or a value outside the proved fragment falls
             * back at the exact current call, never at the stale entry call. */
            outcome = completed == 0u
                ? SPACE_PREPARED_REGISTER_NOT_APPLICABLE
                : SPACE_PREPARED_REGISTER_TAIL_CALL;
            break;
        }
        completed++;
        current = next;
        outcome = step;
        if (step == SPACE_PREPARED_REGISTER_VALUE)
            break;

        if (arena_accounted_live_bytes(&semispaces[active]) >=
            REGISTER_LOOP_NURSERY_BYTES) {
            unsigned destination = active ^ 1u;
            arena_free(&semispaces[destination]);
            arena_init(&semispaces[destination]);
            arena_set_hashcons(&semispaces[destination], NULL);
            arena_set_runtime_kind(
                &semispaces[destination],
                CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            Atom *evacuated = atom_deep_copy(
                &semispaces[destination], current);
            if (!evacuated) {
                fputs("fatal: prepared register-loop evacuation failed\n",
                      stderr);
                abort();
            }
            arena_free(&semispaces[active]);
            arena_init(&semispaces[active]);
            arena_set_hashcons(&semispaces[active], NULL);
            arena_set_runtime_kind(
                &semispaces[active], CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            active = destination;
            current = evacuated;
        }
    }

    if (outcome != SPACE_PREPARED_REGISTER_NOT_APPLICABLE) {
        *result_out = atom_deep_copy(result_arena, current);
        if (!*result_out)
            outcome = SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
    }
    arena_free(&semispaces[0]);
    arena_free(&semispaces[1]);
    return outcome;
}

/* Execute the generated pure recursive-register program on explicit
 * environments, work frames, and value slots.  The machine owns no arena
 * pointers other than the revision-pinned source plan; exact numerics remain
 * in reusable GMP cells and only the final value is materialized. */
SpacePreparedRegisterStep space_prepared_equation_run_register_recursion(
    const SpacePreparedEquation *plan, Atom *call, Arena *result_arena,
    size_t max_calls, Atom **result_out) {
    if (result_out)
        *result_out = NULL;
    if (!plan || !call || !result_arena || !result_out || max_calls == 0u)
        return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
#if CETTA_BUILD_WITH_GMP
    return prepared_mpz_run_register_recursion(
        plan, call, result_arena, max_calls, result_out);
#else
    return SPACE_PREPARED_REGISTER_NOT_APPLICABLE;
#endif
}

CettaCount query_equation_visit(Atom *equation, Atom *query, Arena *a,
                                QueryResultVisitor visitor, void *ctx) {
    QueryResultSink sink;
    QueryVisibleVarSet visible;
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !query || !a || !visitor ||
        !is_equation_atom(equation, &lhs, &rhs)) {
        return 0;
    }
    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return 0;
    }
    query_result_sink_init_visit(&sink, visitor, ctx);
    (void)query_equation_emit_decoded_epoch(
        lhs, rhs, query, &visible, a, fresh_var_suffix(), NULL, &sink);
    query_visible_var_set_free(&visible);
    return sink.emitted;
}

CettaCount query_equations_visit_singleton(
    Atom *equation, Atom *query, Arena *a,
    QueryResultVisitor visitor, void *ctx) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS);
    if (!equation || !query || !a || !visitor)
        return 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES);
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES);
    return query_equation_visit(equation, query, a, visitor, ctx);
}

void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out) {
    QueryResultSink sink;
    query_result_sink_init_collect(&sink, out);
    (void)query_equations_core(s, query, a, &sink);
}

bool space_equations_may_match_known_head(Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE)
        return true;
    if (space_has_overlay_base(s)) {
        /* Scan only the local scratch tail and delegate the base-visible
         * prefix to the base's own (indexed) check.  Base equations removed
         * through the overlay can yield a conservative true, which is
         * harmless for a may-match predicate. */
        CettaCount base_visible = space_overlay_visible_base_count(s);
        CettaCount logical_len = space_length64(s);
        for (CettaIndex i = base_visible; i < logical_len; i++) {
            Atom *equation = space_get_at64(s, i);
            Atom *lhs = NULL;
            Atom *rhs = NULL;
            SymbolId lhs_head;
            if (!equation || !is_equation_atom(equation, &lhs, &rhs))
                continue;
            lhs_head = eq_head_symbol(lhs);
            if (lhs_head == SYMBOL_ID_NONE || lhs_head == head)
                return true;
        }
        return space_equations_may_match_known_head((Space *)s->overlay_base,
                                                    head);
    }
    ensure_eq_index(s);
    if (s->native.eq_idx.wildcard.len > 0)
        return true;
    EqBucket *bucket = &s->native.eq_idx.buckets[symbol_hash(head)];
    if (bucket->len == 0)
        return false;
    if (!bucket->mixed_heads)
        return bucket->head == head;

    for (CettaIndex i = 0; i < bucket->len; i++) {
        CettaIndex atom_idx = bucket->atom_indices[i];
        if (atom_idx >= s->native.len)
            continue;
        AtomId equation_id = space_get_atom_id_at64(s, atom_idx);
        AtomId lhs_id = CETTA_ATOM_ID_NONE;
        AtomId rhs_id = CETTA_ATOM_ID_NONE;
        if (space_equation_child_ids_at_id(s, equation_id, &lhs_id, &rhs_id)) {
            if (eq_head_symbol_id(s, lhs_id) == head)
                return true;
            continue;
        }
        Atom *lhs = NULL;
        Atom *rhs = NULL;
        if (space_equation_children_at_id(s, equation_id, &lhs, &rhs) &&
            eq_head_symbol(lhs) == head) {
            return true;
        }
    }
    return false;
}

static void space_equation_note_head_arity(
    Atom *equation, SymbolId head, CettaExprLen query_arity,
    bool *found, CettaExprLen *minimum, CettaExprLen *maximum,
    bool *has_exact) {
    Atom *lhs = NULL;
    Atom *rhs = NULL;
    if (!equation || !is_equation_atom(equation, &lhs, &rhs) ||
        !lhs || lhs->kind != ATOM_EXPR || lhs->expr.len == 0u ||
        eq_head_symbol(lhs) != head)
        return;
    CettaExprLen arity = lhs->expr.len - 1u;
    if (!*found || arity < *minimum)
        *minimum = arity;
    if (!*found || arity > *maximum)
        *maximum = arity;
    *found = true;
    *has_exact = *has_exact || arity == query_arity;
}

bool space_equation_head_arity_bounds(
    Space *s, SymbolId head, CettaExprLen *minimum,
    CettaExprLen *maximum, bool *has_exact,
    CettaExprLen query_arity) {
    if (minimum)
        *minimum = 0u;
    if (maximum)
        *maximum = 0u;
    if (has_exact)
        *has_exact = false;
    if (!s || head == SYMBOL_ID_NONE || !minimum || !maximum ||
        !has_exact)
        return false;

    bool found = false;
    if (space_has_overlay_base(s)) {
        CettaCount logical_len = space_length64(s);
        for (CettaIndex index = 0u; index < logical_len; index++) {
            space_equation_note_head_arity(
                space_get_at64(s, index), head, query_arity,
                &found, minimum, maximum, has_exact);
        }
        return found;
    }

    ensure_eq_index(s);
    EqBucket *bucket =
        &s->native.eq_idx.buckets[symbol_hash(head)];
    for (CettaIndex index = 0u; index < bucket->len; index++) {
        CettaIndex logical_index = bucket->atom_indices[index];
        if (logical_index >= s->native.len)
            continue;
        space_equation_note_head_arity(
            space_get_at64(s, logical_index), head, query_arity,
            &found, minimum, maximum, has_exact);
    }
    return found;
}
