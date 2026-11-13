#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_elab.h"
#include "mg_elab_internal.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "mg_front.h"
#include "mg_hash.h"
#include "mg_library.h"
#include "mg_pfg.h"
#include "mg_profiles.h"
#include "mg_upstream.h"

/*
 * The port follows megalodon.ml and interpret.ml case for case.  Where
 * OCaml raises Failure the evaluation stops with Megalodon's message;
 * where it raises Not_found and a handler further out catches it, the same
 * handler catches it here (RAISE_NOT_FOUND).  OCaml evaluates the
 * arguments of a constructor right to left, which decides which of two
 * failing subterms is reported; the code sequences such calls explicitly.
 */

__attribute__((noreturn)) void mge_raise_kind(MgElab *e, int kind) {
    /* once the theorem's budget ran out, a failure is not a verdict */
    if (e->budget.stop == MG_EXHAUSTED && kind != RAISE_FATAL)
        kind = RAISE_EXHAUSTED;
    longjmp(*e->fail, kind);
}

__attribute__((noreturn)) void mge_nomem(MgElab *e) {
    mg_buf_reset(&e->msg);
    mg_buf_puts(&e->msg, "Out of memory");
    mge_raise_kind(e, RAISE_FAILURE);
}

__attribute__((noreturn, format(printf, 2, 3)))
void mge_fail(MgElab *e, const char *fmt, ...) {
    mg_buf_reset(&e->msg);
    va_list ap;
    va_start(ap, fmt);
    char small[1024];
    vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    mg_buf_puts(&e->msg, small);
    mge_raise_kind(e, RAISE_FAILURE);
}

__attribute__((noreturn)) void mge_not_found(MgElab *e) {
    mge_raise_kind(e, RAISE_NOT_FOUND);
}

void *mge_chunk_alloc(MgElab *e, Chunk **head, size_t n) {
    n = (n + sizeof(max_align_t) - 1u) / sizeof(max_align_t) * sizeof(max_align_t);
    Chunk *c = *head;
    if (!c || c->used + n > c->cap) {
        size_t cap = n > (1u << 16) ? n : (1u << 16);
        c = malloc(sizeof(Chunk) + cap);
        if (!c)
            mge_nomem(e);
        c->next = *head;
        c->used = 0u;
        c->cap = cap;
        *head = c;
    }
    void *p = (char *)c->data + c->used;
    c->used += n;
    return p;
}

static void chunks_free(Chunk **head) {
    for (Chunk *c = *head, *next; c; c = next) {
        next = c->next;
        free(c);
    }
    *head = NULL;
}


static uint64_t mix(uint64_t k) {
    k ^= k >> 33;
    k *= UINT64_C(0xff51afd7ed558ccd);
    k ^= k >> 33;
    k *= UINT64_C(0xc4ceb9fe1a85ec53);
    k ^= k >> 33;
    return k;
}

static Slot *map_slot(const Map *m, uint64_t key) {
    if (!m->cap)
        return NULL;
    size_t i = mix(key) & (m->cap - 1u);
    while (m->slots[i].used) {
        if (m->slots[i].key == key)
            return &m->slots[i];
        i = (i + 1u) & (m->cap - 1u);
    }
    return NULL;
}

bool mge_map_get(const Map *m, uint64_t key, uint64_t *val) {
    Slot *s = map_slot(m, key);
    if (!s)
        return false;
    if (val)
        *val = s->val;
    return true;
}

void mge_conv_forget(MgElab *e) {
    ++e->conv_epoch;
    e->refuted_len = 0;
}

void mge_map_set(MgElab *e, Map *m, uint64_t key, uint64_t val) {
    if (m == &e->sigdelta || m == &e->sigdelta_opaque)
        mge_conv_forget(e);
    Slot *hit = map_slot(m, key);
    if (hit) {
        hit->val = val;
        return;
    }
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 64u;
        Slot *grown = calloc(cap, sizeof(Slot));
        if (!grown)
            mge_nomem(e);
        for (size_t i = 0; i < m->cap; ++i) {
            if (!m->slots[i].used)
                continue;
            size_t j = mix(m->slots[i].key) & (cap - 1u);
            while (grown[j].used)
                j = (j + 1u) & (cap - 1u);
            grown[j] = m->slots[i];
        }
        free(m->slots);
        m->slots = grown;
        m->cap = cap;
    }
    size_t i = mix(key) & (m->cap - 1u);
    while (m->slots[i].used)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (Slot){key, val, true};
    ++m->len;
}

void mge_map_clear(Map *m) {
    if (m->slots)
        memset(m->slots, 0, m->cap * sizeof(Slot));
    m->len = 0u;
}

static void map_free(Map *m) {
    free(m->slots);
    *m = (Map){0};
}


/* Interned strings: equal names are one pointer. */
static uint64_t str_hash(const char *s) {
    uint64_t h = UINT64_C(1469598103934665603);
    for (; *s; ++s)
        h = (h ^ (unsigned char)*s) * UINT64_C(1099511628211);
    return h;
}

const char *mge_intern(MgElab *e, const char *s) {
    uint64_t h = str_hash(s);
    for (uint64_t k = h;; ++k) {
        uint64_t v;
        if (!mge_map_get(&e->strs, k, &v)) {
            size_t n = strlen(s) + 1u;
            char *copy = mge_chunk_alloc(e, &e->perm, n);
            memcpy(copy, s, n);
            mge_map_set(e, &e->strs, k, V(copy));
            return copy;
        }
        if (strcmp(P(const char, v), s) == 0)
            return P(const char, v);
    }
}

void mge_stk_push(MgElab *e, Map *m, uint64_t key, uint64_t val) {
    Stk *n = PERM(e, Stk);
    uint64_t top = 0;
    mge_map_get(m, key, &top);
    n->val = val;
    n->next = P(const Stk, top);
    mge_map_set(e, m, key, V(n));
}

void mge_stk_pop(MgElab *e, Map *m, uint64_t key) {
    uint64_t top = 0;
    if (mge_map_get(m, key, &top) && top)
        mge_map_set(e, m, key, V(P(const Stk, top)->next));
}

bool mge_stk_top(const Map *m, uint64_t key, uint64_t *val) {
    uint64_t top = 0;
    if (!mge_map_get(m, key, &top) || !top)
        return false;
    *val = P(const Stk, top)->val;
    return true;
}

/* ── kernel terms in the front end's conventions ─────────────────────── */

MgTm *mge_ck(MgElab *e, MgTm *m) {
    if (!m)
        mge_nomem(e);
    return m;
}

MgTp *mge_ckt(MgElab *e, MgTp *a) {
    if (!a)
        mge_nomem(e);
    return a;
}

MgTp *mge_ty_prop(MgElab *e) { return mge_ckt(e, mg_tp_prop(e->s)); }
MgTp *mge_ty_set(MgElab *e) { return mge_ckt(e, mg_tp_base(e->s, 0)); }
MgTp *mge_ty_var(MgElab *e, uint32_t i) { return mge_ckt(e, mg_tp_var(e->s, i)); }
MgTp *mge_ty_arr(MgElab *e, MgTp *a, MgTp *b) { return mge_ckt(e, mg_tp_arr(e->s, a, b)); }
MgTm *mge_tm_db(MgElab *e, uint32_t i) { return mge_ck(e, mg_tm_db(e->s, i)); }
MgTm *mge_tm_prim(MgElab *e, uint32_t i) { return mge_ck(e, mg_tm_prim(e->s, i)); }
MgTm *mge_tm_ap(MgElab *e, MgTm *f, MgTm *x) { return mge_ck(e, mg_tm_ap(e->s, f, x)); }
MgTm *mge_tm_lam(MgElab *e, MgTp *a, MgTm *b) { return mge_ck(e, mg_tm_lam(e->s, a, b)); }
MgTm *mge_tm_imp(MgElab *e, MgTm *p, MgTm *q) { return mge_ck(e, mg_tm_imp(e->s, p, q)); }
MgTm *mge_tm_all(MgElab *e, MgTp *a, MgTm *b) { return mge_ck(e, mg_tm_all(e->s, a, b)); }
MgTm *mge_tm_tap(MgElab *e, MgTm *f, MgTp *a) { return mge_ck(e, mg_tm_tap(e->s, f, a)); }

uint32_t mge_name_id(MgElab *e, const char *text) {
    uint32_t id = mg_name_intern(e->s, text, strlen(text));
    if (id == UINT32_MAX)
        mge_nomem(e);
    return id;
}

const char *mge_name_text(MgElab *e, uint32_t id) {
    const MgName *n = mg_name_get(e->s, id);
    return n ? n->text : "";
}

MgTm *mge_tm_h(MgElab *e, uint32_t id) { return mge_ck(e, mg_tm_name(e->s, id)); }

/* syntax.ml tmshift i j m (a negative result raises NegDB). */
MgTm *mge_tmshift(MgElab *e, uint32_t i, int32_t j, MgTm *m) {
    if (j == 0 || m->fv <= i)
        return m;
    switch (m->tag) {
    case MG_TM_DB: {
        int64_t l = (int64_t)m->n + j;
        if (l < (int64_t)i)
            mge_raise_kind(e, RAISE_NEGDB);
        return mge_tm_db(e, (uint32_t)l);
    }
    case MG_TM_TAP:
        return mge_tm_tap(e, mge_tmshift(e, i, j, m->f), m->tp);
    case MG_TM_AP:
        return mge_tm_ap(e, mge_tmshift(e, i, j, m->f), mge_tmshift(e, i, j, m->x));
    case MG_TM_IMP:
        return mge_tm_imp(e, mge_tmshift(e, i, j, m->f), mge_tmshift(e, i, j, m->x));
    case MG_TM_LAM:
        return mge_tm_lam(e, m->tp, mge_tmshift(e, i + 1u, j, m->f));
    case MG_TM_ALL:
        return mge_tm_all(e, m->tp, mge_tmshift(e, i + 1u, j, m->f));
    default:
        return m;
    }
}

MgTm *mge_shift1(MgElab *e, MgTm *m) { return mge_tmshift(e, 0u, 1, m); }

/* syntax.ml tpsubst a cl */
MgTp *mge_tpsubst(MgElab *e, MgTp *a, MgTp *const *cl, uint32_t n) {
    if (!a->ftv)
        return a;
    switch (a->tag) {
    case MG_TP_VAR:
        if (a->n >= n)
            mge_fail(e, "nth");
        return cl[a->n];
    case MG_TP_ARR: {
        MgTp *b = mge_tpsubst(e, a->b, cl, n);
        return mge_ty_arr(e, mge_tpsubst(e, a->a, cl, n), b);
    }
    default:
        return a;
    }
}

/* syntax.ml free_in_tm_p */
bool mge_notpvarsp(const MgTp *a) { return a->ftv == 0u; }

/* syntax.ml gen_lam_body */
MgTm *mge_gen_lam_body(MgElab *e, MgTm *m) {
    if (m->tag == MG_TM_LAM)
        return m->f;
    return mge_tm_ap(e, mge_shift1(e, m), mge_tm_db(e, 0u));
}

/* syntax.ml tp_args_rtp */
static MgTp *tp_rtp(MgTp *a) {
    while (a->tag == MG_TP_ARR)
        a = a->b;
    return a;
}

/* syntax.ml mk_forall_tm, with the connective as a kind: 0 Imp, else
 * Ap (Ap (TmH op) a) b. */
static MgTm *mk_forall_tm(MgElab *e, MgTp *tp, uint32_t op, MgTm *m, MgTm *n) {
    if (tp->tag == MG_TP_ARR) {
        MgTm *mb = mge_gen_lam_body(e, m);
        MgTm *nb = mge_gen_lam_body(e, n);
        return mge_tm_all(e, tp->a, mk_forall_tm(e, tp->b, op, mb, nb));
    }
    if (op == NONE)
        return mge_tm_imp(e, m, n);
    return mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, op), m), n);
}

/* interpret.ml tm_buildnat */
static MgTm *tm_buildnat(MgElab *e, uint32_t h0, uint32_t hS, int64_t n) {
    MgTm *r = mge_tm_h(e, h0);
    for (int64_t i = 0; i < n; ++i)
        r = mge_tm_ap(e, mge_tm_h(e, hS), r);
    return r;
}

static MgTm *egal_cons(MgElab *e, uint32_t ch, MgTm *x, MgTm *s) {
    return mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, ch), x), s);
}

static MgTm *egal_byte(MgElab *e, uint32_t h0, uint32_t hS, uint32_t ch, int by) {
    MgTm *r = mge_tm_h(e, h0);
    for (int bit = 7; bit >= 0; --bit) {
        MgTm *b = (by >> bit) & 1 ? mge_tm_ap(e, mge_tm_h(e, hS), mge_tm_h(e, h0)) : mge_tm_h(e, h0);
        r = egal_cons(e, ch, b, r);
    }
    return r;
}

static const char HEX_IN[] = "73c9efe869770ab42f7cde0b33fe26bbc3e2bd157dad141c0c27d1e7348d60f5";
static const char HEX_PAIR[] = "d58762d200971dcc7f1850726d9f2328403127deeba124fc3ba2d2d9f7c3cb8c";

static MgTm *hoas_bind(MgElab *e, MgTm *w) {
    return mge_tm_ap(e, mge_tm_h(e, mge_name_id(e, HEX_IN)), mge_tm_lam(e, mge_ty_set(e), w));
}

static MgTm *hoas_pair(MgElab *e, MgTm *m, MgTm *n) {
    return mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, mge_name_id(e, HEX_PAIR)), m), n);
}

static MgTm *hoas_hex_a(MgElab *e, int h) {
    return hoas_pair(e, hoas_pair(e, mge_tm_db(e, (uint32_t)((h >> 3) & 1)), mge_tm_db(e, (uint32_t)((h >> 2) & 1))),
                     hoas_pair(e, mge_tm_db(e, (uint32_t)((h >> 1) & 1)), mge_tm_db(e, (uint32_t)(h & 1))));
}

static MgTm *hoas_byte(MgElab *e, int by) {
    return hoas_bind(e, hoas_bind(e, hoas_pair(e, hoas_hex_a(e, (by >> 4) & 15), hoas_hex_a(e, by & 15))));
}

/* OCaml int_of_string on a decimal numeral. */
static int64_t int_of_string(MgElab *e, const char *digits) {
    int64_t n = 0;
    if (!*digits)
        mge_fail(e, "int_of_string");
    for (const char *p = digits; *p; ++p) {
        if (*p < '0' || *p > '9')
            mge_fail(e, "int_of_string");
        if (n > (INT64_C(4611686018427387903) - (*p - '0')) / 10)
            mge_fail(e, "int_of_string");
        n = n * 10 + (*p - '0');
    }
    return n;
}

/* ── messages ─────────────────────────────────────────────────────────── */

const char *mge_tp_str(MgElab *e, const MgTp *a) {
    MgBuf b = {0};
    mg_front_tp_str(&b, a);
    const char *text = mge_intern(e, mg_buf_text(&b));
    mg_buf_free(&b);
    return text;
}

/* ── the atree view of read terms ─────────────────────────────────────── */

/* ltree_to_atree drops the parentheses of ParenL (a, []). */
const MgL *mge_S(const MgL *l) {
    while (l && l->kind == MG_L_PAREN && l->list_len == 0u)
        l = l->a;
    return l;
}

bool mge_is_name(const MgL *l, const char *x) {
    l = mge_S(l);
    return l && l->kind == MG_L_NAME && strcmp(l->text, x) == 0;
}

/* Nu (false, n, None, None) */
static bool is_nat(const MgL *l) {
    l = mge_S(l);
    return l && l->kind == MG_L_NUM && !l->negative && !l->text2 && !l->text3;
}

static const char *perm_str(MgElab *e, const char *s) { return s ? mge_intern(e, s) : NULL; }

/* A persistent copy of a read term (notation semantics outlive the item). */
static const MgL *copy_l(MgElab *e, const MgL *l);

static const MgLAsc *copy_asc(MgElab *e, const MgLAsc *a) {
    if (!a)
        return NULL;
    MgLAsc *c = PERM(e, MgLAsc);
    c->kind = a->kind;
    c->type = (MgL *)copy_l(e, a->type);
    return c;
}

static const MgL *copy_l(MgElab *e, const MgL *l) {
    if (!l)
        return NULL;
    MgL *c = PERM(e, MgL);
    *c = *l;
    c->text = perm_str(e, l->text);
    c->text2 = perm_str(e, l->text2);
    c->text3 = perm_str(e, l->text3);
    c->asc = copy_asc(e, l->asc);
    c->a = (MgL *)copy_l(e, l->a);
    c->b = (MgL *)copy_l(e, l->b);
    c->c = (MgL *)copy_l(e, l->c);
    if (l->list_len) {
        c->list = mge_chunk_alloc(e, &e->perm, sizeof(MgL *) * l->list_len);
        for (uint32_t i = 0; i < l->list_len; ++i)
            c->list[i] = (MgL *)copy_l(e, l->list[i]);
    }
    if (l->names_len) {
        c->names = mge_chunk_alloc(e, &e->perm, sizeof(char *) * l->names_len);
        for (uint32_t i = 0; i < l->names_len; ++i)
            c->names[i] = mge_intern(e, l->names[i]);
    }
    if (l->group_count) {
        c->groups = mge_chunk_alloc(e, &e->perm, sizeof(MgLVarGroup) * l->group_count);
        for (uint32_t g = 0; g < l->group_count; ++g) {
            MgLVarGroup *dst = &c->groups[g];
            const MgLVarGroup *src = &l->groups[g];
            dst->name_count = src->name_count;
            dst->names = mge_chunk_alloc(e, &e->perm, sizeof(char *) * (src->name_count ? src->name_count : 1u));
            for (uint32_t i = 0; i < src->name_count; ++i)
                dst->names[i] = mge_intern(e, src->names[i]);
            dst->asc = copy_asc(e, src->asc);
        }
    }
    return c;
}

/* ── contexts and the signature ───────────────────────────────────────── */

int mge_tp_position(MgElab *e, const char *x) {
    for (uint32_t i = 0; i < e->ctxtp_len; ++i)
        if (strcmp(e->ctxtp[i], x) == 0)
            return (int)i;
    return -1;
}

/* syntax.ml tmtplookup: a variable is a de Bruijn index counted over
 * variables only; a let stands for its definition. */
bool mge_tmtplookup(MgElab *e, const Cx *cx, const char *x, MgTm **tm, MgTp **tp) {
    uint32_t i = 0;
    for (; cx; cx = cx->next) {
        if (strcmp(cx->name, x) == 0) {
            *tp = cx->tp;
            if (!cx->def)
                *tm = mge_tm_db(e, i);
            else
                *tm = i == 0 ? cx->def : mge_tmshift(e, 0u, (int32_t)i, cx->def);
            return true;
        }
        if (!cx->def)
            ++i;
    }
    return false;
}

uint32_t mge_cx_vars(const Cx *cx) {
    uint32_t n = 0;
    for (; cx; cx = cx->next)
        if (!cx->def)
            ++n;
    return n;
}

uint32_t mge_cx_len(const Cx *cx) {
    uint32_t n = 0;
    for (; cx; cx = cx->next)
        ++n;
    return n;
}

