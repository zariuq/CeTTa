#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_pfg.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mg_upstream.h"

/* ── mathdatapfg.ml terms, on an arena ───────────────────────────────── */

typedef enum { P_DB, P_TMH, P_PRIM, P_AP, P_LAM, P_IMP, P_ALL, P_EX, P_EQ } PTag;

/* Proofgold's simple types: Base, TpArr, Prop. */
typedef struct PTp PTp;
struct PTp {
    uint8_t tag;        /* MG_TP_BASE, MG_TP_ARR, MG_TP_PROP */
    uint32_t n;
    const PTp *a, *b;
};

typedef struct P P;
struct P {
    uint8_t tag;
    uint32_t n;         /* DB index, Prim index */
    MgHash h;           /* TmH */
    const PTp *a;       /* Lam, All, Ex, Eq */
    const P *x, *y, *z; /* Ap x y; Lam/All/Ex body x; Imp x y; Eq x y */
};

typedef struct Chunk Chunk;
struct Chunk {
    Chunk *next;
    size_t used, cap;
    max_align_t data[];
};

typedef struct {
    const MgPfgEnv *env;
    const MgStore *s;
    Chunk *chunks;
    jmp_buf fail;
    MgPfgStatus status;
} Pfg;

static void *alloc(Pfg *g, size_t n) {
    n = (n + sizeof(max_align_t) - 1u) / sizeof(max_align_t) * sizeof(max_align_t);
    Chunk *c = g->chunks;
    if (!c || c->used + n > c->cap) {
        size_t cap = n > 65536u ? n : 65536u;
        c = malloc(sizeof(Chunk) + cap);
        if (!c) {
            g->status = MG_PFG_NOMEM;
            longjmp(g->fail, 1);
        }
        c->next = g->chunks;
        c->used = 0u;
        c->cap = cap;
        g->chunks = c;
    }
    void *p = (char *)c->data + c->used;
    c->used += n;
    return p;
}

static void release(Pfg *g) {
    for (Chunk *c = g->chunks, *next; c; c = next) {
        next = c->next;
        free(c);
    }
    g->chunks = NULL;
}

__attribute__((noreturn, format(printf, 2, 3)))
static void failure(Pfg *g, const char *fmt, ...) {
    if (g->env->message) {
        mg_buf_reset(g->env->message);
        va_list ap;
        va_start(ap, fmt);
        char small[512];
        vsnprintf(small, sizeof small, fmt, ap);
        va_end(ap);
        mg_buf_puts(g->env->message, small);
    }
    g->status = MG_PFG_FAILURE;
    longjmp(g->fail, 1);
}

/* A failure whose message ends in a term (tm_to_str), which can be long. */
__attribute__((noreturn))
static void failure_tm(Pfg *g, const char *prefix, const MgTm *m) {
    if (g->env->message) {
        mg_buf_reset(g->env->message);
        mg_buf_puts(g->env->message, prefix);
        mg_front_tm_str(g->env->message, g->s, m);
    }
    g->status = MG_PFG_FAILURE;
    longjmp(g->fail, 1);
}

static const PTp *ptp(Pfg *g, uint8_t tag, uint32_t n, const PTp *a, const PTp *b) {
    PTp *t = alloc(g, sizeof *t);
    *t = (PTp){tag, n, a, b};
    return t;
}

static const P *mk(Pfg *g, uint8_t tag, uint32_t n, const PTp *a, const P *x, const P *y,
                   const P *z) {
    P *p = alloc(g, sizeof *p);
    memset(p, 0, sizeof *p);
    p->tag = tag;
    p->n = n;
    p->a = a;
    p->x = x;
    p->y = y;
    p->z = z;
    return p;
}

