/* hol_internal.h - private representation of the native HOL Light kernel.
 *
 * Nothing in this header is part of the public contract. External clients
 * use hol_kernel.h and hol_replay.h only; the replay engine, the
 * canonical printer and the tests that inspect representation invariants
 * are the only other users of these definitions.
 *
 * Representation summary (public contract: docs/hol_light.txt):
 *  - names are interned byte strings;
 *  - types and terms are hash-consed: two live nodes are structurally equal
 *    iff they are the same pointer.  Interning compares the full shallow
 *    structure (kind, name, declaration, type, children) on every hash
 *    match, so hash collisions never identify distinct objects;
 *  - binders use HOL Light's named representation (Abs of a Var); identity
 *    substitution's observable renaming difference is documented separately;
 *  - every term caches its type, a 64-bit superset signature of its free
 *    variables and of its type-variable names, and its anchor: the deepest
 *    environment event it mentions;
 *  - theorem payloads (hypotheses, conclusion, axiom dependencies, anchor)
 *    are hash-consed too; proof events and deliveries are separate
 *    (handle slots carry the event serial). */
#ifndef HOL_INTERNAL_H
#define HOL_INTERNAL_H

#include "hol_kernel.h"

#include <stdlib.h>
#include <string.h>

#define HOL_LIKELY(x) __builtin_expect(!!(x), 1)
#define HOL_UNLIKELY(x) __builtin_expect(!!(x), 0)

typedef uint32_t HolName; /* index into the name table; 0 is invalid */

typedef struct HolType HolType;
typedef struct HolTerm HolTerm;
typedef struct HolThm HolThm;
typedef struct HolDecl HolDecl;
typedef struct HolDeps HolDeps;
typedef struct HolBranch HolBranch;

enum { HOL_TY_VAR = 0, HOL_TY_APP = 1 };
/* Term kinds follow the constructor order of fusion.ml, which is also the
 * order OCaml's polymorphic compare uses between constructors. */
enum { HOL_TM_VAR = 0, HOL_TM_CONST = 1, HOL_TM_COMB = 2, HOL_TM_ABS = 3 };

struct HolType {
    uint32_t hash;
    uint8_t kind;
    uint8_t mark;
    uint16_t flags;
    uint32_t nargs;
    uint32_t height;  /* 1 + max height of the arguments */
    HolName name;     /* type-variable name, or the operator's name */
    HolDecl *op;      /* TY_APP: the declaring event; NULL for TY_VAR */
    HolDecl *anchor;  /* deepest declaration used; NULL if none */
    uint64_t tv_sig;  /* superset signature of type-variable names */
    HolType *args[];
};

#define HOL_TMF_HAS_PARENT 0x1u
#define HOL_TMF_SHARED 0x2u

struct HolTerm {
    uint32_t hash;
    uint8_t kind;
    uint8_t mark;
    uint16_t flags;
    uint32_t stamp;  /* traversal memo stamp (single-threaded kernel) */
    uint32_t height; /* recursion bound for every traversal of this term */
    HolType *type;  /* the term's type, established at construction */
    HolDecl *anchor;
    union {
        struct { HolName name; HolDecl *decl; } leaf; /* VAR: decl NULL */
        struct { HolTerm *l, *r; } node;  /* COMB: rator,rand; ABS: var,body */
    } u;
    uint64_t fv_sig; /* superset signature of free variables */
    uint64_t tv_sig; /* superset signature of type-variable names */
};

/* Interned, sorted (by declaration serial) set of axiom events. */
struct HolDeps {
    uint32_t hash;
    uint8_t mark;
    uint32_t n;
    HolDecl *ax[];
};

struct HolThm {
    uint32_t hash;
    uint8_t mark;
    uint8_t flags;
    uint16_t pad;
    uint32_t nhyps;
    HolTerm *concl;
    HolDeps *deps;   /* NULL: no axiom used */
    HolDecl *anchor; /* deepest event the proof used */
    HolTerm *hyps[]; /* sorted by alphaorder, alpha-duplicate free */
};