const Cx *mge_cx_push(MgElab *e, const Cx *next, const char *name, MgTp *tp, MgTm *def,
                         bool perm) {
    Cx *c = perm ? PERM(e, Cx) : TMP(e, Cx);
    *c = (Cx){name, tp, def, next};
    return c;
}

/* A signature name applied to the section variables where it is used. */
MgTm *mge_sig_apply(MgElab *e, MgTm *m, const App *app, const Cx *cxtm) {
    if (!app)
        return m;
    if (app->kind == APP_TP) {
        int i = mge_tp_position(e, app->name);
        if (i < 0)
            mge_not_found(e);
        return mge_tm_tap(e, mge_sig_apply(e, m, app->prev, cxtm), mge_ty_var(e, (uint32_t)i));
    }
    MgTm *v;
    MgTp *ignored;
    if (!mge_tmtplookup(e, cxtm, app->name, &v, &ignored))
        mge_not_found(e);
    return mge_tm_ap(e, mge_sig_apply(e, m, app->prev, cxtm), v);
}

static bool sig_has(const Map *m, const char *x) {
    uint64_t v;
    return mge_stk_top(m, K(x), &v);
}

/* List.assoc x sgtm cxtp cxtm */
MgTm *mge_sig_lookup(MgElab *e, const char *x, const Cx *cxtm) {
    uint64_t v;
    if (!mge_stk_top(&e->sigtm, K(x), &v))
        mge_not_found(e);
    const SigTm *st = P(const SigTm, v);
    return mge_sig_apply(e, st->m, st->app, cxtm);
}

bool mge_poly_get(MgElab *e, const char *x, uint32_t arity, MgTp **tp) {
    uint64_t v;
    if (arity > 6u || !mge_map_get(&e->polytm[arity], K(x), &v))
        return false;
    (void)e;
    *tp = P(MgTp, v);
    return true;
}

bool mge_sigtmof_get(MgElab *e, uint32_t h, uint32_t *levels, MgTp **tp) {
    uint64_t v;
    (void)e;
    if (!mge_map_get(&e->sigtmof, h, &v))
        return false;
    *levels = P(PTp, v)->levels;
    *tp = P(PTp, v)->tp;
    return true;
}

/* syntax.ml extr_tpoftm, over the variables of the context and of the
 * binders it passes. */


static MgTp *db_type(MgElab *e, const TpCx *extra, const Cx *cxtm, uint32_t i) {
    for (; extra; extra = extra->next) {
        if (i == 0)
            return extra->tp;
        --i;
    }
    for (; cxtm; cxtm = cxtm->next) {
        if (cxtm->def)
            continue;
        if (i == 0)
            return cxtm->tp;
        --i;
    }
    mge_fail(e, "nth");
}

static void check_tpoftm(MgElab *e, const TpCx *extra, const Cx *cxtm, MgTm *m, MgTp *a) {
    MgTp *b = mge_extr_tpoftm(e, extra, cxtm, m);
    if (a != b)
        mge_fail(e, "Type Error: Found %s\nbut expected %s", mge_tp_str(e, b), mge_tp_str(e, a));
}

MgTp *mge_extr_tpoftm(MgElab *e, const TpCx *extra, const Cx *cxtm, MgTm *m) {
    switch (m->tag) {
    case MG_TM_DB:
        return db_type(e, extra, cxtm, m->n);
    case MG_TM_TAP: {
        /* TpAp (Prim 0, a), or a name applied to 1 to 6 types */
        MgTp *args[6];
        uint32_t k = 0;
        MgTm *h = m;
        while (h->tag == MG_TM_TAP && k < 7u) {
            if (k == 6u)
                break;
            args[5u - k] = h->tp;
            ++k;
            h = h->f;
        }
        if (k == 1u && h->tag == MG_TM_PRIM && h->n == 0u)
            return mge_ty_arr(e, mge_ty_arr(e, m->tp, mge_ty_prop(e)), m->tp);
        if (h->tag == MG_TM_NAME) {
            uint32_t levels;
            MgTp *d;
            if (!mge_sigtmof_get(e, h->n, &levels, &d))
                mge_not_found(e);
            if (levels != k)
                mge_fail(e, "%s applied to %u type%s, but expected to be applied to %u",
                     mge_name_text(e, h->n), k, k == 1u ? "" : "s", levels);
            return mge_tpsubst(e, d, args + 6u - k, k);
        }
        mge_fail(e, "Nonprefix polymorphism in term");
    }
    case MG_TM_NAME: {
        uint32_t levels;
        MgTp *d;
        if (!mge_sigtmof_get(e, m->n, &levels, &d))
            mge_not_found(e);
        if (levels != 0u)
            mge_fail(e, "%s applied to no types, but expected to be applied to %u",
                 mge_name_text(e, m->n), levels);
        return d;
    }
    case MG_TM_PRIM:
        switch (m->n) {
        case 0: mge_fail(e, "The polymorphic primitive Eps applied to no types, but expected to be applied to 1");
        case 1: return mge_ty_arr(e, mge_ty_set(e), mge_ty_arr(e, mge_ty_set(e), mge_ty_prop(e)));
        case 2: return mge_ty_set(e);
        case 3: return mge_ty_arr(e, mge_ty_set(e), mge_ty_set(e));
        case 4: return mge_ty_arr(e, mge_ty_set(e), mge_ty_set(e));
        case 5: return mge_ty_arr(e, mge_ty_set(e), mge_ty_arr(e, mge_ty_arr(e, mge_ty_set(e), mge_ty_set(e)), mge_ty_set(e)));
        case 6: return mge_ty_arr(e, mge_ty_set(e), mge_ty_set(e));
        default: mge_not_found(e);
        }
    case MG_TM_AP: {
        MgTp *f = mge_extr_tpoftm(e, extra, cxtm, m->f);
        if (f->tag != MG_TP_ARR)
            mge_fail(e, "Type Error: Nonfunction applied to an argument.");
        check_tpoftm(e, extra, cxtm, m->x, f->a);
        return f->b;
    }
    case MG_TM_LAM: {
        TpCx inner = {m->tp, extra};
        return mge_ty_arr(e, m->tp, mge_extr_tpoftm(e, &inner, cxtm, m->f));
    }
    case MG_TM_IMP:
        check_tpoftm(e, extra, cxtm, m->f, mge_ty_prop(e));
        check_tpoftm(e, extra, cxtm, m->x, mge_ty_prop(e));
        return mge_ty_prop(e);
    case MG_TM_ALL: {
        TpCx inner = {m->tp, extra};
        check_tpoftm(e, &inner, cxtm, m->f, mge_ty_prop(e));
        return mge_ty_prop(e);
    }
    default:
        mge_fail(e, "Ill-formed term");
    }
}

/* ── types ────────────────────────────────────────────────────────────── */

/* interpret.ml extract_tp */
MgTp *mge_extract_tp(MgElab *e, const MgL *a) {
    a = mge_S(a);
    switch (a->kind) {
    case MG_L_NAME: {
        if (strcmp(a->text, "prop") == 0)
            return mge_ty_prop(e);
        if (strcmp(a->text, "set") == 0)
            return mge_ty_set(e);
        int i = mge_tp_position(e, a->text);
        if (i >= 0)
            return mge_ty_var(e, (uint32_t)i);
        uint64_t v;
        if (mge_map_get(&e->tpabbrev, K(mge_intern(e, a->text)), &v))
            return P(MgTp, v);
        mge_fail(e, "%s is not a tp", a->text);
    }
    case MG_L_INFIX:
        if (!a->set_infix && strcmp(a->text, "^") == 0 && is_nat(a->b)) {
            int64_t n = int_of_string(e, mge_S(a->b)->text);
            if (n <= 0)
                mge_fail(e, "Type A ^ n must have n > 0");
            MgTp *t = mge_extract_tp(e, a->a);
            if (n == 1)
                return t;
            MgTp *b = t;
            for (int64_t k = 0; k < n; ++k)
                b = mge_ty_arr(e, t, b);
            return mge_ty_arr(e, b, t);
        }
        if (!a->set_infix && strcmp(a->text, "->") == 0) {
            MgTp *b = mge_extract_tp(e, a->b);
            return mge_ty_arr(e, mge_extract_tp(e, a->a), b);
        }
        break;
    case MG_L_IMPLICIT:
        if (mge_is_name(a->a, "Vo") && is_nat(a->b)) {
            int64_t n = int_of_string(e, mge_S(a->b)->text);
            MgTp *t = mge_ty_set(e);
            for (int64_t k = 0; k < n; ++k)
                t = mge_ty_arr(e, t, mge_ty_prop(e));
            return t;
        }
        break;
    default:
        break;
    }
    mge_fail(e, "Not a tp");
}

/* ── elaboration (interpret.ml extract_tm_r / check_tm_r) ─────────────── */



TmTp mge_tt(MgTm *m, MgTp *a) { return (TmTp){m, a}; }

/* A variable group's names pushed in order (the last is de Bruijn 0). */
static const Cx *push_group(MgElab *e, const Cx *cxtm, const MgLVarGroup *g, MgTp *tp) {
    for (uint32_t i = 0; i < g->name_count; ++i)
        cxtm = mge_cx_push(e, cxtm, mge_intern(e, g->names[i]), tp, NULL, false);
    return cxtm;
}

/* The groups of a binder, rest after the first `skip` names of group 0. */
typedef struct {
    const MgLVarGroup *groups;
    uint32_t count;
    uint32_t skip;     /* names of groups[0] already bound */
    bool drop_asc;     /* groups[0]'s ascription was dropped (xr, None) */
} Bvl;

static uint32_t bvl_names(const Bvl *b) { return b->groups[0].name_count - b->skip; }
static const MgLAsc *bvl_asc(const Bvl *b) { return b->drop_asc ? NULL : b->groups[0].asc; }
static Bvl bvl_next_group(const Bvl *b) { return (Bvl){b->groups + 1, b->count - 1u, 0u, false}; }

/* check_tm_r_all */
static MgTm *check_all(MgElab *e, Bvl bvl, const MgL *body, const Cx *cxtm) {
    if (!bvl.count)
        return mge_check_tm_r(e, body, mge_ty_prop(e), cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    const MgLAsc *asc = g->asc;
    if (!g->name_count)
        return check_all(e, bvl_next_group(&bvl), body, cxtm);
    if (!asc || asc->kind == MG_ASC_TP) {
        MgTp *tp = asc ? mge_extract_tp(e, asc->type) : mge_ty_set(e);
        MgTm *n = check_all(e, bvl_next_group(&bvl), body, push_group(e, cxtm, g, tp));
        for (uint32_t i = 0; i < g->name_count; ++i)
            n = mge_tm_all(e, tp, n);
        return n;
    }
    uint32_t op;
    if (asc->kind == MG_ASC_SET) {
        if (e->setIn == NONE)
            mge_fail(e, ":e not declared");
        op = e->setIn;
    } else {
        if (e->setSubeq == NONE)
            mge_fail(e, "c= not declared");
        op = e->setSubeq;
    }
    MgTm *a1s = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
    MgTm *n = check_all(e, bvl_next_group(&bvl), body, push_group(e, cxtm, g, mge_ty_set(e)));
    for (uint32_t k = g->name_count; k-- > 0;) {
        MgTm *bound = mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, op), mge_tm_db(e, 0u)), mge_tmshift(e, 0u, (int32_t)k + 1, a1s));
        n = mge_tm_all(e, mge_ty_set(e), mge_tm_imp(e, bound, n));
    }
    return n;
}

static MgTm *check_lam(MgElab *e, MgTp *btp, Bvl bvl, const MgL *body, const Cx *cxtm);

/* extract_tm_r_lam */
static TmTp extract_lam(MgElab *e, Bvl bvl, const MgL *body, const Cx *cxtm) {
    if (!bvl.count)
        return mge_extract_tm_r(e, body, cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (!g->name_count)
        return extract_lam(e, bvl_next_group(&bvl), body, cxtm);
    const MgLAsc *asc = g->asc;
    if (!asc)
        mge_fail(e, "Could not infer type of lambda bound variable %s", g->names[0]);
    if (asc->kind == MG_ASC_TP) {
        MgTp *tp = mge_extract_tp(e, asc->type);
        TmTp r = extract_lam(e, bvl_next_group(&bvl), body, push_group(e, cxtm, g, tp));
        for (uint32_t i = 0; i < g->name_count; ++i) {
            r.tm = mge_tm_lam(e, tp, r.tm);
            r.tp = mge_ty_arr(e, tp, r.tp);
        }
        return r;
    }
    if (asc->kind == MG_ASC_SET) {
        if (e->setlam == NONE)
            mge_fail(e, "Set level lambdas have not been declared");
    } else {
        if (e->setlam == NONE)
            mge_fail(e, "Set level lambdas have not been declared");
        if (e->setPow == NONE)
            mge_fail(e, "Set level lambdas can only be used with c= when power sets are declared");
    }
    MgTm *a1s = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
    MgTm *n = check_lam(e, mge_ty_set(e), bvl_next_group(&bvl), body, push_group(e, cxtm, g, mge_ty_set(e)));
    MgTm *dom = asc->kind == MG_ASC_SET ? a1s : mge_tm_ap(e, mge_tm_h(e, e->setPow), a1s);
    for (uint32_t i = 0; i < g->name_count; ++i)
        n = mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->setlam), dom), mge_tm_lam(e, mge_ty_set(e), n));
    return mge_tt(n, mge_ty_set(e));
}

/* check_tm_r_lam: after the first name of a group, the group's other
 * names have no ascription, as in Megalodon. */
static MgTm *check_lam(MgElab *e, MgTp *btp, Bvl bvl, const MgL *body, const Cx *cxtm) {
    if (!bvl.count)
        return mge_check_tm_r(e, body, btp, cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (bvl_names(&bvl) == 0u)
        return check_lam(e, btp, bvl_next_group(&bvl), body, cxtm);
    const char *x = mge_intern(e, g->names[bvl.skip]);
    Bvl rest = {bvl.groups, bvl.count, bvl.skip + 1u, true};
    const MgLAsc *asc = bvl_asc(&bvl);
    if (!asc) {
        if (btp->tag != MG_TP_ARR)
            mge_fail(e, "fun expected to be of non-functional type");
        MgTm *n = check_lam(e, btp->b, rest, body, mge_cx_push(e, cxtm, x, btp->a, NULL, false));
        return mge_tm_lam(e, btp->a, n);
    }
    if (asc->kind == MG_ASC_TP) {
        MgTp *a1tp = mge_extract_tp(e, asc->type);
        if (btp->tag != MG_TP_ARR)
            mge_fail(e, "fun expected to be of non-functional type");
        if (btp->a != a1tp)
            mge_fail(e, "domain of fun is expected to be %s but given as %s", mge_tp_str(e, btp->a), mge_tp_str(e, a1tp));
        MgTm *n = check_lam(e, btp->b, rest, body, mge_cx_push(e, cxtm, x, btp->a, NULL, false));
        return mge_tm_lam(e, btp->a, n);
    }
    if (btp != mge_ty_set(e))
        mge_fail(e, "Set level lambda expected to have to be of non-Set type");
    if (e->setlam == NONE)
        mge_fail(e, "Set level lambdas have not been declared");
    if (asc->kind == MG_ASC_SUBEQ && e->setPow == NONE)
        mge_fail(e, "Set level lambdas can only be used with c= when power sets are declared");
    MgTm *a1s = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
    MgTm *n = check_lam(e, mge_ty_set(e), rest, body, mge_cx_push(e, cxtm, x, mge_ty_set(e), NULL, false));
    MgTm *dom = asc->kind == MG_ASC_SET ? a1s : mge_tm_ap(e, mge_tm_h(e, e->setPow), a1s);
    return mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->setlam), dom), mge_tm_lam(e, mge_ty_set(e), n));
}

/* The user binder cases (interpret.ml check_tm_r_bind2 .. bind6). */
typedef struct {
    MgElab *e;
    const char *x;
    const MgL *body;
} Bind;

__attribute__((noreturn)) static void empty_group(Bind *b) {
    mge_fail(b->e, "Empty list of variables to be bound with %s", b->x);
}

/* case 2: polymorphic (?0 -> beta) -> beta */
static MgTm *bind2(Bind *b, MgTm *op, MgTp *beta, Bvl bvl, const Cx *cxtm);
static MgTm *bind2b(Bind *b, MgTm *op, MgTp *beta, const MgLVarGroup *g, uint32_t k, MgTp *ytp,
                    Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind2(b, op, beta, bvl, cxtm);
    MgTm *m = bind2b(b, mge_shift1(e, op), beta, g, k + 1u, ytp, bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), ytp, NULL, false));
    return mge_tm_ap(e, mge_tm_tap(e, op, ytp), mge_tm_lam(e, ytp, m));
}

static MgTm *bind2(Bind *b, MgTm *op, MgTp *beta, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (!bvl.count)
        return mge_check_tm_r(e, b->body, beta, cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (!g->name_count)
        empty_group(b);
    const MgLAsc *asc = g->asc;
    if (asc && asc->kind != MG_ASC_TP)
        mge_fail(e, "Variables bound with %s must be ascribed a type (using :) not a set", b->x);
    MgTp *ytp = asc ? mge_extract_tp(e, asc->type) : mge_ty_set(e);
    return bind2b(b, op, beta, g, 0u, ytp, bvl_next_group(&bvl), cxtm);
}

/* case 3: polymorphic (?0 -> prop) -> prop with a connective */
static MgTm *bind3(Bind *b, MgTm *op, MgTm *c, Bvl bvl, const Cx *cxtm);
static MgTm *bind3b(Bind *b, MgTm *op, MgTm *c, const MgLVarGroup *g, uint32_t k, MgTp *ytp,
                    Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind3(b, op, c, bvl, cxtm);
    MgTm *m = bind3b(b, mge_shift1(e, op), c, g, k + 1u, ytp, bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), ytp, NULL, false));
    return mge_tm_ap(e, mge_tm_tap(e, op, ytp), mge_tm_lam(e, ytp, m));
}

static MgTm *bind3c(Bind *b, MgTm *op, MgTm *r, MgTm *c, const MgLVarGroup *g, uint32_t k,
                    MgTm *yset, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind3(b, op, c, bvl, cxtm);
    yset = mge_shift1(e, yset);
    MgTm *m = bind3c(b, mge_shift1(e, op), r, mge_shift1(e, c), g, k + 1u, yset, bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), mge_ty_set(e), NULL, false));
    MgTm *guard = mge_tm_ap(e, mge_tm_ap(e, c, mge_tm_ap(e, mge_tm_ap(e, r, mge_tm_db(e, 0u)), yset)), m);
    return mge_tm_ap(e, mge_tm_tap(e, op, mge_ty_set(e)), mge_tm_lam(e, mge_ty_set(e), guard));
}

