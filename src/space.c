#include "space.h"
#include "grounded.h"
#include "search_machine.h"
#include "stats.h"
#include <stdlib.h>
#include <string.h>

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

static void disc_add_leaf(DiscNode *n, uint32_t idx) {
    if (n->nleaves >= n->cleaves) {
        n->cleaves = n->cleaves ? n->cleaves * 2 : 4;
        n->leaves = cetta_realloc(n->leaves, sizeof(uint32_t) * n->cleaves);
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

static DiscNode *disc_get_expr(DiscNode *n, uint32_t arity) {
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
        for (uint32_t i = 0; i < a->expr.len; i++)
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
        for (uint32_t i = 0; i < tu_arity(universe, atom_id); i++) {
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

void disc_insert(DiscNode *root, Atom *lhs, uint32_t eq_idx) {
    DiscNode *leaf = disc_insert_atom(root, lhs);
    disc_add_leaf(leaf, eq_idx);
}

bool disc_insert_id(DiscNode *root, const TermUniverse *universe,
                    AtomId atom_id, uint32_t eq_idx) {
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
        for (uint32_t ci = 0; ci < node->expr[i].arity; ci++) {
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
                for (uint32_t ci = 0; ci < q->expr.len; ci++) {
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

void disc_lookup(DiscNode *root, Atom *query, uint32_t **out, uint32_t *nout, uint32_t *cout) {
    *out = NULL; *nout = 0; *cout = 0;
    /* disc_step from root through the query atom */
    DiscNodeSet final;
    dns_init(&final);
    disc_step(root, query, &final);
    /* Collect equation indices from all terminal nodes' leaves */
    for (uint32_t i = 0; i < final.n; i++) {
        DiscNode *n = final.nodes[i];
        for (uint32_t j = 0; j < n->nleaves; j++) {
            if (*nout >= *cout) {
                *cout = *cout ? *cout * 2 : 16;
                *out = cetta_realloc(*out, sizeof(uint32_t) * *cout);
            }
            (*out)[(*nout)++] = n->leaves[j];
        }
    }
    dns_free(&final);
}

/* ── Equation Index ─────────────────────────────────────────────────────── */

static bool __attribute__((unused)) atom_is_eq_subst_safe(Atom *atom);

static void eq_bucket_init(EqBucket *b) {
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
    b->trie = NULL;
    stree_bucket_init(&b->subst);
    b->subst_safe = true;
}

static void eq_bucket_add(EqBucket *b, Atom *lhs, uint32_t atom_idx) {
    uint32_t idx = b->len;
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(uint32_t) * b->cap);
    }
    b->atom_indices[b->len] = atom_idx;
    b->len++;
    /* Add to discrimination trie */
    if (!b->trie) b->trie = disc_node_new();
    disc_insert(b->trie, lhs, idx);
    stree_bucket_insert(&b->subst, lhs, idx);
    b->subst_safe = b->subst_safe && atom_is_eq_subst_safe(lhs);
}

static void eq_bucket_free(EqBucket *b) {
    free(b->atom_indices);
    disc_node_free(b->trie); b->trie = NULL;
    stree_bucket_free(&b->subst);
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
    b->subst_safe = true;
}

static uint32_t symbol_hash(SymbolId id) {
    uint32_t mixed = (uint32_t)((uint64_t)id * 2654435761u);
    return mixed % EQ_INDEX_BUCKETS;
}

static bool __attribute__((unused)) atom_is_eq_subst_safe(Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        return atom->ground.gkind == GV_INT ||
               atom->ground.gkind == GV_STRING;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!atom_is_eq_subst_safe(atom->expr.elems[i]))
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

static void eq_index_add(EqIndex *idx, Atom *lhs, uint32_t atom_idx) {
    SymbolId head = eq_head_symbol(lhs);
    if (head != SYMBOL_ID_NONE) {
        eq_bucket_add(&idx->buckets[symbol_hash(head)], lhs, atom_idx);
    } else {
        eq_bucket_add(&idx->wildcard, lhs, atom_idx);
    }
}

/* ── Type Annotation Index ──────────────────────────────────────────────── */

static void ty_ann_bucket_init(TypeAnnBucket *b) {
    b->atom_indices = NULL; b->len = 0; b->cap = 0;
}
static void ty_ann_bucket_add(TypeAnnBucket *b, uint32_t atom_idx) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4;
        b->atom_indices =
            cetta_realloc(b->atom_indices, sizeof(uint32_t) * b->cap);
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
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return 0;
    if (tu_hdr(s->universe, atom_id)) {
        if (tu_kind(s->universe, atom_id) == ATOM_SYMBOL)
            return symbol_hash(tu_sym(s->universe, atom_id));
        if (tu_kind(s->universe, atom_id) == ATOM_EXPR &&
            tu_arity(s->universe, atom_id) > 0) {
            SymbolId head = tu_head_sym(s->universe, atom_id);
            return head == SYMBOL_ID_NONE ? 0u : symbol_hash(head);
        }
        return 0u;
    }
    Atom *atom = term_universe_get_atom(s->universe, atom_id);
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
static void ty_ann_index_add(TypeAnnIndex *idx, Atom *ann_atom, uint32_t atom_idx) {
    uint32_t h = atom_hash_for_index(ann_atom);
    ty_ann_bucket_add(&idx->buckets[h], atom_idx);
}

static void ty_ann_index_add_id(TypeAnnIndex *idx, const Space *s,
                                AtomId subject_id, uint32_t atom_idx) {
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

static void exact_atom_bucket_add(ExactAtomBucket *b, uint32_t idx) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4;
        b->indices = cetta_realloc(b->indices, sizeof(uint32_t) * b->cap);
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

static bool atom_has_variables(const Atom *atom) {
    if (!atom) return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return true;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
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
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
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
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    const CettaTermHdr *hdr = tu_hdr(s->universe, atom_id);
    if (!hdr)
        return atom_is_exact_indexable(term_universe_get_atom(s->universe, atom_id));
    switch (tu_kind(s->universe, atom_id)) {
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
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        return !tu_has_vars(s->universe, atom_id);
    }
    return false;
}

static uint32_t exact_atom_hash_id(const Space *s, AtomId atom_id) {
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return 0;
    const CettaTermHdr *hdr = tu_hdr(s->universe, atom_id);
    if (hdr)
        return tu_hash32(s->universe, atom_id) % EXACT_INDEX_BUCKETS;
    Atom *atom = term_universe_get_atom(s->universe, atom_id);
    return atom ? exact_atom_hash(atom) : 0u;
}

static void eq_index_rebuild(Space *s);
static void ty_ann_index_rebuild(Space *s);
static void exact_index_rebuild(Space *s);

static TermUniverse g_space_default_universe = {0};
static Arena g_space_default_arena = {0};
static bool g_space_default_universe_ready = false;

static TermUniverse *space_default_universe(void) {
    if (!g_space_default_universe_ready) {
        arena_init(&g_space_default_arena);
        term_universe_init(&g_space_default_universe);
        term_universe_set_persistent_arena(&g_space_default_universe,
                                           &g_space_default_arena);
        g_space_default_universe_ready = true;
    }
    return &g_space_default_universe;
}

static bool space_tracks_atom_ids(const Space *s) {
    return s && s->universe != NULL;
}

static uint32_t space_physical_index(const Space *s, uint32_t idx) {
    return space_is_queue(s) ? (s->start + idx) : idx;
}

static void space_reserve_linear(Space *s, uint32_t min_cap) {
    if (s->cap >= min_cap)
        return;
    uint32_t new_cap = s->cap ? s->cap : 64;
    while (new_cap < min_cap)
        new_cap *= 2;
    s->atom_ids = cetta_realloc(s->atom_ids, sizeof(AtomId) * new_cap);
    s->cap = new_cap;
}

void space_linearize(Space *s) {
    if (!space_is_queue(s) || s->start == 0)
        return;
    if (s->len > 0) {
        memmove(s->atom_ids, s->atom_ids + s->start, sizeof(AtomId) * s->len);
    }
    s->start = 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUEUE_COMPACT);
}

static void space_queue_reserve_tail(Space *s, uint32_t extra) {
    uint32_t tail = s->start + s->len;
    if (tail + extra <= s->cap)
        return;
    if (s->start > 0) {
        space_linearize(s);
        tail = s->len;
        if (tail + extra <= s->cap)
            return;
    }
    space_reserve_linear(s, s->len + extra);
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

static void space_mark_indexes_dirty(Space *s) {
    if (!s) return;
    s->eq_idx_dirty = true;
    s->ty_idx_dirty = true;
    s->exact_idx_dirty = true;
}

static void ensure_eq_index(Space *s) {
    if (s && s->eq_idx_dirty)
        eq_index_rebuild(s);
}

static void ensure_ty_ann_index(Space *s) {
    if (s && s->ty_idx_dirty)
        ty_ann_index_rebuild(s);
}

static void ensure_exact_index(Space *s) {
    if (s && s->exact_idx_dirty)
        exact_index_rebuild(s);
}

/* ── Space ──────────────────────────────────────────────────────────────── */

void space_init_with_universe(Space *s, TermUniverse *universe) {
    s->atom_ids = NULL;
    s->start = 0;
    s->len = 0;
    s->cap = 0;
    s->kind = SPACE_KIND_ATOM;
    s->universe = universe ? universe : space_default_universe();
    eq_index_init(&s->eq_idx);
    ty_ann_index_init(&s->ty_idx);
    exact_index_init(&s->exact_idx);
    s->eq_idx_dirty = false;
    s->ty_idx_dirty = false;
    s->exact_idx_dirty = false;
    s->revision = 0;
    space_match_backend_init(s);
}

void space_init(Space *s) {
    space_init_with_universe(s, NULL);
}

void space_free(Space *s) {
    free(s->atom_ids);
    s->atom_ids = NULL;
    s->start = 0;
    s->len = 0;
    s->cap = 0;
    s->universe = space_default_universe();
    s->revision = 0;
    eq_index_free(&s->eq_idx);
    ty_ann_index_free(&s->ty_idx);
    exact_index_free(&s->exact_idx);
    space_match_backend_free(s);
}

Atom *space_store_atom(Space *s, Arena *fallback, Atom *atom) {
    return term_universe_store_atom(s ? s->universe : NULL, fallback, atom);
}

static bool is_equation_atom(Atom *a, Atom **lhs_out, Atom **rhs_out) {
    if (a->kind != ATOM_EXPR || a->expr.len != 3) return false;
    if (!atom_is_symbol_id(a->expr.elems[0], g_builtin_syms.equals)) return false;
    *lhs_out = a->expr.elems[1];
    *rhs_out = a->expr.elems[2];
    return true;
}

static bool space_equation_child_ids_at_id(const Space *s, AtomId atom_id,
                                           AtomId *lhs_id_out, AtomId *rhs_id_out) {
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!tu_hdr(s->universe, atom_id) ||
        tu_kind(s->universe, atom_id) != ATOM_EXPR ||
        tu_arity(s->universe, atom_id) != 3 ||
        tu_head_sym(s->universe, atom_id) != g_builtin_syms.equals)
        return false;
    AtomId lhs_id = tu_child(s->universe, atom_id, 1);
    AtomId rhs_id = tu_child(s->universe, atom_id, 2);
    if (lhs_id == CETTA_ATOM_ID_NONE || rhs_id == CETTA_ATOM_ID_NONE)
        return false;
    *lhs_id_out = lhs_id;
    *rhs_id_out = rhs_id;
    return true;
}

static bool space_equation_children_at_id(const Space *s, AtomId atom_id,
                                          Atom **lhs_out, Atom **rhs_out) {
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    AtomId lhs_id = CETTA_ATOM_ID_NONE;
    AtomId rhs_id = CETTA_ATOM_ID_NONE;
    if (space_equation_child_ids_at_id(s, atom_id, &lhs_id, &rhs_id)) {
        Atom *lhs = term_universe_get_atom(s->universe, lhs_id);
        Atom *rhs = term_universe_get_atom(s->universe, rhs_id);
        if (lhs && rhs) {
            *lhs_out = lhs;
            *rhs_out = rhs;
            return true;
        }
    }
    if (tu_hdr(s->universe, atom_id))
        return false;
    Atom *atom = term_universe_get_atom(s->universe, atom_id);
    return atom ? is_equation_atom(atom, lhs_out, rhs_out) : false;
}

static bool space_type_annotation_child_ids_at_id(const Space *s,
                                                  AtomId atom_id,
                                                  AtomId *subject_id_out,
                                                  AtomId *type_id_out) {
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return false;
    if (!tu_hdr(s->universe, atom_id) ||
        tu_kind(s->universe, atom_id) != ATOM_EXPR ||
        tu_arity(s->universe, atom_id) != 3 ||
        tu_head_sym(s->universe, atom_id) != g_builtin_syms.colon)
        return false;
    AtomId subject_id = tu_child(s->universe, atom_id, 1);
    AtomId type_id = tu_child(s->universe, atom_id, 2);
    if (subject_id == CETTA_ATOM_ID_NONE || type_id == CETTA_ATOM_ID_NONE)
        return false;
    *subject_id_out = subject_id;
    *type_id_out = type_id;
    return true;
}

static Atom *space_type_annotation_subject_at_id(const Space *s,
                                                 AtomId atom_id) {
    if (!s || !s->universe || atom_id == CETTA_ATOM_ID_NONE)
        return NULL;
    AtomId subject_id = CETTA_ATOM_ID_NONE;
    AtomId type_id = CETTA_ATOM_ID_NONE;
    if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
        if (subject_id != CETTA_ATOM_ID_NONE)
            return term_universe_get_atom(s->universe, subject_id);
    }
    if (tu_hdr(s->universe, atom_id))
        return NULL;
    Atom *atom = term_universe_get_atom(s->universe, atom_id);
    if (atom && atom->kind == ATOM_EXPR && atom->expr.len == 3 &&
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.colon))
        return atom->expr.elems[1];
    return NULL;
}

static void eq_index_rebuild(Space *s) {
    space_linearize(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD);
    eq_index_free(&s->eq_idx);
    eq_index_init(&s->eq_idx);
    for (uint32_t i = 0; i < s->len; i++) {
        Atom *lhs, *rhs;
        AtomId atom_id = space_get_atom_id_at(s, i);
        if (space_equation_children_at_id(s, atom_id, &lhs, &rhs))
            eq_index_add(&s->eq_idx, lhs, i);
    }
    s->eq_idx_dirty = false;
}

static void ty_ann_index_rebuild(Space *s) {
    space_linearize(s);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TY_INDEX_REBUILD);
    ty_ann_index_free(&s->ty_idx);
    ty_ann_index_init(&s->ty_idx);
    for (uint32_t i = 0; i < s->len; i++) {
        AtomId atom_id = space_get_atom_id_at(s, i);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
            ty_ann_index_add_id(&s->ty_idx, s, subject_id, i);
            continue;
        }
        Atom *subject = space_type_annotation_subject_at_id(s, atom_id);
        if (subject)
            ty_ann_index_add(&s->ty_idx, subject, i);
    }
    s->ty_idx_dirty = false;
}

static void exact_index_rebuild(Space *s) {
    space_linearize(s);
    exact_index_free(&s->exact_idx);
    exact_index_init(&s->exact_idx);
    for (uint32_t i = 0; i < s->len; i++) {
        AtomId atom_id = space_get_atom_id_at(s, i);
        if (!atom_id_is_exact_indexable(s, atom_id))
            continue;
        exact_atom_bucket_add(&s->exact_idx.buckets[exact_atom_hash_id(s, atom_id)], i);
    }
    s->exact_idx_dirty = false;
}

static void space_bump_revision(Space *s) {
    if (!s)
        return;
    s->revision++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_REVISION_BUMP);
}

