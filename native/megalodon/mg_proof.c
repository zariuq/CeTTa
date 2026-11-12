#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_aby.h"
#include "mg_elab_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mg_hash.h"
#include "mg_upstream.h"

/*
 * Megalodon's proofs (megalodon.ml evaluate_pftac, interpret.ml
 * extract_pf_r / check_pf_r, syntax.ml conversion, extr_propofpf and the
 * metavariable terms of apply and rewrite), ported case for case.
 *
 * A proof script builds a proof function: each tactic wraps the previous
 * one (megalodon.ml's prooffun), and Qed calls it on no proofs.  Here the
 * wrappers are frames on a stack, applied newest first.  The proof is then
 * checked as Megalodon double-checks it, which prints the knowns it uses
 * (USESKNOWN) and the definitions it unfolds (DELTA), and is printed and
 * admitted by the kernel.
 */

static const char HEX_EQ[] = "5a6af35fb6d6bea477dd0f822b8e01ca0d57cc50dfd41744307bc94597fdaa4a";

/* ── proof terms ──────────────────────────────────────────────────────── */

static MgPf *pck(MgElab *e, MgPf *p) {
    if (!p)
        mge_nomem(e);
    return p;
}

static MgPf *pf_hyp(MgElab *e, uint32_t i) { return pck(e, mg_pf_hyp(e->s, i)); }
static MgPf *pf_known(MgElab *e, uint32_t h) { return pck(e, mg_pf_known(e->s, h)); }
static MgPf *pf_tpap(MgElab *e, MgPf *p, MgTp *a) { return pck(e, mg_pf_tpap(e->s, p, a)); }
static MgPf *pf_tmap(MgElab *e, MgPf *p, MgTm *m) { return pck(e, mg_pf_tmap(e->s, p, m)); }
static MgPf *pf_pfap(MgElab *e, MgPf *p, MgPf *q) { return pck(e, mg_pf_pfap(e->s, p, q)); }
static MgPf *pf_plam(MgElab *e, MgTm *t, MgPf *p) { return pck(e, mg_pf_plam(e->s, t, p)); }
static MgPf *pf_tlam(MgElab *e, MgTp *a, MgPf *p) { return pck(e, mg_pf_tlam(e->s, a, p)); }

/* syntax.ml tmsubst m j n */
static MgTm *tmsubst(MgElab *e, MgTm *m, uint32_t j, MgTm *n) {
    if (m->fv <= j)
        return m;
    switch (m->tag) {
    case MG_TM_DB:
        if (m->n == j)
            return j == 0 ? n : mge_tmshift(e, 0u, (int32_t)j, n);
        return m->n > j ? mge_tm_db(e, m->n - 1u) : m;
    case MG_TM_TAP:
        return mge_tm_tap(e, tmsubst(e, m->f, j, n), m->tp);
    case MG_TM_AP: {
        MgTm *x = tmsubst(e, m->x, j, n);
        return mge_tm_ap(e, tmsubst(e, m->f, j, n), x);
    }
    case MG_TM_IMP: {
        MgTm *x = tmsubst(e, m->x, j, n);
        return mge_tm_imp(e, tmsubst(e, m->f, j, n), x);
    }
    case MG_TM_LAM:
        return mge_tm_lam(e, m->tp, tmsubst(e, m->f, j + 1u, n));
    case MG_TM_ALL:
        return mge_tm_all(e, m->tp, tmsubst(e, m->f, j + 1u, n));
    default:
        return m;
    }
}

/* syntax.ml tmtpsubst m cl */
static MgTm *tmtpsubst(MgElab *e, MgTm *m, MgTp *const *cl, uint32_t n) {
    if (!m->ftv)
        return m;
    switch (m->tag) {
    case MG_TM_TAP: {
        MgTp *a = mge_tpsubst(e, m->tp, cl, n);
        return mge_tm_tap(e, tmtpsubst(e, m->f, cl, n), a);
    }
    case MG_TM_AP: {
        MgTm *x = tmtpsubst(e, m->x, cl, n);
        return mge_tm_ap(e, tmtpsubst(e, m->f, cl, n), x);
    }
    case MG_TM_IMP: {
        MgTm *x = tmtpsubst(e, m->x, cl, n);
        return mge_tm_imp(e, tmtpsubst(e, m->f, cl, n), x);
    }
    case MG_TM_LAM: {
        MgTm *b = tmtpsubst(e, m->f, cl, n);
        return mge_tm_lam(e, mge_tpsubst(e, m->tp, cl, n), b);
    }
    case MG_TM_ALL: {
        MgTm *b = tmtpsubst(e, m->f, cl, n);
        return mge_tm_all(e, mge_tpsubst(e, m->tp, cl, n), b);
    }
    default:
        return m;
    }
}

/* syntax.ml pfshift i j d: hypothesis indices */
static MgPf *pfshift(MgElab *e, uint32_t i, int32_t j, MgPf *d) {
    switch (d->tag) {
    case MG_PF_HYP: {
        if (d->n < i)
            return d;
        int64_t l = (int64_t)d->n + j;
        if (l < (int64_t)i)
            mge_fail(e, "NegDB");
        return pf_hyp(e, (uint32_t)l);
    }
    case MG_PF_TPAP: return pf_tpap(e, pfshift(e, i, j, d->p), d->tp);
    case MG_PF_TMAP: return pf_tmap(e, pfshift(e, i, j, d->p), d->t);
    case MG_PF_PFAP: {
        MgPf *q = pfshift(e, i, j, d->q);
        return pf_pfap(e, pfshift(e, i, j, d->p), q);
    }
    case MG_PF_PLAM: return pf_plam(e, d->t, pfshift(e, i + 1u, j, d->p));
    case MG_PF_TLAM: return pf_tlam(e, d->tp, pfshift(e, i, j, d->p));
    default: return d;
    }
}

/* syntax.ml pftmshift i j d: term indices inside a proof */
static MgPf *pftmshift(MgElab *e, uint32_t i, int32_t j, MgPf *d) {
    switch (d->tag) {
    case MG_PF_TPAP: return pf_tpap(e, pftmshift(e, i, j, d->p), d->tp);
    case MG_PF_TMAP: {
        MgTm *m = mge_tmshift(e, i, j, d->t);
        return pf_tmap(e, pftmshift(e, i, j, d->p), m);
    }
    case MG_PF_PFAP: {
        MgPf *q = pftmshift(e, i, j, d->q);
        return pf_pfap(e, pftmshift(e, i, j, d->p), q);
    }
    case MG_PF_PLAM: {
        MgPf *b = pftmshift(e, i, j, d->p);
        return pf_plam(e, mge_tmshift(e, i, j, d->t), b);
    }
    case MG_PF_TLAM: return pf_tlam(e, d->tp, pftmshift(e, i + 1u, j, d->p));
    default: return d;
    }
}

/* interpret.ml pf_metavar_subst */
static MgPf *pf_metavar_subst(MgElab *e, uint32_t cxn, MgPf *d) {
    if (!e->metavarcx.len)
        return d;
    switch (d->tag) {
    case MG_PF_TPAP: return pf_tpap(e, pf_metavar_subst(e, cxn, d->p), d->tp);
    case MG_PF_TMAP: {
        MgTm *m = mge_metavar_subst(e, cxn, d->t);
        return pf_tmap(e, pf_metavar_subst(e, cxn, d->p), m);
    }
    case MG_PF_PFAP: {
        MgPf *q = pf_metavar_subst(e, cxn, d->q);
        return pf_pfap(e, pf_metavar_subst(e, cxn, d->p), q);
    }
    case MG_PF_PLAM: {
        MgPf *b = pf_metavar_subst(e, cxn, d->p);
        return pf_plam(e, mge_metavar_subst(e, cxn, d->t), b);
    }
    case MG_PF_TLAM: return pf_tlam(e, d->tp, pf_metavar_subst(e, cxn + 1u, d->p));
    default: return d;
    }
}

static MgTm *tmv(MgElab *e, uint32_t cxn, MgTm *m) {
    return e->metavarcx.len ? mge_metavar_subst(e, cxn, m) : m;
}

static const char *pf_str(MgElab *e, const MgPf *d) {
    MgBuf b = {0};
    mg_front_pf_str(&b, e->s, d);
    const char *t = mge_intern(e, mg_buf_text(&b));
    mg_buf_free(&b);
    return t;
}

static const char *tm_str(MgElab *e, const MgTm *m) {
    MgBuf b = {0};
    mg_front_tm_str(&b, e->s, m);
    const char *t = mge_intern(e, mg_buf_text(&b));
    mg_buf_free(&b);
    return t;
}

/* ── the delta set and conversion (syntax.ml 3880-4112) ───────────────── */

struct Dl {
    uint32_t h;
    const Dl *next;
};

static bool dl_mem(const Dl *dl, uint32_t h) {
    for (; dl; dl = dl->next)
        if (dl->h == h)
            return true;
    return false;
}

static const Dl *dl_cons(MgElab *e, uint32_t h, const Dl *dl) {
    Dl *d = PERM(e, Dl);
    d->h = h;
    d->next = dl;
    return d;
}

/* The head of an application spine and its arguments. */
typedef struct {
    MgTm *head;
    MgTm **args;
    uint32_t n;
} Spine;

static Spine spine(MgElab *e, MgTm *m) {
    uint32_t n = 0;
    for (MgTm *h = m; h->tag == MG_TM_AP; h = h->f)
        ++n;
    Spine sp = {m, NULL, n};
    if (n)
        sp.args = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * n);
    for (uint32_t k = n; k-- > 0;) {
        sp.args[k] = sp.head->x;
        sp.head = sp.head->f;
    }
    return sp;
}

/* A head TmH c, possibly applied to 1..6 types: c and the types. */
static bool tap_head(MgTm *h, uint32_t *c, MgTp **types, uint32_t *k) {
    uint32_t n = 0;
    MgTp *rev[7];
    while (h->tag == MG_TM_TAP && n < 7u) {
        rev[n++] = h->tp;
        h = h->f;
    }
    if (h->tag != MG_TM_NAME || n > 6u)
        return false;
    for (uint32_t i = 0; i < n; ++i)
        types[i] = rev[n - 1u - i];
    *c = h->n;
    *k = n;
    return true;
}

static const PTm *delta_of(MgElab *e, uint32_t c) {
    uint64_t v;
    if (!mge_map_get(&e->sigdelta, c, &v) || !v)
        return NULL;
    return P(const PTm, v);
}

static bool defp(MgElab *e, MgTm *h) {
    uint32_t c, k;
    MgTp *types[6];
    return tap_head(h, &c, types, &k) && delta_of(e, c);
}

static bool deltap(const Dl *dl, MgTm *h) {
    uint32_t c, k;
    MgTp *types[6];
    if (!tap_head(h, &c, types, &k))
        return false;
    return k > 0u || dl_mem(dl, c);
}

static MgTm *app_beta_eta_norm(MgElab *e, MgTm *m, MgTm **args, uint32_t n) {
    uint32_t i = 0;
    while (i < n && m->tag == MG_TM_LAM) {
        m = mge_beta_eta_norm(e, tmsubst(e, m->f, 0u, args[i]));
        ++i;
    }
    for (; i < n; ++i)
        m = mge_tm_ap(e, m, args[i]);
    return m;
}

/* syntax.ml delta_exp, its messages and its six-type case included. */
static MgTm *delta_exp(MgElab *e, MgTm *h, MgTm **args, uint32_t n) {
    uint32_t c, k;
    MgTp *types[6];
    const PTm *d = tap_head(h, &c, types, &k) ? delta_of(e, c) : NULL;
    if (!d)
        mge_fail(e, "delta_exp called with an inappropriate head. Bug");
    const char *cs = mge_name_text(e, c);
    uint32_t i = d->levels;
    if (k == 0u) {
        if (i == 0u)
            return app_beta_eta_norm(e, d->m, args, n);
        if (i == 1u)
            mge_fail(e, "%s should be applied to 1 type, but is applied to none.", cs);
        mge_fail(e, "%s should be applied to %u types, but is applied to none.", cs, i);
    }
    if (k == 1u) {
        if (i == 1u)
            return app_beta_eta_norm(e, tmtpsubst(e, d->m, types, 1u), args, n);
        mge_fail(e, "%s should be applied to %u types, but is applied to 1.", cs, i);
    }
    /* megalodon.ml compares six type arguments with 5 */
    if (i == (k == 6u ? 5u : k))
        return app_beta_eta_norm(e, tmtpsubst(e, d->m, types, k), args, n);
    if (i == 1u)
        mge_fail(e, "%s should be applied to 1 type, but is applied to %u.", cs, k);
    mge_fail(e, "%s should be applied to %u types, but is applied to %u.", cs, i, k);
}

static const Dl *delta_cons(MgElab *e, MgTm *h, const Dl *dl) {
    if (h->tag != MG_TM_NAME)
        return dl;
    return dl_mem(dl, h->n) ? dl : dl_cons(e, h->n, dl);
}

static MgTm *headnorm1(MgElab *e, MgTm *m, const Dl **dl) {
    for (;;) {
        if (m->tag == MG_TM_LAM || m->tag == MG_TM_IMP || m->tag == MG_TM_ALL)
            return m;
        Spine sp = spine(e, m);
        if (!defp(e, sp.head))
            return m;
        MgTm *x = delta_exp(e, sp.head, sp.args, sp.n);
        *dl = delta_cons(e, sp.head, *dl);
        m = x;
    }
}

/* syntax.ml headnorm: beta-eta normal form, then the head unfolded. */
static MgTm *headnorm(MgElab *e, MgTm *m, const Dl **dl) {
    return headnorm1(e, mge_beta_eta_norm(e, m), dl);
}

/* What conversion remembers.  Megalodon's syntax.ml remembers nothing; the
 * two shortcuts below give its answers without repeating its work.
 *
 * A refuted pair stays refuted.  conv2 decides whether two beta-eta normal
 * terms are equal up to the definitions in force.  The delta set it is
 * handed only steers which definitions are unfolded first and recorded: it
 * does not change the answer, and a refutation records nothing.  So a pair
 * once refuted is refuted until a definition or its opacity changes
 * (mge_conv_forget), and is answered from the table.
 *
 * A refutation is often asked for more than once.  rewrite asks one
 * conversion for each subterm it passes before its first match.  When two
 * terms differ only below several nested definitions, conv2 compares their
 * arguments, unfolds one side a step and compares again, so the pair at the
 * bottom is refuted anew at every step of every level above it, and the
 * work multiplies with the depth (tests/megalodon/scaling_workloads.metta,
 * the workload "nearly").
 *
 * A term is convertible with itself.  Megalodon unfolds and records nothing
 * to see that; terms are interned here, so it is one comparison.
 *
 * One case could put an answer where Megalodon fails: it cannot unfold a
 * definition with six type variables (delta_exp).  The memory is off once
 * such a definition exists (conv_memory_off). */
struct ConvRefuted {
    MgTm *m, *n;
    uint64_t epoch;
};

static size_t refuted_slot(MgTm *m, MgTm *n, size_t cap) {
    uint64_t h = ((uint64_t)(uintptr_t)m * 0x9E3779B97F4A7C15ull) ^
                 (((uint64_t)(uintptr_t)n * 0xC2B2AE3D27D4EB4Full) >> 7);
    h ^= h >> 29;
    return (size_t)(h & (cap - 1u));
}

/* A slot is free unless it carries a pair of the current epoch. */
static bool refuted_live(const MgElab *e, const struct ConvRefuted *s) {
    return s->m && s->epoch == e->conv_epoch;
}

static bool refuted_p(const MgElab *e, MgTm *m, MgTm *n) {
    if (!e->refuted_len)
        return false;
    for (size_t i = refuted_slot(m, n, e->refuted_cap);; i = (i + 1u) & (e->refuted_cap - 1u)) {
        const struct ConvRefuted *s = &e->refuted[i];
        if (!refuted_live(e, s))
            return false;
        if (s->m == m && s->n == n)
            return true;
    }
}

static void refuted_put(const MgElab *e, struct ConvRefuted *slots, size_t cap, MgTm *m, MgTm *n) {
    for (size_t i = refuted_slot(m, n, cap);; i = (i + 1u) & (cap - 1u)) {
        struct ConvRefuted *s = &slots[i];
        if (!refuted_live(e, s)) {
            *s = (struct ConvRefuted){m, n, e->conv_epoch};
            return;
        }
    }
}

/* Remember a refuted pair.  The table is kept at most half full; when it
 * cannot grow the pair is simply not remembered. */
static void refuted_add(MgElab *e, MgTm *m, MgTm *n) {
    if ((e->refuted_len + 1u) * 2u > e->refuted_cap) {
        size_t cap = e->refuted_cap ? e->refuted_cap * 2u : 256u;
        struct ConvRefuted *slots = calloc(cap, sizeof *slots);
        if (!slots)
            return;
        for (size_t i = 0; i < e->refuted_cap; ++i)
            if (refuted_live(e, &e->refuted[i]))
                refuted_put(e, slots, cap, e->refuted[i].m, e->refuted[i].n);
        free(e->refuted);
        e->refuted = slots;
        e->refuted_cap = cap;
    }
    refuted_put(e, e->refuted, e->refuted_cap, m, n);
    ++e->refuted_len;
}

static bool conv2(MgElab *e, MgTm *m, MgTm *n, const Dl **dl);

static bool convl(MgElab *e, MgTm **ml, MgTm **nl, uint32_t len, const Dl **dl) {
    for (uint32_t i = 0; i < len; ++i)
        if (!conv2(e, ml[i], nl[i], dl))
            return false;
    return true;
}

static bool convrigid2(MgElab *e, Spine m, Spine n, const Dl **dl) {
    if (m.head != n.head || m.n != n.n)
        return m.head == n.head && m.n == n.n;
    return convl(e, m.args, n.args, m.n, dl);
}

static bool convrigid1(MgElab *e, Spine m, MgTm *n, const Dl **dl) {
    for (;;) {
        Spine ns = spine(e, n);
        if (!defp(e, ns.head))
            return convrigid2(e, m, ns, dl);
        if (deltap(*dl, ns.head)) {
            n = delta_exp(e, ns.head, ns.args, ns.n);
            continue;
        }
        const Dl *saved = *dl;
        if (convrigid2(e, m, ns, dl))
            return true;
        *dl = saved;
        MgTm *x = delta_exp(e, ns.head, ns.args, ns.n);
        *dl = delta_cons(e, ns.head, *dl);
        n = x;
    }
}

static bool is_binder_or_imp(const MgTm *m) {
    return m->tag == MG_TM_LAM || m->tag == MG_TM_ALL || m->tag == MG_TM_IMP;
}

/* syntax.ml conv2 on beta-eta normal terms.  On failure *dl is left as it
 * was given. */
static bool conv2(MgElab *e, MgTm *m, MgTm *n, const Dl **dl) {
    const Dl *in = *dl;
    MgTm *m0 = m, *n0 = n;
    bool memory = !e->conv_memory_off;
    ++e->conv_steps;
    if (memory) {
        if (m == n)
            return true;
        if (refuted_p(e, m, n))
            return false;
    }
    for (;;) {
        if (m->tag == MG_TM_LAM && n->tag == MG_TM_LAM) {
            if (m->tp != n->tp)
                goto fail;
            m = m->f;
            n = n->f;
            continue;
        }
        if (m->tag == MG_TM_ALL && n->tag == MG_TM_ALL) {
            if (m->tp != n->tp)
                goto fail;
            m = m->f;
            n = n->f;
            continue;
        }
        if (m->tag == MG_TM_IMP && n->tag == MG_TM_IMP) {
            MgTm *ml[2] = {m->f, m->x}, *nl[2] = {n->f, n->x};
            if (convl(e, ml, nl, 2u, dl))
                return true;
            goto fail;
        }
        if (is_binder_or_imp(m) && is_binder_or_imp(n))
            goto fail; /* different binders */
        if (n->tag == MG_TM_LAM) {
            m = mge_gen_lam_body(e, m);
            n = n->f;
            continue;
        }
        if (m->tag == MG_TM_LAM) {
            m = m->f;
            n = mge_gen_lam_body(e, n);
            continue;
        }
        if (n->tag == MG_TM_ALL || n->tag == MG_TM_IMP) {
            Spine ms = spine(e, m);
            if (!defp(e, ms.head))
                goto fail;
            MgTm *x = delta_exp(e, ms.head, ms.args, ms.n);
            *dl = delta_cons(e, ms.head, *dl);
            m = x;
            continue;
        }
        if (m->tag == MG_TM_ALL || m->tag == MG_TM_IMP) {
            Spine ns = spine(e, n);
            if (!defp(e, ns.head))
                goto fail;
            MgTm *x = delta_exp(e, ns.head, ns.args, ns.n);
            *dl = delta_cons(e, ns.head, *dl);
            n = x;
            continue;
        }
        Spine ms = spine(e, m);
        if (defp(e, ms.head)) {
            if (deltap(*dl, ms.head)) {
                m = delta_exp(e, ms.head, ms.args, ms.n);
                continue;
            }
            const Dl *saved = *dl;
            if (convrigid1(e, ms, n, dl))
                return true;
            *dl = saved;
            MgTm *x = delta_exp(e, ms.head, ms.args, ms.n);
            *dl = delta_cons(e, ms.head, *dl);
            m = x;
            continue;
        }
        if (convrigid1(e, ms, n, dl))
            return true;
        goto fail;
    }
fail:
    *dl = in;
    if (memory)
        refuted_add(e, m0, n0);
    return false;
}

/* syntax.ml conv: on beta-eta normal forms; *dl changes only on success. */
static bool conv(MgElab *e, MgTm *m, MgTm *n, const Dl **dl) {
    const Dl *d = *dl;
    if (!conv2(e, mge_beta_eta_norm(e, m), mge_beta_eta_norm(e, n), &d))
        return false;
    *dl = d;
    return true;
}

/* ── the double check (syntax.ml extr_propofpf, check_propofpf) ──────── */

static MgTm *extr_propofpf(MgElab *e, const TpCx *tps, const PfCx *hyps, MgPf *d, const Dl **dl);

static bool check_propofpf(MgElab *e, const TpCx *tps, const PfCx *hyps, MgPf *d, MgTm *p,
                           const Dl **dl) {
    MgTm *q = extr_propofpf(e, tps, hyps, d, dl);
    return conv(e, q, p, dl);
}

