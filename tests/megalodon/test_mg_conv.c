/*
 * Lazy conversion (mg_conv) against the reference: for every pair,
 * mg_conv(a, b) must equal (normalize(a) == normalize(b)), where normalize
 * is MathdataKernel.normalize (full delta, then beta-eta).
 *
 * Fixed adversarial cases cover beta and eta at both sorts, defined heads
 * that discard arguments, unfolding chains of different ages, polymorphic
 * definitions, and rigid mismatches.  A seeded property test then compares
 * random well-typed terms with random beta/eta/delta expansions of
 * themselves (convertible by construction) and with unrelated terms.
 */
#include "mg_core.h"
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
    MgTm *m = mg_sx_wire_tm(&T->store, sx(w));
    if (!m) {
        printf("bad term %s\n", w);
        exit(2);
    }
    return m;
}
static MgTp *TP(const char *w) { return mg_sx_wire_tp(&T->store, sx(w)); }
static uint32_t NM(const char *n) { return mg_name_intern(&T->store, n, strlen(n)); }

static void parameter(const char *name, const char *type) {
    mg_theory_associate_parameter(T, NM(name), TP(type));
    MgItem item = {.kind = MG_ITEM_PARAMETER, .name = NM(name), .type = TP(type)};
    MgBudget b;
    mg_budget_init(&b, 0);
    if (mg_theory_admit(T, &b, &item, 0, NULL) != MG_OK) {
        printf("parameter %s refused\n", name);
        exit(2);
    }
}

static void definition(const char *name, const char *type, const char *body) {
    MgItem item = {.kind = MG_ITEM_DEFINITION, .name = NM(name), .type = TP(type),
                   .body = TM(body)};
    MgBudget b;
    mg_budget_init(&b, 0);
    MgStatus st = mg_theory_admit(T, &b, &item, 0, NULL);
    if (st != MG_OK) {
        printf("definition %s refused: %s\n", name, mg_status_name(st));
        exit(2);
    }
}

/* Both verdicts; returns the reference verdict, counts disagreement. */
static int g_disagree;
static bool both(MgTm *a, MgTm *b, const char *label) {
    MgBudget b1, b2;
    mg_budget_init(&b1, 0);
    mg_budget_init(&b2, 0);
    MgCheck c1, c2;
    mg_check_init(&c1, mg_theory_snapshot(T), &b1, NULL);
    mg_check_init(&c2, mg_theory_snapshot(T), &b2, NULL);
    bool lazy = false;
    MgStatus st = mg_conv(&c1, a, b, &lazy);
    MgTm *na = mg_tm_normalize(&c2, a);
    MgTm *nb = na ? mg_tm_normalize(&c2, b) : NULL;
    if (st != MG_OK || !nb) {
        printf("undecided %s\n", label);
        ++g_disagree;
        return false;
    }
    bool reference = na == nb;
    if (lazy != reference) {
        ++g_disagree;
        printf("DISAGREE %s lazy=%d reference=%d\n", label, lazy, reference);
    }
    return reference;
}

#define I "(Tp.base 0)"
#define II "(Tp.arr (Tp.base 0) (Tp.base 0))"
#define III "(Tp.arr (Tp.base 0) (Tp.arr (Tp.base 0) (Tp.base 0)))"
#define IP "(Tp.arr (Tp.base 0) (Tp.prop))"
#define N(x) "(Tm.named \"" x "\")"
#define AP(f, x) "(Tm.app " f " " x ")"

static void theory_setup(void) {
    MgStatus st;
    T = mg_theory_new(MG_PROFILE_CUSTOM, "conv-test", &st);
    parameter("c1", I);
    parameter("c2", I);
    parameter("f", II);
    parameter("g", III);
    parameter("P", IP);
    parameter("Q", IP);
    /* d1 x = f (f x); d2 x y = g y x; d3 x = d1 (d2 x c1);
     * k x y = x (discards y); id x = x; dp x = P (d1 x);
     * dall = forall x. P x; dimp p q = p -> q */
    definition("d1", II, "(Tm.lam " I " " AP(N("f"), AP(N("f"), "(Tm.db 0)")) ")");
    definition("d2", III, "(Tm.lam " I " (Tm.lam " I " " AP(AP(N("g"), "(Tm.db 0)"), "(Tm.db 1)") "))");
    definition("d3", II, "(Tm.lam " I " " AP(N("d1"), AP(AP(N("d2"), "(Tm.db 0)"), N("c1"))) ")");
    definition("k", III, "(Tm.lam " I " (Tm.lam " I " (Tm.db 1)))");
    definition("id", II, "(Tm.lam " I " (Tm.db 0))");
    definition("dp", IP, "(Tm.lam " I " " AP(N("P"), AP(N("d1"), "(Tm.db 0)")) ")");
    definition("dall", "(Tp.prop)", "(Tm.all " I " " AP(N("P"), "(Tm.db 0)") ")");
    definition("dimp", "(Tp.arr (Tp.prop) (Tp.arr (Tp.prop) (Tp.prop)))",
               "(Tm.lam (Tp.prop) (Tm.lam (Tp.prop) (Tm.imp (Tm.db 1) (Tm.db 0))))");
    definition("pid", "(Tp.all (Tp.arr (Tp.var 0) (Tp.var 0)))",
               "(Tm.typeLam (Tm.lam (Tp.var 0) (Tm.db 0)))");
}

