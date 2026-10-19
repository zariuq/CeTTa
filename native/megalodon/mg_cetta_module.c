#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_cetta_module.h"

#include "eval.h"
#include "library.h"
#include "seal.h"

#include "mg_atom.h"
#include "mg_core.h"
#include "mg_library.h"
#include "mg_profiles.h"
#include "mg_session.h"
#include "mg_worker.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The `megalodon` library: MeTTa access to the native Megalodon kernel.
 *
 * Authority.  Four kinds of opaque values, each a capsule of a brand whose
 * facets never leave this file:
 *
 *   theory     (Sealed megalodon-theory)     use a theory: define, check,
 *                                            prove, publish, sessions
 *   authority  (Sealed megalodon-authority)  extend the selected authority:
 *                                            parameters, axioms, imports,
 *                                            withdrawal, publication policy
 *   theorem    (Sealed megalodon-theorem)    a checked judgment
 *   session    (Sealed megalodon-session)    a retained checking session
 *
 * The MeTTa wrappers are untrusted callers: every native below decodes
 * ordinary data, runs the kernel, and seals only what the kernel accepted.
 * No native calls back into MeTTa, so no equation a program defines can
 * influence what is sealed.  Public terms, contexts, proofs, rows and
 * inspection results are ordinary data in the wire syntax of
 * NativeSyntaxWire / NativeCursorWire.
 *
 * Outcomes.  A value; or a raised error (Error CALL (KIND "reason")) with
 * KIND one of MegalodonRefused, MegalodonMalformed, MegalodonExhausted,
 * MegalodonResource, MegalodonDenied, MegalodonConflict,
 * MegalodonUnfinished; or, when the evaluator itself was cancelled, an
 * interrupted call.  The try- spelling of an operation (__cetta_lib_
 * megalodon_try-prove ...) returns the same Error as its value instead of
 * raising it, so a program can branch on a refusal; cancellation is still
 * an interrupted call.  Session reports are data: a refused row is a
 * report, not an error.
 *
 * Concurrency.  Each theory has one lock; every operation that reads or
 * extends a theory's store holds it.  Deep kernel work runs on a big-stack
 * worker while this thread waits and forwards evaluator cancellation.
 */

#define MG_HEAD_PREFIX "__cetta_lib_megalodon_"

/* ── brands and capsules ──────────────────────────────────────────────── */

static const char g_kind_theory = 't';
static const char g_kind_admin = 'a';
static const char g_kind_theorem = 'p';
static const char g_kind_session = 's';

/* Process-lifetime private roots. Failed creation remains retryable. */
static CettaSealBrandCell g_theory_brand = CETTA_SEAL_BRAND_CELL_INIT("megalodon-theory");
static CettaSealBrandCell g_admin_brand = CETTA_SEAL_BRAND_CELL_INIT("megalodon-authority");
static CettaSealBrandCell g_theorem_brand = CETTA_SEAL_BRAND_CELL_INIT("megalodon-theorem");
static CettaSealBrandCell g_session_brand = CETTA_SEAL_BRAND_CELL_INIT("megalodon-session");
static CettaSealObject *theory_brand(void) { return cetta_seal_brand_get(&g_theory_brand, NULL); }
static CettaSealObject *admin_brand(void) { return cetta_seal_brand_get(&g_admin_brand, NULL); }
static CettaSealObject *theorem_brand(void) { return cetta_seal_brand_get(&g_theorem_brand, NULL); }
static CettaSealObject *session_brand(void) { return cetta_seal_brand_get(&g_session_brand, NULL); }

static bool brands_ready(void) {
    return theory_brand() && admin_brand() && theorem_brand() && session_brand();
}

static void theory_retain_cb(void *r) { mg_theory_retain(r); }
static void theory_release_cb(void *r) { mg_theory_release(r); }
static void thm_retain_cb(void *r) { mg_thm_retain(r); }
static void thm_release_cb(void *r) { mg_thm_release(r); }

/* A session capsule's resource: the kernel session and its borrowed
 * row-space identity.  The space pointer is never kept: each call passes
 * the space again, and its lifetime identity must match. */
typedef struct {
    atomic_size_t holders;
    MgSession *session;
    MgTheory *theory;
    uint64_t space_instance;
    uint64_t space_revision;     /* the revision the current call reads */
    /* Rows prepared by the calling thread for the walk (address order of
     * the schedule); read() looks them up. */
    struct PreparedRow *prepared;
    size_t prepared_len, prepared_cap;
    uint64_t *prepared_keys;     /* open addressing: address + 1 */
    size_t *prepared_index;
    size_t prepared_map_cap;
} SessionBox;

typedef struct PreparedRow {
    uint64_t address;
    MgStatus status;
    const char *reason;
    MgRow row;
    uint64_t *parents;
} PreparedRow;

static void prepared_clear(SessionBox *b) {
    for (size_t i = 0; i < b->prepared_len; ++i)
        mg_sys_free(b->prepared[i].parents);
    b->prepared_len = 0;
    if (b->prepared_keys)
        memset(b->prepared_keys, 0, sizeof(uint64_t) * b->prepared_map_cap);
}

static void session_box_retain(void *r) {
    atomic_fetch_add(&((SessionBox *)r)->holders, 1u);
}

static void session_box_release(void *r) {
    SessionBox *b = r;
    if (!b || atomic_fetch_sub(&b->holders, 1u) != 1u)
        return;
    MgTheory *t = b->theory;
    pthread_mutex_lock(&t->lock);
    mg_session_release(b->session);
    pthread_mutex_unlock(&t->lock);
    prepared_clear(b);
    mg_sys_free(b->prepared);
    mg_sys_free(b->prepared_keys);
    mg_sys_free(b->prepared_index);
    mg_theory_release(t);
    free(b);
}

static Atom *capsule(Arena *a, CettaSealObject *brand, const char *kind,
                     void *resource, void (*retain)(void *),
                     void (*release)(void *)) {
    CettaSealNativePayload payload = {
        .kind = kind, .resource = resource, .retain = retain, .release = release,
    };
    CettaSealObject *box = NULL;
    if (cetta_seal_native(brand, &payload, &box) != CETTA_SEAL_OK)
        return NULL;
    Atom *atom = cetta_seal_object_atom(a, box);
    cetta_seal_release(box);
    return atom;
}

static void *unseal(Atom *atom, CettaSealObject *brand, const char *kind) {
    if (!brands_ready())
        return NULL;
    CettaSealObject *box = cetta_seal_atom_object(atom);
    void *resource = NULL;
    if (!box || cetta_seal_unseal_native(brand, box, kind, &resource) != CETTA_SEAL_OK)
        return NULL;
    return resource;
}

static MgTheory *theory_of(Atom *a) { return unseal(a, theory_brand(), &g_kind_theory); }
static MgTheory *admin_of(Atom *a) { return unseal(a, admin_brand(), &g_kind_admin); }
static MgThm *theorem_of(Atom *a) { return unseal(a, theorem_brand(), &g_kind_theorem); }
static SessionBox *session_of(Atom *a) { return unseal(a, session_brand(), &g_kind_session); }

/* ── call context and outcomes ────────────────────────────────────────── */

typedef struct {
    Arena *a;
    Atom *head;
    Atom **args;
    uint32_t nargs;
    CettaCallOutcome *out;
    /* A try- form: the error is the call's value, not raised. */
    bool soft;
} Call;

static Atom *call_atom(const Call *c) {
    Atom **elems = arena_alloc(c->a, sizeof(Atom *) * (c->nargs + 1u));
    elems[0] = c->head;
    for (uint32_t i = 0; i < c->nargs; ++i)
        elems[i + 1u] = c->args[i];
    return atom_expr(c->a, elems, c->nargs + 1u);
}

static bool raise_kind(const Call *c, const char *kind, const char *reason) {
    Atom *msg = atom_expr2(c->a, atom_symbol(c->a, kind),
                           atom_string(c->a, reason ? reason : ""));
    Atom *error = atom_error(c->a, call_atom(c), msg);
    *c->out = c->soft ? cetta_call_completed_value(error) : cetta_call_raised(error);
    return true;
}

static bool value(const Call *c, Atom *v) {
    if (!v)
        return raise_kind(c, "MegalodonResource", "no-memory");
    *c->out = cetta_call_completed_value(v);
    return true;
}

/* A truth value, as the evaluator's own comparisons answer one. */
static bool truth(const Call *c, bool yes) {
    *c->out = cetta_call_value(atom_bool(c->a, yes));
    return true;
}

static bool outcome(const Call *c, MgStatus st, const char *reason) {
    switch (st) {
    case MG_OK: return raise_kind(c, "MegalodonResource", "internal: no value");
    case MG_REFUSED: return raise_kind(c, "MegalodonRefused", reason ? reason : "refused");
    case MG_MALFORMED: return raise_kind(c, "MegalodonMalformed", reason ? reason : "malformed");
    case MG_EXHAUSTED: return raise_kind(c, "MegalodonExhausted", reason ? reason : "budget");
    case MG_INTERRUPTED:
        if (eval_native_interrupted()) {
            *c->out = cetta_call_interrupted();
            return true;
        }
        return raise_kind(c, "MegalodonInterrupted", reason ? reason : "interrupted");
    case MG_NOMEM: return raise_kind(c, "MegalodonResource", "no-memory");
    case MG_CAPACITY: return raise_kind(c, "MegalodonResource", reason ? reason : "capacity");
    case MG_UNFINISHED: return raise_kind(c, "MegalodonUnfinished", reason ? reason : "unfinished");
    case MG_DENIED: return raise_kind(c, "MegalodonDenied", reason ? reason : "denied");
    case MG_CONFLICT: return raise_kind(c, "MegalodonConflict", reason ? reason : "conflict");
    }
    return raise_kind(c, "MegalodonMalformed", "unknown outcome");
}

static bool invalid(const Call *c, const char *what) {
    return raise_kind(c, "MegalodonMalformed", what);
}

/* An authority argument that is not one: the theory value, or anything
 * else, does not extend the selected authority. */
static bool not_authority(const Call *c) {
    return raise_kind(c, "MegalodonMalformed",
                      "not an authority: only a theory's authority extends what it selects");
}

/* A theorem argument that is not one: printed text, data, or a capsule of
 * another brand. */
static bool not_theorem(const Call *c) {
    return raise_kind(c, "MegalodonMalformed",
                      "not a theorem: only the kernel's checked rules make theorems");
}

static bool text_arg(Atom *atom, char *buffer, size_t cap) {
    size_t len = 0;
    const char *text = mg_atom_text(atom, &len);
    if (!text || len == 0u || len >= cap)
        return false;
    memcpy(buffer, text, len);
    buffer[len] = '\0';
    return true;
}

static uint32_t name_arg(MgStore *s, Atom *atom) {
    size_t len = 0;
    const char *text = mg_atom_text(atom, &len);
    return text && len ? mg_name_intern(s, text, len) : 0u;
}

static bool bool_arg(Atom *atom, bool *out) {
    if (atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_BOOL) {
        *out = atom->ground.bval;
        return true;
    }
    /* HE spells truth True/False, PeTTa true/false */
    if (atom_is_symbol(atom, "True") || atom_is_symbol(atom, "true")) { *out = true; return true; }
    if (atom_is_symbol(atom, "False") || atom_is_symbol(atom, "false")) { *out = false; return true; }
    return false;
}

/* ── deep work on the worker ──────────────────────────────────────────── */

typedef struct Job Job;
struct Job {
    MgDeepCall call;
    MgStatus (*fn)(Job *job);
    MgBudget budget;
    MgStatus status;
    const char *reason;
    void *ctx;
    /* operands and results for the common shapes */
    MgTheory *theory;
    MgWitness witness;
    MgCtx *context;
    MgThm *premises[2];
    uint32_t count;
    MgPf *proof;
    MgTm *term;
    MgTp *type;
    uint32_t name;
    MgThm *thm;
    const MgDecl *decl;
    void *extra;
};

