/* hol_core.c - syntax, substitution and the ten primitive rules.
 *
 * Each function that mirrors a fusion.ml function carries the OCaml text it
 * follows in a comment, so the correspondence can be audited line by line.
 * OCaml physical equality (==) becomes pointer equality, which under
 * hash-consing is structural equality; docs/CONTRACT.md discusses the one
 * place (vsubst's "s' == s") where this can change a bound name. */
#include "hol_internal.h"

#include <stdio.h>

#define HOL_TICK_INTERVAL 1024u

/* ------------------------------------------------------------------------ */
/* Allocation, fault injection, interruption                                 */
/* ------------------------------------------------------------------------ */

static bool hol_alloc_should_fail(HolKernel *k) {
    if (k->failing) return true;
    if (k->fail_at) {
        if (--k->fail_at == 0) {
            if (k->fail_sticky) k->failing = true;
            return true;
        }
    }
    return false;
}

void *hk_alloc(HolKernel *k, size_t n) {
    k->allocations++;
    if (HOL_UNLIKELY(k->fail_at || k->failing) && hol_alloc_should_fail(k))
        return hk_fail(k, HOL_NOMEM, HOL_R_NOMEM);
    void *p = malloc(n ? n : 1);
    if (!p) return hk_fail(k, HOL_NOMEM, HOL_R_NOMEM);
    k->bytes_live += n;
    k->bytes_since_gc += n;
    if (k->bytes_live > k->bytes_peak) k->bytes_peak = k->bytes_live;
    return p;
}

void *hk_realloc(HolKernel *k, void *p, size_t old_n, size_t new_n) {
    k->allocations++;
    if (HOL_UNLIKELY(k->fail_at || k->failing) && hol_alloc_should_fail(k))
        return hk_fail(k, HOL_NOMEM, HOL_R_NOMEM);
    void *q = realloc(p, new_n ? new_n : 1);
    if (!q) return hk_fail(k, HOL_NOMEM, HOL_R_NOMEM);
    k->bytes_live = k->bytes_live - old_n + new_n;
    if (new_n > old_n) k->bytes_since_gc += new_n - old_n;
    if (k->bytes_live > k->bytes_peak) k->bytes_peak = k->bytes_live;
    return q;
}

void hk_dealloc(HolKernel *k, void *p, size_t n) {
    if (!p) return;
    free(p);
    k->bytes_live -= n;
}

bool hk_tick_slow(HolKernel *k) {
    k->tick = HOL_TICK_INTERVAL;
    if ((k->interrupt && *k->interrupt) ||
        (k->interrupt_poll && k->interrupt_poll(k->interrupt_context))) {
        hk_fail(k, HOL_INTERRUPTED, HOL_R_INTERRUPT);
        return false;
    }
    if (k->fuel_per_call) {
        if (k->fuel <= HOL_TICK_INTERVAL) {
            hk_fail(k, HOL_INTERRUPTED, HOL_R_FUEL);
            return false;
        }
        k->fuel -= HOL_TICK_INTERVAL;
    }
    return true;
}

void hk_begin_call(HolKernel *k) {
    hk_clear(k);
    k->fuel = k->fuel_per_call;
    k->tick = HOL_TICK_INTERVAL;
}

uint64_t hk_fnv1a(const void *data, size_t n, uint64_t h) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* ------------------------------------------------------------------------ */
/* Vectors and scratch                                                      */
/* ------------------------------------------------------------------------ */

int hk_vec_push(HolKernel *k, HolVec *vec, void *p) {
    if (vec->n == vec->cap) {
        size_t ncap = vec->cap ? vec->cap * 2 : 16;
        void **nv = (void **)hk_realloc(k, vec->v, vec->cap * sizeof(void *),
                                         ncap * sizeof(void *));
        if (!nv) return -1;
        vec->v = nv;
        vec->cap = ncap;
    }
    vec->v[vec->n++] = p;
    return 0;
}

void hk_vec_free(HolKernel *k, HolVec *vec) {
    hk_dealloc(k, vec->v, vec->cap * sizeof(void *));
    vec->v = NULL;
    vec->n = vec->cap = 0;
}

/* ------------------------------------------------------------------------ */
/* Names                                                                     */
/* ------------------------------------------------------------------------ */

static uint32_t hol_name_hash_raw(const char *s, size_t len) {
    uint64_t h = hk_fnv1a(s, len, HOL_FNV_OFFSET);
    return (uint32_t)(h ^ (h >> 32));
}

static int hol_name_index_grow(HolKernel *k) {
    size_t ncap = k->name_index_cap ? k->name_index_cap * 2 : 1024;
    uint32_t *ni = (uint32_t *)hk_alloc(k, ncap * sizeof(uint32_t));
    if (!ni) return -1;
    memset(ni, 0, ncap * sizeof(uint32_t));
    for (size_t i = 0; i < k->name_index_cap; i++) {
        uint32_t id = k->name_index[i];
        if (!id) continue;
        size_t j = k->names[id].hash & (ncap - 1);
        while (ni[j]) j = (j + 1) & (ncap - 1);
        ni[j] = id;
    }
    hk_dealloc(k, k->name_index, k->name_index_cap * sizeof(uint32_t));
    k->name_index = ni;
    k->name_index_cap = ncap;
    return 0;
}

HolName hk_name_intern(HolKernel *k, const char *s, size_t len) {
    if (len > UINT32_MAX - 1) { hk_fail(k, HOL_LIMIT, HOL_R_BAD_NAME); return 0; }
    uint32_t h = hol_name_hash_raw(s, len) & k->hash_mask;
    if (k->name_index_cap) {
        size_t mask = k->name_index_cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            uint32_t id = k->name_index[j];
            if (!id) break;
            const HolNameRec *r = &k->names[id];
            if (r->hash == h && r->len == len && memcmp(r->bytes, s, len) == 0)
                return id;
        }
    }
    if ((size_t)(k->nnames + 1) * 2 > k->name_index_cap &&
        hol_name_index_grow(k) < 0)
        return 0;
    if (k->nnames + 1 >= k->capnames) {
        uint32_t ncap = k->capnames ? k->capnames * 2 : 1024;
        HolNameRec *nn = (HolNameRec *)hk_realloc(
            k, k->names, k->capnames * sizeof(HolNameRec),
            (size_t)ncap * sizeof(HolNameRec));
        if (!nn) return 0;
        k->names = nn;
        k->capnames = ncap;
    }
    char *bytes = (char *)hk_alloc(k, len + 1);
    if (!bytes) return 0;
    memcpy(bytes, s, len);
    bytes[len] = 0;
    uint32_t id = ++k->nnames;
    HolNameRec *r = &k->names[id];
    r->bytes = bytes;
    r->len = (uint32_t)len;
    r->hash = h;
    r->types = NULL;
    r->consts = NULL;
    size_t mask = k->name_index_cap - 1;
    size_t j = h & mask;
    while (k->name_index[j]) j = (j + 1) & mask;
    k->name_index[j] = id;
    return id;
}

/* OCaml's string comparison: memcmp of the common prefix, then length. */
int hk_name_cmp(const HolKernel *k, HolName a, HolName b) {
    if (a == b) return 0;
    const HolNameRec *x = &k->names[a], *y = &k->names[b];
    size_t n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->bytes, y->bytes, n);
    if (c) return c < 0 ? -1 : 1;
    if (x->len != y->len) return x->len < y->len ? -1 : 1;
    return 0;
}

HolName hk_name_prime(HolKernel *k, HolName n) {
    const HolNameRec *r = &k->names[n];
    size_t len = r->len;
    char small[256];
    char *buf = small;
    if (len + 1 > sizeof small) {
        buf = (char *)hk_alloc(k, len + 1);
        if (!buf) return 0;
    }
    memcpy(buf, k->names[n].bytes, len); /* names may move on intern */
    buf[len] = '\'';
    HolName out = hk_name_intern(k, buf, len + 1);
    if (buf != small) hk_dealloc(k, buf, len + 1);
    return out;
}

/* ------------------------------------------------------------------------ */
/* Hash-consing tables                                                       */
/* ------------------------------------------------------------------------ */

static inline uint32_t obj_hash(const void *o) { return *(const uint32_t *)o; }

int hk_table_insert(HolKernel *k, HolTable *t, void *obj, uint32_t hash) {
    if ((t->count + 1) * 2 > t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 1024;
        void **ns = (void **)hk_alloc(k, ncap * sizeof(void *));
        if (!ns) return -1;
        memset(ns, 0, ncap * sizeof(void *));
        for (size_t i = 0; i < t->cap; i++) {
            void *o = t->slot[i];
            if (!o) continue;
            size_t j = obj_hash(o) & (ncap - 1);
            while (ns[j]) j = (j + 1) & (ncap - 1);
            ns[j] = o;
        }
        hk_dealloc(k, t->slot, t->cap * sizeof(void *));
        t->slot = ns;
        t->cap = ncap;
    }
    size_t mask = t->cap - 1;
    size_t j = hash & mask;
    while (t->slot[j]) j = (j + 1) & mask;
    t->slot[j] = obj;
    t->count++;
    return 0;
}

void hk_table_free(HolKernel *k, HolTable *t) {
    hk_dealloc(k, t->slot, t->cap * sizeof(void *));
    t->slot = NULL;
    t->cap = t->count = 0;
}

/* ------------------------------------------------------------------------ */
/* Environment anchors                                                       */
/* ------------------------------------------------------------------------ */

/* a is an ancestor-or-self of e (NULL means "no requirement"). */
bool hk_decl_le(const HolDecl *a, const HolDecl *e) {
    if (!a) return true;
    while (e && e->depth >= a->depth) {
        if (e->branch == a->branch) return a->bindex <= e->bindex;
        e = e->branch->fork;
    }
    return false;
}

int hk_anchor_join(HolDecl *a, HolDecl *b, HolDecl **out) {
    if (a == b || !b) { *out = a; return 0; }
    if (!a) { *out = b; return 0; }
    if (a->depth <= b->depth) {
        if (hk_decl_le(a, b)) { *out = b; return 0; }
    } else if (hk_decl_le(b, a)) {
        *out = a;
        return 0;
    }
    return -1;
}

