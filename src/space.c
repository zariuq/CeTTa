#include "space.h"
#include "grounded.h"
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
    for (uint32_t i = 0; i < n->nsym; i++) disc_node_free(n->sym[i].child);
    free(n->sym);
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

static DiscNode *disc_get_sym(DiscNode *n, const char *key) {
    for (uint32_t i = 0; i < n->nsym; i++)
        if (n->sym[i].key == key || strcmp(n->sym[i].key, key) == 0) return n->sym[i].child;
    if (n->nsym >= n->csym) {
        n->csym = n->csym ? n->csym * 2 : 4;
        n->sym = cetta_realloc(n->sym, sizeof(n->sym[0]) * n->csym);
    }
    DiscNode *child = disc_node_new();
    n->sym[n->nsym].key = key;
    n->sym[n->nsym].child = child;
    n->nsym++;
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
    case ATOM_SYMBOL: return disc_get_sym(node, a->name);
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

void disc_insert(DiscNode *root, Atom *lhs, uint32_t eq_idx) {
    DiscNode *leaf = disc_insert_atom(root, lhs);
    disc_add_leaf(leaf, eq_idx);
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
    for (uint32_t i = 0; i < node->nsym; i++)
        dns_push(next, node->sym[i].child);
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
        for (uint32_t i = 0; i < node->nsym; i++)
            if (strcmp(node->sym[i].key, q->name) == 0)
                dns_push(next, node->sym[i].child);
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

static void eq_bucket_init(EqBucket *b) {
    b->lhs = NULL; b->rhs = NULL; b->len = 0; b->cap = 0;
    b->trie = NULL;
}

static void eq_bucket_add(EqBucket *b, Atom *lhs, Atom *rhs) {
    uint32_t idx = b->len;
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 8;
        b->lhs = cetta_realloc(b->lhs, sizeof(Atom *) * b->cap);
        b->rhs = cetta_realloc(b->rhs, sizeof(Atom *) * b->cap);
    }
    b->lhs[b->len] = lhs;
    b->rhs[b->len] = rhs;
    b->len++;
    /* Add to discrimination trie */
    if (!b->trie) b->trie = disc_node_new();
    disc_insert(b->trie, lhs, idx);
}

static void eq_bucket_free(EqBucket *b) {
    free(b->lhs); free(b->rhs);
    disc_node_free(b->trie); b->trie = NULL;
    b->lhs = NULL; b->rhs = NULL; b->len = 0; b->cap = 0;
}

static uint32_t symbol_hash(const char *name) {
    uint32_t h = 5381;
    for (const char *p = name; *p; p++)
        h = ((h << 5) + h) ^ (uint32_t)*p;
    return h % EQ_INDEX_BUCKETS;
}

/* Get the head symbol of an equation LHS for indexing.
   Returns NULL if the head is not a symbol (variable, complex expr). */
static const char *eq_head_symbol(Atom *lhs) {
    if (lhs->kind == ATOM_SYMBOL) return lhs->name;
    if (lhs->kind == ATOM_EXPR && lhs->expr.len > 0 &&
        lhs->expr.elems[0]->kind == ATOM_SYMBOL)
        return lhs->expr.elems[0]->name;
    return NULL;
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

static void eq_index_add(EqIndex *idx, Atom *lhs, Atom *rhs) {
    const char *head = eq_head_symbol(lhs);
    if (head) {
        eq_bucket_add(&idx->buckets[symbol_hash(head)], lhs, rhs);
    } else {
        eq_bucket_add(&idx->wildcard, lhs, rhs);
    }
}

/* ── Type Annotation Index ──────────────────────────────────────────────── */

static void ty_ann_bucket_init(TypeAnnBucket *b) {
    b->annotated_atoms = NULL; b->types = NULL; b->len = 0; b->cap = 0;
}
static void ty_ann_bucket_add(TypeAnnBucket *b, Atom *ann_atom, Atom *type) {
    if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4;
        b->annotated_atoms = cetta_realloc(b->annotated_atoms, sizeof(Atom *) * b->cap);
        b->types = cetta_realloc(b->types, sizeof(Atom *) * b->cap);
    }
    b->annotated_atoms[b->len] = ann_atom;
    b->types[b->len] = type;
    b->len++;
}
static void ty_ann_bucket_free(TypeAnnBucket *b) {
    free(b->annotated_atoms); free(b->types);
    b->annotated_atoms = NULL; b->types = NULL; b->len = 0; b->cap = 0;
}

static uint32_t atom_hash_for_index(Atom *a) {
    /* Hash by name for symbols, or by first element for expressions */
    if (a->kind == ATOM_SYMBOL) return symbol_hash(a->name);
    if (a->kind == ATOM_EXPR && a->expr.len > 0 && a->expr.elems[0]->kind == ATOM_SYMBOL)
        return symbol_hash(a->expr.elems[0]->name);
    return 0;
}

static void ty_ann_index_init(TypeAnnIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        ty_ann_bucket_init(&idx->buckets[i]);
}
static void ty_ann_index_free(TypeAnnIndex *idx) {
    for (uint32_t i = 0; i < EQ_INDEX_BUCKETS; i++)
        ty_ann_bucket_free(&idx->buckets[i]);
}
static void ty_ann_index_add(TypeAnnIndex *idx, Atom *ann_atom, Atom *type) {
    uint32_t h = atom_hash_for_index(ann_atom);
    ty_ann_bucket_add(&idx->buckets[h], ann_atom, type);
}

/* ── Space ──────────────────────────────────────────────────────────────── */

void space_init(Space *s) {
    s->atoms = NULL;
    s->len = 0;
    s->cap = 0;
    eq_index_init(&s->eq_idx);
    ty_ann_index_init(&s->ty_idx);
    space_match_backend_init(s);
}

void space_free(Space *s) {
    free(s->atoms);
    s->atoms = NULL;
    s->len = 0;
    s->cap = 0;
    eq_index_free(&s->eq_idx);
    ty_ann_index_free(&s->ty_idx);
    space_match_backend_free(s);
}

static bool is_equation_atom(Atom *a, Atom **lhs_out, Atom **rhs_out) {
    if (a->kind != ATOM_EXPR || a->expr.len != 3) return false;
    if (!atom_is_symbol(a->expr.elems[0], "=")) return false;
    *lhs_out = a->expr.elems[1];
    *rhs_out = a->expr.elems[2];
    return true;
}

static void eq_index_rebuild(Space *s) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EQ_INDEX_REBUILD);
    eq_index_free(&s->eq_idx);
    eq_index_init(&s->eq_idx);
    for (uint32_t i = 0; i < s->len; i++) {
        Atom *lhs, *rhs;
        if (is_equation_atom(s->atoms[i], &lhs, &rhs))
            eq_index_add(&s->eq_idx, lhs, rhs);
    }
}

