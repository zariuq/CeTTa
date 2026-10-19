/* test_kernel.c - adversarial tests of the kernel through its public API,
 * plus white-box checks (collision injection, standard certification,
 * representation invariants) through the internal header.
 *
 * Expected results are built independently with the constructors and
 * compared by OCaml-order structural comparison (hol_compare_terms = 0),
 * i.e. exactly, including bound-variable names. */
#include "hol_internal.h"
#include "hol_replay.h"
#include "test_util.h"

#include <pthread.h>

static HolKernel *K;
static HolRef ENV, BOOL, A, B, C;

static void die(const char *what, HolStatus s) {
    fprintf(stderr, "fatal: %s: %s (%s)\n", what, hol_status_name(s), hol_reason_name(hol_last_reason(K)));
    exit(2);
}
#define MUST(expr, what) do { HolStatus s_ = (expr); if (s_ != HOL_OK) die(what, s_); } while (0)

static const char *text(HolRef r);
static HolRef tyv(const char *n) { HolRef r; MUST(hol_mk_vartype(K, n, &r), n); return r; }
static HolRef tyapp(const char *n, const HolRef *args, size_t k) { HolRef r; MUST(hol_mk_type(K, ENV, n, args, k, &r), n); return r; }
static HolRef fn(HolRef a, HolRef b) { HolRef args[2] = {a, b}; return tyapp("fun", args, 2); }
static HolRef var(const char *n, HolRef ty) { HolRef r; MUST(hol_mk_var(K, n, ty, &r), n); return r; }
static HolRef comb(HolRef f, HolRef x) { HolRef r; MUST(hol_mk_comb(K, f, x, &r), "comb"); return r; }
static HolRef comb2(HolRef f, HolRef x, HolRef y) { return comb(comb(f, x), y); }
static HolRef lam(HolRef v, HolRef b) { HolRef r; MUST(hol_mk_abs(K, v, b, &r), "abs"); return r; }
static HolRef eqt(HolRef l, HolRef r) { HolRef o; MUST(hol_mk_eq(K, l, r, &o), "eq"); return o; }
/* A monomorphic constant, checked against the expected type. */
static HolRef cnst(const char *n, HolRef ty) {
    HolRef r, got;
    MUST(hol_mk_const(K, ENV, n, NULL, 0, &r), n);
    MUST(hol_type_of(K, r, &got), n);
    if (strcmp(text(got), text(ty)) != 0) die(n, HOL_MALFORMED);
    return r;
}
static HolRef eq_const(HolRef ty) { HolRef theta[2] = {ty, A}, r; MUST(hol_mk_const(K, ENV, "=", theta, 1, &r), "="); return r; }

static bool same(HolRef a, HolRef b) {
    int c = 1;
    return hol_compare_terms(K, a, b, &c) == HOL_OK && c == 0;
}
static HolRef concl(HolRef th) { HolRef r; MUST(hol_thm_concl(K, th, &r), "concl"); return r; }
static size_t nhyps(HolRef th) { size_t n = 0; MUST(hol_thm_nhyps(K, th, &n), "nhyps"); return n; }
static HolRef hyp(HolRef th, size_t i) { HolRef r; MUST(hol_thm_hyp(K, th, i, &r), "hyp"); return r; }
static const char *text(HolRef r) {
    static char bufs[4][8192];
    static int i;
    char *b = bufs[i++ & 3];
    size_t len;
    if (hol_print(K, r, b, sizeof bufs[0], &len) != HOL_OK) strcpy(b, "<print failed>");
    return b;
}
static HolReason reason(void) { return hol_last_reason(K); }

static void setup(const HolConfig *cfg) {
    K = hol_kernel_new(cfg);
    if (!K) { fprintf(stderr, "no kernel\n"); exit(2); }
    MUST(hol_env_bare(K, &ENV), "env");
    MUST(hol_bool_ty(K, &BOOL), "bool");
    A = tyv("A");
    B = tyv("B");
    C = tyv("C");
}

/* ------------------------------------------------------------------------ */

static void test_types_terms(void) {
    SECTION("types-terms");
    HolRef r;
    CHECK(hol_mk_type(K, ENV, "nosuch", NULL, 0, &r) == HOL_REFUSED && reason() == HOL_R_MK_TYPE_UNDECLARED);
    HolRef one[1] = {A};
    CHECK(hol_mk_type(K, ENV, "fun", one, 1, &r) == HOL_REFUSED && reason() == HOL_R_MK_TYPE_ARITY);
    CHECK(hol_mk_type(K, ENV, "bool", one, 1, &r) == HOL_REFUSED && reason() == HOL_R_MK_TYPE_ARITY);
    CHECK(hol_mk_const(K, ENV, "nosuch", NULL, 0, &r) == HOL_REFUSED && reason() == HOL_R_MK_CONST_UNDECLARED);
    HolRef x = var("x", A), f = var("f", fn(A, B)), y = var("y", B);
    CHECK(hol_mk_comb(K, f, y, &r) == HOL_REFUSED && reason() == HOL_R_MK_COMB_TYPES);
    CHECK(hol_mk_comb(K, x, x, &r) == HOL_REFUSED && reason() == HOL_R_MK_COMB_TYPES);
    HolRef fx = comb(f, x);
    HolRef ty;
    MUST(hol_type_of(K, fx, &ty), "type_of");
    CHECK(!strcmp(hol_type_name(K, ty), "B"));
    CHECK(hol_mk_abs(K, fx, x, &r) == HOL_REFUSED && reason() == HOL_R_MK_ABS_NOT_VAR);
    HolRef eqA = eq_const(A);
    CHECK(hol_mk_abs(K, eqA, x, &r) == HOL_REFUSED && reason() == HOL_R_MK_ABS_NOT_VAR);
    /* hash-consing: equal construction, equal object; digests agree */
    HolRef fx2 = comb(f, x);
    CHECK(same(fx, fx2));
    uint64_t d1, d2;
    MUST(hol_digest(K, fx, &d1), "dg");
    MUST(hol_digest(K, fx2, &d2), "dg");
    CHECK(d1 == d2);
    /* type_subst ignores non-variable keys; first pair wins */
    HolRef pairs[6] = {B, A, BOOL, A, B, BOOL};
    MUST(hol_type_subst(K, pairs, 3, fn(A, BOOL), &r), "type_subst");
    CHECK(!strcmp(text(r), text(fn(B, BOOL))));
    /* tyvars order follows lib.ml union */
    HolRef buf[4];
    size_t n;
    MUST(hol_tyvars(K, fn(A, fn(B, A)), buf, 4, &n), "tyvars");
    CHECK(n == 2 && !strcmp(hol_type_name(K, buf[0]), "B") && !strcmp(hol_type_name(K, buf[1]), "A"));
}

static void test_alpha_free(void) {
    SECTION("alpha-free");
    HolRef x = var("x", A), y = var("y", A), z = var("z", A);
    int c;
    MUST(hol_alphaorder(K, lam(x, x), lam(y, y), &c), "ao");
    CHECK(c == 0);
    MUST(hol_alphaorder(K, lam(x, y), lam(y, y), &c), "ao");
    CHECK(c != 0); /* free y vs bound y */
    MUST(hol_alphaorder(K, x, y, &c), "ao");
    CHECK(c != 0);
    /* binder types matter */
    HolRef xB = var("x", B);
    MUST(hol_alphaorder(K, lam(x, x), lam(xB, xB), &c), "ao");
    CHECK(c != 0);
    /* \x.\y. x y vs \y.\x. y x : alpha equal */
    HolRef f = var("f", fn(A, fn(A, A)));
    MUST(hol_alphaorder(K, lam(x, lam(y, comb2(f, x, y))), lam(y, lam(x, comb2(f, y, x))), &c), "ao");
    CHECK(c == 0);
    /* shadowing: \x.\x. x vs \x.\y. x are different (inner binds) */
    MUST(hol_alphaorder(K, lam(x, lam(x, x)), lam(x, lam(y, x)), &c), "ao");
    CHECK(c != 0);
    bool b;
    MUST(hol_vfree_in(K, x, lam(x, comb2(f, x, z)), &b), "vf");
    CHECK(!b);
    MUST(hol_vfree_in(K, z, lam(x, comb2(f, x, z)), &b), "vf");
    CHECK(b);
    /* frees order = lib.ml union: frees (f z y) */
    HolRef buf[4];
    size_t n;
    MUST(hol_frees(K, comb2(f, z, y), buf, 4, &n), "frees");
    CHECK(n == 3 && same(buf[0], f) && same(buf[1], z) && same(buf[2], y));
    /* variant primes until free in none */
    HolRef xp = var("x'", A), xpp = var("x''", A);
    HolRef avoid[2] = {comb2(f, x, xp), y};
    HolRef v;
    MUST(hol_variant(K, avoid, 2, x, &v), "variant");
    CHECK(same(v, xpp));
    /* variant of a constant not free in the avoid list returns it unchanged */
    HolRef eqA = eq_const(A);
    MUST(hol_variant(K, avoid, 2, eqA, &v), "variant const");
    CHECK(same(v, eqA));
    HolRef avoid2[1] = {eqA};
    CHECK(hol_variant(K, avoid2, 1, eqA, &v) == HOL_REFUSED && reason() == HOL_R_VARIANT_NOT_VAR);
    bool ok;
    HolRef acc[1] = {x};
    MUST(hol_freesin(K, acc, 1, lam(y, comb2(f, x, y)), &ok), "freesin");
    CHECK(!ok); /* f is free */
}