static inline int join_or_fail(HolKernel *k, HolDecl *a, HolDecl *b, HolDecl **out) {
    if (hk_anchor_join(a, b, out) < 0) {
        hk_fail(k, HOL_FOREIGN, HOL_R_INCOMPATIBLE_ENV);
        return -1;
    }
    return 0;
}

static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

static inline bool height_ok(HolKernel *k, uint32_t h) {
    if (HOL_UNLIKELY(h > k->max_depth)) {
        hk_fail(k, HOL_LIMIT, HOL_R_DEPTH);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

#define HOL_SEED_TYVAR 0x9e3779b97f4a7c15ULL
#define HOL_SEED_TYAPP 0xc2b2ae3d27d4eb4fULL
#define HOL_SEED_VAR 0x165667b19e3779f9ULL
#define HOL_SEED_CONST 0x27d4eb2f165667c5ULL
#define HOL_SEED_COMB 0x85ebca77c2b2ae63ULL
#define HOL_SEED_ABS 0xff51afd7ed558ccdULL
#define HOL_SEED_THM 0xc4ceb9fe1a85ec53ULL
#define HOL_SEED_DEPS 0x94d049bb133111ebULL

HolType *hk_ty_var(HolKernel *k, HolName name) {
    uint32_t h = hk_hash_finish(k, HOL_SEED_TYVAR ^ k->names[name].hash);
    HolTable *t = &k->types;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolType *o = (HolType *)t->slot[j];
            if (!o) break;
            if (o->hash == h && o->kind == HOL_TY_VAR && o->name == name) {
                k->intern_hits++;
                return o;
            }
        }
    }
    k->intern_misses++;
    HolType *ty = (HolType *)hk_alloc(k, sizeof(HolType));
    if (!ty) return NULL;
    memset(ty, 0, sizeof *ty);
    ty->hash = h;
    ty->kind = HOL_TY_VAR;
    ty->name = name;
    ty->height = 1;
    ty->tv_sig = hk_sig_bit(k->names[name].hash);
    if (hk_table_insert(k, t, ty, h) < 0) {
        hk_dealloc(k, ty, sizeof(HolType));
        return NULL;
    }
    return ty;
}

HolType *hk_ty_app(HolKernel *k, HolDecl *op, HolType *const *args, uint32_t n) {
    uint64_t hh = hk_mix64(HOL_SEED_TYAPP ^ op->serial);
    HolDecl *anchor = op;
    uint32_t height = 0;
    uint64_t sig = 0;
    for (uint32_t i = 0; i < n; i++) {
        hh = hk_mix64(hh ^ args[i]->hash);
        if (join_or_fail(k, anchor, args[i]->anchor, &anchor) < 0) return NULL;
        height = max_u32(height, args[i]->height);
        sig |= args[i]->tv_sig;
    }
    if (!height_ok(k, height + 1)) return NULL;
    uint32_t h = hk_hash_finish(k, hh ^ ((uint64_t)(height + 1) << 40)); /* see node_term */
    HolTable *t = &k->types;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolType *o = (HolType *)t->slot[j];
            if (!o) break;
            if (o->hash != h || o->kind != HOL_TY_APP || o->op != op || o->nargs != n)
                continue;
            uint32_t i = 0;
            while (i < n && o->args[i] == args[i]) i++;
            if (i == n) {
                k->intern_hits++;
                return o;
            }
        }
    }
    k->intern_misses++;
    size_t size = sizeof(HolType) + (size_t)n * sizeof(HolType *);
    HolType *ty = (HolType *)hk_alloc(k, size);
    if (!ty) return NULL;
    memset(ty, 0, sizeof *ty);
    ty->hash = h;
    ty->kind = HOL_TY_APP;
    ty->nargs = n;
    ty->name = op->name;
    ty->op = op;
    ty->anchor = anchor;
    ty->height = height + 1;
    ty->tv_sig = sig;
    for (uint32_t i = 0; i < n; i++) ty->args[i] = args[i];
    if (hk_table_insert(k, t, ty, h) < 0) {
        hk_dealloc(k, ty, size);
        return NULL;
    }
    return ty;
}

HolType *hk_ty_fun(HolKernel *k, HolType *a, HolType *b) {
    HolType *args[2] = {a, b};
    return hk_ty_app(k, k->d_fun, args, 2);
}

/* OCaml compare on hol_type: Tyvar < Tyapp; names by string order; argument
 * lists lexicographically with the shorter prefix first. */
int hk_ty_compare(const HolKernel *k, const HolType *a, const HolType *b) {
    if (a == b) return 0;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    int c = hk_name_cmp(k, a->name, b->name);
    if (c) return c;
    if (a->kind == HOL_TY_VAR) return 0;
    uint32_t n = a->nargs < b->nargs ? a->nargs : b->nargs;
    for (uint32_t i = 0; i < n; i++) {
        c = hk_ty_compare(k, a->args[i], b->args[i]);
        if (c) return c;
    }
    if (a->nargs != b->nargs) return a->nargs < b->nargs ? -1 : 1;
    /* Same spelling and arguments but distinct declarations: only possible
     * across incompatible branches, which no rule combines. */
    return a->op->serial < b->op->serial ? -1 : (a->op->serial > b->op->serial);
}

void hk_tysubst_init(HolTySubst *s, const HolTyPair *p, size_t n) {
    s->p = p;
    s->n = n;
    s->dom_sig = 0;
    for (size_t i = 0; i < n; i++)
        if (p[i].key->kind == HOL_TY_VAR) s->dom_sig |= p[i].key->tv_sig;
}

static uint32_t hk_memo_ctx(HolKernel *k);
static inline bool memo_get(HolKernel *k, const void *key, uint32_t ctx, void **val,
                            uint32_t *tag);
static inline void memo_put(HolKernel *k, const void *key, uint32_t ctx, void *val,
                            uint32_t tag);

/* let rec type_subst i ty =
 *   match ty with
 *     Tyapp(tycon,args) ->
 *         let args' = qmap (type_subst i) args in
 *         if args' == args then ty else Tyapp(tycon,args')
 *     | _ -> rev_assocd ty i ty                                            */
static HolType *type_subst_rec(HolKernel *k, const HolTySubst *s, HolType *ty,
                               uint32_t ctx) {
    if ((ty->tv_sig & s->dom_sig) == 0) return ty;
    if (ty->kind == HOL_TY_VAR) {
        for (size_t i = 0; i < s->n; i++)
            if (s->p[i].key == ty) return s->p[i].repl;
        return ty;
    }
    if (!hk_tick(k)) return NULL;
    {
        void *v;
        uint32_t tag;
        if (memo_get(k, ty, ctx, &v, &tag) && tag == 1) return (HolType *)v;
    }
    HolType *small[8];
    HolType **args = small;
    uint32_t n = ty->nargs;
    if (n > 8) {
        args = (HolType **)hk_alloc(k, (size_t)n * sizeof(HolType *));
        if (!args) return NULL;
    }
    bool changed = false;
    HolType *out = ty;
    for (uint32_t i = 0; i < n; i++) {
        args[i] = type_subst_rec(k, s, ty->args[i], ctx);
        if (!args[i]) { out = NULL; goto done; }
        if (args[i] != ty->args[i]) changed = true;
    }
    if (changed) out = hk_ty_app(k, ty->op, args, n);
done:
    if (args != small) hk_dealloc(k, args, (size_t)n * sizeof(HolType *));
    if (out) memo_put(k, ty, ctx, out, 1);
    return out;
}

HolType *hk_type_subst(HolKernel *k, const HolTySubst *s, HolType *ty) {
    if (s->n == 0) return ty;
    return type_subst_rec(k, s, ty, hk_memo_ctx(k));
}

/* ------------------------------------------------------------------------ */
/* Memo (per-operation caches over hash-consed DAGs)                         */
/* ------------------------------------------------------------------------ */

static void hk_memo_reset(HolKernel *k) {
    HolMemo *m = &k->memo;
    m->gen++;
    m->used = 0;
    if (m->gen == 0) { /* wrapped: clear stale generations */
        if (m->cap) memset(m->e, 0, m->cap * sizeof(HolMemoEntry));
        m->gen = 1;
    }
}

/* A fresh context id; entries of other contexts never match.  Grows the
 * table lazily; failure to grow only disables memoization. */
static uint32_t hk_memo_ctx(HolKernel *k) {
    HolMemo *m = &k->memo;
    if (!m->cap) {
        size_t cap = 1u << 12;
        HolMemoEntry *e = (HolMemoEntry *)malloc(cap * sizeof(HolMemoEntry));
        if (!e) return 0;
        memset(e, 0, cap * sizeof(HolMemoEntry));
        m->e = e;
        m->cap = cap;
        m->gen = 1;
        k->memo_bytes = cap * sizeof(HolMemoEntry);
    } else if (m->used * 4 > m->cap * 3) {
        size_t ncap = m->cap < (1u << 20) ? m->cap * 2 : m->cap;
        if (ncap != m->cap) {
            HolMemoEntry *e = (HolMemoEntry *)malloc(ncap * sizeof(HolMemoEntry));
            if (e) {
                memset(e, 0, ncap * sizeof(HolMemoEntry));
                free(m->e);
                m->e = e;
                m->cap = ncap;
                m->gen = 1;
                m->used = 0;
                k->memo_bytes = ncap * sizeof(HolMemoEntry);
            } else hk_memo_reset(k);
        } else hk_memo_reset(k);
    }
    if (++k->memo_ctx == 0) {
        hk_memo_reset(k);
        k->memo_ctx = 1;
    }
    return k->memo_ctx;
}

/* Lookups and stores re-probe the table: no entry pointer is held across a
 * recursive call, because a nested operation may grow or reset the table. */
static inline bool memo_get(HolKernel *k, const void *key, uint32_t ctx, void **val,
                            uint32_t *tag) {
    HolMemo *m = &k->memo;
    if (!ctx || !m->cap) return false;
    size_t mask = m->cap - 1;
    size_t j = (((uintptr_t)key >> 4) ^ ctx * 0x9e3779b1u) & mask;
    for (size_t probe = 0; probe < 8; probe++, j = (j + 1) & mask) {
        HolMemoEntry *e = &m->e[j];
        if (e->gen != m->gen) return false;
        if (e->key == key && e->ctx == ctx) {
            *val = e->val;
            *tag = e->tag;
            return true;
        }
    }
    return false;
}

