#ifndef CETTA_MEGALODON_ELAB_INTERNAL_H
#define CETTA_MEGALODON_ELAB_INTERNAL_H

/*
 * The document evaluator's state and the helpers its two halves share:
 * mg_elab.c (documents, terms, identities) and mg_proof.c (proof terms,
 * conversion, the tactic engine).  Not part of the public interface.
 */

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mg_core.h"
#include "mg_elab.h"
#include "mg_front.h"
#include "mg_hash.h"
#include "mg_library.h"
#include "mg_source.h"

#define NONE UINT32_MAX

/* RAISE_FATAL: an exception Megalodon does not catch. */
/* RAISE_KERNEL: the kernel refused what Megalodon accepts. */
/* RAISE_NEGDB: syntax.ml's NegDB (a shift made an index negative).
 * RAISE_EXHAUSTED: the theorem's work budget ran out; whatever fails after
 * that is undecided, so every raise but RAISE_FATAL becomes this one. */
enum { RAISE_FAILURE = 1, RAISE_NOT_FOUND = 2, RAISE_FATAL = 3, RAISE_KERNEL = 4,
       RAISE_NEGDB = 5, RAISE_SEARCHLIMIT = 7, RAISE_SEARCHBACKTRACK = 8,
       RAISE_EXHAUSTED = 9 };

/* ── storage ──────────────────────────────────────────────────────────── */

typedef struct Chunk Chunk;
struct Chunk {
    Chunk *next;
    size_t used, cap;
    max_align_t data[];
};

typedef struct {
    uint64_t key;
    uint64_t val;
    bool used;
} Slot;

typedef struct {
    Slot *slots;
    size_t cap, len;
} Map;

typedef struct Cx Cx;          /* the term context, newest first */
struct Cx {
    const char *name;
    MgTp *tp;
    MgTm *def;                 /* a let; NULL for a variable */
    const Cx *next;
};

typedef struct Hy Hy;          /* the hypotheses, newest first */
struct Hy {
    const char *name;
    MgTm *prop;
    const Hy *next;
};

/* Section closures (tparclos, tmallclos, tmlamclos, pflamclos), newest
 * first: a group of variables of one type, or a hypothesis. */
typedef struct Clo Clo;
struct Clo {
    bool hyp;
    MgTp *tp;
    uint32_t count;
    MgTm *prop;
    const Clo *next;
};

/* aptmloc / appfloc: the section variables a signature name is applied
 * to where it is used, looked up by name there; newest first. */
enum { APP_TP, APP_TM, APP_HYP };
typedef struct App App;
struct App {
    uint8_t kind;
    const char *name;
    const App *prev;
};

typedef struct Stk Stk;        /* a binding stack (OCaml Hashtbl.add/remove) */
struct Stk {
    uint64_t val;
    const Stk *next;
};

typedef struct {
    MgTm *m;
    const App *app;
} SigTm;

typedef struct {
    uint32_t known;
    const App *app;
} SigPf;

typedef struct {
    uint32_t levels;
    MgTp *tp;
} PTp;

typedef struct {
    uint32_t levels;
    MgTm *m;
} PTm;

typedef struct {
    bool plus;
    const MgL *a;
    const MgL *b;
} BinderSem;

typedef struct {
    const char *name;
    MgTm *m;
} CarryTm;

typedef struct {
    const char *name;
    uint32_t known;
} CarryPf;

typedef struct {
    const char *name;
    size_t undo_mark;
    const App *aptm, *appf;
    CarryTm *tm;
    size_t tm_len, tm_cap;
    CarryPf *pf;
    size_t pf_len, pf_cap;
} Frame;

enum { U_CTX, U_TPVARS, U_STACK };
typedef struct {
    uint8_t kind;
    Map *map;
    uint64_t key;
    const Cx *ctxtm;
    const Hy *ctxpf;
    const Clo *clos;
} Undo;

/* A polymorphic name that becomes usable as such when its type variables'
 * section ends (pushpolytm, pushpolypf): its type, or its proposition. */
typedef struct {
    const char *name;
    uint32_t arity;
    uint64_t val;
} Future;

typedef struct {
    uint32_t count;            /* variables of the context it was made in */
    const MgTp **tps;
    MgTp *tp;
} MetaCx;

/* pfstate: goals and the separators of structured subproofs. */
typedef struct PfSt PfSt;
struct PfSt {
    bool sep;
    int32_t j;
    bool b;
    MgTm *claim;
    const Cx *ctxtm;
    const Hy *ctxpf;
    const PfSt *next;
};

/* Consecutive name ids of native aby's variable names (mg_aby.c): pool
 * entries first .. first + len - 1 are names base .. base + len - 1. */
