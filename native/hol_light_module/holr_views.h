/* holr_views.h - the module's names, types and terms represented by the native HOL Light kernel's
 * interned objects, read through views that give the module's constructors back.
 *
 *   Str   an interned name (HolName) as an immediate;   constructors nnil / ncons (byte hi lo) rest
 *   Type  a HolType*, tagged 4;                         constructors Tyvar n / Tyapp n args
 *   Term  a HolTerm*, tagged 2;                         constructors Var n ty / Const n ty / Comb f x /
 *                                                       Abs v b
 * Construction goes through the kernel's interning constructors (hk_ty_var, hk_ty_app, hk_tm_var,
 * hk_tm_const, hk_tm_comb, hk_tm_abs), so equal structure gives the same object (the store
 * invariant; see the interning theorem).  A type operator or constant name gets one declaration,
 * made at its first use (holr_ty_app, holr_const); the module's types and constants are syntactic,
 * and one declaration per name gives exactly that identity.  hk_tm_comb and hk_tm_abs refuse an
 * ill-typed combination and a binder that is not a variable: the module never builds either, and a
 * refusal stops the run (a refinement violation), it is never a silent difference.
 *
 * Included by the generated code after it defines RC_EXT_holname_C0..1, RC_EXT_holtype_C0..1 and
 * RC_EXT_holterm_C0..3 (the module's constructor ids, in the orders above).
 */
#ifndef HOLR_VIEWS_H
#define HOLR_VIEWS_H

#include "../rule_runtime/rc_rt.h"
#include "../hol_light/hol_internal.h"

extern HolKernel *holr_k;
extern HolName holr_empty_name;
T *rc_byte_term(unsigned v);
T *holr_tys_of(const HolType *t);                 /* a type's arguments as a module Tys list */
HolType *holr_ty_app(HolName n, T *args);         /* Tyapp n args */
HolTerm *holr_const(HolName n, HolType *ty);      /* Const n ty */
HolName holr_name_cons(T *byte, HolName rest);    /* ncons (byte hi lo) rest */
_Noreturn void holr_refused(const char *what);

#define HOLR_TAG_TERM 2u
#define HOLR_TAG_TYPE 4u
#define HOLR_NAME(x) ((HolName)RC_IMM_VAL(x))
#define HOLR_TY(x) ((HolType *)RC_FOREIGN_PTR(x))
#define HOLR_TM(x) ((HolTerm *)RC_FOREIGN_PTR(x))
static inline T *holr_ty_val(HolType *t) { if (!t) holr_refused("a type"); return RC_FOREIGN(t, HOLR_TAG_TYPE); }
static inline T *holr_tm_val(HolTerm *t) { if (!t) holr_refused("a term"); return RC_FOREIGN(t, HOLR_TAG_TERM); }

/* ---- Str ---- */
static inline uint16_t RC_TAG_holname(T *x) { return holr_k->names[HOLR_NAME(x)].len ? RC_EXT_holname_C1 : RC_EXT_holname_C0; }
static inline T *RC_KID_holname(T *x, int i) {
    const HolNameRec *r = &holr_k->names[HOLR_NAME(x)];
    if (i == 0) return rc_byte_term((uint8_t)r->bytes[0]);
    HolName n = hk_name_intern(holr_k, r->bytes + 1, r->len - 1);
    if (!n) holr_refused("a name");
    return RC_IMM(n);
}
#define RC_NUL_holname(c) RC_IMM(holr_empty_name)
static inline T *RC_MK2_holname(uint16_t c, T *b, T *s) { (void)c; return RC_IMM(holr_name_cons(b, HOLR_NAME(s))); }

/* ---- Type ---- */
static inline uint16_t RC_TAG_holtype(T *x) { return HOLR_TY(x)->kind == HOL_TY_VAR ? RC_EXT_holtype_C0 : RC_EXT_holtype_C1; }
static inline T *RC_KID_holtype(T *x, int i) {
    HolType *t = HOLR_TY(x);
    return i == 0 ? RC_IMM(t->name) : holr_tys_of(t);
}
static inline T *RC_MK1_holtype(uint16_t c, T *n) { (void)c; return holr_ty_val(hk_ty_var(holr_k, HOLR_NAME(n))); }
static inline T *RC_MK2_holtype(uint16_t c, T *n, T *args) { (void)c; return holr_ty_val(holr_ty_app(HOLR_NAME(n), args)); }

