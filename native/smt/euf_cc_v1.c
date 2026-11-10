#include "euf_cc_v1.h"

#include <stdlib.h>
#include <string.h>

/* Terms are made before the first merge; the closure's base state (each
 * application in the signature table and in its arguments' use lists) is
 * set up then and never undone.
 *
 * Classes: every term knows its class's root (quick-find), the root its
 * size; a class's terms form a circular list.  A union moves the smaller
 * class into the larger, its terms' roots rewritten; undoing it splits the
 * lists again and restores those roots.
 *
 * Congruence: each class keeps the applications with an argument in it
 * (its use list).  When a class moves, its applications are looked up again
 * under their new signature (symbol, arguments' roots): one with the same
 * signature is congruent and is merged; otherwise the application is
 * entered under the new signature.  Table entries whose term's signature has
 * since changed are stale and never match.
 *
 * Explanations: each union is one edge between the two terms merged,
 * labelled with the reason or the congruent pair; the edges of a class form
 * a tree, so a = b has one path. */

#define EUF_NONE UINT32_MAX

typedef enum {
    EUF_UNDO_UNION,
    EUF_UNDO_USES,
    EUF_UNDO_LOOKUP,
    EUF_UNDO_EDGE,
} EufUndoKind;

typedef struct {
    EufUndoKind kind;
    uint32_t a, b;
} EufUndo;

typedef struct {
    uint32_t *v;
    uint32_t len, cap;
} EufU32s;

typedef struct {
    uint32_t a, b;           /* the terms merged */
    uint32_t reason;         /* an asserted equality's reason, or EUF_NONE */
    uint32_t p, q;           /* a congruence: the two applications */
    uint32_t next_a, next_b; /* the next edges at a and at b */
} EufEdge;

struct EufCC {
    /* terms */
    uint32_t nterms, capterms;
    uint32_t *sym, *arg_start, *nargs;
    EufU32s args;
    uint32_t *hc;            /* hash-consing: term + 1, 0 free */
    uint32_t hc_cap;
    /* classes */
    uint32_t *root, *size, *next;
    EufU32s *uses;           /* per term, meaningful at roots */
    /* signature table: chains of entries */
    bool based;
    uint32_t *bucket;        /* entry + 1, 0 none */
    uint32_t nbuckets;
    uint32_t *entry_term, *entry_next;
    uint32_t nentries, capentries;
    /* explanation forest */
    uint32_t *adj;           /* per term: its newest edge + 1, 0 none */
    EufEdge *edges;
    uint32_t nedges, capedges;
    /* undo */
    EufUndo *undo;
    uint32_t nundo, capundo;
    /* scratch */
    EufU32s pending;         /* pairs of terms to merge, with their labels */
    uint32_t *stamp, *from_edge, *from_term;
    uint32_t *edge_stamp;
    uint32_t now;
    bool oom;
};

static bool u32_push(EufU32s *x, uint32_t a) {
    if (x->len == x->cap) {
        uint32_t cap = x->cap ? 2u * x->cap : 8u;
        uint32_t *v = realloc(x->v, sizeof(*v) * cap);
        if (!v)
            return false;
        x->v = v;
        x->cap = cap;
    }
    x->v[x->len++] = a;
    return true;
}

static bool grow_u32(uint32_t **p, uint32_t cap) {
    uint32_t *q = realloc(*p, sizeof(**p) * cap);
    if (!q)
        return false;
    *p = q;
    return true;
}

EufCC *euf_new(void) {
    return calloc(1u, sizeof(EufCC));
}

void euf_free(EufCC *cc) {
    if (!cc)
        return;
    free(cc->sym);
    free(cc->arg_start);
    free(cc->nargs);
    free(cc->args.v);
    free(cc->hc);
    free(cc->root);
    free(cc->size);
    free(cc->next);
    if (cc->uses)
        for (uint32_t t = 0u; t < cc->nterms; t++)
            free(cc->uses[t].v);
    free(cc->uses);
    free(cc->bucket);
    free(cc->entry_term);
    free(cc->entry_next);
    free(cc->adj);
    free(cc->edges);
    free(cc->undo);
    free(cc->pending.v);
    free(cc->stamp);
    free(cc->from_edge);
    free(cc->from_term);
    free(cc->edge_stamp);
    free(cc);
}

