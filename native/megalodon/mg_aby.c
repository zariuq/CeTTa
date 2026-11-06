/*
 * Native aby: a proof of the goal an aby step leaves, from its context and
 * the facts the step names, found by search.  The proof found then goes
 * the way of every proof: the proof engine's double check here, the
 * kernel's check at Qed.
 *
 * The calculus is a focused sequent calculus for Megalodon's higher-order
 * logic, intuitionistic, the connectives being their impredicative
 * definitions:
 *
 *   - a goal A -> B or forall x:a, B is introduced (invertible);
 *   - a hypothesis whose proposition is a positive encoding
 *     forall p:prop, B1 -> ... -> Bk -> p, each Bi of the form
 *     forall ys, C1 -> ... -> Cm -> p with p free nowhere else (and, or,
 *     ex, iff, False, ...), is eliminated against the goal once
 *     (invertible): applied to the goal, each Bi becomes a subgoal;
 *   - an atomic goal is closed by backchaining on a hypothesis or a named
 *     fact: its universal binders become metavariables, its premises
 *     subgoals, and its conclusion is unified with the goal, at every
 *     depth of the fact and through the definitions at its head;
 *   - a goal whose head is a definition is unfolded when nothing closes
 *     it folded.
 *
 * A fact with a conjunctive conclusion (and, iff) is split into one fact
 * per conjunct, each with its derivation, so that each is one step from a
 * goal.
 *
 * Unification is higher-order, modulo beta-eta and lazily delta: patterns
 * are solved exactly (Miller), other flexible-rigid pairs by projection
 * and imitation to a bounded depth (Huet), rigid pairs with different
 * heads by unfolding the definition at either head, flexible-flexible
 * pairs postponed, unified again at the end and else given the unifier
 * where both heads ignore their arguments.
 *
 * Search is depth-first with iterative deepening on the backchaining and
 * unfolding steps, backtracking through success continuations, under a
 * step budget; it runs on a stack of its own.  A conclusion with a
 * metavariable at its head costs a level more; a premise equal to its
 * goal is a detour and pruned; facts that can meet the goal's head
 * rigidly come before the others; ground premises are proved first; a
 * ground goal (no metavariable in it or its context) is proved once, and
 * one that had no proof at a depth is not tried again at that depth.
 *
 * While searching, eigenvariables and metavariables are names of their
 * own, so terms carry no free de Bruijn index; the proof found is closed
 * over the goal's context at the end.
 */
#include "mg_aby.h"
#include "mg_library.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── the search state ─────────────────────────────────────────────────── */

enum { V_EIGEN = 1, V_META };

typedef struct {
    MgTm *term;            /* the name standing for it */
    uint32_t name;
    uint8_t kind;
    MgTp *tp;
    uint32_t level;        /* eigen: creation index; meta: eigenvariables it may use */
    MgTm *val;             /* meta: its value */
    int32_t ctx_db;        /* eigen of the goal's context: its index there; else -1 */
} AV;

typedef struct HypE HypE;
enum { H_INT = 1, H_CTX, H_FACT, H_DERIVED };
struct HypE {
    MgTm *prop;            /* with names, no free de Bruijn index */
    uint8_t kind;
    bool done;             /* a positive hypothesis already eliminated */
    int8_t pos;            /* a positive encoding: 1, 0, or -1 not known yet */
    uint32_t uid;          /* its identity: a copy (mark_done) keeps it */
    bool ground;           /* no metavariable in it or in the list below */
    int8_t qflex;          /* closes quantified goals whole (quantified_flex): 1, 0, -1 not known */
    int8_t eqn;            /* concludes in an equation: 1, 0, -1 not known */
    uint32_t index;        /* H_CTX: its index in the goal's hypotheses */
    MgPf *fact;            /* H_FACT: its proof in the goal's context */
    struct APf *derived;   /* H_DERIVED: its proof from the others */
    const HypE *next;
};

typedef struct APf APf;
enum { P_HYP = 1, P_TMAP, P_PFAP, P_TLAM, P_PLAM };
struct APf {
    uint8_t tag;
    const HypE *h;         /* P_HYP: the hypothesis; P_PLAM: the one it binds */
    MgTm *t;               /* P_TMAP: the term; P_PLAM: the proposition */
    MgTp *tp;              /* P_TLAM */
    uint32_t ev;           /* P_TLAM: the eigenvariable bound */
    APf *p, *q;
};

typedef struct UPair UPair;
struct UPair {
    MgTm *s, *t;
    const UPair *next;
};

enum { T_ASSIGN = 1, T_LEVEL, T_FLEX, T_LEMMA };

typedef struct {
    uint8_t kind;
    uint32_t var;
    uint32_t old_level;
} TrailE;

typedef struct ABlock ABlock;
struct ABlock {
    ABlock *next;
    size_t used, cap;
    _Alignas(16) unsigned char data[];
};

struct Failed {
    const MgTm *goal;
    const struct HypE *ctx;
    uint32_t depth;
};

struct Lemma {
    const MgTm *goal;      /* NULL once forgotten */
    const struct HypE *ctx;
    struct APf *proof;
};

typedef struct Aby Aby;
typedef struct Kont Kont;
struct Kont {
    bool (*fn)(Aby *a, const Kont *k);
    const void *data;
    const Kont *next;
};

struct Aby {
    MgElab *e;
    AV *vars;
    uint32_t nvars, cvars, neigen;
    TrailE *trail;
    size_t ntrail, ctrail;
    const UPair *flex;      /* postponed flexible-flexible pairs */
    ABlock *blocks;
    uint64_t steps, max_steps;
    bool out_of_steps;
    const HypE *facts;      /* the named facts */
    const HypE *dneg, *xm;  /* named facts of the classical shapes, if any */
    uint32_t ctx_terms;     /* term variables of the goal's context */
    const TpCx *ctx_tps;    /* their types, innermost first */
    const PfSt *goal;
    MgPf *result;
    APf *root;
    uint64_t rejected;      /* proofs found that failed the double check */
    uint32_t hyps;          /* hypotheses made */
    /* inst's memo: valid for one epoch of assignments (an assignment or
     * its undoing starts a new epoch) */
    uint32_t flex_rounds;   /* k_done's rounds of postponed pairs */
    /* ground sequents (goal, context) that had no proof at a depth */
    struct Failed *failed;
    uint32_t failed_cap, failed_len;
    /* ground sequents proved on the current branch, with their proofs
     * (each on the trail: backtracking past it forgets it) */
    struct Lemma *lemmas;
    uint32_t nlemmas, clemmas;
    char *stack_base;       /* where the search's stack began */
    const void *cut_to;     /* a ground goal whose search is being cut */
    size_t stack_use;       /* how much of it a branch may take */
    uint32_t nassign;       /* metavariables assigned now */
    uint64_t epoch;
    struct {
        const MgTm *key;
        MgTm *val;
        uint64_t epoch;
    } *memo;
    uint32_t memo_cap;
};

static void *aalloc(Aby *a, size_t n) {
    n = (n + 15u) & ~(size_t)15u;
    ABlock *b = a->blocks;
    if (!b || b->cap - b->used < n) {
        size_t cap = n > 65536u ? n : 65536u;
        ABlock *fresh = malloc(sizeof *fresh + cap);
        if (!fresh)
            mge_nomem(a->e);
        fresh->next = b;
        fresh->used = 0;
        fresh->cap = cap;
        a->blocks = b = fresh;
    }
    void *p = b->data + b->used;
    b->used += n;
    memset(p, 0, n);
    return p;
}

/* The search backtracks through success continuations, so a partial
 * proof nests as deep in C as it is long: it runs on a stack of its own
 * (ABY_STACK) and stops a branch well before the end of it. */
enum { ABY_STACK = 1u << 30, ABY_STACK_USE = 3u << 28 };

static bool step(Aby *a) {
    if (a->cut_to)
        return false;
    if (++a->steps > a->max_steps) {
        a->out_of_steps = true;
        return false;
    }
    char here;
    /* the depth as integer addresses: the two locals are separate objects,
     * which ISO C does not order (a flat, downward-growing stack is
     * assumed, as on the platforms CeTTa supports) */
    if ((uintptr_t)a->stack_base - (uintptr_t)&here > a->stack_use) {
        a->out_of_steps = true;
        return false;
    }
    return true;
}

static bool kcall(Aby *a, const Kont *k) { return k->fn(a, k); }

/* ── variables ────────────────────────────────────────────────────────── */

/* Names "\x01aby<i>" stand for the i-th variable of a search; the pool is
 * kept by the elaborator, interned in batches, and recorded as runs of
 * consecutive name ids (MgElab.aby_runs) for the way back. */
static uint32_t pool_name(Aby *a, uint32_t i) {
    MgElab *e = a->e;
    while (i >= e->aby_pool_len) {
        uint32_t want = e->aby_pool_len ? e->aby_pool_len * 2u : 1024u;
        uint32_t *grown = realloc(e->aby_pool, sizeof *grown * want);
        if (!grown)
            mge_nomem(e);
        e->aby_pool = grown;
        for (uint32_t k = e->aby_pool_len; k < want; ++k) {
            char text[32];
            snprintf(text, sizeof text, "\x01" "aby%u", k);
            grown[k] = mge_name_id(e, text);
            MgAbyRun *last = e->aby_nruns ? &e->aby_runs[e->aby_nruns - 1u] : NULL;
            if (last && last->first + last->len == k && last->base + last->len == grown[k]) {
                ++last->len;
                continue;
            }
            if (e->aby_nruns == e->aby_cruns) {
                uint32_t cap = e->aby_cruns ? e->aby_cruns * 2u : 8u;
                MgAbyRun *runs = realloc(e->aby_runs, sizeof *runs * cap);
                if (!runs)
                    mge_nomem(e);
                e->aby_runs = runs;
                e->aby_cruns = cap;
            }
            e->aby_runs[e->aby_nruns++] = (MgAbyRun){grown[k], k, 1u};
        }
        e->aby_pool_len = want;
    }
    return e->aby_pool[i];
}

static int32_t var_of(const Aby *a, uint32_t name) {
    const MgElab *e = a->e;
    for (uint32_t r = 0; r < e->aby_nruns; ++r) {
        const MgAbyRun *run = &e->aby_runs[r];
        if (name >= run->base && name - run->base < run->len) {
            uint32_t i = run->first + (name - run->base);
            return i < a->nvars ? (int32_t)i : -1;
        }
    }
    return -1;
}

static AV *new_var(Aby *a, uint8_t kind, MgTp *tp) {
    if (a->nvars == a->cvars) {
        uint32_t cap = a->cvars ? a->cvars * 2u : 256u;
        AV *grown = realloc(a->vars, sizeof *grown * cap);
        if (!grown)
            mge_nomem(a->e);
        a->vars = grown;
        a->cvars = cap;
    }
    uint32_t i = a->nvars++;
    AV *v = &a->vars[i];
    v->name = pool_name(a, i);
    v->term = mge_tm_h(a->e, v->name);
    v->kind = kind;
    v->tp = tp;
    v->val = NULL;
    v->ctx_db = -1;
    if (kind == V_EIGEN)
        v->level = a->neigen++;
    else
        v->level = a->neigen;
    return v;
}

static AV *meta_of(Aby *a, const MgTm *m) {
    if (m->tag != MG_TM_NAME)
        return NULL;
    int32_t i = var_of(a, m->n);
    return i >= 0 && a->vars[i].kind == V_META ? &a->vars[i] : NULL;
}

static AV *eigen_of(Aby *a, const MgTm *m) {
    if (m->tag != MG_TM_NAME)
        return NULL;
    int32_t i = var_of(a, m->n);
    return i >= 0 && a->vars[i].kind == V_EIGEN ? &a->vars[i] : NULL;
}

/* ── the trail ────────────────────────────────────────────────────────── */

typedef struct {
    size_t trail;
    uint32_t nvars, neigen;
    const UPair *flex;
} Mark;

static Mark mark(const Aby *a) { return (Mark){a->ntrail, a->nvars, a->neigen, a->flex}; }