static void uses_known(MgElab *e, uint32_t h) {
    mge_row_begin(e);
    mg_buf_printf(&e->row, "(USESKNOWN \"%s\")\n", mge_name_text(e, h));
    mge_row_out(e);
}

static const PfCx *pfcx_shift(MgElab *e, const PfCx *hyps) {
    if (!hyps)
        return NULL;
    PfCx *c = TMP(e, PfCx);
    const PfCx *rest = pfcx_shift(e, hyps->next);
    *c = (PfCx){mge_shift1(e, hyps->prop), rest};
    return c;
}

static MgTm *extr_propofpf(MgElab *e, const TpCx *tps, const PfCx *hyps, MgPf *d, const Dl **dl) {
    switch (d->tag) {
    case MG_PF_HYP: {
        const PfCx *h = hyps;
        for (uint32_t i = 0; h && i < d->n; ++i)
            h = h->next;
        if (!h)
            mge_fail(e, "nth");
        return h->prop;
    }
    case MG_PF_TPAP:
    case MG_PF_KNOWN: {
        uint32_t k = 0;
        MgTp *rev[7];
        MgPf *b = d;
        while (b->tag == MG_PF_TPAP && k < 7u) {
            rev[k++] = b->tp;
            b = b->p;
        }
        if (b->tag != MG_PF_KNOWN || k > 6u)
            mge_fail(e, "Ill-formed Proof Term");
        uint32_t h = b->n;
        const char *hs = mge_name_text(e, h);
        uses_known(e, h);
        const PTm *p = delta_of(e, h);
        if (k == 0u) {
            if (!p)
                return mge_tm_h(e, h);
            if (p->levels == 0u)
                return p->m;
            if (p->levels == 1u)
                mge_fail(e, "%s requires 1 type but was given none", hs);
            mge_fail(e, "%s requires %u types but was given none", hs, p->levels);
        }
        if (!p)
            mge_fail(e, "%s should be known, but is not known", hs);
        MgTp *types[6];
        for (uint32_t i = 0; i < k; ++i)
            types[i] = rev[k - 1u - i];
        if (p->levels == k)
            return tmtpsubst(e, p->m, types, k);
        if (k > 1u && p->levels == 1u)
            mge_fail(e, "%s requires 1 type but %u were given", hs, k);
        mge_fail(e, "%s requires %u types but was given %u", hs, p->levels, k);
    }
    case MG_PF_TMAP: {
        MgTm *q = extr_propofpf(e, tps, hyps, d->p, dl);
        const Dl *d2 = *dl;
        MgTm *hq = headnorm(e, q, &d2);
        if (hq->tag != MG_TM_ALL)
            mge_fail(e, "Proof term does not prove a universally quantified proposition but is applied to a term");
        *dl = d2;
        MgTp *b = mge_extr_tpoftm(e, tps, NULL, d->t);
        if (b != hq->tp)
            mge_fail(e, "Proof term for a universally quantified proposition applied to a term of the wrong type\npf %s\nof %s\napplied to %s\nof type %s\n",
                     pf_str(e, d->p), tm_str(e, hq), tm_str(e, d->t), mge_tp_str(e, b));
        return tmsubst(e, hq->f, 0u, d->t);
    }
    case MG_PF_PFAP: {
        MgTm *q = extr_propofpf(e, tps, hyps, d->p, dl);
        const Dl *d2 = *dl;
        MgTm *hq = headnorm(e, q, &d2);
        if (hq->tag != MG_TM_IMP)
            mge_fail(e, "Proof term does not prove an implication but is applied to a proof term");
        *dl = d2;
        if (!check_propofpf(e, tps, hyps, d->q, hq->f, dl))
            mge_fail(e, "Proof term for an implication applied to a proof term for the wrong proposition");
        return hq->x;
    }
    case MG_PF_TLAM: {
        TpCx inner = {d->tp, tps};
        MgTm *q = extr_propofpf(e, &inner, pfcx_shift(e, hyps), d->p, dl);
        return mge_tm_all(e, d->tp, q);
    }
    case MG_PF_PLAM: {
        PfCx inner = {d->t, hyps};
        MgTm *q = extr_propofpf(e, tps, &inner, d->p, dl);
        return mge_tm_imp(e, d->t, q);
    }
    default:
        mge_fail(e, "Ill-formed Proof Term");
    }
}

/* ── proof terms from the source (interpret.ml extract_pf_r, check_pf_r) */

/* The goal's context as the double check sees it. */
static const TpCx *tp_context(MgElab *e, const Cx *cxtm) {
    if (!cxtm)
        return NULL;
    if (cxtm->def)
        return tp_context(e, cxtm->next);
    TpCx *c = TMP(e, TpCx);
    *c = (TpCx){cxtm->tp, tp_context(e, cxtm->next)};
    return c;
}

static const PfCx *pf_context(MgElab *e, const Hy *cxpf) {
    if (!cxpf)
        return NULL;
    PfCx *c = TMP(e, PfCx);
    *c = (PfCx){cxpf->prop, pf_context(e, cxpf->next)};
    return c;
}

/* pfproplookup cxpf x */
static bool hyp_lookup(const Hy *cxpf, const char *x, uint32_t *i, MgTm **prop) {
    uint32_t k = 0;
    for (; cxpf; cxpf = cxpf->next, ++k)
        if (strcmp(cxpf->name, x) == 0) {
            *i = k;
            *prop = cxpf->prop;
            return true;
        }
    return false;
}

/* List.assoc x sgpf cxtp cxtm cxpf: Known h applied to the section's
 * types, variables and hypotheses as they are named where it is used. */
static MgPf *sig_pf_apply(MgElab *e, MgPf *d, const App *app, const Cx *cxtm, const Hy *cxpf) {
    if (!app)
        return d;
    if (app->kind == APP_TP) {
        int i = mge_tp_position(e, app->name);
        if (i < 0)
            mge_not_found(e);
        return pf_tpap(e, sig_pf_apply(e, d, app->prev, cxtm, cxpf), mge_ty_var(e, (uint32_t)i));
    }
    if (app->kind == APP_TM) {
        MgTm *v;
        MgTp *ignored;
        if (!mge_tmtplookup(e, cxtm, app->name, &v, &ignored))
            mge_not_found(e);
        return pf_tmap(e, sig_pf_apply(e, d, app->prev, cxtm, cxpf), v);
    }
    uint32_t hi;
    MgTm *ignored;
    if (!hyp_lookup(cxpf, app->name, &hi, &ignored))
        mge_not_found(e);
    return pf_pfap(e, sig_pf_apply(e, d, app->prev, cxtm, cxpf), pf_hyp(e, hi));
}

static MgPf *sig_pf_lookup(MgElab *e, const char *x, const Cx *cxtm, const Hy *cxpf) {
    uint64_t v;
    if (!mge_stk_top(&e->sigpf, K(x), &v))
        mge_not_found(e);
    const SigPf *sp = P(const SigPf, v);
    return sig_pf_apply(e, pf_known(e, sp->known), sp->app, cxtm, cxpf);
}

static bool polypf_get(MgElab *e, const char *x, uint32_t arity, MgTm **prop) {
    uint64_t v;
    if (arity > 6u || !mge_map_get(&e->polypf[arity], K(x), &v))
        return false;
    *prop = P(MgTm, v);
    return true;
}

typedef struct {
    MgPf *pf;
    MgTm *tm;
} PfTm;

static PfTm pt(MgPf *d, MgTm *p) { return (PfTm){d, p}; }

static PfTm extract_pf_r(MgElab *e, const MgL *a, const Cx *cxtm, const Hy *cxpf);
static MgPf *check_pf_r(MgElab *e, const MgL *a, MgTm *p, const Cx *cxtm, const Hy *cxpf);

typedef struct {
    const char *x;
    const Cx *cxtm;
    const Hy *cxpf;
    PfTm out;
} SigPfTry;

static void signature_proof(MgElab *e, void *ctx) {
    SigPfTry *t = ctx;
    MgPf *d = sig_pf_lookup(e, t->x, t->cxtm, t->cxpf);
    const Dl *dl = e->deltaset;
    MgTm *p = extr_propofpf(e, tp_context(e, t->cxtm), pf_context(e, t->cxpf), d, &dl);
    e->deltaset = dl;
    t->out = pt(d, p);
}

/* struct_unify (interpret.ml): metavariables set where structurally
 * obvious; nothing else. */
static void struct_unify(MgElab *e, uint32_t cxn, MgTm *m, MgTm *n);

static bool metavar_name(MgElab *e, const MgTm *m, const char **vn) {
    if (m->tag != MG_TM_NAME)
        return false;
    const char *h = mge_name_text(e, m->n);
    if (h[0] != '?')
        return false;
    *vn = mge_intern(e, h + 1);
    return true;
}

typedef struct {
    MgTm *m;
    int32_t j;
    MgTm *out;
} ShiftTry;

static void shift_body(MgElab *e, void *ctx) {
    ShiftTry *t = ctx;
    t->out = mge_tmshift(e, 0u, t->j, t->m);
}

static void struct_unify_var(MgElab *e, uint32_t cxn, const char *vn, MgTm *other, bool var_left,
                             MgTm *self) {
    uint64_t cxv, mv;
    if (!mge_map_get(&e->metavarcx, K(vn), &cxv))
        return;
    uint32_t vcxn = P(const MetaCx, cxv)->count;
    if (mge_map_get(&e->metavarassoc, K(vn), &mv)) {
        MgTm *m1 = P(MgTm, mv);
        if (vcxn < cxn)
            m1 = mge_tmshift(e, 0u, (int32_t)(cxn - vcxn), m1);
        else if (vcxn > cxn)
            mge_fail(e, "Metavar ?%s occurs out of context", vn);
        if (var_left)
            struct_unify(e, cxn, m1, other);
        else
            struct_unify(e, cxn, other, m1);
        return;
    }
    (void)self;
    if (vcxn == cxn) {
        mge_map_set(e, &e->metavarassoc, K(vn), V(other));
    } else if (vcxn < cxn) {
        /* tmshift 0 (vcxn - cxn): NegDB means a bound variable is needed */
        ShiftTry t = {other, (int32_t)vcxn - (int32_t)cxn, NULL};
        int k = mge_try_run(e, shift_body, &t);
        if (k == RAISE_NEGDB)
            mge_fail(e, "Metavar ?%s needs to use a bound var", vn);
        if (k)
            mge_raise_kind(e, k);
        mge_map_set(e, &e->metavarassoc, K(vn), V(t.out));
    } else {
        mge_fail(e, "Metavar ?%s occurs out of context", vn);
    }
}

static void struct_unify(MgElab *e, uint32_t cxn, MgTm *m, MgTm *n) {
    const char *vn;
    if (m->tag == MG_TM_TAP && n->tag == MG_TM_TAP) {
        struct_unify(e, cxn, m->f, n->f);
        return;
    }
    if ((m->tag == MG_TM_AP && n->tag == MG_TM_AP) || (m->tag == MG_TM_IMP && n->tag == MG_TM_IMP)) {
        struct_unify(e, cxn, m->f, n->f);
        struct_unify(e, cxn, m->x, n->x);
        return;
    }
    if ((m->tag == MG_TM_LAM && n->tag == MG_TM_LAM) || (m->tag == MG_TM_ALL && n->tag == MG_TM_ALL)) {
        struct_unify(e, cxn + 1u, m->f, n->f);
        return;
    }
    if (metavar_name(e, m, &vn)) {
        if (m != n)
            struct_unify_var(e, cxn, vn, n, true, m);
        return;
    }
    if (metavar_name(e, n, &vn))
        struct_unify_var(e, cxn, vn, m, false, n);
}

typedef struct {
    const MgL *bvl_owner;
    const Cx *cxtm;
    const Hy *cxpf;
} PfLamArgs;

/* A binder's groups, with the names of the first group already bound. */
typedef struct {
    const MgLVarGroup *groups;
    uint32_t count;
    uint32_t skip;
    bool drop_asc;
} PBvl;

static PBvl pbvl_next(PBvl b) { return (PBvl){b.groups + 1, b.count - 1u, 0u, false}; }
static uint32_t pbvl_names(PBvl b) { return b.groups[0].name_count - b.skip; }
static const MgLAsc *pbvl_asc(PBvl b) { return b.drop_asc ? NULL : b.groups[0].asc; }

static const Hy *hyps_shift(MgElab *e, const Hy *cxpf) {
    if (!cxpf)
        return NULL;
    Hy *h = TMP(e, Hy);
    const Hy *rest = hyps_shift(e, cxpf->next);
    *h = (Hy){cxpf->name, mge_shift1(e, cxpf->prop), rest};
    return h;
}

static const Hy *hy_push(MgElab *e, const Hy *next, const char *name, MgTm *prop) {
    Hy *h = TMP(e, Hy);
    *h = (Hy){name, prop, next};
    return h;
}

typedef struct {
    PBvl bvl;
    const MgL *body;
    const Cx *cxtm;
    const Hy *cxpf;
    MgTp *b1tp_out;
    PfTm out;
} LamTypeTry;

static PfTm extract_pf_lam(MgElab *e, PBvl bvl, const MgL *body, const Cx *cxtm, const Hy *cxpf);

/* the type reading of a proof lambda's ascription */
static void lam_as_type(MgElab *e, void *ctx) {
    LamTypeTry *t = ctx;
    const MgLVarGroup *g = &t->bvl.groups[0];
    const char *x = mge_intern(e, g->names[t->bvl.skip]);
    MgTp *b1tp = mge_extract_tp(e, pbvl_asc(t->bvl)->type);
    PBvl rest = {t->bvl.groups, t->bvl.count, t->bvl.skip + 1u, false};
    PfTm r = extract_pf_lam(e, rest, t->body, mge_cx_push(e, t->cxtm, x, b1tp, NULL, false),
                            hyps_shift(e, t->cxpf));
    t->out = pt(pf_tlam(e, b1tp, r.pf), mge_tm_all(e, b1tp, r.tm));
}

/* extract_pf_r_lam */
static PfTm extract_pf_lam(MgElab *e, PBvl bvl, const MgL *body, const Cx *cxtm, const Hy *cxpf) {
    if (!bvl.count)
        return extract_pf_r(e, body, cxtm, cxpf);
    if (pbvl_names(bvl) == 0u)
        return extract_pf_lam(e, pbvl_next(bvl), body, cxtm, cxpf);
    const MgLVarGroup *g = &bvl.groups[0];
    const char *x = mge_intern(e, g->names[bvl.skip]);
    const MgLAsc *asc = pbvl_asc(bvl);
    if (!asc)
        mge_fail(e, "Cannot infer the type or prop for the proof level lambda bound variable %s", x);
    if (asc->kind == MG_ASC_SET)
        mge_fail(e, ":e ascriptions are not allowed for proof level lambdas (for now)");
    if (asc->kind == MG_ASC_SUBEQ)
        mge_fail(e, "c= ascriptions are not allowed for proof level lambdas (for now)");
    LamTypeTry t = {bvl, body, cxtm, cxpf, NULL, {0}};
    int k = mge_try_run(e, lam_as_type, &t);
    if (k == 0)
        return t.out;
    if (k != RAISE_FAILURE)
        mge_raise_kind(e, k);
    MgTm *b1tm = mge_check_tm_r(e, asc->type, mge_ty_prop(e), cxtm);
    PBvl rest = {bvl.groups, bvl.count, bvl.skip + 1u, true};
    PfTm r = extract_pf_lam(e, rest, body, cxtm, hy_push(e, cxpf, x, b1tm));
    return pt(pf_plam(e, b1tm, r.pf), mge_tm_imp(e, b1tm, r.tm));
}

/* check_pf_r_lam */
static MgPf *check_pf_lam(MgElab *e, MgTm *p, PBvl bvl, const MgL *body, const Cx *cxtm,
                          const Hy *cxpf) {
    if (!bvl.count)
        return check_pf_r(e, body, p, cxtm, cxpf);
    if (pbvl_names(bvl) == 0u)
        return check_pf_lam(e, p, pbvl_next(bvl), body, cxtm, cxpf);
    const MgLVarGroup *g = &bvl.groups[0];
    const char *x = mge_intern(e, g->names[bvl.skip]);
    const MgLAsc *asc = pbvl_asc(bvl);
    PBvl rest_none = {bvl.groups, bvl.count, bvl.skip + 1u, true};
    uint32_t cxn = mge_cx_len(cxtm);
    if (!asc) {
        const Dl *dl = e->deltaset;
        MgTm *hp = headnorm(e, p, &dl);
        e->deltaset = dl;
        if (hp->tag == MG_TM_IMP) {
            MgPf *d1 = check_pf_lam(e, hp->x, rest_none, body, cxtm, hy_push(e, cxpf, x, hp->f));
            return pf_plam(e, hp->f, d1);
        }
        if (hp->tag == MG_TM_ALL) {
            MgPf *d1 = check_pf_lam(e, hp->f, rest_none, body,
                                    mge_cx_push(e, cxtm, x, hp->tp, NULL, false), hyps_shift(e, cxpf));
            return pf_tlam(e, hp->tp, d1);
        }
        mge_fail(e, "Lambda in proof term is expected to prove something other than an implication or universal quantifier: %s",
                 tm_str(e, hp));
    }
    if (asc->kind == MG_ASC_SET)
        mge_fail(e, ":e ascriptions are not allowed for proof level lambdas (for now)");
    if (asc->kind == MG_ASC_SUBEQ)
        mge_fail(e, "c= ascriptions are not allowed for proof level lambdas (for now)");
    const Dl *dl = e->deltaset;
    MgTm *hp = headnorm(e, p, &dl);
    e->deltaset = dl;
    if (hp->tag == MG_TM_IMP) {
        MgTm *q1 = mge_check_tm_r(e, asc->type, mge_ty_prop(e), cxtm);
        struct_unify(e, cxn, hp->f, q1);
        q1 = tmv(e, cxn, q1);
        MgTm *p1 = tmv(e, cxn, hp->f);
        MgTm *p2 = tmv(e, cxn, hp->x);
        const Dl *d2 = e->deltaset;
        if (!conv(e, q1, p1, &d2))
            mge_fail(e, "Lambda bound variable %s in proof term ascribed prop %s but expected %s", x,
                     tm_str(e, q1), tm_str(e, p1));
        e->deltaset = d2;
        MgPf *d1 = check_pf_lam(e, p2, rest_none, body, cxtm, hy_push(e, cxpf, x, q1));
        return pf_plam(e, q1, d1);
    }
    if (hp->tag == MG_TM_ALL) {
        MgTp *b1tp = mge_extract_tp(e, asc->type);
        if (b1tp != hp->tp)
            mge_fail(e, "Lambda bound variable %s in proof term ascribed type %s but expected %s", x,
                     mge_tp_str(e, b1tp), mge_tp_str(e, hp->tp));
        PBvl rest_keep = {bvl.groups, bvl.count, bvl.skip + 1u, bvl.drop_asc};
        MgPf *d1 = check_pf_lam(e, hp->f, rest_keep, body,
                                mge_cx_push(e, cxtm, x, hp->tp, NULL, false), hyps_shift(e, cxpf));
        return pf_tlam(e, hp->tp, d1);
    }
    mge_fail(e, "Lambda in proof term is expected to prove something other than an implication or universal quantifier: %s",
             tm_str(e, hp));
}

/* Implop (... (Implop (Na x, a1) ...), ak) */
static bool pf_spine(const MgL *a, const MgL **head, const MgL **args, uint32_t *k) {
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
    const MgL *a;
    const Cx *cxtm;
    const Hy *cxpf;
    PfTm out;
} LetTry;

/* Le (x, None, b, body): the term reading */
static void let_as_term(MgElab *e, void *ctx) {
    LetTry *t = ctx;
    TmTp b = mge_extract_tm_r(e, t->a->a, t->cxtm);
    t->out = extract_pf_r(e, t->a->b, mge_cx_push(e, t->cxtm, mge_intern(e, t->a->text), b.tp, b.tm, false),
                          t->cxpf);
}

/* Le (x, Some (AscTp, a), b, body): the term reading */
static void let_asc_as_term(MgElab *e, void *ctx) {
    LetTry *t = ctx;
    MgTp *atp = mge_extract_tp(e, t->a->asc->type);
    MgTm *btm = mge_check_tm_r(e, t->a->a, atp, t->cxtm);
    t->out = extract_pf_r(e, t->a->b, mge_cx_push(e, t->cxtm, mge_intern(e, t->a->text), atp, btm, false),
                          t->cxpf);
}

typedef struct {
    const MgL *a;
    TmTp at;
    const Cx *cxtm;
    const Hy *cxpf;
    PfTm out;
} LetMTry;

static void let_tuple_proof(MgElab *e, void *ctx) {
    LetMTry *t = ctx;
    MgTp *atp = t->at.tp;
    if (atp->tag != MG_TP_ARR)
        mge_not_found(e);
    MgTp *xtp = atp->b;
    uint32_t n = 0;
    for (MgTp *b = atp->a; b != xtp; b = b->b) {
        if (b->tag != MG_TP_ARR || b->a != xtp)
            mge_not_found(e);
        ++n;
    }
    uint32_t vars = 1u + t->a->names_len;
    if (n != vars)
        mge_fail(e, "The term in 'let [...] := <term>' has type of an %u-metatuple while %u variables were given.", n, vars);
    const Cx *ncx = t->cxtm;
    for (uint32_t i = 0; i < vars; ++i) {
        const char *x = mge_intern(e, i == 0 ? t->a->text : t->a->names[i - 1u]);
        MgTm *proj = mge_tm_db(e, vars - 1u - i);
        for (uint32_t k = 0; k < n; ++k)
            proj = mge_tm_lam(e, xtp, proj);
        ncx = mge_cx_push(e, ncx, x, xtp, mge_tm_ap(e, t->at.tm, proj), false);
    }
    t->out = extract_pf_r(e, t->a->b, ncx, t->cxpf);
}