void space_add(Space *s, Atom *atom) {
    AtomId atom_id = CETTA_ATOM_ID_NONE;
    Atom *backend_atom = atom;
    bool queue_gap = space_is_queue(s) && s->start != 0;
    if (space_tracks_atom_ids(s) && atom) {
        atom_id = term_universe_store_atom_id(s->universe, NULL, atom);
        if (atom_id == CETTA_ATOM_ID_NONE)
            return;
    }
    bool backend_needs_atom =
        !queue_gap && space_match_backend_needs_atom_on_add(s, atom_id);
    if (backend_needs_atom && space_tracks_atom_ids(s) && atom_id != CETTA_ATOM_ID_NONE) {
        backend_atom = term_universe_get_atom(s->universe, atom_id);
        if (!backend_atom)
            return;
    }
    uint32_t idx = s->len;
    if (space_is_queue(s)) {
        space_queue_reserve_tail(s, 1);
        s->atom_ids[s->start + s->len] = atom_id;
    } else {
        space_reserve_linear(s, s->len + 1);
        s->atom_ids[s->len] = atom_id;
    }
    s->len++;
    space_bump_revision(s);
    if (queue_gap) {
        space_mark_indexes_dirty(s);
        space_match_backend_note_remove(s);
        return;
    }
    /* Index equations by head symbol */
    Atom *lhs, *rhs;
    if (!s->eq_idx_dirty && space_equation_children_at_id(s, atom_id, &lhs, &rhs))
        eq_index_add(&s->eq_idx, lhs, idx);
    /* Index type annotations (: atom type) */
    if (!s->ty_idx_dirty) {
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, atom_id, &subject_id, &type_id)) {
            ty_ann_index_add_id(&s->ty_idx, s, subject_id, idx);
        } else {
            Atom *subject = space_type_annotation_subject_at_id(s, atom_id);
            if (subject)
                ty_ann_index_add(&s->ty_idx, subject, idx);
        }
    }
    if (!s->exact_idx_dirty && atom_id_is_exact_indexable(s, atom_id))
        exact_atom_bucket_add(&s->exact_idx.buckets[exact_atom_hash_id(s, atom_id)], idx);
    /* Match backend owns its own incremental indexing policy. */
    space_match_backend_note_add(s, atom_id,
                                 backend_needs_atom ? backend_atom : NULL, idx);
}

