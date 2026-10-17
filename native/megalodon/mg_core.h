#ifndef CETTA_MEGALODON_CORE_H
#define CETTA_MEGALODON_CORE_H

/*
 * Native Megalodon kernel: the Mathdata proof kernel of upstream
 * megalodon/src/mathdata.ml, restricted to the protected source fragment.
 *
 * References (Lean, lean/Mettapedia/Languages/Megalodon/):
 *   MathdataKernel.lean     Tp, Tm, Pf, shift/instantiate/drop, normalize,
 *                           deltaNormalize, inferTerm, checkProposition,
 *                           inferProof, checkProof
 *   ProofJudgment.lean      the eight context-indexed rules (Proves)
 *   BorrowedProofCursor.lean / RuleObservation.lean
 *                           local requirements and refusal vs exhaustion
 *   SourceScopeInvariant.lean, SourceProofInvariant.lean, SourceRuleProof.lean,
 *   SourceCursor.lean       Scoped / Protected / ContextSafe / WitnessSafe
 *   NativeAdmission.lean, SourceAdmission.lean
 *                           ordered fresh-name admission under an authority
 *   NativeSession.lean, ObservedCursor.lean
 *                           resumable sessions, statuses, publication
 *   CursorEnvironment.lean  footprints and revalidation
 *   NativePublication.lean  the commit transaction
 * docs/megalodon_native.md maps every operation to its rule and tests.
 *
 * Representation invariant.  Every type, term, proof, list and context of a
 * theory is interned in that theory's store: structurally equal objects are
 * the same pointer, so judgment equality is pointer equality established by
 * full structural comparison at interning time, never by a hash alone.
 * Objects are immutable.  Cached facts on a node (free-variable bounds,
 * Protected, normal forms) are functions of the node; caches that read the
 * environment carry the revision they were computed at and are used only
 * where no declaration they could depend on has changed (mg_ops.c).
 *
 * Names are unique in a theory lineage: a name has at most one term and one
 * known declaration ever, even after withdrawal.  A snapshot sees a
 * declaration iff added_rev <= rev < removed_rev.
 *
 * Nothing in this kernel trusts a client-supplied flag.  Scope, protection,
 * typing and every rule side condition are recomputed from the data.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── outcomes ─────────────────────────────────────────────────────────── */

typedef enum {
    MG_OK = 0,
    /* A side condition fails: the judgment does not follow. */
    MG_REFUSED,
    /* The data is not a well-formed request: bad shape, bad address, a
     * future or missing parent, a duplicate scheduled row, wrong revision. */
    MG_MALFORMED,
    /* An optional budget ran out: undecided, not false. */
    MG_EXHAUSTED,
    /* Cooperative interruption: undecided. */
    MG_INTERRUPTED,
    /* Allocation failure: undecided, nothing published. */
    MG_NOMEM,
    /* Finite machine capacity (traversal depth, identity space). */
    MG_CAPACITY,
    /* A session stopped before its submitted batch finished. */
    MG_UNFINISHED,
    /* Publication: current permission refused. */
    MG_DENIED,
    /* Publication: the live theory changed since the proposal. */
    MG_CONFLICT,
} MgStatus;

const char *mg_status_name(MgStatus status);

static inline bool mg_status_undecided(MgStatus status) {
    return status == MG_EXHAUSTED || status == MG_INTERRUPTED ||
           status == MG_NOMEM || status == MG_CAPACITY;
}

/* ── budgets ──────────────────────────────────────────────────────────── */

/* Explicit accounting.  One work unit is charged per syntax node visited or
 * built by substitution, shifting, normalization, definition expansion,
 * type inference and comparison, and per proof row or proof node examined.
 * A limit of zero means unlimited: there is no default cutoff.  The
 * interruption hook is polled every MG_POLL_INTERVAL units and once per
 * row. */
#define MG_POLL_INTERVAL 4096u

typedef struct MgBudget {
    uint64_t work_limit;
    uint64_t work_used;
    uint32_t poll_countdown;
    bool (*interrupted)(void *context);
    void *interrupt_context;
    /* Sticky stop: the first exhaustion, interruption or capacity outcome. */
    MgStatus stop;
} MgBudget;