static void fixed_cases(void) {
    struct { const char *a, *b; bool equal; const char *label; } cases[] = {
        {AP("(Tm.lam " I " (Tm.db 0))", N("c1")), N("c1"), true, "beta"},
        {"(Tm.lam " I " " AP(N("f"), "(Tm.db 0)") ")", N("f"), true, "eta"},
        {"(Tm.lam " I " " AP(N("d1"), "(Tm.db 0)") ")", N("d1"), true, "eta over a defined head"},
        {AP(N("d1"), N("c1")), AP(N("f"), AP(N("f"), N("c1"))), true, "delta"},
        {AP(AP(N("k"), N("c1")), N("c2")), AP(AP(N("k"), N("c1")), AP(N("f"), N("c2"))), true,
         "defined head discards a differing argument"},
        {AP(AP(N("k"), N("c1")), N("c2")), N("c1"), true, "constant function"},
        {AP(N("d3"), N("c2")), AP(N("f"), AP(N("f"), AP(AP(N("g"), N("c1")), N("c2")))), true,
         "unfolding chain across ages"},
        {AP(N("d3"), N("c2")), AP(N("d1"), AP(AP(N("d2"), N("c2")), N("c1"))), true,
         "newer head unfolded to an older head"},
        {AP(N("f"), N("c1")), AP(N("f"), N("c2")), false, "rigid argument mismatch"},
        {AP(N("P"), N("c1")), AP(N("Q"), N("c1")), false, "rigid head mismatch"},
        {AP(N("d1"), N("c1")), AP(N("f"), N("c1")), false, "delta then mismatch"},
        {N("dall"), "(Tm.all " I " " AP(N("P"), "(Tm.db 0)") ")", true, "defined head against a quantifier"},
        {N("dall"), "(Tm.all " I " " AP(N("Q"), "(Tm.db 0)") ")", false, "quantifier body mismatch"},
        {AP(AP(N("dimp"), N("dall")), N("dall")), "(Tm.imp " N("dall") " " N("dall") ")", true,
         "defined implication"},
        {AP(N("dp"), N("c1")), AP(N("P"), AP(N("f"), AP(N("f"), N("c1")))), true, "predicate unfolding"},
        {AP("(Tm.typeApp " N("pid") " " I ")", N("c1")), N("c1"), true, "polymorphic identity instance"},
        {"(Tm.typeApp (Tm.typeLam (Tm.lam (Tp.var 0) (Tm.db 0))) " I ")",
         "(Tm.lam " I " (Tm.db 0))", true, "type-level beta"},
        {"(Tm.typeLam (Tm.typeApp " N("pid") " (Tp.var 0)))", N("pid"), true, "type-level eta"},
        {"(Tm.lam " I " " AP(AP(N("g"), "(Tm.db 0)"), "(Tm.db 0)") ")", N("f"), false,
         "lambda against a different function"},
        {"(Tm.lam " I " " AP(N("id"), AP(N("f"), "(Tm.db 0)")) ")", N("f"), true,
         "eta after an identity unfolding"},
        {"(Tm.lam " I " " AP(AP(N("d2"), N("c1")), "(Tm.db 0)") ")",
         AP(N("g"), "(Tm.db 0)"), false, "argument order under eta (scoping)"},
        {"(Tm.all " I " " AP(N("dp"), "(Tm.db 0)") ")",
         "(Tm.all " I " " AP(N("P"), AP(N("d1"), "(Tm.db 0)")) ")", true, "under binders"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        bool ref = both(TM(cases[i].a), TM(cases[i].b), cases[i].label);
        EXPECT(ref == cases[i].equal, cases[i].label);
    }
}

/* ── random terms of type iota ────────────────────────────────────────── */

static uint64_t g_rng = 0x2545f4914f6cdd1du;
static uint32_t rnd(uint32_t n) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng % n);
}

