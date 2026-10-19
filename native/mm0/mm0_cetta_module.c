#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Owned MM0 theories and checked theorems use private native seal payloads.
 * Input data is read during the call; only admitted declarations survive.
 * Reserving the result capsule precedes the last fallible admission step. */
#include "mm0_cetta_module.h"
#include "mm0.h"
#include "mm0_internal.h"
#include "mm0_semantic.h"
#include "seal.h"
#include <pthread.h>
#include "eval.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test-only allocation failures at the record, capsule and output-atom
 * boundaries. Each must leave the theory and live authority unchanged. */
#ifdef MM0_ADAPTER_FAULTS
static bool fault(const char *step) {
    const char *f = getenv("CETTA_MM0_FAULT");
    return f && strcmp(f, step) == 0;
}
#else
#define fault(step) false
#endif

typedef struct {
    Mm0Theory *theory;
    atomic_size_t refs;
    pthread_mutex_t gate;
    uint64_t budget;      /* steps per operation; UINT64_MAX: none */
} Theory;

/* A theorem handle's record.  UNCOMMITTED marks a reservation whose
 * declaration has not been admitted yet; no lookup treats it as live. */
typedef struct { atomic_size_t refs; Theory *owner; uint32_t index; } Held;
#define UNCOMMITTED UINT32_MAX

static atomic_size_t g_live_theories, g_live_theorems;

static void theory_ref(void *value) {
    Theory *t = value;
    if (atomic_fetch_add(&t->refs, 1u) == SIZE_MAX) abort();
}
static void theory_unref(Theory *t) {
    if (atomic_fetch_sub(&t->refs, 1u) != 1u) return;
    mm0_theory_free(t->theory);
    pthread_mutex_destroy(&t->gate);
    free(t);
    atomic_fetch_sub(&g_live_theories, 1);
}
static void free_theory(void *p) { theory_unref(p); }
static void held_ref(void *value) {
    Held *h = value;
    if (atomic_fetch_add(&h->refs, 1u) == SIZE_MAX) abort();
}
static void free_held(void *p) {
    Held *h = p;
    if (atomic_fetch_sub(&h->refs, 1u) != 1u) return;
    if (h->owner) {
        theory_unref(h->owner);
        atomic_fetch_sub(&g_live_theorems, 1);
    }
    free(h);
}
static CettaSealBrandCell g_brand = CETTA_SEAL_BRAND_CELL_INIT("mm0");
static const char g_theory_kind = 't', g_theorem_kind = 'p';
static void *sealed_resource(Atom *atom, const void *kind) {
    CettaSealObject *brand = cetta_seal_brand_get(&g_brand, NULL);
    void *resource = NULL;
    if (!brand || cetta_seal_unseal_native(brand, cetta_seal_atom_object(atom),
                                            kind, &resource) != CETTA_SEAL_OK)
        return NULL;
    return resource;
}

typedef struct {
    struct CettaLibraryContext *host;
    Arena *arena;
    Theory *theory;
    Mm0Status status;
    const char *reason;
    char detail[192];
} Call;

static bool interrupted(void *unused) {
    (void)unused;
    return eval_native_interrupted();
}

static bool fail(Call *c, Mm0Status status, const char *reason) {
    if (c->status == MM0_OK) {
        c->status = status;
        c->reason = reason;
    }
    return false;
}

static bool fail_error(Call *c, const Mm0Error *e) {
    if (c->status != MM0_OK) return false;
    c->status = e->status;
    c->reason = e->reason ? e->reason : "failed";
    size_t n = 0;
    if (e->offset >= 0)
        n += (size_t)snprintf(c->detail + n, sizeof c->detail - n, "MMB offset %lld",
                              (long long)e->offset);
    if (e->line && n < sizeof c->detail)
        n += (size_t)snprintf(c->detail + n, sizeof c->detail - n, "%s.mm0 line %u column %u",
                              n ? ", " : "", e->line, e->column);
    if (e->decl[0] && n < sizeof c->detail)
        snprintf(c->detail + n, sizeof c->detail - n, "%s%s", n ? ", " : "", e->decl);
    return false;
}

static Atom *error_atom(Call *c) {
    bool refusal = c->status == MM0_REFUSED || c->status == MM0_FOREIGN ||
                   c->status == MM0_UNSUPPORTED;
    Atom *items[4] = {atom_symbol(c->arena, refusal ? "mm0:refused" : "mm0:error"),
                      atom_string(c->arena, mm0_status_name(c->status)),
                      atom_string(c->arena, c->reason ? c->reason : "failed"),
                      c->detail[0] ? atom_string(c->arena, c->detail) : NULL};
    return atom_expr(c->arena, items, c->detail[0] ? 4 : 3);
}

static Mm0Options options_of(const Call *c) {
    Mm0Options o;
    mm0_options_init(&o);
    o.budget = c->theory ? c->theory->budget : UINT64_MAX;
    o.poll = interrupted;
    return o;
}

/* ---- arguments ----------------------------------------------------------- */

static Atom *unquote(Atom *x) {
    if (x && x->kind == ATOM_EXPR && x->expr.len == 2 && atom_is_symbol(x->expr.elems[0], "quote"))
        return x->expr.elems[1];
    return x;
}

static const char *name_arg(Atom *x) {
    if (!x) return NULL;
    if (x->kind == ATOM_SYMBOL) return atom_name_cstr(x);
    if (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING &&
        !memchr(x->ground.sval, 0, atom_string_len(x)))
        return x->ground.sval;
    return NULL;
}

static bool uint_arg(Atom *x, uint64_t *out) {
    if (!x || x->kind != ATOM_GROUNDED || x->ground.gkind != GV_INT || x->ground.ival < 0) return false;
    *out = (uint64_t)x->ground.ival;
    return true;
}

static bool is_list(Atom *x) { return x && x->kind == ATOM_EXPR; }

static bool head_is(Atom *x, const char *name, size_t len) {
    return is_list(x) && x->expr.len == len && atom_is_symbol(x->expr.elems[0], name);
}

/* ---- publication --------------------------------------------------------- */

