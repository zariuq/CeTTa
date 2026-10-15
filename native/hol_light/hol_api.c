/* hol_api.c - the reference-based public interface (hol_kernel.h).
 *
 * Every entry point validates its references before use, may collect
 * garbage on entry (when no raw pointer is held), and wraps each result in a
 * fresh reference owned by the current owner.  A theorem reference carries
 * the serial of the rule application that produced it. */
#include "hol_internal.h"
#include "hol_replay.h"

#include <stdatomic.h>
#include <stdio.h>

/* 8192 levels need about 2 MiB of stack at -O2 (the deepest recursions use
 * at most 256 bytes per level), so the default fits ordinary 8 MiB thread
 * stacks; HOL Light's core library needs fewer than 1024. */
#define HOL_DEFAULT_MAX_DEPTH (1u << 13)
#define HOL_DEFAULT_GC_THRESHOLD ((size_t)64 << 20)

/* Kernel tags, the 16-bit kernel field of every reference.  A tag belongs to
 * one live kernel at a time, so no live kernel ever resolves another live
 * kernel's reference; tags are handed out round-robin to delay reuse. */
static _Atomic uint64_t g_live_tags[1024]; /* bit t set: tag t in use; 0 is never used */
static _Atomic uint32_t g_next_tag;

static uint16_t tag_acquire(void) {
    for (uint32_t tries = 0; tries < 65535u; tries++) {
        uint32_t t = atomic_fetch_add(&g_next_tag, 1u) % 65535u + 1u;
        uint64_t bit = (uint64_t)1 << (t & 63);
        if (!(atomic_fetch_or(&g_live_tags[t >> 6], bit) & bit)) return (uint16_t)t;
    }
    return 0; /* 65535 kernels are alive */
}

static void tag_release(uint16_t t) {
    atomic_fetch_and(&g_live_tags[t >> 6], ~((uint64_t)1 << (t & 63)));
}

const char *hol_status_name(HolStatus s) {
    static const char *const names[] = {"OK", "REFUSED", "MALFORMED", "STALE", "FOREIGN",
                                        "NOMEM", "LIMIT", "INTERRUPTED", "POLICY"};
    return (unsigned)s < sizeof names / sizeof names[0] ? names[s] : "?";
}

const char *hol_reason_name(HolReason r) {
    static const char *const names[HOL_R__COUNT] = {
        "none", "null-arg", "bad-handle", "wrong-kind", "stale-handle", "future-handle",
        "foreign-kernel", "incompatible-env", "bad-name", "bad-index",
        "mk_type: undeclared", "mk_type: arity", "dest_type", "dest_vartype",
        "mk_const: undeclared", "const: not an instance", "mk_abs: not a variable",
        "mk_comb: types do not agree", "dest_var", "dest_const", "dest_comb", "dest_abs",
        "dest_eq", "rator", "rand", "variant: not a variable", "vsubst: bad substitution list",
        "TRANS", "MK_COMB: not both equations", "MK_COMB: types do not agree",
        "ABS: not a variable or not an equation", "ABS: variable free in hypothesis",
        "BETA: not a trivial beta-redex", "ASSUME: not a proposition", "EQ_MP",
        "new_type: already declared", "new_constant: already declared",
        "new_axiom: not a proposition", "axiom policy", "policy cannot be loosened",
        "new_basic_definition: shape", "new_basic_definition: already defined",
        "new_definition: term not closed", "new_definition: type variables not reflected",
        "new_basic_type_definition: constants in use",
        "new_basic_type_definition: assumptions in theorem",
        "new_basic_type_definition: not a combination",
        "new_basic_type_definition: predicate not closed",
        "new_basic_type_definition: type already defined",
        "new_basic_type_definition: abs and rep names equal (native atomic refusal)",
        "out of memory", "depth limit", "fuel exhausted", "interrupted",
        "stream: bad magic", "stream: unsupported version", "stream: bad header",
        "stream: truncated", "stream: bad integer encoding", "stream: unknown opcode",
        "stream: future reference", "stream: dropped reference", "stream: failed reference",
        "stream: trailing bytes", "stream: checksum mismatch", "stream: record count mismatch",
        "stream: limit exceeded", "stream: expectation failed", "stream: unknown label",
        "stream: record after END", "stream: missing END", "stream: bad value"};
    return (unsigned)r < HOL_R__COUNT && names[r] ? names[r] : "?";
}

HolKernel *hol_kernel_new(const HolConfig *cfg) {
    HolKernel *k = (HolKernel *)calloc(1, sizeof *k);
    if (!k) return NULL;
    k->tag = tag_acquire();
    if (!k->tag) { free(k); return NULL; }
    k->hash_mask = (cfg && cfg->use_hash_mask) ? cfg->hash_mask : 0xffffffffu;
    k->max_depth = (cfg && cfg->max_depth) ? cfg->max_depth : HOL_DEFAULT_MAX_DEPTH;
    k->gc_threshold = (cfg && cfg->gc_threshold) ? cfg->gc_threshold : HOL_DEFAULT_GC_THRESHOLD;
    k->tick = 1024;
    k->stamp = 0;
    k->owners = (HolOwnerRec *)hk_alloc(k, 16 * sizeof(HolOwnerRec));
    if (!k->owners) {
        tag_release(k->tag);
        free(k);
        return NULL;
    }
    k->capowners = 16;
    k->nowners = 1;
    memset(&k->owners[0], 0, sizeof k->owners[0]);
    k->owners[0].live = 1;
    k->cur_owner = HOL_OWNER_KERNEL;
    if (hk_env_init(k) < 0) {
        hol_kernel_free(k);
        return NULL;
    }
    hk_clear(k);
    return k;
}

