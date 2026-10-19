/*
 * Lazy judgments against the reference rules.
 *
 * Under lazy conversion (the default) judgments stay in source form; the
 * contract is that a lazy check accepts exactly when the reference rule
 * accepts the normal forms, and that its conclusion normalizes to the
 * reference conclusion.  This test holds the implementation to it:
 *
 *   trees     random proof trees (valid and invalid) in random contexts:
 *             mg_infer_proof_lazy in context C against mg_infer_proof in
 *             normalize(C) -- the same verdict, and normalize(lazy) equals
 *             the reference proposition;
 *   LCF       random LCF steps over theorem pairs (lazy, reference) whose
 *             judgments agree up to normalization -- the same verdict, and
 *             conclusions (contexts included) that agree up to it;
 *   evidence  every accepted lazy tree's evidence rechecks in a lazy
 *             session, and the same rows normalized recheck as exact
 *             evidence (lazy rows are accepted exactly when their normal
 *             forms are reference rows); stated garbage is refused, not
 *             converted.
 *
 *   test_mg_lazy [COUNT [SEED]]
 */
#include "mg_core.h"
#include "mg_library.h"
#include "mg_session.h"
#include "mg_sexpr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define EXPECT(cond, label) do { \
        if (cond) { ++g_pass; } \
        else { ++g_fail; printf("FAIL %s (%s:%d)\n", label, __FILE__, __LINE__); } \
    } while (0)

static MgTheory *T;
static MgStore *S;
static MgSxArena *A;

static MgSx *sx(const char *text) {
    MgSxReader r;
    mg_sx_reader_init(&r, text, strlen(text));
    MgSx *x = mg_sx_read(&r, A);
    if (!x) {
        printf("parse error in test data: %s\n", text);
        exit(2);
    }
    return x;
}
static MgTm *TM(const char *w) {
    MgTm *m = mg_sx_wire_tm(S, sx(w));
    if (!m) {
        printf("bad term %s\n", w);
        exit(2);
    }
    return m;
}
static MgTp *TP(const char *w) { return mg_sx_wire_tp(S, sx(w)); }
static uint32_t NM(const char *n) { return mg_name_intern(S, n, strlen(n)); }
static MgTm *NAME(const char *n) { return mg_tm_name(S, NM(n)); }

static void admit(MgItem *item, const char *what) {
    MgBudget b;
    mg_budget_init(&b, 0);
    MgStatus st = mg_theory_admit(T, &b, item, 0, NULL);
    if (st != MG_OK) {
        printf("%s refused: %s\n", what, mg_status_name(st));
        exit(2);
    }
}
static void parameter(const char *name, const char *type) {
    mg_theory_associate_parameter(T, NM(name), TP(type));
    MgItem item = {.kind = MG_ITEM_PARAMETER, .name = NM(name), .type = TP(type)};
    admit(&item, name);
}
static void definition(const char *name, const char *type, const char *body) {
    MgItem item = {.kind = MG_ITEM_DEFINITION, .name = NM(name), .type = TP(type), .body = TM(body)};
    admit(&item, name);
}
static void axiom(const char *name, const char *prop) {
    mg_theory_associate_known(T, NM(name), TM(prop));
    MgItem item = {.kind = MG_ITEM_KNOWN, .name = NM(name), .prop = TM(prop)};
    admit(&item, name);
}

#define I "(Tp.base 0)"
#define II "(Tp.arr (Tp.base 0) (Tp.base 0))"
#define III "(Tp.arr (Tp.base 0) (Tp.arr (Tp.base 0) (Tp.base 0)))"
#define IP "(Tp.arr (Tp.base 0) (Tp.prop))"
#define N(x) "(Tm.named \"" x "\")"
#define AP(f, x) "(Tm.app " f " " x ")"

static void theory_setup(void) {
    MgStatus st;
    T = mg_theory_new(MG_PROFILE_CUSTOM, "lazy-test", &st);
    S = &T->store;
    parameter("c1", I);
    parameter("c2", I);
    parameter("f", II);
    parameter("g", III);
    parameter("P", IP);
    parameter("Q", IP);
    parameter("R", "(Tp.prop)");
    parameter("S", "(Tp.prop)");
    parameter("E", "(Tp.all (Tp.arr (Tp.var 0) (Tp.arr (Tp.var 0) (Tp.prop))))");
    /* d1 x = f (f x); k x y = x; id x = x; dp x = P (d1 x);
     * dimp p q = p -> q; dall = forall x. P x; pid = the polymorphic id */
    definition("d1", II, "(Tm.lam " I " " AP(N("f"), AP(N("f"), "(Tm.db 0)")) ")");
    definition("k", III, "(Tm.lam " I " (Tm.lam " I " (Tm.db 1)))");
    definition("id", II, "(Tm.lam " I " (Tm.db 0))");
    definition("dp", IP, "(Tm.lam " I " " AP(N("P"), AP(N("d1"), "(Tm.db 0)")) ")");
    definition("dimp", "(Tp.arr (Tp.prop) (Tp.arr (Tp.prop) (Tp.prop)))",
               "(Tm.lam (Tp.prop) (Tm.lam (Tp.prop) (Tm.imp (Tm.db 1) (Tm.db 0))))");
    definition("dall", "(Tp.prop)", "(Tm.all " I " " AP(N("P"), "(Tm.db 0)") ")");
    definition("pid", "(Tp.all (Tp.arr (Tp.var 0) (Tp.var 0)))",
               "(Tm.typeLam (Tm.lam (Tp.var 0) (Tm.db 0)))");
    /* knowns stated with definitions, so source and normal forms differ */
    axiom("ax_pq", "(Tm.all " I " (Tm.imp " AP(N("dp"), "(Tm.db 0)") " " AP(N("Q"), "(Tm.db 0)") "))");
    axiom("ax_p", "(Tm.all " I " " AP(N("P"), AP(N("d1"), "(Tm.db 0)")) ")");
    axiom("ax_r", AP(AP(N("dimp"), "(Tm.all " I " " AP(N("Q"), "(Tm.db 0)") ")"), N("R")));
    axiom("ax_s", "(Tm.imp " N("R") " " N("S") ")");
    axiom("ax_dall", N("dall"));
    axiom("ax_refl", "(Tm.typeAll (Tm.all (Tp.var 0) " AP(AP("(Tm.typeApp " N("E") " (Tp.var 0))",
                                                               "(Tm.db 0)"), "(Tm.db 0)") "))");
}