/* ---- Term ---- */
static inline uint16_t RC_TAG_holterm(T *x) {
    switch (HOLR_TM(x)->kind) {
    case HOL_TM_VAR: return RC_EXT_holterm_C0;
    case HOL_TM_CONST: return RC_EXT_holterm_C1;
    case HOL_TM_COMB: return RC_EXT_holterm_C2;
    default: return RC_EXT_holterm_C3;
    }
}
static inline T *RC_KID_holterm(T *x, int i) {
    HolTerm *t = HOLR_TM(x);
    if (t->kind == HOL_TM_VAR || t->kind == HOL_TM_CONST)
        return i == 0 ? RC_IMM(t->u.leaf.name) : RC_FOREIGN(t->type, HOLR_TAG_TYPE);
    return RC_FOREIGN(i == 0 ? t->u.node.l : t->u.node.r, HOLR_TAG_TERM);
}
static inline T *RC_MK2_holterm(uint16_t c, T *a, T *b) {
    if (c == RC_EXT_holterm_C0) return holr_tm_val(hk_tm_var(holr_k, HOLR_NAME(a), HOLR_TY(b)));
    if (c == RC_EXT_holterm_C1) return holr_tm_val(holr_const(HOLR_NAME(a), HOLR_TY(b)));
    hk_clear(holr_k);
    if (c == RC_EXT_holterm_C2) return holr_tm_val(hk_tm_comb(holr_k, HOLR_TM(a), HOLR_TM(b)));
    return holr_tm_val(hk_tm_abs(holr_k, HOLR_TM(a), HOLR_TM(b)));
}

/* ---- Thm ----
 * A theorem is a HolThm* (tag 6) of the kernel's interning store, read as Sequent asl c: the
 * hypotheses as a module Tms list, the conclusion a term.  Sequent asl c is made with hk_thm_make
 * (no axiom dependencies: the module records them itself; the anchor is the join of the terms'
 * anchors, one lineage).  The ten rules' blocks make theorems with the kernel's rules, whose anchors
 * follow the derivation, so two equal sequents may be two objects: the module never tests theorems
 * for equality, and check mode compares them as sequents (rc_foreign_equal_hook). */
#ifdef RC_EXT_holthm_C0
#define HOLR_TAG_THM 6u
#define HOLR_TH(x) ((HolThm *)RC_FOREIGN_PTR(x))
T *holr_thm_hyps(const HolThm *th);
HolThm *holr_thm_make(T *asl, HolTerm *c);
static inline uint16_t RC_TAG_holthm(T *x) { (void)x; return RC_EXT_holthm_C0; }
static inline T *RC_KID_holthm(T *x, int i) {
    HolThm *th = HOLR_TH(x);
    return i == 0 ? holr_thm_hyps(th) : RC_FOREIGN(th->concl, HOLR_TAG_TERM);
}
static inline T *RC_MK2_holthm(uint16_t c, T *asl, T *concl) {
    (void)c;
    HolThm *th = holr_thm_make(asl, HOLR_TM(concl));
    if (!th) holr_refused("a theorem");
    return RC_FOREIGN(th, HOLR_TAG_THM);
}
#endif

/* ---- Table (holr_tables.c) ----
 * A table is an array updated in place (an immediate naming the array and its version) or, as the
 * rules build it, module data ("table" next trie); the view reads both.  The rules construct tables
 * only in the judgments the block table binds, so module-form tables arise only where check mode
 * runs those rules beside the blocks; RC_READBACK gives both results as module data. */
#ifdef RC_EXT_holtable_C0
T *holr_table_kid(T *x, int i);
T *holr_table_readback(T *x);
static inline uint16_t RC_TAG_holtable(T *x) { (void)x; return RC_EXT_holtable_C0; }
static inline T *RC_KID_holtable(T *x, int i) { return RC_IS_HEAP(x) ? x->k[i] : holr_table_kid(x, i); }
static inline T *RC_MK2_holtable(uint16_t c, T *next, T *trie) { return rc_mk2(c, next, trie); }
#define RC_READBACK_holtable(x) holr_table_readback(x)
#endif

#endif
