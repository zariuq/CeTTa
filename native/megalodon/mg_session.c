#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_session.h"

#include "mg_library.h"

#include <stdint.h>
#include <string.h>

/*
 * Sessions (ObservedCursor.walk) and theorem records.
 *
 * The checkpoint is: the snapshot, the source revision, the map from
 * checked addresses to their judgments (full contexts), and the union of
 * the checked rows' footprints.  Each resume replaces only the pending
 * suffix and extends the checkpoint by exactly the rows it accepted.  No
 * journal is kept.  Inspection reads; it never instantiates or rechecks.
 */

const char *mg_session_status_name(MgSessionStatus status) {
    switch (status) {
    case MG_SESSION_FINISHED: return "finished";
    case MG_SESSION_PAUSED: return "paused";
    case MG_SESSION_REFUSED: return "refused";
    case MG_SESSION_MALFORMED: return "malformed";
    case MG_SESSION_EXHAUSTED: return "exhausted";
    case MG_SESSION_INTERRUPTED: return "interrupted";
    case MG_SESSION_NOMEM: return "no-memory";
    case MG_SESSION_CAPACITY: return "capacity";
    case MG_SESSION_CANCELLED: return "cancelled";
    }
    return "invalid";
}

struct MgSession {
    atomic_size_t holders;
    MgTheory *theory;
    MgEnv env;
    MgRowSource source;
    bool source_live;
    uint64_t revision;
    /* address -> cache index + 1 (open addressing; 0 = empty) */
    uint64_t *map_keys;
    uint64_t *map_vals;
    uint64_t map_cap, map_len;
    MgJudgment *cache;
    MgWitness *witnesses;        /* evidence: the accepted rows' witnesses */
    uint64_t **parent_lists;
    uint32_t *parent_counts;
    uint64_t *addresses;
    uint64_t *origins;
    uint64_t cache_len, cache_cap;
    MgFootprint footprint;
    MgSessionStatus status;
    const char *reason;
    uint64_t failed_address;
    uint64_t *pending;
    uint64_t pending_len;
    bool walked;
    bool cancelled;
    bool lazy;                   /* rows hold source-form judgments */
};

static uint64_t addr_hash(uint64_t a) {
    a ^= a >> 33;
    a *= UINT64_C(0xff51afd7ed558ccd);
    a ^= a >> 33;
    return a;
}

static bool map_find(const MgSession *s, uint64_t address, uint64_t *index) {
    if (!s->map_cap)
        return false;
    for (uint64_t i = addr_hash(address) & (s->map_cap - 1u);;
         i = (i + 1u) & (s->map_cap - 1u)) {
        if (!s->map_vals[i])
            return false;
        if (s->map_keys[i] == address) {
            *index = s->map_vals[i] - 1u;
            return true;
        }
    }
}

static bool map_reserve(MgSession *s) {
    if ((s->map_len + 1u) * 2u <= s->map_cap)
        return true;
    uint64_t cap = s->map_cap ? s->map_cap * 2u : 64u;
    uint64_t *keys = mg_sys_alloc(sizeof(uint64_t) * cap);
    uint64_t *vals = mg_sys_alloc(sizeof(uint64_t) * cap);
    if (!keys || !vals) {
        mg_sys_free(keys);
        mg_sys_free(vals);
        return false;
    }
    memset(vals, 0, sizeof(uint64_t) * cap);
    for (uint64_t j = 0; j < s->map_cap; ++j) {
        if (!s->map_vals[j])
            continue;
        uint64_t i = addr_hash(s->map_keys[j]) & (cap - 1u);
        while (vals[i])
            i = (i + 1u) & (cap - 1u);
        keys[i] = s->map_keys[j];
        vals[i] = s->map_vals[j];
    }
    mg_sys_free(s->map_keys);
    mg_sys_free(s->map_vals);
    s->map_keys = keys;
    s->map_vals = vals;
    s->map_cap = cap;
    return true;
}

static void map_insert(MgSession *s, uint64_t address, uint64_t index) {
    uint64_t i = addr_hash(address) & (s->map_cap - 1u);
    while (s->map_vals[i])
        i = (i + 1u) & (s->map_cap - 1u);
    s->map_keys[i] = address;
    s->map_vals[i] = index + 1u;
    ++s->map_len;
}