static MgTm *bind3(Bind *b, MgTm *op, MgTm *c, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (!bvl.count)
        return mge_check_tm_r(e, b->body, mge_ty_prop(e), cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (!g->name_count)
        empty_group(b);
    const MgLAsc *asc = g->asc;
    if (!asc || asc->kind == MG_ASC_TP) {
        MgTp *ytp = asc ? mge_extract_tp(e, asc->type) : mge_ty_set(e);
        return bind3b(b, op, c, g, 0u, ytp, bvl_next_group(&bvl), cxtm);
    }
    uint32_t r;
    if (asc->kind == MG_ASC_SET) {
        if (e->setIn == NONE)
            mge_fail(e, "Cannot ascribe a variable with :e since :e is not (yet) declared");
        r = e->setIn;
    } else {
        if (e->setSubeq == NONE)
            mge_fail(e, "Cannot ascribe a variable with c= since c= is not (yet) declared");
        r = e->setSubeq;
    }
    MgTm *yset = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
    return bind3c(b, op, mge_tm_h(e, r), c, g, 0u, yset, bvl_next_group(&bvl), cxtm);
}

/* case 4: monomorphic (alpha -> beta) -> beta */
static MgTm *bind4(Bind *b, MgTm *op, MgTp *alpha, MgTp *beta, Bvl bvl, const Cx *cxtm);
static MgTm *bind4b(Bind *b, MgTm *op, MgTp *alpha, MgTp *beta, const MgLVarGroup *g, uint32_t k,
                    Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind4(b, op, alpha, beta, bvl, cxtm);
    MgTm *m = bind4b(b, mge_shift1(e, op), alpha, beta, g, k + 1u, bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), alpha, NULL, false));
    return mge_tm_ap(e, op, mge_tm_lam(e, alpha, m));
}

static MgTm *bind4(Bind *b, MgTm *op, MgTp *alpha, MgTp *beta, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (!bvl.count)
        return mge_check_tm_r(e, b->body, beta, cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (!g->name_count)
        empty_group(b);
    const MgLAsc *asc = g->asc;
    if (asc && asc->kind == MG_ASC_TP) {
        MgTp *ytp = mge_extract_tp(e, asc->type);
        if (ytp != alpha)
            mge_fail(e, "Variables bound with %s must have type %s", b->x, mge_tp_str(e, alpha));
    } else if (asc && asc->kind == MG_ASC_SET) {
        mge_fail(e, "Variables bound with %s cannot be ascribed with :e", b->x);
    } else if (asc) {
        mge_fail(e, "Variables bound with %s cannot be ascribed with c=", b->x);
    }
    return bind4b(b, op, alpha, beta, g, 0u, bvl_next_group(&bvl), cxtm);
}

/* case 5: monomorphic (set -> prop) -> prop with a connective */
static MgTm *bind5(Bind *b, MgTm *op, MgTm *c, Bvl bvl, const Cx *cxtm);
static MgTm *bind5b(Bind *b, MgTm *op, MgTm *c, const MgLVarGroup *g, uint32_t k, Bvl bvl,
                    const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind5(b, op, c, bvl, cxtm);
    MgTm *m = bind5b(b, mge_shift1(e, op), c, g, k + 1u, bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), mge_ty_set(e), NULL, false));
    return mge_tm_ap(e, op, mge_tm_lam(e, mge_ty_set(e), m));
}

static MgTm *bind5c(Bind *b, MgTm *op, MgTm *r, MgTm *c, const MgLVarGroup *g, uint32_t k,
                    MgTm *yset, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind5(b, op, c, bvl, cxtm);
    yset = mge_shift1(e, yset);
    MgTm *m = bind5c(b, mge_shift1(e, op), r, mge_shift1(e, c), g, k + 1u, yset, bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), mge_ty_set(e), NULL, false));
    MgTm *guard = mge_tm_ap(e, mge_tm_ap(e, c, mge_tm_ap(e, mge_tm_ap(e, r, mge_tm_db(e, 0u)), yset)), m);
    return mge_tm_ap(e, op, mge_tm_lam(e, mge_ty_set(e), guard));
}

static MgTm *bind5(Bind *b, MgTm *op, MgTm *c, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (!bvl.count)
        return mge_check_tm_r(e, b->body, mge_ty_prop(e), cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (!g->name_count)
        empty_group(b);
    const MgLAsc *asc = g->asc;
    if (!asc)
        return bind5b(b, op, c, g, 0u, bvl_next_group(&bvl), cxtm);
    if (asc->kind == MG_ASC_TP) {
        if (mge_extract_tp(e, asc->type) != mge_ty_set(e))
            mge_fail(e, "Variables bound with %s must be of type set", b->x);
        return bind5b(b, op, c, g, 0u, bvl_next_group(&bvl), cxtm);
    }
    uint32_t r;
    if (asc->kind == MG_ASC_SET) {
        if (e->setIn == NONE)
            mge_fail(e, "Cannot ascribe a variable with :e since :e is not (yet) declared");
        r = e->setIn;
    } else {
        if (e->setSubeq == NONE)
            mge_fail(e, "Cannot ascribe a variable with c= since c= is not (yet) declared");
        r = e->setSubeq;
    }
    MgTm *yset = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
    return bind5c(b, op, mge_tm_h(e, r), c, g, 0u, yset, bvl_next_group(&bvl), cxtm);
}

/* case 6: monomorphic set -> (set -> beta) -> beta */
static MgTm *bind6(Bind *b, MgTm *op, MgTp *beta, Bvl bvl, const Cx *cxtm);
static MgTm *bind6b(Bind *b, MgTm *op, MgTp *beta, MgTm *p, const MgLVarGroup *g, uint32_t k,
                    MgTm *yset, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (k == g->name_count)
        return bind6(b, op, beta, bvl, cxtm);
    MgTm *m = bind6b(b, mge_shift1(e, op), beta, p, g, k + 1u, mge_shift1(e, yset), bvl,
                     mge_cx_push(e, cxtm, mge_intern(e, g->names[k]), mge_ty_set(e), NULL, false));
    MgTm *dom = p ? mge_tm_ap(e, p, yset) : yset;
    return mge_tm_ap(e, mge_tm_ap(e, op, dom), mge_tm_lam(e, mge_ty_set(e), m));
}

static MgTm *bind6(Bind *b, MgTm *op, MgTp *beta, Bvl bvl, const Cx *cxtm) {
    MgElab *e = b->e;
    if (!bvl.count)
        return mge_check_tm_r(e, b->body, beta, cxtm);
    const MgLVarGroup *g = &bvl.groups[0];
    if (!g->name_count)
        empty_group(b);
    const MgLAsc *asc = g->asc;
    if (!asc || asc->kind == MG_ASC_TP)
        mge_fail(e, "Variables bound by %s must be ascribed a set with :e or c=", b->x);
    MgTm *p = NULL;
    if (asc->kind == MG_ASC_SUBEQ) {
        if (e->setPow == NONE)
            mge_fail(e, "Cannot ascribe a %s bound variable with c= since Power is not (yet) declared", b->x);
        p = mge_tm_h(e, e->setPow);
    }
    MgTm *yset = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
    return bind6b(b, op, beta, p, g, 0u, yset, bvl_next_group(&bvl), cxtm);
}

static bool single_var(const MgL *bi) {
    return bi->group_count == 1u && bi->groups[0].name_count == 1u;
}

/* The binder body of a user binder (the try ... with Not_found of
 * extract_tm_r's Bi case). */
static TmTp user_binder(MgElab *e, const MgL *a, const Cx *cxtm) {
    const char *x = mge_intern(e, a->text);
    uint64_t v;
    if (!mge_stk_top(&e->bindersem, K(x), &v))
        mge_not_found(e);
    const BinderSem *bs = P(const BinderSem, v);
    Bvl bvl = {a->groups, a->group_count, 0u, false};
    Bind b = {e, x, a->a};
    const MgL *xb = mge_S(bs->a);
    MgTp *bindoptp;
    if (xb->kind == MG_L_NAME && mge_poly_get(e, mge_intern(e, xb->text), 1u, &bindoptp)) {
        MgTm *bindop = mge_sig_lookup(e, mge_intern(e, xb->text), cxtm);
        MgTp *t0 = mge_ty_var(e, 0u), *prop = mge_ty_prop(e);
        bool eps = bindoptp == mge_ty_arr(e, mge_ty_arr(e, t0, prop), t0);
        if (eps && !bs->b) {
            if (single_var(a) && a->groups[0].asc && a->groups[0].asc->kind == MG_ASC_TP) {
                MgTp *ytp = mge_extract_tp(e, a->groups[0].asc->type);
                MgTm *body = mge_check_tm_r(e, a->a, prop,
                                        mge_cx_push(e, cxtm, mge_intern(e, a->groups[0].names[0]), ytp, NULL, false));
                return mge_tt(mge_tm_ap(e, mge_tm_tap(e, bindop, ytp), mge_tm_lam(e, ytp, body)), ytp);
            }
            mge_fail(e, "%s should only bind one variable and that variable should be ascribed a type.", x);
        }
        if (eps) {
            const MgLAsc *asc = single_var(a) ? a->groups[0].asc : NULL;
            if (asc && asc->kind == MG_ASC_TP) {
                MgTp *ytp = mge_extract_tp(e, asc->type);
                MgTm *body = mge_check_tm_r(e, a->a, prop,
                                        mge_cx_push(e, cxtm, mge_intern(e, a->groups[0].names[0]), ytp, NULL, false));
                return mge_tt(mge_tm_ap(e, mge_tm_tap(e, bindop, ytp), mge_tm_lam(e, ytp, body)), ytp);
            }
            if (asc) {
                uint32_t r;
                if (asc->kind == MG_ASC_SET) {
                    if (e->setIn == NONE)
                        mge_fail(e, ":e not declared");
                    r = e->setIn;
                } else {
                    if (e->setSubeq == NONE)
                        mge_fail(e, "c= not declared");
                    r = e->setSubeq;
                }
                MgTm *c = mge_check_tm_r(e, bs->b, mge_ty_arr(e, prop, mge_ty_arr(e, prop, prop)), cxtm);
                MgTm *yatm = mge_check_tm_r(e, asc->type, mge_ty_set(e), cxtm);
                MgTm *body = mge_check_tm_r(e, a->a, prop,
                                        mge_cx_push(e, cxtm, mge_intern(e, a->groups[0].names[0]), mge_ty_set(e), NULL, false));
                MgTm *guard = mge_tm_ap(e, mge_tm_ap(e, c, mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, r), mge_tm_db(e, 0u)), mge_shift1(e, yatm))), body);
                return mge_tt(mge_tm_ap(e, mge_tm_tap(e, bindop, mge_ty_set(e)), mge_tm_lam(e, mge_ty_set(e), guard)), mge_ty_set(e));
            }
            mge_fail(e, "%s should only bind one variable.", x);
        }
        if (bindoptp->tag == MG_TP_ARR && bindoptp->a->tag == MG_TP_ARR && bindoptp->a->a == t0 &&
            !bs->b && mge_notpvarsp(bindoptp->a->b) && bindoptp->a->b == bindoptp->b) {
            if (!bs->plus && !single_var(a))
                mge_fail(e, "%s can only bind one variable", x);
            MgTp *beta = bindoptp->b;
            return mge_tt(bind2(&b, bindop, beta, bvl, cxtm), beta);
        }
        if (bindoptp == mge_ty_arr(e, mge_ty_arr(e, t0, prop), prop) && bs->b) {
            if (!bs->plus && !single_var(a))
                mge_fail(e, "%s can only bind one variable", x);
            MgTm *c = mge_check_tm_r(e, bs->b, mge_ty_arr(e, prop, mge_ty_arr(e, prop, prop)), cxtm);
            return mge_tt(bind3(&b, bindop, c, bvl, cxtm), prop);
        }
        mge_fail(e, "The binder %s cannot be used in this way.", x);
    }
    TmTp op = mge_extract_tm_r(e, bs->a, cxtm);
    MgTp *t = op.tp, *prop = mge_ty_prop(e), *set = mge_ty_set(e);
    if (t->tag == MG_TP_ARR && t->a->tag == MG_TP_ARR && !bs->b && mge_notpvarsp(t->a->a) &&
        mge_notpvarsp(t->a->b) && t->a->b == t->b) {
        if (!bs->plus && !single_var(a))
            mge_fail(e, "%s can only bind one variable", x);
        return mge_tt(bind4(&b, op.tm, t->a->a, t->b, bvl, cxtm), t->b);
    }
    if (t == mge_ty_arr(e, mge_ty_arr(e, set, prop), prop) && bs->b) {
        if (!bs->plus && !single_var(a))
            mge_fail(e, "%s can only bind one variable", x);
        MgTm *c = mge_check_tm_r(e, bs->b, mge_ty_arr(e, prop, mge_ty_arr(e, prop, prop)), cxtm);
        return mge_tt(bind5(&b, op.tm, c, bvl, cxtm), prop);
    }
    if (t->tag == MG_TP_ARR && t->a == set && t->b->tag == MG_TP_ARR && t->b->a->tag == MG_TP_ARR &&
        t->b->a->a == set && !bs->b && mge_notpvarsp(t->b->a->b) && t->b->a->b == t->b->b) {
        if (!bs->plus && !single_var(a))
            mge_fail(e, "%s can only bind one variable", x);
        return mge_tt(bind6(&b, op.tm, t->b->b, bvl, cxtm), t->b->b);
    }
    if (t->tag == MG_TP_ARR && t->a->tag == MG_TP_ARR && !bs->b && mge_notpvarsp(t->a->a) &&
        mge_notpvarsp(t->a->b) && mge_notpvarsp(t->b)) {
        MgTp *alpha = t->a->a, *beta = t->a->b;
        if (single_var(a)) {
            const MgLAsc *asc = a->groups[0].asc;
            if (!asc || asc->kind == MG_ASC_TP) {
                if (asc && mge_extract_tp(e, asc->type) != alpha)
                    mge_fail(e, "%s must bind one variable with type %s", x, mge_tp_str(e, alpha));
                MgTm *body = mge_check_tm_r(e, a->a, beta,
                                        mge_cx_push(e, cxtm, mge_intern(e, a->groups[0].names[0]), alpha, NULL, false));
                return mge_tt(mge_tm_ap(e, op.tm, mge_tm_lam(e, alpha, body)), t->b);
            }
        }
        mge_fail(e, "%s should only bind one variable which has type %s", x, mge_tp_str(e, alpha));
    }
    mge_fail(e, "The binder %s cannot be used in this way.", x);
}

/* pointwise_binop_tm_r .. pointwise_pred_tm_r */
static TmTp pointwise(MgElab *e, int kind, MgTm *n, MgTp *a, MgTp *b, MgTm *m1, MgTm *m2) {
    if (a == b) {
        MgTm *ap1 = mge_tm_ap(e, n, m1);
        switch (kind) {
        case 0: return mge_tt(mge_tm_ap(e, ap1, m2), b);
        case 1: return mge_tt(mge_tm_ap(e, ap1, m2), mge_ty_prop(e));
        case 2: return mge_tt(ap1, b);
        default: return mge_tt(ap1, mge_ty_prop(e));
        }
    }
    if (a->tag != MG_TP_ARR)
        mge_fail(e, "impossible");
    MgTm *b1 = mge_gen_lam_body(e, m1);
    MgTm *b2 = kind < 2 ? mge_gen_lam_body(e, m2) : NULL;
    TmTp r = pointwise(e, kind, n, a->b, b, b1, b2);
    if (kind == 0 || kind == 2)
        return mge_tt(mge_tm_lam(e, a->a, r.tm), mge_ty_arr(e, a->a, r.tp));
    return mge_tt(mge_tm_all(e, a->a, r.tm), mge_ty_prop(e));
}

/* OCaml's try BODY with ...: runs BODY and returns 0, or the kind of
 * what it raised (the caller handles its own kinds and re-raises the
 * others).  BODY passes its results through CTX, so no local of the caller
 * lives across the longjmp. */
int mge_try_run(MgElab *e, void (*body)(MgElab *, void *), void *ctx) {
    jmp_buf jb;
    jmp_buf *volatile saved = e->fail;
    MgElab *volatile ve = e;
    e->fail = &jb;
    int k = setjmp(jb);
    if (k == 0)
        body(e, ctx);
    ve->fail = saved;
    return k;
}

typedef struct {
    const MgL *a;
    const MgL *xn;
    const char *x;
    const Cx *cxtm;
    TmTp out;
} InfixTry;

/* the polymorphic reading of an infix operator (=) */
static void infix_poly(MgElab *e, void *ctx) {
    InfixTry *t = ctx;
    MgTp *polytp;
    if (t->xn->kind != MG_L_NAME || !mge_poly_get(e, mge_intern(e, t->xn->text), 1u, &polytp))
        mge_not_found(e);
    MgTp *t0 = mge_ty_var(e, 0u);
    if (!(polytp->tag == MG_TP_ARR && polytp->a == t0 && polytp->b->tag == MG_TP_ARR &&
          polytp->b->a == t0))
        mge_fail(e, "%s has an inappropriate type to be a polymorphic infix operator; expected ?0 -> ?0 -> ...", t->x);
    TmTp a1 = mge_extract_tm_r(e, t->a->a, t->cxtm);
    MgTm *a2tm = mge_check_tm_r(e, t->a->b, a1.tp, t->cxtm);
    MgTm *xd = mge_sig_lookup(e, mge_intern(e, t->xn->text), t->cxtm);
    MgTp *cl[1] = {a1.tp};
    t->out = mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_tap(e, xd, a1.tp), a1.tm), a2tm), mge_tpsubst(e, polytp->b->b, cl, 1u));
}

static TmTp infix(MgElab *e, const MgL *a, const Cx *cxtm) {
    const char *x = mge_intern(e, a->text);
    uint64_t v;
    if (!mge_stk_top(&e->infixsem, K(x), &v))
        mge_not_found(e);
    const MgL *xs = P(const MgL, v);
    InfixTry t = {a, mge_S(xs), x, cxtm, {0}};
    int k = mge_try_run(e, infix_poly, &t);
    if (k == 0)
        return t.out;
    if (k != RAISE_NOT_FOUND)
        mge_raise_kind(e, k);
    TmTp n = mge_extract_tm_r(e, xs, cxtm);
    if (n.tp->tag != MG_TP_ARR || n.tp->b->tag != MG_TP_ARR)
        mge_fail(e, "%s does not have the type of something expecting two arguments", x);
    MgTp *a1tp = n.tp->a, *a2tp = n.tp->b->a, *rtp = n.tp->b->b;
    TmTp a1 = mge_extract_tm_r(e, a->a, cxtm);
    if (a1tp == a1.tp) {
        MgTm *a2tm = mge_check_tm_r(e, a->b, a2tp, cxtm);
        return mge_tt(mge_tm_ap(e, mge_tm_ap(e, n.tm, a1.tm), a2tm), rtp);
    }
    /* pwise_tp_p a1tp2 a1tp: a1tp is a codomain of a1tp2 */
    MgTp *p = a1.tp;
    while (p != a1tp && p->tag == MG_TP_ARR)
        p = p->b;
    if (p == a1tp) {
        if (a1tp == a2tp && a1tp == rtp) {
            MgTm *a2tm = mge_check_tm_r(e, a->b, a1.tp, cxtm);
            return pointwise(e, 0, n.tm, a1.tp, a1tp, a1.tm, a2tm);
        }
        if (a1tp == a2tp && rtp == mge_ty_prop(e)) {
            MgTm *a2tm = mge_check_tm_r(e, a->b, a1.tp, cxtm);
            return pointwise(e, 1, n.tm, a1.tp, a1tp, a1.tm, a2tm);
        }
    }
    mge_fail(e, "Ill-typed use of %s", x);
}

