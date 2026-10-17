/* hol_env.c - environments as immutable event histories.
 *
 * fusion.ml keeps four global tables (types, constants, axioms,
 * definitions).  Here every successful declaration, axiom or definition is
 * an event (HolDecl) whose parent is the environment it extends; an
 * environment is the event at its tip.  Lookup by name returns the newest
 * declaration with that name that lies in the environment's history, so an
 * abandoned branch can never lend its meaning to a same-spelled declaration
 * of another branch.  Types and terms point at declaring events, not at
 * names.
 *
 * Atomicity: an extension first creates its events provisionally (placed
 * in a branch so anchors can be computed, but absent from branch tips and
 * name tables), builds every object it returns, and only then commits.  A
 * refusal, an allocation failure or an interruption before the commit
 * leaves no visible trace; the provisional events become garbage. */
#include "hol_internal.h"

#include <stdio.h>

/* ------------------------------------------------------------------------ */
/* Event creation and commit                                                 */
/* ------------------------------------------------------------------------ */

static int decls_reserve(HolKernel *k, size_t extra) {
    if (k->ndecls + extra <= k->capdecls) return 0;
    size_t ncap = k->capdecls ? k->capdecls * 2 : 256;
    while (ncap < k->ndecls + extra) ncap *= 2;
    HolDecl **nd = (HolDecl **)hk_realloc(k, k->decls, k->capdecls * sizeof(HolDecl *),
                                           ncap * sizeof(HolDecl *));
    if (!nd) return -1;
    k->decls = nd;
    k->capdecls = ncap;
    return 0;
}

/* Allocates a provisional event extending parent (which may itself be
 * provisional).  The event is registered for collection but not visible. */
static HolDecl *decl_new(HolKernel *k, HolDecl *parent, int kind, HolName name) {
    if (decls_reserve(k, 1) < 0) return NULL;
    HolDecl *d = (HolDecl *)hk_alloc(k, sizeof(HolDecl));
    if (!d) return NULL;
    memset(d, 0, sizeof *d);
    d->serial = ++k->serial;
    d->kind = (uint8_t)kind;
    d->name = name;
    d->parent = parent;
    d->depth = parent ? parent->depth + 1 : 0;
    d->policy = parent ? parent->policy : HOL_AXIOMS_OPEN;
    if (parent && (!parent->committed || parent->branch->tip == parent)) {
        d->branch = parent->branch;
        d->bindex = parent->bindex + 1;
    } else {
        HolBranch *b = (HolBranch *)hk_alloc(k, sizeof(HolBranch));
        if (!b) {
            hk_dealloc(k, d, sizeof(HolDecl));
            return NULL;
        }
        b->fork = parent;
        b->tip = NULL;
        b->refs = 0;
        d->branch = b;
        d->bindex = 0;
    }
    d->branch->refs++;
    d->index = (uint32_t)k->ndecls;
    k->decls[k->ndecls++] = d;
    return d;
}

/* Makes the provisional chain ending at last visible.  Cannot fail. */
static void decls_commit(HolKernel *k, HolDecl *last) {
    HolDecl *chain[8];
    size_t n = 0;
    for (HolDecl *d = last; d && !d->committed; d = d->parent) chain[n++] = d;
    for (size_t i = n; i-- > 0;) {
        HolDecl *d = chain[i];
        d->committed = 1;
        HolNameRec *nr = &k->names[d->name];
        switch (d->kind) {
        case HOL_DECL_TYPE:
        case HOL_DECL_TYDEF:
            d->next_same = nr->types;
            nr->types = d;
            break;
        case HOL_DECL_CONST:
        case HOL_DECL_DEF:
        case HOL_DECL_TYDEF_ABS:
        case HOL_DECL_TYDEF_REP:
            d->next_same = nr->consts;
            nr->consts = d;
            break;
        default:
            break;
        }
        d->branch->tip = d;
    }
}

/* ------------------------------------------------------------------------ */
/* Digests (cache keys for environments; never evidence)                     */
/* ------------------------------------------------------------------------ */
/* An event's digest chains its parent's digest with its own content.  Types,
 * terms and theorems are digested structurally, each node from its label and
 * its children's digests, memoized per call: shared sub-terms cost once, so
 * a digest is linear in the size of the DAG, not of the printed text.
 * Digests are computed on demand (hol_env_digest) and kept in the event. */

static uint64_t digest_mix(uint64_t h, uint64_t x) { return hk_fnv1a(&x, sizeof x, h); }

static uint64_t digest_name(const HolKernel *k, uint64_t h, HolName n) {
    const HolNameRec *r = &k->names[n];
    return hk_fnv1a(r->bytes, r->len, digest_mix(h, r->len));
}

typedef struct {
    const void **key; /* open addressing, NULL = empty */
    uint64_t *val;
    size_t cap, n;    /* cap is 0 or a power of two */
} DigestMemo;