static inline void memo_put(HolKernel *k, const void *key, uint32_t ctx, void *val,
                            uint32_t tag) {
    HolMemo *m = &k->memo;
    if (!ctx || !m->cap) return;
    size_t mask = m->cap - 1;
    size_t j = (((uintptr_t)key >> 4) ^ ctx * 0x9e3779b1u) & mask;
    for (size_t probe = 0; probe < 8; probe++, j = (j + 1) & mask) {
        HolMemoEntry *e = &m->e[j];
        if (e->gen != m->gen) {
            m->used++;
        } else if (e->key != key || e->ctx != ctx) {
            continue;
        }
        e->key = key;
        e->ctx = ctx;
        e->gen = m->gen;
        e->val = val;
        e->tag = tag;
        return;
    }
}

/* ------------------------------------------------------------------------ */
/* Terms                                                                     */
/* ------------------------------------------------------------------------ */

static inline void note_child(HolTerm *c) {
    if (c->flags & HOL_TMF_HAS_PARENT) c->flags |= HOL_TMF_SHARED;
    else c->flags |= HOL_TMF_HAS_PARENT;
}

static HolTerm *new_term(HolKernel *k, uint32_t h) {
    HolTerm *t = (HolTerm *)hk_alloc(k, sizeof(HolTerm));
    if (!t) return NULL;
    memset(t, 0, sizeof *t);
    t->hash = h;
    return t;
}

static HolTerm *intern_term(HolKernel *k, HolTerm *t) {
    if (hk_table_insert(k, &k->terms, t, t->hash) < 0) {
        hk_dealloc(k, t, sizeof(HolTerm));
        return NULL;
    }
    return t;
}

HolTerm *hk_tm_var(HolKernel *k, HolName name, HolType *ty) {
    if (!height_ok(k, ty->height + 1)) return NULL;
    uint32_t h = hk_hash_finish(k, HOL_SEED_VAR ^ ((uint64_t)k->names[name].hash << 32 | ty->hash));
    HolTable *t = &k->terms;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolTerm *o = (HolTerm *)t->slot[j];
            if (!o) break;
            if (o->hash == h && o->kind == HOL_TM_VAR && o->u.leaf.name == name &&
                o->type == ty) {
                k->intern_hits++;
                return o;
            }
        }
    }
    k->intern_misses++;
    HolTerm *tm = new_term(k, h);
    if (!tm) return NULL;
    tm->kind = HOL_TM_VAR;
    tm->type = ty;
    tm->height = ty->height + 1;
    tm->anchor = ty->anchor;
    tm->u.leaf.name = name;
    tm->u.leaf.decl = NULL;
    tm->fv_sig = hk_sig_bit(h);
    tm->tv_sig = ty->tv_sig;
    return intern_term(k, tm);
}

HolTerm *hk_tm_const(HolKernel *k, HolDecl *decl, HolType *ty) {
    HolDecl *anchor;
    if (join_or_fail(k, decl, ty->anchor, &anchor) < 0) return NULL;
    if (!height_ok(k, ty->height + 1)) return NULL;
    uint32_t h = hk_hash_finish(k, HOL_SEED_CONST ^ (decl->serial << 32 | ty->hash));
    HolTable *t = &k->terms;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolTerm *o = (HolTerm *)t->slot[j];
            if (!o) break;
            if (o->hash == h && o->kind == HOL_TM_CONST && o->u.leaf.decl == decl &&
                o->type == ty) {
                k->intern_hits++;
                return o;
            }
        }
    }
    k->intern_misses++;
    HolTerm *tm = new_term(k, h);
    if (!tm) return NULL;
    tm->kind = HOL_TM_CONST;
    tm->type = ty;
    tm->height = ty->height + 1;
    tm->anchor = anchor;
    tm->u.leaf.name = decl->name;
    tm->u.leaf.decl = decl;
    tm->fv_sig = 0;
    tm->tv_sig = ty->tv_sig;
    return intern_term(k, tm);
}

/* Interning for Comb/Abs nodes whose type is already known. */
static HolTerm *node_term(HolKernel *k, int kind, HolTerm *l, HolTerm *r,
                          HolType *type) {
    HolDecl *anchor;
    if (join_or_fail(k, l->anchor, r->anchor, &anchor) < 0) return NULL;
    uint32_t height = max_u32(l->height, r->height) + 1;
    if (!height_ok(k, height)) return NULL;
    /* The height is mixed in: a hash computed from the children's 32-bit
     * hashes alone iterates a 32-bit map along a long chain and falls into
     * a cycle after about 2^16 steps, piling the rest of the chain onto a
     * few hash values.  The height strictly increases along every chain. */
    uint64_t seed = kind == HOL_TM_COMB ? HOL_SEED_COMB : HOL_SEED_ABS;
    uint32_t h = hk_hash_finish(k, hk_mix64(hk_mix64(seed ^ l->hash ^ ((uint64_t)height << 32)) ^ r->hash));
    HolTable *t = &k->terms;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolTerm *o = (HolTerm *)t->slot[j];
            if (!o) break;
            if (o->hash == h && o->kind == kind && o->u.node.l == l && o->u.node.r == r) {
                k->intern_hits++;
                return o;
            }
        }
    }
    k->intern_misses++;
    HolTerm *tm = new_term(k, h);
    if (!tm) return NULL;
    tm->kind = (uint8_t)kind;
    tm->type = type;
    tm->height = height;
    tm->anchor = anchor;
    tm->u.node.l = l;
    tm->u.node.r = r;
    tm->fv_sig = l->fv_sig | r->fv_sig;
    if (kind == HOL_TM_ABS) tm->fv_sig = r->fv_sig;
    tm->tv_sig = l->tv_sig | r->tv_sig;
    if (!intern_term(k, tm)) return NULL;
    note_child(l);
    note_child(r);
    return tm;
}

/* let mk_comb(f,a) =
 *   match type_of f with
 *     Tyapp("fun",[ty;_]) when compare ty (type_of a) = 0 -> Comb(f,a)
 *   | _ -> failwith "mk_comb: types do not agree"                          */
HolTerm *hk_tm_comb(HolKernel *k, HolTerm *f, HolTerm *x) {
    HolType *fty = f->type;
    if (!hk_ty_is_fun(k, fty) || fty->args[0] != x->type)
        return hk_fail(k, HOL_REFUSED, HOL_R_MK_COMB_TYPES);
    return node_term(k, HOL_TM_COMB, f, x, fty->args[1]);
}

/* let mk_abs(bvar,bod) =
 *   match bvar with Var(_,_) -> Abs(bvar,bod) | _ -> failwith "mk_abs: ..." */
HolTerm *hk_tm_abs(HolKernel *k, HolTerm *v, HolTerm *body) {
    if (v->kind != HOL_TM_VAR) return hk_fail(k, HOL_REFUSED, HOL_R_MK_ABS_NOT_VAR);
    HolType *ty = hk_ty_fun(k, v->type, body->type);
    if (!ty) return NULL;
    return node_term(k, HOL_TM_ABS, v, body, ty);
}

/* let safe_mk_eq l r =
 *   let ty = type_of l in
 *   Comb(Comb(Const("=",Tyapp("fun",[ty;Tyapp("fun",[ty;bool_ty])])),l),r) */
HolTerm *hk_tm_eq(HolKernel *k, HolTerm *l, HolTerm *r) {
    HolType *ty = l->type;
    HolType *t2 = hk_ty_fun(k, ty, k->bool_ty);
    if (!t2) return NULL;
    HolType *t1 = hk_ty_fun(k, ty, t2);
    if (!t1) return NULL;
    HolTerm *c = hk_tm_const(k, k->d_eq, t1);
    if (!c) return NULL;
    HolTerm *cl = hk_tm_comb(k, c, l);
    if (!cl) return NULL;
    return hk_tm_comb(k, cl, r);
}

