/* hol_export.c - proof journal and constructive certificate export.
 *
 * When journaling is on, every theorem produced through the public API is
 * recorded as an event: rule, premise events (serial and payload), term or
 * type arguments, result payload.  Theory events are always available from
 * the environment history.  hol_export writes a HOLR stream that rebuilds
 * the roots from the bare bootstrap: the complete linear history up to the
 * roots' common environment, every journaled event they depend on (and the
 * derivations of type-definition witnesses), one EMIT per root occurrence
 * and an exact EXPECT per root.  The exporter is not trusted: its output is
 * only ever believed after an independent replay checks it. */
#include "hol_internal.h"
#include "hol_replay.h"

/* ------------------------------------------------------------------------ */
/* Journal                                                                   */
/* ------------------------------------------------------------------------ */

void hol_journal_enable(HolKernel *k, bool on) {
    if (k) k->journal_on = on;
}

void hk_journal_add(HolKernel *k, uint8_t op, uint64_t serial, HolThm *result,
                    const uint64_t *prem, HolThm *const *pthm, unsigned nprem,
                    void *const *args, uint32_t nargs) {
    if (!k->journal_on) return;
    if (k->njev == k->capjev) {
        size_t ncap = k->capjev ? k->capjev * 2 : 1024;
        HolJEvent *nj = (HolJEvent *)realloc(k->jev, ncap * sizeof(HolJEvent));
        if (!nj) return; /* journal incomplete: export of this theorem will refuse */
        k->jev = nj;
        k->capjev = ncap;
    }
    void **a = NULL;
    if (nargs) {
        a = (void **)malloc(nargs * sizeof(void *));
        if (!a) return;
        memcpy(a, args, nargs * sizeof(void *));
    }
    HolJEvent *e = &k->jev[k->njev++];
    memset(e, 0, sizeof *e);
    e->serial = serial;
    e->op = op;
    e->nprem = (uint8_t)nprem;
    for (unsigned i = 0; i < nprem; i++) {
        e->prem[i] = prem[i];
        e->pthm[i] = pthm[i];
    }
    e->result = result;
    e->args = a;
    e->nargs = nargs;
    k->journal_arg_bytes += nargs * sizeof(void *);
}

static HolJEvent *journal_find(HolKernel *k, uint64_t serial);

/* A completed proof scope must not retain its peak journal allocation.
 * Shrinking is optional: allocation failure preserves the valid old array. */
static void journal_trim(HolKernel *k) {
    if (!k->njev) {
        free(k->jev);
        k->jev = NULL;
        k->capjev = 0;
    } else if (k->njev <= k->capjev / 4) {
        size_t cap = k->capjev;
        while (cap > 1024 && k->njev <= cap / 2) cap /= 2;
        if (cap < k->capjev) {
            HolJEvent *events = realloc(k->jev, cap * sizeof(*events));
            if (events) { k->jev = events; k->capjev = cap; }
        }
    }
}

/* Called during collection, after the live handles, roots and environment
 * events are marked.  Journal events survive only when some live theorem
 * reference or live type definition (its witness) depends on them; the
 * rest are dropped, so journaling retains derivations, not every
 * temporary.  If the scratch space cannot be allocated, every event is
 * kept (conservative). */