static void trail_push(Aby *a, TrailE t) {
    if (a->ntrail == a->ctrail) {
        size_t cap = a->ctrail ? a->ctrail * 2u : 1024u;
        TrailE *grown = realloc(a->trail, sizeof *grown * cap);
        if (!grown)
            mge_nomem(a->e);
        a->trail = grown;
        a->ctrail = cap;
    }
    a->trail[a->ntrail++] = t;
}

static void undo(Aby *a, Mark m) {
    while (a->ntrail > m.trail) {
        TrailE t = a->trail[--a->ntrail];
        if (t.kind == T_ASSIGN) {
            a->vars[t.var].val = NULL;
            --a->nassign;
            ++a->epoch;
        } else if (t.kind == T_LEVEL) {
            a->vars[t.var].level = t.old_level;
        } else if (t.kind == T_LEMMA) {
            a->lemmas[t.var].goal = NULL; /* its branch is gone */
        }
    }
    a->nvars = m.nvars;
    a->neigen = m.neigen;
    a->flex = m.flex;
}

static void assign(Aby *a, AV *x, MgTm *val) {
    uint32_t i = (uint32_t)(x - a->vars);
    trail_push(a, (TrailE){T_ASSIGN, i, 0});
    a->vars[i].val = val;
    ++a->nassign;
    ++a->epoch;
}

static void lower_level(Aby *a, AV *x, uint32_t level) {
    if (x->level <= level)
        return;
    trail_push(a, (TrailE){T_LEVEL, (uint32_t)(x - a->vars), x->level});
    x->level = level;
}

/* ── terms ────────────────────────────────────────────────────────────── */

static bool has_names(const MgTm *m) { return (m->facts & MG_TM_HAS_NAME) != 0; }

/* m with the assigned metavariables replaced. */
static MgTm *inst_r(Aby *a, MgTm *m);

static MgTm *inst(Aby *a, MgTm *m) {
    if (!has_names(m) || a->nassign == 0u)
        return m;
    if (!a->memo) {
        a->memo_cap = 1u << 16;
        a->memo = calloc(a->memo_cap, sizeof *a->memo);
        if (!a->memo)
            mge_nomem(a->e);
    }
    uint32_t slot = (uint32_t)(((uintptr_t)m >> 4) * 2654435761u) & (a->memo_cap - 1u);
    if (a->memo[slot].key == m && a->memo[slot].epoch == a->epoch)
        return a->memo[slot].val;
    MgTm *r = inst_r(a, m);
    a->memo[slot].key = m;
    a->memo[slot].val = r;
    a->memo[slot].epoch = a->epoch;
    return r;
}

static MgTm *inst_r(Aby *a, MgTm *m) {
    if (!has_names(m))
        return m;
    MgElab *e = a->e;
    switch (m->tag) {
    case MG_TM_NAME: {
        AV *x = meta_of(a, m);
        return x && x->val ? inst_r(a, x->val) : m;
    }
    case MG_TM_AP: {
        MgTm *f = inst_r(a, m->f), *x = inst_r(a, m->x);
        return f == m->f && x == m->x ? m : mge_tm_ap(e, f, x);
    }
    case MG_TM_IMP: {
        MgTm *f = inst_r(a, m->f), *x = inst_r(a, m->x);
        return f == m->f && x == m->x ? m : mge_tm_imp(e, f, x);
    }
    case MG_TM_LAM: {
        MgTm *f = inst_r(a, m->f);
        return f == m->f ? m : mge_tm_lam(e, m->tp, f);
    }
    case MG_TM_ALL: {
        MgTm *f = inst_r(a, m->f);
        return f == m->f ? m : mge_tm_all(e, m->tp, f);
    }
    case MG_TM_TAP: {
        MgTm *f = inst_r(a, m->f);
        return f == m->f ? m : mge_tm_tap(e, f, m->tp);
    }
    default:
        return m;
    }
}

static MgTm *norm(Aby *a, MgTm *m) { return mge_beta_eta_norm(a->e, inst(a, m)); }

enum { MAX_ARGS = 64 };

/* m = h args[0] ... args[n-1] */
static MgTm *spine(MgTm *m, MgTm **args, uint32_t *n) {
    uint32_t k = 0;
    MgTm *rev[MAX_ARGS];
    while (m->tag == MG_TM_AP && k < MAX_ARGS) {
        rev[k++] = m->x;
        m = m->f;
    }
    for (uint32_t i = 0; i < k; ++i)
        args[i] = rev[k - 1u - i];
    *n = k;
    return m;
}

/* m with the name `name` as de Bruijn index `depth` (m has no free index). */
static MgTm *abstract_name(Aby *a, MgTm *m, uint32_t name, uint32_t depth) {
    if (!has_names(m))
        return m;
    MgElab *e = a->e;
    switch (m->tag) {
    case MG_TM_NAME:
        return m->n == name ? mge_tm_db(e, depth) : m;
    case MG_TM_AP: return mge_tm_ap(e, abstract_name(a, m->f, name, depth), abstract_name(a, m->x, name, depth));
    case MG_TM_IMP: return mge_tm_imp(e, abstract_name(a, m->f, name, depth), abstract_name(a, m->x, name, depth));
    case MG_TM_LAM: return mge_tm_lam(e, m->tp, abstract_name(a, m->f, name, depth + 1u));
    case MG_TM_ALL: return mge_tm_all(e, m->tp, abstract_name(a, m->f, name, depth + 1u));
    case MG_TM_TAP: return mge_tm_tap(e, abstract_name(a, m->f, name, depth), m->tp);
    default: return m;
    }
}

/* does the metavariable x occur in m (assignments followed)? */
static bool occurs(Aby *a, const AV *x, MgTm *m) {
    if (!has_names(m))
        return false;
    switch (m->tag) {
    case MG_TM_NAME: {
        AV *y = meta_of(a, m);
        if (y == x)
            return true;
        return y && y->val && occurs(a, x, y->val);
    }
    case MG_TM_AP:
    case MG_TM_IMP: return occurs(a, x, m->f) || occurs(a, x, m->x);
    case MG_TM_LAM:
    case MG_TM_ALL:
    case MG_TM_TAP: return occurs(a, x, m->f);
    default: return false;
    }
}

/* Can a metavariable that sees the eigenvariables below `level` stand for
 * m?  Metavariables in m that see more are restricted to `level`. */
static bool in_scope(Aby *a, MgTm *m, uint32_t level) {
    if (!has_names(m))
        return true;
    switch (m->tag) {
    case MG_TM_NAME: {
        int32_t i = var_of(a, m->n);
        if (i < 0)
            return true;
        AV *v = &a->vars[i];
        if (v->kind == V_EIGEN)
            return v->level < level;
        if (v->val)
            return in_scope(a, v->val, level);
        lower_level(a, v, level);
        return true;
    }
    case MG_TM_AP:
    case MG_TM_IMP: return in_scope(a, m->f, level) && in_scope(a, m->x, level);
    case MG_TM_LAM:
    case MG_TM_ALL:
    case MG_TM_TAP: return in_scope(a, m->f, level);
    default: return true;
    }
}

/* ── types ────────────────────────────────────────────────────────────── */

typedef struct TpS TpS;
struct TpS {
    MgTp *tp;
    const TpS *next;
};

static MgTp *type_of_r(Aby *a, MgTm *m, const TpS *b);

static MgTp *type_of_head(Aby *a, MgTm *m, const TpS *b) {
    MgElab *e = a->e;
    switch (m->tag) {
    case MG_TM_DB: {
        const TpS *s = b;
        for (uint32_t i = 0; s && i < m->n; ++i)
            s = s->next;
        return s ? s->tp : NULL;
    }
    case MG_TM_NAME: {
        int32_t i = var_of(a, m->n);
        if (i >= 0)
            return a->vars[i].tp;
        uint32_t levels;
        MgTp *d;
        if (!mge_sigtmof_get(e, m->n, &levels, &d) || levels != 0u)
            return NULL;
        return d;
    }
    case MG_TM_TAP: {
        MgTp *args[6];
        uint32_t k = 0;
        MgTm *h = m;
        while (h->tag == MG_TM_TAP && k < 6u) {
            args[5u - k] = h->tp;
            ++k;
            h = h->f;
        }
        if (k == 1u && h->tag == MG_TM_PRIM && h->n == 0u)
            return mge_ty_arr(e, mge_ty_arr(e, m->tp, mge_ty_prop(e)), m->tp);
        if (h->tag == MG_TM_NAME) {
            uint32_t levels;
            MgTp *d;
            if (!mge_sigtmof_get(e, h->n, &levels, &d) || levels != k)
                return NULL;
            return mge_tpsubst(e, d, args + 6u - k, k);
        }
        return NULL;
    }
    case MG_TM_PRIM:
        switch (m->n) {
        case 1: return mge_ty_arr(e, mge_ty_set(e), mge_ty_arr(e, mge_ty_set(e), mge_ty_prop(e)));
        case 2: return mge_ty_set(e);
        case 3:
        case 4:
        case 6: return mge_ty_arr(e, mge_ty_set(e), mge_ty_set(e));
        case 5:
            return mge_ty_arr(e, mge_ty_set(e),
                              mge_ty_arr(e, mge_ty_arr(e, mge_ty_set(e), mge_ty_set(e)), mge_ty_set(e)));
        default: return NULL;
        }
    default:
        return type_of_r(a, m, b);
    }
}

static MgTp *type_of_r(Aby *a, MgTm *m, const TpS *b) {
    MgElab *e = a->e;
    switch (m->tag) {
    case MG_TM_AP: {
        MgTp *f = type_of_r(a, m->f, b);
        return f && f->tag == MG_TP_ARR ? f->b : NULL;
    }
    case MG_TM_LAM: {
        TpS inner = {m->tp, b};
        MgTp *r = type_of_r(a, m->f, &inner);
        return r ? mge_ty_arr(e, m->tp, r) : NULL;
    }
    case MG_TM_IMP:
    case MG_TM_ALL: return mge_ty_prop(e);
    case MG_TM_DB:
    case MG_TM_NAME:
    case MG_TM_TAP:
    case MG_TM_PRIM: return type_of_head(a, m, b);
    default: return NULL;
    }
}

static MgTp *type_of(Aby *a, MgTm *m) { return type_of_r(a, m, NULL); }

/* ── unification ──────────────────────────────────────────────────────── */

static const UPair *upair(Aby *a, MgTm *s, MgTm *t, const UPair *next) {
    UPair *p = aalloc(a, sizeof *p);
    *p = (UPair){s, t, next};
    return p;
}

static bool unify_pairs(Aby *a, const UPair *ps, unsigned fuel, const Kont *k);

/* lam x1..xn. body over the n argument types, innermost last. */
static MgTm *lams(Aby *a, MgTp *const *tps, uint32_t n, MgTm *body) {
    for (uint32_t i = n; i-- > 0;)
        body = mge_tm_lam(a->e, tps[i], body);
    return body;
}

/* A fresh metavariable applied to x1..xn (de Bruijn n-1 .. 0), of type
 * xtps -> r, seeing what x sees. */
static MgTm *fresh_app(Aby *a, uint32_t level, MgTp *const *xtps, uint32_t n, MgTp *r) {
    MgTp *tp = r;
    for (uint32_t i = n; i-- > 0;)
        tp = mge_ty_arr(a->e, xtps[i], tp);
    AV *y = new_var(a, V_META, tp);
    y->level = level;
    MgTm *m = y->term;
    for (uint32_t i = 0; i < n; ++i)
        m = mge_tm_ap(a->e, m, mge_tm_db(a->e, n - 1u - i));
    return m;
}

/* x args = t, t rigid.  (Variables are made on the way: x is held by its
 * index, the table may move.) */