static PfTm extract_pf_r(MgElab *e, const MgL *a0, const Cx *cxtm, const Hy *cxpf) {
    const MgL *a = mge_S(a0);
    switch (a->kind) {
    case MG_L_NAME: {
        const char *x = mge_intern(e, a->text);
        uint32_t i;
        MgTm *prop;
        if (hyp_lookup(cxpf, x, &i, &prop))
            return pt(pf_hyp(e, i), prop);
        SigPfTry t = {x, cxtm, cxpf, {0}};
        int k = mge_try_run(e, signature_proof, &t);
        if (k == 0)
            return t.out;
        if (k != RAISE_NOT_FOUND)
            mge_raise_kind(e, k);
        mge_fail(e, "Unknown proof %s -- it might be a term in a position where a proof is expected", x);
    }
    case MG_L_IMPLICIT: {
        const MgL *head, *args[6];
        uint32_t k;
        if (pf_spine(a, &head, args, &k)) {
            const char *x = mge_intern(e, head->text);
            MgTm *xa;
            if (polypf_get(e, x, k, &xa)) {
                MgTp *cl[6];
                for (uint32_t i = 0; i < k; ++i)
                    cl[i] = mge_extract_tp(e, args[i]);
                MgPf *xd = sig_pf_lookup(e, x, cxtm, cxpf);
                for (uint32_t i = 0; i < k; ++i)
                    xd = pf_tpap(e, xd, cl[i]);
                return pt(xd, tmtpsubst(e, xa, cl, k));
            }
        }
        PfTm a1 = extract_pf_r(e, a->a, cxtm, cxpf);
        const Dl *dl = e->deltaset;
        MgTm *p = headnorm(e, a1.tm, &dl);
        e->deltaset = dl;
        uint32_t cxn = mge_cx_len(cxtm);
        if (p->tag == MG_TM_IMP) {
            MgPf *d1 = check_pf_r(e, a->b, p->f, cxtm, cxpf);
            return pt(pf_pfap(e, a1.pf, d1), tmv(e, cxn, p->x));
        }
        if (p->tag == MG_TM_ALL) {
            MgTm *m1 = mge_check_tm_r(e, a->b, p->tp, cxtm);
            MgTm *p2 = tmv(e, cxn, p->f);
            return pt(pf_tmap(e, a1.pf, m1), tmsubst(e, p2, 0u, m1));
        }
        mge_fail(e, "Proof term applied like a function but is not a proof of an implication or universal quantifier: %s",
                 tm_str(e, p));
    }
    case MG_L_BINDER:
        if (strcmp(a->text, "fun") == 0)
            return extract_pf_lam(e, (PBvl){a->groups, a->group_count, 0u, false}, a->a, cxtm, cxpf);
        break;
    case MG_L_LET: {
        if (a->asc && a->asc->kind != MG_ASC_TP)
            mge_fail(e, "let variables in proof terms cannot be ascribed with :e or c=");
        LetTry t = {a, cxtm, cxpf, {0}};
        int k = mge_try_run(e, a->asc ? let_asc_as_term : let_as_term, &t);
        if (k == 0)
            return t.out;
        if (k != RAISE_FAILURE)
            mge_raise_kind(e, k);
        const char *x = mge_intern(e, a->text);
        if (!a->asc) {
            PfTm b = extract_pf_r(e, a->a, cxtm, cxpf);
            PfTm body = extract_pf_r(e, a->b, cxtm, hy_push(e, cxpf, x, b.tm));
            return pt(pf_pfap(e, pf_plam(e, b.tm, body.pf), b.pf), body.tm);
        }
        MgTm *atm = mge_check_tm_r(e, a->asc->type, mge_ty_prop(e), cxtm);
        MgPf *bpf = check_pf_r(e, a->a, atm, cxtm, cxpf);
        PfTm body = extract_pf_r(e, a->b, cxtm, hy_push(e, cxpf, x, atm));
        return pt(pf_pfap(e, pf_plam(e, atm, body.pf), bpf), body.tm);
    }
    case MG_L_LETM: {
        LetMTry t = {a, mge_extract_tm_r(e, a->a, cxtm), cxtm, cxpf, {0}};
        int k = mge_try_run(e, let_tuple_proof, &t);
        if (k == 0)
            return t.out;
        if (k != RAISE_NOT_FOUND)
            mge_raise_kind(e, k);
        mge_fail(e, "The term in 'let [...] := <term>' has type %s which is not a meta-tuple type",
                 mge_tp_str(e, t.at.tp));
    }
    case MG_L_QSTRING:
        mge_fail(e, "automation can only be used in positions where the goal is clear: %s", a->text);
    default:
        break;
    }
    mge_fail(e, "Unknown proof term construct");
}

static MgPf *automation(MgElab *e, const char *x, MgTm *p, const Cx *cxtm, const Hy *cxpf);

static MgPf *check_pf_r(MgElab *e, const MgL *a0, MgTm *p, const Cx *cxtm, const Hy *cxpf) {
    const MgL *a = mge_S(a0);
    if (a->kind == MG_L_QSTRING)
        return automation(e, a->text, p, cxtm, cxpf);
    if (a->kind == MG_L_BINDER && strcmp(a->text, "fun") == 0)
        return check_pf_lam(e, p, (PBvl){a->groups, a->group_count, 0u, false}, a->a, cxtm, cxpf);
    PfTm r = extract_pf_r(e, a, cxtm, cxpf);
    uint32_t cxn = mge_cx_len(cxtm);
    struct_unify(e, cxn, p, r.tm);
    MgPf *d = pf_metavar_subst(e, cxn, r.pf);
    MgTm *dtm = tmv(e, cxn, r.tm);
    p = tmv(e, cxn, p);
    const Dl *dl = e->deltaset;
    if (!conv(e, p, dtm, &dl))
        mge_fail(e, "Proof term proves %s but expected to prove %s", tm_str(e, dtm), tm_str(e, p));
    e->deltaset = dl;
    return d;
}

static PfTm extract_pf(MgElab *e, const MgL *a, const Cx *cxtm, const Hy *cxpf) {
    mge_map_clear(&e->metavarcx);
    mge_map_clear(&e->metavarassoc);
    PfTm r = extract_pf_r(e, a, cxtm, cxpf);
    uint32_t cxn = mge_cx_len(cxtm);
    return pt(pf_metavar_subst(e, cxn, r.pf), tmv(e, cxn, r.tm));
}

static MgPf *check_pf(MgElab *e, const MgL *a, MgTm *p, const Cx *cxtm, const Hy *cxpf) {
    mge_map_clear(&e->metavarcx);
    mge_map_clear(&e->metavarassoc);
    MgPf *d = check_pf_r(e, a, p, cxtm, cxpf);
    return pf_metavar_subst(e, mge_cx_len(cxtm), d);
}

static MgTm *check_tm_goal(MgElab *e, const MgL *a, MgTp *tp, const Cx *cxtm) {
    mge_map_clear(&e->metavarcx);
    mge_map_clear(&e->metavarassoc);
    MgTm *m = mge_check_tm_r(e, a, tp, cxtm);
    return e->metavarcx.len ? mge_metavar_subst(e, mge_cx_len(cxtm), m) : m;
}

static TmTp extract_tm_goal(MgElab *e, const MgL *a, const Cx *cxtm) {
    mge_map_clear(&e->metavarcx);
    mge_map_clear(&e->metavarassoc);
    TmTp r = mge_extract_tm_r(e, a, cxtm);
    if (e->metavarcx.len)
        r.tm = mge_metavar_subst(e, mge_cx_len(cxtm), r.tm);
    return r;
}

/* ── metavariable terms (syntax.ml 4271-4590) ─────────────────────────── */

typedef enum { M_VAR, M_DB, M_TMH, M_PRIM, M_TPAP, M_AP, M_LAM, M_IMP, M_ALL } MTag;

typedef struct Mt Mt;
struct Mt {
    uint8_t tag;
    uint32_t n;            /* VAR number, DB index, TMH name, PRIM index */
    MgTp *a;               /* TPAP, LAM, ALL */
    const Mt *x, *y;       /* TPAP x; AP x y; LAM/ALL body x; IMP x y */
    const Mt **sigma;      /* VAR */
    uint32_t sigma_len;
};

static const Mt *mt(MgElab *e, uint8_t tag, uint32_t n, MgTp *a, const Mt *x, const Mt *y) {
    Mt *m = TMP(e, Mt);
    *m = (Mt){tag, n, a, x, y, NULL, 0u};
    return m;
}

static const Mt *mt_var(MgElab *e, uint32_t n, const Mt **sigma, uint32_t len) {
    Mt *m = TMP(e, Mt);
    *m = (Mt){M_VAR, n, NULL, NULL, NULL, sigma, len};
    return m;
}

static const Mt *mt_db(MgElab *e, uint32_t i) { return mt(e, M_DB, i, NULL, NULL, NULL); }

static bool mt_eq(const Mt *p, const Mt *q) {
    if (p == q)
        return true;
    if (p->tag != q->tag || p->n != q->n || p->a != q->a)
        return false;
    switch (p->tag) {
    case M_VAR:
        if (p->sigma_len != q->sigma_len)
            return false;
        for (uint32_t i = 0; i < p->sigma_len; ++i)
            if (!mt_eq(p->sigma[i], q->sigma[i]))
                return false;
        return true;
    case M_TPAP: case M_LAM: case M_ALL:
        return mt_eq(p->x, q->x);
    case M_AP: case M_IMP:
        return mt_eq(p->x, q->x) && mt_eq(p->y, q->y);
    default:
        return true;
    }
}

static const Mt *tm_to_mtm(MgElab *e, MgTm *q) {
    switch (q->tag) {
    case MG_TM_DB: return mt_db(e, q->n);
    case MG_TM_NAME: return mt(e, M_TMH, q->n, NULL, NULL, NULL);
    case MG_TM_PRIM: return mt(e, M_PRIM, q->n, NULL, NULL, NULL);
    case MG_TM_TAP: return mt(e, M_TPAP, 0, q->tp, tm_to_mtm(e, q->f), NULL);
    case MG_TM_AP: {
        const Mt *y = tm_to_mtm(e, q->x);
        return mt(e, M_AP, 0, NULL, tm_to_mtm(e, q->f), y);
    }
    case MG_TM_IMP: {
        const Mt *y = tm_to_mtm(e, q->x);
        return mt(e, M_IMP, 0, NULL, tm_to_mtm(e, q->f), y);
    }
    case MG_TM_LAM: return mt(e, M_LAM, 0, q->tp, tm_to_mtm(e, q->f), NULL);
    case MG_TM_ALL: return mt(e, M_ALL, 0, q->tp, tm_to_mtm(e, q->f), NULL);
    default: mge_fail(e, "Ill-formed term");
    }
}

/* mtm_to_tm; NULL where a metavariable remains (Not_found). */
static MgTm *mtm_to_tm(MgElab *e, const Mt *q) {
    switch (q->tag) {
    case M_VAR: return NULL;
    case M_DB: return mge_tm_db(e, q->n);
    case M_TMH: return mge_tm_h(e, q->n);
    case M_PRIM: return mge_tm_prim(e, q->n);
    case M_TPAP: {
        MgTm *f = mtm_to_tm(e, q->x);
        return f ? mge_tm_tap(e, f, q->a) : NULL;
    }
    case M_AP: case M_IMP: {
        MgTm *y = mtm_to_tm(e, q->y);
        MgTm *x = y ? mtm_to_tm(e, q->x) : NULL;
        if (!x)
            return NULL;
        return q->tag == M_AP ? mge_tm_ap(e, x, y) : mge_tm_imp(e, x, y);
    }
    case M_LAM: case M_ALL: {
        MgTm *b = mtm_to_tm(e, q->x);
        if (!b)
            return NULL;
        return q->tag == M_LAM ? mge_tm_lam(e, q->a, b) : mge_tm_all(e, q->a, b);
    }
    default: return NULL;
    }
}

static MgTm *mtm_to_tm_or_not_found(MgElab *e, const Mt *q) {
    MgTm *m = mtm_to_tm(e, q);
    if (!m)
        mge_not_found(e);
    return m;
}

static const Mt **sigma_map(MgElab *e, const Mt **sigma, uint32_t len,
                            const Mt *(*f)(MgElab *, const Mt *, void *), void *ctx) {
    if (!len)
        return NULL;
    const Mt **out = mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * len);
    for (uint32_t i = 0; i < len; ++i)
        out[i] = f(e, sigma[i], ctx);
    return out;
}

typedef struct {
    uint32_t i;
    int32_t j;
} ShiftArgs;

static const Mt *mtm_shift(MgElab *e, uint32_t i, int32_t j, const Mt *m);

static const Mt *shift_in(MgElab *e, const Mt *m, void *ctx) {
    ShiftArgs *a = ctx;
    return mtm_shift(e, a->i, a->j, m);
}

static const Mt *mtm_shift(MgElab *e, uint32_t i, int32_t j, const Mt *m) {
    switch (m->tag) {
    case M_VAR: {
        ShiftArgs a = {i, j};
        return mt_var(e, m->n, sigma_map(e, m->sigma, m->sigma_len, shift_in, &a), m->sigma_len);
    }
    case M_DB: {
        if (m->n < i)
            return m;
        int64_t l = (int64_t)m->n + j;
        if (l < (int64_t)i)
            mge_raise_kind(e, RAISE_NEGDB);
        return mt_db(e, (uint32_t)l);
    }
    case M_TPAP: return mt(e, M_TPAP, 0, m->a, mtm_shift(e, i, j, m->x), NULL);
    case M_AP: case M_IMP: {
        const Mt *x = mtm_shift(e, i, j, m->x);
        return mt(e, m->tag, 0, NULL, x, mtm_shift(e, i, j, m->y));
    }
    case M_LAM: case M_ALL: return mt(e, m->tag, 0, m->a, mtm_shift(e, i + 1u, j, m->x), NULL);
    default: return m;
    }
}

typedef struct {
    uint32_t j;
    const Mt *n;
} SubstArgs;

static const Mt *mtm_subst(MgElab *e, const Mt *m, uint32_t j, const Mt *n);

static const Mt *subst_in(MgElab *e, const Mt *m, void *ctx) {
    SubstArgs *a = ctx;
    return mtm_subst(e, m, a->j, a->n);
}

static const Mt *mtm_subst(MgElab *e, const Mt *m, uint32_t j, const Mt *n) {
    switch (m->tag) {
    case M_VAR: {
        SubstArgs a = {j, n};
        return mt_var(e, m->n, sigma_map(e, m->sigma, m->sigma_len, subst_in, &a), m->sigma_len);
    }
    case M_DB:
        if (m->n == j)
            return j == 0 ? n : mtm_shift(e, 0u, (int32_t)j, n);
        return m->n > j ? mt_db(e, m->n - 1u) : m;
    case M_TPAP: return mt(e, M_TPAP, 0, m->a, mtm_subst(e, m->x, j, n), NULL);
    case M_AP: case M_IMP: {
        const Mt *x = mtm_subst(e, m->x, j, n);
        return mt(e, m->tag, 0, NULL, x, mtm_subst(e, m->y, j, n));
    }
    case M_LAM: case M_ALL: return mt(e, m->tag, 0, m->a, mtm_subst(e, m->x, j + 1u, n), NULL);
    default: return m;
    }
}

typedef struct {
    const Mt **tau;
    uint32_t len;
} Tau;

static const Mt *mtm_ssub(MgElab *e, Tau tau, const Mt *q);

static const Mt *ssub_in(MgElab *e, const Mt *m, void *ctx) {
    return mtm_ssub(e, *(Tau *)ctx, m);
}

static Tau tau_under_binder(MgElab *e, Tau tau) {
    Tau t = {mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (tau.len + 1u)), tau.len + 1u};
    t.tau[0] = mt_db(e, 0u);
    for (uint32_t i = 0; i < tau.len; ++i)
        t.tau[i + 1u] = mtm_shift(e, 0u, 1, tau.tau[i]);
    return t;
}

static const Mt *mtm_ssub(MgElab *e, Tau tau, const Mt *q) {
    switch (q->tag) {
    case M_VAR:
        return mt_var(e, q->n, sigma_map(e, q->sigma, q->sigma_len, ssub_in, &tau), q->sigma_len);
    case M_DB:
        if (q->n >= tau.len)
            mge_fail(e, "nth");
        return tau.tau[q->n];
    case M_TPAP: return mt(e, M_TPAP, 0, q->a, mtm_ssub(e, tau, q->x), NULL);
    case M_AP: case M_IMP: {
        const Mt *x = mtm_ssub(e, tau, q->x);
        return mt(e, q->tag, 0, NULL, x, mtm_ssub(e, tau, q->y));
    }
    case M_LAM: case M_ALL:
        return mt(e, q->tag, 0, q->a, mtm_ssub(e, tau_under_binder(e, tau), q->x), NULL);
    default:
        return q;
    }
}

/* theta: metavariable assignments, newest first (a function raising
 * Not_found in Megalodon). */
typedef struct Theta Theta;
struct Theta {
    uint32_t x;
    const Mt *m;
    const Theta *next;
};

static const Mt *theta_get(const Theta *th, uint32_t x) {
    for (; th; th = th->next)
        if (th->x == x)
            return th->m;
    return NULL;
}

static const Mt *mtm_msub(MgElab *e, const Theta *theta, const Mt *q);

static const Mt *msub_in(MgElab *e, const Mt *m, void *ctx) {
    return mtm_msub(e, ctx, m);
}

static const Mt *mtm_msub(MgElab *e, const Theta *theta, const Mt *q) {
    switch (q->tag) {
    case M_VAR: {
        const Mt **ts = sigma_map(e, q->sigma, q->sigma_len, msub_in, (void *)theta);
        const Mt *m = theta_get(theta, q->n);
        if (m)
            return mtm_ssub(e, (Tau){ts, q->sigma_len}, m);
        return mt_var(e, q->n, ts, q->sigma_len);
    }
    case M_TPAP: return mt(e, M_TPAP, 0, q->a, mtm_msub(e, theta, q->x), NULL);
    case M_AP: case M_IMP: {
        const Mt *x = mtm_msub(e, theta, q->x);
        return mt(e, q->tag, 0, NULL, x, mtm_msub(e, theta, q->y));
    }
    case M_LAM: case M_ALL: return mt(e, q->tag, 0, q->a, mtm_msub(e, theta, q->x), NULL);
    default: return q;
    }
}

/* mtm_minap_db: the least number of arguments de Bruijn j is applied to,
 * or -1 when it does not occur. */
static int64_t op_min(int64_t r, int64_t z) { return r < 0 ? z : (r < z ? r : z); }

static int64_t minap_r(const Mt *q, uint32_t j, int64_t z, int64_t r) {
    switch (q->tag) {
    case M_VAR:
        for (uint32_t i = 0; i < q->sigma_len; ++i)
            r = minap_r(q->sigma[i], j, 0, r);
        return r;
    case M_DB: return q->n == j ? op_min(r, z) : r;
    case M_TPAP: return r;
    case M_AP: return minap_r(q->x, j, z + 1, minap_r(q->y, j, 0, r));
    case M_IMP: return minap_r(q->x, j, 0, minap_r(q->y, j, 0, r));
    case M_LAM: case M_ALL: return minap_r(q->x, j + 1u, 0, r);
    default: return r;
    }
}

static bool lammvar_p(const Mt *q) {
    while (q->tag == M_LAM)
        q = q->x;
    return q->tag == M_VAR;
}

/* mtm_betared_if: beta reduce wherever the predicate allows (only_lammvar:
 * the reduced body is a lambda over a metavariable). */
static const Mt *betared_if_1(MgElab *e, const Mt *q, bool only_lammvar, bool *done) {
    switch (q->tag) {
    case M_VAR: {
        bool all = true;
        const Mt **s = q->sigma_len ? mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * q->sigma_len) : NULL;
        for (uint32_t i = 0; i < q->sigma_len; ++i) {
            bool d;
            s[i] = betared_if_1(e, q->sigma[i], only_lammvar, &d);
            all = all && d;
        }
        *done = all;
        return mt_var(e, q->n, s, q->sigma_len);
    }
    case M_AP: {
        bool d1, d2;
        const Mt *q1 = betared_if_1(e, q->x, only_lammvar, &d1);
        const Mt *q2 = betared_if_1(e, q->y, only_lammvar, &d2);
        if (q1->tag == M_LAM && (!only_lammvar || lammvar_p(q1->x))) {
            *done = false;
            return mtm_subst(e, q1->x, 0u, q2);
        }
        *done = d1 && d2;
        return mt(e, M_AP, 0, NULL, q1, q2);
    }
    case M_LAM: case M_ALL: {
        bool d1;
        const Mt *q1 = betared_if_1(e, q->x, only_lammvar, &d1);
        *done = d1;
        return mt(e, q->tag, 0, q->a, q1, NULL);
    }
    case M_IMP: {
        bool d1, d2;
        const Mt *q1 = betared_if_1(e, q->x, only_lammvar, &d1);
        const Mt *q2 = betared_if_1(e, q->y, only_lammvar, &d2);
        *done = d1 && d2;
        return mt(e, M_IMP, 0, NULL, q1, q2);
    }
    default:
        *done = true;
        return q;
    }
}

static const Mt *betared_if(MgElab *e, const Mt *q, bool only_lammvar) {
    for (;;) {
        bool done;
        q = betared_if_1(e, q, only_lammvar, &done);
        if (done)
            return q;
    }
}

/* pattern_invert k sigma q; NULL for Not_found. */
static const Mt *pattern_invert(MgElab *e, uint32_t k, const Mt **sigma, uint32_t len, MgTm *q) {
    switch (q->tag) {
    case MG_TM_DB:
        if (q->n < k)
            return mt_db(e, q->n);
        for (uint32_t j = 0; j < len; ++j)
            if (sigma[j]->tag == M_DB && sigma[j]->n == q->n - k)
                return mt_db(e, j + k);
        return NULL;
    case MG_TM_NAME: return mt(e, M_TMH, q->n, NULL, NULL, NULL);
    case MG_TM_PRIM: return mt(e, M_PRIM, q->n, NULL, NULL, NULL);
    case MG_TM_TAP: {
        const Mt *f = pattern_invert(e, k, sigma, len, q->f);
        return f ? mt(e, M_TPAP, 0, q->tp, f, NULL) : NULL;
    }
    case MG_TM_AP: case MG_TM_IMP: {
        const Mt *x = pattern_invert(e, k, sigma, len, q->f);
        const Mt *y = x ? pattern_invert(e, k, sigma, len, q->x) : NULL;
        return y ? mt(e, q->tag == MG_TM_AP ? M_AP : M_IMP, 0, NULL, x, y) : NULL;
    }
    case MG_TM_LAM: case MG_TM_ALL: {
        const Mt *b = pattern_invert(e, k + 1u, sigma, len, q->f);
        return b ? mt(e, q->tag == MG_TM_LAM ? M_LAM : M_ALL, 0, q->tp, b, NULL) : NULL;
    }
    default:
        return NULL;
    }
}