void mg_budget_init(MgBudget *budget, uint64_t work_limit);
/* Charge `units`; false (with budget->stop set) when the work must stop. */
bool mg_budget_charge(MgBudget *budget, uint64_t units);
bool mg_budget_poll(MgBudget *budget);

/* ── syntax ───────────────────────────────────────────────────────────── */

typedef enum {
    MG_TP_VAR = 1,
    MG_TP_PROP,
    MG_TP_BASE,
    MG_TP_ARR,
    MG_TP_ALL,
} MgTpTag;

typedef struct MgTp MgTp;
struct MgTp {
    uint8_t tag;
    bool plain;        /* no MG_TP_ALL anywhere (Tp.plainWellFormed shape) */
    uint32_t n;        /* VAR index, BASE index */
    MgTp *a, *b;       /* ARR a b ; ALL a */
    uint32_t hash;
    uint32_t ftv;      /* 1 + greatest free type-variable index, 0 if none */
    uint64_t tvmask;   /* free type-variable indices below 64 */
    uint32_t height;
    MgTp *next;        /* intern chain */
    MgTp *shift1;      /* Tp.shift 0 1 cache */
};

typedef enum {
    MG_TM_DB = 1,
    MG_TM_NAME,
    MG_TM_PRIM,
    MG_TM_AP,
    MG_TM_LAM,
    MG_TM_IMP,
    MG_TM_ALL,
    MG_TM_TAP,    /* Tm.typeApp  / TTpAp  */
    MG_TM_TLAM,   /* Tm.typeLam  / TTpLam */
    MG_TM_TALL,   /* Tm.typeAll  / TTpAll */
} MgTmTag;

/* Node facts. */
#define MG_TM_PROTECTED   0x01u  /* SourceScopeInvariant.Protected */
#define MG_TM_HAS_NAME    0x02u  /* some MG_TM_NAME below (delta-relevant) */
#define MG_TM_HAS_TYPEREG 0x04u  /* some TAP/TLAM/TALL below */
/* Memoized answer of mg_tm_free_db(m, 0) (eta asks it of every candidate
 * body); written under the theory lock. */
#define MG_TM_FREE0_KNOWN 0x08u
#define MG_TM_FREE0       0x10u

typedef struct MgTm MgTm;
struct MgTm {
    uint8_t tag;
    uint8_t facts;
    uint32_t n;        /* DB index, PRIM index, NAME id */
    MgTm *f, *x;       /* AP f x; IMP f x; LAM/ALL/TLAM/TALL body = f; TAP f */
    MgTp *tp;          /* LAM, ALL, TAP */
    uint32_t hash;
    uint32_t fv;       /* 1 + greatest free term index (Scoped d iff fv <= d) */
    uint32_t ftv;      /* 1 + greatest free type-variable index */
    uint64_t dbmask;   /* free term indices below 64 */
    uint64_t tvmask;   /* free type-variable indices below 64 */
    uint32_t height;
    MgTm *next;        /* intern chain */
    /* Environment-independent caches. */
    MgTm *nf;          /* beta-eta normal form */
    MgTm *shift1;      /* Tm.shift 0 1 */
    /* Environment-dependent cache: delta-normal form computed at dnf_rev. */
    MgTm *dnf;
    uint64_t dnf_rev;
};

typedef enum {
    MG_PF_GPA = 1,
    MG_PF_HYP,
    MG_PF_KNOWN,
    MG_PF_TMAP,   /* Pf.termApp  proof term   */
    MG_PF_PFAP,   /* Pf.proofApp proof proof  */
    MG_PF_PLAM,   /* Pf.proofLam term proof   */
    MG_PF_TLAM,   /* Pf.termLam  type proof   */
    MG_PF_TPAP,   /* Pf.typeApp  proof type   */
    MG_PF_TPLAM,  /* Pf.typeLam  proof        */
} MgPfTag;

typedef struct MgPf MgPf;
struct MgPf {
    uint8_t tag;
    bool payload_protected;  /* SourceScopeInvariant.ProofPayload */
    uint32_t n;              /* HYP index, KNOWN / GPA name id */
    MgPf *p, *q;             /* function/body, argument */
    MgTm *t;                 /* TMAP argument, PLAM proposition */
    MgTp *tp;                /* TLAM type, TPAP type */
    uint32_t hash;
    uint32_t height;
    uint64_t size;           /* tree size (saturating) */
    MgPf *next;
};