Space *space_heap_clone_shallow(Space *src) {
    Space *clone = cetta_malloc(sizeof(Space));
    space_init_with_universe(clone, src ? src->universe : NULL);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (uint32_t i = 0; i < src->len; i++) {
        space_add(clone, space_get_at(src, i));
    }
    return clone;
}

void space_replace_contents(Space *dst, Space *src) {
    uint64_t old_revision = dst ? dst->revision : 0;
    uint64_t src_revision = src ? src->revision : 0;
    space_free(dst);
    *dst = *src;
    dst->revision = (old_revision > src_revision ? old_revision : src_revision) + 1;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_REVISION_BUMP);
    src->atom_ids = NULL;
    src->start = 0;
    src->len = 0;
    src->cap = 0;
    src->kind = SPACE_KIND_ATOM;
    src->universe = space_default_universe();
    src->revision = 0;
    eq_index_init(&src->eq_idx);
    ty_ann_index_init(&src->ty_idx);
    exact_index_init(&src->exact_idx);
    src->eq_idx_dirty = false;
    src->ty_idx_dirty = false;
    src->exact_idx_dirty = false;
    space_match_backend_init(src);
}

/* ── Query Results ──────────────────────────────────────────────────────── */

void query_results_init(QueryResults *qr) {
    qr->items = NULL;
    qr->len = 0;
    qr->cap = 0;
}