static void free_table(HolKernel *k, HolTable *t, int kind) {
    for (size_t i = 0; i < t->cap; i++) {
        void *o = t->slot[i];
        if (!o) continue;
        size_t sz;
        switch (kind) {
        case 0: sz = sizeof(HolType) + ((HolType *)o)->nargs * sizeof(HolType *); break;
        case 1: sz = sizeof(HolTerm); break;
        case 2: sz = sizeof(HolThm) + ((HolThm *)o)->nhyps * sizeof(HolTerm *); break;
        default: sz = sizeof(HolDeps) + ((HolDeps *)o)->n * sizeof(HolDecl *); break;
        }
        hk_dealloc(k, o, sz);
    }
    hk_table_free(k, t);
}

void hol_kernel_free(HolKernel *k) {
    if (!k) return;
    free_table(k, &k->thms, 2);
    free_table(k, &k->terms, 1);
    free_table(k, &k->types, 0);
    free_table(k, &k->deps, 3);
    hk_env_free_all(k);
    for (uint32_t i = 1; i <= k->nnames; i++)
        hk_dealloc(k, k->names[i].bytes, k->names[i].len + 1);
    hk_dealloc(k, k->names, k->capnames * sizeof(HolNameRec));
    hk_dealloc(k, k->name_index, k->name_index_cap * sizeof(uint32_t));
    hk_handles_free(k);
    hk_journal_free(k);
    free(k->roots);
    free(k->markstack);
    free(k->memo.e);
    tag_release(k->tag);
    free(k);
}

HolStatus hol_last_status(const HolKernel *k) { return k ? k->status : HOL_MALFORMED; }
HolReason hol_last_reason(const HolKernel *k) { return k ? k->reason : HOL_R_NULL_ARG; }

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

#define BEGIN(k)                         \
    do {                                 \
        if (!(k)) return HOL_MALFORMED;  \
        hk_begin_call(k);               \
        hk_maybe_gc(k);                 \
    } while (0)

static HolType *get_ty(HolKernel *k, HolRef r) { return (HolType *)hk_ref_get(k, r, HOL_KIND_TYPE, NULL); }
static HolTerm *get_tm(HolKernel *k, HolRef r) { return (HolTerm *)hk_ref_get(k, r, HOL_KIND_TERM, NULL); }
static HolThm *get_th(HolKernel *k, HolRef r) { return (HolThm *)hk_ref_get(k, r, HOL_KIND_THM, NULL); }
static HolDecl *get_env(HolKernel *k, HolRef r) { return (HolDecl *)hk_ref_get(k, r, HOL_KIND_ENV, NULL); }

static HolStatus status_or(HolKernel *k, HolStatus fallback) {
    if (k->status == HOL_OK) { k->status = fallback; k->reason = HOL_R_BAD_INDEX; }
    return k->status;
}

static HolStatus give(HolKernel *k, HolKind kind, void *obj, uint64_t ev, HolRef *out) {
    if (!out) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    if (!obj) return status_or(k, HOL_MALFORMED);
    HolRef r = hk_ref_new(k, kind, obj, ev);
    if (!r) return status_or(k, HOL_NOMEM);
    *out = r;
    return HOL_OK;
}

/* Wraps a rule result; journals the event when journaling is on. */
static HolStatus give_rule(HolKernel *k, HolThm *th, HolRef *out, uint8_t op,
                           const uint64_t *prem, HolThm *const *pthm, unsigned nprem,
                           void *const *args, uint32_t nargs) {
    if (!th) return give(k, HOL_KIND_THM, NULL, 0, out);
    uint64_t ev = ++k->serial;
    HolStatus s = give(k, HOL_KIND_THM, th, ev, out);
    if (s == HOL_OK) hk_journal_add(k, op, ev, th, prem, pthm, nprem, args, nargs);
    return s;
}

static HolName name_of(HolKernel *k, const char *s) {
    if (!s) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return 0; }
    return hk_name_intern(k, s, strlen(s));
}

static void *alloc_pairs(HolKernel *k, size_t n, size_t elem) {
    if (n > ((size_t)-1) / 4 / elem) return hk_fail(k, HOL_MALFORMED, HOL_R_BAD_INDEX);
    return hk_alloc(k, (n ? n : 1) * elem);
}

static HolTyPair *ty_pairs(HolKernel *k, const HolRef *pairs, size_t n) {
    if (n && !pairs) return hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG);
    HolTyPair *p = (HolTyPair *)alloc_pairs(k, n, sizeof(HolTyPair));
    if (!p) return NULL;
    for (size_t i = 0; i < n; i++) {
        p[i].repl = get_ty(k, pairs[2 * i]);
        p[i].key = get_ty(k, pairs[2 * i + 1]);
        if (!p[i].repl || !p[i].key) { hk_dealloc(k, p, (n ? n : 1) * sizeof(HolTyPair)); return NULL; }
    }
    return p;
}

static HolTmPair *tm_pairs(HolKernel *k, const HolRef *pairs, size_t n) {
    if (n && !pairs) return hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG);
    HolTmPair *p = (HolTmPair *)alloc_pairs(k, n, sizeof(HolTmPair));
    if (!p) return NULL;
    for (size_t i = 0; i < n; i++) {
        p[i].t = get_tm(k, pairs[2 * i]);
        p[i].x = get_tm(k, pairs[2 * i + 1]);
        if (!p[i].t || !p[i].x) { hk_dealloc(k, p, (n ? n : 1) * sizeof(HolTmPair)); return NULL; }
    }
    return p;
}

