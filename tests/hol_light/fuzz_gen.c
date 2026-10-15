/* fuzz-gen: generate HOLR probe-mode streams for differential testing.
 *
 *   fuzz-gen SEED RECORDS OUT.hrs
 *
 * The generator runs its own kernel to choose operations that are likely to
 * succeed (and some that must not), and writes each operation as a record.
 * It never decides the verdict: tools/difftest.sh replays the bytes with the
 * native kernel and with the pinned OCaml kernel and compares the reports.
 *
 * Adversarial shapes it aims for: capture under INST (replacements mention
 * binder names), binder clashes under INST_TYPE (same-named variables at
 * different types), identity and duplicate substitution pairs, alpha-variant
 * middles in TRANS / EQ_MP, ABS over a variable free in a hypothesis, BETA
 * redexes with a wrong argument, ill-typed combinations, definitions with
 * free variables or unreflected type variables, type definitions with
 * assumptions or open predicates, and redeclarations.  It avoids the one
 * documented divergence (type definitions with equal abs/rep names). */
#include "hol_internal.h"
#include "hol_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state;
static uint64_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static size_t pick(size_t n) { return n ? (size_t)(rnd() % n) : 0; }
static bool chance(unsigned pct) { return (rnd() % 100) < pct; }

typedef struct { void *p; uint64_t id; } Obj;
typedef struct { Obj *v; size_t n, cap; } Pool;

static void pool_add(Pool *p, void *obj, uint64_t id) {
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->v = (Obj *)realloc(p->v, p->cap * sizeof(Obj));
        if (!p->v) { fprintf(stderr, "oom\n"); exit(3); }
    }
    p->v[p->n].p = obj;
    p->v[p->n].id = id;
    p->n++;
}

static HolKernel *k;
static HolDecl *env;
static HolWriter *w;
static Pool types, terms, thms;
static uint64_t nn, ny, nt, nh;

typedef struct { const char *s; uint64_t id; bool have; } NameId;
static NameId namecache[256];
static size_t nnamecache;

static uint64_t name_id(const char *s) {
    for (size_t i = 0; i < nnamecache; i++)
        if (!strcmp(namecache[i].s, s)) return namecache[i].id;
    hol_writer_op(w, HOLR_NAME);
    size_t len = strlen(s);
    hol_writer_uv(w, len);
    hol_writer_bytes(w, s, len);
    uint64_t id = nn++;
    if (nnamecache < 256) {
        namecache[nnamecache].s = strdup(s);
        namecache[nnamecache].id = id;
        nnamecache++;
    }
    return id;
}

static HolName kname(const char *s) { return hk_name_intern(k, s, strlen(s)); }

static const char *var_names[] = {"x", "y", "z", "x'", "y'", "f", "g", "p", "q", "a", "r", "t"};
static const char *tyvar_names[] = {"A", "B", "C", "D"};
static char user_types[64][16];
static uint32_t user_arity[64];
static size_t nuser_types;
static char user_consts[64][16];
static size_t nuser_consts;
static unsigned fresh_counter;
/* D1: an identity pair (x,x) followed by another pair for x (docs/CONTRACT.md).
 * Excluded unless --d1 is given, so that exact campaigns compare exactly. */
static bool allow_d1;

/* ---- emission of existing objects (definitions on first use) ---------- */

typedef struct { const void *p; uint64_t id; } MapE;
static MapE *tymap, *tmmap;
static size_t tymap_n, tymap_cap, tmmap_n, tmmap_cap;

static bool map_get(MapE *m, size_t n, const void *p, uint64_t *id) {
    for (size_t i = n; i-- > 0;)
        if (m[i].p == p) { *id = m[i].id; return true; }
    return false;
}
static void map_put(MapE **m, size_t *n, size_t *cap, const void *p, uint64_t id) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 256;
        *m = (MapE *)realloc(*m, *cap * sizeof(MapE));
    }
    (*m)[*n].p = p;
    (*m)[*n].id = id;
    (*n)++;
}

static uint64_t emit_type(HolType *t) {
    uint64_t id;
    if (map_get(tymap, tymap_n, t, &id)) return id;
    if (t->kind == HOL_TY_VAR) {
        uint64_t n = name_id(k->names[t->name].bytes);
        hol_writer_op(w, HOLR_TYVAR);
        hol_writer_uv(w, n);
    } else {
        uint64_t ids[16];
        for (uint32_t i = 0; i < t->nargs && i < 16; i++) ids[i] = emit_type(t->args[i]);
        uint64_t n = name_id(k->names[t->name].bytes);
        hol_writer_op(w, HOLR_TYAPP);
        hol_writer_uv(w, n);
        hol_writer_uv(w, t->nargs);
        for (uint32_t i = 0; i < t->nargs; i++) hol_writer_uv(w, ids[i]);
    }
    id = ny++;
    map_put(&tymap, &tymap_n, &tymap_cap, t, id);
    return id;
}

