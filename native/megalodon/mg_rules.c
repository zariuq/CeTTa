#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_core.h"

#include <string.h>

/*
 * The eight ordinary rules (ProofJudgment.Proves), checked locally.
 *
 * mg_requirements is RuleObservation.observe: from a witness and a stated
 * conclusion it computes the exact premises the rule needs, or refuses, or
 * reports an undecided outcome (budget, interruption, memory, capacity).
 * Every interface ends in it:
 *   - a stored row compares the computed premises with its cited parents;
 *   - an LCF step and a proof-tree node propose a conclusion (mg_conclude)
 *     and then confirm it with mg_requirements against their actual
 *     premises (mg_step).
 * There is no other acceptance path.  Gpa (automation placeholders) is not
 * a rule: proof trees containing it are refused.
 */

const char *mg_rule_name(MgRule rule) {
    switch (rule) {
    case MG_RULE_HYP: return "hyp";
    case MG_RULE_KNOWN: return "known";
    case MG_RULE_TERM_APP: return "termApp";
    case MG_RULE_PROOF_APP: return "proofApp";
    case MG_RULE_TERM_LAM: return "termLam";
    case MG_RULE_PROOF_LAM: return "proofLam";
    case MG_RULE_TYPE_APP: return "typeApp";
    case MG_RULE_TYPE_LAM: return "typeLam";
    }
    return "invalid";
}

static inline MgStore *ST(MgCheck *c) { return &c->env.theory->store; }

static MgTm *tmlist_nth(MgTmList *l, uint32_t i) {
    while (l && i) {
        l = l->tail;
        --i;
    }
    return l ? l->head : NULL;
}

/* SourceCursor.JudgmentSafe: context safe, proposition scoped by the term
 * context and Protected. */
bool mg_judgment_safe(MgJudgment j) {
    return j.ctx && j.prop && j.ctx->safe &&
           j.prop->fv <= mg_tplist_len(j.ctx->terms) && mg_tm_protected(j.prop);
}

/* SourceCursor.WitnessSafe: every explicit operand Protected. */
bool mg_witness_safe(const MgWitness *w) {
    switch (w->rule) {
    case MG_RULE_TERM_APP:
        return w->arg && w->body && w->type && mg_tm_protected(w->arg) &&
               mg_tm_protected(w->body);
    case MG_RULE_PROOF_APP:
        return w->domain && mg_tm_protected(w->domain);
    case MG_RULE_PROOF_LAM:
        return w->source && mg_tm_protected(w->source);
    case MG_RULE_TYPE_APP:
        return w->type && w->body && mg_tm_protected(w->body);
    case MG_RULE_HYP:
    case MG_RULE_KNOWN:
    case MG_RULE_TERM_LAM:
    case MG_RULE_TYPE_LAM:
        return true;
    }
    return false;
}

/* The known rule's conclusion, normalize(prop), cached on the declaration.
 * The normal form depends only on the meaning of the names the proposition
 * mentions; those stay as they are while the declaration is visible, except
 * across a realization or withdrawal, which the change revisions record.
 * The cache saves work, never changes a verdict (a recomputation gives the
 * same interned term).  Callers hold the theory lock. */
static MgTm *known_normal_form(MgCheck *c, const MgDecl *decl) {
    MgDecl *d = (MgDecl *)decl;
    uint64_t lo = d->nf_rev < c->env.rev ? d->nf_rev : c->env.rev;
    uint64_t hi = d->nf_rev < c->env.rev ? c->env.rev : d->nf_rev;
    if (d->nf_prop && !mg_theory_withdrawn_between(c->env.theory, lo, hi))
        return d->nf_prop;
    MgTm *normal = mg_tm_normalize(c, d->prop);
    if (normal) {
        d->nf_prop = normal;
        d->nf_rev = c->env.rev;
    }
    return normal;
}

/* The outcome of a failed sub-computation: a refusal stays a refusal, every
 * other stop is undecided. */
static MgStatus outcome(MgCheck *c) {
    return c->status == MG_OK ? MG_NOMEM : c->status;
}

static MgStatus refuse(MgCheck *c, const char *reason) {
    mg_fail(c, MG_REFUSED, reason);
    return MG_REFUSED;
}

static MgStatus oom(MgCheck *c) {
    mg_fail(c, MG_NOMEM, "allocation");
    return MG_NOMEM;
}