static uint32_t term_hash(uint32_t symbol, const uint32_t *args, uint32_t n) {
    uint32_t h = symbol * 2654435761u ^ n;
    for (uint32_t i = 0u; i < n; i++)
        h = (h ^ args[i]) * 2246822519u + 0x9e3779b9u;
    return h ^ (h >> 15);
}

static bool term_is(const EufCC *cc, uint32_t t, uint32_t symbol,
                    const uint32_t *args, uint32_t n) {
    return cc->sym[t] == symbol && cc->nargs[t] == n &&
           (n == 0u || memcmp(cc->args.v + cc->arg_start[t], args,
                              sizeof(*args) * n) == 0);
}

static bool hc_grow(EufCC *cc) {
    uint32_t cap = cc->hc_cap ? 2u * cc->hc_cap : 256u;
    uint32_t *hc = calloc(cap, sizeof(*hc));
    if (!hc)
        return false;
    for (uint32_t t = 0u; t < cc->nterms; t++) {
        uint32_t k = term_hash(cc->sym[t], cc->args.v + cc->arg_start[t],
                               cc->nargs[t]) & (cap - 1u);
        while (hc[k])
            k = (k + 1u) & (cap - 1u);
        hc[k] = t + 1u;
    }
    free(cc->hc);
    cc->hc = hc;
    cc->hc_cap = cap;
    return true;
}

uint32_t euf_term(EufCC *cc, uint32_t symbol, const uint32_t *args,
                  uint32_t nargs) {
    if (!cc || cc->based)
        return EUF_NONE;
    for (uint32_t i = 0u; i < nargs; i++)
        if (args[i] >= cc->nterms)
            return EUF_NONE;
    if ((cc->nterms + 1u) * 2u > cc->hc_cap && !hc_grow(cc))
        return EUF_NONE;
    uint32_t mask = cc->hc_cap - 1u;
    uint32_t k = term_hash(symbol, args, nargs) & mask;
    for (; cc->hc[k]; k = (k + 1u) & mask)
        if (term_is(cc, cc->hc[k] - 1u, symbol, args, nargs))
            return cc->hc[k] - 1u;
    if (cc->nterms == cc->capterms) {
        uint32_t cap = cc->capterms ? 2u * cc->capterms : 64u;
        if (!grow_u32(&cc->sym, cap) || !grow_u32(&cc->arg_start, cap) ||
            !grow_u32(&cc->nargs, cap))
            return EUF_NONE;
        cc->capterms = cap;
    }
    uint32_t t = cc->nterms;
    cc->sym[t] = symbol;
    cc->arg_start[t] = cc->args.len;
    cc->nargs[t] = nargs;
    for (uint32_t i = 0u; i < nargs; i++)
        if (!u32_push(&cc->args, args[i]))
            return EUF_NONE;
    cc->nterms++;
    cc->hc[k] = t + 1u;
    return t;
}

uint32_t euf_terms(const EufCC *cc) {
    return cc ? cc->nterms : 0u;
}

/* The signature of application t under the current roots. */
static uint32_t sig_hash(const EufCC *cc, uint32_t t) {
    uint32_t n = cc->nargs[t];
    uint32_t h = cc->sym[t] * 2654435761u ^ n;
    for (uint32_t i = 0u; i < n; i++)
        h = (h ^ cc->root[cc->args.v[cc->arg_start[t] + i]]) * 2246822519u + 0x9e3779b9u;
    return h ^ (h >> 15);
}

static bool sig_same(const EufCC *cc, uint32_t t, uint32_t u) {
    if (cc->sym[t] != cc->sym[u] || cc->nargs[t] != cc->nargs[u])
        return false;
    for (uint32_t i = 0u; i < cc->nargs[t]; i++)
        if (cc->root[cc->args.v[cc->arg_start[t] + i]] !=
            cc->root[cc->args.v[cc->arg_start[u] + i]])
            return false;
    return true;
}

