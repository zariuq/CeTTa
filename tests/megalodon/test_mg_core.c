/*
 * Native Megalodon kernel: core controls.
 *
 * Each case states its independently expected verdict.  Cases named after
 * Lean controls reproduce those theorems' data (MathdataKernel.lean,
 * NativeAdmission.lean Controls, SourceAdmission.lean Controls,
 * RuleObservation.lean Controls, SourceScopeInvariant.lean).
 */
#include "mg_core.h"
#include "mg_hash.h"
#include "mg_profiles.h"
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
static MgTm *TM(const char *w) { return mg_sx_wire_tm(&T->store, sx(w)); }
static MgTp *TP(const char *w) { return mg_sx_wire_tp(&T->store, sx(w)); }
static MgPf *PF(const char *w) { return mg_sx_wire_pf(&T->store, sx(w)); }
static uint32_t NM(const char *n) { return mg_name_intern(&T->store, n, strlen(n)); }

static void fresh_theory(MgProfileTag p) {
    if (T)
        mg_theory_release(T);
    MgStatus st;
    T = mg_theory_new(p, "test", &st);
    if (!T) {
        printf("theory creation failed\n");
        exit(2);
    }
}

static MgStatus admit(MgItemKind kind, const char *name, const char *type,
                      const char *body_or_prop, const char *proof) {
    MgItem item = {0};
    item.kind = kind;
    item.name = name ? NM(name) : 0;
    if (type) item.type = TP(type);
    if (kind == MG_ITEM_DEFINITION && body_or_prop) item.body = TM(body_or_prop);
    else if (body_or_prop) item.prop = TM(body_or_prop);
    if (proof) item.proof = PF(proof);
    MgBudget b;
    mg_budget_init(&b, 0);
    return mg_theory_admit(T, &b, &item, 0, NULL);
}

static MgStatus check_proof(const char *proof, const char *claim, uint64_t budget) {
    MgItem item = {0};
    item.kind = MG_ITEM_THEOREM;
    item.name = NM("__probe__");
    item.prop = TM(claim);
    item.proof = PF(proof);
    MgBudget b;
    mg_budget_init(&b, budget);
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    return mg_item_check(&c, &item);
}

/* ── interning and facts ──────────────────────────────────────────────── */
static void test_interning(void) {
    fresh_theory(MG_PROFILE_CUSTOM);
    MgTm *a = TM("(Tm.app (Tm.named \"p\") (Tm.db 0))");
    MgTm *b = TM("(Tm.app (Tm.named \"p\") (Tm.db 0))");
    EXPECT(a && a == b, "structural interning gives one node");
    EXPECT(a->fv == 1u && mg_tm_free_db(a, 0) && !mg_tm_free_db(a, 1), "free term index facts");
    MgTm *lam = TM("(Tm.lam (Tp.base 0) (Tm.app (Tm.named \"p\") (Tm.db 0)))");
    EXPECT(lam->fv == 0u, "binder closes index 0");
    MgTm *bad = TM("(Tm.lam (Tp.prop) (Tm.typeLam (Tm.db 0)))");
    EXPECT(bad && !mg_tm_protected(bad), "SourceAdmission mixed-binder capture term is not Protected");
    MgTm *ok = TM("(Tm.typeApp (Tm.typeLam (Tm.lam (Tp.var 0) (Tm.db 0))) (Tp.var 0))");
    EXPECT(ok && mg_tm_protected(ok), "expanded_polymorphic_identity_protected");
    EXPECT(!mg_tm_protected(TM("(Tm.typeLam (Tm.db 0))")), "raw_capture_example_excluded");
    MgTp *poly = TP("(Tp.all (Tp.arr (Tp.var 0) (Tp.var 0)))");
    EXPECT(poly && mg_tp_poly_wf(poly, 0) && !mg_tp_plain_wf(poly, 0), "prefix polymorphic type formation");
    EXPECT(!mg_tp_poly_wf(TP("(Tp.arr (Tp.all (Tp.var 0)) (Tp.prop))"), 0), "inner TpAll is not formed");
    EXPECT(!mg_tp_plain_wf(TP("(Tp.var 0)"), 0) && mg_tp_plain_wf(TP("(Tp.var 0)"), 1), "type depth scope");
}

/* ── MathdataKernel.lean calibration ──────────────────────────────────── */
static void parameter(const char *name, const char *type) {
    mg_theory_associate_parameter(T, NM(name), TP(type));
    EXPECT(admit(MG_ITEM_PARAMETER, name, type, NULL, NULL) == MG_OK, name);
}