/* ── normal forms ─────────────────────────────────────────────────────── */

static MgCheck g_nf;
static MgBudget g_nf_budget;
static MgTm *nf(MgTm *m) {
    mg_budget_init(&g_nf_budget, 0);
    mg_check_init(&g_nf, mg_theory_snapshot(T), &g_nf_budget, NULL);
    MgTm *r = mg_tm_normalize(&g_nf, m);
    if (!r) {
        printf("normalization failed\n");
        exit(2);
    }
    return r;
}
static MgCtx *nf_ctx(MgCtx *ctx) {
    MgTm *items[64];
    uint32_t n = 0;
    for (MgTmList *l = ctx->hyps; l && n < 64u; l = l->tail)
        items[n++] = nf(l->head);
    MgTmList *hyps = NULL;
    for (uint32_t i = n; i > 0u; --i)
        hyps = mg_tmlist_cons(S, items[i - 1u], hyps);
    return mg_ctx(S, ctx->depth, ctx->terms, hyps);
}

/* ── random terms ─────────────────────────────────────────────────────── */

static uint64_t g_rng = 0x9e3779b97f4a7c15u;
static uint32_t rnd(uint32_t n) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng % n);
}

static MgTm *rand_iota(uint32_t vars, int depth) {
    switch (depth <= 0 ? rnd(3) : rnd(9)) {
    case 0: return NAME("c1");
    case 1: return NAME("c2");
    case 2: return vars ? mg_tm_db(S, rnd(vars)) : NAME("c1");
    case 3: return mg_tm_ap(S, NAME("f"), rand_iota(vars, depth - 1));
    case 4: return mg_tm_ap(S, mg_tm_ap(S, NAME("g"), rand_iota(vars, depth - 1)),
                            rand_iota(vars, depth - 1));
    case 5: return mg_tm_ap(S, NAME("d1"), rand_iota(vars, depth - 1));
    case 6: return mg_tm_ap(S, NAME("id"), rand_iota(vars, depth - 1));
    case 7: return mg_tm_ap(S, mg_tm_tap(S, NAME("pid"), TP(I)), rand_iota(vars, depth - 1));
    default: return mg_tm_ap(S, mg_tm_ap(S, NAME("k"), rand_iota(vars, depth - 1)),
                             rand_iota(vars, depth - 1));
    }
}

/* A convertible variant of an iota term (beta, delta and eta expansions). */
static MgTm *vary_iota(MgTm *m, uint32_t vars, int budget) {
    if (budget <= 0)
        return m;
    MgTm *r = m;
    if (m->tag == MG_TM_AP)
        r = mg_tm_ap(S, m->f->tag == MG_TM_AP ? mg_tm_ap(S, m->f->f, vary_iota(m->f->x, vars, budget - 1))
                                              : m->f,
                     vary_iota(m->x, vars, budget - 1));
    switch (rnd(6)) {
    case 0: return mg_tm_ap(S, NAME("id"), r);
    case 1: return mg_tm_ap(S, mg_tm_ap(S, NAME("k"), r), rand_iota(vars, 1));
    case 2: return mg_tm_ap(S, mg_tm_tap(S, NAME("pid"), TP(I)), r);
    case 3:
        if (r->tag == MG_TM_AP && r->f == NAME("f") && r->x->tag == MG_TM_AP && r->x->f == NAME("f"))
            return mg_tm_ap(S, NAME("d1"), r->x->x);
        return r;
    case 4:
        if (r->tag == MG_TM_AP && r->f == NAME("f"))
            return mg_tm_ap(S, mg_tm_lam(S, TP(I), mg_tm_ap(S, NAME("f"), mg_tm_db(S, 0))), r->x);
        return r;
    default:
        return r;
    }
}

/* Either a convertible variant or (half the time) an unrelated term. */
static MgTm *maybe_iota(MgTm *m, uint32_t vars) {
    return rnd(2) ? vary_iota(m, vars, 3) : rand_iota(vars, 2);
}

static MgTm *E_of(MgTm *a, MgTm *b) {
    return mg_tm_ap(S, mg_tm_ap(S, mg_tm_tap(S, NAME("E"), TP(I)), a), b);
}

static MgTm *rand_prop(uint32_t vars, int depth) {
    switch (depth <= 0 ? rnd(6) : rnd(11)) {
    case 0: return mg_tm_ap(S, NAME("P"), rand_iota(vars, 2));
    case 1: return mg_tm_ap(S, NAME("Q"), rand_iota(vars, 2));
    case 2: return mg_tm_ap(S, NAME("dp"), rand_iota(vars, 2));
    case 3: return NAME("R");
    case 4: return NAME("dall");
    case 5: return E_of(rand_iota(vars, 1), rand_iota(vars, 1));
    case 6: return mg_tm_imp(S, rand_prop(vars, depth - 1), rand_prop(vars, depth - 1));
    case 7: return mg_tm_ap(S, mg_tm_ap(S, NAME("dimp"), rand_prop(vars, depth - 1)),
                            rand_prop(vars, depth - 1));
    case 8: return mg_tm_all(S, TP(I), rand_prop(vars + 1u, depth - 1));
    case 9: return NAME("S");
    default: return mg_tm_ap(S, mg_tm_lam(S, TP("(Tp.prop)"), mg_tm_db(S, 0)),
                             rand_prop(vars, depth - 1));
    }
}