static TmTp unary(MgElab *e, const MgL *a, Map *sem, const Cx *cxtm) {
    const char *x = mge_intern(e, a->text);
    uint64_t v;
    if (!mge_stk_top(sem, K(x), &v))
        mge_not_found(e);
    TmTp n = mge_extract_tm_r(e, P(const MgL, v), cxtm);
    if (n.tp->tag != MG_TP_ARR)
        mge_fail(e, "%s does not have the type of something expecting an argument", x);
    MgTp *a1tp = n.tp->a, *rtp = n.tp->b;
    TmTp a1 = mge_extract_tm_r(e, a->a, cxtm);
    if (a1tp == a1.tp)
        return mge_tt(mge_tm_ap(e, n.tm, a1.tm), rtp);
    MgTp *p = a1.tp;
    while (p != a1tp && p->tag == MG_TP_ARR)
        p = p->b;
    if (p == a1tp) {
        if (rtp == a1tp)
            return pointwise(e, 2, n.tm, a1.tp, a1tp, a1.tm, NULL);
        if (rtp == mge_ty_prop(e))
            return pointwise(e, 3, n.tm, a1.tp, a1tp, a1.tm, NULL);
    }
    mge_fail(e, "Ill-typed use of %s", x);
}

static MgTp *mtuple_tp(MgElab *e, MgTp *ntp, uint32_t l) {
    MgTp *r = ntp;
    for (uint32_t i = 0; i < l; ++i)
        r = mge_ty_arr(e, ntp, r);
    return r;
}

/* interpret.ml string_to_stp */
static MgTp *string_to_stp(MgElab *e, const char *x, size_t *i, size_t l);
static MgTp *string_to_stp2(MgElab *e, const char *x, size_t *i, size_t l, MgTp *a) {
    if (*i < l) {
        MgTp *b = string_to_stp(e, x, i, l);
        return mge_ty_arr(e, a, b);
    }
    return a;
}

static MgTp *string_to_stp(MgElab *e, const char *x, size_t *i, size_t l) {
    if (*i < l) {
        char c = x[*i];
        if (c == 'i') {
            ++*i;
            return string_to_stp2(e, x, i, l, mge_ty_set(e));
        }
        if (c == 'o') {
            ++*i;
            return string_to_stp2(e, x, i, l, mge_ty_prop(e));
        }
        if (c == '(') {
            ++*i;
            MgTp *a = string_to_stp(e, x, i, l);
            if (*i < l && x[*i] == ')') {
                ++*i;
                return string_to_stp2(e, x, i, l, a);
            }
        }
    }
    mge_fail(e, "could not interpret string as simple type");
}

static MetaCx *dbcx(MgElab *e, const Cx *cxtm, MgTp *tp) {
    MetaCx *m = PERM(e, MetaCx);
    m->count = mge_cx_vars(cxtm);
    m->tps = mge_chunk_alloc(e, &e->perm, sizeof(MgTp *) * (m->count ? m->count : 1u));
    uint32_t i = 0;
    for (; cxtm; cxtm = cxtm->next)
        if (!cxtm->def)
            m->tps[i++] = cxtm->tp;
    m->tp = tp;
    return m;
}

static bool dbcx_equal(const MetaCx *m, const Cx *cxtm) {
    uint32_t i = 0;
    for (; cxtm; cxtm = cxtm->next) {
        if (cxtm->def)
            continue;
        if (i >= m->count || m->tps[i] != cxtm->tp)
            return false;
        ++i;
    }
    return i == m->count;
}

/* A metavariable ?KEY (DISPLAY is its spelling in messages). */
static TmTp metavar(MgElab *e, const char *key, const char *display, MgTp *a, const Cx *cxtm) {
    uint64_t v;
    if (mge_map_get(&e->metavarassoc, K(key), &v))
        return mge_tt(P(MgTm, v), a);
    char name[512];
    snprintf(name, sizeof name, "?%s", key);
    if (mge_map_get(&e->metavarcx, K(key), &v)) {
        const MetaCx *m = P(const MetaCx, v);
        if (m->tp != a)
            mge_fail(e, "same metavar used %s with different types", display);
        if (!dbcx_equal(m, cxtm))
            mge_fail(e, "same metavar used %s with different contexts", display);
        return mge_tt(mge_tm_h(e, mge_name_id(e, name)), a);
    }
    mge_map_set(e, &e->metavarcx, K(key), V(dbcx(e, cxtm, a)));
    return mge_tt(mge_tm_h(e, mge_name_id(e, name)), a);
}

static TmTp qstring(MgElab *e, const char *x, const Cx *cxtm) {
    if (!*x) {
        uint64_t v;
        if (mge_map_get(&e->metavarassoc, K(mge_intern(e, "")), &v))
            return mge_tt(P(MgTm, v), mge_ty_set(e));
        return metavar(e, mge_intern(e, ""), "", mge_ty_set(e), cxtm);
    }
    const char *colon = strchr(x, ':');
    if (colon) {
        size_t i = (size_t)(colon - x) + 1u;
        MgTp *a = string_to_stp(e, x, &i, strlen(x));
        char key[512];
        snprintf(key, sizeof key, "%.*s", (int)(colon - x), x);
        char display[520];
        snprintf(display, sizeof display, "?%s", key);
        return metavar(e, mge_intern(e, key), display, a, cxtm);
    }
    size_t i = 0;
    MgTp *a = string_to_stp(e, x, &i, strlen(x));
    uint64_t v;
    if (mge_map_get(&e->metavarassoc, K(mge_intern(e, x)), &v))
        return mge_tt(P(MgTm, v), a);
    const char *empty = mge_intern(e, "");
    if (mge_map_get(&e->metavarcx, K(empty), &v)) {
        const MetaCx *m = P(const MetaCx, v);
        if (m->tp != a)
            mge_fail(e, "same metavar used ? with different types");
        if (!dbcx_equal(m, cxtm))
            mge_fail(e, "same metavar used ? with different contexts");
        return mge_tt(mge_tm_h(e, mge_name_id(e, "?")), a);
    }
    mge_map_set(e, &e->metavarcx, K(empty), V(dbcx(e, cxtm, a)));
    return mge_tt(mge_tm_h(e, mge_name_id(e, "?")), a);
}

/* The SetEnum notation: the declared constructor for n elements, or
 * adjunction onto a smaller enumeration. */
static bool setenum_nth(MgElab *e, size_t i, uint32_t *h) {
    if (i >= e->setenuml_len || e->setenuml[i] == NONE)
        return false;
    *h = e->setenuml[i];
    return true;
}

static MgTm *senr(MgElab *e, size_t i, MgTm **rtml, size_t len) {
    uint32_t h;
    if (setenum_nth(e, i, &h)) {
        MgTm *m = mge_tm_h(e, h);
        for (size_t k = len; k-- > 0;)
            m = mge_tm_ap(e, m, rtml[k]);
        return m;
    }
    if (i > 0) {
        if (!len)
            mge_fail(e, "Should never happen since i should be |rtml|");
        MgTm *inner = senr(e, i - 1u, rtml + 1, len - 1u);
        return mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->setenumadj), inner), rtml[0]);
    }
    mge_fail(e, "There is no way to interpret {}, which is surprising.");
}

typedef struct {
    const MgL *a;
    const Cx *cxtm;
    MgTm *out;
} SetEnumTry;

/* the declared constructor for this many elements, applied to them */
static void set_enum_declared(MgElab *e, void *ctx) {
    SetEnumTry *t = ctx;
    uint32_t h;
    if (!setenum_nth(e, t->a->list_len, &h))
        mge_fail(e, "nth");
    MgTm *m = mge_tm_h(e, h);
    for (size_t i = 0; i < t->a->list_len; ++i)
        m = mge_tm_ap(e, m, mge_check_tm_r(e, t->a->list[i], mge_ty_set(e), t->cxtm));
    t->out = m;
}

static TmTp set_enum(MgElab *e, const MgL *a, const Cx *cxtm) {
    size_t n = a->list_len;
    if (e->setenumadj != NONE) {
        MgTm **atml = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * (n ? n : 1u));
        for (size_t i = 0; i < n; ++i)
            atml[i] = mge_check_tm_r(e, a->list[i], mge_ty_set(e), cxtm);
        /* senr n (List.rev atml): element k of the reversed list */
        MgTm **rev = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * (n ? n : 1u));
        for (size_t i = 0; i < n; ++i)
            rev[i] = atml[n - 1u - i];
        return mge_tt(senr(e, n, rev, n), mge_ty_set(e));
    }
    SetEnumTry t = {a, cxtm, NULL};
    int k = mge_try_run(e, set_enum_declared, &t);
    if (k == 0)
        return mge_tt(t.out, mge_ty_set(e));
    if (k != RAISE_FAILURE)
        mge_raise_kind(e, k);
    mge_fail(e, "No notation has been declared for {...,...} with %zu elements", n);
}

static TmTp tuple(MgElab *e, const MgL *a, const Cx *cxtm) {
    uint32_t iop;
    bool poly;
    if (e->ifopset != NONE && e->setlam != NONE && e->nat0 != NONE && e->natS != NONE) {
        iop = e->ifopset;
        poly = false;
    } else if (e->ifop != NONE && e->setlam != NONE && e->nat0 != NONE && e->natS != NONE) {
        iop = e->ifop;
        poly = true;
    } else {
        mge_fail(e, "Tuples cannot be used until natural numbers, if-then-else and set-level lambdas have been declared.");
    }
    uint32_t n = 1u + a->list_len;
    MgTm **items = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * (n + 1u));
    for (uint32_t i = 0; i < n; ++i)
        items[i] = mge_shift1(e, mge_check_tm_r(e, i == 0 ? a->a : a->list[i - 1u], mge_ty_set(e), cxtm));
    MgTm *body = items[n - 1u];
    for (uint32_t i = n - 1u; i-- > 0;) {
        MgTm *eqt = mge_tm_ap(e, mge_tm_ap(e, mge_tm_tap(e, mge_tm_h(e, e->eqPoly), mge_ty_set(e)), mge_tm_db(e, 0u)),
                          tm_buildnat(e, e->nat0, e->natS, i));
        MgTm *op = poly ? mge_tm_tap(e, mge_tm_h(e, iop), mge_ty_set(e)) : mge_tm_h(e, iop);
        body = mge_tm_ap(e, mge_tm_ap(e, mge_tm_ap(e, op, eqt), items[i]), body);
    }
    MgTm *r = mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->setlam), tm_buildnat(e, e->nat0, e->natS, n)),
                    mge_tm_lam(e, mge_ty_set(e), body));
    return mge_tt(r, mge_ty_set(e));
}

typedef struct {
    const MgL *a;
    TmTp at;
    const Cx *cxtm;
    TmTp out;
} LetTupleTry;

static void let_tuple_body(MgElab *e, void *ctx) {
    LetTupleTry *t = ctx;
    const MgL *a = t->a;
    TmTp at = t->at;
    const Cx *cxtm = t->cxtm;
    {
        MgTp *atp = at.tp;
        if (atp->tag != MG_TP_ARR)
            mge_not_found(e);
        MgTp *xtp = atp->b;
        uint32_t n = 0;
        MgTp *b = atp->a;
        while (b != xtp) {
            if (b->tag != MG_TP_ARR || b->a != xtp)
                mge_not_found(e);
            b = b->b;
            ++n;
        }
        uint32_t vars = 1u + a->names_len;
        if (n != vars)
            mge_fail(e, "The term in 'let [...] := <term>' has type of an %u-metatuple while %u variables were given.", n, vars);
        /* List.rev (x :: xl): the last name projects de Bruijn 0 */
        const Cx *ncx = cxtm;
        const char **all = mge_chunk_alloc(e, &e->tmp, sizeof(char *) * vars);
        all[0] = mge_intern(e, a->text);
        for (uint32_t i = 0; i < a->names_len; ++i)
            all[i + 1u] = mge_intern(e, a->names[i]);
        /* ncxtm @ cxtm with ncxtm = [z(0); y(1); x(2)]: push x first */
        for (uint32_t i = 0; i < vars; ++i) {
            uint32_t idx = vars - 1u - i; /* x gets n-1, ..., last gets 0 */
            MgTm *proj = mge_tm_db(e, idx);
            for (uint32_t k2 = 0; k2 < n; ++k2)
                proj = mge_tm_lam(e, xtp, proj);
            ncx = mge_cx_push(e, ncx, all[i], xtp, mge_tm_ap(e, at.tm, proj), false);
        }
        t->out = mge_extract_tm_r(e, a->b, ncx);
    }
}

static TmTp let_tuple(MgElab *e, const MgL *a, const Cx *cxtm) {
    LetTupleTry t = {a, mge_extract_tm_r(e, a->a, cxtm), cxtm, {0}};
    int k = mge_try_run(e, let_tuple_body, &t);
    if (k == 0)
        return t.out;
    if (k != RAISE_NOT_FOUND)
        mge_raise_kind(e, k);
    mge_fail(e, "The term in 'let [...] := <term>' has type %s which is not a meta-tuple type", mge_tp_str(e, t.at.tp));
}

static TmTp if_then_else(MgElab *e, const MgL *a, const Cx *cxtm) {
    if (e->ifop != NONE) {
        MgTm *atm = mge_check_tm_r(e, a->a, mge_ty_prop(e), cxtm);
        TmTp b = mge_extract_tm_r(e, a->b, cxtm);
        MgTm *ctm = mge_check_tm_r(e, a->c, b.tp, cxtm);
        if (b.tp == mge_ty_set(e) && e->ifopset != NONE)
            return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->ifopset), atm), b.tm), ctm), mge_ty_set(e));
        return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_ap(e, mge_tm_tap(e, mge_tm_h(e, e->ifop), b.tp), atm), b.tm), ctm), b.tp);
    }
    if (e->ifopset != NONE) {
        MgTm *atm = mge_check_tm_r(e, a->a, mge_ty_prop(e), cxtm);
        MgTm *btm = mge_check_tm_r(e, a->b, mge_ty_set(e), cxtm);
        MgTm *ctm = mge_check_tm_r(e, a->c, mge_ty_set(e), cxtm);
        return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->ifopset), atm), btm), ctm), mge_ty_set(e));
    }
    mge_fail(e, "if ... then ... else ... cannot be used until Notation for IfThenElse has been declared.");
}

/* Implop (... (Implop (Na x, a1) ...), ak): the name and its type
 * arguments, outermost last. */
static bool implop_spine(const MgL *a, const MgL **head, const MgL **args, uint32_t *k) {
    uint32_t n = 0;
    const MgL *h = mge_S(a);
    const MgL *rev[7];
    while (h->kind == MG_L_IMPLICIT && n < 7u) {
        rev[n++] = h->b;
        h = mge_S(h->a);
    }
    if (h->kind != MG_L_NAME || n == 0u || n > 6u)
        return false;
    for (uint32_t i = 0; i < n; ++i)
        args[i] = rev[n - 1u - i];
    *head = h;
    *k = n;
    return true;
}

typedef struct {
    const char *x;
    const Cx *cxtm;
    TmTp out;
} NameTry;

/* a name of the signature, applied to the section variables in scope */
static void signature_name(MgElab *e, void *ctx) {
    NameTry *t = ctx;
    MgTm *sm = mge_sig_lookup(e, t->x, t->cxtm);
    t->out = mge_tt(sm, mge_extr_tpoftm(e, NULL, t->cxtm, sm));
}

typedef struct {
    const MgL *a;
    const Cx *cxtm;
    TmTp out;
} BinderTry;

static void user_binder_body(MgElab *e, void *ctx) {
    BinderTry *t = ctx;
    t->out = user_binder(e, t->a, t->cxtm);
}