static bool undo_push(EufCC *cc, EufUndoKind kind, uint32_t a, uint32_t b) {
    if (cc->nundo == cc->capundo) {
        uint32_t cap = cc->capundo ? 2u * cc->capundo : 256u;
        EufUndo *u = realloc(cc->undo, sizeof(*u) * cap);
        if (!u)
            return false;
        cc->undo = u;
        cc->capundo = cap;
    }
    cc->undo[cc->nundo++] = (EufUndo){kind, a, b};
    return true;
}

/* An application with t's signature other than t, or EUF_NONE. */
static uint32_t lookup(const EufCC *cc, uint32_t t) {
    for (uint32_t e = cc->bucket[sig_hash(cc, t) & (cc->nbuckets - 1u)]; e;
         e = cc->entry_next[e - 1u]) {
        uint32_t u = cc->entry_term[e - 1u];
        if (u != t && sig_same(cc, u, t))
            return u;
    }
    return EUF_NONE;
}

static bool enter(EufCC *cc, uint32_t t, bool undoable) {
    if (cc->nentries == cc->capentries) {
        uint32_t cap = cc->capentries ? 2u * cc->capentries : 256u;
        if (!grow_u32(&cc->entry_term, cap) || !grow_u32(&cc->entry_next, cap))
            return false;
        cc->capentries = cap;
    }
    uint32_t b = sig_hash(cc, t) & (cc->nbuckets - 1u);
    if (undoable && !undo_push(cc, EUF_UNDO_LOOKUP, b, cc->bucket[b]))
        return false;
    uint32_t e = cc->nentries++;
    cc->entry_term[e] = t;
    cc->entry_next[e] = cc->bucket[b];
    cc->bucket[b] = e + 1u;
    return true;
}

/* The base state: classes of one, applications in the table and in their
 * arguments' use lists. */
static bool base(EufCC *cc) {
    if (cc->based)
        return true;
    uint32_t n = cc->nterms;
    cc->root = malloc(sizeof(*cc->root) * (n ? n : 1u));
    cc->size = malloc(sizeof(*cc->size) * (n ? n : 1u));
    cc->next = malloc(sizeof(*cc->next) * (n ? n : 1u));
    cc->uses = calloc(n ? n : 1u, sizeof(*cc->uses));
    cc->adj = calloc(n ? n : 1u, sizeof(*cc->adj));
    cc->stamp = calloc(n ? n : 1u, sizeof(*cc->stamp));
    cc->from_edge = malloc(sizeof(*cc->from_edge) * (n ? n : 1u));
    cc->from_term = malloc(sizeof(*cc->from_term) * (n ? n : 1u));
    cc->nbuckets = 64u;
    while (cc->nbuckets < 2u * n)
        cc->nbuckets *= 2u;
    cc->bucket = calloc(cc->nbuckets, sizeof(*cc->bucket));
    if (!cc->root || !cc->size || !cc->next || !cc->uses || !cc->adj ||
        !cc->stamp || !cc->from_edge || !cc->from_term || !cc->bucket)
        return false;
    for (uint32_t t = 0u; t < n; t++) {
        cc->root[t] = t;
        cc->size[t] = 1u;
        cc->next[t] = t;
    }
    for (uint32_t t = 0u; t < n; t++) {
        if (cc->nargs[t] == 0u)
            continue;
        if (!enter(cc, t, false))
            return false;
        for (uint32_t i = 0u; i < cc->nargs[t]; i++)
            if (!u32_push(&cc->uses[cc->args.v[cc->arg_start[t] + i]], t))
                return false;
    }
    cc->based = true;
    return true;
}