typedef struct {
    uint32_t base, first, len;
} MgAbyRun;

struct MgElab {
    MgProfileTag theory;
    FILE *rows;
    MgTheory *t;
    MgStore *s;
    MgBudget budget;
    Chunk *perm;               /* lives with the evaluator */
    Chunk *tmp;                /* reset at every item */
    Map strs;                  /* interned strings, by content hash */
    MgBuf err, msg, row;
    jmp_buf *fail;
    int lineno, charno;
    bool signature;
    bool title_set;

    const char **ctxtp;
    uint32_t ctxtp_len, ctxtp_cap;
    const Cx *ctxtm;
    const Hy *ctxpf;
    const Clo *clos;
    const App *aptm, *appf;
    Frame *frames;
    size_t frame_len, frame_cap;
    Undo *undo;
    size_t undo_len, undo_cap;

    Map sigtm, sigpf;          /* name -> Stk of SigTm* / SigPf* */
    Map sigtmh, sigknh;        /* name -> identity name id */
    Map sigtmof;               /* identity -> PTp* */
    Map sigdelta, sigdelta_opaque; /* identity -> PTm* */
    Map polytm[7];             /* name -> MgTp*, by arity */
    Map polypf[7];             /* name -> MgTm*, by arity */
    Future *future, *future_pf;
    size_t future_len, future_cap, future_pf_len, future_pf_cap;
    Map indextms;              /* identity -> MgTp* */
    Map indexknowns;           /* identity -> 1 */
    Map pfgtmhh;               /* identity -> MgHash* */
    Map tpabbrev;              /* name -> MgTp* */
    Map infixsem, prefixsem, postfixsem, bindersem; /* name -> Stk */
    uint32_t setIn, setSubeq, setPow, setimplop, setlam, replop, sepop, replsepop;
    uint32_t ifop, ifopset, setenumadj, nat0, natS, seqcons, set0;
    uint32_t *setenuml;
    size_t setenuml_len, setenuml_cap;
    uint32_t eqPoly, expoly, expolyI, fal, fale, conj, disj;
    bool expolyIknown;
    Map metavarcx;             /* name -> MetaCx* */
    Map metavarassoc;          /* name -> MgTm* */

    bool proving;
    const char *thm_name;
    uint32_t thm_levels;
    MgTm *thm_prop;            /* gpgtm, closed over the sections */
    uint32_t thm_hash;
    const PfSt *pfstate;
    bool admitted;             /* prooffun raises AdmittedPf */
    bool admit_used, aby_used; /* why: an admit, an aby */
    bool unchecked_proof;
    bool exhausted_proof;      /* why: the theorem's work budget ran out */
    /* typed megaauto's constants and knowns, recognized by identity as
     * they are declared (megalodon.ml megaauto_set_item and
     * megaauto_set_known), NONE until then */
    uint32_t ma_setordsucc, ma_set1, ma_set2, ma_setsum, ma_setlambda, ma_setprod, ma_setap,
        ma_setPi, ma_setexp;
    uint32_t ma_In_0_2, ma_In_1_2, ma_tupleI0, ma_tupleI1, ma_tuple0_setsum, ma_tuple1_setsum,
        ma_tuple_2_0_eq, ma_tuple_2_1_eq, ma_tuple_2_Sigma, ma_tuple_2_setprod, ma_tuple_2_eta,
        ma_ap0_Sigma, ma_ap1_Sigma, ma_tuple_Sigma_eta, ma_lam_ext, ma_lam_Pi, ma_ap_Pi, ma_beta,
        ma_Pi_ext, ma_Pi_eta, ma_Pi_cod_ext;
    /* native aby (mg_aby.c): on when prove_aby; its variable names */
    bool prove_aby;
    uint64_t aby_max_steps;
    uint32_t aby_max_depth;
    uint64_t aby_tried, aby_proved;
    uint32_t *aby_pool;
    uint32_t aby_pool_len;
    MgAbyRun *aby_runs;
    uint32_t aby_nruns, aby_cruns;
    uint64_t unchecked;
    const struct Dl *deltaset;          /* the definitions the proof unfolds */
    const struct PfFrame *prooffun;     /* the proof function, newest first */
    const Clo *thm_clos;                /* the section closures of the theorem */
    uint32_t mvarcnt;                   /* megaauto's metavariable counter */
    int32_t searchlim;                  /* megaauto's step budget */

    /* kernel admission */
    MgAdmission *admission;
    MgImportPolicy policy;
    MgImportReport report;
    bool finished;
};


typedef struct {
    MgTm *tm;
    MgTp *tp;
} TmTp;