static bool poll_evaluator(void *ignored) {
    (void)ignored;
    return eval_native_interrupted();
}

static void job_trampoline(MgDeepCall *call) {
    Job *job = (Job *)call;
    job->status = job->fn(job);
}

static MgStatus run_job(Job *job, uint64_t work_limit) {
    mg_budget_init(&job->budget, work_limit);
    job->budget.interrupted = mg_deep_call_interrupted;
    job->budget.interrupt_context = &job->call;
    job->call.run = job_trampoline;
    atomic_init(&job->call.interrupt, false);
    if (!mg_deep_call(&job->call, poll_evaluator, NULL)) {
        job->reason = "no checking worker could be started";
        return job->status = MG_CAPACITY;
    }
    return job->status;
}

static void job_init(Job *job, MgTheory *theory, MgStatus (*fn)(Job *)) {
    memset(job, 0, sizeof(*job));
    job->theory = theory;
    job->fn = fn;
}

static Atom *theorem_atom(Arena *a, MgThm *thm) {
    /* the capsule takes its own reference */
    return capsule(a, theorem_brand(), &g_kind_theorem, thm, thm_retain_cb,
                   thm_release_cb);
}

/* Seal a fresh theorem (dropping the caller's reference). */
static bool theorem_value(const Call *c, MgThm *thm) {
    Atom *atom = theorem_atom(c->a, thm);
    mg_thm_release(thm);
    return value(c, atom);
}

/* ── decoding under the theory lock ───────────────────────────────────── */

static bool decode_fail(const Call *c, MgStatus st, const char *what) {
    if (st == MG_MALFORMED || st == MG_OK)
        return raise_kind(c, "MegalodonMalformed", what);
    return outcome(c, st, what);
}

/* ── theories ─────────────────────────────────────────────────────────── */

static bool op_new_theory(const Call *c) {
    char profile[64], label[256];
    if (c->nargs < 1u || c->nargs > 2u || !text_arg(c->args[0], profile, sizeof profile))
        return invalid(c, "(mg:new-theory PROFILE) or (mg:new-theory PROFILE LABEL)");
    MgProfileTag tag;
    if (!mg_profile_from_name(profile, &tag))
        return raise_kind(c, "MegalodonMalformed",
                          "unknown profile (hf, egal, mizar, hoas, setmm, custom)");
    const char *revision = NULL;
    if (c->nargs == 2u) {
        if (!text_arg(c->args[1], label, sizeof label))
            return invalid(c, "theory label must be a string");
        revision = label;
    }
    MgStatus st;
    MgTheory *t = mg_theory_new(tag, revision, &st);
    if (!t)
        return outcome(c, st, "theory creation");
    Atom *use = capsule(c->a, theory_brand(), &g_kind_theory, t, theory_retain_cb,
                        theory_release_cb);
    Atom *admin = capsule(c->a, admin_brand(), &g_kind_admin, t, theory_retain_cb,
                          theory_release_cb);
    mg_theory_release(t);
    if (!use || !admin)
        return outcome(c, MG_NOMEM, NULL);
    return value(c, atom_expr3(c->a, atom_symbol(c->a, "Megalodon.theory"), use, admin));
}

static Atom *tp_list_atom(Arena *a, const char *head, Atom **items, uint32_t n) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (n + 1u));
    elems[0] = atom_symbol(a, head);
    for (uint32_t i = 0; i < n; ++i)
        elems[i + 1u] = items[i];
    return atom_expr(a, elems, n + 1u);
}

static bool op_profile(const Call *c) {
    MgTheory *t = c->nargs == 1u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(Megalodon.profile THEORY)");
    const MgProfileSpec *spec = mg_profile_spec(t->profile);
    pthread_mutex_lock(&t->lock);
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->prim_len; ++i)
        n += t->prims[i] ? 1u : 0u;
    Atom **prims = arena_alloc(c->a, sizeof(Atom *) * (n ? n : 1u));
    uint32_t k = 0;
    for (uint32_t i = 0; i < t->prim_len; ++i) {
        if (!t->prims[i])
            continue;
        Atom *items[4] = {atom_symbol(c->a, "Megalodon.primitive"), atom_int(c->a, i),
                          atom_string(c->a, t->prim_names[i] ? t->prim_names[i] : ""),
                          mg_atom_of_tp(c->a, t->prims[i])};
        prims[k++] = atom_expr(c->a, items, 4u);
    }
    Atom **reserved = arena_alloc(c->a, sizeof(Atom *) * (t->reserved_len ? t->reserved_len : 1u));
    for (uint32_t i = 0; i < t->reserved_len; ++i)
        reserved[i] = atom_string(c->a, t->reserved[i]);
    Atom *items[6] = {
        atom_symbol(c->a, "Megalodon.profile"),
        atom_symbol(c->a, spec ? spec->name : "Custom"),
        atom_expr2(c->a, atom_symbol(c->a, "revision-label"),
                   atom_string(c->a, t->revision_label ? t->revision_label : "")),
        tp_list_atom(c->a, "primitives", prims, n),
        tp_list_atom(c->a, "reserved", reserved, t->reserved_len),
        atom_expr2(c->a, atom_symbol(c->a, "proofgold-theory"),
                   spec && spec->proofgold_theory_id
                       ? atom_string(c->a, spec->proofgold_theory_id)
                       : atom_symbol(c->a, "none")),
    };
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_expr(c->a, items, 6u));
}

static const char *decl_kind_name(const MgDecl *d) {
    switch (d->kind) {
    case MG_DECL_PARAMETER: return "Megalodon.parameter";
    case MG_DECL_DEFINITION: return "Megalodon.definition";
    case MG_DECL_AXIOM: return "Megalodon.axiom";
    case MG_DECL_THEOREM: return "Megalodon.theorem";
    }
    return "Megalodon.declaration";
}

static Atom *decl_atom(Arena *a, const MgTheory *t, const MgDecl *d, bool bodies) {
    const MgName *n = mg_name_get(&t->store, d->name);
    Atom *items[7];
    uint32_t k = 0;
    items[k++] = atom_symbol(a, decl_kind_name(d));
    items[k++] = n ? atom_string_n(a, n->text, n->len) : atom_string(a, "");
    if (d->type)
        items[k++] = mg_atom_of_tp(a, d->type);
    if (d->def && bodies)
        items[k++] = mg_atom_of_tm(a, &t->store, d->def);
    if (d->prop)
        items[k++] = mg_atom_of_tm(a, &t->store, d->prop);
    if (d->source_name)
        items[k++] = atom_expr2(a, atom_symbol(a, "source"), atom_string(a, d->source_name));
    if (d->origin)
        items[k++] = atom_expr3(a, atom_symbol(a, "origin"), atom_string(a, d->origin),
                                atom_int(a, (int64_t)d->line));
    return atom_expr(a, items, k);
}

static bool op_inventory(const Call *c) {
    MgTheory *t = c->nargs == 1u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(Megalodon.inventory THEORY)");
    pthread_mutex_lock(&t->lock);
    MgEnv env = mg_theory_snapshot(t);
    uint64_t live = mg_theory_live_decls(t, env);
    Atom **elems = arena_alloc(c->a, sizeof(Atom *) * (size_t)(live + 3u));
    size_t k = 0;
    elems[k++] = atom_symbol(c->a, "Megalodon.inventory");
    elems[k++] = atom_expr2(c->a, atom_symbol(c->a, "revision"), atom_int(c->a, (int64_t)env.rev));
    for (uint64_t i = 0; i < t->decl_len; ++i)
        if (mg_env_decl_visible(env, t->decls[i]))
            elems[k++] = decl_atom(c->a, t, t->decls[i], true);
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_expr(c->a, elems, (CettaExprLen)k));
}

static bool op_revision(const Call *c) {
    MgTheory *t = c->nargs == 1u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:revision THEORY)");
    pthread_mutex_lock(&t->lock);
    uint64_t rev = t->rev;
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_int(c->a, (int64_t)rev));
}

/* (mg:declaration THEORY NAME): the live declarations named NAME. */
static bool op_declaration(const Call *c) {
    MgTheory *t = c->nargs == 2u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:declaration THEORY NAME)");
    pthread_mutex_lock(&t->lock);
    uint32_t name = name_arg(&t->store, c->args[1]);
    MgEnv env = mg_theory_snapshot(t);
    Atom *found[2];
    uint32_t k = 0;
    for (int ns = 0; ns < 2 && name; ++ns) {
        const MgDecl *d = mg_env_lookup_quiet(env, name, (MgNamespace)ns);
        if (d)
            found[k++] = decl_atom(c->a, t, d, true);
    }
    pthread_mutex_unlock(&t->lock);
    if (!k) {
        *c->out = cetta_call_failure();
        return true;
    }
    return value(c, k == 1u ? found[0]
                            : atom_expr3(c->a, atom_symbol(c->a, "Megalodon.both"), found[0], found[1]));
}

/* (mg:lookup THEORY SOURCE-NAME): live declarations whose provenance
 * names SOURCE-NAME (imports name declarations by content hash). */
static bool op_lookup(const Call *c) {
    MgTheory *t = c->nargs == 2u ? theory_of(c->args[0]) : NULL;
    char source[1024];
    if (!t || !text_arg(c->args[1], source, sizeof source))
        return invalid(c, "(mg:lookup THEORY SOURCE-NAME)");
    pthread_mutex_lock(&t->lock);
    MgEnv env = mg_theory_snapshot(t);
    size_t hits = 0;
    for (uint64_t i = 0; i < t->decl_len; ++i) {
        const MgDecl *d = t->decls[i];
        if (mg_env_decl_visible(env, d) && d->source_name && strcmp(d->source_name, source) == 0)
            ++hits;
    }
    Atom **elems = arena_alloc(c->a, sizeof(Atom *) * (hits + 1u));
    size_t k = 0;
    elems[k++] = atom_symbol(c->a, "Megalodon.found");
    for (uint64_t i = 0; i < t->decl_len; ++i) {
        const MgDecl *d = t->decls[i];
        if (mg_env_decl_visible(env, d) && d->source_name && strcmp(d->source_name, source) == 0)
            elems[k++] = decl_atom(c->a, t, d, false);
    }
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_expr(c->a, elems, (CettaExprLen)k));
}

static bool op_dependencies(const Call *c) {
    MgTheory *t = c->nargs == 2u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(Megalodon.dependencies THEORY NAME)");
    pthread_mutex_lock(&t->lock);
    uint32_t name = name_arg(&t->store, c->args[1]);
    MgEnv env = mg_theory_snapshot(t);
    const MgDecl *d = name ? mg_env_lookup_quiet(env, name, MG_NS_KNOWN) : NULL;
    if (!d && name)
        d = mg_env_lookup_quiet(env, name, MG_NS_TERM);
    if (!d) {
        pthread_mutex_unlock(&t->lock);
        return outcome(c, MG_REFUSED, "no live declaration of that name");
    }
    Atom **elems = arena_alloc(c->a, sizeof(Atom *) * ((size_t)d->deps.len + 2u));
    size_t k = 0;
    elems[k++] = atom_symbol(c->a, "Megalodon.dependencies");
    const MgName *self = mg_name_get(&t->store, d->name);
    elems[k++] = atom_string_n(c->a, self->text, self->len);
    for (uint32_t i = 0; i < d->deps.len; ++i) {
        uint64_t key = d->deps.items[i];
        const MgName *dn = mg_name_get(&t->store, (uint32_t)(key >> 1));
        elems[k++] = atom_expr2(c->a, atom_symbol(c->a, (key & 1u) ? "known" : "term"),
                                dn ? atom_string_n(c->a, dn->text, dn->len)
                                   : atom_string(c->a, ""));
    }
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_expr(c->a, elems, (CettaExprLen)k));
}