/* Interned cons lists, sharing tails. */
typedef struct MgTpList MgTpList;
struct MgTpList {
    MgTp *head;
    MgTpList *tail;
    uint32_t len;
    uint32_t hash;
    MgTpList *next;
    MgTpList *shift1;    /* map (Tp.shift 0 1) */
};

typedef struct MgTmList MgTmList;
struct MgTmList {
    MgTm *head;
    MgTmList *tail;
    uint32_t len;
    uint32_t hash;
    uint32_t fv;         /* max fv over elements */
    bool protected_all;
    MgTmList *next;
    MgTmList *shift1;    /* map (Tm.shift 0 1) */
};

/* ProofJudgment.Context: type depth, ordered term types, ordered
 * hypotheses (innermost first, as Lean's lists). */
typedef struct MgCtx MgCtx;
struct MgCtx {
    uint32_t depth;
    MgTpList *terms;     /* NULL = [] */
    MgTmList *hyps;      /* NULL = [] */
    uint32_t hash;
    bool safe;           /* SourceProofInvariant.ContextSafe, computed */
    /* Cache of mg_ctx_formed: every hypothesis was a proposition at
     * formed_rev (true at later revisions until a withdrawal). */
    bool formed;
    uint64_t formed_rev;
    MgCtx *next;
};

static inline uint32_t mg_tplist_len(const MgTpList *l) { return l ? l->len : 0u; }
static inline uint32_t mg_tmlist_len(const MgTmList *l) { return l ? l->len : 0u; }

typedef struct {
    MgCtx *ctx;
    MgTm *prop;
} MgJudgment;

static inline bool mg_judgment_eq(MgJudgment a, MgJudgment b) {
    return a.ctx == b.ctx && a.prop == b.prop;
}

/* ── store ────────────────────────────────────────────────────────────── */

typedef struct MgChunk MgChunk;

typedef struct {
    void **slots;
    uint32_t cap;        /* power of two */
    uint32_t len;
} MgInternTable;

typedef struct MgName MgName;
typedef struct MgDecl MgDecl;
typedef struct MgEvidence MgEvidence;   /* mg_library.h */

/* Direct-name footprint: (namespace, name) pairs consulted, sorted. */
typedef struct {
    uint64_t *items;     /* (name << 1) | ns */
    uint32_t len, cap;
    bool nomem;
} MgFootprint;

typedef struct {
    MgChunk *chunks;
    size_t bytes;            /* bytes allocated from the system */
    uint64_t nodes;          /* interned nodes */
    MgInternTable tps, tms, pfs, tplists, tmlists, ctxs;
    MgName **names;          /* by id; id 0 unused */
    uint32_t name_len, name_cap;
    MgInternTable name_index;
    MgTp *tp_prop;
    MgCtx *ctx_empty;        /* <0, [], []> */
    /* Allocation or table-capacity failures so far.  A decoder that gets
     * NULL from a constructor compares this counter to tell a resource
     * failure (undecided) from bad data (malformed). */
    uint64_t resource_failures;
    /* Decoding stopped at the thread's depth guard (a capacity outcome). */
    uint64_t capacity_failures;
    /* substitution memo (instantiate at 0) */
    void *subst_memo;
} MgStore;

/* ── declarations and theories ────────────────────────────────────────── */

typedef enum {
    MG_NS_TERM = 0,
    MG_NS_KNOWN = 1,
} MgNamespace;

typedef enum {
    MG_DECL_PARAMETER = 1,
    MG_DECL_DEFINITION,
    MG_DECL_AXIOM,       /* known admitted under the selected authority */
    MG_DECL_THEOREM,     /* known admitted from a checked proof */
} MgDeclKind;

