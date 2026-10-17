#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_core.h"

#include <string.h>

/*
 * Lazy conversion (an optimization with the reference verdicts).
 *
 * The reference kernel (MathdataKernel.normalize) decides judgments by full
 * beta-eta-delta normal forms.  On large libraries the normal forms of
 * intermediate propositions can be huge, while a proof only ever compares a
 * few of them.  Upstream's own .mg checker (syntax.ml conv2/headnorm)
 * therefore compares lazily, unfolding definitions on demand.
 *
 * This file decides the same relation as the reference: mg_conv(a, b) holds
 * iff normalize(a) == normalize(b).  On well-typed (hence strongly
 * normalizing) terms the algorithm is complete:
 *   - both sides are put in weak head normal form for beta (both sorts);
 *   - equal structures compare componentwise; a lambda on one side only is
 *     compared through eta (body against the other side applied to the new
 *     variable), at both sorts;
 *   - neutral spines with the same rigid head compare argumentwise;
 *   - whenever that does not settle the question and a head is a defined
 *     name, the later-admitted defined head is unfolded (both, when they are
 *     the same name) and the comparison continues;
 *   - two different undefined heads, or a rigid mismatch with no defined
 *     head, are different normal forms.
 * Definitions are acyclic in a lineage and every unfolding and reduction is
 * charged to the budget, so the procedure terminates or reports an
 * undecided outcome.  Successful and failed comparisons are memoized per
 * call on (node, node) pairs, so shared DAGs are compared once.
 *
 * mg_infer_proof_lazy mirrors MathdataKernel.inferProof with the same
 * rules, but keeps propositions as they are produced (no normalization) and
 * compares through mg_conv where the reference compares normal forms.  Its
 * accept/refuse verdict for a stated claim equals the reference's; the
 * differential tests in tests/megalodon run both on the same data.
 */

static inline MgStore *ST(MgCheck *c) { return &c->env.theory->store; }

static inline bool live(MgCheck *c, uint64_t units) {
    if (c->status != MG_OK)
        return false;
    if (!mg_budget_charge(c->budget, units))
        return mg_fail(c, c->budget->stop, "budget");
    return true;
}

/* ── spines ───────────────────────────────────────────────────────────── */

typedef struct {
    const void *arg;   /* MgTm* (term argument) or MgTp* (type argument) */
    bool type;
} SpineArg;

typedef struct {
    SpineArg *args;    /* args[0] is applied first */
    uint32_t len, cap;
    MgTm *head;
    bool nomem;
} Spine;

static void spine_free(Spine *s) {
    mg_sys_free(s->args);
    memset(s, 0, sizeof(*s));
}

static void spine_of(Spine *sp, MgTm *m) {
    sp->len = 0;
    uint32_t n = 0;
    for (MgTm *t = m; t->tag == MG_TM_AP || t->tag == MG_TM_TAP; t = t->f)
        ++n;
    if (n > sp->cap) {
        SpineArg *grown = mg_sys_realloc(sp->args, sizeof(SpineArg) * n);
        if (!grown) {
            sp->nomem = true;
            return;
        }
        sp->args = grown;
        sp->cap = n;
    }
    sp->len = n;
    MgTm *t = m;
    for (uint32_t i = n; i > 0u; --i) {
        if (t->tag == MG_TM_AP)
            sp->args[i - 1u] = (SpineArg){t->x, false};
        else
            sp->args[i - 1u] = (SpineArg){t->tp, true};
        t = t->f;
    }
    sp->head = t;
}

static MgTm *spine_build(MgStore *s, MgTm *head, const SpineArg *args, uint32_t from,
                         uint32_t to) {
    MgTm *m = head;
    for (uint32_t i = from; m && i < to; ++i)
        m = args[i].type ? mg_tm_tap(s, m, (MgTp *)args[i].arg)
                         : mg_tm_ap(s, m, (MgTm *)args[i].arg);
    return m;
}

/* ── head reduction ───────────────────────────────────────────────────── */

/* Beta at the head, both sorts, no delta. */
static MgTm *whnf_beta(MgCheck *c, MgTm *m) {
    if (m->tag != MG_TM_AP && m->tag != MG_TM_TAP)
        return m;
    Spine sp = {0};
    for (;;) {
        if (!live(c, 1u))
            break;
        if (m->tag != MG_TM_AP && m->tag != MG_TM_TAP)
            break;
        spine_of(&sp, m);
        if (sp.nomem) {
            mg_fail(c, MG_NOMEM, "spine");
            break;
        }
        MgTm *h = sp.head;
        MgTm *reduced = NULL;
        if (h->tag == MG_TM_LAM && !sp.args[0].type)
            reduced = mg_tm_instantiate(c, h->f, 0, (MgTm *)sp.args[0].arg);
        else if (h->tag == MG_TM_TLAM && sp.args[0].type)
            reduced = mg_tm_tinstantiate(c, h->f, 0, (MgTp *)sp.args[0].arg);
        else
            break;
        if (!reduced)
            break;
        m = spine_build(ST(c), reduced, sp.args, 1u, sp.len);
        if (!m) {
            mg_fail(c, MG_NOMEM, "allocation");
            break;
        }
    }
    spine_free(&sp);
    return c->status == MG_OK ? m : NULL;
}