static const P *db(Pfg *g, uint32_t i) { return mk(g, P_DB, i, NULL, NULL, NULL, NULL); }
static const P *ap(Pfg *g, const P *f, const P *x) { return mk(g, P_AP, 0, NULL, f, x, NULL); }
static const P *lam(Pfg *g, const PTp *a, const P *b) { return mk(g, P_LAM, 0, a, b, NULL, NULL); }
static const P *imp(Pfg *g, const P *p, const P *q) { return mk(g, P_IMP, 0, NULL, p, q, NULL); }
static const P *all(Pfg *g, const PTp *a, const P *b) { return mk(g, P_ALL, 0, a, b, NULL, NULL); }
static const P *ex(Pfg *g, const PTp *a, const P *b) { return mk(g, P_EX, 0, a, b, NULL, NULL); }
static const P *eq(Pfg *g, const PTp *a, const P *x, const P *y) {
    return mk(g, P_EQ, 0, a, x, y, NULL);
}

/* ── mathdatapfg.ml uptrm, subst_trmtrm, beta_eta_norm ───────────────── */

/* uptrm t i j; NULL for NotEta (a de Bruijn index made negative). */
static const P *uptrm(Pfg *g, const P *t, uint32_t i, int32_t j) {
    switch (t->tag) {
    case P_DB: {
        if (t->n < i)
            return t;
        int64_t k2 = (int64_t)t->n + j;
        return k2 < 0 ? NULL : db(g, (uint32_t)k2);
    }
    case P_AP:
    case P_IMP: {
        const P *x = uptrm(g, t->x, i, j);
        const P *y = x ? uptrm(g, t->y, i, j) : NULL;
        return y ? mk(g, t->tag, 0, NULL, x, y, NULL) : NULL;
    }
    case P_LAM:
    case P_ALL:
    case P_EX: {
        const P *x = uptrm(g, t->x, i + 1u, j);
        return x ? mk(g, t->tag, 0, t->a, x, NULL, NULL) : NULL;
    }
    case P_EQ: {
        const P *x = uptrm(g, t->x, i, j);
        const P *y = x ? uptrm(g, t->y, i, j) : NULL;
        return y ? eq(g, t->a, x, y) : NULL;
    }
    default:
        return t;
    }
}

/* An uptrm whose shift is nonnegative cannot fail. */
static const P *up(Pfg *g, const P *t, uint32_t i, int32_t j) {
    const P *r = uptrm(g, t, i, j);
    if (!r)
        failure(g, "pfg: negative de Bruijn shift");
    return r;
}

static const P *subst(Pfg *g, const P *t, uint32_t j, const P *s) {
    switch (t->tag) {
    case P_DB:
        if (t->n == j)
            return up(g, s, 0u, (int32_t)j);
        return j < t->n ? db(g, t->n - 1u) : t;
    case P_AP:
    case P_IMP:
        return mk(g, t->tag, 0, NULL, subst(g, t->x, j, s), subst(g, t->y, j, s), NULL);
    case P_LAM:
    case P_ALL:
    case P_EX:
        return mk(g, t->tag, 0, t->a, subst(g, t->x, j + 1u, s), NULL, NULL);
    case P_EQ:
        return eq(g, t->a, subst(g, t->x, j, s), subst(g, t->y, j, s));
    default:
        return t;
    }
}