static Atom *sealed_value(Call *c, bool theorem, void *resource,
                          void (*retain)(void *), void (*release)(void *)) {
    CettaSealObject *brand = cetta_seal_brand_get(&g_brand, NULL);
    if (!brand) { fail(c, MM0_NOMEM, "MM0 seal brand"); return NULL; }
    if (fault(theorem ? "theorem-capsule" : "theory-capsule")) {
        fail(c, MM0_NOMEM, "MM0 capsule allocation"); return NULL;
    }
    CettaSealNativePayload payload = {
        theorem ? &g_theorem_kind : &g_theory_kind, resource, retain, release};
    CettaSealObject *box = NULL;
    CettaSealStatus status = cetta_seal_native(brand, &payload, &box);
    if (status != CETTA_SEAL_OK) {
        fail(c, status == CETTA_SEAL_IDENTITY_EXHAUSTED ? MM0_CAPACITY : MM0_NOMEM,
             cetta_seal_status_name(status));
        return NULL;
    }
    Atom *out = fault(theorem ? "theorem-owned-atom" : "theory-owned-atom") ? NULL
                 : cetta_seal_object_atom(c->arena, box);
    cetta_seal_release(box);
    if (!out) fail(c, MM0_NOMEM, "MM0 capsule atom");
    return out;
}

/* All publication storage exists before admission. An uncommitted capsule
 * cannot be looked up as a theorem. Commit changes only its private index. */
typedef struct { Held *held; Atom *atom; } Reservation;

static bool reserve(Call *c, Reservation *r) {
    *r = (Reservation){0};
    Held *h = fault("theorem-record") ? NULL : calloc(1, sizeof *h);
    if (!h) return fail(c, MM0_NOMEM, "theorem capsule record");
    atomic_init(&h->refs, 1u);
    h->owner = c->theory; h->index = UNCOMMITTED;
    theory_ref(h->owner);
    atomic_fetch_add(&g_live_theorems, 1);
    Atom *out = sealed_value(c, true, h, held_ref, free_held);
    if (out) *r = (Reservation){h, out};
    free_held(h);
    return out != NULL;
}

static Atom *commit(Reservation *r, uint32_t index) {
    r->held->index = index;
    Atom *out = r->atom;
    *r = (Reservation){0};
    return out;
}

static void cancel(Call *c, Reservation *r) {
    (void)c;
    if (r->held) {
        /* The unreturned arena value stays inert until arena cleanup. */
        Theory *owner = r->held->owner;
        r->held->owner = NULL;
        theory_unref(owner);
        atomic_fetch_sub(&g_live_theorems, 1);
    }
    *r = (Reservation){0};
}

static Atom *publish_thm(Call *c, uint32_t index) {
    Reservation r;
    return reserve(c, &r) ? commit(&r, index) : NULL;
}

static Held *held_of(struct CettaLibraryContext *host, Atom *x) {
    (void)host;
    Held *h = sealed_resource(x, &g_theorem_kind);
    return h && h->owner && h->index != UNCOMMITTED ? h : NULL;
}

static Atom *new_theory(Call *c) {
    Theory *t = fault("theory-record") ? NULL : calloc(1, sizeof *t);
    if (!t) { fail(c, MM0_NOMEM, "theory record"); return NULL; }
    atomic_init(&t->refs, 1u);
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr)) {
        free(t); fail(c, MM0_NOMEM, "theory lock"); return NULL;
    }
    int locked = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!locked) locked = pthread_mutex_init(&t->gate, &attr);
    pthread_mutexattr_destroy(&attr);
    if (locked) { free(t); fail(c, MM0_NOMEM, "theory lock"); return NULL; }
    t->budget = UINT64_MAX;
    t->theory = mm0_theory_new();
    atomic_fetch_add(&g_live_theories, 1);
    if (!t->theory) { theory_unref(t); fail(c, MM0_NOMEM, "theory allocation"); return NULL; }
    Atom *out = sealed_value(c, false, t, theory_ref, free_theory);
    theory_unref(t);
    return out;
}

/* ---- reading data -------------------------------------------------------- */

typedef struct { Atom *key; Mm0Node *value; } AtomSlot;
typedef struct { Atom *atom; uint32_t next; } AtomFrame;

typedef struct {
    Call *c;
    Mm0Decl *d;
    Mm0NameMap vars;
    AtomSlot *memo;
    size_t cap, count;
    AtomFrame *stack;
    size_t cap_stack;
    Mm0Node **args;
    size_t cap_args;
} Reader;

static void reader_free(Reader *r) {
    mm0_names_free(&r->vars);
    free(r->memo);
    free(r->stack);
    free(r->args);
}

static bool decl_ok(Reader *r) {
    const Mm0Error *e = mm0_decl_error(r->d);
    return e->status == MM0_OK || fail_error(r->c, e);
}

static AtomSlot *memo_slot(Reader *r, Atom *key) {
    size_t i = ((uintptr_t)key >> 4) & (r->cap - 1);
    while (r->memo[i].key && r->memo[i].key != key) i = (i + 1) & (r->cap - 1);
    return &r->memo[i];
}

static bool memo_put(Reader *r, Atom *key, Mm0Node *value) {
    if ((r->count + 1) * 2 > r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 64;
        AtomSlot *t = calloc(cap, sizeof *t);
        if (!t) return fail(r->c, MM0_NOMEM, "reader allocation");
        AtomSlot *old = r->memo;
        size_t old_cap = r->cap;
        r->memo = t;
        r->cap = cap;
        for (size_t i = 0; i < old_cap; ++i)
            if (old[i].key) *memo_slot(r, old[i].key) = old[i];
        free(old);
    }
    AtomSlot *s = memo_slot(r, key);
    if (!s->key) ++r->count;
    *s = (AtomSlot){key, value};
    return true;
}

static Mm0Node *memo_get(Reader *r, Atom *key) {
    if (!r->cap) return NULL;
    AtomSlot *s = memo_slot(r, key);
    return s->key ? s->value : NULL;
}

static bool read_leaf(Reader *r, Atom *x, Mm0Node **out) {
    if (x->kind == ATOM_VAR) return fail(r->c, MM0_MALFORMED, "a MeTTa variable is not an MM0 variable");
    const char *name = x->kind == ATOM_SYMBOL ? atom_name_cstr(x) : NULL;
    uint32_t index;
    if (!name || !mm0_names_get(&r->vars, name, strlen(name), &index))
        return fail(r->c, MM0_MALFORMED, "unknown MM0 variable");
    *out = mm0_decl_var(r->d, index);
    return *out != NULL || decl_ok(r);
}

