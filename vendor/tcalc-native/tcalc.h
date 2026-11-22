#pragma once
/* tcalc: a small kernel for the transform step.

   Terms are hash-consed n-ary nodes; a Term is an index. Each node caches its sort,
   the holes below it, its dependency mask (the bound variables it may mention) and
   whether it contains unknowns.

   State <K ; W>: K binds unknowns (with a trail, for two-way unification) and holds
   disequations; W is the set of occurrences, each a term in a space.

   One rule, fire: take occurrences, keep (read) occurrences, tell equations and
   disequations into K, check the hole sorts and the separation guards, compute the rule's
   sealed operations, check that absent/count queries hold at every solution, check the
   rule's sealed guards, mint a fresh name if asked, make the result.

   Sorts and separation guards come from the binder context a rule is written in:
   a bound hole x must stay apart from every hole that does not depend on x.
   Unknowns (tc_meta) acquire a trailed sort when placed in a sorted constructor
   argument or unified with a sorted term; binding to an incompatible sort is refused.
   A hole of a sort takes a term of that sort: an unknown is not yet one, so a firing
   whose sorted hole would hold an unbound unknown is refused until a tell binds it.

   Extensions: tc_promote checks a proof script once against its context and installs
   it as a rule (SHARE); tc_def adds a defined constructor whose unfolding is checked
   by conversion steps (EXTEND with delta); fresh names are minted by fire.

   The contract after tc_seal (the caller is then untrusted):
   - Only firings (and conversion, active and rho steps) write into a protected static space.
     Dynamic spaces (any other name of the space sort) cannot be protected: callers may post facts
     into them, but not birth markers; only a firing bears a space.
   - Range restriction: a rule that makes into a protected space makes only holes that its take or
     keep patterns bind, that its operations compute, that it mints fresh, or that it declares as
     inputs (Rule.inputs: the holes a caller may supply). tc_rule refuses a rule that makes into a
     space already protected without this; the seal, and tc_protect after it, bar every other such
     rule (tc_rule_barred), and a firing whose computed space is protected checks it at commit. A
     derived rule's inputs are the holes of its conclusion that its hypotheses do not bind.
   - Evidence spaces: after the seal only declared producers can publish into a protected evidence
     space, and a producer makes nothing its caller chose (its input list is empty). The current
     backend is a set of producer rules per space, fixed before the seal (tc_publishers); every
     other rule, every derived rule, and conversion, active and rho steps are refused there.
   - Sealed functions (tc_fn): guards decide a predicate and operations compute words. Both are
     pure, total functions of the words they are given: they read nothing else (not the store, not
     host state) and write only their outputs, keep no state and make no kernel call. Their inputs
     are holes of word sorts bound by matching the current board, or outputs of an earlier
     operation of the same rule; the kernel checks that each input is a word of its sort before the
     call. An operation that returns false has no value and refuses the firing. Its outputs are
     bound by the kernel only (a caller binding of one refuses the firing) and are checked like any
     hole. A firing runs its operations in declaration order after matching, then the absent and
     count queries, then its guards, then makes; neither changes which occurrences matched. A
     function's statement name and contract hash are provenance metadata, never evidence that the
     function computes that statement: registered C stays trusted until a proved native image
     replaces it.
   - Names are minted before they are used: every name a caller supplies (in a posted fact, a tell,
     a rule, a lemma or a hole value) must already be minted, by tc_fresh_name or a fresh firing
     (ids whose top two bits are 01 count as minted), so a fresh name is new to the whole state.
     The names of a word sort are the 64-bit words, literals that need no minting; a fresh hole has
     no word sort.
   - Closed by the seal: tc_rule, tc_fn, tc_publishers, tc_unset, tc_set_quote_con, tc_set_rho_con,
     tc_set_handle_con. Open: tc_sort, tc_con and tc_def (they make new sorts and constructors and
     cannot change one that a sealed rule or a protected fact uses; the HOL checker declares types
     while it replays), tc_protect (it only adds protection), and lemmas (tc_lemma_end, tc_promote:
     derived rules).
   - A firing is atomic: a refusal leaves the store, the bindings, the name counter and the unknown
     counter as they were. The guards a rule declares (absent, count, birth law, its sealed
     operations and guards, and where it may publish) are part of it: tc_eval_prepared_ex checks
     absent, count and birth on every commit into a protected space, whatever its switches, and
     operations, guards and publication in every variant.
   - Custody: the kernel is single threaded and takes no locks. A host gives one thread exclusive
     custody of the whole kernel state for each call, through every commit; concurrent calls are
     outside this contract (no atomic or versioned protocol is provided).
   - Reach (tc_fire_reach, seeds of a prepared step) checks a firing against the spaces reachable
     from the seeds the caller presents; any caller may present any space, so it is a check of the
     caller's intent, not an authority.
   - A conversion, active step or rho step rewrites one occurrence into an equal term: in a store
     with multiplicity (some rule takes or counts, or a located fact exists) it replaces the source,
     as a firing taking it would; in a set store it adds the equal fact.
   - Caps (term depth 4096, reach sets of 512 terms, 1024 search candidates, the 2^27-slot board,
     memory) end in a refusal, never in a wrong result. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t Term;                       /* 0 = none; every refusal returns 0 or -1 */