static uint64_t emit_term(HolTerm *t) {
    uint64_t id;
    if (map_get(tmmap, tmmap_n, t, &id)) return id;
    if (t->kind == HOL_TM_VAR || t->kind == HOL_TM_CONST) {
        uint64_t ty = emit_type(t->type);
        uint64_t n = name_id(k->names[t->u.leaf.name].bytes);
        hol_writer_op(w, t->kind == HOL_TM_VAR ? HOLR_VAR : HOLR_CONST);
        hol_writer_uv(w, n);
        hol_writer_uv(w, ty);
    } else {
        uint64_t a = emit_term(t->u.node.l), b = emit_term(t->u.node.r);
        hol_writer_op(w, t->kind == HOL_TM_COMB ? HOLR_COMB : HOLR_ABS);
        hol_writer_uv(w, a);
        hol_writer_uv(w, b);
    }
    id = nt++;
    map_put(&tmmap, &tmmap_n, &tmmap_cap, t, id);
    return id;
}

/* ---- random syntax ---------------------------------------------------- */

static HolType *rand_type(int depth) {
    unsigned c = (unsigned)pick(100);
    if (depth <= 0 || c < 35) return hk_ty_var(k, kname(tyvar_names[pick(depth > 1 ? 4 : 2)]));
    if (c < 55) return k->bool_ty;
    if (c < 85 || nuser_types == 0) {
        HolType *a = rand_type(depth - 1), *b = rand_type(depth - 1);
        return a && b ? hk_ty_fun(k, a, b) : NULL;
    }
    size_t i = pick(nuser_types);
    HolType *args[2];
    for (uint32_t j = 0; j < user_arity[i]; j++) args[j] = rand_type(depth - 1);
    return hk_mk_type_in(k, env, kname(user_types[i]), args, user_arity[i]);
}

static void note_type(HolType *t) {
    if (t) pool_add(&types, t, emit_type(t));
}

static HolTerm *pick_term_of_type(HolType *ty) {
    size_t start = pick(terms.n ? terms.n : 1);
    for (size_t i = 0; i < terms.n; i++) {
        HolTerm *t = (HolTerm *)terms.v[(start + i) % terms.n].p;
        if (t->type == ty) return t;
    }
    return NULL;
}

static HolTerm *rand_var(HolType *ty) {
    return hk_tm_var(k, kname(var_names[pick(sizeof var_names / sizeof var_names[0])]), ty);
}

static HolTerm *rand_leaf(void) {
    unsigned c = (unsigned)pick(100);
    if (c < 60 || (nuser_consts == 0 && c < 85)) {
        HolType *ty = types.n && chance(70) ? (HolType *)types.v[pick(types.n)].p : rand_type(2);
        return ty ? rand_var(ty) : NULL;
    }
    if (c < 85) { /* equality at a random type */
        HolType *ty = rand_type(2);
        HolTyPair p = {ty, k->aty};
        return ty ? hk_mk_const_in(k, env, k->n_eq, &p, 1) : NULL;
    }
    size_t i = pick(nuser_consts);
    HolDecl *d = hk_lookup_const(k, env, kname(user_consts[i]));
    if (!d) return NULL;
    HolTyPair p[2] = {{rand_type(1), hk_ty_var(k, kname("A"))}, {rand_type(1), hk_ty_var(k, kname("B"))}};
    if (!p[0].repl || !p[1].repl) return NULL;
    return hk_mk_const_in(k, env, d->name, p, 2);
}

/* Builds a term by kernel calls only, so it is well formed by construction. */
static HolTerm *rand_term(int depth) {
    if (depth <= 0 || chance(30)) {
        if (terms.n && chance(50)) return (HolTerm *)terms.v[pick(terms.n)].p;
        return rand_leaf();
    }
    unsigned c = (unsigned)pick(100);
    if (c < 45) {
        HolTerm *f = rand_term(depth - 1);
        if (!f) return NULL;
        if (hk_ty_is_fun(k, f->type)) {
            HolTerm *x = pick_term_of_type(f->type->args[0]);
            if (!x || chance(40)) x = rand_var(f->type->args[0]);
            return x ? hk_tm_comb(k, f, x) : NULL;
        }
        return NULL;
    }
    if (c < 80) {
        HolTerm *body = rand_term(depth - 1);
        HolTerm *v = NULL;
        if (body && chance(60)) { /* bind a variable that occurs in the body */
            HolVec fr = {0};
            if (hk_frees_exact(k, body, &fr) == 0 && fr.n) v = (HolTerm *)fr.v[pick(fr.n)];
            hk_vec_free(k, &fr);
        }
        if (!v) v = rand_var(chance(50) ? k->bool_ty : rand_type(1));
        return body && v ? hk_tm_abs(k, v, body) : NULL;
    }
    /* an equation between two terms of one type */
    HolTerm *l = rand_term(depth - 1);
    if (!l) return NULL;
    HolTerm *r = chance(50) ? l : pick_term_of_type(l->type);
    if (!r) r = rand_var(l->type);
    return hk_tm_eq(k, l, r);
}

/* ---- recording kernel operations as records ---------------------------- */

static void note_term(HolTerm *t) {
    if (t) pool_add(&terms, t, emit_term(t));
}

static uint64_t thm_id_of(size_t i) { return thms.v[i].id; }

static uint64_t failed_h[256];
static size_t nfailed_h;