static void test_mathdata_examples(void) {
    fresh_theory(MG_PROFILE_CUSTOM);
    parameter("p", "(Tp.arr (Tp.base 0) (Tp.prop))");
    const char *dom = "(Tm.all (Tp.base 0) (Tm.app (Tm.named \"p\") (Tm.db 0)))";
    char goal[512], proof[512], wrong[512];
    snprintf(goal, sizeof goal, "(Tm.imp %s %s)", dom, dom);
    snprintf(proof, sizeof proof,
             "(Pf.proofLam %s (Pf.termLam (Tp.base 0) (Pf.termApp (Pf.hyp 0) (Tm.db 0))))", dom);
    EXPECT(check_proof(proof, goal, 0) == MG_OK, "forallIdentityProof accepted");
    snprintf(wrong, sizeof wrong,
             "(Pf.proofLam %s (Pf.termApp (Pf.hyp 0) (Tm.named \"p\")))", dom);
    EXPECT(check_proof(wrong, goal, 0) == MG_REFUSED, "wrongTermApplication refused");

    fresh_theory(MG_PROFILE_CUSTOM);
    parameter("q", "(Tp.prop)");
    EXPECT(check_proof("(Pf.typeLam (Pf.proofLam (Tm.named \"q\") (Pf.hyp 0)))",
                       "(Tm.typeAll (Tm.imp (Tm.named \"q\") (Tm.named \"q\")))", 0) == MG_OK,
           "polymorphicIdentityProof accepted at empty contexts");
    {
        /* Same proof inside a nonempty term context is refused (PTpLam). */
        MgBudget b; mg_budget_init(&b, 0);
        MgCheck c; mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
        MgTpList *terms = mg_tplist_cons(&T->store, mg_tp_prop(&T->store), NULL);
        MgCtx *ctx = mg_ctx(&T->store, 0, terms, NULL);
        MgJudgment j;
        MgStatus st = mg_infer_proof(&c, ctx,
            PF("(Pf.typeLam (Pf.proofLam (Tm.named \"q\") (Pf.hyp 0)))"), &j);
        EXPECT(st == MG_REFUSED, "polymorphic proof refused in a nonempty term context");
    }

    /* polymorphic_known_reuse_accepted / polymorphic_unknown_reuse_rejected */
    fresh_theory(MG_PROFILE_CUSTOM);
    const char *body = "(Tm.all (Tp.arr (Tp.var 0) (Tp.prop)) (Tm.imp "
        "(Tm.all (Tp.var 0) (Tm.app (Tm.db 1) (Tm.db 0))) "
        "(Tm.all (Tp.var 0) (Tm.app (Tm.db 1) (Tm.db 0)))))";
    char pgoal[1024];
    snprintf(pgoal, sizeof pgoal, "(Tm.typeAll %s)", body);
    mg_theory_associate_known(T, NM("reuse"), TM(pgoal));
    EXPECT(admit(MG_ITEM_KNOWN, "reuse", NULL, pgoal, NULL) == MG_OK, "admit polymorphic known");
    EXPECT(check_proof("(Pf.typeLam (Pf.termLam (Tp.arr (Tp.var 0) (Tp.prop)) "
                       "(Pf.termApp (Pf.typeApp (Pf.known \"reuse\") (Tp.var 0)) (Tm.db 0))))",
                       pgoal, 0) == MG_OK, "polymorphic_known_reuse_accepted");
    EXPECT(check_proof("(Pf.typeLam (Pf.termLam (Tp.arr (Tp.var 0) (Tp.prop)) "
                       "(Pf.termApp (Pf.typeApp (Pf.known \"nope\") (Tp.var 0)) (Tm.db 0))))",
                       pgoal, 0) == MG_REFUSED, "polymorphic_unknown_reuse_rejected");

    /* definition_conversion_accepted / opaque_identity_rejected */
    fresh_theory(MG_PROFILE_CUSTOM);
    EXPECT(admit(MG_ITEM_DEFINITION, "idp", "(Tp.arr (Tp.prop) (Tp.prop))",
                 "(Tm.lam (Tp.prop) (Tm.db 0))", NULL) == MG_OK, "admit idp");
    parameter("p", "(Tp.prop)");
    const char *dgoal = "(Tm.imp (Tm.app (Tm.named \"idp\") (Tm.named \"p\")) (Tm.named \"p\"))";
    const char *dproof = "(Pf.proofLam (Tm.app (Tm.named \"idp\") (Tm.named \"p\")) (Pf.hyp 0))";
    EXPECT(check_proof(dproof, dgoal, 0) == MG_OK, "definition_conversion_accepted");
    fresh_theory(MG_PROFILE_CUSTOM);
    parameter("idp", "(Tp.arr (Tp.prop) (Tp.prop))");
    parameter("p", "(Tp.prop)");
    EXPECT(check_proof(dproof, dgoal, 0) == MG_REFUSED, "opaque_identity_rejected");

    /* Ordinary and type-level beta (Tm.normalize examples). */
    fresh_theory(MG_PROFILE_CUSTOM);
    MgBudget b; mg_budget_init(&b, 0);
    MgCheck c; mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    EXPECT(mg_tm_nf(&c, TM("(Tm.app (Tm.lam (Tp.base 0) (Tm.db 0)) (Tm.named \"x\"))")) == TM("(Tm.named \"x\")"),
           "term beta");
    EXPECT(mg_tm_nf(&c, TM("(Tm.typeApp (Tm.typeLam (Tm.all (Tp.var 0) (Tm.db 0))) (Tp.base 0))")) ==
               TM("(Tm.all (Tp.base 0) (Tm.db 0))"), "type beta");
    EXPECT(mg_tm_nf(&c, TM("(Tm.lam (Tp.base 0) (Tm.app (Tm.named \"f\") (Tm.db 0)))")) == TM("(Tm.named \"f\")"),
           "term eta");
    EXPECT(mg_tm_nf(&c, TM("(Tm.lam (Tp.base 0) (Tm.app (Tm.db 0) (Tm.db 0)))")) ==
               TM("(Tm.lam (Tp.base 0) (Tm.app (Tm.db 0) (Tm.db 0)))"), "no eta when the variable occurs");
    EXPECT(mg_tm_nf(&c, TM("(Tm.typeLam (Tm.typeApp (Tm.named \"g\") (Tp.var 0)))")) == TM("(Tm.named \"g\")"),
           "type eta");
    EXPECT(mg_tm_nf(&c, TM("(Tm.typeLam (Tm.typeApp (Tm.typeApp (Tm.named \"g\") (Tp.var 0)) (Tp.var 0)))")) ==
               TM("(Tm.typeLam (Tm.typeApp (Tm.typeApp (Tm.named \"g\") (Tp.var 0)) (Tp.var 0)))"),
           "no type eta when the variable occurs");
    /* Open type arguments in an enclosing type scope: two parameters
     * instantiated in order (OpenTypeParameters). */
    MgTm *pair = TM("(Tm.typeApp (Tm.typeApp (Tm.typeLam (Tm.typeLam "
                    "(Tm.lam (Tp.var 1) (Tm.lam (Tp.var 0) (Tm.db 1))))) (Tp.var 0)) (Tp.base 0))");
    EXPECT(mg_tm_nf(&c, pair) == TM("(Tm.lam (Tp.var 0) (Tm.lam (Tp.base 0) (Tm.db 1)))"),
           "open type argument order and shifting");
    EXPECT(mg_infer(&c, 1, NULL, pair) ==
               TP("(Tp.arr (Tp.var 0) (Tp.arr (Tp.base 0) (Tp.var 0)))"), "open type argument typing");
    EXPECT(mg_infer(&c, 0, NULL, pair) == NULL, "open type argument outside its scope refused");
}