static HolStatus give_vec(HolKernel *k, HolKind kind, HolVec *v, HolRef *buf, size_t cap,
                          size_t *count) {
    if (!count) { hk_vec_free(k, v); hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    *count = v->n;
    for (size_t i = 0; i < v->n && i < cap; i++) {
        if (!buf) break;
        HolRef r = hk_ref_new(k, kind, v->v[i], 0);
        if (!r) { /* a failed call leaves no reference behind */
            while (i-- > 0) hk_ref_drop(k, buf[i]);
            hk_vec_free(k, v);
            return status_or(k, HOL_NOMEM);
        }
        buf[i] = r;
    }
    hk_vec_free(k, v);
    return HOL_OK;
}

/* ------------------------------------------------------------------------ */
/* Environments                                                              */
/* ------------------------------------------------------------------------ */

HolStatus hol_env_bare(HolKernel *k, HolRef *env_out) {
    BEGIN(k);
    return give(k, HOL_KIND_ENV, k->d_eq, k->d_eq->serial, env_out);
}

HolStatus hol_env_set_axiom_policy(HolKernel *k, HolRef env, HolAxiomPolicy p, HolRef *env_out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    HolDecl *d = hk_env_set_policy(k, e, (int)p);
    return give(k, HOL_KIND_ENV, d, d ? d->serial : 0, env_out);
}

HolStatus hol_new_type(HolKernel *k, HolRef env, const char *name, uint32_t arity,
                       HolRef *env_out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    HolName n = name_of(k, name);
    if (!n) return k->status;
    HolDecl *d = hk_env_new_type(k, e, n, arity);
    return give(k, HOL_KIND_ENV, d, d ? d->serial : 0, env_out);
}

HolStatus hol_new_constant(HolKernel *k, HolRef env, const char *name, HolRef type,
                           HolRef *env_out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    HolType *ty = e ? get_ty(k, type) : NULL;
    if (!ty) return k->status;
    HolName n = name_of(k, name);
    if (!n) return k->status;
    HolDecl *d = hk_env_new_constant(k, e, n, ty);
    return give(k, HOL_KIND_ENV, d, d ? d->serial : 0, env_out);
}

HolStatus hol_new_axiom(HolKernel *k, HolRef env, HolRef prop, HolRef *env_out, HolRef *thm_out) {
    BEGIN(k);
    if (!env_out || !thm_out) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    HolDecl *e = get_env(k, env);
    HolTerm *tm = e ? get_tm(k, prop) : NULL;
    if (!tm) return k->status;
    HolThm *th = NULL;
    HolDecl *d = hk_env_new_axiom(k, e, tm, &th);
    if (!d) return status_or(k, HOL_MALFORMED);
    HolRef r1 = 0, r2 = 0;
    if (give(k, HOL_KIND_ENV, d, d->serial, &r1) != HOL_OK) return k->status;
    if (give(k, HOL_KIND_THM, th, d->serial, &r2) != HOL_OK) {
        hk_ref_drop(k, r1);
        return HOL_NOMEM;
    }
    *env_out = r1;
    *thm_out = r2;
    return HOL_OK;
}

HolStatus hol_new_basic_definition(HolKernel *k, HolRef env, HolRef eqn, HolRef *env_out,
                                   HolRef *thm_out) {
    BEGIN(k);
    if (!env_out || !thm_out) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    HolDecl *e = get_env(k, env);
    HolTerm *tm = e ? get_tm(k, eqn) : NULL;
    if (!tm) return k->status;
    HolThm *th = NULL;
    HolDecl *d = hk_env_new_definition(k, e, tm, &th);
    if (!d) return status_or(k, HOL_MALFORMED);
    HolRef r1 = 0, r2 = 0;
    if (give(k, HOL_KIND_ENV, d, d->serial, &r1) != HOL_OK) return k->status;
    if (give(k, HOL_KIND_THM, th, d->serial, &r2) != HOL_OK) {
        hk_ref_drop(k, r1);
        return HOL_NOMEM;
    }
    *env_out = r1;
    *thm_out = r2;
    return HOL_OK;
}

HolStatus hol_new_basic_type_definition(HolKernel *k, HolRef env, const char *tyname,
                                        const char *absname, const char *repname, HolRef witness,
                                        HolRef *env_out, HolRef *abs_rep_out, HolRef *rep_abs_out) {
    BEGIN(k);
    if (!env_out || !abs_rep_out || !rep_abs_out) {
        hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG);
        return k->status;
    }
    HolDecl *e = get_env(k, env);
    uint64_t wev = 0;
    HolThm *w = e ? (HolThm *)hk_ref_get(k, witness, HOL_KIND_THM, &wev) : NULL;
    if (!w) return k->status;
    HolName nt = name_of(k, tyname), na = nt ? name_of(k, absname) : 0,
            nr = na ? name_of(k, repname) : 0;
    if (!nr) return k->status;
    HolThm *t1 = NULL, *t2 = NULL;
    HolDecl *d = hk_env_new_type_definition(k, e, nt, na, nr, w, &t1, &t2);
    if (!d) return status_or(k, HOL_MALFORMED);
    d->witness_event = d->parent->witness_event = d->parent->parent->witness_event = wev;
    HolRef r1 = 0, r2 = 0, r3 = 0;
    if (give(k, HOL_KIND_ENV, d, d->serial, &r1) != HOL_OK) return k->status;
    if (give(k, HOL_KIND_THM, t1, d->serial, &r2) != HOL_OK) {
        hk_ref_drop(k, r1);
        return HOL_NOMEM;
    }
    if (give(k, HOL_KIND_THM, t2, d->serial, &r3) != HOL_OK) {
        hk_ref_drop(k, r1);
        hk_ref_drop(k, r2);
        return HOL_NOMEM;
    }
    *env_out = r1;
    *abs_rep_out = r2;
    *rep_abs_out = r3;
    return HOL_OK;
}

static HolDecl *anchor_of(HolKernel *k, HolRef obj, bool *found) {
    *found = true;
    switch (hk_ref_kind(k, obj)) {
    case HOL_KIND_TYPE: return get_ty(k, obj)->anchor;
    case HOL_KIND_TERM: return get_tm(k, obj)->anchor;
    case HOL_KIND_THM: return get_th(k, obj)->anchor;
    case HOL_KIND_ENV: return get_env(k, obj);
    default: *found = false; return NULL;
    }
}