/* hol_compare_terms is OCaml's polymorphic compare on fusion.ml's term
 * type: constructor rank Var < Const < Comb < Abs, then fields left to
 * right (names by bytes then length); types Tyvar < Tyapp. */
static void test_compare_order(void) {
    SECTION("compare-order");
    HolRef x = var("x", A), e = var("e", fn(A, fn(A, BOOL)));
    HolRef eqA = eq_const(A);
    HolRef h = var("h", fn(fn(A, fn(A, BOOL)), BOOL));
    HolRef cv = comb(h, e), cc = comb(h, eqA), ab = lam(x, x);
    int c1, c2, c3, c4, c5, c6, c7;
    MUST(hol_compare_terms(K, x, eqA, &c1), "cmp");   /* Var < Const */
    MUST(hol_compare_terms(K, eqA, cv, &c2), "cmp");  /* Const < Comb */
    MUST(hol_compare_terms(K, cv, ab, &c3), "cmp");   /* Comb < Abs */
    MUST(hol_compare_terms(K, cv, cc, &c4), "cmp");   /* inside: Var < Const */
    MUST(hol_compare_terms(K, ab, x, &c5), "cmp");    /* antisymmetry */
    MUST(hol_compare_terms(K, var("x", A), var("x", BOOL), &c6), "cmp"); /* Tyvar < Tyapp */
    MUST(hol_compare_terms(K, var("ab", A), var("b", A), &c7), "cmp");   /* bytes first */
    CHECK(c1 < 0 && c2 < 0 && c3 < 0 && c4 < 0 && c5 > 0 && c6 < 0 && c7 < 0);
    int c8;
    MUST(hol_compare_terms(K, var("x", A), var("xx", A), &c8), "cmp");   /* prefix: shorter first */
    CHECK(c8 < 0);
}

static void test_vsubst_exact(void) {
    SECTION("vsubst");
    HolRef x = var("x", A), y = var("y", A), z = var("z", A), r;
    HolRef yp = var("y'", A), ypp = var("y''", A);
    HolRef f = var("f", fn(A, fn(A, A)));
    /* capture: [y/x] (\y. x) = \y'. y */
    HolRef p1[2] = {y, x};
    MUST(hol_vsubst(K, p1, 1, lam(y, x), &r), "vsubst");
    CHECK(same(r, lam(yp, y)));
    /* rename past an existing y': [y/x](\y. f x y') = \y''. f y y' */
    MUST(hol_vsubst(K, p1, 1, lam(y, comb2(f, x, yp)), &r), "vsubst2");
    CHECK(same(r, lam(ypp, comb2(f, y, yp))));
    /* no capture when the binder is not free in the replacement */
    HolRef p2[2] = {z, x};
    MUST(hol_vsubst(K, p2, 1, lam(y, comb2(f, x, y)), &r), "vsubst3");
    CHECK(same(r, lam(y, comb2(f, z, y))));
    /* bound occurrences are not substituted */
    MUST(hol_vsubst(K, p2, 1, lam(x, x), &r), "vsubst4");
    CHECK(same(r, lam(x, x)));
    /* simultaneous swap */
    HolRef sw[4] = {y, x, x, y};
    MUST(hol_vsubst(K, sw, 2, comb2(f, x, y), &r), "swap");
    CHECK(same(r, comb2(f, y, x)));
    /* first pair wins for a repeated target */
    HolRef dup[4] = {y, x, z, x};
    MUST(hol_vsubst(K, dup, 2, comb2(f, x, x), &r), "dup");
    CHECK(same(r, comb2(f, y, y)));
    /* bad substitution lists */
    HolRef bad1[2] = {var("b", BOOL), x};
    CHECK(hol_vsubst(K, bad1, 1, x, &r) == HOL_REFUSED && reason() == HOL_R_VSUBST_BAD);
    HolRef bad2[2] = {y, comb2(f, x, x)};
    CHECK(hol_vsubst(K, bad2, 1, x, &r) == HOL_REFUSED && reason() == HOL_R_VSUBST_BAD);
    /* empty substitution is the identity, even on anything */
    MUST(hol_vsubst(K, NULL, 0, lam(y, x), &r), "empty");
    CHECK(same(r, lam(y, x)));
    /* D1 (docs/CONTRACT.md): an identity pair first, then a shadowed pair whose
     * replacement mentions the binder.  Under hash-consing the body is
     * unchanged, so no renaming happens; fusion.ml renames when the identity
     * replacement is a physically different object.  Both are alpha-equal. */
    HolRef d1[4] = {x, x, z, x};
    MUST(hol_vsubst(K, d1, 2, lam(z, comb2(f, x, z)), &r), "d1");
    CHECK(same(r, lam(z, comb2(f, x, z))));
}

static void test_inst_exact(void) {
    SECTION("inst");
    HolRef xA = var("x", A), xB = var("x", B), r;
    HolRef xpB = var("x'", B), xppB = var("x''", B);
    HolRef ty[2] = {B, A};
    /* \x:A. x:B  [B/A]  =  \x':B. x:B */
    MUST(hol_inst(K, ty, 1, lam(xA, xB), &r), "inst");
    CHECK(same(r, lam(xpB, xB)));
    /* rename must also avoid an instantiated x':A that becomes x':B */
    HolRef g = var("g", fn(B, fn(B, BOOL)));
    HolRef xpA = var("x'", A);
    HolRef ga = var("g", fn(A, fn(B, BOOL)));
    MUST(hol_inst(K, ty, 1, lam(xA, comb2(ga, xpA, xB)), &r), "inst2");
    CHECK(same(r, lam(xppB, comb2(g, xpB, xB))));
    /* bound occurrence stays bound */
    MUST(hol_inst(K, ty, 1, lam(xA, xA), &r), "inst3");
    CHECK(same(r, lam(xB, xB)));
    /* clash on an outer binder from an inner free variable */
    HolRef yA = var("y", A);
    HolRef hB = var("h", fn(B, fn(B, BOOL)));
    HolRef hA = var("h", fn(A, fn(B, BOOL)));
    MUST(hol_inst(K, ty, 1, lam(xA, lam(yA, comb2(hA, yA, xB))), &r), "inst4");
    CHECK(same(r, lam(xpB, lam(var("y", B), comb2(hB, var("y", B), xB)))));
    /* free variables of different types collapse (no clash without binders) */
    HolRef q = var("q", fn(B, fn(B, BOOL)));
    HolRef qa = var("q", fn(A, fn(B, BOOL)));
    MUST(hol_inst(K, ty, 1, comb2(qa, xA, xB), &r), "collapse");
    CHECK(same(r, comb2(q, xB, xB)));
    /* non-variable keys are ignored */
    HolRef tynv[2] = {B, BOOL};
    MUST(hol_inst(K, tynv, 1, lam(xA, xB), &r), "nonvar key");
    CHECK(same(r, lam(xA, xB)));
}