static bool cache_reserve(MgSession *s) {
    if (s->cache_len < s->cache_cap)
        return true;
    uint64_t cap = s->cache_cap ? s->cache_cap * 2u : 64u;
    MgJudgment *cache = mg_sys_realloc(s->cache, sizeof(MgJudgment) * cap);
    if (!cache)
        return false;
    s->cache = cache;
    MgWitness *w = mg_sys_realloc(s->witnesses, sizeof(MgWitness) * cap);
    if (!w)
        return false;
    s->witnesses = w;
    uint64_t **pl = mg_sys_realloc(s->parent_lists, sizeof(uint64_t *) * cap);
    if (!pl)
        return false;
    s->parent_lists = pl;
    uint32_t *pc = mg_sys_realloc(s->parent_counts, sizeof(uint32_t) * cap);
    if (!pc)
        return false;
    s->parent_counts = pc;
    uint64_t *ad = mg_sys_realloc(s->addresses, sizeof(uint64_t) * cap);
    if (!ad)
        return false;
    s->addresses = ad;
    uint64_t *og = mg_sys_realloc(s->origins, sizeof(uint64_t) * cap);
    if (!og)
        return false;
    s->origins = og;
    s->cache_cap = cap;
    return true;
}

static void checkpoint_free(MgSession *s) {
    for (uint64_t i = 0; i < s->cache_len; ++i)
        mg_sys_free(s->parent_lists[i]);
    mg_sys_free(s->map_keys);
    mg_sys_free(s->map_vals);
    mg_sys_free(s->cache);
    mg_sys_free(s->witnesses);
    mg_sys_free(s->parent_lists);
    mg_sys_free(s->parent_counts);
    mg_sys_free(s->addresses);
    mg_sys_free(s->origins);
    mg_sys_free(s->pending);
    mg_footprint_free(&s->footprint);
    s->map_keys = s->map_vals = NULL;
    s->cache = NULL;
    s->witnesses = NULL;
    s->parent_lists = NULL;
    s->parent_counts = NULL;
    s->addresses = s->origins = s->pending = NULL;
    s->map_cap = s->map_len = s->cache_len = s->cache_cap = 0;
    s->pending_len = 0;
}

MgSession *mg_session_begin(MgTheory *theory, MgRowSource source,
                            MgStatus *status) {
    return mg_session_begin_mode(theory, source, false, status);
}