struct MgDecl {
    uint32_t name;
    uint8_t ns;
    uint8_t kind;
    uint64_t seq;            /* admission order */
    uint64_t added_rev;
    uint64_t removed_rev;    /* UINT64_MAX while live */
    MgTp *type;              /* term declarations */
    MgTm *def;               /* definitions (beta-eta normal) */
    MgTm *source_def;        /* the source body when an import normalized it
                              * (display metadata, no authority) */
    MgTm *prop;              /* knowns */
    /* Cache: the normal form of prop, computed at snapshot nf_rev (the
     * known rule's conclusion); reused while no change revision of the
     * theory separates nf_rev from the checking snapshot. */
    MgTm *nf_prop;
    uint64_t nf_rev;
    /* Names this declaration's admission consulted directly; withdrawal
     * of any of them withdraws this declaration too. */
    MgFootprint deps;
    /* Theorem evidence: the checked proof tree (store-interned), or rows
     * owned by the declaration (published from an LCF or session value). */
    MgPf *proof;
    MgEvidence *evidence;
    /* Provenance (display/source metadata, no authority). */
    char *source_name;
    char *origin;
    uint64_t line;
};

struct MgName {
    uint32_t id;
    uint32_t len;
    uint32_t hash;
    MgDecl *decl[2];         /* by namespace; at most one each, ever */
    /* A definition realizing the parameter decl[MG_NS_TERM]: admitted only
     * when the name is the content identity of the body (mg_hash.h), so the
     * parameter always denoted exactly this object.  Lookups at and after
     * its revision see the definition. */
    MgDecl *realization;
    char text[];
};

typedef enum {
    MG_PROFILE_HF = 0,
    MG_PROFILE_EGAL,
    MG_PROFILE_MIZAR,
    MG_PROFILE_HOAS,
    MG_PROFILE_SETMM,
    MG_PROFILE_CUSTOM,
    MG_PROFILE_COUNT,
} MgProfileTag;

const char *mg_profile_name(MgProfileTag tag);
bool mg_profile_from_name(const char *name, MgProfileTag *out);

/* A selected import association: a parameter (name, type) or a known
 * (name, proposition) that this theory's authority may admit. */
typedef struct {
    uint32_t name;
    uint8_t ns;
    MgTp *type;
    MgTm *prop;
    bool admitted;
} MgAssociation;

typedef struct MgTheory MgTheory;
struct MgTheory {
    atomic_size_t holders;
    pthread_mutex_t lock;
    uint64_t identity;          /* process-unique, never displayed */
    MgProfileTag profile;
    char *revision_label;       /* selected theory revision (display) */
    MgStore store;
    /* Primitive inventory: types by Prim index; NULL = not a primitive. */
    MgTp **prims;
    uint32_t prim_len;
    char **prim_names;          /* source names of the primitives */
    /* Selected authority (NativeAdmission.Authority). */
    MgAssociation *assoc;
    uint32_t assoc_len, assoc_cap;
    /* Admission log. */
    MgDecl **decls;
    uint64_t decl_len, decl_cap;
    uint64_t rev;               /* live revision */
    /* Revisions at which a visible declaration's meaning changed (a
     * withdrawal, or a realization); env-dependent caches never cross one. */
    uint64_t *withdrawals;
    uint32_t withdrawal_len, withdrawal_cap;
    /* Reserved names (profile: SetMM wi/wal). */
    const char *const *reserved;
    uint32_t reserved_len;
    /* Current publication permission, checked at every commit. */
    bool publication_permitted;
    /* Decide theorem items by full normal forms (the reference algorithm)
     * instead of lazy conversion; both give the same verdicts. */
    bool reference_conversion;
    /* Teardown: no operation may use the theory once closed. */
    bool closed;
};

/* A snapshot: immutable view of a theory at a revision. */
typedef struct {
    MgTheory *theory;
    uint64_t rev;
} MgEnv;

/* ── checking context ─────────────────────────────────────────────────── */

typedef struct {
    MgEnv env;
    MgBudget *budget;
    MgFootprint *footprint;   /* optional */
    uint32_t depth;           /* recursion guard */
    uint32_t depth_limit;
    MgStatus status;          /* first non-OK outcome */
    const char *reason;       /* static text for refusals */
    uint64_t betas;           /* statistics */
    uint64_t deltas;
} MgCheck;

/* The recursion guard of checks started on a thread: reaching it is
 * MG_CAPACITY, never a refusal.  Big-stack workers (mg_worker.h) raise it
 * for their thread; other threads keep a default sized for an ordinary
 * 8 MiB stack. */