/* Context.termBinder: a new term variable; hypotheses shift under it. */
static MgCtx *term_binder(MgCheck *c, MgCtx *ctx, MgTp *type) {
    MgTpList *terms = mg_tplist_cons(ST(c), type, ctx->terms);
    MgTmList *hyps = ctx->hyps ? mg_tmlist_shift1(c, ctx->hyps) : NULL;
    if (!terms || (ctx->hyps && !hyps))
        return NULL;
    return mg_ctx(ST(c), ctx->depth, terms, hyps);
}

static MgCtx *proof_binder(MgCheck *c, MgCtx *ctx, MgTm *prop) {
    MgTmList *hyps = mg_tmlist_cons(ST(c), prop, ctx->hyps);
    return hyps ? mg_ctx(ST(c), ctx->depth, ctx->terms, hyps) : NULL;
}

/* The fresh type context of a type generalization.  NULL with the check's
 * status set: CAPACITY at the end of the type-index space, else NOMEM. */
static MgCtx *type_binder(MgCheck *c, MgCtx *ctx) {
    if (ctx->depth == UINT32_MAX) {
        mg_fail(c, MG_CAPACITY, "type depth");
        return NULL;
    }
    MgCtx *inner = mg_ctx(ST(c), ctx->depth + 1u, NULL, NULL);
    if (!inner)
        mg_fail(c, MG_NOMEM, "allocation");
    return inner;
}

MgStatus mg_requirements(MgCheck *c, const MgWitness *w, MgJudgment concl,
                         MgJudgment premises[2], uint32_t *count) {
    *count = 0;
    if (c->status != MG_OK)
        return c->status;
    if (!mg_budget_charge(c->budget, 1u)) {
        mg_fail(c, c->budget->stop, "budget");
        return c->status;
    }
    MgCtx *ctx = concl.ctx;
    MgTm *prop = concl.prop;
    MgStore *s = ST(c);
    switch (w->rule) {
    case MG_RULE_HYP:
        if (tmlist_nth(ctx->hyps, w->index) != prop)
            return refuse(c, "hypothesis does not state the conclusion");
        return MG_OK;

    case MG_RULE_KNOWN: {
        const MgDecl *decl = mg_env_lookup(c, w->name, MG_NS_KNOWN);
        if (!decl)
            return refuse(c, "unknown proposition");
        MgTm *normal = mg_tm_normalize(c, decl->prop);
        if (!normal)
            return outcome(c);
        if (normal != prop)
            return refuse(c, "known proposition does not state the conclusion");
        return MG_OK;
    }

    case MG_RULE_TERM_APP: {
        MgTp *actual = mg_infer(c, ctx->depth, ctx->terms, w->arg);
        if (!actual)
            return outcome(c);
        if (actual != w->type)
            return refuse(c, "term argument has the wrong type");
        MgTm *argument = mg_tm_delta(c, w->arg);
        MgTm *instance = argument ? mg_tm_instantiate(c, w->body, 0, argument)
                                  : NULL;
        MgTm *normal = instance ? mg_tm_normalize(c, instance) : NULL;
        if (!normal)
            return outcome(c);
        if (normal != prop)
            return refuse(c, "instantiation does not state the conclusion");
        MgTm *quantified = mg_tm_all(s, w->type, w->body);
        if (!quantified)
            return oom(c);
        premises[0] = (MgJudgment){ctx, quantified};
        *count = 1;
        return MG_OK;
    }

    case MG_RULE_PROOF_APP: {
        MgTm *implication = mg_tm_imp(s, w->domain, prop);
        if (!implication)
            return oom(c);
        premises[0] = (MgJudgment){ctx, implication};
        premises[1] = (MgJudgment){ctx, w->domain};
        *count = 2;
        return MG_OK;
    }

    case MG_RULE_TERM_LAM: {
        if (prop->tag != MG_TM_ALL)
            return refuse(c, "conclusion is not a term quantification");
        if (!mg_tp_plain_wf(prop->tp, ctx->depth))
            return refuse(c, "ill-formed quantifier type");
        MgCtx *inner = term_binder(c, ctx, prop->tp);
        if (!inner)
            return outcome(c);
        premises[0] = (MgJudgment){inner, prop->f};
        *count = 1;
        return MG_OK;
    }

    case MG_RULE_PROOF_LAM: {
        if (prop->tag != MG_TM_IMP)
            return refuse(c, "conclusion is not an implication");
        MgTp *type = mg_infer(c, ctx->depth, ctx->terms, w->source);
        if (!type)
            return outcome(c);
        if (type != s->tp_prop)
            return refuse(c, "discharged hypothesis is not a proposition");
        MgTm *normal = mg_tm_normalize(c, w->source);
        if (!normal)
            return outcome(c);
        if (normal != prop->f)
            return refuse(c, "discharged hypothesis does not match");
        MgCtx *inner = proof_binder(c, ctx, normal);
        if (!inner)
            return oom(c);
        premises[0] = (MgJudgment){inner, prop->x};
        *count = 1;
        return MG_OK;
    }

    case MG_RULE_TYPE_APP: {
        MgTm *instance = mg_tm_tinstantiate(c, w->body, 0, w->type);
        if (!instance)
            return outcome(c);
        if (instance != prop)
            return refuse(c, "type instantiation does not state the conclusion");
        MgTm *quantified = mg_tm_tall(s, w->body);
        if (!quantified)
            return oom(c);
        premises[0] = (MgJudgment){ctx, quantified};
        *count = 1;
        return MG_OK;
    }

    case MG_RULE_TYPE_LAM: {
        if (prop->tag != MG_TM_TALL)
            return refuse(c, "conclusion is not a type quantification");
        if (ctx->terms || ctx->hyps)
            return refuse(c, "type generalization needs empty term and hypothesis contexts");
        MgCtx *inner = type_binder(c, ctx);
        if (!inner)
            return c->status;
        premises[0] = (MgJudgment){inner, prop->f};
        *count = 1;
        return MG_OK;
    }
    }
    mg_fail(c, MG_MALFORMED, "unknown rule");
    return MG_MALFORMED;
}