TmTp mge_extract_tm_r(MgElab *e, const MgL *a0, const Cx *cxtm) {
    const MgL *a = mge_S(a0);
    switch (a->kind) {
    case MG_L_NAME: {
        const char *x = mge_intern(e, a->text);
        MgTm *m;
        MgTp *tp;
        if (mge_tmtplookup(e, cxtm, x, &m, &tp))
            return mge_tt(m, tp);
        NameTry t = {x, cxtm, {0}};
        int k = mge_try_run(e, signature_name, &t);
        if (k == 0)
            return t.out;
        if (k != RAISE_NOT_FOUND)
            mge_raise_kind(e, k);
        if (e->theory == MG_PROFILE_SETMM && strcmp(x, "wi") == 0) {
            MgTp *prop = mge_ty_prop(e);
            return mge_tt(mge_tm_lam(e, prop, mge_tm_lam(e, prop, mge_tm_imp(e, mge_tm_db(e, 1u), mge_tm_db(e, 0u)))),
                      mge_ty_arr(e, prop, mge_ty_arr(e, prop, prop)));
        }
        if (e->theory == MG_PROFILE_SETMM && strcmp(x, "wal") == 0) {
            MgTp *sp = mge_ty_arr(e, mge_ty_set(e), mge_ty_prop(e));
            return mge_tt(mge_tm_lam(e, sp, mge_tm_all(e, mge_ty_set(e), mge_tm_db(e, 0u))), mge_ty_arr(e, sp, mge_ty_prop(e)));
        }
        mge_fail(e, "Unknown term %s -- it might be a proof in the position where a term is expected", x);
    }
    case MG_L_IMPLICIT: {
        const MgL *head, *args[6];
        uint32_t k;
        if (implop_spine(a, &head, args, &k)) {
            const char *x = mge_intern(e, head->text);
            MgTp *xa;
            if (mge_poly_get(e, x, k, &xa)) {
                MgTp *cl[6];
                for (uint32_t i = 0; i < k; ++i)
                    cl[i] = mge_extract_tp(e, args[i]);
                MgTm *xd = mge_sig_lookup(e, x, cxtm);
                for (uint32_t i = 0; i < k; ++i)
                    xd = mge_tm_tap(e, xd, cl[i]);
                return mge_tt(xd, mge_tpsubst(e, xa, cl, k));
            }
        }
        if (e->theory == MG_PROFILE_SETMM && mge_is_name(a->a, "wal")) {
            MgTp *sp = mge_ty_arr(e, mge_ty_set(e), mge_ty_prop(e));
            MgTm *n1 = mge_check_tm_r(e, a->b, sp, cxtm);
            if (n1->tag == MG_TM_LAM && n1->tp == mge_ty_set(e))
                return mge_tt(mge_tm_all(e, mge_ty_set(e), n1->f), mge_ty_prop(e));
            return mge_tt(mge_tm_ap(e, mge_tm_lam(e, sp, mge_tm_all(e, mge_ty_set(e), mge_tm_db(e, 0u))), n1), mge_ty_prop(e));
        }
        if (e->theory == MG_PROFILE_SETMM && mge_S(a->a)->kind == MG_L_IMPLICIT && mge_is_name(mge_S(a->a)->a, "wi")) {
            MgTm *n1 = mge_check_tm_r(e, mge_S(a->a)->b, mge_ty_prop(e), cxtm);
            MgTm *n2 = mge_check_tm_r(e, a->b, mge_ty_prop(e), cxtm);
            return mge_tt(mge_tm_imp(e, n1, n2), mge_ty_prop(e));
        }
        TmTp n1 = mge_extract_tm_r(e, a->a, cxtm);
        if (n1.tp->tag == MG_TP_ARR) {
            MgTm *n2 = mge_check_tm_r(e, a->b, n1.tp->a, cxtm);
            return mge_tt(mge_tm_ap(e, n1.tm, n2), n1.tp->b);
        }
        if (n1.tp == mge_ty_set(e)) {
            if (e->setimplop == NONE)
                mge_fail(e, "An implicit operator was used on a set before declared.");
            MgTm *n2 = mge_check_tm_r(e, a->b, mge_ty_set(e), cxtm);
            return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->setimplop), n1.tm), n2), mge_ty_set(e));
        }
        mge_fail(e, "Non-function applied to an argument");
    }
    case MG_L_INFIX:
        if (!a->set_infix) {
            if (strcmp(a->text, "->") == 0) {
                MgTm *n1 = mge_check_tm_r(e, a->a, mge_ty_prop(e), cxtm);
                MgTm *n2 = mge_check_tm_r(e, a->b, mge_ty_prop(e), cxtm);
                return mge_tt(mge_tm_imp(e, n1, n2), mge_ty_prop(e));
            }
            return infix(e, a, cxtm);
        }
        if (a->set_op == MG_SET_MEM) {
            if (e->setIn == NONE)
                mge_fail(e, ":e not declared");
            MgTm *a1 = mge_check_tm_r(e, a->a, mge_ty_set(e), cxtm);
            MgTm *a2 = mge_check_tm_r(e, a->b, mge_ty_set(e), cxtm);
            return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->setIn), a1), a2), mge_ty_prop(e));
        } else {
            TmTp a1 = mge_extract_tm_r(e, a->a, cxtm);
            MgTp *rtp = tp_rtp(a1.tp);
            if (rtp == mge_ty_prop(e)) {
                MgTm *a2 = mge_check_tm_r(e, a->b, a1.tp, cxtm);
                return mge_tt(mk_forall_tm(e, a1.tp, NONE, a1.tm, a2), mge_ty_prop(e));
            }
            if (rtp == mge_ty_set(e)) {
                if (e->setSubeq == NONE)
                    mge_fail(e, "c= not declared for sets (yet)");
                MgTm *a2 = mge_check_tm_r(e, a->b, a1.tp, cxtm);
                return mge_tt(mk_forall_tm(e, a1.tp, e->setSubeq, a1.tm, a2), mge_ty_prop(e));
            }
            mge_fail(e, "c= cannot be used with type %s", mge_tp_str(e, a1.tp));
        }
    case MG_L_BINDER: {
        Bvl bvl = {a->groups, a->group_count, 0u, false};
        if (strcmp(a->text, "fun") == 0)
            return extract_lam(e, bvl, a->a, cxtm);
        if (strcmp(a->text, "forall") == 0)
            return mge_tt(check_all(e, bvl, a->a, cxtm), mge_ty_prop(e));
        if (a->group_count == 0u)
            mge_fail(e, "Binder %s should bind at least one variable.", a->text);
        BinderTry t = {a, cxtm, {0}};
        int k = mge_try_run(e, user_binder_body, &t);
        if (k == 0)
            return t.out;
        if (k != RAISE_NOT_FOUND)
            mge_raise_kind(e, k);
        mge_fail(e, "Unknown binder %s -- bug", a->text);
    }
    case MG_L_POSTFIX:
        return unary(e, a, &e->postfixsem, cxtm);
    case MG_L_PREFIX:
        return unary(e, a, &e->prefixsem, cxtm);
    case MG_L_MTUPLE: {
        uint32_t l = 1u + a->list_len;
        TmTp n = mge_extract_tm_r(e, a->a, cxtm);
        MgTm **ml = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * (a->list_len ? a->list_len : 1u));
        for (uint32_t i = 0; i < a->list_len; ++i)
            ml[i] = mge_check_tm_r(e, a->list[i], n.tp, cxtm);
        MgTp *ltp = mtuple_tp(e, n.tp, l);
        MgTm *ap = mge_tm_db(e, 0u);
        for (uint32_t i = l; i > 0; --i)
            ap = mge_tm_ap(e, ap, mge_tm_db(e, i));
        MgTm *ctor = mge_tm_lam(e, mge_ty_arr(e, ltp, n.tp), ap);
        for (uint32_t i = 0; i < l; ++i)
            ctor = mge_tm_lam(e, n.tp, ctor);
        MgTm *r = mge_tm_ap(e, ctor, n.tm);
        for (uint32_t i = 0; i < a->list_len; ++i)
            r = mge_tm_ap(e, r, ml[i]);
        return mge_tt(r, mge_ty_arr(e, ltp, n.tp));
    }
    case MG_L_REP: {
        if (a->set_op == MG_SET_MEM && e->replop == NONE)
            mge_fail(e, "{..|x :e ..} notation can only be used after Notation Repl is declared");
        if (a->set_op == MG_SET_SUBQ && e->replop == NONE)
            mge_fail(e, "{..|x c= ..} notation can only be used after Notation Repl is declared");
        if (a->set_op == MG_SET_SUBQ && e->setPow == NONE)
            mge_fail(e, "{..|x c= ..} notation can only be used after Power is declared");
        const Cx *inner = mge_cx_push(e, cxtm, mge_intern(e, a->text), mge_ty_set(e), NULL, false);
        MgTm *atm = mge_check_tm_r(e, a->a, mge_ty_set(e), inner);
        MgTm *btm = mge_check_tm_r(e, a->b, mge_ty_set(e), cxtm);
        if (a->set_op == MG_SET_SUBQ)
            btm = mge_tm_ap(e, mge_tm_h(e, e->setPow), btm);
        return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->replop), btm), mge_tm_lam(e, mge_ty_set(e), atm)), mge_ty_set(e));
    }
    case MG_L_SEP: {
        if (a->set_op == MG_SET_MEM && e->sepop == NONE)
            mge_fail(e, "{x :e .. | ..} notation can only be used after Notation Sep is declared");
        if (a->set_op == MG_SET_SUBQ && e->sepop == NONE)
            mge_fail(e, "{x c= .. | ..} notation can only be used after Notation Sep is declared");
        if (a->set_op == MG_SET_SUBQ && e->setPow == NONE)
            mge_fail(e, "{x c= .. | ..} notation can only be used after Power is declared");
        MgTm *atm = mge_check_tm_r(e, a->a, mge_ty_set(e), cxtm);
        const Cx *inner = mge_cx_push(e, cxtm, mge_intern(e, a->text), mge_ty_set(e), NULL, false);
        MgTm *btm = mge_check_tm_r(e, a->b, mge_ty_prop(e), inner);
        if (a->set_op == MG_SET_SUBQ)
            atm = mge_tm_ap(e, mge_tm_h(e, e->setPow), atm);
        return mge_tt(mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->sepop), atm), mge_tm_lam(e, mge_ty_set(e), btm)), mge_ty_set(e));
    }
    case MG_L_SEPREP: {
        if (a->set_op == MG_SET_MEM && e->replsepop == NONE)
            mge_fail(e, "{.. |x :e .., ..} notation can only be used after Notation SepRepl is declared");
        if (a->set_op == MG_SET_SUBQ && e->replsepop == NONE)
            mge_fail(e, "{.. |x c= .., ..} notation can only be used after Notation SepRepl is declared");
        if (a->set_op == MG_SET_SUBQ && e->setPow == NONE)
            mge_fail(e, "{.. |x c= .., ..} notation can only be used after Power is declared");
        const Cx *inner = mge_cx_push(e, cxtm, mge_intern(e, a->text), mge_ty_set(e), NULL, false);
        MgTm *atm = mge_check_tm_r(e, a->a, mge_ty_set(e), inner);
        MgTm *btm = mge_check_tm_r(e, a->b, mge_ty_set(e), cxtm);
        MgTm *ctm = mge_check_tm_r(e, a->c, mge_ty_prop(e), inner);
        if (a->set_op == MG_SET_SUBQ)
            btm = mge_tm_ap(e, mge_tm_h(e, e->setPow), btm);
        MgTm *r = mge_tm_ap(e, mge_tm_ap(e, mge_tm_ap(e, mge_tm_h(e, e->replsepop), btm), mge_tm_lam(e, mge_ty_set(e), ctm)),
                        mge_tm_lam(e, mge_ty_set(e), atm));
        return mge_tt(r, mge_ty_set(e));
    }
    case MG_L_LET: {
        const char *x = mge_intern(e, a->text);
        if (!a->asc) {
            TmTp at = mge_extract_tm_r(e, a->a, cxtm);
            return mge_extract_tm_r(e, a->b, mge_cx_push(e, cxtm, x, at.tp, at.tm, false));
        }
        if (a->asc->kind == MG_ASC_TP) {
            MgTp *ctp = mge_extract_tp(e, a->asc->type);
            MgTm *atm = mge_check_tm_r(e, a->a, ctp, cxtm);
            return mge_extract_tm_r(e, a->b, mge_cx_push(e, cxtm, x, ctp, atm, false));
        }
        break;
    }
    case MG_L_LETM:
        return let_tuple(e, a, cxtm);
    case MG_L_IF:
        return if_then_else(e, a, cxtm);
    case MG_L_SETENUM:
        return set_enum(e, a, cxtm);
    case MG_L_NUM:
        if (!a->negative && !a->text2 && !a->text3) {
            int64_t n = int_of_string(e, a->text);
            if (e->nat0 == NONE || e->natS == NONE)
                mge_fail(e, "A notation for natural numbers has not been declared");
            return mge_tt(tm_buildnat(e, e->nat0, e->natS, n), mge_ty_set(e));
        }
        break;
    case MG_L_PAREN:
        return tuple(e, a, cxtm);
    case MG_L_QSTRING:
        return qstring(e, a->text, cxtm);
    case MG_L_STRING: {
        const unsigned char *x = (const unsigned char *)a->text;
        size_t len = strlen(a->text);
        if (e->theory == MG_PROFILE_HOAS) {
            MgTm *tm = hoas_bind(e, mge_tm_db(e, 0u));
            for (size_t i = len; i-- > 0;)
                tm = hoas_pair(e, hoas_byte(e, x[i]), tm);
            return mge_tt(tm, mge_ty_set(e));
        }
        if (e->theory == MG_PROFILE_EGAL) {
            if (e->seqcons == NONE)
                mge_fail(e, "cannot use strings since seq_cons not defined");
            if (e->nat0 == NONE || e->natS == NONE)
                mge_fail(e, "cannot use strings since a notation for the natural numbers has not been declared");
            MgTm *tm = mge_tm_h(e, e->nat0);
            for (size_t i = len; i-- > 0;)
                tm = egal_cons(e, e->seqcons, egal_byte(e, e->nat0, e->natS, e->seqcons, x[i]), tm);
            return mge_tt(tm, mge_ty_set(e));
        }
        mge_fail(e, "strings are currently only allowed in HOAS and Egal");
    }
    case MG_L_BYTE:
        if (e->theory == MG_PROFILE_HOAS)
            return mge_tt(hoas_byte(e, a->byte), mge_ty_set(e));
        if (e->theory == MG_PROFILE_EGAL) {
            if (e->seqcons == NONE)
                mge_fail(e, "cannot use bytes since seq_cons not defined");
            if (e->nat0 == NONE || e->natS == NONE)
                mge_fail(e, "cannot use bytes since a notation for the natural numbers has not been declared");
            return mge_tt(egal_byte(e, e->nat0, e->natS, e->seqcons, a->byte), mge_ty_set(e));
        }
        mge_fail(e, "bytes are currently only allowed in HOAS and Egal");
    default:
        break;
    }
    mge_fail(e, "Ill-formed term");
}

MgTm *mge_check_tm_r(MgElab *e, const MgL *a, MgTp *mtp, const Cx *cxtm) {
    const MgL *sa = mge_S(a);
    if (sa->kind == MG_L_BINDER && strcmp(sa->text, "fun") == 0)
        return check_lam(e, mtp, (Bvl){sa->groups, sa->group_count, 0u, false}, sa->a, cxtm);
    TmTp n = mge_extract_tm_r(e, a, cxtm);
    if (n.tp != mtp)
        mge_fail(e, "Type Error found %s expected %s", mge_tp_str(e, n.tp), mge_tp_str(e, mtp));
    return n.tm;
}

/* interpret.ml tm_metavar_subst: assigned metavariables are replaced. */
MgTm *mge_metavar_subst(MgElab *e, uint32_t cxn, MgTm *m) {
    switch (m->tag) {
    case MG_TM_NAME: {
        const char *h = mge_name_text(e, m->n);
        if (h[0] != '?')
            return m;
        const char *vn = mge_intern(e, h + 1);
        uint64_t cxv, mv;
        if (!mge_map_get(&e->metavarcx, K(vn), &cxv) || !mge_map_get(&e->metavarassoc, K(vn), &mv))
            return m;
        uint32_t vcxn = P(const MetaCx, cxv)->count;
        if (vcxn == cxn)
            return P(MgTm, mv);
        if (vcxn < cxn)
            return mge_tmshift(e, 0u, (int32_t)(cxn - vcxn), P(MgTm, mv));
        mge_fail(e, "Metavar ?%s occurs out of context", vn);
    }
    case MG_TM_TAP:
        return mge_tm_tap(e, mge_metavar_subst(e, cxn, m->f), m->tp);
    case MG_TM_AP:
        return mge_tm_ap(e, mge_metavar_subst(e, cxn, m->f), mge_metavar_subst(e, cxn, m->x));
    case MG_TM_IMP:
        return mge_tm_imp(e, mge_metavar_subst(e, cxn, m->f), mge_metavar_subst(e, cxn, m->x));
    case MG_TM_LAM:
        return mge_tm_lam(e, m->tp, mge_metavar_subst(e, cxn + 1u, m->f));
    case MG_TM_ALL:
        return mge_tm_all(e, m->tp, mge_metavar_subst(e, cxn + 1u, m->f));
    default:
        return m;
    }
}

static MgTm *finish_metavars(MgElab *e, MgTm *m) {
    if (!e->metavarcx.len)
        return m;
    return mge_metavar_subst(e, mge_cx_len(e->ctxtm), m);
}

static TmTp extract_tm(MgElab *e, const MgL *a) {
    mge_map_clear(&e->metavarcx);
    mge_map_clear(&e->metavarassoc);
    TmTp r = mge_extract_tm_r(e, a, e->ctxtm);
    r.tm = finish_metavars(e, r.tm);
    return r;
}

static MgTm *check_tm(MgElab *e, const MgL *a, MgTp *mtp) {
    mge_map_clear(&e->metavarcx);
    mge_map_clear(&e->metavarassoc);
    return finish_metavars(e, mge_check_tm_r(e, a, mtp, e->ctxtm));
}

/* ── closures over the sections ───────────────────────────────────────── */

static MgTp *tparclos(MgElab *e, MgTp *a) {
    for (const Clo *c = e->clos; c; c = c->next)
        if (!c->hyp)
            for (uint32_t i = 0; i < c->count; ++i)
                a = mge_ty_arr(e, c->tp, a);
    return a;
}

static MgTm *tmlamclos(MgElab *e, MgTm *m) {
    for (const Clo *c = e->clos; c; c = c->next)
        if (!c->hyp)
            for (uint32_t i = 0; i < c->count; ++i)
                m = mge_tm_lam(e, c->tp, m);
    return m;
}

static MgTm *tmallclos(MgElab *e, MgTm *m) {
    for (const Clo *c = e->clos; c; c = c->next) {
        if (c->hyp) {
            m = mge_tm_imp(e, c->prop, m);
            continue;
        }
        for (uint32_t i = 0; i < c->count; ++i)
            m = mge_tm_all(e, c->tp, m);
    }
    return m;
}

/* ── identities ───────────────────────────────────────────────────────── */

/* The kernel's convention for a declaration with `levels` type binders:
 * the front end's TpVar i (outermost first) is de Bruijn levels-1-i. */
MgTp *mge_kernel_tp(MgElab *e, MgTp *a, uint32_t levels) {
    if (!a->ftv)
        return a;
    switch (a->tag) {
    case MG_TP_VAR:
        if (a->n >= levels)
            mge_fail(e, "type variable out of scope");
        return mge_ty_var(e, levels - 1u - a->n);
    case MG_TP_ARR: {
        MgTp *b = mge_kernel_tp(e, a->b, levels);
        return mge_ty_arr(e, mge_kernel_tp(e, a->a, levels), b);
    }
    default:
        return a;
    }
}

MgTm *mge_kernel_tm(MgElab *e, MgTm *m, uint32_t levels) {
    if (!levels || !m->ftv)
        return m;
    switch (m->tag) {
    case MG_TM_TAP:
        return mge_tm_tap(e, mge_kernel_tm(e, m->f, levels), mge_kernel_tp(e, m->tp, levels));
    case MG_TM_AP:
        return mge_tm_ap(e, mge_kernel_tm(e, m->f, levels), mge_kernel_tm(e, m->x, levels));
    case MG_TM_IMP:
        return mge_tm_imp(e, mge_kernel_tm(e, m->f, levels), mge_kernel_tm(e, m->x, levels));
    case MG_TM_LAM:
        return mge_tm_lam(e, mge_kernel_tp(e, m->tp, levels), mge_kernel_tm(e, m->f, levels));
    case MG_TM_ALL:
        return mge_tm_all(e, mge_kernel_tp(e, m->tp, levels), mge_kernel_tm(e, m->f, levels));
    default:
        return m;
    }
}

void mge_check_init(MgElab *e, MgCheck *c) {
    mg_check_init(c, (MgEnv){e->t, e->t->rev}, &e->budget, NULL);
}

/* syntax.ml ptm_lam_id (lam) / ptm_all_id: the identity name id. */
static uint32_t identity(MgElab *e, MgTm *m, uint32_t levels, bool lam) {
    MgTm *k = mge_kernel_tm(e, m, levels);
    for (uint32_t i = 0; i < levels; ++i)
        k = mge_ck(e, lam ? mg_tm_tlam(e->s, k) : mg_tm_tall(e->s, k));
    MgCheck c;
    mge_check_init(e, &c);
    MgHash h;
    MgStatus st = mg_hash_identity(&c, k, levels, lam, &h);
    if (st != MG_OK)
        mge_fail(e, "identity: %s", c.reason ? c.reason : mg_status_name(st));
    char hex[65];
    mg_hash_hex(&h, hex);
    return mge_name_id(e, hex);
}

uint32_t mge_tm_id(MgElab *e, MgTm *m) { return identity(e, m, 0u, true); }

typedef struct {
    const char *hash;
    size_t field;
    bool definition;      /* only when declared as a definition */
} MaConst;

#define MA_FIELD(f) offsetof(MgElab, f)

