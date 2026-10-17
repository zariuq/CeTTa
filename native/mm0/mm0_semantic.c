/* mm0_semantic.c - checking MM0 declarations and proof DAGs given as data.
 *
 * Rule sources (pinned as in mm0_internal.h): mm0.md "Verification" and
 * "Definition checking"; Lean MM0/Kernel: Context.lean (binders),
 * Typing.lean (applications), AdmissibleSubstitution.lean (theorem
 * application), Conversion.lean, Unfolding.lean, FreshDummies.lean,
 * DeclarationAdmission.lean and BorrowedProofCursor.lean (rows). */
#include "mm0_semantic.h"
#include "mm0_internal.h"

#include <stdatomic.h>
#include <stdlib.h>

struct Mm0Node {
    const Mm0Decl *owner;    /* the builder whose table holds it */
    uint64_t hash;
    uint32_t head, nargs;
    uint8_t var, bound;
    uint32_t sort;
    uint64_t *vars;          /* V(e) over context positions (mm0.md "Vars") */
    Mm0Node *args[];
};

/* A conversion this builder checked; Mm0Conv names one by its index. */
typedef struct {
    Mm0Node *left, *right;
    uint32_t sort;
} ConvEntry;

typedef struct {
    uint32_t sort;
    uint8_t bound, dummy;
    uint32_t ndeps;
    uint32_t *dep_list;      /* positions of the bound binders it depends on */
    uint64_t *deps;          /* the same as a set, once the binders are complete */
    const char *name;
} CtxBinder;

typedef struct { const Mm0Expr *e; uint32_t next; } SubstFrame;
typedef struct { Mm0Node *n; uint32_t next; } NodeFrame;

typedef struct { const Mm0Expr *key; Mm0Node *value; } SubstSlot;

struct Mm0Decl {
    Mm0Theory *theory;
    uint64_t serial;         /* process-unique, never 0: the issuer of its conversions */
    Mm0Error error;
    Mm0Steps steps;
    Mm0Arena arena;
    CtxBinder *binders;
    size_t nbinders, cap_binders;
    uint32_t nordinary;      /* binders before the first dummy */
    uint32_t words;          /* words per variable set, fixed by the first expression */
    bool frozen;
    Mm0Node **vars_cache;
    Mm0Node **table;         /* intern table */
    size_t table_cap, table_count;
    Mm0Node **hyps;
    size_t nhyps, cap_hyps;
    Mm0Node *concl;
    Mm0Node **rows;
    size_t nrows, cap_rows;
    SubstSlot *subst;        /* per-instantiation memo */
    size_t subst_cap;
    SubstFrame *stack;
    size_t cap_stack;
    Mm0Node **scratch;
    size_t cap_scratch;
    ConvEntry *convs;        /* every conversion issued, in order */
    size_t nconvs, cap_convs;
};

static bool fail(Mm0Decl *d, Mm0Status status, const char *reason) {
    mm0_error_set(&d->error, status, reason);
    return false;
}
#define ENSURE(cond, reason) do { if (!(cond)) return fail(d, MM0_REFUSED, reason); } while (0)
#define ENSURE_SHAPE(cond, reason) do { if (!(cond)) return fail(d, MM0_MALFORMED, reason); } while (0)
#define CHECK(expr) do { if (!(expr)) return false; } while (0)
#define LIVE() do { if (d->error.status != MM0_OK) return false; } while (0)

static bool step(Mm0Decl *d) {
    Mm0Status status = mm0_step(&d->steps);
    if (status == MM0_OK) return true;
    return fail(d, status, status == MM0_LIMIT ? "step budget exhausted" : "interrupted");
}

/* `n` steps of promised traversal work. */
static bool steps(Mm0Decl *d, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i)
        if (!step(d)) return false;
    return true;
}

/* An expression of this builder.  A node of another builder would break
 * hash-consing (equality is identity) and its variable sets are sized by
 * that builder's binders. */
static bool owned(Mm0Decl *d, const Mm0Node *e) {
    if (!e) return fail(d, MM0_MALFORMED, "missing expression");
    if (e->owner != d) return fail(d, MM0_FOREIGN, "an expression of another builder");
    return true;
}

static atomic_uint_fast64_t g_decl_serial = 1;

Mm0Decl *mm0_decl_new(Mm0Theory *theory, const Mm0Options *options) {
    if (!theory) return NULL;
    Mm0Decl *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->theory = theory;
    d->serial = atomic_fetch_add(&g_decl_serial, 1);
    d->error = (Mm0Error){.status = MM0_OK, .offset = -1};
    mm0_steps_init(&d->steps, options);
    mm0_arena_init(&d->arena);
    return d;
}

void mm0_decl_free(Mm0Decl *d) {
    if (!d) return;
    mm0_arena_free(&d->arena);
    free(d->binders);
    free(d->vars_cache);
    free(d->table);
    free(d->hyps);
    free(d->rows);
    free(d->subst);
    free(d->stack);
    free(d->scratch);
    free(d->convs);
    free(d);
}

const Mm0Error *mm0_decl_error(const Mm0Decl *d) { return &d->error; }

Mm0Status mm0_decl_resume(Mm0Decl *d, const Mm0Options *options) {
    if (d->error.status != MM0_OK && d->error.status != MM0_LIMIT &&
        d->error.status != MM0_INTERRUPTED)
        return MM0_MALFORMED;
    d->error = (Mm0Error){.status = MM0_OK, .offset = -1};
    mm0_steps_init(&d->steps, options);
    return MM0_OK;
}
uint32_t mm0_decl_nbinders(const Mm0Decl *d) { return (uint32_t)d->nbinders; }
uint32_t mm0_node_sort(const Mm0Node *node) { return node->sort; }

static bool has_bit(const uint64_t *set, size_t i) { return (set[i / 64] >> (i % 64)) & 1; }
static void set_bit(uint64_t *set, size_t i) { set[i / 64] |= UINT64_C(1) << (i % 64); }

/* ---- binders (Context.lean AdmitsBinder) -------------------------------- */