typedef struct TpCx TpCx;
struct TpCx {
    MgTp *tp;
    const TpCx *next;
};

typedef struct PfCx PfCx;   /* the hypotheses' propositions, de Bruijn 0 first */
struct PfCx {
    MgTm *prop;
    const PfCx *next;
};

typedef struct Dl Dl;
typedef struct PfFrame PfFrame;

/* mg_elab.c: syntax.ml tm_id, the identity of a closed term (a name id) */
uint32_t mge_tm_id(MgElab *e, MgTm *m);
/* megalodon.ml megaauto_set_item (h a primitive, parameter or definition;
 * d: a definition) and megaauto_set_known (h a known proposition), Egal
 * only. */
void mge_megaauto_item(MgElab *e, uint32_t h, bool d);
void mge_megaauto_known(MgElab *e, uint32_t h);

/* mg_proof.c, for typed megaauto (mg_type_megaauto.c): conversion with
 * the signature's definitions, a definition at a head and its unfolding,
 * and the shifts of proofs and hypotheses */
bool mge_conv(MgElab *e, MgTm *m, MgTm *n);
bool mge_defp(MgElab *e, MgTm *h);
MgTm *mge_delta_exp(MgElab *e, MgTm *h, MgTm **args, uint32_t n);
MgPf *mge_pfshift(MgElab *e, uint32_t i, int32_t j, MgPf *d);
MgPf *mge_pftmshift(MgElab *e, uint32_t i, int32_t j, MgPf *d);
const PfCx *mge_pfcx_shift(MgElab *e, const PfCx *hyps);

/* mg_type_megaauto.c: megaauto.ml type_megaauto, the search behind
 * ?t:SALT?; raises RAISE_SEARCHLIMIT or RAISE_SEARCHBACKTRACK when the
 * salt finds no proof */
MgPf *mge_type_megaauto(MgElab *e, int lm, MgHash salt, const TpCx *cx, const PfCx *hyps, MgTm *g);

/* mg_proof.c, for native aby (mg_aby.c) */
const PTm *mge_delta_of(MgElab *e, uint32_t c);
MgTm *mge_tm_subst0(MgElab *e, MgTm *body, MgTm *arg);
MgTm *mge_tm_tpsubst(MgElab *e, MgTm *m, MgTp *const *types, uint32_t n);
/* The proof and proposition a name denotes as a proof in g's context (a
 * hypothesis, or a known applied to the section's variables); false when
 * it denotes none alone (a polymorphic known wants its types). */
bool mge_aby_fact(MgElab *e, const char *name, const PfSt *g, MgPf **pf, MgTm **prop);
/* The double check of a proof of g's claim in g's context, without rows
 * and without recording definitions unfolded. */
bool mge_check_pf_goal(MgElab *e, const PfSt *g, MgPf *d);

/* mg_proof.c */
const PfSt *mge_pf_goal(MgElab *e, MgTm *claim, const Cx *ctxtm, const Hy *ctxpf, const PfSt *next);
void mge_proof_begin(MgElab *e, MgTm *atm);
void mge_eval_pftac(MgElab *e, const MgSourceItem *it);

#define K(p) ((uint64_t)(uintptr_t)(p))
#define V(p) ((uint64_t)(uintptr_t)(p))
#define P(T, v) ((T *)(uintptr_t)(v))
#define PERM(e, T) ((T *)mge_chunk_alloc((e), &(e)->perm, sizeof(T)))
#define TMP(e, T) ((T *)mge_chunk_alloc((e), &(e)->tmp, sizeof(T)))

