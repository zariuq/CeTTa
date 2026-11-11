/* Megalodon's typed megaauto (megaauto.ml type_megaauto and its helpers),
 * behind ?t:SALT? placeholders.  It proves goals x :e A and equations
 * between sets.  The proofs use the Egal constants and knowns that the
 * elaborator recognizes by identity as they are declared (mg_elab.c
 * mge_megaauto_item, mge_megaauto_known).
 *
 * The search is Megalodon's, choice for choice.  A word of the salt decides
 * each branch, and every step hashes the salt with its own tag, so a salt
 * names one attempt.  OCaml's exceptions are the elaborator's raises
 * (SearchLimit, SearchBacktrack, Not_found), and each try block is one
 * mge_try_run, covering what the OCaml block covers. */

#include "mg_elab_internal.h"

#include <string.h>

/* Below this a salt word means "7/8 of the time". */
#define SEVEN_EIGHTHS 1610612734

static const char IN_ID[] = "73c9efe869770ab42f7cde0b33fe26bbc3e2bd157dad141c0c27d1e7348d60f5";
static const char EQ_ID[] = "5a6af35fb6d6bea477dd0f822b8e01ca0d57cc50dfd41744307bc94597fdaa4a";

typedef struct {
    MgPf *d;
    MgTm *t;    /* the type extracted, or the reduct */
} PfTm;

/* ── terms, proofs, salts ─────────────────────────────────────────────── */