/* Operands a premise implies (a termApp domain and body, a proofApp
 * domain, a proofLam source, a termLam type, a typeApp body) are filled in
 * only when the witness omits them; a stated operand is kept, and
 * mg_requirements then holds it to the rule. */
MgStatus mg_conclude(MgCheck *c, MgWitness *w, MgCtx *ctx,
                     const MgJudgment *p, uint32_t count, MgJudgment *out) {
    MgStore *s = ST(c);
    switch (w->rule) {
    case MG_RULE_HYP: {
        if (count != 0u || !ctx)
            return refuse(c, "hypothesis takes no premises");
        MgTm *h = tmlist_nth(ctx->hyps, w->index);
        if (!h)
            return refuse(c, "no such hypothesis");
        *out = (MgJudgment){ctx, h};
        return MG_OK;
    }
    case MG_RULE_KNOWN: {
        if (count != 0u || !ctx)
            return refuse(c, "known proposition takes no premises");
        const MgDecl *decl = mg_env_lookup(c, w->name, MG_NS_KNOWN);
        if (!decl)
            return refuse(c, "unknown proposition");
        MgTm *normal = known_normal_form(c, decl);
        if (!normal)
            return outcome(c);
        *out = (MgJudgment){ctx, normal};
        return MG_OK;
    }
    case MG_RULE_TERM_APP: {
        if (count != 1u || p[0].prop->tag != MG_TM_ALL)
            return refuse(c, "term application needs a quantified premise");
        if (!w->type)
            w->type = p[0].prop->tp;
        if (!w->body)
            w->body = p[0].prop->f;
        MgTm *argument = mg_tm_delta(c, w->arg);
        MgTm *instance = argument ? mg_tm_instantiate(c, w->body, 0, argument)
                                  : NULL;
        MgTm *normal = instance ? mg_tm_normalize(c, instance) : NULL;
        if (!normal)
            return outcome(c);
        *out = (MgJudgment){p[0].ctx, normal};
        return MG_OK;
    }
    case MG_RULE_PROOF_APP: {
        if (count != 2u || p[0].prop->tag != MG_TM_IMP)
            return refuse(c, "proof application needs an implication premise");
        if (!w->domain)
            w->domain = p[0].prop->f;
        *out = (MgJudgment){p[0].ctx, p[0].prop->x};
        return MG_OK;
    }
    case MG_RULE_TERM_LAM: {
        if (count != 1u)
            return refuse(c, "generalization needs one premise");
        MgCtx *inner = p[0].ctx;
        if (!inner->terms)
            return refuse(c, "premise does not bind a term variable");
        if (!w->type)
            w->type = inner->terms->head;
        if (inner->terms->head != w->type)
            return refuse(c, "premise does not bind a variable of this type");
        MgTmList *hyps = inner->hyps ? mg_tmlist_unshift1(c, inner->hyps) : NULL;
        if (inner->hyps && !hyps)
            return outcome(c);
        MgCtx *outer = mg_ctx(s, inner->depth, inner->terms->tail, hyps);
        MgTm *all = outer ? mg_tm_all(s, w->type, p[0].prop) : NULL;
        if (!all)
            return oom(c);
        *out = (MgJudgment){outer, all};
        return MG_OK;
    }
    case MG_RULE_PROOF_LAM: {
        if (count != 1u)
            return refuse(c, "discharge needs one premise");
        MgCtx *inner = p[0].ctx;
        if (!inner->hyps)
            return refuse(c, "premise has no hypothesis to discharge");
        /* Without a stated source the discharged hypothesis is the source;
         * either way the rule then requires the premise context to be the
         * outer context extended by the normalized source. */
        if (!w->source)
            w->source = inner->hyps->head;
        MgCtx *outer = mg_ctx(s, inner->depth, inner->terms, inner->hyps->tail);
        MgTm *imp = outer ? mg_tm_imp(s, inner->hyps->head, p[0].prop) : NULL;
        if (!imp)
            return oom(c);
        *out = (MgJudgment){outer, imp};
        return MG_OK;
    }
    case MG_RULE_TYPE_APP: {
        if (count != 1u || p[0].prop->tag != MG_TM_TALL)
            return refuse(c, "type application needs a type-quantified premise");
        if (!w->body)
            w->body = p[0].prop->f;
        MgTm *instance = mg_tm_tinstantiate(c, w->body, 0, w->type);
        if (!instance)
            return outcome(c);
        *out = (MgJudgment){p[0].ctx, instance};
        return MG_OK;
    }
    case MG_RULE_TYPE_LAM: {
        if (count != 1u)
            return refuse(c, "type generalization needs one premise");
        MgCtx *inner = p[0].ctx;
        if (inner->terms || inner->hyps || inner->depth == 0u)
            return refuse(c, "premise is not in a fresh type context");
        MgCtx *outer = mg_ctx(s, inner->depth - 1u, NULL, NULL);
        MgTm *all = outer ? mg_tm_tall(s, p[0].prop) : NULL;
        if (!all)
            return oom(c);
        *out = (MgJudgment){outer, all};
        return MG_OK;
    }
    }
    mg_fail(c, MG_MALFORMED, "unknown rule");
    return MG_MALFORMED;
}