static bool add_binder(Mm0Decl *d, const char *name, uint32_t sort, bool bound, bool dummy,
                       const uint32_t *deps, uint32_t ndeps) {
    LIVE();
    ENSURE_SHAPE(!d->frozen, "binders must precede expressions");
    ENSURE_SHAPE(d->nbinders < UINT32_MAX - 1, "too many binders");
    ENSURE(sort < d->theory->nsorts, "unknown sort");
    uint8_t mods = d->theory->sorts[sort].mods;
    if (bound) ENSURE(!(mods & MM0_SORT_STRICT), "bound variable in strict sort");
    if (dummy) ENSURE(!(mods & (MM0_SORT_STRICT | MM0_SORT_FREE)), "dummy variable in strict or free sort");
    if (!MM0_RESERVE(d->binders, d->cap_binders, d->nbinders + 1))
        return fail(d, MM0_NOMEM, "binder allocation");
    /* Dependencies are positions of earlier bound binders; they become a set
     * when the binder list is complete and the set size is known. */
    uint32_t *list = NULL;
    if (ndeps) {
        ENSURE(!bound, "bound variables should not have dependencies");
        list = mm0_arena_alloc(&d->arena, (size_t)ndeps * sizeof *list);
        if (!list) return fail(d, MM0_NOMEM, "binder allocation");
        for (uint32_t i = 0; i < ndeps; ++i) {
            ENSURE(deps[i] < d->nbinders && d->binders[deps[i]].bound, "expecting bound variable");
            list[i] = deps[i];
        }
    }
    char *kept = NULL;
    if (name && !(kept = mm0_arena_strndup(&d->arena, name, strlen(name))))
        return fail(d, MM0_NOMEM, "binder allocation");
    d->binders[d->nbinders] = (CtxBinder){sort, bound, dummy, ndeps, list, NULL, kept};
    if (!dummy) d->nordinary = (uint32_t)d->nbinders + 1;
    ++d->nbinders;
    return true;
}

Mm0Status mm0_decl_binder(Mm0Decl *d, const char *name, uint32_t sort, bool bound,
                          const uint32_t *deps, uint32_t ndeps) {
    if (d->nbinders > d->nordinary) {
        fail(d, MM0_MALFORMED, "binders must precede dummies");
        return d->error.status;
    }
    add_binder(d, name, sort, bound, false, deps, ndeps);
    return d->error.status;
}

Mm0Status mm0_decl_dummy(Mm0Decl *d, const char *name, uint32_t sort) {
    add_binder(d, name, sort, true, true, NULL, 0);
    return d->error.status;
}

/* The binder list is complete: size the variable sets and convert each
 * position list into a set. */
static bool freeze(Mm0Decl *d) {
    if (d->frozen) return true;
    LIVE();
    d->frozen = true;
    d->words = (uint32_t)(d->nbinders / 64 + 1);
    for (size_t i = 0; i < d->nbinders; ++i) {
        uint64_t *set = mm0_arena_alloc(&d->arena, d->words * sizeof *set);
        if (!set) return fail(d, MM0_NOMEM, "binder allocation");
        memset(set, 0, d->words * sizeof *set);
        if (d->binders[i].bound) set_bit(set, i);
        for (uint32_t k = 0; k < d->binders[i].ndeps; ++k) set_bit(set, d->binders[i].dep_list[k]);
        d->binders[i].deps = set;
    }
    d->vars_cache = calloc(d->nbinders ? d->nbinders : 1, sizeof *d->vars_cache);
    if (!d->vars_cache) return fail(d, MM0_NOMEM, "expression allocation");
    return true;
}

/* ---- interned expressions (Typing.lean) --------------------------------- */

static uint64_t mix(uint64_t h, uint64_t x) {
    h ^= x + UINT64_C(0x9e3779b97f4a7c15) + (h << 6) + (h >> 2);
    return h;
}

static uint64_t node_hash(uint8_t var, uint32_t head, Mm0Node *const *args, uint32_t nargs) {
    uint64_t h = mix(var ? UINT64_C(0xa5a5) : UINT64_C(0x5a5a), head);
    for (uint32_t i = 0; i < nargs; ++i) h = mix(h, (uint64_t)(uintptr_t)args[i]);
    return h;
}

static bool table_grow(Mm0Decl *d) {
    size_t cap = d->table_cap ? d->table_cap * 2 : 256;
    if (cap < d->table_cap || cap > SIZE_MAX / sizeof(Mm0Node *)) return fail(d, MM0_NOMEM, "expression table");
    Mm0Node **t = calloc(cap, sizeof *t);
    if (!t) return fail(d, MM0_NOMEM, "expression table");
    for (size_t i = 0; i < d->table_cap; ++i) {
        Mm0Node *n = d->table[i];
        if (!n) continue;
        size_t j = (size_t)n->hash & (cap - 1);
        while (t[j]) j = (j + 1) & (cap - 1);
        t[j] = n;
    }
    free(d->table);
    d->table = t;
    d->table_cap = cap;
    return true;
}

static Mm0Node *intern(Mm0Decl *d, uint8_t var, uint32_t head, Mm0Node *const *args,
                       uint32_t nargs, uint32_t sort, uint8_t bound) {
    if (!step(d)) return NULL;
    if ((d->table_count + 1) * 2 > d->table_cap && !table_grow(d)) return NULL;
    uint64_t h = node_hash(var, head, args, nargs);
    size_t i = (size_t)h & (d->table_cap - 1);
    for (Mm0Node *n; (n = d->table[i]); i = (i + 1) & (d->table_cap - 1)) {
        if (n->hash == h && n->var == var && n->head == head && n->nargs == nargs &&
            (nargs == 0 || memcmp(n->args, args, nargs * sizeof *args) == 0))
            return n;
    }
    Mm0Node *n = mm0_arena_alloc(&d->arena, sizeof *n + (size_t)nargs * sizeof *args);
    uint64_t *vars = mm0_arena_alloc(&d->arena, d->words * sizeof *vars);
    if (!n || !vars) { fail(d, MM0_NOMEM, "expression allocation"); return NULL; }
    *n = (Mm0Node){d, h, head, nargs, var, bound, sort, vars};
    if (nargs) memcpy(n->args, args, nargs * sizeof *args);
    if (var) {
        memcpy(vars, d->binders[head].deps, d->words * sizeof *vars);
    } else {
        /* mm0.md: Vars(foo e1 ... en) = U i, Si */
        memset(vars, 0, d->words * sizeof *vars);
        for (uint32_t k = 0; k < nargs; ++k)
            for (uint32_t w = 0; w < d->words; ++w) vars[w] |= args[k]->vars[w];
    }
    d->table[i] = n;
    ++d->table_count;
    return n;
}

