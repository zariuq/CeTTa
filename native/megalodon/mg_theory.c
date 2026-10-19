#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_core.h"
#include "mg_profiles.h"
#include "mg_library.h"
#include "mg_hash.h"

#include <stdlib.h>
#include <string.h>

/*
 * Theory lineages, ordered admission and atomic commits.
 *
 * NativeAdmission.admit / SourceAdmission.admit decide each item against an
 * immutable snapshot.  A commit reserves every allocation first (the
 * declaration record, its index slot, its dependency footprint), then
 * re-validates the captured revision and the current permission, and only
 * then mutates: the live state after any non-OK outcome equals the live
 * state before (NativePublication.failed_commit_unchanged).
 *
 * Callers hold t->lock around every function taking a live theory.
 */

static _Atomic uint64_t g_theory_identity = 1u;

static char *dup_text(const char *text) {
    if (!text)
        return NULL;
    size_t n = strlen(text);
    char *copy = mg_sys_alloc(n + 1u);
    if (copy)
        memcpy(copy, text, n + 1u);
    return copy;
}

static void decl_free(MgDecl *d) {
    if (!d)
        return;
    mg_evidence_destroy(d->evidence);
    mg_footprint_free(&d->deps);
    mg_sys_free(d->source_name);
    mg_sys_free(d->origin);
    mg_sys_free(d);
}

static void theory_free(MgTheory *t) {
    for (uint64_t i = 0; i < t->decl_len; ++i)
        decl_free(t->decls[i]);
    mg_sys_free(t->decls);
    mg_sys_free(t->withdrawals);
    mg_sys_free(t->assoc);
    mg_sys_free(t->prims);
    mg_sys_free(t->prim_names);
    mg_sys_free(t->revision_label);
    mg_store_free(&t->store);
    pthread_mutex_destroy(&t->lock);
    mg_sys_free(t);
}