#define MG_DEFAULT_DEPTH_LIMIT 20000u
void mg_set_thread_depth_limit(uint32_t limit);
uint32_t mg_thread_depth_limit(void);

void mg_check_init(MgCheck *check, MgEnv env, MgBudget *budget,
                   MgFootprint *footprint);
/* Record a non-OK outcome (the first one wins) and return false. */
bool mg_fail(MgCheck *check, MgStatus status, const char *reason);

/* ── allocation hooks (tests inject failure) ─────────────────────────── */

void *mg_sys_alloc(size_t size);
void *mg_sys_realloc(void *pointer, size_t size);
void mg_sys_free(void *pointer);
#ifdef MG_TEST_HOOKS
void mg_test_fail_allocation_after(uint64_t successful);
uint64_t mg_test_allocation_count(void);
#endif

/* ── store operations (mg_store.c) ────────────────────────────────────── */

bool mg_store_init(MgStore *store);
void mg_store_free(MgStore *store);
void *mg_store_alloc(MgStore *store, size_t size);

MgTp *mg_tp_var(MgStore *s, uint32_t n);
MgTp *mg_tp_prop(MgStore *s);
MgTp *mg_tp_base(MgStore *s, uint32_t n);
MgTp *mg_tp_arr(MgStore *s, MgTp *a, MgTp *b);
MgTp *mg_tp_all(MgStore *s, MgTp *a);

MgTm *mg_tm_db(MgStore *s, uint32_t n);
MgTm *mg_tm_name(MgStore *s, uint32_t name);
MgTm *mg_tm_prim(MgStore *s, uint32_t n);
MgTm *mg_tm_ap(MgStore *s, MgTm *f, MgTm *x);
MgTm *mg_tm_lam(MgStore *s, MgTp *a, MgTm *body);
MgTm *mg_tm_imp(MgStore *s, MgTm *p, MgTm *q);
MgTm *mg_tm_all(MgStore *s, MgTp *a, MgTm *body);
MgTm *mg_tm_tap(MgStore *s, MgTm *f, MgTp *a);
MgTm *mg_tm_tlam(MgStore *s, MgTm *body);
MgTm *mg_tm_tall(MgStore *s, MgTm *body);

MgPf *mg_pf_gpa(MgStore *s, uint32_t name);
MgPf *mg_pf_hyp(MgStore *s, uint32_t n);
MgPf *mg_pf_known(MgStore *s, uint32_t name);
MgPf *mg_pf_tmap(MgStore *s, MgPf *p, MgTm *t);
MgPf *mg_pf_pfap(MgStore *s, MgPf *p, MgPf *q);
MgPf *mg_pf_plam(MgStore *s, MgTm *t, MgPf *p);
MgPf *mg_pf_tlam(MgStore *s, MgTp *a, MgPf *p);
MgPf *mg_pf_tpap(MgStore *s, MgPf *p, MgTp *a);
MgPf *mg_pf_tplam(MgStore *s, MgPf *p);

MgTpList *mg_tplist_cons(MgStore *s, MgTp *head, MgTpList *tail);
MgTmList *mg_tmlist_cons(MgStore *s, MgTm *head, MgTmList *tail);
MgCtx *mg_ctx(MgStore *s, uint32_t depth, MgTpList *terms, MgTmList *hyps);

/* Names: interning text never declares anything. 0 on failure. */
uint32_t mg_name_intern(MgStore *s, const char *text, size_t len);
const MgName *mg_name_get(const MgStore *s, uint32_t id);

static inline bool mg_tm_protected(const MgTm *m) {
    return (m->facts & MG_TM_PROTECTED) != 0u;
}
/* Exact: term index k occurs free in m. */
bool mg_tm_free_db(const MgTm *m, uint32_t k);
/* Exact: type variable k occurs free in m / a. */
bool mg_tm_free_tv(const MgTm *m, uint32_t k);
bool mg_tp_free_tv(const MgTp *a, uint32_t k);

static inline bool mg_tp_plain_wf(const MgTp *a, uint32_t depth) {
    return a->plain && a->ftv <= depth;
}
bool mg_tp_poly_wf(const MgTp *a, uint32_t depth);