Mm0Node *mm0_decl_var(Mm0Decl *d, uint32_t index) {
    if (d->error.status != MM0_OK || !freeze(d)) return NULL;
    if (index >= d->nbinders) { fail(d, MM0_MALFORMED, "unknown variable"); return NULL; }
    if (d->vars_cache[index]) return d->vars_cache[index];
    Mm0Node *n = intern(d, 1, index, NULL, 0, d->binders[index].sort, d->binders[index].bound);
    return d->vars_cache[index] = n;
}

/* An argument fits a binder: the same sort, and a bound variable for a
 * bound binder (mm0.md "Type checking"). */
static bool fits(const Mm0Binder *b, const Mm0Node *e) {
    return e->sort == b->sort && (!b->bound || (e->var && e->bound));
}

Mm0Node *mm0_decl_app(Mm0Decl *d, uint32_t term, Mm0Node *const *args, uint32_t nargs) {
    if (d->error.status != MM0_OK || !freeze(d)) return NULL;
    if (term >= d->theory->nterms) { fail(d, MM0_MALFORMED, "unknown term"); return NULL; }
    const Mm0TermDecl *t = &d->theory->terms[term];
    if (nargs != t->nargs) { fail(d, MM0_REFUSED, "wrong number of arguments"); return NULL; }
    for (uint32_t i = 0; i < nargs; ++i) {
        if (!args[i]) { fail(d, MM0_MALFORMED, "missing argument"); return NULL; }
        if (!owned(d, args[i])) return NULL;
        if (!fits(&t->args[i], args[i])) { fail(d, MM0_REFUSED, "type mismatch"); return NULL; }
    }
    return intern(d, 0, term, args, nargs, t->ret_sort, 0);
}

/* ---- statements --------------------------------------------------------- */

static bool mentions_dummy(const Mm0Decl *d, const Mm0Node *e) {
    for (size_t i = d->nordinary; i < d->nbinders; ++i)
        if (has_bit(e->vars, i)) return true;
    return false;
}

static bool statement(Mm0Decl *d, Mm0Node *e) {
    LIVE();
    ENSURE_SHAPE(e, "missing expression");
    CHECK(owned(d, e));
    ENSURE(e->sort < d->theory->nsorts && (d->theory->sorts[e->sort].mods & MM0_SORT_PROVABLE),
           "statement should have provable sort");
    ENSURE(!mentions_dummy(d, e), "a statement may not use proof dummies");
    return true;
}

Mm0Status mm0_decl_hyp(Mm0Decl *d, Mm0Node *hyp) {
    if (statement(d, hyp)) {
        if (d->concl) fail(d, MM0_MALFORMED, "hypotheses precede the conclusion");
        else if (!MM0_RESERVE(d->hyps, d->cap_hyps, d->nhyps + 1))
            fail(d, MM0_NOMEM, "hypothesis allocation");
        else d->hyps[d->nhyps++] = hyp;
    }
    return d->error.status;
}

Mm0Status mm0_decl_concl(Mm0Decl *d, Mm0Node *concl) {
    if (statement(d, concl)) {
        if (d->concl) fail(d, MM0_MALFORMED, "a statement has one conclusion");
        else d->concl = concl;
    }
    return d->error.status;
}

/* ---- substitution (Term.lean substitute; Proof.lean instantiate?) ------- */

static Mm0Node *subst_lookup(Mm0Decl *d, const Mm0Expr *e) {
    if (!d->subst_cap) return NULL;
    size_t mask = d->subst_cap - 1, i = ((uintptr_t)e >> 3) & mask;
    for (; d->subst[i].key; i = (i + 1) & mask)
        if (d->subst[i].key == e) return d->subst[i].value;
    return NULL;
}

static bool subst_put(Mm0Decl *d, const Mm0Expr *e, Mm0Node *value, size_t *count) {
    if ((*count + 1) * 2 > d->subst_cap) {
        size_t cap = d->subst_cap ? d->subst_cap * 2 : 64;
        SubstSlot *t = calloc(cap, sizeof *t);
        if (!t) return fail(d, MM0_NOMEM, "substitution allocation");
        for (size_t i = 0; i < d->subst_cap; ++i) {
            if (!d->subst[i].key) continue;
            size_t j = ((uintptr_t)d->subst[i].key >> 3) & (cap - 1);
            while (t[j].key) j = (j + 1) & (cap - 1);
            t[j] = d->subst[i];
        }
        free(d->subst);
        d->subst = t;
        d->subst_cap = cap;
    }
    size_t i = ((uintptr_t)e >> 3) & (d->subst_cap - 1);
    while (d->subst[i].key) i = (i + 1) & (d->subst_cap - 1);
    d->subst[i] = (SubstSlot){e, value};
    ++*count;
    return true;
}

/* Instantiate a statement of the theory: variable i becomes values[i].
 * Statements may share subterms, so each node is substituted once. */