#define X (&a->vars[xi])
static bool flex_rigid(Aby *a, AV *x0, MgTm **args, uint32_t n, MgTm *s, MgTm *t, const UPair *rest,
                       unsigned fuel, const Kont *k) {
    MgElab *e = a->e;
    uint32_t xi = (uint32_t)(x0 - a->vars);
    const uint32_t xlevel = x0->level;
    AV *x = x0;
    /* the argument types */
    MgTp *xt = x->tp, *atps[MAX_ARGS];
    (void)x;
    for (uint32_t i = 0; i < n; ++i) {
        if (!xt || xt->tag != MG_TP_ARR)
            return false;
        atps[i] = xt->a;
        xt = xt->b;
    }
    MgTp *target = xt;
    /* a pattern: distinct eigenvariables x cannot see */
    bool pattern = true;
    for (uint32_t i = 0; i < n && pattern; ++i) {
        AV *v = eigen_of(a, args[i]);
        pattern = v && v->level >= xlevel;
        for (uint32_t j = 0; j < i && pattern; ++j)
            pattern = args[j] != args[i];
    }
    if (pattern) {
        if (occurs(a, X, t))
            return false;
        Mark m0 = mark(a);
        MgTm *body = t;
        for (uint32_t i = 0; i < n; ++i)
            body = abstract_name(a, body, args[i]->n, n - 1u - i);
        /* what remains must be visible to x */
        if (!in_scope(a, body, xlevel)) {
            undo(a, m0);
            return false;
        }
        assign(a, X, lams(a, atps, n, body));
        if (unify_pairs(a, rest, fuel, k))
            return true;
        undo(a, m0);
        return false;
    }
    if (fuel == 0u || !step(a))
        return false;
    /* projections, then imitation */
    MgTm *targs[MAX_ARGS];
    uint32_t tn;
    MgTm *th = spine(t, targs, &tn);
    for (uint32_t i = 0; i < n; ++i) {
        MgTp *pt = atps[i], *ptps[MAX_ARGS];
        uint32_t pn = 0;
        while (pt && pt != target && pt->tag == MG_TP_ARR && pn < MAX_ARGS) {
            ptps[pn++] = pt->a;
            pt = pt->b;
        }
        if (pt != target)
            continue;
        Mark m0 = mark(a);
        MgTm *body = mge_tm_db(e, n - 1u - i);
        for (uint32_t j = 0; j < pn; ++j)
            body = mge_tm_ap(e, body, fresh_app(a, xlevel, atps, n, ptps[j]));
        assign(a, X, lams(a, atps, n, body));
        if (unify_pairs(a, upair(a, s, t, rest), fuel - 1u, k))
            return true;
        undo(a, m0);
    }
    /* imitation: a rigid head x may name */
    bool imitable = false;
    MgTm *ih = NULL;
    if (t->tag == MG_TM_IMP || t->tag == MG_TM_ALL) {
        imitable = true;
    } else if (th->tag == MG_TM_NAME || th->tag == MG_TM_PRIM || th->tag == MG_TM_TAP) {
        AV *v = eigen_of(a, th);
        imitable = v ? v->level < xlevel : meta_of(a, th) == NULL;
        ih = th;
    }
    if (!imitable)
        return false;
    Mark m0 = mark(a);
    MgTm *body;
    if (t->tag == MG_TM_IMP) {
        body = mge_tm_imp(e, fresh_app(a, xlevel, atps, n, mge_ty_prop(e)), fresh_app(a, xlevel, atps, n, mge_ty_prop(e)));
    } else if (t->tag == MG_TM_ALL) {
        MgTp *xtps[MAX_ARGS + 1];
        if (n + 1u > MAX_ARGS)
            return false;
        memcpy(xtps, atps, sizeof(MgTp *) * n);
        xtps[n] = t->tp;
        body = mge_tm_all(e, t->tp, fresh_app(a, xlevel, xtps, n + 1u, mge_ty_prop(e)));
    } else {
        MgTp *ht = type_of(a, ih);
        body = ih;
        for (uint32_t j = 0; j < tn; ++j) {
            if (!ht || ht->tag != MG_TP_ARR) {
                undo(a, m0);
                return false;
            }
            body = mge_tm_ap(e, body, fresh_app(a, xlevel, atps, n, ht->a));
            ht = ht->b;
        }
    }
    assign(a, X, lams(a, atps, n, body));
    if (unify_pairs(a, upair(a, s, t, rest), fuel - 1u, k))
        return true;
    undo(a, m0);
    return false;
}
#undef X

static bool same_rigid_head(const MgTm *s, const MgTm *t) {
    if (s == t)
        return true;
    if (s->tag != t->tag)
        return false;
    switch (s->tag) {
    case MG_TM_NAME:
    case MG_TM_PRIM:
    case MG_TM_DB: return s->n == t->n;
    case MG_TM_TAP: return s->tp == t->tp && same_rigid_head(s->f, t->f);
    default: return false;
    }
}

static MgTm *unfold_head(Aby *a, MgTm *m);

/* Rigid sides that differ at the head: equal only through a definition
 * at a head, unfolded lazily (one side, then the other), at a unit of
 * fuel each. */
static bool unify_delta(Aby *a, MgTm *s, MgTm *t, const UPair *rest, unsigned fuel, const Kont *k) {
    if (fuel == 0u)
        return false;
    MgTm *su = s->tag == MG_TM_ALL || s->tag == MG_TM_IMP ? NULL : unfold_head(a, s);
    MgTm *tu = t->tag == MG_TM_ALL || t->tag == MG_TM_IMP ? NULL : unfold_head(a, t);
    Mark m0 = mark(a);
    if (su && unify_pairs(a, upair(a, su, t, rest), fuel - 1u, k))
        return true;
    undo(a, m0);
    if (a->out_of_steps)
        return false;
    if (tu && unify_pairs(a, upair(a, s, tu, rest), fuel - 1u, k))
        return true;
    undo(a, m0);
    return false;
}

static bool unify_pairs(Aby *a, const UPair *ps, unsigned fuel, const Kont *k) {
    MgElab *e = a->e;
    for (;;) {
        if (!ps)
            return kcall(a, k);
        if (!step(a))
            return false;
        MgTm *s = norm(a, ps->s), *t = norm(a, ps->t);
        const UPair *rest = ps->next;
        if (s == t) {
            ps = rest;
            continue;
        }
        if (s->tag == MG_TM_LAM || t->tag == MG_TM_LAM) {
            MgTp *tp = s->tag == MG_TM_LAM ? s->tp : t->tp;
            if (s->tag == MG_TM_LAM && t->tag == MG_TM_LAM && s->tp != t->tp)
                return false;
            MgTm *c = new_var(a, V_EIGEN, tp)->term;
            MgTm *s2 = s->tag == MG_TM_LAM ? mge_tm_subst0(e, s->f, c) : mge_tm_ap(e, s, c);
            MgTm *t2 = t->tag == MG_TM_LAM ? mge_tm_subst0(e, t->f, c) : mge_tm_ap(e, t, c);
            ps = upair(a, s2, t2, rest);
            continue;
        }
        MgTm *sa[MAX_ARGS], *ta[MAX_ARGS];
        uint32_t sn, tn;
        MgTm *sh = spine(s, sa, &sn), *th = spine(t, ta, &tn);
        AV *sx = meta_of(a, sh), *tx = meta_of(a, th);
        if (sx && tx) {
            if (sx == tx && sn == tn) {
                const UPair *more = rest;
                for (uint32_t i = sn; i-- > 0;)
                    more = upair(a, sa[i], ta[i], more);
                /* same head: arguments equal is one solution; others are
                 * left aside */
                ps = more;
                continue;
            }
            a->flex = upair(a, s, t, a->flex);
            ps = rest;
            continue;
        }
        if (sx)
            return flex_rigid(a, sx, sa, sn, s, t, rest, fuel, k);
        if (tx)
            return flex_rigid(a, tx, ta, tn, t, s, rest, fuel, k);
        bool binders_differ = (s->tag == MG_TM_ALL || t->tag == MG_TM_ALL || s->tag == MG_TM_IMP ||
                               t->tag == MG_TM_IMP) &&
                              (s->tag != t->tag || (s->tag == MG_TM_ALL && s->tp != t->tp));
        if (binders_differ || ((s->tag != MG_TM_ALL && s->tag != MG_TM_IMP) && (!same_rigid_head(sh, th) || sn != tn)))
            return unify_delta(a, s, t, rest, fuel, k);
        if (s->tag == MG_TM_ALL) {
            MgTm *c = new_var(a, V_EIGEN, s->tp)->term;
            ps = upair(a, mge_tm_subst0(e, s->f, c), mge_tm_subst0(e, t->f, c), rest);
            continue;
        }
        if (s->tag == MG_TM_IMP) {
            ps = upair(a, s->f, t->f, upair(a, s->x, t->x, rest));
            continue;
        }
        const UPair *more = rest;
        for (uint32_t i = sn; i-- > 0;)
            more = upair(a, sa[i], ta[i], more);
        ps = more;
    }
}

enum { UNIFY_FUEL = 4 };

static bool unify(Aby *a, MgTm *s, MgTm *t, const Kont *k) { return unify_pairs(a, upair(a, s, t, NULL), UNIFY_FUEL, k); }

/* ── definitions ──────────────────────────────────────────────────────── */

/* m with the definition at its head unfolded and beta-reduced, or NULL. */
static MgTm *unfold_head(Aby *a, MgTm *m) {
    MgElab *e = a->e;
    MgTm *args[MAX_ARGS];
    uint32_t n;
    MgTm *h = spine(m, args, &n);
    MgTp *types[6];
    uint32_t k = 0;
    MgTm *c = h;
    while (c->tag == MG_TM_TAP && k < 6u) {
        types[5u - k] = c->tp;
        ++k;
        c = c->f;
    }
    if (c->tag != MG_TM_NAME || var_of(a, c->n) >= 0)
        return NULL;
    const PTm *d = mge_delta_of(e, c->n);
    if (!d || d->levels != k)
        return NULL;
    MgTm *body = k ? mge_tm_tpsubst(e, d->m, types + 6u - k, k) : d->m;
    for (uint32_t i = 0; i < n; ++i)
        body = mge_tm_ap(e, body, args[i]);
    return norm(a, body);
}

/* ── proofs under construction ────────────────────────────────────────── */

/* Does m name a metavariable, assigned or not?  (Backtracking may undo an
 * assignment, so only a proposition naming none has a fixed shape.) */
static bool mentions_meta(Aby *a, MgTm *m) {
    if (!has_names(m))
        return false;
    switch (m->tag) {
    case MG_TM_NAME: return meta_of(a, m) != NULL;
    case MG_TM_AP:
    case MG_TM_IMP: return mentions_meta(a, m->f) || mentions_meta(a, m->x);
    case MG_TM_LAM:
    case MG_TM_ALL:
    case MG_TM_TAP: return mentions_meta(a, m->f);
    default: return false;
    }
}


static APf *apf(Aby *a, uint8_t tag) {
    APf *p = aalloc(a, sizeof *p);
    p->tag = tag;
    return p;
}

static const HypE *hyp(Aby *a, MgTm *prop, uint8_t kind, const HypE *next) {
    HypE *h = aalloc(a, sizeof *h);
    h->prop = prop;
    h->kind = kind;
    h->pos = -1;
    h->qflex = -1;
    h->eqn = -1;
    h->uid = ++a->hyps;
    h->next = next;
    h->ground = !mentions_meta(a, prop) && (!next || next->ground);
    return h;
}

/* ctx with h marked eliminated (the list up to h copied). */
static const HypE *mark_done(Aby *a, const HypE *ctx, const HypE *h) {
    if (!ctx)
        return NULL;
    HypE *c = aalloc(a, sizeof *c);
    *c = *ctx;
    if (ctx == h) {
        c->done = true;
        return c;
    }
    c->next = mark_done(a, ctx->next, h);
    return c;
}

/* ── the search ───────────────────────────────────────────────────────── */

/* MG_ABY_TRACE=1: each goal the search takes up, on stderr (a
 * development aid). */
static int g_trace = -1;