/* An MM0 expression: a symbol is a variable of the declaration, (t e1 ...
 * en) applies term t.  Read in postorder with an explicit stack. */
static bool read_expr(Reader *r, Atom *root, Mm0Node **out) {
    Mm0Node *done = memo_get(r, root);
    if (done) { *out = done; return true; }
    if (root->kind != ATOM_EXPR) return read_leaf(r, root, out);
    size_t sp = 0;
    if (!MM0_RESERVE(r->stack, r->cap_stack, 1)) return fail(r->c, MM0_NOMEM, "reader allocation");
    r->stack[sp++] = (AtomFrame){root, 1};
    while (sp) {
        AtomFrame *f = &r->stack[sp - 1];
        Atom *x = f->atom;
        if (!x->expr.len || x->expr.elems[0]->kind != ATOM_SYMBOL)
            return fail(r->c, MM0_MALFORMED, "an application needs a term name");
        if (f->next < x->expr.len) {
            Atom *child = x->expr.elems[f->next++];
            if (memo_get(r, child)) continue;
            if (child->kind != ATOM_EXPR) {
                Mm0Node *leaf;
                if (!read_leaf(r, child, &leaf) || !memo_put(r, child, leaf)) return false;
                continue;
            }
            if (!MM0_RESERVE(r->stack, r->cap_stack, sp + 1)) return fail(r->c, MM0_NOMEM, "reader allocation");
            r->stack[sp++] = (AtomFrame){child, 1};
            continue;
        }
        const char *name = atom_name_cstr(x->expr.elems[0]);
        uint32_t term;
        if (!mm0_names_get(&r->c->theory->theory->term_names, name, strlen(name), &term))
            return fail(r->c, MM0_MALFORMED, "unknown MM0 term");
        uint32_t n = (uint32_t)x->expr.len - 1;
        if (!MM0_RESERVE(r->args, r->cap_args, n ? n : 1)) return fail(r->c, MM0_NOMEM, "reader allocation");
        for (uint32_t i = 0; i < n; ++i) r->args[i] = memo_get(r, x->expr.elems[i + 1]);
        Mm0Node *node = mm0_decl_app(r->d, term, r->args, n);
        if (!node) return decl_ok(r) && fail(r->c, MM0_REFUSED, "ill-typed expression");
        if (!memo_put(r, x, node)) return false;
        --sp;
    }
    *out = memo_get(r, root);
    return true;
}

static bool read_sort(Reader *r, Atom *x, uint32_t *out) {
    const char *name = name_arg(x);
    if (!name || !mm0_names_get(&r->c->theory->theory->sort_names, name, strlen(name), out))
        return fail(r->c, MM0_MALFORMED, "unknown MM0 sort");
    return true;
}

static bool add_var_name(Reader *r, const char *name) {
    uint32_t existing, position = mm0_decl_nbinders(r->d);
    if (mm0_names_get(&r->vars, name, strlen(name), &existing))
        return fail(r->c, MM0_MALFORMED, "duplicate variable name");
    if (!mm0_names_put(&r->vars, name, strlen(name), position))
        return fail(r->c, MM0_NOMEM, "reader allocation");
    return true;
}

/* (bound x s) | (var x s) | (var x s (y ...)) */
static bool read_binders(Reader *r, Atom *list) {
    list = unquote(list);
    if (!is_list(list)) return fail(r->c, MM0_MALFORMED, "binder list");
    for (size_t i = 0; i < list->expr.len; ++i) {
        Atom *b = list->expr.elems[i];
        bool bound = head_is(b, "bound", 3);
        if (!bound && !head_is(b, "var", 3) && !head_is(b, "var", 4))
            return fail(r->c, MM0_MALFORMED, "binder: (bound x s) or (var x s (deps))");
        const char *name = b->expr.elems[1]->kind == ATOM_SYMBOL ? atom_name_cstr(b->expr.elems[1]) : NULL;
        uint32_t sort;
        if (!name) return fail(r->c, MM0_MALFORMED, "binder name");
        if (!read_sort(r, b->expr.elems[2], &sort)) return false;
        uint32_t *deps = NULL, ndeps = 0;
        if (b->expr.len == 4) {
            Atom *dl = b->expr.elems[3];
            if (!is_list(dl)) return fail(r->c, MM0_MALFORMED, "binder dependencies");
            deps = malloc((dl->expr.len ? dl->expr.len : 1) * sizeof *deps);
            if (!deps) return fail(r->c, MM0_NOMEM, "reader allocation");
            for (size_t k = 0; k < dl->expr.len; ++k) {
                const char *dep = dl->expr.elems[k]->kind == ATOM_SYMBOL ? atom_name_cstr(dl->expr.elems[k]) : NULL;
                if (!dep || !mm0_names_get(&r->vars, dep, strlen(dep), &deps[ndeps])) {
                    free(deps);
                    return fail(r->c, MM0_MALFORMED, "unknown dependency variable");
                }
                ++ndeps;
            }
        }
        bool ok = add_var_name(r, name);
        if (ok) {
            mm0_decl_binder(r->d, name, sort, bound, deps, ndeps);
            ok = decl_ok(r);
        }
        free(deps);
        if (!ok) return false;
    }
    return true;
}

/* (dummy x s) ... */
static bool read_dummies(Reader *r, Atom *list) {
    list = unquote(list);
    if (!is_list(list)) return fail(r->c, MM0_MALFORMED, "dummy list");
    for (size_t i = 0; i < list->expr.len; ++i) {
        Atom *b = list->expr.elems[i];
        if (!head_is(b, "dummy", 3) || b->expr.elems[1]->kind != ATOM_SYMBOL)
            return fail(r->c, MM0_MALFORMED, "dummy: (dummy x s)");
        const char *name = atom_name_cstr(b->expr.elems[1]);
        uint32_t sort;
        if (!read_sort(r, b->expr.elems[2], &sort) || !add_var_name(r, name)) return false;
        mm0_decl_dummy(r->d, name, sort);
        if (!decl_ok(r)) return false;
    }
    return true;
}

