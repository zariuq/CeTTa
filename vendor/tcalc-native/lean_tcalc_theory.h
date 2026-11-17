/* lean_tcalc_theory.h: Pure flat/mflat tcalc theory specification for Lean 4 Kernel.
 *
 * Every declaration-admission and judgment-derivation rule in this theory is
 * formulated in tcalc's pure flat fragment:
 *   - All 12 leaf emission rules (R_CHECK_SORT .. R_EMIT_DECL_REQ3) have
 *     `k->mflat == true` (nkeep == 0, flat K_APP conclusion over context vars).
 *   - All 11 premise-bearing rules (R_DERIVE_IS_TYPE, R_DERIVE_IS_PROP,
 *     R_CHECK_CONV, and R_ADMIT_*) have `k->flat == true` and
 *     `FL[k->fl].ordinary == true` (all conclusion variables bound by `keep`
 *     premises of arity <= 3, with all cross-premise equality guards and the
 *     universe-zero Prop constant guard `T_ID_ZERO` compiled into `FL[k->fl]`).
 *
 * This enables both:
 *   1. Direct execution through the Cesta-verified ordinary-flat runtime
 *      (`tc_flat_fire_ordinary` / `cesta/tc_scanner.cesta`), and
 *   2. Build-time Second Futamura specialization via `lean_tcalc_compile` into
 *      `lean_tcalc_gen.h` (emitting both Level 2a unrolled arena functions and
 *      Level 2b schema-specialized flat-tuple functions from `R[]` and `FL[]`).
 */
#pragma once
#include "tcalc.h"
#include <stdlib.h>
#include <string.h>

#define LEAN_SPACE_WORK 0
#define LEAN_SPACE_ENV  1
#define LEAN_SPACE_THM  2

static int S_ID, S_LVL, S_EXPR, S_DECL, S_THM;
static int C_ID_ZERO, C_LVL_NODE, C_EXPR_NODE;
static Term T_ID_ZERO;
static int C_DECL_AXIOM, C_DECL_DEFN, C_DECL_THM, C_DECL_OPAQUE, C_DECL_QUOT, C_DECL_IND, C_DECL_CTOR, C_DECL_REC;
static int C_TYPED, C_DEFEQ, C_IS_TYPE, C_IS_PROP;
static int C_SORT_OF, C_DECL_REQ2, C_DECL_REQ3;

static int R_ADMIT_AXIOM, R_ADMIT_DEFN, R_ADMIT_THM, R_ADMIT_OPAQUE, R_ADMIT_QUOT, R_ADMIT_IND, R_ADMIT_CTOR, R_ADMIT_REC;
static int R_CHECK_SORT, R_CHECK_CONST, R_CHECK_APP, R_CHECK_LAM, R_CHECK_FORALL, R_CHECK_LET, R_CHECK_PROJ, R_CHECK_LIT;
static int R_EMIT_DEFEQ, R_EMIT_SORT_OF, R_EMIT_DECL_REQ2, R_EMIT_DECL_REQ3;
static int R_DERIVE_IS_TYPE, R_DERIVE_IS_PROP, R_CHECK_CONV;

static Term  *g_tc_id_terms     = NULL;
static size_t g_tc_id_terms_cap = 0;

static inline Term lean_tc_id_term(uint64_t x) {
    /* Bijective injection of 0-based IDs into tcalc S_ID terms:
     *   0   -> T_ID_ZERO (0-ary constructor term usable in rule patterns before tc_seal)
     *   x>0 -> fresh tcalc name allocated via tc_fresh_name() so name_ok() always holds */
    if (x == 0) return T_ID_ZERO;
    if (x >= g_tc_id_terms_cap) {
        size_t ncap = g_tc_id_terms_cap ? g_tc_id_terms_cap * 2u : 4096u;
        while (ncap <= x) ncap *= 2u;
        g_tc_id_terms = (Term *)realloc(g_tc_id_terms, ncap * sizeof(Term));
        memset(g_tc_id_terms + g_tc_id_terms_cap, 0, (ncap - g_tc_id_terms_cap) * sizeof(Term));
        g_tc_id_terms_cap = ncap;
    }
    if (!g_tc_id_terms[x]) {
        g_tc_id_terms[x] = tc_name((uint16_t)S_ID, tc_fresh_name());
    }
    return g_tc_id_terms[x];
}

