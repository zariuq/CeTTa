#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_core.h"

#include <stdlib.h>
#include <string.h>

/*
 * Mathdata operations (MathdataKernel.lean / mathdata.ml).
 *
 * Exactness.  Term substitution follows Lean's Tm.instantiateAt; upstream
 * tmsubst does not descend into type regions.  The two agree on Protected
 * terms (SourceScopeInvariant.publication_substitution_agrees), and every
 * entry point of this kernel admits only Protected terms, so they agree on
 * everything this kernel computes.  Both shortcuts below (`fv <= depth`,
 * `ftv <= depth` leave a node unchanged) are exact identities of the Lean
 * operations.
 *
 * Normal forms.  mg_tm_nf computes the beta-eta normal form innermost
 * first, as upstream's tm_beta_eta_norm does pass by pass; beta-eta normal
 * forms are unique, so the result is the reference result whenever the
 * reference terminates (mg_tm_nf charges the budget for every node, so a
 * diverging computation on ill-typed data stops as EXHAUSTED/INTERRUPTED,
 * never as a refusal).
 *
 * Caches.  nf and Tm.shift 0 1 are cached on nodes (pure).  Other pure
 * operations use a bounded, lossy direct-mapped cache keyed by all their
 * arguments.  Environment-dependent results (delta-normal forms, inferred
 * types) record the revision they were computed at and are reused at a
 * revision R only when computed at or before R with no withdrawal in
 * between, and only when no absent name was consulted.
 */

/* ── statuses, budgets, checks ────────────────────────────────────────── */

const char *mg_status_name(MgStatus status) {
    switch (status) {
    case MG_OK: return "ok";
    case MG_REFUSED: return "refused";
    case MG_MALFORMED: return "malformed";
    case MG_EXHAUSTED: return "exhausted";
    case MG_INTERRUPTED: return "interrupted";
    case MG_NOMEM: return "no-memory";
    case MG_CAPACITY: return "capacity";
    case MG_UNFINISHED: return "unfinished";
    case MG_DENIED: return "denied";
    case MG_CONFLICT: return "conflict";
    }
    return "invalid";
}

void mg_budget_init(MgBudget *budget, uint64_t work_limit) {
    memset(budget, 0, sizeof(*budget));
    budget->work_limit = work_limit;
    budget->poll_countdown = MG_POLL_INTERVAL;
    budget->stop = MG_OK;
}

bool mg_budget_poll(MgBudget *budget) {
    if (!budget)
        return true;
    if (budget->stop != MG_OK)
        return false;
    if (budget->interrupted &&
        budget->interrupted(budget->interrupt_context)) {
        budget->stop = MG_INTERRUPTED;
        return false;
    }
    return true;
}

bool mg_budget_charge(MgBudget *budget, uint64_t units) {
    if (!budget)
        return true;
    if (budget->stop != MG_OK)
        return false;
    if (budget->work_limit &&
        (units > budget->work_limit ||
         budget->work_used > budget->work_limit - units)) {
        budget->work_used = budget->work_limit;
        budget->stop = MG_EXHAUSTED;
        return false;
    }
    budget->work_used += units;
    if (budget->poll_countdown <= units) {
        budget->poll_countdown = MG_POLL_INTERVAL;
        return mg_budget_poll(budget);
    }
    budget->poll_countdown -= (uint32_t)units;
    return true;
}

static _Thread_local uint32_t t_depth_limit;

void mg_set_thread_depth_limit(uint32_t limit) { t_depth_limit = limit; }

uint32_t mg_thread_depth_limit(void) {
    return t_depth_limit ? t_depth_limit : MG_DEFAULT_DEPTH_LIMIT;
}

void mg_check_init(MgCheck *c, MgEnv env, MgBudget *budget,
                   MgFootprint *footprint) {
    memset(c, 0, sizeof(*c));
    c->env = env;
    c->budget = budget;
    c->footprint = footprint;
    c->depth_limit = mg_thread_depth_limit();
    c->status = MG_OK;
}

bool mg_fail(MgCheck *c, MgStatus status, const char *reason) {
    if (c->status == MG_OK) {
        c->status = status;
        c->reason = reason;
    }
    return false;
}

static inline bool mg_live(MgCheck *c) {
    if (c->status != MG_OK)
        return false;
    if (!mg_budget_charge(c->budget, 1u))
        return mg_fail(c, c->budget->stop, "budget");
    return true;
}

/* Enter one recursion level; the matching leave is mg_leave. */
static inline bool mg_enter(MgCheck *c) {
    if (!mg_live(c))
        return false;
    if (++c->depth > c->depth_limit) {
        --c->depth;
        return mg_fail(c, MG_CAPACITY, "traversal depth");
    }
    return true;
}

static inline void mg_leave(MgCheck *c) { --c->depth; }

static inline MgStore *ST(MgCheck *c) { return &c->env.theory->store; }

static inline void *mg_oom(MgCheck *c) {
    mg_fail(c, MG_NOMEM, "allocation");
    return NULL;
}

/* ── the bounded operation cache ──────────────────────────────────────── */

typedef enum {
    OP_TP_SHIFT = 1,
    OP_TP_INST,
    OP_TM_SHIFT,
    OP_TM_TSHIFT,
    OP_TM_INST,
    OP_TM_TINST,
    OP_INFER,
} MgOp;

typedef struct {
    const void *a;
    const void *b;
    void *result;
    uint64_t rev;     /* environment-dependent entries */
    uint32_t param;
    int32_t amount;
    uint8_t op;
} MgCacheEntry;

#define MG_CACHE_BITS 16u
#define MG_CACHE_SIZE (1u << MG_CACHE_BITS)