void hk_journal_mark(HolKernel *k) {
    if (!k->njev) return;
    uint8_t *live = (uint8_t *)calloc(k->njev, 1);
    size_t *stack = (size_t *)malloc(k->njev * sizeof(size_t));
    size_t sp = 0;
    if (live && stack) {
#define ROOT(serial)                                                             \
        do {                                                                     \
            HolJEvent *e_ = journal_find(k, (serial));                           \
            if (e_ && !live[e_ - k->jev]) {                                      \
                live[e_ - k->jev] = 1;                                           \
                stack[sp++] = (size_t)(e_ - k->jev);                             \
            }                                                                    \
        } while (0)
        for (uint32_t i = 0; i < k->nslots; i++)
            if (k->slots[i].kind == HOL_KIND_THM && k->slots[i].event) ROOT(k->slots[i].event);
        for (size_t i = 0; i < k->ndecls; i++)
            if (k->decls[i]->mark && k->decls[i]->witness_event) ROOT(k->decls[i]->witness_event);
        while (sp) {
            HolJEvent *e = &k->jev[stack[--sp]];
            for (unsigned j = 0; j < e->nprem; j++) ROOT(e->prem[j]);
        }
#undef ROOT
        size_t o = 0;
        for (size_t i = 0; i < k->njev; i++) {
            if (live[i]) k->jev[o++] = k->jev[i];
            else {
                k->journal_arg_bytes -= k->jev[i].nargs * sizeof(void *);
                free(k->jev[i].args);
            }
        }
        k->njev = o;
        journal_trim(k);
    }
    free(live);
    free(stack);
    for (size_t i = 0; i < k->njev; i++) {
        HolJEvent *e = &k->jev[i];
        hk_mark_thm(k, e->result);
        for (unsigned j = 0; j < e->nprem; j++) hk_mark_thm(k, e->pthm[j]);
        for (uint32_t j = 0; j < e->nargs; j++) {
            if (e->op == HOLR_INST_TYPE) hk_mark_type(k, (HolType *)e->args[j]);
            else hk_mark_term(k, (HolTerm *)e->args[j]);
        }
    }
}

void hk_journal_free(HolKernel *k) {
    for (size_t i = 0; i < k->njev; i++) free(k->jev[i].args);
    free(k->jev);
    k->jev = NULL;
    k->njev = k->capjev = 0;
    k->journal_arg_bytes = 0;
}

static HolJEvent *journal_find(HolKernel *k, uint64_t serial) {
    size_t lo = 0, hi = k->njev;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (k->jev[mid].serial < serial) lo = mid + 1;
        else hi = mid;
    }
    return lo < k->njev && k->jev[lo].serial == serial ? &k->jev[lo] : NULL;
}

static HolDecl *decl_find(HolKernel *k, uint64_t serial) {
    size_t lo = 0, hi = k->ndecls;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (k->decls[mid]->serial < serial) lo = mid + 1;
        else hi = mid;
    }
    return lo < k->ndecls && k->decls[lo]->serial == serial ? k->decls[lo] : NULL;
}

/* ------------------------------------------------------------------------ */
/* Pointer maps                                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
    const void **key;
    uint64_t *val;
    size_t cap, n;
} PMap;

static bool pmap_get(const PMap *m, const void *key, uint64_t *val) {
    if (!m->cap) return false;
    size_t mask = m->cap - 1;
    for (size_t j = ((uintptr_t)key >> 4) & mask;; j = (j + 1) & mask) {
        if (!m->key[j]) return false;
        if (m->key[j] == key) { *val = m->val[j]; return true; }
    }
}

static bool pmap_put(PMap *m, const void *key, uint64_t val) {
    if ((m->n + 1) * 2 > m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 1024;
        const void **nk = (const void **)calloc(ncap, sizeof(void *));
        uint64_t *nv = (uint64_t *)calloc(ncap, sizeof(uint64_t));
        if (!nk || !nv) { free(nk); free(nv); return false; }
        for (size_t i = 0; i < m->cap; i++) {
            if (!m->key[i]) continue;
            size_t j = ((uintptr_t)m->key[i] >> 4) & (ncap - 1);
            while (nk[j]) j = (j + 1) & (ncap - 1);
            nk[j] = m->key[i];
            nv[j] = m->val[i];
        }
        free(m->key);
        free(m->val);
        m->key = nk;
        m->val = nv;
        m->cap = ncap;
    }
    size_t mask = m->cap - 1;
    size_t j = ((uintptr_t)key >> 4) & mask;
    while (m->key[j] && m->key[j] != key) j = (j + 1) & mask;
    if (!m->key[j]) m->n++;
    m->key[j] = key;
    m->val[j] = val;
    return true;
}

static void pmap_free(PMap *m) {
    free(m->key);
    free(m->val);
    memset(m, 0, sizeof *m);
}

/* ------------------------------------------------------------------------ */
/* Export                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct ThEntry ThEntry;
typedef struct {
    HolKernel *k;
    HolWriter *w;
    PMap names, types, terms;
    PMap thms; /* serial + 1 -> index into ents */
    ThEntry *ents;
    size_t nents, capents;
    uint64_t nn, ny, nt, nh;
    bool fail;
} Ex;