MgTheory *mg_theory_new(MgProfileTag profile, const char *revision_label,
                        MgStatus *status) {
    MgStatus ignored;
    if (!status)
        status = &ignored;
    const MgProfileSpec *spec = mg_profile_spec(profile);
    if (!spec) {
        *status = MG_MALFORMED;
        return NULL;
    }
    MgTheory *t = mg_sys_alloc(sizeof(*t));
    if (!t) {
        *status = MG_NOMEM;
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    if (pthread_mutex_init(&t->lock, NULL) != 0) {
        mg_sys_free(t);
        *status = MG_NOMEM;
        return NULL;
    }
    if (!mg_store_init(&t->store)) {
        pthread_mutex_destroy(&t->lock);
        mg_sys_free(t);
        *status = MG_NOMEM;
        return NULL;
    }
    atomic_init(&t->holders, 1u);
    t->identity = atomic_fetch_add(&g_theory_identity, 1u);
    t->profile = profile;
    t->publication_permitted = true;
    t->revision_label = dup_text(revision_label ? revision_label : "native");
    uint32_t prim_len = 0;
    for (uint32_t i = 0; i < spec->prim_count; ++i)
        if (spec->prims[i].index + 1u > prim_len)
            prim_len = spec->prims[i].index + 1u;
    if (prim_len) {
        t->prims = mg_sys_alloc(sizeof(MgTp *) * prim_len);
        t->prim_names = mg_sys_alloc(sizeof(char *) * prim_len);
    }
    if (!t->revision_label || (prim_len && (!t->prims || !t->prim_names))) {
        theory_free(t);
        *status = MG_NOMEM;
        return NULL;
    }
    if (prim_len) {
        memset(t->prims, 0, sizeof(MgTp *) * prim_len);
        memset(t->prim_names, 0, sizeof(char *) * prim_len);
    }
    t->prim_len = prim_len;
    for (uint32_t i = 0; i < spec->prim_count; ++i) {
        const MgPrimSpec *p = &spec->prims[i];
        MgTp *type = mg_profile_parse_type(&t->store, p->type, p->type_variables);
        if (!type) {
            theory_free(t);
            *status = MG_NOMEM;
            return NULL;
        }
        t->prims[p->index] = type;
        t->prim_names[p->index] = (char *)p->name;
    }
    t->reserved = spec->reserved;
    t->reserved_len = spec->reserved_count;
    *status = MG_OK;
    return t;
}

void mg_theory_retain(MgTheory *t) {
    if (t)
        atomic_fetch_add(&t->holders, 1u);
}

void mg_theory_release(MgTheory *t) {
    if (t && atomic_fetch_sub(&t->holders, 1u) == 1u)
        theory_free(t);
}

MgEnv mg_theory_snapshot(MgTheory *t) {
    return (MgEnv){t, t->rev};
}

bool mg_theory_withdrawn_between(const MgTheory *t, uint64_t after,
                                 uint64_t upto) {
    for (uint32_t i = 0; i < t->withdrawal_len; ++i)
        if (t->withdrawals[i] > after && t->withdrawals[i] <= upto)
            return true;
    return false;
}

static bool name_reserved(const MgTheory *t, uint32_t name) {
    const MgName *n = mg_name_get(&t->store, name);
    for (uint32_t i = 0; n && i < t->reserved_len; ++i)
        if (strcmp(n->text, t->reserved[i]) == 0)
            return true;
    return false;
}

bool mg_theory_name_fresh(const MgTheory *t, uint32_t name, MgNamespace ns) {
    const MgName *n = mg_name_get(&t->store, name);
    return n && !n->decl[ns] && !name_reserved(t, name);
}

uint64_t mg_theory_live_decls(const MgTheory *t, MgEnv env) {
    uint64_t live = 0;
    for (uint64_t i = 0; i < t->decl_len; ++i)
        live += mg_env_decl_visible(env, t->decls[i]) ? 1u : 0u;
    return live;
}

/* ── associations (the selected authority) ────────────────────────────── */

static MgStatus add_association(MgTheory *t, uint32_t name, MgNamespace ns,
                                MgTp *type, MgTm *prop) {
    for (uint32_t i = 0; i < t->assoc_len; ++i) {
        MgAssociation *a = &t->assoc[i];
        if (a->name == name && a->ns == ns)
            return (a->type == type && a->prop == prop) ? MG_OK : MG_CONFLICT;
    }
    if (t->assoc_len == t->assoc_cap) {
        uint32_t cap = t->assoc_cap ? t->assoc_cap * 2u : 16u;
        MgAssociation *grown = mg_sys_realloc(t->assoc, sizeof(*grown) * cap);
        if (!grown)
            return MG_NOMEM;
        t->assoc = grown;
        t->assoc_cap = cap;
    }
    t->assoc[t->assoc_len++] = (MgAssociation){name, (uint8_t)ns, type, prop, false};
    return MG_OK;
}

MgStatus mg_theory_associate_parameter(MgTheory *t, uint32_t name, MgTp *type) {
    if (!name || !type || !mg_tp_poly_wf(type, 0))
        return MG_MALFORMED;
    return add_association(t, name, MG_NS_TERM, type, NULL);
}

MgStatus mg_theory_associate_known(MgTheory *t, uint32_t name, MgTm *prop) {
    if (!name || !prop)
        return MG_MALFORMED;
    return add_association(t, name, MG_NS_KNOWN, NULL, prop);
}

static bool associated(const MgTheory *t, uint32_t name, MgNamespace ns,
                       const MgTp *type, const MgTm *prop) {
    for (uint32_t i = 0; i < t->assoc_len; ++i) {
        const MgAssociation *a = &t->assoc[i];
        if (a->name == name && a->ns == ns && a->type == type && a->prop == prop)
            return true;
    }
    return false;
}

/* ── proof footprints ─────────────────────────────────────────────────── */

typedef struct {
    const MgPf **slots;
    uint32_t cap, len;
} PfSet;

static bool pfset_insert(PfSet *s, const MgPf *pf, bool *fresh) {
    if ((s->len + 1u) * 2u > s->cap) {
        uint32_t cap = s->cap ? s->cap * 2u : 64u;
        const MgPf **slots = mg_sys_alloc(sizeof(*slots) * cap);
        if (!slots)
            return false;
        memset(slots, 0, sizeof(*slots) * cap);
        for (uint32_t i = 0; i < s->cap; ++i) {
            const MgPf *x = s->slots[i];
            if (!x)
                continue;
            uint32_t j = (uint32_t)((uintptr_t)x >> 4) & (cap - 1u);
            while (slots[j])
                j = (j + 1u) & (cap - 1u);
            slots[j] = x;
        }
        mg_sys_free(s->slots);
        s->slots = slots;
        s->cap = cap;
    }
    uint32_t j = (uint32_t)((uintptr_t)pf >> 4) & (s->cap - 1u);
    while (s->slots[j]) {
        if (s->slots[j] == pf) {
            *fresh = false;
            return true;
        }
        j = (j + 1u) & (s->cap - 1u);
    }
    s->slots[j] = pf;
    ++s->len;
    *fresh = true;
    return true;
}

bool mg_footprint_add_proof(MgFootprint *f, const MgPf *root) {
    PfSet seen = {0};
    const MgPf **stack = NULL;
    uint32_t len = 0, cap = 0;
    bool ok = true;
#define PUSH(x) do { \
        if (len == cap) { \
            uint32_t nc = cap ? cap * 2u : 64u; \
            const MgPf **ns = mg_sys_realloc(stack, sizeof(*ns) * nc); \
            if (!ns) { ok = false; break; } \
            stack = ns; cap = nc; \
        } \
        stack[len++] = (x); \
    } while (0)
    PUSH(root);
    while (ok && len) {
        const MgPf *pf = stack[--len];
        bool fresh = false;
        if (!pfset_insert(&seen, pf, &fresh)) {
            ok = false;
            break;
        }
        if (!fresh)
            continue;
        if (pf->tag == MG_PF_KNOWN && !mg_footprint_add(f, pf->n, MG_NS_KNOWN))
            ok = false;
        if (pf->t && !mg_footprint_add_term(f, pf->t))
            ok = false;
        if (pf->p)
            PUSH(pf->p);
        if (pf->q)
            PUSH(pf->q);
    }
#undef PUSH
    mg_sys_free(stack);
    mg_sys_free(seen.slots);
    return ok && !f->nomem;
}