static Mm0Node *instantiate(Mm0Decl *d, const Mm0Expr *root, Mm0Node *const *values,
                            uint32_t nvalues) {
    size_t count = 0, sp = 0;
    if (d->subst_cap) memset(d->subst, 0, d->subst_cap * sizeof *d->subst);
    Mm0Node *result = NULL;
    if (!MM0_RESERVE(d->stack, d->cap_stack, 1)) { fail(d, MM0_NOMEM, "substitution allocation"); return NULL; }
    d->stack[sp++] = (SubstFrame){root, 0};
    while (sp) {
        const Mm0Expr *e = d->stack[sp - 1].e;
        if (e->var) {
            if (e->head >= nvalues) { fail(d, MM0_MALFORMED, "statement variable out of range"); return NULL; }
            result = values[e->head];
            --sp;
            continue;
        }
        if (subst_lookup(d, e)) { result = subst_lookup(d, e); --sp; continue; }
        uint32_t next = d->stack[sp - 1].next;
        if (next < e->nargs) {
            d->stack[sp - 1].next = next + 1;
            const Mm0Expr *child = e->args[next];
            if (!child->var && !subst_lookup(d, child)) {
                if (!MM0_RESERVE(d->stack, d->cap_stack, sp + 1)) {
                    fail(d, MM0_NOMEM, "substitution allocation");
                    return NULL;
                }
                d->stack[sp++] = (SubstFrame){child, 0};
            }
            continue;
        }
        if (!MM0_RESERVE(d->scratch, d->cap_scratch, e->nargs ? e->nargs : 1)) {
            fail(d, MM0_NOMEM, "substitution allocation");
            return NULL;
        }
        for (uint32_t i = 0; i < e->nargs; ++i) {
            const Mm0Expr *child = e->args[i];
            if (child->var) {
                if (child->head >= nvalues) { fail(d, MM0_MALFORMED, "statement variable out of range"); return NULL; }
                d->scratch[i] = values[child->head];
            } else {
                d->scratch[i] = subst_lookup(d, child);
            }
        }
        Mm0Node *n = mm0_decl_app(d, e->head, d->scratch, e->nargs);
        if (!n || !subst_put(d, e, n, &count)) return NULL;
        result = n;
        --sp;
    }
    return result;
}

/* ---- conversion witnesses (Conversion.lean ConvWitness.conversion?) ------ */

/* Record a conversion a constructor below checked and issue its name. */
static bool issue(Mm0Decl *d, Mm0Node *left, Mm0Node *right, uint32_t sort, Mm0Conv *out) {
    CHECK(step(d));
    ENSURE_SHAPE(d->nconvs < UINT32_MAX, "too many conversions");
    if (!MM0_RESERVE(d->convs, d->cap_convs, d->nconvs + 1))
        return fail(d, MM0_NOMEM, "conversion allocation");
    d->convs[d->nconvs] = (ConvEntry){left, right, sort};
    *out = (Mm0Conv){d->serial, (uint32_t)d->nconvs};
    ++d->nconvs;
    return true;
}

/* A conversion this builder issued; any other value is refused. */
static const ConvEntry *issued(Mm0Decl *d, const Mm0Conv *c) {
    if (!c) { fail(d, MM0_MALFORMED, "missing conversion"); return NULL; }
    if (c->issuer != d->serial || c->index >= d->nconvs) {
        fail(d, MM0_FOREIGN, "a conversion this builder did not issue");
        return NULL;
    }
    return &d->convs[c->index];
}

static const ConvEntry *peek(const Mm0Decl *d, Mm0Conv c) {
    return c.issuer == d->serial && c.index < d->nconvs ? &d->convs[c.index] : NULL;
}

Mm0Node *mm0_conv_left(const Mm0Decl *d, Mm0Conv c) {
    const ConvEntry *e = peek(d, c);
    return e ? e->left : NULL;
}

Mm0Node *mm0_conv_right(const Mm0Decl *d, Mm0Conv c) {
    const ConvEntry *e = peek(d, c);
    return e ? e->right : NULL;
}

bool mm0_conv_refl(Mm0Decl *d, Mm0Node *e, Mm0Conv *out) {
    LIVE();
    ENSURE_SHAPE(e && out, "missing expression");
    CHECK(owned(d, e));
    return issue(d, e, e, e->sort, out);
}

bool mm0_conv_symm(Mm0Decl *d, const Mm0Conv *c, Mm0Conv *out) {
    LIVE();
    ENSURE_SHAPE(out, "missing result");
    const ConvEntry *e = issued(d, c);
    CHECK(e);
    ConvEntry v = *e;      /* issue may move the table */
    return issue(d, v.right, v.left, v.sort, out);
}

bool mm0_conv_trans(Mm0Decl *d, const Mm0Conv *first, const Mm0Conv *second, Mm0Conv *out) {
    LIVE();
    ENSURE_SHAPE(out, "missing result");
    const ConvEntry *a = issued(d, first);
    CHECK(a);
    ConvEntry x = *a;
    const ConvEntry *b = issued(d, second);
    CHECK(b);
    ConvEntry y = *b;
    ENSURE(x.right == y.left && x.sort == y.sort,
           "conversion transitivity: the middle expressions differ");
    return issue(d, x.left, y.right, x.sort, out);
}

bool mm0_conv_cong(Mm0Decl *d, uint32_t term, const Mm0Conv *children, uint32_t n, Mm0Conv *out) {
    LIVE();
    ENSURE_SHAPE(out && (children || n == 0), "missing conversion");
    ENSURE_SHAPE(term < d->theory->nterms, "unknown term");
    const Mm0TermDecl *t = &d->theory->terms[term];
    ENSURE(n == t->nargs, "wrong number of arguments");
    Mm0Node **left = malloc((n ? n : 1) * sizeof *left), **right = malloc((n ? n : 1) * sizeof *right);
    if (!left || !right) { free(left); free(right); return fail(d, MM0_NOMEM, "conversion allocation"); }
    bool ok = true;
    for (uint32_t i = 0; ok && i < n; ++i) {
        const ConvEntry *c = issued(d, &children[i]);
        if (!c) { ok = false; break; }
        /* ConvertsArgs: both sides fit the binder (a bound slot holds a
         * bound variable on both sides) */
        if (c->sort != t->args[i].sort || !fits(&t->args[i], c->left) ||
            !fits(&t->args[i], c->right))
            ok = fail(d, MM0_REFUSED, "conversion congruence: argument does not fit its binder");
        left[i] = c->left;
        right[i] = c->right;
    }
    Mm0Node *l = ok ? mm0_decl_app(d, term, left, n) : NULL;
    Mm0Node *r = l ? mm0_decl_app(d, term, right, n) : NULL;
    free(left);
    free(right);
    CHECK(r);
    return issue(d, l, r, t->ret_sort, out);
}

/* Unfolding.lean: unfold? checks the arguments, the fresh dummy images
 * (FreshDummies.lean: each image is a bound variable of the dummy's sort,
 * fresh for every argument and every earlier image), then substitutes the
 * stored body. */