/* ── operations (mg_ops.c) ────────────────────────────────────────────── */

MgTp *mg_tp_shift(MgCheck *c, MgTp *a, uint32_t cutoff, int32_t amount);
MgTp *mg_tp_instantiate(MgCheck *c, MgTp *a, uint32_t depth, MgTp *b);
MgTm *mg_tm_shift(MgCheck *c, MgTm *m, uint32_t cutoff, int32_t amount);
MgTm *mg_tm_tshift(MgCheck *c, MgTm *m, uint32_t cutoff, int32_t amount);
/* Tm.instantiateAt depth replacement body (upstream tmsubst on Protected). */
MgTm *mg_tm_instantiate(MgCheck *c, MgTm *body, uint32_t depth, MgTm *n);
/* Tm.typeInstantiateAt depth replacement body (upstream tmtpsubst). */
MgTm *mg_tm_tinstantiate(MgCheck *c, MgTm *body, uint32_t depth, MgTp *b);

MgTpList *mg_tplist_shift1(MgCheck *c, MgTpList *l);
MgTmList *mg_tmlist_shift1(MgCheck *c, MgTmList *l);
MgTmList *mg_tmlist_unshift1(MgCheck *c, MgTmList *l);

/* Beta-eta normal form (Tm.normalize without fuel; budget-charged). */
MgTm *mg_tm_nf(MgCheck *c, MgTm *m);
/* deltaNormalize: every defined visible name expanded. */
MgTm *mg_tm_delta(MgCheck *c, MgTm *m);
/* MathdataKernel.normalize = nf (delta m). */
MgTm *mg_tm_normalize(MgCheck *c, MgTm *m);
/* Upstream tm_norm_p: beta-eta normal (definitions allowed). */
bool mg_tm_is_beta_eta_normal(MgCheck *c, MgTm *m);

/* inferTerm: NULL with c->status REFUSED when ill-typed. */
MgTp *mg_infer(MgCheck *c, uint32_t depth, MgTpList *terms, MgTm *m);
/* checkProposition (prefix type-universals allowed). */
bool mg_check_proposition(MgCheck *c, uint32_t depth, MgTpList *terms,
                          MgTm *m);

/* Environment lookups (record the footprint). */
const MgDecl *mg_env_lookup(MgCheck *c, uint32_t name, MgNamespace ns);
const MgDecl *mg_env_lookup_quiet(MgEnv env, uint32_t name, MgNamespace ns);
bool mg_env_decl_visible(MgEnv env, const MgDecl *decl);

/* ── lazy conversion (mg_conv.c) ──────────────────────────────────────── */

/* *equal iff normalize(a) == normalize(b), decided lazily (an optimization
 * with the reference verdict; see mg_conv.c).  Undecided outcomes are
 * returned as such. */
MgStatus mg_conv(MgCheck *c, MgTm *a, MgTm *b, bool *equal);
/* Beta (both sorts) and delta at the head only. */
MgTm *mg_whnf(MgCheck *c, MgTm *m);
/* inferProof with propositions kept unnormalized: *prop is convertible with
 * the reference result, and refusals coincide. */
MgStatus mg_infer_proof_lazy(MgCheck *c, MgCtx *ctx, MgPf *proof, MgTm **prop);

/* Lazy judgments are formed.  Every term the lazy rules convert is typed
 * first (a context entering from data has proposition hypotheses, a stated
 * proposition or operand is a proposition), so conversion only meets
 * well-typed terms: it terminates, and the reference normal forms it stands
 * for exist.  mg_ctx_formed: every hypothesis is a proposition of the term
 * context (cached on the context, see MgCtx.formed). */
bool mg_ctx_formed(MgCheck *c, MgCtx *ctx);
/* A proposition as the rules derive one: type quantifiers only in
 * proposition positions (the prefix, implication sides, quantifier bodies;
 * a termLam over a polymorphic known gives forall x. TAll ...), every
 * other part typed by inferTerm.  Stated propositions and operands of lazy
 * steps and rows are held to it before they are converted. */