enum {
    HOL_DECL_TYPE = 1,      /* new_type, and bool / fun at the root        */
    HOL_DECL_CONST = 2,     /* new_constant, and = at the root             */
    HOL_DECL_AXIOM = 3,     /* new_axiom                                   */
    HOL_DECL_DEF = 4,       /* new_basic_definition (introduces a constant) */
    HOL_DECL_TYDEF = 5,     /* new_basic_type_definition: the type          */
    HOL_DECL_TYDEF_ABS = 6, /*   ... its abstraction constant             */
    HOL_DECL_TYDEF_REP = 7, /*   ... its representation constant          */
    HOL_DECL_POLICY = 8     /* axiom-policy tightening                     */
};

enum {
    HOL_AUTH_PRIMITIVE = 1,  /* fusion.ml bootstrap: bool, fun, =          */
    HOL_AUTH_DECLARED = 2,   /* new_type / new_constant                    */
    HOL_AUTH_DEFINED = 3,    /* conservative definition                    */
    HOL_AUTH_USER_AXIOM = 4, /* new_axiom under the OPEN policy             */
    HOL_AUTH_STD_ETA = 5,    /* certified standard axioms                   */
    HOL_AUTH_STD_SELECT = 6,
    HOL_AUTH_STD_INFINITY = 7,
    HOL_AUTH_POLICY = 8
};

struct HolBranch {
    HolDecl *fork; /* parent of the branch's first event (NULL at the root) */
    HolDecl *tip;  /* last event appended to this branch */
    uint32_t refs; /* live events on this branch */
};

struct HolDecl {
    uint64_t serial;      /* creation serial, shared with proof events */
    uint32_t depth;       /* 0 for the first root event */
    uint32_t bindex;      /* position within its branch */
    HolDecl *parent;
    HolBranch *branch;
    uint8_t kind;
    uint8_t authority;
    uint8_t policy;       /* axiom policy in force after this event */
    uint8_t mark;
    uint8_t std_cert;     /* 0 unknown, 1 standard-certified, 2 not */
    uint8_t committed;    /* linked into its branch and the name tables */
    uint8_t digest_ok;    /* digest computed (on demand, hk_env_digest) */
    HolName name;         /* type or constant name (0 for axioms/policy) */
    uint32_t arity;       /* TYPE, TYDEF */
    uint32_t index;       /* position in k->decls (for sweeping) */
    HolType *gtype;       /* generic type of a constant */
    HolTerm *body;        /* DEF: right-hand side; AXIOM: statement; TYDEF: P */
    HolThm *thm;          /* AXIOM: |- p; DEF: |- c = r; REP: |- abs(rep a) = a */
    HolThm *thm2;         /* REP: |- P r = (rep(abs r) = r) */
    HolThm *witness;      /* TYDEF*, REP: the witness |- P x */
    HolDecl *next_same;   /* older declaration chain with the same name */
    uint64_t witness_event; /* TYDEF*: proof event of the witness (journal) */
    uint64_t digest;      /* history digest (cache key only), if digest_ok */
};

typedef struct {
    char *bytes;
    uint32_t len;
    uint32_t hash;
    HolDecl *types;  /* type operators with this name, newest first */
    HolDecl *consts; /* constants with this name, newest first */
} HolNameRec;

typedef struct {
    void **slot;
    size_t cap; /* power of two */
    size_t count;
} HolTable;

typedef struct {
    void *obj;
    uint64_t event;
    uint32_t gen;
    uint8_t kind; /* HolKind; HOL_KIND_NONE when free */
    uint8_t retired;
    uint16_t pad;
    uint32_t owner; /* validated registry index, not the public owner token */
    uint32_t next; /* owner list or free list (index + 1; 0 ends) */
    uint32_t prev;
} HolSlot;