void mge_megaauto_item(MgElab *e, uint32_t h, bool d) {
    if (e->theory != MG_PROFILE_EGAL)
        return;
    const char *x = mge_name_text(e, h);
    if (strcmp(x, "9db634daee7fc36315ddda5f5f694934869921e9c5f55e8b25c91c0a07c5cbec") == 0) { /* ordsucc */
        e->ma_setordsucc = h;
        uint32_t h1 = mge_tm_id(e, mge_tm_ap(e, mge_tm_h(e, h), mge_tm_prim(e, 2u)));
        e->ma_set1 = h1;
        e->ma_set2 = mge_tm_id(e, mge_tm_ap(e, mge_tm_h(e, h), mge_tm_h(e, h1)));
        return;
    }
    static const MaConst items[] = {
        {"b260cb5327df5c1f762d4d3068ddb3c7cc96a9cccf7c89cee6abe113920d16f1", MA_FIELD(ma_setsum), false},
        {"93592da87a6f2da9f7eb0fbd449e0dc4730682572e0685b6a799ae16c236dcae", MA_FIELD(ma_setlambda), false},
        {"ecef5cea93b11859a42b1ea5e8a89184202761217017f3a5cdce1b91d10b34a7", MA_FIELD(ma_setprod), true},
        {"58c1782da006f2fb2849c53d5d8425049fad551eb4f8025055d260f0c9e1fe40", MA_FIELD(ma_setap), false},
        {"8ab5fa18b3cb4b4b313a431cc37bdd987f036cc47f175379215f69af5977eb3b", MA_FIELD(ma_setPi), false},
        {"fcd77a77362d494f90954f299ee3eb7d4273ae93d2d776186c885fc95baa40dc", MA_FIELD(ma_setexp), true},
    };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; ++i)
        if (strcmp(x, items[i].hash) == 0) {
            if (!items[i].definition || d)
                *(uint32_t *)((char *)e + items[i].field) = h;
            return;
        }
}

void mge_megaauto_known(MgElab *e, uint32_t h) {
    if (e->theory != MG_PROFILE_EGAL)
        return;
    static const MaConst knowns[] = {
        {"4dcf737d976ab59871f178ab4227edffb26181c236613caeb68c84de8f2e6aa1", MA_FIELD(ma_In_0_2), false},
        {"b28b8818076ea2727fe37ccdc19db7910186e7991e3cd809d9bd88239f294936", MA_FIELD(ma_In_1_2), false},
        {"037887d4c129ed2b2fa3e5f38118106ab9d3677e677e927197774264a221a83e", MA_FIELD(ma_tupleI0), false},
        {"4b42b19620bd13f38596de6607fccc7b23ef8c6ed2a71e396055f290a6d75265", MA_FIELD(ma_tupleI1), false},
        {"30940631e6ea9dd2e197436d1254b1a8d7d9087b0d6df24ed44090a81c91efef", MA_FIELD(ma_tuple0_setsum), false},
        {"02c877b50d6e337679aa91261e3c90c651ccef333d0318a0d5dd2735af5f37ba", MA_FIELD(ma_tuple1_setsum), false},
        {"8ad8ee9f2a2cceb15dc4d629f57caaf70a3ef361eb59f77fe6523c202000d9e8", MA_FIELD(ma_tuple_2_0_eq), false},
        {"494f183d032dc85055f2fcc389f025ec799bf85f978311eac319162a5c3c1ae8", MA_FIELD(ma_tuple_2_1_eq), false},
        {"2553ebe05147c51d42ad34303cd2b9bcfe20abd60f990923124e40051ba30fbf", MA_FIELD(ma_tuple_2_Sigma), false},
        {"5385a6f08cba961a8f90f2e36a2f7d49681349aec6db4e85249a481e5be48899", MA_FIELD(ma_tuple_2_setprod), false},
        {"ebb9bad843b554c6cd9c373bd2fb7dd8b30c1a3fbb10a4018fafd80db6caaaee", MA_FIELD(ma_tuple_2_eta), false},
        {"73d7865ec772f43295363850120c05f9e72b0466bd3af5828a60b87caf35f70a", MA_FIELD(ma_ap0_Sigma), false},
        {"d4c92acb68a5b835371527eab3ea8ddb1c122948915729172786e2d04b552ad3", MA_FIELD(ma_ap1_Sigma), false},
        {"a00be5d41e8522ab870b586366e81c4fd66f8fb2ca0a74e27018431d280a7fbb", MA_FIELD(ma_tuple_Sigma_eta), false},
        {"c98183df697f6c43af2633457df05e325bcd6e757b0eef980ed1fa53febad476", MA_FIELD(ma_lam_ext), false},
        {"7ab4c6697df228e0723c27c62bb05e611b05b2215b151c2507aab35b4ee34676", MA_FIELD(ma_lam_Pi), false},
        {"0fc03e3865b6f7cd7e469e8661923dc1edd845b81ec12c9dd8728082d51ed83b", MA_FIELD(ma_ap_Pi), false},
        {"3b9b4b76541e3d742c0ccd9247bf93c028026783cdf1360b09677bdbfd191f7c", MA_FIELD(ma_beta), false},
        {"73fa526ffdb4fe1b47297609c1b8558ce7ee058c64e17b285de72ae0a78e2103", MA_FIELD(ma_Pi_ext), false},
        {"a87285d54bb15d7ad174232d02bcf414291aa8cf12bdae3ba6a7d97fcfef591f", MA_FIELD(ma_Pi_eta), false},
        {"c1253491187dd3685e59eca2206b98305093ded6398e1deec69a95b81adc2515", MA_FIELD(ma_Pi_cod_ext), false},
    };
    const char *x = mge_name_text(e, h);
    for (size_t i = 0; i < sizeof knowns / sizeof knowns[0]; ++i)
        if (strcmp(x, knowns[i].hash) == 0) {
            *(uint32_t *)((char *)e + knowns[i].field) = h;
            return;
        }
}

/* syntax.ml tm_beta_eta_norm */
MgTm *mge_beta_eta_norm(MgElab *e, MgTm *m) {
    MgCheck c;
    mge_check_init(e, &c);
    MgTm *n = mg_tm_nf(&c, m);
    if (!n)
        mge_fail(e, "normalization: %s", c.reason ? c.reason : mg_status_name(c.status));
    return n;
}

static bool pfg_pure(void *ctx, uint32_t name, MgHash *pure) {
    MgElab *e = ctx;
    uint64_t v;
    if (!mge_map_get(&e->pfgtmhh, name, &v))
        return false;
    *pure = *P(MgHash, v);
    return true;
}

/* The Proofgold translation's Failure carries its message in e->msg. */
static void pfg_status(MgElab *e, MgPfgStatus st) {
    switch (st) {
    case MG_PFG_OK:
        return;
    case MG_PFG_FAILURE:
        mge_raise_kind(e, RAISE_FAILURE);
    case MG_PFG_REDEXMAX:
        mg_buf_reset(&e->msg);
        mg_buf_puts(&e->msg, "Fatal error: exception Mathdatapfg.RedexMax");
        mge_raise_kind(e, RAISE_FATAL);
    default:
        mge_nomem(e);
    }
}

/* pfg_propid of a monomorphic proposition; *propid receives the
 * proposition identity. */
static void pfg_propid(MgElab *e, MgTm *agtm, MgHash *propid) {
    MgTm *normal = mge_beta_eta_norm(e, agtm);
    MgPfgEnv env = {e->theory, pfg_pure, e, &e->msg};
    MgHash pure;
    pfg_status(e, mg_pfg_propid(&env, e->s, normal, &pure, propid));
}

static void pfg_objid(MgElab *e, MgTm *bgtm, MgTp *bgtp, MgHash *pure) {
    MgTm *normal = mge_beta_eta_norm(e, bgtm);
    MgPfgEnv env = {e->theory, pfg_pure, e, &e->msg};
    MgHash objid;
    pfg_status(e, mg_pfg_objid(&env, e->s, normal, bgtp, pure, &objid));
}

static void set_pure(MgElab *e, uint32_t h, const MgHash *pure) {
    MgHash *copy = PERM(e, MgHash);
    *copy = *pure;
    mge_map_set(e, &e->pfgtmhh, h, V(copy));
}

/* ── kernel admission ─────────────────────────────────────────────────── */

__attribute__((noreturn)) void mge_kernel_refused(MgElab *e) {
    mg_buf_reset(&e->msg);
    mg_buf_puts(&e->msg, e->report.message);
    mge_raise_kind(e, RAISE_KERNEL);
}

MgTp *mge_poly_type(MgElab *e, MgTp *a, uint32_t levels) {
    MgTp *k = mge_kernel_tp(e, a, levels);
    for (uint32_t i = 0; i < levels; ++i)
        k = mge_ckt(e, mg_tp_all(e->s, k));
    return k;
}

MgTm *mge_poly_term(MgElab *e, MgTm *m, uint32_t levels, bool lam) {
    MgTm *k = mge_kernel_tm(e, m, levels);
    for (uint32_t i = 0; i < levels; ++i)
        k = mge_ck(e, lam ? mg_tm_tlam(e->s, k) : mg_tm_tall(e->s, k));
    return k;
}

/* megalodon.ml add_sigdelta */
static void add_sigdelta(MgElab *e, uint32_t h, uint32_t levels, MgTm *m) {
    MgTm *n = mge_beta_eta_norm(e, m);
    if (n->tag == MG_TM_NAME)
        return;
    MgTm *head = n;
    uint32_t k = 0;
    while (head->tag == MG_TM_TAP && k < 3u) {
        if (head->tp != mge_ty_var(e, 0u) && head->tp != mge_ty_var(e, 1u) && head->tp != mge_ty_var(e, 2u))
            break;
        head = head->f;
        ++k;
    }
    if (k > 0u && head->tag == MG_TM_NAME) {
        /* TpAp(TmH, TpVar 0), TpAp(TpAp(TmH, TpVar 0), TpVar 1), ... */
        bool exact = true;
        MgTm *walk = n;
        for (uint32_t i = k; i > 0; --i) {
            if (walk->tp != mge_ty_var(e, i - 1u))
                exact = false;
            walk = walk->f;
        }
        if (exact)
            return;
    }
    PTm *d = PERM(e, PTm);
    d->levels = levels;
    d->m = n;
    if (levels >= 6u)
        e->conv_memory_off = true;
    mge_map_set(e, &e->sigdelta, h, V(d));
}

/* ── rows ─────────────────────────────────────────────────────────────── */

void mge_row_out(MgElab *e) {
    if (e->rows && !e->row.failed)
        fwrite(e->row.data, 1u, e->row.len, e->rows);
    mg_buf_reset(&e->row);
}

void mge_row_begin(MgElab *e) { mg_buf_reset(&e->row); }

/* ── the signature and sections ───────────────────────────────────────── */

static void undo_push(MgElab *e, Undo u) {
    if (!e->frame_len)
        return;
    if (e->undo_len == e->undo_cap) {
        size_t cap = e->undo_cap ? e->undo_cap * 2u : 64u;
        Undo *grown = realloc(e->undo, cap * sizeof(Undo));
        if (!grown)
            mge_nomem(e);
        e->undo = grown;
        e->undo_cap = cap;
    }
    e->undo[e->undo_len++] = u;
}

void mge_undo_ctx(MgElab *e) {
    undo_push(e, (Undo){.kind = U_CTX, .ctxtm = e->ctxtm, .ctxpf = e->ctxpf, .clos = e->clos});
}

static void scoped_push(MgElab *e, Map *m, const char *x, uint64_t val) {
    mge_stk_push(e, m, K(x), val);
    undo_push(e, (Undo){.kind = U_STACK, .map = m, .key = K(x)});
}

static void carry_tm(MgElab *e, const char *x, MgTm *m) {
    for (size_t i = 0; i < e->frame_len; ++i) {
        Frame *f = &e->frames[i];
        if (f->tm_len == f->tm_cap) {
            size_t cap = f->tm_cap ? f->tm_cap * 2u : 16u;
            CarryTm *g = realloc(f->tm, cap * sizeof(CarryTm));
            if (!g)
                mge_nomem(e);
            f->tm = g;
            f->tm_cap = cap;
        }
        f->tm[f->tm_len++] = (CarryTm){x, m};
    }
}

void mge_carry_pf(MgElab *e, const char *x, uint32_t known) {
    for (size_t i = 0; i < e->frame_len; ++i) {
        Frame *f = &e->frames[i];
        if (f->pf_len == f->pf_cap) {
            size_t cap = f->pf_cap ? f->pf_cap * 2u : 16u;
            CarryPf *g = realloc(f->pf, cap * sizeof(CarryPf));
            if (!g)
                mge_nomem(e);
            f->pf = g;
            f->pf_cap = cap;
        }
        f->pf[f->pf_len++] = (CarryPf){x, known};
    }
}

static void sigtm_add(MgElab *e, const char *x, MgTm *m, const App *app) {
    SigTm *st = PERM(e, SigTm);
    st->m = m;
    st->app = app;
    scoped_push(e, &e->sigtm, x, V(st));
}

void mge_sigpf_add(MgElab *e, const char *x, uint32_t known, const App *app) {
    SigPf *sp = PERM(e, SigPf);
    sp->known = known;
    sp->app = app;
    scoped_push(e, &e->sigpf, x, V(sp));
}

/* sigtm := (x, aptmloc m) :: sigtm, and every open section carries it. */
static void declare_tm(MgElab *e, const char *x, uint32_t h) {
    MgTm *m = mge_tm_h(e, h);
    sigtm_add(e, x, m, e->aptm);
    carry_tm(e, x, m);
}

void mge_declare_pf(MgElab *e, const char *x, uint32_t h) {
    mge_sigpf_add(e, x, h, e->appf);
    mge_carry_pf(e, x, h);
}

bool mge_used(MgElab *e, const char *x) {
    if (sig_has(&e->sigtm, x) || sig_has(&e->sigpf, x) || mge_tp_position(e, x) >= 0)
        return true;
    for (const Cx *c = e->ctxtm; c; c = c->next)
        if (strcmp(c->name, x) == 0)
            return true;
    for (const Hy *h = e->ctxpf; h; h = h->next)
        if (strcmp(h->name, x) == 0)
            return true;
    return false;
}

static void handlepolysnow(MgElab *e) {
    for (size_t i = 0; i < e->future_len; ++i)
        mge_map_set(e, &e->polytm[e->future[i].arity], K(e->future[i].name), e->future[i].val);
    e->future_len = 0u;
    for (size_t i = 0; i < e->future_pf_len; ++i)
        mge_map_set(e, &e->polypf[e->future_pf[i].arity], K(e->future_pf[i].name), e->future_pf[i].val);
    e->future_pf_len = 0u;
}

static void push_future(MgElab *e, Future **v, size_t *len, size_t *cap, const char *x,
                        uint32_t arity, uint64_t what) {
    if (*len == *cap) {
        size_t c = *cap ? *cap * 2u : 16u;
        Future *g = realloc(*v, c * sizeof(Future));
        if (!g)
            mge_nomem(e);
        *v = g;
        *cap = c;
    }
    (*v)[(*len)++] = (Future){x, arity, what};
}

static void pushpolytm(MgElab *e, const char *x, uint32_t i, MgTp *tp) {
    if (i <= 6u)
        push_future(e, &e->future, &e->future_len, &e->future_cap, x, i, V(tp));
}

void mge_pushpolypf(MgElab *e, const char *x, uint32_t i, MgTm *prop) {
    if (i <= 6u)
        push_future(e, &e->future_pf, &e->future_pf_len, &e->future_pf_cap, x, i, V(prop));
}

static void run_undo(MgElab *e, size_t mark) {
    mge_conv_forget(e);
    while (e->undo_len > mark) {
        Undo u = e->undo[--e->undo_len];
        switch (u.kind) {
        case U_CTX:
            e->ctxtm = u.ctxtm;
            e->ctxpf = u.ctxpf;
            e->clos = u.clos;
            break;
        case U_TPVARS:
            handlepolysnow(e);
            e->ctxtp_len = 0u;
            break;
        default:
            mge_stk_pop(e, u.map, u.key);
            break;
        }
    }
}

const App *mge_app_push(MgElab *e, const App *prev, uint8_t kind, const char *name) {
    App *a = PERM(e, App);
    *a = (App){kind, name, prev};
    return a;
}

/* ── document items ───────────────────────────────────────────────────── */

static uint32_t sigtmh_get(MgElab *e, const char *x, bool *found) {
    uint64_t v;
    *found = mge_map_get(&e->sigtmh, K(x), &v);
    return *found ? (uint32_t)v : NONE;
}

static const MgPrimSpec *primname(MgElab *e, const char *x) {
    const MgProfileSpec *spec = mg_profile_spec(e->theory);
    for (uint32_t i = 0; spec && i < spec->prim_count; ++i)
        if (spec->prims[i].name && strcmp(spec->prims[i].name, x) == 0)
            return &spec->prims[i];
    return NULL;
}

static MgTp *prim_type(MgElab *e, const MgPrimSpec *p) {
    return mge_ckt(e, mg_front_parse_tp(e->s, p->type));
}

static void reserved_setmm(MgElab *e, const char *x) {
    if (e->theory == MG_PROFILE_SETMM && (strcmp(x, "wi") == 0 || strcmp(x, "wal") == 0))
        mge_fail(e, "%s is a reserved built-in name for SetMM", x);
}

void mge_tp_row(MgElab *e, MgTp *a) { mg_front_tp_sexpr(&e->row, a); }
void mge_tm_row(MgElab *e, MgTm *m) { mg_front_tm_sexpr(&e->row, e->s, m); }