void query_results_push(QueryResults *qr, Atom *result, Bindings *b) {
    if (qr->len >= qr->cap) {
        qr->cap = qr->cap ? qr->cap * 2 : 8;
        qr->items = cetta_realloc(qr->items, sizeof(QueryResult) * qr->cap);
    }
    qr->items[qr->len].result = result;
    if (!bindings_clone(&qr->items[qr->len].bindings, b))
        return;
    qr->len++;
}

void query_results_push_move(QueryResults *qr, Atom *result, Bindings *b) {
    if (qr->len >= qr->cap) {
        qr->cap = qr->cap ? qr->cap * 2 : 8;
        qr->items = cetta_realloc(qr->items, sizeof(QueryResult) * qr->cap);
    }
    qr->items[qr->len].result = result;
    bindings_move(&qr->items[qr->len].bindings, b);
    qr->len++;
}

uint32_t query_results_visit(const QueryResults *qr, QueryResultVisitor visitor,
                             void *ctx) {
    uint32_t visited = 0;
    if (!qr || !visitor)
        return 0;
    for (uint32_t i = 0; i < qr->len; i++) {
        visited++;
        if (!visitor(qr->items[i].result, &qr->items[i].bindings, ctx))
            break;
    }
    return visited;
}

void query_results_free(QueryResults *qr) {
    for (uint32_t i = 0; i < qr->len; i++)
        bindings_free(&qr->items[i].bindings);
    free(qr->items);
    qr->items = NULL;
    qr->len = 0;
    qr->cap = 0;
}

typedef struct {
    VarId var_id;
    SymbolId spelling;
} QueryVisibleVar;

#define QUERY_VISIBLE_INLINE_CAP 8

typedef struct {
    QueryVisibleVar inline_items[QUERY_VISIBLE_INLINE_CAP];
    QueryVisibleVar *items;
    uint32_t len;
    uint32_t cap;
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
                                          uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t next_cap = set->cap ? set->cap : QUERY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    QueryVisibleVar *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(QueryVisibleVar) * next_cap)
        : cetta_realloc(set->items, sizeof(QueryVisibleVar) * next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(QueryVisibleVar) * set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool query_visible_var_set_add(QueryVisibleVarSet *set, VarId var_id,
                                      SymbolId spelling) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].var_id == var_id)
            return true;
    }
    if (!query_visible_var_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].var_id = var_id;
    set->items[set->len].spelling = spelling;
    set->len++;
    return true;
}