/* ── item checks ──────────────────────────────────────────────────────── */

/* SourceAdmission.ItemSafe, decided before any native admission check
 * (SourceAdmission.admit). */
static bool item_safe(const MgItem *item) {
    switch (item->kind) {
    case MG_ITEM_PARAMETER: return true;
    case MG_ITEM_DEFINITION: return item->body && mg_tm_protected(item->body);
    case MG_ITEM_KNOWN:
    case MG_ITEM_CONJECTURE: return item->prop && mg_tm_protected(item->prop);
    case MG_ITEM_THEOREM:
        return item->prop && item->proof && mg_tm_protected(item->prop) &&
               item->proof->payload_protected;
    }
    return false;
}

MgStatus mg_check_theorem_proof(MgCheck *c, MgTm *claim, MgPf *proof) {
    MgStore *s = &c->env.theory->store;
    if (c->env.theory->reference_conversion) {
        MgTm *normal = mg_tm_normalize(c, claim);
        if (!normal)
            return c->status;
        MgJudgment j;
        MgStatus st = mg_infer_proof(c, s->ctx_empty, proof, &j);
        if (st != MG_OK)
            return st;
        if (j.prop != normal)
            return mg_fail(c, MG_REFUSED, "proof does not prove the stated proposition"),
                   MG_REFUSED;
        return MG_OK;
    }
    MgTm *proved = NULL;
    MgStatus st = mg_infer_proof_lazy(c, s->ctx_empty, proof, &proved);
    if (st != MG_OK)
        return st;
    bool same = false;
    st = mg_conv(c, proved, claim, &same);
    if (st != MG_OK)
        return st;
    if (!same)
        return mg_fail(c, MG_REFUSED, "proof does not prove the stated proposition"),
               MG_REFUSED;
    return MG_OK;
}