static MgCacheEntry *cache_table(MgCheck *c) {
    MgStore *s = ST(c);
    if (!s->subst_memo) {
        s->subst_memo = mg_sys_alloc(sizeof(MgCacheEntry) * MG_CACHE_SIZE);
        if (s->subst_memo)
            memset(s->subst_memo, 0, sizeof(MgCacheEntry) * MG_CACHE_SIZE);
    }
    return s->subst_memo;
}

static inline uint32_t cache_slot(uint8_t op, const void *a, const void *b,
                                  uint32_t param, int32_t amount) {
    uint64_t h = (uint64_t)(uintptr_t)a * UINT64_C(0x9e3779b97f4a7c15);
    h ^= (uint64_t)(uintptr_t)b * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= ((uint64_t)op << 56) ^ ((uint64_t)param << 20) ^ (uint32_t)amount;
    h ^= h >> 29;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 32;
    return (uint32_t)h & (MG_CACHE_SIZE - 1u);
}

static void *cache_get(MgCheck *c, uint8_t op, const void *a, const void *b,
                       uint32_t param, int32_t amount, bool env_dependent) {
    MgCacheEntry *table = cache_table(c);
    if (!table)
        return NULL;
    MgCacheEntry *e = &table[cache_slot(op, a, b, param, amount)];
    if (e->op != op || e->a != a || e->b != b || e->param != param ||
        e->amount != amount)
        return NULL;
    if (env_dependent &&
        (e->rev > c->env.rev ||
         mg_theory_withdrawn_between(c->env.theory, e->rev, c->env.rev)))
        return NULL;
    return e->result;
}

static void cache_put(MgCheck *c, uint8_t op, const void *a, const void *b,
                      uint32_t param, int32_t amount, void *result) {
    MgCacheEntry *table = cache_table(c);
    if (!table || !result)
        return;
    MgCacheEntry *e = &table[cache_slot(op, a, b, param, amount)];
    *e = (MgCacheEntry){.a = a, .b = b, .result = result, .rev = c->env.rev,
                        .param = param, .amount = amount, .op = op};
}

/* ── types ────────────────────────────────────────────────────────────── */