/* syntax.ml pattern_match: false for MatchFail. */
static bool pattern_match(MgElab *e, const Mt *p, MgTm *q, const Theta **theta) {
    if (p->tag == M_VAR) {
        const Mt *m = theta_get(*theta, p->n);
        if (m)
            return pattern_match(e, mtm_ssub(e, (Tau){p->sigma, p->sigma_len}, m), q, theta);
        const Mt *inv = pattern_invert(e, 0u, p->sigma, p->sigma_len, q);
        if (!inv)
            return false;
        Theta *t = TMP(e, Theta);
        *t = (Theta){p->n, inv, *theta};
        *theta = t;
        return true;
    }
    if (p->tag == M_DB && q->tag == MG_TM_DB && p->n == q->n)
        return true;
    if (p->tag == M_PRIM && q->tag == MG_TM_PRIM && p->n == q->n)
        return true;
    if (p->tag == M_TMH && q->tag == MG_TM_NAME && p->n == q->n)
        return true;
    if (p->tag == M_TPAP && q->tag == MG_TM_TAP) {
        /* a name applied to 1 to 3 types, the same on both sides */
        const Mt *pp = p;
        MgTm *qq = q;
        uint32_t k = 0;
        bool same = true;
        while (pp->tag == M_TPAP && qq->tag == MG_TM_TAP && k < 4u) {
            same = same && pp->a == qq->tp;
            pp = pp->x;
            qq = qq->f;
            ++k;
        }
        if (k <= 3u && pp->tag == M_TMH && qq->tag == MG_TM_NAME && pp->n == qq->n && same)
            return true;
    }
    if ((p->tag == M_AP && q->tag == MG_TM_AP) || (p->tag == M_IMP && q->tag == MG_TM_IMP)) {
        return pattern_match(e, p->x, q->f, theta) && pattern_match(e, p->y, q->x, theta);
    }
    if ((p->tag == M_LAM && q->tag == MG_TM_LAM && p->a == q->tp) ||
        (p->tag == M_ALL && q->tag == MG_TM_ALL && p->a == q->tp))
        return pattern_match(e, p->x, q->f, theta);
    MgTm *p1 = mtm_to_tm(e, p);
    if (!p1)
        return false;
    const Dl *dl = NULL;
    return conv(e, p1, q, &dl);
}

static bool mdefp(MgElab *e, const Mt *h, uint32_t *c, MgTp **types, uint32_t *k) {
    uint32_t n = 0;
    MgTp *rev[4];
    while (h->tag == M_TPAP && n < 4u) {
        rev[n++] = h->a;
        h = h->x;
    }
    if (h->tag != M_TMH || n > 3u)
        return false;
    for (uint32_t i = 0; i < n; ++i)
        types[i] = rev[n - 1u - i];
    *c = h->n;
    *k = n;
    return delta_of(e, h->n) != NULL;
}

static const Mt *mtm_app_red(MgElab *e, const Mt *m, const Mt **args, uint32_t n) {
    uint32_t i = 0;
    while (i < n && m->tag == M_LAM) {
        m = mtm_subst(e, m->x, 0u, args[i]);
        ++i;
    }
    for (; i < n; ++i)
        m = mt(e, M_AP, 0, NULL, m, args[i]);
    return m;
}

static const Mt *mdelta_exp(MgElab *e, uint32_t c, MgTp **types, uint32_t k, const Mt **args,
                            uint32_t n) {
    const PTm *d = delta_of(e, c);
    const char *cs = mge_name_text(e, c);
    uint32_t i = d->levels;
    if (k == 0u) {
        if (i == 0u)
            return mtm_app_red(e, tm_to_mtm(e, d->m), args, n);
        if (i == 1u)
            mge_fail(e, "%s should be applied to 1 type, but is applied to none.", cs);
        mge_fail(e, "%s should be applied to %u types, but is applied to none.", cs, i);
    }
    if (i == k)
        return mtm_app_red(e, tm_to_mtm(e, tmtpsubst(e, d->m, types, k)), args, n);
    if (k > 1u && i == 1u)
        mge_fail(e, "%s should be applied to 1 type, but is applied to %u.", cs, k);
    mge_fail(e, "%s should be applied to %u types, but is applied to %u.", cs, i, k);
}

/* mheadnorm: the head unfolded; every unfolding recorded (mdelta_cons
 * does not look for repeats). */
static const Mt *mheadnorm(MgElab *e, const Mt *m, const Dl **dl) {
    for (;;) {
        if (m->tag == M_LAM || m->tag == M_IMP || m->tag == M_ALL)
            return m;
        uint32_t n = 0;
        for (const Mt *h = m; h->tag == M_AP; h = h->x)
            ++n;
        const Mt **args = n ? mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * n) : NULL;
        const Mt *h = m;
        for (uint32_t k = n; k-- > 0;) {
            args[k] = h->y;
            h = h->x;
        }
        uint32_t c, k;
        MgTp *types[3];
        if (!mdefp(e, h, &c, types, &k))
            return m;
        m = mdelta_exp(e, c, types, k, args, n);
        if (h->tag == M_TMH)
            *dl = dl_cons(e, c, *dl);
    }
}

/* ── megaauto (megaauto.ml: the basic search behind ?? and ?salt?) ───── */

static int32_t salt_word(const MgHash *h, int k) {
    const uint8_t *b = h->b + 4 * k;
    return (int32_t)((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | (uint32_t)b[3]);
}

static MgHash salt_next(const MgHash *salt, uint32_t tag) { return mg_hash_tag(salt, tag); }

__attribute__((noreturn)) static void search_limit(MgElab *e) { mge_raise_kind(e, RAISE_SEARCHLIMIT); }
__attribute__((noreturn)) static void search_backtrack(MgElab *e) { mge_raise_kind(e, RAISE_SEARCHBACKTRACK); }

static bool tp_returns_p(const MgTp *b, const MgTp *a) {
    for (;;) {
        if (b == a)
            return true;
        if (b->tag != MG_TP_ARR)
            return false;
        b = b->b;
    }
}

static uint32_t tpcx_len(const TpCx *cx) {
    uint32_t n = 0;
    for (; cx; cx = cx->next)
        ++n;
    return n;
}

static MgTm *ma_tm(MgElab *e, int lm, MgHash salt, const TpCx *cx, MgTp *a);

/* basic_megaauto_spine */
static MgTm *ma_spine(MgElab *e, int lm, MgHash salt, const TpCx *cx, MgTp *a, MgTp *b, MgTm *m) {
    for (;;) {
        if (lm <= 0 || e->searchlim <= 0)
            search_limit(e);
        --e->searchlim;
        if (a == b)
            return m;
        if (b->tag != MG_TP_ARR)
            search_backtrack(e);
        MgTm *m1 = ma_tm(e, lm - 1, salt, cx, b->a);
        m = mge_tm_ap(e, m, m1);
        b = b->b;
    }
}

typedef struct {
    int lm;
    MgHash salt;
    const TpCx *cx;
    MgTp *a, *b;
    uint32_t i;
    MgTm *out;
} SpineTry;

static void spine_body(MgElab *e, void *ctx) {
    SpineTry *t = ctx;
    t->out = ma_spine(e, t->lm, t->salt, t->cx, t->a, t->b, mge_tm_db(e, t->i));
}

/* basic_megaauto_apvar: a variable whose type ends in a, applied to
 * found arguments; the salt advances only for a candidate tried. */
static MgTm *ma_apvar(MgElab *e, int lm, MgHash salt, const TpCx *cx, MgTp *a) {
    uint32_t i = 0;
    for (const TpCx *bl = cx; bl; bl = bl->next, ++i) {
        if (!tp_returns_p(bl->tp, a))
            continue;
        MgHash salt2 = salt_next(&salt, 23u);
        if (salt_word(&salt2, 7) < 0) {
            SpineTry t = {lm - 1, salt2, cx, a, bl->tp, i, NULL};
            int k = mge_try_run(e, spine_body, &t);
            if (k == 0)
                return t.out;
            if (k != RAISE_SEARCHBACKTRACK)
                mge_raise_kind(e, k);
        }
    }
    search_backtrack(e);
}

/* basic_megaauto_var */
static MgTm *ma_var(MgElab *e, MgHash salt, const TpCx *cx, MgTp *a) {
    uint32_t i = 0;
    for (const TpCx *bl = cx; bl; bl = bl->next, ++i) {
        if (bl->tp != a)
            continue;
        MgHash salt2 = salt_next(&salt, 23u);
        if (salt_word(&salt2, 7) < 0)
            return mge_tm_db(e, i);
    }
    search_backtrack(e);
}

/* basic_megaauto_tm: a term of type a */
static MgTm *ma_tm(MgElab *e, int lm, MgHash salt, const TpCx *cx, MgTp *a) {
    if (lm <= 0 || e->searchlim <= 0)
        search_limit(e);
    --e->searchlim;
    switch (a->tag) {
    case MG_TP_ARR:
        if (salt_word(&salt, 7) < 0) {
            TpCx inner = {a->a, cx};
            MgTm *m = ma_tm(e, lm, salt_next(&salt, 23u), &inner, a->b);
            return mge_tm_lam(e, a->a, m);
        }
        return ma_var(e, salt, cx, a);
    case MG_TP_PROP:
        if (salt_word(&salt, 7) < 0) {
            MgTp *prop = mge_ty_prop(e);
            if (salt_word(&salt, 6) < 0)
                return mge_tm_all(e, prop, mge_tm_db(e, 0u));
            return mge_tm_all(e, prop, mge_tm_imp(e, mge_tm_db(e, 0u), mge_tm_db(e, 0u)));
        }
        return ma_apvar(e, lm, salt, cx, a);
    case MG_TP_BASE:
        return ma_apvar(e, lm, salt, cx, a);
    default:
        search_backtrack(e);
    }
}

/* The proof builder d and the subgoals of basic_megaauto_apply_1. */
typedef enum { MD_HYP, MD_TMAP, MD_PFAP } MdKind;
typedef struct MaD MaD;
struct MaD {
    uint8_t kind;
    uint32_t n;          /* HYP index, TMAP metavariable */
    const MaD *prev;
};

typedef struct MaSub MaSub;  /* premises, newest first */
struct MaSub {
    const Mt *g;
    const MaSub *next;
};

typedef struct MaXl MaXl;    /* metavariables to ground, newest first */
struct MaXl {
    uint32_t xi;
    MgTp *a;
    const MaXl *next;
};

static MgPf *ma_basic(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *g);

/* d theta dl; NULL for Not_found */
static MgPf *ma_build(MgElab *e, const MaD *d, const Theta *theta, MgPf **dl, uint32_t dn) {
    switch (d->kind) {
    case MD_HYP:
        return pf_hyp(e, d->n);
    case MD_TMAP: {
        const Mt *m = theta_get(theta, d->n);
        MgTm *t = m ? mtm_to_tm(e, m) : NULL;
        if (!t)
            return NULL;
        MgPf *inner = ma_build(e, d->prev, theta, dl, dn);
        return inner ? pf_tmap(e, inner, t) : NULL;
    }
    default: {
        if (!dn)
            return NULL;
        MgPf *inner = ma_build(e, d->prev, theta, dl + 1, dn - 1u);
        return inner ? pf_pfap(e, inner, dl[0]) : NULL;
    }
    }
}

typedef struct {
    int lm;
    MgHash salt;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *g;
    const Mt *p;
    const MaSub *subgoals;
    const MaD *d;
    const MaXl *xl;
    const Theta *theta;
    bool matchfail;
    MgPf *out;
} Apply1;

static void apply1_body(MgElab *e, void *ctx) {
    Apply1 *t = ctx;
    const Theta *theta = t->theta;
    if (!pattern_match(e, t->p, t->g, &theta)) {
        t->matchfail = true;
        return;
    }
    /* ground the metavariables pattern_match left open; Megalodon's new
     * theta keeps only the one it grounds (it asks the old theta for xi) */
    for (const MaXl *x = t->xl; x; x = x->next) {
        if (theta_get(theta, x->xi))
            continue;
        MgTm *m = ma_tm(e, t->lm, t->salt, t->cx, x->a);
        Theta *nt = TMP(e, Theta);
        *nt = (Theta){x->xi, tm_to_mtm(e, m), NULL};
        theta = nt;
    }
    uint32_t n = 0;
    for (const MaSub *s = t->subgoals; s; s = s->next)
        ++n;
    MgTm **gl = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * (n ? n : 1u));
    uint32_t k = 0;
    for (const MaSub *s = t->subgoals; s; s = s->next) {
        gl[k] = mtm_to_tm(e, mtm_msub(e, theta, s->g));
        if (!gl[k])
            mge_not_found(e);
        ++k;
    }
    MgPf **dl = mge_chunk_alloc(e, &e->tmp, sizeof(MgPf *) * (n ? n : 1u));
    for (uint32_t i = 0; i < n; ++i)
        dl[i] = ma_basic(e, t->lm, salt_next(&t->salt, i + 1u), t->cx, t->hyps, gl[i]);
    t->out = ma_build(e, t->d, theta, dl, n);
    if (!t->out)
        mge_not_found(e);
}

static MgPf *ma_apply_1(MgElab *e, Apply1 a) {
    for (;;) {
        a.matchfail = false;
        int k = mge_try_run(e, apply1_body, &a);
        if (k == RAISE_NOT_FOUND)
            search_backtrack(e);
        if (k)
            mge_raise_kind(e, k);
        if (!a.matchfail)
            return a.out;
        const Mt *p = a.p;
        if (p->tag == M_ALL) {
            uint32_t n = tpcx_len(a.cx);
            const Mt **sigma = mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (n ? n : 1u));
            for (uint32_t j = 0; j < n; ++j)
                sigma[j] = mt_db(e, j);
            uint32_t xi = ++e->mvarcnt;
            const Mt *x = mt_var(e, xi, sigma, n);
            Tau tau = {mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (n + 1u)), n + 1u};
            tau.tau[0] = x;
            for (uint32_t j = 0; j < n; ++j)
                tau.tau[j + 1u] = sigma[j];
            MaD *d = TMP(e, MaD);
            *d = (MaD){MD_TMAP, xi, a.d};
            MaXl *xl = TMP(e, MaXl);
            *xl = (MaXl){xi, p->a, a.xl};
            a.p = mtm_ssub(e, tau, p->x);
            a.d = d;
            a.xl = xl;
            continue;
        }
        if (p->tag == M_IMP) {
            MaSub *sub = TMP(e, MaSub);
            *sub = (MaSub){p->x, a.subgoals};
            MaD *d = TMP(e, MaD);
            *d = (MaD){MD_PFAP, 0u, a.d};
            a.p = p->y;
            a.subgoals = sub;
            a.d = d;
            continue;
        }
        search_backtrack(e);
    }
}

typedef struct {
    Apply1 *a;
    MgPf *out;
} ApplyTry1;

static void apply_1_try(MgElab *e, void *ctx) {
    ApplyTry1 *t = ctx;
    t->out = ma_apply_1(e, *t->a);
}

/* basic_megaauto_apply: try the hypotheses in order; the salt decides
 * which to skip */
static MgPf *ma_apply(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *g) {
    uint32_t i = 0;
    for (const PfCx *h = hyps; h; h = h->next, ++i) {
        salt = salt_next(&salt, 23u);
        if (salt_word(&salt, 0) < 0)
            continue;
        MaD *base = TMP(e, MaD);
        *base = (MaD){MD_HYP, i, NULL};
        Apply1 a = {lm, salt, cx, hyps, g, tm_to_mtm(e, h->prop), NULL, base, NULL, NULL, false, NULL};
        ApplyTry1 t = {&a, NULL};
        int k = mge_try_run(e, apply_1_try, &t);
        if (k == 0)
            return t.out;
        if (k != RAISE_SEARCHBACKTRACK)
            mge_raise_kind(e, k);
    }
    search_backtrack(e);
}

/* local_hyp: the first hypothesis convertible with the goal */
static bool local_hyp(MgElab *e, const PfCx *hyps, MgTm *g, uint32_t *i) {
    uint32_t k = 0;
    for (const PfCx *h = hyps; h; h = h->next, ++k) {
        const Dl *dl = NULL;
        if (conv(e, h->prop, g, &dl)) {
            *i = k;
            return true;
        }
    }
    return false;
}

static MgPf *ma_basic(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *g) {
    if (lm <= 0 || e->searchlim <= 0)
        search_limit(e);
    --e->searchlim;
    uint32_t i;
    if (local_hyp(e, hyps, g, &i))
        return pf_hyp(e, i);
    MgHash salt2 = salt_next(&salt, 23u);
    int32_t s0 = salt_word(&salt2, 0), s1 = salt_word(&salt2, 1);
    MgTp *prop = mge_ty_prop(e);
    MgTm *fls = NULL;
    if (g->tag == MG_TM_IMP) {
        if (s0 < 1610612734) {
            PfCx inner = {g->f, hyps};
            return pf_plam(e, g->f, ma_basic(e, lm - 1, salt2, cx, &inner, g->x));
        }
        if (s1 < 1610612734)
            return ma_apply(e, lm - 1, salt2, cx, hyps, g);
        fls = mge_tm_all(e, prop, mge_tm_db(e, 0u));
        return pf_tmap(e, ma_basic(e, lm - 1, salt2, cx, hyps, fls), g);
    }
    if (g->tag == MG_TM_ALL) {
        if (g->tp == prop && g->f->tag == MG_TM_DB && g->f->n == 0u)
            return ma_apply(e, lm - 1, salt2, cx, hyps, g);
        if (g->f->tag == MG_TM_IMP && g->f->f == g->f->x)
            return pf_tlam(e, g->tp, pf_plam(e, g->f->f, pf_hyp(e, 0u)));
        if (s0 < 1610612734) {
            TpCx inner = {g->tp, cx};
            return pf_tlam(e, g->tp, ma_basic(e, lm - 1, salt2, &inner, pfcx_shift(e, hyps), g->f));
        }
        if (s1 < 1610612734)
            return ma_apply(e, lm - 1, salt2, cx, hyps, g);
        fls = mge_tm_all(e, prop, mge_tm_db(e, 0u));
        return pf_tmap(e, ma_basic(e, lm - 1, salt2, cx, hyps, fls), g);
    }
    return ma_apply(e, lm - 1, salt2, cx, hyps, g);
}