typedef struct {
    uint32_t head; /* slot index + 1 */
    uint32_t generation;
    uint32_t next_free; /* owner index; zero terminates */
    uint8_t live;
} HolOwnerRec;

/* One journaled proof event (hol_export.c). */
typedef struct {
    uint64_t serial;     /* the event's own serial */
    uint8_t op;          /* HOLR opcode of the rule */
    uint8_t nprem;
    uint32_t nargs;      /* terms (or types, for INST_TYPE) */
    uint64_t prem[2];    /* premise event serials */
    HolThm *pthm[2];     /* premise payloads (disambiguate typedef pairs) */
    HolThm *result;
    void **args;
} HolJEvent;
typedef void (*HolRootFn)(HolKernel *k, void *ctx);
typedef struct { HolRootFn fn; void *ctx; } HolRootRec;

/* Generation-stamped pointer memo, cleared in O(1) per operation. */
typedef struct {
    const void *key;
    void *val;
    uint32_t ctx;
    uint32_t gen;
    uint32_t tag;
    uint32_t pad;
} HolMemoEntry;
typedef struct {
    HolMemoEntry *e;
    size_t cap;
    size_t used;
    uint32_t gen;
} HolMemo;

struct HolKernel {
    uint16_t tag;
    HolStatus status;
    HolReason reason;

    /* allocation */
    size_t bytes_live, bytes_peak, bytes_since_gc, gc_threshold;
    uint64_t allocations, fail_at;
    bool fail_sticky, failing;

    uint32_t hash_mask;
    uint32_t max_depth;

    /* names */
    HolNameRec *names;
    uint32_t nnames, capnames;
    uint32_t *name_index;
    size_t name_index_cap;

    /* hash-consed objects */
    HolTable types, terms, thms, deps;

    /* environment events */
    HolDecl **decls;
    size_t ndecls, capdecls;
    HolDecl *d_fun, *d_bool, *d_eq; /* the bare bootstrap */
    uint64_t serial;

    /* frequently used objects (always rooted) */
    HolType *bool_ty, *aty;
    HolName n_eq, n_fun, n_bool, n_A, n_a, n_r;

    /* traversal support */
    uint32_t stamp;
    uint32_t tick;
    uint64_t fuel, fuel_per_call;
    volatile int *interrupt;
    bool (*interrupt_poll)(void *);
    void *interrupt_context;
    HolMemo memo;
    uint32_t memo_ctx;
    size_t memo_bytes;

    /* handles */
    HolSlot *slots;
    uint32_t nslots, capslots, free_slot;
    HolOwnerRec *owners;
    uint32_t nowners, capowners, free_owner;
    HolOwner cur_owner;
    size_t live_refs;

    /* extra GC roots (replay engines, writers) */
    HolRootRec *roots;
    size_t nroots, caproots;
    HolThm **pins; /* theorems pinned by environment events are reached via decls */

    /* gc */
    void **markstack;
    size_t nmark, capmark;
    bool mark_failed;
    uint64_t gc_runs;

    /* proof journal (optional; hol_journal_enable) */
    bool journal_on;
    HolJEvent *jev;
    size_t njev, capjev;
    size_t journal_arg_bytes;

    /* statistics */
    uint64_t intern_hits, intern_misses;
    uint64_t renames_vsubst, renames_inst; /* binder renamings actually performed */
};

/* ---- errors ------------------------------------------------------------ */

static inline void *hk_fail(HolKernel *k, HolStatus s, HolReason r) {
    if (k->status == HOL_OK) { k->status = s; k->reason = r; }
    return NULL;
}
static inline void hk_clear(HolKernel *k) { k->status = HOL_OK; k->reason = HOL_R_NONE; }
#define HOL_ERR(k) ((k)->status != HOL_OK)