static void test_rules(void) {
    SECTION("rules");
    HolRef x = var("x", A), y = var("y", A), p = var("p", BOOL), q = var("q", BOOL), th, th2, r;
    HolRef f = var("f", fn(A, A)), g = var("g", fn(A, A));
    /* REFL */
    MUST(hol_REFL(K, x, &th), "refl");
    CHECK(same(concl(th), eqt(x, x)) && nhyps(th) == 0);
    /* TRANS: alpha-equivalent middle accepted */
    HolRef lx = lam(x, x), ly = lam(y, y);
    HolRef t1, t2;
    MUST(hol_REFL(K, lx, &t1), "refl lx");
    MUST(hol_REFL(K, ly, &t2), "refl ly");
    MUST(hol_TRANS(K, t1, t2, &r), "trans alpha");
    CHECK(same(concl(r), eqt(lx, ly)));
    MUST(hol_REFL(K, y, &th2), "refl y");
    CHECK(hol_TRANS(K, th, th2, &r) == HOL_REFUSED && reason() == HOL_R_TRANS);
    HolRef ap;
    MUST(hol_ASSUME(K, p, &ap), "assume");
    CHECK(hol_TRANS(K, ap, ap, &r) == HOL_REFUSED && reason() == HOL_R_TRANS);
    /* MK_COMB */
    HolRef rf;
    MUST(hol_REFL(K, f, &rf), "refl f");
    MUST(hol_MK_COMB(K, rf, th, &r), "mk_comb");
    CHECK(same(concl(r), eqt(comb(f, x), comb(f, x))));
    HolRef rb;
    MUST(hol_REFL(K, var("b", BOOL), &rb), "refl b");
    CHECK(hol_MK_COMB(K, rf, rb, &r) == HOL_REFUSED && reason() == HOL_R_MK_COMB_RULE_TYPES);
    CHECK(hol_MK_COMB(K, ap, th, &r) == HOL_REFUSED && reason() == HOL_R_MK_COMB_NOT_EQS);
    CHECK(hol_MK_COMB(K, th, rf, &r) == HOL_REFUSED && reason() == HOL_R_MK_COMB_RULE_TYPES);
    /* ABS: forbidden free variable in a hypothesis */
    HolRef ax;
    MUST(hol_ASSUME(K, eqt(x, y), &ax), "assume x=y");
    CHECK(hol_ABS(K, x, ax, &r) == HOL_REFUSED && reason() == HOL_R_ABS_FREE_IN_HYP);
    HolRef z = var("z", A);
    MUST(hol_ABS(K, z, ax, &r), "abs ok");
    CHECK(same(concl(r), eqt(lam(z, x), lam(z, y))) && nhyps(r) == 1);
    CHECK(hol_ABS(K, comb(f, x), th, &r) == HOL_REFUSED && reason() == HOL_R_ABS_NOT_VAR_OR_EQ);
    CHECK(hol_ABS(K, x, ap, &r) == HOL_REFUSED && reason() == HOL_R_ABS_NOT_VAR_OR_EQ);
    /* BETA: only (\v. b) v */
    MUST(hol_BETA(K, comb(lam(x, comb(f, x)), x), &r), "beta");
    CHECK(same(concl(r), eqt(comb(lam(x, comb(f, x)), x), comb(f, x))));
    CHECK(hol_BETA(K, comb(lam(x, comb(f, x)), y), &r) == HOL_REFUSED && reason() == HOL_R_BETA);
    CHECK(hol_BETA(K, comb(f, x), &r) == HOL_REFUSED && reason() == HOL_R_BETA);
    CHECK(hol_BETA(K, lam(x, x), &r) == HOL_REFUSED && reason() == HOL_R_BETA);
    /* ASSUME */
    CHECK(hol_ASSUME(K, x, &r) == HOL_REFUSED && reason() == HOL_R_ASSUME_NOT_BOOL);
    /* EQ_MP with an alpha-equivalent proposition */
    HolRef P = var("P", fn(A, BOOL));
    HolRef all1 = eqt(lam(x, comb(P, x)), lam(x, comb(P, x)));
    HolRef all2 = eqt(lam(y, comb(P, y)), lam(y, comb(P, y)));
    HolRef e1, e2;
    MUST(hol_ASSUME(K, all2, &e2), "assume all2");
    MUST(hol_REFL(K, all1, &e1), "refl all1");
    MUST(hol_EQ_MP(K, e1, e2, &r), "eq_mp alpha");
    CHECK(same(concl(r), all1) && nhyps(r) == 1 && same(hyp(r, 0), all2));
    CHECK(hol_EQ_MP(K, e1, ap, &r) == HOL_REFUSED && reason() == HOL_R_EQ_MP);
    CHECK(hol_EQ_MP(K, ap, ap, &r) == HOL_REFUSED && reason() == HOL_R_EQ_MP);
    /* DEDUCT_ANTISYM_RULE: {p} |- p, {q} |- q  ==>  {p, q} |- p = q;
     * with alpha-equal hypotheses discharged */
    HolRef aq;
    MUST(hol_ASSUME(K, q, &aq), "assume q");
    MUST(hol_DEDUCT_ANTISYM_RULE(K, ap, aq, &r), "deduct");
    CHECK(same(concl(r), eqt(p, q)) && nhyps(r) == 2);
    MUST(hol_DEDUCT_ANTISYM_RULE(K, ap, ap, &r), "deduct self");
    CHECK(same(concl(r), eqt(p, p)) && nhyps(r) == 0);
    HolRef a1, a2;
    MUST(hol_ASSUME(K, all1, &a1), "a1");
    MUST(hol_ASSUME(K, all2, &a2), "a2");
    MUST(hol_DEDUCT_ANTISYM_RULE(K, a1, a2, &r), "deduct alpha");
    CHECK(nhyps(r) == 0); /* each discharges the other's alpha-equal hypothesis */
    /* hypothesis sets are alpha-aware: merging alpha-equal hypotheses */
    MUST(hol_DEDUCT_ANTISYM_RULE(K, a1, ap, &r), "deduct mixed");
    HolRef m;
    MUST(hol_EQ_MP(K, r, a1, &m), "eqmp merge"); /* hyps {all1, p} u {all1} */
    CHECK(nhyps(m) == 2);
    /* INST: instantiation can merge two hypotheses into one */
    HolRef hx, hy, both;
    MUST(hol_ASSUME(K, eqt(x, x), &hx), "hx");
    MUST(hol_ASSUME(K, eqt(y, y), &hy), "hy");
    MUST(hol_DEDUCT_ANTISYM_RULE(K, hx, hy, &both), "both");
    CHECK(nhyps(both) == 2);
    HolRef sub[2] = {x, y};
    MUST(hol_INST(K, sub, 1, both, &r), "inst merge");
    CHECK(nhyps(r) == 1);
    /* INST_TYPE preserves hypotheses and conclusion structure */
    HolRef tys[2] = {BOOL, A};
    MUST(hol_INST_TYPE(K, tys, 1, hx, &r), "inst_type");
    CHECK(same(concl(r), eqt(var("x", BOOL), var("x", BOOL))) && nhyps(r) == 1);
    /* INST with a bad list is refused before touching the theorem */
    HolRef bad[2] = {p, x};
    CHECK(hol_INST(K, bad, 1, hx, &r) == HOL_REFUSED && reason() == HOL_R_VSUBST_BAD);
    (void)g;
}

static void test_definitions(void) {
    SECTION("definitions");
    HolRef env0 = ENV, r, envd, th;
    HolRef p = var("p", BOOL), x = var("x", A);
    HolRef c = var("c", fn(BOOL, BOOL));
    HolRef body = lam(p, p);
    /* closed, ok */
    MUST(hol_new_basic_definition(K, ENV, eqt(c, body), &envd, &th), "def");
    ENV = envd;
    CHECK(same(concl(th), eqt(cnst("c", fn(BOOL, BOOL)), body)));
    /* redefinition: the left side is now parsed as a variable again */
    CHECK(hol_new_basic_definition(K, ENV, eqt(c, body), &r, &th) == HOL_REFUSED &&
          reason() == HOL_R_CONST_EXISTS);
    /* left side already a constant */
    HolRef cc = cnst("c", fn(BOOL, BOOL));
    CHECK(hol_new_basic_definition(K, ENV, eqt(cc, body), &r, &th) == HOL_REFUSED &&
          reason() == HOL_R_DEF_ALREADY_CONST);
    /* free variable on the right */
    HolRef d = var("d", BOOL), q = var("q", BOOL);
    CHECK(hol_new_basic_definition(K, ENV, eqt(d, q), &r, &th) == HOL_REFUSED &&
          reason() == HOL_R_DEF_NOT_CLOSED);
    /* type variable not reflected in the constant's type */
    HolRef e = var("e", BOOL);
    HolRef ex = lam(x, x);
    HolRef xeq = eqt(ex, ex); /* (\x:A. x) = (\x:A. x) : bool mentions A */
    CHECK(hol_new_basic_definition(K, ENV, eqt(e, xeq), &r, &th) == HOL_REFUSED &&
          reason() == HOL_R_DEF_TYVARS);
    /* not an equation / left side not a variable */
    CHECK(hol_new_basic_definition(K, ENV, p, &r, &th) == HOL_REFUSED && reason() == HOL_R_DEF_SHAPE);
    HolRef ff = var("ff", fn(BOOL, BOOL));
    CHECK(hol_new_basic_definition(K, ENV, eqt(comb(ff, p), p), &r, &th) == HOL_REFUSED &&
          reason() == HOL_R_DEF_SHAPE);
    /* a recursive-looking definition: the right side mentions a VARIABLE
     * with the constant's name, which is free, hence refused */
    HolRef rec = var("rec", fn(BOOL, BOOL));
    CHECK(hol_new_basic_definition(K, ENV, eqt(rec, lam(p, comb(rec, p))), &r, &th) == HOL_REFUSED &&
          reason() == HOL_R_DEF_NOT_CLOSED);
    /* polymorphic definition: type variable reflected */
    HolRef idc = var("idc", fn(A, A));
    MUST(hol_new_basic_definition(K, ENV, eqt(idc, lam(x, x)), &envd, &th), "poly def");
    ENV = envd;
    uint64_t len;
    MUST(hol_env_length(K, ENV, &len), "len");
    CHECK(len == 2);
    (void)env0;
}

static HolRef truth_witness(HolRef *T_out) {
    /* T := (\p. p) = (\p. p); witness |- T = T, i.e. P x with P = (=) T */
    HolRef p = var("p", BOOL), Tv = var("T", BOOL), envd, th;
    HolRef idp = lam(p, p);
    MUST(hol_new_basic_definition(K, ENV, eqt(Tv, eqt(idp, idp)), &envd, &th), "T");
    ENV = envd;
    HolRef T = cnst("T", BOOL);
    *T_out = T;
    HolRef w;
    MUST(hol_REFL(K, T, &w), "witness");
    return w;
}