static bool has_metavar(MgElab *e, const MgTm *m) {
    switch (m->tag) {
    case MG_TM_NAME: return mge_name_text(e, m->n)[0] == '?';
    case MG_TM_TAP: return has_metavar(e, m->f);
    case MG_TM_AP: case MG_TM_IMP: return has_metavar(e, m->f) || has_metavar(e, m->x);
    case MG_TM_LAM: case MG_TM_ALL: return has_metavar(e, m->f);
    default: return false;
    }
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* basic_megaauto or type_megaauto, from the salt given */
typedef MgPf *(*MaFn)(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *g);

typedef struct {
    MaFn fn;
    MgHash salt;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *g;
    MgPf *out;
} MaTry;

static void ma_body(MgElab *e, void *ctx) {
    MaTry *t = ctx;
    e->mvarcnt = 0u;
    e->searchlim = 1000;
    t->out = t->fn(e, 50, t->salt, t->cx, t->hyps, t->g);
}

/* A line Megalodon prints while checking, with the item's position. */
static void ma_line(MgElab *e, const char *what, const char *pre, const MgHash *salt) {
    mge_row_begin(e);
    if (salt) {
        char h[65];
        mg_hash_hex(salt, h);
        int n = 63;
        while (n >= 0 && h[n] == '0')
            --n;
        h[n + 1] = '\0';
        mg_buf_printf(&e->row, "Line %d Char %d: Use ?%s%s?\n", e->lineno, e->charno, pre, h);
    } else {
        mg_buf_printf(&e->row, "Line %d Char %d: %s\n", e->lineno, e->charno, what);
    }
    mge_row_out(e);
}

static void salt_set(MgHash *salt, int k, uint32_t w) {
    uint8_t *b = salt->b + 4 * k;
    b[0] = (uint8_t)(w >> 24);
    b[1] = (uint8_t)(w >> 16);
    b[2] = (uint8_t)(w >> 8);
    b[3] = (uint8_t)w;
}

static uint32_t int32_rev(uint32_t x) {
    return (x & 0xffu) << 24 | (x >> 8 & 0xffu) << 16 | (x >> 16 & 0xffu) << 8 | (x >> 24 & 0xffu);
}

/* basic_megaauto_search_nxt and type_megaauto_search_nxt: the salt after
 * this one */
static MgHash salt_succ(MgHash salt) {
    uint32_t s[8];
    for (int k = 0; k < 8; ++k)
        s[k] = (uint32_t)salt_word(&salt, k);
    if (!(s[1] | s[2] | s[3] | s[4] | s[5] | s[6] | s[7])) {
        uint32_t r = int32_rev(s[0]);
        if (r == UINT32_MAX) {
            s[0] = 0u;
            s[1] = 1u;
        } else {
            s[0] = int32_rev(r + 1u);
        }
    } else if (s[0] != UINT32_MAX) {
        ++s[0];
    } else if (s[1] != UINT32_MAX) {
        ++s[1];
    } else if (s[2] != UINT32_MAX) {
        ++s[2];
    } else {
        ++s[3];
    }
    for (int k = 0; k < 8; ++k)
        salt_set(&salt, k, s[k]);
    return salt;
}

/* basic_megaauto_search and type_megaauto_search: salts from the one
 * given on, megaautosaltlimit of them */
static MgPf *ma_search(MgElab *e, MaTry *t, const char *pre) {
    for (int masl = 65536;; --masl) {
        if (masl <= 0) {
            ma_line(e, "Failed to find salt", pre, NULL);
            search_limit(e);
        }
        int k = mge_try_run(e, ma_body, t);
        if (k == 0) {
            ma_line(e, NULL, pre, &t->salt);
            return t->out;
        }
        if (k != RAISE_SEARCHLIMIT && k != RAISE_SEARCHBACKTRACK)
            mge_raise_kind(e, k);
        t->salt = salt_succ(t->salt);
    }
}

/* basic_megaauto_small_search and type_megaauto_small_search: the salts
 * whose first 16 bits alone are set, nibble by nibble */
static MgPf *ma_small_search(MgElab *e, MaTry *t, const char *pre) {
    for (uint32_t l = 0; l < 16u; ++l)
        for (uint32_t k = 0; k < 16u; ++k)
            for (uint32_t j = 0; j < 16u; ++j)
                for (uint32_t i = 0; i < 16u; ++i) {
                    memset(&t->salt, 0, sizeof t->salt);
                    salt_set(&t->salt, 0, i << 28 | j << 24 | k << 20 | l << 16);
                    int r = mge_try_run(e, ma_body, t);
                    if (r == 0) {
                        ma_line(e, NULL, pre, &t->salt);
                        return t->out;
                    }
                    if (r != RAISE_SEARCHLIMIT && r != RAISE_SEARCHBACKTRACK)
                        mge_raise_kind(e, r);
                }
    ma_line(e, "Failed to find small salt", pre, NULL);
    search_limit(e);
}

/* megaauto.ml megaauto: PRE "" (basic) or "t" (typed); OPS a salt in hex,
 * *SALT (search from it) or + (the small salts) */
static MgPf *megaauto(MgElab *e, const char *pre, size_t plen, const char *ops, const TpCx *cx,
                      const PfCx *hyps, MgTm *g) {
    MaTry t = {ma_basic, {{0}}, cx, hyps, g, NULL};
    const char *use = "";
    if (plen == 1u && pre[0] == 't') {
        t.fn = mge_type_megaauto;
        use = "t:";
    } else if (plen != 0u) {
        mge_fail(e, "unknown prefix \"%.*s\" for megaauto", (int)plen, pre);
    }
    bool search = ops[0] == '*';
    if (!search && strcmp(ops, "+") == 0)
        return ma_small_search(e, &t, use);
    const char *digits = search ? ops + 1 : ops;
    size_t n = strlen(digits);
    if (n > 64u)
        mge_fail(e, "salt for megaauto too long \"%s\"", ops);
    char h[65];
    memcpy(h, digits, n);
    memset(h + n, '0', 64u - n);
    h[64] = '\0';
    for (int i = 0; i < 32; ++i) {
        int hi = hexval(h[2 * i]), lo = hexval(h[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            /* Hashbtc.of_hexstring's exception, which nothing catches */
            mg_buf_reset(&e->msg);
            mg_buf_puts(&e->msg, "Fatal error: exception Invalid_argument(\"c_hexstring_bebits: not a hex string!\")");
            mge_raise_kind(e, RAISE_FATAL);
        }
        t.salt.b[i] = (uint8_t)(hi << 4 | lo);
    }
    if (search)
        return ma_search(e, &t, use);
    e->mvarcnt = 0u;
    e->searchlim = 1000;
    return t.fn(e, 50, t.salt, cx, hyps, g);
}

typedef struct {
    const char *pre;
    size_t plen;
    const char *ops;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *g;
    MgPf *out;
} MaCall;

static void megaauto_body(MgElab *e, void *ctx) {
    MaCall *c = ctx;
    c->out = megaauto(e, c->pre, c->plen, c->ops, c->cx, c->hyps, c->g);
}

/* megaauto as interpret.ml calls it: a search that finds nothing is
 * "megaauto failed" */
static void megaauto_failing(MgElab *e, void *ctx) {
    int k = mge_try_run(e, megaauto_body, ctx);
    if (k == RAISE_SEARCHLIMIT || k == RAISE_SEARCHBACKTRACK)
        mge_fail(e, "megaauto failed");
    if (k)
        mge_raise_kind(e, k);
}

/* check_pf_r's QString case: megaauto PRE OPS on the goal, with the
 * context's variables and hypotheses. */
static MgPf *automation(MgElab *e, const char *x, MgTm *p, const Cx *cxtm, const Hy *cxpf) {
    uint32_t cxn = mge_cx_len(cxtm);
    const PfCx *hyps = NULL;
    {
        /* dcxpf, in order */
        uint32_t n = 0;
        for (const Hy *h = cxpf; h; h = h->next)
            ++n;
        PfCx **cells = mge_chunk_alloc(e, &e->tmp, sizeof(PfCx *) * (n ? n : 1u));
        uint32_t k = 0;
        for (const Hy *h = cxpf; h; h = h->next) {
            PfCx *c = TMP(e, PfCx);
            c->prop = tmv(e, cxn, h->prop);
            c->next = NULL;
            cells[k++] = c;
        }
        for (uint32_t i = n; i-- > 0;) {
            cells[i]->next = hyps;
            hyps = cells[i];
        }
    }
    const TpCx *cx = tp_context(e, cxtm);
    p = tmv(e, tpcx_len(cx), p);
    if (has_metavar(e, p))
        mge_fail(e, "automation can only be used when goal has no metavars: %s", x);
    for (const PfCx *h = hyps; h; h = h->next)
        if (has_metavar(e, h->prop))
            mge_fail(e, "automation can only be used when local hypotheses have no metavars: %s", x);
    const char *colon = strchr(x, ':');
    MaCall c = {"", 0u, x, cx, hyps, p, NULL};
    if (colon) {
        MaCall pre = {x, (size_t)(colon - x), colon + 1, cx, hyps, p, NULL};
        int k = mge_try_run(e, megaauto_failing, &pre);
        if (k == 0)
            return pre.out;
        if (k != RAISE_NOT_FOUND)
            mge_raise_kind(e, k);
    }
    /* no colon, or Not_found from the search: the whole text is basic
     * megaauto's salt */
    megaauto_failing(e, &c);
    return c.out;
}

/* ── proof function frames ────────────────────────────────────────────── */

typedef enum {
    F_EXACT, F_TLAM, F_PLAM, F_CLAIM, F_APPLY, F_REWRITE, F_FALSEE, F_OLDFALSEE,
    F_WITNESS_I, F_WITNESS, F_SYM, F_CHAIN,
} FrameKind;

/* apff's steps (apply, rewrite), outermost last */
typedef enum { S_PFAP, S_TMAP_DEFAULT, S_TMAP_META } StepKind;
typedef struct Step Step;
struct Step {
    uint8_t kind;
    MgTm *defelt;
    const Step *prev;
};

struct PfFrame {
    uint8_t kind;
    MgPf *d;
    MgTp *a;
    MgTm *m, *n, *p;
    MgPf *base;            /* APPLY, REWRITE: apf */
    const Step *steps;
    MgTm **ml;
    uint32_t ml_len;
    uint32_t nsubs;
    bool sym;
    MgTm *leibq;
    MgTp *etp;
    /* F_CHAIN (transitivity, f_equal): the links' proofs, each lifted
     * through its context for f_equal (a term whose de Bruijn 0 is the
     * hole, its other indices raised by one) */
    bool fequal;
    MgTm **ctx;
    uint32_t links;
    MgTm *final_lhs, *final_rhs;
    const PfFrame *prev;
};

static void push_frame(MgElab *e, PfFrame *f) {
    f->prev = e->prooffun;
    e->prooffun = f;
}

static PfFrame *new_frame(MgElab *e, uint8_t kind) {
    PfFrame *f = PERM(e, PfFrame);
    memset(f, 0, sizeof *f);
    f->kind = kind;
    return f;
}

/* The proof list dl: top at the end. */
typedef struct {
    MgPf **items;
    uint32_t len, cap;
} Dlist;

static void dl_push(MgElab *e, Dlist *l, MgPf *d) {
    if (l->len == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2u : 16u;
        MgPf **g = mge_chunk_alloc(e, &e->tmp, sizeof(MgPf *) * cap);
        if (l->len)
            memcpy(g, l->items, sizeof(MgPf *) * l->len);
        l->items = g;
        l->cap = cap;
    }
    l->items[l->len++] = d;
}

static MgPf *dl_pop(MgElab *e, Dlist *l, const char *message) {
    if (!l->len)
        mge_fail(e, "%s", message);
    return l->items[--l->len];
}

/* apff ml dl (megalodon.ml): from the outermost step in. */
static MgPf *eval_steps(MgElab *e, const PfFrame *f, const Step *s, uint32_t mi, MgPf **dl,
                        uint32_t dn, uint32_t di) {
    if (!s)
        return f->base;
    switch (s->kind) {
    case S_PFAP: {
        if (di >= dn)
            mge_fail(e, "proof reconstruction problem");
        MgPf *inner = eval_steps(e, f, s->prev, mi, dl, dn, di + 1u);
        return pf_pfap(e, inner, dl[di]);
    }
    case S_TMAP_DEFAULT: {
        if (di >= dn)
            mge_fail(e, "proof reconstruction problem");
        MgPf *inner = eval_steps(e, f, s->prev, mi, dl, dn, di + 1u);
        return pf_tmap(e, inner, s->defelt);
    }
    default: {
        if (mi >= f->ml_len)
            mge_fail(e, "proof reconstruction problem");
        MgPf *inner = eval_steps(e, f, s->prev, mi + 1u, dl, dn, di);
        return pf_tmap(e, inner, f->ml[mi]);
    }
    }
}

static MgTm *rleib_imp_sleib_body(MgElab *e, MgTp *a, MgPf **out);

static MgPf *apply_frame(MgElab *e, const PfFrame *f, Dlist *dl) {
    MgTp *prop = mge_ty_prop(e);
    switch (f->kind) {
    case F_EXACT:
        dl_push(e, dl, f->d);
        return NULL;
    case F_TLAM:
        dl_push(e, dl, pf_tlam(e, f->a, dl_pop(e, dl, "proof reconstruction problem")));
        return NULL;
    case F_PLAM:
        dl_push(e, dl, pf_plam(e, f->p, dl_pop(e, dl, "proof reconstruction problem")));
        return NULL;
    case F_CLAIM: {
        if (dl->len < 2u)
            mge_fail(e, "proof reconstruction problem");
        MgPf *d1 = dl_pop(e, dl, "");
        MgPf *d2 = dl_pop(e, dl, "");
        dl_push(e, dl, pf_pfap(e, pf_plam(e, f->p, d2), d1));
        return NULL;
    }
    case F_APPLY: {
        /* split_list nsubs dl; apff (List.rev dl1) */
        uint32_t k = f->nsubs < dl->len ? f->nsubs : dl->len;
        MgPf **arg = k ? mge_chunk_alloc(e, &e->tmp, sizeof(MgPf *) * k) : NULL;
        for (uint32_t i = 0; i < k; ++i)  /* dl1 = top k; reversed: deepest first */
            arg[i] = dl->items[dl->len - k + i];
        dl->len -= k;
        dl_push(e, dl, eval_steps(e, f, f->steps, 0u, arg, k, 0u));
        return NULL;
    }
    case F_REWRITE: {
        if (!dl->len)
            mge_fail(e, "proof reconstruction problem");
        MgPf *d0 = dl->items[--dl->len];
        uint32_t want = f->nsubs > 0u ? f->nsubs - 1u : 0u;
        uint32_t k = want < dl->len ? want : dl->len;
        MgPf **arg = mge_chunk_alloc(e, &e->tmp, sizeof(MgPf *) * (k + 1u));
        arg[0] = d0;
        for (uint32_t i = 0; i < k; ++i)
            arg[i + 1u] = dl->items[dl->len - k + i];
        dl->len -= k;
        /* apffm: the first proof proves the rewritten claim */
        MgPf *epf = eval_steps(e, f, f->steps, 0u, arg + 1, k, 0u);
        MgTp *etp = f->etp;
        MgTm *lq = f->sym ? mge_shift1(e, f->leibq) : mge_tmshift(e, 1u, 1, f->leibq);
        MgTm *pred = mge_tm_lam(e, etp, mge_tm_lam(e, etp, lq));
        dl_push(e, dl, pf_pfap(e, pf_tmap(e, epf, pred), arg[0]));
        return NULL;
    }
    case F_FALSEE: {
        MgPf *d1 = dl_pop(e, dl, "proof reconstruction problem");
        dl_push(e, dl, pf_tmap(e, pf_pfap(e, pf_known(e, e->fale), d1), f->m));
        return NULL;
    }
    case F_OLDFALSEE: {
        MgPf *d1 = dl_pop(e, dl, "proof reconstruction problem");
        dl_push(e, dl, pf_tmap(e, d1, f->m));
        return NULL;
    }
    case F_WITNESS_I: {
        MgPf *d1 = dl_pop(e, dl, "proof reconstruction problem");
        MgPf *k = pf_tmap(e, pf_tmap(e, pf_tpap(e, pf_known(e, e->expolyI), f->a), f->m), f->n);
        dl_push(e, dl, pf_pfap(e, k, d1));
        return NULL;
    }
    case F_WITNESS: {
        MgPf *d1 = dl_pop(e, dl, "proof reconstruction problem");
        MgTp *etp = f->a;
        MgTm *ep = f->m, *atm = f->n;
        MgTm *hyp = mge_tm_all(e, etp, mge_tm_imp(e, mge_gen_lam_body(e, mge_shift1(e, ep)), mge_tm_db(e, 1u)));
        MgPf *body = pf_pfap(e, pf_tmap(e, pf_hyp(e, 0u), mge_shift1(e, atm)),
                             pftmshift(e, 0u, 1, pfshift(e, 0u, 1, d1)));
        dl_push(e, dl, pf_tlam(e, prop, pf_plam(e, hyp, body)));
        return NULL;
    }
    case F_SYM: {
        MgPf *d = dl_pop(e, dl, "no pf of symmetric eqn");
        MgTp *a = f->a;
        MgTp *rel = mge_ty_arr(e, a, mge_ty_arr(e, a, prop));
        MgTm *sw = mge_tm_lam(e, a, mge_tm_lam(e, a, mge_tm_ap(e, mge_tm_ap(e, mge_tm_db(e, 2u), mge_tm_db(e, 0u)), mge_tm_db(e, 1u))));
        dl_push(e, dl, pf_tlam(e, rel, pf_tmap(e, pftmshift(e, 0u, 1, d), sw)));
        return NULL;
    }
    case F_CHAIN: {
        /* transitivity, f_equal: links d1 .. dk, the last closed by Hyp 0 */
        MgPf *acc = pf_hyp(e, 0u);
        if (dl->len < f->links)
            mge_fail(e, "%s", f->fequal ? "mssing proof of eqn in f_equal" : "mssing proof of eqn in transitivity");
        MgPf **links = mge_chunk_alloc(e, &e->tmp, sizeof(MgPf *) * (f->links ? f->links : 1u));
        for (uint32_t i = 0; i < f->links; ++i)
            links[i] = dl_pop(e, dl, "");
        MgTm *step = mge_tm_lam(e, f->a, mge_tm_db(e, 1u));
        for (uint32_t i = f->links; i-- > 0;) {
            MgPf *d1 = pfshift(e, 0u, 1, pftmshift(e, 0u, 1, links[i]));
            if (f->ctx) {
                /* lift_eqn_pf d1 f: through the link's context */
                MgTp *rel = mge_ty_arr(e, f->a, mge_ty_arr(e, f->a, prop));
                MgTm *c = f->ctx[i];
                MgTm *lifted = mge_tm_lam(e, f->a, mge_tm_lam(e, f->a,
                    mge_tm_ap(e, mge_tm_ap(e, mge_tm_db(e, 2u), tmsubst(e, c, 0u, mge_tm_db(e, 1u))),
                              tmsubst(e, c, 0u, mge_tm_db(e, 0u)))));
                d1 = pf_tlam(e, rel, pf_tmap(e, pftmshift(e, 0u, 1, d1), lifted));
            }
            acc = pf_pfap(e, pf_tmap(e, d1, step), acc);
        }
        MgPf *rleib;
        rleib_imp_sleib_body(e, f->a, &rleib);
        MgTm *prhs = mge_tm_ap(e, mge_tm_db(e, 0u), mge_shift1(e, f->final_rhs));
        MgPf *d = pf_pfap(e, pf_tmap(e, pf_tmap(e, rleib, f->final_lhs), f->final_rhs),
                          pf_tlam(e, mge_ty_arr(e, f->a, prop), pf_plam(e, prhs, acc)));
        dl_push(e, dl, d);
        return NULL;
    }
    default:
        mge_fail(e, "proof reconstruction problem");
    }
}

/* rleib_imp_sleib (transitivity, f_equal): Leibniz to symmetric equality. */
static MgTm *rleib_imp_sleib_body(MgElab *e, MgTp *a, MgPf **out) {
    MgTp *prop = mge_ty_prop(e);
    MgTm *db0 = mge_tm_db(e, 0u), *db1 = mge_tm_db(e, 1u), *db2 = mge_tm_db(e, 2u);
    MgTm *h0 = mge_tm_all(e, mge_ty_arr(e, a, prop), mge_tm_imp(e, mge_tm_ap(e, db0, db1), mge_tm_ap(e, db0, db2)));
    MgTm *q = mge_tm_lam(e, a, mge_tm_imp(e, mge_tm_ap(e, mge_tm_ap(e, db1, db0), db2),
                                          mge_tm_ap(e, mge_tm_ap(e, db1, db2), db0)));
    MgPf *inner = pf_pfap(e, pf_tmap(e, pf_hyp(e, 0u), q),
                          pf_plam(e, mge_tm_ap(e, mge_tm_ap(e, db0, db1), db1), pf_hyp(e, 0u)));
    *out = pf_tlam(e, a, pf_tlam(e, a, pf_plam(e, h0, pf_tlam(e, mge_ty_arr(e, a, mge_ty_arr(e, a, prop)), inner))));
    return NULL;
}

/* The proof the script builds: the frames newest first, then the
 * theorem's base (the section's proof closures). */
static MgPf *proof_of_script(MgElab *e) {
    Dlist dl = {0};
    for (const PfFrame *f = e->prooffun; f; f = f->prev)
        apply_frame(e, f, &dl);
    if (dl.len != 1u)
        mge_fail(e, "Bug with proof construction");
    MgPf *d = dl.items[0];
    for (const Clo *c = e->thm_clos; c; c = c->next) {
        if (c->hyp) {
            d = pf_plam(e, c->prop, d);
            continue;
        }
        for (uint32_t i = 0; i < c->count; ++i)
            d = pf_tlam(e, c->tp, d);
    }
    return d;
}

/* ── goals ────────────────────────────────────────────────────────────── */

const PfSt *mge_pf_goal(MgElab *e, MgTm *claim, const Cx *ctxtm, const Hy *ctxpf, const PfSt *next) {
    PfSt *p = PERM(e, PfSt);
    *p = (PfSt){false, 0, false, claim, ctxtm, ctxpf, next};
    return p;
}

static const PfSt *pf_sep(MgElab *e, int32_t j, bool b, const PfSt *next) {
    PfSt *p = PERM(e, PfSt);
    *p = (PfSt){true, j, b, NULL, NULL, NULL, next};
    return p;
}

static const PfSt *structure_pfstate(MgElab *e, int32_t i, const PfSt *goal1, const PfSt *rest) {
    if (rest && !rest->sep) {
        const PfSt *g2 = mge_pf_goal(e, rest->claim, rest->ctxtm, rest->ctxpf, NULL);
        return pf_sep(e, i, true, mge_pf_goal(e, goal1->claim, goal1->ctxtm, goal1->ctxpf,
                                              structure_pfstate(e, i, g2, rest->next)));
    }
    return pf_sep(e, i, true, mge_pf_goal(e, goal1->claim, goal1->ctxtm, goal1->ctxpf,
                                          pf_sep(e, i, false, rest)));
}

static const PfSt *current_goal(MgElab *e, const char *message) {
    const PfSt *g = e->pfstate;
    if (!g || g->sep)
        mge_fail(e, "%s", message);
    return g;
}

/* Persistent copies of a goal's context, extended by tactics. */
static const Cx *cx_perm(MgElab *e, const Cx *next, const char *name, MgTp *tp, MgTm *def) {
    return mge_cx_push(e, next, name, tp, def, true);
}

static const Hy *hy_perm(MgElab *e, const Hy *next, const char *name, MgTm *prop) {
    Hy *h = PERM(e, Hy);
    *h = (Hy){name, prop, next};
    return h;
}

static const Hy *hyps_shift_perm(MgElab *e, const Hy *cxpf) {
    if (!cxpf)
        return NULL;
    Hy *h = PERM(e, Hy);
    const Hy *rest = hyps_shift_perm(e, cxpf->next);
    *h = (Hy){cxpf->name, mge_shift1(e, cxpf->prop), rest};
    return h;
}

/* ── tactics ──────────────────────────────────────────────────────────── */

static void tac_exact(MgElab *e, const MgSourceItem *it) {
    const PfSt *g = current_goal(e, "No current claim");
    MgPf *dpf = check_pf(e, it->a, g->claim, g->ctxtm, g->ctxpf);
    PfFrame *f = new_frame(e, F_EXACT);
    f->d = dpf;
    push_frame(e, f);
    e->pfstate = g->next;
}

/* let x ... and assume H ...: one binder at a time; headnorm only when the
 * claim does not already show the binder. */
static void tac_let(MgElab *e, const MgSourceItem *it) {
    MgTp *btp = it->a ? mge_extract_tp(e, it->a) : NULL;
    const PfSt *g = current_goal(e, "No current claim");
    MgTm *claim = g->claim;
    const Cx *cxtm = g->ctxtm;
    const Hy *cxpf = g->ctxpf;
    for (uint32_t k = 0; k < it->names_len; ++k) {
        const char *x = mge_intern(e, it->names[k]);
        MgTm *c = claim;
        if (c->tag != MG_TM_ALL) {
            const Dl *ignored = e->deltaset;
            c = headnorm(e, c, &ignored);
            if (c->tag != MG_TM_ALL)
                mge_fail(e, "let tactic used with %s when claim is not a universal quantifier", x);
        }
        if (btp && c->tp != btp)
            mge_fail(e, "%s ascribe type %s but the claim is universally quantified over type %s", x,
                     mge_tp_str(e, btp), mge_tp_str(e, c->tp));
        PfFrame *f = new_frame(e, F_TLAM);
        f->a = c->tp;
        push_frame(e, f);
        cxtm = cx_perm(e, cxtm, x, c->tp, NULL);
        cxpf = hyps_shift_perm(e, cxpf);
        claim = c->f;
    }
    e->pfstate = mge_pf_goal(e, claim, cxtm, cxpf, g->next);
}

static void tac_assume(MgElab *e, const MgSourceItem *it) {
    const PfSt *g = current_goal(e, "No current claim");
    MgTm *btm = it->a ? check_tm_goal(e, it->a, mge_ty_prop(e), g->ctxtm) : NULL;
    MgTm *claim = g->claim;
    const Hy *cxpf = g->ctxpf;
    for (uint32_t k = 0; k < it->names_len; ++k) {
        const char *x = mge_intern(e, it->names[k]);
        MgTm *c = claim;
        if (c->tag != MG_TM_IMP) {
            const Dl *ignored = e->deltaset;
            c = headnorm(e, c, &ignored);
            if (c->tag != MG_TM_IMP)
                mge_fail(e, "assume tactic used with %s when claim is not an implication", x);
        }
        MgTm *p1 = c->f;
        if (btm) {
            const Dl *dl = e->deltaset;
            if (!conv(e, p1, btm, &dl))
                mge_fail(e, "%s ascribed prop %s but the antecendent of the claim is %s", x,
                         tm_str(e, btm), tm_str(e, p1));
            e->deltaset = dl;
            p1 = btm;
        }
        PfFrame *f = new_frame(e, F_PLAM);
        f->p = p1;
        push_frame(e, f);
        cxpf = hy_perm(e, cxpf, x, p1);
        claim = c->x;
    }
    e->pfstate = mge_pf_goal(e, claim, g->ctxtm, cxpf, g->next);
}

static void tac_set(MgElab *e, const MgSourceItem *it) {
    const PfSt *g = current_goal(e, "set tactic cannot be used when there is no claim");
    MgTp *atp = it->a ? mge_extract_tp(e, it->a) : NULL;
    MgTm *btm;
    MgTp *btp;
    if (atp) {
        btm = check_tm_goal(e, it->b, atp, g->ctxtm);
        btp = atp;
    } else {
        TmTp b = extract_tm_goal(e, it->b, g->ctxtm);
        btm = b.tm;
        btp = b.tp;
    }
    e->pfstate = mge_pf_goal(e, g->claim, cx_perm(e, g->ctxtm, mge_intern(e, it->name), btp, btm),
                             g->ctxpf, g->next);
}

static void tac_claim(MgElab *e, const MgSourceItem *it) {
    const PfSt *g = current_goal(e, "claim tactic cannot be used when there is no claim");
    MgTm *atm = check_tm_goal(e, it->a, mge_ty_prop(e), g->ctxtm);
    const PfSt *rest = mge_pf_goal(e, g->claim, g->ctxtm, hy_perm(e, g->ctxpf, mge_intern(e, it->name), atm), g->next);
    e->pfstate = mge_pf_goal(e, atm, g->ctxtm, g->ctxpf, rest);
    PfFrame *f = new_frame(e, F_CLAIM);
    f->p = atm;
    push_frame(e, f);
}

static void tac_prove(MgElab *e, const MgSourceItem *it) {
    if (it->terms_len)
        mge_fail(e, "prove tactic can currently only be used with one proposition");
    const PfSt *g = current_goal(e, "prove tactic cannot be used when there is no claim");
    MgTm *atm = check_tm_goal(e, it->a, mge_ty_prop(e), g->ctxtm);
    const Dl *dl = e->deltaset;
    if (conv(e, g->claim, atm, &dl)) {
        e->deltaset = dl;
    } else {
        dl = e->deltaset;
        if (conv(e, mge_tm_h(e, e->fal), atm, &dl)) {
            e->deltaset = dl;
            PfFrame *f = new_frame(e, F_FALSEE);
            f->m = g->claim;
            push_frame(e, f);
        } else {
            dl = e->deltaset;
            MgTm *old = mge_tm_all(e, mge_ty_prop(e), mge_tm_db(e, 0u));
            if (!conv(e, old, atm, &dl))
                mge_fail(e, "Proposition given with prove tactic does not match the current claim.\n%s\n%s",
                         tm_str(e, g->claim), tm_str(e, atm));
            e->deltaset = dl;
            PfFrame *f = new_frame(e, F_OLDFALSEE);
            f->m = g->claim;
            push_frame(e, f);
        }
    }
    e->pfstate = mge_pf_goal(e, atm, g->ctxtm, g->ctxpf, g->next);
}

typedef struct {
    const MgSourceItem *it;
    const PfSt *g;
} WitnessTry;

static void witness_body(MgElab *e, void *ctx);

static void tac_witness(MgElab *e, const MgSourceItem *it) {
    WitnessTry t = {it, current_goal(e, "witness tactic cannot be used when there is no claim")};
    int k = mge_try_run(e, witness_body, &t);
    if (k == RAISE_NOT_FOUND)
        mge_fail(e, "witness tactic can only be used when claim is existential");
    if (k)
        mge_raise_kind(e, k);
}

static void witness_body(MgElab *e, void *ctx) {
    WitnessTry *t = ctx;
    const MgSourceItem *it = t->it;
    const PfSt *g = t->g;
    MgTm *c = g->claim;
    /* extract_exclaim: Ap (TpAp (TmH ex, etp), ep) */
    if (!(c->tag == MG_TM_AP && c->f->tag == MG_TM_TAP && c->f->f->tag == MG_TM_NAME &&
          c->f->f->n == e->expoly))
        mge_not_found(e);
    MgTp *etp = c->f->tp;
    MgTm *ep = c->x;
    MgTm *atm = check_tm_goal(e, it->a, etp, g->ctxtm);
    MgTm *epatm = ep->tag == MG_TM_LAM ? tmsubst(e, ep->f, 0u, atm) : mge_tm_ap(e, ep, atm);
    e->pfstate = mge_pf_goal(e, epatm, g->ctxtm, g->ctxpf, g->next);
    PfFrame *f = new_frame(e, e->expolyIknown ? F_WITNESS_I : F_WITNESS);
    f->a = etp;
    f->m = ep;
    f->n = atm;
    push_frame(e, f);
}

/* foapplyf / forewritef's descent through a known's premises. */
typedef struct {
    const Mt **margs;      /* newest first */
    uint32_t margs_len, margs_cap;
    const Mt **subclaims;  /* newest first */
    uint32_t sub_len, sub_cap;
    const Step *steps;
    const Mt **sigma;
    uint32_t sigma_len;
    uint32_t n;
} Descent;

static void push_mt(MgElab *e, const Mt ***v, uint32_t *len, uint32_t *cap, const Mt *m) {
    if (*len == *cap) {
        uint32_t c = *cap ? *cap * 2u : 16u;
        const Mt **g = mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * c);
        if (*len)
            memcpy(g, *v, sizeof(Mt *) * *len);
        *v = g;
        *cap = c;
    }
    (*v)[(*len)++] = m;
}

static const Step *step(MgElab *e, uint8_t kind, MgTm *defelt, const Step *prev) {
    Step *s = PERM(e, Step);
    *s = (Step){kind, defelt, prev};
    return s;
}

static const Mt **db_sigma(MgElab *e, uint32_t from, uint32_t count, const Mt **tail, uint32_t tail_len,
                           int32_t tail_shift, uint32_t *len) {
    const Mt **s = mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (count + tail_len + 1u));
    for (uint32_t j = 0; j < count; ++j)
        s[j] = mt_db(e, from + j);
    for (uint32_t j = 0; j < tail_len; ++j)
        s[count + j] = tail_shift ? mtm_shift(e, 0u, tail_shift, tail[j]) : tail[j];
    *len = count + tail_len;
    return s;
}