/* ---- allocation (hol_core.c) ------------------------------------------ */
void *hk_alloc(HolKernel *k, size_t n);
void *hk_realloc(HolKernel *k, void *p, size_t old_n, size_t new_n);
void hk_dealloc(HolKernel *k, void *p, size_t n);
bool hk_tick_slow(HolKernel *k);
static inline bool hk_tick(HolKernel *k) {
    if (HOL_LIKELY(--k->tick != 0)) return true;
    return hk_tick_slow(k);
}
void hk_begin_call(HolKernel *k); /* resets status, fuel */

/* ---- hashing ----------------------------------------------------------- */
static inline uint64_t hk_mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31; return x;
}
static inline uint32_t hk_hash_finish(const HolKernel *k, uint64_t h) {
    return (uint32_t)(hk_mix64(h) >> 32) & k->hash_mask;
}
static inline uint64_t hk_sig_bit(uint32_t h) { return 1ULL << ((h ^ (h >> 6) ^ (h >> 12)) & 63u); }
uint64_t hk_fnv1a(const void *data, size_t n, uint64_t h);
#define HOL_FNV_OFFSET 0xcbf29ce484222325ULL

/* ---- names -------------------------------------------------------------- */
HolName hk_name_intern(HolKernel *k, const char *s, size_t len);
static inline const HolNameRec *hk_name(const HolKernel *k, HolName n) { return &k->names[n]; }
int hk_name_cmp(const HolKernel *k, HolName a, HolName b); /* OCaml string order */
HolName hk_name_prime(HolKernel *k, HolName n);            /* n ^ "'" */

/* ---- types --------------------------------------------------------------- */
HolType *hk_ty_var(HolKernel *k, HolName name);
/* Interning constructor; checks only branch compatibility of the parts.
 * Arity and declaration checks belong to the callers (mk_type). */
HolType *hk_ty_app(HolKernel *k, HolDecl *op, HolType *const *args, uint32_t n);
HolType *hk_ty_fun(HolKernel *k, HolType *a, HolType *b);
int hk_ty_compare(const HolKernel *k, const HolType *a, const HolType *b);
static inline bool hk_ty_is_fun(const HolKernel *k, const HolType *t) {
    return t->kind == HOL_TY_APP && t->op == k->d_fun;
}

typedef struct { HolType *repl; HolType *key; } HolTyPair;
typedef struct { const HolTyPair *p; size_t n; uint64_t dom_sig; } HolTySubst;
void hk_tysubst_init(HolTySubst *s, const HolTyPair *p, size_t n);
HolType *hk_type_subst(HolKernel *k, const HolTySubst *s, HolType *ty);

/* ---- terms ---------------------------------------------------------------- */
HolTerm *hk_tm_var(HolKernel *k, HolName name, HolType *ty);
HolTerm *hk_tm_const(HolKernel *k, HolDecl *decl, HolType *ty);
HolTerm *hk_tm_comb(HolKernel *k, HolTerm *f, HolTerm *x);   /* checked */
HolTerm *hk_tm_abs(HolKernel *k, HolTerm *v, HolTerm *body); /* checked */
HolTerm *hk_tm_eq(HolKernel *k, HolTerm *l, HolTerm *r);     /* safe_mk_eq */
static inline bool hk_tm_is_eq(const HolKernel *k, const HolTerm *t) {
    return t->kind == HOL_TM_COMB && t->u.node.l->kind == HOL_TM_COMB &&
           t->u.node.l->u.node.l->kind == HOL_TM_CONST &&
           t->u.node.l->u.node.l->u.leaf.decl == k->d_eq;
}
static inline HolTerm *hk_eq_lhs(const HolTerm *t) { return t->u.node.l->u.node.r; }
static inline HolTerm *hk_eq_rhs(const HolTerm *t) { return t->u.node.r; }

