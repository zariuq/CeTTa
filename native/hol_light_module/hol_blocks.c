/* hol_blocks.c - justified blocks of the HOL Light kernel module: judgments answered by the native
 * kernel's functions (native/hol_light/hol_core.c, bound, not changed).
 *
 * The contract of each block is its judgment's rules in the module; the block table (rulec
 * --blocks) gives each block its label: proved (the theorems), checked (every call of a corpus run
 * also runs the compiled rules on the same inputs and the results are compared as module data), or
 * trusted (the reason).  The functions here only map arguments and results between the module's
 * representation (lists of native values, the module's constructors for flags, orders and results)
 * and the native calls; each native call starts with a clear status, and a native refusal is mapped
 * to the module's result exactly where the module's rules have one (vsubst, mk_comb, mk_abs); any
 * other native failure is a resource failure and stops the run.
 */
#include "../rule_runtime/rc_rt.h"
#include "generated/hol_module_ids.h"
#include "../hol_light/hol_internal.h"

#include <stdio.h>
#include <stdlib.h>

extern HolKernel *holr_k;
_Noreturn void holr_refused(const char *what);

#define NUL(c) ((T *)&rc_nul[(c)])
#define TM(x) ((HolTerm *)RC_FOREIGN_PTR(x))
#define TY(x) ((HolType *)RC_FOREIGN_PTR(x))
#define NAME(x) ((HolName)RC_IMM_VAL(x))
static inline T *tm_val(HolTerm *t) { if (!t) holr_refused("a term operation"); return RC_FOREIGN(t, 2u); }
static inline T *ty_val(HolType *t) { if (!t) holr_refused("a type operation"); return RC_FOREIGN(t, 4u); }
static inline T *ord(int c) { return c < 0 ? NUL(C_hl_lt) : c > 0 ? NUL(C_hl_gt) : NUL(C_hl_eq); }
static inline T *flag(int b) { return b ? NUL(C_hl_yes) : NUL(C_hl_no); }

/* ---- lists between the module and arrays ---- */
typedef struct { void **v; size_t n, cap; void *small[32]; } Arr;
static void arr_init(Arr *a) { a->v = a->small; a->n = 0; a->cap = 32; }
static void arr_push(Arr *a, void *p) {
    if (a->n == a->cap) {
        size_t cap = a->cap * 2;
        void **v = malloc(cap * sizeof(void *));
        if (!v) holr_refused("memory");
        memcpy(v, a->v, a->n * sizeof(void *));
        if (a->v != a->small) free(a->v);
        a->v = v; a->cap = cap;
    }
    a->v[a->n++] = p;
}
static void arr_free(Arr *a) { if (a->v != a->small) free(a->v); }
static void tms_arr(T *l, Arr *a) { arr_init(a); for (; l->tag == C_hl_tmcons; l = l->k[1]) arr_push(a, TM(l->k[0])); }
static T *arr_tms(void *const *v, size_t n) {
    T *l = NUL(C_hl_tmnil);
    while (n--) l = rc_mk2(C_hl_tmcons, RC_FOREIGN(v[n], 2u), l);
    return l;
}
static T *arr_tys(void *const *v, size_t n) {
    T *l = NUL(C_hl_tynil);
    while (n--) l = rc_mk2(C_hl_tycons, RC_FOREIGN(v[n], 4u), l);
    return l;
}

/* ---- equality and order ---- */
/* names, types and terms are interned: equal values are the same object */
T *holb_str_eq(T *a, T *b) { return flag(a == b); }
T *holb_ty_eq(T *a, T *b) { return flag(a == b); }
T *holb_tm_eq(T *a, T *b) { return flag(a == b); }
/* OCaml's string order: memcmp of the common prefix, then length (hk_name_cmp) */
T *holb_cmp_str(T *a, T *b) { return ord(hk_name_cmp(holr_k, NAME(a), NAME(b))); }
T *holb_str_prime(T *a) {
    HolName n = hk_name_prime(holr_k, NAME(a));
    if (!n) holr_refused("a name");
    return RC_IMM(n);
}
T *holb_cmp_type(T *a, T *b) { return ord(hk_ty_compare(holr_k, TY(a), TY(b))); }
T *holb_cmp_term(T *a, T *b) { return ord(hk_tm_compare(holr_k, TM(a), TM(b))); }
T *holb_alphaorder(T *a, T *b) {
    int c;
    hk_clear(holr_k);
    if (hk_alphaorder(holr_k, TM(a), TM(b), &c) < 0) holr_refused("alphaorder");
    return ord(c);
}