/* The visible definition of a name head, or NULL. */
static const MgDecl *defined_head(MgCheck *c, const MgTm *head) {
    if (head->tag != MG_TM_NAME)
        return NULL;
    const MgDecl *d = mg_env_lookup(c, head->n, MG_NS_TERM);
    return d && d->def ? d : NULL;
}

static const MgDecl *head_definition(MgCheck *c, MgTm *m) {
    while (m->tag == MG_TM_AP || m->tag == MG_TM_TAP)
        m = m->f;
    return defined_head(c, m);
}

/* Replace the defined head of a spine by its body. */
static MgTm *unfold_head(MgCheck *c, MgTm *m) {
    Spine sp = {0};
    spine_of(&sp, m);
    MgTm *r = NULL;
    if (sp.nomem) {
        mg_fail(c, MG_NOMEM, "spine");
    } else {
        const MgDecl *d = defined_head(c, sp.head);
        if (d && live(c, 1u)) {
            ++c->deltas;
            r = spine_build(ST(c), d->def, sp.args, 0u, sp.len);
            if (!r)
                mg_fail(c, MG_NOMEM, "allocation");
        }
    }
    spine_free(&sp);
    return r;
}

MgTm *mg_whnf(MgCheck *c, MgTm *m) {
    for (;;) {
        m = whnf_beta(c, m);
        if (!m)
            return NULL;
        if (!head_definition(c, m))
            return m;
        m = unfold_head(c, m);
        if (!m)
            return NULL;
    }
}

/* ── conversion ───────────────────────────────────────────────────────── */

typedef struct {
    const MgTm *a, *b;
    uint8_t result;   /* 1 equal, 2 different */
} ConvSlot;

typedef struct {
    MgCheck *c;
    ConvSlot *slots;
    size_t cap, len;
    bool frozen;
} Conv;

static size_t conv_index(const MgTm *a, const MgTm *b, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)a * UINT64_C(0x9e3779b97f4a7c15) ^
                 (uint64_t)(uintptr_t)b * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 29;
    return (size_t)h & (cap - 1u);
}

static uint8_t conv_get(const Conv *v, const MgTm *a, const MgTm *b) {
    if (!v->cap)
        return 0;
    for (size_t i = conv_index(a, b, v->cap);; i = (i + 1u) & (v->cap - 1u)) {
        if (!v->slots[i].a)
            return 0;
        if (v->slots[i].a == a && v->slots[i].b == b)
            return v->slots[i].result;
    }
}

static void conv_put(Conv *v, const MgTm *a, const MgTm *b, bool equal) {
    if (v->frozen)
        return;
    if ((v->len + 1u) * 2u > v->cap) {
        size_t cap = v->cap ? v->cap * 2u : 256u;
        ConvSlot *slots = mg_sys_alloc(sizeof(ConvSlot) * cap);
        if (!slots) {
            v->frozen = true;
            return;
        }
        memset(slots, 0, sizeof(ConvSlot) * cap);
        for (size_t j = 0; j < v->cap; ++j) {
            ConvSlot e = v->slots[j];
            if (!e.a)
                continue;
            size_t i = conv_index(e.a, e.b, cap);
            while (slots[i].a)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e;
        }
        mg_sys_free(v->slots);
        v->slots = slots;
        v->cap = cap;
    }
    size_t i = conv_index(a, b, v->cap);
    while (v->slots[i].a)
        i = (i + 1u) & (v->cap - 1u);
    v->slots[i] = (ConvSlot){a, b, (uint8_t)(equal ? 1u : 2u)};
    ++v->len;
}

static bool is_structure(const MgTm *m) {
    switch (m->tag) {
    case MG_TM_LAM:
    case MG_TM_ALL:
    case MG_TM_IMP:
    case MG_TM_TLAM:
    case MG_TM_TALL:
        return true;
    default:
        return false;
    }
}

static bool conv_rec(Conv *v, MgTm *a, MgTm *b);

/* Compare two neutral spines with the same head argumentwise. */
static bool spine_args_conv(Conv *v, const Spine *x, const Spine *y) {
    if (x->len != y->len)
        return false;
    for (uint32_t i = 0; i < x->len; ++i) {
        if (x->args[i].type != y->args[i].type)
            return false;
        if (x->args[i].type) {
            if (x->args[i].arg != y->args[i].arg)
                return false;
        } else if (!conv_rec(v, (MgTm *)x->args[i].arg, (MgTm *)y->args[i].arg)) {
            return false;
        }
    }
    return true;
}