int hk_tm_compare(const HolKernel *k, const HolTerm *a, const HolTerm *b);
/* alphaorder; returns 0 and sets *out, or -1 on resource error. */
int hk_alphaorder(HolKernel *k, HolTerm *a, HolTerm *b, int *out);
/* vfree_in: 1, 0, or -1 on resource error. */
int hk_vfree_in(HolKernel *k, HolTerm *v, HolTerm *tm);
/* freesin acc tm with acc given as an array. */
int hk_freesin(HolKernel *k, HolTerm *const *acc, size_t n, HolTerm *tm);
/* Exact-order frees / type_vars_in_term / tyvars into a growable array. */
typedef struct { void **v; size_t n, cap; } HolVec;
int hk_vec_push(HolKernel *k, HolVec *vec, void *p);
void hk_vec_free(HolKernel *k, HolVec *vec);
int hk_frees_exact(HolKernel *k, HolTerm *tm, HolVec *out);
int hk_tyvars_exact(HolKernel *k, HolType *ty, HolVec *out);
int hk_type_vars_in_term_exact(HolKernel *k, HolTerm *tm, HolVec *out);
HolTerm *hk_variant(HolKernel *k, HolTerm *const *avoid, size_t n, HolTerm *v);

typedef struct { HolTerm *t; HolTerm *x; } HolTmPair; /* replace x by t */
/* Full fusion.ml vsubst including the "Bad substitution list" check. */
HolTerm *hk_vsubst(HolKernel *k, const HolTmPair *p, size_t n, HolTerm *tm);
int hk_vsubst_check(HolKernel *k, const HolTmPair *p, size_t n);
HolTerm *hk_inst(HolKernel *k, const HolTyPair *p, size_t n, HolTerm *tm);

/* ---- theorems -------------------------------------------------------------- */
HolDeps *hk_deps_union(HolKernel *k, HolDeps *a, HolDeps *b);
HolDeps *hk_deps_single(HolKernel *k, HolDecl *ax);
HolThm *hk_thm_make(HolKernel *k, HolTerm *const *hyps, uint32_t nhyps,
                     HolTerm *concl, HolDeps *deps, HolDecl *anchor);
int hk_anchor_join(HolDecl *a, HolDecl *b, HolDecl **out);
bool hk_decl_le(const HolDecl *a, const HolDecl *e);

HolThm *hk_REFL(HolKernel *k, HolTerm *tm);
HolThm *hk_TRANS(HolKernel *k, HolThm *a, HolThm *b);
HolThm *hk_MK_COMB(HolKernel *k, HolThm *a, HolThm *b);
HolThm *hk_ABS(HolKernel *k, HolTerm *v, HolThm *th);
HolThm *hk_BETA(HolKernel *k, HolTerm *tm);
HolThm *hk_ASSUME(HolKernel *k, HolTerm *tm);
HolThm *hk_EQ_MP(HolKernel *k, HolThm *a, HolThm *b);
HolThm *hk_DEDUCT_ANTISYM_RULE(HolKernel *k, HolThm *a, HolThm *b);
HolThm *hk_INST_TYPE(HolKernel *k, const HolTyPair *p, size_t n, HolThm *th);
HolThm *hk_INST(HolKernel *k, const HolTmPair *p, size_t n, HolThm *th);

/* ---- environments (hol_env.c) ------------------------------------------------ */
HolDecl *hk_lookup_type(HolKernel *k, HolDecl *env, HolName name);
HolDecl *hk_lookup_const(HolKernel *k, HolDecl *env, HolName name);
HolType *hk_mk_type_in(HolKernel *k, HolDecl *env, HolName name,
                        HolType *const *args, uint32_t n);
HolTerm *hk_mk_const_in(HolKernel *k, HolDecl *env, HolName name,
                         const HolTyPair *theta, size_t n);