bool mg_prop_formed(MgCheck *c, uint32_t depth, MgTpList *terms, MgTm *m);
/* Contexts up to conversion: the same type depth and term context and
 * pairwise convertible hypotheses (the reference compares normal forms). */
MgStatus mg_ctx_conv(MgCheck *c, MgCtx *a, MgCtx *b, bool *same);

/* ── rules (mg_rules.c) ───────────────────────────────────────────────── */

typedef enum {
    MG_RULE_HYP = 1,
    MG_RULE_KNOWN,
    MG_RULE_TERM_APP,
    MG_RULE_PROOF_APP,
    MG_RULE_TERM_LAM,
    MG_RULE_PROOF_LAM,
    MG_RULE_TYPE_APP,
    MG_RULE_TYPE_LAM,
} MgRule;

const char *mg_rule_name(MgRule rule);

/* BorrowedProofCursor.Witness. */
typedef struct {
    MgRule rule;
    uint32_t index;    /* HYP */
    uint32_t name;     /* KNOWN */
    MgTm *arg;         /* TERM_APP argument */
    MgTp *type;        /* TERM_APP domain, TYPE_APP type */
    MgTm *body;        /* TERM_APP body, TYPE_APP body */
    MgTm *domain;      /* PROOF_APP domain */
    MgTm *source;      /* PROOF_LAM source */
} MgWitness;

/* SourceCursor.JudgmentSafe / WitnessSafe. */
bool mg_judgment_safe(MgJudgment j);
bool mg_witness_safe(const MgWitness *w);

/* RuleObservation.observe: the exact premises a local rule requires for a
 * stated conclusion.  OK with *count premises, REFUSED, or an undecided
 * outcome.  This is the single checked core: rows, proof trees and LCF
 * steps all end here. */
MgStatus mg_requirements(MgCheck *c, const MgWitness *w, MgJudgment concl,
                         MgJudgment premises[2], uint32_t *count);

/* Forward helpers (LCF and tree inference) propose a conclusion; callers
 * confirm it with mg_requirements against the actual premises. The witness
 * is mutable: omitted operands are filled from those premises. Callers
 * retaining an input witness must pass their own copy. */
MgStatus mg_conclude(MgCheck *c, MgWitness *w, MgCtx *ctx,
                     const MgJudgment *premises, uint32_t count,
                     MgJudgment *out);

/* One checked step: propose with mg_conclude, confirm with mg_requirements
 * that the actual premises are exactly the required ones, in order. */
MgStatus mg_step(MgCheck *c, MgWitness *w, MgCtx *ctx,
                 const MgJudgment *premises, uint32_t count,
                 MgJudgment *out);

/* One checked step under lazy conversion (mg_conv.c).  The conclusion is
 * kept in source form -- convertible to the reference rule's conclusion,
 * whose normal form need never be built -- and every side condition is
 * decided up to beta-eta-delta conversion, contexts hypothesis-wise: a step
 * on formed premises is accepted exactly when the reference rule accepts
 * their normal forms (tests/megalodon/test_mg_lazy.c holds it to that).
 * Stated implied operands (a termApp or typeApp body, a proofApp domain)
 * must be formed and are held to the rule up to conversion; omitted ones
 * are filled from the premises; arguments and discharged sources are used
 * as given, as the reference does.  A context entering by hyp or known
 * must be formed. */
MgStatus mg_step_lazy(MgCheck *c, MgWitness *w, MgCtx *ctx,
                      const MgJudgment *premises, uint32_t count,
                      MgJudgment *out);

/* inferProof over a proof tree (shared subproofs memoized per context). */
MgStatus mg_infer_proof(MgCheck *c, MgCtx *ctx, MgPf *proof, MgJudgment *out);

/* The same check, recording each distinct (proof node, context) step as a
 * row once its rule succeeded, premises first.  row() returns the row's
 * address, or UINT64_MAX when it could not store it (then NOMEM). */
typedef struct {
    void *context;
    uint64_t (*row)(void *context, MgJudgment conclusion, const MgWitness *w,
                    const uint64_t *parents, uint32_t count);
} MgRowRecorder;
MgStatus mg_infer_proof_recorded(MgCheck *c, MgCtx *ctx, MgPf *proof,
                                 MgRowRecorder *recorder, MgJudgment *out,
                                 uint64_t *root);