static void trace_tm(Aby *a, const char *what, MgTm *m, uint32_t depth) {
    if (g_trace < 0) {
        const char *t = getenv("MG_ABY_TRACE");
        g_trace = t && t[0] == '1';
    }
    if (!g_trace)
        return;
    fprintf(stderr, "[aby] %*s%s d=%u ", (int)(a->steps % 1u), "", what, depth);
    mg_print_tm(stderr, a->e->s, m);
    fputc('\n', stderr);
}


static bool prove(Aby *a, MgTm *g, const HypE *ctx, uint32_t depth, APf **hole, const Kont *k);
static bool is_false(Aby *a, MgTm *g);

/* A positive encoding: forall p:prop, B1 -> ... -> Bk -> p, each Bi ending
 * in p after its own binders and premises, p free nowhere else, and some
 * Bi with a premise or binder (True-like ones give nothing). */
static bool branch_ok(MgTm *b, uint32_t depth) {
    bool gives = false;
    for (;;) {
        if (b->tag == MG_TM_ALL) {
            gives = true;
            b = b->f;
            ++depth;
        } else if (b->tag == MG_TM_IMP) {
            if (mg_tm_free_db(b->f, depth))
                return false;
            gives = true;
            b = b->x;
        } else {
            return b->tag == MG_TM_DB && b->n == depth && gives;
        }
    }
}

/* m, or the unfolding of its head until a universal shows (NULL if none
 * does within a few unfoldings). */
static MgTm *expose_all(Aby *a, MgTm *m) {
    for (int i = 0; m && m->tag != MG_TM_ALL && i < 6; ++i)
        m = m->tag == MG_TM_IMP ? NULL : unfold_head(a, m);
    return m && m->tag == MG_TM_ALL ? m : NULL;
}

static bool positive_prop(Aby *a, MgTm *prop) {
    MgTm *p = expose_all(a, norm(a, prop));
    if (!p || p->tp->tag != MG_TP_PROP)
        return false;
    MgTm *b = p->f;
    while (b->tag == MG_TM_IMP) {
        if (!branch_ok(b->f, 0u))
            return false;
        b = b->x;
    }
    return b->tag == MG_TM_DB && b->n == 0u;
}

static bool positive(Aby *a, const HypE *h) {
    if (h->pos >= 0)
        return h->pos == 1;
    bool pos = positive_prop(a, h->prop);
    if (!mentions_meta(a, h->prop))
        ((HypE *)h)->pos = pos ? 1 : 0;
    return pos;
}

/* Prove the premises in order. */
typedef struct Prem Prem;
struct Prem {
    MgTm *prop;
    APf **hole;
    const Prem *next;
};

typedef struct {
    const Prem *prems;
    const HypE *ctx;
    uint32_t depth;
} PremK;

static bool k_prems(Aby *a, const Kont *k);

/* The premises in any order (each fills its own hole): a ground one
 * first, so that premises that fix metavariables come before those that
 * would guess them. */
static bool prove_prems(Aby *a, const Prem *prems, const HypE *ctx, uint32_t depth, const Kont *k) {
    if (!prems)
        return kcall(a, k);
    const Prem *pick = prems;
    for (const Prem *p = prems; p; p = p->next)
        if (!mentions_meta(a, norm(a, p->prop))) {
            pick = p;
            break;
        }
    const Prem *rest = prems->next;
    if (pick != prems) {
        /* the list without pick, the others in their order */
        Prem *head = NULL, **tail = &head;
        for (const Prem *p = prems; p; p = p->next) {
            if (p == pick)
                continue;
            Prem *q = aalloc(a, sizeof *q);
            *q = (Prem){p->prop, p->hole, NULL};
            *tail = q;
            tail = (Prem **)&q->next;
        }
        rest = head;
    }
    PremK *d = aalloc(a, sizeof *d);
    *d = (PremK){rest, ctx, depth};
    Kont next = {k_prems, d, k};
    return prove(a, pick->prop, ctx, depth, pick->hole, &next);
}

static bool k_prems(Aby *a, const Kont *k) {
    const PremK *d = k->data;
    return prove_prems(a, d->prems, d->ctx, d->depth, k->next);
}

/* After the conclusion unified: the proof is in place, the premises
 * remain. */
typedef struct {
    APf **hole;
    APf *proof;
    const Prem *prems;
    const HypE *ctx;
    uint32_t depth;
    MgTm *goal;
} UnifiedK;

static bool k_unified(Aby *a, const Kont *k) {
    const UnifiedK *d = k->data;
    /* a premise that is the goal itself, in the same context, is a detour:
     * never a shorter proof */
    MgTm *g = norm(a, d->goal);
    for (const Prem *p = d->prems; p; p = p->next)
        if (norm(a, p->prop) == g)
            return false;
    *d->hole = d->proof;
    return prove_prems(a, d->prems, d->ctx, d->depth, k->next);
}

/* Is g False (forall p:prop, p), folded or not? */
static bool is_false(Aby *a, MgTm *g) {
    MgTm *p = g->tag == MG_TM_ALL ? g : expose_all(a, g);
    return p && p->tp->tag == MG_TP_PROP && p->f->tag == MG_TM_DB && p->f->n == 0u;
}

/* Backchain on the proposition c (a hypothesis's, partly peeled; proof is
 * its proof so far, prems its premises so far, newest first). */
static bool backchain(Aby *a, MgTm *c, APf *proof, const Prem *prems_rev, MgTm *g, const HypE *ctx,
                      uint32_t depth, APf **hole, const Kont *k, unsigned unfolds) {
    if (!step(a))
        return false;
    c = norm(a, c);
    /* the conclusion here; one with a metavariable at its head matches
     * almost any goal, so using a fact that way (forward through a
     * positive conclusion, or an equation as a rewrite) costs a level
     * more */
    MgTm *cargs[MAX_ARGS];
    uint32_t cn;
    bool bare = meta_of(a, spine(c, cargs, &cn)) != NULL;
    /* concluding in a bare propositional variable on a False goal (dneg,
     * an elimination) gives nothing False itself would not */
    bool useless = bare && cn == 0u && is_false(a, g);
    if (!useless && (!bare || depth > 0u)) {
        uint32_t pdepth = bare ? depth - 1u : depth;
        Mark m0 = mark(a);
        /* premises in their order */
        const Prem *prems = NULL;
        for (const Prem *p = prems_rev; p; p = p->next) {
            Prem *q = aalloc(a, sizeof *q);
            *q = (Prem){p->prop, p->hole, prems};
            prems = q;
        }
        UnifiedK *d = aalloc(a, sizeof *d);
        *d = (UnifiedK){hole, proof, prems, ctx, pdepth, g};
        Kont next = {k_unified, d, k};
        if (unify(a, c, g, &next))
            return true;
        undo(a, m0);
        if (a->out_of_steps)
            return false;
    }
    MgElab *e = a->e;
    if (c->tag == MG_TM_ALL) {
        Mark m0 = mark(a);
        AV *x = new_var(a, V_META, c->tp);
        APf *p = apf(a, P_TMAP);
        p->p = proof;
        p->t = x->term;
        if (backchain(a, mge_tm_subst0(e, c->f, x->term), p, prems_rev, g, ctx, depth, hole, k, unfolds))
            return true;
        undo(a, m0);
        return false;
    }
    if (c->tag == MG_TM_IMP) {
        APf *p = apf(a, P_PFAP);
        p->p = proof;
        Prem *q = aalloc(a, sizeof *q);
        *q = (Prem){c->f, &p->q, prems_rev};
        return backchain(a, c->x, p, q, g, ctx, depth, hole, k, unfolds);
    }
    if (unfolds > 0u) {
        MgTm *u = unfold_head(a, c);
        if (u)
            return backchain(a, u, proof, prems_rev, g, ctx, depth, hole, k, unfolds - 1u);
    }
    return false;
}

enum { PEEL_UNFOLDS = 3 };

/* How a proposition's conclusions can meet a goal with head gh (a rigid
 * head), seen on its de Bruijn form without substituting anything: at
 * each depth of peeling (and through PEEL_UNFOLDS unfoldings), the
 * conclusion's head is one of the fact's own bound variables (it would
 * become a metavariable: flexible), the goal's head (rigid), or another
 * rigid head (no match there).  M_RIGID: some depth matches gh rigidly;
 * M_FLEX: only flexible depths; M_NONE: none can. */
enum { M_NONE = 0, M_FLEX = 1, M_RIGID = 2 };

static int meets(Aby *a, MgTm *c, MgTm *gh, uint32_t bound, unsigned unfolds) {
    int best = M_NONE;
    for (int guard = 0; guard < 64; ++guard) {
        if (c->tag == MG_TM_ALL) {
            c = c->f;
            ++bound;
            continue;
        }
        if (c->tag == MG_TM_IMP) {
            c = c->x;
            continue;
        }
        MgTm *args[MAX_ARGS];
        uint32_t n;
        MgTm *h = spine(c, args, &n);
        if (h->tag == MG_TM_DB && h->n < bound)
            return best > M_FLEX ? best : M_FLEX; /* flexible: matches, deeper too */
        if (h->tag == MG_TM_NAME && meta_of(a, h))
            return best > M_FLEX ? best : M_FLEX;
        if (same_rigid_head(h, gh))
            best = M_RIGID;
        if (unfolds == 0u)
            return best;
        MgTm *u = unfold_head(a, c);
        if (!u)
            return best;
        c = u;
        --unfolds;
    }
    return best;
}

/* Can c (a fact's proposition, de Bruijn form) close a quantified goal
 * whole: at some depth of peeling, a universal whose conclusion has at
 * its head one of the fact's own bound variables (an induction principle,
 * forall p, ... -> forall n, ... -> p n)?  Such a fact must meet the goal
 * before its binders are introduced, or the predicate is lost. */
static bool quantified_flex(MgTm *c) {
    uint32_t bound = 0;
    for (int guard = 0; guard < 64; ++guard) {
        if (c->tag == MG_TM_IMP) {
            c = c->x;
            continue;
        }
        if (c->tag != MG_TM_ALL)
            return false;
        if (bound > 0u) {
            /* under this universal: its conclusion's head */
            MgTm *d = c;
            uint32_t inner = 0;
            while (d->tag == MG_TM_ALL || d->tag == MG_TM_IMP) {
                if (d->tag == MG_TM_ALL)
                    ++inner;
                d = d->tag == MG_TM_ALL ? d->f : d->x;
            }
            MgTm *args[MAX_ARGS];
            uint32_t n;
            MgTm *h = spine(d, args, &n);
            if (h->tag == MG_TM_DB && h->n >= inner && h->n < inner + bound)
                return true;
        }
        c = c->f;
        ++bound;
    }
    return false;
}

static APf *hyp_ref(Aby *a, const HypE *h) {
    APf *p = apf(a, P_HYP);
    p->h = h;
    return p;
}

/* ── equational rewriting ─────────────────────────────────────────────── */

/* c as an equation l = r: Megalodon's eq, forall Q:A->A->prop, Q l r ->
 * Q r l, folded or not; *tp is A. */
static bool equation(Aby *a, MgTm *c, MgTm **l, MgTm **r, MgTp **tp) {
    MgTm *p = expose_all(a, c);
    if (!p)
        return false;
    MgTp *qt = p->tp;
    if (qt->tag != MG_TP_ARR || qt->b->tag != MG_TP_ARR || qt->b->b->tag != MG_TP_PROP || qt->a != qt->b->a)
        return false;
    MgTm *b = p->f;
    if (b->tag != MG_TM_IMP)
        return false;
    MgTm *x = b->f, *y = b->x;
    if (x->tag != MG_TM_AP || x->f->tag != MG_TM_AP || x->f->f->tag != MG_TM_DB || x->f->f->n != 0u ||
        y->tag != MG_TM_AP || y->f->tag != MG_TM_AP || y->f->f->tag != MG_TM_DB || y->f->f->n != 0u)
        return false;
    MgTm *L = x->f->x, *R = x->x;
    if (y->f->x != R || y->x != L || mg_tm_free_db(L, 0u) || mg_tm_free_db(R, 0u))
        return false;
    *l = mge_tmshift(a->e, 0u, -1, L);
    *r = mge_tmshift(a->e, 0u, -1, R);
    *tp = qt->a;
    return true;
}