static bool add_edge(EufCC *cc, uint32_t a, uint32_t b, uint32_t reason,
                     uint32_t p, uint32_t q) {
    if (cc->nedges == cc->capedges) {
        uint32_t cap = cc->capedges ? 2u * cc->capedges : 256u;
        EufEdge *e = realloc(cc->edges, sizeof(*e) * cap);
        uint32_t *s = realloc(cc->edge_stamp, sizeof(*s) * cap);
        if (e)
            cc->edges = e;
        if (s)
            cc->edge_stamp = s;
        if (!e || !s)
            return false;
        cc->capedges = cap;
    }
    uint32_t e = cc->nedges++;
    cc->edges[e] = (EufEdge){a, b, reason, p, q, cc->adj[a], cc->adj[b]};
    cc->edge_stamp[e] = 0u;
    cc->adj[a] = e + 1u;
    cc->adj[b] = e + 1u;
    return undo_push(cc, EUF_UNDO_EDGE, e, 0u);
}

/* Merge the classes of a and b, the edge labelled; then the congruences
 * the move makes, each merged the same way. */
static bool union_terms(EufCC *cc, uint32_t a, uint32_t b, uint32_t reason,
                        uint32_t p, uint32_t q) {
    cc->pending.len = 0u;
    if (!u32_push(&cc->pending, a) || !u32_push(&cc->pending, b) ||
        !u32_push(&cc->pending, reason) || !u32_push(&cc->pending, p) ||
        !u32_push(&cc->pending, q))
        return false;
    for (uint32_t at = 0u; at < cc->pending.len; at += 5u) {
        uint32_t x = cc->pending.v[at], y = cc->pending.v[at + 1u];
        uint32_t rx = cc->root[x], ry = cc->root[y];
        if (rx == ry)
            continue;
        if (!add_edge(cc, x, y, cc->pending.v[at + 2u], cc->pending.v[at + 3u],
                      cc->pending.v[at + 4u]))
            return false;
        /* the smaller class (rx) moves into the larger (ry) */
        if (cc->size[rx] > cc->size[ry]) {
            uint32_t t = rx;
            rx = ry;
            ry = t;
        }
        if (!undo_push(cc, EUF_UNDO_UNION, rx, ry))
            return false;
        uint32_t t = rx;
        do {
            cc->root[t] = ry;
            t = cc->next[t];
        } while (t != rx);
        uint32_t nx = cc->next[rx];
        cc->next[rx] = cc->next[ry];
        cc->next[ry] = nx;
        cc->size[ry] += cc->size[rx];
        /* the moved class's applications under their new signatures */
        EufU32s *moved = &cc->uses[rx];
        if (!undo_push(cc, EUF_UNDO_USES, ry, cc->uses[ry].len))
            return false;
        for (uint32_t k = 0u; k < moved->len; k++) {
            uint32_t app = moved->v[k];
            uint32_t other = lookup(cc, app);
            if (other != EUF_NONE) {
                if (cc->root[other] != cc->root[app] &&
                    (!u32_push(&cc->pending, app) || !u32_push(&cc->pending, other) ||
                     !u32_push(&cc->pending, EUF_NONE) || !u32_push(&cc->pending, app) ||
                     !u32_push(&cc->pending, other)))
                    return false;
            } else if (!enter(cc, app, true)) {
                return false;
            }
            if (!u32_push(&cc->uses[ry], app))
                return false;
        }
    }
    return true;
}

bool euf_merge(EufCC *cc, uint32_t a, uint32_t b, uint32_t reason) {
    if (!cc || a >= cc->nterms || b >= cc->nterms || !base(cc))
        return false;
    return union_terms(cc, a, b, reason, EUF_NONE, EUF_NONE);
}

bool euf_equal(const EufCC *cc, uint32_t a, uint32_t b) {
    if (!cc || a >= cc->nterms || b >= cc->nterms)
        return false;
    if (!cc->based)
        return a == b;
    return cc->root[a] == cc->root[b];
}

uint32_t euf_height(const EufCC *cc) {
    return cc ? cc->nundo : 0u;
}