bool mm0_conv_unfold(Mm0Decl *d, uint32_t term, Mm0Node *const *args, uint32_t nargs,
                     const uint32_t *images, uint32_t nimages, Mm0Conv *out) {
    LIVE();
    CHECK(freeze(d));
    ENSURE_SHAPE(term < d->theory->nterms, "unknown term");
    const Mm0TermDecl *t = &d->theory->terms[term];
    ENSURE((t->flags & MM0_DECL_DEF) && t->body, "unfold: not a definition");
    ENSURE(nargs == t->nargs, "wrong number of arguments");
    ENSURE(nimages == t->ndummies, "unfold: wrong number of dummy images");
    ENSURE_SHAPE(out && (args || nargs == 0) && (images || nimages == 0), "missing argument");
    Mm0Node *left = mm0_decl_app(d, term, args, nargs);
    CHECK(left);
    size_t total = (size_t)nargs + nimages;
    Mm0Node **values = malloc((total ? total : 1) * sizeof *values);
    if (!values) return fail(d, MM0_NOMEM, "conversion allocation");
    if (nargs) memcpy(values, args, (size_t)nargs * sizeof *values);
    bool ok = true;
    for (uint32_t k = 0; ok && k < nimages; ++k) {
        uint32_t p = images[k];
        if (p >= d->nbinders || !d->binders[p].bound || d->binders[p].sort != t->dummy_sorts[k]) {
            ok = fail(d, MM0_REFUSED, "unfold: a dummy image is not a bound variable of its sort");
            break;
        }
        for (size_t j = 0; ok && j < (size_t)nargs + k; ++j)
            if (has_bit(values[j]->vars, p)) ok = fail(d, MM0_REFUSED, "unfold: a dummy image is not fresh");
        if (ok) {
            values[nargs + k] = mm0_decl_var(d, p);
            ok = values[nargs + k] != NULL;
        }
    }
    Mm0Node *right = ok ? instantiate(d, t->body, values, (uint32_t)total) : NULL;
    free(values);
    CHECK(right);
    ENSURE(right->sort == t->ret_sort, "unfold: the body has the wrong sort");
    return issue(d, left, right, t->ret_sort, out);
}

/* ---- rows (BorrowedProofCursor.lean Rules) ------------------------------- */

/* Every row is one step, however little checking it needs. */
static bool add_row(Mm0Decl *d, Mm0Node *concl, Mm0Node *claim, uint32_t *row) {
    CHECK(step(d));
    if (claim) {
        CHECK(owned(d, claim));
        ENSURE(claim == concl, "the row's claim differs from its conclusion");
    }
    if (!MM0_RESERVE(d->rows, d->cap_rows, d->nrows + 1)) return fail(d, MM0_NOMEM, "row allocation");
    ENSURE_SHAPE(d->nrows < UINT32_MAX, "too many rows");
    d->rows[d->nrows] = concl;
    if (row) *row = (uint32_t)d->nrows;
    ++d->nrows;
    return true;
}

Mm0Status mm0_row_hyp(Mm0Decl *d, uint32_t hyp, Mm0Node *claim, uint32_t *row) {
    if (d->error.status == MM0_OK) {
        if (hyp >= d->nhyps) fail(d, MM0_REFUSED, "no such hypothesis");
        else add_row(d, d->hyps[hyp], claim, row);
    }
    return d->error.status;
}

/* mm0.md "Verification": the substitution fits the binders, bound binders
 * receive bound variables, and if vi is bound and vj does not depend on vi
 * then ei is not in Vars(ej); every substituted hypothesis is a parent. */
static bool thm_row(Mm0Decl *d, uint32_t thm, Mm0Node *const *args, uint32_t nargs,
                    const uint32_t *parents, uint32_t nparents, Mm0Node *claim, uint32_t *row) {
    LIVE();
    CHECK(freeze(d));
    ENSURE_SHAPE(thm < d->theory->nthms, "unknown theorem");
    const Mm0ThmDecl *t = &d->theory->thms[thm];
    ENSURE(nargs == t->nargs, "wrong number of arguments");
    ENSURE_SHAPE(args || nargs == 0, "missing argument");
    ENSURE_SHAPE(parents || nparents == 0, "missing parents");
    /* the disjointness check below visits every pair of arguments */
    CHECK(steps(d, nargs));
    for (uint32_t i = 0; i < nargs; ++i) {
        ENSURE_SHAPE(args[i], "missing argument");
        CHECK(owned(d, args[i]));
        ENSURE(fits(&t->args[i], args[i]), "type mismatch");
    }
    for (uint32_t i = 0; i < nargs; ++i) {
        if (!t->args[i].bound) continue;
        uint32_t x = args[i]->head, k = t->args[i].ordinal;
        for (uint32_t j = 0; j < nargs; ++j) {
            if (j == i) continue;
            const Mm0Binder *b = &t->args[j];
            bool depends = !b->bound && ((b->deps[k / 64] >> (k % 64)) & 1);
            if (!depends) ENSURE(!has_bit(args[j]->vars, x), "disjoint variable violation");
        }
    }
    ENSURE(nparents == t->nhyps, "wrong number of hypotheses");
    for (uint32_t h = 0; h < t->nhyps; ++h) {
        ENSURE_SHAPE(parents[h] < d->nrows, "parent row not checked earlier");
        Mm0Node *want = instantiate(d, t->hyps[h], args, nargs);
        CHECK(want);
        ENSURE(d->rows[parents[h]] == want, "parent row does not prove the hypothesis");
    }
    Mm0Node *concl = instantiate(d, t->concl, args, nargs);
    CHECK(concl);
    return add_row(d, concl, claim, row);
}

Mm0Status mm0_row_thm(Mm0Decl *d, uint32_t thm, Mm0Node *const *args, uint32_t nargs,
                      const uint32_t *parents, uint32_t nparents, Mm0Node *claim, uint32_t *row) {
    thm_row(d, thm, args, nargs, parents, nparents, claim, row);
    return d->error.status;
}

Mm0Status mm0_row_conv(Mm0Decl *d, const Mm0Conv *conv, uint32_t parent, Mm0Node *claim,
                       uint32_t *row) {
    if (d->error.status == MM0_OK) {
        const ConvEntry *c = issued(d, conv);
        if (!c) {
            /* refused by issued() */
        } else if (parent >= d->nrows) {
            fail(d, MM0_MALFORMED, "parent row not checked earlier");
        } else if (d->rows[parent] != c->left) {
            fail(d, MM0_REFUSED, "parent row does not prove the conversion's left side");
        } else {
            add_row(d, c->right, claim, row);
        }
    }
    return d->error.status;
}