HolStatus hol_env_admits(HolKernel *k, HolRef env, HolRef object, bool *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e || !out) return status_or(k, HOL_MALFORMED);
    bool found;
    HolDecl *a = anchor_of(k, object, &found);
    if (!found) return status_or(k, HOL_MALFORMED);
    *out = hk_decl_le(a, e);
    return HOL_OK;
}

HolStatus hol_env_extends(HolKernel *k, HolRef older, HolRef newer, bool *out) {
    BEGIN(k);
    HolDecl *a = get_env(k, older);
    HolDecl *b = a ? get_env(k, newer) : NULL;
    if (!b || !out) return status_or(k, HOL_MALFORMED);
    *out = hk_decl_le(a, b);
    return HOL_OK;
}

HolStatus hol_get_type_arity(HolKernel *k, HolRef env, const char *name, uint32_t *arity_out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    HolName n = e ? name_of(k, name) : 0;
    if (!n || !arity_out) return status_or(k, HOL_MALFORMED);
    HolDecl *d = hk_lookup_type(k, e, n);
    if (!d) { hk_fail(k, HOL_REFUSED, HOL_R_MK_TYPE_UNDECLARED); return k->status; }
    *arity_out = d->arity;
    return HOL_OK;
}

HolStatus hol_get_const_type(HolKernel *k, HolRef env, const char *name, HolRef *type_out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    HolName n = e ? name_of(k, name) : 0;
    if (!n) return k->status;
    HolDecl *d = hk_lookup_const(k, e, n);
    if (!d) { hk_fail(k, HOL_REFUSED, HOL_R_MK_CONST_UNDECLARED); return k->status; }
    return give(k, HOL_KIND_TYPE, d->gtype, 0, type_out);
}

HolStatus hol_env_policy(HolKernel *k, HolRef env, HolAxiomPolicy *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e || !out) return status_or(k, HOL_MALFORMED);
    *out = (HolAxiomPolicy)e->policy;
    return HOL_OK;
}

HolStatus hol_env_length(HolKernel *k, HolRef env, uint64_t *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e || !out) return status_or(k, HOL_MALFORMED);
    *out = e->depth - k->d_eq->depth;
    return HOL_OK;
}

HolStatus hol_env_digest(HolKernel *k, HolRef env, uint64_t *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e || !out) return status_or(k, HOL_MALFORMED);
    if (hk_env_digest(k, e, out) < 0) return status_or(k, HOL_NOMEM);
    return HOL_OK;
}

static HolStatus env_thms(HolKernel *k, HolRef env, int kind, HolRef *buf, size_t cap,
                          size_t *count) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e || !count) return status_or(k, HOL_MALFORMED);
    size_t n = 0;
    for (HolDecl *d = e; d; d = d->parent) {
        if (d->kind != kind) continue;
        if (buf && n < cap) {
            HolRef r = hk_ref_new(k, HOL_KIND_THM, d->thm, d->serial);
            if (!r) { /* a failed call leaves no reference behind */
                while (n-- > 0) hk_ref_drop(k, buf[n]);
                return status_or(k, HOL_NOMEM);
            }
            buf[n] = r;
        }
        n++;
    }
    *count = n;
    return HOL_OK;
}

HolStatus hol_env_axioms(HolKernel *k, HolRef env, HolRef *buf, size_t cap, size_t *count) {
    return env_thms(k, env, HOL_DECL_AXIOM, buf, cap, count);
}

HolStatus hol_env_definitions(HolKernel *k, HolRef env, HolRef *buf, size_t cap, size_t *count) {
    return env_thms(k, env, HOL_DECL_DEF, buf, cap, count);
}

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

HolStatus hol_mk_vartype(HolKernel *k, const char *name, HolRef *out) {
    BEGIN(k);
    HolName n = name_of(k, name);
    if (!n) return k->status;
    return give(k, HOL_KIND_TYPE, hk_ty_var(k, n), 0, out);
}

HolStatus hol_mk_type(HolKernel *k, HolRef env, const char *name, const HolRef *args, size_t nargs,
                      HolRef *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    HolName n = name_of(k, name);
    if (!n) return k->status;
    if (nargs > UINT32_MAX || (nargs && !args)) { hk_fail(k, HOL_MALFORMED, HOL_R_BAD_INDEX); return k->status; }
    HolType **a = (HolType **)hk_alloc(k, (nargs ? nargs : 1) * sizeof(HolType *));
    if (!a) return k->status;
    for (size_t i = 0; i < nargs; i++)
        if (!(a[i] = get_ty(k, args[i]))) { hk_dealloc(k, a, (nargs ? nargs : 1) * sizeof(HolType *)); return k->status; }
    HolType *t = hk_mk_type_in(k, e, n, a, (uint32_t)nargs);
    hk_dealloc(k, a, (nargs ? nargs : 1) * sizeof(HolType *));
    return give(k, HOL_KIND_TYPE, t, 0, out);
}

HolStatus hol_bool_ty(HolKernel *k, HolRef *out) { BEGIN(k); return give(k, HOL_KIND_TYPE, k->bool_ty, 0, out); }
HolStatus hol_aty(HolKernel *k, HolRef *out) { BEGIN(k); return give(k, HOL_KIND_TYPE, k->aty, 0, out); }

bool hol_is_vartype(HolKernel *k, HolRef ty) {
    if (!k) return false;
    hk_begin_call(k);
    HolType *t = get_ty(k, ty);
    return t && t->kind == HOL_TY_VAR;
}

bool hol_is_type(HolKernel *k, HolRef ty) {
    if (!k) return false;
    hk_begin_call(k);
    HolType *t = get_ty(k, ty);
    return t && t->kind == HOL_TY_APP;
}

const char *hol_type_name(HolKernel *k, HolRef ty) {
    if (!k) return NULL;
    hk_begin_call(k);
    HolType *t = get_ty(k, ty);
    return t ? k->names[t->name].bytes : NULL;
}