/* ── the eight rules, LCF route ───────────────────────────────────────── */
static MgCtx *ctx_of(uint32_t depth, const char *const *types, uint32_t nt,
                     const char *const *hyps, uint32_t nh) {
    MgTpList *tl = NULL;
    for (uint32_t i = nt; i > 0; --i)
        tl = mg_tplist_cons(&T->store, TP(types[i - 1u]), tl);
    MgTmList *hl = NULL;
    for (uint32_t i = nh; i > 0; --i)
        hl = mg_tmlist_cons(&T->store, TM(hyps[i - 1u]), hl);
    return mg_ctx(&T->store, depth, tl, hl);
}

static MgStatus step(MgWitness *w, MgCtx *ctx, MgJudgment *p, uint32_t n, MgJudgment *out,
                     uint64_t budget) {
    MgBudget b; mg_budget_init(&b, budget);
    MgCheck c; mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    return mg_step(&c, w, ctx, p, n, out);
}

static void test_rules(void) {
    fresh_theory(MG_PROFILE_CUSTOM);
    parameter("P", "(Tp.prop)");
    parameter("Q", "(Tp.prop)");
    parameter("R", "(Tp.arr (Tp.base 0) (Tp.prop))");
    parameter("a", "(Tp.base 0)");
    mg_theory_associate_known(T, NM("ax"), TM("(Tm.imp (Tm.named \"P\") (Tm.named \"Q\"))"));
    EXPECT(admit(MG_ITEM_KNOWN, "ax", NULL, "(Tm.imp (Tm.named \"P\") (Tm.named \"Q\"))", NULL) == MG_OK,
           "admit selected axiom");
    MgWitness w;
    MgJudgment j, j2, out;

    /* hyp */
    const char *hp[] = {"(Tm.named \"P\")"};
    MgCtx *cp = ctx_of(0, NULL, 0, hp, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    EXPECT(step(&w, cp, NULL, 0, &out, 0) == MG_OK && out.prop == TM("(Tm.named \"P\")"), "hyp accepts");
    w.index = 1;
    EXPECT(step(&w, cp, NULL, 0, &out, 0) == MG_REFUSED, "hyp out of range refused");

    /* known */
    w = (MgWitness){.rule = MG_RULE_KNOWN, .name = NM("ax")};
    EXPECT(step(&w, cp, NULL, 0, &j, 0) == MG_OK &&
           j.prop == TM("(Tm.imp (Tm.named \"P\") (Tm.named \"Q\"))") && j.ctx == cp, "known in an open context");
    w.name = NM("missing");
    EXPECT(step(&w, cp, NULL, 0, &out, 0) == MG_REFUSED, "absent known refused");

    /* proofApp: ax : P -> Q, hyp : P  ==> Q ; repeated premise occurrences */
    w = (MgWitness){.rule = MG_RULE_KNOWN, .name = NM("ax")};
    step(&w, cp, NULL, 0, &j, 0);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, cp, NULL, 0, &j2, 0);
    MgJudgment prem[2] = {j, j2};
    w = (MgWitness){.rule = MG_RULE_PROOF_APP};
    EXPECT(step(&w, NULL, prem, 2, &out, 0) == MG_OK && out.prop == TM("(Tm.named \"Q\")"), "proofApp accepts");
    MgJudgment swapped[2] = {j2, j};
    w = (MgWitness){.rule = MG_RULE_PROOF_APP};
    EXPECT(step(&w, NULL, swapped, 2, &out, 0) == MG_REFUSED, "proofApp with swapped premises refused");
    MgCtx *cq = ctx_of(0, NULL, 0, (const char *[]){"(Tm.named \"Q\")"}, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    MgJudgment jq;
    step(&w, cq, NULL, 0, &jq, 0);
    MgJudgment mixed[2] = {j, jq};
    w = (MgWitness){.rule = MG_RULE_PROOF_APP};
    EXPECT(step(&w, NULL, mixed, 2, &out, 0) == MG_REFUSED, "proofApp across different contexts refused");
    /* (P -> P) applied to two occurrences of the same premise judgment */
    MgCtx *cpp = ctx_of(0, NULL, 0, (const char *[]){"(Tm.imp (Tm.named \"P\") (Tm.named \"P\"))",
                                                    "(Tm.named \"P\")"}, 2);
    MgJudgment f1, a1;
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, cpp, NULL, 0, &f1, 0);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 1};
    step(&w, cpp, NULL, 0, &a1, 0);
    MgJudgment fa[2] = {f1, a1};
    w = (MgWitness){.rule = MG_RULE_PROOF_APP};
    EXPECT(step(&w, NULL, fa, 2, &out, 0) == MG_OK && out.prop == TM("(Tm.named \"P\")"),
           "proofApp with an implication from P to P");

    /* termApp */
    const char *hall[] = {"(Tm.all (Tp.base 0) (Tm.app (Tm.named \"R\") (Tm.db 0)))"};
    MgCtx *ca = ctx_of(0, NULL, 0, hall, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, ca, NULL, 0, &j, 0);
    w = (MgWitness){.rule = MG_RULE_TERM_APP, .arg = TM("(Tm.named \"a\")")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_OK &&
           out.prop == TM("(Tm.app (Tm.named \"R\") (Tm.named \"a\"))"), "termApp accepts");
    w = (MgWitness){.rule = MG_RULE_TERM_APP, .arg = TM("(Tm.named \"P\")")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED, "termApp argument of the wrong type refused");
    w = (MgWitness){.rule = MG_RULE_TERM_APP,
                    .arg = TM("(Tm.app (Tm.lam (Tp.prop) (Tm.typeLam (Tm.db 0))) (Tm.named \"P\"))")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_MALFORMED, "hidden mixed-binder operand refused");

    /* termLam: generalize over a variable; eigenvariable condition */
    const char *ty[] = {"(Tp.base 0)"};
    const char *hdep[] = {"(Tm.app (Tm.named \"R\") (Tm.db 0))"};
    MgCtx *cv = ctx_of(0, ty, 1, hdep, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, cv, NULL, 0, &j, 0);
    w = (MgWitness){.rule = MG_RULE_TERM_LAM, .type = TP("(Tp.base 0)")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED,
           "termLam refused when a hypothesis mentions the variable");
    const char *hfree[] = {"(Tm.named \"P\")"};
    MgCtx *cv2 = ctx_of(0, ty, 1, hfree, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, cv2, NULL, 0, &j, 0);
    w = (MgWitness){.rule = MG_RULE_TERM_LAM, .type = TP("(Tp.base 0)")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_OK && out.prop == TM("(Tm.all (Tp.base 0) (Tm.named \"P\"))") &&
           out.ctx == ctx_of(0, NULL, 0, hfree, 1), "termLam accepts and unshifts the context");
    w = (MgWitness){.rule = MG_RULE_TERM_LAM, .type = TP("(Tp.prop)")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED, "termLam with the wrong binder type refused");

    /* proofLam */
    MgCtx *cpl = ctx_of(0, NULL, 0, (const char *[]){"(Tm.named \"P\")"}, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, cpl, NULL, 0, &j, 0);
    w = (MgWitness){.rule = MG_RULE_PROOF_LAM, .source = TM("(Tm.named \"P\")")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_OK && out.ctx == T->store.ctx_empty &&
           out.prop == TM("(Tm.imp (Tm.named \"P\") (Tm.named \"P\"))"), "proofLam accepts");
    w = (MgWitness){.rule = MG_RULE_PROOF_LAM, .source = TM("(Tm.named \"Q\")")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED, "proofLam with another source refused");
    w = (MgWitness){.rule = MG_RULE_PROOF_LAM, .source = TM("(Tm.named \"a\")")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED, "proofLam with a non-proposition refused");

    /* typeApp / typeLam */
    MgCtx *c1 = mg_ctx(&T->store, 1, NULL, NULL);
    MgCtx *c1h = ctx_of(1, NULL, 0, (const char *[]){"(Tm.named \"P\")"}, 1);
    w = (MgWitness){.rule = MG_RULE_HYP, .index = 0};
    step(&w, c1h, NULL, 0, &j, 0);
    w = (MgWitness){.rule = MG_RULE_PROOF_LAM, .source = TM("(Tm.named \"P\")")};
    step(&w, NULL, &j, 1, &j2, 0);
    EXPECT(j2.ctx == c1, "proofLam discharges into a type context");
    w = (MgWitness){.rule = MG_RULE_TYPE_LAM};
    EXPECT(step(&w, NULL, &j2, 1, &out, 0) == MG_OK && out.ctx == T->store.ctx_empty &&
           out.prop == TM("(Tm.typeAll (Tm.imp (Tm.named \"P\") (Tm.named \"P\")))"), "typeLam accepts");
    MgJudgment poly = out;
    w = (MgWitness){.rule = MG_RULE_TYPE_LAM};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED, "typeLam with a hypothesis refused");
    w = (MgWitness){.rule = MG_RULE_TYPE_APP, .type = TP("(Tp.base 0)")};
    EXPECT(step(&w, NULL, &poly, 1, &out, 0) == MG_OK &&
           out.prop == TM("(Tm.imp (Tm.named \"P\") (Tm.named \"P\"))"), "typeApp accepts");
    w = (MgWitness){.rule = MG_RULE_TYPE_APP, .type = TP("(Tp.base 0)")};
    EXPECT(step(&w, NULL, &j, 1, &out, 0) == MG_REFUSED, "typeApp on a monomorphic premise refused");
}