MgStatus mg_item_check(MgCheck *c, const MgItem *item) {
    MgTheory *t = c->env.theory;
    MgStore *s = &t->store;
    if (!item)
        return MG_MALFORMED;
    if (!item_safe(item)) {
        mg_fail(c, MG_MALFORMED, "item outside the protected fragment");
        return MG_MALFORMED;
    }
    switch (item->kind) {
    case MG_ITEM_PARAMETER:
        if (!item->type || !mg_theory_name_fresh(t, item->name, MG_NS_TERM))
            return mg_fail(c, MG_REFUSED, "name is not fresh"), MG_REFUSED;
        if (!mg_tp_poly_wf(item->type, 0))
            return mg_fail(c, MG_REFUSED, "ill-formed parameter type"), MG_REFUSED;
        if (!associated(t, item->name, MG_NS_TERM, item->type, NULL))
            return mg_fail(c, MG_REFUSED,
                           "parameter is not in the selected authority"),
                   MG_REFUSED;
        return MG_OK;
    case MG_ITEM_DEFINITION: {
        if (!item->type || !item->body)
            return mg_fail(c, MG_REFUSED, "name is not fresh"), MG_REFUSED;
        bool realizing = false;
        if (!mg_theory_name_fresh(t, item->name, MG_NS_TERM)) {
            /* A content-addressed parameter may be realized by the body it
             * names; nothing else may reuse a term name. */
            const MgName *n = mg_name_get(s, item->name);
            const MgDecl *p = n ? n->decl[MG_NS_TERM] : NULL;
            realizing = p && p->kind == MG_DECL_PARAMETER && !n->realization &&
                        mg_env_decl_visible(c->env, p) && p->type == item->type;
            if (!realizing)
                return mg_fail(c, MG_REFUSED, "name is not fresh"), MG_REFUSED;
        }
        if (!mg_tp_poly_wf(item->type, 0))
            return mg_fail(c, MG_REFUSED, "ill-formed definition type"), MG_REFUSED;
        if (!mg_tm_protected(item->body))
            return mg_fail(c, MG_MALFORMED,
                           "definition outside the protected fragment"),
                   MG_MALFORMED;
        if (!mg_tm_is_beta_eta_normal(c, item->body))
            return c->status != MG_OK ? c->status
                   : (mg_fail(c, MG_REFUSED, "definition is not beta-eta normal"),
                      MG_REFUSED);
        MgTp *type = mg_infer(c, 0, NULL, item->body);
        if (!type)
            return c->status;
        if (type != item->type)
            return mg_fail(c, MG_REFUSED, "definition has another type"), MG_REFUSED;
        if (realizing && !mg_hash_names_definition(c, item->name, item->type, item->body))
            return c->status != MG_OK ? c->status
                   : (mg_fail(c, MG_REFUSED,
                              "the parameter's name is not the identity of this body"),
                      MG_REFUSED);
        return MG_OK;
    }
    case MG_ITEM_KNOWN:
        if (!item->prop || !mg_theory_name_fresh(t, item->name, MG_NS_KNOWN))
            return mg_fail(c, MG_REFUSED, "name is not fresh"), MG_REFUSED;
        if (!mg_tm_protected(item->prop))
            return mg_fail(c, MG_MALFORMED,
                           "proposition outside the protected fragment"),
                   MG_MALFORMED;
        if (!mg_check_proposition(c, 0, NULL, item->prop))
            return c->status;
        if (!associated(t, item->name, MG_NS_KNOWN, NULL, item->prop))
            return mg_fail(c, MG_REFUSED,
                           "axiom is not in the selected authority"),
                   MG_REFUSED;
        return MG_OK;
    case MG_ITEM_THEOREM: {
        if (!item->prop || !item->proof ||
            !mg_theory_name_fresh(t, item->name, MG_NS_KNOWN))
            return mg_fail(c, MG_REFUSED, "name is not fresh"), MG_REFUSED;
        if (!mg_tm_protected(item->prop) || !item->proof->payload_protected)
            return mg_fail(c, MG_MALFORMED,
                           "theorem outside the protected fragment"),
                   MG_MALFORMED;
        if (!mg_check_proposition(c, 0, NULL, item->prop))
            return c->status;
        return mg_check_theorem_proof(c, item->prop, item->proof);
    }
    case MG_ITEM_CONJECTURE:
        if (!item->prop)
            return MG_MALFORMED;
        if (!mg_tm_protected(item->prop))
            return mg_fail(c, MG_MALFORMED,
                           "proposition outside the protected fragment"),
                   MG_MALFORMED;
        if (!mg_check_proposition(c, 0, NULL, item->prop))
            return c->status;
        return MG_OK;
    }
    return MG_MALFORMED;
}