static bool read_statement(Reader *r, Atom *hyps, Atom *concl) {
    hyps = unquote(hyps);
    if (!is_list(hyps)) return fail(r->c, MM0_MALFORMED, "hypothesis list");
    for (size_t i = 0; i < hyps->expr.len; ++i) {
        Mm0Node *h;
        if (!read_expr(r, hyps->expr.elems[i], &h)) return false;
        mm0_decl_hyp(r->d, h);
        if (!decl_ok(r)) return false;
    }
    Mm0Node *e;
    if (!read_expr(r, unquote(concl), &e)) return false;
    mm0_decl_concl(r->d, e);
    return decl_ok(r);
}

static bool term_arg(Reader *r, Atom *x, uint32_t *out) {
    const char *name = name_arg(x);
    if (!name || !mm0_names_get(&r->c->theory->theory->term_names, name, strlen(name), out))
        return fail(r->c, MM0_MALFORMED, "unknown MM0 term");
    return true;
}

static bool expr_list(Reader *r, Atom *list, Mm0Node ***out, uint32_t *n) {
    if (!is_list(list)) return fail(r->c, MM0_MALFORMED, "expression list");
    *n = (uint32_t)list->expr.len;
    *out = malloc((*n ? *n : 1) * sizeof **out);
    if (!*out) return fail(r->c, MM0_NOMEM, "reader allocation");
    for (uint32_t i = 0; i < *n; ++i)
        if (!read_expr(r, list->expr.elems[i], &(*out)[i])) { free(*out); *out = NULL; return false; }
    return true;
}

/* Conversion witnesses (Lean ConvWitness): (refl e), (symm w), (trans w w),
 * (cong t (w ...)), (unfold d (e ...) (x ...)).  Evaluated in postorder. */
typedef struct { Atom *atom; uint32_t next, nchildren, base; } WitnessFrame;

static uint32_t witness_children(Atom *w, Atom ***children) {
    if (head_is(w, "symm", 2)) { *children = &w->expr.elems[1]; return 1; }
    if (head_is(w, "trans", 3)) { *children = &w->expr.elems[1]; return 2; }
    if (head_is(w, "cong", 3) && is_list(w->expr.elems[2])) {
        *children = w->expr.elems[2]->expr.elems;
        return (uint32_t)w->expr.elems[2]->expr.len;
    }
    *children = NULL;
    return 0;
}

static bool read_witness(Reader *r, Atom *root, Mm0Conv *out) {
    WitnessFrame *stack = NULL;
    Mm0Conv *values = NULL;
    size_t sp = 0, cap_stack = 0, nv = 0, cap_values = 0;
    bool ok = MM0_RESERVE(stack, cap_stack, 1) || fail(r->c, MM0_NOMEM, "reader allocation");
    if (ok) {
        Atom **kids;
        stack[sp++] = (WitnessFrame){root, 0, witness_children(root, &kids), 0};
    }
    while (ok && sp) {
        WitnessFrame *f = &stack[sp - 1];
        Atom **kids;
        witness_children(f->atom, &kids);
        if (f->next < f->nchildren) {
            Atom *child = kids[f->next++];
            Atom **gk;
            uint32_t n = witness_children(child, &gk);
            if (!MM0_RESERVE(stack, cap_stack, sp + 1)) { ok = fail(r->c, MM0_NOMEM, "reader allocation"); break; }
            stack[sp++] = (WitnessFrame){child, 0, n, (uint32_t)nv};
            continue;
        }
        Atom *w = f->atom;
        Mm0Conv result;
        Mm0Conv *kidv = values + f->base;
        if (head_is(w, "refl", 2)) {
            Mm0Node *e;
            ok = read_expr(r, w->expr.elems[1], &e) && mm0_conv_refl(r->d, e, &result);
        } else if (head_is(w, "symm", 2)) {
            ok = mm0_conv_symm(r->d, &kidv[0], &result);
        } else if (head_is(w, "trans", 3)) {
            ok = mm0_conv_trans(r->d, &kidv[0], &kidv[1], &result);
        } else if (head_is(w, "cong", 3) && is_list(w->expr.elems[2])) {
            uint32_t term;
            ok = term_arg(r, w->expr.elems[1], &term) &&
                 mm0_conv_cong(r->d, term, kidv, f->nchildren, &result);
        } else if (head_is(w, "unfold", 4)) {
            uint32_t term, nargs = 0;
            Mm0Node **args = NULL;
            Atom *imgs = w->expr.elems[3];
            ok = term_arg(r, w->expr.elems[1], &term) && expr_list(r, w->expr.elems[2], &args, &nargs) &&
                 (is_list(imgs) || fail(r->c, MM0_MALFORMED, "unfold images"));
            uint32_t *images = NULL;
            if (ok) {
                images = malloc((imgs->expr.len ? imgs->expr.len : 1) * sizeof *images);
                if (!images) ok = fail(r->c, MM0_NOMEM, "reader allocation");
                for (size_t i = 0; ok && i < imgs->expr.len; ++i) {
                    const char *v = imgs->expr.elems[i]->kind == ATOM_SYMBOL ? atom_name_cstr(imgs->expr.elems[i]) : NULL;
                    if (!v || !mm0_names_get(&r->vars, v, strlen(v), &images[i]))
                        ok = fail(r->c, MM0_MALFORMED, "unfold image: a variable");
                }
            }
            ok = ok && mm0_conv_unfold(r->d, term, args, nargs, images, (uint32_t)imgs->expr.len, &result);
            free(args);
            free(images);
        } else {
            ok = fail(r->c, MM0_MALFORMED, "conversion witness: refl, symm, trans, cong or unfold");
        }
        if (!ok) break;
        nv = f->base;
        --sp;
        if (!MM0_RESERVE(values, cap_values, nv + 1)) { ok = fail(r->c, MM0_NOMEM, "reader allocation"); break; }
        values[nv++] = result;
    }
    if (ok) *out = values[0];
    free(stack);
    free(values);
    /* a witness the builder refused leaves its reason in the builder */
    return decl_ok(r) && ok;
}

/* ---- proof rows ---------------------------------------------------------- */

typedef struct { uint64_t id; uint32_t row; uint8_t used; } IdSlot;
typedef struct { IdSlot *slots; size_t cap, count; } IdMap;

static IdSlot *id_slot(IdMap *m, uint64_t id) {
    size_t i = (size_t)((id * UINT64_C(0x9e3779b97f4a7c15)) >> 7) & (m->cap - 1);
    while (m->slots[i].used && m->slots[i].id != id) i = (i + 1) & (m->cap - 1);
    return &m->slots[i];
}

