/* hol_kernel.h - public interface of the native HOL Light kernel.
 *
 * Logical reference: HOL Light fusion.ml, revision
 * 924faf0c3471a04174ffdeb8576ce7cabcb92d64 (byte-identical to the fusion.ml of
 * 233b83c3202370e03c54de2bd090e4d34f677e91, SHA-256 below).  Every function
 * documented "exact" computes the same structure as the OCaml function of the
 * same name, subject to the named-term observation contract in
 * docs/hol_light.txt. In particular, identity substitutions can choose
 * alpha-equivalent binder names differently from OCaml's physical-equality
 * implementation; subsequent inspection of those names can distinguish them.
 *
 * Objects are reached only through owned references (HolRef).  A reference
 * names a slot in the kernel's handle table together with a generation and a
 * kernel tag, so released, never-issued, mistyped and other-kernel
 * references are refused instead of being dereferenced.  There is no
 * function that turns a claimed sequent into a theorem: theorem references
 * are only produced by the ten primitive rules, the theory-extension
 * operations and checked replay (hol_replay.h).
 *
 * Environments are immutable values.  Every declaration, axiom and
 * definition creates a successor environment; older environments stay
 * valid.  Types, terms and theorems record the deepest declaration they
 * depend on (their anchor) and are usable exactly in the environments that
 * contain that declaration.  On one linear history this changes nothing
 * relative to HOL Light; on branches it refuses mixing.
 *
 * Threading: a kernel is single-threaded.  Callers serialize all calls on
 * one HolKernel; distinct kernels are independent. */
#ifndef HOL_KERNEL_H
#define HOL_KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOL_KERNEL_SEMANTICS_VERSION 1u
#define HOL_REFERENCE_REVISION "924faf0c3471a04174ffdeb8576ce7cabcb92d64"
#define HOL_REFERENCE_FUSION_SHA256 \
    "29544be92d9cc1e6b3b59ba5b210604b9a748366a68b3bcdc2619717d68fd98a"

typedef struct HolKernel HolKernel;

/* An owned reference.  0 is never a valid reference. */
typedef uint64_t HolRef;
#define HOL_NULL ((HolRef)0)

/* A group of references released together (one per CeTTa arena/request). */
typedef uint64_t HolOwner;
#define HOL_OWNER_KERNEL ((HolOwner)0)

/* Outcome classes.  They are disjoint: a logical refusal is never reported
 * as an allocation failure and vice versa. */
typedef enum {
    HOL_OK = 0,
    HOL_REFUSED,     /* a side condition of HOL Light failed (see reason)    */
    HOL_MALFORMED,   /* argument or input encoding is not well formed        */
    HOL_STALE,       /* released, abandoned or never-issued reference        */
    HOL_FOREIGN,     /* other kernel, or objects from incompatible branches  */
    HOL_NOMEM,       /* allocation failed; nothing was published             */
    HOL_LIMIT,       /* configured depth/size limit exceeded; nothing published */
    HOL_INTERRUPTED, /* interrupt flag or fuel exhausted; nothing published  */
    HOL_POLICY       /* the environment's axiom policy refuses the request   */
} HolStatus;