/* A random term of type iota with `vars` iota variables in scope. */
static MgTm *rand_iota(uint32_t vars, int depth) {
    MgStore *s = &T->store;
    uint32_t pick = depth <= 0 ? rnd(3) : rnd(9);
    switch (pick) {
    case 0: return mg_tm_name(s, NM("c1"));
    case 1: return mg_tm_name(s, NM("c2"));
    case 2: return vars ? mg_tm_db(s, rnd(vars)) : mg_tm_name(s, NM("c1"));
    case 3: return mg_tm_ap(s, mg_tm_name(s, NM("f")), rand_iota(vars, depth - 1));
    case 4:
        return mg_tm_ap(s, mg_tm_ap(s, mg_tm_name(s, NM("g")), rand_iota(vars, depth - 1)),
                        rand_iota(vars, depth - 1));
    case 5: return mg_tm_ap(s, mg_tm_name(s, NM("d1")), rand_iota(vars, depth - 1));
    case 6:
        return mg_tm_ap(s, mg_tm_ap(s, mg_tm_name(s, NM("d2")), rand_iota(vars, depth - 1)),
                        rand_iota(vars, depth - 1));
    case 7: return mg_tm_ap(s, mg_tm_name(s, NM("d3")), rand_iota(vars, depth - 1));
    default:
        return mg_tm_ap(s, mg_tm_ap(s, mg_tm_name(s, NM("k")), rand_iota(vars, depth - 1)),
                        rand_iota(vars, depth - 1));
    }
}

/* A convertible variant: expand random subterms (beta-expansion through
 * id or an explicit redex, delta-folding of f (f x) into d1 x, eta of the
 * function position). */
static MgTm *expand(MgCheck *c, MgTm *m, uint32_t vars, int budget) {
    MgStore *s = &T->store;
    if (budget <= 0)
        return m;
    MgTm *r = m;
    if (m->tag == MG_TM_AP) {
        MgTm *f = expand(c, m->f, vars, budget - 1);
        MgTm *x = expand(c, m->x, vars, budget - 1);
        r = mg_tm_ap(s, f, x);
    }
    switch (rnd(6)) {
    case 0: /* id r */
        return mg_tm_ap(s, mg_tm_name(s, NM("id")), r);
    case 1: { /* (\y. r^) c  with y unused */
        MgTm *shifted = mg_tm_shift(c, r, 0, 1);
        return mg_tm_ap(s, mg_tm_lam(s, TP(I), shifted), rand_iota(vars, 1));
    }
    case 2: /* k r junk */
        return mg_tm_ap(s, mg_tm_ap(s, mg_tm_name(s, NM("k")), r), rand_iota(vars, 1));
    case 3: /* f (f u) -> d1 u */
        if (r->tag == MG_TM_AP && r->f->tag == MG_TM_NAME && r->f->n == NM("f") &&
            r->x->tag == MG_TM_AP && r->x->f->tag == MG_TM_NAME && r->x->f->n == NM("f"))
            return mg_tm_ap(s, mg_tm_name(s, NM("d1")), r->x->x);
        return r;
    case 4: /* eta-expanded function position */
        if (r->tag == MG_TM_AP && r->f->tag == MG_TM_NAME && r->f->n == NM("f")) {
            MgTm *eta = mg_tm_lam(s, TP(I), mg_tm_ap(s, mg_tm_name(s, NM("f")), mg_tm_db(s, 0)));
            return mg_tm_ap(s, eta, r->x);
        }
        return r;
    default:
        return r;
    }
}

static void property_cases(int count) {
    int equal_pairs = 0, unequal_pairs = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t vars = rnd(3);
        MgTm *a = rand_iota(vars, 4);
        MgBudget b;
        mg_budget_init(&b, 0);
        MgCheck c;
        mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
        MgTm *e = expand(&c, a, vars, 6);
        /* close the variables under lambdas so both are closed terms */
        MgTm *ca = a, *ce = e;
        for (uint32_t v = 0; v < vars; ++v) {
            ca = mg_tm_lam(&T->store, TP(I), ca);
            ce = mg_tm_lam(&T->store, TP(I), ce);
        }
        char label[64];
        snprintf(label, sizeof label, "expansion %d", i);
        if (both(ca, ce, label))
            ++equal_pairs;
        else {
            ++g_fail;
            printf("FAIL expansion %d is not convertible by the reference\n", i);
        }
        MgTm *other = rand_iota(vars, 4);
        MgTm *co = other;
        for (uint32_t v = 0; v < vars; ++v)
            co = mg_tm_lam(&T->store, TP(I), co);
        snprintf(label, sizeof label, "random pair %d", i);
        if (!both(ca, co, label))
            ++unequal_pairs;
    }
    EXPECT(g_disagree == 0, "lazy conversion agrees with the reference on every pair");
    printf("(property pairs: %d convertible, %d not convertible, %d random-equal)\n",
           equal_pairs, unequal_pairs, count - unequal_pairs);
}

int main(int argc, char **argv) {
    int count = argc > 1 ? atoi(argv[1]) : 2000;
    if (argc > 2)
        g_rng = strtoull(argv[2], NULL, 10) | 1u;
    A = mg_sx_arena_new();
    theory_setup();
    fixed_cases();
    property_cases(count);
    mg_theory_release(T);
    mg_sx_arena_free(A);
    printf("(MgConvSummary %d %d disagreements=%d)\n", g_pass, g_fail, g_disagree);
    return g_fail || g_disagree ? 1 : 0;
}