static bool query_visible_var_set_contains(const QueryVisibleVarSet *set,
                                           VarId var_id) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].var_id == var_id)
            return true;
    }
    return false;
}

static bool collect_query_visible_vars_rec(Atom *atom, QueryVisibleVarSet *set) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR)
        return query_visible_var_set_add(set, atom->var_id, atom->sym_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
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
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!atom_refs_only_query_visible_vars(atom->expr.elems[i], visible))
            return false;
    }
    return true;
}

static Atom *bindings_apply_without_self_id(Bindings *full, Arena *a,
                                            VarId skip_id, Atom *value) {
    if (!value || !atom_has_vars(value) || !full || full->len == 0)
        return value;
    Bindings reduced;
    if (!bindings_clone(&reduced, full))
        return bindings_apply(full, a, value);
    bool removed = false;
    for (uint32_t i = 0; i < reduced.len; i++) {
        if (reduced.entries[i].var_id != skip_id)
            continue;
        for (uint32_t j = i + 1; j < reduced.len; j++)
            reduced.entries[j - 1] = reduced.entries[j];
        reduced.len--;
        reduced.lookup_cache_count = 0;
        reduced.lookup_cache_next = 0;
        removed = true;
        break;
    }
    Atom *resolved = removed ? bindings_apply(&reduced, a, value)
                             : bindings_apply(full, a, value);
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

    Atom *slot_var = atom_var_with_spelling(a, wanted->spelling, wanted->var_id);
    Atom *resolved = bindings_apply((Bindings *)full, a, slot_var);
    if (resolved != slot_var)
        return resolved;

    uint32_t wanted_base = var_base_id(wanted->var_id);
    if (wanted_base == 0)
        return slot_var;
    for (uint32_t i = full->len; i > 0; i--) {
        const Binding *entry = &full->entries[i - 1];
        if (var_base_id(entry->var_id) != wanted_base)
            continue;
        if (entry->spelling != wanted->spelling)
            continue;
        if (!atom_has_vars(entry->val))
            return entry->val;
        return bindings_apply_without_self_id((Bindings *)full, a,
                                              entry->var_id, entry->val);
    }
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
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == visible->items[i].var_id &&
            resolved->sym_id == visible->items[i].spelling) {
            continue;
        }
        if (!bindings_add_id(projected, visible->items[i].var_id,
                             visible->items[i].spelling, resolved)) {
            bindings_free(projected);
            return false;
        }
    }

    for (uint32_t i = 0; i < full->eq_len; i++) {
        Atom *lhs = bindings_apply((Bindings *)full, a, full->constraints[i].lhs);
        Atom *rhs = bindings_apply((Bindings *)full, a, full->constraints[i].rhs);
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

void registry_init(Registry *r) { r->len = 0; }

void registry_bind_id(Registry *r, SymbolId key, Atom *value) {
    if (!r || key == SYMBOL_ID_NONE) return;
    /* Update existing or add new */
    for (uint32_t i = 0; i < r->len; i++) {
        if (r->entries[i].key == key) {
            r->entries[i].value = value;
            return;
        }
    }
    if (r->len < MAX_REGISTRY) {
        r->entries[r->len].key = key;
        r->entries[r->len].value = value;
        r->len++;
    }
}

Atom *registry_lookup_id(Registry *r, SymbolId key) {
    if (!r || key == SYMBOL_ID_NONE) return NULL;
    for (uint32_t i = 0; i < r->len; i++)
        if (r->entries[i].key == key)
            return r->entries[i].value;
    return NULL;
}

void registry_bind(Registry *r, const char *name, Atom *value) {
    registry_bind_id(r, symbol_intern_cstr(g_symbols, name), value);
}

Atom *registry_lookup(Registry *r, const char *name) {
    return registry_lookup_id(r, symbol_intern_cstr(g_symbols, name));
}

Space *resolve_space(Registry *r, Atom *ref) {
    /* Grounded space atom → direct pointer */
    if (ref->kind == ATOM_GROUNDED && ref->ground.gkind == GV_SPACE)
        return (Space *)ref->ground.ptr;
    /* Symbol like &self → registry lookup */
    if (ref->kind == ATOM_SYMBOL) {
        Atom *val = registry_lookup_id(r, ref->sym_id);
        if (val && val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE)
            return (Space *)val->ground.ptr;
    }
    return NULL;
}

bool space_remove(Space *s, Atom *atom) {
    if (!s)
        return false;
    if (space_is_queue(s))
        space_linearize(s);
    for (uint32_t i = 0; i < s->len; i++) {
        if (atom_eq(space_get_at(s, i), atom)) {
            if (space_is_ordered(s)) {
                for (uint32_t j = i + 1; j < s->len; j++) {
                    s->atom_ids[j - 1] = s->atom_ids[j];
                }
                s->len--;
            } else {
                s->atom_ids[i] = s->atom_ids[--s->len]; /* swap with last */
            }
            space_mark_indexes_dirty(s);
            space_match_backend_note_remove(s);
            space_bump_revision(s);
            return true;
        }
    }
    return false;
}

AtomId space_get_atom_id_at(const Space *s, uint32_t idx) {
    if (!s || idx >= s->len)
        return CETTA_ATOM_ID_NONE;
    return s->atom_ids[space_physical_index(s, idx)];
}

Atom *space_get_at(const Space *s, uint32_t idx) {
    AtomId id = space_get_atom_id_at(s, idx);
    return term_universe_get_atom(s ? s->universe : NULL, id);
}

Atom *space_peek(const Space *s) {
    if (!s || s->len == 0) return NULL;
    return space_get_at(s, space_is_queue(s) ? 0 : (s->len - 1));
}

bool space_pop(Space *s, Atom **out) {
    Atom *top = space_peek(s);
    if (!top) return false;
    if (out) *out = top;
    if (space_is_queue(s)) {
        s->start++;
        s->len--;
        if (s->len == 0)
            s->start = 0;
    } else {
        s->len--;
    }
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

bool space_truncate(Space *s, uint32_t new_len) {
    if (!s || new_len > s->len) return false;
    if (new_len == s->len) return true;
    s->len = new_len;
    if (s->len == 0)
        s->start = 0;
    space_mark_indexes_dirty(s);
    space_match_backend_note_remove(s);
    space_bump_revision(s);
    return true;
}

uint32_t space_length(const Space *s) {
    return space_match_backend_logical_len(s);
}

uint32_t space_exact_match_indices(Space *s, Atom *atom, uint32_t **out) {
    *out = NULL;
    if (!space_is_hash(s) || !atom || atom_has_variables(atom) || !atom_is_exact_indexable(atom))
        return 0;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_LOOKUP);
    ensure_exact_index(s);
    ExactAtomBucket *bucket = &s->exact_idx.buckets[exact_atom_hash(atom)];
    if (bucket->len == 0)
        return 0;
    AtomId query_id = term_universe_lookup_atom_id(s ? s->universe : NULL, atom);
    uint32_t *matches = cetta_malloc(sizeof(uint32_t) * bucket->len);
    uint32_t n = 0;
    for (uint32_t i = 0; i < bucket->len; i++) {
        uint32_t idx = bucket->indices[i];
        if (idx >= s->len) continue;
        AtomId candidate_id = space_get_atom_id_at(s, idx);
        if (candidate_id == CETTA_ATOM_ID_NONE)
            continue;
        if ((query_id != CETTA_ATOM_ID_NONE && candidate_id == query_id) ||
            term_universe_atom_id_eq(s->universe, candidate_id, atom))
            matches[n++] = idx;
    }
    if (n == 0) {
        free(matches);
        return 0;
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_HASH_SPACE_EXACT_HIT, n);
    *out = matches;
    return n;
}

bool space_contains_exact(Space *s, Atom *atom) {
    uint32_t *matches = NULL;
    uint32_t n = space_exact_match_indices(s, atom, &matches);
    free(matches);
    return n > 0;
}

/* ── Type Expression Normalization ───────────────────────────────────────── */

/* Recursively evaluate grounded arithmetic in type expressions.
   E.g., (VecN String (+ (+ 0 1) 1)) → (VecN String 2) */
static Atom *normalize_type_expr(Arena *a, Atom *ty) {
    if (ty->kind != ATOM_EXPR || ty->expr.len < 2) return ty;
    /* First normalize children */
    Atom **new_elems = arena_alloc(a, sizeof(Atom *) * ty->expr.len);
    bool changed = false;
    for (uint32_t i = 0; i < ty->expr.len; i++) {
        new_elems[i] = normalize_type_expr(a, ty->expr.elems[i]);
        if (new_elems[i] != ty->expr.elems[i]) changed = true;
    }
    Atom *norm = changed ? atom_expr(a, new_elems, ty->expr.len) : ty;
    /* Try grounded dispatch on the normalized expression */
    SymbolId op_id = SYMBOL_ID_NONE;
    if (norm->expr.elems[0]->kind == ATOM_SYMBOL) {
        op_id = norm->expr.elems[0]->sym_id;
    }
    if (norm->expr.len >= 3 && op_id != SYMBOL_ID_NONE && is_grounded_op(op_id)) {
        Atom *result = grounded_dispatch(a, norm->expr.elems[0],
            norm->expr.elems + 1, norm->expr.len - 1);
        if (result) return result;
    }
    return norm;
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
            } else if (space->match_backend.kind == SPACE_ENGINE_PATHMAP) {
                space_type = "pathmap";
            } else if (space->match_backend.kind == SPACE_ENGINE_MORK) {
                space_type = "mork";
            } else if (space_is_hash(space)) {
                space_type = "hash";
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
    }
    return atom_undefined_type(a);
}