/* Whether a type quantifier occurs (it may not be an argument). */
static bool has_tall(const MgTm *m) {
    if (!m || !(m->facts & MG_TM_HAS_TYPEREG))
        return false;
    if (m->tag == MG_TM_TALL)
        return true;
    return has_tall(m->f) || ((m->tag == MG_TM_AP || m->tag == MG_TM_IMP) && has_tall(m->x));
}

/* A convertible variant of a proposition, formed as stated data must be:
 * a proposition with a type quantifier is never made an argument. */
static MgTm *vary_prop(MgTm *m, uint32_t vars, int budget) {
    if (budget <= 0)
        return m;
    switch (m->tag) {
    case MG_TM_IMP: {
        MgTm *a = vary_prop(m->f, vars, budget - 1), *b = vary_prop(m->x, vars, budget - 1);
        return rnd(2) || has_tall(a) || has_tall(b) ? mg_tm_imp(S, a, b)
                                                    : mg_tm_ap(S, mg_tm_ap(S, NAME("dimp"), a), b);
    }
    case MG_TM_ALL:
        if (m->f->tag == MG_TM_AP && m->f->f == NAME("P") && m->f->x == mg_tm_db(S, 0) && rnd(2))
            return NAME("dall");
        return mg_tm_all(S, m->tp, vary_prop(m->f, vars + 1u, budget - 1));
    case MG_TM_AP:
        if (m->f == NAME("P") || m->f == NAME("Q")) {
            if (m->f == NAME("P") && m->x->tag == MG_TM_AP && m->x->f == NAME("d1") && rnd(2))
                return mg_tm_ap(S, NAME("dp"), vary_iota(m->x->x, vars, budget - 1));
            return mg_tm_ap(S, m->f, vary_iota(m->x, vars, budget - 1));
        }
        if (m->f == NAME("dp"))
            return rnd(2) ? mg_tm_ap(S, NAME("P"), mg_tm_ap(S, NAME("d1"), vary_iota(m->x, vars, budget - 1)))
                          : mg_tm_ap(S, NAME("dp"), vary_iota(m->x, vars, budget - 1));
        break;
    default:
        break;
    }
    return rnd(3) || has_tall(m) ? m : mg_tm_ap(S, mg_tm_lam(S, TP("(Tp.prop)"), mg_tm_db(S, 0)), m);
}

/* The iota variables generated terms may use: the context's term
 * variables when all are iota (the out-of-scope binder contexts have
 * none usable). */
static uint32_t iota_vars(const MgCtx *ctx) {
    for (const MgTpList *l = ctx->terms; l; l = l->tail)
        if (l->head != TP(I))
            return 0;
    return mg_tplist_len(ctx->terms);
}

static MgTm *maybe_prop(MgTm *m, uint32_t vars) {
    return rnd(2) ? vary_prop(m, vars, 3) : rand_prop(vars, 2);
}

/* A random context: up to two iota variables and up to three formed
 * hypotheses in source form. */
static MgCtx *rand_ctx(void) {
    if (rnd(6) == 0)
        return mg_ctx(S, 1, NULL, NULL);   /* a fresh type context */
    if (rnd(10) == 0)                      /* a binder type out of scope */
        return mg_ctx(S, 0, mg_tplist_cons(S, TP("(Tp.var 2)"), NULL), NULL);
    uint32_t vars = rnd(3);
    MgTpList *terms = NULL;
    for (uint32_t i = 0; i < vars; ++i)
        terms = mg_tplist_cons(S, TP(I), terms);
    MgTmList *hyps = NULL;
    for (uint32_t i = rnd(4); i > 0u; --i)
        hyps = mg_tmlist_cons(S, rand_prop(vars, 2), hyps);
    return mg_ctx(S, 0, terms, hyps);
}

/* ── proof trees ──────────────────────────────────────────────────────── */

static MgPf *K(const char *n) { return mg_pf_known(S, NM(n)); }

/* A random proof in a context with `vars` iota variables and `hyps`
 * hypotheses, from templates that are valid when their random operands
 * happen to line up (about half the time), plus outright garbage. */