/* Constant at an explicit instance type (replay form); checks instance. */
HolTerm *hk_mk_const_at(HolKernel *k, HolDecl *env, HolName name, HolType *ty);
HolDecl *hk_env_new_type(HolKernel *k, HolDecl *env, HolName name, uint32_t arity);
HolDecl *hk_env_new_constant(HolKernel *k, HolDecl *env, HolName name, HolType *ty);
HolDecl *hk_env_new_axiom(HolKernel *k, HolDecl *env, HolTerm *tm, HolThm **th);
HolDecl *hk_env_new_definition(HolKernel *k, HolDecl *env, HolTerm *tm, HolThm **th);
HolDecl *hk_env_new_type_definition(HolKernel *k, HolDecl *env, HolName tyname,
                                     HolName absname, HolName repname,
                                     HolThm *witness, HolThm **th1, HolThm **th2);
HolDecl *hk_env_set_policy(HolKernel *k, HolDecl *env, int policy);
int hk_env_init(HolKernel *k);
void hk_env_free_all(HolKernel *k);
bool hk_thm_is_standard(HolKernel *k, HolThm *th);
int hk_decl_standard(HolKernel *k, HolDecl *d); /* 1 yes, 0 no, -1 error */
int hk_env_digest(HolKernel *k, HolDecl *env, uint64_t *out); /* 0, or -1 with status */

/* ---- gc and handles (hol_gc.c) ------------------------------------------------ */
void hk_mark_type(HolKernel *k, HolType *t);
void hk_mark_term(HolKernel *k, HolTerm *t);
void hk_mark_thm(HolKernel *k, HolThm *t);
void hk_mark_decl(HolKernel *k, HolDecl *d);
int hk_root_add(HolKernel *k, HolRootFn fn, void *ctx);
void hk_root_remove(HolKernel *k, HolRootFn fn, void *ctx);
void hk_maybe_gc(HolKernel *k);
HolRef hk_ref_new(HolKernel *k, HolKind kind, void *obj, uint64_t event);
void *hk_ref_get(HolKernel *k, HolRef r, HolKind kind, uint64_t *event);
void hk_handles_free(HolKernel *k);
void hk_ref_drop(HolKernel *k, HolRef ref);
HolKind hk_ref_kind(HolKernel *k, HolRef ref);
int hk_table_insert(HolKernel *k, HolTable *t, void *obj, uint32_t hash);
void hk_table_free(HolKernel *k, HolTable *t);

/* ---- proof journal (hol_export.c) ------------------------------------------------ */
void hk_journal_add(HolKernel *k, uint8_t op, uint64_t serial, HolThm *result,
                    const uint64_t *prem, HolThm *const *pthm, unsigned nprem,
                    void *const *args, uint32_t nargs);
void hk_journal_mark(HolKernel *k);
void hk_journal_free(HolKernel *k);

/* ---- printing (hol_print.c) ---------------------------------------------------- */
typedef struct {
    char *buf;
    size_t len, cap;
    bool failed;    /* allocation failed or, with k, fuel or interrupt stopped it */
    HolKernel *k;   /* allocator and fuel; NULL: plain malloc and no fuel */
    bool hash_only; /* feed fnv instead of appending */
    uint64_t fnv;
} HolStr;
void hk_str_put(HolStr *s, const char *p, size_t n);
void hk_print_name(HolStr *s, const HolNameRec *n);
int hk_print_type(HolKernel *k, HolStr *s, HolType *t);
int hk_print_term(HolKernel *k, HolStr *s, HolTerm *t);
int hk_print_thm(HolKernel *k, HolStr *s, HolThm *th);
int hk_print_thm_alpha(HolKernel *k, HolStr *s, HolThm *th);
int hk_print_term_alpha(HolKernel *k, HolStr *s, HolTerm *t);
/* Parser for the canonical syntax, names resolved in env (tests, data). */
HolType *hk_parse_type(HolKernel *k, HolDecl *env, const char *text, size_t len);
HolTerm *hk_parse_term(HolKernel *k, HolDecl *env, const char *text, size_t len);

#endif /* HOL_INTERNAL_H */