/* OCaml compare on term: Var < Const < Comb < Abs, then fields in order. */
int hk_tm_compare(const HolKernel *k, const HolTerm *a, const HolTerm *b) {
    while (a != b) {
        if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
        if (a->kind == HOL_TM_VAR || a->kind == HOL_TM_CONST) {
            int c = hk_name_cmp(k, a->u.leaf.name, b->u.leaf.name);
            if (c) return c;
            c = hk_ty_compare(k, a->type, b->type);
            if (c) return c;
            if (a->kind == HOL_TM_CONST && a->u.leaf.decl != b->u.leaf.decl)
                return a->u.leaf.decl->serial < b->u.leaf.decl->serial ? -1 : 1;
            return 0;
        }
        int c = hk_tm_compare(k, a->u.node.l, b->u.node.l);
        if (c) return c;
        a = a->u.node.r;
        b = b->u.node.r;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Alpha order                                                               */
/* ------------------------------------------------------------------------ */

typedef struct { HolTerm *x1, *x2; } OrdaPair;
typedef struct {
    OrdaPair *p;
    size_t n, cap, nonid;
} OrdaEnv;

/* let rec ordav env x1 x2 =
 *   match env with
 *     [] -> compare x1 x2
 *   | (t1,t2)::oenv -> if compare x1 t1 = 0
 *                      then if compare x2 t2 = 0 then 0 else -1
 *                      else if compare x2 t2 = 0 then 1
 *                      else ordav oenv x1 x2                               */
static int ordav(const HolKernel *k, const OrdaEnv *env, HolTerm *x1, HolTerm *x2) {
    for (size_t i = env->n; i-- > 0;) {
        if (x1 == env->p[i].x1) return x2 == env->p[i].x2 ? 0 : -1;
        if (x2 == env->p[i].x2) return 1;
    }
    return hk_tm_compare(k, x1, x2);
}

/* let rec orda env tm1 tm2 =
 *   if tm1 == tm2 && forall (fun (x,y) -> x = y) env then 0 else
 *   match (tm1,tm2) with
 *     Var(x1,ty1),Var(x2,ty2) -> ordav env tm1 tm2
 *   | Const(x1,ty1),Const(x2,ty2) -> compare tm1 tm2
 *   | Comb(s1,t1),Comb(s2,t2) ->
 *         let c = orda env s1 s2 in if c <> 0 then c else orda env t1 t2
 *   | Abs(Var(_,ty1) as x1,t1),Abs(Var(_,ty2) as x2,t2) ->
 *         let c = compare ty1 ty2 in
 *         if c <> 0 then c else orda ((x1,x2)::env) t1 t2
 *   | Const(_,_),_ -> -1 | _,Const(_,_) -> 1
 *   | Var(_,_),_ -> -1   | _,Var(_,_) -> 1
 *   | Comb(_,_),_ -> -1  | _,Comb(_,_) -> 1                                 */
static int orda(HolKernel *k, OrdaEnv *env, HolTerm *t1, HolTerm *t2, int *out) {
    for (;;) {
        if (t1 == t2 && env->nonid == 0) { *out = 0; return 0; }
        if (!hk_tick(k)) return -1;
        int a = t1->kind, b = t2->kind;
        if (a == b) {
            switch (a) {
            case HOL_TM_VAR: *out = ordav(k, env, t1, t2); return 0;
            case HOL_TM_CONST: *out = hk_tm_compare(k, t1, t2); return 0;
            case HOL_TM_COMB: {
                int c;
                if (orda(k, env, t1->u.node.l, t2->u.node.l, &c) < 0) return -1;
                if (c) { *out = c; return 0; }
                t1 = t1->u.node.r;
                t2 = t2->u.node.r;
                continue;
            }
            default: {
                HolTerm *x1 = t1->u.node.l, *x2 = t2->u.node.l;
                int c = hk_ty_compare(k, x1->type, x2->type);
                if (c) { *out = c; return 0; }
                if (env->n == env->cap) {
                    size_t ncap = env->cap ? env->cap * 2 : 32;
                    OrdaPair *np = (OrdaPair *)hk_realloc(k, env->p, env->cap * sizeof(OrdaPair),
                                                           ncap * sizeof(OrdaPair));
                    if (!np) return -1;
                    env->p = np;
                    env->cap = ncap;
                }
                env->p[env->n].x1 = x1;
                env->p[env->n].x2 = x2;
                env->n++;
                if (x1 != x2) env->nonid++;
                int r = orda(k, env, t1->u.node.r, t2->u.node.r, out);
                env->n--;
                if (x1 != x2) env->nonid--;
                return r;
            }
            }
        }
        if (a == HOL_TM_CONST) *out = -1;
        else if (b == HOL_TM_CONST) *out = 1;
        else if (a == HOL_TM_VAR) *out = -1;
        else if (b == HOL_TM_VAR) *out = 1;
        else if (a == HOL_TM_COMB) *out = -1;
        else *out = 1;
        return 0;
    }
}

int hk_alphaorder(HolKernel *k, HolTerm *a, HolTerm *b, int *out) {
    if (a == b) { *out = 0; return 0; }
    OrdaEnv env = {0};
    int r = orda(k, &env, a, b, out);
    hk_dealloc(k, env.p, env.cap * sizeof(OrdaPair));
    return r;
}

/* ------------------------------------------------------------------------ */
/* Free variables                                                            */
/* ------------------------------------------------------------------------ */

static uint32_t next_stamp(HolKernel *k) {
    if (++k->stamp == 0) {
        for (size_t i = 0; i < k->terms.cap; i++) {
            HolTerm *t = (HolTerm *)k->terms.slot[i];
            if (t) t->stamp = 0;
        }
        k->stamp = 1;
    }
    return k->stamp;
}

/* let rec vfree_in v tm =
 *   match tm with
 *     Abs(bv,bod) -> v <> bv && vfree_in v bod
 *   | Comb(s,t) -> vfree_in v s || vfree_in v t
 *   | _ -> compare tm v = 0
 * A visited node whose answer was false is not revisited (the answer does
 * not depend on the context), which makes the walk linear in DAG size. */
static int vfree_rec(HolKernel *k, HolTerm *v, uint64_t vbit, uint32_t stamp, HolTerm *tm) {
    for (;;) {
        if (vbit && !(tm->fv_sig & vbit)) return 0;
        if (tm->stamp == stamp) return 0;
        if (!hk_tick(k)) return -1;
        int r;
        switch (tm->kind) {
        case HOL_TM_ABS:
            if (tm->u.node.l == v) { r = 0; break; }
            r = vfree_rec(k, v, vbit, stamp, tm->u.node.r);
            break;
        case HOL_TM_COMB:
            r = vfree_rec(k, v, vbit, stamp, tm->u.node.l);
            if (r == 0) r = vfree_rec(k, v, vbit, stamp, tm->u.node.r);
            break;
        default:
            r = tm == v;
        }
        if (r == 0) tm->stamp = stamp;
        return r;
    }
}

int hk_vfree_in(HolKernel *k, HolTerm *v, HolTerm *tm) {
    uint64_t vbit = v->kind == HOL_TM_VAR ? v->fv_sig : 0;
    if (vbit && !(tm->fv_sig & vbit)) return 0;
    /* only leaves are compared with v: a combination or abstraction v is
     * not free in anything, itself included */
    if (tm->kind == HOL_TM_VAR || tm->kind == HOL_TM_CONST) return tm == v;
    return vfree_rec(k, v, vbit, next_stamp(k), tm);
}

/* let rec freesin acc tm =
 *   match tm with
 *     Var(_,_) -> mem tm acc
 *   | Const(_,_) -> true
 *   | Abs(bv,bod) -> freesin (bv::acc) bod
 *   | Comb(s,t) -> freesin acc s && freesin acc t                         */
static int freesin_rec(HolKernel *k, HolVec *acc, HolTerm *tm) {
    for (;;) {
        if (!hk_tick(k)) return -1;
        switch (tm->kind) {
        case HOL_TM_VAR:
            for (size_t i = acc->n; i-- > 0;)
                if (acc->v[i] == tm) return 1;
            return 0;
        case HOL_TM_CONST: return 1;
        case HOL_TM_ABS: {
            if (hk_vec_push(k, acc, tm->u.node.l) < 0) return -1;
            int r = freesin_rec(k, acc, tm->u.node.r);
            acc->n--;
            return r;
        }
        default: {
            int r = freesin_rec(k, acc, tm->u.node.l);
            if (r <= 0) return r;
            tm = tm->u.node.r;
        }
        }
    }
}

int hk_freesin(HolKernel *k, HolTerm *const *acc, size_t n, HolTerm *tm) {
    if (n == 0 && tm->fv_sig == 0) return 1;
    HolVec v = {0};
    for (size_t i = 0; i < n; i++)
        if (hk_vec_push(k, &v, acc[i]) < 0) { hk_vec_free(k, &v); return -1; }
    int r = freesin_rec(k, &v, tm);
    hk_vec_free(k, &v);
    return r;
}

static bool vec_mem(const HolVec *v, const void *p) {
    for (size_t i = 0; i < v->n; i++)
        if (v->v[i] == p) return true;
    return false;
}

/* let union l1 l2 = itlist insert l1 l2  (insert x l = if mem x l then l else x::l)
 * Result: elements of l1 absent from l2 (later duplicates of l1 win), then l2. */
static int vec_union_into(HolKernel *k, const HolVec *l1, HolVec *l2) {
    HolVec out = {0};
    for (size_t i = 0; i < l1->n; i++) {
        void *x = l1->v[i];
        bool later = false;
        for (size_t j = i + 1; j < l1->n && !later; j++) later = l1->v[j] == x;
        if (later || vec_mem(l2, x)) continue;
        if (hk_vec_push(k, &out, x) < 0) { hk_vec_free(k, &out); return -1; }
    }
    for (size_t i = 0; i < l2->n; i++)
        if (hk_vec_push(k, &out, l2->v[i]) < 0) { hk_vec_free(k, &out); return -1; }
    hk_vec_free(k, l2);
    *l2 = out;
    return 0;
}

/* let rec frees tm =
 *   match tm with
 *     Var(_,_) -> [tm]
 *   | Const(_,_) -> []
 *   | Abs(bv,bod) -> subtract (frees bod) [bv]
 *   | Comb(s,t) -> union (frees s) (frees t)                               */
int hk_frees_exact(HolKernel *k, HolTerm *tm, HolVec *out) {
    if (!hk_tick(k)) return -1;
    switch (tm->kind) {
    case HOL_TM_VAR: return hk_vec_push(k, out, tm);
    case HOL_TM_CONST: return 0;
    case HOL_TM_ABS: {
        HolVec b = {0};
        if (hk_frees_exact(k, tm->u.node.r, &b) < 0) { hk_vec_free(k, &b); return -1; }
        for (size_t i = 0; i < b.n; i++)
            if (b.v[i] != tm->u.node.l && hk_vec_push(k, out, b.v[i]) < 0) {
                hk_vec_free(k, &b);
                return -1;
            }
        hk_vec_free(k, &b);
        return 0;
    }
    default: {
        HolVec s = {0}, t = {0};
        int r = -1;
        if (hk_frees_exact(k, tm->u.node.l, &s) == 0 &&
            hk_frees_exact(k, tm->u.node.r, &t) == 0 && vec_union_into(k, &s, &t) == 0) {
            r = 0;
            for (size_t i = 0; i < t.n && r == 0; i++) r = hk_vec_push(k, out, t.v[i]);
        }
        hk_vec_free(k, &s);
        hk_vec_free(k, &t);
        return r;
    }
    }
}

/* let rec tyvars = function
 *     (Tyapp(_,args)) -> itlist (union o tyvars) args []
 *   | (Tyvar v as tv) -> [tv]                                              */
int hk_tyvars_exact(HolKernel *k, HolType *ty, HolVec *out) {
    if (ty->kind == HOL_TY_VAR) return hk_vec_push(k, out, ty);
    if (!hk_tick(k)) return -1;
    HolVec acc = {0};
    for (uint32_t i = ty->nargs; i-- > 0;) {
        HolVec a = {0};
        if (hk_tyvars_exact(k, ty->args[i], &a) < 0 || vec_union_into(k, &a, &acc) < 0) {
            hk_vec_free(k, &a);
            hk_vec_free(k, &acc);
            return -1;
        }
        hk_vec_free(k, &a);
    }
    int r = 0;
    for (size_t i = 0; i < acc.n && r == 0; i++) r = hk_vec_push(k, out, acc.v[i]);
    hk_vec_free(k, &acc);
    return r;
}

/* let rec type_vars_in_term tm =
 *   match tm with
 *     Var(_,ty) -> tyvars ty | Const(_,ty) -> tyvars ty
 *   | Comb(s,t) -> union (type_vars_in_term s) (type_vars_in_term t)
 *   | Abs(Var(_,ty),t) -> union (tyvars ty) (type_vars_in_term t)         */
int hk_type_vars_in_term_exact(HolKernel *k, HolTerm *tm, HolVec *out) {
    if (!hk_tick(k)) return -1;
    if (tm->kind == HOL_TM_VAR || tm->kind == HOL_TM_CONST)
        return hk_tyvars_exact(k, tm->type, out);
    HolVec s = {0}, t = {0};
    int r = -1;
    int rs = tm->kind == HOL_TM_ABS ? hk_tyvars_exact(k, tm->u.node.l->type, &s)
                                    : hk_type_vars_in_term_exact(k, tm->u.node.l, &s);
    if (rs == 0 && hk_type_vars_in_term_exact(k, tm->u.node.r, &t) == 0 &&
        vec_union_into(k, &s, &t) == 0) {
        r = 0;
        for (size_t i = 0; i < t.n && r == 0; i++) r = hk_vec_push(k, out, t.v[i]);
    }
    hk_vec_free(k, &s);
    hk_vec_free(k, &t);
    return r;
}

/* let rec variant avoid v =
 *   if not(exists (vfree_in v) avoid) then v else
 *   match v with
 *     Var(s,ty) -> variant avoid (Var(s^"'",ty))
 *   | _ -> failwith "variant: not a variable"                              */
HolTerm *hk_variant(HolKernel *k, HolTerm *const *avoid, size_t n, HolTerm *v) {
    for (;;) {
        bool hit = false;
        for (size_t i = 0; i < n && !hit; i++) {
            int r = hk_vfree_in(k, v, avoid[i]);
            if (r < 0) return NULL;
            hit = r != 0;
        }
        if (!hit) return v;
        if (v->kind != HOL_TM_VAR) return hk_fail(k, HOL_REFUSED, HOL_R_VARIANT_NOT_VAR);
        HolName n2 = hk_name_prime(k, v->u.leaf.name);
        if (!n2) return NULL;
        v = hk_tm_var(k, n2, v->type);
        if (!v) return NULL;
    }
}

/* ------------------------------------------------------------------------ */
/* Scratch stack for substitution lists                                      */
/* ------------------------------------------------------------------------ */

typedef struct {
    const HolTmPair *p;
    size_t n;
    uint64_t dom_sig;
    uint32_t ctx;
} VsList;

static uint64_t vs_dom_sig(const HolTmPair *p, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s |= p[i].x->fv_sig;
    return s;
}

/* let rec vsubst ilist tm =
 *   match tm with
 *     Var(_,_) -> rev_assocd tm ilist tm
 *   | Const(_,_) -> tm
 *   | Comb(s,t) -> let s' = vsubst ilist s and t' = vsubst ilist t in
 *                  if s' == s && t' == t then tm else Comb(s',t')
 *   | Abs(v,s) -> let ilist' = filter (fun (t,x) -> x <> v) ilist in
 *                 if ilist' = [] then tm else
 *                 let s' = vsubst ilist' s in
 *                 if s' == s then tm else
 *                 if exists (fun (t,x) -> vfree_in v t && vfree_in x s) ilist'
 *                 then let v' = variant [s'] v in
 *                      Abs(v',vsubst ((v',v)::ilist') s)
 *                 else Abs(v,s')
 * The first test below is exact: when no substituted variable can be free
 * in tm, every branch above returns tm itself. */
static HolTerm *vs_rec(HolKernel *k, const VsList *il, HolTerm *tm) {
    if (!(tm->fv_sig & il->dom_sig)) return tm;
    switch (tm->kind) {
    case HOL_TM_VAR:
        for (size_t i = 0; i < il->n; i++)
            if (il->p[i].x == tm) return il->p[i].t;
        return tm;
    case HOL_TM_CONST: return tm;
    case HOL_TM_COMB: {
        if (!hk_tick(k)) return NULL;
        bool shared = (tm->flags & HOL_TMF_SHARED) != 0;
        void *mv;
        uint32_t mtag;
        if (shared && memo_get(k, tm, il->ctx, &mv, &mtag)) return (HolTerm *)mv;
        HolTerm *s = vs_rec(k, il, tm->u.node.l);
        if (!s) return NULL;
        HolTerm *t = vs_rec(k, il, tm->u.node.r);
        if (!t) return NULL;
        HolTerm *res = (s == tm->u.node.l && t == tm->u.node.r)
                           ? tm : node_term(k, HOL_TM_COMB, s, t, tm->type);
        if (res && shared) memo_put(k, tm, il->ctx, res, 2);
        return res;
    }
    default: {
        if (!hk_tick(k)) return NULL;
        bool shared = (tm->flags & HOL_TMF_SHARED) != 0;
        void *mv;
        uint32_t mtag;
        if (shared && memo_get(k, tm, il->ctx, &mv, &mtag)) return (HolTerm *)mv;
        HolTerm *v = tm->u.node.l, *s = tm->u.node.r;
        size_t m = 0;
        for (size_t i = 0; i < il->n; i++)
            if (il->p[i].x != v) m++;
        if (m == 0) return tm;
        VsList il2 = *il;
        HolTmPair *buf = NULL;
        size_t bufn = 0;
        HolTerm *res = NULL;
        if (m != il->n) {
            bufn = m + 1;
            buf = (HolTmPair *)hk_alloc(k, bufn * sizeof(HolTmPair));
            if (!buf) return NULL;
            size_t j = 1; /* slot 0 reserved for a possible (v', v) */
            for (size_t i = 0; i < il->n; i++)
                if (il->p[i].x != v) buf[j++] = il->p[i];
            il2.p = buf + 1;
            il2.n = m;
            il2.dom_sig = vs_dom_sig(il2.p, m);
            il2.ctx = hk_memo_ctx(k);
        }
        HolTerm *s1 = vs_rec(k, &il2, s);
        if (!s1) goto out;
        if (s1 == s) { res = tm; goto out; }
        bool capture = false;
        for (size_t i = 0; i < il2.n && !capture; i++) {
            int r = hk_vfree_in(k, v, il2.p[i].t);
            if (r < 0) goto out;
            if (r) {
                r = hk_vfree_in(k, il2.p[i].x, s);
                if (r < 0) goto out;
                capture = r != 0;
            }
        }
        if (!capture) {
            res = node_term(k, HOL_TM_ABS, v, s1, tm->type);
            goto out;
        }
        {
            HolTerm *avoid[1] = {s1};
            HolTerm *v1 = hk_variant(k, avoid, 1, v);
            if (!v1) goto out;
            k->renames_vsubst++;
            if (!buf) {
                bufn = il->n + 1;
                buf = (HolTmPair *)hk_alloc(k, bufn * sizeof(HolTmPair));
                if (!buf) goto out;
                for (size_t i = 0; i < il->n; i++) buf[i + 1] = il->p[i];
            }
            buf[0].t = v1;
            buf[0].x = v;
            VsList il3;
            il3.p = buf;
            il3.n = il2.n + 1;
            il3.dom_sig = il2.dom_sig | v->fv_sig;
            il3.ctx = hk_memo_ctx(k);
            HolTerm *s2 = vs_rec(k, &il3, s);
            if (!s2) goto out;
            res = node_term(k, HOL_TM_ABS, v1, s2, tm->type);
        }
    out:
        if (buf) hk_dealloc(k, buf, bufn * sizeof(HolTmPair));
        if (res && shared) memo_put(k, tm, il->ctx, res, 2);
        return res;
    }
    }
}

/* fun theta ->
 *   if theta = [] then (fun tm -> tm) else
 *   if forall (function (t,Var(_,y)) -> compare (type_of t) y = 0
 *                     | _ -> false) theta
 *   then vsubst theta else failwith "vsubst: Bad substitution list"        */
int hk_vsubst_check(HolKernel *k, const HolTmPair *p, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (p[i].x->kind != HOL_TM_VAR || p[i].t->type != p[i].x->type) {
            hk_fail(k, HOL_REFUSED, HOL_R_VSUBST_BAD);
            return -1;
        }
    return 0;
}

HolTerm *hk_vsubst(HolKernel *k, const HolTmPair *p, size_t n, HolTerm *tm) {
    if (n == 0) return tm;
    if (hk_vsubst_check(k, p, n) < 0) return NULL;
    for (size_t i = 0; i < n; i++) {
        HolDecl *a;
        if (join_or_fail(k, p[i].t->anchor, tm->anchor, &a) < 0) return NULL;
    }
    VsList il = {p, n, vs_dom_sig(p, n), hk_memo_ctx(k)};
    return vs_rec(k, &il, tm);
}

/* ------------------------------------------------------------------------ */
/* Type instantiation                                                        */
/* ------------------------------------------------------------------------ */

typedef struct { HolTerm *y, *yp; } InstPair;
typedef struct {
    HolTySubst ts;
    uint32_t tctx;
    InstPair *env;
    size_t n, cap;
    size_t changed;
    uint64_t changed_sig;
    uint32_t ctx;
} InstCtx;

enum { INST_OK = 0, INST_CLASH = 1, INST_ERR = -1 };

static HolType *inst_ty(HolKernel *k, InstCtx *c, HolType *ty) {
    if ((ty->tv_sig & c->ts.dom_sig) == 0) return ty;
    return type_subst_rec(k, &c->ts, ty, c->tctx);
}

/* let rec inst env tyin tm =
 *   match tm with
 *     Var(n,ty)   -> let ty' = type_subst tyin ty in
 *                    let tm' = if ty' == ty then tm else Var(n,ty') in
 *                    if compare (rev_assocd tm' env tm) tm = 0
 *                    then tm' else raise (Clash tm')
 *   | Const(c,ty) -> let ty' = type_subst tyin ty in
 *                    if ty' == ty then tm else Const(c,ty')
 *   | Comb(f,x)   -> let f' = inst env tyin f and x' = inst env tyin x in
 *                    if f' == f && x' == x then tm else Comb(f',x')
 *   | Abs(y,t)    -> let y' = inst [] tyin y in
 *                    let env' = (y,y')::env in
 *                    try let t' = inst env' tyin t in
 *                        if y' == y && t' == t then tm else Abs(y',t')
 *                    with (Clash(w') as ex) ->
 *                    if w' <> y' then raise ex else
 *                    let ifrees = map (inst [] tyin) (frees t) in
 *                    let y'' = variant ifrees y' in
 *                    let z = Var(fst(dest_var y''),snd(dest_var y)) in
 *                    inst env tyin (Abs(z,vsubst[z,y] t))
 * OCaml evaluates "let ... and ..." left to right (checked), so f is
 * instantiated before x and its Clash, if any, propagates first.
 * The first test below is exact: with no instantiated type variable in tm,
 * all types are unchanged, and a clash needs a free variable of tm equal to
 * the image of a binder whose type changed. */
static int inst_rec(HolKernel *k, InstCtx *c, HolTerm *tm, HolTerm **out,
                    HolTerm **clash) {
    if (!(tm->tv_sig & c->ts.dom_sig) &&
        (c->changed == 0 || !(tm->fv_sig & c->changed_sig))) {
        *out = tm;
        return INST_OK;
    }
    if (!hk_tick(k)) return INST_ERR;
    switch (tm->kind) {
    case HOL_TM_VAR: {
        HolType *ty = inst_ty(k, c, tm->type);
        if (!ty) return INST_ERR;
        HolTerm *tm1 = ty == tm->type ? tm : hk_tm_var(k, tm->u.leaf.name, ty);
        if (!tm1) return INST_ERR;
        HolTerm *r = tm;
        for (size_t i = c->n; i-- > 0;)
            if (c->env[i].yp == tm1) { r = c->env[i].y; break; }
        if (r == tm) { *out = tm1; return INST_OK; }
        *clash = tm1;
        return INST_CLASH;
    }
    case HOL_TM_CONST: {
        HolType *ty = inst_ty(k, c, tm->type);
        if (!ty) return INST_ERR;
        *out = ty == tm->type ? tm : hk_tm_const(k, tm->u.leaf.decl, ty);
        return *out ? INST_OK : INST_ERR;
    }
    case HOL_TM_COMB: {
        bool shared = (tm->flags & HOL_TMF_SHARED) != 0;
        void *mv;
        uint32_t mtag;
        if (shared && memo_get(k, tm, c->ctx, &mv, &mtag)) {
            if (mtag == 2) { *out = (HolTerm *)mv; return INST_OK; }
            *clash = (HolTerm *)mv;
            return INST_CLASH;
        }
        HolTerm *f1, *x1;
        int rc = inst_rec(k, c, tm->u.node.l, &f1, clash);
        if (rc == INST_OK) rc = inst_rec(k, c, tm->u.node.r, &x1, clash);
        if (rc == INST_CLASH) {
            if (shared) memo_put(k, tm, c->ctx, *clash, 3);
            return rc;
        }
        if (rc != INST_OK) return rc;
        HolTerm *res = (f1 == tm->u.node.l && x1 == tm->u.node.r) ? tm : hk_tm_comb(k, f1, x1);
        if (!res) return INST_ERR;
        if (shared) memo_put(k, tm, c->ctx, res, 2);
        *out = res;
        return INST_OK;
    }
    default: {
        bool shared = (tm->flags & HOL_TMF_SHARED) != 0;
        void *mv;
        uint32_t mtag;
        if (shared && memo_get(k, tm, c->ctx, &mv, &mtag)) {
            if (mtag == 2) { *out = (HolTerm *)mv; return INST_OK; }
            *clash = (HolTerm *)mv;
            return INST_CLASH;
        }
        HolTerm *y = tm->u.node.l, *t = tm->u.node.r;
        HolType *yty = inst_ty(k, c, y->type);
        if (!yty) return INST_ERR;
        HolTerm *y1 = yty == y->type ? y : hk_tm_var(k, y->u.leaf.name, yty);
        if (!y1) return INST_ERR;
        if (c->n == c->cap) {
            size_t ncap = c->cap ? c->cap * 2 : 32;
            InstPair *ne = (InstPair *)hk_realloc(k, c->env, c->cap * sizeof(InstPair),
                                                   ncap * sizeof(InstPair));
            if (!ne) return INST_ERR;
            c->env = ne;
            c->cap = ncap;
        }
        uint32_t saved_ctx = c->ctx;
        size_t saved_changed = c->changed;
        uint64_t saved_sig = c->changed_sig;
        c->env[c->n].y = y;
        c->env[c->n].yp = y1;
        c->n++;
        if (y1 != y) { c->changed++; c->changed_sig |= y1->fv_sig; }
        c->ctx = hk_memo_ctx(k);
        HolTerm *t1, *w;
        int rc = inst_rec(k, c, t, &t1, &w);
        c->n--;
        c->ctx = saved_ctx;
        c->changed = saved_changed;
        c->changed_sig = saved_sig;
        if (rc == INST_OK) {
            HolTerm *res = (y1 == y && t1 == t) ? tm : NULL;
            if (!res) {
                HolType *aty = hk_ty_fun(k, y1->type, t1->type);
                if (!aty) return INST_ERR;
                res = node_term(k, HOL_TM_ABS, y1, t1, aty);
                if (!res) return INST_ERR;
            }
            if (shared) memo_put(k, tm, c->ctx, res, 2);
            *out = res;
            return INST_OK;
        }
        if (rc == INST_ERR) return INST_ERR;
        if (w != y1) {
            if (shared) memo_put(k, tm, c->ctx, w, 3);
            *clash = w;
            return INST_CLASH;
        }
        /* Clash on this binder: rename it apart from the instantiated frees. */
        k->renames_inst++;
        HolVec fr = {0};
        if (hk_frees_exact(k, t, &fr) < 0) { hk_vec_free(k, &fr); return INST_ERR; }
        for (size_t i = 0; i < fr.n; i++) {
            HolTerm *fv = (HolTerm *)fr.v[i];
            HolType *fty = inst_ty(k, c, fv->type);
            HolTerm *fi = fty ? (fty == fv->type ? fv : hk_tm_var(k, fv->u.leaf.name, fty)) : NULL;
            if (!fi) { hk_vec_free(k, &fr); return INST_ERR; }
            fr.v[i] = fi;
        }
        HolTerm *y2 = hk_variant(k, (HolTerm *const *)fr.v, fr.n, y1);
        hk_vec_free(k, &fr);
        if (!y2) return INST_ERR;
        HolTerm *z = hk_tm_var(k, y2->u.leaf.name, y->type);
        if (!z) return INST_ERR;
        HolTmPair pr = {z, y};
        VsList il = {&pr, 1, y->fv_sig, hk_memo_ctx(k)};
        HolTerm *body = vs_rec(k, &il, t);
        if (!body) return INST_ERR;
        HolTerm *renamed = node_term(k, HOL_TM_ABS, z, body, tm->type);
        if (!renamed) return INST_ERR;
        int rc2 = inst_rec(k, c, renamed, out, clash);
        if (shared && rc2 == INST_OK) memo_put(k, tm, c->ctx, *out, 2);
        else if (shared && rc2 == INST_CLASH) memo_put(k, tm, c->ctx, *clash, 3);
        return rc2;
    }
    }
}

/* fun tyin -> if tyin = [] then fun tm -> tm else inst [] tyin */
HolTerm *hk_inst(HolKernel *k, const HolTyPair *p, size_t n, HolTerm *tm) {
    if (n == 0) return tm;
    for (size_t i = 0; i < n; i++) {
        HolDecl *a;
        if (join_or_fail(k, p[i].repl->anchor, tm->anchor, &a) < 0) return NULL;
    }
    InstCtx c;
    memset(&c, 0, sizeof c);
    hk_tysubst_init(&c.ts, p, n);
    c.tctx = hk_memo_ctx(k);
    c.ctx = hk_memo_ctx(k);
    HolTerm *out = NULL, *clash = NULL;
    int rc = inst_rec(k, &c, tm, &out, &clash);
    hk_dealloc(k, c.env, c.cap * sizeof(InstPair));
    if (rc == INST_OK) return out;
    if (rc == INST_CLASH) {
        /* Unreachable: every Clash names the image of an enclosing binder,
         * whose frame handles it.  Report rather than trust. */
        fprintf(stderr, "hol: internal error: Clash escaped inst\n");
        abort();
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* Axiom dependency sets                                                     */
/* ------------------------------------------------------------------------ */

static HolDeps *deps_intern(HolKernel *k, HolDecl *const *ax, uint32_t n) {
    if (n == 0) return NULL;
    uint64_t hh = HOL_SEED_DEPS;
    for (uint32_t i = 0; i < n; i++) hh = hk_mix64(hh ^ ax[i]->serial);
    uint32_t h = hk_hash_finish(k, hh);
    HolTable *t = &k->deps;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolDeps *o = (HolDeps *)t->slot[j];
            if (!o) break;
            if (o->hash != h || o->n != n) continue;
            uint32_t i = 0;
            while (i < n && o->ax[i] == ax[i]) i++;
            if (i == n) return o;
        }
    }
    size_t size = sizeof(HolDeps) + (size_t)n * sizeof(HolDecl *);
    HolDeps *d = (HolDeps *)hk_alloc(k, size);
    if (!d) return NULL;
    memset(d, 0, sizeof *d);
    d->hash = h;
    d->n = n;
    for (uint32_t i = 0; i < n; i++) d->ax[i] = ax[i];
    if (hk_table_insert(k, t, d, h) < 0) {
        hk_dealloc(k, d, size);
        return NULL;
    }
    return d;
}

HolDeps *hk_deps_single(HolKernel *k, HolDecl *ax) { return deps_intern(k, &ax, 1); }

/* Returns the union; on allocation failure returns NULL with status set
 * (callers distinguish "empty" from failure by checking the status). */
HolDeps *hk_deps_union(HolKernel *k, HolDeps *a, HolDeps *b) {
    if (!a || a == b) return b;
    if (!b) return a;
    HolDecl *small[16];
    HolDecl **buf = small;
    size_t cap = (size_t)a->n + b->n;
    if (cap > 16) {
        buf = (HolDecl **)hk_alloc(k, cap * sizeof(HolDecl *));
        if (!buf) return NULL;
    }
    uint32_t i = 0, j = 0, n = 0;
    while (i < a->n && j < b->n) {
        if (a->ax[i] == b->ax[j]) { buf[n++] = a->ax[i]; i++; j++; }
        else if (a->ax[i]->serial < b->ax[j]->serial) buf[n++] = a->ax[i++];
        else buf[n++] = b->ax[j++];
    }
    while (i < a->n) buf[n++] = a->ax[i++];
    while (j < b->n) buf[n++] = b->ax[j++];
    HolDeps *d = n == a->n ? a : (n == b->n ? b : deps_intern(k, buf, n));
    if (buf != small) hk_dealloc(k, buf, cap * sizeof(HolDecl *));
    return d;
}

/* ------------------------------------------------------------------------ */
/* Theorem payloads                                                          */
/* ------------------------------------------------------------------------ */

HolThm *hk_thm_make(HolKernel *k, HolTerm *const *hyps, uint32_t nhyps,
                     HolTerm *concl, HolDeps *deps, HolDecl *anchor) {
    /* Sequential mixing: the conclusion is mixed before any hypothesis is
     * combined, so that {p} |- p and similar sequents do not cancel out. */
    uint64_t hh = hk_mix64(HOL_SEED_THM ^ concl->hash);
    for (uint32_t i = 0; i < nhyps; i++) hh = hk_mix64(hh ^ hyps[i]->hash);
    hh = hk_mix64(hh ^ (deps ? deps->hash : 0));
    hh ^= anchor ? anchor->serial : 0;
    uint32_t h = hk_hash_finish(k, hh);
    HolTable *t = &k->thms;
    if (t->cap) {
        size_t mask = t->cap - 1;
        for (size_t j = h & mask;; j = (j + 1) & mask) {
            HolThm *o = (HolThm *)t->slot[j];
            if (!o) break;
            if (o->hash != h || o->concl != concl || o->nhyps != nhyps || o->deps != deps ||
                o->anchor != anchor)
                continue;
            uint32_t i = 0;
            while (i < nhyps && o->hyps[i] == hyps[i]) i++;
            if (i == nhyps) {
                k->intern_hits++;
                return o;
            }
        }
    }
    k->intern_misses++;
    size_t size = sizeof(HolThm) + (size_t)nhyps * sizeof(HolTerm *);
    HolThm *th = (HolThm *)hk_alloc(k, size);
    if (!th) return NULL;
    memset(th, 0, sizeof *th);
    th->hash = h;
    th->nhyps = nhyps;
    th->concl = concl;
    th->deps = deps;
    th->anchor = anchor;
    for (uint32_t i = 0; i < nhyps; i++) th->hyps[i] = hyps[i];
    if (hk_table_insert(k, t, th, h) < 0) {
        hk_dealloc(k, th, size);
        return NULL;
    }
    return th;
}

/* let rec term_union l1 l2 =
 *   match (l1,l2) with
 *     ([],l2) -> l2 | (l1,[]) -> l1
 *   | (h1::t1,h2::t2) -> let c = alphaorder h1 h2 in
 *                        if c = 0 then h1::(term_union t1 t2)
 *                        else if c < 0 then h1::(term_union t1 l2)
 *                        else h2::(term_union l1 t2)                       */
static int term_union(HolKernel *k, HolTerm *const *a, size_t na, HolTerm *const *b,
                      size_t nb, HolTerm **out, size_t *nout) {
    size_t i = 0, j = 0, o = 0;
    while (i < na && j < nb) {
        int c;
        if (hk_alphaorder(k, a[i], b[j], &c) < 0) return -1;
        if (c == 0) { out[o++] = a[i]; i++; j++; }
        else if (c < 0) out[o++] = a[i++];
        else out[o++] = b[j++];
    }
    while (i < na) out[o++] = a[i++];
    while (j < nb) out[o++] = b[j++];
    *nout = o;
    return 0;
}

/* let rec term_remove t l =
 *   match l with
 *     s::ss -> let c = alphaorder t s in
 *              if c > 0 then let ss' = term_remove t ss in
 *                            if ss' == ss then l else s::ss'
 *              else if c = 0 then ss else l
 *   | [] -> l                                                              */
static int term_remove(HolKernel *k, HolTerm *t, HolTerm *const *l, size_t n,
                       HolTerm **out, size_t *nout) {
    size_t i = 0, o = 0;
    for (; i < n; i++) {
        int c;
        if (hk_alphaorder(k, t, l[i], &c) < 0) return -1;
        if (c > 0) { out[o++] = l[i]; continue; }
        if (c == 0) i++;
        break;
    }
    for (; i < n; i++) out[o++] = l[i];
    *nout = o;
    return 0;
}

typedef HolTerm *(*TermMap)(HolKernel *k, void *ctx, HolTerm *t);

/* let rec term_image f l =
 *   match l with
 *     h::t -> let h' = f h and t' = term_image f t in
 *             if h' == h && t' == t then l else term_union [h'] t'
 *   | [] -> l                                                              */
static int term_image(HolKernel *k, TermMap f, void *ctx, HolTerm *const *l, size_t n,
                      HolTerm ***out, size_t *nout, size_t *outcap) {
    HolTerm **acc = NULL, **tmp = NULL;
    size_t cap = n ? n : 1, na = 0;
    bool unchanged = true; /* acc equals l[i+1..] */
    acc = (HolTerm **)hk_alloc(k, cap * sizeof(HolTerm *));
    tmp = (HolTerm **)hk_alloc(k, cap * sizeof(HolTerm *));
    if (!acc || !tmp) goto fail;
    HolTerm **images = (HolTerm **)hk_alloc(k, cap * sizeof(HolTerm *));
    if (!images) goto fail;
    for (size_t i = 0; i < n; i++) { /* left to right, as OCaml evaluates */
        images[i] = f(k, ctx, l[i]);
        if (!images[i]) { hk_dealloc(k, images, cap * sizeof(HolTerm *)); goto fail; }
    }
    for (size_t i = n; i-- > 0;) {
        HolTerm *h1 = images[i];
        if (h1 == l[i] && unchanged) continue;
        if (unchanged) { /* materialize the unchanged suffix */
            na = 0;
            for (size_t j = i + 1; j < n; j++) acc[na++] = l[j];
            unchanged = false;
        }
        size_t nn;
        if (term_union(k, &h1, 1, acc, na, tmp, &nn) < 0) {
            hk_dealloc(k, images, cap * sizeof(HolTerm *));
            goto fail;
        }
        HolTerm **sw = acc; acc = tmp; tmp = sw;
        na = nn;
    }
    hk_dealloc(k, images, cap * sizeof(HolTerm *));
    if (unchanged) {
        for (size_t j = 0; j < n; j++) acc[j] = l[j];
        na = n;
    }
    hk_dealloc(k, tmp, cap * sizeof(HolTerm *));
    *out = acc;
    *nout = na;
    *outcap = cap;
    return 0;
fail:
    hk_dealloc(k, acc, cap * sizeof(HolTerm *));
    hk_dealloc(k, tmp, cap * sizeof(HolTerm *));
    return -1;
}

/* ------------------------------------------------------------------------ */
/* The ten primitive rules                                                   */
/* ------------------------------------------------------------------------ */

/* let REFL tm = Sequent([],safe_mk_eq tm tm) */
HolThm *hk_REFL(HolKernel *k, HolTerm *tm) {
    HolTerm *eq = hk_tm_eq(k, tm, tm);
    if (!eq) return NULL;
    return hk_thm_make(k, NULL, 0, eq, NULL, tm->anchor);
}

static int merge_hyps(HolKernel *k, const HolThm *a, const HolThm *b, HolTerm ***out,
                      size_t *n, size_t *cap) {
    *cap = (size_t)a->nhyps + b->nhyps;
    *out = (HolTerm **)hk_alloc(k, (*cap ? *cap : 1) * sizeof(HolTerm *));
    if (!*out) return -1;
    if (term_union(k, a->hyps, a->nhyps, b->hyps, b->nhyps, *out, n) < 0) {
        hk_dealloc(k, *out, (*cap ? *cap : 1) * sizeof(HolTerm *));
        return -1;
    }
    return 0;
}

static HolThm *binary_result(HolKernel *k, const HolThm *a, const HolThm *b,
                             HolTerm *concl) {
    HolDecl *anchor;
    if (join_or_fail(k, a->anchor, b->anchor, &anchor) < 0) return NULL;
    if (join_or_fail(k, anchor, concl->anchor, &anchor) < 0) return NULL;
    HolDeps *deps = hk_deps_union(k, a->deps, b->deps);
    if (HOL_ERR(k)) return NULL;
    HolTerm **hyps;
    size_t n, cap;
    if (merge_hyps(k, a, b, &hyps, &n, &cap) < 0) return NULL;
    HolThm *th = hk_thm_make(k, hyps, (uint32_t)n, concl, deps, anchor);
    hk_dealloc(k, hyps, (cap ? cap : 1) * sizeof(HolTerm *));
    return th;
}

/* let TRANS (Sequent(asl1,c1)) (Sequent(asl2,c2)) =
 *   match (c1,c2) with
 *     Comb((Comb(Const("=",_),_) as eql),m1),Comb(Comb(Const("=",_),m2),r)
 *       when alphaorder m1 m2 = 0 -> Sequent(term_union asl1 asl2,Comb(eql,r))
 *   | _ -> failwith "TRANS"                                                */
HolThm *hk_TRANS(HolKernel *k, HolThm *a, HolThm *b) {
    HolDecl *anchor;
    if (join_or_fail(k, a->anchor, b->anchor, &anchor) < 0) return NULL;
    if (!hk_tm_is_eq(k, a->concl) || !hk_tm_is_eq(k, b->concl))
        return hk_fail(k, HOL_REFUSED, HOL_R_TRANS);
    int c;
    if (hk_alphaorder(k, hk_eq_rhs(a->concl), hk_eq_lhs(b->concl), &c) < 0) return NULL;
    if (c != 0) return hk_fail(k, HOL_REFUSED, HOL_R_TRANS);
    HolTerm *concl = hk_tm_comb(k, a->concl->u.node.l, hk_eq_rhs(b->concl));
    if (!concl) return NULL;
    return binary_result(k, a, b, concl);
}

/* let MK_COMB(Sequent(asl1,c1),Sequent(asl2,c2)) =
 *    match (c1,c2) with
 *      Comb(Comb(Const("=",_),l1),r1),Comb(Comb(Const("=",_),l2),r2) ->
 *       (match type_of r1 with
 *          Tyapp("fun",[ty;_]) when compare ty (type_of r2) = 0
 *            -> Sequent(term_union asl1 asl2,
 *                       safe_mk_eq (Comb(l1,l2)) (Comb(r1,r2)))
 *        | _ -> failwith "MK_COMB: types do not agree")
 *    | _ -> failwith "MK_COMB: not both equations"                        */
HolThm *hk_MK_COMB(HolKernel *k, HolThm *a, HolThm *b) {
    HolDecl *anchor;
    if (join_or_fail(k, a->anchor, b->anchor, &anchor) < 0) return NULL;
    if (!hk_tm_is_eq(k, a->concl) || !hk_tm_is_eq(k, b->concl))
        return hk_fail(k, HOL_REFUSED, HOL_R_MK_COMB_NOT_EQS);
    HolTerm *l1 = hk_eq_lhs(a->concl), *r1 = hk_eq_rhs(a->concl);
    HolTerm *l2 = hk_eq_lhs(b->concl), *r2 = hk_eq_rhs(b->concl);
    if (!hk_ty_is_fun(k, r1->type) || r1->type->args[0] != r2->type)
        return hk_fail(k, HOL_REFUSED, HOL_R_MK_COMB_RULE_TYPES);
    HolTerm *lc = hk_tm_comb(k, l1, l2);
    if (!lc) return NULL;
    HolTerm *rc = hk_tm_comb(k, r1, r2);
    if (!rc) return NULL;
    HolTerm *eq = hk_tm_eq(k, lc, rc);
    if (!eq) return NULL;
    return binary_result(k, a, b, eq);
}

/* let ABS v (Sequent(asl,c)) =
 *   match (v,c) with
 *     Var(_,_),Comb(Comb(Const("=",_),l),r) when not(exists (vfree_in v) asl)
 *        -> Sequent(asl,safe_mk_eq (Abs(v,l)) (Abs(v,r)))
 *   | _ -> failwith "ABS"                                                  */
HolThm *hk_ABS(HolKernel *k, HolTerm *v, HolThm *th) {
    HolDecl *anchor;
    if (join_or_fail(k, v->anchor, th->anchor, &anchor) < 0) return NULL;
    if (v->kind != HOL_TM_VAR || !hk_tm_is_eq(k, th->concl))
        return hk_fail(k, HOL_REFUSED, HOL_R_ABS_NOT_VAR_OR_EQ);
    for (uint32_t i = 0; i < th->nhyps; i++) {
        int r = hk_vfree_in(k, v, th->hyps[i]);
        if (r < 0) return NULL;
        if (r) return hk_fail(k, HOL_REFUSED, HOL_R_ABS_FREE_IN_HYP);
    }
    HolTerm *la = hk_tm_abs(k, v, hk_eq_lhs(th->concl));
    if (!la) return NULL;
    HolTerm *ra = hk_tm_abs(k, v, hk_eq_rhs(th->concl));
    if (!ra) return NULL;
    HolTerm *eq = hk_tm_eq(k, la, ra);
    if (!eq) return NULL;
    return hk_thm_make(k, th->hyps, th->nhyps, eq, th->deps, anchor);
}

/* let BETA tm =
 *   match tm with
 *     Comb(Abs(v,bod),arg) when compare arg v = 0
 *       -> Sequent([],safe_mk_eq tm bod)
 *   | _ -> failwith "BETA: not a trivial beta-redex"                       */
HolThm *hk_BETA(HolKernel *k, HolTerm *tm) {
    if (tm->kind != HOL_TM_COMB || tm->u.node.l->kind != HOL_TM_ABS ||
        tm->u.node.r != tm->u.node.l->u.node.l)
        return hk_fail(k, HOL_REFUSED, HOL_R_BETA);
    HolTerm *eq = hk_tm_eq(k, tm, tm->u.node.l->u.node.r);
    if (!eq) return NULL;
    return hk_thm_make(k, NULL, 0, eq, NULL, tm->anchor);
}

/* let ASSUME tm =
 *   if compare (type_of tm) bool_ty = 0 then Sequent([tm],tm)
 *   else failwith "ASSUME: not a proposition"                              */
HolThm *hk_ASSUME(HolKernel *k, HolTerm *tm) {
    if (tm->type != k->bool_ty) return hk_fail(k, HOL_REFUSED, HOL_R_ASSUME_NOT_BOOL);
    return hk_thm_make(k, &tm, 1, tm, NULL, tm->anchor);
}

/* let EQ_MP (Sequent(asl1,eq)) (Sequent(asl2,c)) =
 *   match eq with
 *     Comb(Comb(Const("=",_),l),r) when alphaorder l c = 0
 *       -> Sequent(term_union asl1 asl2,r)
 *   | _ -> failwith "EQ_MP"                                                */
HolThm *hk_EQ_MP(HolKernel *k, HolThm *a, HolThm *b) {
    HolDecl *anchor;
    if (join_or_fail(k, a->anchor, b->anchor, &anchor) < 0) return NULL;
    if (!hk_tm_is_eq(k, a->concl)) return hk_fail(k, HOL_REFUSED, HOL_R_EQ_MP);
    int c;
    if (hk_alphaorder(k, hk_eq_lhs(a->concl), b->concl, &c) < 0) return NULL;
    if (c != 0) return hk_fail(k, HOL_REFUSED, HOL_R_EQ_MP);
    return binary_result(k, a, b, hk_eq_rhs(a->concl));
}

/* let DEDUCT_ANTISYM_RULE (Sequent(asl1,c1)) (Sequent(asl2,c2)) =
 *   let asl1' = term_remove c2 asl1 and asl2' = term_remove c1 asl2 in
 *   Sequent(term_union asl1' asl2',safe_mk_eq c1 c2)                       */
HolThm *hk_DEDUCT_ANTISYM_RULE(HolKernel *k, HolThm *a, HolThm *b) {
    HolDecl *anchor;
    if (join_or_fail(k, a->anchor, b->anchor, &anchor) < 0) return NULL;
    size_t cap = (size_t)a->nhyps + b->nhyps + 1;
    HolTerm **h1 = (HolTerm **)hk_alloc(k, cap * sizeof(HolTerm *));
    HolTerm **h2 = (HolTerm **)hk_alloc(k, cap * sizeof(HolTerm *));
    HolTerm **hu = (HolTerm **)hk_alloc(k, cap * sizeof(HolTerm *));
    HolThm *th = NULL;
    size_t n1, n2, nu;
    if (!h1 || !h2 || !hu) goto out;
    if (term_remove(k, b->concl, a->hyps, a->nhyps, h1, &n1) < 0) goto out;
    if (term_remove(k, a->concl, b->hyps, b->nhyps, h2, &n2) < 0) goto out;
    if (term_union(k, h1, n1, h2, n2, hu, &nu) < 0) goto out;
    {
        HolTerm *eq = hk_tm_eq(k, a->concl, b->concl);
        if (!eq) goto out;
        HolDeps *deps = hk_deps_union(k, a->deps, b->deps);
        if (HOL_ERR(k)) goto out;
        th = hk_thm_make(k, hu, (uint32_t)nu, eq, deps, anchor);
    }
out:
    hk_dealloc(k, h1, cap * sizeof(HolTerm *));
    hk_dealloc(k, h2, cap * sizeof(HolTerm *));
    hk_dealloc(k, hu, cap * sizeof(HolTerm *));
    return th;
}

typedef struct { const HolTyPair *p; size_t n; } InstTypeCtx;
static HolTerm *map_inst(HolKernel *k, void *ctx, HolTerm *t) {
    InstTypeCtx *c = (InstTypeCtx *)ctx;
    return hk_inst(k, c->p, c->n, t);
}

/* let INST_TYPE theta (Sequent(asl,c)) =
 *   let inst_fn = inst theta in
 *   Sequent(term_image inst_fn asl,inst_fn c)                              */
HolThm *hk_INST_TYPE(HolKernel *k, const HolTyPair *p, size_t n, HolThm *th) {
    HolDecl *anchor = th->anchor;
    for (size_t i = 0; i < n; i++) {
        if (join_or_fail(k, anchor, p[i].repl->anchor, &anchor) < 0) return NULL;
        if (join_or_fail(k, anchor, p[i].key->anchor, &anchor) < 0) return NULL;
    }
    if (n == 0) return th;
    InstTypeCtx c = {p, n};
    HolTerm **hyps;
    size_t nh, cap;
    if (term_image(k, map_inst, &c, th->hyps, th->nhyps, &hyps, &nh, &cap) < 0) return NULL;
    HolThm *res = NULL;
    HolTerm *concl = hk_inst(k, p, n, th->concl);
    if (concl) res = hk_thm_make(k, hyps, (uint32_t)nh, concl, th->deps, anchor);
    hk_dealloc(k, hyps, cap * sizeof(HolTerm *));
    return res;
}

typedef struct { const HolTmPair *p; size_t n; } VsubstCtx;
static HolTerm *map_vsubst(HolKernel *k, void *ctx, HolTerm *t) {
    VsubstCtx *c = (VsubstCtx *)ctx;
    VsList il = {c->p, c->n, vs_dom_sig(c->p, c->n), hk_memo_ctx(k)};
    return vs_rec(k, &il, t);
}

/* let INST theta (Sequent(asl,c)) =
 *   let inst_fun = vsubst theta in
 *   Sequent(term_image inst_fun asl,inst_fun c)
 * "vsubst theta" checks the substitution before touching the theorem. */
HolThm *hk_INST(HolKernel *k, const HolTmPair *p, size_t n, HolThm *th) {
    if (n == 0) return th;
    if (hk_vsubst_check(k, p, n) < 0) return NULL;
    HolDecl *anchor = th->anchor;
    for (size_t i = 0; i < n; i++) {
        if (join_or_fail(k, anchor, p[i].t->anchor, &anchor) < 0) return NULL;
        if (join_or_fail(k, anchor, p[i].x->anchor, &anchor) < 0) return NULL;
    }
    VsubstCtx c = {p, n};
    HolTerm **hyps;
    size_t nh, cap;
    if (term_image(k, map_vsubst, &c, th->hyps, th->nhyps, &hyps, &nh, &cap) < 0) return NULL;
    HolThm *res = NULL;
    HolTerm *concl = map_vsubst(k, &c, th->concl);
    if (concl) res = hk_thm_make(k, hyps, (uint32_t)nh, concl, th->deps, anchor);
    hk_dealloc(k, hyps, cap * sizeof(HolTerm *));
    return res;
}