typedef enum {
    HOL_R_NONE = 0,
    /* references and arguments */
    HOL_R_NULL_ARG, HOL_R_BAD_HANDLE, HOL_R_WRONG_KIND, HOL_R_STALE_HANDLE,
    HOL_R_FUTURE_HANDLE, HOL_R_FOREIGN_KERNEL, HOL_R_INCOMPATIBLE_ENV,
    HOL_R_BAD_NAME, HOL_R_BAD_INDEX,
    /* types (fusion.ml mk_type, dest_type, dest_vartype) */
    HOL_R_MK_TYPE_UNDECLARED, HOL_R_MK_TYPE_ARITY, HOL_R_DEST_TYPE,
    HOL_R_DEST_VARTYPE,
    /* terms */
    HOL_R_MK_CONST_UNDECLARED, HOL_R_CONST_NOT_INSTANCE, HOL_R_MK_ABS_NOT_VAR,
    HOL_R_MK_COMB_TYPES, HOL_R_DEST_VAR, HOL_R_DEST_CONST, HOL_R_DEST_COMB,
    HOL_R_DEST_ABS, HOL_R_DEST_EQ, HOL_R_RATOR, HOL_R_RAND,
    HOL_R_VARIANT_NOT_VAR, HOL_R_VSUBST_BAD,
    /* primitive rules */
    HOL_R_TRANS, HOL_R_MK_COMB_NOT_EQS, HOL_R_MK_COMB_RULE_TYPES,
    HOL_R_ABS_NOT_VAR_OR_EQ, HOL_R_ABS_FREE_IN_HYP, HOL_R_BETA,
    HOL_R_ASSUME_NOT_BOOL, HOL_R_EQ_MP,
    /* theory extension */
    HOL_R_TYPE_EXISTS, HOL_R_CONST_EXISTS, HOL_R_AXIOM_NOT_BOOL,
    HOL_R_AXIOM_POLICY, HOL_R_POLICY_LOOSEN,
    HOL_R_DEF_SHAPE, HOL_R_DEF_ALREADY_CONST, HOL_R_DEF_NOT_CLOSED,
    HOL_R_DEF_TYVARS,
    HOL_R_TYDEF_CONSTS_IN_USE, HOL_R_TYDEF_HYPS, HOL_R_TYDEF_NOT_COMB,
    HOL_R_TYDEF_NOT_CLOSED, HOL_R_TYDEF_TYPE_EXISTS, HOL_R_TYDEF_SAME_NAMES,
    /* resources */
    HOL_R_NOMEM, HOL_R_DEPTH, HOL_R_FUEL, HOL_R_INTERRUPT,
    /* replay streams (hol_replay.h) */
    HOL_R_STREAM_MAGIC, HOL_R_STREAM_VERSION, HOL_R_STREAM_HEADER,
    HOL_R_STREAM_TRUNCATED, HOL_R_STREAM_INTEGER, HOL_R_STREAM_OPCODE,
    HOL_R_STREAM_FUTURE_REF, HOL_R_STREAM_DROPPED_REF, HOL_R_STREAM_FAILED_REF,
    HOL_R_STREAM_TRAILING, HOL_R_STREAM_CHECKSUM, HOL_R_STREAM_COUNT,
    HOL_R_STREAM_LIMIT, HOL_R_STREAM_EXPECT, HOL_R_STREAM_LABEL,
    HOL_R_STREAM_AFTER_END, HOL_R_STREAM_MISSING_END, HOL_R_STREAM_VALUE,
    HOL_R__COUNT
} HolReason;

const char *hol_status_name(HolStatus status);
const char *hol_reason_name(HolReason reason);

typedef struct {
    /* Maximum height of a type or term, which bounds every recursion
     * (0: default 8192, about 2 MiB of stack at -O2; raise it together with
     * the thread's stack).  Exceeding it yields HOL_LIMIT, never a refusal. */
    uint32_t max_depth;
    /* Bytes allocated between automatic collections (0: default 64 MiB). */
    size_t gc_threshold;
    /* Testing hook: when set, every structural hash is AND-ed with
     * hash_mask, so hash_mask = 0 makes every object collide. */
    bool use_hash_mask;
    uint32_t hash_mask;
} HolConfig;

/* NULL config means defaults.  Returns NULL if allocation fails or 65535
 * kernels are alive.  A reference must not outlive its kernel: while two
 * kernels are alive, each refuses the other's references (FOREIGN). */
HolKernel *hol_kernel_new(const HolConfig *config);
/* Releases every object, reference and environment of the kernel. */
void hol_kernel_free(HolKernel *k);

/* Status and reason of the most recent call on this kernel. */
HolStatus hol_last_status(const HolKernel *k);
HolReason hol_last_reason(const HolKernel *k);

/* ---- Ownership -------------------------------------------------------- */

HolOwner hol_owner_new(HolKernel *k);              /* 0 on failure */
/* New references are created in the current owner; returns the previous. */
HolOwner hol_owner_set_current(HolKernel *k, HolOwner owner);
/* Releases every reference held by the owner. Its registry slot may be
 * reused under a fresh generation; old and foreign owners remain invalid. */
HolStatus hol_owner_release(HolKernel *k, HolOwner owner);
/* Releases one reference.  Releasing twice reports HOL_STALE. */
HolStatus hol_ref_release(HolKernel *k, HolRef ref);
/* A new reference to the same object (a separate delivery occurrence of
 * the same proof event, for theorems).  Owned by the current owner. */
HolStatus hol_ref_dup(HolKernel *k, HolRef ref, HolRef *out);
HolStatus hol_ref_move(HolKernel *k, HolRef ref, HolOwner owner);