/* ── admission ────────────────────────────────────────────────────────── */

/* Item reasons come back through a check's reason; mg_theory_admit runs
 * its own check, so rerun the check for a reason on refusal. */
static const char *admit_reason(MgTheory *t, const MgItem *item) {
    MgBudget b;
    mg_budget_init(&b, 0);
    MgCheck chk;
    mg_check_init(&chk, mg_theory_snapshot(t), &b, NULL);
    mg_item_check(&chk, item);
    return chk.reason;
}

static MgStatus job_admit_reasoned(Job *job) {
    MgItem *item = job->extra;
    MgStatus st = mg_theory_admit(job->theory, &job->budget, item, 0, &job->decl);
    if (st == MG_REFUSED || st == MG_MALFORMED)
        job->reason = admit_reason(job->theory, item);
    return st;
}

static bool admit_locked(const Call *c, MgTheory *t, MgItem *item) {
    Job job;
    job_init(&job, t, job_admit_reasoned);
    job.extra = item;
    MgStatus st = run_job(&job, 0);
    if (st != MG_OK)
        return outcome(c, st, job.reason ? job.reason : mg_status_name(st));
    const MgName *n = mg_name_get(&t->store, item->name);
    return value(c, atom_expr2(c->a, atom_symbol(c->a, "Megalodon.declared"),
                               atom_string_n(c->a, n->text, n->len)));
}

/* (mg:parameter AUTHORITY NAME TYPE): select and admit a parameter. */
static bool op_parameter(const Call *c) {
    if (c->nargs == 3u && !admin_of(c->args[0]))
        return not_authority(c);
    MgTheory *t = c->nargs == 3u ? admin_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:parameter AUTHORITY NAME TYPE)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    uint32_t name = name_arg(&t->store, c->args[1]);
    MgTp *type = name ? mg_atom_tp(&t->store, c->args[2], &st) : NULL;
    bool done;
    if (!name || !type) {
        done = decode_fail(c, name ? st : MG_MALFORMED, "parameter name or type");
    } else {
        st = mg_theory_associate_parameter(t, name, type);
        if (st != MG_OK) {
            done = outcome(c, st, st == MG_CONFLICT
                                      ? "the authority already selects another type"
                                      : "ill-formed parameter type");
        } else {
            MgItem item = {.kind = MG_ITEM_PARAMETER, .name = name, .type = type};
            done = admit_locked(c, t, &item);
        }
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:axiom AUTHORITY NAME PROP): select and admit an axiom. */
static bool op_axiom(const Call *c) {
    if (c->nargs == 3u && !admin_of(c->args[0]))
        return not_authority(c);
    MgTheory *t = c->nargs == 3u ? admin_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:axiom AUTHORITY NAME PROPOSITION)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    uint32_t name = name_arg(&t->store, c->args[1]);
    MgTm *prop = name ? mg_atom_tm(&t->store, c->args[2], &st) : NULL;
    bool done;
    if (!name || !prop) {
        done = decode_fail(c, name ? st : MG_MALFORMED, "axiom name or proposition");
    } else {
        st = mg_theory_associate_known(t, name, prop);
        if (st != MG_OK) {
            done = outcome(c, st, "the authority already selects another proposition");
        } else {
            MgItem item = {.kind = MG_ITEM_KNOWN, .name = name, .prop = prop};
            done = admit_locked(c, t, &item);
        }
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:define THEORY NAME TYPE BODY): a checked definition. */
static bool op_define(const Call *c) {
    MgTheory *t = c->nargs == 4u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:define THEORY NAME TYPE BODY)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK, st2 = MG_OK;
    uint32_t name = name_arg(&t->store, c->args[1]);
    MgTp *type = name ? mg_atom_tp(&t->store, c->args[2], &st) : NULL;
    MgTm *body = type ? mg_atom_tm(&t->store, c->args[3], &st2) : NULL;
    bool done;
    if (!body) {
        done = decode_fail(c, !name ? MG_MALFORMED : !type ? st : st2,
                           "definition name, type or body");
    } else {
        MgItem item = {.kind = MG_ITEM_DEFINITION, .name = name, .type = type, .body = body};
        done = admit_locked(c, t, &item);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static bool op_withdraw(const Call *c) {
    if (c->nargs == 2u && !admin_of(c->args[0]))
        return not_authority(c);
    MgTheory *t = c->nargs == 2u ? admin_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:withdraw AUTHORITY NAME)");
    pthread_mutex_lock(&t->lock);
    uint32_t name = name_arg(&t->store, c->args[1]);
    uint64_t before = mg_theory_live_decls(t, mg_theory_snapshot(t));
    MgStatus st = MG_REFUSED;
    for (int ns = 1; ns >= 0 && name; --ns) {
        MgStatus one = mg_theory_withdraw(t, name, (MgNamespace)ns);
        if (one == MG_OK)
            st = MG_OK;
        else if (one != MG_REFUSED) {
            st = one;
            break;
        }
    }
    uint64_t after = mg_theory_live_decls(t, mg_theory_snapshot(t));
    pthread_mutex_unlock(&t->lock);
    if (st != MG_OK)
        return outcome(c, st, "no live declaration of that name");
    return value(c, atom_expr2(c->a, atom_symbol(c->a, "Megalodon.withdrawn"),
                               atom_int(c->a, (int64_t)(before - after))));
}

static bool op_permit_publication(const Call *c) {
    if (c->nargs == 2u && !admin_of(c->args[0]))
        return not_authority(c);
    MgTheory *t = c->nargs == 2u ? admin_of(c->args[0]) : NULL;
    bool permitted;
    if (!t || !bool_arg(c->args[1], &permitted))
        return invalid(c, "(mg:permit-publication AUTHORITY True|False)");
    pthread_mutex_lock(&t->lock);
    t->publication_permitted = permitted;
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_unit(c->a));
}

/* (mg:set-conversion AUTHORITY reference|lazy): how theorem items are
 * decided.  Both give the reference verdicts; lazy avoids building full
 * normal forms of intermediate propositions. */
static bool op_set_conversion(const Call *c) {
    if (c->nargs == 2u && !admin_of(c->args[0]))
        return not_authority(c);
    MgTheory *t = c->nargs == 2u ? admin_of(c->args[0]) : NULL;
    char mode[32];
    if (!t || !text_arg(c->args[1], mode, sizeof mode) ||
        (strcmp(mode, "reference") != 0 && strcmp(mode, "lazy") != 0))
        return invalid(c, "(mg:set-conversion AUTHORITY reference|lazy)");
    pthread_mutex_lock(&t->lock);
    t->reference_conversion = strcmp(mode, "reference") == 0;
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_unit(c->a));
}

/* ── checking and publication ─────────────────────────────────────────── */

static MgStatus job_check_tree(Job *job) {
    return mg_thm_check_tree(job->theory, job->context, job->proof, &job->budget,
                             &job->reason, &job->thm);
}