MgStatus mg_step(MgCheck *c, MgWitness *w, MgCtx *ctx,
                 const MgJudgment *premises, uint32_t count, MgJudgment *out) {
    for (uint32_t i = 0; i < count; ++i)
        if (!mg_judgment_safe(premises[i])) {
            mg_fail(c, MG_MALFORMED, "premise outside the protected fragment");
            return MG_MALFORMED;
        }
    if (ctx && !ctx->safe) {
        mg_fail(c, MG_MALFORMED, "context outside the protected fragment");
        return MG_MALFORMED;
    }
    MgJudgment candidate;
    MgStatus st = mg_conclude(c, w, ctx, premises, count, &candidate);
    if (st != MG_OK)
        return st;
    if (!mg_witness_safe(w)) {
        mg_fail(c, MG_MALFORMED, "operand outside the protected fragment");
        return MG_MALFORMED;
    }
    MgJudgment required[2];
    uint32_t needed = 0;
    st = mg_requirements(c, w, candidate, required, &needed);
    if (st != MG_OK)
        return st;
    if (needed != count)
        return refuse(c, "wrong number of premises");
    for (uint32_t i = 0; i < count; ++i)
        if (!mg_judgment_eq(required[i], premises[i]))
            return refuse(c, "premise judgment does not match the rule");
    /* The invariant is preserved by every rule (SourceProofInvariant);
     * checking it costs O(1) and guards the implementation. */
    if (!mg_judgment_safe(candidate)) {
        mg_fail(c, MG_REFUSED, "internal: protected invariant violated");
        return MG_REFUSED;
    }
    *out = candidate;
    return MG_OK;
}

/* ── proof trees (inferProof) ─────────────────────────────────────────── */