static inline void lean_tc_init_kernel(void) {
    free(g_tc_id_terms);
    g_tc_id_terms = NULL;
    g_tc_id_terms_cap = 0;

    tc_init(1u << 23, 1u << 22);
    tc_protect(LEAN_SPACE_ENV);
    tc_protect(LEAN_SPACE_THM);

    S_ID   = tc_sort(SORT_STRICT);
    S_LVL  = tc_sort(SORT_STRICT);
    S_EXPR = tc_sort(SORT_STRICT);
    S_DECL = tc_sort(SORT_STRICT | SORT_PROVABLE);
    S_THM  = tc_sort(SORT_STRICT | SORT_PROVABLE);

    C_ID_ZERO = tc_con((uint16_t)S_ID, 0, 0, NULL);
    T_ID_ZERO = tc_app(C_ID_ZERO, NULL);

    Binder b_id[5] = {
        { .sort = (uint16_t)S_ID, .bound = 0, .deps = 0 },
        { .sort = (uint16_t)S_ID, .bound = 0, .deps = 0 },
        { .sort = (uint16_t)S_ID, .bound = 0, .deps = 0 },
        { .sort = (uint16_t)S_ID, .bound = 0, .deps = 0 },
        { .sort = (uint16_t)S_ID, .bound = 0, .deps = 0 }
    };
    C_LVL_NODE  = tc_con((uint16_t)S_LVL, 0, 1, b_id);
    C_EXPR_NODE = tc_con((uint16_t)S_EXPR, 0, 1, b_id);

    C_DECL_AXIOM  = tc_con((uint16_t)S_DECL, 0, 2, b_id);
    C_DECL_DEFN   = tc_con((uint16_t)S_DECL, 0, 3, b_id);
    C_DECL_THM    = tc_con((uint16_t)S_DECL, 0, 3, b_id);
    C_DECL_OPAQUE = tc_con((uint16_t)S_DECL, 0, 3, b_id);
    C_DECL_QUOT   = tc_con((uint16_t)S_DECL, 0, 2, b_id);
    C_DECL_IND    = tc_con((uint16_t)S_DECL, 0, 2, b_id);
    C_DECL_CTOR   = tc_con((uint16_t)S_DECL, 0, 3, b_id);
    C_DECL_REC    = tc_con((uint16_t)S_DECL, 0, 3, b_id);

    C_TYPED     = tc_con((uint16_t)S_THM, 0, 2, b_id); /* HasType(e, ty) */
    C_DEFEQ     = tc_con((uint16_t)S_THM, 0, 2, b_id); /* DefEq(a, b) */
    C_IS_TYPE   = tc_con((uint16_t)S_THM, 0, 2, b_id); /* IsType(ty, u) */
    C_IS_PROP   = tc_con((uint16_t)S_THM, 0, 1, b_id); /* IsProp(ty) */
    C_SORT_OF   = tc_con((uint16_t)S_THM, 0, 2, b_id); /* SortOf(sort_expr, u) */
    C_DECL_REQ2 = tc_con((uint16_t)S_THM, 0, 2, b_id); /* DeclReq2(name, ty) */
    C_DECL_REQ3 = tc_con((uint16_t)S_THM, 0, 3, b_id); /* DeclReq3(name, a1, a2) */

    /* 1. Leaf typing & certificate header rules in LEAN_SPACE_THM (12 k->mflat rules) */
    int *leaf_thm_rules[8] = {
        &R_CHECK_SORT, &R_CHECK_CONST, &R_CHECK_APP, &R_CHECK_LAM,
        &R_CHECK_FORALL, &R_CHECK_LET, &R_CHECK_PROJ, &R_CHECK_LIT
    };
    for (int i = 0; i < 8; i++) {
        tc_ctx(2, b_id);
        Term vs[2] = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.make = (Pat){ .t = tc_app(C_TYPED, vs), .space = LEAN_SPACE_THM };
        r.inputs = 0x3;   /* both holes come from the stream */
        *leaf_thm_rules[i] = tc_rule(&r);
    }
    {
        tc_ctx(2, b_id);
        Term vs[2] = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.make = (Pat){ .t = tc_app(C_DEFEQ, vs), .space = LEAN_SPACE_THM };
        r.inputs = 0x3;   /* both holes come from the stream */
        R_EMIT_DEFEQ = tc_rule(&r);
    }
    {
        tc_ctx(2, b_id);
        Term vs[2] = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.make = (Pat){ .t = tc_app(C_SORT_OF, vs), .space = LEAN_SPACE_THM };
        r.inputs = 0x3;   /* both holes come from the stream */
        R_EMIT_SORT_OF = tc_rule(&r);
    }
    {
        tc_ctx(2, b_id);
        Term vs[2] = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.make = (Pat){ .t = tc_app(C_DECL_REQ2, vs), .space = LEAN_SPACE_THM };
        r.inputs = 0x3;   /* both holes come from the stream */
        R_EMIT_DECL_REQ2 = tc_rule(&r);
    }
    {
        tc_ctx(3, b_id);
        Term vs[3] = { tc_v(0), tc_v(1), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.make = (Pat){ .t = tc_app(C_DECL_REQ3, vs), .space = LEAN_SPACE_THM };
        r.inputs = 0x7;   /* all three holes come from the stream */
        R_EMIT_DECL_REQ3 = tc_rule(&r);
    }

    /* 2. Premise-bearing judgment derivation rules in LEAN_SPACE_THM (3 k->flat rules) */
    {
        /* R_DERIVE_IS_TYPE: HasType(ty, sort_expr) + SortOf(sort_expr, u) |- IsType(ty, u) */
        tc_ctx(3, b_id);
        Term p_typed[2]  = { tc_v(0), tc_v(1) };
        Term p_sortof[2] = { tc_v(1), tc_v(2) };
        Term m_istype[2] = { tc_v(0), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 2;
        r.keep[0] = (Pat){ .t = tc_app(C_TYPED, p_typed),    .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_SORT_OF, p_sortof), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_IS_TYPE, m_istype), .space = LEAN_SPACE_THM };
        R_DERIVE_IS_TYPE = tc_rule(&r);
    }
    {
        /* R_DERIVE_IS_PROP: IsType(ty, 0) |- IsProp(ty)
         * Universe level 0 (Prop) is hardcoded as T_ID_ZERO in the premise pattern,
         * so tcalc itself refuses to derive IsProp(ty) for any non-zero universe level! */
        tc_ctx(1, b_id);
        Term p_istype[2] = { tc_v(0), T_ID_ZERO };
        Term m_isprop[1] = { tc_v(0) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 1;
        r.keep[0] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_IS_PROP, m_isprop), .space = LEAN_SPACE_THM };
        R_DERIVE_IS_PROP = tc_rule(&r);
    }
    {
        /* R_CHECK_CONV: HasType(val, actual_ty) + DefEq(actual_ty, expected_ty) + IsType(expected_ty, u)
         *               |- HasType(val, expected_ty) */
        tc_ctx(4, b_id);
        Term p_typed[2]  = { tc_v(0), tc_v(1) };
        Term p_defeq[2]  = { tc_v(1), tc_v(2) };
        Term p_istype[2] = { tc_v(2), tc_v(3) };
        Term m_typed[2]  = { tc_v(0), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 3;
        r.keep[0] = (Pat){ .t = tc_app(C_TYPED, p_typed),    .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_DEFEQ, p_defeq),    .space = LEAN_SPACE_THM };
        r.keep[2] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_TYPED, m_typed),    .space = LEAN_SPACE_THM };
        R_CHECK_CONV = tc_rule(&r);
    }

    /* 3. Premise-bearing declaration admission rules in LEAN_SPACE_ENV (8 k->flat rules) */
    {
        /* R_ADMIT_AXIOM: DeclReq2(name, ty) + IsType(ty, u) |- DeclAxiom(name, ty) */
        tc_ctx(3, b_id);
        Term p_req[2]    = { tc_v(0), tc_v(1) };
        Term p_istype[2] = { tc_v(1), tc_v(2) };
        Term m_decl[2]   = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 2;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ2, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_AXIOM, m_decl), .space = LEAN_SPACE_ENV };
        R_ADMIT_AXIOM = tc_rule(&r);
    }
    {
        /* R_ADMIT_QUOT: DeclReq2(name, ty) + IsType(ty, u) |- DeclQuot(name, ty) */
        tc_ctx(3, b_id);
        Term p_req[2]    = { tc_v(0), tc_v(1) };
        Term p_istype[2] = { tc_v(1), tc_v(2) };
        Term m_decl[2]   = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 2;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ2, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_QUOT, m_decl), .space = LEAN_SPACE_ENV };
        R_ADMIT_QUOT = tc_rule(&r);
    }
    {
        /* R_ADMIT_IND: DeclReq2(name, ty) + IsType(ty, u) |- DeclInd(name, ty) */
        tc_ctx(3, b_id);
        Term p_req[2]    = { tc_v(0), tc_v(1) };
        Term p_istype[2] = { tc_v(1), tc_v(2) };
        Term m_decl[2]   = { tc_v(0), tc_v(1) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 2;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ2, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_IND, m_decl),  .space = LEAN_SPACE_ENV };
        R_ADMIT_IND = tc_rule(&r);
    }
    {
        /* R_ADMIT_CTOR: DeclReq3(name, ind_name, ty) + DeclInd(ind_name, ind_ty) + IsType(ty, u)
         *               |- DeclCtor(name, ind_name, ty) */
        tc_ctx(5, b_id);
        Term p_req[3]    = { tc_v(0), tc_v(1), tc_v(2) };
        Term p_ind[2]    = { tc_v(1), tc_v(3) };
        Term p_istype[2] = { tc_v(2), tc_v(4) };
        Term m_decl[3]   = { tc_v(0), tc_v(1), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 3;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ3, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_DECL_IND, p_ind),   .space = LEAN_SPACE_ENV };
        r.keep[2] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_CTOR, m_decl), .space = LEAN_SPACE_ENV };
        R_ADMIT_CTOR = tc_rule(&r);
    }
    {
        /* R_ADMIT_REC: DeclReq3(name, ind_name, ty) + IsType(ty, u) |- DeclRec(name, ind_name, ty) */
        tc_ctx(4, b_id);
        Term p_req[3]    = { tc_v(0), tc_v(1), tc_v(2) };
        Term p_istype[2] = { tc_v(2), tc_v(3) };
        Term m_decl[3]   = { tc_v(0), tc_v(1), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 2;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ3, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_REC, m_decl),  .space = LEAN_SPACE_ENV };
        R_ADMIT_REC = tc_rule(&r);
    }
    {
        /* R_ADMIT_DEFN: DeclReq3(name, ty, val) + IsType(ty, u) + HasType(val, ty)
         *               |- DeclDefn(name, ty, val) */
        tc_ctx(4, b_id);
        Term p_req[3]    = { tc_v(0), tc_v(1), tc_v(2) };
        Term p_istype[2] = { tc_v(1), tc_v(3) };
        Term p_typed[2]  = { tc_v(2), tc_v(1) };
        Term m_decl[3]   = { tc_v(0), tc_v(1), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 3;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ3, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype), .space = LEAN_SPACE_THM };
        r.keep[2] = (Pat){ .t = tc_app(C_TYPED, p_typed),    .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_DEFN, m_decl), .space = LEAN_SPACE_ENV };
        R_ADMIT_DEFN = tc_rule(&r);
    }
    {
        /* R_ADMIT_OPAQUE: DeclReq3(name, ty, val) + IsType(ty, u) + HasType(val, ty)
         *                 |- DeclOpaque(name, ty, val) */
        tc_ctx(4, b_id);
        Term p_req[3]    = { tc_v(0), tc_v(1), tc_v(2) };
        Term p_istype[2] = { tc_v(1), tc_v(3) };
        Term p_typed[2]  = { tc_v(2), tc_v(1) };
        Term m_decl[3]   = { tc_v(0), tc_v(1), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 3;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ3, p_req),    .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_TYPE, p_istype),   .space = LEAN_SPACE_THM };
        r.keep[2] = (Pat){ .t = tc_app(C_TYPED, p_typed),      .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_OPAQUE, m_decl), .space = LEAN_SPACE_ENV };
        R_ADMIT_OPAQUE = tc_rule(&r);
    }
    {
        /* R_ADMIT_THM: DeclReq3(name, ty, val) + IsProp(ty) + HasType(val, ty)
         *              |- DeclThm(name, ty, val) */
        tc_ctx(3, b_id);
        Term p_req[3]    = { tc_v(0), tc_v(1), tc_v(2) };
        Term p_isprop[1] = { tc_v(1) };
        Term p_typed[2]  = { tc_v(2), tc_v(1) };
        Term m_decl[3]   = { tc_v(0), tc_v(1), tc_v(2) };
        Rule r;
        memset(&r, 0, sizeof(r));
        r.fresh = -1;
        r.nkeep = 3;
        r.keep[0] = (Pat){ .t = tc_app(C_DECL_REQ3, p_req),  .space = LEAN_SPACE_THM };
        r.keep[1] = (Pat){ .t = tc_app(C_IS_PROP, p_isprop), .space = LEAN_SPACE_THM };
        r.keep[2] = (Pat){ .t = tc_app(C_TYPED, p_typed),    .space = LEAN_SPACE_THM };
        r.make    = (Pat){ .t = tc_app(C_DECL_THM, m_decl),  .space = LEAN_SPACE_ENV };
        R_ADMIT_THM = tc_rule(&r);
    }

    tc_seal();
}