static int32_t word(const MgHash *h, int k) {
    const uint8_t *b = h->b + 4 * k;
    return (int32_t)((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | (uint32_t)b[3]);
}

static MgHash hashtag(const MgHash *salt, uint32_t tag) { return mg_hash_tag(salt, tag); }

static MgPf *ck(MgElab *e, MgPf *p) {
    if (!p)
        mge_nomem(e);
    return p;
}

static MgPf *hyp(MgElab *e, uint32_t i) { return ck(e, mg_pf_hyp(e->s, i)); }
static MgPf *known(MgElab *e, uint32_t h) { return ck(e, mg_pf_known(e->s, h)); }
static MgPf *tmap(MgElab *e, MgPf *p, MgTm *m) { return ck(e, mg_pf_tmap(e->s, p, m)); }
static MgPf *pfap(MgElab *e, MgPf *p, MgPf *q) { return ck(e, mg_pf_pfap(e->s, p, q)); }
static MgPf *plam(MgElab *e, MgTm *t, MgPf *p) { return ck(e, mg_pf_plam(e->s, t, p)); }
static MgPf *tlam(MgElab *e, MgTp *a, MgPf *p) { return ck(e, mg_pf_tlam(e->s, a, p)); }

static MgPf *tmap3(MgElab *e, MgPf *p, MgTm *x, MgTm *y, MgTm *z) {
    return tmap(e, tmap(e, tmap(e, p, x), y), z);
}

static MgTp *set(MgElab *e) { return mge_ty_set(e); }
static MgTp *rel(MgElab *e, MgTp *a) { return mge_ty_arr(e, a, mge_ty_arr(e, a, mge_ty_prop(e))); }
static MgTm *db(MgElab *e, uint32_t i) { return mge_tm_db(e, i); }
static MgTm *th(MgElab *e, uint32_t h) { return mge_tm_h(e, h); }
static MgTm *ap(MgElab *e, MgTm *f, MgTm *x) { return mge_tm_ap(e, f, x); }
static MgTm *ap2(MgElab *e, MgTm *f, MgTm *x, MgTm *y) { return ap(e, ap(e, f, x), y); }
static MgTm *lam_set(MgElab *e, MgTm *b) { return mge_tm_lam(e, set(e), b); }
static MgTm *sh(MgElab *e, int32_t j, MgTm *m) { return mge_tmshift(e, 0u, j, m); }

/* Ap(Ap(Prim 1, x), a): megaauto takes Prim 1 to be In */
static MgTm *in(MgElab *e, MgTm *x, MgTm *a) { return ap2(e, mge_tm_prim(e, 1u), x, a); }

static MgTm *eq_(MgElab *e, MgTp *a, MgTm *l, MgTm *r) {
    return ap2(e, mge_tm_tap(e, th(e, mge_name_id(e, EQ_ID)), a), l, r);
}

/* Ap(Ap(TmH h, x), y), h a constant recognized (never NONE) */
static bool app2(const MgTm *m, uint32_t h, MgTm **x, MgTm **y) {
    if (h == NONE || m->tag != MG_TM_AP || m->f->tag != MG_TM_AP || m->f->f->tag != MG_TM_NAME ||
        m->f->f->n != h)
        return false;
    *x = m->f->x;
    *y = m->x;
    return true;
}

static bool named(MgElab *e, const MgTm *m, const char *id) {
    return m->tag == MG_TM_NAME && strcmp(mge_name_text(e, m->n), id) == 0;
}

/* x :e a, as Prim 1 or as the identity of In */
static bool is_in(MgElab *e, const MgTm *m, MgTm **x, MgTm **a) {
    if (m->tag != MG_TM_AP || m->f->tag != MG_TM_AP)
        return false;
    const MgTm *h = m->f->f;
    if (!(h->tag == MG_TM_PRIM && h->n == 1u) && !named(e, h, IN_ID))
        return false;
    *x = m->f->x;
    *a = m->x;
    return true;
}

/* Ap(Ap(TpAp(TmH eq, Set), l), r) */
static bool is_eqset(MgElab *e, const MgTm *m, MgTm **l, MgTm **r) {
    if (m->tag != MG_TM_AP || m->f->tag != MG_TM_AP)
        return false;
    const MgTm *h = m->f->f;
    if (h->tag != MG_TM_TAP || h->tp != set(e) || !named(e, h->f, EQ_ID))
        return false;
    *l = m->f->x;
    *r = m->x;
    return true;
}

/* gen_lam_body */
static MgTm *gen_lam_body(MgElab *e, MgTm *n) {
    return n->tag == MG_TM_LAM ? n->f : ap(e, sh(e, 1, n), db(e, 0u));
}

/* syntax.ml free_in_tm_p */
static bool free_in(const MgTm *m, uint32_t j) {
    if (m->fv <= j)
        return false;
    switch (m->tag) {
    case MG_TM_DB: return m->n == j;
    case MG_TM_AP: case MG_TM_IMP: return free_in(m->f, j) || free_in(m->x, j);
    case MG_TM_LAM: case MG_TM_ALL: return free_in(m->f, j + 1u);
    default: return false;   /* TpAp: closed, by invariant */
    }
}

static const TpCx *cx_set(MgElab *e, const TpCx *cx) {
    TpCx *c = TMP(e, TpCx);
    *c = (TpCx){set(e), cx};
    return c;
}

/* tphyp :: List.map (tmshift 0 1) hyps */
static const PfCx *hyps_under(MgElab *e, MgTm *tphyp, const PfCx *hyps) {
    PfCx *c = TMP(e, PfCx);
    *c = (PfCx){tphyp, mge_pfcx_shift(e, hyps)};
    return c;
}

__attribute__((noreturn)) static void search_limit(MgElab *e) { mge_raise_kind(e, RAISE_SEARCHLIMIT); }
__attribute__((noreturn)) static void search_backtrack(MgElab *e) { mge_raise_kind(e, RAISE_SEARCHBACKTRACK); }

static void step(MgElab *e, int lm) {
    if (lm <= 0 || e->searchlim <= 0)
        search_limit(e);
    --e->searchlim;
}

/* pf_eqset_ref, pf_eqset_sym and pf_eqset_tra: reflexivity, symmetry and
 * transitivity of equality on sets, with the symmetric Leibniz equality */
static MgPf *eqset_ref(MgElab *e) {
    MgTp *a = set(e);
    return tlam(e, a, tlam(e, rel(e, a), plam(e, ap2(e, db(e, 0u), db(e, 1u), db(e, 1u)), hyp(e, 0u))));
}

static MgPf *eqset_sym(MgElab *e) {
    MgTp *a = set(e);
    MgTm *motive = lam_set(e, lam_set(e, ap2(e, db(e, 2u), db(e, 0u), db(e, 1u))));
    return tlam(e, a, tlam(e, a, plam(e, eq_(e, a, db(e, 1u), db(e, 0u)),
                                      tlam(e, rel(e, a), tmap(e, hyp(e, 0u), motive)))));
}

static MgPf *eqset_tra(MgElab *e) {
    MgTp *a = set(e);
    MgTm *motive = mge_tm_lam(e, a, mge_tm_lam(e, a, eq_(e, a, db(e, 4u), db(e, 1u))));
    MgPf *body = pfap(e, tmap(e, hyp(e, 0u), motive), hyp(e, 1u));
    return tlam(e, a, tlam(e, a, tlam(e, a, plam(e, eq_(e, a, db(e, 2u), db(e, 1u)),
                                                  plam(e, eq_(e, a, db(e, 1u), db(e, 0u)), body)))));
}

/* d :: x = y gives y = x */
static MgPf *sym(MgElab *e, MgTm *x, MgTm *y, MgPf *d) { return pfap(e, tmap(e, tmap(e, eqset_sym(e), x), y), d); }

/* x = y and y = z give x = z */
static MgPf *tra(MgElab *e, MgTm *x, MgTm *y, MgTm *z, MgPf *dxy, MgPf *dyz) {
    return pfap(e, pfap(e, tmap3(e, eqset_tra(e), x, y, z), dxy), dyz);
}

/* tm_id m: compared with set0, set1 and set2, open terms included */
static uint32_t id_of(MgElab *e, MgTm *m) { return mge_tm_id(e, m); }

/* headnorm salt m: beta-eta normal, then the head unfolded while the salt
 * says so (7/8 of the time per unfolding) */
static MgTm *headnorm(MgElab *e, MgHash salt, MgTm *m) {
    m = mge_beta_eta_norm(e, m);
    for (;;) {
        if (m->tag == MG_TM_LAM || m->tag == MG_TM_IMP || m->tag == MG_TM_ALL)
            return m;
        MgTm *h = m;
        uint32_t n = 0;
        for (; h->tag == MG_TM_AP; h = h->f)
            ++n;
        if (!mge_defp(e, h))
            return m;
        salt = hashtag(&salt, 24u);
        if (word(&salt, 7) >= SEVEN_EIGHTHS)
            return m;
        MgTm **args = n ? mge_chunk_alloc(e, &e->tmp, sizeof(MgTm *) * n) : NULL;
        MgTm *c = m;
        for (uint32_t k = n; k-- > 0; c = c->f)
            args[k] = c->x;
        m = mge_delta_exp(e, h, args, n);
    }
}

/* invert_tuple2: m is lam 2 (fun i => if i = 0 then m1 else m2), the
 * pair (m1, m2) */
static bool invert_tuple2(MgElab *e, MgTm *m, MgTm **m1, MgTm **m2) {
    MgTm *two, *mb;
    if (!app2(m, e->setlam, &two, &mb) || mb->tag != MG_TM_LAM || mb->tp != set(e))
        return false;
    if (id_of(e, two) != e->ma_set2)
        return false;
    mb = mb->f;
    if (mb->tag != MG_TM_AP || mb->f->tag != MG_TM_AP || mb->f->f->tag != MG_TM_AP)
        return false;
    const MgTm *hif = mb->f->f->f;
    bool ifset = hif->tag == MG_TM_NAME && e->ifopset != NONE && hif->n == e->ifopset;
    bool ifpoly = hif->tag == MG_TM_TAP && hif->tp == set(e) && hif->f->tag == MG_TM_NAME &&
                  e->ifop != NONE && hif->f->n == e->ifop;
    if (!ifset && !ifpoly)
        return false;
    MgTm *mcond = mb->f->f->x, *mth = mb->f->x, *mels = mb->x;
    MgTm *x, *m0;
    if (!is_eqset(e, mcond, &x, &m0) || x != db(e, 0u) || id_of(e, m0) != e->set0)
        return false;
    if (free_in(mth, 0u) || free_in(mels, 0u))
        return false;
    *m1 = sh(e, -1, mth);
    *m2 = sh(e, -1, mels);
    return true;
}

/* ── the search ───────────────────────────────────────────────────────── */

/* red_3's context hl, innermost first: hl u is u under these frames */
enum { HL_FUN, HL_ARG, HL_BOUND };
typedef struct Hl Hl;
struct Hl {
    uint8_t kind;       /* Ap(u, t); Ap(t, u); Ap(Ap(TmH h, u), t) */
    uint32_t h;
    MgTm *t;
    const Hl *next;
};

static const Hl *hl_push(MgElab *e, uint8_t kind, uint32_t h, MgTm *t, const Hl *next) {
    Hl *c = TMP(e, Hl);
    *c = (Hl){kind, h, t, next};
    return c;
}

static MgTm *hl_apply(MgElab *e, const Hl *hl, MgTm *u) {
    for (; hl; hl = hl->next)
        u = hl->kind == HL_FUN ? ap(e, u, hl->t) : hl->kind == HL_ARG ? ap(e, hl->t, u)
                                                                       : ap2(e, th(e, hl->h), u, hl->t);
    return u;
}

static MgPf *ta_of(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m, MgTm *a);
static MgPf *ta_of_2b(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m, MgTm *a);
static PfTm ta_extr(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m);
static PfTm ta_extr_2b(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m);
static MgPf *ta_conv(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m1, MgTm *m2);
static MgPf *ta_conv_2b(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m1, MgTm *m2);
static PfTm ta_red(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m);
static PfTm ta_red_2(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m);
static PfTm ta_red_3(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m, const Hl *hl);
static PfTm ta_red_4(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m);

/* One call of the search, to run inside a try block. */
typedef enum { C_OF2, C_EXTR2, C_CONV2, C_RED2, C_RED3, C_RED4 } CallFn;
typedef struct {
    CallFn fn;
    int lm;
    MgHash salt;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *m, *a;
    const Hl *hl;
    PfTm out;
} Call;

static void call_body(MgElab *e, void *ctx) {
    Call *c = ctx;
    switch (c->fn) {
    case C_OF2: c->out.d = ta_of_2b(e, c->lm, c->salt, c->cx, c->hyps, c->m, c->a); return;
    case C_EXTR2: c->out = ta_extr_2b(e, c->lm, c->salt, c->cx, c->hyps, c->m); return;
    case C_CONV2: c->out.d = ta_conv_2b(e, c->lm, c->salt, c->cx, c->hyps, c->m, c->a); return;
    case C_RED2: c->out = ta_red_2(e, c->lm, c->salt, c->cx, c->hyps, c->m); return;
    case C_RED3: c->out = ta_red_3(e, c->lm, c->salt, c->cx, c->hyps, c->m, c->hl); return;
    case C_RED4: c->out = ta_red_4(e, c->lm, c->salt, c->cx, c->hyps, c->m); return;
    }
}

/* try CALL with KIND -> ...: true when the call returned, false when it
 * raised KIND; every other raise goes on */
static bool attempt(MgElab *e, Call *c, int kind) {
    int k = mge_try_run(e, call_body, c);
    if (k == 0)
        return true;
    if (k != kind)
        mge_raise_kind(e, k);
    return false;
}

/* The orders type_megaauto_of and type_megaauto_conv try a pair and its
 * head normal forms in, SearchBacktrack moving to the next. */
static PfTm normal_forms(MgElab *e, Call *c, MgTm *m, MgTm *a, MgTm *mn, MgTm *an, bool s1neg) {
    MgTm *seq[4][2];
    int n = 0;
#define TRY(x, y) (seq[n][0] = (x), seq[n][1] = (y), ++n)
    if (mn == m && an == a) {
        TRY(m, a);
    } else {
        TRY(mn, an);
        if (mn == m) {
            TRY(m, an);
        } else if (an == a) {
            TRY(mn, a);
        } else if (s1neg) {
            TRY(m, an);
            TRY(mn, a);
        } else {
            TRY(mn, a);
            TRY(m, an);
        }
        TRY(m, a);
    }
#undef TRY
    for (int i = 0; i + 1 < n; ++i) {
        c->m = seq[i][0];
        c->a = seq[i][1];
        if (attempt(e, c, RAISE_SEARCHBACKTRACK))
            return c->out;
    }
    c->m = seq[n - 1][0];
    c->a = seq[n - 1][1];
    call_body(e, c);
    return c->out;
}

/* type_megaauto_of: a proof of m :e a */
static MgPf *ta_of(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m, MgTm *a) {
    MgHash salt2 = hashtag(&salt, 23u);
    MgTm *mn = word(&salt2, 6) < SEVEN_EIGHTHS ? headnorm(e, salt2, m) : m;
    MgTm *an = word(&salt2, 7) < SEVEN_EIGHTHS ? headnorm(e, salt2, a) : a;
    Call c = {C_OF2, lm, salt2, cx, hyps, NULL, NULL, NULL, {NULL, NULL}};
    return normal_forms(e, &c, m, a, mn, an, word(&salt2, 1) < 0).d;
}

/* lam b1 m1 :e Pi a1 a2 (exp false) or lam b1 m1 :e setexp a2 a1 (exp
 * true), through known_lam_Pi, converting b1 to a1 half the time when
 * they differ */
static MgPf *lam_in(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *b1, MgTm *m1,
                    MgTm *a1, MgTm *a2, bool exp) {
    uint32_t h = e->ma_setlambda, k = exp ? e->ma_setexp : e->ma_setPi;
    MgPf *eq = NULL;
    if (!mge_conv(e, a1, b1)) {
        if (word(&salt, 0) >= 0)
            search_backtrack(e);
        eq = ta_conv(e, lm - 1, salt, cx, hyps, b1, a1);
    }
    if (e->ma_lam_Pi == NONE)
        search_backtrack(e);
    MgTm *tphyp = in(e, db(e, 0u), sh(e, 1, a1));
    MgTm *a2b = exp ? sh(e, 1, a2) : gen_lam_body(e, a2);
    MgPf *d = ta_of(e, lm - 1, salt, cx_set(e, cx), hyps_under(e, tphyp, hyps), gen_lam_body(e, m1), a2b);
    MgPf *lamPi = tmap3(e, known(e, e->ma_lam_Pi), a1, exp && !eq ? lam_set(e, a2b) : a2, m1);
    MgPf *of = pfap(e, lamPi, tlam(e, set(e), plam(e, tphyp, d)));
    if (!eq)
        return of;
    MgTm *motive = lam_set(e, lam_set(e, in(e, ap2(e, th(e, h), db(e, 0u), sh(e, 2, m1)),
                                            ap2(e, th(e, k), sh(e, 2, a1), sh(e, 2, a2)))));
    return pfap(e, tmap(e, eq, motive), of);
}

typedef struct {
    int lm;
    MgHash salt;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *m, *a;
    MgPf *out;
} OfTuple;

/* type_megaauto_of_2b's last case, the part its Not_found handler covers:
 * a pair in a Sigma, a product or a sum */
static void of_tuple_body(MgElab *e, void *ctx) {
    OfTuple *t = ctx;
    MgTm *m1, *m2, *a1, *a2;
    if (!invert_tuple2(e, t->m, &m1, &m2))
        mge_not_found(e);
    if (app2(t->a, e->ma_setlambda, &a1, &a2)) {   /* lam is Sigma */
        MgPf *d1 = ta_of(e, t->lm, t->salt, t->cx, t->hyps, m1, a1);
        MgTm *a2b = gen_lam_body(e, a2);
        MgPf *d2 = ta_of(e, t->lm, t->salt, t->cx, t->hyps, m2, mge_tm_subst0(e, a2b, m1));
        if (e->ma_tuple_2_Sigma == NONE)
            search_backtrack(e);
        t->out = pfap(e, tmap(e, pfap(e, tmap3(e, known(e, e->ma_tuple_2_Sigma), a1, a2, m1), d1), m2), d2);
        return;
    }
    if (app2(t->a, e->ma_setprod, &a1, &a2)) {
        MgPf *d1 = ta_of(e, t->lm, t->salt, t->cx, t->hyps, m1, a1);
        MgPf *d2 = ta_of(e, t->lm, t->salt, t->cx, t->hyps, m2, a2);
        if (e->ma_tuple_2_setprod == NONE)
            search_backtrack(e);
        t->out = pfap(e, tmap(e, pfap(e, tmap3(e, known(e, e->ma_tuple_2_setprod), a1, a2, m1), d1), m2), d2);
        return;
    }
    if (app2(t->a, e->ma_setsum, &a1, &a2)) {
        uint32_t h1 = id_of(e, m1);
        uint32_t ks;
        MgPf *d;
        if (h1 == e->set0) {
            d = ta_of(e, t->lm - 1, t->salt, t->cx, t->hyps, m2, a1);
            ks = e->ma_tuple0_setsum;
        } else if (h1 == e->ma_set1) {
            d = ta_of(e, t->lm - 1, t->salt, t->cx, t->hyps, m2, a2);
            ks = e->ma_tuple1_setsum;
        } else {
            search_backtrack(e);
        }
        if (ks == NONE)
            search_backtrack(e);
        t->out = pfap(e, tmap3(e, known(e, ks), a1, a2, m2), d);
        return;
    }
    search_backtrack(e);
}

/* type_megaauto_of_2b */
static MgPf *ta_of_2b(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m, MgTm *a) {
    step(e, lm);
    MgTm *b1, *m1, *a1, *a2;
    if (app2(m, e->ma_setlambda, &b1, &m1)) {
        if (app2(a, e->ma_setPi, &a1, &a2))
            return lam_in(e, lm, salt, cx, hyps, b1, m1, a1, a2, false);
        if (app2(a, e->ma_setexp, &a2, &a1))
            return lam_in(e, lm, salt, cx, hyps, b1, m1, a1, a2, true);
    }
    OfTuple t = {lm, salt, cx, hyps, m, a, NULL};
    int k = mge_try_run(e, of_tuple_body, &t);
    if (k == 0)
        return t.out;
    if (k != RAISE_NOT_FOUND)
        mge_raise_kind(e, k);
    PfTm r = ta_extr(e, lm, salt, cx, hyps, m);
    if (mge_conv(e, a, r.t))
        return r.d;
    if (word(&salt, 0) >= 0)
        search_backtrack(e);
    MgPf *eq = ta_conv(e, lm - 1, salt, cx, hyps, a, r.t);
    /* Megalodon's motive, Prim 2 at its head as there */
    MgTm *motive = lam_set(e, lam_set(e, ap2(e, mge_tm_prim(e, 2u), sh(e, 2, m), db(e, 0u))));
    return pfap(e, tmap(e, eq, motive), r.d);
}

/* type_megaauto_extr: a type of m and a proof that m is in it */
static PfTm ta_extr(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m) {
    MgHash salt2 = hashtag(&salt, 23u);
    MgTm *mn = word(&salt2, 6) < SEVEN_EIGHTHS ? headnorm(e, salt2, m) : m;
    Call c = {C_EXTR2, lm, salt2, cx, hyps, mn, NULL, NULL, {NULL, NULL}};
    if (mn != m && attempt(e, &c, RAISE_SEARCHBACKTRACK))
        return c.out;
    c.m = m;
    call_body(e, &c);
    return c.out;
}

/* handle_ap: ap m1 m2 by the type of m1, a Pi or a setexp */
static PfTm handle_ap(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m1, MgTm *m2) {
    PfTm r = ta_extr(e, lm, salt, cx, hyps, m1);
    MgTm *b1, *b2;
    if (app2(r.t, e->ma_setPi, &b1, &b2)) {
        MgPf *d2 = ta_of(e, lm - 1, salt, cx, hyps, m2, b1);
        if (e->ma_ap_Pi == NONE)
            search_backtrack(e);
        MgPf *k = tmap(e, tmap3(e, known(e, e->ma_ap_Pi), b1, b2, m1), m2);
        return (PfTm){pfap(e, pfap(e, k, r.d), d2), mge_tm_subst0(e, gen_lam_body(e, b2), m2)};
    }
    if (app2(r.t, e->ma_setexp, &b2, &b1)) {
        MgPf *d2 = ta_of(e, lm - 1, salt, cx, hyps, m2, b1);
        if (e->ma_ap_Pi == NONE)
            search_backtrack(e);
        MgPf *k = tmap(e, tmap3(e, known(e, e->ma_ap_Pi), b1, lam_set(e, sh(e, 1, b2)), m1), m2);
        return (PfTm){pfap(e, pfap(e, k, r.d), d2), b2};
    }
    search_backtrack(e);
}

/* ap m1 0 (one false) or ap m1 1 (one true): a component of a pair in a
 * Sigma or a product */
static PfTm extr_component(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m1,
                           MgTm *m2, bool one) {
    PfTm r = ta_extr(e, lm, salt, cx, hyps, m1);
    uint32_t hk = one ? e->ma_ap1_Sigma : e->ma_ap0_Sigma;
    MgTm *b1, *b2;
    if (app2(r.t, e->ma_setlambda, &b1, &b2)) {   /* lam is Sigma */
        if (hk == NONE)
            search_backtrack(e);
        MgPf *d = pfap(e, tmap3(e, known(e, hk), b1, b2, m1), r.d);
        if (!one)
            return (PfTm){d, b1};
        MgTm *first = ap2(e, th(e, e->ma_setap), m1, mge_tm_prim(e, 2u));
        return (PfTm){d, mge_tm_subst0(e, gen_lam_body(e, b2), first)};
    }
    if (app2(r.t, e->ma_setprod, &b1, &b2)) {
        if (hk == NONE)
            search_backtrack(e);
        MgPf *d = pfap(e, tmap3(e, known(e, hk), b1, lam_set(e, sh(e, 1, b2)), m1), r.d);
        return (PfTm){d, one ? b2 : b1};
    }
    return handle_ap(e, lm, salt, cx, hyps, m1, m2);
}

typedef struct {
    int lm;
    MgHash salt;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *m;
    PfTm out;
} ExtrTuple;

/* type_megaauto_extr_2b's last case, the part its Not_found handler
 * covers: a pair is in the product of its components' types */
static void extr_tuple_body(MgElab *e, void *ctx) {
    ExtrTuple *t = ctx;
    MgTm *m1, *m2;
    if (e->ma_setprod == NONE || e->ma_tuple_2_Sigma == NONE || !invert_tuple2(e, t->m, &m1, &m2))
        mge_not_found(e);
    PfTm r1 = ta_extr(e, t->lm, t->salt, t->cx, t->hyps, m1);
    PfTm r2 = ta_extr(e, t->lm, t->salt, t->cx, t->hyps, m2);
    MgPf *k = tmap3(e, known(e, e->ma_tuple_2_Sigma), r1.t, lam_set(e, sh(e, 1, r2.t)), m1);
    t->out = (PfTm){pfap(e, tmap(e, pfap(e, k, r1.d), m2), r2.d), ap2(e, th(e, e->ma_setprod), r1.t, r2.t)};
}

/* type_megaauto_extr_2b */
static PfTm ta_extr_2b(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m) {
    step(e, lm);
    if (m->tag == MG_TM_DB) {   /* local_type_hyp */
        uint32_t i = 0;
        for (const PfCx *h = hyps; h; h = h->next, ++i) {
            MgTm *n, *b;
            if (is_in(e, h->prop, &n, &b) && mge_conv(e, n, m))
                return (PfTm){hyp(e, i), b};
        }
        search_backtrack(e);
    }
    MgTm *m1, *m2;
    if (app2(m, e->ma_setap, &m1, &m2)) {
        uint32_t i2 = id_of(e, m2);
        if (i2 == e->set0)
            return extr_component(e, lm, salt, cx, hyps, m1, m2, false);
        if (i2 == e->ma_set1)
            return extr_component(e, lm, salt, cx, hyps, m1, m2, true);
        return handle_ap(e, lm, salt, cx, hyps, m1, m2);
    }
    uint32_t h = id_of(e, m);
    if (h == e->set0 || h == e->ma_set1) {
        uint32_t k = h == e->set0 ? e->ma_In_0_2 : e->ma_In_1_2;
        uint32_t hs = e->ma_setordsucc;
        if (k == NONE || hs == NONE)
            search_backtrack(e);
        return (PfTm){known(e, k), ap(e, th(e, hs), ap(e, th(e, hs), mge_tm_prim(e, 2u)))};
    }
    ExtrTuple t = {lm, salt, cx, hyps, m, {NULL, NULL}};
    int k = mge_try_run(e, extr_tuple_body, &t);
    if (k == 0)
        return t.out;
    if (k == RAISE_NOT_FOUND)
        search_backtrack(e);
    mge_raise_kind(e, k);
}

/* type_megaauto_conv: a proof of m1 = m2; the salt passed on is the one
 * received, as there */
static MgPf *ta_conv(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m1, MgTm *m2) {
    MgHash salt2 = hashtag(&salt, 23u);
    MgTm *m1n = word(&salt2, 6) < SEVEN_EIGHTHS ? headnorm(e, salt2, m1) : m1;
    MgTm *m2n = word(&salt2, 7) < SEVEN_EIGHTHS ? headnorm(e, salt2, m2) : m2;
    Call c = {C_CONV2, lm, salt, cx, hyps, NULL, NULL, NULL, {NULL, NULL}};
    return normal_forms(e, &c, m1, m2, m1n, m2n, word(&salt2, 1) < 0).d;
}

typedef struct {
    int lm;
    MgHash salt;
    const TpCx *cx;
    const PfCx *hyps;
    MgTm *m1, *m2;
    PfTm r1;           /* m1 reduced: d1 :: m1 = m1r */
    MgPf *out;
} Conv3;

/* conv_3, once m1 reduced: reduce m2 and meet */
static void conv3_both(MgElab *e, void *ctx) {
    Conv3 *t = ctx;
    PfTm r2 = ta_red(e, t->lm, t->salt, t->cx, t->hyps, t->m2);
    MgTm *m1 = t->m1, *m2 = t->m2, *m1r = t->r1.t, *m2r = r2.t;
    MgPf *d1 = t->r1.d, *d2 = r2.d;
    if (m1r == m2r) {
        t->out = tra(e, m1, m2r, m2, d1, sym(e, m2, m2r, d2));
        return;
    }
    Call c = {C_CONV2, t->lm - 1, t->salt, t->cx, t->hyps, m1r, m2r, NULL, {NULL, NULL}};
    if (attempt(e, &c, RAISE_SEARCHBACKTRACK)) {   /* m1 = m1r = m2r = m2 */
        MgPf *d13 = tra(e, m1, m1r, m2r, d1, c.out.d);
        t->out = tra(e, m1, m2r, m2, d13, sym(e, m2, m2r, d2));
        return;
    }
    c.m = m1;
    c.a = m2r;
    if (attempt(e, &c, RAISE_SEARCHBACKTRACK)) {   /* m1 = m2r = m2 */
        t->out = tra(e, m1, m2r, m2, c.out.d, sym(e, m2, m2r, d2));
        return;
    }
    c.m = m1r;
    c.a = m2;
    call_body(e, &c);                               /* m1 = m1r = m2 */
    t->out = tra(e, m1, m1r, m2, d1, c.out.d);
}

/* conv_3's outer block: reduce m1, then m2 (Not_found: m2 stays) */
static void conv3_first(MgElab *e, void *ctx) {
    Conv3 *t = ctx;
    t->r1 = ta_red(e, t->lm, t->salt, t->cx, t->hyps, t->m1);
    int k = mge_try_run(e, conv3_both, t);
    if (k == 0)
        return;
    if (k != RAISE_NOT_FOUND)
        mge_raise_kind(e, k);
    MgTm *m1r = t->r1.t;
    if (m1r == t->m2) {
        t->out = t->r1.d;
        return;
    }
    MgPf *d3 = ta_conv_2b(e, t->lm - 1, t->salt, t->cx, t->hyps, m1r, t->m2);
    t->out = tra(e, t->m1, m1r, t->m2, t->r1.d, d3);
}

/* conv_3's last handler: m1 does not reduce; reduce m2 */
static void conv3_second(MgElab *e, void *ctx) {
    Conv3 *t = ctx;
    PfTm r2 = ta_red(e, t->lm, t->salt, t->cx, t->hyps, t->m2);
    if (t->m1 == r2.t) {
        t->out = sym(e, t->m2, r2.t, r2.d);
        return;
    }
    MgPf *d3 = ta_conv_2b(e, t->lm - 1, t->salt, t->cx, t->hyps, t->m1, r2.t);
    t->out = tra(e, t->m1, r2.t, t->m2, d3, sym(e, t->m2, r2.t, r2.d));
}

/* type_megaauto_conv_2b and type_megaauto_conv_3 */
static MgPf *ta_conv_2b(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m1, MgTm *m2) {
    step(e, lm);
    if (m1 == m2)
        return tmap(e, eqset_ref(e), m1);
    Conv3 t = {lm, salt, cx, hyps, m1, m2, {NULL, NULL}, NULL};
    int k = mge_try_run(e, conv3_first, &t);
    if (k == 0)
        return t.out;
    if (k != RAISE_NOT_FOUND)
        mge_raise_kind(e, k);
    k = mge_try_run(e, conv3_second, &t);
    if (k == 0)
        return t.out;
    if (k == RAISE_NOT_FOUND)
        search_backtrack(e);
    mge_raise_kind(e, k);
}

/* The projection of a pair at 0 or 1, the reduction red and red_3 try
 * first: m = ap (lam 2 ..) m2 with m1 that lam; false for Not_found. */
static bool project(MgElab *e, int32_t s0, MgTm *m1, MgTm *m2, MgPf **d, MgTm **r) {
    MgTm *m1a, *m1b;
    if (!invert_tuple2(e, m1, &m1a, &m1b))
        return false;
    uint32_t h2 = id_of(e, m2);
    uint32_t kte;
    if (h2 == e->set0) {
        kte = e->ma_tuple_2_0_eq;
        *r = m1a;
    } else if (h2 == e->ma_set1) {
        kte = e->ma_tuple_2_1_eq;
        *r = m1b;
    } else {
        return false;
    }
    /* 7/8 of the time: unlike beta it needs no type check */
    if (s0 >= SEVEN_EIGHTHS || kte == NONE)
        return false;
    *d = tmap(e, tmap(e, known(e, kte), m1a), m1b);
    return true;
}

/* beta: ap (lam a (fun x => b)) m2 = b[m2], with m2 :e a */
static PfTm beta(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *a, MgTm *m1pb, MgTm *m2) {
    MgPf *d = ta_of(e, lm - 1, salt, cx, hyps, m2, a);
    MgTm *b = gen_lam_body(e, m1pb);
    return (PfTm){pfap(e, tmap3(e, known(e, e->ma_beta), a, lam_set(e, b), m2), d), mge_tm_subst0(e, b, m2)};
}

/* type_megaauto_red: one reduction step of m and its proof; Not_found
 * when none */
static PfTm ta_red(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m) {
    MgHash salt2 = hashtag(&salt, 25u);
    int32_t s0 = word(&salt2, 0);
    MgTm *m1, *m2, *a, *m1pb;
    if (app2(m, e->ma_setap, &m1, &m2) && app2(m1, e->ma_setlambda, &a, &m1pb)) {
        PfTm r;
        if (project(e, s0, m1, m2, &r.d, &r.t))
            return r;
        if (s0 < 0) {
            if (e->ma_beta == NONE)
                mge_not_found(e);
            return beta(e, lm, salt2, cx, hyps, a, m1pb, m2);
        }
        Call c = {C_RED2, lm, salt2, cx, hyps, m, NULL, NULL, {NULL, NULL}};
        if (attempt(e, &c, RAISE_NOT_FOUND))
            return c.out;
    }
    return ta_red_2(e, lm, salt2, cx, hyps, m);
}

/* type_megaauto_red_2: inside an application */
static PfTm ta_red_2(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m) {
    MgHash salt2 = hashtag(&salt, 26u);
    if (m->tag != MG_TM_AP)
        mge_not_found(e);
    if (word(&salt2, 0) >= 0)
        return ta_red_4(e, lm, salt2, cx, hyps, m);
    const Hl *fun = hl_push(e, HL_FUN, NONE, sh(e, 1, m->x), NULL);
    const Hl *arg = hl_push(e, HL_ARG, NONE, sh(e, 1, m->f), NULL);
    bool s1neg = word(&salt2, 1) < 0;
    Call c = {C_RED3, lm, salt2, cx, hyps, s1neg ? m->f : m->x, NULL, s1neg ? fun : arg, {NULL, NULL}};
    if (attempt(e, &c, RAISE_NOT_FOUND))
        return c.out;
    c.m = s1neg ? m->x : m->f;
    c.hl = s1neg ? arg : fun;
    if (attempt(e, &c, RAISE_NOT_FOUND))
        return c.out;
    return ta_red_4(e, lm, salt2, cx, hyps, m);
}

/* From d :: m = mr, a proof of hl m = hl mr (by Leibniz, through the
 * context), and hl mr. */
static PfTm under_hl(MgElab *e, const Hl *hl, MgTm *m, MgPf *d, MgTm *mr) {
    MgTm *hl0 = hl_apply(e, hl, db(e, 0u));
    MgTm *hlp = lam_set(e, hl0);
    MgTm *hlp_1 = sh(e, 1, hlp), *hlp_3 = sh(e, 3, hlp);
    MgTm *motive = lam_set(e, lam_set(e, ap2(e, db(e, 2u), ap(e, hlp_3, db(e, 1u)), ap(e, hlp_3, db(e, 0u)))));
    MgPf *body = pfap(e, tmap(e, mge_pftmshift(e, 0u, 1, mge_pfshift(e, 0u, 1, d)), motive), hyp(e, 0u));
    MgTm *q = ap2(e, db(e, 0u), ap(e, hlp_1, sh(e, 1, m)), ap(e, hlp_1, sh(e, 1, mr)));
    MgPf *dhl = tlam(e, rel(e, set(e)), plam(e, q, body));
    return (PfTm){dhl, mge_tm_subst0(e, hl0, mr)};
}

/* type_megaauto_red_3: a reduction inside m, m under the context hl */
static PfTm ta_red_3(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m, const Hl *hl) {
    MgHash salt2 = hashtag(&salt, 25u);
    int32_t s0 = word(&salt2, 0), s1 = word(&salt2, 1), s2 = word(&salt2, 2), s3 = word(&salt2, 3);
    MgTm *a1, *m2;
    if ((app2(m, e->ma_setlambda, &a1, &m2) || app2(m, e->ma_setPi, &a1, &m2))) {
        /* reduce the type bound (not under a binder) */
        const Hl *bound = hl_push(e, HL_BOUND, m->f->f->n, sh(e, 1, m2), hl);
        if (s3 < SEVEN_EIGHTHS) {
            Call c = {C_RED4, lm, salt2, cx, hyps, m, NULL, NULL, {NULL, NULL}};
            if (attempt(e, &c, RAISE_NOT_FOUND))
                return under_hl(e, hl, m, c.out.d, c.out.t);
            if (s2 >= 0)
                mge_not_found(e);
        }
        return ta_red_3(e, lm, salt2, cx, hyps, a1, bound);
    }
    MgTm *m1, *a, *m1pb;
    if (app2(m, e->ma_setap, &m1, &m2) && app2(m1, e->ma_setlambda, &a, &m1pb)) {
        PfTm r;
        if (project(e, s0, m1, m2, &r.d, &r.t))
            return under_hl(e, hl, m, r.d, r.t);
        if (s0 >= 0 || e->ma_beta == NONE)
            mge_not_found(e);
        r = beta(e, lm, salt2, cx, hyps, a, m1pb, m2);
        return under_hl(e, hl, m, r.d, r.t);
    }
    if (m->tag != MG_TM_AP)
        mge_not_found(e);
    const Hl *fun = hl_push(e, HL_FUN, NONE, sh(e, 1, m->x), hl);
    const Hl *arg = hl_push(e, HL_ARG, NONE, sh(e, 1, m->f), hl);
    Call c = {C_RED3, lm, salt2, cx, hyps, s1 < 0 ? m->f : m->x, NULL, s1 < 0 ? fun : arg, {NULL, NULL}};
    if (attempt(e, &c, RAISE_NOT_FOUND))
        return c.out;
    return ta_red_3(e, lm, salt2, cx, hyps, s1 < 0 ? m->x : m->f, s1 < 0 ? arg : fun);
}

/* type_megaauto_red_4: under the binder of a set lambda or a Pi */
static PfTm ta_red_4(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *m) {
    MgHash salt2 = hashtag(&salt, 27u);
    MgTm *a1, *m2;
    uint32_t h, kn;
    if (app2(m, e->setlam, &a1, &m2)) {
        h = e->setlam;
        kn = e->ma_lam_ext;
    } else if (app2(m, e->ma_setPi, &a1, &m2)) {
        h = e->ma_setPi;
        kn = e->ma_Pi_cod_ext;
    } else {
        mge_not_found(e);
    }
    MgTm *tphyp = in(e, db(e, 0u), sh(e, 1, a1));
    PfTm r = ta_red(e, lm, salt2, cx_set(e, cx), hyps_under(e, tphyp, hyps), gen_lam_body(e, m2));
    if (kn == NONE)
        mge_not_found(e);
    MgTm *lr = lam_set(e, r.t);
    MgPf *d = pfap(e, tmap3(e, known(e, kn), a1, m2, lr), tlam(e, set(e), plam(e, tphyp, r.d)));
    return (PfTm){d, ap2(e, th(e, h), a1, lr)};
}

/* type_megaauto */
MgPf *mge_type_megaauto(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *g) {
    step(e, lm);
    if (g->tag == MG_TM_IMP) {
        PfCx *c = TMP(e, PfCx);
        *c = (PfCx){g->f, hyps};
        return plam(e, g->f, mge_type_megaauto(e, lm - 1, salt, cx, c, g->x));
    }
    if (g->tag == MG_TM_ALL && g->tp == set(e))
        return tlam(e, set(e), mge_type_megaauto(e, lm - 1, salt, cx_set(e, cx), mge_pfcx_shift(e, hyps), g->f));
    MgTm *m, *a;
    if (is_in(e, g, &m, &a))
        return ta_of(e, lm, salt, cx, hyps, m, a);
    if (is_eqset(e, g, &m, &a))
        return ta_conv(e, lm, salt, cx, hyps, m, a);
    search_backtrack(e);
}