static bool conv_rec(Conv *v, MgTm *a, MgTm *b) {
    MgCheck *c = v->c;
    if (a == b)
        return true;
    if (c->status != MG_OK)
        return false;
    uint8_t memo = conv_get(v, a, b);
    if (memo)
        return memo == 1u;
    const MgTm *key_a = a, *key_b = b;
    if (++c->depth > c->depth_limit) {
        --c->depth;
        return mg_fail(c, MG_CAPACITY, "conversion depth");
    }
    bool result = false;
    for (;;) {
        if (!live(c, 1u))
            break;
        a = whnf_beta(c, a);
        b = a ? whnf_beta(c, b) : NULL;
        if (!a || !b)
            break;
        if (a == b) {
            result = true;
            break;
        }
        /* eta, both sorts: a lambda against a neutral term; against any
         * other structure it is a different normal-form constructor */
        if (a->tag == MG_TM_LAM && b->tag == MG_TM_LAM) {
            result = a->tp == b->tp && conv_rec(v, a->f, b->f);
            break;
        }
        if (a->tag == MG_TM_TLAM && b->tag == MG_TM_TLAM) {
            result = conv_rec(v, a->f, b->f);
            break;
        }
        if (a->tag == MG_TM_LAM || b->tag == MG_TM_LAM ||
            a->tag == MG_TM_TLAM || b->tag == MG_TM_TLAM) {
            bool left = a->tag == MG_TM_LAM || a->tag == MG_TM_TLAM;
            MgTm *lam = left ? a : b;
            MgTm *other = left ? b : a;
            if (is_structure(other))
                break;
            MgTm *applied = NULL;
            if (lam->tag == MG_TM_LAM) {
                MgTm *shifted = mg_tm_shift(c, other, 0, 1);
                applied = shifted ? mg_tm_ap(ST(c), shifted, mg_tm_db(ST(c), 0)) : NULL;
            } else {
                MgTm *shifted = mg_tm_tshift(c, other, 0, 1);
                applied = shifted ? mg_tm_tap(ST(c), shifted, mg_tp_var(ST(c), 0)) : NULL;
            }
            if (!applied) {
                if (c->status == MG_OK)
                    mg_fail(c, MG_NOMEM, "allocation");
                break;
            }
            result = conv_rec(v, lam->f, applied);
            break;
        }
        bool sa = is_structure(a), sb = is_structure(b);
        if (sa && sb) {
            if (a->tag != b->tag)
                break; /* different normal-form constructors */
            switch (a->tag) {
            case MG_TM_ALL:
                result = a->tp == b->tp && conv_rec(v, a->f, b->f);
                break;
            case MG_TM_IMP:
                result = conv_rec(v, a->f, b->f) && conv_rec(v, a->x, b->x);
                break;
            case MG_TM_TALL:
                result = conv_rec(v, a->f, b->f);
                break;
            default:
                break;
            }
            break;
        }
        if (sa || sb) {
            /* a structure against a neutral: only unfolding can help */
            MgTm *neutral = sa ? b : a;
            if (!head_definition(c, neutral))
                break;
            MgTm *u = unfold_head(c, neutral);
            if (!u)
                break;
            if (sa)
                b = u;
            else
                a = u;
            continue;
        }
        /* two neutral spines */
        Spine x = {0}, y = {0};
        spine_of(&x, a);
        spine_of(&y, b);
        if (x.nomem || y.nomem) {
            spine_free(&x);
            spine_free(&y);
            mg_fail(c, MG_NOMEM, "spine");
            break;
        }
        bool same_head = x.head == y.head;
        if (same_head && spine_args_conv(v, &x, &y)) {
            spine_free(&x);
            spine_free(&y);
            result = true;
            break;
        }
        const MgDecl *da = defined_head(c, x.head);
        const MgDecl *db = defined_head(c, y.head);
        spine_free(&x);
        spine_free(&y);
        if (c->status != MG_OK)
            break;
        if (!da && !db)
            break; /* rigid heads disagree, or rigid arguments disagree */
        if (da && db) {
            if (da == db) {
                a = unfold_head(c, a);
                b = a ? unfold_head(c, b) : NULL;
            } else if (da->seq >= db->seq) {
                a = unfold_head(c, a);
            } else {
                b = unfold_head(c, b);
            }
        } else if (da) {
            a = unfold_head(c, a);
        } else {
            b = unfold_head(c, b);
        }
        if (!a || !b)
            break;
    }
    --c->depth;
    if (c->status == MG_OK) {
        conv_put(v, key_a, key_b, result);
        return result;
    }
    return false;
}

MgStatus mg_conv(MgCheck *c, MgTm *a, MgTm *b, bool *equal) {
    *equal = false;
    if (c->status != MG_OK)
        return c->status;
    Conv v = {.c = c};
    bool r = conv_rec(&v, a, b);
    mg_sys_free(v.slots);
    if (c->status != MG_OK)
        return c->status;
    *equal = r;
    return MG_OK;
}

/* ── lazy proof-tree inference ────────────────────────────────────────── */

typedef struct {
    const MgPf *pf;
    const MgCtx *ctx;
    MgTm *prop;
    uint64_t address;   /* recorded row, when recording */
} LazySlot;

typedef struct {
    LazySlot *slots;
    uint32_t cap, len;
    bool frozen;
} LazyMemo;

static uint32_t lazy_hash(const MgPf *pf, const MgCtx *ctx) {
    uint64_t h = (uint64_t)(uintptr_t)pf * UINT64_C(0x9e3779b97f4a7c15) ^
                 (uint64_t)(uintptr_t)ctx * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 31;
    return (uint32_t)h;
}