static uint64_t ex_name(Ex *x, HolName n) {
    uint64_t id;
    if (pmap_get(&x->names, (const void *)(uintptr_t)(n + 1), &id)) return id;
    const HolNameRec *r = &x->k->names[n];
    hol_writer_op(x->w, HOLR_NAME);
    hol_writer_uv(x->w, r->len);
    hol_writer_bytes(x->w, r->bytes, r->len);
    id = x->nn++;
    if (!pmap_put(&x->names, (const void *)(uintptr_t)(n + 1), id)) x->fail = true;
    return id;
}

static uint64_t ex_type(Ex *x, HolType *t) {
    uint64_t id;
    if (pmap_get(&x->types, t, &id)) return id;
    if (t->kind == HOL_TY_VAR) {
        uint64_t n = ex_name(x, t->name);
        hol_writer_op(x->w, HOLR_TYVAR);
        hol_writer_uv(x->w, n);
    } else {
        uint64_t small[8], *ids = small;
        if (t->nargs > 8) {
            ids = (uint64_t *)malloc(t->nargs * sizeof(uint64_t));
            if (!ids) { x->fail = true; return 0; }
        }
        for (uint32_t i = 0; i < t->nargs; i++) ids[i] = ex_type(x, t->args[i]);
        uint64_t n = ex_name(x, t->name);
        hol_writer_op(x->w, HOLR_TYAPP);
        hol_writer_uv(x->w, n);
        hol_writer_uv(x->w, t->nargs);
        for (uint32_t i = 0; i < t->nargs; i++) hol_writer_uv(x->w, ids[i]);
        if (ids != small) free(ids);
    }
    id = x->ny++;
    if (!pmap_put(&x->types, t, id)) x->fail = true;
    return id;
}

static uint64_t ex_term(Ex *x, HolTerm *t) {
    uint64_t id;
    if (pmap_get(&x->terms, t, &id)) return id;
    if (t->kind == HOL_TM_VAR || t->kind == HOL_TM_CONST) {
        uint64_t ty = ex_type(x, t->type);
        uint64_t n = ex_name(x, t->u.leaf.name);
        hol_writer_op(x->w, t->kind == HOL_TM_VAR ? HOLR_VAR : HOLR_CONST);
        hol_writer_uv(x->w, n);
        hol_writer_uv(x->w, ty);
    } else {
        uint64_t a = ex_term(x, t->u.node.l);
        uint64_t b = ex_term(x, t->u.node.r);
        hol_writer_op(x->w, t->kind == HOL_TM_COMB ? HOLR_COMB : HOLR_ABS);
        hol_writer_uv(x->w, a);
        hol_writer_uv(x->w, b);
    }
    id = x->nt++;
    if (!pmap_put(&x->terms, t, id)) x->fail = true;
    return id;
}

/* Theorem ids are keyed exactly by (event serial, payload): a type
 * definition's two theorems share one event serial, so each serial maps to
 * at most two (payload, id) entries.  No hash value is used as identity. */
struct ThEntry { const HolThm *th[2]; uint64_t id[2]; unsigned n; };

static bool ex_thm_get(Ex *x, uint64_t serial, const HolThm *th, uint64_t *id) {
    uint64_t idx;
    if (!pmap_get(&x->thms, (const void *)(uintptr_t)(serial + 1), &idx)) return false;
    const ThEntry *e = &x->ents[idx];
    for (unsigned i = 0; i < e->n; i++)
        if (e->th[i] == th) { *id = e->id[i]; return true; }
    return false;
}