typedef enum { HOL_KIND_NONE = 0, HOL_KIND_TYPE, HOL_KIND_TERM, HOL_KIND_THM,
               HOL_KIND_ENV } HolKind;
HolKind hol_ref_kind(HolKernel *k, HolRef ref);    /* NONE if invalid */

/* Reclaims every object unreachable from live references, live
 * environments and registered roots.  Never invalidates a live reference. */
void hol_gc(HolKernel *k);

/* ---- Environments ------------------------------------------------------ */

typedef enum {
    HOL_AXIOMS_OPEN = 0,     /* stock new_axiom: any proposition, recorded as user authority */
    HOL_AXIOMS_STANDARD = 1, /* only the certified standard axioms ETA/SELECT/INFINITY */
    HOL_AXIOMS_SEALED = 2    /* no axioms */
} HolAxiomPolicy;

/* The bare kernel bootstrap of fusion.ml: types bool/0, fun/2 and the
 * constant = : A->A->bool; no axioms, no definitions, policy OPEN. */
HolStatus hol_env_bare(HolKernel *k, HolRef *env_out);
/* Tightens the axiom policy (OPEN < STANDARD < SEALED); loosening is refused. */
HolStatus hol_env_set_axiom_policy(HolKernel *k, HolRef env, HolAxiomPolicy p,
                                   HolRef *env_out);
HolStatus hol_new_type(HolKernel *k, HolRef env, const char *name,
                       uint32_t arity, HolRef *env_out);
HolStatus hol_new_constant(HolKernel *k, HolRef env, const char *name,
                           HolRef type, HolRef *env_out);
HolStatus hol_new_axiom(HolKernel *k, HolRef env, HolRef prop,
                        HolRef *env_out, HolRef *thm_out);
HolStatus hol_new_basic_definition(HolKernel *k, HolRef env, HolRef eqn,
                                   HolRef *env_out, HolRef *thm_out);
/* Atomic: on any refusal no type or constant is declared (stock HOL Light
 * declares the type and the abstraction constant before failing when
 * absname = repname; see docs/hol_light.txt). */
HolStatus hol_new_basic_type_definition(HolKernel *k, HolRef env,
                                        const char *tyname,
                                        const char *absname,
                                        const char *repname, HolRef witness,
                                        HolRef *env_out, HolRef *abs_rep_out,
                                        HolRef *rep_abs_out);

/* The object is usable in env (its anchor is in env's history). */
HolStatus hol_env_admits(HolKernel *k, HolRef env, HolRef object, bool *out);
/* older is an ancestor-or-self of newer. */
HolStatus hol_env_extends(HolKernel *k, HolRef older, HolRef newer, bool *out);
HolStatus hol_get_type_arity(HolKernel *k, HolRef env, const char *name,
                             uint32_t *arity_out);
HolStatus hol_get_const_type(HolKernel *k, HolRef env, const char *name,
                             HolRef *type_out);
HolStatus hol_env_policy(HolKernel *k, HolRef env, HolAxiomPolicy *out);
/* Number of events from the bare bootstrap to env (0 for the bootstrap). */
HolStatus hol_env_length(HolKernel *k, HolRef env, uint64_t *out);
/* Content digest of env's whole history (a cache key, not evidence). */
HolStatus hol_env_digest(HolKernel *k, HolRef env, uint64_t *out);
/* Axioms (newest first, as fusion.ml axioms()) and basic definitions. */
HolStatus hol_env_axioms(HolKernel *k, HolRef env, HolRef *buf, size_t cap,
                         size_t *count);
HolStatus hol_env_definitions(HolKernel *k, HolRef env, HolRef *buf,
                              size_t cap, size_t *count);

/* ---- Types (exact) ----------------------------------------------------- */

HolStatus hol_mk_vartype(HolKernel *k, const char *name, HolRef *out);
HolStatus hol_mk_type(HolKernel *k, HolRef env, const char *name,
                      const HolRef *args, size_t nargs, HolRef *out);