void euf_undo(EufCC *cc, uint32_t height) {
    while (cc && cc->nundo > height) {
        EufUndo u = cc->undo[--cc->nundo];
        switch (u.kind) {
        case EUF_UNDO_EDGE: {
            EufEdge *e = &cc->edges[u.a];
            cc->adj[e->a] = e->next_a;
            cc->adj[e->b] = e->next_b;
            cc->nedges--;
            break;
        }
        case EUF_UNDO_UNION: {
            uint32_t rx = u.a, ry = u.b;
            uint32_t nx = cc->next[rx];
            cc->next[rx] = cc->next[ry];
            cc->next[ry] = nx;
            cc->size[ry] -= cc->size[rx];
            uint32_t t = rx;
            do {
                cc->root[t] = rx;
                t = cc->next[t];
            } while (t != rx);
            break;
        }
        case EUF_UNDO_USES:
            cc->uses[u.a].len = u.b;
            break;
        case EUF_UNDO_LOOKUP:
            cc->bucket[u.a] = u.b;
            cc->nentries--;
            break;
        }
    }
}

/* The edges on the path from a to b in the forest, by breadth-first search
 * from a, each written to `path`. */
static bool forest_path(EufCC *cc, uint32_t a, uint32_t b, EufU32s *path,
                        EufU32s *queue) {
    cc->now++;
    queue->len = 0u;
    if (!u32_push(queue, a))
        return false;
    cc->stamp[a] = cc->now;
    cc->from_edge[a] = EUF_NONE;
    bool found = a == b;
    for (uint32_t h = 0u; !found && h < queue->len; h++) {
        uint32_t t = queue->v[h];
        for (uint32_t e1 = cc->adj[t]; e1; ) {
            const EufEdge *e = &cc->edges[e1 - 1u];
            uint32_t other = e->a == t ? e->b : e->a;
            uint32_t next = e->a == t ? e->next_a : e->next_b;
            if (cc->stamp[other] != cc->now) {
                cc->stamp[other] = cc->now;
                cc->from_edge[other] = e1 - 1u;
                cc->from_term[other] = t;
                if (other == b) {
                    found = true;
                    break;
                }
                if (!u32_push(queue, other))
                    return false;
            }
            e1 = next;
        }
    }
    if (!found)
        return false;
    path->len = 0u;
    for (uint32_t t = b; t != a; t = cc->from_term[t])
        if (!u32_push(path, cc->from_edge[t]))
            return false;
    return true;
}

static int cmp_u32(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return (a > b) - (a < b);
}

bool euf_explain(EufCC *cc, uint32_t a, uint32_t b, uint32_t **reasons,
                 uint32_t *count) {
    *reasons = NULL;
    *count = 0u;
    if (!euf_equal(cc, a, b))
        return false;
    EufU32s out = {0}, work = {0}, path = {0}, queue = {0};
    /* each edge is explained once per call */
    uint32_t mark = ++cc->now;
    bool ok = u32_push(&work, a) && u32_push(&work, b);
    while (ok && work.len) {
        uint32_t y = work.v[--work.len], x = work.v[--work.len];
        if (x == y)
            continue;
        ok = forest_path(cc, x, y, &path, &queue);
        for (uint32_t k = 0u; ok && k < path.len; k++) {
            uint32_t e = path.v[k];
            if (cc->edge_stamp[e] == mark)
                continue;
            cc->edge_stamp[e] = mark;
            const EufEdge *edge = &cc->edges[e];
            if (edge->reason != EUF_NONE) {
                ok = u32_push(&out, edge->reason);
                continue;
            }
            for (uint32_t i = 0u; ok && i < cc->nargs[edge->p]; i++)
                ok = u32_push(&work, cc->args.v[cc->arg_start[edge->p] + i]) &&
                     u32_push(&work, cc->args.v[cc->arg_start[edge->q] + i]);
        }
    }
    free(work.v);
    free(path.v);
    free(queue.v);
    if (!ok) {
        free(out.v);
        return false;
    }
    if (out.len > 1u)
        qsort(out.v, out.len, sizeof(*out.v), cmp_u32);
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < out.len; i++)
        if (n == 0u || out.v[n - 1u] != out.v[i])
            out.v[n++] = out.v[i];
    *reasons = out.v;
    *count = n;
    return true;
}