static MgPf *rand_proof(uint32_t vars, uint32_t hyps, int depth) {
    MgTm *t = rand_iota(vars, 2);
    switch (depth <= 0 ? rnd(4) : rnd(12)) {
    case 0: /* ax_pq t (ax_p t') : Q t, valid iff t ~ t' */
        return mg_pf_pfap(S, mg_pf_tmap(S, K("ax_pq"), t), mg_pf_tmap(S, K("ax_p"), maybe_iota(t, vars)));
    case 1: /* a hypothesis (possibly out of range) */
        return mg_pf_hyp(S, rnd(hyps + 1u));
    case 2: /* E t t by reflexivity, at iota or (wrongly) at prop */
        return mg_pf_tmap(S, mg_pf_tpap(S, K("ax_refl"), rnd(4) ? TP(I) : TP("(Tp.prop)")), t);
    case 3: /* not a quantifier: refused */
        return rnd(2) ? mg_pf_tmap(S, K("ax_r"), t) : mg_pf_pfap(S, K("ax_p"), K("ax_p"));
    case 4: /* generalize (over an out-of-scope type now and then) */
        return mg_pf_tlam(S, rnd(8) ? TP(I) : TP("(Tp.var 3)"), rand_proof(vars + 1u, hyps, depth - 1));
    case 5: { /* ax_r (\x. ax_pq x' (ax_p x'')) : R */
        MgTm *x = mg_tm_db(S, 0);
        MgPf *inner = mg_pf_pfap(S, mg_pf_tmap(S, K("ax_pq"), maybe_iota(x, vars + 1u)),
                                 mg_pf_tmap(S, K("ax_p"), maybe_iota(x, vars + 1u)));
        return mg_pf_pfap(S, K("ax_r"), mg_pf_tlam(S, TP(I), inner));
    }
    case 6: /* S from R */
        return mg_pf_pfap(S, K("ax_s"), rand_proof(vars, hyps, depth - 1));
    case 7: { /* \h : src. ax_pq t h : src -> Q t, valid iff src ~ dp t */
        MgTm *src = maybe_prop(mg_tm_ap(S, NAME("dp"), t), vars);
        return mg_pf_plam(S, src, mg_pf_pfap(S, mg_pf_tmap(S, K("ax_pq"), t), mg_pf_hyp(S, 0)));
    }
    case 8: { /* (\h : src. h) (ax_p t) : src, valid iff src ~ P (d1 t) */
        MgTm *src = maybe_prop(mg_tm_ap(S, NAME("P"), mg_tm_ap(S, NAME("d1"), t)), vars);
        return mg_pf_pfap(S, mg_pf_plam(S, src, mg_pf_hyp(S, 0)), mg_pf_tmap(S, K("ax_p"), t));
    }
    case 9: /* discharge a random formed proposition over a subproof */
        return mg_pf_plam(S, rand_prop(vars, 2), rand_proof(vars, hyps + 1u, depth - 1));
    case 10: /* a type generalization (only valid in an empty context) */
        return mg_pf_tplam(S, mg_pf_tpap(S, K("ax_refl"), TP("(Tp.var 0)")));
    default: /* dall instantiated: P t */
        return mg_pf_tmap(S, K("ax_dall"), t);
    }
}

static int g_tree_ok, g_tree_refused, g_tree_disagree, g_evidence_ok;

static MgStatus check_reference(MgCtx *ctx, MgPf *pf, MgJudgment *out) {
    MgBudget b;
    mg_budget_init(&b, 0);
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    return mg_infer_proof(&c, ctx, pf, out);
}

static MgStatus check_lazy(MgCtx *ctx, MgPf *pf, MgTm **out) {
    MgBudget b;
    mg_budget_init(&b, 0);
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    return mg_infer_proof_lazy(&c, ctx, pf, out);
}

/* The rows normalized: what the reference session must accept. */
static MgEvidence normalized(const MgEvidence *ev) {
    MgEvidence n = *ev;
    n.lazy = false;
    n.rows = malloc(sizeof(MgRow) * (ev->row_count ? ev->row_count : 1u));
    for (uint32_t i = 0; i < ev->row_count; ++i) {
        MgRow r = ev->rows[i];
        r.conclusion = (MgJudgment){nf_ctx(r.conclusion.ctx), nf(r.conclusion.prop)};
        switch (r.witness.rule) {
        case MG_RULE_TERM_APP:
        case MG_RULE_TYPE_APP:
            r.witness.body = nf(r.witness.body);
            break;
        case MG_RULE_PROOF_APP:
            r.witness.domain = nf(r.witness.domain);
            break;
        default:
            break;
        }
        n.rows[i] = r;
    }
    return n;
}

static void lazy_evidence(MgCtx *ctx, MgPf *pf, MgJudgment reference) {
    MgThm *thm = NULL, *again = NULL, *exact = NULL;
    const char *why = NULL;
    T->reference_conversion = false;
    MgStatus st = mg_thm_check_tree(T, ctx, pf, NULL, &why, &thm);
    EXPECT(st == MG_OK && thm->lazy, "the lazy tree is a lazy theorem");
    if (st != MG_OK)
        return;
    MgEvidence ev;
    st = mg_evidence_of(thm, &ev);
    EXPECT(st == MG_OK && ev.lazy, "lazy evidence");
    if (st == MG_OK) {
        st = mg_evidence_check(T, &ev, NULL, &why, &again);
        EXPECT(st == MG_OK && again && mg_judgment_eq(again->judgment, thm->judgment),
               "lazy evidence rechecks in a lazy session");
        MgEvidence n = normalized(&ev);
        st = mg_evidence_check(T, &n, NULL, &why, &exact);
        bool same = st == MG_OK && exact && mg_judgment_eq(exact->judgment, reference);
        EXPECT(same, "normalized lazy evidence is reference evidence");
        if (!same)
            printf("  exact recheck: %s %s\n", mg_status_name(st), why ? why : "");
        else
            ++g_evidence_ok;
        free(n.rows);
        mg_evidence_free(&ev);
    }
    mg_thm_release(exact);
    mg_thm_release(again);
    mg_thm_release(thm);
}

static void tree_cases(int count) {
    for (int i = 0; i < count; ++i) {
        MgCtx *ctx = rnd(3) ? rand_ctx() : S->ctx_empty;
        MgPf *pf = rand_proof(iota_vars(ctx), mg_tmlist_len(ctx->hyps), 3);
        MgJudgment ref;
        MgTm *lazy = NULL;
        MgStatus rs = check_reference(nf_ctx(ctx), pf, &ref);
        MgStatus ls = check_lazy(ctx, pf, &lazy);
        bool agree = rs == ls && (rs != MG_OK || nf(lazy) == ref.prop);
        if (!agree) {
            ++g_tree_disagree;
            printf("DISAGREE tree %d: reference %s lazy %s\n", i, mg_status_name(rs), mg_status_name(ls));
            continue;
        }
        if (rs == MG_OK) {
            ++g_tree_ok;
            lazy_evidence(ctx, pf, ref);
        } else {
            ++g_tree_refused;
        }
    }
    EXPECT(g_tree_disagree == 0, "lazy proof trees agree with the reference");
    printf("(trees: %d accepted, %d refused, %d evidence rechecks)\n", g_tree_ok, g_tree_refused,
           g_evidence_ok);
}