typedef uint32_t Occ;                        /* occurrence id, 0 = none */
enum { K_NAME = 1, K_BV, K_VAR, K_META, K_APP };
enum { SORT_PURE = 1, SORT_STRICT = 2, SORT_PROVABLE = 4, SORT_FREE = 8, SORT_WORD = 16 };
enum { FIBRE_PURE = 1, FIBRE_GROUND = 2, FIBRE_LINEAR = 4, FIBRE_FLAT = 8, FIBRE_FN = 16 };  /* FN: calls tc_fn functions */
#define TC_ANY   0                           /* sort 0: unsorted, no checks */
#define TC_VARS  64                          /* holes per context */
#define TC_ARGS  32                          /* constructor arity */
#define TC_HYPS  24                          /* kept premises / hypotheses per rule */
#define TC_FN_IN   16                        /* input words of a sealed function */
#define TC_FN_OUT  4                         /* output words of an operation */
#define TC_FN_USES 4                         /* operations, and guards, per rule */
#define TC_FN_NAME 128                       /* bytes of a statement name, with its terminator */

/* A sealed function: a guard (nout = 0) decides its predicate of in[0 .. nin); an operation writes
   out[0 .. nout) and returns true, or returns false: no value. See the contract above. */
typedef bool (*TcFn)(const uint64_t *in, uint64_t *out);
enum { TC_FN_GUARD = 1, TC_FN_OP = 2 };
typedef struct { int16_t fn; uint8_t in[TC_FN_IN]; } TcGuardUse;                       /* holes, nin of them */
typedef struct { int16_t fn; uint8_t in[TC_FN_IN]; uint8_t out[TC_FN_OUT]; } TcOpUse;  /* holes, nin and nout */
typedef struct {
  const char *name;                          /* the statement the function is meant to compute */
  uint8_t kind, nin, nout;
  TcFn fn;
  uint8_t contract_sha256[32];               /* provenance metadata, not evidence */
} TcFnInfo;

typedef struct { uint16_t sort; uint8_t bound; uint64_t deps; } Binder;  /* deps: earlier positions */
typedef struct { Term t; uint8_t space; } Pat;
typedef struct { Term t; uint8_t space; uint32_t n; } Count;
typedef struct {
  uint8_t ntake, nkeep, nabsent, ncount, ntell, nneq, nmake_extra, birth_law;
  Pat     take[4], absent[4], make, make_extra[4];
  Count   count[4];
  Term    tell[4][2], neq[4][2];
  int8_t  fresh;                             /* hole bound to a newly minted name, or -1 */
  uint8_t nop, nguard;                       /* sealed functions the rule calls: operations run in order */
  uint64_t inputs;                           /* explicit input holes: made holes a caller may supply */
  TcOpUse    op[TC_FN_USES];
  TcGuardUse guard[TC_FN_USES];
  Pat     keep[TC_HYPS];                     /* last: an installed rule stores only nkeep of them */
} Rule;