static const LazySlot *lazy_get(const LazyMemo *m, const MgPf *pf, const MgCtx *ctx) {
    if (!m->cap)
        return NULL;
    for (uint32_t i = lazy_hash(pf, ctx) & (m->cap - 1u);; i = (i + 1u) & (m->cap - 1u)) {
        if (!m->slots[i].pf)
            return NULL;
        if (m->slots[i].pf == pf && m->slots[i].ctx == ctx)
            return &m->slots[i];
    }
}

/* With a recorder the memo is required (a node's row address is found
 * through it): a failed growth is then a resource stop, not a skip. */
static bool lazy_put(LazyMemo *m, const MgPf *pf, const MgCtx *ctx, MgTm *prop,
                     uint64_t address) {
    if (m->frozen)
        return false;
    if ((m->len + 1u) * 2u > m->cap) {
        uint32_t cap = m->cap ? m->cap * 2u : 256u;
        LazySlot *slots = mg_sys_alloc(sizeof(LazySlot) * cap);
        if (!slots) {
            m->frozen = true;
            return false;
        }
        memset(slots, 0, sizeof(LazySlot) * cap);
        for (uint32_t j = 0; j < m->cap; ++j) {
            LazySlot e = m->slots[j];
            if (!e.pf)
                continue;
            uint32_t i = lazy_hash(e.pf, e.ctx) & (cap - 1u);
            while (slots[i].pf)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e;
        }
        mg_sys_free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    uint32_t i = lazy_hash(pf, ctx) & (m->cap - 1u);
    while (m->slots[i].pf)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (LazySlot){pf, ctx, prop, address};
    ++m->len;
    return true;
}

typedef struct {
    MgCheck *c;
    LazyMemo memo;
    MgRowRecorder *recorder;   /* records each distinct step as a row */
} LazyRun;

static MgTm *lazy_refuse(MgCheck *c, const char *why) {
    mg_fail(c, MG_REFUSED, why);
    return NULL;
}

static MgTm *tmlist_nth(MgTmList *l, uint32_t i) {
    while (l && i) {
        l = l->tail;
        --i;
    }
    return l ? l->head : NULL;
}

static MgTm *lazy_infer(LazyRun *run, MgCtx *ctx, MgPf *pf, uint64_t *addr) {
    MgCheck *c = run->c;
    const LazySlot *hit = lazy_get(&run->memo, pf, ctx);
    if (hit) {
        *addr = hit->address;
        return hit->prop;
    }
    if (!live(c, 1u))
        return NULL;
    if (++c->depth > c->depth_limit) {
        --c->depth;
        mg_fail(c, MG_CAPACITY, "proof depth");
        return NULL;
    }
    MgStore *s = ST(c);
    MgTm *r = NULL;
    /* the step as a row: its witness and its premises' rows */
    MgWitness w;
    memset(&w, 0, sizeof(w));
    uint64_t parents[2] = {0, 0};
    uint32_t nparents = 0;
    switch (pf->tag) {
    case MG_PF_GPA:
        lazy_refuse(c, "automation placeholder is not a proof");
        break;
    case MG_PF_HYP:
        w.rule = MG_RULE_HYP;
        w.index = pf->n;
        r = tmlist_nth(ctx->hyps, pf->n);
        if (!r)
            lazy_refuse(c, "no such hypothesis");
        break;
    case MG_PF_KNOWN: {
        w.rule = MG_RULE_KNOWN;
        w.name = pf->n;
        const MgDecl *d = mg_env_lookup(c, pf->n, MG_NS_KNOWN);
        r = d ? d->prop : lazy_refuse(c, "unknown proposition");
        break;
    }
    case MG_PF_TMAP: {
        MgTm *q = lazy_infer(run, ctx, pf->p, &parents[0]);
        nparents = 1;
        MgTm *h = q ? mg_whnf(c, q) : NULL;
        if (!h)
            break;
        if (h->tag != MG_TM_ALL) {
            lazy_refuse(c, "term application needs a quantified premise");
            break;
        }
        MgTp *actual = mg_infer(c, ctx->depth, ctx->terms, pf->t);
        if (!actual)
            break;
        if (actual != h->tp) {
            lazy_refuse(c, "term argument has the wrong type");
            break;
        }
        w.rule = MG_RULE_TERM_APP;
        w.arg = pf->t;
        w.type = h->tp;
        w.body = h->f;
        r = mg_tm_instantiate(c, h->f, 0, pf->t);
        break;
    }
    case MG_PF_PFAP: {
        MgTm *q = lazy_infer(run, ctx, pf->p, &parents[0]);
        MgTm *h = q ? mg_whnf(c, q) : NULL;
        if (!h)
            break;
        if (h->tag != MG_TM_IMP) {
            lazy_refuse(c, "proof application needs an implication premise");
            break;
        }
        MgTm *arg = lazy_infer(run, ctx, pf->q, &parents[1]);
        nparents = 2;
        if (!arg)
            break;
        bool same = false;
        if (mg_conv(c, arg, h->f, &same) != MG_OK)
            break;
        if (!same) {
            lazy_refuse(c, "premise judgment does not match the rule");
            break;
        }
        w.rule = MG_RULE_PROOF_APP;
        w.domain = h->f;
        r = h->x;
        break;
    }
    case MG_PF_TLAM: {
        if (!mg_tp_plain_wf(pf->tp, ctx->depth)) {
            lazy_refuse(c, "ill-formed quantifier type");
            break;
        }
        MgTpList *terms = mg_tplist_cons(s, pf->tp, ctx->terms);
        MgTmList *hyps = ctx->hyps ? mg_tmlist_shift1(c, ctx->hyps) : NULL;
        MgCtx *inner = terms && (!ctx->hyps || hyps) ? mg_ctx(s, ctx->depth, terms, hyps) : NULL;
        if (!inner) {
            if (c->status == MG_OK)
                mg_fail(c, MG_NOMEM, "allocation");
            break;
        }
        MgTm *q = lazy_infer(run, inner, pf->p, &parents[0]);
        nparents = 1;
        w.rule = MG_RULE_TERM_LAM;
        w.type = pf->tp;
        r = q ? mg_tm_all(s, pf->tp, q) : NULL;
        if (q && !r)
            mg_fail(c, MG_NOMEM, "allocation");
        break;
    }
    case MG_PF_PLAM: {
        MgTp *type = mg_infer(c, ctx->depth, ctx->terms, pf->t);
        if (!type)
            break;
        if (type != s->tp_prop) {
            lazy_refuse(c, "discharged hypothesis is not a proposition");
            break;
        }
        MgTmList *hyps = mg_tmlist_cons(s, pf->t, ctx->hyps);
        MgCtx *inner = hyps ? mg_ctx(s, ctx->depth, ctx->terms, hyps) : NULL;
        if (!inner) {
            mg_fail(c, MG_NOMEM, "allocation");
            break;
        }
        MgTm *q = lazy_infer(run, inner, pf->p, &parents[0]);
        nparents = 1;
        w.rule = MG_RULE_PROOF_LAM;
        w.source = pf->t;
        r = q ? mg_tm_imp(s, pf->t, q) : NULL;
        if (q && !r)
            mg_fail(c, MG_NOMEM, "allocation");
        break;
    }
    case MG_PF_TPAP: {
        MgTm *q = lazy_infer(run, ctx, pf->p, &parents[0]);
        nparents = 1;
        MgTm *h = q ? mg_whnf(c, q) : NULL;
        if (!h)
            break;
        if (h->tag != MG_TM_TALL) {
            lazy_refuse(c, "type application needs a type-quantified premise");
            break;
        }
        w.rule = MG_RULE_TYPE_APP;
        w.type = pf->tp;
        w.body = h->f;
        r = mg_tm_tinstantiate(c, h->f, 0, pf->tp);
        break;
    }
    case MG_PF_TPLAM: {
        if (ctx->terms || ctx->hyps) {
            lazy_refuse(c, "type generalization needs empty term and hypothesis contexts");
            break;
        }
        if (ctx->depth == UINT32_MAX) {
            mg_fail(c, MG_CAPACITY, "type depth");
            break;
        }
        MgCtx *inner = mg_ctx(s, ctx->depth + 1u, NULL, NULL);
        if (!inner) {
            mg_fail(c, MG_NOMEM, "allocation");
            break;
        }
        MgTm *q = lazy_infer(run, inner, pf->p, &parents[0]);
        nparents = 1;
        w.rule = MG_RULE_TYPE_LAM;
        r = q ? mg_tm_tall(s, q) : NULL;
        if (q && !r)
            mg_fail(c, MG_NOMEM, "allocation");
        break;
    }
    default:
        mg_fail(c, MG_MALFORMED, "unknown proof constructor");
        break;
    }
    --c->depth;
    if (r && c->status == MG_OK) {
        uint64_t address = 0;
        if (run->recorder) {
            address = run->recorder->row(run->recorder->context, (MgJudgment){ctx, r}, &w,
                                         parents, nparents);
            if (address == UINT64_MAX) {
                mg_fail(c, MG_NOMEM, "evidence row");
                return NULL;
            }
        }
        if (!lazy_put(&run->memo, pf, ctx, r, address) && run->recorder) {
            mg_fail(c, MG_NOMEM, "allocation");
            return NULL;
        }
        *addr = address;
        return r;
    }
    if (c->status == MG_OK)
        mg_fail(c, MG_NOMEM, "allocation");
    return NULL;
}

/* ── formed judgments ─────────────────────────────────────────────────── */

bool mg_ctx_formed(MgCheck *c, MgCtx *ctx) {
    if (!ctx->hyps)
        return true;
    if (ctx->formed && ctx->formed_rev <= c->env.rev &&
        !mg_theory_withdrawn_between(c->env.theory, ctx->formed_rev, c->env.rev))
        return true;
    for (MgTmList *l = ctx->hyps; l; l = l->tail) {
        MgTp *type = mg_infer(c, ctx->depth, ctx->terms, l->head);
        if (!type)
            return false;
        if (type != ST(c)->tp_prop) {
            mg_fail(c, MG_REFUSED, "a hypothesis of the context is not a proposition");
            return false;
        }
    }
    ctx->formed = true;
    ctx->formed_rev = c->env.rev;
    return true;
}

bool mg_prop_formed(MgCheck *c, uint32_t depth, MgTpList *terms, MgTm *m) {
    uint32_t entered = 0;
    bool ok = true;
    for (;;) {
        if (m->facts & MG_TM_HAS_TYPEREG) {
            if (m->tag == MG_TM_TALL) {
                /* closed in term variables (Protected) */
                m = m->f;
                ++depth;
                terms = NULL;
                continue;
            }
            if (m->tag == MG_TM_ALL) {
                if (!mg_tp_plain_wf(m->tp, depth)) {
                    ok = mg_fail(c, MG_REFUSED, "ill-formed quantifier type");
                    break;
                }
                terms = mg_tplist_cons(ST(c), m->tp, terms);
                if (!terms) {
                    ok = mg_fail(c, MG_NOMEM, "allocation");
                    break;
                }
                m = m->f;
                continue;
            }
            if (m->tag == MG_TM_IMP) {
                if (++c->depth > c->depth_limit) {
                    --c->depth;
                    ok = mg_fail(c, MG_CAPACITY, "proposition depth");
                    break;
                }
                ++entered;
                if (!mg_prop_formed(c, depth, terms, m->f)) {
                    ok = false;
                    break;
                }
                m = m->x;
                continue;
            }
        }
        MgTp *t = mg_infer(c, depth, terms, m);
        if (!t)
            ok = false;
        else if (t != ST(c)->tp_prop)
            ok = mg_fail(c, MG_REFUSED, "not a proposition");
        break;
    }
    c->depth -= entered;
    return ok && c->status == MG_OK;
}

MgStatus mg_ctx_conv(MgCheck *c, MgCtx *a, MgCtx *b, bool *same) {
    *same = a == b;
    if (*same || a->depth != b->depth || a->terms != b->terms)
        return c->status;
    MgTmList *x = a->hyps, *y = b->hyps;
    while (x != y) {
        if (!x || !y)
            return c->status;   /* different lengths */
        bool equal = x->head == y->head;
        if (!equal && mg_conv(c, x->head, y->head, &equal) != MG_OK)
            return c->status;
        if (!equal)
            return c->status;
        x = x->tail;
        y = y->tail;
    }
    *same = true;
    return c->status;
}

/* ── lazy LCF steps ───────────────────────────────────────────────────── */

/* Whether two terms are convertible, failing the check when undecided. */
static bool lazy_same(MgCheck *c, MgTm *a, MgTm *b, bool *same) {
    *same = a == b;
    return *same || mg_conv(c, a, b, same) == MG_OK;
}

/* A stated operand is typed before it is converted (see mg_ctx_formed). */
static bool lazy_stated(MgCheck *c, const MgCtx *ctx, MgTm *m) {
    return mg_prop_formed(c, ctx->depth, ctx->terms, m);
}

/* Context.termBinder inverted: the hypotheses without the generalized
 * variable.  The reference rule sees normal forms; a source-form
 * hypothesis can mention the variable while its normal form does not, so
 * exactly such a hypothesis is normalized (it is formed, so this ends). */
static MgTmList *lazy_unshift(MgCheck *c, MgTmList *hyps) {
    bool dependent = false;
    for (MgTmList *l = hyps; l && !dependent; l = l->tail)
        dependent = mg_tm_free_db(l->head, 0);
    if (!dependent)
        return mg_tmlist_unshift1(c, hyps);
    uint32_t n = mg_tmlist_len(hyps), k = 0;
    MgTm **items = mg_sys_alloc(sizeof(MgTm *) * n);
    if (!items) {
        mg_fail(c, MG_NOMEM, "allocation");
        return NULL;
    }
    for (MgTmList *l = hyps; l; l = l->tail) {
        MgTm *h = l->head;
        if (mg_tm_free_db(h, 0)) {
            h = mg_tm_normalize(c, h);
            if (h && mg_tm_free_db(h, 0)) {
                mg_fail(c, MG_REFUSED, "hypothesis depends on the generalized variable");
                h = NULL;
            }
        }
        MgTm *shifted = h ? mg_tm_shift(c, h, 0, -1) : NULL;
        if (!shifted)
            break;
        items[k++] = shifted;
    }
    MgTmList *r = NULL;
    for (uint32_t i = k; k == n && i > 0u; --i)
        if (!(r = mg_tmlist_cons(ST(c), items[i - 1u], r)))
            break;
    mg_sys_free(items);
    if (!r && c->status == MG_OK)
        mg_fail(c, MG_NOMEM, "allocation");
    return c->status == MG_OK ? r : NULL;
}

MgStatus mg_step_lazy(MgCheck *c, MgWitness *w, MgCtx *ctx,
                      const MgJudgment *p, uint32_t count, MgJudgment *out) {
    for (uint32_t i = 0; i < count; ++i)
        if (!mg_judgment_safe(p[i])) {
            mg_fail(c, MG_MALFORMED, "premise outside the protected fragment");
            return MG_MALFORMED;
        }
    if (ctx && !ctx->safe) {
        mg_fail(c, MG_MALFORMED, "context outside the protected fragment");
        return MG_MALFORMED;
    }
    /* Stated operands must be protected; omitted ones are filled from the
     * premises (and are then protected with them). */
    if ((w->arg && !mg_tm_protected(w->arg)) || (w->body && !mg_tm_protected(w->body)) ||
        (w->domain && !mg_tm_protected(w->domain)) ||
        (w->source && !mg_tm_protected(w->source))) {
        mg_fail(c, MG_MALFORMED, "operand outside the protected fragment");
        return MG_MALFORMED;
    }
    if (!live(c, 1u))
        return c->status;
    MgStore *s = ST(c);
    MgJudgment r = {NULL, NULL};
    bool same = false;
    switch (w->rule) {
    case MG_RULE_HYP:
        if (count != 0u || !ctx) {
            lazy_refuse(c, "hypothesis takes no premises");
            break;
        }
        if (!mg_ctx_formed(c, ctx))
            break;
        r = (MgJudgment){ctx, tmlist_nth(ctx->hyps, w->index)};
        if (!r.prop)
            lazy_refuse(c, "no such hypothesis");
        break;
    case MG_RULE_KNOWN: {
        if (count != 0u || !ctx) {
            lazy_refuse(c, "known proposition takes no premises");
            break;
        }
        const MgDecl *d = mg_env_lookup(c, w->name, MG_NS_KNOWN);
        if (!d) {
            lazy_refuse(c, "unknown proposition");
            break;
        }
        if (!mg_ctx_formed(c, ctx))
            break;
        r = (MgJudgment){ctx, d->prop};
        break;
    }
    case MG_RULE_TERM_APP: {
        if (count != 1u) {
            lazy_refuse(c, "term application needs one premise");
            break;
        }
        MgTm *q = mg_whnf(c, p[0].prop);
        if (!q)
            break;
        if (q->tag != MG_TM_ALL) {
            lazy_refuse(c, "term application needs a quantified premise");
            break;
        }
        if (!w->arg) {
            mg_fail(c, MG_MALFORMED, "term application needs an argument");
            break;
        }
        MgTp *actual = mg_infer(c, p[0].ctx->depth, p[0].ctx->terms, w->arg);
        if (!actual)
            break;
        if (actual != q->tp) {
            lazy_refuse(c, "term argument has the wrong type");
            break;
        }
        if (w->type && w->type != q->tp) {
            lazy_refuse(c, "the stated domain is not the premise's");
            break;
        }
        if (w->body && w->body != q->f) {
            MgTm *stated = mg_tm_all(s, q->tp, w->body);
            if (!stated) {
                mg_fail(c, MG_NOMEM, "allocation");
                break;
            }
            if (!lazy_stated(c, p[0].ctx, stated) || !lazy_same(c, stated, p[0].prop, &same))
                break;
            if (!same) {
                lazy_refuse(c, "the stated body is not the premise's");
                break;
            }
        } else {
            w->body = q->f;
        }
        w->type = q->tp;
        MgTm *instance = mg_tm_instantiate(c, w->body, 0, w->arg);
        if (instance)
            r = (MgJudgment){p[0].ctx, instance};
        break;
    }
    case MG_RULE_PROOF_APP: {
        if (count != 2u) {
            lazy_refuse(c, "proof application needs two premises");
            break;
        }
        if (mg_ctx_conv(c, p[0].ctx, p[1].ctx, &same) != MG_OK)
            break;
        if (!same) {
            lazy_refuse(c, "premise judgment does not match the rule");
            break;
        }
        MgTm *q = mg_whnf(c, p[0].prop);
        if (!q)
            break;
        if (q->tag != MG_TM_IMP) {
            lazy_refuse(c, "proof application needs an implication premise");
            break;
        }
        if (!lazy_same(c, p[1].prop, q->f, &same))
            break;
        if (!same) {
            lazy_refuse(c, "premise judgment does not match the rule");
            break;
        }
        if (w->domain && w->domain != q->f) {
            if (!lazy_stated(c, p[0].ctx, w->domain) || !lazy_same(c, w->domain, q->f, &same))
                break;
            if (!same) {
                lazy_refuse(c, "the stated domain is not the premise's");
                break;
            }
        } else {
            w->domain = q->f;
        }
        r = (MgJudgment){p[0].ctx, q->x};
        break;
    }
    case MG_RULE_PROOF_LAM: {
        if (count != 1u) {
            lazy_refuse(c, "discharge needs one premise");
            break;
        }
        MgCtx *inner = p[0].ctx;
        if (!inner->hyps) {
            lazy_refuse(c, "premise has no hypothesis to discharge");
            break;
        }
        MgCtx *outer = mg_ctx(s, inner->depth, inner->terms, inner->hyps->tail);
        if (!outer) {
            mg_fail(c, MG_NOMEM, "allocation");
            break;
        }
        /* As the reference rule does, whether or not a source is stated:
         * the discharged hypothesis must be a proposition (a context given
         * as data might hold anything). */
        if (!w->source)
            w->source = inner->hyps->head;
        MgTp *type = mg_infer(c, outer->depth, outer->terms, w->source);
        if (!type)
            break;
        if (type != s->tp_prop) {
            lazy_refuse(c, "discharged hypothesis is not a proposition");
            break;
        }
        if (!lazy_same(c, w->source, inner->hyps->head, &same))
            break;
        if (!same) {
            lazy_refuse(c, "discharged hypothesis does not match");
            break;
        }
        MgTm *imp = mg_tm_imp(s, inner->hyps->head, p[0].prop);
        if (!imp)
            mg_fail(c, MG_NOMEM, "allocation");
        else
            r = (MgJudgment){outer, imp};
        break;
    }
    case MG_RULE_TYPE_APP: {
        if (count != 1u) {
            lazy_refuse(c, "type application needs one premise");
            break;
        }
        MgTm *q = mg_whnf(c, p[0].prop);
        if (!q)
            break;
        if (q->tag != MG_TM_TALL) {
            lazy_refuse(c, "type application needs a type-quantified premise");
            break;
        }
        if (!w->type) {
            mg_fail(c, MG_MALFORMED, "type application needs a type");
            break;
        }
        if (w->body && w->body != q->f) {
            MgTm *stated = mg_tm_tall(s, w->body);
            if (!stated) {
                mg_fail(c, MG_NOMEM, "allocation");
                break;
            }
            if (!lazy_stated(c, p[0].ctx, stated) || !lazy_same(c, stated, p[0].prop, &same))
                break;
            if (!same) {
                lazy_refuse(c, "the stated body is not the premise's");
                break;
            }
        } else {
            w->body = q->f;
        }
        MgTm *instance = mg_tm_tinstantiate(c, w->body, 0, w->type);
        if (instance)
            r = (MgJudgment){p[0].ctx, instance};
        break;
    }
    case MG_RULE_TERM_LAM: {
        if (count != 1u) {
            lazy_refuse(c, "generalization needs one premise");
            break;
        }
        MgCtx *inner = p[0].ctx;
        if (!inner->terms) {
            lazy_refuse(c, "premise does not bind a term variable");
            break;
        }
        if (!w->type)
            w->type = inner->terms->head;
        if (inner->terms->head != w->type) {
            lazy_refuse(c, "premise does not bind a variable of this type");
            break;
        }
        MgTmList *hyps = inner->hyps ? lazy_unshift(c, inner->hyps) : NULL;
        if (inner->hyps && !hyps)
            break;
        if (!mg_tp_plain_wf(w->type, inner->depth)) {
            lazy_refuse(c, "ill-formed quantifier type");
            break;
        }
        MgCtx *outer = mg_ctx(s, inner->depth, inner->terms->tail, hyps);
        MgTm *all = outer ? mg_tm_all(s, w->type, p[0].prop) : NULL;
        if (!all)
            mg_fail(c, MG_NOMEM, "allocation");
        else
            r = (MgJudgment){outer, all};
        break;
    }
    case MG_RULE_TYPE_LAM:
        /* Structural: no proposition or hypothesis is compared, so the
         * reference rule decides it exactly as after normalization. */
        return mg_step(c, w, ctx, p, count, out);
    default:
        mg_fail(c, MG_MALFORMED, "unknown rule");
        break;
    }
    if (c->status != MG_OK)
        return c->status;
    if (!r.prop) {
        mg_fail(c, MG_NOMEM, "allocation");
        return MG_NOMEM;
    }
    /* The invariant is preserved by every rule (SourceProofInvariant), and
     * the filled witness is a complete row witness. */
    if (!mg_judgment_safe(r) || !mg_witness_safe(w)) {
        mg_fail(c, MG_REFUSED, "internal: protected invariant violated");
        return MG_REFUSED;
    }
    *out = r;
    return MG_OK;
}

MgStatus mg_infer_proof_lazy(MgCheck *c, MgCtx *ctx, MgPf *proof, MgTm **prop) {
    *prop = NULL;
    if (!ctx || !proof)
        return MG_MALFORMED;
    if (!ctx->safe || !proof->payload_protected) {
        mg_fail(c, MG_MALFORMED, "proof outside the protected fragment");
        return MG_MALFORMED;
    }
    if (!mg_ctx_formed(c, ctx))
        return c->status;
    LazyRun run = {.c = c};
    uint64_t root = 0;
    MgTm *r = lazy_infer(&run, ctx, proof, &root);
    mg_sys_free(run.memo.slots);
    if (!r)
        return c->status == MG_OK ? MG_NOMEM : c->status;
    *prop = r;
    return MG_OK;
}

MgStatus mg_infer_proof_lazy_recorded(MgCheck *c, MgCtx *ctx, MgPf *proof,
                                      MgRowRecorder *recorder, MgTm **prop,
                                      uint64_t *root) {
    *prop = NULL;
    if (!ctx || !proof)
        return MG_MALFORMED;
    if (!ctx->safe || !proof->payload_protected) {
        mg_fail(c, MG_MALFORMED, "proof outside the protected fragment");
        return MG_MALFORMED;
    }
    if (!mg_ctx_formed(c, ctx))
        return c->status;
    LazyRun run = {.c = c, .recorder = recorder};
    uint64_t address = 0;
    MgTm *r = lazy_infer(&run, ctx, proof, &address);
    mg_sys_free(run.memo.slots);
    if (!r)
        return c->status == MG_OK ? MG_NOMEM : c->status;
    *prop = r;
    if (root)
        *root = address;
    return MG_OK;
}