Mm0Node *mm0_row_concl(const Mm0Decl *d, uint32_t row) {
    return row < d->nrows ? d->rows[row] : NULL;
}

/* ---- free variables of a definition body (mm0.md "Definition checking") -- */

typedef struct { const Mm0Node *key; uint64_t *fv; } FvSlot;

static bool body_free_vars(Mm0Decl *d, Mm0Node *body, uint64_t **out) {
    size_t cap = 64, count = 0;
    FvSlot *memo = calloc(cap, sizeof *memo);
    NodeFrame *stack = NULL;
    size_t sp = 0, cap_stack = 0;
    bool ok = memo != NULL;
    if (!ok) return fail(d, MM0_NOMEM, "free variable allocation");
#define FV_FIND(node, slot) \
    do { size_t i_ = ((uintptr_t)(node) >> 3) & (cap - 1); \
         while (memo[i_].key && memo[i_].key != (node)) i_ = (i_ + 1) & (cap - 1); \
         slot = &memo[i_]; } while (0)
    ok = MM0_RESERVE(stack, cap_stack, 1);
    if (ok) stack[sp++] = (NodeFrame){body, 0};
    uint64_t *result = NULL;
    while (ok && sp) {
        Mm0Node *n = stack[sp - 1].n;
        FvSlot *slot;
        FV_FIND(n, slot);
        if (slot->key) { result = slot->fv; --sp; continue; }
        if (stack[sp - 1].next == 0 && !step(d)) { ok = false; break; }
        if (!n->var && stack[sp - 1].next < n->nargs) {
            Mm0Node *child = n->args[stack[sp - 1].next++];
            if (!MM0_RESERVE(stack, cap_stack, sp + 1)) { ok = false; break; }
            stack[sp++] = (NodeFrame){child, 0};
            continue;
        }
        uint64_t *fv = mm0_arena_alloc(&d->arena, d->words * sizeof *fv);
        if (!fv) { ok = false; break; }
        if (n->var) {
            memcpy(fv, n->vars, d->words * sizeof *fv);
        } else {
            memset(fv, 0, d->words * sizeof *fv);
            const Mm0TermDecl *t = &d->theory->terms[n->head];
            /* bv[k]: FV of the argument in the k-th bound slot */
            uint64_t **bv = malloc((t->nbound ? t->nbound : 1) * sizeof *bv);
            if (!bv) { ok = false; break; }
            uint32_t k = 0;
            for (uint32_t i = 0; i < n->nargs; ++i) {
                FvSlot *cs;
                FV_FIND(n->args[i], cs);
                if (t->args[i].bound) { bv[k++] = cs->fv; continue; }
                for (uint32_t w = 0; w < d->words; ++w) {
                    uint64_t bits = cs->fv[w];
                    for (uint32_t j = 0; j < k; ++j)
                        if ((t->args[i].deps[j / 64] >> (j % 64)) & 1) bits &= ~bv[j][w];
                    fv[w] |= bits;
                }
            }
            for (uint32_t j = 0; j < k; ++j)
                if ((t->ret_deps[j / 64] >> (j % 64)) & 1)
                    for (uint32_t w = 0; w < d->words; ++w) fv[w] |= bv[j][w];
            free(bv);
        }
        if ((count + 1) * 2 > cap) {
            size_t ncap = cap * 2;
            FvSlot *t = calloc(ncap, sizeof *t);
            if (!t) { ok = false; break; }
            for (size_t i = 0; i < cap; ++i) {
                if (!memo[i].key) continue;
                size_t j = ((uintptr_t)memo[i].key >> 3) & (ncap - 1);
                while (t[j].key) j = (j + 1) & (ncap - 1);
                t[j] = memo[i];
            }
            free(memo);
            memo = t;
            cap = ncap;
        }
        FV_FIND(n, slot);
        *slot = (FvSlot){n, fv};
        ++count;
        result = fv;
        --sp;
    }
#undef FV_FIND
    free(memo);
    free(stack);
    if (!ok) return d->error.status != MM0_OK ? false : fail(d, MM0_NOMEM, "free variable allocation");
    *out = result;
    return true;
}

/* ---- admission (DeclarationAdmission.lean) ------------------------------- */

typedef struct { const Mm0Node *key; Mm0Expr *value; } CopySlot;

/* Copy a builder expression into the theory, keeping its sharing. */
static Mm0Expr *copy_expr(Mm0Decl *d, Mm0Node *root, CopySlot **memo, size_t *cap, size_t *count) {
    Mm0Arena *arena = &d->theory->arena;
    NodeFrame *stack = NULL;
    size_t sp = 0, cap_stack = 0;
    Mm0Expr *result = NULL;
#define CP_FIND(node, slot) \
    do { size_t i_ = ((uintptr_t)(node) >> 3) & (*cap - 1); \
         while ((*memo)[i_].key && (*memo)[i_].key != (node)) i_ = (i_ + 1) & (*cap - 1); \
         slot = &(*memo)[i_]; } while (0)
    if (!*cap) {
        *cap = 64;
        *memo = calloc(*cap, sizeof **memo);
        if (!*memo) { fail(d, MM0_NOMEM, "statement allocation"); return NULL; }
    }
    if (!MM0_RESERVE(stack, cap_stack, 1)) { fail(d, MM0_NOMEM, "statement allocation"); return NULL; }
    stack[sp++] = (NodeFrame){root, 0};
    bool ok = true;
    while (ok && sp) {
        Mm0Node *n = stack[sp - 1].n;
        CopySlot *slot;
        CP_FIND(n, slot);
        if (slot->key) { result = slot->value; --sp; continue; }
        if (stack[sp - 1].next == 0 && !step(d)) { ok = false; break; }
        if (!n->var && stack[sp - 1].next < n->nargs) {
            Mm0Node *child = n->args[stack[sp - 1].next++];
            if (!MM0_RESERVE(stack, cap_stack, sp + 1)) { ok = false; break; }
            stack[sp++] = (NodeFrame){child, 0};
            continue;
        }
        Mm0Expr *e = n->var ? mm0_expr_var(arena, n->head) : mm0_expr_app(arena, n->head, n->nargs);
        if (!e) { ok = false; break; }
        for (uint32_t i = 0; i < n->nargs; ++i) {
            CopySlot *cs;
            CP_FIND(n->args[i], cs);
            e->args[i] = cs->value;
        }
        if ((*count + 1) * 2 > *cap) {
            size_t ncap = *cap * 2;
            CopySlot *t = calloc(ncap, sizeof *t);
            if (!t) { ok = false; break; }
            for (size_t i = 0; i < *cap; ++i) {
                if (!(*memo)[i].key) continue;
                size_t j = ((uintptr_t)(*memo)[i].key >> 3) & (ncap - 1);
                while (t[j].key) j = (j + 1) & (ncap - 1);
                t[j] = (*memo)[i];
            }
            free(*memo);
            *memo = t;
            *cap = ncap;
        }
        CP_FIND(n, slot);
        *slot = (CopySlot){n, e};
        ++*count;
        result = e;
        --sp;
    }
#undef CP_FIND
    free(stack);
    if (!ok) {
        if (d->error.status == MM0_OK) fail(d, MM0_NOMEM, "statement allocation");
        return NULL;
    }
    return result;
}