static void eval_param(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    reserved_setmm(e, x);
    if (mge_used(e, x))
        mge_fail(e, "%s has already been used.", x);
    uint32_t i = e->ctxtp_len;
    MgTp *atp = mge_extract_tp(e, it->a);
    MgTp *agtp = tparclos(e, atp);
    const MgPrimSpec *prim = primname(e, x);
    if (prim) {
        if (prim->type_variables != i || prim_type(e, prim) != agtp)
            mge_fail(e, "%s is the name of a built-in primitive which does not have the given type.", x);
        if (i > 6u)
            mge_fail(e, "It is forbidden to have more than 6 type variables.");
        uint32_t xj = prim->index;
        uint32_t xhv = identity(e, mge_tm_prim(e, xj), 0u, true);
        mge_row_begin(e);
        mg_buf_printf(&e->row, "(PRIM %u \"%s\" \"%s\" ", xj, x, mge_name_text(e, xhv));
        mge_tp_row(e, agtp);
        mg_buf_printf(&e->row, " %d)\n", e->lineno);
        mge_row_out(e);
        if (e->admission &&
            !mg_admit_prim(e->admission, x, mge_name_text(e, xhv), xj, mge_poly_type(e, agtp, 0u), (uint64_t)e->lineno))
            mge_kernel_refused(e);
        if (strcmp(x, "Empty") == 0)
            e->set0 = xhv;
        add_sigdelta(e, xhv, 0u, mge_tm_prim(e, xj));
        mge_megaauto_item(e, xhv, false);
        if (e->theory == MG_PROFILE_SETMM) {
            MgHash pure;
            pfg_objid(e, mge_tm_prim(e, xj), agtp, &pure);
            set_pure(e, xhv, &pure);
        }
        if (strcmp(x, "Power") == 0)
            e->setPow = xhv;
        if (strcmp(x, "In") == 0) {
            e->setIn = xhv;
            MgTp *set = mge_ty_set(e);
            MgTm *in = mge_tm_h(e, xhv);
            MgTm *sub = mge_tm_lam(e, set, mge_tm_lam(e, set, mge_tm_all(e, set,
                mge_tm_imp(e, mge_tm_ap(e, mge_tm_ap(e, in, mge_tm_db(e, 0u)), mge_tm_db(e, 2u)),
                       mge_tm_ap(e, mge_tm_ap(e, in, mge_tm_db(e, 0u)), mge_tm_db(e, 1u))))));
            e->setSubeq = identity(e, sub, 0u, true);
        }
        mge_map_set(e, &e->sigtmh, K(x), xhv);
        PTp *pt = PERM(e, PTp);
        *pt = (PTp){i, agtp};
        mge_map_set(e, &e->sigtmof, xhv, V(pt));
        if (i > 0u)
            pushpolytm(e, x, i, agtp);
        if (i == 0u && !mge_map_get(&e->indextms, xhv, NULL))
            mge_map_set(e, &e->indextms, xhv, V(prim_type(e, prim)));
        declare_tm(e, x, xhv);
        return;
    }
    if (i > 0u)
        mge_fail(e, "%s must be defined. The only polymorphic parameter allowed are built-in primitives.", x);
    if (mge_used(e, x))
        mge_fail(e, "%s has already been used.", x);
    bool found;
    uint32_t xhv = sigtmh_get(e, x, &found);
    if (!found)
        mge_fail(e, "Unknown id for %s", x);
    mge_row_begin(e);
    mg_buf_printf(&e->row, "(PARAM \"%s\" \"%s\" %u ", x, mge_name_text(e, xhv), i);
    mge_tp_row(e, agtp);
    mg_buf_printf(&e->row, " %d)\n", e->lineno);
    mge_row_out(e);
    if (e->admission &&
        !mg_admit_param(e->admission, x, mge_name_text(e, xhv), i, mge_poly_type(e, agtp, i), (uint64_t)e->lineno))
        mge_kernel_refused(e);
    mge_megaauto_item(e, xhv, false);
    if (e->theory == MG_PROFILE_EGAL &&
        strcmp(mge_name_text(e, xhv), "7a7fd30507c2156eeace3d2784ada104fee81316a9d6f02db384ad7f0a180e26") == 0)
        e->seqcons = xhv;
    if (strcmp(x, "Subq") == 0) {
        if (e->setSubeq == NONE)
            mge_fail(e, "Subq can only be used after In is declared and then only to mean fun X Y:set => forall x:set, x :e X -> x :e Y");
        if (xhv != e->setSubeq)
            mge_fail(e, "Subq can only be used to mean fun X Y:set => forall x:set, x :e X -> x :e Y");
    }
    uint64_t itp;
    bool known = mge_map_get(&e->indextms, xhv, &itp);
    if (!known && !mge_map_get(&e->pfgtmhh, xhv, NULL))
        return; /* neither indexed nor known to Proofgold: not declared */
    if (known && P(MgTp, itp) != agtp)
        mge_fail(e, "The id %s associated with the parameter %s has is indexed to have the type %s not %s",
             mge_name_text(e, xhv), x, mge_tp_str(e, P(MgTp, itp)), mge_tp_str(e, agtp));
    PTp *pt = PERM(e, PTp);
    *pt = (PTp){i, agtp};
    mge_map_set(e, &e->sigtmof, xhv, V(pt));
    declare_tm(e, x, xhv);
}

static void eval_def(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    reserved_setmm(e, x);
    if (mge_used(e, x))
        mge_fail(e, "%s has already been used.", x);
    uint32_t i = e->ctxtp_len;
    MgTm *bgtm;
    MgTp *bgtp;
    if (!it->a) {
        TmTp b = extract_tm(e, it->b);
        bgtm = tmlamclos(e, b.tm);
        bgtp = tparclos(e, b.tp);
    } else {
        MgTp *atp = mge_extract_tp(e, it->a);
        MgTm *btm = check_tm(e, it->b, atp);
        bgtm = tmlamclos(e, btm);
        bgtp = tparclos(e, atp);
    }
    uint32_t xhv = identity(e, bgtm, i, true);
    mge_megaauto_item(e, xhv, true);
    if (e->theory == MG_PROFILE_EGAL &&
        strcmp(mge_name_text(e, xhv), "7a7fd30507c2156eeace3d2784ada104fee81316a9d6f02db384ad7f0a180e26") == 0)
        e->seqcons = xhv;
    mge_row_begin(e);
    mg_buf_printf(&e->row, "(DEF \"%s\" \"%s\" %u ", x, mge_name_text(e, xhv), i);
    mge_tp_row(e, bgtp);
    mg_buf_putc(&e->row, ' ');
    mge_tm_row(e, bgtm);
    mg_buf_printf(&e->row, " %d)\n", e->lineno);
    mge_row_out(e);
    if (e->admission &&
        !mg_admit_def(e->admission, x, mge_name_text(e, xhv), i, mge_poly_type(e, bgtp, i),
                      mge_poly_term(e, bgtm, i, true), (uint64_t)e->lineno))
        mge_kernel_refused(e);
    if (i == 0u) {
        MgHash pure;
        pfg_objid(e, bgtm, bgtp, &pure);
        set_pure(e, xhv, &pure);
    }
    add_sigdelta(e, xhv, i, bgtm);
    if (strcmp(x, "Subq") == 0) {
        if (e->setSubeq == NONE)
            mge_fail(e, "Subq can only be used after In is declared and then only to mean fun X Y:set => forall x:set, x :e X -> x :e Y");
        if (xhv != e->setSubeq)
            mge_fail(e, "Subq can only be used to mean fun X Y:set => forall x:set, x :e X -> x :e Y");
    }
    if (i > 0u)
        pushpolytm(e, x, i, bgtp);
    bool found;
    uint32_t xhv2 = sigtmh_get(e, x, &found);
    if (found && xhv2 != xhv)
        mge_fail(e, "%s was explicitly assigned id %s but this does not match its computed id %s",
             x, mge_name_text(e, xhv2), mge_name_text(e, xhv));
    mge_map_set(e, &e->sigtmh, K(x), xhv);
    PTp *pt = PERM(e, PTp);
    *pt = (PTp){i, bgtp};
    mge_map_set(e, &e->sigtmof, xhv, V(pt));
    if (i == 0u) {
        uint64_t itp;
        if (mge_map_get(&e->indextms, xhv, &itp)) {
            if (P(MgTp, itp) != bgtp)
                mge_fail(e, "The id %s associated with the definition of %s has is indexed to have the type %s not %s. This indicates some fundamental bug since it would presumably imply a hash collision.",
                     mge_name_text(e, xhv), x, mge_tp_str(e, P(MgTp, itp)), mge_tp_str(e, bgtp));
        } else {
            mge_map_set(e, &e->indextms, xhv, V(bgtp));
        }
    }
    declare_tm(e, x, xhv);
}

void mge_activate_special_knowns(MgElab *e, uint32_t h) {
    if (h == e->expolyI)
        e->expolyIknown = true;
}

static void eval_axiom(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    if (mge_used(e, x))
        mge_fail(e, "%s has already been used.", x);
    uint32_t i = e->ctxtp_len;
    MgTm *atm = check_tm(e, it->a, mge_ty_prop(e));
    MgTm *agtm = tmallclos(e, atm);
    uint32_t ahv = identity(e, agtm, i, false);
    mge_row_begin(e);
    mg_buf_printf(&e->row, "(AXIOM \"%s\" \"%s\" %u ", x, mge_name_text(e, ahv), i);
    mge_tm_row(e, agtm);
    mg_buf_printf(&e->row, " %d)\n", e->lineno);
    mge_row_out(e);
    if (e->admission &&
        !mg_admit_axiom(e->admission, x, mge_name_text(e, ahv), i, mge_poly_term(e, agtm, i, false),
                        (uint64_t)e->lineno))
        mge_kernel_refused(e);
    add_sigdelta(e, ahv, i, agtm);
    mge_map_set(e, &e->sigknh, K(x), ahv);
    mge_activate_special_knowns(e, ahv);
    mge_megaauto_known(e, ahv);
    mge_sigpf_add(e, x, ahv, e->appf);
    if (i > 0u)
        mge_pushpolypf(e, x, i, agtm);
    if (i == 0u) {
        MgHash ignored;
        pfg_propid(e, agtm, &ignored);
    }
    mge_map_set(e, &e->indexknowns, ahv, 1u);
    mge_carry_pf(e, x, ahv);
}

static void eval_theorem(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    reserved_setmm(e, x);
    if (mge_used(e, x))
        mge_fail(e, "%s has already been used.", x);
    if (e->signature)
        mge_fail(e, "Included signature file includes a theorem (%s), but should only include axioms.", x);
    uint32_t i = e->ctxtp_len;
    MgTm *atm = check_tm(e, it->a, mge_ty_prop(e));
    MgTm *agtm = tmallclos(e, atm);
    uint32_t ahv = identity(e, agtm, i, false);
    char pfgid[65] = "none";
    if (i == 0u) {
        MgHash propid;
        pfg_propid(e, agtm, &propid);
        mg_hash_hex(&propid, pfgid);
    }
    mge_map_set(e, &e->sigknh, K(x), ahv);
    add_sigdelta(e, ahv, i, agtm);
    mge_row_begin(e);
    mg_buf_printf(&e->row, "(THM \"%s\" \"%s\" \"%s\" %u ", x, mge_name_text(e, ahv), pfgid, i);
    mge_tm_row(e, agtm);
    mg_buf_printf(&e->row, " %d)\n", e->lineno);
    mge_row_out(e);
    if (e->admission &&
        !mg_admit_theorem(e->admission, x, mge_name_text(e, ahv), i, mge_poly_term(e, agtm, i, false),
                          (uint64_t)e->lineno))
        mge_kernel_refused(e);
    e->proving = true;
    e->thm_name = x;
    e->thm_levels = i;
    e->thm_prop = agtm;
    e->thm_hash = ahv;
    mge_proof_begin(e, atm);
}

static void eval_var(MgElab *e, const MgSourceItem *it) {
    if (it->asc_kind == MG_ASC_TP && it->a->kind == MG_L_NAME && strcmp(it->a->text, "SType") == 0) {
        if (e->ctxtp_len || e->ctxtm || e->ctxpf)
            mge_fail(e, "Type variables can only be declared when the context is empty.");
        if (it->names_len > e->ctxtp_cap) {
            const char **g = realloc(e->ctxtp, it->names_len * sizeof(char *));
            if (!g)
                mge_nomem(e);
            e->ctxtp = g;
            e->ctxtp_cap = it->names_len;
        }
        for (uint32_t k = 0; k < it->names_len; ++k)
            e->ctxtp[k] = mge_intern(e, it->names[k]);
        e->ctxtp_len = it->names_len;
        if (e->ctxtp_len > 6u)
            mge_fail(e, "More than 6 type variables are not allowed.");
        for (uint32_t k = 0; k < it->names_len; ++k) {
            e->aptm = mge_app_push(e, e->aptm, APP_TP, e->ctxtp[k]);
            e->appf = mge_app_push(e, e->appf, APP_TP, e->ctxtp[k]);
        }
        undo_push(e, (Undo){.kind = U_TPVARS});
        return;
    }
    if (it->asc_kind != MG_ASC_TP)
        mge_fail(e, "Variables must be ascribed a type, not a set");
    MgTp *atp = mge_extract_tp(e, it->a);
    mge_undo_ctx(e);
    uint32_t n = it->names_len;
    const Cx *cx = e->ctxtm;
    for (uint32_t k = 0; k < n; ++k)
        cx = mge_cx_push(e, cx, mge_intern(e, it->names[k]), atp, NULL, true);
    /* the hypotheses move under the new variables */
    const Hy **old = mge_chunk_alloc(e, &e->tmp, sizeof(Hy *) * 64u);
    size_t count = 0, cap = 64u;
    for (const Hy *h = e->ctxpf; h; h = h->next) {
        if (count == cap) {
            const Hy **g = mge_chunk_alloc(e, &e->tmp, sizeof(Hy *) * cap * 2u);
            memcpy(g, old, sizeof(Hy *) * cap);
            old = g;
            cap *= 2u;
        }
        old[count++] = h;
    }
    const Hy *pf = NULL;
    for (size_t k = count; k-- > 0;) {
        Hy *h = PERM(e, Hy);
        mge_hy_set(h, old[k]->name, mge_tmshift(e, 0u, (int32_t)n, old[k]->prop), pf);
        pf = h;
    }
    e->ctxtm = cx;
    e->ctxpf = pf;
    for (uint32_t k = 0; k < n; ++k) {
        const char *name = mge_intern(e, it->names[k]);
        e->aptm = mge_app_push(e, e->aptm, APP_TM, name);
        e->appf = mge_app_push(e, e->appf, APP_TM, name);
    }
    Clo *c = PERM(e, Clo);
    *c = (Clo){false, atp, n, NULL, e->clos};
    e->clos = c;
}

static void eval_hyp(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    MgTm *atm = check_tm(e, it->a, mge_ty_prop(e));
    mge_undo_ctx(e);
    Hy *h = PERM(e, Hy);
    mge_hy_set(h, x, atm, e->ctxpf);
    e->ctxpf = h;
    e->appf = mge_app_push(e, e->appf, APP_HYP, x);
    Clo *c = PERM(e, Clo);
    *c = (Clo){true, NULL, 0u, atm, e->clos};
    e->clos = c;
}

static void eval_let(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    if (!it->asc) {
        TmTp b = extract_tm(e, it->a);
        mge_undo_ctx(e);
        e->ctxtm = mge_cx_push(e, e->ctxtm, x, b.tp, b.tm, true);
        return;
    }
    if (it->asc->kind == MG_ASC_TP && it->asc->type->kind == MG_L_NAME &&
        strcmp(it->asc->type->text, "Type") == 0) {
        MgTp *btp = mge_extract_tp(e, it->a);
        mge_map_set(e, &e->tpabbrev, K(x), V(btp));
        return;
    }
    if (it->asc->kind == MG_ASC_TP) {
        MgTp *btp = mge_extract_tp(e, it->asc->type);
        MgTm *btm = check_tm(e, it->a, btp);
        mge_undo_ctx(e);
        e->ctxtm = mge_cx_push(e, e->ctxtm, x, btp, btm, true);
        return;
    }
    mge_fail(e, "Lets must be ascribed a type, not a set");
}

/* NotationDecl: the identity of a name, of the expected type. */
static bool notation_name(MgElab *e, const char *y, uint32_t levels, MgTp *tp, uint32_t *h) {
    bool found;
    uint32_t id = sigtmh_get(e, mge_intern(e, y), &found);
    if (!found)
        return false;
    uint32_t l;
    MgTp *t;
    if (!mge_sigtmof_get(e, id, &l, &t))
        return false;
    *h = id;
    return l == levels && t == tp;
}

static void set_setenuml_n(MgElab *e, size_t i, uint32_t h) {
    if (i >= e->setenuml_cap) {
        size_t cap = i + 8u;
        uint32_t *g = realloc(e->setenuml, cap * sizeof(uint32_t));
        if (!g)
            mge_nomem(e);
        e->setenuml = g;
        e->setenuml_cap = cap;
    }
    while (e->setenuml_len <= i)
        e->setenuml[e->setenuml_len++] = NONE;
    e->setenuml[i] = h;
}

static void eval_notation(MgElab *e, const MgSourceItem *it) {
    const char *x = it->name;
    MgTp *set = mge_ty_set(e), *prop = mge_ty_prop(e), *t0 = mge_ty_var(e, 0u);
    MgTp *ss = mge_ty_arr(e, set, set);
    uint32_t h;
    const char *y = it->names_len == 1u ? it->names[0] : NULL;
#define NOTE(key, levels, type, target, message)                                   \
    if (strcmp(x, key) == 0) {                                                       \
        if (!y || !notation_name(e, y, levels, type, &h))                            \
            mge_fail(e, "%s", message);                                                  \
        target = h;                                                                  \
        return;                                                                      \
    }
    if (strcmp(x, "IfThenElse") == 0) {
        static const char msg[] = "IfThenElse Notation should be given a polymorphic name of type prop->?0->?0->?0 or prop->set->set->set";
        if (y && notation_name(e, y, 1u, mge_ty_arr(e, prop, mge_ty_arr(e, t0, mge_ty_arr(e, t0, t0))), &h)) {
            e->ifop = h;
            return;
        }
        if (y && notation_name(e, y, 0u, mge_ty_arr(e, prop, mge_ty_arr(e, set, mge_ty_arr(e, set, set))), &h)) {
            e->ifopset = h;
            return;
        }
        mge_fail(e, "%s", msg);
    }
    NOTE("Repl", 0u, mge_ty_arr(e, set, mge_ty_arr(e, ss, set)), e->replop,
         "Repl Notation should be given a parameter or definition name of type set->(set->set)->set")
    NOTE("Sep", 0u, mge_ty_arr(e, set, mge_ty_arr(e, mge_ty_arr(e, set, prop), set)), e->sepop,
         "Repl Notation should be given a parameter or definition name of type set->(set->set)->set")
    NOTE("ReplSep", 0u, mge_ty_arr(e, set, mge_ty_arr(e, mge_ty_arr(e, set, prop), mge_ty_arr(e, ss, set))), e->replsepop,
         "ReplSep Notation should be given a parameter or definition name of type set->(set->prop)->(set->set)->set")
    if (strcmp(x, "SetEnum") == 0) {
        if (it->names_len <= 1u)
            mge_fail(e, "SetEnum should be given at least n>=2 names: constructors for the first n-1 cases and finally constructor for adjoining to a set");
        e->setenuml_len = 0u;
        MgTp *ztp = set;
        uint32_t *built = mge_chunk_alloc(e, &e->tmp, sizeof(uint32_t) * it->names_len);
        size_t n = 0;
        for (uint32_t k = 0; k < it->names_len; ++k) {
            const char *yk = it->names[k];
            if (k + 1u == it->names_len) {
                if (!notation_name(e, yk, 0u, mge_ty_arr(e, set, mge_ty_arr(e, set, set)), &h))
                    mge_fail(e, "The last name in the SetEnum Notation should be given a parameter or definition name of type set->set->set");
                e->setenumadj = h;
                break;
            }
            bool found;
            uint32_t id = sigtmh_get(e, mge_intern(e, yk), &found);
            uint32_t l;
            MgTp *t;
            if (!found || !mge_sigtmof_get(e, id, &l, &t) || l != 0u)
                mge_fail(e, "Name %u in the SetEnum Notation should be given a parameter or definition name of type %s", k, mge_tp_str(e, ztp));
            if (t != ztp) {
                printf("y = %s h = %s\nhtp = %s\nztp = %s\n", yk, mge_name_text(e, id), mge_tp_str(e, t), mge_tp_str(e, ztp));
                fflush(stdout);
                mge_fail(e, "Name %u in the SetEnum Notation should be given a parameter or definition name of type %s", k, mge_tp_str(e, ztp));
            }
            built[n++] = id;
            ztp = mge_ty_arr(e, set, ztp);
        }
        for (size_t k = 0; k < n; ++k)
            set_setenuml_n(e, k, built[k]);
        return;
    }
    if (strcmp(x, "SetEnum0") == 0 || strcmp(x, "SetEnum1") == 0 || strcmp(x, "SetEnum2") == 0) {
        static const char *const msgs[] = {
            "SetEnum0 Notation should be given a parameter or definition name of type set",
            "SetEnum1 Notation should be given a parameter or definition name of type set->set",
            "SetEnum2 Notation should be given a parameter or definition name of type set->set->set",
        };
        int n = x[7] - '0';
        MgTp *tp = n == 0 ? set : n == 1 ? ss : mge_ty_arr(e, set, ss);
        if (!y || !notation_name(e, y, 0u, tp, &h))
            mge_fail(e, "%s", msgs[n]);
        set_setenuml_n(e, (size_t)n, h);
        return;
    }
    if (strcmp(x, "Nat") == 0) {
        static const char msg[] = "Nat Notation should be given a name of type set (for 0) and a name of type set->set (for successor)";
        uint32_t h0, hS;
        if (it->names_len != 2u)
            mge_fail(e, "%s", msg);
        bool f0, fS;
        h0 = sigtmh_get(e, mge_intern(e, it->names[0]), &f0);
        hS = sigtmh_get(e, mge_intern(e, it->names[1]), &fS);
        uint32_t l0, lS;
        MgTp *t0p, *tSp;
        if (!f0 || !fS || !mge_sigtmof_get(e, h0, &l0, &t0p) || !mge_sigtmof_get(e, hS, &lS, &tSp) ||
            l0 != 0u || t0p != set || lS != 0u || tSp != ss)
            mge_fail(e, "%s", msg);
        e->nat0 = h0;
        e->natS = hS;
        return;
    }
    NOTE("SetLam", 0u, mge_ty_arr(e, set, mge_ty_arr(e, ss, set)), e->setlam,
         "SetLam Notation should be given a name of type set->(set->set)->set")
    NOTE("SetImplicitOp", 0u, mge_ty_arr(e, set, ss), e->setimplop,
         "SetAp Notation should be given a name of type set->set->set")
#undef NOTE
    mge_fail(e, "Unknown Notation %s", x);
}