HolStatus hol_bool_ty(HolKernel *k, HolRef *out);
HolStatus hol_aty(HolKernel *k, HolRef *out);          /* Tyvar "A" */
bool hol_is_vartype(HolKernel *k, HolRef ty);
bool hol_is_type(HolKernel *k, HolRef ty);
/* Name of a type variable or type operator; valid while the kernel lives. */
const char *hol_type_name(HolKernel *k, HolRef ty);
HolStatus hol_type_nargs(HolKernel *k, HolRef ty, size_t *out);
HolStatus hol_type_arg(HolKernel *k, HolRef ty, size_t i, HolRef *out);
/* pairs = {repl0, key0, repl1, key1, ...}; non-variable keys are ignored
 * and the first pair for a variable wins, as in fusion.ml type_subst. */
HolStatus hol_type_subst(HolKernel *k, const HolRef *pairs, size_t npairs,
                         HolRef ty, HolRef *out);
/* tyvars, in fusion.ml order.  Two-call pattern: count is always set. */
HolStatus hol_tyvars(HolKernel *k, HolRef ty, HolRef *buf, size_t cap,
                     size_t *count);

/* ---- Terms (exact) ----------------------------------------------------- */

HolStatus hol_mk_var(HolKernel *k, const char *name, HolRef type, HolRef *out);
/* theta pairs as for hol_type_subst, applied to the generic type. */
HolStatus hol_mk_const(HolKernel *k, HolRef env, const char *name,
                       const HolRef *theta, size_t npairs, HolRef *out);
/* The constant at an instance of its generic type; theta is found by
 * matching (HOL Light's mk_mconst over the kernel's mk_const). */
HolStatus hol_mk_const_at(HolKernel *k, HolRef env, const char *name,
                          HolRef type, HolRef *out);
HolStatus hol_mk_comb(HolKernel *k, HolRef f, HolRef a, HolRef *out);
HolStatus hol_mk_abs(HolKernel *k, HolRef v, HolRef body, HolRef *out);
HolStatus hol_mk_eq(HolKernel *k, HolRef l, HolRef r, HolRef *out);

typedef enum { HOL_TERM_VAR = 0, HOL_TERM_CONST = 1, HOL_TERM_COMB = 2,
               HOL_TERM_ABS = 3, HOL_TERM_INVALID = 255 } HolTermKind;
HolTermKind hol_term_kind(HolKernel *k, HolRef tm);
const char *hol_term_name(HolKernel *k, HolRef tm);   /* Var/Const only */
HolStatus hol_type_of(HolKernel *k, HolRef tm, HolRef *out);
/* Comb: 0 = rator, 1 = rand.  Abs: 0 = bound variable, 1 = body. */
HolStatus hol_term_part(HolKernel *k, HolRef tm, int which, HolRef *out);
HolStatus hol_dest_eq(HolKernel *k, HolRef tm, HolRef *l, HolRef *r);

HolStatus hol_alphaorder(HolKernel *k, HolRef a, HolRef b, int *out);
HolStatus hol_aconv(HolKernel *k, HolRef a, HolRef b, bool *out);
HolStatus hol_compare_terms(HolKernel *k, HolRef a, HolRef b, int *out);
HolStatus hol_vfree_in(HolKernel *k, HolRef v, HolRef tm, bool *out);
HolStatus hol_freesin(HolKernel *k, const HolRef *acc, size_t n, HolRef tm,
                      bool *out);
HolStatus hol_frees(HolKernel *k, HolRef tm, HolRef *buf, size_t cap,
                    size_t *count);
HolStatus hol_type_vars_in_term(HolKernel *k, HolRef tm, HolRef *buf,
                                size_t cap, size_t *count);
HolStatus hol_variant(HolKernel *k, const HolRef *avoid, size_t n, HolRef v,
                      HolRef *out);
/* pairs = {t0, x0, t1, x1, ...}: replace variable x_i by t_i. */
HolStatus hol_vsubst(HolKernel *k, const HolRef *pairs, size_t npairs,
                     HolRef tm, HolRef *out);
HolStatus hol_inst(HolKernel *k, const HolRef *pairs, size_t npairs,
                   HolRef tm, HolRef *out);

/* ---- Primitive inference (exact) --------------------------------------- */