/* ── admission controls ───────────────────────────────────────────────── */
static void test_admission(void) {
    fresh_theory(MG_PROFILE_MIZAR);
    mg_theory_associate_parameter(T, NM("p"), TP("(Tp.prop)"));
    mg_theory_associate_known(T, NM("axiom"), TM("(Tm.named \"p\")"));
    EXPECT(admit(MG_ITEM_PARAMETER, "unknown", "(Tp.prop)", NULL, NULL) == MG_REFUSED,
           "parameter_without_import_authority_refused");
    EXPECT(admit(MG_ITEM_PARAMETER, "p", "(Tp.prop)", NULL, NULL) == MG_OK, "imported_parameter_accepted");
    EXPECT(admit(MG_ITEM_KNOWN, "forged", NULL, "(Tm.named \"p\")", NULL) == MG_REFUSED,
           "axiom_text_does_not_grant_authority");
    EXPECT(admit(MG_ITEM_CONJECTURE, NULL, NULL, "(Tm.named \"p\")", NULL) == MG_OK &&
           T->rev == 1u, "conjecture_does_not_add_knowledge");
    EXPECT(admit(MG_ITEM_DEFINITION, "p", "(Tp.prop)", "(Tm.named \"p\")", NULL) == MG_REFUSED,
           "shadowing_definition_refused");
    EXPECT(admit(MG_ITEM_DEFINITION, "q", "(Tp.prop)", "(Tm.named \"q\")", NULL) == MG_REFUSED,
           "self_definition_refused");
    EXPECT(admit(MG_ITEM_DEFINITION, "q", "(Tp.prop)", "(Tm.named \"p\")", NULL) == MG_OK,
           "older_alias_accepted");
    EXPECT(admit(MG_ITEM_KNOWN, "axiom", NULL, "(Tm.named \"p\")", NULL) == MG_OK, "selected_known_accepted");
    EXPECT(admit(MG_ITEM_KNOWN, "axiom", NULL, "(Tm.named \"p\")", NULL) == MG_REFUSED, "known names are fresh");

    fresh_theory(MG_PROFILE_MIZAR);
    EXPECT(admit(MG_ITEM_DEFINITION, "id", "(Tp.all (Tp.arr (Tp.var 0) (Tp.var 0)))",
                 "(Tm.typeLam (Tm.lam (Tp.var 0) (Tm.db 0)))", NULL) == MG_OK,
           "polymorphic_identity_admitted");
    EXPECT(admit(MG_ITEM_DEFINITION, "capture", "(Tp.arr (Tp.prop) (Tp.all (Tp.prop)))",
                 "(Tm.lam (Tp.prop) (Tm.typeLam (Tm.db 0)))", NULL) == MG_MALFORMED,
           "mixed_binder_capture_definition_refused");
    EXPECT(admit(MG_ITEM_DEFINITION, "unknown", "(Tp.prop)", "(Tm.prim 99)", NULL) == MG_REFUSED,
           "primitive_inventory_still_selected");
    EXPECT(admit(MG_ITEM_DEFINITION, "redex", "(Tp.arr (Tp.base 0) (Tp.base 0))",
                 "(Tm.app (Tm.lam (Tp.arr (Tp.base 0) (Tp.base 0)) (Tm.db 0)) (Tm.lam (Tp.base 0) (Tm.db 0)))",
                 NULL) == MG_REFUSED, "non-normal definition refused (tm_norm_p)");
    EXPECT(admit(MG_ITEM_DEFINITION, "inmem", "(Tp.arr (Tp.base 0) (Tp.arr (Tp.base 0) (Tp.prop)))",
                 "(Tm.prim 1)", NULL) == MG_OK, "Mizar primitive In is selected");
    EXPECT(admit(MG_ITEM_DEFINITION, "power", "(Tp.arr (Tp.base 0) (Tp.base 0))",
                 "(Tm.prim 4)", NULL) == MG_REFUSED, "Mizar has no Power primitive");
    fresh_theory(MG_PROFILE_EGAL);
    EXPECT(admit(MG_ITEM_DEFINITION, "power", "(Tp.arr (Tp.base 0) (Tp.base 0))",
                 "(Tm.prim 4)", NULL) == MG_OK, "Egal Power primitive is selected");
    EXPECT(admit(MG_ITEM_DEFINITION, "univ", "(Tp.arr (Tp.base 0) (Tp.base 0))",
                 "(Tm.prim 6)", NULL) == MG_OK, "Egal UnivOf primitive is selected");
    EXPECT(admit(MG_ITEM_DEFINITION, "eps", "(Tp.base 0)", "(Tm.prim 0)", NULL) == MG_REFUSED,
           "Prim 0 is not a selected primitive");
    fresh_theory(MG_PROFILE_SETMM);
    EXPECT(T->prim_len == 1291u && T->prims[0] == TP("(Tp.arr (Tp.prop) (Tp.prop))"), "SetMM inventory wn");
    mg_theory_associate_parameter(T, NM("wi"), TP("(Tp.prop)"));
    EXPECT(admit(MG_ITEM_PARAMETER, "wi", "(Tp.prop)", NULL, NULL) == MG_REFUSED, "SetMM reserves wi");
    fresh_theory(MG_PROFILE_HOAS);
    EXPECT(T->prim_len == 0u, "HOAS selects no primitive through primname");
}