HolStatus hol_type_nargs(HolKernel *k, HolRef ty, size_t *out) {
    BEGIN(k);
    HolType *t = get_ty(k, ty);
    if (!t || !out) return status_or(k, HOL_MALFORMED);
    if (t->kind != HOL_TY_APP) { hk_fail(k, HOL_REFUSED, HOL_R_DEST_TYPE); return k->status; }
    *out = t->nargs;
    return HOL_OK;
}

HolStatus hol_type_arg(HolKernel *k, HolRef ty, size_t i, HolRef *out) {
    BEGIN(k);
    HolType *t = get_ty(k, ty);
    if (!t) return k->status;
    if (t->kind != HOL_TY_APP) { hk_fail(k, HOL_REFUSED, HOL_R_DEST_TYPE); return k->status; }
    if (i >= t->nargs) { hk_fail(k, HOL_MALFORMED, HOL_R_BAD_INDEX); return k->status; }
    return give(k, HOL_KIND_TYPE, t->args[i], 0, out);
}

HolStatus hol_type_subst(HolKernel *k, const HolRef *pairs, size_t npairs, HolRef ty, HolRef *out) {
    BEGIN(k);
    HolType *t = get_ty(k, ty);
    if (!t) return k->status;
    HolTyPair *p = ty_pairs(k, pairs, npairs);
    if (!p) return k->status;
    HolTySubst s;
    hk_tysubst_init(&s, p, npairs);
    HolType *r = hk_type_subst(k, &s, t);
    hk_dealloc(k, p, (npairs ? npairs : 1) * sizeof(HolTyPair));
    return give(k, HOL_KIND_TYPE, r, 0, out);
}

HolStatus hol_tyvars(HolKernel *k, HolRef ty, HolRef *buf, size_t cap, size_t *count) {
    BEGIN(k);
    HolType *t = get_ty(k, ty);
    if (!t) return k->status;
    HolVec v = {0};
    if (hk_tyvars_exact(k, t, &v) < 0) { hk_vec_free(k, &v); return k->status; }
    return give_vec(k, HOL_KIND_TYPE, &v, buf, cap, count);
}

/* ------------------------------------------------------------------------ */
/* Terms                                                                     */
/* ------------------------------------------------------------------------ */

HolStatus hol_mk_var(HolKernel *k, const char *name, HolRef type, HolRef *out) {
    BEGIN(k);
    HolType *t = get_ty(k, type);
    if (!t) return k->status;
    HolName n = name_of(k, name);
    if (!n) return k->status;
    return give(k, HOL_KIND_TERM, hk_tm_var(k, n, t), 0, out);
}

HolStatus hol_mk_const(HolKernel *k, HolRef env, const char *name, const HolRef *theta, size_t npairs,
                       HolRef *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    HolName n = name_of(k, name);
    if (!n) return k->status;
    HolTyPair *p = ty_pairs(k, theta, npairs);
    if (!p) return k->status;
    HolTerm *c = hk_mk_const_in(k, e, n, p, npairs);
    hk_dealloc(k, p, (npairs ? npairs : 1) * sizeof(HolTyPair));
    return give(k, HOL_KIND_TERM, c, 0, out);
}

HolStatus hol_mk_const_at(HolKernel *k, HolRef env, const char *name, HolRef type, HolRef *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    HolType *t = get_ty(k, type);
    if (!t) return k->status;
    HolName n = name_of(k, name);
    if (!n) return k->status;
    return give(k, HOL_KIND_TERM, hk_mk_const_at(k, e, n, t), 0, out);
}

HolStatus hol_mk_comb(HolKernel *k, HolRef f, HolRef a, HolRef *out) {
    BEGIN(k);
    HolTerm *x = get_tm(k, f), *y = x ? get_tm(k, a) : NULL;
    if (!y) return k->status;
    return give(k, HOL_KIND_TERM, hk_tm_comb(k, x, y), 0, out);
}

HolStatus hol_mk_abs(HolKernel *k, HolRef v, HolRef body, HolRef *out) {
    BEGIN(k);
    HolTerm *x = get_tm(k, v), *y = x ? get_tm(k, body) : NULL;
    if (!y) return k->status;
    return give(k, HOL_KIND_TERM, hk_tm_abs(k, x, y), 0, out);
}

/* let mk_eq = let eq = mk_const("=",[]) in
 *   fun (l,r) -> try let ty = type_of l in
 *                    let eq_tm = inst [ty,aty] eq in
 *                    mk_comb(mk_comb(eq_tm,l),r)
 *                with Failure _ -> failwith "mk_eq"                         */
HolStatus hol_mk_eq(HolKernel *k, HolRef l, HolRef r, HolRef *out) {
    BEGIN(k);
    HolTerm *x = get_tm(k, l), *y = x ? get_tm(k, r) : NULL;
    if (!y) return k->status;
    if (x->type != y->type) { hk_fail(k, HOL_REFUSED, HOL_R_MK_COMB_TYPES); return k->status; }
    return give(k, HOL_KIND_TERM, hk_tm_eq(k, x, y), 0, out);
}

HolTermKind hol_term_kind(HolKernel *k, HolRef tm) {
    if (!k) return HOL_TERM_INVALID;
    hk_begin_call(k);
    HolTerm *t = get_tm(k, tm);
    return t ? (HolTermKind)t->kind : HOL_TERM_INVALID;
}

