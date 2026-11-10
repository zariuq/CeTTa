#include "support_v1.h"

#include <stdlib.h>
#include <string.h>

#define SUPPORT_NONE UINT32_MAX

typedef struct {
    uint32_t *v;
    uint32_t len, cap;
} U32s;

static bool u32_push(U32s *x, uint32_t a) {
    if (x->len == x->cap) {
        uint32_t cap = x->cap ? 2u * x->cap : 4u;
        uint32_t *v = realloc(x->v, sizeof(*v) * cap);
        if (!v)
            return false;
        x->v = v;
        x->cap = cap;
    }
    x->v[x->len++] = a;
    return true;
}

struct SupportGraph {
    uint32_t natoms, capatoms;
    uint32_t *source;      /* per atom: its source justification, or NONE */
    uint8_t *invalid;      /* per atom: without a source */
    uint8_t *in_active;    /* per atom: listed in `active` */
    U32s *heads;           /* per atom: its justifications */
    U32s *occurs;          /* per atom: the justifications with it in the body,
                            * once per occurrence */
    uint32_t njust, capjust;
    uint32_t *jhead;       /* per justification: its head */
    uint32_t *jstart;      /* per justification: its body's start in jbody */
    uint32_t *jlen;
    uint32_t *jbody;
    uint32_t nbody, capbody;
    uint32_t *need;        /* scratch per justification: body atoms without a source */
    uint32_t *queue;       /* scratch: justifications ready to be sources */
    U32s active;           /* atoms without a source a recheck is to look at */
    U32s stack;
};

static bool activate(SupportGraph *g, uint32_t a) {
    if (g->in_active[a])
        return true;
    g->in_active[a] = 1u;
    return u32_push(&g->active, a);
}

bool support_grow(SupportGraph *g, uint32_t natoms) {
    if (natoms <= g->natoms)
        return true;
    if (natoms > g->capatoms) {
        uint32_t cap = g->capatoms ? g->capatoms : 16u;
        while (cap < natoms)
            cap *= 2u;
        uint32_t *source = realloc(g->source, sizeof(*source) * cap);
        if (source)
            g->source = source;
        uint8_t *invalid = realloc(g->invalid, cap);
        if (invalid)
            g->invalid = invalid;
        uint8_t *in_active = realloc(g->in_active, cap);
        if (in_active)
            g->in_active = in_active;
        U32s *heads = realloc(g->heads, sizeof(*heads) * cap);
        if (heads)
            g->heads = heads;
        U32s *occurs = realloc(g->occurs, sizeof(*occurs) * cap);
        if (occurs)
            g->occurs = occurs;
        if (!source || !invalid || !in_active || !heads || !occurs)
            return false;
        g->capatoms = cap;
    }
    for (uint32_t a = g->natoms; a < natoms; a++) {
        g->source[a] = SUPPORT_NONE;
        g->invalid[a] = 1u;
        g->in_active[a] = 0u;
        memset(&g->heads[a], 0, sizeof(g->heads[a]));
        memset(&g->occurs[a], 0, sizeof(g->occurs[a]));
    }
    uint32_t from = g->natoms;
    g->natoms = natoms;
    for (uint32_t a = from; a < natoms; a++)
        if (!activate(g, a))
            return false;
    return true;
}

SupportGraph *support_new(uint32_t natoms) {
    SupportGraph *g = calloc(1u, sizeof(*g));
    if (!g)
        return NULL;
    if (!support_grow(g, natoms)) {
        support_free(g);
        return NULL;
    }
    return g;
}

void support_free(SupportGraph *g) {
    if (!g)
        return;
    for (uint32_t a = 0u; a < g->natoms; a++) {
        free(g->heads[a].v);
        free(g->occurs[a].v);
    }
    free(g->source);
    free(g->invalid);
    free(g->in_active);
    free(g->heads);
    free(g->occurs);
    free(g->jhead);
    free(g->jstart);
    free(g->jlen);
    free(g->jbody);
    free(g->need);
    free(g->queue);
    free(g->active.v);
    free(g->stack.v);
    free(g);
}

uint32_t support_atoms(const SupportGraph *g) {
    return g->natoms;
}