/* ── LCF steps ────────────────────────────────────────────────────────── */

typedef struct {
    MgThm *lazy, *ref;
} Pair;

static Pair g_pool[4096];
static uint32_t g_pool_len;
static int g_lcf_ok[9], g_lcf_refused[9], g_lcf_disagree;

static const char *g_step_why;
static MgStatus step(bool reference, const MgWitness *w, MgCtx *ctx, MgThm **premises,
                     uint32_t count, MgThm **out) {
    T->reference_conversion = reference;
    MgWitness copy = *w;
    const char *why = NULL;
    MgStatus st = mg_thm_step(T, &copy, ctx, premises, count, NULL, &why, out);
    T->reference_conversion = false;
    g_step_why = why;
    return st;
}

static void show_judgment(const char *label, MgJudgment j) {
    printf("  %s: depth %u, %u terms, hyps [", label, j.ctx->depth, mg_tplist_len(j.ctx->terms));
    for (MgTmList *l = j.ctx->hyps; l; l = l->tail) {
        mg_print_tm(stdout, S, l->head);
        printf("%s", l->tail ? "; " : "");
    }
    printf("] |- ");
    mg_print_tm(stdout, S, j.prop);
    printf("\n");
}

static bool judgments_agree(MgJudgment lazy, MgJudgment ref) {
    return nf_ctx(lazy.ctx) == ref.ctx && nf(lazy.prop) == ref.prop;
}

/* Returns the new pool index, or -1. */
static int lcf_try(const MgWitness *wl, const MgWitness *wr, MgCtx *ctx, Pair *ps, uint32_t count) {
    MgThm *pl[2] = {NULL, NULL}, *pr[2] = {NULL, NULL};
    for (uint32_t i = 0; i < count; ++i) {
        pl[i] = ps[i].lazy;
        pr[i] = ps[i].ref;
    }
    MgThm *l = NULL, *r = NULL;
    MgStatus ls = step(false, wl, ctx, pl, count, &l);
    const char *lwhy = g_step_why;
    MgStatus rs = step(true, wr, ctx ? nf_ctx(ctx) : NULL, pr, count, &r);
    const char *rwhy = g_step_why;
    bool agree = ls == rs && (ls != MG_OK || judgments_agree(l->judgment, r->judgment));
    if (!agree) {
        ++g_lcf_disagree;
        printf("DISAGREE LCF %s: lazy %s (%s) reference %s (%s)\n", mg_rule_name(wl->rule),
               mg_status_name(ls), lwhy ? lwhy : "", mg_status_name(rs), rwhy ? rwhy : "");
        for (uint32_t i = 0; i < count; ++i) {
            show_judgment("lazy premise", pl[i]->judgment);
            show_judgment("reference premise", pr[i]->judgment);
        }
        if (wl->arg) {
            printf("  arg ");
            mg_print_tm(stdout, S, wl->arg);
            printf("\n");
        }
        if (wl->body) {
            printf("  body ");
            mg_print_tm(stdout, S, wl->body);
            printf("\n");
        }
        if (wl->domain) {
            printf("  domain ");
            mg_print_tm(stdout, S, wl->domain);
            printf("\n");
        }
    }
    if (ls == MG_OK && rs == MG_OK && agree && g_pool_len < sizeof g_pool / sizeof g_pool[0]) {
        ++g_lcf_ok[wl->rule];
        g_pool[g_pool_len] = (Pair){l, r};
        return (int)g_pool_len++;
    }
    if (ls != MG_OK)
        ++g_lcf_refused[wl->rule];
    mg_thm_release(l);
    mg_thm_release(r);
    return -1;
}

/* known + termApp in a context: the instance's pool index, or -1. */
static int instance_of(const char *known, MgCtx *ctx, MgTm *arg) {
    MgWitness k = {.rule = MG_RULE_KNOWN, .name = NM(known)};
    int at = lcf_try(&k, &k, ctx, NULL, 0);
    if (at < 0)
        return -1;
    MgWitness t = {.rule = MG_RULE_TERM_APP, .arg = arg};
    return lcf_try(&t, &t, NULL, &g_pool[at], 1);
}

/* Modus ponens of ax_pq t on ax_p t' in a random context (valid iff
 * t ~ t'), then perhaps a generalization and a discharge. */
static void lcf_composite(void) {
    MgCtx *ctx = rand_ctx();
    if (ctx->depth)
        ctx = S->ctx_empty;
    uint32_t vars = iota_vars(ctx);
    MgTm *t = rand_iota(vars, 2);
    int imp = instance_of("ax_pq", ctx, t);
    int ant = instance_of("ax_p", ctx, maybe_iota(t, vars));
    if (imp < 0 || ant < 0)
        return;
    Pair two[2] = {g_pool[imp], g_pool[ant]};
    MgWitness wl = {.rule = MG_RULE_PROOF_APP}, wr = wl;
    if (rnd(2)) {
        wl.domain = rnd(3) ? vary_prop(two[0].lazy->judgment.prop->f, vars, 2) : rand_prop(vars, 1);
        wr.domain = nf(wl.domain);
    }
    int mp = lcf_try(&wl, &wr, NULL, two, 2);
    if (mp < 0)
        return;
    if (vars && rnd(2)) {
        MgWitness gen = {.rule = MG_RULE_TERM_LAM};
        mp = lcf_try(&gen, &gen, NULL, &g_pool[mp], 1);
    }
    if (mp >= 0 && g_pool[mp].lazy->judgment.ctx->hyps && rnd(2)) {
        MgWitness dis = {.rule = MG_RULE_PROOF_LAM};
        lcf_try(&dis, &dis, NULL, &g_pool[mp], 1);
    }
}