static void ty_ann_index_rebuild(Space *s) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TY_INDEX_REBUILD);
    ty_ann_index_free(&s->ty_idx);
    ty_ann_index_init(&s->ty_idx);
    for (uint32_t i = 0; i < s->len; i++) {
        Atom *atom = s->atoms[i];
        if (atom->kind == ATOM_EXPR && atom->expr.len == 3 &&
            atom_is_symbol(atom->expr.elems[0], ":"))
            ty_ann_index_add(&s->ty_idx, atom->expr.elems[1], atom->expr.elems[2]);
    }
}

void space_add(Space *s, Atom *atom) {
    if (s->len >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->atoms = cetta_realloc(s->atoms, sizeof(Atom *) * s->cap);
    }
    uint32_t idx = s->len;
    s->atoms[s->len++] = atom;
    /* Index equations by head symbol */
    Atom *lhs, *rhs;
    if (is_equation_atom(atom, &lhs, &rhs))
        eq_index_add(&s->eq_idx, lhs, rhs);
    /* Index type annotations (: atom type) */
    if (atom->kind == ATOM_EXPR && atom->expr.len == 3 &&
        atom_is_symbol(atom->expr.elems[0], ":"))
        ty_ann_index_add(&s->ty_idx, atom->expr.elems[1], atom->expr.elems[2]);
    /* Match backend owns its own incremental indexing policy. */
    space_match_backend_note_add(s, atom, idx);
}

Space *space_heap_clone_shallow(Space *src) {
    Space *clone = cetta_malloc(sizeof(Space));
    space_init(clone);
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (uint32_t i = 0; i < src->len; i++) {
        space_add(clone, src->atoms[i]);
    }
    return clone;
}