/* One step down: an implication's premise becomes a subclaim, a
 * quantifier a metavariable (or Eps_ : a1, False where vacuous); else beta
 * or delta reduce the head, or give up (Not_found). */
static const Mt *descend(MgElab *e, Descent *d, const Mt *p, const char *nonequation) {
    if (p->tag == M_IMP) {
        push_mt(e, &d->subclaims, &d->sub_len, &d->sub_cap, p->x);
        d->steps = step(e, S_PFAP, NULL, d->steps);
        return p->y;
    }
    if (p->tag == M_ALL) {
        int64_t l = minap_r(p->x, 0u, 0, -1);
        MgTp *a1 = p->a;
        if (l < 0) {
            MgTm *defelt = mge_tm_ap(e, mge_tm_tap(e, mge_tm_prim(e, 0u), a1),
                                     mge_tm_lam(e, a1, mge_tm_all(e, mge_ty_prop(e), mge_tm_db(e, 0u))));
            d->steps = step(e, S_TMAP_DEFAULT, defelt, d->steps);
            /* the body still mentions no de Bruijn 0 */
            return p->x;
        }
        const Mt *mvar;
        if (l == 0) {
            mvar = mt_var(e, d->n, d->sigma, d->sigma_len);
        } else {
            uint32_t len;
            const Mt **sigmal = db_sigma(e, 0u, (uint32_t)l, d->sigma, d->sigma_len, (int32_t)l, &len);
            const Mt *m = mt_var(e, d->n, sigmal, len);
            MgTp *a = a1;
            const Mt **lams = mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (size_t)l);
            MgTp **tps = mge_chunk_alloc(e, &e->tmp, sizeof(MgTp *) * (size_t)l);
            for (int64_t i = 0; i < l; ++i) {
                if (a->tag != MG_TP_ARR)
                    mge_fail(e, "Type Error found while attempting apply tactic");
                tps[i] = a->a;
                a = a->b;
            }
            (void)lams;
            for (int64_t i = l; i-- > 0;)
                m = mt(e, M_LAM, 0, tps[i], m, NULL);
            mvar = m;
        }
        /* mtm_ssub (mvar :: sigma) p2 */
        Tau tau = {mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (d->sigma_len + 1u)), d->sigma_len + 1u};
        tau.tau[0] = mvar;
        for (uint32_t i = 0; i < d->sigma_len; ++i)
            tau.tau[i + 1u] = d->sigma[i];
        const Mt *p3 = mtm_ssub(e, tau, p->x);
        if (l > 0)
            p3 = betared_if(e, p3, true);
        push_mt(e, &d->margs, &d->margs_len, &d->margs_cap, mvar);
        d->steps = step(e, S_TMAP_META, NULL, d->steps);
        ++d->n;
        return p3;
    }
    const Mt *p1 = betared_if(e, p, false);
    if (!mt_eq(p, p1))
        return p1;
    const Dl *del = e->deltaset;
    const Mt *p2 = mheadnorm(e, p, &del);
    if (mt_eq(p, p2)) {
        if (nonequation)
            mge_fail(e, "%s", nonequation);
        mge_not_found(e);
    }
    e->deltaset = del;
    return p2;
}

static const Mt **goal_sigma(MgElab *e, const Cx *cxtm, uint32_t *len) {
    uint32_t n = mge_cx_vars(cxtm);
    const Mt **s = mge_chunk_alloc(e, &e->tmp, sizeof(Mt *) * (n ? n : 1u));
    for (uint32_t j = 0; j < n; ++j)
        s[j] = mt_db(e, j);
    *len = n;
    return s;
}

/* The subclaims, earliest premise first, and the terms the metavariables
 * received (newest first), once matched under theta. */
static void ground(MgElab *e, Descent *d, const Theta *theta, MgTm ***subs, MgTm ***ml) {
    *ml = d->margs_len ? mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * d->margs_len) : NULL;
    for (uint32_t i = 0; i < d->margs_len; ++i)
        (*ml)[i] = mtm_to_tm_or_not_found(e, mtm_msub(e, theta, d->margs[d->margs_len - 1u - i]));
    *subs = d->sub_len ? mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * d->sub_len) : NULL;
    /* subclaims newest first; the reversal puts the earliest premise first */
    for (uint32_t i = 0; i < d->sub_len; ++i)
        (*subs)[i] = mtm_to_tm_or_not_found(e, mtm_msub(e, theta, d->subclaims[i]));
}

static MgTm **perm_terms(MgElab *e, MgTm **v, uint32_t n) {
    if (!n)
        return NULL;
    MgTm **p = mge_chunk_alloc(e, &e->perm, sizeof(MgTm *) * n);
    memcpy(p, v, sizeof(MgTm *) * n);
    return p;
}

/* The subclaims become goals: the first is current; the rest follow. */
static void push_subgoals(MgElab *e, const PfSt *g, MgTm **subs, uint32_t n) {
    const PfSt *rest = g->next;
    for (uint32_t i = n; i-- > 0;)
        rest = mge_pf_goal(e, subs[i], g->ctxtm, g->ctxpf, rest);
    e->pfstate = rest;
}

typedef struct {
    const MgSourceItem *it;
} ApplyTry;

static void apply_body(MgElab *e, void *ctx) {
    ApplyTry *t = ctx;
    const PfSt *g = current_goal(e, "apply tactic cannot be used when there is no claim");
    PfTm a = extract_pf(e, t->it->a, g->ctxtm, g->ctxpf);
    Descent d = {0};
    d.sigma = goal_sigma(e, g->ctxtm, &d.sigma_len);
    const Mt *p = tm_to_mtm(e, a.tm);
    for (;;) {
        const Theta *theta = NULL;
        if (pattern_match(e, p, g->claim, &theta)) {
            /* subclaims earliest premise first; ml newest first, as apff
             * consumes it from the outermost step */
            MgTm **subs, **ml;
            ground(e, &d, theta, &subs, &ml);
            PfFrame *f = new_frame(e, F_APPLY);
            f->base = a.pf;
            f->steps = d.steps;
            f->ml = perm_terms(e, ml, d.margs_len);
            f->ml_len = d.margs_len;
            f->nsubs = d.sub_len;
            push_frame(e, f);
            push_subgoals(e, g, subs, d.sub_len);
            return;
        }
        p = descend(e, &d, p, NULL);
    }
}

static void tac_apply(MgElab *e, const MgSourceItem *it) {
    ApplyTry t = {it};
    int k = mge_try_run(e, apply_body, &t);
    if (k == 0)
        return;
    if (k == RAISE_NOT_FOUND)
        mge_fail(e, "apply does not match the current claim");
    mge_raise_kind(e, k);
}

/* ── rewrite ──────────────────────────────────────────────────────────── */

typedef struct {
    MgElab *e;
    const MgSourceItem *it;
    uint32_t posr;
} Rw;

static bool inpos(const Rw *rw, uint32_t i) {
    if (!rw->it->positions_len)
        return true;
    for (uint32_t k = 0; k < rw->it->positions_len; ++k)
        if ((uint64_t)rw->it->positions[k] == i)
            return true;
    return false;
}

static MgTm *forewritef5(Rw *rw, uint32_t z, MgTm *etmi, MgTm *m);

static MgTm *forewritef4(Rw *rw, uint32_t z, MgTm *etmi, MgTm *m) {
    if (m == etmi) {
        ++rw->posr;
        if (inpos(rw, rw->posr))
            return mge_tm_db(rw->e, z);
    }
    return forewritef5(rw, z, etmi, m);
}

static MgTm *forewritef5(Rw *rw, uint32_t z, MgTm *etmi, MgTm *m) {
    MgElab *e = rw->e;
    switch (m->tag) {
    case MG_TM_AP: case MG_TM_IMP: {
        MgTm *a = forewritef4(rw, z, etmi, m->f);
        MgTm *b = forewritef4(rw, z, etmi, m->x);
        return m->tag == MG_TM_AP ? mge_tm_ap(e, a, b) : mge_tm_imp(e, a, b);
    }
    case MG_TM_LAM: case MG_TM_ALL: {
        MgTm *b = forewritef4(rw, z + 1u, mge_shift1(e, etmi), m->f);
        return m->tag == MG_TM_LAM ? mge_tm_lam(e, m->tp, b) : mge_tm_all(e, m->tp, b);
    }
    case MG_TM_DB:
        return m->n >= z ? mge_tm_db(e, m->n + 1u) : m;
    default:
        return m;
    }
}

typedef struct {
    MgTm *leib;
    const Theta *theta;
} Found;

/* forewritef2 / forewritef3: the leftmost-outermost instance of the
 * equation's side, at the requested positions; false for Not_found. */
static bool forewritef3(Rw *rw, uint32_t z, uint32_t n, const Mt *etm, MgTm *m, Found *out);

static bool forewritef2(Rw *rw, uint32_t z, uint32_t n, const Mt *etm, MgTm *m, Found *out) {
    MgElab *e = rw->e;
    const Theta *theta = NULL;
    if (pattern_match(e, etm, m, &theta)) {
        bool all = true;
        for (uint32_t i = 0; i < n; ++i)
            if (!theta_get(theta, i))
                all = false;
        if (all) {
            ++rw->posr;
            if (inpos(rw, rw->posr)) {
                out->leib = mge_tm_db(e, z);
                out->theta = theta;
                return true;
            }
        }
    }
    return forewritef3(rw, z, n, etm, m, out);
}

static bool forewritef3(Rw *rw, uint32_t z, uint32_t n, const Mt *etm, MgTm *m, Found *out) {
    MgElab *e = rw->e;
    switch (m->tag) {
    case MG_TM_AP: case MG_TM_IMP: {
        Found f1;
        if (forewritef2(rw, z, n, etm, m->f, &f1)) {
            MgTm *inst = mtm_to_tm(e, mtm_msub(e, f1.theta, etm));
            if (inst) {
                MgTm *b = forewritef4(rw, z, inst, m->x);
                out->leib = m->tag == MG_TM_AP ? mge_tm_ap(e, f1.leib, b) : mge_tm_imp(e, f1.leib, b);
                out->theta = f1.theta;
                return true;
            }
        }
        Found f2;
        if (!forewritef2(rw, z, n, etm, m->x, &f2))
            return false;
        MgTm *a = mge_tmshift(e, z, 1, m->f);
        out->leib = m->tag == MG_TM_AP ? mge_tm_ap(e, a, f2.leib) : mge_tm_imp(e, a, f2.leib);
        out->theta = f2.theta;
        return true;
    }
    case MG_TM_LAM: case MG_TM_ALL: {
        Found f1;
        if (!forewritef2(rw, z + 1u, n, mtm_shift(e, 0u, 1, etm), m->f, &f1))
            return false;
        out->leib = m->tag == MG_TM_LAM ? mge_tm_lam(e, m->tp, f1.leib) : mge_tm_all(e, m->tp, f1.leib);
        out->theta = f1.theta;
        return true;
    }
    default:
        return false;
    }
}

typedef struct {
    const Mt *m;
    const Mt *out;
} MtShiftTry;

static void mt_unshift_body(MgElab *e, void *ctx) {
    MtShiftTry *t = ctx;
    t->out = mtm_shift(e, 0u, -1, t->m);
}

/* destruct_leibeq: eq at a type, or its unfolding */
static bool destruct_leibeq(MgElab *e, const Mt *p, MgTp **etp, const Mt **l, const Mt **r) {
    if (p->tag == M_AP && p->x->tag == M_AP && p->x->x->tag == M_TPAP && p->x->x->x->tag == M_TMH &&
        p->x->x->x->n == e->eqPoly) {
        *etp = p->x->x->a;
        *l = p->x->y;
        *r = p->y;
        return true;
    }
    if (p->tag == M_ALL && p->a->tag == MG_TP_ARR && p->a->b == mge_ty_prop(e) && p->x->tag == M_IMP &&
        p->x->x->tag == M_AP && p->x->x->x->tag == M_DB && p->x->x->x->n == 0u &&
        p->x->y->tag == M_AP && p->x->y->x->tag == M_DB && p->x->y->x->n == 0u) {
        /* mtm_shift 0 (-1); NegDB: not an equation here */
        MtShiftTry tl = {p->x->x->y, NULL}, tr = {p->x->y->y, NULL};
        int k = mge_try_run(e, mt_unshift_body, &tl);
        if (k == RAISE_NEGDB)
            return false;
        if (k)
            mge_raise_kind(e, k);
        k = mge_try_run(e, mt_unshift_body, &tr);
        if (k == RAISE_NEGDB)
            return false;
        if (k)
            mge_raise_kind(e, k);
        *etp = p->a->a;
        *l = tl.out;
        *r = tr.out;
        return true;
    }
    return false;
}

typedef struct {
    const MgSourceItem *it;
} RewriteTry;

static void rewrite_body(MgElab *e, void *ctx) {
    RewriteTry *t = ctx;
    const PfSt *g = current_goal(e, "rewrite tactic cannot be used when there is no claim");
    PfTm a = extract_pf(e, t->it->a, g->ctxtm, g->ctxpf);
    Descent d = {0};
    d.sigma = goal_sigma(e, g->ctxtm, &d.sigma_len);
    const Mt *p = tm_to_mtm(e, a.tm);
    bool sym = t->it->flag;
    for (;;) {
        MgTp *etp;
        const Mt *ltm, *rtm;
        if (destruct_leibeq(e, p, &etp, &ltm, &rtm)) {
            const Mt *tm1 = sym ? rtm : ltm;
            const Mt *tm2 = sym ? ltm : rtm;
            Rw rw = {e, t->it, 0u};
            Found found;
            if (!forewritef2(&rw, 0u, d.n, tm1, g->claim, &found))
                mge_fail(e, "rewrite tactic failed");
            MgTm *tm2i = mtm_to_tm_or_not_found(e, mtm_msub(e, found.theta, tm2));
            MgTm **subs, **ml;
            ground(e, &d, found.theta, &subs, &ml);
            uint32_t nsubs = d.sub_len + 1u;
            MgTm **ordered = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * nsubs);
            ordered[0] = tmsubst(e, found.leib, 0u, tm2i);
            for (uint32_t i = 0; i < d.sub_len; ++i)
                ordered[i + 1u] = subs[i];
            PfFrame *f = new_frame(e, F_REWRITE);
            f->base = a.pf;
            f->steps = d.steps;
            f->ml = perm_terms(e, ml, d.margs_len);
            f->ml_len = d.margs_len;
            f->nsubs = nsubs;
            f->sym = sym;
            f->leibq = found.leib;
            f->etp = etp;
            push_frame(e, f);
            push_subgoals(e, g, ordered, nsubs);
            return;
        }
        p = descend(e, &d, p, "rewrite tactic given a proof of a non-equation");
    }
}