__attribute__((noreturn)) void mge_raise_kind(MgElab *e, int kind);
__attribute__((noreturn)) void mge_nomem(MgElab *e);
__attribute__((noreturn, format(printf, 2, 3)))
void mge_fail(MgElab *e, const char *fmt, ...);
__attribute__((noreturn)) void mge_not_found(MgElab *e);
void *mge_chunk_alloc(MgElab *e, Chunk **head, size_t n);
bool mge_map_get(const Map *m, uint64_t key, uint64_t *val);
void mge_map_set(MgElab *e, Map *m, uint64_t key, uint64_t val);
void mge_map_clear(Map *m);
const char *mge_intern(MgElab *e, const char *s);
void mge_stk_push(MgElab *e, Map *m, uint64_t key, uint64_t val);
void mge_stk_pop(MgElab *e, Map *m, uint64_t key);
bool mge_stk_top(const Map *m, uint64_t key, uint64_t *val);
MgTm *mge_ck(MgElab *e, MgTm *m);
MgTp *mge_ckt(MgElab *e, MgTp *a);
MgTp *mge_ty_prop(MgElab *e);
MgTp *mge_ty_set(MgElab *e);
MgTp *mge_ty_var(MgElab *e, uint32_t i);
MgTp *mge_ty_arr(MgElab *e, MgTp *a, MgTp *b);
MgTm *mge_tm_db(MgElab *e, uint32_t i);
MgTm *mge_tm_prim(MgElab *e, uint32_t i);
MgTm *mge_tm_ap(MgElab *e, MgTm *f, MgTm *x);
MgTm *mge_tm_lam(MgElab *e, MgTp *a, MgTm *b);
MgTm *mge_tm_imp(MgElab *e, MgTm *p, MgTm *q);
MgTm *mge_tm_all(MgElab *e, MgTp *a, MgTm *b);
MgTm *mge_tm_tap(MgElab *e, MgTm *f, MgTp *a);
uint32_t mge_name_id(MgElab *e, const char *text);
const char *mge_name_text(MgElab *e, uint32_t id);
MgTm *mge_tm_h(MgElab *e, uint32_t id);
MgTm *mge_tmshift(MgElab *e, uint32_t i, int32_t j, MgTm *m);
MgTm *mge_shift1(MgElab *e, MgTm *m);
MgTp *mge_tpsubst(MgElab *e, MgTp *a, MgTp *const *cl, uint32_t n);
bool mge_notpvarsp(const MgTp *a);
MgTm *mge_gen_lam_body(MgElab *e, MgTm *m);
const char *mge_tp_str(MgElab *e, const MgTp *a);
const MgL *mge_S(const MgL *l);
bool mge_is_name(const MgL *l, const char *x);
int mge_tp_position(MgElab *e, const char *x);
bool mge_tmtplookup(MgElab *e, const Cx *cx, const char *x, MgTm **tm, MgTp **tp);
uint32_t mge_cx_vars(const Cx *cx);
uint32_t mge_cx_len(const Cx *cx);
const Cx *mge_cx_push(MgElab *e, const Cx *next, const char *name, MgTp *tp, MgTm *def, bool perm);
MgTm *mge_sig_apply(MgElab *e, MgTm *m, const App *app, const Cx *cxtm);
MgTm *mge_sig_lookup(MgElab *e, const char *x, const Cx *cxtm);
bool mge_poly_get(MgElab *e, const char *x, uint32_t arity, MgTp **tp);
bool mge_sigtmof_get(MgElab *e, uint32_t h, uint32_t *levels, MgTp **tp);
MgTp *mge_extr_tpoftm(MgElab *e, const TpCx *extra, const Cx *cxtm, MgTm *m);
MgTp *mge_extract_tp(MgElab *e, const MgL *a);
TmTp mge_tt(MgTm *m, MgTp *a);
int mge_try_run(MgElab *e, void (*body)(MgElab *, void *), void *ctx);
TmTp mge_extract_tm_r(MgElab *e, const MgL *a0, const Cx *cxtm);
MgTm *mge_check_tm_r(MgElab *e, const MgL *a, MgTp *mtp, const Cx *cxtm);
MgTm *mge_metavar_subst(MgElab *e, uint32_t cxn, MgTm *m);
MgTp *mge_kernel_tp(MgElab *e, MgTp *a, uint32_t levels);
MgTm *mge_kernel_tm(MgElab *e, MgTm *m, uint32_t levels);
void mge_check_init(MgElab *e, MgCheck *c);
MgTm *mge_beta_eta_norm(MgElab *e, MgTm *m);
__attribute__((noreturn)) void mge_kernel_refused(MgElab *e);
MgTp *mge_poly_type(MgElab *e, MgTp *a, uint32_t levels);
MgTm *mge_poly_term(MgElab *e, MgTm *m, uint32_t levels, bool lam);
void mge_row_out(MgElab *e);
void mge_row_begin(MgElab *e);
void mge_undo_ctx(MgElab *e);
void mge_carry_pf(MgElab *e, const char *x, uint32_t known);
void mge_sigpf_add(MgElab *e, const char *x, uint32_t known, const App *app);
void mge_declare_pf(MgElab *e, const char *x, uint32_t h);
bool mge_used(MgElab *e, const char *x);
void mge_pushpolypf(MgElab *e, const char *x, uint32_t i, MgTm *prop);
const App *mge_app_push(MgElab *e, const App *prev, uint8_t kind, const char *name);
void mge_tp_row(MgElab *e, MgTp *a);
void mge_tm_row(MgElab *e, MgTm *m);
void mge_activate_special_knowns(MgElab *e, uint32_t h);

#endif