/* m with every occurrence of the subterm s replaced by n (s has no free
 * index: it occurs nowhere a binder of m would capture). */
static MgTm *replace_tm(Aby *a, MgTm *m, MgTm *s, MgTm *n) {
    if (m == s)
        return n;
    if (m->height <= s->height)
        return m;
    MgElab *e = a->e;
    switch (m->tag) {
    case MG_TM_AP: return mge_tm_ap(e, replace_tm(a, m->f, s, n), replace_tm(a, m->x, s, n));
    case MG_TM_IMP: return mge_tm_imp(e, replace_tm(a, m->f, s, n), replace_tm(a, m->x, s, n));
    case MG_TM_LAM: return mge_tm_lam(e, m->tp, replace_tm(a, m->f, s, mge_tmshift(e, 0u, 1, n)));
    case MG_TM_ALL: return mge_tm_all(e, m->tp, replace_tm(a, m->f, s, mge_tmshift(e, 0u, 1, n)));
    case MG_TM_TAP: return mge_tm_tap(e, replace_tm(a, m->f, s, n), m->tp);
    default: return m;
    }
}

enum { MAX_SUBTERMS = 48 };

/* The closed subterms of m that are applications or constants (the places
 * a rewrite can act), outermost first, at most MAX_SUBTERMS. */
static void subterms(Aby *a, MgTm *m, MgTm **out, uint32_t *n, uint32_t under) {
    if (*n >= MAX_SUBTERMS)
        return;
    if (m->fv == 0u && (m->tag == MG_TM_AP || m->tag == MG_TM_NAME || m->tag == MG_TM_PRIM || m->tag == MG_TM_TAP)) {
        bool seen = false;
        for (uint32_t i = 0; i < *n && !seen; ++i)
            seen = out[i] == m;
        if (!seen)
            out[(*n)++] = m;
    }
    (void)under;
    switch (m->tag) {
    case MG_TM_AP:
    case MG_TM_IMP:
        subterms(a, m->f, out, n, under);
        subterms(a, m->x, out, n, under);
        break;
    case MG_TM_LAM:
    case MG_TM_ALL:
        subterms(a, m->f, out, n, under + 1u);
        break;
    case MG_TM_TAP:
        subterms(a, m->f, out, n, under);
        break;
    default:
        break;
    }
}

typedef struct {
    MgTm *g, *s, *to;      /* rewrite s (matched) to `to` in g */
    MgTp *tp;
    bool forward;          /* s matched r: Q := fun u v => C[u]; else C[v] */
    APf *eq;               /* the equation's proof, applied */
    const Prem *prems;     /* its premises */
    const HypE *ctx;
    uint32_t depth;
    APf **hole;
} RewriteK;

static bool k_rewrite(Aby *a, const Kont *k) {
    const RewriteK *d = k->data;
    MgElab *e = a->e;
    MgTm *g = norm(a, d->g), *s = norm(a, d->s), *to = norm(a, d->to);
    MgTm *g2 = norm(a, replace_tm(a, g, s, to));
    if (g2 == g)
        return false;
    /* Q: C with the occurrence as u (fun u v => C[u]) or as v */
    AV *z = new_var(a, V_EIGEN, d->tp);
    MgTm *cz = replace_tm(a, g, s, z->term);
    MgTm *q = mge_tm_lam(e, d->tp, mge_tm_lam(e, d->tp, abstract_name(a, cz, z->name, d->forward ? 1u : 0u)));
    APf *tq = apf(a, P_TMAP);
    tq->p = d->eq;
    tq->t = q;
    APf *use = apf(a, P_PFAP);
    use->p = tq;
    *d->hole = use;
    Prem *goal = aalloc(a, sizeof *goal);
    *goal = (Prem){g2, &use->q, d->prems};
    return prove_prems(a, goal, d->ctx, d->depth, k->next);
}

/* Rewrite the goal with the equation h concludes (after its binders, as
 * metavariables, and its premises, as subgoals): wherever one side of it
 * meets a subterm of the goal, that subterm becomes the other side. */
static bool rewrite_with(Aby *a, const HypE *h, MgTm *g, const HypE *ctx, uint32_t depth, APf **hole,
                         const Kont *k) {
    MgElab *e = a->e;
    MgTm *c = norm(a, h->prop);
    APf *proof = hyp_ref(a, h);
    const Prem *prems_rev = NULL;
    MgTm *l, *r;
    MgTp *tp;
    for (int guard = 0;; ++guard) {
        if (guard > 32 || !step(a))
            return false;
        if (equation(a, c, &l, &r, &tp))
            break;
        if (c->tag == MG_TM_ALL) {
            AV *x = new_var(a, V_META, c->tp);
            APf *t = apf(a, P_TMAP);
            t->p = proof;
            t->t = x->term;
            proof = t;
            c = norm(a, mge_tm_subst0(e, c->f, x->term));
        } else if (c->tag == MG_TM_IMP) {
            APf *ap = apf(a, P_PFAP);
            ap->p = proof;
            Prem *q = aalloc(a, sizeof *q);
            *q = (Prem){c->f, &ap->q, prems_rev};
            prems_rev = q;
            proof = ap;
            c = c->x;
        } else {
            return false;
        }
    }
    const Prem *prems = NULL;
    for (const Prem *p = prems_rev; p; p = p->next) {
        Prem *q = aalloc(a, sizeof *q);
        *q = (Prem){p->prop, p->hole, prems};
        prems = q;
    }
    MgTm *subs[MAX_SUBTERMS];
    uint32_t ns = 0;
    subterms(a, g, subs, &ns, 0u);
    for (int dir = 0; dir < 2; ++dir) {
        MgTm *side = norm(a, dir == 0 ? r : l), *other = dir == 0 ? l : r;
        MgTm *sargs[MAX_ARGS];
        uint32_t sn;
        MgTm *sh = spine(side, sargs, &sn);
        if (meta_of(a, sh))
            continue; /* a flexible side meets everything */
        for (uint32_t i = 0; i < ns; ++i) {
            MgTm *targs[MAX_ARGS];
            uint32_t tn;
            if (!same_rigid_head(sh, spine(subs[i], targs, &tn)) || tn != sn)
                continue;
            if (type_of(a, subs[i]) != tp)
                continue;
            Mark m0 = mark(a);
            RewriteK *d = aalloc(a, sizeof *d);
            *d = (RewriteK){g, subs[i], other, tp, dir == 0, proof, prems, ctx, depth, hole};
            Kont next = {k_rewrite, d, k};
            if (unify(a, side, subs[i], &next))
                return true;
            undo(a, m0);
            if (a->out_of_steps || a->cut_to)
                return false;
        }
    }
    return false;
}

/* Does h's proposition conclude in an equation (seen once, through a
 * scratch peeling that is undone)? */
static bool concludes_equation(Aby *a, const HypE *h) {
    if (h->eqn >= 0)
        return h->eqn == 1;
    Mark m0 = mark(a);
    MgTm *c = norm(a, h->prop), *l, *r;
    MgTp *tp;
    bool yes = false;
    for (int guard = 0; guard < 32; ++guard) {
        if (equation(a, c, &l, &r, &tp)) {
            yes = true;
            break;
        }
        if (c->tag == MG_TM_ALL)
            c = norm(a, mge_tm_subst0(a->e, c->f, new_var(a, V_META, c->tp)->term));
        else if (c->tag == MG_TM_IMP)
            c = c->x;
        else
            break;
    }
    undo(a, m0);
    if (!mentions_meta(a, h->prop))
        ((HypE *)h)->eqn = yes ? 1 : 0;
    return yes;
}

/* ── classical reductio ───────────────────────────────────────────────── */

/* m as not A (A -> False, folded or not): A, else NULL. */
static MgTm *negation_of(Aby *a, MgTm *m) {
    MgTm *n = m;
    for (int i = 0; n && n->tag != MG_TM_IMP && i < 4; ++i)
        n = unfold_head(a, n);
    return n && n->tag == MG_TM_IMP && is_false(a, norm(a, n->x)) ? n->f : NULL;
}

/* A fact forall P:prop, not (not P) -> P (dneg) or forall P:prop, P \/ not P
 * (xm), seen on its opened proposition. */
static bool dneg_shaped(Aby *a, MgTm *prop) {
    MgTm *p = norm(a, prop);
    if (p->tag != MG_TM_ALL || p->tp->tag != MG_TP_PROP || p->f->tag != MG_TM_IMP)
        return false;
    AV *x = new_var(a, V_EIGEN, p->tp);
    MgTm *b = norm(a, mge_tm_subst0(a->e, p->f, x->term));
    if (b->tag != MG_TM_IMP || b->x != x->term)
        return false;
    MgTm *nn = negation_of(a, b->f);
    MgTm *n = nn ? negation_of(a, norm(a, nn)) : NULL;
    return n && norm(a, n) == x->term;
}

static bool xm_shaped(Aby *a, MgTm *prop) {
    MgTm *p = norm(a, prop);
    if (p->tag != MG_TM_ALL || p->tp->tag != MG_TP_PROP)
        return false;
    AV *x = new_var(a, V_EIGEN, p->tp);
    MgTm *b = expose_all(a, norm(a, mge_tm_subst0(a->e, p->f, x->term)));
    /* forall q:prop, (P -> q) -> (not P -> q) -> q */
    if (!b || b->tp->tag != MG_TP_PROP || b->f->tag != MG_TM_IMP || b->f->x->tag != MG_TM_IMP)
        return false;
    MgTm *l = b->f->f, *r = b->f->x->f, *c = b->f->x->x;
    if (c->tag != MG_TM_DB || c->n != 0u || l->tag != MG_TM_IMP || r->tag != MG_TM_IMP)
        return false;
    MgTm *lp = mge_tmshift(a->e, 0u, -1, l->f), *rp = mge_tmshift(a->e, 0u, -1, r->f);
    MgTm *n = negation_of(a, norm(a, rp));
    return norm(a, lp) == x->term && n && norm(a, n) == x->term && l->x->tag == MG_TM_DB &&
           l->x->n == 0u && r->x->tag == MG_TM_DB && r->x->n == 0u;
}

/* Is not g a hypothesis already? */
static bool assumes_negation(Aby *a, const HypE *ctx, MgTm *g) {
    for (const HypE *h = ctx; h; h = h->next) {
        MgTm *n = negation_of(a, norm(a, h->prop));
        if (n && norm(a, n) == g)
            return true;
    }
    return false;
}

/* g by reductio: assume not g, prove False, through the named dneg
 * (dneg g (fun h => F)) or xm (xm g g (fun x => x) (fun h => F g)). */
static bool reductio(Aby *a, MgTm *g, const HypE *ctx, uint32_t depth, APf **hole, const Kont *k) {
    MgElab *e = a->e;
    MgTm *f = NULL;
    /* False, as the fact states it (folded) */
    MgTm *ng = NULL;
    {
        /* not g: g -> False, False as forall p:prop, p */
        f = mge_tm_all(e, mge_ty_prop(e), mge_tm_db(e, 0u));
        ng = mge_tm_imp(e, g, f);
    }
    const HypE *h = hyp(a, ng, H_INT, ctx);
    APf *lam = apf(a, P_PLAM);
    lam->h = h;
    lam->t = ng;
    if (a->dneg) {
        APf *t = apf(a, P_TMAP);
        t->p = hyp_ref(a, a->dneg);
        t->t = g;
        APf *use = apf(a, P_PFAP);
        use->p = t;
        use->q = lam;
        *hole = use;
        return prove(a, f, h, depth, &lam->p, k);
    }
    /* xm g g (fun x => x) (fun h => F g) */
    APf *t1 = apf(a, P_TMAP);
    t1->p = hyp_ref(a, a->xm);
    t1->t = g;
    APf *t2 = apf(a, P_TMAP);
    t2->p = t1;
    t2->t = g;
    const HypE *xh = hyp(a, g, H_INT, ctx);
    APf *id = apf(a, P_PLAM);
    id->h = xh;
    id->t = g;
    id->p = hyp_ref(a, xh);
    APf *a1 = apf(a, P_PFAP);
    a1->p = t2;
    a1->q = id;
    APf *a2 = apf(a, P_PFAP);
    a2->p = a1;
    a2->q = lam;
    APf *fg = apf(a, P_TMAP);
    fg->t = g;
    lam->p = fg;
    *hole = a2;
    return prove(a, f, h, depth, &fg->p, k);
}