static bool id_put(Call *c, IdMap *m, uint64_t id, uint32_t row) {
    if ((m->count + 1) * 2 > m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 64;
        IdSlot *old = m->slots;
        size_t old_cap = m->cap;
        m->slots = calloc(cap, sizeof *m->slots);
        if (!m->slots) { m->slots = old; return fail(c, MM0_NOMEM, "row index allocation"); }
        m->cap = cap;
        for (size_t i = 0; i < old_cap; ++i) if (old[i].used) *id_slot(m, old[i].id) = old[i];
        free(old);
    }
    IdSlot *s = id_slot(m, id);
    if (s->used) return fail(c, MM0_REFUSED, "duplicate proof row ID");
    *s = (IdSlot){id, row, 1};
    ++m->count;
    return true;
}

static bool id_get(IdMap *m, uint64_t id, uint32_t *row) {
    if (!m->cap) return false;
    IdSlot *s = id_slot(m, id);
    if (!s->used) return false;
    *row = s->row;
    return true;
}

static bool cited_theorem(Reader *r, Atom *x, uint32_t *thm) {
    Call *c = r->c;
    Held *h = held_of(c->host, x);
    if (h) {
        if (h->owner != c->theory) return fail(c, MM0_FOREIGN, "theorem of another MM0 theory");
        *thm = h->index;
        return true;
    }
    const char *name = name_arg(x);
    if (!name || !mm0_names_get(&c->theory->theory->thm_names, name, strlen(name), thm))
        return fail(c, MM0_MALFORMED, "unknown MM0 theorem");
    return true;
}

/* (Mm0Row ID RULE (PARENT ...)) or with a claimed conclusion fifth:
 *   RULE = (hyp K) | (thm NAME-OR-HANDLE (ARG ...)) | (conv WITNESS) */
static bool read_row(Reader *r, IdMap *ids, Atom *row) {
    Call *c = r->c;
    uint64_t id;
    if (!is_list(row) || (row->expr.len != 4 && row->expr.len != 5) ||
        !atom_is_symbol(row->expr.elems[0], "Mm0Row") || !uint_arg(row->expr.elems[1], &id))
        return fail(c, MM0_MALFORMED, "proof row: (Mm0Row ID RULE (PARENTS) [CLAIM])");
    Atom *rule = row->expr.elems[2], *parents = row->expr.elems[3];
    if (!is_list(parents)) return fail(c, MM0_MALFORMED, "proof row parents");
    uint32_t np = (uint32_t)parents->expr.len;
    uint32_t *prow = malloc((np ? np : 1) * sizeof *prow);
    if (!prow) return fail(c, MM0_NOMEM, "reader allocation");
    bool ok = true;
    for (uint32_t i = 0; ok && i < np; ++i) {
        uint64_t pid;
        if (!uint_arg(parents->expr.elems[i], &pid)) ok = fail(c, MM0_MALFORMED, "proof row parent ID");
        else if (!id_get(ids, pid, &prow[i])) ok = fail(c, MM0_REFUSED, "proof parent not checked earlier");
    }
    Mm0Node *claim = NULL;
    if (ok && row->expr.len == 5) ok = read_expr(r, row->expr.elems[4], &claim);
    uint32_t index = 0;
    if (ok) {
        if (head_is(rule, "hyp", 2)) {
            uint64_t k;
            ok = (uint_arg(rule->expr.elems[1], &k) && k <= UINT32_MAX && np == 0) ||
                 fail(c, MM0_MALFORMED, "hypothesis row: (hyp K) with no parents");
            if (ok) { mm0_row_hyp(r->d, (uint32_t)k, claim, &index); ok = decl_ok(r); }
        } else if (head_is(rule, "thm", 3)) {
            uint32_t thm, nargs = 0;
            Mm0Node **args = NULL;
            ok = cited_theorem(r, rule->expr.elems[1], &thm) &&
                 expr_list(r, rule->expr.elems[2], &args, &nargs);
            if (ok) {
                mm0_row_thm(r->d, thm, args, nargs, prow, np, claim, &index);
                ok = decl_ok(r);
            }
            free(args);
        } else if (head_is(rule, "conv", 2)) {
            Mm0Conv conv;
            ok = (np == 1 || fail(c, MM0_MALFORMED, "a conversion row has one parent")) &&
                 read_witness(r, rule->expr.elems[1], &conv);
            if (ok) { mm0_row_conv(r->d, &conv, prow[0], claim, &index); ok = decl_ok(r); }
        } else {
            ok = fail(c, MM0_MALFORMED, "proof rule: hyp, thm or conv");
        }
    }
    free(prow);
    return ok && id_put(c, ids, id, index);
}

/* ---- writing statements -------------------------------------------------- */

typedef struct { const Mm0Expr *key; Atom *value; } ExprSlot;

static Atom *var_symbol(Call *c, const char *const *names, uint32_t count, uint32_t i) {
    if (i < count && names && names[i]) return atom_symbol(c->arena, names[i]);
    char buf[32];
    snprintf(buf, sizeof buf, "#%u", i);
    return atom_symbol(c->arena, buf);
}

static Atom *term_symbol(Call *c, uint32_t t) {
    const char *name = c->theory->theory->terms[t].name;
    if (name) return atom_symbol(c->arena, name);
    char buf[32];
    snprintf(buf, sizeof buf, "#term%u", t);
    return atom_symbol(c->arena, buf);
}