static void test_type_definitions(void) {
    SECTION("type-definitions");
    HolRef T;
    HolRef w = truth_witness(&T);
    HolRef env1 = ENV, envd, t1, t2, r1, r2, envx;
    MUST(hol_new_basic_type_definition(K, ENV, "one", "mk_one", "dest_one", w, &envd, &t1, &t2), "typedef");
    /* |- mk_one (dest_one a) = a ;  |- ((=) T) r = (dest_one (mk_one r) = r) */
    ENV = envd;
    HolRef one = tyapp("one", NULL, 0);
    HolRef a = var("a", one), rr = var("r", BOOL);
    HolRef mk = cnst("mk_one", fn(BOOL, one)), dst = cnst("dest_one", fn(one, BOOL));
    CHECK(same(concl(t1), eqt(comb(mk, comb(dst, a)), a)));
    HolRef P = comb(eq_const(BOOL), T);
    CHECK(same(concl(t2), eqt(comb(P, rr), eqt(comb(dst, comb(mk, rr)), rr))));
    /* refusals, in fusion.ml's order */
    CHECK(hol_new_basic_type_definition(K, ENV, "two", "mk_one", "d2", w, &envx, &r1, &r2) == HOL_REFUSED &&
          reason() == HOL_R_TYDEF_CONSTS_IN_USE);
    HolRef ap;
    MUST(hol_ASSUME(K, eqt(T, T), &ap), "assume");
    CHECK(hol_new_basic_type_definition(K, ENV, "two", "m2", "d2", ap, &envx, &r1, &r2) == HOL_REFUSED &&
          reason() == HOL_R_TYDEF_HYPS);
    HolRef q = var("q", BOOL), envq, axq;
    MUST(hol_new_axiom(K, ENV, q, &envq, &axq), "axiom q");
    CHECK(hol_new_basic_type_definition(K, envq, "two", "m2", "d2", axq, &envx, &r1, &r2) == HOL_REFUSED &&
          reason() == HOL_R_TYDEF_NOT_COMB);
    HolRef x = var("x", A), wfree;
    MUST(hol_REFL(K, x, &wfree), "refl x"); /* P = (=) x is not closed */
    CHECK(hol_new_basic_type_definition(K, ENV, "two", "m2", "d2", wfree, &envx, &r1, &r2) == HOL_REFUSED &&
          reason() == HOL_R_TYDEF_NOT_CLOSED);
    CHECK(hol_new_basic_type_definition(K, ENV, "one", "m2", "d2", w, &envx, &r1, &r2) == HOL_REFUSED &&
          reason() == HOL_R_TYDEF_TYPE_EXISTS);
    /* native atomic refusal; fusion.ml would leave "two" and "same" declared */
    CHECK(hol_new_basic_type_definition(K, ENV, "two", "same", "same", w, &envx, &r1, &r2) == HOL_REFUSED &&
          reason() == HOL_R_TYDEF_SAME_NAMES);
    uint32_t ar;
    CHECK(hol_get_type_arity(K, ENV, "two", &ar) == HOL_REFUSED);
    HolRef gty;
    CHECK(hol_get_const_type(K, ENV, "same", &gty) == HOL_REFUSED);
    /* polymorphic type definition: parameters are the sorted type variables */
    HolRef xb = var("x", B), xa = var("x", A);
    HolRef pr = lam(xa, lam(xb, T)); /* closed, mentions A and B */
    HolRef wp;
    MUST(hol_REFL(K, pr, &wp), "refl pr"); /* P = (=) pr, x = pr */
    MUST(hol_new_basic_type_definition(K, ENV, "pt", "mk_pt", "dest_pt", wp, &envd, &t1, &t2), "poly typedef");
    ENV = envd;
    MUST(hol_get_type_arity(K, ENV, "pt", &ar), "arity");
    CHECK(ar == 2);
    (void)env1;
}

static void test_axioms_policy(void) {
    SECTION("axioms-policy");
    HolRef p = var("p", BOOL), envd, ax, r, th;
    MUST(hol_new_axiom(K, ENV, p, &envd, &ax), "axiom");
    bool stdok = true;
    MUST(hol_thm_standard(K, ax, &stdok), "std");
    CHECK(!stdok);
    HolRef ap;
    MUST(hol_ASSUME(K, p, &ap), "assume");
    MUST(hol_DEDUCT_ANTISYM_RULE(K, ax, ap, &th), "uses axiom");
    HolRef deps[2];
    size_t n;
    MUST(hol_thm_axioms(K, th, deps, 2, &n), "deps");
    CHECK(n == 1);
    MUST(hol_thm_standard(K, th, &stdok), "std2");
    CHECK(!stdok);
    HolRef refl;
    MUST(hol_REFL(K, p, &refl), "refl");
    MUST(hol_thm_axioms(K, refl, deps, 2, &n), "deps0");
    CHECK(n == 0);
    MUST(hol_thm_standard(K, refl, &stdok), "std3");
    CHECK(stdok); /* no axiom at all */
    CHECK(hol_new_axiom(K, ENV, var("x", A), &r, &th) == HOL_REFUSED && reason() == HOL_R_AXIOM_NOT_BOOL);
    HolRef sealed, envx;
    MUST(hol_env_set_axiom_policy(K, ENV, HOL_AXIOMS_SEALED, &sealed), "seal");
    CHECK(hol_new_axiom(K, sealed, p, &envx, &th) == HOL_POLICY && reason() == HOL_R_AXIOM_POLICY);
    CHECK(hol_env_set_axiom_policy(K, sealed, HOL_AXIOMS_OPEN, &envx) == HOL_POLICY &&
          reason() == HOL_R_POLICY_LOOSEN);
    HolRef stdenv;
    MUST(hol_env_set_axiom_policy(K, ENV, HOL_AXIOMS_STANDARD, &stdenv), "standard");
    CHECK(hol_new_axiom(K, stdenv, p, &envx, &th) == HOL_POLICY);
}

/* Builds the standard theory from the embedded canonical texts through the
 * internal constructors, and checks the certification labels. */
#include "hol_std_data.inc"
static void test_standard_certification(void) {
    SECTION("standard");
    HolKernel *k = hol_kernel_new(NULL);
    HolDecl *env = k->d_eq;
    for (size_t i = 0; i < sizeof hol_std_defs / sizeof hol_std_defs[0]; i++) {
        hk_begin_call(k);
        if (!strcmp(hol_std_defs[i].name, "ONE_ONE")) { /* nums.ml order: @ and ind first */
            HolType *cty = hk_parse_type(k, env, hol_std_choice_type, strlen(hol_std_choice_type));
            env = hk_env_new_constant(k, env, hk_name_intern(k, "@", 1), cty);
            env = hk_env_new_type(k, env, hk_name_intern(k, "ind", 3), 0);
        }
        HolTerm *body = hk_parse_term(k, env, hol_std_defs[i].body, strlen(hol_std_defs[i].body));
        CHECK(body != NULL);
        if (!body) break;
        HolTerm *v = hk_tm_var(k, hk_name_intern(k, hol_std_defs[i].name, strlen(hol_std_defs[i].name)), body->type);
        HolThm *th;
        HolDecl *d = hk_env_new_definition(k, env, hk_tm_eq(k, v, body), &th);
        CHECK(d != NULL);
        env = d;
        CHECK(hk_decl_standard(k, d) == 1);
    }
    for (int i = 0; i < 3; i++) {
        hk_begin_call(k);
        HolTerm *ax = hk_parse_term(k, env, hol_std_axioms[i], strlen(hol_std_axioms[i]));
        HolThm *th;
        HolDecl *d = ax ? hk_env_new_axiom(k, env, ax, &th) : NULL;
        CHECK(d && d->authority == HOL_AUTH_STD_ETA + i);
        if (d) env = d;
        if (d) CHECK(hk_thm_is_standard(k, th));
    }
    /* under the STANDARD policy the same axioms are admitted again on a
     * fresh branch, and a user axiom is refused */
    hk_begin_call(k);
    HolDecl *pol = hk_env_set_policy(k, env, HOL_AXIOMS_STANDARD);
    CHECK(pol != NULL);
    HolTerm *p = hk_tm_var(k, hk_name_intern(k, "p", 1), k->bool_ty);
    HolThm *th;
    CHECK(hk_env_new_axiom(k, pol, p, &th) == NULL && k->status == HOL_POLICY);
    hol_kernel_free(k);

    /* a lookalike: "!" defined differently makes ETA_AX non-standard */
    k = hol_kernel_new(NULL);
    env = k->d_eq;
    hk_begin_call(k);
    HolTerm *tb = hk_parse_term(k, env, hol_std_defs[0].body, strlen(hol_std_defs[0].body));
    HolDecl *dT = hk_env_new_definition(k, env, hk_tm_eq(k, hk_tm_var(k, hk_name_intern(k, "T", 1), k->bool_ty), tb), &th);
    env = dT;
    /* ! := \P. P = P  (wrong) */
    const char *bad_forall = "(L (v \"P\" (T \"fun\" (V \"A\") (T \"bool\"))) (A (A (c \"=\" (T \"fun\" (T \"fun\" (V \"A\") (T \"bool\")) (T \"fun\" (T \"fun\" (V \"A\") (T \"bool\")) (T \"bool\")))) (v \"P\" (T \"fun\" (V \"A\") (T \"bool\")))) (v \"P\" (T \"fun\" (V \"A\") (T \"bool\")))))";
    HolTerm *fb = hk_parse_term(k, env, bad_forall, strlen(bad_forall));
    CHECK(fb != NULL);
    HolDecl *dF = hk_env_new_definition(k, env, hk_tm_eq(k, hk_tm_var(k, hk_name_intern(k, "!", 1), fb->type), fb), &th);
    CHECK(dF != NULL && hk_decl_standard(k, dF) == 0);
    env = dF;
    HolTerm *eta = hk_parse_term(k, env, hol_std_axioms[0], strlen(hol_std_axioms[0]));
    CHECK(eta != NULL);
    HolDecl *dE = eta ? hk_env_new_axiom(k, env, eta, &th) : NULL;
    CHECK(dE && dE->authority == HOL_AUTH_USER_AXIOM);
    if (dE) CHECK(!hk_thm_is_standard(k, th));
    hol_kernel_free(k);
}