/* ---- syntax ---- */
T *holb_type_of(T *t) { return ty_val(TM(t)->type); }
T *holb_mk_comb(T *f, T *a) {
    hk_clear(holr_k);
    HolTerm *r = hk_tm_comb(holr_k, TM(f), TM(a));
    if (r) return rc_mk1(C_hl_tm_ok, RC_FOREIGN(r, 2u));
    if (holr_k->reason != HOL_R_MK_COMB_TYPES) holr_refused("mk_comb");
    return rc_mk1(C_hl_tm_fail, NUL(C_hl_r_mk_comb_types));
}
T *holb_mk_abs(T *v, T *b) {
    hk_clear(holr_k);
    HolTerm *r = hk_tm_abs(holr_k, TM(v), TM(b));
    if (r) return rc_mk1(C_hl_tm_ok, RC_FOREIGN(r, 2u));
    if (holr_k->reason != HOL_R_MK_ABS_NOT_VAR) holr_refused("mk_abs");
    return rc_mk1(C_hl_tm_fail, NUL(C_hl_r_mk_abs_not_var));
}
T *holb_safe_mk_eq(T *l, T *r) { hk_clear(holr_k); return tm_val(hk_tm_eq(holr_k, TM(l), TM(r))); }

/* ---- free variables, type variables ---- */
T *holb_frees(T *t) {
    HolVec v = {0};
    hk_clear(holr_k);
    if (hk_frees_exact(holr_k, TM(t), &v) < 0) holr_refused("frees");
    T *l = arr_tms(v.v, v.n);
    hk_vec_free(holr_k, &v);
    return l;
}
T *holb_freesin(T *acc, T *t) {
    Arr a;
    tms_arr(acc, &a);
    hk_clear(holr_k);
    int r = hk_freesin(holr_k, (HolTerm *const *)a.v, a.n, TM(t));
    arr_free(&a);
    if (r < 0) holr_refused("freesin");
    return flag(r);
}
T *holb_vfree_in(T *v, T *t) {
    hk_clear(holr_k);
    int r = hk_vfree_in(holr_k, TM(v), TM(t));
    if (r < 0) holr_refused("vfree_in");
    return flag(r);
}
T *holb_type_vars_in_term(T *t) {
    HolVec v = {0};
    hk_clear(holr_k);
    if (hk_type_vars_in_term_exact(holr_k, TM(t), &v) < 0) holr_refused("type_vars_in_term");
    T *l = arr_tys(v.v, v.n);
    hk_vec_free(holr_k, &v);
    return l;
}
T *holb_tyvars(T *ty) {
    HolVec v = {0};
    hk_clear(holr_k);
    if (hk_tyvars_exact(holr_k, TY(ty), &v) < 0) holr_refused("tyvars");
    T *l = arr_tys(v.v, v.n);
    hk_vec_free(holr_k, &v);
    return l;
}