/* beta_eta_norm1: one pass; *done when nothing was rewritten. */
static const P *norm1(Pfg *g, const P *t, bool *done) {
    switch (t->tag) {
    case P_AP: {
        bool r1, r2;
        const P *t2 = norm1(g, t->x, &r1);
        const P *t3 = norm1(g, t->y, &r2);
        if (t2->tag == P_LAM) {
            *done = false;
            return subst(g, t2->x, 0u, t3);
        }
        *done = r1 && r2;
        return ap(g, t2, t3);
    }
    case P_LAM: {
        bool r1;
        const P *t2 = norm1(g, t->x, &r1);
        if (t2->tag == P_AP && t2->y->tag == P_DB && t2->y->n == 0u) {
            const P *m = uptrm(g, t2->x, 0u, -1);
            if (m) {
                *done = false;
                return m;
            }
        }
        *done = r1;
        return lam(g, t->a, t2);
    }
    case P_IMP: {
        bool r1, r2;
        const P *t2 = norm1(g, t->x, &r1);
        const P *t3 = norm1(g, t->y, &r2);
        *done = r1 && r2;
        return imp(g, t2, t3);
    }
    case P_ALL: {
        bool r1;
        const P *t2 = norm1(g, t->x, &r1);
        *done = r1;
        return all(g, t->a, t2);
    }
    case P_EX: {
        bool ignored;
        const P *n1 = norm1(g, t->x, &ignored);
        const P *n1b = up(g, n1, 1u, 1);
        const PTp *prop = ptp(g, MG_TP_PROP, 0, NULL, NULL);
        *done = false;
        return all(g, prop, imp(g, all(g, t->a, imp(g, n1b, db(g, 1u))), db(g, 0u)));
    }
    case P_EQ: {
        bool ignored;
        const P *n1 = norm1(g, t->x, &ignored);
        const P *n2 = norm1(g, t->y, &ignored);
        const P *n1b = up(g, n1, 0u, 1);
        const P *n2b = up(g, n2, 0u, 1);
        const PTp *prop = ptp(g, MG_TP_PROP, 0, NULL, NULL);
        const PTp *rel = ptp(g, MG_TP_ARR, 0, t->a, ptp(g, MG_TP_ARR, 0, t->a, prop));
        *done = false;
        return all(g, rel, imp(g, ap(g, ap(g, db(g, 0u), n1b), n2b),
                               ap(g, ap(g, db(g, 0u), n2b), n1b)));
    }
    default:
        *done = true;
        return t;
    }
}

static const P *beta_eta_norm(Pfg *g, const P *t, int count) {
    for (;;) {
        if (count <= 0) {
            g->status = MG_PFG_REDEXMAX;
            longjmp(g->fail, 1);
        }
        bool done;
        t = norm1(g, t, &done);
        if (done)
            return t;
        --count;
    }
}

/* ── hashing (mathdatapfg.ml hashtp, tm_hashroot) ────────────────────── */

static void ser_tp(Pfg *g, MgBits *w, const PTp *a) {
    switch (a->tag) {
    case MG_TP_ARR:
        mg_bits_put(w, 0u, 2);
        ser_tp(g, w, a->a);
        ser_tp(g, w, a->b);
        return;
    case MG_TP_PROP:
        mg_bits_put(w, 2u, 2);
        return;
    default:
        mg_bits_put(w, 1u, 1);
        if (!mg_bits_varintb(w, a->n))
            failure(g, "Invalid base type");
        return;
    }
}

static MgHash hashtp(Pfg *g, const PTp *a) {
    MgBits w = {0};
    ser_tp(g, &w, a);
    MgHash digest;
    if (!mg_bits_digest(&w, &digest)) {
        g->status = MG_PFG_NOMEM;
        longjmp(g->fail, 1);
    }
    return mg_hash_tag(&digest, 64u);
}

static MgHash hashroot(Pfg *g, const P *m) {
    MgHash a, b, p;
    switch (m->tag) {
    case P_TMH:
        return m->h;
    case P_PRIM:
        a = mg_hash_int32(m->n);
        return mg_hash_tag(&a, 96u);
    case P_DB:
        a = mg_hash_int32(m->n);
        return mg_hash_tag(&a, 97u);
    case P_AP:
    case P_IMP:
        a = hashroot(g, m->x);
        b = hashroot(g, m->y);
        p = mg_hash_pair(&a, &b);
        return mg_hash_tag(&p, m->tag == P_AP ? 98u : 100u);
    case P_LAM:
    case P_ALL:
    case P_EX:
        a = hashtp(g, m->a);
        b = hashroot(g, m->x);
        p = mg_hash_pair(&a, &b);
        return mg_hash_tag(&p, m->tag == P_LAM ? 99u : m->tag == P_ALL ? 101u : 102u);
    default: { /* P_EQ */
        a = hashtp(g, m->a);
        b = hashroot(g, m->x);
        MgHash c = hashroot(g, m->y);
        p = mg_hash_pair(&a, &b);
        MgHash q = mg_hash_pair(&p, &c);
        return mg_hash_tag(&q, 103u);
    }
    }
}

/* ── the theories' translations (syntax.ml to_mathdatapfg*_tm) ───────── */