enum { TC_ENT_KEEPS = 0, TC_ENT_INPUTS = 1, TC_ENT_FRESH = 2 };
typedef struct {
  const char *name;
  uint8_t kind;
  int rule;
  uint8_t nholes;
  uint8_t holes[TC_ARGS];
} TcEntry;
typedef struct {
  const char *prefix, *header, *body_type;     /* names the specializer emits under */
  uint64_t max_slot;
  int nentries;
  TcEntry entries[32];
  int unset_rule, unset_nbinds;
  uint8_t unset_hole[TC_ARGS];
  uint64_t unset_val[TC_ARGS];
} TcTheory;

enum { CV_REFL, CV_CONG, CV_SYM, CV_UNFOLD };
typedef struct Conv {
  uint8_t kind; int con; Term t;             /* CONG/UNFOLD: con; UNFOLD: t, the unfolded term, if stated */
  int n; const struct Conv *kid;             /* CONG: n children; SYM, UNFOLD: kid[0] */
  const Term *args, *dummies;                /* UNFOLD: args (checked if given), dummies */
} Conv;
enum { ST_RULE, ST_CONV };
typedef struct {
  uint8_t kind; int rule; int prem[TC_HYPS]; /* prem: >= 0 an earlier step, -1-k hypothesis k */
  const Term *inst; Term target; const Conv *conv;
} Step;

bool tc_init(uint32_t nodes, uint32_t occs);
int  tc_sort(uint8_t flags);
int  tc_con(uint16_t sort, uint64_t retdeps, int arity, const Binder *args);
bool tc_ctx(int n, const Binder *vars);      /* the context later terms, rules and lemmas use */
bool tc_ctx_push(Binder var);                /* extend the current context by one variable */
Term tc_v(int i);                            /* context variable i */
Term tc_app(int con, const Term *args);      /* checked formation */
Term tc_name(uint16_t sort, uint64_t id);
Term tc_bv(uint16_t sort, unsigned i);       /* a concrete bound variable, i < 64 */
Term tc_meta(void);                          /* a fresh unknown */
uint64_t tc_fresh_name(void);
uint64_t tc_next_name(void);
void tc_node(Term t, uint8_t *kind, uint32_t *head, int *arity, const Term **args);  /* args: valid until a term is made */
uint64_t tc_name_id(Term t);
uint16_t tc_sort_of(Term t);
uint64_t tc_steps(void);                     /* rule and conversion steps checked so far */

void tc_protect(uint8_t space);              /* only firings write here once sealed */
void tc_seal(void);
int  tc_fn(const char *name, uint8_t kind, uint8_t nin, uint8_t nout, TcFn fn,
           const uint8_t contract_sha256[32]);   /* register a sealed function before the seal: its id, or -1 */
int  tc_fn_count(void);
bool tc_fn_info(int fn, TcFnInfo *out);      /* out->name lives in the kernel until tc_init */
bool tc_publishers(uint8_t space, const int *rules, int n);  /* before the seal: make the protected space an
                                                evidence space whose producers are these rules (added to any
                                                declared before; n = 0: none). Each rule is installed by
                                                tc_rule, range restricted, has no inputs and makes there. */
int  tc_space_publishers(uint8_t space, int *out, int cap);  /* producers of an evidence space, or -1 */
int  tc_rule(const Rule *r);                 /* trusted, in the current context */
int  tc_rule_count(void);                    /* number of installed rules */
bool tc_rule_arity(int rule, uint8_t *out_ntake, uint8_t *out_nkeep, uint8_t *out_nholes);
uint8_t tc_rule_fibre(int rule);             /* FIBRE_PURE / GROUND / LINEAR / FLAT / FN */
int  tc_rule_fns(int rule, int *out, int cap);   /* the sealed functions the rule calls: operations, then guards */
bool tc_rule_barred(int rule);               /* true: no firing of the rule commits after the seal (it makes into
                                                a protected space without range restriction, or into an
                                                evidence space it does not produce) */
int  tc_def(uint16_t sort, uint64_t retdeps, int arity, int ndummies, Term body);
int  tc_promote(int nbinders, uint8_t space, int nhyp, const Term *hyps, Term concl,
                const Step *steps, int nsteps);