const char *hol_term_name(HolKernel *k, HolRef tm) {
    if (!k) return NULL;
    hk_begin_call(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return NULL;
    if (t->kind != HOL_TM_VAR && t->kind != HOL_TM_CONST) { hk_fail(k, HOL_REFUSED, HOL_R_DEST_VAR); return NULL; }
    return k->names[t->u.leaf.name].bytes;
}

HolStatus hol_type_of(HolKernel *k, HolRef tm, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    return give(k, HOL_KIND_TYPE, t->type, 0, out);
}

HolStatus hol_term_part(HolKernel *k, HolRef tm, int which, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    if (t->kind != HOL_TM_COMB && t->kind != HOL_TM_ABS) {
        hk_fail(k, HOL_REFUSED, HOL_R_DEST_COMB);
        return k->status;
    }
    if (which != 0 && which != 1) { hk_fail(k, HOL_MALFORMED, HOL_R_BAD_INDEX); return k->status; }
    return give(k, HOL_KIND_TERM, which ? t->u.node.r : t->u.node.l, 0, out);
}

HolStatus hol_dest_eq(HolKernel *k, HolRef tm, HolRef *l, HolRef *r) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    if (!hk_tm_is_eq(k, t)) { hk_fail(k, HOL_REFUSED, HOL_R_DEST_EQ); return k->status; }
    if (give(k, HOL_KIND_TERM, hk_eq_lhs(t), 0, l) != HOL_OK) return k->status;
    if (give(k, HOL_KIND_TERM, hk_eq_rhs(t), 0, r) != HOL_OK) {
        hk_ref_drop(k, *l);
        return HOL_NOMEM;
    }
    return HOL_OK;
}

HolStatus hol_alphaorder(HolKernel *k, HolRef a, HolRef b, int *out) {
    BEGIN(k);
    HolTerm *x = get_tm(k, a), *y = x ? get_tm(k, b) : NULL;
    if (!y || !out) return status_or(k, HOL_MALFORMED);
    HolDecl *anc;
    if (hk_anchor_join(x->anchor, y->anchor, &anc) < 0) { hk_fail(k, HOL_FOREIGN, HOL_R_INCOMPATIBLE_ENV); return k->status; }
    int c;
    if (hk_alphaorder(k, x, y, &c) < 0) return k->status;
    *out = c;
    return HOL_OK;
}

HolStatus hol_aconv(HolKernel *k, HolRef a, HolRef b, bool *out) {
    int c = 0;
    HolStatus s = hol_alphaorder(k, a, b, &c);
    if (s == HOL_OK && out) *out = c == 0;
    return s;
}

HolStatus hol_compare_terms(HolKernel *k, HolRef a, HolRef b, int *out) {
    BEGIN(k);
    HolTerm *x = get_tm(k, a), *y = x ? get_tm(k, b) : NULL;
    if (!y || !out) return status_or(k, HOL_MALFORMED);
    *out = hk_tm_compare(k, x, y);
    return HOL_OK;
}

HolStatus hol_vfree_in(HolKernel *k, HolRef v, HolRef tm, bool *out) {
    BEGIN(k);
    HolTerm *x = get_tm(k, v), *y = x ? get_tm(k, tm) : NULL;
    if (!y || !out) return status_or(k, HOL_MALFORMED);
    int r = hk_vfree_in(k, x, y);
    if (r < 0) return k->status;
    *out = r != 0;
    return HOL_OK;
}

HolStatus hol_freesin(HolKernel *k, const HolRef *acc, size_t n, HolRef tm, bool *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t || !out || (n && !acc)) return status_or(k, HOL_MALFORMED);
    HolTerm **a = (HolTerm **)alloc_pairs(k, n, sizeof(HolTerm *));
    if (!a) return k->status;
    for (size_t i = 0; i < n; i++)
        if (!(a[i] = get_tm(k, acc[i]))) { hk_dealloc(k, a, (n ? n : 1) * sizeof(HolTerm *)); return k->status; }
    int r = hk_freesin(k, a, n, t);
    hk_dealloc(k, a, (n ? n : 1) * sizeof(HolTerm *));
    if (r < 0) return k->status;
    *out = r != 0;
    return HOL_OK;
}

HolStatus hol_frees(HolKernel *k, HolRef tm, HolRef *buf, size_t cap, size_t *count) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    HolVec v = {0};
    if (hk_frees_exact(k, t, &v) < 0) { hk_vec_free(k, &v); return k->status; }
    return give_vec(k, HOL_KIND_TERM, &v, buf, cap, count);
}

HolStatus hol_type_vars_in_term(HolKernel *k, HolRef tm, HolRef *buf, size_t cap, size_t *count) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    HolVec v = {0};
    if (hk_type_vars_in_term_exact(k, t, &v) < 0) { hk_vec_free(k, &v); return k->status; }
    return give_vec(k, HOL_KIND_TYPE, &v, buf, cap, count);
}

HolStatus hol_variant(HolKernel *k, const HolRef *avoid, size_t n, HolRef v, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, v);
    if (!t || (n && !avoid)) return status_or(k, HOL_MALFORMED);
    HolTerm **a = (HolTerm **)alloc_pairs(k, n, sizeof(HolTerm *));
    if (!a) return k->status;
    for (size_t i = 0; i < n; i++)
        if (!(a[i] = get_tm(k, avoid[i]))) { hk_dealloc(k, a, (n ? n : 1) * sizeof(HolTerm *)); return k->status; }
    HolTerm *r = hk_variant(k, a, n, t);
    hk_dealloc(k, a, (n ? n : 1) * sizeof(HolTerm *));
    return give(k, HOL_KIND_TERM, r, 0, out);
}

HolStatus hol_vsubst(HolKernel *k, const HolRef *pairs, size_t npairs, HolRef tm, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    HolTmPair *p = tm_pairs(k, pairs, npairs);
    if (!p) return k->status;
    HolTerm *r = hk_vsubst(k, p, npairs, t);
    hk_dealloc(k, p, (npairs ? npairs : 1) * sizeof(HolTmPair));
    return give(k, HOL_KIND_TERM, r, 0, out);
}

HolStatus hol_inst(HolKernel *k, const HolRef *pairs, size_t npairs, HolRef tm, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    HolTyPair *p = ty_pairs(k, pairs, npairs);
    if (!p) return k->status;
    HolTerm *r = hk_inst(k, p, npairs, t);
    hk_dealloc(k, p, (npairs ? npairs : 1) * sizeof(HolTyPair));
    return give(k, HOL_KIND_TERM, r, 0, out);
}