/* (mg:check THEORY CONTEXT PROOF [LIMIT]) */
static bool op_check(const Call *c) {
    MgTheory *t = (c->nargs == 3u || c->nargs == 4u) ? theory_of(c->args[0]) : NULL;
    uint64_t limit = 0;
    if (!t || (c->nargs == 4u && !mg_atom_natural(c->args[3], &limit)))
        return invalid(c, "(mg:check THEORY CONTEXT PROOF) or with a work LIMIT");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK, st2 = MG_OK;
    MgCtx *ctx = mg_atom_context(&t->store, c->args[1], &st);
    MgPf *proof = ctx ? mg_atom_pf(&t->store, c->args[2], &st2) : NULL;
    bool done;
    if (!proof) {
        done = decode_fail(c, !ctx ? st : st2, !ctx ? "context" : "proof");
    } else {
        Job job;
        job_init(&job, t, job_check_tree);
        job.context = ctx;
        job.proof = proof;
        st = run_job(&job, limit);
        done = st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_prove(Job *job) {
    MgItem *item = job->extra;
    MgStatus st = mg_theory_admit(job->theory, &job->budget, item, 0, &job->decl);
    if (st == MG_REFUSED || st == MG_MALFORMED)
        job->reason = admit_reason(job->theory, item);
    if (st != MG_OK)
        return st;
    return mg_thm_from_known(job->theory, item->name, &job->reason, &job->thm);
}

/* (mg:prove THEORY NAME CLAIM PROOF): check, admit and publish one closed
 * theorem as a single transaction; the value is the published known. */
static bool op_prove(const Call *c) {
    MgTheory *t = c->nargs == 4u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:prove THEORY NAME CLAIM PROOF)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK, st2 = MG_OK;
    uint32_t name = name_arg(&t->store, c->args[1]);
    MgTm *claim = name ? mg_atom_tm(&t->store, c->args[2], &st) : NULL;
    MgPf *proof = claim ? mg_atom_pf(&t->store, c->args[3], &st2) : NULL;
    bool done;
    if (!proof) {
        done = decode_fail(c, !name ? MG_MALFORMED : !claim ? st : st2,
                           "theorem name, claim or proof");
    } else {
        MgItem item = {.kind = MG_ITEM_THEOREM, .name = name, .prop = claim, .proof = proof};
        Job job;
        job_init(&job, t, job_prove);
        job.extra = &item;
        st = run_job(&job, 0);
        done = st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_publish(Job *job) {
    MgStatus st = mg_thm_publish(job->theory, job->premises[0], job->name, job->term,
                                 &job->budget, &job->reason, &job->decl);
    if (st != MG_OK)
        return st;
    return mg_thm_from_known(job->theory, job->name, &job->reason, &job->thm);
}

/* (mg:publish THEORY THEOREM NAME [CLAIM]) */
static bool op_publish(const Call *c) {
    MgTheory *t = (c->nargs == 3u || c->nargs == 4u) ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:publish THEORY THEOREM NAME) or with a CLAIM");
    MgThm *thm = theorem_of(c->args[1]);
    if (!thm)
        return not_theorem(c);
    if (thm->theory != t)
        return outcome(c, MG_REFUSED, "theorem belongs to another theory");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    uint32_t name = name_arg(&t->store, c->args[2]);
    MgTm *claim = NULL;
    if (name && c->nargs == 4u)
        claim = mg_atom_tm(&t->store, c->args[3], &st);
    bool done;
    if (!name || (c->nargs == 4u && !claim)) {
        done = decode_fail(c, name ? st : MG_MALFORMED, "name or claim");
    } else {
        Job job;
        job_init(&job, t, job_publish);
        job.premises[0] = thm;
        job.name = name;
        job.term = claim;
        st = run_job(&job, 0);
        done = st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_known(Job *job) {
    return mg_thm_from_known(job->theory, job->name, &job->reason, &job->thm);
}

/* (mg:known THEORY NAME): a published known as a theorem value. */
static bool op_known(const Call *c) {
    MgTheory *t = c->nargs == 2u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:known THEORY NAME)");
    pthread_mutex_lock(&t->lock);
    uint32_t name = name_arg(&t->store, c->args[1]);
    bool done;
    if (!name) {
        done = invalid(c, "known name");
    } else {
        Job job;
        job_init(&job, t, job_known);
        job.name = name;
        MgStatus st = run_job(&job, 0);
        done = st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* ── LCF: the eight rules over retained premises ──────────────────────── */

static MgStatus job_step(Job *job) {
    return mg_thm_step(job->theory, &job->witness, job->context, job->premises,
                       job->count, &job->budget, &job->reason, &job->thm);
}

static bool step(const Call *c, MgTheory *t, MgWitness w, MgCtx *ctx,
                 MgThm *p0, MgThm *p1, uint32_t count) {
    Job job;
    job_init(&job, t, job_step);
    job.witness = w;
    job.context = ctx;
    job.premises[0] = p0;
    job.premises[1] = p1;
    job.count = count;
    MgStatus st = run_job(&job, 0);
    return st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
}

/* (mg:hyp THEORY CONTEXT INDEX) */
static bool op_hyp(const Call *c) {
    MgTheory *t = c->nargs == 3u ? theory_of(c->args[0]) : NULL;
    uint64_t index;
    if (!t || !mg_atom_natural(c->args[2], &index) || index > UINT32_MAX)
        return invalid(c, "(mg:hyp THEORY CONTEXT INDEX)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgCtx *ctx = mg_atom_context(&t->store, c->args[1], &st);
    bool done = ctx ? step(c, t, (MgWitness){.rule = MG_RULE_HYP, .index = (uint32_t)index},
                           ctx, NULL, NULL, 0)
                    : decode_fail(c, st, "context");
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:known-in THEORY CONTEXT NAME) */
static bool op_known_in(const Call *c) {
    MgTheory *t = c->nargs == 3u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:known-in THEORY CONTEXT NAME)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgCtx *ctx = mg_atom_context(&t->store, c->args[1], &st);
    uint32_t name = ctx ? name_arg(&t->store, c->args[2]) : 0u;
    bool done = name ? step(c, t, (MgWitness){.rule = MG_RULE_KNOWN, .name = name},
                            ctx, NULL, NULL, 0)
                     : decode_fail(c, ctx ? MG_MALFORMED : st, "context or name");
    pthread_mutex_unlock(&t->lock);
    return done;
}

static bool premise_args(const Call *c, uint32_t first, uint32_t count,
                         MgThm **out, MgTheory **theory) {
    *theory = NULL;
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = first + i < c->nargs ? theorem_of(c->args[first + i]) : NULL;
        if (!out[i])
            return false;
        if (!*theory)
            *theory = out[i]->theory;
    }
    return true;
}

/* (mg:term-app THEOREM TERM): instantiate a term quantifier. */
static bool op_term_app(const Call *c) {
    MgThm *p[1];
    MgTheory *t;
    if (c->nargs != 2u)
        return invalid(c, "(mg:term-app THEOREM TERM)");
    if (!premise_args(c, 0, 1, p, &t))
        return not_theorem(c);
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgTm *arg = mg_atom_tm(&t->store, c->args[1], &st);
    bool done = arg ? step(c, t, (MgWitness){.rule = MG_RULE_TERM_APP, .arg = arg},
                           NULL, p[0], NULL, 1)
                    : decode_fail(c, st, "term argument");
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:proof-app IMPLICATION ANTECEDENT): modus ponens, in this order. */
static bool op_proof_app(const Call *c) {
    MgThm *p[2];
    MgTheory *t;
    if (c->nargs != 2u)
        return invalid(c, "(mg:proof-app THEOREM THEOREM)");
    if (!premise_args(c, 0, 2, p, &t))
        return not_theorem(c);
    pthread_mutex_lock(&t->lock);
    bool done = step(c, t, (MgWitness){.rule = MG_RULE_PROOF_APP}, NULL, p[0], p[1], 2);
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:term-lam THEOREM [TYPE]): generalize the innermost term variable. */
static bool op_term_lam(const Call *c) {
    MgThm *p[1];
    MgTheory *t;
    if ((c->nargs != 1u && c->nargs != 2u))
        return invalid(c, "(mg:term-lam THEOREM) or (mg:term-lam THEOREM TYPE)");
    if (!premise_args(c, 0, 1, p, &t))
        return not_theorem(c);
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgTp *type = c->nargs == 2u ? mg_atom_tp(&t->store, c->args[1], &st) : NULL;
    bool done = (c->nargs == 1u || type)
        ? step(c, t, (MgWitness){.rule = MG_RULE_TERM_LAM, .type = type}, NULL, p[0], NULL, 1)
        : decode_fail(c, st, "binder type");
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:proof-lam THEOREM [SOURCE]): discharge the innermost hypothesis. */
static bool op_proof_lam(const Call *c) {
    MgThm *p[1];
    MgTheory *t;
    if ((c->nargs != 1u && c->nargs != 2u))
        return invalid(c, "(mg:proof-lam THEOREM) or (mg:proof-lam THEOREM SOURCE)");
    if (!premise_args(c, 0, 1, p, &t))
        return not_theorem(c);
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgTm *source = c->nargs == 2u ? mg_atom_tm(&t->store, c->args[1], &st) : NULL;
    bool done = (c->nargs == 1u || source)
        ? step(c, t, (MgWitness){.rule = MG_RULE_PROOF_LAM, .source = source}, NULL, p[0], NULL, 1)
        : decode_fail(c, st, "discharged proposition");
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:type-app THEOREM TYPE): instantiate a type quantifier. */
static bool op_type_app(const Call *c) {
    MgThm *p[1];
    MgTheory *t;
    if (c->nargs != 2u)
        return invalid(c, "(mg:type-app THEOREM TYPE)");
    if (!premise_args(c, 0, 1, p, &t))
        return not_theorem(c);
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgTp *type = mg_atom_tp(&t->store, c->args[1], &st);
    bool done = type ? step(c, t, (MgWitness){.rule = MG_RULE_TYPE_APP, .type = type},
                            NULL, p[0], NULL, 1)
                     : decode_fail(c, st, "type argument");
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:type-lam THEOREM): generalize the innermost type variable. */
static bool op_type_lam(const Call *c) {
    MgThm *p[1];
    MgTheory *t;
    if (c->nargs != 1u)
        return invalid(c, "(mg:type-lam THEOREM)");
    if (!premise_args(c, 0, 1, p, &t))
        return not_theorem(c);
    pthread_mutex_lock(&t->lock);
    bool done = step(c, t, (MgWitness){.rule = MG_RULE_TYPE_LAM}, NULL, p[0], NULL, 1);
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* ── theorem views (read-only data) ───────────────────────────────────── */

static bool op_statement(const Call *c) {
    if (c->nargs != 1u)
        return invalid(c, "(mg:statement THEOREM)");
    MgThm *thm = theorem_of(c->args[0]);
    if (!thm)
        return not_theorem(c);
    MgTheory *t = thm->theory;
    pthread_mutex_lock(&t->lock);
    Atom *j = mg_atom_of_judgment(c->a, &t->store, thm->judgment);
    pthread_mutex_unlock(&t->lock);
    return value(c, j);
}

static bool op_proposition(const Call *c) {
    if (c->nargs != 1u)
        return invalid(c, "(mg:proposition THEOREM)");
    MgThm *thm = theorem_of(c->args[0]);
    if (!thm)
        return not_theorem(c);
    MgTheory *t = thm->theory;
    pthread_mutex_lock(&t->lock);
    Atom *p = mg_atom_of_tm(c->a, &t->store, thm->judgment.prop);
    pthread_mutex_unlock(&t->lock);
    return value(c, p);
}

static bool op_context(const Call *c) {
    if (c->nargs != 1u)
        return invalid(c, "(mg:context THEOREM)");
    MgThm *thm = theorem_of(c->args[0]);
    if (!thm)
        return not_theorem(c);
    MgTheory *t = thm->theory;
    pthread_mutex_lock(&t->lock);
    Atom *p = mg_atom_of_context(c->a, &t->store, thm->judgment.ctx);
    pthread_mutex_unlock(&t->lock);
    return value(c, p);
}

static const char *origin_name(MgThmOrigin o) {
    switch (o) {
    case MG_THM_STEP: return "step";
    case MG_THM_TREE: return "tree";
    case MG_THM_SESSION: return "session";
    case MG_THM_DECLARATION: return "declaration";
    }
    return "unknown";
}

static bool op_theorem_info(const Call *c) {
    if (c->nargs != 1u)
        return invalid(c, "(mg:theorem-info THEOREM)");
    MgThm *thm = theorem_of(c->args[0]);
    if (!thm)
        return not_theorem(c);
    MgTheory *t = thm->theory;
    pthread_mutex_lock(&t->lock);
    bool valid = mg_thm_valid_in(thm, mg_theory_snapshot(t));
    Atom *items[8];
    uint32_t k = 0;
    items[k++] = atom_symbol(c->a, "Megalodon.theorem-info");
    items[k++] = atom_expr2(c->a, atom_symbol(c->a, "profile"),
                            atom_symbol(c->a, mg_profile_name(t->profile)));
    items[k++] = atom_expr2(c->a, atom_symbol(c->a, "theory-label"),
                            atom_string(c->a, t->revision_label ? t->revision_label : ""));
    items[k++] = atom_expr2(c->a, atom_symbol(c->a, "checked-at"), atom_int(c->a, (int64_t)thm->rev));
    items[k++] = atom_expr2(c->a, atom_symbol(c->a, "origin"),
                            atom_symbol(c->a, origin_name(thm->origin)));
    items[k++] = atom_expr2(c->a, atom_symbol(c->a, "valid-now"),
                            valid ? atom_true(c->a) : atom_false(c->a));
    items[k++] = atom_expr2(c->a, atom_symbol(c->a, "closed"),
                            thm->judgment.ctx == t->store.ctx_empty ? atom_true(c->a)
                                                                    : atom_false(c->a));
    if (thm->decl) {
        const MgName *n = mg_name_get(&t->store, thm->decl->name);
        items[k++] = atom_expr3(c->a, atom_symbol(c->a, "declaration"),
                                atom_string_n(c->a, n->text, n->len),
                                atom_symbol(c->a, thm->decl->kind == MG_DECL_AXIOM
                                                      ? "axiom" : "theorem"));
    }
    pthread_mutex_unlock(&t->lock);
    return value(c, atom_expr(c->a, items, k));
}

static bool op_is_theorem(const Call *c) {
    bool yes = c->nargs == 1u && theorem_of(c->args[0]) != NULL;
    return truth(c, yes);
}

static bool op_is_valid(const Call *c) {
    if (c->nargs != 1u)
        return invalid(c, "(mg:is-valid THEOREM)");
    MgThm *thm = theorem_of(c->args[0]);
    if (!thm)
        return not_theorem(c);
    MgTheory *t = thm->theory;
    pthread_mutex_lock(&t->lock);
    bool valid = mg_thm_valid_in(thm, mg_theory_snapshot(t));
    pthread_mutex_unlock(&t->lock);
    return truth(c, valid);
}

/* ── pure syntax services (no authority) ──────────────────────────────── */

static MgStatus job_normalize(Job *job) {
    MgCheck chk;
    mg_check_init(&chk, mg_theory_snapshot(job->theory), &job->budget, NULL);
    MgTm *nf = mg_tm_normalize(&chk, job->term);
    job->reason = chk.reason;
    if (!nf)
        return chk.status == MG_OK ? MG_NOMEM : chk.status;
    job->term = nf;
    return MG_OK;
}

/* (mg:normalize THEORY TERM [LIMIT]): beta-eta-delta normal form. */
static bool op_normalize(const Call *c) {
    MgTheory *t = (c->nargs == 2u || c->nargs == 3u) ? theory_of(c->args[0]) : NULL;
    uint64_t limit = 0;
    if (!t || (c->nargs == 3u && !mg_atom_natural(c->args[2], &limit)))
        return invalid(c, "(mg:normalize THEORY TERM) or with a work LIMIT");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgTm *m = mg_atom_tm(&t->store, c->args[1], &st);
    bool done;
    if (!m) {
        done = decode_fail(c, st, "term");
    } else if (!mg_tm_protected(m)) {
        done = outcome(c, MG_MALFORMED, "term outside the protected fragment");
    } else {
        Job job;
        job_init(&job, t, job_normalize);
        job.term = m;
        st = run_job(&job, limit);
        done = st == MG_OK ? value(c, mg_atom_of_tm(c->a, &t->store, job.term))
                           : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_infer(Job *job) {
    MgCheck chk;
    mg_check_init(&chk, mg_theory_snapshot(job->theory), &job->budget, NULL);
    MgTp *tp = mg_infer(&chk, job->context->depth, job->context->terms, job->term);
    job->reason = chk.reason;
    if (!tp)
        return chk.status == MG_OK ? MG_NOMEM : chk.status;
    job->type = tp;
    return MG_OK;
}

/* (mg:infer THEORY CONTEXT TERM): the type of a term in a context. */
static bool op_infer(const Call *c) {
    MgTheory *t = c->nargs == 3u ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, "(mg:infer THEORY CONTEXT TERM)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK, st2 = MG_OK;
    MgCtx *ctx = mg_atom_context(&t->store, c->args[1], &st);
    MgTm *m = ctx ? mg_atom_tm(&t->store, c->args[2], &st2) : NULL;
    bool done;
    if (!m) {
        done = decode_fail(c, ctx ? st2 : st, ctx ? "term" : "context");
    } else if (!mg_tm_protected(m)) {
        done = outcome(c, MG_MALFORMED, "term outside the protected fragment");
    } else {
        Job job;
        job_init(&job, t, job_infer);
        job.context = ctx;
        job.term = m;
        st = run_job(&job, 0);
        done = st == MG_OK ? value(c, mg_atom_of_tp(c->a, job.type)) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_convertible(Job *job) {
    MgCheck chk;
    mg_check_init(&chk, mg_theory_snapshot(job->theory), &job->budget, NULL);
    MgTm **pair = job->extra;
    MgTp *left = mg_infer(&chk, job->context->depth, job->context->terms, pair[0]);
    MgTp *right = left ? mg_infer(&chk, job->context->depth, job->context->terms, pair[1])
                       : NULL;
    job->reason = chk.reason;
    if (!left || !right)
        return chk.status == MG_OK ? MG_NOMEM : chk.status;
    bool equal = false;
    if (left == right) {
        MgStatus st = mg_conv(&chk, pair[0], pair[1], &equal);
        job->reason = chk.reason;
        if (st != MG_OK)
            return st;
    }
    job->count = equal ? 1u : 0u;
    return MG_OK;
}

/* (mg:convertible THEORY CONTEXT M N [LIMIT]): whether two well-typed terms
 * of CONTEXT are beta-eta-delta convertible (the lazy decision procedure,
 * with the reference verdict).  Terms of different types are not. */
static bool op_convertible(const Call *c) {
    MgTheory *t = (c->nargs == 4u || c->nargs == 5u) ? theory_of(c->args[0]) : NULL;
    uint64_t limit = 0;
    if (!t || (c->nargs == 5u && !mg_atom_natural(c->args[4], &limit)))
        return invalid(c, "(mg:convertible THEORY CONTEXT M N) or with a work LIMIT");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK, st2 = MG_OK, st3 = MG_OK;
    MgCtx *ctx = mg_atom_context(&t->store, c->args[1], &st);
    MgTm *pair[2] = {NULL, NULL};
    pair[0] = ctx ? mg_atom_tm(&t->store, c->args[2], &st2) : NULL;
    pair[1] = pair[0] ? mg_atom_tm(&t->store, c->args[3], &st3) : NULL;
    bool done;
    if (!ctx) {
        done = decode_fail(c, st, "context");
    } else if (!pair[0] || !pair[1]) {
        done = decode_fail(c, pair[0] ? st3 : st2, "term");
    } else if (!mg_tm_protected(pair[0]) || !mg_tm_protected(pair[1])) {
        done = outcome(c, MG_MALFORMED, "term outside the protected fragment");
    } else {
        Job job;
        job_init(&job, t, job_convertible);
        job.context = ctx;
        job.extra = pair;
        st = run_job(&job, limit);
        done = st == MG_OK ? truth(c, job.count != 0u) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_binder(Job *job) {
    MgCheck chk;
    mg_check_init(&chk, mg_theory_snapshot(job->theory), &job->budget, NULL);
    MgStore *s = &job->theory->store;
    MgCtx *ctx = job->context;
    MgCtx *inner = NULL;
    switch ((int)job->count) {
    case 0: { /* term binder */
        MgTpList *terms = mg_tplist_cons(s, job->type, ctx->terms);
        MgTmList *hyps = ctx->hyps ? mg_tmlist_shift1(&chk, ctx->hyps) : NULL;
        if (terms && (!ctx->hyps || hyps))
            inner = mg_ctx(s, ctx->depth, terms, hyps);
        break;
    }
    case 1: { /* proof binder: the proposition in the theory's judgment
               * form (normalized for reference conversion, as stated for
               * lazy conversion, where judgments are in source form) */
        MgTp *tp = mg_infer(&chk, ctx->depth, ctx->terms, job->term);
        if (!tp) {
            job->reason = chk.reason;
            return chk.status == MG_OK ? MG_NOMEM : chk.status;
        }
        if (tp != s->tp_prop) {
            job->reason = "hypothesis is not a proposition";
            return MG_REFUSED;
        }
        MgTm *nf = job->theory->reference_conversion ? mg_tm_normalize(&chk, job->term)
                                                     : job->term;
        MgTmList *hyps = nf ? mg_tmlist_cons(s, nf, ctx->hyps) : NULL;
        if (hyps)
            inner = mg_ctx(s, ctx->depth, ctx->terms, hyps);
        break;
    }
    default: /* type binder */
        if (ctx->depth == UINT32_MAX) {
            job->reason = "type depth";
            return MG_CAPACITY;
        }
        inner = mg_ctx(s, ctx->depth + 1u, NULL, NULL);
        break;
    }
    if (!inner) {
        job->reason = chk.reason;
        return chk.status == MG_OK ? MG_NOMEM : chk.status;
    }
    job->context = inner;
    return MG_OK;
}

/* (mg:term-binder THEORY CONTEXT TYPE), (mg:proof-binder THEORY CONTEXT
 * PROP), (mg:type-binder THEORY CONTEXT): the contexts the rules enter. */
static bool op_binder(const Call *c, int which) {
    uint32_t want = which == 2 ? 2u : 3u;
    MgTheory *t = c->nargs == want ? theory_of(c->args[0]) : NULL;
    if (!t)
        return invalid(c, which == 0 ? "(mg:term-binder THEORY CONTEXT TYPE)"
                          : which == 1 ? "(mg:proof-binder THEORY CONTEXT PROPOSITION)"
                                       : "(mg:type-binder THEORY CONTEXT)");
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK, st2 = MG_OK;
    MgCtx *ctx = mg_atom_context(&t->store, c->args[1], &st);
    Job job;
    job_init(&job, t, job_binder);
    job.context = ctx;
    job.count = (uint32_t)which;
    bool ok = ctx != NULL;
    if (ok && which == 0)
        ok = (job.type = mg_atom_tp(&t->store, c->args[2], &st2)) != NULL;
    if (ok && which == 1)
        ok = (job.term = mg_atom_tm(&t->store, c->args[2], &st2)) != NULL;
    bool done;
    if (!ok) {
        done = decode_fail(c, ctx ? st2 : st, "context or operand");
    } else if (!ctx->safe || (which == 1 && !mg_tm_protected(job.term))) {
        done = outcome(c, MG_MALFORMED, "outside the protected fragment");
    } else {
        st = run_job(&job, 0);
        done = st == MG_OK ? value(c, mg_atom_of_context(c->a, &t->store, job.context))
                           : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* ── sessions over a borrowed row space ───────────────────────────────── */

static SymbolId g_node_symbol;
static pthread_once_t g_node_once = PTHREAD_ONCE_INIT;
static void node_symbol_init(void) {
    g_node_symbol = symbol_intern_cstr(g_symbols, "Megalodon.node");
}

/* The rows of a space: (Megalodon.node ADDRESS ROW). */
static bool node_entry(Atom *atom, uint64_t *address, Atom **row) {
    pthread_once(&g_node_once, node_symbol_init);
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom_symbol_current_id(atom->expr.elems[0]) != g_node_symbol ||
        !mg_atom_natural(atom->expr.elems[1], address))
        return false;
    *row = atom->expr.elems[2];
    return true;
}

static uint64_t box_revision(void *context) {
    return ((SessionBox *)context)->space_revision;
}

static size_t prepared_slot(const SessionBox *b, uint64_t address) {
    uint64_t h = (address + 1u) * UINT64_C(0x9e3779b97f4a7c15);
    h ^= h >> 31;
    return (size_t)h & (b->prepared_map_cap - 1u);
}

static const PreparedRow *prepared_find(const SessionBox *b, uint64_t address) {
    if (!b->prepared_map_cap)
        return NULL;
    for (size_t i = prepared_slot(b, address);; i = (i + 1u) & (b->prepared_map_cap - 1u)) {
        if (!b->prepared_keys[i])
            return NULL;
        if (b->prepared_keys[i] == address + 1u)
            return &b->prepared[b->prepared_index[i]];
    }
}

static MgStatus box_read(void *context, MgTheory *theory, uint64_t address,
                         MgRow *out, const char **reason) {
    (void)theory;
    const PreparedRow *p = prepared_find(context, address);
    if (!p) {
        *reason = "no row at this address";
        return MG_MALFORMED;
    }
    if (p->status != MG_OK) {
        *reason = p->reason;
        return p->status;
    }
    *out = p->row;
    return MG_OK;
}

static void box_release_source(void *context) { (void)context; }

static bool prepared_reserve(SessionBox *b, size_t n) {
    if (n > b->prepared_cap) {
        PreparedRow *grown = mg_sys_realloc(b->prepared, sizeof(PreparedRow) * n);
        if (!grown)
            return false;
        b->prepared = grown;
        b->prepared_cap = n;
    }
    size_t cap = 16u;
    while (cap < n * 2u + 2u)
        cap *= 2u;
    if (cap > b->prepared_map_cap) {
        uint64_t *keys = mg_sys_alloc(sizeof(uint64_t) * cap);
        size_t *index = mg_sys_alloc(sizeof(size_t) * cap);
        if (!keys || !index) {
            mg_sys_free(keys);
            mg_sys_free(index);
            return false;
        }
        mg_sys_free(b->prepared_keys);
        mg_sys_free(b->prepared_index);
        b->prepared_keys = keys;
        b->prepared_index = index;
        b->prepared_map_cap = cap;
    }
    memset(b->prepared_keys, 0, sizeof(uint64_t) * b->prepared_map_cap);
    return true;
}

/* An index of a space's node entries: address -> atom, with duplicates
 * flagged.  Built per call from the space the call passes. */
typedef struct {
    uint64_t *keys;     /* address + 1 */
    Atom **rows;        /* NULL marks two rows at one address */
    size_t cap;
} NodeIndex;

static size_t node_slot(uint64_t address, size_t cap) {
    uint64_t h = (address + 1u) * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 29;
    return (size_t)h & (cap - 1u);
}

static bool node_index_build(NodeIndex *ix, Space *space) {
    memset(ix, 0, sizeof(*ix));
    CettaCount n = space_view_length64(space);
    size_t cap = 16u;
    while (cap < (size_t)n * 2u + 2u)
        cap *= 2u;
    ix->keys = mg_sys_alloc(sizeof(uint64_t) * cap);
    ix->rows = mg_sys_alloc(sizeof(Atom *) * cap);
    if (!ix->keys || !ix->rows) {
        mg_sys_free(ix->keys);
        mg_sys_free(ix->rows);
        return false;
    }
    memset(ix->keys, 0, sizeof(uint64_t) * cap);
    ix->cap = cap;
    for (CettaCount i = 0; i < n; ++i) {
        uint64_t address;
        Atom *row;
        if (!node_entry(space_view_get_at64(space, i), &address, &row))
            continue;
        size_t j = node_slot(address, cap);
        while (ix->keys[j] && ix->keys[j] != address + 1u)
            j = (j + 1u) & (cap - 1u);
        if (ix->keys[j]) {
            ix->rows[j] = NULL;
        } else {
            ix->keys[j] = address + 1u;
            ix->rows[j] = row;
        }
    }
    return true;
}

static bool node_index_find(const NodeIndex *ix, uint64_t address, Atom **row) {
    for (size_t j = node_slot(address, ix->cap);; j = (j + 1u) & (ix->cap - 1u)) {
        if (!ix->keys[j])
            return false;
        if (ix->keys[j] == address + 1u) {
            *row = ix->rows[j];
            return true;
        }
    }
}

static void node_index_free(NodeIndex *ix) {
    mg_sys_free(ix->keys);
    mg_sys_free(ix->rows);
}

static Space *space_arg(Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_SPACE
        ? atom->ground.ptr : NULL;
}

/* Decode the rows of `schedule` (up to `count`) from the space into the
 * box.  A decoding failure is recorded on its row and reported when the
 * walk reaches it, in schedule order. */
static MgStatus prepare_rows(SessionBox *b, MgTheory *t, const NodeIndex *ix,
                             const uint64_t *schedule, size_t count) {
    prepared_clear(b);
    if (!prepared_reserve(b, count))
        return MG_NOMEM;
    for (size_t i = 0; i < count; ++i) {
        uint64_t address = schedule[i];
        if (prepared_find(b, address))
            continue; /* the walk reports the duplicate */
        PreparedRow *p = &b->prepared[b->prepared_len];
        memset(p, 0, sizeof(*p));
        p->address = address;
        Atom *row = NULL;
        if (!node_index_find(ix, address, &row)) {
            p->status = MG_MALFORMED;
            p->reason = "no row at this address";
        } else if (!row) {
            p->status = MG_MALFORMED;
            p->reason = "two rows at one address";
        } else {
            uint32_t cap = 0;
            MgStatus st = MG_OK;
            if (!mg_atom_row(&t->store, row, &p->row, &p->parents, &cap, &st)) {
                p->status = st == MG_OK ? MG_MALFORMED : st;
                p->reason = st == MG_MALFORMED || st == MG_OK
                    ? "not a Megalodon.row in the wire syntax" : mg_status_name(st);
                mg_sys_free(p->parents);
                p->parents = NULL;
            } else {
                p->row.parents = p->parents;
            }
        }
        size_t j = prepared_slot(b, address);
        while (b->prepared_keys[j])
            j = (j + 1u) & (b->prepared_map_cap - 1u);
        b->prepared_keys[j] = address + 1u;
        b->prepared_index[j] = b->prepared_len;
        ++b->prepared_len;
    }
    return MG_OK;
}

static Atom *addresses_atom(Arena *a, const char *head, const uint64_t *xs, uint64_t n) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (size_t)(n + 1u));
    elems[0] = atom_symbol(a, head);
    for (uint64_t i = 0; i < n; ++i)
        elems[i + 1u] = atom_int(a, (int64_t)xs[i]);
    return atom_expr(a, elems, (CettaExprLen)(n + 1u));
}

static Atom *report_atom(Arena *a, const MgSessionReport *r) {
    Atom *items[7];
    uint32_t k = 0;
    items[k++] = atom_symbol(a, "Megalodon.session-report");
    items[k++] = atom_symbol(a, mg_session_status_name(r->status));
    items[k++] = atom_expr2(a, atom_symbol(a, "checked"), atom_int(a, (int64_t)r->checked_rows));
    items[k++] = addresses_atom(a, "pending", r->pending, r->pending_len);
    if (r->status != MG_SESSION_FINISHED && r->status != MG_SESSION_PAUSED &&
        r->status != MG_SESSION_CANCELLED)
        items[k++] = atom_expr3(a, atom_symbol(a, "stopped-at"),
                                atom_int(a, (int64_t)r->failed_address),
                                atom_string(a, r->reason ? r->reason : ""));
    items[k++] = atom_expr2(a, atom_symbol(a, "attempted"), atom_int(a, (int64_t)r->attempted_rows));
    items[k++] = atom_expr2(a, atom_symbol(a, "work"), atom_int(a, (int64_t)r->work_used));
    return atom_expr(a, items, k);
}

/* (mg:session-begin THEORY SPACE) */
/* (mg:session-begin THEORY SPACE [exact|lazy]): exact rows state the
 * reference rule's judgment (NativeCursorWire); lazy rows state a formed
 * judgment convertible to it (the form lazy evidence uses). */
static bool op_session_begin(const Call *c) {
    bool shaped = c->nargs == 2u || c->nargs == 3u;
    MgTheory *t = shaped ? theory_of(c->args[0]) : NULL;
    Space *space = shaped ? space_arg(c->args[1]) : NULL;
    char mode[16] = "exact";
    if (!t || !space ||
        (c->nargs == 3u && (!text_arg(c->args[2], mode, sizeof mode) ||
                            (strcmp(mode, "exact") != 0 && strcmp(mode, "lazy") != 0))))
        return invalid(c, "(mg:session-begin THEORY SPACE) or with exact|lazy");
    SessionBox *b = calloc(1u, sizeof(*b));
    if (!b)
        return outcome(c, MG_NOMEM, NULL);
    atomic_init(&b->holders, 1u);
    mg_theory_retain(t);
    b->theory = t;
    b->space_instance = space_instance_id(space);
    b->space_revision = space_revision(space);
    pthread_mutex_lock(&t->lock);
    MgStatus st;
    b->session = mg_session_begin_mode(t, (MgRowSource){box_revision, box_read,
                                                        box_release_source, b},
                                       strcmp(mode, "lazy") == 0, &st);
    pthread_mutex_unlock(&t->lock);
    if (!b->session) {
        mg_theory_release(t);
        free(b);
        return outcome(c, st, "session");
    }
    Atom *atom = capsule(c->a, session_brand(), &g_kind_session, b, session_box_retain,
                         session_box_release);
    session_box_release(b);
    return value(c, atom);
}

typedef struct {
    SessionBox *box;
    const uint64_t *schedule;
    uint64_t n;
    uint64_t bound;
    MgSessionReport report;
} AdvanceArgs;

static MgStatus job_advance(Job *job) {
    AdvanceArgs *a = job->extra;
    return mg_session_resume(a->box->session, a->schedule, a->n, a->bound,
                             &job->budget, &a->report);
}

static bool schedule_arg(const Call *c, Atom *atom, uint64_t **out, uint64_t *n) {
    if (!atom || atom->kind != ATOM_EXPR)
        return false;
    *n = atom->expr.len;
    *out = arena_alloc(c->a, sizeof(uint64_t) * (size_t)(*n ? *n : 1u));
    for (uint64_t i = 0; i < *n; ++i)
        if (!mg_atom_natural(atom->expr.elems[i], &(*out)[i]))
            return false;
    return true;
}

static bool advance(const Call *c, SessionBox *b, Space *space,
                    const uint64_t *schedule, uint64_t n, uint64_t bound,
                    uint64_t limit) {
    MgTheory *t = b->theory;
    if (space_instance_id(space) != b->space_instance)
        return outcome(c, MG_MALFORMED, "not the session's row space");
    pthread_mutex_lock(&t->lock);
    b->space_revision = space_revision(space);
    NodeIndex ix;
    uint64_t take = bound && bound < n ? bound : n;
    MgStatus st = node_index_build(&ix, space) ? MG_OK : MG_NOMEM;
    if (st == MG_OK) {
        st = prepare_rows(b, t, &ix, schedule, (size_t)take);
        node_index_free(&ix);
    }
    bool done;
    if (st != MG_OK) {
        done = outcome(c, st, "preparing rows");
    } else {
        AdvanceArgs args = {.box = b, .schedule = schedule, .n = n, .bound = bound};
        Job job;
        job_init(&job, t, job_advance);
        job.extra = &args;
        st = run_job(&job, limit);
        if (st == MG_CAPACITY && !args.report.status)
            done = outcome(c, st, job.reason);
        else
            done = value(c, report_atom(c->a, &args.report));
    }
    prepared_clear(b);
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* (mg:session-advance SESSION SPACE (A ...) ROW-BOUND WORK-LIMIT) */
static bool op_session_advance(const Call *c) {
    SessionBox *b = c->nargs == 5u ? session_of(c->args[0]) : NULL;
    Space *space = b ? space_arg(c->args[1]) : NULL;
    uint64_t *schedule = NULL, n = 0, bound = 0, limit = 0;
    if (!b || !space || !schedule_arg(c, c->args[2], &schedule, &n) ||
        !mg_atom_natural(c->args[3], &bound) || !mg_atom_natural(c->args[4], &limit))
        return invalid(c, "(mg:session-advance SESSION SPACE (ADDRESS ...) ROW-BOUND WORK-LIMIT)");
    return advance(c, b, space, schedule, n, bound, limit);
}

/* (mg:session-resume SESSION SPACE ROW-BOUND WORK-LIMIT): its pending
 * suffix. */
static bool op_session_resume(const Call *c) {
    SessionBox *b = c->nargs == 4u ? session_of(c->args[0]) : NULL;
    Space *space = b ? space_arg(c->args[1]) : NULL;
    uint64_t bound = 0, limit = 0;
    if (!b || !space || !mg_atom_natural(c->args[2], &bound) ||
        !mg_atom_natural(c->args[3], &limit))
        return invalid(c, "(mg:session-resume SESSION SPACE ROW-BOUND WORK-LIMIT)");
    MgTheory *t = b->theory;
    pthread_mutex_lock(&t->lock);
    MgSessionReport r;
    mg_session_inspect(b->session, &r);
    uint64_t n = r.pending_len;
    uint64_t *copy = arena_alloc(c->a, sizeof(uint64_t) * (size_t)(n ? n : 1u));
    if (n)
        memcpy(copy, r.pending, sizeof(uint64_t) * (size_t)n);
    pthread_mutex_unlock(&t->lock);
    return advance(c, b, space, copy, n, bound, limit);
}

static bool op_session_inspect(const Call *c) {
    SessionBox *b = c->nargs == 1u ? session_of(c->args[0]) : NULL;
    if (!b)
        return invalid(c, "(mg:session-inspect SESSION)");
    pthread_mutex_lock(&b->theory->lock);
    MgSessionReport r;
    mg_session_inspect(b->session, &r);
    Atom *atom = report_atom(c->a, &r);
    pthread_mutex_unlock(&b->theory->lock);
    return value(c, atom);
}

static bool op_session_is_done(const Call *c) {
    SessionBox *b = c->nargs == 1u ? session_of(c->args[0]) : NULL;
    if (!b)
        return invalid(c, "(mg:session-is-done SESSION)");
    pthread_mutex_lock(&b->theory->lock);
    MgSessionReport r;
    mg_session_inspect(b->session, &r);
    pthread_mutex_unlock(&b->theory->lock);
    bool done = r.status == MG_SESSION_FINISHED && r.pending_len == 0u;
    return truth(c, done);
}

/* (mg:session-judgment SESSION ADDRESS): a checked row's judgment. */
static bool op_session_judgment(const Call *c) {
    SessionBox *b = c->nargs == 2u ? session_of(c->args[0]) : NULL;
    uint64_t address;
    if (!b || !mg_atom_natural(c->args[1], &address))
        return invalid(c, "(mg:session-judgment SESSION ADDRESS)");
    pthread_mutex_lock(&b->theory->lock);
    MgJudgment j;
    Atom *atom = mg_session_judgment(b->session, address, &j)
        ? mg_atom_of_judgment(c->a, &b->theory->store, j) : NULL;
    pthread_mutex_unlock(&b->theory->lock);
    if (!atom) {
        *c->out = cetta_call_failure();
        return true;
    }
    return value(c, atom);
}

/* (mg:session-rebase SESSION SPACE): adopt the space's current revision
 * when every checked row reads unchanged. */
static bool op_session_rebase(const Call *c) {
    SessionBox *b = c->nargs == 2u ? session_of(c->args[0]) : NULL;
    Space *space = b ? space_arg(c->args[1]) : NULL;
    if (!b || !space)
        return invalid(c, "(mg:session-rebase SESSION SPACE)");
    if (space_instance_id(space) != b->space_instance)
        return outcome(c, MG_MALFORMED, "not the session's row space");
    MgTheory *t = b->theory;
    pthread_mutex_lock(&t->lock);
    MgSessionReport r;
    mg_session_inspect(b->session, &r);
    (void)r;
    /* Prepare every checked address. */
    NodeIndex ix;
    MgStatus st = node_index_build(&ix, space) ? MG_OK : MG_NOMEM;
    b->space_revision = space_revision(space);
    if (st == MG_OK) {
        size_t n = 0;
        for (size_t j = 0; j < ix.cap; ++j)
            n += ix.keys[j] ? 1u : 0u;
        uint64_t *all = mg_sys_alloc(sizeof(uint64_t) * (n ? n : 1u));
        if (!all) {
            st = MG_NOMEM;
        } else {
            size_t k = 0;
            for (size_t j = 0; j < ix.cap; ++j)
                if (ix.keys[j])
                    all[k++] = ix.keys[j] - 1u;
            st = prepare_rows(b, t, &ix, all, k);
            mg_sys_free(all);
        }
        node_index_free(&ix);
    }
    if (st == MG_OK)
        st = mg_session_rebase(b->session);
    prepared_clear(b);
    bool done;
    if (st != MG_OK) {
        done = outcome(c, st, st == MG_REFUSED ? "a checked row changed" : NULL);
    } else {
        mg_session_inspect(b->session, &r);
        done = value(c, report_atom(c->a, &r));
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static bool op_session_revalidate(const Call *c) {
    SessionBox *b = c->nargs == 1u ? session_of(c->args[0]) : NULL;
    if (!b)
        return invalid(c, "(mg:session-revalidate SESSION)");
    pthread_mutex_lock(&b->theory->lock);
    MgStatus st = mg_session_revalidate(b->session);
    MgSessionReport r;
    mg_session_inspect(b->session, &r);
    Atom *atom = st == MG_OK ? report_atom(c->a, &r) : NULL;
    pthread_mutex_unlock(&b->theory->lock);
    return st == MG_OK ? value(c, atom)
                       : outcome(c, st, "the checkpoint depends on a changed declaration");
}

static bool op_session_cancel(const Call *c) {
    SessionBox *b = c->nargs == 1u ? session_of(c->args[0]) : NULL;
    if (!b)
        return invalid(c, "(mg:session-cancel SESSION)");
    pthread_mutex_lock(&b->theory->lock);
    mg_session_cancel(b->session);
    pthread_mutex_unlock(&b->theory->lock);
    return value(c, atom_unit(c->a));
}

typedef struct {
    MgSession *session;
    uint64_t root;
    MgTm *claim;
} FinishArgs;

static MgStatus job_finish(Job *job) {
    FinishArgs *f = job->extra;
    return mg_session_finish(f->session, f->root, f->claim, &job->budget,
                             &job->reason, &job->thm);
}

/* (mg:session-finish SESSION ROOT CLAIM) */
static bool op_session_finish(const Call *c) {
    SessionBox *b = c->nargs == 3u ? session_of(c->args[0]) : NULL;
    uint64_t root;
    if (!b || !mg_atom_natural(c->args[1], &root))
        return invalid(c, "(mg:session-finish SESSION ROOT CLAIM)");
    MgTheory *t = b->theory;
    pthread_mutex_lock(&t->lock);
    MgStatus st = MG_OK;
    MgTm *claim = mg_atom_tm(&t->store, c->args[2], &st);
    bool done;
    if (!claim) {
        done = decode_fail(c, st, "claim");
    } else {
        FinishArgs args = {b->session, root, claim};
        Job job;
        job_init(&job, t, job_finish);
        job.extra = &args;
        st = run_job(&job, 0);
        done = st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* ── libraries ────────────────────────────────────────────────────────── */

typedef struct {
    const char *path;
    MgImportPolicy policy;
    MgImportReport report;
} ImportArgs;

static MgStatus job_import(Job *job) {
    ImportArgs *a = job->extra;
    return mg_import_sexprinfo_file(job->theory, a->path, &a->policy, &job->budget,
                                    &a->report);
}

static MgStatus job_load(Job *job) {
    ImportArgs *a = job->extra;
    return mg_library_load_file(job->theory, a->path, &a->policy, &job->budget, &a->report);
}

static Atom *import_report_atom(Arena *a, const MgImportReport *r) {
    Atom *items[16];
    uint32_t k = 0;
    items[k++] = atom_symbol(a, "Megalodon.imported");
    items[k++] = atom_expr2(a, atom_symbol(a, "primitives"), atom_int(a, (int64_t)r->primitives));
    items[k++] = atom_expr2(a, atom_symbol(a, "parameters"), atom_int(a, (int64_t)r->parameters));
    items[k++] = atom_expr2(a, atom_symbol(a, "definitions"), atom_int(a, (int64_t)r->definitions));
    items[k++] = atom_expr2(a, atom_symbol(a, "axioms"), atom_int(a, (int64_t)r->axioms));
    items[k++] = atom_expr2(a, atom_symbol(a, "theorems"), atom_int(a, (int64_t)r->theorems));
    items[k++] = atom_expr2(a, atom_symbol(a, "reproved"), atom_int(a, (int64_t)r->reproved));
    items[k++] = atom_expr2(a, atom_symbol(a, "repeated"), atom_int(a, (int64_t)r->repeated));
    items[k++] = atom_expr2(a, atom_symbol(a, "identities"), atom_int(a, (int64_t)r->identities));
    items[k++] = atom_expr2(a, atom_symbol(a, "realized"), atom_int(a, (int64_t)r->realized));
    /* without authority: never admitted, never known */
    items[k++] = atom_expr2(a, atom_symbol(a, "unproved"), atom_int(a, (int64_t)r->unproved));
    items[k++] = atom_expr2(a, atom_symbol(a, "depends-on-unproved"),
                            atom_int(a, (int64_t)r->dependent));
    items[k++] = atom_expr2(a, atom_symbol(a, "unsupported"), atom_int(a, (int64_t)r->unsupported));
    items[k++] = atom_expr2(a, atom_symbol(a, "revision"), atom_int(a, (int64_t)r->revision));
    if (r->stopped)
        items[k++] = atom_expr3(a, atom_symbol(a, "stopped-at"), atom_int(a, (int64_t)r->line),
                                atom_string(a, r->message));
    return atom_expr(a, items, k);
}

static bool policy_arg(const Call *c, Atom *atom, MgImportPolicy *p) {
    memset(p, 0, sizeof(*p));
    /* (axioms all) | (axioms "name" ...) | (axioms) */
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len < 1u ||
        !atom_is_symbol(atom->expr.elems[0], "axioms"))
        return false;
    if (atom->expr.len == 2u && atom_is_symbol(atom->expr.elems[1], "all")) {
        p->all_axioms = true;
        return true;
    }
    p->axiom_count = atom->expr.len - 1u;
    p->axioms = arena_alloc(c->a, sizeof(char *) * (p->axiom_count ? p->axiom_count : 1u));
    for (uint32_t i = 0; i < p->axiom_count; ++i) {
        size_t len = 0;
        const char *text = mg_atom_text(atom->expr.elems[i + 1u], &len);
        if (!text)
            return false;
        char *copy = arena_alloc(c->a, len + 1u);
        memcpy(copy, text, len);
        copy[len] = '\0';
        p->axioms[i] = copy;
    }
    return true;
}

/* (mg:import AUTHORITY PATH POLICY): a megalodon -sexprinfo export,
 * checked natively. */
static bool op_import(const Call *c, bool library) {
    if (c->nargs == 3u && !admin_of(c->args[0]))
        return not_authority(c);
    MgTheory *t = c->nargs == 3u ? admin_of(c->args[0]) : NULL;
    char path[4096];
    ImportArgs args;
    if (!t || !text_arg(c->args[1], path, sizeof path) || !policy_arg(c, c->args[2], &args.policy))
        return invalid(c, library ? "(mg:load-library AUTHORITY PATH (axioms all|NAME ...))"
                                  : "(mg:import AUTHORITY PATH (axioms all|NAME ...))");
    args.path = path;
    memset(&args.report, 0, sizeof(args.report));
    pthread_mutex_lock(&t->lock);
    Job job;
    job_init(&job, t, library ? job_load : job_import);
    job.extra = &args;
    MgStatus st = run_job(&job, 0);
    bool done;
    if (st == MG_OK) {
        done = value(c, import_report_atom(c->a, &args.report));
    } else {
        /* A stop is the error of its item; the items before it stay
         * admitted, each atomically, and are in the inventory. */
        char reason[640];
        if (args.report.stopped && args.report.line)
            snprintf(reason, sizeof reason, "line %llu: %s",
                     (unsigned long long)args.report.line, args.report.message);
        else
            snprintf(reason, sizeof reason, "%s",
                     args.report.message[0] ? args.report.message
                                            : (job.reason ? job.reason : mg_status_name(st)));
        done = outcome(c, st, reason);
    }
    pthread_mutex_unlock(&t->lock);
    return done;
}

static MgStatus job_save(Job *job) {
    return mg_library_save_file(job->theory, job->extra, &job->reason);
}

/* (mg:save-library THEORY PATH) */
static bool op_save_library(const Call *c) {
    MgTheory *t = c->nargs == 2u ? theory_of(c->args[0]) : NULL;
    char path[4096];
    if (!t || !text_arg(c->args[1], path, sizeof path))
        return invalid(c, "(mg:save-library THEORY PATH)");
    pthread_mutex_lock(&t->lock);
    Job job;
    job_init(&job, t, job_save);
    job.extra = path;
    MgStatus st = run_job(&job, 0);
    pthread_mutex_unlock(&t->lock);
    if (st != MG_OK)
        return outcome(c, st, job.reason);
    return value(c, atom_expr2(c->a, atom_symbol(c->a, "Megalodon.saved"), atom_string(c->a, path)));
}

/* (mg:evidence THEOREM): portable evidence, ordinary data. */
static bool op_evidence(const Call *c) {
    if (c->nargs != 1u)
        return invalid(c, "(mg:evidence THEOREM)");
    MgThm *thm = theorem_of(c->args[0]);
    if (!thm)
        return not_theorem(c);
    MgTheory *t = thm->theory;
    pthread_mutex_lock(&t->lock);
    MgEvidence ev;
    MgStatus st = mg_evidence_of(thm, &ev);
    Atom *atom = NULL;
    if (st == MG_OK) {
        Atom **elems = arena_alloc(c->a, sizeof(Atom *) * ((size_t)ev.row_count + 1u));
        elems[0] = atom_symbol(c->a, "nodes");
        for (uint32_t i = 0; i < ev.row_count; ++i)
            elems[i + 1u] = atom_expr3(c->a, atom_symbol(c->a, "Megalodon.node"),
                                       atom_int(c->a, (int64_t)ev.addresses[i]),
                                       mg_atom_of_row(c->a, &t->store, &ev.rows[i]));
        /* Where it was checked: the selected profile, the theory's
         * revision label and the snapshot revision (display; a recheck
         * requires the same profile, and decides against its own theory). */
        Atom *where[4] = {atom_symbol(c->a, "theory"),
                          atom_expr2(c->a, atom_symbol(c->a, "profile"),
                                     atom_symbol(c->a, mg_profile_name(t->profile))),
                          atom_expr2(c->a, atom_symbol(c->a, "theory-label"),
                                     atom_string(c->a, t->revision_label ? t->revision_label : "")),
                          atom_expr2(c->a, atom_symbol(c->a, "checked-at"),
                                     atom_int(c->a, (int64_t)thm->rev))};
        Atom *items[6] = {atom_symbol(c->a, "Megalodon.evidence"),
                          atom_expr2(c->a, atom_symbol(c->a, "root"),
                                     atom_int(c->a, (int64_t)ev.root)),
                          mg_atom_of_judgment(c->a, &t->store, thm->judgment),
                          atom_expr(c->a, elems, ev.row_count + 1u),
                          atom_expr(c->a, where, 4u),
                          atom_expr2(c->a, atom_symbol(c->a, "conversion"),
                                     atom_symbol(c->a, "lazy"))};
        /* Lazy evidence states source-form judgments and says so; exact
         * evidence is the reference wire form (NativeCursorWire). */
        atom = atom_expr(c->a, items, ev.lazy ? 6u : 5u);
        mg_evidence_free(&ev);
    }
    pthread_mutex_unlock(&t->lock);
    return st == MG_OK ? value(c, atom) : outcome(c, st, "evidence");
}

typedef struct {
    MgEvidence ev;
} CheckEvidenceArgs;

static MgStatus job_check_evidence(Job *job) {
    CheckEvidenceArgs *a = job->extra;
    return mg_evidence_check(job->theory, &a->ev, &job->budget, &job->reason, &job->thm);
}

/* The profile named by an evidence's (theory (profile P) ...) item, or
 * NULL when the item is not of that shape. */
static const char *evidence_profile(Atom *where) {
    if (!where || where->kind != ATOM_EXPR || where->expr.len != 4u ||
        !atom_is_symbol(where->expr.elems[0], "theory"))
        return NULL;
    Atom *p = where->expr.elems[1], *l = where->expr.elems[2], *r = where->expr.elems[3];
    uint64_t rev;
    if (p->kind != ATOM_EXPR || p->expr.len != 2u || !atom_is_symbol(p->expr.elems[0], "profile") ||
        l->kind != ATOM_EXPR || l->expr.len != 2u || !atom_is_symbol(l->expr.elems[0], "theory-label") ||
        r->kind != ATOM_EXPR || r->expr.len != 2u || !atom_is_symbol(r->expr.elems[0], "checked-at") ||
        !mg_atom_natural(r->expr.elems[1], &rev))
        return NULL;
    return atom_name_cstr(p->expr.elems[1]);
}

/* (mg:check-evidence THEORY (Megalodon.evidence (root A) JUDGMENT (nodes
 * (Megalodon.node A ROW) ...) (theory (profile P) (theory-label L)
 * (checked-at R)) [(conversion lazy|exact)])): portable evidence rechecked
 * from scratch against THEORY's live snapshot (by a lazy session when it
 * says so); P must be THEORY's profile and the root must state JUDGMENT. */
static bool op_check_evidence(const Call *c) {
    MgTheory *t = c->nargs == 2u ? theory_of(c->args[0]) : NULL;
    Atom *e = t ? c->args[1] : NULL;
    if (!t || !e || e->kind != ATOM_EXPR || (e->expr.len != 5u && e->expr.len != 6u) ||
        !atom_is_symbol(e->expr.elems[0], "Megalodon.evidence"))
        return invalid(c, "(mg:check-evidence THEORY (Megalodon.evidence (root A) JUDGMENT (nodes ...) (theory ...)))");
    Atom *root = e->expr.elems[1], *nodes = e->expr.elems[3];
    const char *profile = evidence_profile(e->expr.elems[4]);
    Atom *mode = e->expr.len == 6u ? e->expr.elems[5] : NULL;
    uint64_t root_address;
    if (root->kind != ATOM_EXPR || root->expr.len != 2u ||
        !atom_is_symbol(root->expr.elems[0], "root") ||
        !mg_atom_natural(root->expr.elems[1], &root_address) ||
        nodes->kind != ATOM_EXPR || nodes->expr.len < 1u ||
        !atom_is_symbol(nodes->expr.elems[0], "nodes") || !profile ||
        (mode && (mode->kind != ATOM_EXPR || mode->expr.len != 2u ||
                  !atom_is_symbol(mode->expr.elems[0], "conversion") ||
                  !(atom_is_symbol(mode->expr.elems[1], "lazy") ||
                    atom_is_symbol(mode->expr.elems[1], "exact")))))
        return invalid(c, "evidence shape");
    if (strcmp(profile, mg_profile_name(t->profile)) != 0)
        return outcome(c, MG_REFUSED, "evidence of another profile");
    uint32_t n = nodes->expr.len - 1u;
    pthread_mutex_lock(&t->lock);
    CheckEvidenceArgs args;
    memset(&args, 0, sizeof(args));
    args.ev.rows = mg_sys_alloc(sizeof(MgRow) * (n ? n : 1u));
    args.ev.addresses = mg_sys_alloc(sizeof(uint64_t) * (n ? n : 1u));
    uint64_t **parents = mg_sys_alloc(sizeof(uint64_t *) * (n ? n : 1u));
    MgStatus st = args.ev.rows && args.ev.addresses && parents ? MG_OK : MG_NOMEM;
    if (parents)
        memset(parents, 0, sizeof(uint64_t *) * (n ? n : 1u));
    MgJudgment stated;
    if (st == MG_OK && !mg_atom_judgment(&t->store, e->expr.elems[2], &stated, &st) && st == MG_OK)
        st = MG_MALFORMED;
    for (uint32_t i = 0; st == MG_OK && i < n; ++i) {
        Atom *node = nodes->expr.elems[i + 1u];
        uint32_t cap = 0;
        if (node->kind != ATOM_EXPR || node->expr.len != 3u ||
            !atom_is_symbol(node->expr.elems[0], "Megalodon.node") ||
            !mg_atom_natural(node->expr.elems[1], &args.ev.addresses[i])) {
            st = MG_MALFORMED;
            break;
        }
        if (!mg_atom_row(&t->store, node->expr.elems[2], &args.ev.rows[i], &parents[i], &cap, &st))
            st = st == MG_OK ? MG_MALFORMED : st;
    }
    args.ev.row_count = n;
    args.ev.root = root_address;
    args.ev.lazy = mode && atom_is_symbol(mode->expr.elems[1], "lazy");
    bool done;
    if (st != MG_OK) {
        done = decode_fail(c, st, "evidence rows");
    } else {
        Job job;
        job_init(&job, t, job_check_evidence);
        job.extra = &args;
        st = run_job(&job, 0);
        if (st == MG_OK && !mg_judgment_eq(job.thm->judgment, stated)) {
            mg_thm_release(job.thm);
            done = outcome(c, MG_REFUSED, "the evidence root does not state the judgment");
        } else {
            done = st == MG_OK ? theorem_value(c, job.thm) : outcome(c, st, job.reason);
        }
    }
    for (uint32_t i = 0; parents && i < n; ++i)
        mg_sys_free(parents[i]);
    mg_sys_free(parents);
    mg_sys_free(args.ev.rows);
    mg_sys_free(args.ev.addresses);
    pthread_mutex_unlock(&t->lock);
    return done;
}

/* ── dispatch ─────────────────────────────────────────────────────────── */

/* The operation a head names, after the prefix and an optional "try-". */
static const char *head_op(Atom *head, bool *soft) {
    const char *name = atom_name_cstr(head);
    size_t prefix = sizeof(MG_HEAD_PREFIX) - 1u;
    if (!name || strncmp(name, MG_HEAD_PREFIX, prefix) != 0)
        return NULL;
    name += prefix;
    *soft = strncmp(name, "try-", 4) == 0;
    return *soft ? name + 4 : name;
}

typedef struct {
    const char *name;
    bool (*fn)(const Call *c);
} OpEntry;

static bool op_term_binder(const Call *c) { return op_binder(c, 0); }
static bool op_proof_binder(const Call *c) { return op_binder(c, 1); }
static bool op_type_binder(const Call *c) { return op_binder(c, 2); }
static bool op_import_sexpr(const Call *c) { return op_import(c, false); }
static bool op_load_library(const Call *c) { return op_import(c, true); }

static const OpEntry g_ops[] = {
    {"new-theory", op_new_theory},
    {"new-labeled-theory", op_new_theory},
    {"check-bounded", op_check},
    {"publish-claim", op_publish},
    {"term-lam-at", op_term_lam},
    {"proof-lam-of", op_proof_lam},
    {"normalize-bounded", op_normalize},
    {"check-evidence", op_check_evidence},
    {"profile", op_profile},
    {"inventory", op_inventory},
    {"revision", op_revision},
    {"declaration", op_declaration},
    {"lookup", op_lookup},
    {"dependencies", op_dependencies},
    {"parameter", op_parameter},
    {"axiom", op_axiom},
    {"define", op_define},
    {"withdraw", op_withdraw},
    {"permit-publication", op_permit_publication},
    {"set-conversion", op_set_conversion},
    {"check", op_check},
    {"prove", op_prove},
    {"publish", op_publish},
    {"known", op_known},
    {"hyp", op_hyp},
    {"known-in", op_known_in},
    {"term-app", op_term_app},
    {"proof-app", op_proof_app},
    {"term-lam", op_term_lam},
    {"proof-lam", op_proof_lam},
    {"type-app", op_type_app},
    {"type-lam", op_type_lam},
    {"statement", op_statement},
    {"proposition", op_proposition},
    {"context", op_context},
    {"theorem-info", op_theorem_info},
    {"is-theorem", op_is_theorem},
    {"is-valid", op_is_valid},
    {"evidence", op_evidence},
    {"normalize", op_normalize},
    {"infer", op_infer},
    {"convertible", op_convertible},
    {"convertible-bounded", op_convertible},
    {"term-binder", op_term_binder},
    {"proof-binder", op_proof_binder},
    {"type-binder", op_type_binder},
    {"session-begin", op_session_begin},
    {"session-begin-mode", op_session_begin},
    {"session-advance", op_session_advance},
    {"session-resume", op_session_resume},
    {"session-inspect", op_session_inspect},
    {"session-is-done", op_session_is_done},
    {"session-judgment", op_session_judgment},
    {"session-rebase", op_session_rebase},
    {"session-revalidate", op_session_revalidate},
    {"session-cancel", op_session_cancel},
    {"session-finish", op_session_finish},
    {"import", op_import_sexpr},
    {"load-library", op_load_library},
    {"save-library", op_save_library},
};

bool cetta_megalodon_module_call(struct CettaLibraryContext *ctx, Space *space,
                                 Arena *a, Atom *head, Atom **args,
                                 uint32_t nargs, CettaCallOutcome *out) {
    (void)ctx;
    (void)space;
    if (!head || head->kind != ATOM_SYMBOL || !a || !out)
        return false;
    bool soft = false;
    const char *op = head_op(head, &soft);
    if (!op)
        return false;
    for (size_t i = 0; i < sizeof g_ops / sizeof g_ops[0]; ++i) {
        if (strcmp(op, g_ops[i].name) != 0)
            continue;
        Call call = {a, head, args, nargs, out, soft};
        if (!brands_ready())
            return raise_kind(&call, "MegalodonResource", "no-memory");
        return g_ops[i].fn(&call);
    }
    return false;
}