/* ── budgets: exhaustion is not refusal; retrying succeeds ────────────── */
static void test_budget(void) {
    fresh_theory(MG_PROFILE_CUSTOM);
    parameter("p", "(Tp.prop)");
    EXPECT(admit(MG_ITEM_DEFINITION, "alias", "(Tp.prop)", "(Tm.named \"p\")", NULL) == MG_OK, "alias");
    mg_theory_associate_known(T, NM("proof"), TM("(Tm.named \"alias\")"));
    EXPECT(admit(MG_ITEM_KNOWN, "proof", NULL, "(Tm.named \"alias\")", NULL) == MG_OK, "known alias");
    MgCtx *e = T->store.ctx_empty;
    MgWitness w = {.rule = MG_RULE_KNOWN, .name = NM("proof")};
    MgJudgment claim = {e, TM("(Tm.named \"p\")")}, prem[2];
    uint32_t n;
    MgBudget b; mg_budget_init(&b, 1);
    MgCheck c; mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    EXPECT(mg_requirements(&c, &w, claim, prem, &n) == MG_EXHAUSTED,
           "insufficient_conversion_fuel_is_exhaustion");
    mg_budget_init(&b, 0);
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    EXPECT(mg_requirements(&c, &w, claim, prem, &n) == MG_OK && n == 0u,
           "retry_preserves_request_and_accepts");
    MgJudgment other = {e, TM("(Tm.prim 0)")};
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    EXPECT(mg_requirements(&c, &w, other, prem, &n) == MG_REFUSED, "changing_claim_is_refusal");
    w.name = NM("missing");
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    EXPECT(mg_requirements(&c, &w, claim, prem, &n) == MG_REFUSED, "missing_known_is_refusal");
    /* An Omega-like hypothesis: normalization diverges; a budget stops it
     * as exhaustion, never as refusal. */
    const char *om = "(Tm.app (Tm.lam (Tp.prop) (Tm.app (Tm.db 0) (Tm.db 0))) "
                     "(Tm.lam (Tp.prop) (Tm.app (Tm.db 0) (Tm.db 0))))";
    char hyp[512];
    snprintf(hyp, sizeof hyp, "(Tm.all (Tp.prop) %s)", om);
    MgCtx *ch = ctx_of(0, NULL, 0, (const char *[]){hyp}, 1);
    MgWitness wh = {.rule = MG_RULE_HYP, .index = 0};
    MgJudgment jh, out;
    step(&wh, ch, NULL, 0, &jh, 0);
    MgWitness wa = {.rule = MG_RULE_TERM_APP, .arg = TM("(Tm.named \"p\")")};
    EXPECT(step(&wa, NULL, &jh, 1, &out, 5000) == MG_EXHAUSTED, "diverging normalization exhausts");
}