/* An atomic goal: eliminate a positive hypothesis, else backchain on a
 * hypothesis or fact, else rewrite it with an equation, else (with a
 * classical fact named) by reductio, else unfold the goal. */
static bool prove_atomic(Aby *a, MgTm *g, const HypE *ctx, uint32_t depth, APf **hole, const Kont *k) {
    for (const HypE *h = ctx; h; h = h->next) {
        if (h->done || !positive(a, h))
            continue;
        MgTm *p = expose_all(a, norm(a, h->prop));
        if (!p)
            continue;
        trace_tm(a, "eliminate", h->prop, depth);
        /* invertible: the only way on from here */
        const HypE *ctx2 = mark_done(a, ctx, h);
        APf *proof = apf(a, P_TMAP);
        proof->p = hyp_ref(a, h);
        proof->t = g;
        MgTm *c = mge_tm_subst0(a->e, p->f, g);
        Prem *prems_rev = NULL;
        APf *cur = proof;
        while (c->tag == MG_TM_IMP) {
            APf *ap = apf(a, P_PFAP);
            ap->p = cur;
            Prem *q = aalloc(a, sizeof *q);
            *q = (Prem){c->f, &ap->q, prems_rev};
            prems_rev = q;
            cur = ap;
            c = c->x;
        }
        const Prem *prems = NULL;
        for (const Prem *q = prems_rev; q; q = q->next) {
            Prem *r = aalloc(a, sizeof *r);
            *r = (Prem){q->prop, q->hole, prems};
            prems = r;
        }
        *hole = cur;
        return prove_prems(a, prems, ctx2, depth, k);
    }
    if (depth == 0u)
        return false;
    /* hypotheses (newest first), then the facts: first those whose
     * conclusion can meet the goal's head rigidly, then those that meet it
     * only through a conclusion of their own bound variable */
    MgTm *gargs[MAX_ARGS];
    uint32_t gn;
    MgTm *gh = spine(g, gargs, &gn);
    bool gflex = meta_of(a, gh) != NULL || g->tag == MG_TM_IMP || g->tag == MG_TM_ALL;
    for (int round = 0; round < 2; ++round) {
        for (int pass = 0; pass < 2; ++pass) {
            for (const HypE *h = pass ? a->facts : ctx; h; h = h->next) {
                if (!gflex) {
                    int how = meets(a, h->prop, gh, 0u, PEEL_UNFOLDS);
                    if (how == M_NONE || (round == 0) != (how == M_RIGID))
                        continue;
                } else if (round == 1) {
                    continue;
                }
                Mark m0 = mark(a);
                if (backchain(a, h->prop, hyp_ref(a, h), NULL, g, ctx, depth - 1u, hole, k, PEEL_UNFOLDS))
                    return true;
                undo(a, m0);
                if (a->out_of_steps)
                    return false;
            }
        }
    }
    for (int pass = 0; pass < 2 && depth > 0u; ++pass) {
        for (const HypE *h = pass ? a->facts : ctx; h; h = h->next) {
            if (!concludes_equation(a, h))
                continue;
            Mark m0 = mark(a);
            if (rewrite_with(a, h, g, ctx, depth - 1u, hole, k))
                return true;
            undo(a, m0);
            if (a->out_of_steps || a->cut_to)
                return false;
        }
    }
    if ((a->dneg || a->xm) && depth > 0u && !is_false(a, g) && !mentions_meta(a, g) && !assumes_negation(a, ctx, g)) {
        Mark m0 = mark(a);
        if (reductio(a, g, ctx, depth - 1u, hole, k))
            return true;
        undo(a, m0);
        if (a->out_of_steps || a->cut_to)
            return false;
    }
    MgTm *u = unfold_head(a, g);
    if (u)
        return prove(a, u, ctx, depth - 1u, hole, k);
    return false;
}

/* ── ground goals ─────────────────────────────────────────────────────── */

/* A goal with no metavariable, in a context with none, shares nothing
 * with the rest of the proof: its first proof is as good as any other (no
 * alternatives are tried after it), and one that had no proof at a depth
 * has none at that depth or less (the failure memo). */
static size_t failed_slot(const Aby *a, const MgTm *g, const HypE *ctx) {
    uint64_t h = ((uint64_t)(uintptr_t)g * UINT64_C(0x9e3779b97f4a7c15)) ^ ((uint64_t)(uintptr_t)ctx >> 4);
    return (size_t)(h >> 17) & (a->failed_cap - 1u);
}

static bool known_failed(const Aby *a, const MgTm *g, const HypE *ctx, uint32_t depth) {
    if (!a->failed_cap)
        return false;
    for (size_t i = failed_slot(a, g, ctx);; i = (i + 1u) & (a->failed_cap - 1u)) {
        if (!a->failed[i].goal)
            return false;
        if (a->failed[i].goal == g && a->failed[i].ctx == ctx)
            return depth <= a->failed[i].depth;
    }
}

static void note_failed(Aby *a, const MgTm *g, const HypE *ctx, uint32_t depth) {
    if ((a->failed_len + 1u) * 2u > a->failed_cap) {
        uint32_t cap = a->failed_cap ? a->failed_cap * 2u : 4096u;
        struct Failed *old = a->failed;
        uint32_t ocap = a->failed_cap;
        a->failed = calloc(cap, sizeof *a->failed);
        if (!a->failed)
            mge_nomem(a->e);
        a->failed_cap = cap;
        a->failed_len = 0;
        for (uint32_t i = 0; i < ocap; ++i)
            if (old[i].goal)
                note_failed(a, old[i].goal, old[i].ctx, old[i].depth);
        free(old);
    }
    for (size_t i = failed_slot(a, g, ctx);; i = (i + 1u) & (a->failed_cap - 1u)) {
        if (!a->failed[i].goal) {
            a->failed[i].goal = g;
            a->failed[i].ctx = ctx;
            a->failed[i].depth = depth;
            ++a->failed_len;
            return;
        }
        if (a->failed[i].goal == g && a->failed[i].ctx == ctx) {
            if (depth > a->failed[i].depth)
                a->failed[i].depth = depth;
            return;
        }
    }
}

typedef struct {
    bool proved;
    MgTm *goal;
    const HypE *ctx;
    APf **hole;
} GroundK;

/* The newest live lemma for (g, ctx), or NULL. */
static APf *lemma_of(const Aby *a, const MgTm *g, const HypE *ctx) {
    for (uint32_t i = a->nlemmas; i-- > 0;)
        if (a->lemmas[i].goal == g && a->lemmas[i].ctx == ctx)
            return a->lemmas[i].proof;
    return NULL;
}

static void lemma_add(Aby *a, MgTm *g, const HypE *ctx, APf *proof) {
    /* forgotten lemmas at the top are reused */
    while (a->nlemmas && !a->lemmas[a->nlemmas - 1u].goal)
        --a->nlemmas;
    if (a->nlemmas == a->clemmas) {
        uint32_t cap = a->clemmas ? a->clemmas * 2u : 256u;
        struct Lemma *grown = realloc(a->lemmas, sizeof *grown * cap);
        if (!grown)
            mge_nomem(a->e);
        a->lemmas = grown;
        a->clemmas = cap;
    }
    uint32_t i = a->nlemmas++;
    a->lemmas[i] = (struct Lemma){g, ctx, proof};
    trail_push(a, (TrailE){T_LEMMA, i, 0});
}

/* The first proof of a ground goal goes on to the rest of the proof; if
 * that fails, the goal's search stops (a->cut_to: every step refuses
 * until the goal's own frame takes the cut back). */
static bool k_ground(Aby *a, const Kont *k) {
    GroundK *seen = (GroundK *)k->data;
    if (seen->proved)
        return false;
    seen->proved = true;
    lemma_add(a, seen->goal, seen->ctx, *seen->hole);
    if (kcall(a, k->next))
        return true;
    a->cut_to = seen;
    return false;
}

static bool prove_r(Aby *a, MgTm *g, const HypE *ctx, uint32_t depth, APf **hole, const Kont *k);

static bool prove(Aby *a, MgTm *g0, const HypE *ctx, uint32_t depth, APf **hole, const Kont *k) {
    if (!step(a))
        return false;
    MgTm *g = norm(a, g0);
    if (mentions_meta(a, g) || (ctx && !ctx->ground))
        return prove_r(a, g, ctx, depth, hole, k);
    if (known_failed(a, g, ctx, depth))
        return false;
    APf *known = lemma_of(a, g, ctx);
    if (known) {
        *hole = known;
        return kcall(a, k);
    }
    GroundK seen = {false, g, ctx, hole};
    Kont once = {k_ground, &seen, k};
    if (prove_r(a, g, ctx, depth, hole, &once))
        return true;
    if (a->cut_to == &seen)
        a->cut_to = NULL;
    if (!seen.proved && !a->out_of_steps && !a->cut_to)
        note_failed(a, g, ctx, depth);
    return false;
}

static bool prove_r(Aby *a, MgTm *g, const HypE *ctx, uint32_t depth, APf **hole, const Kont *k) {
    MgElab *e = a->e;
    trace_tm(a, "goal", g, depth);
    /* a quantified goal meets the induction-like facts whole first */
    if (g->tag == MG_TM_ALL && depth > 0u) {
        for (const HypE *h = a->facts; h; h = h->next) {
            if (h->qflex < 0)
                ((HypE *)h)->qflex = quantified_flex(h->prop) ? 1 : 0;
            if (!h->qflex)
                continue;
            Mark m0 = mark(a);
            if (backchain(a, h->prop, hyp_ref(a, h), NULL, g, ctx, depth - 1u, hole, k, 0u))
                return true;
            undo(a, m0);
            if (a->out_of_steps || a->cut_to)
                return false;
        }
    }
    if (g->tag == MG_TM_IMP) {
        const HypE *h = hyp(a, g->f, H_INT, ctx);
        APf *p = apf(a, P_PLAM);
        p->h = h;
        p->t = g->f;
        *hole = p;
        return prove(a, g->x, h, depth, &p->p, k);
    }
    if (g->tag == MG_TM_ALL && is_false(a, g)) {
        /* False stays a goal to meet (by what concludes it), not an
         * arbitrary proposition to prove */
        return prove_atomic(a, g, ctx, depth, hole, k);
    }
    if (g->tag == MG_TM_ALL) {
        AV *c = new_var(a, V_EIGEN, g->tp);
        APf *p = apf(a, P_TLAM);
        p->tp = g->tp;
        p->ev = (uint32_t)(c - a->vars);
        *hole = p;
        return prove(a, mge_tm_subst0(e, g->f, c->term), ctx, depth, &p->p, k);
    }
    return prove_atomic(a, g, ctx, depth, hole, k);
}

/* ── closing the proof ────────────────────────────────────────────────── */

static MgPf *pfk(MgElab *e, MgPf *p) {
    if (!p)
        mge_nomem(e);
    return p;
}

static MgPf *apf_hyp(MgElab *e, uint32_t n) { return pfk(e, mg_pf_hyp(e->s, n)); }
static MgPf *apf_tmap(MgElab *e, MgPf *p, MgTm *t) { return pfk(e, mg_pf_tmap(e->s, p, t)); }
static MgPf *apf_pfap(MgElab *e, MgPf *p, MgPf *q) { return pfk(e, mg_pf_pfap(e->s, p, q)); }
static MgPf *apf_plam(MgElab *e, MgTm *t, MgPf *p) { return pfk(e, mg_pf_plam(e->s, t, p)); }
static MgPf *apf_tlam(MgElab *e, MgTp *a, MgPf *p) { return pfk(e, mg_pf_tlam(e->s, a, p)); }
static MgPf *apf_tpap(MgElab *e, MgPf *p, MgTp *a) { return pfk(e, mg_pf_tpap(e->s, p, a)); }