static const char HEX_EX[] = "912ad2cdc2d23bb8aa0a5070945f2a90976a948b0e8308917244591f3747f099";
static const char HEX_EQ[] = "5a6af35fb6d6bea477dd0f822b8e01ca0d57cc50dfd41744307bc94597fdaa4a";
static const char HEX_NEQ[] = "7966a66a9bb198103c2a540ccd5ebebdff33c10843cc10eebfc98715e142989c";

static const PTp *to_tp(Pfg *g, const MgTp *a) {
    switch (a->tag) {
    case MG_TP_PROP: return ptp(g, MG_TP_PROP, 0, NULL, NULL);
    case MG_TP_BASE: return ptp(g, MG_TP_BASE, a->n, NULL, NULL);
    case MG_TP_ARR: return ptp(g, MG_TP_ARR, 0, to_tp(g, a->a), to_tp(g, a->b));
    default: failure(g, "no poly in pfg");
    }
}

static const char *name_hex(Pfg *g, uint32_t id) {
    const MgName *n = mg_name_get(g->s, id);
    return n ? n->text : "";
}

/* TAP(NAME h, a) with h one of ex/eq/neq: which, and a. */
static int special(Pfg *g, const MgTm *m, const MgTp **a) {
    if (m->tag != MG_TM_TAP || m->f->tag != MG_TM_NAME)
        return 0;
    const char *h = name_hex(g, m->f->n);
    *a = m->tp;
    if (strcmp(h, HEX_EX) == 0) return 1;
    if (strcmp(h, HEX_EQ) == 0) return 2;
    if (strcmp(h, HEX_NEQ) == 0) return 3;
    return 0;
}

static const P *pfgex(Pfg *g, const PTp *a, const P *m) {
    return ex(g, a, ap(g, up(g, m, 0u, 1), db(g, 0u)));
}

/* Megalodon's tmshift 0 1 on a front-end term, which cannot fail. */
static const MgTm *shift1(Pfg *g, const MgTm *m, uint32_t i) {
    MgStore *s = (MgStore *)g->s;
    switch (m->tag) {
    case MG_TM_DB:
        return m->n < i ? m : mg_tm_db(s, m->n + 1u);
    case MG_TM_TAP:
        return mg_tm_tap(s, (MgTm *)shift1(g, m->f, i), m->tp);
    case MG_TM_AP:
        return mg_tm_ap(s, (MgTm *)shift1(g, m->f, i), (MgTm *)shift1(g, m->x, i));
    case MG_TM_IMP:
        return mg_tm_imp(s, (MgTm *)shift1(g, m->f, i), (MgTm *)shift1(g, m->x, i));
    case MG_TM_LAM:
        return mg_tm_lam(s, m->tp, (MgTm *)shift1(g, m->f, i + 1u));
    case MG_TM_ALL:
        return mg_tm_all(s, m->tp, (MgTm *)shift1(g, m->f, i + 1u));
    default:
        return m;
    }
}

static const P *to_tm(Pfg *g, MgProfileTag thy, const MgTm *m);

static const P *not_eq(Pfg *g, MgProfileTag thy, const P *e) {
    if (thy == MG_PROFILE_HF)
        return ap(g, mk(g, P_PRIM, 3u, NULL, NULL, NULL, NULL), e);
    const PTp *prop = ptp(g, MG_TP_PROP, 0, NULL, NULL);
    return imp(g, e, all(g, prop, db(g, 0u)));
}

static const char *thy_tag(MgProfileTag thy) {
    switch (thy) {
    case MG_PROFILE_HF: return "hf";
    case MG_PROFILE_EGAL: return "egal";
    case MG_PROFILE_MIZAR: return "miz";
    case MG_PROFILE_SETMM: return "setmm";
    default: return "hoas";
    }
}