uint32_t support_add(SupportGraph *g, uint32_t head, const uint32_t *body,
                     uint32_t nbody) {
    if (head >= g->natoms)
        return UINT32_MAX;
    for (uint32_t k = 0u; k < nbody; k++)
        if (body[k] >= g->natoms)
            return UINT32_MAX;
    if (g->njust == g->capjust) {
        uint32_t cap = g->capjust ? 2u * g->capjust : 16u;
        uint32_t *jhead = realloc(g->jhead, sizeof(*jhead) * cap);
        if (jhead)
            g->jhead = jhead;
        uint32_t *jstart = realloc(g->jstart, sizeof(*jstart) * cap);
        if (jstart)
            g->jstart = jstart;
        uint32_t *jlen = realloc(g->jlen, sizeof(*jlen) * cap);
        if (jlen)
            g->jlen = jlen;
        uint32_t *need = realloc(g->need, sizeof(*need) * cap);
        if (need)
            g->need = need;
        uint32_t *queue = realloc(g->queue, sizeof(*queue) * cap);
        if (queue)
            g->queue = queue;
        if (!jhead || !jstart || !jlen || !need || !queue)
            return UINT32_MAX;
        g->capjust = cap;
    }
    if (g->nbody + nbody > g->capbody) {
        uint32_t cap = g->capbody ? g->capbody : 64u;
        while (cap < g->nbody + nbody)
            cap *= 2u;
        uint32_t *jbody = realloc(g->jbody, sizeof(*jbody) * cap);
        if (!jbody)
            return UINT32_MAX;
        g->jbody = jbody;
        g->capbody = cap;
    }
    uint32_t j = g->njust;
    if (!u32_push(&g->heads[head], j))
        return UINT32_MAX;
    for (uint32_t k = 0u; k < nbody; k++)
        if (!u32_push(&g->occurs[body[k]], j))
            return UINT32_MAX;
    g->jhead[j] = head;
    g->jstart[j] = g->nbody;
    g->jlen[j] = nbody;
    if (nbody)
        memcpy(g->jbody + g->nbody, body, sizeof(*body) * nbody);
    g->nbody += nbody;
    g->njust++;
    if (g->invalid[head] && !activate(g, head))
        return UINT32_MAX;
    return j;
}

static bool lose_source(SupportGraph *g, uint32_t a) {
    g->source[a] = SUPPORT_NONE;
    g->invalid[a] = 1u;
    return activate(g, a) && u32_push(&g->stack, a);
}

bool support_lose(SupportGraph *g, uint32_t j) {
    if (j >= g->njust)
        return false;
    uint32_t head = g->jhead[j];
    if (g->source[head] != j)
        return true;
    g->stack.len = 0u;
    bool ok = lose_source(g, head);
    while (ok && g->stack.len > 0u) {
        uint32_t a = g->stack.v[--g->stack.len];
        const U32s *occ = &g->occurs[a];
        for (uint32_t k = 0u; ok && k < occ->len; k++) {
            uint32_t q = occ->v[k];
            uint32_t c = g->jhead[q];
            if (g->source[c] == q)
                ok = lose_source(g, c);
        }
    }
    return ok;
}

bool support_reopen(SupportGraph *g, uint32_t a) {
    return a >= g->natoms || !g->invalid[a] || activate(g, a);
}

static int cmp_u32(const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return (a > b) - (a < b);
}

bool support_recheck(SupportGraph *g, support_enabled_fn enabled,
                     support_excluded_fn excluded, void *context,
                     uint32_t **unsupported, uint32_t *count) {
    *unsupported = NULL;
    *count = 0u;
    uint32_t head = 0u, tail = 0u;
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < g->active.len; i++) {
        uint32_t a = g->active.v[i];
        if (!g->invalid[a] || (excluded && excluded(context, a))) {
            g->in_active[a] = 0u;      /* until reopened or a source is lost */
            continue;
        }
        g->active.v[kept++] = a;
        const U32s *hs = &g->heads[a];
        for (uint32_t k = 0u; k < hs->len; k++) {
            uint32_t j = hs->v[k];
            uint32_t need = 0u;
            for (uint32_t b = 0u; b < g->jlen[j]; b++)
                need += g->invalid[g->jbody[g->jstart[j] + b]];
            g->need[j] = need;
            if (need == 0u && (!enabled || enabled(context, j)))
                g->queue[tail++] = j;
        }
    }
    g->active.len = kept;
    while (head < tail) {
        uint32_t j = g->queue[head++];
        uint32_t a = g->jhead[j];
        if (!g->invalid[a])
            continue;
        g->source[a] = j;
        g->invalid[a] = 0u;
        const U32s *occ = &g->occurs[a];
        for (uint32_t k = 0u; k < occ->len; k++) {
            uint32_t q = occ->v[k];
            uint32_t c = g->jhead[q];
            if (g->invalid[c] && g->in_active[c] &&
                !(excluded && excluded(context, c)) &&
                --g->need[q] == 0u && (!enabled || enabled(context, q)))
                g->queue[tail++] = q;
        }
    }
    kept = 0u;
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < g->active.len; i++) {
        uint32_t a = g->active.v[i];
        if (!g->invalid[a]) {
            g->in_active[a] = 0u;
            continue;
        }
        g->active.v[kept++] = a;
        n++;
    }
    g->active.len = kept;
    if (n == 0u)
        return true;
    uint32_t *out = malloc(sizeof(*out) * n);
    if (!out)
        return false;
    for (uint32_t i = 0u; i < n; i++)
        out[i] = g->active.v[i];
    if (n > 1u)
        qsort(out, n, sizeof(*out), cmp_u32);
    *unsupported = out;
    *count = n;
    return true;
}

bool support_has_source(const SupportGraph *g, uint32_t a) {
    return a < g->natoms && !g->invalid[a];
}

uint32_t support_source(const SupportGraph *g, uint32_t a) {
    return a < g->natoms ? g->source[a] : SUPPORT_NONE;
}