/* ── commits ──────────────────────────────────────────────────────────── */

/* Reserve a declaration and its index slot; nothing is published. */
static MgDecl *reserve_decl(MgTheory *t) {
    if (t->decl_len == t->decl_cap) {
        uint64_t cap = t->decl_cap ? t->decl_cap * 2u : 64u;
        MgDecl **grown = mg_sys_realloc(t->decls, sizeof(MgDecl *) * cap);
        if (!grown)
            return NULL;
        t->decls = grown;
        t->decl_cap = cap;
    }
    MgDecl *d = mg_sys_alloc(sizeof(*d));
    if (d)
        memset(d, 0, sizeof(*d));
    return d;
}

static bool decl_set_provenance(MgDecl *d, const char *source_name,
                                const char *origin, uint64_t line) {
    d->source_name = dup_text(source_name);
    d->origin = dup_text(origin);
    d->line = line;
    return (!source_name || d->source_name) && (!origin || d->origin);
}

/* Reserve the change-revision slot a realization needs. */
static bool reserve_change(MgTheory *t) {
    if (t->withdrawal_len < t->withdrawal_cap)
        return true;
    uint32_t cap = t->withdrawal_cap ? t->withdrawal_cap * 2u : 8u;
    uint64_t *grown = mg_sys_realloc(t->withdrawals, sizeof(uint64_t) * cap);
    if (!grown)
        return false;
    t->withdrawals = grown;
    t->withdrawal_cap = cap;
    return true;
}

/* Publish a reserved declaration.  Cannot fail (a realization's change slot
 * is reserved by the caller). */
static void commit_decl(MgTheory *t, MgDecl *d) {
    MgName *n = t->store.names[d->name];
    d->seq = t->decl_len;
    d->added_rev = t->rev + 1u;
    d->removed_rev = UINT64_MAX;
    if (d->ns == MG_NS_TERM && n->decl[MG_NS_TERM]) {
        n->realization = d;
        t->withdrawals[t->withdrawal_len++] = d->added_rev;
    } else {
        n->decl[d->ns] = d;
    }
    t->decls[t->decl_len++] = d;
    t->rev += 1u;
    for (uint32_t i = 0; i < t->assoc_len; ++i)
        if (t->assoc[i].name == d->name && t->assoc[i].ns == d->ns)
            t->assoc[i].admitted = true;
}

MgStatus mg_theory_admit(MgTheory *t, MgBudget *budget, const MgItem *item,
                         uint64_t expected_rev, const MgDecl **out) {
    if (out)
        *out = NULL;
    if (t->closed)
        return MG_DENIED;
    if (expected_rev && expected_rev != t->rev)
        return MG_CONFLICT;
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(t), budget, NULL);
    MgStatus st = mg_item_check(&c, item);
    if (st != MG_OK)
        return st;
    if (item->kind == MG_ITEM_CONJECTURE)
        return MG_OK; /* conjectures add nothing */
    if (item->kind != MG_ITEM_PARAMETER && item->kind != MG_ITEM_DEFINITION &&
        !t->publication_permitted)
        return MG_DENIED;
    MgDecl *d = reserve_decl(t);
    if (!d)
        return MG_NOMEM;
    mg_footprint_init(&d->deps);
    d->name = item->name;
    switch (item->kind) {
    case MG_ITEM_PARAMETER:
        d->ns = MG_NS_TERM;
        d->kind = MG_DECL_PARAMETER;
        d->type = item->type;
        break;
    case MG_ITEM_DEFINITION:
        d->ns = MG_NS_TERM;
        d->kind = MG_DECL_DEFINITION;
        d->type = item->type;
        d->def = item->body;
        if (!mg_footprint_add_term(&d->deps, item->body))
            goto nomem;
        break;
    case MG_ITEM_KNOWN:
        d->ns = MG_NS_KNOWN;
        d->kind = MG_DECL_AXIOM;
        d->prop = item->prop;
        if (!mg_footprint_add_term(&d->deps, item->prop))
            goto nomem;
        break;
    case MG_ITEM_THEOREM:
        d->ns = MG_NS_KNOWN;
        d->kind = MG_DECL_THEOREM;
        d->prop = item->prop;
        d->proof = item->proof;
        if (!mg_footprint_add_term(&d->deps, item->prop) ||
            !mg_footprint_add_proof(&d->deps, item->proof))
            goto nomem;
        break;
    default:
        decl_free(d);
        return MG_MALFORMED;
    }
    if (!decl_set_provenance(d, item->source_name, item->origin, item->line))
        goto nomem;
    if (item->kind == MG_ITEM_DEFINITION && !mg_theory_name_fresh(t, item->name, MG_NS_TERM) &&
        !reserve_change(t))
        goto nomem;
    commit_decl(t, d);
    if (out)
        *out = d;
    return MG_OK;