static const P *to_tm(Pfg *g, MgProfileTag thy, const MgTm *m) {
    switch (m->tag) {
    case MG_TM_DB:
        return db(g, m->n);
    case MG_TM_NAME: {
        const char *hex = name_hex(g, m->n);
        uint32_t j;
        if (mg_upstream_pfg_prim(thy, hex, &j))
            return mk(g, P_PRIM, j, NULL, NULL, NULL, NULL);
        P *t = (P *)mk(g, P_TMH, 0, NULL, NULL, NULL, NULL);
        if (g->env->pure && g->env->pure(g->env->ctx, m->n, &t->h))
            return t;
        failure(g, "Could not find corresponding obj id for TmH(%s)\n", hex);
    }
    case MG_TM_PRIM: {
        uint32_t j = m->n;
        switch (thy) {
        case MG_PROFILE_HF: {
            /* Eps In Empty Union Power Repl UnivOf */
            static const int32_t hf[] = {-1, 7, 9, 10, 11, 12, -1};
            if (j < 7u && hf[j] >= 0)
                return mk(g, P_PRIM, (uint32_t)hf[j], NULL, NULL, NULL, NULL);
            failure(g, "bad prim %u case for pfg hf", j);
        }
        case MG_PROFILE_EGAL:
            if (j > 0u)
                return mk(g, P_PRIM, j, NULL, NULL, NULL, NULL);
            failure(g, "bad prim %u case for pfg egal", j);
        case MG_PROFILE_MIZAR:
            if (j == 1u || j == 3u)
                return mk(g, P_PRIM, j, NULL, NULL, NULL, NULL);
            failure(g, "bad prim %u case for pfg miz", j);
        case MG_PROFILE_SETMM:
            if (j < 1291u)
                return mk(g, P_PRIM, j, NULL, NULL, NULL, NULL);
            failure(g, "bad prim %u case for pfg setmm", j);
        default:
            failure(g, "bad prim %u case for pfg hoas", j);
        }
    }
    default:
        break;
    }
    /* ex, eq and neq at their instances, except in SetMM */
    if (thy != MG_PROFILE_SETMM) {
        const MgTp *a = NULL;
        /* HOAS translates neq's operands as Mizar does */
        MgProfileTag nthy = thy == MG_PROFILE_HOAS ? MG_PROFILE_MIZAR : thy;
        /* OCaml evaluates constructor and tuple arguments right to left,
         * which decides the failure reported first. */
        if (m->tag == MG_TM_AP) {
            int k = special(g, m->f, &a);
            if (k == 1) {
                const P *body = to_tm(g, thy, m->x);
                return pfgex(g, to_tp(g, a), body);
            }
            if (k == 2) {
                const PTp *a1 = to_tp(g, a);
                return lam(g, a1, eq(g, a1, to_tm(g, thy, shift1(g, m->x, 0u)), db(g, 0u)));
            }
            if (k == 3) {
                const PTp *a1 = to_tp(g, a);
                return lam(g, a1, not_eq(g, thy, eq(g, a1, to_tm(g, nthy, shift1(g, m->x, 0u)),
                                                     db(g, 0u))));
            }
            if (m->f->tag == MG_TM_AP) {
                k = special(g, m->f->f, &a);
                if (k == 2 || k == 3) {
                    MgProfileTag ot = k == 2 ? thy : nthy;
                    const P *y = to_tm(g, ot, m->x);
                    const P *x = to_tm(g, ot, m->f->x);
                    const P *e = eq(g, to_tp(g, a), x, y);
                    return k == 2 ? e : not_eq(g, thy, e);
                }
            }
        } else if (m->tag == MG_TM_TAP) {
            int k = special(g, m, &a);
            if (k == 2 || k == 3) {
                const PTp *a1 = to_tp(g, a);
                const P *e = eq(g, a1, db(g, 1u), db(g, 0u));
                return lam(g, a1, lam(g, a1, k == 2 ? e : not_eq(g, thy, e)));
            }
        }
    }
    switch (m->tag) {
    case MG_TM_TAP: {
        char prefix[64];
        snprintf(prefix, sizeof prefix, "no polymorphism allowed in pfg: tm_pfg%s ",
                 thy == MG_PROFILE_HOAS ? "hf" : thy_tag(thy));
        failure_tm(g, prefix, m);
    }
    case MG_TM_AP:
    case MG_TM_IMP: {
        const P *y = to_tm(g, thy, m->x);
        const P *x = to_tm(g, thy, m->f);
        return m->tag == MG_TM_AP ? ap(g, x, y) : imp(g, x, y);
    }
    case MG_TM_LAM:
    case MG_TM_ALL: {
        const P *body = to_tm(g, thy, m->f);
        const PTp *a = to_tp(g, m->tp);
        return m->tag == MG_TM_LAM ? lam(g, a, body) : all(g, a, body);
    }
    default:
        failure(g, "pfg: unexpected term");
    }
}