static void test_handles(void) {
    SECTION("handles");
    HolRef x = var("x", A), th, th2, r;
    MUST(hol_REFL(K, x, &th), "refl");
    MUST(hol_REFL(K, x, &th2), "refl again");
    bool sp;
    MUST(hol_thm_same_payload(K, th, th2, &sp), "same");
    CHECK(sp); /* shared payload ... */
    uint64_t e1, e2;
    MUST(hol_thm_event(K, th, &e1), "ev");
    MUST(hol_thm_event(K, th2, &e2), "ev2");
    CHECK(e1 != e2); /* ... separate proof events */
    HolRef d;
    MUST(hol_ref_dup(K, th, &d), "dup");
    uint64_t e3;
    MUST(hol_thm_event(K, d, &e3), "ev3");
    CHECK(e3 == e1 && d != th); /* a second delivery of the same event */
    /* stale */
    MUST(hol_ref_release(K, d), "release");
    CHECK(hol_ref_release(K, d) == HOL_STALE && reason() == HOL_R_STALE_HANDLE);
    CHECK(hol_REFL(K, d, &r) == HOL_STALE);
    CHECK(hol_thm_concl(K, d, &r) == HOL_STALE);
    /* wrong kind */
    CHECK(hol_REFL(K, th, &r) == HOL_MALFORMED && reason() == HOL_R_WRONG_KIND);
    CHECK(hol_mk_abs(K, A, x, &r) == HOL_MALFORMED);
    /* forged: future slot, future generation, garbage, zero */
    HolRef forged = (x & ~(HolRef)0xffffffffu) | 0x7fffffffu;
    CHECK(hol_REFL(K, forged, &r) == HOL_STALE && reason() == HOL_R_FUTURE_HANDLE);
    HolRef future_gen = x + ((HolRef)1 << 32);
    CHECK(hol_REFL(K, future_gen, &r) == HOL_STALE && reason() == HOL_R_FUTURE_HANDLE);
    CHECK(hol_REFL(K, 0, &r) == HOL_MALFORMED && reason() == HOL_R_NULL_ARG);
    CHECK(hol_REFL(K, 0x123456789abcdefULL, &r) != HOL_OK);
    /* cross-kernel */
    HolKernel *k2 = hol_kernel_new(NULL);
    HolRef x2;
    MUST(hol_mk_vartype(k2, "A", &x2), "k2");
    HolRef v2;
    CHECK(hol_mk_var(K, "x", x2, &v2) == HOL_FOREIGN && reason() == HOL_R_FOREIGN_KERNEL);
    hol_kernel_free(k2);
    /* owners */
    HolOwner o = hol_owner_new(K);
    HolOwner prev = hol_owner_set_current(K, o);
    HolRef a = var("ownedvar", A), b;
    MUST(hol_REFL(K, a, &b), "owned refl");
    hol_owner_set_current(K, prev);
    MUST(hol_owner_release(K, o), "owner release");
    CHECK(hol_thm_concl(K, b, &r) == HOL_STALE);
    CHECK(hol_owner_release(K, o) == HOL_STALE);
    /* a moved reference survives its old owner */
    HolOwner o2 = hol_owner_new(K);
    prev = hol_owner_set_current(K, o2);
    HolRef keep;
    MUST(hol_REFL(K, x, &keep), "keep");
    hol_owner_set_current(K, prev);
    MUST(hol_ref_move(K, keep, HOL_OWNER_KERNEL), "move");
    MUST(hol_owner_release(K, o2), "release o2");
    CHECK(hol_thm_concl(K, keep, &r) == HOL_OK);
    /* generations: a slot reused after release does not revive old refs */
    HolRef t1 = var("gen1", A);
    MUST(hol_ref_release(K, t1), "rel");
    HolRef t2 = var("gen2", A);
    CHECK(hol_term_kind(K, t1) == HOL_TERM_INVALID);
    CHECK(hol_term_kind(K, t2) == HOL_TERM_VAR);
    /* generations: a slot reused until its 12-bit generation is exhausted is
     * retired, so no stale reference ever aliases a later object */
    MUST(hol_ref_release(K, t2), "rel t2");
    HolRef first = var("cycle", A), cur = first;
    unsigned aliased = 0, reissued_same_slot = 0;
    MUST(hol_ref_release(K, first), "rel first");
    for (int i = 0; i < 5000; i++) {
        MUST(hol_mk_var(K, "cycle", A, &cur), "cycle");
        if ((cur & 0xffffffffu) == (first & 0xffffffffu)) reissued_same_slot++;
        if (cur == first || hol_term_kind(K, first) != HOL_TERM_INVALID) aliased++;
        MUST(hol_ref_release(K, cur), "rel cur");
    }
    CHECK(aliased == 0);
    CHECK(reissued_same_slot > 0 && reissued_same_slot < 5000);
}

static void test_branches(void) {
    SECTION("branches");
    HolRef base = ENV, ea, eb, tha, thb, r;
    HolRef p = var("p", BOOL);
    HolRef cv = var("cbr", BOOL);
    HolRef idp = lam(p, p);
    /* branch a: cbr := (\p.p) = (\p.p); branch b: cbr := p ==> ... (another body) */
    MUST(hol_new_basic_definition(K, base, eqt(cv, eqt(idp, idp)), &ea, &tha), "a");
    HolRef q = var("q", BOOL);
    MUST(hol_new_basic_definition(K, base, eqt(cv, eqt(lam(q, q), lam(q, q))), &eb, &thb), "b");
    bool ok;
    MUST(hol_env_admits(K, ea, tha, &ok), "admits");
    CHECK(ok);
    MUST(hol_env_admits(K, eb, tha, &ok), "admits b");
    CHECK(!ok); /* an old theorem never acquires the other branch's meaning */
    MUST(hol_env_extends(K, base, ea, &ok), "extends");
    CHECK(ok);
    MUST(hol_env_extends(K, ea, eb, &ok), "extends ab");
    CHECK(!ok);
    CHECK(hol_TRANS(K, tha, thb, &r) == HOL_FOREIGN && reason() == HOL_R_INCOMPATIBLE_ENV);
    CHECK(hol_DEDUCT_ANTISYM_RULE(K, tha, thb, &r) == HOL_FOREIGN);
    /* same spelling, distinct constants */
    HolRef ca, cb;
    HolRef save = ENV;
    ENV = ea;
    ca = cnst("cbr", BOOL);
    ENV = eb;
    cb = cnst("cbr", BOOL);
    ENV = save;
    CHECK(!strcmp(text(ca), text(cb)));
    int c;
    MUST(hol_compare_terms(K, ca, cb, &c), "cmp");
    CHECK(c != 0);
    CHECK(hol_mk_eq(K, ca, cb, &r) == HOL_FOREIGN);
    /* the base theory is unchanged by either branch */
    HolRef ty;
    CHECK(hol_get_const_type(K, base, "cbr", &ty) == HOL_REFUSED);
    /* branch digests differ; identical histories give identical digests */
    uint64_t da, db;
    MUST(hol_env_digest(K, ea, &da), "da");
    MUST(hol_env_digest(K, eb, &db), "db");
    CHECK(da != db);
}

static void test_gc(void) {
    SECTION("gc");
    HolStats s0, s1, s2;
    hol_gc(K);
    hol_stats(K, &s0);
    HolOwner o = hol_owner_new(K);
    HolOwner prev = hol_owner_set_current(K, o);
    HolRef keep = 0;
    for (int i = 0; i < 2000; i++) {
        char nm[32];
        snprintf(nm, sizeof nm, "tmp%d", i);
        HolRef v = var(nm, A), th;
        MUST(hol_REFL(K, lam(v, v), &th), "refl tmp");
        if (i == 7) MUST(hol_ref_dup(K, th, &keep), "keep");
    }
    hol_owner_set_current(K, prev);
    MUST(hol_ref_move(K, keep, HOL_OWNER_KERNEL), "move keep");
    hol_stats(K, &s1);
    CHECK(s1.terms > s0.terms + 1000);
    MUST(hol_owner_release(K, o), "release");
    hol_gc(K);
    hol_stats(K, &s2);
    CHECK(s2.terms < s1.terms);
    CHECK(s2.thms < s1.thms);
    HolRef c;
    CHECK(hol_thm_concl(K, keep, &c) == HOL_OK); /* still valid after collection */
    HolRef envs_thm[4];
    size_t n;
    MUST(hol_env_definitions(K, ENV, envs_thm, 4, &n), "defs");
    CHECK(n >= 1);
}

/* Runs a fixed scenario; returns a digest of every result, or 0 on any
 * status other than OK (statuses are recorded in *bad). */