typedef struct Bind Bind;
struct Bind {
    bool term;            /* a TLam (else a PLam) */
    uint32_t ev;          /* TLam: the eigenvariable */
    const HypE *h;        /* PLam: the hypothesis */
    const Bind *next;
};

/* A closed instance for an unassigned metavariable of type tp. */
static MgTm *default_of(Aby *a, MgTp *tp) {
    MgElab *e = a->e;
    if (tp->tag == MG_TP_ARR) {
        MgTm *b = default_of(a, tp->b);
        return b ? mge_tm_lam(e, tp->a, mge_tmshift(e, 0u, 1, b)) : NULL;
    }
    if (tp->tag == MG_TP_PROP)
        return mge_tm_all(e, mge_ty_prop(e), mge_tm_imp(e, mge_tm_db(e, 0u), mge_tm_db(e, 0u)));
    if (tp == mge_ty_set(e))
        return mge_tm_prim(e, 2u);
    return NULL;
}

/* m over the binders b (term binders counted) and the goal's context:
 * names of eigenvariables become de Bruijn indices; depth counts the
 * binders inside m above the current subterm. */
static MgTm *close_tm(Aby *a, MgTm *m, const Bind *b, uint32_t depth, bool *ok) {
    MgElab *e = a->e;
    if (!has_names(m))
        return m; /* closed: search terms have no free index */
    switch (m->tag) {
    case MG_TM_NAME: {
        int32_t i = var_of(a, m->n);
        if (i < 0)
            return m;
        AV *v = &a->vars[i];
        if (v->kind == V_META) {
            if (v->val)
                return close_tm(a, norm(a, v->val), b, depth, ok);
            MgTm *d = default_of(a, v->tp);
            if (!d) {
                *ok = false;
                return m;
            }
            assign(a, v, d);
            return close_tm(a, d, b, depth, ok);
        }
        uint32_t k = 0;
        for (const Bind *s = b; s; s = s->next) {
            if (!s->term)
                continue;
            if (s->ev == (uint32_t)i)
                return mge_tm_db(e, depth + k);
            ++k;
        }
        if (v->ctx_db >= 0)
            return mge_tm_db(e, depth + k + (uint32_t)v->ctx_db);
        *ok = false; /* an eigenvariable outside its binder */
        return m;
    }
    case MG_TM_AP: return mge_tm_ap(e, close_tm(a, m->f, b, depth, ok), close_tm(a, m->x, b, depth, ok));
    case MG_TM_IMP: return mge_tm_imp(e, close_tm(a, m->f, b, depth, ok), close_tm(a, m->x, b, depth, ok));
    case MG_TM_LAM: return mge_tm_lam(e, m->tp, close_tm(a, m->f, b, depth + 1u, ok));
    case MG_TM_ALL: return mge_tm_all(e, m->tp, close_tm(a, m->f, b, depth + 1u, ok));
    case MG_TM_TAP: return mge_tm_tap(e, close_tm(a, m->f, b, depth, ok), m->tp);
    default: return m;
    }
}

/* A fact's proof, given in the goal's context, under the binders b. */
static MgPf *shift_pf(MgElab *e, MgPf *d, uint32_t hcut, uint32_t hby, uint32_t tcut, uint32_t tby) {
    if (!hby && !tby)
        return d;
    switch (d->tag) {
    case MG_PF_HYP: return apf_hyp(e, d->n >= hcut ? d->n + hby : d->n);
    case MG_PF_TMAP:
        return apf_tmap(e, shift_pf(e, d->p, hcut, hby, tcut, tby), mge_tmshift(e, tcut, (int32_t)tby, d->t));
    case MG_PF_PFAP:
        return apf_pfap(e, shift_pf(e, d->p, hcut, hby, tcut, tby), shift_pf(e, d->q, hcut, hby, tcut, tby));
    case MG_PF_PLAM:
        return apf_plam(e, mge_tmshift(e, tcut, (int32_t)tby, d->t), shift_pf(e, d->p, hcut + 1u, hby, tcut, tby));
    case MG_PF_TLAM:
        return apf_tlam(e, d->tp, shift_pf(e, d->p, hcut, hby, tcut + 1u, tby));
    case MG_PF_TPAP: return apf_tpap(e, shift_pf(e, d->p, hcut, hby, tcut, tby), d->tp);
    default: return d;
    }
}

static MgPf *close_pf(Aby *a, const APf *p, const Bind *b, bool *ok) {
    MgElab *e = a->e;
    if (!p) {
        *ok = false;
        return NULL;
    }
    switch (p->tag) {
    case P_HYP: {
        const HypE *h = p->h;
        uint32_t hk = 0, tk = 0;
        for (const Bind *s = b; s; s = s->next) {
            if (s->term) {
                ++tk;
                continue;
            }
            if (s->h->uid == h->uid)
                return apf_hyp(e, hk);
            ++hk;
        }
        if (h->kind == H_CTX)
            return apf_hyp(e, hk + h->index);
        if (h->kind == H_FACT)
            return shift_pf(e, h->fact, 0u, hk, 0u, tk);
        if (h->kind == H_DERIVED)
            return close_pf(a, h->derived, b, ok);
        *ok = false;
        return NULL;
    }
    case P_TMAP: return apf_tmap(e, close_pf(a, p->p, b, ok), close_tm(a, inst(a, p->t), b, 0u, ok));
    case P_PFAP: return apf_pfap(e, close_pf(a, p->p, b, ok), close_pf(a, p->q, b, ok));
    case P_TLAM: {
        Bind inner = {true, p->ev, NULL, b};
        return apf_tlam(e, p->tp, close_pf(a, p->p, &inner, ok));
    }
    case P_PLAM: {
        MgTm *t = close_tm(a, norm(a, p->t), b, 0u, ok);
        Bind inner = {false, 0u, p->h, b};
        return apf_plam(e, t, close_pf(a, p->p, &inner, ok));
    }
    default:
        *ok = false;
        return NULL;
    }
}

/* A flexible-flexible pair still standing at the end: both heads become
 * one fresh metavariable that ignores their arguments, when the result
 * types agree (a unifier, not the most general one). */
static bool flex_flex_trivial(Aby *a, MgTm *s, MgTm *t) {
    MgTm *sa[MAX_ARGS], *ta[MAX_ARGS];
    uint32_t sn, tn;
    AV *x = meta_of(a, spine(s, sa, &sn)), *y = meta_of(a, spine(t, ta, &tn));
    if (!x || !y)
        return false;
    uint32_t xi = (uint32_t)(x - a->vars), yi = (uint32_t)(y - a->vars);
    MgTp *xt = x->tp, *yt = y->tp, *xs[MAX_ARGS], *ys[MAX_ARGS];
    for (uint32_t i = 0; i < sn; ++i) {
        if (xt->tag != MG_TP_ARR)
            return false;
        xs[i] = xt->a;
        xt = xt->b;
    }
    for (uint32_t i = 0; i < tn; ++i) {
        if (yt->tag != MG_TP_ARR)
            return false;
        ys[i] = yt->a;
        yt = yt->b;
    }
    if (xt != yt)
        return false;
    uint32_t level = x->level < y->level ? x->level : y->level;
    AV *h = new_var(a, V_META, xt);
    h->level = level;
    MgTm *hm = h->term;
    MgElab *e = a->e;
    MgTm *xb = hm, *yb = hm;
    for (uint32_t i = 0; i < sn; ++i)
        xb = mge_tm_lam(e, xs[sn - 1u - i], xb);
    for (uint32_t i = 0; i < tn; ++i)
        yb = mge_tm_lam(e, ys[tn - 1u - i], yb);
    if (xi == yi)
        return sn == tn && xb == yb ? (assign(a, &a->vars[xi], xb), true) : false;
    assign(a, &a->vars[xi], xb);
    assign(a, &a->vars[yi], yb);
    return true;
}

static bool k_close(Aby *a, const Kont *k);

/* The last continuation: the postponed pairs are unified again (more is
 * known now) until none moves; what still stands gets the trivial
 * flexible-flexible unifier; then the proof closes (metavariables left
 * free get a closed instance of their type) and passes the double check.
 * Else the search goes on. */
static bool k_done(Aby *a, const Kont *k) {
    if (a->flex) {
        const UPair *pending = a->flex;
        bool moved = false;
        for (const UPair *f = pending; f && !moved; f = f->next) {
            MgTm *sa[MAX_ARGS], *ta[MAX_ARGS];
            uint32_t sn, tn;
            MgTm *s = norm(a, f->s), *t = norm(a, f->t);
            moved = s == t || !meta_of(a, spine(s, sa, &sn)) || !meta_of(a, spine(t, ta, &tn));
        }
        Mark m0 = mark(a);
        a->flex = NULL;
        if (moved && a->flex_rounds < 4u) {
            ++a->flex_rounds;
            bool found = unify_pairs(a, pending, UNIFY_FUEL, k);
            --a->flex_rounds;
            if (found)
                return true;
            undo(a, m0);
            return false;
        }
        for (const UPair *f = pending; f; f = f->next) {
            MgTm *s = norm(a, f->s), *t = norm(a, f->t);
            if (s != t && !flex_flex_trivial(a, s, t)) {
                undo(a, m0);
                return false;
            }
        }
        if (k_close(a, k))
            return true;
        undo(a, m0);
        return false;
    }
    return k_close(a, k);
}

static bool k_close(Aby *a, const Kont *k) {
    (void)k;
    Mark m0 = mark(a);
    bool ok = true;
    MgPf *d = close_pf(a, a->root, NULL, &ok);
    if (!ok || !d || !mge_check_pf_goal(a->e, a->goal, d)) {
        if (g_trace > 0 && a->rejected < 3u) {
            fprintf(stderr, "[aby] rejected (%s): ", ok ? "check" : "close");
            if (d)
                mg_print_pf(stderr, a->e->s, d);
            fprintf(stderr, "\n[aby]   %.*s\n", (int)a->e->msg.len, a->e->msg.data ? a->e->msg.data : "");
        }
        undo(a, m0);
        ++a->rejected;
        return false;
    }
    a->result = d;
    return true;
}

/* ── the entry ────────────────────────────────────────────────────────── */

static void aby_free(Aby *a) {
    for (ABlock *b = a->blocks; b;) {
        ABlock *n = b->next;
        free(b);
        b = n;
    }
    free(a->vars);
    free(a->trail);
    free(a->memo);
    free(a->failed);
    free(a->lemmas);
}

/* ── facts split at their conjunctions ────────────────────────────────── */

/* c as a conjunction: forall p:prop, (A1 -> ... -> Am -> p) -> p, m >= 2,
 * p free in no Ai (and, iff, nested ands, through their definitions).
 * Its components go to comps; m is returned, 0 if c is not one. */
static uint32_t conjunction(Aby *a, MgTm *c, MgTm **comps, uint32_t cap) {
    MgTm *p = expose_all(a, c);
    if (!p || p->tp->tag != MG_TP_PROP || p->f->tag != MG_TM_IMP)
        return 0;
    MgTm *branch = p->f->f, *rest = p->f->x;
    if (rest->tag != MG_TM_DB || rest->n != 0u)
        return 0; /* more than one branch: a disjunction */
    uint32_t m = 0;
    while (branch->tag == MG_TM_IMP) {
        if (mg_tm_free_db(branch->f, 0u) || m == cap)
            return 0;
        comps[m++] = mge_tmshift(a->e, 0u, -1, branch->f);
        branch = branch->x;
    }
    return branch->tag == MG_TM_DB && branch->n == 0u && m >= 2u ? m : 0u;
}

enum { MAX_SPLIT = 64 };