bool tc_lemma(int nbinders, uint8_t space);   /* the same check, one step at a time: open a lemma, */
bool tc_lemma_hyp(Term hyp);                 /*   add hypothesis k (referred to as -1-k), */
int  tc_lemma_step(const Step *step);        /*   check a step (its index, or -1), */
Term tc_lemma_term(int ref);                 /*   read what a step or hypothesis states, */
int  tc_lemma_end(int ref);                  /*   install ref's statement as a rule (its id, or -1) */
void tc_transient(void);                     /* terms made from now on are released by the next
                                                tc_rule or tc_promote, except the patterns it installs.
                                                Anything that writes kernel state (board, K, defs, a new
                                                context or unknown) cancels it, so after a release no kernel
                                                structure holds a released term; id-keyed caches are reset. */
void tc_birth(void);                         /* birth-named transient scope: each tc_app allocates a fresh
                                                occurrence node; rule patterns compile to bytecode before release.
                                                A rule or lemma installed in the scope keeps hash-consed copies of
                                                its patterns; firings, conversions and tc_term end the scope. */
void tc_unset(Occ u);                        /* declare u as the default occurrence for unset slots (slot mode: a
                                                register table). Only before the seal, and only in a theory with no
                                                take, count, absent or located rule; posts then append above it. */
Occ  tc_post(uint8_t space, Term t);         /* trusted for protected spaces */
Occ  tc_fire(int rule, const Occ *take, const Occ *keep, const Term *inst);
const Term *tc_flat_args(Occ o, uint32_t ktag);   /* valid until a term is made */
Occ  tc_flat_make(uint8_t space, uint32_t head, uint16_t sort, int arity, const Term *args);
Occ  tc_flat_put(Occ dest, uint8_t space, int con, const Term *args);
Occ  tc_flat_out(Occ dest, uint8_t space, int con, const Term *args);
Occ  tc_flat_fire(int rule, const Occ *keep, const Term *inst, Occ dest);
/* dest (tc_flat_put, tc_flat_out, tc_flat_fire, and a prepared step's dest for a flat rule through
   tc_commit_prepared and tc_eval_prepared_ex alike): the fact is written in place at dest when dest
   holds a fact of the same constructor and space, the constructor has at most 4 arguments, and the
   space is not protected after the seal; otherwise it is appended as a new occurrence. */
Occ  tc_convert(Occ p, const Conv *c, Term target);
bool tc_alive(Occ o);
Term tc_term(Occ o);
bool tc_tell(Term u, Term v);
bool tc_tell_neq(Term u, Term v);
Term tc_deref(Term t);

/* ---- Rows 3-5: NamedSpaces, SpacesCreation (Births), and NamedSpacesReach ---- */
uint16_t tc_space_sort(void);                /* sort of space handles (allocated lazily) */
Term tc_space_term(uint8_t space);           /* canonical Term for static space 0..255 */
Term tc_at_space(Term space, Term t);        /* located pattern/term: t at space */
Term tc_at_birth(Term space, Term t);        /* located birth-marker pattern/term: t birthing space */
Occ  tc_post_at(Term space, Term t, bool marker);
Term tc_occ_space(Occ o);                    /* space Term of occurrence o */
bool tc_occ_marker(Occ o);                   /* true iff o is a space birth marker */
bool tc_space_born(Term space);              /* true iff space has a live birth marker in W */
void tc_set_handle_con(int con);             /* unary constructor handle(s) carrying space reach */
int  tc_reach(const Term *seeds, int nseed, Term *out, int cap);
Occ  tc_fire_reach(int rule, const Occ *take, const Occ *keep, const Term *inst,
                   const Term *seeds, int nseed);
Occ  tc_search_reach(int rule, const Term *seeds, int nseed);
Occ  tc_last_extra(int idx);                 /* extra made occurrence idx (0..3) of last firing */

/* ---- Row 6: ProgramReflection & ProgramBindersQuoted ---- */
void tc_set_quote_con(int quote_con, int activate_con, int lam_con, int app_con);
Term tc_active_step(Term t);                 /* one active-context step (beta or activate-quote; quote inert);
                                                0 if no step applies, or if the step would capture a bound
                                                variable (no renaming) or the term is deeper than 4096 */