/* mg_infer_proof_lazy, recording each distinct (proof node, context) step
 * as a row in source form (evidence for a lazy session, mg_conv.c). */
MgStatus mg_infer_proof_lazy_recorded(MgCheck *c, MgCtx *ctx, MgPf *proof,
                                      MgRowRecorder *recorder, MgTm **prop,
                                      uint64_t *root);

/* ── theory (mg_theory.c) ─────────────────────────────────────────────── */

MgTheory *mg_theory_new(MgProfileTag profile, const char *revision_label,
                        MgStatus *status);
void mg_theory_retain(MgTheory *t);
void mg_theory_release(MgTheory *t);
MgEnv mg_theory_snapshot(MgTheory *t);   /* caller holds t->lock */

/* Authority associations (NativeAdmission.Authority). */
MgStatus mg_theory_associate_parameter(MgTheory *t, uint32_t name, MgTp *type);
MgStatus mg_theory_associate_known(MgTheory *t, uint32_t name, MgTm *prop);

/* Admission items (NativeAdmission.Item); all keep the live state
 * unchanged on any non-OK outcome. */
typedef enum {
    MG_ITEM_PARAMETER = 1,
    MG_ITEM_DEFINITION,
    MG_ITEM_KNOWN,
    MG_ITEM_THEOREM,
    MG_ITEM_CONJECTURE,
} MgItemKind;

typedef struct {
    MgItemKind kind;
    uint32_t name;
    MgTp *type;
    MgTm *body;
    MgTm *prop;
    MgPf *proof;
    const char *source_name;
    const char *origin;
    uint64_t line;
} MgItem;

/* Check an item against snapshot `env` without changing the theory. */
MgStatus mg_item_check(MgCheck *c, const MgItem *item);
/* Whether a closed proof proves `claim` (already checked to be a
 * proposition), by the theory's selected conversion algorithm. */
MgStatus mg_check_theorem_proof(MgCheck *c, MgTm *claim, MgPf *proof);
/* Prepare + commit an item atomically on the live theory (caller holds the
 * lock).  `expected_rev` 0 means "the live revision". */
MgStatus mg_theory_admit(MgTheory *t, MgBudget *budget, const MgItem *item,
                         uint64_t expected_rev, const MgDecl **out);
/* Commit a checked closed theorem as a known (NativePublication.commit).
 * `evidence` (may be NULL) passes to the declaration on success and is
 * destroyed otherwise. */
MgStatus mg_theory_publish_known(MgTheory *t, uint64_t captured_rev,
                                 uint32_t name, MgTm *prop,
                                 const MgFootprint *deps, MgEvidence *evidence,
                                 const char *source_name,
                                 const char *origin, const MgDecl **out);
/* Whether `name` can still be declared in namespace `ns` (fresh in the
 * lineage and not reserved by the profile). */
bool mg_theory_name_fresh(const MgTheory *t, uint32_t name, MgNamespace ns);
/* Footprint of a proof: names of its term operands and cited knowns. */
bool mg_footprint_add_proof(MgFootprint *f, const MgPf *proof);
/* Withdrawn / live declarations count (inspection). */
uint64_t mg_theory_live_decls(const MgTheory *t, MgEnv env);
MgStatus mg_theory_withdraw(MgTheory *t, uint32_t name, MgNamespace ns);
bool mg_theory_withdrawn_between(const MgTheory *t, uint64_t after,
                                 uint64_t upto);

/* Footprints (CursorEnvironment). */
void mg_footprint_init(MgFootprint *f);
void mg_footprint_free(MgFootprint *f);
bool mg_footprint_add(MgFootprint *f, uint32_t name, MgNamespace ns);
bool mg_footprint_union(MgFootprint *dst, const MgFootprint *src);
/* Agreement of every footprint name, closed under definition bodies and
 * known propositions of the source snapshot, between two snapshots. */
bool mg_footprint_revalidate(const MgFootprint *f, MgEnv from, MgEnv to);
/* Footprint of the names in a term / witness. */
bool mg_footprint_add_term(MgFootprint *f, const MgTm *m);

#endif /* CETTA_MEGALODON_CORE_H */