static uint64_t scenario(HolKernel *k, HolStatus *bad) {
    HolRef env, Ab, Bb, bo, x, y, f, th, th2, r, envd;
    uint64_t acc = 1469598103934665603ULL, d;
    *bad = HOL_OK;
#define S(e) do { HolStatus s_ = (e); if (s_ != HOL_OK) { *bad = s_; return 0; } } while (0)
    S(hol_env_bare(k, &env));
    S(hol_mk_vartype(k, "A", &Ab));
    S(hol_mk_vartype(k, "B", &Bb));
    S(hol_bool_ty(k, &bo));
    S(hol_mk_var(k, "x", Ab, &x));
    S(hol_mk_var(k, "y", Ab, &y));
    HolRef args[2] = {Ab, Ab}, fty;
    S(hol_mk_type(k, env, "fun", args, 2, &fty));
    S(hol_mk_var(k, "f", fty, &f));
    HolRef lamyx, fx;
    S(hol_mk_comb(k, f, x, &fx));
    S(hol_mk_abs(k, y, fx, &lamyx));
    S(hol_REFL(k, lamyx, &th));
    HolRef pr[2] = {y, x};
    S(hol_INST(k, pr, 1, th, &th2));
    S(hol_digest(k, th2, &d));
    acc = acc * 31 + d;
    HolRef xB, lam2, t3, tyl[2] = {Bb, Ab};
    S(hol_mk_var(k, "x", Bb, &xB));
    S(hol_mk_abs(k, x, xB, &lam2));
    S(hol_REFL(k, lam2, &t3));
    S(hol_INST_TYPE(k, tyl, 1, t3, &r));
    S(hol_digest(k, r, &d));
    acc = acc * 31 + d;
    HolRef p, Tv, idp, e1, e2, dth;
    S(hol_mk_var(k, "p", bo, &p));
    S(hol_mk_var(k, "T", bo, &Tv));
    S(hol_mk_abs(k, p, p, &idp));
    S(hol_mk_eq(k, idp, idp, &e1));
    S(hol_mk_eq(k, Tv, e1, &e2));
    S(hol_new_basic_definition(k, env, e2, &envd, &dth));
    S(hol_digest(k, dth, &d));
    acc = acc * 31 + d;
    HolRef Tc, w, envt, a1, a2;
    S(hol_mk_const(k, envd, "T", NULL, 0, &Tc));
    S(hol_REFL(k, Tc, &w));
    S(hol_new_basic_type_definition(k, envd, "u", "mk_u", "dest_u", w, &envt, &a1, &a2));
    S(hol_digest(k, a2, &d));
    acc = acc * 31 + d;
#undef S
    return acc;
}

/* ---- regressions from the review pass (docs/VALIDATION.md) ------------- */

/* Theory events over shared DAGs cost the DAG, not its printed tree: the
 * environment digest is structural and computed on demand.  Every call
 * here runs under fuel, so a regression fails instead of hanging. */
static void test_dag_events(void) {
    SECTION("dag-events");
    uint64_t dg[2] = {0, 1}, d;
    for (int round = 0; round < 2; round++) {
        HolKernel *k = hol_kernel_new(NULL);
        hol_set_fuel(k, 1u << 20);
        HolRef env, Bo, ty, e2, e3, p, t, th;
        hol_env_bare(k, &env);
        hol_bool_ty(k, &Bo);
        ty = Bo;
        for (int i = 0; i < 64; i++) { /* a type with 2^64 leaves */
            HolRef a2[2] = {ty, ty};
            MUST(hol_mk_type(k, env, "fun", a2, 2, &ty), "dag type");
        }
        CHECK(hol_new_constant(k, env, "c", ty, &e2) == HOL_OK);
        MUST(hol_mk_var(k, "p", Bo, &p), "p");
        t = p;
        for (int i = 0; i < 64; i++) { /* t = (t = t), nested */
            HolRef e;
            MUST(hol_mk_eq(k, t, t, &e), "dag eq");
            t = e;
        }
        CHECK(hol_new_axiom(k, e2, t, &e3, &th) == HOL_OK);
        CHECK(hol_env_digest(k, e3, &dg[round]) == HOL_OK);
        if (round == 0) {
            /* printing repeats shared sub-terms, and observes fuel */
            HolRef t20 = p, t26;
            for (int i = 0; i < 20; i++) MUST(hol_mk_eq(k, t20, t20, &t20), "t20");
            t26 = t20;
            for (int i = 20; i < 26; i++) MUST(hol_mk_eq(k, t26, t26, &t26), "t26");
            hol_set_fuel(k, 1u << 16);
            size_t len;
            CHECK(hol_digest(k, t26, &d) == HOL_INTERRUPTED && hol_last_reason(k) == HOL_R_FUEL);
            CHECK(hol_print(k, t20, NULL, 0, &len) == HOL_INTERRUPTED);
        }
        hol_kernel_free(k);
    }
    CHECK(dg[0] == dg[1]); /* the same history in two kernels */
}

/* The default depth limit fits an ordinary 8 MiB thread stack: each
 * recursive operation runs on objects as deep as the default admits. */
#if defined(__SANITIZE_ADDRESS__)
#define TEST_STACK_SCALE 8 /* sanitizer frames are larger */
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_STACK_SCALE 8
#endif
#endif
#ifndef TEST_STACK_SCALE
#define TEST_STACK_SCALE 1
#endif

static void *default_depth_thread(void *arg) {
    bool *ok = (bool *)arg;
    HolKernel *k = hol_kernel_new(NULL);
    HolRef env, Ab, Bb, x, y, f, fty, th, out, buf[4];
    hol_env_bare(k, &env);
    hol_mk_vartype(k, "A", &Ab);
    hol_mk_vartype(k, "B", &Bb);
    hol_mk_var(k, "x", Ab, &x);
    hol_mk_var(k, "y", Ab, &y);
    HolRef args[2] = {Ab, Ab};
    hol_mk_type(k, env, "fun", args, 2, &fty);
    hol_mk_var(k, "f", fty, &f);
    /* f (f (... x)) up to the limit; ring[j & 3] holds the j-th */
    HolRef ring[4] = {x, x, x, x};
    unsigned built = 0;
    HolStatus st;
    for (;;) {
        HolRef n;
        if ((st = hol_mk_comb(k, f, ring[built & 3], &n)) != HOL_OK) break;
        ring[++built & 3] = n;
    }
    HolRef t = ring[(built - 3) & 3]; /* three levels below the limit: REFL adds two */
    size_t n;
    int c;
    bool b;
    uint64_t dg;
    HolRef pr[2] = {y, x}, tp[2] = {Bb, Ab};
    ok[0] = st == HOL_LIMIT && built > 1000;
    ok[1] = hol_REFL(k, t, &th) == HOL_OK;
    ok[2] = ok[1] && hol_INST(k, pr, 1, th, &out) == HOL_OK;
    ok[3] = ok[1] && hol_INST_TYPE(k, tp, 1, th, &out) == HOL_OK;
    ok[4] = hol_frees(k, t, buf, 4, &n) == HOL_OK && n == 2;
    ok[5] = hol_type_vars_in_term(k, t, buf, 4, &n) == HOL_OK && n == 1;
    ok[6] = hol_alphaorder(k, t, t, &c) == HOL_OK && c == 0;
    ok[7] = ok[1] && hol_digest(k, th, &dg) == HOL_OK;
    ok[8] = hol_vfree_in(k, x, t, &b) == HOL_OK && b;
    /* fun A (fun A (... A)) up to the limit, then a theorem about a variable
     * of a type six levels below it (a term's height includes its type's,
     * and z = z adds the type of = and two combinations) */
    HolRef tys[8] = {Ab, Ab, Ab, Ab, Ab, Ab, Ab, Ab};
    built = 0;
    for (;;) {
        HolRef a2[2] = {Ab, tys[built & 7]}, nt;
        if ((st = hol_mk_type(k, env, "fun", a2, 2, &nt)) != HOL_OK) break;
        tys[++built & 7] = nt;
    }
    HolRef deep = tys[(built - 6) & 7], z, rz;
    ok[9] = st == HOL_LIMIT && built > 1000;
    ok[10] = hol_tyvars(k, deep, buf, 4, &n) == HOL_OK && n == 1;
    ok[11] = hol_mk_var(k, "z", deep, &z) == HOL_OK && hol_REFL(k, z, &rz) == HOL_OK &&
             hol_INST_TYPE(k, tp, 1, rz, &out) == HOL_OK;
    hol_kernel_free(k);
    return NULL;
}

static void test_default_depth_stack(void) {
    SECTION("default-depth-stack");
    bool ok[12] = {false};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, ((size_t)8 * TEST_STACK_SCALE) << 20);
    pthread_t th;
    if (pthread_create(&th, &attr, default_depth_thread, ok) == 0) pthread_join(th, NULL);
    else CHECK(!"thread");
    pthread_attr_destroy(&attr);
    for (int i = 0; i < 12; i++) CHECK(ok[i]);
}

/* fusion.ml compares only leaves in vfree_in: a combination is not free in
 * anything, itself included, and variant returns it unchanged. */
static void test_vfree_nonleaf(void) {
    SECTION("vfree-nonleaf");
    HolRef f = var("f", fn(A, A)), x = var("x", A), fx = comb(f, x), out, c;
    bool b = true;
    CHECK(hol_vfree_in(K, fx, fx, &b) == HOL_OK && !b);
    CHECK(hol_vfree_in(K, x, fx, &b) == HOL_OK && b);
    CHECK(hol_variant(K, &fx, 1, fx, &out) == HOL_OK && same(out, fx));
    c = eq_const(A);
    CHECK(hol_vfree_in(K, c, c, &b) == HOL_OK && b); /* leaves compare equal */
    CHECK(hol_variant(K, &c, 1, c, &out) == HOL_REFUSED && reason() == HOL_R_VARIANT_NOT_VAR);
}