static void tac_rewrite(MgElab *e, const MgSourceItem *it) {
    RewriteTry t = {it};
    int k = mge_try_run(e, rewrite_body, &t);
    if (k == 0)
        return;
    if (k == RAISE_NOT_FOUND)
        mge_fail(e, "rewrite tactic failed");
    mge_raise_kind(e, k);
}

/* ── the equality tactics ─────────────────────────────────────────────── */

static bool is_equation(MgElab *e, MgTm *c, MgTp **a, MgTm **l, MgTm **r) {
    if (!(c->tag == MG_TM_AP && c->f->tag == MG_TM_AP && c->f->f->tag == MG_TM_TAP &&
          c->f->f->f->tag == MG_TM_NAME && strcmp(mge_name_text(e, c->f->f->f->n), HEX_EQ) == 0))
        return false;
    *a = c->f->f->tp;
    *l = c->f->x;
    *r = c->x;
    return true;
}

/* ── distinct (megalodon.ml SpecialTac "distinct") ────────────────────── */

/* A named constant of the tactic: as a name id. */
static uint32_t dconst(MgElab *e, const char *name) {
    const char *h = mg_upstream_distinct(name);
    if (!h)
        mge_fail(e, "distinct: no constant %s", name);
    return mge_name_id(e, h);
}

static MgPf *dknown(MgElab *e, const char *name) { return pf_known(e, dconst(e, name)); }
static MgTm *dtmh(MgElab *e, const char *name) { return mge_tm_h(e, dconst(e, name)); }
static bool dis(MgElab *e, const MgTm *m, const char *name) {
    return m->tag == MG_TM_NAME && m->n == dconst(e, name);
}

static MgTm *eq_set(MgElab *e, MgTm *a, MgTm *b) {
    MgTm *eq = mge_tm_tap(e, mge_tm_h(e, mge_name_id(e, HEX_EQ)), mge_ty_set(e));
    return mge_tm_ap(e, mge_tm_ap(e, eq, a), b);
}

/* HOAS: nil, pair and bind */
static bool hoas_pair_p(MgElab *e, const MgTm *m, MgTm **a, MgTm **b) {
    if (m->tag == MG_TM_AP && m->f->tag == MG_TM_AP && m->f->f->tag == MG_TM_NAME &&
        strcmp(mge_name_text(e, m->f->f->n), "d58762d200971dcc7f1850726d9f2328403127deeba124fc3ba2d2d9f7c3cb8c") == 0) {
        *a = m->f->x;
        *b = m->x;
        return true;
    }
    return false;
}

static bool hoas_bind_p(MgElab *e, const MgTm *m, MgTm **body) {
    if (m->tag == MG_TM_AP && m->f->tag == MG_TM_NAME &&
        strcmp(mge_name_text(e, m->f->n), "73c9efe869770ab42f7cde0b33fe26bbc3e2bd157dad141c0c27d1e7348d60f5") == 0) {
        *body = m->x;
        return true;
    }
    return false;
}

static bool hoas_nil_p(MgElab *e, const MgTm *m) {
    return m->tag == MG_TM_NAME &&
           strcmp(mge_name_text(e, m->n), "f2f23aaed49035f6a02316839e024937b84f34a64984a739b9926cab5a542a5f") == 0;
}

static MgPf *tm2(MgElab *e, MgPf *k, MgTm *a, MgTm *b) { return pf_tmap(e, pf_tmap(e, k, a), b); }

static MgPf *distinct_hoas_2(MgElab *e, MgTm *m, MgTm *n, MgPf *d) {
    MgTp *set = mge_ty_set(e);
    MgTm *a, *b, *c, *x;
    for (;;) {
        if (hoas_nil_p(e, m)) {
            if (hoas_pair_p(e, n, &a, &b))
                return pf_pfap(e, tm2(e, dknown(e, "neq_nil_pair"), a, b), d);
            if (hoas_bind_p(e, n, &x) && x->tag == MG_TM_LAM && x->tp == set) {
                MgTm *body = x->f;
                if (hoas_nil_p(e, body))
                    return pf_pfap(e, dknown(e, "neq_nil_bind_nil"), d);
                if (hoas_pair_p(e, body, &a, &b))
                    return pf_pfap(e, tm2(e, dknown(e, "neq_nil_bind_pair"), mge_tm_lam(e, set, a), mge_tm_lam(e, set, b)), d);
                if (hoas_bind_p(e, body, &c))
                    return pf_pfap(e, pf_tmap(e, dknown(e, "neq_nil_bind_bind"), mge_tm_lam(e, set, c)), d);
            }
            mge_fail(e, "distinct failed nil vs ?");
        }
        MgTm *m1, *m2;
        if (hoas_pair_p(e, m, &m1, &m2)) {
            MgTm *n1, *n2;
            if (hoas_nil_p(e, n))
                return pf_pfap(e, tm2(e, dknown(e, "neq_pair_nil"), m1, m2), d);
            if (hoas_pair_p(e, n, &n1, &n2)) {
                const char *inj = m1 == n1 ? "pair_inj_2" : "pair_inj_1";
                d = pf_pfap(e, tm2(e, tm2(e, dknown(e, inj), m1, m2), n1, n2), d);
                if (m1 == n1) {
                    m = m2;
                    n = n2;
                } else {
                    m = m1;
                    n = n1;
                }
                continue;
            }
            if (hoas_bind_p(e, n, &n1))
                return pf_pfap(e, pf_tmap(e, tm2(e, dknown(e, "pair_not_bind"), m1, m2), n1), d);
            mge_fail(e, "distinct failed pair vs ?");
        }
        MgTm *mb;
        if (hoas_bind_p(e, m, &mb) && mb->tag == MG_TM_LAM && mb->tp == set) {
            MgTm *m1b = mb->f;
            MgTm *n1, *n2;
            if (hoas_nil_p(e, n)) {
                if (hoas_nil_p(e, m1b))
                    return pf_pfap(e, dknown(e, "neq_bind_nil_nil"), d);
                if (hoas_pair_p(e, m1b, &a, &b))
                    return pf_pfap(e, tm2(e, dknown(e, "neq_bind_pair_nil"), mge_tm_lam(e, set, a), mge_tm_lam(e, set, b)), d);
                if (hoas_bind_p(e, m1b, &c))
                    return pf_pfap(e, pf_tmap(e, dknown(e, "neq_bind_bind_nil"), mge_tm_lam(e, set, c)), d);
                mge_fail(e, "distinct failed bind ? vs nil");
            }
            if (hoas_pair_p(e, n, &n1, &n2))
                return pf_pfap(e, pf_tmap(e, tm2(e, dknown(e, "bind_not_pair"), mb, n1), n2), d);
            MgTm *nb;
            if (hoas_bind_p(e, n, &nb) && nb->tag == MG_TM_LAM && nb->tp == set) {
                MgTm *niltm = mge_tm_h(e, mge_name_id(e, "f2f23aaed49035f6a02316839e024937b84f34a64984a739b9926cab5a542a5f"));
                MgTm *pairh = mge_tm_h(e, mge_name_id(e, "d58762d200971dcc7f1850726d9f2328403127deeba124fc3ba2d2d9f7c3cb8c"));
                MgTm *nil2 = mge_tm_ap(e, mge_tm_ap(e, pairh, niltm), niltm);
                MgTm *m1c = tmsubst(e, m1b, 0u, niltm);
                MgTm *n1c = tmsubst(e, nb->f, 0u, niltm);
                MgTm *arg = niltm;
                if (m1c == n1c) {
                    m1c = tmsubst(e, m1b, 0u, nil2);
                    n1c = tmsubst(e, nb->f, 0u, nil2);
                    if (m1c == n1c)
                        mge_fail(e, "distinct failed in an impossible way");
                    arg = nil2;
                }
                d = pf_pfap(e, pf_tmap(e, tm2(e, dknown(e, "bind_inj_ap"), mb, nb), arg), d);
                m = m1c;
                n = n1c;
                continue;
            }
            mge_fail(e, "distinct failed bind vs ?");
        }
        mge_fail(e, "distinct failed ? vs _");
    }
}

/* Egal: unary numerals, sequences of them, sequences of those */
static MgTm *unary_nat(MgElab *e, uint64_t m) {
    MgTm *r = dtmh(e, "empty");
    MgTm *s = dtmh(e, "ordsucc");
    for (uint64_t i = 0; i < m; ++i)
        r = mge_tm_ap(e, s, r);
    return r;
}

static MgPf *omega_nat(MgElab *e, uint64_t m) {
    if (m > 9u)
        return pf_pfap(e, pf_tmap(e, dknown(e, "omega_ordsucc"), unary_nat(e, m - 1u)), omega_nat(e, m - 1u));
    char name[16];
    snprintf(name, sizeof name, "omega_%u", (unsigned)m);
    return dknown(e, name);
}

static bool unary_nat_p(MgElab *e, const MgTm *m) {
    for (;;) {
        if (dis(e, m, "empty"))
            return true;
        if (m->tag == MG_TM_AP && dis(e, m->f, "ordsucc")) {
            m = m->x;
            continue;
        }
        return false;
    }
}

static bool seq_over_p(MgElab *e, int level, const MgTm *m) {
    for (;;) {
        if (dis(e, m, "empty"))
            return true;
        if (m->tag == MG_TM_AP && m->f->tag == MG_TM_AP && dis(e, m->f->f, "seqcons")) {
            bool ok = level == 0 ? unary_nat_p(e, m->f->x) : seq_over_p(e, level - 1, m->f->x);
            if (!ok)
                return false;
            m = m->x;
            continue;
        }
        return false;
    }
}

static uint64_t eval_unary_nat(MgElab *e, const MgTm *m) {
    uint64_t n = 0;
    for (;;) {
        if (dis(e, m, "empty"))
            return n;
        if (m->tag == MG_TM_AP && dis(e, m->f, "ordsucc")) {
            ++n;
            m = m->x;
            continue;
        }
        mge_fail(e, "distinct failed");
    }
}

static const char *unary_nat_special_case(MgElab *e, uint64_t m, uint64_t n) {
    if (m >= 1u && m <= 9u && n < m) {
        static char name[16];
        snprintf(name, sizeof name, "neq_%u_%u", (unsigned)m, (unsigned)n);
        return name;
    }
    mge_fail(e, "distinct failed");
}

static MgPf *distinct_unary_r(MgElab *e, MgTm *m, uint64_t m1, MgTm *n, uint64_t n1) {
    if (m1 == 0u && n1 == 1u)
        return dknown(e, "neq_0_1");
    if (m1 > n1 && m1 < 10u)
        return dknown(e, unary_nat_special_case(e, m1, n1));
    if (n1 > m1 && n1 < 10u)
        return pf_pfap(e, tm2(e, dknown(e, "neq_i_sym"), n, m), dknown(e, unary_nat_special_case(e, n1, m1)));
    if (m->tag == MG_TM_AP && n->tag == MG_TM_AP) {
        MgPf *inner = distinct_unary_r(e, m->x, m1 - 1u, n->x, n1 - 1u);
        return pf_pfap(e, tm2(e, dknown(e, "ordsucc_inj_contra"), m->x, n->x), inner);
    }
    if (dis(e, m, "empty") && n->tag == MG_TM_AP)
        return pf_tmap(e, dknown(e, "neq_0_ordsucc"), n->x);
    if (m->tag == MG_TM_AP && dis(e, n, "empty"))
        return pf_tmap(e, dknown(e, "neq_ordsucc_0"), m->x);
    mge_fail(e, "distinct failed");
}

/* d (a proof of x <> y) and dne (a proof of x <> 0) at a level: 0 for
 * unary numerals, k for sequences over level k-1. */
static MgPf *distinct_level(MgElab *e, int level, MgTm *m, MgTm *n);

static MgPf *distinct_ne(MgElab *e, int level, MgTm *m) {
    if (level == 0) {
        uint64_t m1 = eval_unary_nat(e, m);
        return distinct_unary_r(e, m, m1, dtmh(e, "empty"), 0u);
    }
    /* distinct_egal_seq_over_ne dne m */
    if (!(m->tag == MG_TM_AP && m->f->tag == MG_TM_AP && dis(e, m->f->f, "seqcons")))
        mge_fail(e, "distinct failed");
    MgTm *x = m->f->x;
    MgPf *dnex = distinct_ne(e, level - 1, x);
    MgPf *dm0x;
    /* seq_at_eq m 0 x */
    dm0x = tm2(e, dknown(e, "seq_cons_0"), x, m->x);
    MgPf *k = pf_tmap(e, tm2(e, dknown(e, "neq0_ap0"), m, dtmh(e, "empty")), x);
    return pf_pfap(e, pf_pfap(e, k, dm0x), dnex);
}

static MgPf *seq_at_eq(MgElab *e, MgTm *m, uint64_t i, MgTm *x) {
    if (!(m->tag == MG_TM_AP && m->f->tag == MG_TM_AP && dis(e, m->f->f, "seqcons")))
        mge_fail(e, "distinct failed");
    MgTm *y = m->f->x, *mr = m->x;
    if (i > 0u) {
        MgPf *rest = seq_at_eq(e, mr, i - 1u, x);
        MgPf *om = omega_nat(e, i - 1u);
        MgPf *k = pf_tmap(e, pf_tmap(e, tm2(e, dknown(e, "seq_cons_S_tra"), y, mr), x), unary_nat(e, i - 1u));
        return pf_pfap(e, pf_pfap(e, k, om), rest);
    }
    return tm2(e, dknown(e, "seq_cons_0"), x, mr);
}

static MgPf *seq_at_eq_0(MgElab *e, MgTm *m, uint64_t i) {
    if (dis(e, m, "empty"))
        return pf_tmap(e, dknown(e, "ap_const_0"), unary_nat(e, i));
    if (m->tag == MG_TM_AP && m->f->tag == MG_TM_AP && dis(e, m->f->f, "seqcons")) {
        if (i > 0u) {
            MgTm *x = m->f->x, *mr = m->x;
            MgPf *d = seq_at_eq_0(e, mr, i - 1u);
            MgPf *k = pf_tmap(e, pf_tmap(e, tm2(e, dknown(e, "seq_cons_S_tra"), x, mr), dtmh(e, "empty")),
                              unary_nat(e, i - 1u));
            return pf_pfap(e, pf_pfap(e, k, omega_nat(e, i - 1u)), d);
        }
        mge_fail(e, "distinct failed");
    }
    mge_fail(e, "distinct failed");
}

static MgPf *distinct_level(MgElab *e, int level, MgTm *m, MgTm *n) {
    if (level == 0)
        return distinct_unary_r(e, m, eval_unary_nat(e, m), n, eval_unary_nat(e, n));
    /* distinguishing_elt m n 0 */
    uint64_t i = 0;
    MgTm *mm = m, *nn = n, *x = NULL, *y = NULL;
    for (;;) {
        if (mm->tag == MG_TM_AP && mm->f->tag == MG_TM_AP && nn->tag == MG_TM_AP && nn->f->tag == MG_TM_AP) {
            if (mm->f->x == nn->f->x) {
                mm = mm->x;
                nn = nn->x;
                ++i;
                continue;
            }
            x = mm->f->x;
            y = nn->f->x;
            break;
        }
        if (mm->tag == MG_TM_AP && mm->f->tag == MG_TM_AP && mm->f->f->tag == MG_TM_NAME && dis(e, nn, "empty"))
            x = mm->f->x;
        else if (dis(e, mm, "empty") && nn->tag == MG_TM_AP && nn->f->tag == MG_TM_AP && nn->f->f->tag == MG_TM_NAME)
            y = nn->f->x;
        break;
    }
    MgTp *set = mge_ty_set(e);
    MgTm *aph = dtmh(e, "ap");
    MgTm *ui = unary_nat(e, i);
    MgPf *h0 = pf_hyp(e, 0u);
    if (x && y) {
        MgPf *dmix = seq_at_eq(e, m, i, x);
        MgPf *dniy = seq_at_eq(e, n, i, y);
        MgPf *ddif = distinct_level(e, level - 1, x, y);
        MgTm *l1 = mge_tm_lam(e, set, mge_tm_lam(e, set, eq_set(e, mge_tm_ap(e, mge_tm_ap(e, aph, mge_tm_db(e, 1u)), ui), x)));
        MgTm *l2 = mge_tm_lam(e, set, mge_tm_lam(e, set, eq_set(e, mge_tm_db(e, 1u), y)));
        MgPf *body = pf_pfap(e, ddif, pf_pfap(e, pf_tmap(e, pf_pfap(e, pf_tmap(e, h0, l1), dmix), l2), dniy));
        return pf_plam(e, eq_set(e, m, n), body);
    }
    if (x) {
        MgPf *dmix = seq_at_eq(e, m, i, x);
        MgPf *dni0 = seq_at_eq_0(e, n, i);
        MgPf *dnex = distinct_ne(e, level - 1, x);
        MgTm *l1 = mge_tm_lam(e, set, mge_tm_lam(e, set, eq_set(e, mge_tm_ap(e, mge_tm_ap(e, aph, mge_tm_db(e, 1u)), ui), x)));
        MgTm *l2 = mge_tm_lam(e, set, mge_tm_lam(e, set, eq_set(e, mge_tm_db(e, 1u), dtmh(e, "empty"))));
        MgPf *body = pf_pfap(e, dnex, pf_pfap(e, pf_tmap(e, pf_pfap(e, pf_tmap(e, h0, l1), dmix), l2), dni0));
        return pf_plam(e, eq_set(e, m, n), body);
    }
    if (y) {
        MgPf *dmi0 = seq_at_eq_0(e, m, i);
        MgPf *dniy = seq_at_eq(e, n, i, y);
        MgPf *dney = distinct_ne(e, level - 1, y);
        MgTm *l1 = mge_tm_lam(e, set, mge_tm_lam(e, set, eq_set(e, mge_tm_ap(e, mge_tm_ap(e, aph, mge_tm_db(e, 0u)), ui), y)));
        MgTm *l2 = mge_tm_lam(e, set, mge_tm_lam(e, set, eq_set(e, mge_tm_db(e, 1u), dtmh(e, "empty"))));
        MgPf *body = pf_pfap(e, dney, pf_pfap(e, pf_tmap(e, pf_pfap(e, pf_tmap(e, h0, l1), dniy), l2), dmi0));
        return pf_plam(e, eq_set(e, m, n), body);
    }
    mge_fail(e, "distinct failed");
}

static void tac_distinct(MgElab *e) {
    if (e->theory != MG_PROFILE_HOAS && e->theory != MG_PROFILE_EGAL)
        mge_fail(e, "distinct tactic is only implemented for HOAS and Egal");
    const PfSt *g = e->pfstate;
    if (!g || g->sep)
        mge_fail(e, "distinct tactic requires a goal");
    MgTm *c = g->claim, *m = NULL, *n = NULL;
    MgTp *set = mge_ty_set(e);
    /* neq at set, or eq at set implying False or the old False */
    if (c->tag == MG_TM_AP && c->f->tag == MG_TM_AP && c->f->f->tag == MG_TM_TAP && c->f->f->tp == set &&
        c->f->f->f->tag == MG_TM_NAME &&
        strcmp(mge_name_text(e, c->f->f->f->n), "7966a66a9bb198103c2a540ccd5ebebdff33c10843cc10eebfc98715e142989c") == 0) {
        m = c->f->x;
        n = c->x;
    } else if (c->tag == MG_TM_IMP && c->f->tag == MG_TM_AP && c->f->f->tag == MG_TM_AP &&
               c->f->f->f->tag == MG_TM_TAP && c->f->f->f->tp == set && c->f->f->f->f->tag == MG_TM_NAME &&
               strcmp(mge_name_text(e, c->f->f->f->f->n), HEX_EQ) == 0 &&
               ((c->x->tag == MG_TM_NAME && c->x->n == e->fal) ||
                (c->x->tag == MG_TM_ALL && c->x->tp == mge_ty_prop(e) && c->x->f->tag == MG_TM_DB && c->x->f->n == 0u))) {
        m = c->f->f->x;
        n = c->f->x;
    }
    if (!m || m == n)
        mge_fail(e, "distinct tactic requires goal to be diseqn");
    MgPf *d;
    if (e->theory == MG_PROFILE_HOAS) {
        d = pf_plam(e, eq_set(e, m, n), distinct_hoas_2(e, m, n, pf_hyp(e, 0u)));
    } else if (unary_nat_p(e, m) && unary_nat_p(e, n)) {
        d = distinct_level(e, 0, m, n);
    } else if (seq_over_p(e, 0, m) && seq_over_p(e, 0, n)) {
        d = distinct_level(e, 1, m, n);
    } else if (seq_over_p(e, 1, m) && seq_over_p(e, 1, n)) {
        d = distinct_level(e, 2, m, n);
    } else {
        mge_fail(e, "distinct failed");
    }
    PfFrame *f = new_frame(e, F_EXACT);
    f->d = d;
    push_frame(e, f);
    e->pfstate = g->next;
}