/* ------------------------------------------------------------------------ */
/* Rules                                                                     */
/* ------------------------------------------------------------------------ */

HolStatus hol_REFL(HolKernel *k, HolRef tm, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    void *args[1] = {t};
    return give_rule(k, hk_REFL(k, t), out, HOLR_REFL, NULL, NULL, 0, args, 1);
}

#define BINARY_RULE(api, fn, op)                                                     \
    HolStatus api(HolKernel *k, HolRef th1, HolRef th2, HolRef *out) {               \
        BEGIN(k);                                                                    \
        uint64_t ev[2] = {0, 0};                                                     \
        HolThm *a = (HolThm *)hk_ref_get(k, th1, HOL_KIND_THM, &ev[0]);              \
        HolThm *b = a ? (HolThm *)hk_ref_get(k, th2, HOL_KIND_THM, &ev[1]) : NULL;   \
        if (!b) return k->status;                                                    \
        HolThm *p[2] = {a, b};                                                       \
        return give_rule(k, fn(k, a, b), out, op, ev, p, 2, NULL, 0);                \
    }
BINARY_RULE(hol_TRANS, hk_TRANS, HOLR_TRANS)
BINARY_RULE(hol_MK_COMB, hk_MK_COMB, HOLR_MK_COMB)
BINARY_RULE(hol_EQ_MP, hk_EQ_MP, HOLR_EQ_MP)
BINARY_RULE(hol_DEDUCT_ANTISYM_RULE, hk_DEDUCT_ANTISYM_RULE, HOLR_DEDUCT)

HolStatus hol_ABS(HolKernel *k, HolRef v, HolRef th, HolRef *out) {
    BEGIN(k);
    uint64_t ev = 0;
    HolTerm *x = get_tm(k, v);
    HolThm *t = x ? (HolThm *)hk_ref_get(k, th, HOL_KIND_THM, &ev) : NULL;
    if (!t) return k->status;
    void *args[1] = {x};
    return give_rule(k, hk_ABS(k, x, t), out, HOLR_ABS_RULE, &ev, &t, 1, args, 1);
}

HolStatus hol_BETA(HolKernel *k, HolRef tm, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    void *args[1] = {t};
    return give_rule(k, hk_BETA(k, t), out, HOLR_BETA, NULL, NULL, 0, args, 1);
}

HolStatus hol_ASSUME(HolKernel *k, HolRef tm, HolRef *out) {
    BEGIN(k);
    HolTerm *t = get_tm(k, tm);
    if (!t) return k->status;
    void *args[1] = {t};
    return give_rule(k, hk_ASSUME(k, t), out, HOLR_ASSUME, NULL, NULL, 0, args, 1);
}

HolStatus hol_INST_TYPE(HolKernel *k, const HolRef *pairs, size_t npairs, HolRef th, HolRef *out) {
    BEGIN(k);
    uint64_t ev = 0;
    HolThm *t = (HolThm *)hk_ref_get(k, th, HOL_KIND_THM, &ev);
    if (!t) return k->status;
    HolTyPair *p = ty_pairs(k, pairs, npairs);
    if (!p) return k->status;
    HolThm *r = hk_INST_TYPE(k, p, npairs, t);
    HolStatus s = give_rule(k, r, out, HOLR_INST_TYPE, &ev, &t, 1, (void *const *)p,
                            (uint32_t)(2 * npairs));
    hk_dealloc(k, p, (npairs ? npairs : 1) * sizeof(HolTyPair));
    return s;
}

HolStatus hol_INST(HolKernel *k, const HolRef *pairs, size_t npairs, HolRef th, HolRef *out) {
    BEGIN(k);
    uint64_t ev = 0;
    HolThm *t = (HolThm *)hk_ref_get(k, th, HOL_KIND_THM, &ev);
    if (!t) return k->status;
    HolTmPair *p = tm_pairs(k, pairs, npairs);
    if (!p) return k->status;
    HolThm *r = hk_INST(k, p, npairs, t);
    HolStatus s = give_rule(k, r, out, HOLR_INST, &ev, &t, 1, (void *const *)p,
                            (uint32_t)(2 * npairs));
    hk_dealloc(k, p, (npairs ? npairs : 1) * sizeof(HolTmPair));
    return s;
}

/* ------------------------------------------------------------------------ */
/* Theorem observation                                                       */
/* ------------------------------------------------------------------------ */

HolStatus hol_thm_concl(HolKernel *k, HolRef th, HolRef *out) {
    BEGIN(k);
    HolThm *t = get_th(k, th);
    if (!t) return k->status;
    return give(k, HOL_KIND_TERM, t->concl, 0, out);
}

HolStatus hol_thm_nhyps(HolKernel *k, HolRef th, size_t *out) {
    BEGIN(k);
    HolThm *t = get_th(k, th);
    if (!t || !out) return status_or(k, HOL_MALFORMED);
    *out = t->nhyps;
    return HOL_OK;
}

HolStatus hol_thm_hyp(HolKernel *k, HolRef th, size_t i, HolRef *out) {
    BEGIN(k);
    HolThm *t = get_th(k, th);
    if (!t) return k->status;
    if (i >= t->nhyps) { hk_fail(k, HOL_MALFORMED, HOL_R_BAD_INDEX); return k->status; }
    return give(k, HOL_KIND_TERM, t->hyps[i], 0, out);
}

HolStatus hol_thm_axioms(HolKernel *k, HolRef th, HolRef *buf, size_t cap, size_t *count) {
    BEGIN(k);
    HolThm *t = get_th(k, th);
    if (!t || !count) return status_or(k, HOL_MALFORMED);
    size_t n = t->deps ? t->deps->n : 0;
    for (size_t i = 0; i < n && i < cap && buf; i++) {
        HolDecl *d = t->deps->ax[i];
        HolRef r = hk_ref_new(k, HOL_KIND_THM, d->thm, d->serial);
        if (!r) { /* a failed call leaves no reference behind */
            while (i-- > 0) hk_ref_drop(k, buf[i]);
            return status_or(k, HOL_NOMEM);
        }
        buf[i] = r;
    }
    *count = n;
    return HOL_OK;
}