/* From h : forall xs, P1 -> ... -> Pn -> A1 /\ ... /\ Am (binders and
 * premises in any order), the facts with the same binders and premises
 * concluding Ai, each with its proof
 *   fun xs p1 .. pn => h xs p1 .. pn Ai (fun c1 .. cm => ci)
 * (arguments in the order of the binders), split again where Ai is a
 * conjunction; *added counts them. */
static const HypE *split_fact(Aby *a, const HypE *h, const HypE *facts, uint32_t *added, unsigned depth) {
    MgElab *e = a->e;
    if (depth > 4u || *added >= MAX_SPLIT)
        return facts;
    /* peel, in order: a binder (an eigenvariable of the derivation) or a
     * premise (a hypothesis of it) */
    struct {
        bool term;
        uint32_t ev;
        MgTp *tp;
        const HypE *prem;
    } steps[MAX_ARGS];
    uint32_t n = 0;
    MgTm *c = norm(a, h->prop);
    for (;;) {
        if (n == MAX_ARGS)
            return facts;
        if (c->tag == MG_TM_ALL) {
            AV *v = new_var(a, V_EIGEN, c->tp);
            steps[n].term = true;
            steps[n].ev = (uint32_t)(v - a->vars);
            steps[n].tp = c->tp;
            steps[n].prem = NULL;
            ++n;
            c = norm(a, mge_tm_subst0(e, c->f, v->term));
        } else if (c->tag == MG_TM_IMP) {
            steps[n].term = false;
            steps[n].ev = 0;
            steps[n].tp = NULL;
            steps[n].prem = hyp(a, c->f, H_INT, NULL);
            ++n;
            c = c->x;
        } else {
            break;
        }
    }
    MgTm *comps[16];
    uint32_t m = conjunction(a, c, comps, 16u);
    if (m == 0u)
        return facts;
    for (uint32_t i = 0; i < m && *added < MAX_SPLIT; ++i) {
        /* the proposition, its binders and premises as h has them */
        MgTm *prop = comps[i];
        for (uint32_t j = n; j-- > 0;)
            prop = steps[j].term
                ? mge_tm_all(e, steps[j].tp, abstract_name(a, prop, a->vars[steps[j].ev].name, 0u))
                : mge_tm_imp(e, steps[j].prem->prop, prop);
        /* its proof: h applied in the order of its binders */
        APf *app = apf(a, P_HYP);
        app->h = h;
        for (uint32_t j = 0; j < n; ++j) {
            if (steps[j].term) {
                APf *t = apf(a, P_TMAP);
                t->p = app;
                t->t = a->vars[steps[j].ev].term;
                app = t;
            } else {
                APf *q = apf(a, P_PFAP);
                q->p = app;
                q->q = apf(a, P_HYP);
                q->q->h = steps[j].prem;
                app = q;
            }
        }
        APf *elim = apf(a, P_TMAP);
        elim->p = app;
        elim->t = comps[i];
        /* fun c1 .. cm => ci */
        const HypE *cs[16];
        APf *lamroot = NULL, **lh = &lamroot;
        for (uint32_t j = 0; j < m; ++j) {
            cs[j] = hyp(a, comps[j], H_INT, NULL);
            APf *l = apf(a, P_PLAM);
            l->h = cs[j];
            l->t = comps[j];
            *lh = l;
            lh = &l->p;
        }
        APf *pick = apf(a, P_HYP);
        pick->h = cs[i];
        *lh = pick;
        APf *use = apf(a, P_PFAP);
        use->p = elim;
        use->q = lamroot;
        APf *root = NULL, **rh = &root;
        for (uint32_t j = 0; j < n; ++j) {
            APf *l;
            if (steps[j].term) {
                l = apf(a, P_TLAM);
                l->tp = steps[j].tp;
                l->ev = steps[j].ev;
            } else {
                l = apf(a, P_PLAM);
                l->h = steps[j].prem;
                l->t = steps[j].prem->prop;
            }
            *rh = l;
            rh = &l->p;
        }
        *rh = use;
        HypE *d = (HypE *)hyp(a, prop, H_DERIVED, facts);
        d->derived = root;
        facts = d;
        ++*added;
        facts = split_fact(a, d, facts, added, depth + 1u);
    }
    return facts;
}

/* m with the goal context's de Bruijn indices as their eigenvariables. */
static MgTm *open_tm(Aby *a, MgTm *m, MgTm *const *open, uint32_t n) {
    for (uint32_t i = 0; i < n && m->fv > 0u; ++i)
        m = mge_tm_subst0(a->e, m, open[i]);
    return m;
}

typedef struct {
    Aby *a;
    MgTm *goal;
    const HypE *ctx;
    uint32_t max_depth;
} SearchJob;

static void search_body(MgElab *e, void *ctx) {
    (void)e;
    SearchJob *j = ctx;
    Aby *a = j->a;
    char base;
    a->stack_base = &base;
    Kont done = {k_done, NULL, NULL};
    for (uint32_t depth = 1; depth <= j->max_depth && !a->result && !a->out_of_steps; ++depth) {
        Mark m0 = mark(a);
        if (prove(a, j->goal, j->ctx, depth, &a->root, &done))
            break;
        undo(a, m0);
    }
}

typedef struct {
    MgElab *e;
    SearchJob *job;
    int raised;
} StackJob;

static void *stack_main(void *arg) {
    StackJob *s = arg;
    s->raised = mge_try_run(s->e, search_body, s->job);
    return NULL;
}

/* search_body on a thread with a stack of ABY_STACK bytes (the caller
 * waits: the elaborator is used by one thread at a time), or here when no
 * thread can be made. */
static int run_on_stack(MgElab *e, SearchJob *job) {
    StackJob s = {e, job, 0};
    pthread_attr_t attr;
    pthread_t th;
    if (pthread_attr_init(&attr) == 0) {
        job->a->stack_use = ABY_STACK_USE;
        bool started = pthread_attr_setstacksize(&attr, ABY_STACK) == 0 &&
                       pthread_create(&th, &attr, stack_main, &s) == 0;
        pthread_attr_destroy(&attr);
        if (started) {
            pthread_join(th, NULL);
            return s.raised;
        }
    }
    job->a->stack_use = (size_t)4u << 20; /* the caller's stack: a few MB */
    return mge_try_run(e, search_body, job);
}

/* What mge_aby_prove sets up and searches with, kept where a raise in the
 * setup leaves it, so that it is freed whatever happens. */
typedef struct {
    Aby a;
    const char *const *names;
    uint32_t count;
    MgTm **open;
    const Cx **cxs;
    const Hy **hys;
    uint32_t usable, unusable;
    int raised;                /* the search's raise, 0 if none */
} AbyProve;

static void prove_body(MgElab *e, void *arg) {
    AbyProve *p = arg;
    Aby *a = &p->a;
    const PfSt *g = a->goal;
    uint32_t nt = 0, nh = 0;
    for (const Cx *c = g->ctxtm; c; c = c->next)
        nt += c->def ? 0u : 1u;
    for (const Hy *h = g->ctxpf; h; h = h->next)
        ++nh;
    p->open = malloc(sizeof *p->open * (nt ? nt : 1u));
    p->cxs = malloc(sizeof *p->cxs * (nt ? nt : 1u));
    p->hys = malloc(sizeof *p->hys * (nh ? nh : 1u));
    if (!p->open || !p->cxs || !p->hys)
        mge_nomem(e);
    MgTm **open = p->open;
    const Cx **cxs = p->cxs;
    const Hy **hys = p->hys;
    uint32_t k = 0;
    for (const Cx *c = g->ctxtm; c; c = c->next)
        if (!c->def)
            cxs[k++] = c;
    /* outermost first, so that each eigenvariable's level follows its
     * scope; de Bruijn 0 is the innermost */
    for (uint32_t i = nt; i-- > 0;) {
        AV *v = new_var(a, V_EIGEN, cxs[i]->tp);
        v->ctx_db = (int32_t)i;
        open[i] = v->term;
    }
    a->ctx_terms = nt;
    k = 0;
    for (const Hy *h = g->ctxpf; h; h = h->next)
        hys[k++] = h;
    const HypE *ctx = NULL;
    for (uint32_t i = nh; i-- > 0;) {
        HypE *h = (HypE *)hyp(a, open_tm(a, hys[i]->prop, open, nt), H_CTX, ctx);
        h->index = i;
        ctx = h;
    }
    const HypE *facts = NULL;
    for (uint32_t i = p->count; i-- > 0;) {
        MgPf *pf;
        MgTm *prop;
        if (!mge_aby_fact(e, p->names[i], g, &pf, &prop)) {
            ++p->unusable;
            continue;
        }
        if (pf->tag == MG_PF_HYP)
            continue; /* a hypothesis of the context: there already */
        HypE *h = (HypE *)hyp(a, open_tm(a, prop, open, nt), H_FACT, facts);
        h->fact = pf;
        facts = h;
        ++p->usable;
    }
    /* the facts with conjunctive conclusions split into their parts, the
     * parts after the facts (a step from the goal each) */
    uint32_t added = 0;
    const HypE *parts = NULL;
    for (const HypE *h = facts; h && h->kind == H_FACT; h = h->next)
        parts = split_fact(a, h, parts, &added, 0u);
    if (parts) {
        /* facts, then parts: the facts' list copied with the parts below,
         * every fact however many the step names */
        uint32_t nf = 0;
        for (const HypE *h = facts; h; h = h->next)
            ++nf;
        const HypE **rev = aalloc(a, sizeof *rev * (nf ? nf : 1u));
        nf = 0;
        for (const HypE *h = facts; h; h = h->next)
            rev[nf++] = h;
        const HypE *all = parts;
        for (uint32_t i = nf; i-- > 0;) {
            HypE *c = aalloc(a, sizeof *c);
            *c = *rev[i];
            c->next = all;
            all = c;
        }
        facts = all;
    }
    a->facts = facts;
    for (const HypE *h = facts; h; h = h->next) {
        Mark m0 = mark(a);
        if (!a->dneg && dneg_shaped(a, h->prop))
            a->dneg = h;
        else if (!a->xm && xm_shaped(a, h->prop))
            a->xm = h;
        undo(a, m0);
    }
    SearchJob job = {a, open_tm(a, g->claim, open, nt), ctx, e->aby_max_depth ? e->aby_max_depth : 8u};
    p->raised = run_on_stack(e, &job);
}

/* A search leaves only the proof it found: the terms it interned, and the
 * caches it filled, are released with the store mark taken before it
 * (mg_store_release, mg_store_release_keeping). */
MgPf *mge_aby_prove(MgElab *e, const PfSt *g, const char *const *names, uint32_t count, MgAbyStats *stats) {
    MgStoreMark mark;
    bool marked = mg_store_mark(e->s, &mark);
    AbyProve p = {.a = {.e = e, .max_steps = e->aby_max_steps ? e->aby_max_steps : 200000u, .goal = g},
                  .names = names, .count = count};
    /* a raise while setting up (no memory, a budget run out while a fact
     * is read) goes on after the cleanup, as it did before */
    int setup = mge_try_run(e, prove_body, &p);
    MgPf *result = setup || p.raised ? NULL : p.a.result;
    if (stats) {
        stats->steps = p.a.steps;
        stats->out_of_steps = p.a.out_of_steps;
        stats->facts = p.usable;
        stats->unusable_facts = p.unusable;
        stats->rejected = p.a.rejected;
        stats->raised = setup ? setup : p.raised;
    }
    free(p.open);
    free(p.cxs);
    free(p.hys);
    aby_free(&p.a);
    if (marked) {
        /* the proof found stays, made again after the release; without
         * room to remake it the step has no proof, which is sound */
        if (result)
            mg_store_release_keeping(e->s, &mark, &result);
        else
            mg_store_release(e->s, &mark);
    }
    if (setup)
        mge_raise_kind(e, setup);
    if (p.raised == RAISE_FATAL || p.raised == RAISE_EXHAUSTED)
        mge_raise_kind(e, p.raised);
    return result;
}