static void ex_thm_put(Ex *x, uint64_t serial, const HolThm *th, uint64_t id) {
    uint64_t idx;
    if (!pmap_get(&x->thms, (const void *)(uintptr_t)(serial + 1), &idx)) {
        if (x->nents == x->capents) {
            size_t ncap = x->capents ? x->capents * 2 : 256;
            ThEntry *ne = (ThEntry *)realloc(x->ents, ncap * sizeof(ThEntry));
            if (!ne) { x->fail = true; return; }
            x->ents = ne;
            x->capents = ncap;
        }
        idx = x->nents++;
        x->ents[idx].n = 0;
        if (!pmap_put(&x->thms, (const void *)(uintptr_t)(serial + 1), idx)) { x->fail = true; return; }
    }
    ThEntry *e = &x->ents[idx];
    if (e->n == 2) { x->fail = true; return; }
    e->th[e->n] = th;
    e->id[e->n] = id;
    e->n++;
}

typedef struct { uint64_t *v; size_t n, cap; } U64Vec;
static bool u64_push(U64Vec *v, uint64_t x) {
    if (v->n == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 64;
        uint64_t *nv = (uint64_t *)realloc(v->v, ncap * sizeof(uint64_t));
        if (!nv) return false;
        v->v = nv;
        v->cap = ncap;
    }
    v->v[v->n++] = x;
    return true;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static HolStatus ex_fail(HolKernel *k, HolStatus s, HolReason r) {
    hk_fail(k, s, r);
    return k->status;
}

HolStatus hol_export(HolKernel *k, const HolRef *roots, size_t n, uint8_t **out, size_t *len) {
    if (!k || (n && !roots) || !out || !len) return HOL_MALFORMED;
    hk_begin_call(k);
    *out = NULL;
    *len = 0;
    HolThm **rth = (HolThm **)calloc(n ? n : 1, sizeof(HolThm *));
    uint64_t *rev = (uint64_t *)calloc(n ? n : 1, sizeof(uint64_t));
    U64Vec need = {0}, work = {0};
    PMap seen = {0};
    HolDecl **chain = NULL;
    HolStatus st = HOL_OK;
    Ex x;
    memset(&x, 0, sizeof x);
    x.k = k;
    if (!rth || !rev) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
    HolDecl *target = k->d_eq;
    for (size_t i = 0; i < n; i++) {
        rth[i] = (HolThm *)hk_ref_get(k, roots[i], HOL_KIND_THM, &rev[i]);
        if (!rth[i]) { st = k->status; goto done; }
        if (hk_anchor_join(target, rth[i]->anchor, &target) < 0) {
            st = ex_fail(k, HOL_FOREIGN, HOL_R_INCOMPATIBLE_ENV);
            goto done;
        }
        if (!u64_push(&work, rev[i])) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
    }
    /* Closure over journaled events; theory events end the walk. */
    for (int pass = 0; pass < 2; pass++) {
        while (work.n) {
            uint64_t s = work.v[--work.n];
            uint64_t dummy;
            if (pmap_get(&seen, (const void *)(uintptr_t)(s * 2 + 1), &dummy)) continue;
            if (!pmap_put(&seen, (const void *)(uintptr_t)(s * 2 + 1), 1)) {
                st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM);
                goto done;
            }
            HolJEvent *e = journal_find(k, s);
            if (e) {
                if (hk_anchor_join(target, e->result->anchor, &target) < 0) {
                    st = ex_fail(k, HOL_FOREIGN, HOL_R_INCOMPATIBLE_ENV);
                    goto done;
                }
                if (!u64_push(&need, s)) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
                for (unsigned j = 0; j < e->nprem; j++)
                    if (!u64_push(&work, e->prem[j])) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
                continue;
            }
            HolDecl *d = decl_find(k, s);
            if (!d || !d->committed) { st = ex_fail(k, HOL_REFUSED, HOL_R_BAD_HANDLE); goto done; }
            if (hk_anchor_join(target, d, &target) < 0) {
                st = ex_fail(k, HOL_FOREIGN, HOL_R_INCOMPATIBLE_ENV);
                goto done;
            }
        }
        if (pass == 1) break;
        /* The history may contain type definitions whose witnesses need
         * their own derivations. */
        for (HolDecl *d = target; d; d = d->parent)
            if (d->kind == HOL_DECL_TYDEF_REP) {
                if (!d->witness_event) { st = ex_fail(k, HOL_REFUSED, HOL_R_BAD_HANDLE); goto done; }
                if (!u64_push(&work, d->witness_event)) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
            }
    }
    qsort(need.v, need.n, sizeof(uint64_t), cmp_u64);
    /* The linear history, bootstrap excluded. */
    size_t nchain = 0;
    for (HolDecl *d = target; d && d != k->d_eq; d = d->parent) nchain++;
    chain = (HolDecl **)malloc((nchain ? nchain : 1) * sizeof(HolDecl *));
    if (!chain) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
    {
        size_t i = nchain;
        for (HolDecl *d = target; d && d != k->d_eq; d = d->parent) chain[--i] = d;
    }
    x.w = hol_writer_new(0, HOL_AXIOMS_OPEN);
    if (!x.w) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
    size_t ci = 0, ei = 0;
    while (ci < nchain || ei < need.n) {
        bool take_decl = ci < nchain && (ei == need.n || chain[ci]->serial < need.v[ei]);
        if (take_decl) {
            HolDecl *d = chain[ci++];
            switch (d->kind) {
            case HOL_DECL_TYPE: {
                uint64_t nm = ex_name(&x, d->name);
                hol_writer_op(x.w, HOLR_NEW_TYPE);
                hol_writer_uv(x.w, nm);
                hol_writer_uv(x.w, d->arity);
                break;
            }
            case HOL_DECL_CONST: {
                uint64_t ty = ex_type(&x, d->gtype);
                uint64_t nm = ex_name(&x, d->name);
                hol_writer_op(x.w, HOLR_NEW_CONSTANT);
                hol_writer_uv(x.w, nm);
                hol_writer_uv(x.w, ty);
                break;
            }
            case HOL_DECL_AXIOM: {
                uint64_t t = ex_term(&x, d->body);
                hol_writer_op(x.w, HOLR_NEW_AXIOM);
                hol_writer_uv(x.w, t);
                ex_thm_put(&x, d->serial, d->thm, x.nh++);
                break;
            }
            case HOL_DECL_DEF: {
                HolTerm *v = hk_tm_var(k, d->name, d->gtype);
                HolTerm *eq = v ? hk_tm_eq(k, v, d->body) : NULL;
                if (!eq) { st = k->status; goto done; }
                uint64_t t = ex_term(&x, eq);
                hol_writer_op(x.w, HOLR_NEW_DEFINITION);
                hol_writer_uv(x.w, t);
                ex_thm_put(&x, d->serial, d->thm, x.nh++);
                break;
            }
            case HOL_DECL_TYDEF:
            case HOL_DECL_TYDEF_ABS:
                break; /* emitted with the representation event */
            case HOL_DECL_TYDEF_REP: {
                HolDecl *da = d->parent, *dt = da->parent;
                uint64_t wid;
                if (!ex_thm_get(&x, d->witness_event, d->witness, &wid)) {
                    st = ex_fail(k, HOL_REFUSED, HOL_R_BAD_HANDLE);
                    goto done;
                }
                uint64_t nt = ex_name(&x, dt->name), na = ex_name(&x, da->name), nr = ex_name(&x, d->name);
                hol_writer_op(x.w, HOLR_NEW_TYPEDEF);
                hol_writer_uv(x.w, nt);
                hol_writer_uv(x.w, na);
                hol_writer_uv(x.w, nr);
                hol_writer_uv(x.w, wid);
                ex_thm_put(&x, d->serial, d->thm, x.nh++);
                ex_thm_put(&x, d->serial, d->thm2, x.nh++);
                break;
            }
            case HOL_DECL_POLICY:
                hol_writer_op(x.w, HOLR_POLICY);
                hol_writer_uv(x.w, d->policy);
                break;
            default:
                break;
            }
        } else {
            HolJEvent *e = journal_find(k, need.v[ei++]);
            uint64_t prem[2] = {0, 0};
            for (unsigned j = 0; j < e->nprem; j++)
                if (!ex_thm_get(&x, e->prem[j], e->pthm[j], &prem[j])) {
                    st = ex_fail(k, HOL_REFUSED, HOL_R_BAD_HANDLE);
                    goto done;
                }
            uint64_t args[2] = {0, 0};
            switch (e->op) {
            case HOLR_REFL: case HOLR_BETA: case HOLR_ASSUME:
                args[0] = ex_term(&x, (HolTerm *)e->args[0]);
                hol_writer_op(x.w, e->op);
                hol_writer_uv(x.w, args[0]);
                break;
            case HOLR_TRANS: case HOLR_MK_COMB: case HOLR_EQ_MP: case HOLR_DEDUCT:
                hol_writer_op(x.w, e->op);
                hol_writer_uv(x.w, prem[0]);
                hol_writer_uv(x.w, prem[1]);
                break;
            case HOLR_ABS_RULE:
                args[0] = ex_term(&x, (HolTerm *)e->args[0]);
                hol_writer_op(x.w, e->op);
                hol_writer_uv(x.w, args[0]);
                hol_writer_uv(x.w, prem[0]);
                break;
            case HOLR_INST_TYPE:
            case HOLR_INST: {
                uint64_t *ids = (uint64_t *)malloc((e->nargs ? e->nargs : 1) * sizeof(uint64_t));
                if (!ids) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
                for (uint32_t j = 0; j < e->nargs; j++)
                    ids[j] = e->op == HOLR_INST_TYPE ? ex_type(&x, (HolType *)e->args[j])
                                                     : ex_term(&x, (HolTerm *)e->args[j]);
                hol_writer_op(x.w, e->op);
                hol_writer_uv(x.w, e->nargs / 2);
                for (uint32_t j = 0; j < e->nargs; j++) hol_writer_uv(x.w, ids[j]);
                hol_writer_uv(x.w, prem[0]);
                free(ids);
                break;
            }
            default:
                st = ex_fail(k, HOL_MALFORMED, HOL_R_STREAM_OPCODE);
                goto done;
            }
            ex_thm_put(&x, e->serial, e->result, x.nh++);
        }
        if (x.fail) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
    }
    for (size_t i = 0; i < n; i++) {
        uint64_t id;
        if (!ex_thm_get(&x, rev[i], rth[i], &id)) { st = ex_fail(k, HOL_REFUSED, HOL_R_BAD_HANDLE); goto done; }
        hol_writer_op(x.w, HOLR_EMIT);
        hol_writer_uv(x.w, id);
    }
    for (size_t i = 0; i < n; i++) {
        uint64_t id;
        if (!ex_thm_get(&x, rev[i], rth[i], &id)) { st = ex_fail(k, HOL_REFUSED, HOL_R_BAD_HANDLE); goto done; }
        uint64_t *hy = (uint64_t *)malloc((rth[i]->nhyps ? rth[i]->nhyps : 1) * sizeof(uint64_t));
        if (!hy) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
        for (uint32_t j = 0; j < rth[i]->nhyps; j++) hy[j] = ex_term(&x, rth[i]->hyps[j]);
        uint64_t c = ex_term(&x, rth[i]->concl);
        hol_writer_op(x.w, HOLR_EXPECT);
        hol_writer_uv(x.w, id);
        hol_writer_uv(x.w, rth[i]->nhyps);
        for (uint32_t j = 0; j < rth[i]->nhyps; j++) hol_writer_uv(x.w, hy[j]);
        hol_writer_uv(x.w, c);
        free(hy);
    }
    {
        size_t blen;
        const uint8_t *b = hol_writer_finish(x.w, &blen);
        if (!b || x.fail) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
        *out = (uint8_t *)malloc(blen);
        if (!*out) { st = ex_fail(k, HOL_NOMEM, HOL_R_NOMEM); goto done; }
        memcpy(*out, b, blen);
        *len = blen;
    }
done:
    if (x.w) hol_writer_free(x.w);
    pmap_free(&x.names);
    pmap_free(&x.types);
    pmap_free(&x.terms);
    pmap_free(&x.thms);
    free(x.ents);
    pmap_free(&seen);
    free(need.v);
    free(work.v);
    free(chain);
    free(rth);
    free(rev);
    return st;
}