/* A call that fails part way through filling a buffer of references
 * leaves none of them live. */
static void test_failed_vectors(void) {
    SECTION("failed-vectors");
    HolKernel *k = hol_kernel_new(NULL);
    HolRef env, a, b2, c2, ab[2], abc[2], ty, buf[8];
    hol_env_bare(k, &env);
    hol_mk_vartype(k, "A", &a);
    hol_mk_vartype(k, "B", &b2);
    hol_mk_vartype(k, "C", &c2);
    ab[0] = a;
    ab[1] = b2;
    hol_mk_type(k, env, "fun", ab, 2, &abc[0]);
    abc[1] = c2;
    hol_mk_type(k, env, "fun", abc, 2, &ty); /* (A -> B) -> C */
    /* each round: no free slot and room for exactly one more reference, so
     * the second of the three results must grow the slot array; the n-th
     * allocation of the call fails */
    unsigned failed = 0, clean = 0, between = 0;
    for (uint64_t n = 1; n <= 24; n++) {
        HolRef spare;
        while (k->free_slot || k->nslots != k->capslots - 1) hol_ref_dup(k, a, &spare);
        HolStats s0, s1;
        hol_stats(k, &s0);
        hol_test_fail_allocation(k, n, false);
        size_t cnt = 0;
        HolStatus st = hol_tyvars(k, ty, buf, 8, &cnt);
        hol_test_fail_allocation(k, 0, false);
        hol_stats(k, &s1);
        if (st == HOL_NOMEM) {
            failed++;
            clean += s1.refs == s0.refs;
            between += k->nslots == k->capslots; /* the first result was made, then dropped */
        } else if (st == HOL_OK) {
            for (size_t i = 0; i < cnt; i++) hol_ref_release(k, buf[i]);
        }
    }
    CHECK(between > 0 && clean == failed);
    hol_kernel_free(k);
}

/* Kernel tags are unique among live kernels, also after the 16-bit tag
 * counter wraps: a live kernel's reference is refused by every other. */
static void test_kernel_tags(void) {
    SECTION("kernel-tags");
    HolKernel *a = hol_kernel_new(NULL);
    HolRef ta, out;
    hol_mk_vartype(a, "A", &ta);
    unsigned accepted = 0, made = 0;
    for (unsigned i = 0; i < 70000; i++) {
        HolKernel *b = hol_kernel_new(NULL);
        if (!b) break;
        made++;
        if (hol_mk_var(b, "x", ta, &out) != HOL_FOREIGN) accepted++;
        hol_kernel_free(b);
    }
    CHECK(made == 70000 && accepted == 0);
    hol_kernel_free(a);
}

/* equals_thm compares sequents; payload identity also compares the axiom
 * dependencies and the anchor (the join of everything a step consumed). */
static void test_equals_thm(void) {
    SECTION("equals-thm");
    HolRef p = var("p", BOOL), r1, r2, e2, as;
    MUST(hol_REFL(K, p, &r1), "refl");
    MUST(hol_new_type(K, ENV, "foo", 0, &e2), "foo");
    HolRef save = ENV;
    ENV = e2;
    HolRef foo = tyapp("foo", NULL, 0);
    ENV = save;
    HolRef pr[2] = {foo, A};
    MUST(hol_INST_TYPE(K, pr, 1, r1, &r2), "inst_type"); /* A does not occur */
    bool eq = false, same_payload = true, adm = true;
    CHECK(hol_thm_equal(K, r1, r2, &eq) == HOL_OK && eq);
    CHECK(hol_thm_same_payload(K, r1, r2, &same_payload) == HOL_OK && !same_payload);
    CHECK(hol_env_admits(K, ENV, r2, &adm) == HOL_OK && !adm); /* the anchor joins foo */
    MUST(hol_ASSUME(K, p, &as), "assume");
    CHECK(hol_thm_equal(K, r1, as, &eq) == HOL_OK && !eq);
    /* the same conclusion and number of hypotheses, other hypotheses:
     * {r = (p = q), r} |- p = q  and  {(p = q) = (p = q), p = q} |- p = q */
    HolRef q = var("q", BOOL), r = var("r", BOOL), pq = eqt(p, q), y1, y2, y3, x1, x2, x3;
    MUST(hol_ASSUME(K, eqt(r, pq), &y1), "y1");
    MUST(hol_ASSUME(K, r, &y2), "y2");
    MUST(hol_EQ_MP(K, y1, y2, &y3), "y3");
    MUST(hol_ASSUME(K, eqt(pq, pq), &x1), "x1");
    MUST(hol_ASSUME(K, pq, &x2), "x2");
    MUST(hol_EQ_MP(K, x1, x2, &x3), "x3");
    CHECK(nhyps(y3) == 2 && nhyps(x3) == 2 && same(concl(y3), concl(x3)));
    CHECK(hol_thm_equal(K, y3, x3, &eq) == HOL_OK && !eq);
}

static void test_fault_injection(void) {
    SECTION("fault-injection");
    HolKernel *k = hol_kernel_new(NULL);
    HolStatus bad;
    uint64_t base_allocs = hol_test_allocations(k);
    uint64_t expected = scenario(k, &bad);
    CHECK(expected != 0 && bad == HOL_OK);
    uint64_t total = hol_test_allocations(k) - base_allocs;
    hol_kernel_free(k);
    unsigned nomem = 0, ok = 0;
    for (uint64_t n = 1; n <= total + 5; n++) {
        k = hol_kernel_new(NULL);
        hol_test_fail_allocation(k, n, false);
        uint64_t got = scenario(k, &bad);
        if (got == 0) {
            CHECK(bad == HOL_NOMEM); /* never a refusal, never a crash */
            nomem++;
            /* continued use after the failure: the same scenario on a
             * fresh branch of the same kernel gives the reference result */
            hol_test_fail_allocation(k, 0, false);
            uint64_t again = scenario(k, &bad);
            CHECK(again == expected);
        } else {
            CHECK(got == expected);
            ok++;
        }
        hol_kernel_free(k);
    }
    CHECK(nomem > 50);
    printf("  fault injection: %u allocation sites failed cleanly, %u runs past the last site\n", nomem, ok);
}

static void test_collisions(void) {
    SECTION("collisions");
    HolKernel *k = hol_kernel_new(NULL);
    HolStatus bad;
    uint64_t expected = scenario(k, &bad);
    hol_kernel_free(k);
    HolConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.use_hash_mask = true;
    cfg.hash_mask = 0; /* every structural hash collides */
    k = hol_kernel_new(&cfg);
    uint64_t got = scenario(k, &bad);
    CHECK(got == expected && bad == HOL_OK);
    /* distinct objects stay distinct under total collision */
    HolRef a, b, va, vb;
    hol_mk_vartype(k, "A", &a);
    hol_mk_vartype(k, "B", &b);
    hol_mk_var(k, "x", a, &va);
    hol_mk_var(k, "x", b, &vb);
    int c;
    CHECK(hol_compare_terms(k, va, vb, &c) == HOL_OK && c != 0);
    HolStats st;
    hol_stats(k, &st);
    CHECK(st.terms >= 2);
    hol_kernel_free(k);
    cfg.hash_mask = 3;
    k = hol_kernel_new(&cfg);
    CHECK(scenario(k, &bad) == expected);
    hol_kernel_free(k);
}

static bool host_interrupt(void *context) {
    return *(bool *)context;
}

static void test_fuel_interrupt(void) {
    SECTION("fuel-interrupt");
    HolKernel *k = hol_kernel_new(NULL);
    HolRef env, Ab, x, f, t, th;
    hol_env_bare(k, &env);
    hol_mk_vartype(k, "A", &Ab);
    hol_mk_var(k, "x", Ab, &x);
    HolRef args[2] = {Ab, Ab}, fty;
    hol_mk_type(k, env, "fun", args, 2, &fty);
    hol_mk_var(k, "f", fty, &f);
    t = x;
    for (int i = 0; i < 3000; i++) hol_mk_comb(k, f, t, &t); /* f (f ... x) */
    HolRef y, pr[2];
    hol_mk_var(k, "y", Ab, &y);
    pr[0] = y;
    pr[1] = x;
    hol_REFL(k, t, &th);
    HolStats s0, s1;
    hol_stats(k, &s0);
    hol_set_fuel(k, 1500);
    HolRef out;
    CHECK(hol_INST(k, pr, 1, th, &out) == HOL_INTERRUPTED && hol_last_reason(k) == HOL_R_FUEL);
    hol_stats(k, &s1);
    CHECK(s1.refs == s0.refs); /* nothing published */
    hol_set_fuel(k, 0);
    CHECK(hol_INST(k, pr, 1, th, &out) == HOL_OK);
    volatile int flag = 1;
    hol_set_interrupt(k, &flag);
    CHECK(hol_vsubst(k, pr, 1, t, &out) == HOL_INTERRUPTED && hol_last_reason(k) == HOL_R_INTERRUPT);
    flag = 0;
    CHECK(hol_vsubst(k, pr, 1, t, &out) == HOL_OK);
    bool stop = true;
    hol_set_interrupt_poll(k, host_interrupt, &stop);
    hol_stats(k, &s0);
    CHECK(hol_vsubst(k, pr, 1, t, &out) == HOL_INTERRUPTED && hol_last_reason(k) == HOL_R_INTERRUPT);
    hol_stats(k, &s1);
    CHECK(s1.refs == s0.refs);
    stop = false;
    CHECK(hol_vsubst(k, pr, 1, t, &out) == HOL_OK);
    hol_set_interrupt_poll(k, NULL, NULL);
    hol_kernel_free(k);
}