static void tac_special(MgElab *e, const MgSourceItem *it) {
    const char *x = it->name;
    MgTp *prop = mge_ty_prop(e);
    if (strcmp(x, "reflexivity") == 0 && !it->terms_len) {
        const PfSt *g = current_goal(e, "reflexivity tactic cannot be used when there is no claim");
        MgTp *a;
        MgTm *m, *n;
        if (!is_equation(e, g->claim, &a, &m, &n))
            mge_fail(e, "reflexivity tactic cannot be used when there is not an equation");
        const Dl *dl = e->deltaset;
        if (!conv(e, m, n, &dl))
            mge_fail(e, "reflexivity failed");
        e->deltaset = dl;
        MgTp *rel = mge_ty_arr(e, a, mge_ty_arr(e, a, prop));
        MgPf *reflp = pf_tlam(e, rel, pf_plam(e, mge_tm_ap(e, mge_tm_ap(e, mge_tm_db(e, 0u), mge_shift1(e, m)),
                                                          mge_shift1(e, n)), pf_hyp(e, 0u)));
        PfFrame *f = new_frame(e, F_EXACT);
        f->d = reflp;
        push_frame(e, f);
        e->pfstate = g->next;
        return;
    }
    if (strcmp(x, "symmetry") == 0 && !it->terms_len) {
        const PfSt *g = current_goal(e, "symmetry tactic cannot be used when there is no claim");
        MgTp *a;
        MgTm *m, *n;
        if (!is_equation(e, g->claim, &a, &m, &n))
            mge_fail(e, "symmetry tactic cannot be used when there is not an equation");
        PfFrame *f = new_frame(e, F_SYM);
        f->a = a;
        push_frame(e, f);
        MgTm *q = g->claim->f->f;
        e->pfstate = mge_pf_goal(e, mge_tm_ap(e, mge_tm_ap(e, q, n), m), g->ctxtm, g->ctxpf, g->next);
        return;
    }
    if (strcmp(x, "transitivity") == 0 && it->terms_len) {
        const PfSt *g = current_goal(e, "transitivity tactic cannot be used when there is no claim");
        MgTp *a;
        MgTm *lhs, *rhs;
        if (!is_equation(e, g->claim, &a, &lhs, &rhs))
            mge_fail(e, "transitivity tactic cannot be used when there is not an equation");
        MgTm *q = g->claim->f->f;
        uint32_t k = it->terms_len;
        MgTm **mids = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * k);
        for (uint32_t i = 0; i < k; ++i)
            mids[i] = check_tm_goal(e, it->terms[i], a, g->ctxtm);
        /* goals: lhs = m1, m1 = m2, ..., mk = rhs */
        const PfSt *rest = mge_pf_goal(e, mge_tm_ap(e, mge_tm_ap(e, q, mids[k - 1u]), rhs), g->ctxtm, g->ctxpf, g->next);
        for (uint32_t i = k - 1u; i-- > 0;)
            rest = mge_pf_goal(e, mge_tm_ap(e, mge_tm_ap(e, q, mids[i]), mids[i + 1u]), g->ctxtm, g->ctxpf, rest);
        rest = mge_pf_goal(e, mge_tm_ap(e, mge_tm_ap(e, q, lhs), mids[0]), g->ctxtm, g->ctxpf, rest);
        PfFrame *f = new_frame(e, F_CHAIN);
        f->a = a;
        f->links = k + 1u;
        f->final_lhs = lhs;
        f->final_rhs = rhs;
        push_frame(e, f);
        e->pfstate = rest;
        return;
    }
    if (strcmp(x, "f_equal") == 0 && !it->terms_len) {
        const PfSt *g = current_goal(e, "f_equal tactic cannot be used when there is no claim");
        MgTp *a;
        MgTm *lhs, *rhs;
        if (!is_equation(e, g->claim, &a, &lhs, &rhs))
            mge_fail(e, "f_equal tactic cannot be used when there is not an equation");
        MgTm *q = g->claim->f->f;
        /* f_equal_intermediates: the argument pairs that differ, innermost
         * first, each with its context f as a term F whose de Bruijn 0 is
         * the hole (its other indices raised by one): f m = F[0 := m]. */
        uint32_t cap = 8u, len = 0;
        MgTm **ms = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * cap);
        MgTm **ns = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * cap);
        MgTm **cs = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * cap);
        MgTm *F = mge_tm_db(e, 0u);
        MgTm *l = lhs, *r = rhs;
        while (l != r) {
            if (l->tag != MG_TM_AP || r->tag != MG_TM_AP)
                mge_fail(e, "f_equal does not apply here");
            MgTm *la4 = mge_tmshift(e, 0u, 4, l->x);
            MgTm *room = mge_tmshift(e, 1u, 1, F);
            if (l->x != r->x) {
                if (len == cap) {
                    uint32_t c2 = cap * 2u;
                    MgTm **m2 = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * c2);
                    MgTm **n2 = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * c2);
                    MgTm **k2 = mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * c2);
                    memcpy(m2, ms, sizeof(MgTm *) * len);
                    memcpy(n2, ns, sizeof(MgTm *) * len);
                    memcpy(k2, cs, sizeof(MgTm *) * len);
                    ms = m2;
                    ns = n2;
                    cs = k2;
                    cap = c2;
                }
                MgTm *rf4 = mge_tmshift(e, 0u, 4, r->f);
                /* fun m -> f (Ap (rf4, m)) */
                cs[len] = tmsubst(e, room, 0u, mge_tm_ap(e, mge_shift1(e, rf4), mge_tm_db(e, 0u)));
                ms[len] = l->x;
                ns[len] = r->x;
                ++len;
            }
            /* fun m -> f (Ap (m, la4)) */
            F = tmsubst(e, room, 0u, mge_tm_ap(e, mge_tm_db(e, 0u), mge_shift1(e, la4)));
            l = l->f;
            r = r->f;
        }
        /* mnl lists the innermost difference first */
        const PfSt *rest = g->next;
        for (uint32_t i = 0; i < len; ++i)
            rest = mge_pf_goal(e, mge_tm_ap(e, mge_tm_ap(e, q, ms[i]), ns[i]), g->ctxtm, g->ctxpf, rest);
        PfFrame *f = new_frame(e, F_CHAIN);
        f->a = a;
        f->fequal = true;
        f->links = len;
        f->ctx = mge_chunk_alloc(e, &e->perm, sizeof(MgTm *) * (len ? len : 1u));
        for (uint32_t i = 0; i < len; ++i)
            f->ctx[i] = cs[len - 1u - i];
        f->final_lhs = lhs;
        f->final_rhs = rhs;
        push_frame(e, f);
        e->pfstate = rest;
        return;
    }
    if (strcmp(x, "distinct") == 0 && !it->terms_len) {
        tac_distinct(e);
        return;
    }
    mge_fail(e, "Unknown proof tactic");
}

/* ── the script ───────────────────────────────────────────────────────── */

/* pfproplookup cxpf x, else List.assoc x sigpf applied in the goal's
 * context (every section variable and hypothesis it names in scope). */
static bool aby_fact_known(MgElab *e, const char *x, const PfSt *g) {
    uint32_t i;
    MgTm *prop;
    if (hyp_lookup(g->ctxpf, x, &i, &prop))
        return true;
    uint64_t v;
    if (!mge_stk_top(&e->sigpf, K(x), &v))
        return false;
    for (const App *a = P(const SigPf, v)->app; a; a = a->prev) {
        bool ok = false;
        if (a->kind == APP_TP) {
            ok = mge_tp_position(e, a->name) >= 0;
        } else if (a->kind == APP_TM) {
            MgTm *m;
            MgTp *tp;
            ok = mge_tmtplookup(e, g->ctxtm, a->name, &m, &tp);
        } else {
            ok = hyp_lookup(g->ctxpf, a->name, &i, &prop);
        }
        if (!ok)
            return false;
    }
    return true;
}

/* What Qed and Admitted record: the theorem is known from here on. */
static void theorem_known(MgElab *e) {
    mge_declare_pf(e, e->thm_name, e->thm_hash);
    if (e->thm_levels > 0u)
        mge_pushpolypf(e, e->thm_name, e->thm_levels, e->thm_prop);
    e->proving = false;
}

/* The proof in the kernel's conventions, with its type binders. */
static MgPf *kernel_pf(MgElab *e, MgPf *d, uint32_t levels) {
    switch (d->tag) {
    case MG_PF_TPAP: return pf_tpap(e, kernel_pf(e, d->p, levels), mge_kernel_tp(e, d->tp, levels));
    case MG_PF_TMAP: return pf_tmap(e, kernel_pf(e, d->p, levels), mge_kernel_tm(e, d->t, levels));
    case MG_PF_PFAP: {
        MgPf *q = kernel_pf(e, d->q, levels);
        return pf_pfap(e, kernel_pf(e, d->p, levels), q);
    }
    case MG_PF_PLAM: return pf_plam(e, mge_kernel_tm(e, d->t, levels), kernel_pf(e, d->p, levels));
    case MG_PF_TLAM: return pf_tlam(e, mge_kernel_tp(e, d->tp, levels), kernel_pf(e, d->p, levels));
    default: return d;
    }
}

/* Why a finished proof has no authority: the document's admits first,
 * then a budget that ran out, then aby steps. */
static MgVerdict unproved_why(const MgElab *e) {
    if (e->admit_used)
        return MG_VERDICT_ADMITTED;
    if (e->exhausted_proof)
        return MG_VERDICT_EXHAUSTED;
    return e->aby_used ? MG_VERDICT_ABY : MG_VERDICT_ADMITTED;
}

/* ── for native aby (mg_aby.c) ────────────────────────────────────────── */

const PTm *mge_delta_of(MgElab *e, uint32_t c) { return delta_of(e, c); }

MgTm *mge_tm_subst0(MgElab *e, MgTm *body, MgTm *arg) { return tmsubst(e, body, 0u, arg); }

bool mge_conv(MgElab *e, MgTm *m, MgTm *n) {
    const Dl *dl = NULL;
    return conv(e, m, n, &dl);
}

bool mge_defp(MgElab *e, MgTm *h) { return defp(e, h); }

MgTm *mge_delta_exp(MgElab *e, MgTm *h, MgTm **args, uint32_t n) { return delta_exp(e, h, args, n); }

MgPf *mge_pfshift(MgElab *e, uint32_t i, int32_t j, MgPf *d) { return pfshift(e, i, j, d); }

MgPf *mge_pftmshift(MgElab *e, uint32_t i, int32_t j, MgPf *d) { return pftmshift(e, i, j, d); }

const PfCx *mge_pfcx_shift(MgElab *e, const PfCx *hyps) { return pfcx_shift(e, hyps); }

MgTm *mge_tm_tpsubst(MgElab *e, MgTm *m, MgTp *const *types, uint32_t n) { return tmtpsubst(e, m, types, n); }

typedef struct {
    const char *name;
    const PfSt *g;
    PfTm out;
} FactTry;

static void fact_body(MgElab *e, void *ctx) {
    FactTry *t = ctx;
    MgL l = {.kind = MG_L_NAME, .text = t->name};
    t->out = extract_pf(e, &l, t->g->ctxtm, t->g->ctxpf);
}

bool mge_aby_fact(MgElab *e, const char *name, const PfSt *g, MgPf **pf, MgTm **prop) {
    FILE *rows = e->rows;
    e->rows = NULL;
    FactTry t = {name, g, {0}};
    int k = mge_try_run(e, fact_body, &t);
    e->rows = rows;
    if (k == RAISE_FATAL || k == RAISE_EXHAUSTED)
        mge_raise_kind(e, k);
    if (k)
        return false;
    *pf = t.out.pf;
    *prop = t.out.tm;
    return true;
}

typedef struct {
    const PfSt *g;
    MgPf *d;
    bool ok;
} PfCheckTry;

static void pf_check_body(MgElab *e, void *ctx) {
    PfCheckTry *t = ctx;
    const Dl *dl = e->deltaset;
    t->ok = check_propofpf(e, tp_context(e, t->g->ctxtm), pf_context(e, t->g->ctxpf), t->d, t->g->claim, &dl);
}

bool mge_check_pf_goal(MgElab *e, const PfSt *g, MgPf *d) {
    FILE *rows = e->rows;
    const Dl *deltas = e->deltaset;
    e->rows = NULL;
    PfCheckTry t = {g, d, false};
    int k = mge_try_run(e, pf_check_body, &t);
    e->rows = rows;
    e->deltaset = deltas;
    if (k == RAISE_FATAL || k == RAISE_EXHAUSTED)
        mge_raise_kind(e, k);
    return k == 0 && t.ok;
}

static void qed_checked(MgElab *e, void *ctx);

static void qed(MgElab *e) {
    if (e->pfstate && !e->unchecked_proof)
        mge_fail(e, "Proof of %s is incomplete", e->thm_name);
    mge_map_set(e, &e->indexknowns, e->thm_hash, 1u);
    mge_activate_special_knowns(e, e->thm_hash);
    mge_megaauto_known(e, e->thm_hash);
    theorem_known(e);
    e->pfstate = NULL;
    if (e->admitted || e->unchecked_proof) {
        if (e->admitted) {
            mge_row_begin(e);
            mg_buf_puts(&e->row, "(QEDWITHADMITS)\n");
            mge_row_out(e);
        }
        /* a proof with admits has no authority; nor, until the engine
         * evaluates every tactic, an unchecked one */
        if (e->admission && !mg_admit_unproved_as(e->admission, unproved_why(e)))
            mge_kernel_refused(e);
        return;
    }
    int k = mge_try_run(e, qed_checked, NULL);
    if (k == RAISE_EXHAUSTED) {
        /* the theorem's budget ran out while its proof was checked */
        e->exhausted_proof = true;
        if (e->admission && !mg_admit_unproved_as(e->admission, MG_VERDICT_EXHAUSTED))
            mge_kernel_refused(e);
        return;
    }
    if (k)
        mge_raise_kind(e, k);
}

/* qed for a proof without admits: the proof term checked against the
 * proposition, its rows, then the kernel's check. */
static void qed_checked(MgElab *e, void *ctx) {
    (void)ctx;
    MgPf *dgpf = proof_of_script(e);
    const Dl *dl = e->deltaset;
    if (!check_propofpf(e, NULL, NULL, dgpf, e->thm_prop, &dl))
        mge_fail(e, "Proof doesn't prove the proposition.");
    e->deltaset = dl;
    for (const Dl *d = dl; d; d = d->next) {
        mge_row_begin(e);
        mg_buf_printf(&e->row, "(DELTA \"%s\")\n", mge_name_text(e, d->h));
        mge_row_out(e);
    }
    mge_row_begin(e);
    mg_buf_printf(&e->row, "(PROOF \"%s\" ", e->thm_name);
    mg_front_pf_sexpr(&e->row, e->s, dgpf);
    mg_buf_puts(&e->row, ")\n(QED)\n");
    mge_row_out(e);
    if (e->admission) {
        MgPf *k = kernel_pf(e, dgpf, e->thm_levels);
        for (uint32_t i = 0; i < e->thm_levels; ++i)
            k = pck(e, mg_pf_tplam(e->s, k));
        if (!mg_admit_proof(e->admission, k))
            mge_kernel_refused(e);
    }
}

void mge_proof_begin(MgElab *e, MgTm *atm) {
    e->deltaset = NULL;
    e->prooffun = NULL;
    e->thm_clos = e->clos;
    e->admitted = false;
    e->admit_used = false;
    e->aby_used = false;
    e->unchecked_proof = false;
    e->exhausted_proof = false;
    e->pfstate = mge_pf_goal(e, atm, e->ctxtm, e->ctxpf, NULL);
}

typedef struct {
    const MgSourceItem *it;
} TacTry;

static void tactic_body(MgElab *e, void *ctx) {
    const MgSourceItem *it = ((TacTry *)ctx)->it;
    switch ((MgTacKind)it->kind) {
    case MG_TAC_EXACT: tac_exact(e, it); break;
    case MG_TAC_LET: tac_let(e, it); break;
    case MG_TAC_ASSUME: tac_assume(e, it); break;
    case MG_TAC_SET: tac_set(e, it); break;
    case MG_TAC_APPLY: tac_apply(e, it); break;
    case MG_TAC_CLAIM: tac_claim(e, it); break;
    case MG_TAC_PROVE: tac_prove(e, it); break;
    case MG_TAC_CASES: mge_fail(e, "cases tactic is not yet implemented");
    case MG_TAC_WITNESS: tac_witness(e, it); break;
    case MG_TAC_REWRITE: tac_rewrite(e, it); break;
    case MG_TAC_SPECIAL: tac_special(e, it); break;
    default: mge_fail(e, "Unknown proof tactic");
    }
}

static void eval_pftac(MgElab *e, const MgSourceItem *it);

static void eval_pftac_body(MgElab *e, void *ctx) { eval_pftac(e, (const MgSourceItem *)ctx); }

void mge_eval_pftac(MgElab *e, const MgSourceItem *it) {
    int k = mge_try_run(e, eval_pftac_body, (void *)it);
    if (k == RAISE_EXHAUSTED) {
        /* the theorem's budget ran out during this step: the rest of the
         * script is counted, its qed admits it as EXHAUSTED */
        e->exhausted_proof = true;
        e->unchecked_proof = true;
        ++e->unchecked;
        return;
    }
    if (k)
        mge_raise_kind(e, k);
}

static void eval_pftac(MgElab *e, const MgSourceItem *it) {
    /* Once the theorem's budget has run out, its goals are unknown here:
     * the rest of the script is counted, an admit still makes the proof
     * one with admits. */
    if (e->unchecked_proof && it->kind != MG_TAC_QED && it->kind != MG_TAC_ADMITTED) {
        if (it->kind == MG_TAC_ADMIT || it->kind == MG_TAC_ABY)
            e->admitted = true;
        if (it->kind == MG_TAC_ADMIT)
            e->admit_used = true;
        if (it->kind == MG_TAC_ABY)
            e->aby_used = true;
        ++e->unchecked;
        return;
    }
    switch ((MgTacKind)it->kind) {
    case MG_TAC_STRUCT: {
        int32_t i = it->depth;
        const PfSt *p = e->pfstate;
        if (i < 4) {
            if (p && p->sep && p->b) {
                if (p->j != i)
                    mge_fail(e, "Previous subproof not completed");
                e->pfstate = p->next;
            } else if (p && p->sep) {
                mge_fail(e, "Proof structuring bug");
            } else if (!p) {
                mge_fail(e, "No claim to prove here");
            } else if (p->next && !p->next->sep) {
                const PfSt *g2 = p->next;
                e->pfstate = mge_pf_goal(e, p->claim, p->ctxtm, p->ctxpf,
                                         structure_pfstate(e, i, mge_pf_goal(e, g2->claim, g2->ctxtm, g2->ctxpf, NULL),
                                                           g2->next));
            } else {
                mge_fail(e, "Inappropriate place for this proof structuring symbol since there is only one goal");
            }
        } else if (i == 4) {
            if (p && p->sep)
                mge_fail(e, "A subproof cannot be started here");
            if (!p)
                mge_fail(e, "No claim to prove here");
            e->pfstate = mge_pf_goal(e, p->claim, p->ctxtm, p->ctxpf, pf_sep(e, 5, true, p->next));
        } else {
            if (p && p->sep) {
                if (p->j != 5)
                    mge_fail(e, "Previous subproof not completed");
                e->pfstate = p->next;
            } else if (!p) {
                mge_fail(e, "Not proving a goal");
            } else {
                mge_fail(e, "Subproof not completed");
            }
        }
        break;
    }
    case MG_TAC_ADMIT:
        if (!e->pfstate)
            mge_fail(e, "No goal to admit");
        e->pfstate = e->pfstate->next;
        e->admitted = true;
        e->admit_used = true;
        break;
    case MG_TAC_ABY: {
        /* Megalodon's aby: the named facts must exist where the goal is
         * stated; the goal is then admitted (a hammer may take it up).
         * With prove_aby, native aby (mg_aby.c) looks for the proof
         * first: found, it is the step's proof, as an exact would be. */
        const PfSt *g = e->pfstate;
        if (!g || g->sep)
            mge_fail(e, "No goal to aby");
        for (uint32_t k = 0; k < it->names_len; ++k) {
            const char *x = mge_intern(e, it->names[k]);
            if (!aby_fact_known(e, x, g))
                mge_fail(e, "Unknown proof %s -- it might be a term in a position where a proof is expected", x);
        }
        if (e->prove_aby) {
            ++e->aby_tried;
            /* MG_ABY_STATS=1: one line per aby step on stderr (a
             * measurement aid; it changes nothing) */
            static int stats_on = -1;
            if (stats_on < 0) {
                const char *v = getenv("MG_ABY_STATS");
                stats_on = v && v[0] == '1';
            }
            MgAbyStats st;
            size_t store_before = e->s->bytes;
            MgPf *d = mge_aby_prove(e, g, it->names, it->names_len, &st);
            if (stats_on) {
                /* the goal's head, after its binders and premises */
                MgTm *c = g->claim;
                while (c->tag == MG_TM_ALL || c->tag == MG_TM_IMP)
                    c = c->tag == MG_TM_ALL ? c->f : c->x;
                while (c->tag == MG_TM_AP)
                    c = c->f;
                while (c->tag == MG_TM_TAP)
                    c = c->f;
                const char *head = c->tag == MG_TM_NAME ? mge_name_text(e, c->n) : c->tag == MG_TM_PRIM ? "prim" : "var";
                fprintf(stderr, "[aby-step] line %d char %d hints %u usable %u unusable %u steps %llu %s%s rejected %llu store +%zu head %.12s\n",
                        e->lineno, e->charno, it->names_len, st.facts, st.unusable_facts, (unsigned long long)st.steps,
                        d ? "proved" : st.out_of_steps ? "out-of-steps" : "failed", st.raised ? " raised" : "",
                        (unsigned long long)st.rejected, e->s->bytes - store_before, head);
            }
            if (d) {
                ++e->aby_proved;
                PfFrame *f = new_frame(e, F_EXACT);
                f->d = d;
                push_frame(e, f);
                e->pfstate = g->next;
                break;
            }
        }
        e->pfstate = g->next;
        e->admitted = true;
        e->aby_used = true;
        break;
    }
    case MG_TAC_QED:
        qed(e);
        return;
    case MG_TAC_ADMITTED:
        mge_row_begin(e);
        mg_buf_puts(&e->row, "(ADMITTED)\n");
        mge_row_out(e);
        if (e->admission && !mg_admit_unproved(e->admission))
            mge_kernel_refused(e);
        if (e->thm_levels > 0u)
            mge_pushpolypf(e, e->thm_name, e->thm_levels, e->thm_prop);
        mge_activate_special_knowns(e, e->thm_hash);
        mge_declare_pf(e, e->thm_name, e->thm_hash);
        e->proving = false;
        e->pfstate = NULL;
        return;
    default: {
        TacTry t = {it};
        int k = mge_try_run(e, tactic_body, &t);
        if (k)
            mge_raise_kind(e, k);
        break;
    }
    }
    /* evaluate_pftac_2: a finished structured subproof closes itself */
    while (e->pfstate && e->pfstate->sep && !e->pfstate->b)
        e->pfstate = e->pfstate->next;
}