static void rule_result(HolThm *th) {
    uint64_t id = nh++;
    if (th) pool_add(&thms, th, id);
    else if (nfailed_h < 256) failed_h[nfailed_h++] = id;
    hk_clear(k);
}

static void op1t(uint8_t op, HolTerm *t, HolThm *(*fn)(HolKernel *, HolTerm *)) {
    uint64_t id = emit_term(t);
    hk_clear(k);
    HolThm *th = fn(k, t);
    hol_writer_op(w, op);
    hol_writer_uv(w, id);
    rule_result(th);
}

static void op2(uint8_t op, size_t a, size_t b, HolThm *(*fn)(HolKernel *, HolThm *, HolThm *)) {
    hk_clear(k);
    HolThm *th = fn(k, (HolThm *)thms.v[a].p, (HolThm *)thms.v[b].p);
    hol_writer_op(w, op);
    hol_writer_uv(w, thm_id_of(a));
    hol_writer_uv(w, thm_id_of(b));
    rule_result(th);
}

static size_t find_thm(bool (*pred)(HolThm *, void *), void *ctx) {
    if (!thms.n) return (size_t)-1;
    size_t start = pick(thms.n);
    for (size_t i = 0; i < thms.n; i++) {
        size_t j = (start + i) % thms.n;
        if (pred((HolThm *)thms.v[j].p, ctx)) return j;
    }
    return (size_t)-1;
}

static bool is_eq_thm(HolThm *th, void *ctx) { (void)ctx; return hk_tm_is_eq(k, th->concl); }
static bool lhs_alpha(HolThm *th, void *ctx) {
    if (!hk_tm_is_eq(k, th->concl)) return false;
    int c;
    return hk_alphaorder(k, hk_eq_lhs(th->concl), (HolTerm *)ctx, &c) == 0 && c == 0;
}
static bool concl_alpha(HolThm *th, void *ctx) {
    int c;
    return hk_alphaorder(k, th->concl, (HolTerm *)ctx, &c) == 0 && c == 0;
}

/* ---- the operation menu ------------------------------------------------- */

/* Records the kernel must refuse or accept on its own: syntax built from
 * arbitrary parts, constants at arbitrary types, unknown type operators. */
static void gen_raw_syntax(void) {
    unsigned c = (unsigned)pick(100);
    if (c < 45 && terms.n) {
        size_t a = pick(terms.n), b = pick(terms.n);
        hk_clear(k);
        HolTerm *r = hk_tm_comb(k, (HolTerm *)terms.v[a].p, (HolTerm *)terms.v[b].p);
        hol_writer_op(w, HOLR_COMB);
        hol_writer_uv(w, terms.v[a].id);
        hol_writer_uv(w, terms.v[b].id);
        uint64_t id = nt++;
        if (r) { pool_add(&terms, r, id); map_put(&tmmap, &tmmap_n, &tmmap_cap, r, id); }
    } else if (c < 60 && terms.n) {
        size_t a = pick(terms.n), b = pick(terms.n);
        hk_clear(k);
        HolTerm *r = hk_tm_abs(k, (HolTerm *)terms.v[a].p, (HolTerm *)terms.v[b].p);
        hol_writer_op(w, HOLR_ABS);
        hol_writer_uv(w, terms.v[a].id);
        hol_writer_uv(w, terms.v[b].id);
        uint64_t id = nt++;
        if (r) { pool_add(&terms, r, id); map_put(&tmmap, &tmmap_n, &tmmap_cap, r, id); }
    } else if (c < 80) {
        const char *cn = chance(50) ? "=" : (nuser_consts ? user_consts[pick(nuser_consts)] : "nosuch");
        HolType *ty = rand_type(3);
        if (!ty) return;
        uint64_t tyid = emit_type(ty), n = name_id(cn);
        hk_clear(k);
        HolTerm *r = hk_mk_const_at(k, env, kname(cn), ty);
        hol_writer_op(w, HOLR_CONST);
        hol_writer_uv(w, n);
        hol_writer_uv(w, tyid);
        uint64_t id = nt++;
        if (r) { pool_add(&terms, r, id); map_put(&tmmap, &tmmap_n, &tmmap_cap, r, id); }
    } else {
        const char *tn = chance(30) ? "bool" : chance(30) ? "fun" : (nuser_types ? user_types[pick(nuser_types)] : "nosuchty");
        uint32_t nargs = (uint32_t)pick(3);
        HolType *args[3];
        uint64_t ids[3];
        for (uint32_t i = 0; i < nargs; i++) {
            args[i] = rand_type(1);
            if (!args[i]) return;
            ids[i] = emit_type(args[i]);
        }
        uint64_t n = name_id(tn);
        hk_clear(k);
        HolType *r = hk_mk_type_in(k, env, kname(tn), args, nargs);
        hol_writer_op(w, HOLR_TYAPP);
        hol_writer_uv(w, n);
        hol_writer_uv(w, nargs);
        for (uint32_t i = 0; i < nargs; i++) hol_writer_uv(w, ids[i]);
        uint64_t id = ny++;
        if (r) { pool_add(&types, r, id); map_put(&tymap, &tymap_n, &tymap_cap, r, id); }
    }
    hk_clear(k);
}