/* ax_refl at a type variable, generalized: typeApp then typeLam. */
static void lcf_polymorphic(void) {
    MgWitness k = {.rule = MG_RULE_KNOWN, .name = NM("ax_refl")};
    int at = lcf_try(&k, &k, mg_ctx(S, 1, NULL, NULL), NULL, 0);
    if (at < 0)
        return;
    MgWitness ta = {.rule = MG_RULE_TYPE_APP, .type = rnd(3) ? TP("(Tp.var 0)") : TP(I)};
    if (rnd(2)) {
        /* the body stated in a convertible form */
        ta.body = rnd(4) ? mg_tm_ap(S, mg_tm_lam(S, TP("(Tp.prop)"), mg_tm_db(S, 0)),
                                    g_pool[at].lazy->judgment.prop->f)
                         : NAME("R");   /* typed, not convertible */
    }
    MgWitness tr = ta;
    if (ta.body)
        tr.body = nf(ta.body);
    int inst = lcf_try(&ta, &tr, NULL, &g_pool[at], 1);
    if (inst < 0)
        return;
    MgWitness tl = {.rule = MG_RULE_TYPE_LAM};
    lcf_try(&tl, &tl, NULL, &g_pool[inst], 1);
}

static Pair *pick(void) { return &g_pool[rnd(g_pool_len)]; }

/* The reference spelling of a stated implied operand is its normal form;
 * arguments and discharged sources are used as given by both rules. */
static void lcf_cases(int count) {
    for (int i = 0; i < count; ++i) {
        MgWitness wl;
        memset(&wl, 0, sizeof(wl));
        uint32_t choice = g_pool_len < 8u ? rnd(2) : rnd(11);
        if (choice == 9u) {
            lcf_composite();
            continue;
        }
        if (choice == 10u) {
            lcf_polymorphic();
            continue;
        }
        if (choice <= 1u) { /* hyp / known in a random context */
            MgCtx *ctx = rand_ctx();
            if (choice == 0u && ctx->hyps) {
                wl.rule = MG_RULE_HYP;
                wl.index = rnd(mg_tmlist_len(ctx->hyps) + 1u);
            } else {
                static const char *knowns[] = {"ax_pq", "ax_p", "ax_r", "ax_s", "ax_dall", "ax_refl"};
                wl.rule = MG_RULE_KNOWN;
                wl.name = NM(knowns[rnd(6)]);
            }
            lcf_try(&wl, &wl, ctx, NULL, 0);
            continue;
        }
        Pair *a = pick();
        MgWitness wr;
        switch (choice) {
        case 2: { /* termApp: an iota argument (or an ill-typed one), body stated or not */
            uint32_t vars = iota_vars(a->lazy->judgment.ctx);
            wl.rule = MG_RULE_TERM_APP;
            wl.arg = rnd(5) ? rand_iota(vars, 2) : rand_prop(vars, 1);
            wr = wl;
            MgTm *p = a->lazy->judgment.prop;
            if (p->tag == MG_TM_ALL && rnd(2)) {
                wl.body = rnd(4) ? vary_prop(p->f, vars + 1u, 2) : rand_prop(vars + 1u, 1);
                wr.body = nf(wl.body);
            }
            lcf_try(&wl, &wr, NULL, a, 1);
            break;
        }
        case 3: { /* proofApp, domain stated or not */
            Pair two[2] = {*a, *pick()};
            if (rnd(3) == 0 && two[0].lazy->judgment.prop->tag == MG_TM_IMP) {
                /* find an antecedent proof by its normal form, if any */
                MgTm *want = nf(two[0].lazy->judgment.prop->f);
                for (uint32_t k = 0; k < g_pool_len; ++k)
                    if (g_pool[k].ref->judgment.prop == want &&
                        nf_ctx(g_pool[k].lazy->judgment.ctx) == nf_ctx(two[0].lazy->judgment.ctx)) {
                        two[1] = g_pool[k];
                        break;
                    }
            }
            wl.rule = MG_RULE_PROOF_APP;
            wr = wl;
            MgTm *p = two[0].lazy->judgment.prop;
            if (p->tag == MG_TM_IMP && rnd(2)) {
                uint32_t vars = iota_vars(two[0].lazy->judgment.ctx);
                wl.domain = rnd(4) ? vary_prop(p->f, vars, 2) : rand_prop(vars, 1);
                wr.domain = nf(wl.domain);
            }
            lcf_try(&wl, &wr, NULL, two, 2);
            break;
        }
        case 4: { /* proofLam: source omitted, a variant of the hypothesis, or random */
            wl.rule = MG_RULE_PROOF_LAM;
            MgCtx *ctx = a->lazy->judgment.ctx;
            uint32_t vars = iota_vars(ctx);
            if (ctx->hyps && rnd(3))
                wl.source = rnd(4) ? vary_prop(ctx->hyps->head, vars, 2) : rand_prop(vars, 1);
            wr = wl;
            lcf_try(&wl, &wr, NULL, a, 1);
            break;
        }
        case 5:
        case 6: { /* termLam, type stated or not */
            wl.rule = MG_RULE_TERM_LAM;
            uint32_t r = rnd(4);
            wl.type = r == 0 ? NULL : r == 3 ? TP("(Tp.prop)") : TP(I);
            wr = wl;
            lcf_try(&wl, &wr, NULL, a, 1);
            break;
        }
        case 7: { /* typeApp */
            wl.rule = MG_RULE_TYPE_APP;
            wl.type = rnd(3) ? TP(I) : TP("(Tp.prop)");
            wr = wl;
            lcf_try(&wl, &wr, NULL, a, 1);
            break;
        }
        default: { /* typeLam */
            wl.rule = MG_RULE_TYPE_LAM;
            wr = wl;
            lcf_try(&wl, &wr, NULL, a, 1);
            break;
        }
        }
    }
    EXPECT(g_lcf_disagree == 0, "lazy LCF steps agree with the reference");
    printf("(LCF accepted:");
    for (int r = MG_RULE_HYP; r <= MG_RULE_TYPE_LAM; ++r)
        printf(" %s=%d", mg_rule_name((MgRule)r), g_lcf_ok[r]);
    printf("; refused:");
    for (int r = MG_RULE_HYP; r <= MG_RULE_TYPE_LAM; ++r)
        printf(" %s=%d", mg_rule_name((MgRule)r), g_lcf_refused[r]);
    printf(")\n");
}