static Atom *write_expr(Call *c, const Mm0Expr *root, const char *const *names, uint32_t count) {
    size_t cap = 64, n = 0, sp = 0, cap_stack = 0;
    ExprSlot *memo = calloc(cap, sizeof *memo);
    struct Frame { const Mm0Expr *e; uint32_t next; } *stack = NULL;
    Atom *result = NULL;
    bool ok = memo && MM0_RESERVE(stack, cap_stack, 1);
#define FIND(K, S) \
    do { \
        size_t i_ = ((uintptr_t)(K) >> 3) & (cap - 1); \
        while (memo[i_].key && memo[i_].key != (K)) i_ = (i_ + 1) & (cap - 1); \
        S = &memo[i_]; \
    } while (0)
    if (ok) stack[sp++] = (struct Frame){root, 0};
    while (ok && sp) {
        const Mm0Expr *e = stack[sp - 1].e;
        ExprSlot *slot;
        FIND(e, slot);
        if (slot->key) { result = slot->value; --sp; continue; }
        if (!e->var && stack[sp - 1].next < e->nargs) {
            const Mm0Expr *child = e->args[stack[sp - 1].next++];
            if (!MM0_RESERVE(stack, cap_stack, sp + 1)) { ok = false; break; }
            stack[sp++] = (struct Frame){child, 0};
            continue;
        }
        Atom *a;
        if (e->var) {
            a = var_symbol(c, names, count, e->head);
        } else {
            Atom **items = malloc(((size_t)e->nargs + 1) * sizeof *items);
            if (!items) { ok = false; break; }
            items[0] = term_symbol(c, e->head);
            for (uint32_t i = 0; i < e->nargs; ++i) {
                ExprSlot *cs;
                FIND(e->args[i], cs);
                items[i + 1] = cs->value;
            }
            a = atom_expr(c->arena, items, (CettaExprLen)e->nargs + 1);
            free(items);
        }
        if ((n + 1) * 2 > cap) {
            size_t ncap = cap * 2;
            ExprSlot *t = calloc(ncap, sizeof *t);
            if (!t) { ok = false; break; }
            ExprSlot *old = memo;
            size_t old_cap = cap;
            memo = t;
            cap = ncap;
            for (size_t i = 0; i < old_cap; ++i) {
                if (!old[i].key) continue;
                ExprSlot *moved;
                FIND(old[i].key, moved);
                *moved = old[i];
            }
            free(old);
        }
        FIND(e, slot);
        *slot = (ExprSlot){e, a};
        ++n;
        result = a;
        --sp;
    }
#undef FIND
    free(memo);
    free(stack);
    if (!ok) fail(c, MM0_NOMEM, "statement output");
    return ok ? result : NULL;
}

static Atom *write_binders(Call *c, const Mm0Binder *args, uint32_t nargs, const char *const *names) {
    Atom **items = malloc((nargs ? nargs : 1) * sizeof *items);
    if (!items) { fail(c, MM0_NOMEM, "statement output"); return NULL; }
    Mm0Theory *th = c->theory->theory;
    for (uint32_t i = 0; i < nargs; ++i) {
        const Mm0Binder *b = &args[i];
        Atom *sort = th->sorts[b->sort].name ? atom_symbol(c->arena, th->sorts[b->sort].name)
                                            : atom_int(c->arena, b->sort);
        if (b->bound) {
            Atom *parts[3] = {atom_symbol(c->arena, "bound"), var_symbol(c, names, nargs, i), sort};
            items[i] = atom_expr(c->arena, parts, 3);
            continue;
        }
        Atom **deps = malloc((i ? i : 1) * sizeof *deps);
        if (!deps) { free(items); fail(c, MM0_NOMEM, "statement output"); return NULL; }
        uint32_t nd = 0;
        for (uint32_t j = 0; j < i; ++j)
            if (args[j].bound && ((b->deps[args[j].ordinal / 64] >> (args[j].ordinal % 64)) & 1))
                deps[nd++] = var_symbol(c, names, nargs, j);
        Atom *parts[4] = {atom_symbol(c->arena, "var"), var_symbol(c, names, nargs, i), sort,
                          atom_expr(c->arena, deps, nd)};
        free(deps);
        items[i] = atom_expr(c->arena, parts, 4);
    }
    Atom *out = atom_expr(c->arena, items, nargs);
    free(items);
    return out;
}

/* (theorem NAME (BINDERS) (HYPS) CONCL), or axiom */
static Atom *statement(Call *c, uint32_t index) {
    const Mm0ThmDecl *t = &c->theory->theory->thms[index];
    char local[32];
    snprintf(local, sizeof local, "#thm%u", index);
    Atom *binders = write_binders(c, t->args, t->nargs, t->var_names);
    Atom **hyps = malloc((t->nhyps ? t->nhyps : 1) * sizeof *hyps);
    if (!binders || !hyps) { free(hyps); fail(c, MM0_NOMEM, "statement output"); return NULL; }
    for (uint32_t i = 0; i < t->nhyps; ++i)
        if (!(hyps[i] = write_expr(c, t->hyps[i], t->var_names, t->nargs))) { free(hyps); return NULL; }
    Atom *concl = write_expr(c, t->concl, t->var_names, t->nargs);
    Atom *items[5] = {atom_symbol(c->arena, (t->flags & MM0_DECL_AXIOM) ? "axiom" : "theorem"),
                      atom_symbol(c->arena, t->name ? t->name : local), binders,
                      atom_expr(c->arena, hyps, t->nhyps), concl};
    free(hyps);
    return concl ? atom_expr(c->arena, items, 5) : NULL;
}

/* ---- operations ---------------------------------------------------------- */