static size_t dm_slot(const DigestMemo *m, const void *key) {
    size_t j = (size_t)(((uint64_t)(uintptr_t)key >> 4) * 0x9e3779b97f4a7c15ull) & (m->cap - 1);
    while (m->key[j] && m->key[j] != key) j = (j + 1) & (m->cap - 1);
    return j;
}

static void dm_free(HolKernel *k, DigestMemo *m) {
    hk_dealloc(k, m->key, m->cap * sizeof(void *));
    hk_dealloc(k, m->val, m->cap * sizeof(uint64_t));
    memset(m, 0, sizeof *m);
}

static int dm_put(HolKernel *k, DigestMemo *m, const void *key, uint64_t v) {
    if (2 * (m->n + 1) > m->cap) {
        DigestMemo g = {NULL, NULL, m->cap ? 2 * m->cap : 64, 0};
        g.key = (const void **)hk_alloc(k, g.cap * sizeof(void *));
        g.val = g.key ? (uint64_t *)hk_alloc(k, g.cap * sizeof(uint64_t)) : NULL;
        if (!g.val) { dm_free(k, &g); return -1; }
        memset(g.key, 0, g.cap * sizeof(void *));
        for (size_t i = 0; i < m->cap; i++)
            if (m->key[i]) {
                size_t j = dm_slot(&g, m->key[i]);
                g.key[j] = m->key[i];
                g.val[j] = m->val[i];
                g.n++;
            }
        dm_free(k, m);
        *m = g;
    }
    size_t j = dm_slot(m, key);
    if (!m->key[j]) { m->key[j] = key; m->n++; }
    m->val[j] = v;
    return 0;
}

static bool dm_get(const DigestMemo *m, const void *key, uint64_t *out) {
    if (!m->cap) return false;
    size_t j = dm_slot(m, key);
    if (!m->key[j]) return false;
    *out = m->val[j];
    return true;
}

static int ty_digest(HolKernel *k, DigestMemo *m, HolType *t, uint64_t *out) {
    if (dm_get(m, t, out)) return 0;
    if (!hk_tick(k)) return -1;
    uint64_t h = digest_name(k, digest_mix(HOL_FNV_OFFSET, t->kind), t->name);
    h = digest_mix(h, t->nargs);
    for (uint32_t i = 0; i < t->nargs; i++) {
        uint64_t a;
        if (ty_digest(k, m, t->args[i], &a) < 0) return -1;
        h = digest_mix(h, a);
    }
    *out = h;
    return dm_put(k, m, t, h);
}

static int tm_digest(HolKernel *k, DigestMemo *m, HolTerm *t, uint64_t *out) {
    if (dm_get(m, t, out)) return 0;
    if (!hk_tick(k)) return -1;
    uint64_t h = digest_mix(HOL_FNV_OFFSET, 16u + t->kind), a, b;
    if (t->kind == HOL_TM_VAR || t->kind == HOL_TM_CONST) {
        if (ty_digest(k, m, t->type, &a) < 0) return -1;
        h = digest_mix(digest_name(k, h, t->u.leaf.name), a);
    } else {
        if (tm_digest(k, m, t->u.node.l, &a) < 0 || tm_digest(k, m, t->u.node.r, &b) < 0) return -1;
        h = digest_mix(digest_mix(h, a), b);
    }
    *out = h;
    return dm_put(k, m, t, h);
}

static int decl_digest(HolKernel *k, DigestMemo *m, HolDecl *d) {
    uint64_t h = d->parent ? d->parent->digest : HOL_FNV_OFFSET, x;
    h = digest_mix(h, d->kind);
    if (d->name) h = digest_name(k, h, d->name);
    h = digest_mix(digest_mix(h, d->arity), d->policy);
    if (d->gtype) {
        if (ty_digest(k, m, d->gtype, &x) < 0) return -1;
        h = digest_mix(h, x);
    }
    if (d->body) {
        if (tm_digest(k, m, d->body, &x) < 0) return -1;
        h = digest_mix(h, x);
    }
    if (d->witness) {
        h = digest_mix(h, d->witness->nhyps);
        for (uint32_t i = 0; i < d->witness->nhyps; i++) {
            if (tm_digest(k, m, d->witness->hyps[i], &x) < 0) return -1;
            h = digest_mix(h, x);
        }
        if (tm_digest(k, m, d->witness->concl, &x) < 0) return -1;
        h = digest_mix(h, x);
    }
    d->digest = h;
    d->digest_ok = 1;
    return 0;
}