HolStatus hol_REFL(HolKernel *k, HolRef tm, HolRef *out);
HolStatus hol_TRANS(HolKernel *k, HolRef th1, HolRef th2, HolRef *out);
HolStatus hol_MK_COMB(HolKernel *k, HolRef th1, HolRef th2, HolRef *out);
HolStatus hol_ABS(HolKernel *k, HolRef v, HolRef th, HolRef *out);
HolStatus hol_BETA(HolKernel *k, HolRef tm, HolRef *out);
HolStatus hol_ASSUME(HolKernel *k, HolRef tm, HolRef *out);
HolStatus hol_EQ_MP(HolKernel *k, HolRef th1, HolRef th2, HolRef *out);
HolStatus hol_DEDUCT_ANTISYM_RULE(HolKernel *k, HolRef th1, HolRef th2,
                                  HolRef *out);
HolStatus hol_INST_TYPE(HolKernel *k, const HolRef *pairs, size_t npairs,
                        HolRef th, HolRef *out);
HolStatus hol_INST(HolKernel *k, const HolRef *pairs, size_t npairs,
                   HolRef th, HolRef *out);

/* ---- Theorem observation (never fabrication) --------------------------- */

HolStatus hol_thm_concl(HolKernel *k, HolRef th, HolRef *out);
HolStatus hol_thm_nhyps(HolKernel *k, HolRef th, size_t *out);
HolStatus hol_thm_hyp(HolKernel *k, HolRef th, size_t i, HolRef *out);
/* Axiom dependencies: references to the axiom theorems, oldest first. */
HolStatus hol_thm_axioms(HolKernel *k, HolRef th, HolRef *buf, size_t cap,
                         size_t *count);
/* True when every axiom dependency is a certified standard axiom
 * (ETA_AX, SELECT_AX, INFINITY_AX over certified standard definitions). */
HolStatus hol_thm_standard(HolKernel *k, HolRef th, bool *out);
/* The proof event that produced this reference (distinct per rule call). */
HolStatus hol_thm_event(HolKernel *k, HolRef th, uint64_t *out);
/* fusion.ml's equals_thm: the same hypotheses and conclusion. */
HolStatus hol_thm_equal(HolKernel *k, HolRef a, HolRef b, bool *out);
/* The same payload object: the same sequent, axiom dependencies and anchor
 * (stricter than hol_thm_equal). */
HolStatus hol_thm_same_payload(HolKernel *k, HolRef a, HolRef b, bool *out);

/* Canonical text (docs/hol_light_stream.txt, "Canonical syntax"), written into
 * buf with the two-call pattern; *len excludes the terminating NUL.  The
 * text repeats shared sub-terms, so its size can be exponential in the
 * size of a DAG; printing and digests observe fuel and the interrupt flag. */
HolStatus hol_print(HolKernel *k, HolRef obj, char *buf, size_t cap,
                    size_t *len);
/* FNV-1a 64 of the canonical text (types, terms or theorems). */
HolStatus hol_digest(HolKernel *k, HolRef obj, uint64_t *out);
/* Canonical text back to a type or term of env, by the checked
 * constructors (never a theorem). */
HolStatus hol_parse_type(HolKernel *k, HolRef env, const char *text, size_t len,
                         HolRef *out);
HolStatus hol_parse_term(HolKernel *k, HolRef env, const char *text, size_t len,
                         HolRef *out);

/* ---- Statistics, limits and test hooks --------------------------------- */

typedef struct {
    /* Core allocations and traversal memo; journal storage is separate. */
    size_t bytes_live, bytes_peak;
    size_t journal_events, journal_bytes;
    uint64_t allocations;
    size_t types, terms, thms, depsets, decls, names, refs;
    uint64_t gc_runs, intern_hits, intern_misses, events;
    uint64_t renames_vsubst, renames_inst; /* capture-avoiding renamings done */
} HolStats;
void hol_stats(HolKernel *k, HolStats *out);

/* A nonzero *flag makes long operations stop with HOL_INTERRUPTED. */
void hol_set_interrupt(HolKernel *k, volatile int *flag);
/* Optional host interruption poll. The callback must not reenter the kernel. */
void hol_set_interrupt_poll(HolKernel *k, bool (*poll)(void *), void *context);
/* Work units (traversal steps) allowed per call; 0 = unlimited. */
void hol_set_fuel(HolKernel *k, uint64_t fuel_per_call);

/* Testing only: make the n-th allocation from now fail (n >= 1), once;
 * n = 0 disables.  sticky = true makes every later allocation fail too. */
void hol_test_fail_allocation(HolKernel *k, uint64_t n, bool sticky);
uint64_t hol_test_allocations(const HolKernel *k);

#ifdef __cplusplus
}
#endif
#endif /* HOL_KERNEL_H */