/* ---- renaming and substitution ---- */
T *holb_variant(T *avoid, T *v) {
    Arr a;
    tms_arr(avoid, &a);
    hk_clear(holr_k);
    HolTerm *r = hk_variant(holr_k, (HolTerm *const *)a.v, a.n, TM(v));
    arr_free(&a);
    return tm_val(r);
}
/* theta is a TmPairs list: tmp-cons VALUE KEY rest, read as the pair (VALUE, KEY) */
T *holb_vsubst(T *theta, T *tm) {
    size_t n = 0;
    for (T *l = theta; l->tag == C_hl_tmp_cons; l = l->k[2]) n++;
    HolTmPair small[16], *p = n <= 16 ? small : malloc(n * sizeof(HolTmPair));
    if (!p) holr_refused("memory");
    size_t i = 0;
    for (T *l = theta; l->tag == C_hl_tmp_cons; l = l->k[2], i++) { p[i].t = TM(l->k[0]); p[i].x = TM(l->k[1]); }
    hk_clear(holr_k);
    HolTerm *r = hk_vsubst(holr_k, p, n, TM(tm));
    if (p != small) free(p);
    if (r) return rc_mk1(C_hl_vs_ok, RC_FOREIGN(r, 2u));
    if (holr_k->reason != HOL_R_VSUBST_BAD) holr_refused("vsubst");
    return NUL(C_hl_vs_bad);
}
static size_t ty_pairs(T *tyin, HolTyPair *small, HolTyPair **out) {
    size_t n = 0;
    for (T *l = tyin; l->tag == C_hl_typ_cons; l = l->k[2]) n++;
    HolTyPair *p = n <= 16 ? small : malloc(n * sizeof(HolTyPair));
    if (!p) holr_refused("memory");
    size_t i = 0;
    for (T *l = tyin; l->tag == C_hl_typ_cons; l = l->k[2], i++) { p[i].repl = TY(l->k[0]); p[i].key = TY(l->k[1]); }
    *out = p;
    return n;
}
T *holb_inst(T *tyin, T *tm) {
    HolTyPair small[16], *p;
    size_t n = ty_pairs(tyin, small, &p);
    hk_clear(holr_k);
    HolTerm *r = hk_inst(holr_k, p, n, TM(tm));
    if (p != small) free(p);
    return tm_val(r);
}
T *holb_type_subst(T *tyin, T *ty) {
    HolTyPair small[16], *p;
    size_t n = ty_pairs(tyin, small, &p);
    HolTySubst s;
    hk_tysubst_init(&s, n ? p : NULL, n);
    hk_clear(holr_k);
    HolType *r = hk_type_subst(holr_k, &s, TY(ty));
    if (p != small) free(p);
    return ty_val(r);
}

/* vsubst's recursion (fusion.ml's inner vsubst), which the rules call only after the list was checked
 * (vsubst-good): hk_vsubst on a list that passes the check; a list that fails it stops the run */
static size_t tm_pairs(T *theta, HolTmPair *small, HolTmPair **out) {
    size_t n = 0;
    for (T *l = theta; l->tag == C_hl_tmp_cons; l = l->k[2]) n++;
    HolTmPair *p = n <= 16 ? small : malloc(n * sizeof(HolTmPair));
    if (!p) holr_refused("memory");
    size_t i = 0;
    for (T *l = theta; l->tag == C_hl_tmp_cons; l = l->k[2], i++) { p[i].t = TM(l->k[0]); p[i].x = TM(l->k[1]); }
    *out = p;
    return n;
}
T *holb_vsubst_rec(T *theta, T *tm) {
    HolTmPair small[16], *p;
    size_t n = tm_pairs(theta, small, &p);
#ifdef HOLB_FAULT_VSUBST_REC
    if (n > 1) n = 1;   /* planted fault: only the first pair is substituted */
#endif
    hk_clear(holr_k);
    HolTerm *r = hk_vsubst(holr_k, p, n, TM(tm));
    if (p != small) free(p);
    if (!r) holr_refused("vsubst's recursion on a list vsubst refuses");
    return RC_FOREIGN(r, 2u);
}

/* ---- hypothesis lists (fusion.ml's term_union and term_remove, over alphaorder) ---- */
static int alpha(HolTerm *a, HolTerm *b) {
    int c;
    hk_clear(holr_k);
    if (hk_alphaorder(holr_k, a, b, &c) < 0) holr_refused("alphaorder");
    return c;
}
/* let rec term_union l1 l2 = match (l1,l2) with ([],l2) -> l2 | (l1,[]) -> l1
 *   | (h1::t1,h2::t2) -> let c = alphaorder h1 h2 in
 *       if c = 0 then h1::(term_union t1 t2) else if c < 0 then h1::(term_union t1 l2)
 *       else h2::(term_union l1 t2)
 * The result shares the remaining tail of whichever list runs on alone, as the rules do. */