MgTp *mg_tp_shift(MgCheck *c, MgTp *a, uint32_t cutoff, int32_t amount) {
    if (!a || amount == 0 || a->ftv <= cutoff)
        return a;
    if (cutoff == 0u && amount == 1 && a->shift1)
        return a->shift1;
    MgTp *hit = cache_get(c, OP_TP_SHIFT, a, NULL, cutoff, amount, false);
    if (hit)
        return hit;
    if (!mg_enter(c))
        return NULL;
    MgTp *r = NULL;
    switch (a->tag) {
    case MG_TP_VAR: {
        int64_t n = (int64_t)a->n + amount;
        if (n < 0 || n > (int64_t)UINT32_MAX - 1)
            mg_fail(c, MG_CAPACITY, "type index");
        else
            r = mg_tp_var(ST(c), (uint32_t)n);
        break;
    }
    case MG_TP_ARR: {
        MgTp *x = mg_tp_shift(c, a->a, cutoff, amount);
        MgTp *y = x ? mg_tp_shift(c, a->b, cutoff, amount) : NULL;
        r = y ? mg_tp_arr(ST(c), x, y) : NULL;
        break;
    }
    case MG_TP_ALL: {
        MgTp *x = mg_tp_shift(c, a->a, cutoff + 1u, amount);
        r = x ? mg_tp_all(ST(c), x) : NULL;
        break;
    }
    default:
        r = a;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    if (cutoff == 0u && amount == 1)
        a->shift1 = r;
    else
        cache_put(c, OP_TP_SHIFT, a, NULL, cutoff, amount, r);
    return r;
}

MgTp *mg_tp_instantiate(MgCheck *c, MgTp *a, uint32_t depth, MgTp *b) {
    if (!a || !b || a->ftv <= depth)
        return a;
    MgTp *hit = cache_get(c, OP_TP_INST, a, b, depth, 0, false);
    if (hit)
        return hit;
    if (!mg_enter(c))
        return NULL;
    MgTp *r = NULL;
    switch (a->tag) {
    case MG_TP_VAR:
        if (a->n < depth)
            r = a;
        else if (a->n == depth)
            r = mg_tp_shift(c, b, 0, (int32_t)depth);
        else
            r = mg_tp_var(ST(c), a->n - 1u);
        break;
    case MG_TP_ARR: {
        MgTp *x = mg_tp_instantiate(c, a->a, depth, b);
        MgTp *y = x ? mg_tp_instantiate(c, a->b, depth, b) : NULL;
        r = y ? mg_tp_arr(ST(c), x, y) : NULL;
        break;
    }
    case MG_TP_ALL: {
        MgTp *x = mg_tp_instantiate(c, a->a, depth + 1u, b);
        r = x ? mg_tp_all(ST(c), x) : NULL;
        break;
    }
    default:
        r = a;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    cache_put(c, OP_TP_INST, a, b, depth, 0, r);
    return r;
}

/* ── exact per-call memos ─────────────────────────────────────────────── */

/* Within one operation the result at a node depends on the node and on its
 * binder offset (cutoff / depth / term context), so (node, p1, p2) keys an
 * exact memo: a shared DAG is rebuilt once per distinct pair, never once
 * per path.  Small subterms skip the memo; it starts empty and allocates on
 * first use.  When it cannot grow it stops memoizing: still exact. */
#define MG_MEMO_MIN_HEIGHT 4u

typedef struct {
    const void *node;
    uint64_t p1;
    const void *p2;
    void *result;
} MemoSlot;

typedef struct {
    MemoSlot *slots;
    size_t cap, len;
    bool frozen;
} OpMemo;

static size_t opmemo_index(const void *node, uint64_t p1, const void *p2, size_t cap) {
    uint64_t h = (uint64_t)(uintptr_t)node * UINT64_C(0x9e3779b97f4a7c15);
    h ^= p1 * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= (uint64_t)(uintptr_t)p2 * UINT64_C(0x165667b19e3779f9);
    h ^= h >> 29;
    return (size_t)h & (cap - 1u);
}

static void *opmemo_get(const OpMemo *m, const void *node, uint64_t p1, const void *p2) {
    if (!m->cap)
        return NULL;
    for (size_t i = opmemo_index(node, p1, p2, m->cap);; i = (i + 1u) & (m->cap - 1u)) {
        const MemoSlot *e = &m->slots[i];
        if (!e->node)
            return NULL;
        if (e->node == node && e->p1 == p1 && e->p2 == p2)
            return e->result;
    }
}

static void opmemo_put(OpMemo *m, const void *node, uint64_t p1, const void *p2,
                       void *result) {
    if (m->frozen || !result)
        return;
    if ((m->len + 1u) * 2u > m->cap) {
        size_t cap = m->cap ? m->cap * 2u : 256u;
        MemoSlot *slots = mg_sys_alloc(sizeof(MemoSlot) * cap);
        if (!slots) {
            m->frozen = true;
            return;
        }
        memset(slots, 0, sizeof(MemoSlot) * cap);
        for (size_t j = 0; j < m->cap; ++j) {
            MemoSlot e = m->slots[j];
            if (!e.node)
                continue;
            size_t i = opmemo_index(e.node, e.p1, e.p2, cap);
            while (slots[i].node)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e;
        }
        mg_sys_free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    size_t i = opmemo_index(node, p1, p2, m->cap);
    while (m->slots[i].node)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (MemoSlot){node, p1, p2, result};
    ++m->len;
}

static void opmemo_free(OpMemo *m) {
    mg_sys_free(m->slots);
    memset(m, 0, sizeof(*m));
}

/* ── term shifts ──────────────────────────────────────────────────────── */

typedef struct {
    MgCheck *c;
    OpMemo memo;
    int32_t amount;
} ShiftRun;

static MgTm *shift_rec(ShiftRun *run, MgTm *m, uint32_t cutoff) {
    MgCheck *c = run->c;
    int32_t amount = run->amount;
    if (m->fv <= cutoff)
        return m;
    if (cutoff == 0u && amount == 1 && m->shift1)
        return m->shift1;
    bool memo = m->height >= MG_MEMO_MIN_HEIGHT;
    if (memo) {
        MgTm *hit = opmemo_get(&run->memo, m, cutoff, NULL);
        if (hit)
            return hit;
    }
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTm *r = NULL;
    switch (m->tag) {
    case MG_TM_DB: {
        int64_t n = (int64_t)m->n + amount;
        if (n < 0 || n > (int64_t)UINT32_MAX - 1)
            mg_fail(c, MG_CAPACITY, "term index");
        else
            r = mg_tm_db(s, (uint32_t)n);
        break;
    }
    case MG_TM_AP:
    case MG_TM_IMP: {
        MgTm *f = shift_rec(run, m->f, cutoff);
        MgTm *x = f ? shift_rec(run, m->x, cutoff) : NULL;
        r = x ? (m->tag == MG_TM_AP ? mg_tm_ap(s, f, x) : mg_tm_imp(s, f, x))
              : NULL;
        break;
    }
    case MG_TM_LAM:
    case MG_TM_ALL: {
        MgTm *b = shift_rec(run, m->f, cutoff + 1u);
        r = b ? (m->tag == MG_TM_LAM ? mg_tm_lam(s, m->tp, b)
                                     : mg_tm_all(s, m->tp, b))
              : NULL;
        break;
    }
    case MG_TM_TAP: {
        MgTm *f = shift_rec(run, m->f, cutoff);
        r = f ? mg_tm_tap(s, f, m->tp) : NULL;
        break;
    }
    case MG_TM_TLAM:
    case MG_TM_TALL: {
        MgTm *b = shift_rec(run, m->f, cutoff);
        r = b ? (m->tag == MG_TM_TLAM ? mg_tm_tlam(s, b) : mg_tm_tall(s, b))
              : NULL;
        break;
    }
    default:
        r = m;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    if (cutoff == 0u && amount == 1)
        m->shift1 = r;
    if (memo)
        opmemo_put(&run->memo, m, cutoff, NULL, r);
    return r;
}

MgTm *mg_tm_shift(MgCheck *c, MgTm *m, uint32_t cutoff, int32_t amount) {
    if (!m || amount == 0 || m->fv <= cutoff)
        return m;
    if (cutoff == 0u && amount == 1 && m->shift1)
        return m->shift1;
    MgTm *hit = cache_get(c, OP_TM_SHIFT, m, NULL, cutoff, amount, false);
    if (hit)
        return hit;
    ShiftRun run = {.c = c, .amount = amount};
    MgTm *r = shift_rec(&run, m, cutoff);
    opmemo_free(&run.memo);
    if (r && !(cutoff == 0u && amount == 1))
        cache_put(c, OP_TM_SHIFT, m, NULL, cutoff, amount, r);
    return r;
}

static MgTm *tshift_rec(ShiftRun *run, MgTm *m, uint32_t cutoff) {
    MgCheck *c = run->c;
    int32_t amount = run->amount;
    if (m->ftv <= cutoff)
        return m;
    bool memo = m->height >= MG_MEMO_MIN_HEIGHT;
    if (memo) {
        MgTm *hit = opmemo_get(&run->memo, m, cutoff, NULL);
        if (hit)
            return hit;
    }
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTm *r = NULL;
    switch (m->tag) {
    case MG_TM_AP:
    case MG_TM_IMP: {
        MgTm *f = tshift_rec(run, m->f, cutoff);
        MgTm *x = f ? tshift_rec(run, m->x, cutoff) : NULL;
        r = x ? (m->tag == MG_TM_AP ? mg_tm_ap(s, f, x) : mg_tm_imp(s, f, x))
              : NULL;
        break;
    }
    case MG_TM_LAM:
    case MG_TM_ALL: {
        MgTp *a = mg_tp_shift(c, m->tp, cutoff, amount);
        MgTm *b = a ? tshift_rec(run, m->f, cutoff) : NULL;
        r = b ? (m->tag == MG_TM_LAM ? mg_tm_lam(s, a, b) : mg_tm_all(s, a, b))
              : NULL;
        break;
    }
    case MG_TM_TAP: {
        MgTm *f = tshift_rec(run, m->f, cutoff);
        MgTp *a = f ? mg_tp_shift(c, m->tp, cutoff, amount) : NULL;
        r = a ? mg_tm_tap(s, f, a) : NULL;
        break;
    }
    case MG_TM_TLAM:
    case MG_TM_TALL: {
        MgTm *b = tshift_rec(run, m->f, cutoff + 1u);
        r = b ? (m->tag == MG_TM_TLAM ? mg_tm_tlam(s, b) : mg_tm_tall(s, b))
              : NULL;
        break;
    }
    default:
        r = m;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    if (memo)
        opmemo_put(&run->memo, m, cutoff, NULL, r);
    return r;
}

MgTm *mg_tm_tshift(MgCheck *c, MgTm *m, uint32_t cutoff, int32_t amount) {
    if (!m || amount == 0 || m->ftv <= cutoff)
        return m;
    MgTm *hit = cache_get(c, OP_TM_TSHIFT, m, NULL, cutoff, amount, false);
    if (hit)
        return hit;
    ShiftRun run = {.c = c, .amount = amount};
    MgTm *r = tshift_rec(&run, m, cutoff);
    opmemo_free(&run.memo);
    if (r)
        cache_put(c, OP_TM_TSHIFT, m, NULL, cutoff, amount, r);
    return r;
}

/* ── substitution ─────────────────────────────────────────────────────── */

typedef struct {
    MgCheck *c;
    OpMemo memo;
    MgTm *n;
    MgTp *b;
} SubstRun;

static MgTm *inst_rec(SubstRun *run, MgTm *m, uint32_t depth) {
    MgCheck *c = run->c;
    if (m->fv <= depth)
        return m;
    bool memo = m->height >= MG_MEMO_MIN_HEIGHT;
    if (memo) {
        MgTm *hit = opmemo_get(&run->memo, m, depth, NULL);
        if (hit)
            return hit;
    }
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTm *r = NULL;
    switch (m->tag) {
    case MG_TM_DB:
        if (m->n < depth)
            r = m;
        else if (m->n == depth)
            r = mg_tm_shift(c, run->n, 0, (int32_t)depth);
        else
            r = mg_tm_db(s, m->n - 1u);
        break;
    case MG_TM_AP:
    case MG_TM_IMP: {
        MgTm *f = inst_rec(run, m->f, depth);
        MgTm *x = f ? inst_rec(run, m->x, depth) : NULL;
        r = x ? (m->tag == MG_TM_AP ? mg_tm_ap(s, f, x) : mg_tm_imp(s, f, x))
              : NULL;
        break;
    }
    case MG_TM_LAM:
    case MG_TM_ALL: {
        MgTm *b = inst_rec(run, m->f, depth + 1u);
        r = b ? (m->tag == MG_TM_LAM ? mg_tm_lam(s, m->tp, b)
                                     : mg_tm_all(s, m->tp, b))
              : NULL;
        break;
    }
    case MG_TM_TAP: {
        /* Reached only by non-Protected data, which no entry admits. */
        MgTm *f = inst_rec(run, m->f, depth);
        r = f ? mg_tm_tap(s, f, m->tp) : NULL;
        break;
    }
    case MG_TM_TLAM:
    case MG_TM_TALL: {
        MgTm *b = inst_rec(run, m->f, depth);
        r = b ? (m->tag == MG_TM_TLAM ? mg_tm_tlam(s, b) : mg_tm_tall(s, b))
              : NULL;
        break;
    }
    default:
        r = m;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    if (memo)
        opmemo_put(&run->memo, m, depth, NULL, r);
    return r;
}

MgTm *mg_tm_instantiate(MgCheck *c, MgTm *m, uint32_t depth, MgTm *n) {
    if (!m || !n || m->fv <= depth)
        return m;
    MgTm *hit = cache_get(c, OP_TM_INST, m, n, depth, 0, false);
    if (hit)
        return hit;
    SubstRun run = {.c = c, .n = n};
    MgTm *r = inst_rec(&run, m, depth);
    opmemo_free(&run.memo);
    if (r)
        cache_put(c, OP_TM_INST, m, n, depth, 0, r);
    return r;
}

static MgTm *tinst_rec(SubstRun *run, MgTm *m, uint32_t depth) {
    MgCheck *c = run->c;
    MgTp *b = run->b;
    if (m->ftv <= depth)
        return m;
    bool memo = m->height >= MG_MEMO_MIN_HEIGHT;
    if (memo) {
        MgTm *hit = opmemo_get(&run->memo, m, depth, NULL);
        if (hit)
            return hit;
    }
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTm *r = NULL;
    switch (m->tag) {
    case MG_TM_AP:
    case MG_TM_IMP: {
        MgTm *f = tinst_rec(run, m->f, depth);
        MgTm *x = f ? tinst_rec(run, m->x, depth) : NULL;
        r = x ? (m->tag == MG_TM_AP ? mg_tm_ap(s, f, x) : mg_tm_imp(s, f, x))
              : NULL;
        break;
    }
    case MG_TM_LAM:
    case MG_TM_ALL: {
        MgTp *a = mg_tp_instantiate(c, m->tp, depth, b);
        MgTm *body = a ? tinst_rec(run, m->f, depth) : NULL;
        r = body ? (m->tag == MG_TM_LAM ? mg_tm_lam(s, a, body)
                                        : mg_tm_all(s, a, body))
                 : NULL;
        break;
    }
    case MG_TM_TAP: {
        MgTm *f = tinst_rec(run, m->f, depth);
        MgTp *a = f ? mg_tp_instantiate(c, m->tp, depth, b) : NULL;
        r = a ? mg_tm_tap(s, f, a) : NULL;
        break;
    }
    case MG_TM_TLAM:
    case MG_TM_TALL: {
        MgTm *body = tinst_rec(run, m->f, depth + 1u);
        r = body ? (m->tag == MG_TM_TLAM ? mg_tm_tlam(s, body)
                                         : mg_tm_tall(s, body))
                 : NULL;
        break;
    }
    default:
        r = m;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    if (memo)
        opmemo_put(&run->memo, m, depth, NULL, r);
    return r;
}

MgTm *mg_tm_tinstantiate(MgCheck *c, MgTm *m, uint32_t depth, MgTp *b) {
    if (!m || !b || m->ftv <= depth)
        return m;
    MgTm *hit = cache_get(c, OP_TM_TINST, m, b, depth, 0, false);
    if (hit)
        return hit;
    SubstRun run = {.c = c, .b = b};
    MgTm *r = tinst_rec(&run, m, depth);
    opmemo_free(&run.memo);
    if (r)
        cache_put(c, OP_TM_TINST, m, b, depth, 0, r);
    return r;
}

/* ── list shifts (binder contexts) ────────────────────────────────────── */

MgTpList *mg_tplist_shift1(MgCheck *c, MgTpList *l) {
    if (!l)
        return NULL;
    if (l->shift1)
        return l->shift1;
    if (!mg_enter(c))
        return NULL;
    MgTp *head = mg_tp_shift(c, l->head, 0, 1);
    MgTpList *tail = l->tail ? mg_tplist_shift1(c, l->tail) : NULL;
    MgTpList *r = head && (tail || !l->tail)
        ? mg_tplist_cons(ST(c), head, tail) : NULL;
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    l->shift1 = r;
    return r;
}

MgTmList *mg_tmlist_shift1(MgCheck *c, MgTmList *l) {
    if (!l)
        return NULL;
    if (l->shift1)
        return l->shift1;
    if (!mg_enter(c))
        return NULL;
    MgTm *head = mg_tm_shift(c, l->head, 0, 1);
    MgTmList *tail = l->tail ? mg_tmlist_shift1(c, l->tail) : NULL;
    MgTmList *r = head && (tail || !l->tail)
        ? mg_tmlist_cons(ST(c), head, tail) : NULL;
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    l->shift1 = r;
    return r;
}

/* The inverse of mg_tmlist_shift1 where it exists: every element must have
 * term index 0 not free.  NULL with REFUSED otherwise. */
MgTmList *mg_tmlist_unshift1(MgCheck *c, MgTmList *l) {
    if (!l)
        return NULL;
    if (!mg_enter(c))
        return NULL;
    MgTmList *r = NULL;
    if (mg_tm_free_db(l->head, 0)) {
        mg_fail(c, MG_REFUSED, "hypothesis depends on the generalized variable");
    } else {
        MgTm *head = mg_tm_shift(c, l->head, 0, -1);
        MgTmList *tail = l->tail ? mg_tmlist_unshift1(c, l->tail) : NULL;
        if (head && (tail || !l->tail))
            r = mg_tmlist_cons(ST(c), head, tail);
    }
    mg_leave(c);
    if (!r && c->status == MG_OK)
        mg_oom(c);
    return r;
}

/* ── normalization ────────────────────────────────────────────────────── */

MgTm *mg_tm_nf(MgCheck *c, MgTm *m) {
    if (!m)
        return NULL;
    if (m->nf)
        return m->nf;
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTm *r = NULL;
    switch (m->tag) {
    case MG_TM_DB:
    case MG_TM_NAME:
    case MG_TM_PRIM:
        r = m;
        break;
    case MG_TM_AP: {
        MgTm *f = mg_tm_nf(c, m->f);
        MgTm *x = f ? mg_tm_nf(c, m->x) : NULL;
        if (!x)
            break;
        if (f->tag == MG_TM_LAM) {
            ++c->betas;
            MgTm *t = mg_tm_instantiate(c, f->f, 0, x);
            r = t ? mg_tm_nf(c, t) : NULL;
        } else {
            r = mg_tm_ap(s, f, x);
        }
        break;
    }
    case MG_TM_LAM: {
        MgTm *b = mg_tm_nf(c, m->f);
        if (!b)
            break;
        if (b->tag == MG_TM_AP && b->x->tag == MG_TM_DB && b->x->n == 0u &&
            !mg_tm_free_db(b->f, 0))
            r = mg_tm_shift(c, b->f, 0, -1);     /* eta */
        else
            r = mg_tm_lam(s, m->tp, b);
        break;
    }
    case MG_TM_TAP: {
        MgTm *f = mg_tm_nf(c, m->f);
        if (!f)
            break;
        if (f->tag == MG_TM_TLAM) {
            ++c->betas;
            MgTm *t = mg_tm_tinstantiate(c, f->f, 0, m->tp);
            r = t ? mg_tm_nf(c, t) : NULL;       /* type-level beta */
        } else {
            r = mg_tm_tap(s, f, m->tp);
        }
        break;
    }
    case MG_TM_TLAM: {
        MgTm *b = mg_tm_nf(c, m->f);
        if (!b)
            break;
        if (b->tag == MG_TM_TAP && b->tp->tag == MG_TP_VAR && b->tp->n == 0u &&
            !mg_tm_free_tv(b->f, 0))
            r = mg_tm_tshift(c, b->f, 0, -1);    /* type-level eta */
        else
            r = mg_tm_tlam(s, b);
        break;
    }
    case MG_TM_IMP: {
        MgTm *p = mg_tm_nf(c, m->f);
        MgTm *q = p ? mg_tm_nf(c, m->x) : NULL;
        r = q ? mg_tm_imp(s, p, q) : NULL;
        break;
    }
    case MG_TM_ALL: {
        MgTm *b = mg_tm_nf(c, m->f);
        r = b ? mg_tm_all(s, m->tp, b) : NULL;
        break;
    }
    case MG_TM_TALL: {
        MgTm *b = mg_tm_nf(c, m->f);
        r = b ? mg_tm_tall(s, b) : NULL;
        break;
    }
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    m->nf = r;
    if (!r->nf)
        r->nf = r;
    return r;
}

/* Upstream tm_norm_p: no beta or eta redex at either sort.  The normal form
 * is unique and memoized on nodes, so this is its fixed point. */
bool mg_tm_is_beta_eta_normal(MgCheck *c, MgTm *m) {
    MgTm *nf = mg_tm_nf(c, m);
    return nf == m && c->status == MG_OK;
}

/* ── environment ──────────────────────────────────────────────────────── */

bool mg_env_decl_visible(MgEnv env, const MgDecl *decl) {
    return decl && decl->added_rev <= env.rev && env.rev < decl->removed_rev;
}

const MgDecl *mg_env_lookup_quiet(MgEnv env, uint32_t name, MgNamespace ns) {
    const MgName *entry = mg_name_get(&env.theory->store, name);
    if (!entry)
        return NULL;
    if (ns == MG_NS_TERM && entry->realization &&
        mg_env_decl_visible(env, entry->realization))
        return entry->realization;
    const MgDecl *decl = entry->decl[ns];
    return mg_env_decl_visible(env, decl) ? decl : NULL;
}

const MgDecl *mg_env_lookup(MgCheck *c, uint32_t name, MgNamespace ns) {
    if (c->footprint && !mg_footprint_add(c->footprint, name, ns))
        mg_fail(c, MG_NOMEM, "footprint");
    return mg_env_lookup_quiet(c->env, name, ns);
}

/* deltaNormalize.  An absent or undefined name is left as it is.  A name's
 * definition is admitted earlier than the name in the same lineage, so
 * expansion terminates; the depth guard still bounds a hostile chain. */
static MgTm *delta_rec(MgCheck *c, MgTm *m, bool *touched_absent) {
    if (!(m->facts & MG_TM_HAS_NAME))
        return m;
    if (m->dnf && m->dnf_rev <= c->env.rev &&
        !mg_theory_withdrawn_between(c->env.theory, m->dnf_rev, c->env.rev))
        return m->dnf;
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTm *r = NULL;
    bool absent = false;
    switch (m->tag) {
    case MG_TM_NAME: {
        const MgDecl *decl = mg_env_lookup(c, m->n, MG_NS_TERM);
        if (!decl) {
            absent = true;
            r = m;
        } else if (decl->def) {
            ++c->deltas;
            r = delta_rec(c, decl->def, &absent);
        } else {
            r = m;
        }
        break;
    }
    case MG_TM_AP:
    case MG_TM_IMP: {
        MgTm *f = delta_rec(c, m->f, &absent);
        MgTm *x = f ? delta_rec(c, m->x, &absent) : NULL;
        r = x ? (m->tag == MG_TM_AP ? mg_tm_ap(s, f, x) : mg_tm_imp(s, f, x))
              : NULL;
        break;
    }
    case MG_TM_LAM:
    case MG_TM_ALL: {
        MgTm *b = delta_rec(c, m->f, &absent);
        r = b ? (m->tag == MG_TM_LAM ? mg_tm_lam(s, m->tp, b)
                                     : mg_tm_all(s, m->tp, b))
              : NULL;
        break;
    }
    case MG_TM_TAP: {
        MgTm *f = delta_rec(c, m->f, &absent);
        r = f ? mg_tm_tap(s, f, m->tp) : NULL;
        break;
    }
    case MG_TM_TLAM:
    case MG_TM_TALL: {
        MgTm *b = delta_rec(c, m->f, &absent);
        r = b ? (m->tag == MG_TM_TLAM ? mg_tm_tlam(s, b) : mg_tm_tall(s, b))
              : NULL;
        break;
    }
    default:
        r = m;
        break;
    }
    mg_leave(c);
    if (!r)
        return c->status == MG_OK ? mg_oom(c) : NULL;
    if (absent) {
        *touched_absent = true;
    } else {
        m->dnf = r;
        m->dnf_rev = c->env.rev;
    }
    return r;
}

MgTm *mg_tm_delta(MgCheck *c, MgTm *m) {
    bool absent = false;
    return m ? delta_rec(c, m, &absent) : NULL;
}

MgTm *mg_tm_normalize(MgCheck *c, MgTm *m) {
    MgTm *d = mg_tm_delta(c, m);
    return d ? mg_tm_nf(c, d) : NULL;
}

/* ── type inference ───────────────────────────────────────────────────── */

static MgTp *tplist_nth(MgTpList *l, uint32_t i) {
    while (l && i) {
        l = l->tail;
        --i;
    }
    return l ? l->head : NULL;
}

typedef struct {
    MgCheck *c;
    OpMemo memo;
} InferRun;

static MgTp *infer_rec(InferRun *run, uint32_t depth, MgTpList *terms, MgTm *m) {
    MgCheck *c = run->c;
    if (!m)
        return NULL;
    /* Closed-in-terms results do not depend on the term context. */
    MgTpList *key_terms = m->fv == 0u ? NULL : terms;
    bool memo = m->height >= MG_MEMO_MIN_HEIGHT;
    if (memo) {
        MgTp *hit = opmemo_get(&run->memo, m, depth, key_terms);
        if (hit)
            return hit;
    }
    MgTp *hit = cache_get(c, OP_INFER, m, key_terms, depth, 0, true);
    if (hit)
        return hit;
    if (!mg_enter(c))
        return NULL;
    MgStore *s = ST(c);
    MgTp *r = NULL;
    switch (m->tag) {
    case MG_TM_DB:
        r = tplist_nth(terms, m->n);
        if (!r)
            mg_fail(c, MG_REFUSED, "unbound term variable");
        break;
    case MG_TM_PRIM: {
        MgTheory *t = c->env.theory;
        r = m->n < t->prim_len ? t->prims[m->n] : NULL;
        if (!r)
            mg_fail(c, MG_REFUSED, "not a primitive of the selected theory");
        break;
    }
    case MG_TM_NAME: {
        const MgDecl *decl = mg_env_lookup(c, m->n, MG_NS_TERM);
        r = decl ? decl->type : NULL;
        if (!r)
            mg_fail(c, MG_REFUSED, "unknown term");
        break;
    }
    case MG_TM_AP: {
        MgTp *tf = infer_rec(run, depth, terms, m->f);
        if (!tf)
            break;
        if (tf->tag != MG_TP_ARR) {
            mg_fail(c, MG_REFUSED, "application of a non-function");
            break;
        }
        MgTp *tx = infer_rec(run, depth, terms, m->x);
        if (!tx)
            break;
        if (tx != tf->a)
            mg_fail(c, MG_REFUSED, "argument type mismatch");
        else
            r = tf->b;
        break;
    }
    case MG_TM_TAP: {
        if (!mg_tp_plain_wf(m->tp, depth)) {
            mg_fail(c, MG_REFUSED, "ill-formed type argument");
            break;
        }
        MgTp *tf = infer_rec(run, depth, terms, m->f);
        if (!tf)
            break;
        if (tf->tag != MG_TP_ALL)
            mg_fail(c, MG_REFUSED, "type application of a monomorphic term");
        else
            r = mg_tp_instantiate(c, tf->a, 0, m->tp);
        break;
    }
    case MG_TM_LAM: {
        if (!mg_tp_plain_wf(m->tp, depth)) {
            mg_fail(c, MG_REFUSED, "ill-formed binder type");
            break;
        }
        MgTpList *inner = mg_tplist_cons(s, m->tp, terms);
        if (!inner) {
            mg_oom(c);
            break;
        }
        MgTp *tb = infer_rec(run, depth, inner, m->f);
        r = tb ? mg_tp_arr(s, m->tp, tb) : NULL;
        if (tb && !r)
            mg_oom(c);
        break;
    }
    case MG_TM_TLAM: {
        MgTpList *shifted = terms ? mg_tplist_shift1(c, terms) : NULL;
        if (terms && !shifted)
            break;
        MgTp *tb = infer_rec(run, depth + 1u, shifted, m->f);
        r = tb ? mg_tp_all(s, tb) : NULL;
        if (tb && !r)
            mg_oom(c);
        break;
    }
    case MG_TM_IMP: {
        MgTp *tp = infer_rec(run, depth, terms, m->f);
        if (!tp)
            break;
        if (tp != s->tp_prop) {
            mg_fail(c, MG_REFUSED, "implication antecedent is not a proposition");
            break;
        }
        MgTp *tq = infer_rec(run, depth, terms, m->x);
        if (!tq)
            break;
        if (tq != s->tp_prop)
            mg_fail(c, MG_REFUSED, "implication consequent is not a proposition");
        else
            r = s->tp_prop;
        break;
    }
    case MG_TM_ALL: {
        if (!mg_tp_plain_wf(m->tp, depth)) {
            mg_fail(c, MG_REFUSED, "ill-formed quantifier type");
            break;
        }
        MgTpList *inner = mg_tplist_cons(s, m->tp, terms);
        if (!inner) {
            mg_oom(c);
            break;
        }
        MgTp *tb = infer_rec(run, depth, inner, m->f);
        if (!tb)
            break;
        if (tb != s->tp_prop)
            mg_fail(c, MG_REFUSED, "quantifier body is not a proposition");
        else
            r = s->tp_prop;
        break;
    }
    case MG_TM_TALL:
        mg_fail(c, MG_REFUSED, "type quantifier outside a proposition prefix");
        break;
    }
    mg_leave(c);
    if (r) {
        cache_put(c, OP_INFER, m, key_terms, depth, 0, r);
        if (memo)
            opmemo_put(&run->memo, m, depth, key_terms, r);
    }
    return r;
}

MgTp *mg_infer(MgCheck *c, uint32_t depth, MgTpList *terms, MgTm *m) {
    InferRun run = {.c = c};
    MgTp *r = infer_rec(&run, depth, terms, m);
    opmemo_free(&run.memo);
    return r;
}

bool mg_check_proposition(MgCheck *c, uint32_t depth, MgTpList *terms,
                          MgTm *m) {
    while (m->tag == MG_TM_TALL) {
        m = m->f;
        ++depth;
        terms = NULL;
    }
    MgTp *t = mg_infer(c, depth, terms, m);
    if (!t)
        return false;
    if (t != ST(c)->tp_prop)
        return mg_fail(c, MG_REFUSED, "not a proposition");
    return true;
}

/* ── footprints ───────────────────────────────────────────────────────── */

void mg_footprint_init(MgFootprint *f) { memset(f, 0, sizeof(*f)); }

void mg_footprint_free(MgFootprint *f) {
    mg_sys_free(f->items);
    memset(f, 0, sizeof(*f));
}

bool mg_footprint_add(MgFootprint *f, uint32_t name, MgNamespace ns) {
    uint64_t key = ((uint64_t)name << 1) | (uint64_t)ns;
    uint32_t lo = 0, hi = f->len;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (f->items[mid] < key)
            lo = mid + 1u;
        else
            hi = mid;
    }
    if (lo < f->len && f->items[lo] == key)
        return true;
    if (f->len == f->cap) {
        uint32_t cap = f->cap ? f->cap * 2u : 8u;
        uint64_t *items = mg_sys_realloc(f->items, sizeof(uint64_t) * cap);
        if (!items) {
            f->nomem = true;
            return false;
        }
        f->items = items;
        f->cap = cap;
    }
    memmove(f->items + lo + 1u, f->items + lo,
            sizeof(uint64_t) * (f->len - lo));
    f->items[lo] = key;
    ++f->len;
    return true;
}

bool mg_footprint_union(MgFootprint *dst, const MgFootprint *src) {
    for (uint32_t i = 0; src && i < src->len; ++i)
        if (!mg_footprint_add(dst, (uint32_t)(src->items[i] >> 1),
                              (MgNamespace)(src->items[i] & 1u)))
            return false;
    return true;
}

/* The names of a term (iterative; a shared DAG visits each node once). */
bool mg_footprint_add_term(MgFootprint *f, const MgTm *root) {
    if (!root || !(root->facts & MG_TM_HAS_NAME))
        return true;
    OpMemo seen = {0};
    const MgTm **stack = NULL;
    size_t len = 0, cap = 0;
    bool ok = true;
#define FPUSH(x) do { \
        if (len == cap) { \
            size_t nc = cap ? cap * 2u : 64u; \
            const MgTm **g = mg_sys_realloc(stack, sizeof(*g) * nc); \
            if (!g) { ok = false; break; } \
            stack = g; cap = nc; \
        } \
        stack[len++] = (x); \
    } while (0)
    FPUSH(root);
    while (ok && len) {
        const MgTm *m = stack[--len];
        if (!(m->facts & MG_TM_HAS_NAME))
            continue;
        if (m->height >= MG_MEMO_MIN_HEIGHT) {
            if (opmemo_get(&seen, m, 0, NULL))
                continue;
            opmemo_put(&seen, m, 0, NULL, (void *)m);
        }
        switch (m->tag) {
        case MG_TM_NAME:
            ok = mg_footprint_add(f, m->n, MG_NS_TERM);
            break;
        case MG_TM_AP:
        case MG_TM_IMP:
            FPUSH(m->x);
            FPUSH(m->f);
            break;
        default:
            if (m->f)
                FPUSH(m->f);
            break;
        }
    }
#undef FPUSH
    mg_sys_free(stack);
    opmemo_free(&seen);
    return ok && !f->nomem;
}

/* CursorEnvironment.revalidate: agreement on the dependency closure of the
 * footprint, computed in the source snapshot, including absent names. */
typedef struct {
    uint64_t *items;
    uint32_t len, cap;
} NameStack;

static bool stack_push(NameStack *st, uint64_t key) {
    if (st->len == st->cap) {
        uint32_t cap = st->cap ? st->cap * 2u : 32u;
        uint64_t *items = mg_sys_realloc(st->items, sizeof(uint64_t) * cap);
        if (!items)
            return false;
        st->items = items;
        st->cap = cap;
    }
    st->items[st->len++] = key;
    return true;
}

static bool push_term_names(MgFootprint *seen, NameStack *st, const MgTm *m) {
    MgFootprint names;
    mg_footprint_init(&names);
    bool ok = mg_footprint_add_term(&names, m);
    for (uint32_t i = 0; ok && i < names.len; ++i) {
        uint64_t key = names.items[i];
        uint32_t before = seen->len;
        if (!mg_footprint_add(seen, (uint32_t)(key >> 1), (MgNamespace)(key & 1u)))
            ok = false;
        else if (seen->len != before)
            ok = stack_push(st, key);
    }
    mg_footprint_free(&names);
    return ok;
}

bool mg_footprint_revalidate(const MgFootprint *f, MgEnv from, MgEnv to) {
    if (!f || from.theory != to.theory)
        return false;
    if (from.rev == to.rev)
        return true;
    MgFootprint seen;
    mg_footprint_init(&seen);
    NameStack st = {0};
    bool ok = true;
    for (uint32_t i = 0; ok && i < f->len; ++i) {
        uint32_t before = seen.len;
        ok = mg_footprint_add(&seen, (uint32_t)(f->items[i] >> 1),
                              (MgNamespace)(f->items[i] & 1u)) &&
             (seen.len == before || stack_push(&st, f->items[i]));
    }
    while (ok && st.len) {
        uint64_t key = st.items[--st.len];
        uint32_t name = (uint32_t)(key >> 1);
        MgNamespace ns = (MgNamespace)(key & 1u);
        const MgDecl *a = mg_env_lookup_quiet(from, name, ns);
        const MgDecl *b = mg_env_lookup_quiet(to, name, ns);
        if (a != b) {
            ok = false;
            break;
        }
        if (a && a->def)
            ok = push_term_names(&seen, &st, a->def);
        if (ok && a && a->prop)
            ok = push_term_names(&seen, &st, a->prop);
    }
    mg_sys_free(st.items);
    mg_footprint_free(&seen);
    return ok;
}