/* Deliberate capture: \v. B[x] under INST [t/x] with v free in t, nested
 * binders and already-primed names; and binder clashes: \x:A. B[x:B] under
 * INST_TYPE [B/A].  The theorem is REFL of the term (or ASSUME of an
 * equation built from it), so both rules and hypotheses are exercised. */
static HolTerm *nest(HolTerm *body, HolTerm *const *binders, size_t nb) {
    for (size_t i = nb; i-- > 0 && body;) body = hk_tm_abs(k, binders[i], body);
    return body;
}

static void gen_capture(void) {
    HolType *A = hk_ty_var(k, kname("A")), *B = hk_ty_var(k, kname(chance(50) ? "B" : "A"));
    HolType *ty = chance(50) ? A : k->bool_ty;
    const char *bn[] = {"v", "v'", "x", "y", "z"};
    HolTerm *x = hk_tm_var(k, kname("x"), ty);
    HolTerm *binders[3];
    size_t nb = 1 + pick(3);
    for (size_t i = 0; i < nb; i++) binders[i] = hk_tm_var(k, kname(bn[pick(5)]), chance(80) ? ty : B);
    HolTerm *f = hk_tm_var(k, kname("f"), hk_ty_fun(k, ty, hk_ty_fun(k, ty, ty)));
    HolTerm *body = x;
    if (chance(70) && f) { /* f x b0 */
        HolTerm *other = binders[pick(nb)];
        if (other->type == ty) body = hk_tm_comb(k, hk_tm_comb(k, f, x), other);
    }
    HolTerm *lam = body ? nest(body, binders, nb) : NULL;
    if (!lam) return;
    HolThm *th = NULL;
    uint64_t lid = emit_term(lam);
    if (chance(70)) {
        hk_clear(k);
        th = hk_REFL(k, lam);
        hol_writer_op(w, HOLR_REFL);
        hol_writer_uv(w, lid);
    } else {
        HolTerm *e = hk_tm_eq(k, lam, lam);
        if (!e) return;
        uint64_t eid = emit_term(e);
        hk_clear(k);
        th = hk_ASSUME(k, e);
        hol_writer_op(w, HOLR_ASSUME);
        hol_writer_uv(w, eid);
    }
    rule_result(th);
    if (!th) return;
    uint64_t thid = nh - 1;
    if (chance(50)) { /* INST [t/x] with binder names free in t */
        HolTerm *t = binders[pick(nb)];
        if (t->type != ty) t = hk_tm_var(k, t->u.leaf.name, ty);
        if (chance(40) && f) t = hk_tm_comb(k, hk_tm_comb(k, f, t), hk_tm_var(k, kname(bn[pick(5)]), ty));
        if (!t) return;
        HolTmPair pr[2] = {{t, x}, {x, x}};
        size_t np = chance(20) ? 2 : 1;
        if (t == x && !allow_d1) np = 1;
        if (allow_d1 && chance(30)) { pr[0].t = x; pr[1].t = t; np = 2; } /* identity first */
        uint64_t ti = emit_term(t), xi = emit_term(x);
        hk_clear(k);
        HolThm *r = hk_INST(k, pr, np, th);
        hol_writer_op(w, HOLR_INST);
        hol_writer_uv(w, np);
        hol_writer_uv(w, ti);
        hol_writer_uv(w, xi);
        if (np == 2) { hol_writer_uv(w, xi); hol_writer_uv(w, xi); }
        hol_writer_uv(w, thid);
        rule_result(r);
    } else { /* INST_TYPE that identifies binder types with free-variable types */
        HolTyPair pr[2] = {{B, A}, {A, B}};
        size_t np = chance(30) ? 2 : 1;
        uint64_t i0 = emit_type(pr[0].repl), i1 = emit_type(pr[0].key);
        uint64_t i2 = emit_type(pr[1].repl), i3 = emit_type(pr[1].key);
        hk_clear(k);
        HolThm *r = hk_INST_TYPE(k, pr, np, th);
        hol_writer_op(w, HOLR_INST_TYPE);
        hol_writer_uv(w, np);
        hol_writer_uv(w, i0);
        hol_writer_uv(w, i1);
        if (np == 2) { hol_writer_uv(w, i2); hol_writer_uv(w, i3); }
        hol_writer_uv(w, thid);
        rule_result(r);
    }
}