void space_replace_contents(Space *dst, Space *src) {
    space_free(dst);
    *dst = *src;
    src->atoms = NULL;
    src->len = 0;
    src->cap = 0;
    eq_index_init(&src->eq_idx);
    ty_ann_index_init(&src->ty_idx);
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

void query_results_free(QueryResults *qr) {
    for (uint32_t i = 0; i < qr->len; i++)
        bindings_free(&qr->items[i].bindings);
    free(qr->items);
    qr->items = NULL;
    qr->len = 0;
    qr->cap = 0;
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

/* is_equation_atom defined above in Space section */

/* ── Space Registry ─────────────────────────────────────────────────────── */

void registry_init(Registry *r) { r->len = 0; }

void registry_bind(Registry *r, const char *name, Atom *value) {
    /* Update existing or add new */
    for (uint32_t i = 0; i < r->len; i++) {
        if (strcmp(r->entries[i].name, name) == 0) {
            r->entries[i].value = value;
            return;
        }
    }
    if (r->len < MAX_REGISTRY) {
        r->entries[r->len].name = name;
        r->entries[r->len].value = value;
        r->len++;
    }
}

Atom *registry_lookup(Registry *r, const char *name) {
    for (uint32_t i = 0; i < r->len; i++)
        if (strcmp(r->entries[i].name, name) == 0)
            return r->entries[i].value;
    return NULL;
}

Space *resolve_space(Registry *r, Atom *ref) {
    /* Grounded space atom → direct pointer */
    if (ref->kind == ATOM_GROUNDED && ref->ground.gkind == GV_SPACE)
        return (Space *)ref->ground.ptr;
    /* Symbol like &self → registry lookup */
    if (ref->kind == ATOM_SYMBOL) {
        Atom *val = registry_lookup(r, ref->name);
        if (val && val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE)
            return (Space *)val->ground.ptr;
    }
    return NULL;
}

bool space_remove(Space *s, Atom *atom) {
    for (uint32_t i = 0; i < s->len; i++) {
        if (atom_eq(s->atoms[i], atom)) {
            s->atoms[i] = s->atoms[--s->len]; /* swap with last */
            eq_index_rebuild(s);
            ty_ann_index_rebuild(s);
            space_match_backend_note_remove(s);
            return true;
        }
    }
    return false;
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
    if (norm->expr.len >= 3 && norm->expr.elems[0]->kind == ATOM_SYMBOL &&
        is_grounded_op(norm->expr.elems[0]->name)) {
        Atom *result = grounded_dispatch(a, norm->expr.elems[0],
            norm->expr.elems + 1, norm->expr.len - 1);
        if (result) return result;
    }
    return norm;
}

/* ── Type Lookup ─────────────────────────────────────────────────────────── */

Atom *get_grounded_type(Arena *a, Atom *atom) {
    if (atom->kind != ATOM_GROUNDED) return atom_undefined_type(a);
    switch (atom->ground.gkind) {
    case GV_INT:    return atom_symbol(a, "Number");
    case GV_FLOAT:  return atom_symbol(a, "Number");
    case GV_BOOL:   return atom_symbol(a, "Bool");
    case GV_STRING: return atom_symbol(a, "String");
    case GV_SPACE:  return atom_symbol(a, "SpaceType");
    case GV_FOREIGN:
        return atom_symbol(a, "Foreign");
    case GV_CAPTURE:
        return atom_expr3(a, atom_symbol(a, "->"),
                          atom_atom_type(a), atom_atom_type(a));
    case GV_STATE: {
        StateCell *cell = (StateCell *)atom->ground.ptr;
        if (cell->content_type && !atom_is_symbol(cell->content_type, "%Undefined%"))
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
    uint32_t h = atom_hash_for_index(atom);
    TypeAnnBucket *bucket = &s->ty_idx.buckets[h];
    Atom **types = NULL;
    uint32_t count = 0, cap = 0;
    for (uint32_t i = 0; i < bucket->len; i++) {
        if (!atom_eq(bucket->annotated_atoms[i], atom)) continue;
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            types = cetta_realloc(types, sizeof(Atom *) * cap);
        }
        types[count++] = rename_vars(a, bucket->types[i], fresh_var_suffix());
    }
    *out_types = types;
    return count;
}

uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types) {
    uint32_t count = 0;
    Atom **types = NULL;

    switch (atom->kind) {
    case ATOM_VAR:
        /* Variables have no type → %Undefined% */
        break;
    case ATOM_GROUNDED: {
        Atom *ty = get_grounded_type(a, atom);
        if (!atom_is_symbol(ty, "%Undefined%")) {
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
            /* Also try recursively inferred types for the operator */
            if (nop == 0 && op->kind == ATOM_EXPR) {
                Atom **recur_types = NULL;
                nop = get_atom_types(s, a, op, &recur_types);
                /* Filter: only keep function types */
                op_types = NULL;
                uint32_t nfunc = 0;
                for (uint32_t ri = 0; ri < nop; ri++) {
                    if (recur_types[ri]->kind == ATOM_EXPR && recur_types[ri]->expr.len >= 3 &&
                        atom_is_symbol(recur_types[ri]->expr.elems[0], "->")) {
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
                    atom_is_symbol(ft->expr.elems[0], "->")) {
                    tried_func_type = true;
                    /* Check arity match */
                    if (ft->expr.len - 2 != atom->expr.len - 1) continue;
                    /* Return type is the last element of (-> ... ret) */
                    Atom *ret_type = ft->expr.elems[ft->expr.len - 1];
                    /* Freshen type vars and try to unify args to get concrete ret type */
                    uint32_t tsuf = fresh_var_suffix();
                    Atom *fresh_ft = rename_vars(a, ft, tsuf);
                    Atom *fresh_ret = fresh_ft->expr.elems[fresh_ft->expr.len - 1];
                    Bindings tb;
                    bindings_init(&tb);
                    bool all_ok = true;
                    for (uint32_t ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
                        /* Apply accumulated bindings to resolve type vars from earlier args */
                        Atom *arg_type_decl = bindings_apply(&tb, a, fresh_ft->expr.elems[ai + 1]);
                        Atom **atypes = NULL;
                        uint32_t nat = get_atom_types(s, a, atom->expr.elems[ai + 1], &atypes);
                        bool found = false;
                        for (uint32_t ti = 0; ti < nat; ti++) {
                            if (match_types(arg_type_decl, atypes[ti], &tb)) {
                                found = true;
                                break;
                            }
                        }
                        free(atypes);
                        if (!found) all_ok = false;
                    }
                    if (all_ok) {
                        /* Apply accumulated type bindings to return type,
                           then normalize arithmetic in type expressions */
                        Atom *concrete_ret = normalize_type_expr(a,
                            bindings_apply(&tb, a, fresh_ret));
                        if (count >= 1) {
                            types = cetta_realloc(types, sizeof(Atom *) * (count + 1));
                        } else {
                            types = cetta_malloc(sizeof(Atom *));
                        }
                        types[count++] = concrete_ret;
                    }
                    bindings_free(&tb);
                    (void)ret_type;
                }
            }
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

/* Try matching equations from a bucket against a query.
   Uses discrimination trie to prune candidates when available. */
static void query_bucket(EqBucket *bucket, Atom *query, Arena *a, QueryResults *out) {
    if (bucket->trie && bucket->len > 4) {
        /* Use trie for larger buckets — prune candidates */
        uint32_t *candidates = NULL;
        uint32_t ncand = 0, ccand = 0;
        disc_lookup(bucket->trie, query, &candidates, &ncand, &ccand);
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                                ncand);
        for (uint32_t ci = 0; ci < ncand; ci++) {
            uint32_t i = candidates[ci];
            if (i >= bucket->len) continue;
            uint32_t suffix = fresh_var_suffix();
            Atom *rlhs = rename_vars(a, bucket->lhs[i], suffix);
            Atom *rrhs = rename_vars(a, bucket->rhs[i], suffix);
            Bindings b;
            bindings_init(&b);
            if (match_atoms(rlhs, query, &b) && !bindings_has_loop(&b)) {
                Atom *result = bindings_apply(&b, a, rrhs);
                query_results_push(out, result, &b);
            }
            bindings_free(&b);
        }
        free(candidates);
    } else {
        /* Small bucket: linear scan is fine */
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_QUERY_EQUATION_CANDIDATES,
                                bucket->len);
        for (uint32_t i = 0; i < bucket->len; i++) {
            uint32_t suffix = fresh_var_suffix();
            Atom *rlhs = rename_vars(a, bucket->lhs[i], suffix);
            Atom *rrhs = rename_vars(a, bucket->rhs[i], suffix);
            Bindings b;
            bindings_init(&b);
            if (match_atoms(rlhs, query, &b) && !bindings_has_loop(&b)) {
                Atom *result = bindings_apply(&b, a, rrhs);
                query_results_push(out, result, &b);
            }
            bindings_free(&b);
        }
    }
}

void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out) {
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EQUATIONS);
    /* Use head-symbol index for O(1) lookup instead of O(N) scan.
       This is the key optimization from Vampire's LiteralIndex. */
    const char *head = eq_head_symbol(query);
    if (head) {
        /* Query has a known head symbol — look up matching bucket */
        query_bucket(&s->eq_idx.buckets[symbol_hash(head)], query, a, out);
    }
    /* Non-symbol-headed queries may still match wildcard equations whose LHS
       head is itself a variable or complex term, but they must not unlock
       every named equation bucket by unifying the head variable with an
       unrelated function symbol. HE treats ($f x) as data unless a wildcard
       equation explicitly matches it. */
    query_bucket(&s->eq_idx.wildcard, query, a, out);
}