/* Binders of the first `n` context positions, with dependency sets over
 * bound ordinals as the theory stores them. */
static bool theory_binders(Mm0Decl *d, uint32_t n, Mm0Binder **out, uint32_t *nbound,
                           uint32_t *depw, const char ***names) {
    Mm0Arena *arena = &d->theory->arena;
    /* each binder's dependency set is rebuilt over all binders */
    CHECK(steps(d, n));
    uint32_t *ordinal = malloc((n ? n : 1) * sizeof *ordinal);
    if (!ordinal) return fail(d, MM0_NOMEM, "statement allocation");
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; ++i) ordinal[i] = d->binders[i].bound ? k++ : 0;
    *nbound = k;
    *depw = k / 64 + 1;
    Mm0Binder *b = n ? mm0_arena_alloc(arena, n * sizeof *b) : NULL;
    const char **nm = n ? mm0_arena_alloc(arena, n * sizeof *nm) : NULL;
    bool ok = !n || (b && nm);
    for (uint32_t i = 0; ok && i < n; ++i) {
        uint64_t *deps = mm0_arena_alloc(arena, *depw * sizeof *deps);
        if (!deps) { ok = false; break; }
        memset(deps, 0, *depw * sizeof *deps);
        for (uint32_t p = 0; p < n; ++p)
            if (d->binders[p].bound && has_bit(d->binders[i].deps, p)) set_bit(deps, ordinal[p]);
        b[i] = (Mm0Binder){d->binders[i].sort, d->binders[i].bound, ordinal[i], deps};
        nm[i] = NULL;
        if (d->binders[i].name &&
            !(nm[i] = mm0_arena_strndup(arena, d->binders[i].name, strlen(d->binders[i].name))))
            ok = false;
    }
    free(ordinal);
    if (!ok) return fail(d, MM0_NOMEM, "statement allocation");
    *out = b;
    *names = nm;
    return true;
}

static bool fresh_name(Mm0Decl *d, const Mm0NameMap *names, const char *name) {
    uint32_t existing;
    if (name) ENSURE(!mm0_names_get(names, name, strlen(name), &existing), "duplicate name");
    return true;
}

static const char *keep_name(Mm0Decl *d, const char *name) {
    if (!name) return NULL;
    const char *kept = mm0_arena_strndup(&d->theory->arena, name, strlen(name));
    if (!kept) fail(d, MM0_NOMEM, "statement allocation");
    return kept;
}

Mm0Status mm0_add_sort(Mm0Theory *theory, const char *name, uint8_t mods, Mm0Error *error) {
    Mm0Error local = {.status = MM0_OK, .offset = -1};
    if (!error) error = &local;
    *error = (Mm0Error){.status = MM0_OK, .offset = -1};
    uint32_t existing;
    if (mods & ~0x0Fu) mm0_error_set(error, MM0_MALFORMED, "unknown sort modifier");
    else if (name && mm0_names_get(&theory->sort_names, name, strlen(name), &existing))
        mm0_error_set(error, MM0_REFUSED, "duplicate sort name");
    else {
        Mm0ArenaMark mark = mm0_arena_mark(&theory->arena);
        const char *kept = name ? mm0_arena_strndup(&theory->arena, name, strlen(name)) : NULL;
        if ((name && !kept) || !mm0_theory_push_sort(theory, kept, mods)) {
            mm0_arena_reset(&theory->arena, mark);
            mm0_error_set(error, MM0_NOMEM, "theory allocation");
        }
    }
    return error->status;
}

static bool ret_deps_set(Mm0Decl *d, const uint32_t *ret_deps, uint32_t n, uint64_t *positions) {
    for (uint32_t i = 0; i < n; ++i) {
        ENSURE(ret_deps[i] < d->nordinary && d->binders[ret_deps[i]].bound, "expecting bound variable");
        set_bit(positions, ret_deps[i]);
    }
    return true;
}