/* Scan space for (: atom type) annotations */
static uint32_t get_annotated_types(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types) {
    /* Use type annotation index for O(bucket_size) instead of O(N) */
    ensure_ty_ann_index(s);
    uint32_t h = atom_hash_for_index(atom);
    TypeAnnBucket *bucket = &s->ty_idx.buckets[h];
    Atom **types = NULL;
    uint32_t count = 0, cap = 0;
    for (uint32_t i = 0; i < bucket->len; i++) {
        uint32_t idx = bucket->atom_indices[i];
        if (idx >= s->len)
            continue;
        AtomId annotation_id = space_get_atom_id_at(s, idx);
        AtomId subject_id = CETTA_ATOM_ID_NONE;
        AtomId type_id = CETTA_ATOM_ID_NONE;
        if (space_type_annotation_child_ids_at_id(s, annotation_id,
                                                  &subject_id, &type_id)) {
            if (term_universe_atom_id_eq(s->universe, subject_id, atom)) {
                Atom *type_copy = term_universe_copy_atom_epoch(
                    s->universe, a, type_id, fresh_var_suffix());
                if (type_copy) {
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
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            types = cetta_realloc(types, sizeof(Atom *) * cap);
        }
        types[count++] = rename_vars(a, annotation->expr.elems[2], fresh_var_suffix());
    }
    *out_types = types;
    return count;
}

uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types) {
    uint32_t count = 0;
    Atom **types = NULL;
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
        count = get_annotated_types(s, a, atom, &types);
        break;
    case ATOM_EXPR:
        count = get_annotated_types(s, a, atom, &types);
        /* Also try to infer type from operator's function type */
        if (count == 0 && atom->expr.len >= 2) {
            Atom *op = atom->expr.elems[0];
            Atom **op_types = NULL;
            uint32_t nop = get_annotated_types(s, a, op, &op_types);
            Arena scratch;
            arena_init(&scratch);
            arena_set_hashcons(&scratch, NULL);
            /* Also try recursively inferred types for the operator */
            if (nop == 0 && op->kind == ATOM_EXPR) {
                Atom **recur_types = NULL;
                nop = get_atom_types(s, a, op, &recur_types);
                /* Filter: only keep function types */
                op_types = NULL;
                uint32_t nfunc = 0;
                for (uint32_t ri = 0; ri < nop; ri++) {
                    if (recur_types[ri]->kind == ATOM_EXPR && recur_types[ri]->expr.len >= 3 &&
                        atom_is_symbol_id(recur_types[ri]->expr.elems[0], g_builtin_syms.arrow)) {
                        op_types = cetta_realloc(op_types, sizeof(Atom *) * (nfunc + 1));
                        op_types[nfunc++] = recur_types[ri];
                    }
                }
                free(recur_types);
                nop = nfunc;
            }
            bool tried_func_type = false;
            for (uint32_t oi = 0; oi < nop; oi++) {
                Atom *ft = op_types[oi];
                /* Check if it's a function type (-> ...) */
                if (ft->kind == ATOM_EXPR && ft->expr.len >= 3 &&
                    atom_is_symbol_id(ft->expr.elems[0], g_builtin_syms.arrow)) {
                    tried_func_type = true;
                    /* Check arity match */
                    if (ft->expr.len - 2 != atom->expr.len - 1) continue;
                    /* Freshen type vars and try to unify args to get concrete ret type */
                    ArenaMark scratch_mark = arena_mark(&scratch);
                    uint32_t tsuf = fresh_var_suffix();
                    Atom *fresh_ft = rename_vars(&scratch, ft, tsuf);
                    Atom *fresh_ret = fresh_ft->expr.elems[fresh_ft->expr.len - 1];
                    Bindings tb;
                    bindings_init(&tb);
                    bool all_ok = true;
                    for (uint32_t ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
                        /* Apply accumulated bindings to resolve type vars from earlier args */
                        Atom *arg_type_decl =
                            bindings_apply(&tb, &scratch, fresh_ft->expr.elems[ai + 1]);
                        Atom **atypes = NULL;
                        uint32_t nat = get_atom_types(s, a, atom->expr.elems[ai + 1], &atypes);
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
                            ChoicePoint point = search_context_save(&trial_context);
                            if (match_types_builder(arg_type_decl, atypes[ti],
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
                            &scratch, bindings_apply(&tb, &scratch, fresh_ret));
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
            if (tried_func_type && count == 0) {
                *out_types = NULL;
                return 0;  /* empty = ill-typed */
            }
        }
        break;
    }

    /* If no types found, return [%Undefined%] */
    if (count == 0) {
        types = cetta_malloc(sizeof(Atom *));
        types[0] = atom_undefined_type(a);
        count = 1;
    }
    *out_types = types;
    return count;
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

static void query_bucket_legacy(Space *s, EqBucket *bucket, Atom *query,
                                const QueryVisibleVarSet *visible, Arena *a,
                                QueryResults *out) {
    SymbolId query_head = eq_head_symbol(query);
    if (bucket->trie && bucket->len > 4) {
        uint32_t *candidates = NULL;
        uint32_t ncand = 0, ccand = 0;
        uint32_t considered = 0;
        disc_lookup(bucket->trie, query, &candidates, &ncand, &ccand);
        for (uint32_t ci = 0; ci < ncand; ci++) {
            uint32_t i = candidates[ci];
            if (i >= bucket->len) continue;
            uint32_t atom_idx = bucket->atom_indices[i];
            if (atom_idx >= s->len)
                continue;
            AtomId equation_id = space_get_atom_id_at(s, atom_idx);
            Atom *lhs = NULL, *rhs = NULL;
            if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
                continue;
            SymbolId lhs_head = eq_head_symbol(lhs);
            if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
                lhs_head != query_head) {
                continue;
            }
            considered++;
            uint32_t suffix = fresh_var_suffix();
            Atom *rlhs = rename_vars(a, lhs, suffix);
            Atom *rrhs = rename_vars(a, rhs, suffix);
            Bindings b;
            bindings_init(&b);
            if (match_atoms(rlhs, query, &b) && !bindings_has_loop(&b)) {
                Atom *result = bindings_apply(&b, a, rrhs);
                Bindings projected;
                if (project_query_visible_bindings(a, visible, &b, &projected)) {
                    query_results_push_move(out, result, &projected);
                }
                bindings_free(&projected);
            }
            bindings_free(&b);
        }
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                                considered);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
        free(candidates);
        return;
    }

    uint32_t considered = 0;
    for (uint32_t i = 0; i < bucket->len; i++) {
        uint32_t atom_idx = bucket->atom_indices[i];
        if (atom_idx >= s->len)
            continue;
        AtomId equation_id = space_get_atom_id_at(s, atom_idx);
        Atom *lhs = NULL, *rhs = NULL;
        if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
            continue;
        SymbolId lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        uint32_t suffix = fresh_var_suffix();
        Atom *rlhs = rename_vars(a, lhs, suffix);
        Atom *rrhs = rename_vars(a, rhs, suffix);
        Bindings b;
        bindings_init(&b);
        if (match_atoms(rlhs, query, &b) && !bindings_has_loop(&b)) {
            Atom *result = bindings_apply(&b, a, rrhs);
            Bindings projected;
            if (project_query_visible_bindings(a, visible, &b, &projected)) {
                query_results_push_move(out, result, &projected);
            }
            bindings_free(&projected);
        }
        bindings_free(&b);
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_LEGACY_CANDIDATES, considered);
}

/* Try matching equations from a bucket against a query.
   Large buckets reuse the substitution-tree epoch path to avoid per-candidate
   rename_vars on both sides of each equation. */
static void query_bucket(Space *s, EqBucket *bucket, Atom *query,
                         const QueryVisibleVarSet *visible, Arena *a,
                         QueryResults *out) {
    SymbolId query_head = eq_head_symbol(query);
    if (!bucket || bucket->len == 0)
        return;
    if (bucket->subst.count <= 4 || !bucket->subst.root ||
        !bucket->subst_safe || !atom_is_eq_subst_safe(query)) {
        query_bucket_legacy(s, bucket, query, visible, a, out);
        return;
    }

    uint32_t out_before = out->len;
    SubstMatchSet matches;
    uint32_t considered = 0;
    smset_init(&matches);
    stree_query_bucket(&bucket->subst, a, query, NULL, &matches);
    for (uint32_t mi = 0; mi < matches.len; mi++) {
        const SubstMatch *sm = &matches.items[mi];
        if (sm->atom_idx >= bucket->len)
            continue;
        uint32_t atom_idx = bucket->atom_indices[sm->atom_idx];
        if (atom_idx >= s->len)
            continue;
        AtomId equation_id = space_get_atom_id_at(s, atom_idx);
        Atom *lhs = NULL, *rhs = NULL;
        if (!space_equation_children_at_id(s, equation_id, &lhs, &rhs))
            continue;
        SymbolId lhs_head = eq_head_symbol(lhs);
        if (query_head != SYMBOL_ID_NONE && lhs_head != SYMBOL_ID_NONE &&
            lhs_head != query_head) {
            continue;
        }
        considered++;
        bool emitted = false;
        Bindings merged;
        if (!bindings_clone(&merged, &sm->bindings))
            continue;
        if (match_atoms_epoch(query, lhs, &merged, a, sm->epoch) &&
            !bindings_has_loop(&merged)) {
            Atom *result = bindings_apply_epoch(&merged, a, rhs, sm->epoch);
            Bindings projected;
            if (project_query_visible_bindings(a, visible, &merged, &projected)) {
                query_results_push_move(out, result, &projected);
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_EMITTED);
                emitted = true;
            }
            bindings_free(&projected);
        }
        bindings_free(&merged);
        if (!emitted) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_CANDIDATE_FALLBACK);
            uint32_t suffix = fresh_var_suffix();
            Atom *rlhs = rename_vars(a, lhs, suffix);
            Atom *rrhs = rename_vars(a, rhs, suffix);
            Bindings exact;
            bindings_init(&exact);
            if (match_atoms(rlhs, query, &exact) && !bindings_has_loop(&exact)) {
                Atom *result = bindings_apply(&exact, a, rrhs);
                Bindings projected;
                if (project_query_visible_bindings(a, visible, &exact, &projected)) {
                    query_results_push_move(out, result, &projected);
                }
                bindings_free(&projected);
            }
            bindings_free(&exact);
        }
    }
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                            considered);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_CANDIDATES, considered);
    smset_free(&matches);
    if (out->len == out_before) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_QUERY_EQUATION_SUBST_BUCKET_FALLBACK);
        query_bucket_legacy(s, bucket, query, visible, a, out);
    }
}