/* ── fixed cases ──────────────────────────────────────────────────────── */

static void fixed_cases(void) {
    const char *why = NULL;
    MgThm *h = NULL, *g = NULL, *bad = NULL;
    /* termLam over a hypothesis that mentions the variable only in source
     * form: [x : iota][P (k c1 x)] |- P (k c1 x) generalizes, as the
     * reference does for its normal form [x][P c1] */
    MgCtx *ctx = mg_ctx(S, 0, mg_tplist_cons(S, TP(I), NULL),
                        mg_tmlist_cons(S, mg_tm_ap(S, NAME("P"),
                                                   mg_tm_ap(S, mg_tm_ap(S, NAME("k"), NAME("c1")),
                                                            mg_tm_db(S, 0))), NULL));
    MgWitness hyp = {.rule = MG_RULE_HYP, .index = 0};
    MgWitness tlam = {.rule = MG_RULE_TERM_LAM};
    EXPECT(mg_thm_step(T, &hyp, ctx, NULL, 0, NULL, &why, &h) == MG_OK, "hyp mentioning the variable");
    EXPECT(h && mg_thm_step(T, &tlam, NULL, &h, 1, NULL, &why, &g) == MG_OK &&
           g->judgment.ctx->hyps && !mg_tm_free_db(g->judgment.ctx->hyps->head, 0) &&
           !g->judgment.ctx->terms,
           "termLam normalizes exactly the hypothesis that mentions the variable");
    mg_thm_release(g);
    g = NULL;
    /* ... and refuses when the normal form still mentions it */
    MgCtx *dep = mg_ctx(S, 0, mg_tplist_cons(S, TP(I), NULL),
                        mg_tmlist_cons(S, mg_tm_ap(S, NAME("P"), mg_tm_ap(S, NAME("id"), mg_tm_db(S, 0))),
                                       NULL));
    mg_thm_release(h);
    h = NULL;
    EXPECT(mg_thm_step(T, &hyp, dep, NULL, 0, NULL, &why, &h) == MG_OK &&
           mg_thm_step(T, &tlam, NULL, &h, 1, NULL, &why, &bad) == MG_REFUSED && !bad,
           "termLam over a hypothesis whose normal form depends on the variable is refused");
    mg_thm_release(h);
    h = NULL;

    /* contexts are compared up to conversion: proofApp over premises in
     * convertible (not identical) contexts */
    MgTm *pd = mg_tm_ap(S, NAME("dp"), NAME("c1"));
    MgTm *pf2 = mg_tm_ap(S, NAME("P"), mg_tm_ap(S, NAME("d1"), NAME("c1")));
    MgCtx *c1 = mg_ctx(S, 0, NULL, mg_tmlist_cons(S, pd, NULL));
    MgCtx *c2 = mg_ctx(S, 0, NULL, mg_tmlist_cons(S, pf2, NULL));
    MgThm *imp = NULL, *ant = NULL, *mp = NULL, *ax = NULL;
    MgWitness known = {.rule = MG_RULE_KNOWN, .name = NM("ax_pq")};
    MgWitness tapp = {.rule = MG_RULE_TERM_APP, .arg = NAME("c1")};
    MgWitness papp = {.rule = MG_RULE_PROOF_APP};
    EXPECT(mg_thm_step(T, &known, c1, NULL, 0, NULL, &why, &ax) == MG_OK &&
           mg_thm_step(T, &tapp, NULL, &ax, 1, NULL, &why, &imp) == MG_OK &&
           mg_thm_step(T, &hyp, c2, NULL, 0, NULL, &why, &ant) == MG_OK, "premises in convertible contexts");
    MgThm *two[2] = {imp, ant};
    EXPECT(imp && ant && mg_thm_step(T, &papp, NULL, two, 2, NULL, &why, &mp) == MG_OK &&
           nf(mp->judgment.prop) == nf(mg_tm_ap(S, NAME("Q"), NAME("c1"))),
           "proofApp across convertible contexts");
    mg_thm_release(mp);
    mg_thm_release(ant);
    mg_thm_release(imp);
    mg_thm_release(ax);

    /* formedness: a context hypothesis, a stated operand and a stated row
     * conclusion are typed before they are converted */
    MgTm *self = mg_tm_lam(S, TP(I), mg_tm_ap(S, mg_tm_db(S, 0), mg_tm_db(S, 0)));
    MgTm *omega = mg_tm_ap(S, self, self);   /* ill-typed, no normal form */
    MgCtx *ill = mg_ctx(S, 0, NULL, mg_tmlist_cons(S, omega, NULL));
    EXPECT(mg_thm_step(T, &hyp, ill, NULL, 0, NULL, &why, &bad) == MG_REFUSED && !bad,
           "a hypothesis without a type is refused, not converted");
    MgThm *p = NULL;
    EXPECT(mg_thm_step(T, &known, S->ctx_empty, NULL, 0, NULL, &why, &ax) == MG_OK, "known");
    MgWitness tapp_bad = {.rule = MG_RULE_TERM_APP, .arg = NAME("c1"), .body = omega};
    EXPECT(mg_thm_step(T, &tapp_bad, NULL, &ax, 1, NULL, &why, &bad) == MG_REFUSED && !bad,
           "a stated body without a type is refused, not converted");
    EXPECT(mg_thm_step(T, &tapp, NULL, &ax, 1, NULL, &why, &p) == MG_OK, "termApp");
    /* lazy evidence of p, with the root's stated conclusion replaced */
    MgEvidence ev;
    if (p && mg_evidence_of(p, &ev) == MG_OK) {
        MgThm *thm = NULL;
        MgRow saved = ev.rows[ev.root];
        ev.rows[ev.root].conclusion.prop = omega;
        EXPECT(mg_evidence_check(T, &ev, NULL, &why, &thm) == MG_REFUSED && !thm,
               "a lazy row stating an untyped conclusion is refused, not converted");
        ev.rows[ev.root].conclusion.prop = mg_tm_imp(S, mg_tm_ap(S, NAME("P"), NAME("c1")),
                                                     mg_tm_ap(S, NAME("Q"), NAME("c1")));
        EXPECT(mg_evidence_check(T, &ev, NULL, &why, &thm) == MG_REFUSED && !thm,
               "a lazy row stating an inconvertible conclusion is refused");
        ev.rows[ev.root].conclusion.prop =
            mg_tm_ap(S, mg_tm_ap(S, NAME("dimp"), mg_tm_ap(S, NAME("P"), mg_tm_ap(S, NAME("f"),
                                                       mg_tm_ap(S, NAME("f"), NAME("c1"))))),
                     mg_tm_ap(S, NAME("Q"), mg_tm_ap(S, NAME("id"), NAME("c1"))));
        EXPECT(mg_evidence_check(T, &ev, NULL, &why, &thm) == MG_OK && thm,
               "a lazy row may state any formed convertible conclusion");
        mg_thm_release(thm);
        thm = NULL;
        ev.lazy = false;
        EXPECT(mg_evidence_check(T, &ev, NULL, &why, &thm) == MG_REFUSED && !thm,
               "the same rows are not exact evidence");
        ev.rows[ev.root] = saved;
        mg_evidence_free(&ev);
    } else {
        EXPECT(false, "evidence of termApp");
    }
    mg_thm_release(p);
    mg_thm_release(ax);

    /* the rules derive type quantifiers inside propositions (termLam over
     * a polymorphic known); stated data of that shape is formed */
    MgThm *refl = NULL, *gen2 = NULL, *inst2 = NULL;
    MgWitness kr = {.rule = MG_RULE_KNOWN, .name = NM("ax_refl")};
    MgCtx *x1 = mg_ctx(S, 0, mg_tplist_cons(S, TP(I), NULL), NULL);
    EXPECT(mg_thm_step(T, &kr, x1, NULL, 0, NULL, &why, &refl) == MG_OK &&
           mg_thm_step(T, &tlam, NULL, &refl, 1, NULL, &why, &gen2) == MG_OK &&
           gen2->judgment.prop->tag == MG_TM_ALL && gen2->judgment.prop->f->tag == MG_TM_TALL,
           "termLam over a polymorphic known: forall x. TAll ...");
    MgTm *eyy = TM(AP(AP("(Tm.typeApp " N("E") " (Tp.var 0))", "(Tm.db 0)"), "(Tm.db 0)"));
    MgTm *variant = mg_tm_tall(S, mg_tm_all(S, TP("(Tp.var 0)"),
                                            mg_tm_ap(S, mg_tm_lam(S, TP("(Tp.prop)"), mg_tm_db(S, 0)), eyy)));
    MgWitness tb = {.rule = MG_RULE_TERM_APP, .arg = NAME("c1"), .body = variant};
    EXPECT(gen2 && mg_thm_step(T, &tb, NULL, &gen2, 1, NULL, &why, &inst2) == MG_OK,
           "a stated body with a type quantifier in proposition position is formed");
    if (gen2 && mg_evidence_of(gen2, &ev) == MG_OK) {
        MgThm *thm = NULL;
        ev.rows[ev.root].conclusion.prop = mg_tm_all(S, TP(I), variant);
        EXPECT(mg_evidence_check(T, &ev, NULL, &why, &thm) == MG_OK && thm,
               "a lazy row stating forall x. TAll ... in a convertible form");
        mg_thm_release(thm);
        mg_evidence_free(&ev);
    }
    mg_thm_release(inst2);
    mg_thm_release(gen2);
    mg_thm_release(refl);

    /* a lazy theorem publishes under its source claim and under any
     * formed convertible claim; the reference form is never built */
    MgThm *tree = NULL;
    MgPf *proof = mg_pf_tmap(S, K("ax_p"), NAME("c2"));
    EXPECT(mg_thm_check_tree(T, S->ctx_empty, proof, NULL, &why, &tree) == MG_OK && tree->lazy,
           "a lazy tree theorem");
    const MgDecl *d = NULL;
    EXPECT(tree && mg_thm_publish(T, tree, NM("p_c2"), mg_tm_ap(S, NAME("dp"), NAME("c2")), NULL, &why,
                                  &d) == MG_OK, "published under a convertible claim");
    mg_thm_release(tree);
}

int main(int argc, char **argv) {
    int count = argc > 1 ? atoi(argv[1]) : 3000;
    if (argc > 2)
        g_rng = strtoull(argv[2], NULL, 10) | 1u;
    A = mg_sx_arena_new();
    theory_setup();
    fixed_cases();
    tree_cases(count);
    lcf_cases(count);
    for (uint32_t i = 0; i < g_pool_len; ++i) {
        mg_thm_release(g_pool[i].lazy);
        mg_thm_release(g_pool[i].ref);
    }
    mg_theory_release(T);
    mg_sx_arena_free(A);
    printf("(MgLazySummary %d %d disagreements=%d)\n", g_pass, g_fail, g_tree_disagree + g_lcf_disagree);
    return g_fail || g_tree_disagree || g_lcf_disagree ? 1 : 0;
}