static void gen_clash(void) {
    /* \x:A. f (x:B) (y:A) ... with INST_TYPE [B/A] or [A/B] */
    HolType *A = hk_ty_var(k, kname("A")), *B = chance(70) ? hk_ty_var(k, kname("B")) : k->bool_ty;
    HolTerm *xA = hk_tm_var(k, kname("x"), A), *xB = hk_tm_var(k, kname("x"), B);
    HolTerm *xpB = hk_tm_var(k, kname("x'"), B);
    HolTerm *g = hk_tm_var(k, kname("g"), hk_ty_fun(k, B, hk_ty_fun(k, B, k->bool_ty)));
    if (!xA || !xB || !xpB || !g) return;
    HolTerm *inner = hk_tm_comb(k, hk_tm_comb(k, g, xB), chance(50) ? xpB : xB);
    if (!inner) return;
    HolTerm *lam = hk_tm_abs(k, xA, inner);
    if (chance(50) && lam) lam = hk_tm_abs(k, chance(50) ? xpB : hk_tm_var(k, kname("x'"), A), lam);
    if (!lam) return;
    uint64_t lid = emit_term(lam);
    hk_clear(k);
    HolThm *th = hk_REFL(k, lam);
    hol_writer_op(w, HOLR_REFL);
    hol_writer_uv(w, lid);
    rule_result(th);
    if (!th) return;
    uint64_t thid = nh - 1;
    HolTyPair pr = {B, A};
    if (B == k->bool_ty && chance(50)) pr.repl = k->bool_ty;
    uint64_t i0 = emit_type(pr.repl), i1 = emit_type(pr.key);
    hk_clear(k);
    HolThm *r = hk_INST_TYPE(k, &pr, 1, th);
    hol_writer_op(w, HOLR_INST_TYPE);
    hol_writer_uv(w, 1);
    hol_writer_uv(w, i0);
    hol_writer_uv(w, i1);
    hol_writer_uv(w, thid);
    rule_result(r);
}