static void *deep_thread(void *arg) {
    (void)arg;
    HolConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.max_depth = 200000;
    HolKernel *k = hol_kernel_new(&cfg);
    HolRef env, Ab, x, f, t, th, y;
    hol_env_bare(k, &env);
    hol_mk_vartype(k, "A", &Ab);
    hol_mk_var(k, "x", Ab, &x);
    hol_mk_var(k, "y", Ab, &y);
    HolRef args[2] = {Ab, Ab}, fty;
    hol_mk_type(k, env, "fun", args, 2, &fty);
    hol_mk_var(k, "f", fty, &f);
    t = x;
    for (int i = 0; i < 100000; i++) {
        HolRef n;
        if (hol_mk_comb(k, f, t, &n) != HOL_OK) break;
        t = n;
    }
    CHECK(hol_REFL(k, t, &th) == HOL_OK);
    HolRef pr[2] = {y, x}, out;
    CHECK(hol_INST(k, pr, 1, th, &out) == HOL_OK);
    int c;
    CHECK(hol_alphaorder(k, t, t, &c) == HOL_OK && c == 0);
    hol_kernel_free(k);
    /* a small limit is reported as LIMIT, not as a refusal */
    cfg.max_depth = 50;
    k = hol_kernel_new(&cfg);
    hol_env_bare(k, &env);
    hol_mk_vartype(k, "A", &Ab);
    hol_mk_var(k, "x", Ab, &x);
    hol_mk_type(k, env, "fun", args, 2, &fty);
    HolRef args2[2] = {Ab, Ab};
    hol_mk_type(k, env, "fun", args2, 2, &fty);
    hol_mk_var(k, "f", fty, &f);
    t = x;
    HolStatus last = HOL_OK;
    for (int i = 0; i < 100 && last == HOL_OK; i++) {
        HolRef n;
        last = hol_mk_comb(k, f, t, &n);
        if (last == HOL_OK) t = n;
    }
    CHECK(last == HOL_LIMIT && hol_last_reason(k) == HOL_R_DEPTH);
    hol_kernel_free(k);
    return NULL;
}

static void test_deep(void) {
    SECTION("deep");
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)512 << 20);
    pthread_t th;
    if (pthread_create(&th, &attr, deep_thread, NULL) == 0) pthread_join(th, NULL);
    else CHECK(!"thread");
    pthread_attr_destroy(&attr);
}

static void test_export_roundtrip(void) {
    SECTION("export");
    /* runs on a fresh kernel (see main) */
    hol_journal_enable(K, true);
    HolRef T;
    HolRef w = truth_witness(&T);
    HolRef envd, t1, t2;
    MUST(hol_new_basic_type_definition(K, ENV, "one", "mk_one", "dest_one", w, &envd, &t1, &t2), "typedef");
    ENV = envd;
    HolRef x = var("x", A), y = var("y", A), th, r;
    MUST(hol_REFL(K, lam(y, x), &th), "refl");
    HolRef pr[2] = {y, x};
    MUST(hol_INST(K, pr, 1, th, &r), "inst");
    HolRef tt;
    MUST(hol_TRANS(K, r, r, &tt), "trans same premise twice");
    HolRef roots[4] = {tt, t2, tt, w};
    uint8_t *bytes = NULL;
    size_t len = 0;
    MUST(hol_export(K, roots, 4, &bytes, &len), "export");
    /* replay in a fresh kernel */
    HolKernel *k2 = hol_kernel_new(NULL);
    HolReplay *rp = hol_replay_new(k2, NULL, HOL_AXIOMS_OPEN);
    hol_replay_feed(rp, bytes, len, true);
    HolReplayState st = hol_replay_resume(rp, 0);
    CHECK(st == HOL_REPLAY_ACCEPTED);
    HolReplayInfo info;
    hol_replay_info(rp, &info);
    CHECK(info.emitted == 4 && info.expectations_checked == 4);
    HolRef d;
    uint64_t ord, ev0 = 0, ev2 = 0;
    unsigned delivered = 0;
    while (hol_replay_next(rp, &d, &ord) == HOL_OK) {
        if (ord == 0) hol_thm_event(k2, d, &ev0);
        if (ord == 2) hol_thm_event(k2, d, &ev2);
        delivered++;
    }
    CHECK(delivered == 4); /* duplicate roots are delivered twice */
    CHECK(ev0 != ev2);     /* as separate deliveries */
    hol_replay_free(rp);
    hol_kernel_free(k2);
    /* an export that the receiver checks with a mutated expectation fails */
    uint8_t *mut = (uint8_t *)malloc(len);
    memcpy(mut, bytes, len);
    /* corrupt one byte inside the body: the checksum catches it */
    mut[len / 2] ^= 0x01;
    k2 = hol_kernel_new(NULL);
    rp = hol_replay_new(k2, NULL, HOL_AXIOMS_OPEN);
    hol_replay_feed(rp, mut, len, true);
    CHECK(hol_replay_resume(rp, 0) != HOL_REPLAY_ACCEPTED);
    hol_replay_free(rp);
    hol_kernel_free(k2);
    free(mut);
    free(bytes);
    /* branch mixing is refused by the exporter */
    HolRef base;
    MUST(hol_env_bare(K, &base), "base");
    HolRef q = var("q", BOOL), e2, ax;
    MUST(hol_new_axiom(K, base, q, &e2, &ax), "ax");
    HolRef mixed[2] = {ax, t2};
    CHECK(hol_export(K, mixed, 2, &bytes, &len) == HOL_FOREIGN);
    /* a theorem made with journaling off cannot be exported */
    hol_journal_enable(K, false);
    HolRef nj;
    MUST(hol_REFL(K, x, &nj), "unjournaled");
    CHECK(hol_export(K, &nj, 1, &bytes, &len) == HOL_REFUSED);
}

/* With journaling on, collection keeps exactly the derivations that live
 * theorems depend on: unrelated temporaries are reclaimed, and the live
 * root still exports and replays. */
static void test_journal_gc(void) {
    SECTION("journal-gc");
    HolKernel *k = hol_kernel_new(NULL);
    HolKernel *save = K;
    HolRef saveenv = ENV, sA = A, sB = B, sC = C, sBOOL = BOOL;
    K = k;
    MUST(hol_env_bare(K, &ENV), "env");
    MUST(hol_bool_ty(K, &BOOL), "bool");
    A = tyv("A");
    hol_journal_enable(K, true);
    HolRef x = var("x", A), y = var("y", A), f = var("f", fn(A, A));
    HolOwner o = hol_owner_new(K);
    HolOwner prev = hol_owner_set_current(K, o);
    HolRef th, keep = 0;
    MUST(hol_REFL(K, comb(f, x), &th), "refl");
    for (int i = 0; i < 200; i++) {
        HolRef pr[2] = {y, x}, t2, t3;
        MUST(hol_INST(K, pr, 1, th, &t2), "inst");
        MUST(hol_TRANS(K, t2, t2, &t3), "trans");
        if (i == 100) MUST(hol_ref_dup(K, t3, &keep), "keep");
        HolRef junk;
        char nm[32];
        snprintf(nm, sizeof nm, "junk%d", i);
        MUST(hol_REFL(K, var(nm, A), &junk), "junk");
    }
    hol_owner_set_current(K, prev);
    MUST(hol_ref_move(K, keep, HOL_OWNER_KERNEL), "move");
    MUST(hol_owner_release(K, o), "release");
    HolStats s0, s1;
    hol_stats(K, &s0);
    hol_gc(K);
    hol_stats(K, &s1);
    CHECK(s1.terms < s0.terms && s1.thms < 10);
    uint8_t *bytes = NULL;
    size_t len = 0;
    CHECK(hol_export(K, &keep, 1, &bytes, &len) == HOL_OK);
    HolKernel *k2 = hol_kernel_new(NULL);
    HolReplay *rp = hol_replay_new(k2, NULL, HOL_AXIOMS_SEALED);
    hol_replay_feed(rp, bytes, len, true);
    CHECK(hol_replay_resume(rp, 0) == HOL_REPLAY_ACCEPTED);
    hol_replay_free(rp);
    hol_kernel_free(k2);
    free(bytes);
    hol_kernel_free(k);
    K = save;
    ENV = saveenv;
    A = sA;
    B = sB;
    C = sC;
    BOOL = sBOOL;
}

int main(void) {
    setup(NULL);
    test_types_terms();
    test_alpha_free();
    test_compare_order();
    test_vsubst_exact();
    test_inst_exact();
    test_rules();
    test_definitions();
    test_type_definitions();
    test_axioms_policy();
    test_handles();
    test_branches();
    test_gc();
    test_vfree_nonleaf();
    test_equals_thm();
    hol_kernel_free(K);
    test_dag_events();
    test_default_depth_stack();
    test_failed_vectors();
    test_kernel_tags();
    test_standard_certification();
    test_fault_injection();
    test_collisions();
    test_fuel_interrupt();
    test_deep();
    setup(NULL);
    test_export_roundtrip();
    test_journal_gc();
    hol_kernel_free(K);
    return test_summary("test-kernel");
}