static void eval_paramhash(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    reserved_setmm(e, x);
    const MgPrimSpec *prim = primname(e, x);
    if (prim) {
        uint32_t xh = identity(e, mge_tm_prim(e, prim->index), prim->type_variables, true);
        if (strcmp(it->name2, mge_name_text(e, xh)) != 0)
            mge_fail(e, "%s is the name of a built-in primitive and must have the id %s", x, mge_name_text(e, xh));
        return;
    }
    if (mge_map_get(&e->sigtmh, K(x), NULL))
        mge_fail(e, "%s is already assigned an id", x);
    uint32_t h = mge_name_id(e, it->name2);
    mge_map_set(e, &e->sigtmh, K(x), h);
    if (it->name3) {
        MgHash pure;
        memset(&pure, 0, sizeof pure);
        mg_hash_parse(it->name3, strlen(it->name3), &pure);
        set_pure(e, h, &pure);
    }
}

static void make_opaque(MgElab *e, const char *x, bool opaque) {
    bool found;
    uint32_t xh = sigtmh_get(e, mge_intern(e, x), &found);
    Map *from = opaque ? &e->sigdelta : &e->sigdelta_opaque;
    Map *to = opaque ? &e->sigdelta_opaque : &e->sigdelta;
    uint64_t v;
    if (found && mge_map_get(from, xh, &v) && v) {
        mge_map_set(e, to, xh, v);
        mge_map_set(e, from, xh, 0u);
        return;
    }
    mge_row_begin(e);
    mg_buf_printf(&e->row, opaque ? "WARNING: %s was not transparent, so not made opaque.\n"
                                  : "WARNING: %s was not opaque, so not made transparent.\n", x);
    mge_row_out(e);
}

static void eval_section_end(MgElab *e, const MgSourceItem *it) {
    const char *x = mge_intern(e, it->name);
    if (!e->frame_len)
        mge_fail(e, "No Section To End");
    Frame *f = &e->frames[e->frame_len - 1u];
    if (f->name != x)
        mge_fail(e, "Section %s cannot be ended with End %s", f->name, x);
    run_undo(e, f->undo_mark);
    e->aptm = f->aptm;
    e->appf = f->appf;
    Frame done = *f;
    --e->frame_len;
    /* sigtm := st and sigpf := sp: what the section declared, applied to
     * the variables still in scope */
    for (size_t k = 0; k < done.tm_len; ++k)
        sigtm_add(e, done.tm[k].name, done.tm[k].m, done.aptm);
    for (size_t k = 0; k < done.pf_len; ++k)
        mge_sigpf_add(e, done.pf[k].name, done.pf[k].known, done.appf);
    free(done.tm);
    free(done.pf);
}

static void eval_docitem(MgElab *e, const MgSourceItem *it) {
    switch ((MgDocKind)it->kind) {
    case MG_DOC_AUTHOR:
    case MG_DOC_SALT:
    case MG_DOC_TREASURE:
    case MG_DOC_SHOWPROOFTERMS:
    case MG_DOC_UNICODE:
    case MG_DOC_SUBSCRIPT:
    case MG_DOC_SUPERSCRIPT:
        return;
    case MG_DOC_TITLE:
        if (e->title_set)
            mge_fail(e, "Title can only be declared once");
        e->title_set = true;
        return;
    case MG_DOC_OPAQUE:
    case MG_DOC_TRANSPARENT:
        for (uint32_t k = 0; k < it->names_len; ++k)
            make_opaque(e, it->names[k], it->kind == MG_DOC_OPAQUE);
        return;
    case MG_DOC_SECTION: {
        if (e->frame_len == e->frame_cap) {
            size_t cap = e->frame_cap ? e->frame_cap * 2u : 8u;
            Frame *g = realloc(e->frames, cap * sizeof(Frame));
            if (!g)
                mge_nomem(e);
            e->frames = g;
            e->frame_cap = cap;
        }
        e->frames[e->frame_len++] = (Frame){.name = mge_intern(e, it->name), .undo_mark = e->undo_len,
                                            .aptm = e->aptm, .appf = e->appf};
        return;
    }
    case MG_DOC_END:
        eval_section_end(e, it);
        return;
    case MG_DOC_VAR:
        eval_var(e, it);
        return;
    case MG_DOC_LET:
        eval_let(e, it);
        return;
    case MG_DOC_HYP:
        eval_hyp(e, it);
        return;
    case MG_DOC_POSTINFIX:
        scoped_push(e, it->picase == MG_PIC_POSTFIX ? &e->postfixsem : &e->infixsem,
                    mge_intern(e, it->name), V(copy_l(e, it->a)));
        return;
    case MG_DOC_PREFIX:
        scoped_push(e, &e->prefixsem, mge_intern(e, it->name), V(copy_l(e, it->a)));
        return;
    case MG_DOC_BINDER: {
        BinderSem *bs = PERM(e, BinderSem);
        bs->plus = it->flag;
        bs->a = copy_l(e, it->a);
        bs->b = copy_l(e, it->b);
        scoped_push(e, &e->bindersem, mge_intern(e, it->name), V(bs));
        return;
    }
    case MG_DOC_NOTATION:
        eval_notation(e, it);
        return;
    case MG_DOC_PARAM:
        eval_param(e, it);
        return;
    case MG_DOC_DEF:
        eval_def(e, it);
        return;
    case MG_DOC_PARAMHASH:
        eval_paramhash(e, it);
        return;
    case MG_DOC_AXIOM:
        eval_axiom(e, it);
        return;
    case MG_DOC_THEOREM:
        eval_theorem(e, it);
        return;
    }
}

/* ── the evaluator ───────────────────────────────────────────────────── */

static void preset(MgElab *e) {
    MgUpstreamPreset presets[2];
    uint32_t n = mg_upstream_presets(e->theory, presets);
    for (uint32_t k = 0; k < n; ++k) {
        if (presets[k].clears) {
            mge_map_clear(&e->indextms);
            mge_map_clear(&e->indexknowns);
        }
        for (uint32_t r = 0; r < presets[k].tm_count; ++r)
            mge_map_set(e, &e->indextms, mge_name_id(e, presets[k].tms[r].hash),
                    V(mge_ckt(e, mg_front_parse_tp(e->s, presets[k].tms[r].type))));
        for (uint32_t r = 0; r < presets[k].known_count; ++r)
            mge_map_set(e, &e->indexknowns, mge_name_id(e, presets[k].knowns[r]), 1u);
    }
    /* the names megalodon.ml computes before reading anything */
    MgTp *prop = mge_ty_prop(e), *t0 = mge_ty_var(e, 0u);
    e->fal = identity(e, mge_tm_all(e, prop, mge_tm_db(e, 0u)), 0u, true);
    e->fale = identity(e, mge_tm_imp(e, mge_tm_h(e, e->fal), mge_tm_all(e, prop, mge_tm_db(e, 0u))), 0u, false);
    MgTp *rel = mge_ty_arr(e, t0, mge_ty_arr(e, t0, prop));
    MgTm *eqb = mge_tm_lam(e, t0, mge_tm_lam(e, t0, mge_tm_all(e, rel,
        mge_tm_imp(e, mge_tm_ap(e, mge_tm_ap(e, mge_tm_db(e, 0u), mge_tm_db(e, 2u)), mge_tm_db(e, 1u)),
               mge_tm_ap(e, mge_tm_ap(e, mge_tm_db(e, 0u), mge_tm_db(e, 1u)), mge_tm_db(e, 2u))))));
    e->eqPoly = identity(e, eqb, 1u, true);
    e->conj = identity(e, mge_tm_lam(e, prop, mge_tm_lam(e, prop, mge_tm_all(e, prop,
        mge_tm_imp(e, mge_tm_imp(e, mge_tm_db(e, 2u), mge_tm_imp(e, mge_tm_db(e, 1u), mge_tm_db(e, 0u))), mge_tm_db(e, 0u))))), 0u, true);
    e->disj = identity(e, mge_tm_lam(e, prop, mge_tm_lam(e, prop, mge_tm_all(e, prop,
        mge_tm_imp(e, mge_tm_imp(e, mge_tm_db(e, 2u), mge_tm_db(e, 0u)),
               mge_tm_imp(e, mge_tm_imp(e, mge_tm_db(e, 1u), mge_tm_db(e, 0u)), mge_tm_db(e, 0u)))))), 0u, true);
    MgTp *pred = mge_ty_arr(e, t0, prop);
    MgTm *exb = mge_tm_lam(e, pred, mge_tm_all(e, prop, mge_tm_imp(e, mge_tm_all(e, t0,
        mge_tm_imp(e, mge_tm_ap(e, mge_tm_db(e, 2u), mge_tm_db(e, 0u)), mge_tm_db(e, 1u))), mge_tm_db(e, 0u))));
    e->expoly = identity(e, exb, 1u, true);
    MgTm *exI = mge_tm_all(e, pred, mge_tm_all(e, t0, mge_tm_imp(e, mge_tm_ap(e, mge_tm_db(e, 1u), mge_tm_db(e, 0u)),
        mge_tm_ap(e, mge_tm_tap(e, mge_tm_h(e, e->expoly), t0), mge_tm_db(e, 1u)))));
    e->expolyI = identity(e, exI, 1u, false);
}

static void preset_body(MgElab *e, void *ctx) {
    (void)ctx;
    preset(e);
}

MgElab *mg_elab_new(const MgElabOptions *options) {
    MgElab *e = calloc(1u, sizeof *e);
    if (!e)
        return NULL;
    e->theory = options ? options->theory : MG_PROFILE_EGAL;
    e->rows = options ? options->rows : NULL;
    MgStatus st;
    e->t = mg_theory_new(e->theory, "megalodon-document", &st);
    if (!e->t) {
        free(e);
        return NULL;
    }
    e->s = &e->t->store;
    mg_budget_init(&e->budget, 0);
    uint32_t *refs[] = {&e->setIn, &e->setSubeq, &e->setPow, &e->setimplop, &e->setlam, &e->replop,
                        &e->sepop, &e->replsepop, &e->ifop, &e->ifopset, &e->setenumadj, &e->nat0,
                        &e->natS, &e->seqcons, &e->set0,
                        &e->ma_setordsucc, &e->ma_set1, &e->ma_set2, &e->ma_setsum, &e->ma_setlambda,
                        &e->ma_setprod, &e->ma_setap, &e->ma_setPi, &e->ma_setexp, &e->ma_In_0_2,
                        &e->ma_In_1_2, &e->ma_tupleI0, &e->ma_tupleI1, &e->ma_tuple0_setsum,
                        &e->ma_tuple1_setsum, &e->ma_tuple_2_0_eq, &e->ma_tuple_2_1_eq,
                        &e->ma_tuple_2_Sigma, &e->ma_tuple_2_setprod, &e->ma_tuple_2_eta,
                        &e->ma_ap0_Sigma, &e->ma_ap1_Sigma, &e->ma_tuple_Sigma_eta, &e->ma_lam_ext,
                        &e->ma_lam_Pi, &e->ma_ap_Pi, &e->ma_beta, &e->ma_Pi_ext, &e->ma_Pi_eta,
                        &e->ma_Pi_cod_ext};
    for (size_t k = 0; k < sizeof refs / sizeof refs[0]; ++k)
        *refs[k] = NONE;
    if (mge_try_run(e, preset_body, NULL) != 0) {
        mg_elab_free(e);
        return NULL;
    }
    if (options && options->kernel) {
        e->policy = (MgImportPolicy){.all_axioms = true};
        e->admission = mg_admission_new(e->t, "megalodon-document", &e->policy, &e->budget, &e->report);
        if (!e->admission) {
            mg_elab_free(e);
            return NULL;
        }
        if (options->ledger)
            mg_admission_record(e->admission, options->ledger);
        mg_admission_budget_theorems(e->admission, options->theorem_work);
    }
    if (options) {
        e->prove_aby = options->prove_aby;
        e->aby_max_steps = options->aby_max_steps;
        e->aby_max_depth = options->aby_max_depth;
        e->conv_memory_off = options->reference_conversion;
    }
    return e;
}

void mg_elab_aby_counts(const MgElab *e, uint64_t *tried, uint64_t *proved) {
    *tried = e->aby_tried;
    *proved = e->aby_proved;
}

uint64_t mg_elab_conversion_steps(const MgElab *e) { return e->conv_steps; }

void mg_elab_free(MgElab *e) {
    if (!e)
        return;
    if (e->admission && !e->finished)
        mg_admission_finish(e->admission);
    Map *maps[] = {&e->strs, &e->sigtm, &e->sigpf, &e->sigtmh, &e->sigknh, &e->sigtmof,
                   &e->sigdelta, &e->sigdelta_opaque, &e->indextms, &e->indexknowns, &e->pfgtmhh,
                   &e->tpabbrev, &e->infixsem, &e->prefixsem, &e->postfixsem, &e->bindersem,
                   &e->metavarcx, &e->metavarassoc};
    for (size_t k = 0; k < sizeof maps / sizeof maps[0]; ++k)
        map_free(maps[k]);
    for (int k = 0; k < 7; ++k) {
        map_free(&e->polytm[k]);
        map_free(&e->polypf[k]);
    }
    for (size_t k = 0; k < e->frame_len; ++k) {
        free(e->frames[k].tm);
        free(e->frames[k].pf);
    }
    free(e->frames);
    free(e->undo);
    free(e->refuted);
    free(e->aby_pool);
    free(e->aby_runs);
    free(e->future);
    free(e->future_pf);
    free(e->ctxtp);
    free(e->setenuml);
    mg_buf_free(&e->err);
    mg_buf_free(&e->msg);
    mg_buf_free(&e->row);
    chunks_free(&e->perm);
    chunks_free(&e->tmp);
    mg_theory_release(e->t);
    free(e);
}

void mg_elab_begin_file(MgElab *e, bool signature) {
    /* init_env: the local context and the sections start empty; what open
     * sections declared stays, as in Megalodon */
    e->ctxtp_len = 0u;
    e->ctxtm = NULL;
    e->ctxpf = NULL;
    e->clos = NULL;
    e->aptm = NULL;
    e->appf = NULL;
    for (size_t k = 0; k < e->frame_len; ++k) {
        free(e->frames[k].tm);
        free(e->frames[k].pf);
    }
    e->frame_len = 0u;
    e->undo_len = 0u;
    if (e->signature)
        e->title_set = false;
    e->signature = signature;
    if (e->admission)
        mg_admission_set_origin(e->admission, signature ? MG_ORIGIN_SIGNATURE : MG_ORIGIN_DOCUMENT);
}

static void item_body(MgElab *e, void *ctx) {
    const MgSourceItem *item = ctx;
    if (item->tactic)
        mge_eval_pftac(e, item);
    else
        eval_docitem(e, item);
}

int mg_elab_item(MgElab *e, const MgSourceItem *item) {
    e->lineno = item->end_line;
    e->charno = item->end_char;
    mge_conv_forget(e);
    int k = mge_try_run(e, item_body, (void *)item);
    chunks_free(&e->tmp);
    if (k == 0)
        return 0;
    mg_buf_reset(&e->err);
    if (k == RAISE_NOT_FOUND) {
        mg_buf_puts(&e->err, "Fatal error: exception Not_found");
        mg_buf_text(&e->err);
        return -2;
    }
    if (k == RAISE_FATAL) {
        mg_buf_puts(&e->err, mg_buf_text(&e->msg));
        mg_buf_text(&e->err);
        return -2;
    }
    if (k == RAISE_NEGDB) {
        mg_buf_puts(&e->err, "Fatal error: exception Syntax.NegDB");
        mg_buf_text(&e->err);
        return -2;
    }
    if (k == RAISE_KERNEL) {
        mg_buf_printf(&e->err, "Kernel refused at line %d: %s", e->lineno, mg_buf_text(&e->msg));
        mg_buf_text(&e->err);
        return -3;
    }
    mg_buf_printf(&e->err, "Failure at line %d char %d: %s", e->lineno, e->charno, mg_buf_text(&e->msg));
    mg_buf_text(&e->err);
    return -1;
}

const char *mg_elab_error(const MgElab *e) {
    return e->err.data ? e->err.data : "";
}

uint64_t mg_elab_unchecked_tactics(const MgElab *e) { return e->unchecked; }

MgTheory *mg_elab_theory(MgElab *e) { return e->t; }

MgStatus mg_elab_finish(MgElab *e, char *summary, size_t cap) {
    if (!e->admission || e->finished) {
        if (cap)
            summary[0] = '\0';
        return MG_OK;
    }
    e->finished = true;
    MgStatus st = mg_admission_finish(e->admission);
    e->admission = NULL;
    const MgImportReport *r = &e->report;
    snprintf(summary, cap,
             "(MegalodonKernel %s prims=%llu params=%llu defs=%llu axioms=%llu thms=%llu "
             "unproved=%llu repeated=%llu realized=%llu identities=%llu unchecked_tactics=%llu)",
             st == MG_OK ? "ok" : mg_status_name(st),
             (unsigned long long)r->primitives, (unsigned long long)r->parameters,
             (unsigned long long)r->definitions, (unsigned long long)r->axioms,
             (unsigned long long)r->theorems, (unsigned long long)r->unproved,
             (unsigned long long)r->repeated, (unsigned long long)r->realized,
             (unsigned long long)r->identities, (unsigned long long)e->unchecked);
    return st;
}