static void gen_one(void) {
    if (chance(8)) { gen_capture(); return; }
    if (chance(5)) { gen_clash(); return; }
    if (chance(4)) { gen_raw_syntax(); return; }
    if (nfailed_h && chance(1) && thms.n) { /* dependent failure */
        hol_writer_op(w, HOLR_TRANS);
        hol_writer_uv(w, failed_h[pick(nfailed_h)]);
        hol_writer_uv(w, thm_id_of(pick(thms.n)));
        rule_result(NULL);
        return;
    }
    if (chance(1)) { /* a user axiom (OPEN policy), sometimes not boolean */
        HolTerm *t = rand_term(3);
        if (!t) return;
        uint64_t id = emit_term(t);
        hk_clear(k);
        HolThm *th = NULL;
        HolDecl *d = hk_env_new_axiom(k, env, t, &th);
        hol_writer_op(w, HOLR_NEW_AXIOM);
        hol_writer_uv(w, id);
        if (d) env = d;
        rule_result(d ? th : NULL);
        return;
    }
    if (chance(2)) { /* rare refusal shapes */
        unsigned m = (unsigned)pick(4);
        if (m == 0) { /* definition that is not "var = closed" */
            HolTerm *t = rand_term(3);
            if (!t) return;
            if (chance(50) && nuser_consts) { /* lhs already a constant */
                HolDecl *d = hk_lookup_const(k, env, kname(user_consts[pick(nuser_consts)]));
                HolTyPair p[2] = {{rand_type(1), hk_ty_var(k, kname("A"))}, {rand_type(1), hk_ty_var(k, kname("B"))}};
                HolTerm *c0 = d && p[0].repl && p[1].repl ? hk_mk_const_in(k, env, d->name, p, 2) : NULL;
                HolTerm *r0 = c0 ? pick_term_of_type(c0->type) : NULL;
                if (c0 && !r0) r0 = rand_var(c0->type);
                if (c0 && r0) t = hk_tm_eq(k, c0, r0);
            }
            if (!t) return;
            uint64_t id = emit_term(t);
            hk_clear(k);
            HolThm *th = NULL;
            HolDecl *d = hk_env_new_definition(k, env, t, &th);
            hol_writer_op(w, HOLR_NEW_DEFINITION);
            hol_writer_uv(w, id);
            if (d) env = d;
            rule_result(d ? th : NULL);
        } else if (m == 1) { /* MK_COMB / EQ_MP on arbitrary theorems */
            if (thms.n < 2) return;
            size_t a = pick(thms.n), b = pick(thms.n);
            if (chance(50)) op2(HOLR_MK_COMB, a, b, hk_MK_COMB);
            else op2(HOLR_EQ_MP, a, b, hk_EQ_MP);
        } else if (m == 2) { /* a hypothesis-free theorem whose conclusion is a variable */
            HolTerm *p0 = hk_tm_var(k, kname(var_names[pick(12)]), k->bool_ty);
            if (!p0) return;
            uint64_t id = emit_term(p0);
            hk_clear(k);
            HolThm *th = NULL;
            HolDecl *d = hk_env_new_axiom(k, env, p0, &th);
            hol_writer_op(w, HOLR_NEW_AXIOM);
            hol_writer_uv(w, id);
            if (d) env = d;
            rule_result(d ? th : NULL);
            if (d) { /* then use it as a type-definition witness */
                char nt_[16], na[16], nr[16];
                unsigned u = fresh_counter++;
                snprintf(nt_, sizeof nt_, "tq%u", u);
                snprintf(na, sizeof na, "absq%u", u);
                snprintf(nr, sizeof nr, "repq%u", u);
                uint64_t i1 = name_id(nt_), i2 = name_id(na), i3 = name_id(nr);
                hk_clear(k);
                HolThm *t1 = NULL, *t2 = NULL;
                HolDecl *d2 = hk_env_new_type_definition(k, env, kname(nt_), kname(na), kname(nr), th, &t1, &t2);
                hol_writer_op(w, HOLR_NEW_TYPEDEF);
                hol_writer_uv(w, i1);
                hol_writer_uv(w, i2);
                hol_writer_uv(w, i3);
                hol_writer_uv(w, nh - 1);
                if (d2) env = d2;
                rule_result(d2 ? t1 : NULL);
                rule_result(d2 ? t2 : NULL);
            }
        } else { /* type operator that does not exist */
            uint64_t n = name_id("nosuchty");
            hk_clear(k);
            HolType *r = hk_mk_type_in(k, env, kname("nosuchty"), NULL, 0);
            hol_writer_op(w, HOLR_TYAPP);
            hol_writer_uv(w, n);
            hol_writer_uv(w, 0);
            uint64_t id = ny++;
            if (r) pool_add(&types, r, id);
            hk_clear(k);
        }
        return;
    }
    unsigned c = (unsigned)pick(1000);
    if (c < 60) { note_type(rand_type(3)); return; }
    if (c < 200) { note_term(rand_term(4)); return; }
    if (c < 270) { /* REFL */
        HolTerm *t = terms.n && chance(70) ? (HolTerm *)terms.v[pick(terms.n)].p : rand_term(3);
        if (t) op1t(HOLR_REFL, t, hk_REFL);
        return;
    }
    if (c < 330) { /* ASSUME, sometimes not boolean */
        HolTerm *t = rand_term(3);
        if (t && (t->type == k->bool_ty || chance(25))) op1t(HOLR_ASSUME, t, hk_ASSUME);
        return;
    }
    if (c < 380) { /* BETA: trivial redex or a wrong argument */
        HolTerm *v = rand_var(rand_type(1));
        HolTerm *body = rand_term(3);
        if (!v || !body) return;
        HolTerm *lam = hk_tm_abs(k, v, body);
        HolTerm *arg = chance(75) ? v : rand_var(v->type);
        HolTerm *red = lam && arg ? hk_tm_comb(k, lam, arg) : NULL;
        if (red) op1t(HOLR_BETA, red, hk_BETA);
        return;
    }
    if (c < 470) { /* TRANS: matching (alpha) middle when one exists */
        size_t a = find_thm(is_eq_thm, NULL);
        if (a == (size_t)-1) return;
        HolThm *ta = (HolThm *)thms.v[a].p;
        size_t b = find_thm(lhs_alpha, hk_eq_rhs(ta->concl));
        if (b == (size_t)-1 || chance(15)) b = pick(thms.n);
        op2(HOLR_TRANS, a, b, hk_TRANS);
        return;
    }
    if (c < 540) { /* MK_COMB */
        size_t a = find_thm(is_eq_thm, NULL), b = find_thm(is_eq_thm, NULL);
        if (a == (size_t)-1 || b == (size_t)-1) return;
        HolThm *ta = (HolThm *)thms.v[a].p;
        if (hk_ty_is_fun(k, hk_eq_rhs(ta->concl)->type) && chance(80)) {
            HolType *dom = hk_eq_rhs(ta->concl)->type->args[0];
            HolTerm *x = pick_term_of_type(dom);
            if (!x) x = rand_var(dom);
            if (x) {
                op1t(HOLR_REFL, x, hk_REFL);
                b = thms.n - 1;
                if (((HolThm *)thms.v[b].p)->concl->u.node.r != x) b = pick(thms.n);
            }
        }
        op2(HOLR_MK_COMB, a, b, hk_MK_COMB);
        return;
    }
    if (c < 610) { /* ABS: a variable, maybe free in a hypothesis */
        size_t a = find_thm(is_eq_thm, NULL);
        if (a == (size_t)-1) return;
        HolThm *th = (HolThm *)thms.v[a].p;
        HolTerm *v = NULL;
        if (th->nhyps && chance(40)) {
            HolVec fr = {0};
            if (hk_frees_exact(k, th->hyps[0], &fr) == 0 && fr.n) v = (HolTerm *)fr.v[pick(fr.n)];
            hk_vec_free(k, &fr);
        }
        if (!v) {
            HolVec fr = {0};
            if (hk_frees_exact(k, th->concl, &fr) == 0 && fr.n && chance(70)) v = (HolTerm *)fr.v[pick(fr.n)];
            hk_vec_free(k, &fr);
        }
        if (!v) v = rand_var(rand_type(1));
        if (!v) return;
        if (chance(5) && terms.n) v = (HolTerm *)terms.v[pick(terms.n)].p; /* maybe not a variable */
        uint64_t vid = emit_term(v);
        hk_clear(k);
        HolThm *r = hk_ABS(k, v, th);
        hol_writer_op(w, HOLR_ABS_RULE);
        hol_writer_uv(w, vid);
        hol_writer_uv(w, thm_id_of(a));
        rule_result(r);
        return;
    }
    if (c < 670) { /* EQ_MP with an alpha-equal premise when possible */
        size_t b = pick(thms.n);
        if (!thms.n) return;
        HolThm *tb = (HolThm *)thms.v[b].p;
        size_t a = find_thm(lhs_alpha, tb->concl);
        if (a == (size_t)-1) {
            /* make one: REFL of the conclusion is an equation with that lhs */
            op1t(HOLR_REFL, tb->concl, hk_REFL);
            a = thms.n - 1;
        }
        op2(HOLR_EQ_MP, a, b, hk_EQ_MP);
        return;
    }
    if (c < 720) { /* DEDUCT_ANTISYM_RULE */
        if (thms.n < 1) return;
        size_t a = pick(thms.n), b = pick(thms.n);
        if (chance(50)) {
            HolThm *ta = (HolThm *)thms.v[a].p;
            size_t bb = find_thm(concl_alpha, ta->concl);
            if (bb != (size_t)-1) b = bb;
        }
        op2(HOLR_DEDUCT, a, b, hk_DEDUCT_ANTISYM_RULE);
        return;
    }
    if (c < 820) { /* INST: capture-prone, identity and duplicate pairs */
        if (!thms.n) return;
        size_t a = pick(thms.n);
        HolThm *th = (HolThm *)thms.v[a].p;
        HolVec fr = {0};
        if (hk_frees_exact(k, th->concl, &fr) < 0) { hk_vec_free(k, &fr); return; }
        HolTmPair pairs[4];
        size_t np = 0;
        size_t want = 1 + pick(3);
        for (size_t i = 0; i < want; i++) {
            HolTerm *x = fr.n && chance(85) ? (HolTerm *)fr.v[pick(fr.n)] : rand_var(rand_type(1));
            if (!x) continue;
            HolTerm *t;
            unsigned m = (unsigned)pick(100);
            if (m < 15) t = x; /* identity pair */
            else if (m < 45) t = rand_var(x->type); /* often a binder name: capture */
            else t = pick_term_of_type(x->type);
            if (!t) t = rand_var(x->type);
            if (chance(4) && terms.n) t = (HolTerm *)terms.v[pick(terms.n)].p; /* maybe ill typed */
            if (chance(3) && terms.n) x = (HolTerm *)terms.v[pick(terms.n)].p; /* maybe not a var */
            if (!t) continue;
            pairs[np].t = t;
            pairs[np].x = x;
            np++;
            if (np < 4 && chance(15)) { /* duplicate target, later pair shadowed */
                bool first_is_identity = false;
                for (size_t q = 0; q < np; q++)
                    if (pairs[q].x == x) { first_is_identity = pairs[q].t == x; break; }
                if (!first_is_identity || allow_d1) {
                    pairs[np].t = rand_var(x->type);
                    pairs[np].x = x;
                    if (pairs[np].t) np++;
                }
            }
            if (np >= 3) break;
        }
        hk_vec_free(k, &fr);
        uint64_t ids[8];
        for (size_t i = 0; i < np; i++) {
            ids[2 * i] = emit_term(pairs[i].t);
            ids[2 * i + 1] = emit_term(pairs[i].x);
        }
        hk_clear(k);
        HolThm *r = hk_INST(k, pairs, np, th);
        hol_writer_op(w, HOLR_INST);
        hol_writer_uv(w, np);
        for (size_t i = 0; i < 2 * np; i++) hol_writer_uv(w, ids[i]);
        hol_writer_uv(w, thm_id_of(a));
        rule_result(r);
        return;
    }
    if (c < 900) { /* INST_TYPE, including clashes between same-named vars */
        if (!thms.n) return;
        size_t a = pick(thms.n);
        HolThm *th = (HolThm *)thms.v[a].p;
        HolTyPair pairs[3];
        size_t np = 1 + pick(2);
        for (size_t i = 0; i < np; i++) {
            pairs[i].key = hk_ty_var(k, kname(tyvar_names[pick(3)]));
            pairs[i].repl = chance(40) ? hk_ty_var(k, kname(tyvar_names[pick(3)])) : rand_type(2);
            if (!pairs[i].repl || !pairs[i].key) return;
            if (chance(5)) pairs[i].key = k->bool_ty; /* non-variable key: ignored */
        }
        uint64_t ids[6];
        for (size_t i = 0; i < np; i++) {
            ids[2 * i] = emit_type(pairs[i].repl);
            ids[2 * i + 1] = emit_type(pairs[i].key);
        }
        hk_clear(k);
        HolThm *r = hk_INST_TYPE(k, pairs, np, th);
        hol_writer_op(w, HOLR_INST_TYPE);
        hol_writer_uv(w, np);
        for (size_t i = 0; i < 2 * np; i++) hol_writer_uv(w, ids[i]);
        hol_writer_uv(w, thm_id_of(a));
        rule_result(r);
        return;
    }
    if (c < 925) { /* new_type, sometimes a redeclaration */
        char nm[16];
        if (nuser_types && chance(20)) strcpy(nm, user_types[pick(nuser_types)]);
        else snprintf(nm, sizeof nm, "ty%u", fresh_counter++);
        uint32_t ar = (uint32_t)pick(3);
        uint64_t n = name_id(nm);
        hk_clear(k);
        HolDecl *d = hk_env_new_type(k, env, kname(nm), ar);
        hol_writer_op(w, HOLR_NEW_TYPE);
        hol_writer_uv(w, n);
        hol_writer_uv(w, ar);
        if (d) {
            env = d;
            if (nuser_types < 64) { strcpy(user_types[nuser_types], nm); user_arity[nuser_types++] = ar; }
        }
        hk_clear(k);
        return;
    }
    if (c < 950) { /* new_constant */
        char nm[16];
        if (nuser_consts && chance(20)) strcpy(nm, user_consts[pick(nuser_consts)]);
        else snprintf(nm, sizeof nm, "c%u", fresh_counter++);
        HolType *ty = rand_type(2);
        if (!ty) return;
        uint64_t tyid = emit_type(ty);
        uint64_t n = name_id(nm);
        hk_clear(k);
        HolDecl *d = hk_env_new_constant(k, env, kname(nm), ty);
        hol_writer_op(w, HOLR_NEW_CONSTANT);
        hol_writer_uv(w, n);
        hol_writer_uv(w, tyid);
        if (d) {
            env = d;
            if (nuser_consts < 64) strcpy(user_consts[nuser_consts++], nm);
        }
        hk_clear(k);
        return;
    }
    if (c < 980) { /* new_basic_definition, with closedness/type-variable mistakes */
        char nm[16];
        if (nuser_consts && chance(15)) strcpy(nm, user_consts[pick(nuser_consts)]);
        else snprintf(nm, sizeof nm, "d%u", fresh_counter++);
        HolTerm *rhs = rand_term(3);
        if (!rhs) return;
        if (chance(60)) { /* close it by abstracting its free variables */
            HolVec fr = {0};
            if (hk_frees_exact(k, rhs, &fr) == 0)
                for (size_t i = 0; i < fr.n && rhs; i++) rhs = hk_tm_abs(k, (HolTerm *)fr.v[i], rhs);
            hk_vec_free(k, &fr);
            if (!rhs) return;
        }
        HolType *cty = rhs->type;
        HolTerm *lhs = hk_tm_var(k, kname(nm), cty);
        HolTerm *eq = lhs ? hk_tm_eq(k, lhs, rhs) : NULL;
        if (!eq) return;
        uint64_t id = emit_term(eq);
        hk_clear(k);
        HolThm *th = NULL;
        HolDecl *d = hk_env_new_definition(k, env, eq, &th);
        hol_writer_op(w, HOLR_NEW_DEFINITION);
        hol_writer_uv(w, id);
        if (d) {
            env = d;
            if (nuser_consts < 64) strcpy(user_consts[nuser_consts++], nm);
        }
        rule_result(d ? th : NULL);
        return;
    }
    /* new_basic_type_definition: witness from REFL or another theorem */
    {
        if (!thms.n) return;
        size_t a = pick(thms.n);
        char nt_[16], na[16], nr[16];
        unsigned u = fresh_counter++;
        if (nuser_types && chance(15)) strcpy(nt_, user_types[pick(nuser_types)]);
        else snprintf(nt_, sizeof nt_, "td%u", u);
        snprintf(na, sizeof na, "abs%u", u);
        snprintf(nr, sizeof nr, "rep%u", u);
        if (nuser_consts && chance(10)) strcpy(na, user_consts[pick(nuser_consts)]);
        uint64_t i1 = name_id(nt_), i2 = name_id(na), i3 = name_id(nr);
        hk_clear(k);
        HolThm *t1 = NULL, *t2 = NULL;
        HolDecl *d = hk_env_new_type_definition(k, env, kname(nt_), kname(na), kname(nr),
                                                (HolThm *)thms.v[a].p, &t1, &t2);
        hol_writer_op(w, HOLR_NEW_TYPEDEF);
        hol_writer_uv(w, i1);
        hol_writer_uv(w, i2);
        hol_writer_uv(w, i3);
        hol_writer_uv(w, thm_id_of(a));
        if (d) {
            env = d;
            if (nuser_types < 64) { strcpy(user_types[nuser_types], nt_); user_arity[nuser_types++] = d->parent->parent->arity; }
            if (nuser_consts < 63) { strcpy(user_consts[nuser_consts++], na); strcpy(user_consts[nuser_consts++], nr); }
        }
        rule_result(d ? t1 : NULL);
        rule_result(d ? t2 : NULL);
    }
}

int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[4], "--d1")) allow_d1 = true;
    else if (argc != 4) {
        fprintf(stderr, "usage: fuzz-gen SEED RECORDS OUT.hrs [--d1]\n");
        return 2;
    }
    rng_state = strtoull(argv[1], NULL, 10) * 0x9e3779b97f4a7c15ULL + 1;
    uint64_t target = strtoull(argv[2], NULL, 10);
    k = hol_kernel_new(NULL);
    w = hol_writer_new(HOLR_FLAG_PROBE, HOL_AXIOMS_OPEN);
    if (!k || !w) return 3;
    env = k->d_eq;
    while (hol_writer_records(w) < target) gen_one();
    size_t len;
    const uint8_t *b = hol_writer_finish(w, &len);
    FILE *f = fopen(argv[3], "wb");
    if (!f || !b || fwrite(b, 1, len, f) != len) return 4;
    fclose(f);
    hol_writer_free(w);
    hol_kernel_free(k);
    return 0;
}