/* ── theorems, reuse, polymorphic schema, withdrawal, footprints ──────── */
static void test_theorems(void) {
    fresh_theory(MG_PROFILE_EGAL);
    /* The polymorphic predicate-identity schema and a base-type instance. */
    const char *schema = "(Tm.typeAll (Tm.all (Tp.arr (Tp.var 0) (Tp.prop)) (Tm.imp "
        "(Tm.all (Tp.var 0) (Tm.app (Tm.db 1) (Tm.db 0))) "
        "(Tm.all (Tp.var 0) (Tm.app (Tm.db 1) (Tm.db 0))))))";
    const char *proof = "(Pf.typeLam (Pf.termLam (Tp.arr (Tp.var 0) (Tp.prop)) "
        "(Pf.proofLam (Tm.all (Tp.var 0) (Tm.app (Tm.db 0) (Tm.db 0))) (Pf.hyp 0))))";
    EXPECT(admit(MG_ITEM_THEOREM, "pred_id", NULL, schema, proof) == MG_REFUSED,
           "schema with a mis-scoped hypothesis refused");
    const char *proof_ok = "(Pf.typeLam (Pf.termLam (Tp.arr (Tp.var 0) (Tp.prop)) "
        "(Pf.proofLam (Tm.all (Tp.var 0) (Tm.app (Tm.db 1) (Tm.db 0))) (Pf.hyp 0))))";
    EXPECT(admit(MG_ITEM_THEOREM, "pred_id", NULL, schema, proof_ok) == MG_OK,
           "polymorphic predicate-identity schema published");
    const char *inst = "(Tm.all (Tp.arr (Tp.base 0) (Tp.prop)) (Tm.imp "
        "(Tm.all (Tp.base 0) (Tm.app (Tm.db 1) (Tm.db 0))) "
        "(Tm.all (Tp.base 0) (Tm.app (Tm.db 1) (Tm.db 0)))))";
    EXPECT(admit(MG_ITEM_THEOREM, "pred_id_set", NULL, inst,
                 "(Pf.typeApp (Pf.known \"pred_id\") (Tp.base 0))") == MG_OK,
           "base-type instance published from the schema");
    EXPECT(admit(MG_ITEM_THEOREM, "pred_id_prop", NULL, inst,
                 "(Pf.typeApp (Pf.known \"pred_id\") (Tp.prop))") == MG_REFUSED,
           "instance at another type does not prove the base-type statement");
    EXPECT(admit(MG_ITEM_THEOREM, "gpa", NULL, inst, "(Pf.gpa \"auto\")") == MG_REFUSED,
           "automation placeholder refused");

    /* Withdrawal cascades to dependents; unrelated declarations stay. */
    fresh_theory(MG_PROFILE_EGAL);
    parameter("A", "(Tp.prop)");
    parameter("B", "(Tp.prop)");
    mg_theory_associate_known(T, NM("hA"), TM("(Tm.named \"A\")"));
    EXPECT(admit(MG_ITEM_KNOWN, "hA", NULL, "(Tm.named \"A\")", NULL) == MG_OK, "assume A");
    EXPECT(admit(MG_ITEM_THEOREM, "useA", NULL, "(Tm.imp (Tm.named \"B\") (Tm.named \"A\"))",
                 "(Pf.proofLam (Tm.named \"B\") (Pf.known \"hA\"))") == MG_OK, "derive B -> A from hA");
    EXPECT(admit(MG_ITEM_THEOREM, "idB", NULL, "(Tm.imp (Tm.named \"B\") (Tm.named \"B\"))",
                 "(Pf.proofLam (Tm.named \"B\") (Pf.hyp 0))") == MG_OK, "unrelated theorem");
    MgEnv before = mg_theory_snapshot(T);
    MgFootprint f; mg_footprint_init(&f);
    mg_footprint_add(&f, NM("useA"), MG_NS_KNOWN);
    MgFootprint g; mg_footprint_init(&g);
    mg_footprint_add(&g, NM("idB"), MG_NS_KNOWN);
    parameter("C", "(Tp.prop)");
    EXPECT(mg_footprint_revalidate(&f, before, mg_theory_snapshot(T)),
           "unrelated extension preserves a footprint");
    EXPECT(mg_theory_withdraw(T, NM("hA"), MG_NS_KNOWN) == MG_OK, "withdraw hA");
    MgEnv after = mg_theory_snapshot(T);
    EXPECT(!mg_env_lookup_quiet(after, NM("useA"), MG_NS_KNOWN), "dependent theorem withdrawn with its axiom");
    EXPECT(mg_env_lookup_quiet(after, NM("idB"), MG_NS_KNOWN) != NULL, "independent theorem stays");
    EXPECT(mg_env_lookup_quiet(before, NM("useA"), MG_NS_KNOWN) != NULL, "old snapshot still sees it");
    EXPECT(!mg_footprint_revalidate(&f, before, after), "withdrawn dependency fails revalidation");
    EXPECT(mg_footprint_revalidate(&g, before, after), "unaffected footprint revalidates");
    EXPECT(admit(MG_ITEM_KNOWN, "hA", NULL, "(Tm.named \"A\")", NULL) == MG_REFUSED,
           "a withdrawn name is never reused");
    mg_footprint_free(&f);
    mg_footprint_free(&g);
    /* Formerly absent, newly present: a footprint with an absent name. */
    MgFootprint h; mg_footprint_init(&h);
    mg_footprint_add(&h, NM("later"), MG_NS_TERM);
    MgEnv absent = mg_theory_snapshot(T);
    parameter("later", "(Tp.prop)");
    EXPECT(!mg_footprint_revalidate(&h, absent, mg_theory_snapshot(T)),
           "a newly present formerly absent name invalidates");
    mg_footprint_free(&h);
}