T *holb_term_union(T *l1, T *l2) {
    Arr out;
    arr_init(&out);
    T *a = l1, *b = l2;
    while (a->tag == C_hl_tmcons && b->tag == C_hl_tmcons) {
        int c = alpha(TM(a->k[0]), TM(b->k[0]));
#ifdef HOLB_FAULT_TERM_UNION
        c = -c;   /* planted fault: the larger head first */
#endif
        if (c == 0) { arr_push(&out, a->k[0]); a = a->k[1]; b = b->k[1]; }
        else if (c < 0) { arr_push(&out, a->k[0]); a = a->k[1]; }
        else { arr_push(&out, b->k[0]); b = b->k[1]; }
    }
    T *tail = a->tag == C_hl_tmcons ? a : b;
    for (size_t i = out.n; i-- > 0;) tail = rc_mk2(C_hl_tmcons, (T *)out.v[i], tail);
    arr_free(&out);
    return tail;
}
/* let rec term_remove t l = match l with
 *     s::ss -> let c = alphaorder t s in
 *              if c > 0 then s::(term_remove t ss) else if c = 0 then ss else l
 *   | [] -> l */
T *holb_term_remove(T *t, T *l) {
    Arr out;
    arr_init(&out);
    T *x = l;
    while (x->tag == C_hl_tmcons) {
        int c = alpha(TM(t), TM(x->k[0]));
        if (c > 0) { arr_push(&out, x->k[0]); x = x->k[1]; continue; }
        if (c == 0) x = x->k[1];
        break;
    }
    for (size_t i = out.n; i-- > 0;) x = rc_mk2(C_hl_tmcons, (T *)out.v[i], x);
    arr_free(&out);
    return x;
}

/* term_image f l, as the rules term-image-vsubst and term-image-inst give it:
 *   term_image f (h::t) = let h1 = f h and t1 = term_image f t in
 *                         if h1::t1 = h::t (as lists) then h::t else term_union [h1] t1
 * (the input list itself when nothing changes) */
static bool tms_equal(T *a, T *b) {
    while (a->tag == C_hl_tmcons && b->tag == C_hl_tmcons) {
        if (a->k[0] != b->k[0]) return false;
        a = a->k[1];
        b = b->k[1];
    }
    return a->tag == b->tag;
}
typedef HolTerm *(*TermMap)(const void *pairs, size_t n, HolTerm *t);
static HolTerm *map_vsubst(const void *pairs, size_t n, HolTerm *t) {
    hk_clear(holr_k);
    HolTerm *r = hk_vsubst(holr_k, (const HolTmPair *)pairs, n, t);
    if (!r) holr_refused("vsubst's recursion on a list vsubst refuses");
    return r;
}
static HolTerm *map_inst(const void *pairs, size_t n, HolTerm *t) {
    hk_clear(holr_k);
    HolTerm *r = hk_inst(holr_k, (const HolTyPair *)pairs, n, t);
    if (!r) holr_refused("inst");
    return r;
}
static T *term_image(T *l, TermMap f, const void *pairs, size_t np) {
    Arr cells;
    arr_init(&cells);
    for (T *x = l; x->tag == C_hl_tmcons; x = x->k[1]) arr_push(&cells, x);
    T *r = NUL(C_hl_tmnil);
    for (size_t i = cells.n; i-- > 0;) {
        T *li = (T *)cells.v[i];
        HolTerm *h = TM(li->k[0]), *h1 = f(pairs, np, h);
        if (h1 == h && tms_equal(r, li->k[1])) r = li;
        else r = holb_term_union(rc_mk2(C_hl_tmcons, RC_FOREIGN(h1, 2u), NUL(C_hl_tmnil)), r);
    }
    arr_free(&cells);
    return r;
}
T *holb_term_image_vsubst(T *theta, T *l) {
    HolTmPair small[16], *p;
    size_t n = tm_pairs(theta, small, &p);
    T *r = term_image(l, map_vsubst, p, n);
    if (p != small) free(p);
    return r;
}
T *holb_term_image_inst(T *tyin, T *l) {
    HolTyPair small[16], *p;
    size_t n = ty_pairs(tyin, small, &p);
    T *r = term_image(l, map_inst, p, n);
    if (p != small) free(p);
    return r;
}

/* the pair lists for hol_blocks_thm.c (theorems as kernel theorems, an optional build) */
size_t holb_ty_pairs(T *tyin, HolTyPair *small, HolTyPair **out) { return ty_pairs(tyin, small, out); }
size_t holb_tm_pairs(T *theta, HolTmPair *small, HolTmPair **out) { return tm_pairs(theta, small, out); }