static unsigned char *read_file(Call *c, const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { fail(c, MM0_MALFORMED, "cannot open file"); return NULL; }
    size_t cap = 1 << 16, n = 0;
    unsigned char *buf = malloc(cap);
    while (buf) {
        if (n == cap) {
            unsigned char *grown = cap < SIZE_MAX / 2 ? realloc(buf, cap * 2) : NULL;
            if (!grown) { free(buf); buf = NULL; break; }
            buf = grown;
            cap *= 2;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (!got) break;
    }
    bool bad = !buf || ferror(f);
    fclose(f);
    if (bad) { free(buf); fail(c, buf ? MM0_MALFORMED : MM0_NOMEM, "cannot read file"); return NULL; }
    *len = n;
    return buf;
}

static Atom *load_mmb(Call *c, Atom *mmb_path, Atom *spec_path, bool check_only) {
    const char *mp = name_arg(mmb_path), *sp = name_arg(spec_path);
    if (!mp || !sp) { fail(c, MM0_MALFORMED, "expected MMB and .mm0 paths"); return NULL; }
    size_t mlen = 0, slen = 0;
    unsigned char *mmb = read_file(c, mp, &mlen);
    unsigned char *spec = mmb ? read_file(c, sp, &slen) : NULL;
    Atom *result = NULL;
    if (spec) {
        Mm0Options o = options_of(c);
        o.check_only = check_only;
        Mm0Theory *target = check_only ? mm0_theory_new() : c->theory->theory;
        Mm0Error e;
        if (!target) fail(c, MM0_NOMEM, "theory allocation");
        else if (mm0_load_mmb(target, mmb, mlen, (const char *)spec, slen, &o, &e) != MM0_OK) fail_error(c, &e);
        else {
            Mm0TheoryStats s = mm0_theory_stats(target);
            Atom *items[4] = {atom_symbol(c->arena, check_only ? "mm0:verified" : "mm0:loaded"),
                              atom_int(c->arena, (int64_t)s.sorts), atom_int(c->arena, (int64_t)s.terms),
                              atom_int(c->arena, (int64_t)s.theorems)};
            if (check_only) {
                /* counts of a check-only run come from the file's header */
                items[1] = atom_int(c->arena, mmb[5]);
                items[2] = atom_int(c->arena, (int64_t)mm0_le32(mmb + 8));
                items[3] = atom_int(c->arena, (int64_t)mm0_le32(mmb + 12));
            }
            result = atom_expr(c->arena, items, 4);
        }
        if (check_only) mm0_theory_free(target);
    }
    free(mmb);
    free(spec);
    return result;
}

static Atom *prove(Call *c, Atom *stmt, Atom *dummies, Atom *rows, Atom *root_arg) {
    stmt = unquote(stmt);
    rows = unquote(rows);
    uint64_t root_id;
    if (!head_is(stmt, "theorem", 5) || !is_list(rows) || !uint_arg(root_arg, &root_id)) {
        fail(c, MM0_MALFORMED, "expected (theorem NAME BINDERS HYPS CONCL), rows and a root ID");
        return NULL;
    }
    const char *name = name_arg(stmt->expr.elems[1]);
    if (!name) { fail(c, MM0_MALFORMED, "theorem name"); return NULL; }
    Mm0Options o = options_of(c);
    Reader r = {.c = c, .d = mm0_decl_new(c->theory->theory, &o)};
    IdMap ids = {0};
    Atom *result = NULL;
    if (!r.d) { fail(c, MM0_NOMEM, "declaration allocation"); return NULL; }
    bool ok = read_binders(&r, stmt->expr.elems[2]) && read_dummies(&r, dummies) &&
              read_statement(&r, stmt->expr.elems[3], stmt->expr.elems[4]);
    for (size_t i = 0; ok && i < rows->expr.len; ++i) {
        if (interrupted(NULL)) ok = fail(c, MM0_INTERRUPTED, "interrupted");
        else ok = read_row(&r, &ids, rows->expr.elems[i]);
    }
    uint32_t root = 0, index;
    if (ok && !id_get(&ids, root_id, &root)) ok = fail(c, MM0_REFUSED, "proof root not checked");
    /* one transaction: the handle exists before the theorem is admitted */
    Reservation res = {0};
    if (ok) ok = reserve(c, &res);
    if (ok) {
        mm0_decl_finish_theorem(r.d, name, root, &index);
        ok = decl_ok(&r);
    }
    if (ok) result = commit(&res, index);
    else cancel(c, &res);
    free(ids.slots);
    reader_free(&r);
    mm0_decl_free(r.d);
    return result;
}

static bool sort_mods(Call *c, Atom *list, uint8_t *mods) {
    list = unquote(list);
    if (!is_list(list)) return fail(c, MM0_MALFORMED, "sort modifiers");
    *mods = 0;
    for (size_t i = 0; i < list->expr.len; ++i) {
        Atom *m = list->expr.elems[i];
        if (atom_is_symbol(m, "pure")) *mods |= MM0_SORT_PURE;
        else if (atom_is_symbol(m, "strict")) *mods |= MM0_SORT_STRICT;
        else if (atom_is_symbol(m, "provable")) *mods |= MM0_SORT_PROVABLE;
        else if (atom_is_symbol(m, "free")) *mods |= MM0_SORT_FREE;
        else return fail(c, MM0_MALFORMED, "sort modifier: pure, strict, provable or free");
    }
    return true;
}

/* (SORT (DEP ...)) */
static bool ret_type(Reader *r, Atom *x, uint32_t *sort, uint32_t **deps, uint32_t *ndeps) {
    if (!is_list(x) || x->expr.len != 2 || !is_list(x->expr.elems[1]))
        return fail(r->c, MM0_MALFORMED, "return type: (SORT (DEPS))");
    if (!read_sort(r, x->expr.elems[0], sort)) return false;
    Atom *dl = x->expr.elems[1];
    *ndeps = (uint32_t)dl->expr.len;
    *deps = malloc((*ndeps ? *ndeps : 1) * sizeof **deps);
    if (!*deps) return fail(r->c, MM0_NOMEM, "reader allocation");
    for (uint32_t i = 0; i < *ndeps; ++i) {
        const char *v = dl->expr.elems[i]->kind == ATOM_SYMBOL ? atom_name_cstr(dl->expr.elems[i]) : NULL;
        if (!v || !mm0_names_get(&r->vars, v, strlen(v), &(*deps)[i])) {
            free(*deps);
            *deps = NULL;
            return fail(r->c, MM0_MALFORMED, "unknown dependency variable");
        }
    }
    return true;
}

static Atom *admitted(Call *c, const char *kind, const char *name) {
    Atom *items[3] = {atom_symbol(c->arena, "mm0:admitted"), atom_symbol(c->arena, kind),
                      atom_symbol(c->arena, name)};
    return atom_expr(c->arena, items, 3);
}

/* (sort NAME (MODS)) | (term NAME BINDERS RET) | (def NAME BINDERS RET DUMMIES BODY)
 * | (axiom NAME BINDERS HYPS CONCL) */
static Atom *declare(Call *c, Atom *decl) {
    decl = unquote(decl);
    const char *name = is_list(decl) && decl->expr.len >= 3 ? name_arg(decl->expr.elems[1]) : NULL;
    if (!name) { fail(c, MM0_MALFORMED, "declaration: (sort|term|def|axiom NAME ...)"); return NULL; }
    if (head_is(decl, "sort", 3)) {
        uint8_t mods;
        Mm0Error e;
        if (!sort_mods(c, decl->expr.elems[2], &mods)) return NULL;
        if (mm0_add_sort(c->theory->theory, name, mods, &e) != MM0_OK) { fail_error(c, &e); return NULL; }
        return admitted(c, "sort", name);
    }
    if (head_is(decl, "theorem", 5)) {
        fail(c, MM0_MALFORMED, "a theorem needs a proof: use mm0:prove");
        return NULL;
    }
    bool term = head_is(decl, "term", 4), def = head_is(decl, "def", 6), axiom = head_is(decl, "axiom", 5);
    if (!term && !def && !axiom) {
        fail(c, MM0_MALFORMED, "declaration: (sort|term|def|axiom NAME ...)");
        return NULL;
    }
    Mm0Options o = options_of(c);
    Reader r = {.c = c, .d = mm0_decl_new(c->theory->theory, &o)};
    if (!r.d) { fail(c, MM0_NOMEM, "declaration allocation"); return NULL; }
    Atom *result = NULL;
    uint32_t index, sort = 0, ndeps = 0, *deps = NULL;
    bool ok = read_binders(&r, decl->expr.elems[2]);
    if (ok && axiom) {
        ok = read_statement(&r, decl->expr.elems[3], decl->expr.elems[4]);
        Reservation res = {0};
        if (ok) ok = reserve(c, &res);
        if (ok) { mm0_decl_finish_axiom(r.d, name, &index); ok = decl_ok(&r); }
        if (ok) result = commit(&res, index);
        else cancel(c, &res);
    } else if (ok) {
        ok = (!def || read_dummies(&r, decl->expr.elems[4])) &&
             ret_type(&r, decl->expr.elems[3], &sort, &deps, &ndeps);
        Mm0Node *body = NULL;
        if (ok && def) ok = read_expr(&r, unquote(decl->expr.elems[5]), &body);
        if (ok) {
            if (def) mm0_decl_finish_def(r.d, name, sort, deps, ndeps, body, &index);
            else mm0_decl_finish_term(r.d, name, sort, deps, ndeps, &index);
            ok = decl_ok(&r);
        }
        if (ok) result = admitted(c, def ? "def" : "term", name);
    }
    free(deps);
    reader_free(&r);
    mm0_decl_free(r.d);
    return result;
}

static Atom *theory_call(Call *c, const char *op, Atom **args, uint32_t n) {
    Mm0Theory *th = c->theory->theory;
    if (!strcmp(op, "load-mmb") && n == 3) return load_mmb(c, args[1], args[2], false);
    if (!strcmp(op, "declare") && n == 2) return declare(c, args[1]);
    if (!strcmp(op, "prove") && n == 5) return prove(c, args[1], args[2], args[3], args[4]);
    if (!strcmp(op, "theorem") && n == 2) {
        const char *name = name_arg(args[1]);
        uint32_t index;
        if (!name || !mm0_names_get(&th->thm_names, name, strlen(name), &index)) {
            fail(c, MM0_MALFORMED, "unknown MM0 theorem");
            return NULL;
        }
        return publish_thm(c, index);
    }
    if (!strcmp(op, "stats") && n == 1) {
        Mm0TheoryStats s = mm0_theory_stats(th);
        Atom *items[5] = {atom_symbol(c->arena, "Mm0Stats"), atom_int(c->arena, (int64_t)s.sorts),
                          atom_int(c->arena, (int64_t)s.terms), atom_int(c->arena, (int64_t)s.theorems),
                          atom_int(c->arena, (int64_t)s.bytes)};
        return atom_expr(c->arena, items, 5);
    }
    if (!strcmp(op, "budget") && n == 2) {
        Atom *x = args[1];
        if (!x || x->kind != ATOM_GROUNDED || x->ground.gkind != GV_INT || x->ground.ival < -1) {
            fail(c, MM0_MALFORMED, "budget: steps, or -1 for none");
            return NULL;
        }
        c->theory->budget = x->ground.ival < 0 ? UINT64_MAX : (uint64_t)x->ground.ival;
        return atom_symbol(c->arena, "ok");
    }
    fail(c, MM0_MALFORMED, "MM0 theory operation or arity");
    return NULL;
}

Atom *cetta_mm0_dispatch(struct CettaLibraryContext *host, Space *space, Arena *arena,
                         Atom *head, Atom **args, uint32_t n) {
    (void)space;
    static const char prefix[] = "__cetta_lib_mm0_";
    if (!head || head->kind != ATOM_SYMBOL || strncmp(atom_name_cstr(head), prefix, sizeof prefix - 1))
        return NULL;
    const char *op = atom_name_cstr(head) + sizeof prefix - 1;
    Call c = {.host = host, .arena = arena, .status = MM0_OK};
    Atom *result = NULL;
    if (interrupted(NULL)) {
        fail(&c, MM0_INTERRUPTED, "interrupted");
    } else if (!strcmp(op, "theory") && n == 1 && atom_is_symbol(args[0], "empty")) {
        /* a constant argument: a nullary native head is data in Prime */
        result = new_theory(&c);
    } else if (!strcmp(op, "check-mmb") && n == 2) {
        result = load_mmb(&c, args[0], args[1], true);
    } else if (!strcmp(op, "live") && n == 1 && atom_is_symbol(args[0], "all")) {
        Atom *items[3] = {atom_symbol(arena, "Mm0Live"),
                          atom_int(arena, (int64_t)atomic_load(&g_live_theories)),
                          atom_int(arena, (int64_t)atomic_load(&g_live_theorems))};
        result = atom_expr(arena, items, 3);
    } else if ((!strcmp(op, "statement") && n == 1) || (!strcmp(op, "same-declaration") && n == 2)) {
        Held *h = held_of(host, args[0]);
        Held *h2 = n == 2 ? held_of(host, args[1]) : NULL;
        if (!h || (n == 2 && !h2)) {
            fail(&c, MM0_MALFORMED, "expected a live MM0 theorem");
        } else if (n == 2) {
            result = atom_symbol(arena, h->owner == h2->owner && h->index == h2->index ? "true" : "false");
        } else {
            c.theory = h->owner;
            pthread_mutex_lock(&h->owner->gate);
            result = statement(&c, h->index);
            pthread_mutex_unlock(&h->owner->gate);
        }
    } else {
        Theory *t = n ? sealed_resource(args[0], &g_theory_kind) : NULL;
        if (!t) {
            fail(&c, MM0_MALFORMED, "expected a live MM0 theory");
        } else {
            c.theory = t;
            pthread_mutex_lock(&t->gate);
            result = theory_call(&c, op, args, n);
            pthread_mutex_unlock(&t->gate);
        }
    }
    if (c.status != MM0_OK) return error_atom(&c);
    if (!result) { fail(&c, MM0_MALFORMED, "missing result"); return error_atom(&c); }
    return result;
}