int hk_env_digest(HolKernel *k, HolDecl *env, uint64_t *out) {
    size_t n = 0;
    for (HolDecl *d = env; d && !d->digest_ok; d = d->parent) n++;
    if (n) {
        HolDecl **todo = (HolDecl **)hk_alloc(k, n * sizeof(HolDecl *));
        if (!todo) return -1;
        size_t i = n;
        for (HolDecl *d = env; i > 0; d = d->parent) todo[--i] = d;
        DigestMemo m = {0};
        int r = 0;
        for (i = 0; i < n && r == 0; i++) r = decl_digest(k, &m, todo[i]);
        dm_free(k, &m);
        hk_dealloc(k, todo, n * sizeof(HolDecl *));
        if (r < 0) return -1;
    }
    *out = env->digest;
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Bootstrap                                                                 */
/* ------------------------------------------------------------------------ */

int hk_env_init(HolKernel *k) {
    k->n_fun = hk_name_intern(k, "fun", 3);
    k->n_bool = hk_name_intern(k, "bool", 4);
    k->n_eq = hk_name_intern(k, "=", 1);
    k->n_A = hk_name_intern(k, "A", 1);
    k->n_a = hk_name_intern(k, "a", 1);
    k->n_r = hk_name_intern(k, "r", 1);
    if (!k->n_fun || !k->n_bool || !k->n_eq || !k->n_A || !k->n_a || !k->n_r) return -1;
    /* the_type_constants = ["bool",0; "fun",2]: listing newest first must
     * give bool then fun, so fun is the older event. */
    HolDecl *dfun = decl_new(k, NULL, HOL_DECL_TYPE, k->n_fun);
    if (!dfun) return -1;
    dfun->arity = 2;
    dfun->authority = HOL_AUTH_PRIMITIVE;
    HolDecl *dbool = decl_new(k, dfun, HOL_DECL_TYPE, k->n_bool);
    if (!dbool) return -1;
    dbool->authority = HOL_AUTH_PRIMITIVE;
    k->d_fun = dfun;
    k->d_bool = dbool;
    k->bool_ty = hk_ty_app(k, dbool, NULL, 0);
    k->aty = hk_ty_var(k, k->n_A);
    if (!k->bool_ty || !k->aty) return -1;
    /* the_term_constants = ["=",Tyapp("fun",[aty;Tyapp("fun",[aty;bool_ty])])] */
    HolDecl *deq = decl_new(k, dbool, HOL_DECL_CONST, k->n_eq);
    if (!deq) return -1;
    deq->authority = HOL_AUTH_PRIMITIVE;
    HolType *t2 = hk_ty_fun(k, k->aty, k->bool_ty);
    HolType *t1 = t2 ? hk_ty_fun(k, k->aty, t2) : NULL;
    if (!t1) return -1;
    deq->gtype = t1;
    k->d_eq = deq;
    decls_commit(k, deq);
    return 0;
}

void hk_env_free_all(HolKernel *k) {
    for (size_t i = 0; i < k->ndecls; i++) {
        HolDecl *d = k->decls[i];
        if (--d->branch->refs == 0) hk_dealloc(k, d->branch, sizeof(HolBranch));
        hk_dealloc(k, d, sizeof(HolDecl));
    }
    hk_dealloc(k, k->decls, k->capdecls * sizeof(HolDecl *));
    k->decls = NULL;
    k->ndecls = k->capdecls = 0;
}

/* ------------------------------------------------------------------------ */
/* Lookup                                                                    */
/* ------------------------------------------------------------------------ */

HolDecl *hk_lookup_type(HolKernel *k, HolDecl *env, HolName name) {
    for (HolDecl *d = k->names[name].types; d; d = d->next_same)
        if (hk_decl_le(d, env)) return d;
    return NULL;
}

HolDecl *hk_lookup_const(HolKernel *k, HolDecl *env, HolName name) {
    for (HolDecl *d = k->names[name].consts; d; d = d->next_same)
        if (hk_decl_le(d, env)) return d;
    return NULL;
}

static bool in_env(HolKernel *k, HolDecl *env, HolDecl *anchor) {
    if (hk_decl_le(anchor, env)) return true;
    hk_fail(k, HOL_FOREIGN, HOL_R_INCOMPATIBLE_ENV);
    return false;
}

/* let mk_type(tyop,args) =
 *   let arity = try get_type_arity tyop with Failure _ ->
 *     failwith ("mk_type: type "^tyop^" has not been defined") in
 *   if arity = length args then Tyapp(tyop,args)
 *   else failwith ("mk_type: wrong number of arguments to "^tyop)          */
HolType *hk_mk_type_in(HolKernel *k, HolDecl *env, HolName name,
                        HolType *const *args, uint32_t n) {
    HolDecl *d = hk_lookup_type(k, env, name);
    if (!d) return hk_fail(k, HOL_REFUSED, HOL_R_MK_TYPE_UNDECLARED);
    if (d->arity != n) return hk_fail(k, HOL_REFUSED, HOL_R_MK_TYPE_ARITY);
    for (uint32_t i = 0; i < n; i++)
        if (!in_env(k, env, args[i]->anchor)) return NULL;
    return hk_ty_app(k, d, args, n);
}

/* let mk_const(name,theta) =
 *   let uty = try get_const_type name with Failure _ ->
 *     failwith "mk_const: not a constant name" in
 *   Const(name,type_subst theta uty)                                        */
HolTerm *hk_mk_const_in(HolKernel *k, HolDecl *env, HolName name,
                         const HolTyPair *theta, size_t n) {
    HolDecl *d = hk_lookup_const(k, env, name);
    if (!d) return hk_fail(k, HOL_REFUSED, HOL_R_MK_CONST_UNDECLARED);
    for (size_t i = 0; i < n; i++)
        if (!in_env(k, env, theta[i].repl->anchor) || !in_env(k, env, theta[i].key->anchor))
            return NULL;
    HolTySubst s;
    hk_tysubst_init(&s, theta, n);
    HolType *ty = hk_type_subst(k, &s, d->gtype);
    if (!ty) return NULL;
    return hk_tm_const(k, d, ty);
}

/* One-way matching of a generic type against an instance: succeeds exactly
 * when some theta gives type_subst theta pattern = instance. */
static int ty_match(HolKernel *k, HolType *pat, HolType *inst, HolVec *bind) {
    if (pat->kind == HOL_TY_VAR) {
        for (size_t i = 0; i + 1 < bind->n; i += 2)
            if (bind->v[i] == pat) return bind->v[i + 1] == inst ? 1 : 0;
        if (hk_vec_push(k, bind, pat) < 0 || hk_vec_push(k, bind, inst) < 0) return -1;
        return 1;
    }
    if (inst->kind != HOL_TY_APP || inst->op != pat->op || inst->nargs != pat->nargs) return 0;
    for (uint32_t i = 0; i < pat->nargs; i++) {
        int r = ty_match(k, pat->args[i], inst->args[i], bind);
        if (r <= 0) return r;
    }
    return 1;
}

HolTerm *hk_mk_const_at(HolKernel *k, HolDecl *env, HolName name, HolType *ty) {
    HolDecl *d = hk_lookup_const(k, env, name);
    if (!d) return hk_fail(k, HOL_REFUSED, HOL_R_MK_CONST_UNDECLARED);
    if (!in_env(k, env, ty->anchor)) return NULL;
    HolVec bind = {0};
    int r = ty_match(k, d->gtype, ty, &bind);
    hk_vec_free(k, &bind);
    if (r < 0) return NULL;
    if (r == 0) return hk_fail(k, HOL_REFUSED, HOL_R_CONST_NOT_INSTANCE);
    return hk_tm_const(k, d, ty);
}

/* ------------------------------------------------------------------------ */
/* Declarations                                                              */
/* ------------------------------------------------------------------------ */

/* let new_type(name,arity) =
 *   if can get_type_arity name then
 *     failwith ("new_type: type "^name^" has already been declared")
 *   else the_type_constants := (name,arity)::(!the_type_constants)          */
HolDecl *hk_env_new_type(HolKernel *k, HolDecl *env, HolName name, uint32_t arity) {
    if (hk_lookup_type(k, env, name)) return hk_fail(k, HOL_REFUSED, HOL_R_TYPE_EXISTS);
    HolDecl *d = decl_new(k, env, HOL_DECL_TYPE, name);
    if (!d) return NULL;
    d->arity = arity;
    d->authority = HOL_AUTH_DECLARED;
    decls_commit(k, d);
    return d;
}

/* let new_constant(name,ty) =
 *   if can get_const_type name then
 *     failwith ("new_constant: constant "^name^" has already been declared")
 *   else the_term_constants := (name,ty)::(!the_term_constants)            */
HolDecl *hk_env_new_constant(HolKernel *k, HolDecl *env, HolName name, HolType *ty) {
    if (!in_env(k, env, ty->anchor)) return NULL;
    if (hk_lookup_const(k, env, name)) return hk_fail(k, HOL_REFUSED, HOL_R_CONST_EXISTS);
    HolDecl *d = decl_new(k, env, HOL_DECL_CONST, name);
    if (!d) return NULL;
    d->gtype = ty;
    d->authority = HOL_AUTH_DECLARED;
    decls_commit(k, d);
    return d;
}

HolDecl *hk_env_set_policy(HolKernel *k, HolDecl *env, int policy) {
    if (policy < HOL_AXIOMS_OPEN || policy > HOL_AXIOMS_SEALED)
        return hk_fail(k, HOL_MALFORMED, HOL_R_BAD_INDEX);
    if (policy < env->policy) return hk_fail(k, HOL_POLICY, HOL_R_POLICY_LOOSEN);
    HolDecl *d = decl_new(k, env, HOL_DECL_POLICY, 0);
    if (!d) return NULL;
    d->policy = (uint8_t)policy;
    d->authority = HOL_AUTH_POLICY;
    decls_commit(k, d);
    return d;
}

static int std_recognize_axiom(HolKernel *k, HolDecl *env, HolTerm *tm);

/* let new_axiom tm =
 *   if compare (type_of tm) bool_ty = 0 then
 *     let th = Sequent([],tm) in
 *      (the_axioms := th::(!the_axioms); th)
 *   else failwith "new_axiom: Not a proposition"
 * The axiom theorem depends on its own event, so no theorem derived from
 * it can pass for a theorem of an axiom-free or standard theory. */
HolDecl *hk_env_new_axiom(HolKernel *k, HolDecl *env, HolTerm *tm, HolThm **th_out) {
    if (!in_env(k, env, tm->anchor)) return NULL;
    if (tm->type != k->bool_ty) return hk_fail(k, HOL_REFUSED, HOL_R_AXIOM_NOT_BOOL);
    if (env->policy == HOL_AXIOMS_SEALED) return hk_fail(k, HOL_POLICY, HOL_R_AXIOM_POLICY);
    int std = std_recognize_axiom(k, env, tm);
    if (std < 0) return NULL;
    if (env->policy == HOL_AXIOMS_STANDARD && std == 0)
        return hk_fail(k, HOL_POLICY, HOL_R_AXIOM_POLICY);
    HolDecl *d = decl_new(k, env, HOL_DECL_AXIOM, 0);
    if (!d) return NULL;
    d->body = tm;
    d->authority = std ? (uint8_t)std : HOL_AUTH_USER_AXIOM;
    HolDeps *deps = hk_deps_single(k, d);
    if (!deps) return NULL;
    HolThm *th = hk_thm_make(k, NULL, 0, tm, deps, d);
    if (!th) return NULL;
    d->thm = th;
    decls_commit(k, d);
    *th_out = th;
    return d;
}

static int subset_types(HolKernel *k, HolTerm *r, HolType *ty, bool *out) {
    HolVec a = {0}, b = {0};
    int rc = -1;
    if (hk_type_vars_in_term_exact(k, r, &a) == 0 && hk_tyvars_exact(k, ty, &b) == 0) {
        rc = 0;
        *out = true;
        for (size_t i = 0; i < a.n && *out; i++) {
            bool found = false;
            for (size_t j = 0; j < b.n && !found; j++) found = a.v[i] == b.v[j];
            *out = found;
        }
    }
    hk_vec_free(k, &a);
    hk_vec_free(k, &b);
    return rc;
}

/* let new_basic_definition tm =
 *   match tm with
 *     Comb(Comb(Const("=",_),Var(cname,ty)),r) ->
 *       if not(freesin [] r) then failwith "new_definition: term not closed"
 *       else if not (subset (type_vars_in_term r) (tyvars ty))
 *       then failwith "new_definition: Type variables not reflected in constant"
 *       else let c = new_constant(cname,ty); Const(cname,ty) in
 *            let dth = Sequent([],safe_mk_eq c r) in
 *            the_definitions := dth::(!the_definitions); dth
 *   | Comb(Comb(Const("=",_),Const(cname,ty)),r) ->
 *     failwith ("new_basic_definition: '" ^ cname ^ "' is already defined")
 *   | _ -> failwith "new_basic_definition"                                 */
HolDecl *hk_env_new_definition(HolKernel *k, HolDecl *env, HolTerm *tm, HolThm **th_out) {
    if (!in_env(k, env, tm->anchor)) return NULL;
    if (!hk_tm_is_eq(k, tm)) return hk_fail(k, HOL_REFUSED, HOL_R_DEF_SHAPE);
    HolTerm *lhs = hk_eq_lhs(tm), *rhs = hk_eq_rhs(tm);
    if (lhs->kind == HOL_TM_CONST) return hk_fail(k, HOL_REFUSED, HOL_R_DEF_ALREADY_CONST);
    if (lhs->kind != HOL_TM_VAR) return hk_fail(k, HOL_REFUSED, HOL_R_DEF_SHAPE);
    int closed = hk_freesin(k, NULL, 0, rhs);
    if (closed < 0) return NULL;
    if (!closed) return hk_fail(k, HOL_REFUSED, HOL_R_DEF_NOT_CLOSED);
    bool sub;
    if (subset_types(k, rhs, lhs->type, &sub) < 0) return NULL;
    if (!sub) return hk_fail(k, HOL_REFUSED, HOL_R_DEF_TYVARS);
    if (hk_lookup_const(k, env, lhs->u.leaf.name))
        return hk_fail(k, HOL_REFUSED, HOL_R_CONST_EXISTS);
    HolDecl *d = decl_new(k, env, HOL_DECL_DEF, lhs->u.leaf.name);
    if (!d) return NULL;
    d->gtype = lhs->type;
    d->body = rhs;
    d->authority = HOL_AUTH_DEFINED;
    HolTerm *c = hk_tm_const(k, d, lhs->type);
    HolTerm *eq = c ? hk_tm_eq(k, c, rhs) : NULL;
    HolThm *th = eq ? hk_thm_make(k, NULL, 0, eq, NULL, d) : NULL;
    if (!th) return NULL;
    d->thm = th;
    decls_commit(k, d);
    *th_out = th;
    return d;
}

static int ty_sort_cmp_k(const HolKernel *k, const void *a, const void *b) {
    return hk_ty_compare(k, (const HolType *)a, (const HolType *)b);
}

/* let new_basic_type_definition tyname (absname,repname) (Sequent(asl,c)) =
 *   if exists (can get_const_type) [absname; repname] then
 *     failwith "new_basic_type_definition: Constant(s) already in use" else
 *   if not (asl = []) then
 *     failwith "new_basic_type_definition: Assumptions in theorem" else
 *   let P,x = try dest_comb c
 *             with Failure _ ->
 *               failwith "new_basic_type_definition: Not a combination" in
 *   if not(freesin [] P) then
 *     failwith "new_basic_type_definition: Predicate is not closed" else
 *   let tyvars = sort (<=) (type_vars_in_term P) in
 *   let _ = try new_type(tyname,length tyvars)
 *           with Failure _ ->
 *               failwith "new_basic_type_definition: Type already defined" in
 *   let aty = Tyapp(tyname,tyvars)
 *   and rty = type_of x in
 *   let absty = Tyapp("fun",[rty;aty]) and repty = Tyapp("fun",[aty;rty]) in
 *   let abs = (new_constant(absname,absty); Const(absname,absty))
 *   and rep = (new_constant(repname,repty); Const(repname,repty)) in
 *   let a = Var("a",aty) and r = Var("r",rty) in
 *   Sequent([],safe_mk_eq (Comb(abs,mk_comb(rep,a))) a),
 *   Sequent([],safe_mk_eq (Comb(P,r))
 *                         (safe_mk_eq (mk_comb(rep,mk_comb(abs,r))) r))
 * Deliberate divergence: when absname = repname, fusion.ml has already
 * declared the type and the abstraction constant when the second
 * new_constant fails.  Here the request is refused before any event is
 * committed (HOL_R_TYDEF_SAME_NAMES), after all of fusion.ml's checks. */
HolDecl *hk_env_new_type_definition(HolKernel *k, HolDecl *env, HolName tyname,
                                     HolName absname, HolName repname,
                                     HolThm *witness, HolThm **th1_out, HolThm **th2_out) {
    if (!in_env(k, env, witness->anchor)) return NULL;
    if (hk_lookup_const(k, env, absname) || hk_lookup_const(k, env, repname))
        return hk_fail(k, HOL_REFUSED, HOL_R_TYDEF_CONSTS_IN_USE);
    if (witness->nhyps != 0) return hk_fail(k, HOL_REFUSED, HOL_R_TYDEF_HYPS);
    HolTerm *c = witness->concl;
    if (c->kind != HOL_TM_COMB) return hk_fail(k, HOL_REFUSED, HOL_R_TYDEF_NOT_COMB);
    HolTerm *P = c->u.node.l, *x = c->u.node.r;
    int closed = hk_freesin(k, NULL, 0, P);
    if (closed < 0) return NULL;
    if (!closed) return hk_fail(k, HOL_REFUSED, HOL_R_TYDEF_NOT_CLOSED);
    HolVec tv = {0};
    if (hk_type_vars_in_term_exact(k, P, &tv) < 0) { hk_vec_free(k, &tv); return NULL; }
    /* sort (<=): the elements are distinct type variables, so any correct
     * sort gives the unique ascending order by OCaml compare. */
    for (size_t i = 1; i < tv.n; i++) {
        void *x0 = tv.v[i];
        size_t j = i;
        while (j > 0 && ty_sort_cmp_k(k, tv.v[j - 1], x0) > 0) {
            tv.v[j] = tv.v[j - 1];
            j--;
        }
        tv.v[j] = x0;
    }
    if (hk_lookup_type(k, env, tyname)) {
        hk_vec_free(k, &tv);
        return hk_fail(k, HOL_REFUSED, HOL_R_TYDEF_TYPE_EXISTS);
    }
    if (absname == repname) {
        hk_vec_free(k, &tv);
        return hk_fail(k, HOL_REFUSED, HOL_R_TYDEF_SAME_NAMES);
    }
    if (tv.n > UINT32_MAX) { hk_vec_free(k, &tv); return hk_fail(k, HOL_LIMIT, HOL_R_DEPTH); }
    HolDecl *dt = decl_new(k, env, HOL_DECL_TYDEF, tyname);
    HolDecl *da = dt ? decl_new(k, dt, HOL_DECL_TYDEF_ABS, absname) : NULL;
    HolDecl *dr = da ? decl_new(k, da, HOL_DECL_TYDEF_REP, repname) : NULL;
    if (!dr) { hk_vec_free(k, &tv); return NULL; }
    dt->arity = (uint32_t)tv.n;
    dt->body = P;
    dt->witness = witness;
    da->body = P;
    da->witness = witness;
    dr->body = P;
    dr->witness = witness;
    dt->authority = da->authority = dr->authority = HOL_AUTH_DEFINED;
    HolType *aty = hk_ty_app(k, dt, (HolType *const *)tv.v, (uint32_t)tv.n);
    hk_vec_free(k, &tv);
    if (!aty) return NULL;
    HolType *rty = x->type;
    HolType *absty = hk_ty_fun(k, rty, aty);
    HolType *repty = absty ? hk_ty_fun(k, aty, rty) : NULL;
    if (!repty) return NULL;
    da->gtype = absty;
    dr->gtype = repty;
    HolTerm *abs = hk_tm_const(k, da, absty);
    HolTerm *rep = abs ? hk_tm_const(k, dr, repty) : NULL;
    HolTerm *a = rep ? hk_tm_var(k, k->n_a, aty) : NULL;
    HolTerm *r = a ? hk_tm_var(k, k->n_r, rty) : NULL;
    if (!r) return NULL;
    HolTerm *rep_a = hk_tm_comb(k, rep, a);
    HolTerm *abs_rep_a = rep_a ? hk_tm_comb(k, abs, rep_a) : NULL;
    HolTerm *e1 = abs_rep_a ? hk_tm_eq(k, abs_rep_a, a) : NULL;
    HolTerm *p_r = e1 ? hk_tm_comb(k, P, r) : NULL;
    HolTerm *abs_r = p_r ? hk_tm_comb(k, abs, r) : NULL;
    HolTerm *rep_abs_r = abs_r ? hk_tm_comb(k, rep, abs_r) : NULL;
    HolTerm *inner = rep_abs_r ? hk_tm_eq(k, rep_abs_r, r) : NULL;
    HolTerm *e2 = inner ? hk_tm_eq(k, p_r, inner) : NULL;
    if (!e2) return NULL;
    HolThm *th1 = hk_thm_make(k, NULL, 0, e1, witness->deps, dr);
    HolThm *th2 = th1 ? hk_thm_make(k, NULL, 0, e2, witness->deps, dr) : NULL;
    if (!th2) return NULL;
    dr->thm = th1;
    dr->thm2 = th2;
    decls_commit(k, dr);
    *th1_out = th1;
    *th2_out = th2;
    return dr;
}

/* ------------------------------------------------------------------------ */
/* Standard axiom certification                                              */
/* ------------------------------------------------------------------------ */

/* The standard theory as exported from HOL Light 924faf0c (identical to the
 * definitions in cetta-reference/lib/hol_light/profile.metta), written in
 * the canonical syntax.  An event is standard-certified when it is one of
 * the primitive bootstrap events, the primitive declarations of "@" and
 * "ind", or a basic definition whose name, generic type and right-hand side
 * (up to alpha) match these texts, read in the event's own environment, and
 * whose constants are themselves standard-certified.  An axiom is a
 * certified standard axiom when its statement matches ETA_AX, SELECT_AX or
 * INFINITY_AX the same way.  Certification is a label on the authority of
 * an event; it never admits anything the OPEN policy would not. */
#include "hol_std_data.inc"

typedef struct {
    const char *name;
    const char *type;
    const char *body;
} StdDef;

static const StdDef *std_def_by_name(const HolKernel *k, HolName n) {
    const HolNameRec *r = &k->names[n];
    for (size_t i = 0; i < sizeof hol_std_defs / sizeof hol_std_defs[0]; i++)
        if (strlen(hol_std_defs[i].name) == r->len &&
            memcmp(hol_std_defs[i].name, r->bytes, r->len) == 0)
            return (const StdDef *)&hol_std_defs[i];
    return NULL;
}

static int collect_decls_ty(HolKernel *k, HolType *t, HolVec *out) {
    if (t->kind == HOL_TY_VAR) return 0;
    bool seen = false;
    for (size_t i = 0; i < out->n && !seen; i++) seen = out->v[i] == t->op;
    if (!seen && hk_vec_push(k, out, t->op) < 0) return -1;
    for (uint32_t i = 0; i < t->nargs; i++)
        if (collect_decls_ty(k, t->args[i], out) < 0) return -1;
    return 0;
}

static int collect_decls_tm(HolKernel *k, HolTerm *t, HolVec *out) {
    if (!hk_tick(k)) return -1;
    switch (t->kind) {
    case HOL_TM_VAR: return collect_decls_ty(k, t->type, out);
    case HOL_TM_CONST: {
        bool seen = false;
        for (size_t i = 0; i < out->n && !seen; i++) seen = out->v[i] == t->u.leaf.decl;
        if (!seen && hk_vec_push(k, out, t->u.leaf.decl) < 0) return -1;
        return collect_decls_ty(k, t->type, out);
    }
    default:
        if (collect_decls_tm(k, t->u.node.l, out) < 0) return -1;
        return collect_decls_tm(k, t->u.node.r, out);
    }
}

static int all_standard(HolKernel *k, HolTerm *t) {
    HolVec ds = {0};
    if (collect_decls_tm(k, t, &ds) < 0) { hk_vec_free(k, &ds); return -1; }
    int ok = 1;
    for (size_t i = 0; i < ds.n && ok == 1; i++) ok = hk_decl_standard(k, (HolDecl *)ds.v[i]);
    hk_vec_free(k, &ds);
    return ok;
}

/* Reads a canonical text in env; a refusal (unknown name, ill-typed text)
 * means "does not match", not an error of the caller. */
static int read_matches(HolKernel *k, HolDecl *env, const char *text, HolTerm *actual) {
    HolStatus saved = k->status;
    HolReason saved_r = k->reason;
    HolTerm *expected = hk_parse_term(k, env, text, strlen(text));
    if (!expected) {
        if (k->status == HOL_NOMEM || k->status == HOL_INTERRUPTED || k->status == HOL_LIMIT)
            return -1;
        k->status = saved;
        k->reason = saved_r;
        return 0;
    }
    int c;
    if (hk_alphaorder(k, expected, actual, &c) < 0) return -1;
    return c == 0;
}

int hk_decl_standard(HolKernel *k, HolDecl *d) {
    if (d->std_cert) return d->std_cert == 1;
    int ok = 0;
    switch (d->kind) {
    case HOL_DECL_TYPE: {
        if (d->authority == HOL_AUTH_PRIMITIVE) ok = 1;
        else {
            const HolNameRec *r = &k->names[d->name];
            ok = r->len == 3 && memcmp(r->bytes, "ind", 3) == 0 && d->arity == 0;
        }
        break;
    }
    case HOL_DECL_CONST: {
        if (d->authority == HOL_AUTH_PRIMITIVE) ok = 1;
        else {
            const HolNameRec *r = &k->names[d->name];
            if (r->len == 1 && r->bytes[0] == '@') {
                HolType *expect = hk_parse_type(k, d->parent, hol_std_choice_type,
                                                 strlen(hol_std_choice_type));
                if (!expect) {
                    if (k->status == HOL_NOMEM || k->status == HOL_INTERRUPTED) return -1;
                    hk_clear(k);
                    ok = 0;
                } else ok = expect == d->gtype;
            }
        }
        break;
    }
    case HOL_DECL_DEF: {
        const StdDef *sd = std_def_by_name(k, d->name);
        if (!sd) break;
        HolType *ety = hk_parse_type(k, d->parent, sd->type, strlen(sd->type));
        if (!ety) {
            if (k->status == HOL_NOMEM || k->status == HOL_INTERRUPTED) return -1;
            hk_clear(k);
            break;
        }
        if (ety != d->gtype) break;
        int m = read_matches(k, d->parent, sd->body, d->body);
        if (m < 0) return -1;
        if (!m) break;
        m = all_standard(k, d->body);
        if (m < 0) return -1;
        ok = m;
        break;
    }
    case HOL_DECL_AXIOM:
        ok = d->authority >= HOL_AUTH_STD_ETA && d->authority <= HOL_AUTH_STD_INFINITY;
        break;
    default:
        ok = 0;
    }
    d->std_cert = ok ? 1 : 2;
    return ok;
}

/* 0: not standard; otherwise the HOL_AUTH_STD_* label. */
static int std_recognize_axiom(HolKernel *k, HolDecl *env, HolTerm *tm) {
    static const int labels[3] = {HOL_AUTH_STD_ETA, HOL_AUTH_STD_SELECT, HOL_AUTH_STD_INFINITY};
    for (int i = 0; i < 3; i++) {
        int m = read_matches(k, env, hol_std_axioms[i], tm);
        if (m < 0) return -1;
        if (!m) continue;
        m = all_standard(k, tm);
        if (m < 0) return -1;
        return m ? labels[i] : 0;
    }
    return 0;
}

bool hk_thm_is_standard(HolKernel *k, HolThm *th) {
    (void)k;
    if (!th->deps) return true;
    for (uint32_t i = 0; i < th->deps->n; i++) {
        HolDecl *d = th->deps->ax[i];
        if (!(d->authority >= HOL_AUTH_STD_ETA && d->authority <= HOL_AUTH_STD_INFINITY))
            return false;
    }
    return true;
}