typedef struct {
    const MgPf *pf;
    const MgCtx *ctx;
    MgTm *prop;
    uint64_t address;   /* recorded row, when recording */
} TreeMemoEntry;

typedef struct {
    TreeMemoEntry *slots;
    uint32_t cap, len;
    bool disabled;
} TreeMemo;

static uint32_t memo_hash(const MgPf *pf, const MgCtx *ctx) {
    uint64_t h = (uint64_t)(uintptr_t)pf * UINT64_C(0x9e3779b97f4a7c15) ^
                 (uint64_t)(uintptr_t)ctx * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 31;
    return (uint32_t)h;
}

static TreeMemoEntry *memo_get(TreeMemo *m, const MgPf *pf, const MgCtx *ctx) {
    if (!m->slots)
        return NULL;
    for (uint32_t i = memo_hash(pf, ctx) & (m->cap - 1u);; i = (i + 1u) & (m->cap - 1u)) {
        TreeMemoEntry *e = &m->slots[i];
        if (!e->pf)
            return NULL;
        if (e->pf == pf && e->ctx == ctx)
            return e;
    }
}

static void memo_put(TreeMemo *m, const MgPf *pf, const MgCtx *ctx, MgTm *prop,
                     uint64_t address) {
    if (m->disabled)
        return;
    if (!m->slots || (m->len + 1u) * 2u > m->cap) {
        uint32_t cap = m->cap ? m->cap * 2u : 256u;
        TreeMemoEntry *slots = mg_sys_alloc(sizeof(TreeMemoEntry) * cap);
        if (!slots) {
            m->disabled = true; /* memo is an optimization only */
            return;
        }
        memset(slots, 0, sizeof(TreeMemoEntry) * cap);
        for (uint32_t j = 0; j < m->cap; ++j) {
            TreeMemoEntry e = m->slots[j];
            if (!e.pf)
                continue;
            uint32_t i = memo_hash(e.pf, e.ctx) & (cap - 1u);
            while (slots[i].pf)
                i = (i + 1u) & (cap - 1u);
            slots[i] = e;
        }
        mg_sys_free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    uint32_t i = memo_hash(pf, ctx) & (m->cap - 1u);
    while (m->slots[i].pf)
        i = (i + 1u) & (m->cap - 1u);
    m->slots[i] = (TreeMemoEntry){pf, ctx, prop, address};
    ++m->len;
}

typedef struct {
    TreeMemo memo;
    MgRowRecorder *recorder;
} TreeRun;

static MgStatus infer_tree(MgCheck *c, TreeRun *run, MgCtx *ctx, MgPf *pf,
                           MgTm **out, uint64_t *address_out) {
    TreeMemo *memo = &run->memo;
    TreeMemoEntry *cached = memo_get(memo, pf, ctx);
    if (cached) {
        *out = cached->prop;
        *address_out = cached->address;
        return MG_OK;
    }
    if (c->status != MG_OK)
        return c->status;
    if (++c->depth > c->depth_limit) {
        --c->depth;
        mg_fail(c, MG_CAPACITY, "proof depth");
        return MG_CAPACITY;
    }
    MgWitness w;
    memset(&w, 0, sizeof(w));
    MgJudgment premises[2];
    uint64_t addresses[2] = {0, 0};
    uint32_t count = 0;
    MgStatus st = MG_OK;
    switch (pf->tag) {
    case MG_PF_GPA:
        st = refuse(c, "automation placeholder is not a proof");
        break;
    case MG_PF_HYP:
        w.rule = MG_RULE_HYP;
        w.index = pf->n;
        break;
    case MG_PF_KNOWN:
        w.rule = MG_RULE_KNOWN;
        w.name = pf->n;
        break;
    case MG_PF_TMAP: {
        MgTm *q = NULL;
        st = infer_tree(c, run, ctx, pf->p, &q, &addresses[0]);
        w.rule = MG_RULE_TERM_APP;
        w.arg = pf->t;
        premises[0] = (MgJudgment){ctx, q};
        count = 1;
        break;
    }
    case MG_PF_PFAP: {
        MgTm *q = NULL, *r = NULL;
        st = infer_tree(c, run, ctx, pf->p, &q, &addresses[0]);
        if (st == MG_OK)
            st = infer_tree(c, run, ctx, pf->q, &r, &addresses[1]);
        w.rule = MG_RULE_PROOF_APP;
        premises[0] = (MgJudgment){ctx, q};
        premises[1] = (MgJudgment){ctx, r};
        count = 2;
        break;
    }
    case MG_PF_TLAM: {
        if (!mg_tp_plain_wf(pf->tp, ctx->depth)) {
            st = refuse(c, "ill-formed quantifier type");
            break;
        }
        MgCtx *inner = term_binder(c, ctx, pf->tp);
        MgTm *q = NULL;
        st = inner ? infer_tree(c, run, inner, pf->p, &q, &addresses[0]) : outcome(c);
        w.rule = MG_RULE_TERM_LAM;
        w.type = pf->tp;
        premises[0] = (MgJudgment){inner, q};
        count = 1;
        break;
    }
    case MG_PF_PLAM: {
        MgTp *type = mg_infer(c, ctx->depth, ctx->terms, pf->t);
        if (!type) {
            st = outcome(c);
            break;
        }
        if (type != ST(c)->tp_prop) {
            st = refuse(c, "discharged hypothesis is not a proposition");
            break;
        }
        MgTm *normal = mg_tm_normalize(c, pf->t);
        MgCtx *inner = normal ? proof_binder(c, ctx, normal) : NULL;
        MgTm *q = NULL;
        st = inner ? infer_tree(c, run, inner, pf->p, &q, &addresses[0]) : outcome(c);
        w.rule = MG_RULE_PROOF_LAM;
        w.source = pf->t;
        premises[0] = (MgJudgment){inner, q};
        count = 1;
        break;
    }
    case MG_PF_TPAP: {
        MgTm *q = NULL;
        st = infer_tree(c, run, ctx, pf->p, &q, &addresses[0]);
        w.rule = MG_RULE_TYPE_APP;
        w.type = pf->tp;
        premises[0] = (MgJudgment){ctx, q};
        count = 1;
        break;
    }
    case MG_PF_TPLAM: {
        if (ctx->terms || ctx->hyps) {
            st = refuse(c, "type generalization needs empty term and hypothesis contexts");
            break;
        }
        MgCtx *inner = type_binder(c, ctx);
        MgTm *q = NULL;
        st = inner ? infer_tree(c, run, inner, pf->p, &q, &addresses[0]) : c->status;
        w.rule = MG_RULE_TYPE_LAM;
        premises[0] = (MgJudgment){inner, q};
        count = 1;
        break;
    }
    default:
        st = MG_MALFORMED;
        mg_fail(c, MG_MALFORMED, "unknown proof constructor");
        break;
    }
    if (st == MG_OK) {
        MgJudgment result;
        st = mg_step(c, &w, (w.rule == MG_RULE_HYP || w.rule == MG_RULE_KNOWN)
                                ? ctx : NULL,
                     premises, count, &result);
        if (st == MG_OK && result.ctx != ctx)
            st = refuse(c, "internal: rule changed the proof context");
        uint64_t address = 0;
        if (st == MG_OK && run->recorder) {
            address = run->recorder->row(run->recorder->context, result, &w,
                                         addresses, count);
            if (address == UINT64_MAX) {
                mg_fail(c, MG_NOMEM, "evidence row");
                st = MG_NOMEM;
            }
        }
        if (st == MG_OK) {
            memo_put(memo, pf, ctx, result.prop, address);
            *out = result.prop;
            *address_out = address;
        }
    }
    --c->depth;
    return st;
}

MgStatus mg_infer_proof_recorded(MgCheck *c, MgCtx *ctx, MgPf *proof,
                                 MgRowRecorder *recorder, MgJudgment *out,
                                 uint64_t *root) {
    if (!ctx || !proof)
        return MG_MALFORMED;
    if (!ctx->safe || !proof->payload_protected) {
        mg_fail(c, MG_MALFORMED, "proof outside the protected fragment");
        return MG_MALFORMED;
    }
    TreeRun run = {.recorder = recorder};
    MgTm *prop = NULL;
    uint64_t address = 0;
    MgStatus st = infer_tree(c, &run, ctx, proof, &prop, &address);
    mg_sys_free(run.memo.slots);
    if (st == MG_OK) {
        *out = (MgJudgment){ctx, prop};
        if (root)
            *root = address;
    }
    return st;
}

MgStatus mg_infer_proof(MgCheck *c, MgCtx *ctx, MgPf *proof, MgJudgment *out) {
    return mg_infer_proof_recorded(c, ctx, proof, NULL, out, NULL);
}