nomem:
    decl_free(d);
    return MG_NOMEM;
}

MgStatus mg_theory_publish_known(MgTheory *t, uint64_t captured_rev,
                                 uint32_t name, MgTm *prop,
                                 const MgFootprint *deps, MgEvidence *evidence,
                                 const char *source_name, const char *origin,
                                 const MgDecl **out) {
    if (out)
        *out = NULL;
    if (t->closed) {
        mg_evidence_destroy(evidence);
        return MG_DENIED;
    }
    /* Reserve first, so every failure below leaves the theory unchanged. */
    MgDecl *d = reserve_decl(t);
    if (!d) {
        mg_evidence_destroy(evidence);
        return MG_NOMEM;
    }
    mg_footprint_init(&d->deps);
    d->evidence = evidence;
    if (!mg_footprint_union(&d->deps, deps) ||
        !mg_footprint_add_term(&d->deps, prop) ||
        !decl_set_provenance(d, source_name, origin, 0u)) {
        decl_free(d);
        return MG_NOMEM;
    }
    MgStatus st = MG_OK;
    if (!t->publication_permitted)
        st = MG_DENIED;
    else if (captured_rev != t->rev)
        st = MG_CONFLICT;
    else if (!mg_theory_name_fresh(t, name, MG_NS_KNOWN))
        st = MG_REFUSED;
    if (st != MG_OK) {
        decl_free(d);
        return st;
    }
    d->name = name;
    d->ns = MG_NS_KNOWN;
    d->kind = MG_DECL_THEOREM;
    d->prop = prop;
    commit_decl(t, d);
    if (out)
        *out = d;
    return MG_OK;
}

/* Withdraw a declaration and, at the same revision, every live declaration
 * whose admission consulted a withdrawn name (transitively). */
MgStatus mg_theory_withdraw(MgTheory *t, uint32_t name, MgNamespace ns) {
    if (t->closed)
        return MG_DENIED;
    const MgName *n = mg_name_get(&t->store, name);
    MgDecl *target = n ? n->decl[ns] : NULL;
    MgEnv live = mg_theory_snapshot(t);
    if (!target || !mg_env_decl_visible(live, target))
        return MG_REFUSED;
    MgDecl *realization = ns == MG_NS_TERM ? n->realization : NULL;
    if (t->withdrawal_len == t->withdrawal_cap) {
        uint32_t cap = t->withdrawal_cap ? t->withdrawal_cap * 2u : 8u;
        uint64_t *grown = mg_sys_realloc(t->withdrawals, sizeof(uint64_t) * cap);
        if (!grown)
            return MG_NOMEM;
        t->withdrawals = grown;
        t->withdrawal_cap = cap;
    }
    uint64_t rev = t->rev + 1u;
    target->removed_rev = rev;
    if (realization && mg_env_decl_visible(live, realization))
        realization->removed_rev = rev;
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint64_t i = 0; i < t->decl_len; ++i) {
            MgDecl *d = t->decls[i];
            if (d->removed_rev != UINT64_MAX)
                continue;
            for (uint32_t k = 0; k < d->deps.len; ++k) {
                const MgName *dn = mg_name_get(&t->store,
                                               (uint32_t)(d->deps.items[k] >> 1));
                const MgDecl *dep = dn ? dn->decl[d->deps.items[k] & 1u] : NULL;
                if (dep && dep->removed_rev == rev) {
                    d->removed_rev = rev;
                    changed = true;
                    break;
                }
            }
        }
    }
    t->withdrawals[t->withdrawal_len++] = rev;
    t->rev = rev;
    return MG_OK;
}