MgSession *mg_session_begin_mode(MgTheory *theory, MgRowSource source,
                                 bool lazy, MgStatus *status) {
    MgSession *s = mg_sys_alloc(sizeof(*s));
    if (!s) {
        if (source.release)
            source.release(source.context);
        if (status)
            *status = MG_NOMEM;
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    atomic_init(&s->holders, 1u);
    mg_theory_retain(theory);
    s->theory = theory;
    s->env = mg_theory_snapshot(theory);
    s->source = source;
    s->source_live = true;
    s->revision = source.revision(source.context);
    s->status = MG_SESSION_FINISHED;
    s->lazy = lazy;
    mg_footprint_init(&s->footprint);
    if (status)
        *status = MG_OK;
    return s;
}

void mg_session_cancel(MgSession *s) {
    if (!s)
        return;
    if (s->source_live && s->source.release)
        s->source.release(s->source.context);
    s->source_live = false;
    checkpoint_free(s);
    s->cancelled = true;
    s->status = MG_SESSION_CANCELLED;
}

void mg_session_retain(MgSession *s) {
    if (s)
        atomic_fetch_add(&s->holders, 1u);
}

void mg_session_release(MgSession *s) {
    if (!s || atomic_fetch_sub(&s->holders, 1u) != 1u)
        return;
    mg_session_cancel(s);
    mg_theory_release(s->theory);
    mg_sys_free(s);
}

MgEnv mg_session_env(const MgSession *s) { return s->env; }
const MgFootprint *mg_session_footprint(const MgSession *s) { return &s->footprint; }

bool mg_session_judgment(const MgSession *s, uint64_t address, MgJudgment *out) {
    uint64_t index;
    if (s->cancelled || !map_find(s, address, &index))
        return false;
    *out = s->cache[index];
    return true;
}

void mg_session_inspect(const MgSession *s, MgSessionReport *r) {
    memset(r, 0, sizeof(*r));
    r->status = s->status;
    r->reason = s->reason;
    r->failed_address = s->failed_address;
    r->checked_rows = s->cache_len;
    r->pending = s->pending;
    r->pending_len = s->pending_len;
}

static MgSessionStatus session_status_of(MgStatus st) {
    switch (st) {
    case MG_REFUSED: return MG_SESSION_REFUSED;
    case MG_EXHAUSTED: return MG_SESSION_EXHAUSTED;
    case MG_INTERRUPTED: return MG_SESSION_INTERRUPTED;
    case MG_NOMEM: return MG_SESSION_NOMEM;
    case MG_CAPACITY: return MG_SESSION_CAPACITY;
    default: return MG_SESSION_MALFORMED;
    }
}

/* CursorEnvironment.requested: the exact rule's environmental reads. */
static bool witness_footprint(MgFootprint *f, const MgWitness *w) {
    switch (w->rule) {
    case MG_RULE_KNOWN: return mg_footprint_add(f, w->name, MG_NS_KNOWN);
    case MG_RULE_TERM_APP:
        return mg_footprint_add_term(f, w->arg) && mg_footprint_add_term(f, w->body);
    case MG_RULE_PROOF_LAM: return mg_footprint_add_term(f, w->source);
    default: return true;
    }
}

static bool context_footprint(MgFootprint *f, const MgCtx *ctx) {
    for (const MgTmList *h = ctx->hyps; h; h = h->tail)
        if (!mg_footprint_add_term(f, h->head))
            return false;
    return true;
}

static bool judgment_footprint(MgFootprint *f, MgJudgment judgment) {
    return context_footprint(f, judgment.ctx) &&
           mg_footprint_add_term(f, judgment.prop);
}

/* ContextReadFootprint: lazy checks also form contexts and compare source
 * propositions/operands by conversion. Retain their syntax support even on
 * a conversion-cache hit; revalidation closes it through definition bodies.
 * The exact checker's smaller witness-only footprint remains sufficient
 * only for exact checks. */
static bool row_footprint(MgFootprint *f, const MgWitness *w,
                           MgJudgment judgment, bool lazy) {
    if (!witness_footprint(f, w))
        return false;
    if (!lazy)
        return true;
    return judgment_footprint(f, judgment) &&
           mg_footprint_add_term(f, w->arg) &&
           mg_footprint_add_term(f, w->body) &&
           mg_footprint_add_term(f, w->domain) &&
           mg_footprint_add_term(f, w->source);
}

/* A lazy row states its rule's conclusion up to conversion: a formed
 * judgment (stated data is typed before it is converted, mg_ctx_formed)
 * whose context and proposition are convertible to the derived ones. */
static MgStatus lazy_states(MgCheck *c, MgJudgment derived, MgJudgment stated) {
    bool same = true;
    if (derived.ctx != stated.ctx &&
        (!mg_ctx_formed(c, stated.ctx) ||
         mg_ctx_conv(c, derived.ctx, stated.ctx, &same) != MG_OK))
        return c->status;
    if (same && derived.prop != stated.prop &&
        (!mg_prop_formed(c, stated.ctx->depth, stated.ctx->terms, stated.prop) ||
         mg_conv(c, derived.prop, stated.prop, &same) != MG_OK))
        return c->status;
    if (!same) {
        mg_fail(c, MG_REFUSED, "the row does not state its rule's conclusion");
        return MG_REFUSED;
    }
    return MG_OK;
}

MgStatus mg_session_resume(MgSession *s, const uint64_t *schedule,
                           uint64_t n, uint64_t row_bound, MgBudget *budget,
                           MgSessionReport *report) {
    memset(report, 0, sizeof(*report));
    if (s->cancelled) {
        report->status = MG_SESSION_CANCELLED;
        return MG_MALFORMED;
    }
    /* The unfinished suffix is at most the whole schedule: reserve it
     * first, so no later failure can lose it.  Without it the session is
     * left exactly as it was. */
    uint64_t *pending = NULL;
    if (n) {
        pending = n <= SIZE_MAX / sizeof(uint64_t)
            ? mg_sys_alloc(sizeof(uint64_t) * (size_t)n) : NULL;
        if (!pending) {
            mg_session_inspect(s, report);
            report->status = MG_SESSION_NOMEM;
            report->reason = "allocation";
            return MG_NOMEM;
        }
    }
    uint64_t start_work = budget ? budget->work_used : 0u;
    MgSessionStatus status = MG_SESSION_FINISHED;
    const char *reason = NULL;
    uint64_t failed = 0;
    uint64_t i = 0, attempted = 0;
    for (; i < n; ++i) {
        if (row_bound && attempted == row_bound) {
            status = MG_SESSION_PAUSED;
            break;
        }
        uint64_t address = schedule[i];
        /* Every row is charged and polled, whatever its kind. */
        if (budget && (!mg_budget_poll(budget) || !mg_budget_charge(budget, 1u))) {
            status = session_status_of(budget->stop);
            reason = "budget";
            failed = address;
            break;
        }
        ++attempted;
        uint64_t index;
        if (s->source.revision(s->source.context) != s->revision) {
            status = MG_SESSION_MALFORMED;
            reason = "row store revision changed";
        } else if (map_find(s, address, &index)) {
            status = MG_SESSION_MALFORMED;
            reason = "duplicate scheduled address";
        }
        if (status != MG_SESSION_FINISHED) {
            failed = address;
            break;
        }
        MgRow row;
        memset(&row, 0, sizeof(row));
        const char *why = NULL;
        MgStatus st = s->source.read(s->source.context, s->theory, address,
                                     &row, &why);
        if (st != MG_OK) {
            status = st == MG_NOMEM ? MG_SESSION_NOMEM : MG_SESSION_MALFORMED;
            reason = why ? why : "no well-formed row at this address";
            failed = address;
            break;
        }
        /* SourceCursor.decode: NodeSafe is part of decoding. */
        if (!mg_judgment_safe(row.conclusion) || !mg_witness_safe(&row.witness)) {
            status = MG_SESSION_MALFORMED;
            reason = "row outside the protected fragment";
            failed = address;
            break;
        }
        /* ObservedCursor.premiseValues: every cited parent must already be
         * checked; a future, absent or self parent is malformed.  Values
         * beyond two can never equal a rule's requirements. */
        MgJudgment values[2];
        bool parents_ok = true;
        for (uint32_t k = 0; parents_ok && k < row.parent_count; ++k) {
            uint64_t pi;
            if (!map_find(s, row.parents[k], &pi))
                parents_ok = false;
            else if (k < 2u)
                values[k] = s->cache[pi];
        }
        if (!parents_ok) {
            status = MG_SESSION_MALFORMED;
            reason = "parent not yet checked (future or absent)";
            failed = address;
            break;
        }
        MgCheck c;
        mg_check_init(&c, s->env, budget, NULL);
        if (s->lazy) {
            /* The rule's conclusion from the cited parents, in source form;
             * the row must state it in the same context, up to conversion. */
            MgJudgment derived;
            MgWitness w = row.witness;
            if (row.parent_count > 2u) {
                st = MG_REFUSED;
                c.reason = "cited parents are not the required premises";
            } else {
                st = mg_step_lazy(&c, &w, row.conclusion.ctx, values, row.parent_count, &derived);
            }
            if (st == MG_OK)
                st = lazy_states(&c, derived, row.conclusion);
        } else {
            MgJudgment required[2];
            uint32_t count = 0;
            st = mg_requirements(&c, &row.witness, row.conclusion, required, &count);
            if (st == MG_OK) {
                bool same = count == row.parent_count;
                for (uint32_t k = 0; same && k < count; ++k)
                    same = mg_judgment_eq(required[k], values[k]);
                if (!same) {
                    st = MG_REFUSED;
                    c.reason = "cited parents are not the required premises";
                }
            }
        }
        if (st != MG_OK) {
            status = session_status_of(st);
            reason = c.reason ? c.reason : mg_status_name(st);
            failed = address;
            break;
        }
        /* Accept: reserve everything, then extend the checkpoint. */
        uint64_t *parents_copy = NULL;
        if (row.parent_count) {
            parents_copy = mg_sys_alloc(sizeof(uint64_t) * row.parent_count);
            if (parents_copy)
                memcpy(parents_copy, row.parents, sizeof(uint64_t) * row.parent_count);
        }
        if ((row.parent_count && !parents_copy) || !cache_reserve(s) ||
            !map_reserve(s) ||
            !row_footprint(&s->footprint, &row.witness, row.conclusion, s->lazy)) {
            mg_sys_free(parents_copy);
            status = MG_SESSION_NOMEM;
            reason = "allocation";
            failed = address;
            break;
        }
        index = s->cache_len++;
        s->cache[index] = row.conclusion;
        s->witnesses[index] = row.witness;
        s->parent_lists[index] = parents_copy;
        s->parent_counts[index] = row.parent_count;
        s->addresses[index] = address;
        s->origins[index] = row.origin;
        map_insert(s, address, index);
    }
    /* The exact unfinished suffix, the stopping row included.  `schedule`
     * may be the previous pending suffix itself: copy before freeing. */
    uint64_t remaining = n - i;
    if (remaining)
        memcpy(pending, schedule + i, sizeof(uint64_t) * (size_t)remaining);
    mg_sys_free(s->pending);
    s->pending = remaining ? pending : NULL;
    s->pending_len = remaining;
    if (!remaining)
        mg_sys_free(pending);
    s->status = status;
    s->reason = reason;
    s->failed_address = failed;
    s->walked = true;
    report->status = status;
    report->reason = reason;
    report->failed_address = failed;
    report->checked_rows = s->cache_len;
    report->attempted_rows = attempted;
    report->work_used = budget ? budget->work_used - start_work : 0u;
    report->pending = s->pending;
    report->pending_len = s->pending_len;
    return MG_OK;
}

static bool witness_eq(const MgWitness *a, const MgWitness *b) {
    return a->rule == b->rule && a->index == b->index && a->name == b->name &&
           a->arg == b->arg && a->type == b->type && a->body == b->body &&
           a->domain == b->domain && a->source == b->source;
}

MgStatus mg_session_rebase(MgSession *s) {
    if (s->cancelled)
        return MG_MALFORMED;
    uint64_t current = s->source.revision(s->source.context);
    if (current == s->revision)
        return MG_OK;
    for (uint64_t i = 0; i < s->cache_len; ++i) {
        MgRow row;
        memset(&row, 0, sizeof(row));
        const char *why = NULL;
        MgStatus st = s->source.read(s->source.context, s->theory,
                                     s->addresses[i], &row, &why);
        if (st == MG_NOMEM)
            return MG_NOMEM;
        if (st != MG_OK || !mg_judgment_eq(row.conclusion, s->cache[i]) ||
            !witness_eq(&row.witness, &s->witnesses[i]) ||
            row.origin != s->origins[i] ||
            row.parent_count != s->parent_counts[i] ||
            (row.parent_count &&
             memcmp(row.parents, s->parent_lists[i],
                    sizeof(uint64_t) * row.parent_count) != 0))
            return MG_REFUSED;
    }
    s->revision = current;
    return MG_OK;
}

MgStatus mg_session_revalidate(MgSession *s) {
    if (s->cancelled)
        return MG_MALFORMED;
    MgEnv live = mg_theory_snapshot(s->theory);
    if (!mg_footprint_revalidate(&s->footprint, s->env, live))
        return MG_REFUSED;
    s->env = live;
    return MG_OK;
}

/* ── theorem records ──────────────────────────────────────────────────── */

static MgThm *thm_new(MgTheory *theory, uint64_t rev, MgJudgment j,
                      MgThmOrigin origin) {
    MgThm *t = mg_sys_alloc(sizeof(*t));
    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    atomic_init(&t->holders, 1u);
    mg_theory_retain(theory);
    t->theory = theory;
    t->rev = rev;
    t->judgment = j;
    t->origin = origin;
    mg_footprint_init(&t->footprint);
    return t;
}

void mg_thm_retain(MgThm *t) {
    if (t)
        atomic_fetch_add(&t->holders, 1u);
}

void mg_thm_release(MgThm *t) {
    if (!t || atomic_fetch_sub(&t->holders, 1u) != 1u)
        return;
    for (uint32_t i = 0; i < t->premise_count; ++i)
        mg_thm_release(t->premises[i]);
    mg_evidence_destroy(t->evidence);
    mg_footprint_free(&t->footprint);
    mg_theory_release(t->theory);
    mg_sys_free(t);
}

bool mg_thm_valid_in(const MgThm *thm, MgEnv env) {
    if (!thm || thm->theory != env.theory)
        return false;
    if (thm->rev == env.rev)
        return true;
    return mg_footprint_revalidate(&thm->footprint,
                                   (MgEnv){thm->theory, thm->rev}, env);
}

MgStatus mg_thm_step(MgTheory *theory, const MgWitness *w, MgCtx *ctx,
                     MgThm *const *premises, uint32_t count,
                     MgBudget *budget, const char **reason, MgThm **out) {
    *out = NULL;
    if (count > 2u)
        return MG_MALFORMED;
    MgEnv env = mg_theory_snapshot(theory);
    MgJudgment pj[2];
    for (uint32_t i = 0; i < count; ++i) {
        if (!premises[i] || premises[i]->theory != theory) {
            if (reason)
                *reason = "premise belongs to another theory";
            return MG_REFUSED;
        }
        if (!mg_thm_valid_in(premises[i], env)) {
            if (reason)
                *reason = "premise depends on a changed declaration";
            return MG_REFUSED;
        }
        pj[i] = premises[i]->judgment;
    }
    /* Lazy conversion (the default) keeps judgments in source form; a
     * premise in source form also needs the lazy rule. */
    bool lazy = !theory->reference_conversion;
    for (uint32_t i = 0; i < count; ++i)
        lazy = lazy || premises[i]->lazy;
    MgWitness filled = *w;
    MgCheck c;
    mg_check_init(&c, env, budget, NULL);
    MgJudgment concl;
    MgStatus st = lazy ? mg_step_lazy(&c, &filled, ctx, pj, count, &concl)
                       : mg_step(&c, &filled, ctx, pj, count, &concl);
    if (st != MG_OK) {
        if (reason)
            *reason = c.reason;
        return st;
    }
    MgThm *t = thm_new(theory, env.rev, concl, MG_THM_STEP);
    if (!t)
        return MG_NOMEM;
    t->witness = filled;
    t->lazy = lazy;
    bool ok = row_footprint(&t->footprint, &filled, concl, lazy);
    for (uint32_t i = 0; ok && i < count; ++i)
        ok = mg_footprint_union(&t->footprint, &premises[i]->footprint) &&
             (!lazy || judgment_footprint(&t->footprint, pj[i]));
    if (!ok) {
        mg_thm_release(t);
        return MG_NOMEM;
    }
    for (uint32_t i = 0; i < count; ++i) {
        mg_thm_retain(premises[i]);
        t->premises[i] = premises[i];
    }
    t->premise_count = count;
    *out = t;
    return MG_OK;
}

MgStatus mg_thm_check_tree(MgTheory *theory, MgCtx *ctx, MgPf *proof,
                           MgBudget *budget, const char **reason,
                           MgThm **out) {
    *out = NULL;
    MgEnv env = mg_theory_snapshot(theory);
    MgCheck c;
    mg_check_init(&c, env, budget, NULL);
    MgJudgment j = {ctx, NULL};
    bool lazy = !theory->reference_conversion;
    MgStatus st = lazy ? mg_infer_proof_lazy(&c, ctx, proof, &j.prop)
                       : mg_infer_proof(&c, ctx, proof, &j);
    if (st != MG_OK) {
        if (reason)
            *reason = c.reason;
        return st;
    }
    MgThm *t = thm_new(theory, env.rev, j, MG_THM_TREE);
    if (!t)
        return MG_NOMEM;
    t->proof = proof;
    t->lazy = lazy;
    if (!mg_footprint_add_proof(&t->footprint, proof) ||
        (lazy && !judgment_footprint(&t->footprint, j))) {
        mg_thm_release(t);
        return MG_NOMEM;
    }
    *out = t;
    return MG_OK;
}

/* The rows the root's derivation actually uses (its parent cone, not every
 * checked row of the session), with their footprint.  Rows are renumbered
 * 0..n-1 in checkpoint order, which is a valid schedule: every parent was
 * checked before its child.  Iterative. */
static bool cone_evidence(const MgSession *s, uint64_t root, MgFootprint *f,
                          MgEvidence **out) {
    *out = NULL;
    uint64_t n = s->cache_len;
    uint64_t *renumber = mg_sys_alloc(sizeof(uint64_t) * (size_t)n);
    uint64_t *stack = mg_sys_alloc(sizeof(uint64_t) * (size_t)n);
    MgEvidence *ev = mg_sys_alloc(sizeof(*ev));
    bool ok = renumber && stack && ev;
    uint64_t rows = 0, parents = 0;
    if (ok) {
        memset(ev, 0, sizeof(*ev));
        for (uint64_t i = 0; i < n; ++i)
            renumber[i] = UINT64_MAX;
        uint64_t len = 0;
        stack[len++] = root;
        renumber[root] = 0;
        while (ok && len) {
            uint64_t i = stack[--len];
            ok = row_footprint(f, &s->witnesses[i], s->cache[i], s->lazy);
            ++rows;
            parents += s->parent_counts[i];
            for (uint32_t k = 0; ok && k < s->parent_counts[i]; ++k) {
                uint64_t pi = 0;
                if (map_find(s, s->parent_lists[i][k], &pi) && renumber[pi] == UINT64_MAX) {
                    renumber[pi] = 0;
                    stack[len++] = pi;
                }
            }
        }
    }
    if (ok) {
        ev->rows = mg_sys_alloc(sizeof(MgRow) * (size_t)(rows ? rows : 1u));
        ev->addresses = mg_sys_alloc(sizeof(uint64_t) * (size_t)(rows ? rows : 1u));
        ev->parent_store = mg_sys_alloc(sizeof(uint64_t) * (size_t)(parents ? parents : 1u));
        ok = ev->rows && ev->addresses && ev->parent_store;
    }
    if (ok) {
        uint64_t next = 0, used = 0;
        for (uint64_t i = 0; i < n; ++i) {
            if (renumber[i] == UINT64_MAX)
                continue;
            renumber[i] = next;
            MgRow *row = &ev->rows[next];
            row->conclusion = s->cache[i];
            row->witness = s->witnesses[i];
            row->origin = s->origins[i];
            row->parents = ev->parent_store + used;
            row->parent_count = s->parent_counts[i];
            for (uint32_t k = 0; k < s->parent_counts[i]; ++k) {
                uint64_t pi = 0;
                map_find(s, s->parent_lists[i][k], &pi);
                row->parents[k] = renumber[pi];
            }
            used += s->parent_counts[i];
            ev->addresses[next] = next;
            ++next;
        }
        ev->row_count = (uint32_t)rows;
        ev->root = renumber[root];
        ev->lazy = s->lazy;
    }
    mg_sys_free(renumber);
    mg_sys_free(stack);
    if (!ok) {
        mg_evidence_destroy(ev);
        return false;
    }
    *out = ev;
    return true;
}

MgStatus mg_session_finish(MgSession *s, uint64_t root, MgTm *claim,
                           MgBudget *budget, const char **reason,
                           MgThm **out) {
    *out = NULL;
    if (s->cancelled || !s->walked || s->status != MG_SESSION_FINISHED ||
        s->pending_len) {
        if (reason)
            *reason = "the submitted batch has not finished";
        return MG_UNFINISHED;
    }
    uint64_t index;
    if (!map_find(s, root, &index)) {
        if (reason)
            *reason = "root row was not checked";
        return MG_MALFORMED;
    }
    MgJudgment j = s->cache[index];
    if (j.ctx != s->theory->store.ctx_empty) {
        if (reason)
            *reason = "root judgment is not closed";
        return MG_REFUSED;
    }
    if (!claim || !mg_tm_protected(claim)) {
        if (reason)
            *reason = "claim outside the protected fragment";
        return MG_REFUSED;
    }
    MgCheck c;
    mg_check_init(&c, s->env, budget, NULL);
    if (!mg_check_proposition(&c, 0, NULL, claim)) {
        if (reason)
            *reason = c.reason ? c.reason : "claim is not a proposition";
        return c.status == MG_OK ? MG_REFUSED : c.status;
    }
    bool same = false;
    if (s->lazy) {
        if (mg_conv(&c, claim, j.prop, &same) != MG_OK) {
            if (reason)
                *reason = c.reason;
            return c.status;
        }
    } else {
        MgTm *normal = mg_tm_normalize(&c, claim);
        if (!normal) {
            if (reason)
                *reason = c.reason;
            return c.status;
        }
        same = normal == j.prop;
    }
    if (!same) {
        if (reason)
            *reason = "claim does not normalize to the root proposition";
        return MG_REFUSED;
    }
    MgThm *t = thm_new(s->theory, s->env.rev, j, MG_THM_SESSION);
    if (!t)
        return MG_NOMEM;
    t->lazy = s->lazy;
    if (!cone_evidence(s, index, &t->footprint, &t->evidence) ||
        !mg_footprint_add_term(&t->footprint, claim)) {
        mg_thm_release(t);
        return MG_NOMEM;
    }
    t->claim = claim;
    *out = t;
    return MG_OK;
}

MgStatus mg_session_theorem(MgSession *s, uint64_t root, const char **reason,
                            MgThm **out) {
    *out = NULL;
    if (s->cancelled || !s->walked || s->status != MG_SESSION_FINISHED ||
        s->pending_len) {
        if (reason)
            *reason = "the submitted batch has not finished";
        return MG_UNFINISHED;
    }
    uint64_t index;
    if (!map_find(s, root, &index)) {
        if (reason)
            *reason = "root row was not checked";
        return MG_MALFORMED;
    }
    MgThm *t = thm_new(s->theory, s->env.rev, s->cache[index], MG_THM_SESSION);
    if (!t)
        return MG_NOMEM;
    t->lazy = s->lazy;
    if (!cone_evidence(s, index, &t->footprint, &t->evidence)) {
        mg_thm_release(t);
        return MG_NOMEM;
    }
    *out = t;
    return MG_OK;
}

MgStatus mg_thm_from_known(MgTheory *theory, uint32_t name,
                           const char **reason, MgThm **out) {
    *out = NULL;
    MgEnv env = mg_theory_snapshot(theory);
    const MgDecl *d = mg_env_lookup_quiet(env, name, MG_NS_KNOWN);
    if (!d) {
        if (reason)
            *reason = "unknown proposition";
        return MG_REFUSED;
    }
    /* Under lazy conversion the known states its proposition as admitted;
     * the reference form is its normal form. */
    bool lazy = !theory->reference_conversion;
    MgTm *prop = d->prop;
    if (!lazy) {
        MgBudget b;
        mg_budget_init(&b, 0);
        MgCheck c;
        mg_check_init(&c, env, &b, NULL);
        prop = mg_tm_normalize(&c, d->prop);
        if (!prop)
            return c.status;
    }
    MgThm *t = thm_new(theory, env.rev,
                       (MgJudgment){theory->store.ctx_empty, prop},
                       MG_THM_DECLARATION);
    if (!t)
        return MG_NOMEM;
    t->lazy = lazy;
    if (!mg_footprint_add(&t->footprint, name, MG_NS_KNOWN)) {
        mg_thm_release(t);
        return MG_NOMEM;
    }
    t->decl = d;
    t->claim = d->prop;
    *out = t;
    return MG_OK;
}

MgStatus mg_thm_publish(MgTheory *theory, MgThm *thm, uint32_t name,
                        MgTm *claim, MgBudget *budget, const char **reason,
                        const MgDecl **out) {
    if (out)
        *out = NULL;
    MgEnv env = mg_theory_snapshot(theory);
    if (!thm || thm->theory != theory) {
        if (reason)
            *reason = "theorem belongs to another theory";
        return MG_REFUSED;
    }
    if (!mg_thm_valid_in(thm, env)) {
        if (reason)
            *reason = "theorem depends on a changed declaration";
        return MG_REFUSED;
    }
    if (thm->judgment.ctx != theory->store.ctx_empty) {
        if (reason)
            *reason = "theorem is not closed";
        return MG_REFUSED;
    }
    if (!claim)
        claim = thm->claim ? thm->claim : thm->judgment.prop;
    if (!mg_tm_protected(claim)) {
        if (reason)
            *reason = "claim outside the protected fragment";
        return MG_REFUSED;
    }
    MgCheck c;
    mg_check_init(&c, env, budget, NULL);
    if (!mg_check_proposition(&c, 0, NULL, claim)) {
        if (reason)
            *reason = c.reason ? c.reason : "claim is not a proposition";
        return c.status == MG_OK ? MG_REFUSED : c.status;
    }
    bool same = false;
    if (thm->lazy) {
        /* the claim and the source-form judgment, up to conversion */
        if (mg_conv(&c, claim, thm->judgment.prop, &same) != MG_OK) {
            if (reason)
                *reason = c.reason;
            return c.status;
        }
    } else {
        MgTm *normal = mg_tm_normalize(&c, claim);
        if (!normal) {
            if (reason)
                *reason = c.reason;
            return c.status;
        }
        same = normal == thm->judgment.prop;
    }
    if (!same) {
        if (reason)
            *reason = "claim does not normalize to the theorem";
        return MG_REFUSED;
    }
    /* The declaration keeps the theorem's evidence (a tree proof is
     * store-interned; other derivations are copied as rows). */
    MgEvidence *evidence = NULL;
    MgPf *proof = NULL;
    if (thm->origin == MG_THM_TREE && thm->judgment.ctx == theory->store.ctx_empty) {
        proof = thm->proof;
    } else if (thm->origin != MG_THM_DECLARATION) {
        MgEvidence ev;
        MgStatus est = mg_evidence_of(thm, &ev);
        if (est != MG_OK) {
            if (reason)
                *reason = "evidence";
            return est;
        }
        evidence = mg_evidence_clone(&ev);
        mg_evidence_free(&ev);
        if (!evidence)
            return MG_NOMEM;
    }
    const MgDecl *made = NULL;
    MgStatus st = mg_theory_publish_known(theory, env.rev, name, claim,
                                          &thm->footprint, evidence, NULL, NULL,
                                          &made);
    if (st == MG_OK && proof)
        ((MgDecl *)made)->proof = proof;
    if (out)
        *out = made;
    if (st != MG_OK && reason)
        *reason = st == MG_REFUSED ? "name is not fresh" : mg_status_name(st);
    return st;
}