static const MgHash *thyid(MgProfileTag thy) {
    static const char *const ids[MG_PROFILE_COUNT] = {
        [MG_PROFILE_HF] = "6ffc9680fbe00a58d70cdeb319f11205ed998131ce51bb96f16c7904faf74a3d",
        [MG_PROFILE_EGAL] = "29c988c5e6c620410ef4e61bcfcbe4213c77013974af40759d8b732c07d61967",
        [MG_PROFILE_MIZAR] = "5ab3df7b0b4ef20889de0517a318df8746940971ad9b2021e54c820eb9e74dce",
        [MG_PROFILE_HOAS] = "513140056e2032628f48d11e221efe29892e9a03a661d3b691793524a5176ede",
        [MG_PROFILE_SETMM] = "85ecfdcf26657b94532af5af2393c6945cee05c4aabccb8a819f793a7dbc4acf",
    };
    static MgHash parsed[MG_PROFILE_COUNT];
    static bool ready[MG_PROFILE_COUNT];
    if ((int)thy < 0 || thy >= MG_PROFILE_COUNT || !ids[thy])
        return NULL;
    if (!ready[thy]) {
        mg_hash_parse(ids[thy], 64u, &parsed[thy]);
        ready[thy] = true;
    }
    return &parsed[thy];
}

static MgPfgStatus pure_root(const MgPfgEnv *env, const MgStore *s, const MgTm *normal,
                             MgHash *pure) {
    Pfg g = {.env = env, .s = s, .status = MG_PFG_OK};
    if (setjmp(g.fail)) {
        release(&g);
        return g.status;
    }
    const P *m3 = beta_eta_norm(&g, to_tm(&g, env->theory, normal), 20);
    *pure = hashroot(&g, m3);
    release(&g);
    return MG_PFG_OK;
}

MgPfgStatus mg_pfg_propid(const MgPfgEnv *env, const MgStore *s, const MgTm *normal,
                          MgHash *pure, MgHash *propid) {
    const MgHash *id = thyid(env->theory);
    if (!id)
        return MG_PFG_FAILURE;
    MgPfgStatus st = pure_root(env, s, normal, pure);
    if (st != MG_PFG_OK)
        return st;
    MgHash p = mg_hash_pair(id, pure);
    *propid = mg_hash_tag(&p, 33u);
    return MG_PFG_OK;
}

MgPfgStatus mg_pfg_objid_of_pure(const MgPfgEnv *env, const MgHash *pure, const MgTp *tp,
                                 MgHash *objid) {
    const MgHash *id = thyid(env->theory);
    if (!id)
        return MG_PFG_FAILURE;
    Pfg g = {.env = env, .s = NULL, .status = MG_PFG_OK};
    if (setjmp(g.fail)) {
        release(&g);
        return g.status;
    }
    MgHash ah = hashtp(&g, to_tp(&g, tp));
    release(&g);
    MgHash inner = mg_hash_pair(pure, &ah);
    MgHash p = mg_hash_pair(id, &inner);
    *objid = mg_hash_tag(&p, 32u);
    return MG_PFG_OK;
}

MgPfgStatus mg_pfg_objid(const MgPfgEnv *env, const MgStore *s, const MgTm *normal,
                         const MgTp *tp, MgHash *pure, MgHash *objid) {
    MgPfgStatus st = pure_root(env, s, normal, pure);
    return st == MG_PFG_OK ? mg_pfg_objid_of_pure(env, pure, tp, objid) : st;
}