HolStatus hol_thm_standard(HolKernel *k, HolRef th, bool *out) {
    BEGIN(k);
    HolThm *t = get_th(k, th);
    if (!t || !out) return status_or(k, HOL_MALFORMED);
    *out = hk_thm_is_standard(k, t);
    return HOL_OK;
}

HolStatus hol_thm_event(HolKernel *k, HolRef th, uint64_t *out) {
    BEGIN(k);
    uint64_t ev = 0;
    HolThm *t = (HolThm *)hk_ref_get(k, th, HOL_KIND_THM, &ev);
    if (!t || !out) return status_or(k, HOL_MALFORMED);
    *out = ev;
    return HOL_OK;
}

/* let equals_thm th th' = dest_thm th = dest_thm th'
 * Hash-consed terms are equal iff they are the same object. */
HolStatus hol_thm_equal(HolKernel *k, HolRef a, HolRef b, bool *out) {
    BEGIN(k);
    HolThm *x = get_th(k, a), *y = x ? get_th(k, b) : NULL;
    if (!y || !out) return status_or(k, HOL_MALFORMED);
    bool eq = x->concl == y->concl && x->nhyps == y->nhyps;
    for (uint32_t i = 0; eq && i < x->nhyps; i++) eq = x->hyps[i] == y->hyps[i];
    *out = eq;
    return HOL_OK;
}

HolStatus hol_thm_same_payload(HolKernel *k, HolRef a, HolRef b, bool *out) {
    BEGIN(k);
    HolThm *x = get_th(k, a), *y = x ? get_th(k, b) : NULL;
    if (!y || !out) return status_or(k, HOL_MALFORMED);
    *out = x == y;
    return HOL_OK;
}

static int print_obj(HolKernel *k, HolRef obj, HolStr *s) {
    switch (hk_ref_kind(k, obj)) {
    case HOL_KIND_TYPE: return hk_print_type(k, s, get_ty(k, obj));
    case HOL_KIND_TERM: return hk_print_term(k, s, get_tm(k, obj));
    case HOL_KIND_THM: return hk_print_thm(k, s, get_th(k, obj));
    default: hk_fail(k, HOL_MALFORMED, HOL_R_WRONG_KIND); return -1;
    }
}

HolStatus hol_print(HolKernel *k, HolRef obj, char *buf, size_t cap, size_t *len) {
    BEGIN(k);
    if (!len) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    HolStr s = {0};
    s.k = k;
    if (print_obj(k, obj, &s) < 0) {
        hk_dealloc(k, s.buf, s.cap);
        return status_or(k, HOL_NOMEM);
    }
    *len = s.len;
    if (buf && cap) {
        size_t n = s.len < cap - 1 ? s.len : cap - 1;
        if (s.buf) memcpy(buf, s.buf, n);
        buf[n] = 0;
    }
    hk_dealloc(k, s.buf, s.cap);
    return HOL_OK;
}

HolStatus hol_digest(HolKernel *k, HolRef obj, uint64_t *out) {
    BEGIN(k);
    if (!out) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    HolStr s = {0};
    s.k = k;
    s.hash_only = true;
    s.fnv = HOL_FNV_OFFSET;
    if (print_obj(k, obj, &s) < 0) return k->status;
    *out = s.fnv;
    return HOL_OK;
}

HolStatus hol_parse_type(HolKernel *k, HolRef env, const char *text, size_t len, HolRef *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    if (!text) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    return give(k, HOL_KIND_TYPE, hk_parse_type(k, e, text, len), 0, out);
}

HolStatus hol_parse_term(HolKernel *k, HolRef env, const char *text, size_t len, HolRef *out) {
    BEGIN(k);
    HolDecl *e = get_env(k, env);
    if (!e) return k->status;
    if (!text) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return k->status; }
    return give(k, HOL_KIND_TERM, hk_parse_term(k, e, text, len), 0, out);
}

/* ------------------------------------------------------------------------ */
/* Statistics and hooks                                                      */
/* ------------------------------------------------------------------------ */

void hol_stats(HolKernel *k, HolStats *out) {
    if (!k || !out) return;
    memset(out, 0, sizeof *out);
    out->bytes_live = k->bytes_live + k->memo_bytes;
    out->bytes_peak = k->bytes_peak + k->memo_bytes;
    out->journal_events = k->njev;
    out->journal_bytes = k->capjev * sizeof(*k->jev) + k->journal_arg_bytes;
    out->allocations = k->allocations;
    out->types = k->types.count;
    out->terms = k->terms.count;
    out->thms = k->thms.count;
    out->depsets = k->deps.count;
    out->decls = k->ndecls;
    out->names = k->nnames;
    out->refs = k->live_refs;
    out->gc_runs = k->gc_runs;
    out->intern_hits = k->intern_hits;
    out->intern_misses = k->intern_misses;
    out->events = k->serial;
    out->renames_vsubst = k->renames_vsubst;
    out->renames_inst = k->renames_inst;
}

void hol_set_interrupt(HolKernel *k, volatile int *flag) { if (k) k->interrupt = flag; }
void hol_set_interrupt_poll(HolKernel *k, bool (*poll)(void *), void *context) {
    if (k) { k->interrupt_poll = poll; k->interrupt_context = context; }
}
void hol_set_fuel(HolKernel *k, uint64_t fuel) { if (k) k->fuel_per_call = fuel; }

void hol_test_fail_allocation(HolKernel *k, uint64_t n, bool sticky) {
    if (!k) return;
    k->fail_at = n;
    k->fail_sticky = sticky;
    k->failing = false;
}

uint64_t hol_test_allocations(const HolKernel *k) { return k ? k->allocations : 0; }