static bool finish_term(Mm0Decl *d, const char *name, uint32_t ret_sort, const uint32_t *ret_deps,
                        uint32_t nret_deps, Mm0Node *body, uint32_t *index) {
    LIVE();
    CHECK(freeze(d));
    Mm0Theory *th = d->theory;
    CHECK(fresh_name(d, &th->term_names, name));
    ENSURE(ret_sort < th->nsorts, "unknown sort");
    ENSURE(!(th->sorts[ret_sort].mods & MM0_SORT_PURE), "term in pure sort");
    uint64_t *positions = mm0_arena_alloc(&d->arena, d->words * sizeof *positions);
    if (!positions) return fail(d, MM0_NOMEM, "statement allocation");
    memset(positions, 0, d->words * sizeof *positions);
    CHECK(ret_deps_set(d, ret_deps, nret_deps, positions));
    if (body) {
        CHECK(owned(d, body));
        ENSURE(body->sort == ret_sort, "type mismatch");
        uint64_t *fv;
        CHECK(body_free_vars(d, body, &fv));
        for (uint32_t w = 0; w < d->words; ++w)
            ENSURE((fv[w] & ~positions[w]) == 0, "type has unaccounted dependencies");
    } else {
        ENSURE_SHAPE(d->nbinders == d->nordinary, "dummies belong to definitions");
    }
    Mm0ArenaMark mark = mm0_arena_mark(&th->arena);
    Mm0TermDecl t = {.nargs = d->nordinary, .ret_sort = ret_sort};
    const char **names = NULL;
    bool ok = theory_binders(d, d->nordinary, &t.args, &t.nbound, &t.depw, &names);
    if (ok) {
        t.ret_deps = mm0_arena_alloc(&th->arena, t.depw * sizeof *t.ret_deps);
        ok = t.ret_deps != NULL || fail(d, MM0_NOMEM, "statement allocation");
    }
    if (ok) {
        memset(t.ret_deps, 0, t.depw * sizeof *t.ret_deps);
        for (uint32_t i = 0; i < nret_deps; ++i) {
            uint32_t ord = t.args[ret_deps[i]].ordinal;
            t.ret_deps[ord / 64] |= UINT64_C(1) << (ord % 64);
        }
    }
    t.var_names = names;
    if (ok && body) {
        t.flags = MM0_DECL_DEF;
        t.ndummies = (uint32_t)(d->nbinders - d->nordinary);
        if (t.ndummies) {
            t.dummy_sorts = mm0_arena_alloc(&th->arena, t.ndummies * sizeof *t.dummy_sorts);
            const char **all = mm0_arena_alloc(&th->arena, d->nbinders * sizeof *all);
            ok = (t.dummy_sorts && all) || fail(d, MM0_NOMEM, "statement allocation");
            for (uint32_t k = 0; ok && k < t.ndummies; ++k) t.dummy_sorts[k] = d->binders[t.nargs + k].sort;
            for (size_t i = 0; ok && i < d->nbinders; ++i) {
                all[i] = i < t.nargs ? names[i] : NULL;
                if (i >= t.nargs && d->binders[i].name &&
                    !(all[i] = mm0_arena_strndup(&th->arena, d->binders[i].name, strlen(d->binders[i].name))))
                    ok = fail(d, MM0_NOMEM, "statement allocation");
            }
            if (ok) t.var_names = all;
        }
        CopySlot *memo = NULL;
        size_t cap = 0, count = 0;
        if (ok) ok = (t.body = copy_expr(d, body, &memo, &cap, &count)) != NULL;
        free(memo);
    }
    if (ok && name && !(t.name = keep_name(d, name))) ok = false;
    if (ok && !name) t.flags |= MM0_DECL_LOCAL;
    if (ok && !mm0_theory_push_term(th, &t)) ok = fail(d, MM0_NOMEM, "theory allocation");
    if (!ok) { mm0_arena_reset(&th->arena, mark); return false; }
    if (index) *index = (uint32_t)(th->nterms - 1);
    return true;
}

Mm0Status mm0_decl_finish_term(Mm0Decl *d, const char *name, uint32_t ret_sort,
                               const uint32_t *ret_deps, uint32_t nret_deps, uint32_t *index) {
    finish_term(d, name, ret_sort, ret_deps, nret_deps, NULL, index);
    return d->error.status;
}

Mm0Status mm0_decl_finish_def(Mm0Decl *d, const char *name, uint32_t ret_sort,
                              const uint32_t *ret_deps, uint32_t nret_deps, Mm0Node *body,
                              uint32_t *index) {
    if (!body) fail(d, MM0_MALFORMED, "missing definition body");
    finish_term(d, name, ret_sort, ret_deps, nret_deps, body, index);
    return d->error.status;
}

static bool finish_assertion(Mm0Decl *d, const char *name, bool axiom, uint32_t root, uint32_t *index) {
    LIVE();
    CHECK(freeze(d));
    Mm0Theory *th = d->theory;
    ENSURE_SHAPE(d->concl, "missing conclusion");
    CHECK(fresh_name(d, &th->thm_names, name));
    if (axiom) {
        ENSURE_SHAPE(d->nbinders == d->nordinary, "axioms have no proof dummies");
        ENSURE_SHAPE(d->nrows == 0, "axioms have no proof");
    } else {
        ENSURE_SHAPE(root < d->nrows, "the proof's root row is missing");
        ENSURE(d->rows[root] == d->concl, "the proof does not conclude the statement");
    }
    Mm0ArenaMark mark = mm0_arena_mark(&th->arena);
    Mm0ThmDecl t = {.nargs = d->nordinary, .nhyps = (uint32_t)d->nhyps,
                    .flags = (uint8_t)(axiom ? MM0_DECL_AXIOM : 0)};
    bool ok = theory_binders(d, d->nordinary, &t.args, &t.nbound, &t.depw, &t.var_names);
    CopySlot *memo = NULL;
    size_t cap = 0, count = 0;
    if (ok && d->nhyps) {
        t.hyps = mm0_arena_alloc(&th->arena, d->nhyps * sizeof *t.hyps);
        ok = t.hyps != NULL || fail(d, MM0_NOMEM, "statement allocation");
        for (size_t i = 0; ok && i < d->nhyps; ++i)
            ok = (t.hyps[i] = copy_expr(d, d->hyps[i], &memo, &cap, &count)) != NULL;
    }
    if (ok) ok = (t.concl = copy_expr(d, d->concl, &memo, &cap, &count)) != NULL;
    free(memo);
    if (ok && name && !(t.name = keep_name(d, name))) ok = false;
    if (ok && !name) t.flags |= MM0_DECL_LOCAL;
    if (ok && !mm0_theory_push_thm(th, &t)) ok = fail(d, MM0_NOMEM, "theory allocation");
    if (!ok) { mm0_arena_reset(&th->arena, mark); return false; }
    if (index) *index = (uint32_t)(th->nthms - 1);
    return true;
}

Mm0Status mm0_decl_finish_axiom(Mm0Decl *d, const char *name, uint32_t *index) {
    finish_assertion(d, name, true, 0, index);
    return d->error.status;
}

Mm0Status mm0_decl_finish_theorem(Mm0Decl *d, const char *name, uint32_t root, uint32_t *index) {
    finish_assertion(d, name, false, root, index);
    return d->error.status;
}