Term tc_active_norm(Term t, int max_steps);  /* normalize in active contexts (0 if stuck/diverged, or if a step
                                                was refused for capture or depth: that is not a normal form) */
Occ  tc_active_step_occ(Occ o);              /* one active-context step on a board occurrence */

/* ---- Row 7: RhoContinuation & ContextualCommunicationReflection ---- */
void tc_set_rho_con(int quote_con, int drop_con, bool allow_proc_drop);
Term tc_rho_norm_name(Term t);               /* normalize Name-sort quote(drop(n)) = n */
Occ  tc_rho_step_occ(Occ o);                 /* Process-sort drop(quote(P)) -> P (gated by allow_proc_drop) */
Term tc_rho_comm(Term ch_out, Term ch_in, Term payload_proc,
                 Term cont_pat, int var_name, int var_proc);

/* ---- Modular Prepared-Step & Cursor Interface (CeTTa <-> tcalc Bridge) ----
 * 1. Candidate / PreparedStep: proposed by an external cursor/index (e.g. CeTTa
 *    hash-consed SpaceMatchBackend or a candidate generator).
 * 2. Admission & Commit (tc_commit_prepared): tcalc checks the prepared step in
 *    <K ; W>, consumes linear takes, mints fresh names/births, and populates a
 *    TcAdmissionReceipt only when admitted (refused candidates leave no receipt). */
typedef struct {
  int        rule;
  const Occ *take;      /* optional array of ntake occurrence IDs (NULL if ntake == 0) */
  const Occ *keep;      /* optional array of nkeep occurrence IDs (NULL if nkeep == 0) */
  const Term *inst;     /* optional hole binding array (NULL if inferred from premises) */
  Occ        dest;      /* optional destination slot for FIBRE_FLAT rules (0 = new occ) */
  int        nseeds;    /* optional reachability root count (> 0 uses tc_fire_reach) */
  const Term *seeds;    /* optional reachability root seeds */
} TcPreparedStep;

typedef struct {
  bool    admitted;     /* true iff admission succeeded */
  int     rule;
  Occ     out;          /* primary made occurrence (0 if refused) */
  uint8_t nextra;
  Occ     extra[4];     /* extra made occurrences (0..3) */
  Term    out_term;     /* primary made Term (0 if refused or flat-only slot) */
} TcAdmissionReceipt;

typedef struct TcCandidateCursor {
  void *ctx;
  bool (*next)(void *ctx, TcPreparedStep *out);
} TcCandidateCursor;

enum {
  TC_PREP_OK              = 0,
  TC_PREP_BAD_RULE        = -1,
  TC_PREP_ABSENT_RESOURCE = -2,
  TC_PREP_DOUBLE_TAKE     = -3,
  TC_PREP_SHAPE_UNIFY     = -4,
  TC_PREP_ABSENT_GUARD    = -5,
  TC_PREP_COUNT_GUARD     = -6,
  TC_PREP_BIRTH_GUARD     = -7,
  TC_PREP_REACH_GUARD     = -8,
  TC_PREP_FN_GUARD        = -9,   /* an operation had no value, an input was not a word, or a guard failed */
  TC_PREP_PUBLISH_GUARD   = -10   /* the rule may not make there after the seal (tc_rule_barred, evidence) */
};

int  tc_eval_prepared_ex(const TcPreparedStep *step,
                         bool check_absent, bool check_count,
                         bool check_birth, bool check_reach,
                         bool do_commit,
                         Term out_make_terms[5], Term out_make_spaces[5],
                         uint8_t out_make_marks[5], uint8_t *out_nmake,
                         Occ *out_occ);
bool tc_cursor_next_raw(const TcCandidateCursor *cursor, TcPreparedStep *out);
bool tc_commit_prepared(const TcPreparedStep *step, TcAdmissionReceipt *receipt);
int  tc_step_cursor(TcCandidateCursor *cursor, TcAdmissionReceipt *receipt);