void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS);
    ensure_eq_index(s);
    QueryVisibleVarSet visible;
    query_visible_var_set_init(&visible);
    if (!collect_query_visible_vars_rec(query, &visible)) {
        query_visible_var_set_free(&visible);
        return;
    }
    /* Use head-symbol index for O(1) lookup instead of O(N) scan.
       This is the key optimization from Vampire's LiteralIndex. */
    SymbolId head = eq_head_symbol(query);
    if (head != SYMBOL_ID_NONE) {
        /* Query has a known head symbol — look up matching bucket */
        query_bucket(s, &s->eq_idx.buckets[symbol_hash(head)], query, &visible, a, out);
    }
    /* Non-symbol-headed queries may still match wildcard equations whose LHS
       head is itself a variable or complex term, but they must not unlock
       every named equation bucket by unifying the head variable with an
       unrelated function symbol. HE treats ($f x) as data unless a wildcard
       equation explicitly matches it. */
    query_bucket(s, &s->eq_idx.wildcard, query, &visible, a, out);
    query_visible_var_set_free(&visible);
}

bool space_equations_may_match_known_head(Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE)
        return true;
    ensure_eq_index(s);
    if (s->eq_idx.wildcard.len > 0)
        return true;
    return s->eq_idx.buckets[symbol_hash(head)].len > 0;
}