/* ── content identities: upstream test vectors ────────────────────────── */

/* The HF/Egal logical definitions and their identities as hard-coded by
 * upstream megalodon (src/megalodon.ml th0skip, src/syntax.ml pfghfprim);
 * the bodies are the -sexprinfo export of the Egal preamble. */
#define H_FALSE "5bf697cb0d1cdefbe881504469f6c48cc388994115b82514dfc4fb5e67ac1a87"
#define H_AND "87fba1d2da67f06ec37e7ab47c3ef935ef8137209b42e40205afb5afd835b738"
static void test_identities(void) {
    fresh_theory(MG_PROFILE_EGAL);
    uint8_t d[32];
    MgHash h;
    char hex[65];
    mg_sha256("abc", 3, d);
    memcpy(h.b, d, 32);
    mg_hash_hex(&h, hex);
    EXPECT(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
           "SHA-256 known answer (abc)");
    mg_sha256("", 0, d);
    memcpy(h.b, d, 32);
    mg_hash_hex(&h, hex);
    EXPECT(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
           "SHA-256 known answer (empty)");
    static const struct { const char *name, *hash, *body; } vectors[] = {
        {"True", "5867641425602c707eaecd5be95229f6fd709c9b58d50af108dfe27cb49ac069",
         "(Tm.all (Tp.prop) (Tm.imp (Tm.db 0) (Tm.db 0)))"},
        {"False", H_FALSE, "(Tm.all (Tp.prop) (Tm.db 0))"},
        {"not", "058f630dd89cad5a22daa56e097e3bdf85ce16ebd3dbf7994e404e2a98800f7f",
         "(Tm.lam (Tp.prop) (Tm.imp (Tm.db 0) (Tm.named \"" H_FALSE "\")))"},
        {"and", H_AND,
         "(Tm.lam (Tp.prop) (Tm.lam (Tp.prop) (Tm.all (Tp.prop) (Tm.imp (Tm.imp (Tm.db 2) "
         "(Tm.imp (Tm.db 1) (Tm.db 0))) (Tm.db 0)))))"},
        {"or", "cfe97741543f37f0262568fe55abbab5772999079ff734a49f37ed123e4363d7",
         "(Tm.lam (Tp.prop) (Tm.lam (Tp.prop) (Tm.all (Tp.prop) (Tm.imp (Tm.imp (Tm.db 2) (Tm.db 0)) "
         "(Tm.imp (Tm.imp (Tm.db 1) (Tm.db 0)) (Tm.db 0))))))"},
        {"iff", "9c60bab687728bc4482e12da2b08b8dbc10f5d71f5cab91acec3c00a79b335a3",
         "(Tm.lam (Tp.prop) (Tm.lam (Tp.prop) (Tm.app (Tm.app (Tm.named \"" H_AND "\") "
         "(Tm.imp (Tm.db 1) (Tm.db 0))) (Tm.imp (Tm.db 0) (Tm.db 1)))))"},
    };
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; ++i) {
        MgBudget b;
        mg_budget_init(&b, 0);
        MgCheck c;
        mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
        MgHash id;
        bool ok = mg_hash_identity(&c, TM(vectors[i].body), 0, true, &id) == MG_OK;
        if (ok)
            mg_hash_hex(&id, hex);
        EXPECT(ok && strcmp(hex, vectors[i].hash) == 0, vectors[i].name);
    }
    /* a one-bit change of the body changes the identity */
    MgBudget b;
    mg_budget_init(&b, 0);
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(T), &b, NULL);
    MgHash id;
    EXPECT(mg_hash_identity(&c, TM("(Tm.all (Tp.prop) (Tm.imp (Tm.db 0) (Tm.db 1)))"), 0, true, &id) != MG_OK ||
           (mg_hash_hex(&id, hex), strcmp(hex, vectors[0].hash) != 0), "a different body, a different identity");
}

int main(void) {
    A = mg_sx_arena_new();
    test_identities();
    test_interning();
    test_mathdata_examples();
    test_rules();
    test_admission();
    test_budget();
    test_theorems();
    mg_theory_release(T);
    mg_sx_arena_free(A);
    printf("(MgCoreSummary %d %d)\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
