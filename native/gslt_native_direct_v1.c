/*
 * The C target of the direct GSLT compilers: the selected modes of a closed
 * entry residual, every one functional, as one C translation unit over the
 * runtime in gslt_native_runtime_v1.h.
 *
 * Each mode is a function from its input arguments to its outputs, false
 * when the relation has no answer.  Its rules are tried in their authored
 * order; a rule matches its head's input patterns against the arguments,
 * runs its premises left to right, and builds the head's output terms:
 *
 *   - a premise calling a relation runs that relation in the mode its
 *     arguments' groundness selects (the analysis selected it and proved it
 *     functional), then matches the outputs against the premise's patterns;
 *   - (gslt:different (different A B)) succeeds when the ground terms A and
 *     B differ.
 *
 * Because every mode is functional, the first rule with an answer gives the
 * only answer.  A rule's last premise is a jump rather than a call when the
 * rule decides the mode's answer once the premise is reached (it is the
 * mode's last rule, or the mode's input heads are disjoint) and the premise
 * calls a mode of the same tail group (a cycle of such calls): the head's
 * outputs are written first, each output of the premise left as a hole the
 * callee fills.  A loop over the input then runs in constant stack.  The source list that langdef reads ((cons (cp N) ...) ending
 * in nil) is never built by the presentations, only taken apart, so the
 * runtime reads it as a cursor over the file's code points; a rule that
 * builds cons or nil is refused.  So is anything else outside this fragment:
 * the target never chooses a meaning the analysis did not establish.
 *
 * Each mode's entry, each rule's answer or miss, and a mode's having no
 * answer carry a trace hook (GN_TRACE_CALL, _RULE, _MISS, _FAIL), which is
 * no code unless the program is built with -DGN_TRACE
 * (gslt_native_runtime_v1.h).
 */
#include "gslt_direct_internal_v1.h"
#include "gslt_native_runtime_v1.h"   /* the decoding literal texts must read back by */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    size_t len, cap;
    bool nomem;
} NBufV1;

static void nb_put(NBufV1 *b, const char *text, size_t n) {
    if (b->nomem)
        return;
    if (b->len + n + 1u > b->cap) {
        size_t cap = b->cap ? b->cap * 2u : 65536u;
        while (cap < b->len + n + 1u)
            cap *= 2u;
        uint8_t *grown = realloc(b->bytes, cap);
        if (!grown) {
            b->nomem = true;
            return;
        }
        b->bytes = grown;
        b->cap = cap;
    }
    memcpy(b->bytes + b->len, text, n);
    b->len += n;
    b->bytes[b->len] = 0;
}

static void nb_puts(NBufV1 *b, const char *text) { nb_put(b, text, strlen(text)); }

static void nb_printf(NBufV1 *b, const char *format, ...) {
    char small[1024];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(small, sizeof small, format, args);
    va_end(args);
    if (n < 0) {
        b->nomem = true;
        return;
    }
    if ((size_t)n < sizeof small) {
        nb_put(b, small, (size_t)n);
        return;
    }
    char *big = malloc((size_t)n + 1u);
    if (!big) {
        b->nomem = true;
        return;
    }
    va_start(args, format);
    vsnprintf(big, (size_t)n + 1u, format, args);
    va_end(args);
    nb_put(b, big, (size_t)n);
    free(big);
}

static bool native_error(char *error, size_t cap, const char *format, ...) {
    if (error && cap) {
        va_list args;
        va_start(args, format);
        vsnprintf(error, cap, format, args);
        va_end(args);
    }
    return false;
}

/* The prefix of the exported names (PREFIX_program, PREFIX_entry_R_BITS),
 * so that several compiled programs link into one binary. */
static char g_native_prefix[128] = "gn";

void cetta_gslt_native_set_prefix_v1(const char *prefix) {
    snprintf(g_native_prefix, sizeof g_native_prefix, "%s", prefix && *prefix ? prefix : "gn");
}

/* A name as a C string literal's contents (quotes, backslashes and control
 * bytes escaped). */
static void nb_c_string(NBufV1 *b, const char *name) {
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if (*p == '"' || *p == '\\')
            nb_printf(b, "\\%c", *p);
        else if (*p < 32 || *p == 127)
            nb_printf(b, "\\%03o", *p);
        else
            nb_put(b, (const char *)p, 1u);
    }
}

/* A name inside a C comment: "*" followed by "/" never appears. */
static void nb_comment(NBufV1 *b, const char *name) {
    for (const char *p = name; *p; p++) {
        nb_put(b, p, 1u);
        if (p[0] == '*' && p[1] == '/')
            nb_put(b, " ", 1u);
    }
}

/* ── constructors ─────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    size_t arity;
} NOpV1;

typedef struct {
    NOpV1 *items;
    size_t len, cap;
} NOpsV1;

/* Constructor ids start after the runtime's own kinds. */
enum { NATIVE_FIRST_OP_V1 = 3 };   /* GN_FIRST_OP */

/* A trace hook (gslt_native_runtime_v1.h, GN_TRACE): INDENT HOOK(cx, "NAME" ARGS); */
static void nb_trace(NBufV1 *b, const char *indent, const char *hook, const char *name, const char *args) {
    nb_printf(b, "%sGN_TRACE_%s(cx, \"", indent, hook);
    nb_c_string(b, name);
    nb_printf(b, "\"%s);\n", args);
}

static bool is_var(const Atom *a) { return cetta_gslt_source_variable_v1(a); }

static const char *sym(const Atom *a) {
    return a && a->kind == ATOM_SYMBOL ? atom_name_cstr((Atom *)a) : NULL;
}

static bool is_int(const Atom *a, int64_t *v) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT)
        return false;
    *v = a->ground.ival;
    return true;
}

static long op_find(const NOpsV1 *ops, const char *name, size_t arity) {
    for (size_t i = 0; i < ops->len; i++)
        if (ops->items[i].arity == arity && strcmp(ops->items[i].name, name) == 0)
            return (long)i;
    return -1;
}

static bool op_add(NOpsV1 *ops, const char *name, size_t arity) {
    if (op_find(ops, name, arity) >= 0)
        return true;
    if (ops->len == ops->cap) {
        size_t cap = ops->cap ? ops->cap * 2u : 256u;
        NOpV1 *grown = realloc(ops->items, cap * sizeof *grown);
        if (!grown)
            return false;
        ops->items = grown;
        ops->cap = cap;
    }
    ops->items[ops->len++] = (NOpV1){name, arity};
    return true;
}

static const DirectBindingModeV1 *mode_find(const DirectBindingModesV1 *modes, const char *relation,
                                            size_t arity, const uint8_t *bits) {
    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (m->arity == arity && strcmp(m->relation, relation) == 0 &&
            memcmp(m->input_ground, bits, arity) == 0)
            return m;
    }
    return NULL;
}

static bool is_relation(const DirectBindingModesV1 *modes, const char *name, size_t arity) {
    for (size_t i = 0; i < modes->len; i++)
        if (modes->items[i].arity == arity && strcmp(modes->items[i].relation, name) == 0)
            return true;
    return false;
}

/* Every constructor a term mentions (relations' heads excluded). */
static bool collect_ops(const Atom *t, const DirectBindingModesV1 *modes, NOpsV1 *ops, bool as_call) {
    if (is_var(t))
        return true;
    int64_t v;
    if (is_int(t, &v))
        return true;
    const char *s = sym(t);
    if (s)
        return op_add(ops, s, 0u);
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u)
        return false;
    const char *head = sym(t->expr.elems[0]);
    if (!head)
        return false;
    size_t n = t->expr.len - 1u;
    if (!(as_call && is_relation(modes, head, n)) && !op_add(ops, head, n))
        return false;
    for (size_t i = 1; i < t->expr.len; i++)
        if (!collect_ops(t->expr.elems[i], modes, ops, false))
            return false;
    return true;
}

/* ── scans ────────────────────────────────────────────────────────────── */

/*
 * A class run is a mode with one input, the source list, and two outputs,
 * the run and the list after it, whose rules say, a step at a time:
 *
 *   at the end, or at a scalar outside a class K:   (NIL, the input)
 *   at a scalar C in K, with the rest R:              ((CONS C RUN), AFTER),
 *                                                     (RUN, AFTER) the mode's
 *                                                     answer on R
 *
 * for constructors CONS and NIL of the program.  By induction on the list,
 * its answer is the longest stretch of K at the list's start and the list
 * after it, which the runtime scans (gn_scan_class) and returns as a span.
 *
 * The rules are read by evaluating one step symbolically.  The input is the
 * source list; its first scalar is unknown but for the ranges the rules'
 * tests leave it.  The relations the rules call (a peek, a class table, the
 * run's own helpers) are evaluated in place, rule by rule in their order,
 * so a rule answers exactly where every earlier one did not.  Anything the
 * evaluation cannot decide leaves the mode as compiled.
 */

typedef struct NScanBlockV1 NScanBlockV1;
struct NScanBlockV1 {
    NScanBlockV1 *next;
    size_t used, cap;
    _Alignas(16) unsigned char data[];
};

/* Scalars as sorted disjoint closed ranges (in the evaluation's arena). */
typedef struct {
    uint32_t (*r)[2];
    size_t len;
} NSetV1;

/* Where the input list may be: at its end (end), or at a first scalar in
 * set. */
typedef struct {
    bool end;
    NSetV1 set;
} NCondV1;

typedef enum { NSV_CUR, NSV_SCALAR, NSV_INT, NSV_APP, NSV_HOLE } NSvKindV1;

/* A value of the evaluation: the source list from scalar pos on (pos 0 the
 * input, 1 its rest), the scalar at pos as (cp N), an integer, a
 * constructor application, or output n of the mode's own answer on the rest
 * (a hole). */
typedef struct NSvV1 NSvV1;
struct NSvV1 {
    NSvKindV1 kind;
    int pos;
    int64_t n;
    long op;
    size_t nk;
    NSvV1 **kid;
};

typedef struct NBindV1 NBindV1;
struct NBindV1 {
    const char *name;
    NSvV1 *v;
    const NBindV1 *next;
};

typedef struct {
    NCondV1 cond;
    const NBindV1 *b;
} NPathV1;

typedef struct {
    NPathV1 *items;
    size_t len, cap;
} NPathsV1;

/* An answer of a mode: where, its outputs, and whether it is the run's
 * step (the mode's own answer on the rest gave its holes). */
typedef struct {
    NCondV1 cond;
    NSvV1 *out[8];
    bool loop;
} NResV1;

typedef struct {
    NResV1 *items;
    size_t len, cap;
} NResultsV1;

typedef struct {
    NScanBlockV1 *blocks;
    bool failed;              /* out of memory, or something the evaluation cannot read */
    const DirectRulesV1 *rules;
    const DirectBindingModesV1 *modes;
    const NOpsV1 *ops;
    long op_cons, op_nil, op_cp;
    const DirectBindingModeV1 *entry;
    size_t entry_input, steps;
} NScanCtxV1;

enum { NSCAN_DEPTH_V1 = 16, NSCAN_STEPS_V1 = 200000, NSCAN_MAX_SCALAR_V1 = 0x10FFFF };

static void *sc_alloc(NScanCtxV1 *c, size_t n) {
    n = (n + 15u) & ~(size_t)15u;
    NScanBlockV1 *b = c->blocks;
    if (!b || b->cap - b->used < n) {
        size_t cap = n > (1u << 16) ? n : (1u << 16);
        b = malloc(sizeof *b + cap);
        if (!b) {
            c->failed = true;
            return NULL;
        }
        b->next = c->blocks;
        b->used = 0;
        b->cap = cap;
        c->blocks = b;
    }
    void *p = b->data + b->used;
    b->used += n;
    return p;
}

static void sc_free(NScanCtxV1 *c) {
    for (NScanBlockV1 *b = c->blocks; b;) {
        NScanBlockV1 *next = b->next;
        free(b);
        b = next;
    }
    c->blocks = NULL;
}

static bool sc_unread(NScanCtxV1 *c) {
    c->failed = true;
    return false;
}

/* ── sets and conditions ── */

static NSetV1 set_new(NScanCtxV1 *c, size_t cap) {
    NSetV1 s = {cap ? sc_alloc(c, cap * sizeof *s.r) : NULL, 0u};
    if (cap && !s.r)
        s.len = 0u;
    return s;
}

static void set_push(NSetV1 *s, uint32_t lo, uint32_t hi) {
    if (s->r == NULL)
        return;
    if (s->len && s->r[s->len - 1u][1] + 1u >= lo) {
        if (hi > s->r[s->len - 1u][1])
            s->r[s->len - 1u][1] = hi;
        return;
    }
    s->r[s->len][0] = lo;
    s->r[s->len][1] = hi;
    s->len++;
}

static NSetV1 set_one(NScanCtxV1 *c, uint32_t lo, uint32_t hi) {
    NSetV1 s = set_new(c, 1u);
    set_push(&s, lo, hi);
    return s;
}

static NSetV1 set_union(NScanCtxV1 *c, NSetV1 a, NSetV1 b) {
    NSetV1 s = set_new(c, a.len + b.len + 1u);
    size_t i = 0, j = 0;
    while (i < a.len || j < b.len) {
        bool left = j == b.len || (i < a.len && a.r[i][0] <= b.r[j][0]);
        uint32_t (*x)[2] = left ? &a.r[i++] : &b.r[j++];
        set_push(&s, (*x)[0], (*x)[1]);
    }
    return s;
}

static NSetV1 set_inter(NScanCtxV1 *c, NSetV1 a, NSetV1 b) {
    NSetV1 s = set_new(c, a.len + b.len + 1u);
    for (size_t i = 0, j = 0; i < a.len && j < b.len;) {
        uint32_t lo = a.r[i][0] > b.r[j][0] ? a.r[i][0] : b.r[j][0];
        uint32_t hi = a.r[i][1] < b.r[j][1] ? a.r[i][1] : b.r[j][1];
        if (lo <= hi)
            set_push(&s, lo, hi);
        if (a.r[i][1] < b.r[j][1])
            i++;
        else
            j++;
    }
    return s;
}

/* All scalars but those of b, intersected with a. */
static NSetV1 set_minus(NScanCtxV1 *c, NSetV1 a, NSetV1 b) {
    NSetV1 rest = set_new(c, b.len + 2u);
    uint32_t from = 0;
    for (size_t j = 0; j < b.len; j++) {
        if (b.r[j][0] > from)
            set_push(&rest, from, b.r[j][0] - 1u);
        from = b.r[j][1] + 1u;
    }
    if (from <= NSCAN_MAX_SCALAR_V1)
        set_push(&rest, from, NSCAN_MAX_SCALAR_V1);
    return set_inter(c, a, rest);
}

static bool set_equal(NSetV1 a, NSetV1 b) {
    if (a.len != b.len)
        return false;
    for (size_t i = 0; i < a.len; i++)
        if (a.r[i][0] != b.r[i][0] || a.r[i][1] != b.r[i][1])
            return false;
    return true;
}

static bool cond_empty(NCondV1 x) { return !x.end && x.set.len == 0u; }

static NCondV1 cond_inter(NScanCtxV1 *c, NCondV1 a, NCondV1 b) {
    return (NCondV1){a.end && b.end, set_inter(c, a.set, b.set)};
}

static NCondV1 cond_minus(NScanCtxV1 *c, NCondV1 a, NCondV1 b) {
    return (NCondV1){a.end && !b.end, set_minus(c, a.set, b.set)};
}

static NCondV1 cond_union(NScanCtxV1 *c, NCondV1 a, NCondV1 b) {
    return (NCondV1){a.end || b.end, set_union(c, a.set, b.set)};
}

static NCondV1 cond_all(NScanCtxV1 *c) { return (NCondV1){true, set_one(c, 0u, NSCAN_MAX_SCALAR_V1)}; }

/* ── values ── */

static NSvV1 *sv_new(NScanCtxV1 *c, NSvKindV1 kind, size_t nk) {
    NSvV1 *v = sc_alloc(c, sizeof *v);
    NSvV1 **kid = nk ? sc_alloc(c, nk * sizeof *kid) : NULL;
    if (!v || (nk && !kid))
        return NULL;
    *v = (NSvV1){kind, 0, 0, -1, nk, kid};
    return v;
}

static const NBindV1 *bind_find(const NBindV1 *b, const char *name) {
    for (; b; b = b->next)
        if (strcmp(b->name, name) == 0)
            return b;
    return NULL;
}

static const NBindV1 *bind_add(NScanCtxV1 *c, const NBindV1 *b, const char *name, NSvV1 *v) {
    NBindV1 *n = sc_alloc(c, sizeof *n);
    if (!n)
        return NULL;
    *n = (NBindV1){name, v, b};
    return n;
}

/* Whether every variable of t is bound. */
static bool sv_bound(const Atom *t, const NBindV1 *b) {
    if (is_var(t))
        return bind_find(b, cetta_gslt_source_variable_name_v1(t)) != NULL;
    if (t && t->kind == ATOM_EXPR)
        for (size_t i = 1; i < t->expr.len; i++)
            if (!sv_bound(t->expr.elems[i], b))
                return false;
    return true;
}

/* The value of the term t, its variables bound. */
static NSvV1 *sv_term(NScanCtxV1 *c, const Atom *t, const NBindV1 *b) {
    if (is_var(t)) {
        const NBindV1 *x = bind_find(b, cetta_gslt_source_variable_name_v1(t));
        if (!x)
            sc_unread(c);
        return x ? x->v : NULL;
    }
    int64_t n;
    if (is_int(t, &n)) {
        NSvV1 *v = sv_new(c, NSV_INT, 0u);
        if (v)
            v->n = n;
        return v;
    }
    const char *s = sym(t);
    if (s) {
        long id = op_find(c->ops, s, 0u);
        if (id < 0 || id == c->op_nil) {
            sc_unread(c);
            return NULL;
        }
        NSvV1 *v = sv_new(c, NSV_APP, 0u);
        if (v)
            v->op = id;
        return v;
    }
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u || !sym(t->expr.elems[0])) {
        sc_unread(c);
        return NULL;
    }
    size_t arity = t->expr.len - 1u;
    long id = op_find(c->ops, sym(t->expr.elems[0]), arity);
    if (id < 0 || id == c->op_cons) {
        sc_unread(c);
        return NULL;
    }
    NSvV1 *v = sv_new(c, NSV_APP, arity);
    if (!v)
        return NULL;
    v->op = id;
    for (size_t i = 0; i < arity; i++)
        if (!(v->kid[i] = sv_term(c, t->expr.elems[i + 1u], b)))
            return NULL;
    return v;
}

/* Whether v is known without the input: no list, scalar or hole in it. */
static bool sv_static(const NSvV1 *v) {
    if (v->kind == NSV_INT)
        return true;
    if (v->kind != NSV_APP)
        return false;
    for (size_t i = 0; i < v->nk; i++)
        if (!sv_static(v->kid[i]))
            return false;
    return true;
}

static bool sv_same_static(const NSvV1 *a, const NSvV1 *b) {
    if (a->kind != b->kind)
        return false;
    if (a->kind == NSV_INT)
        return a->n == b->n;
    if (a->op != b->op || a->nk != b->nk)
        return false;
    for (size_t i = 0; i < a->nk; i++)
        if (!sv_same_static(a->kid[i], b->kid[i]))
            return false;
    return true;
}

/* The integer N of a static (cp N), or -1. */
static int64_t sv_cp_int(const NScanCtxV1 *c, const NSvV1 *v) {
    if (v->kind == NSV_APP && v->op == c->op_cp && v->nk == 1u && v->kid[0]->kind == NSV_INT &&
        v->kid[0]->n >= 0 && v->kid[0]->n <= NSCAN_MAX_SCALAR_V1)
        return v->kid[0]->n;
    return -1;
}

static bool paths_push(NScanCtxV1 *c, NPathsV1 *ps, NCondV1 cond, const NBindV1 *b) {
    if (cond_empty(cond))
        return true;
    if (ps->len == ps->cap) {
        size_t cap = ps->cap ? ps->cap * 2u : 8u;
        NPathV1 *grown = sc_alloc(c, cap * sizeof *grown);
        if (!grown)
            return false;
        if (ps->len)
            memcpy(grown, ps->items, ps->len * sizeof *grown);
        ps->items = grown;
        ps->cap = cap;
    }
    ps->items[ps->len++] = (NPathV1){cond, b};
    return true;
}

/* The condition where values a and b are equal (decided), into *yes and
 * *no; false when the evaluation cannot decide it. */
static bool sv_compare(NScanCtxV1 *c, const NSvV1 *a, const NSvV1 *b, NCondV1 cond, NCondV1 *yes, NCondV1 *no) {
    if (a->kind == NSV_HOLE || b->kind == NSV_HOLE)
        return sc_unread(c);
    if (a->kind == NSV_SCALAR && b->kind == NSV_SCALAR && a->pos == b->pos) {
        *yes = cond;
        *no = (NCondV1){false, {NULL, 0u}};
        return true;
    }
    if (a->kind == NSV_CUR && b->kind == NSV_CUR && a->pos == b->pos) {
        *yes = cond;
        *no = (NCondV1){false, {NULL, 0u}};
        return true;
    }
    const NSvV1 *s = a->kind == NSV_SCALAR ? a : b->kind == NSV_SCALAR ? b : NULL;
    const NSvV1 *k = s == a ? b : a;
    if (s && s->pos == 0 && sv_static(k)) {
        int64_t n = sv_cp_int(c, k);
        NCondV1 at = {false, n >= 0 ? set_one(c, (uint32_t)n, (uint32_t)n) : (NSetV1){NULL, 0u}};
        *yes = cond_inter(c, cond, at);
        *no = cond_minus(c, cond, at);
        return true;
    }
    if (sv_static(a) && sv_static(b)) {
        bool same = sv_same_static(a, b);
        *yes = same ? cond : (NCondV1){false, {NULL, 0u}};
        *no = same ? (NCondV1){false, {NULL, 0u}} : cond;
        return true;
    }
    return sc_unread(c);
}

/* The paths, from `path`, where the pattern p matches the value v. */
static bool sc_match(NScanCtxV1 *c, const Atom *p, NSvV1 *v, NPathV1 path, NPathsV1 *out) {
    if (++c->steps > NSCAN_STEPS_V1)
        return sc_unread(c);
    if (is_var(p)) {
        const char *name = cetta_gslt_source_variable_name_v1(p);
        const NBindV1 *x = bind_find(path.b, name);
        if (!x) {
            const NBindV1 *b = bind_add(c, path.b, name, v);
            return b && paths_push(c, out, path.cond, b);
        }
        NCondV1 yes, no;
        return sv_compare(c, x->v, v, path.cond, &yes, &no) && paths_push(c, out, yes, path.b);
    }
    if (v->kind == NSV_HOLE)
        return sc_unread(c);
    int64_t n;
    if (is_int(p, &n))
        return v->kind == NSV_INT ? (v->n == n ? paths_push(c, out, path.cond, path.b) : true) : sc_unread(c);
    const char *s = sym(p);
    if (s) {
        long id = op_find(c->ops, s, 0u);
        if (id == c->op_nil) {
            if (v->kind != NSV_CUR || v->pos != 0)
                return v->kind == NSV_APP ? true : sc_unread(c);
            return paths_push(c, out, (NCondV1){path.cond.end, {NULL, 0u}}, path.b);
        }
        if (v->kind == NSV_CUR || v->kind == NSV_SCALAR || v->kind == NSV_INT)
            return true;   /* a list, a (cp N) or an integer is never this constant */
        return v->op == id && v->nk == 0u ? paths_push(c, out, path.cond, path.b) : true;
    }
    if (!p || p->kind != ATOM_EXPR || p->expr.len == 0u || !sym(p->expr.elems[0]))
        return sc_unread(c);
    size_t arity = p->expr.len - 1u;
    long id = op_find(c->ops, sym(p->expr.elems[0]), arity);
    if (id == c->op_cons) {
        if (v->kind != NSV_CUR)
            return v->kind == NSV_APP || v->kind == NSV_SCALAR || v->kind == NSV_INT ? true : sc_unread(c);
        if (v->pos != 0)
            return sc_unread(c);   /* looking past the first scalar */
        NSvV1 *h = sv_new(c, NSV_SCALAR, 0u), *t = sv_new(c, NSV_CUR, 0u);
        if (!h || !t)
            return false;
        h->pos = 0;
        t->pos = 1;
        NPathsV1 heads = {0};
        if (!sc_match(c, p->expr.elems[1], h, (NPathV1){{false, path.cond.set}, path.b}, &heads))
            return false;
        for (size_t i = 0; i < heads.len; i++)
            if (!sc_match(c, p->expr.elems[2], t, heads.items[i], out))
                return false;
        return true;
    }
    if (id == c->op_cp && v->kind == NSV_SCALAR) {
        int64_t k;
        if (v->pos != 0 || !is_int(p->expr.elems[1], &k))
            return sc_unread(c);
        if (k < 0 || k > NSCAN_MAX_SCALAR_V1)
            return true;
        NCondV1 at = {false, set_one(c, (uint32_t)k, (uint32_t)k)};
        return paths_push(c, out, cond_inter(c, path.cond, at), path.b);
    }
    if (v->kind == NSV_CUR || v->kind == NSV_SCALAR || v->kind == NSV_INT)
        return true;
    if (v->op != id || v->nk != arity)
        return true;
    NPathsV1 cur = {0};
    if (!paths_push(c, &cur, path.cond, path.b))
        return false;
    for (size_t i = 0; i < arity; i++) {
        NPathsV1 next = {0};
        for (size_t j = 0; j < cur.len; j++)
            if (!sc_match(c, p->expr.elems[i + 1u], v->kid[i], cur.items[j], &next))
                return false;
        cur = next;
    }
    for (size_t j = 0; j < cur.len; j++)
        if (!paths_push(c, out, cur.items[j].cond, cur.items[j].b))
            return false;
    return true;
}

static bool res_push(NScanCtxV1 *c, NResultsV1 *rs, const NResV1 *r) {
    if (cond_empty(r->cond))
        return true;
    if (rs->len == rs->cap) {
        size_t cap = rs->cap ? rs->cap * 2u : 8u;
        NResV1 *grown = sc_alloc(c, cap * sizeof *grown);
        if (!grown)
            return false;
        if (rs->len)
            memcpy(grown, rs->items, rs->len * sizeof *grown);
        rs->items = grown;
        rs->cap = cap;
    }
    rs->items[rs->len++] = *r;
    return true;
}

static size_t sc_outputs(const DirectBindingModeV1 *m) {
    size_t n = 0;
    for (size_t a = 0; a < m->arity; a++)
        n += m->input_ground[a] ? 0u : 1u;
    return n;
}

static bool sc_mode(NScanCtxV1 *c, const DirectBindingModeV1 *m, NSvV1 *const *in, NCondV1 cond, unsigned depth,
                    bool tail, NResultsV1 *out);

/* The answers of rule `rule` of mode m from premise k on, along `path`. */
static bool sc_body(NScanCtxV1 *c, const DirectRuleV1 *rule, const DirectBindingModeV1 *m, size_t k, NPathV1 path,
                    unsigned depth, bool tail, bool loop, NResultsV1 *out) {
    const Atom *body = rule->body;
    size_t premises = body && body->kind == ATOM_EXPR && body->expr.len ? body->expr.len - 1u : 0u;
    if (k == premises) {
        NResV1 r = {path.cond, {0}, loop};
        size_t o = 0;
        for (size_t a = 0; a < m->arity; a++) {
            if (m->input_ground[a])
                continue;
            if (o == 8u || !(r.out[o++] = sv_term(c, rule->head->expr.elems[a + 1u], path.b)))
                return sc_unread(c);
        }
        return res_push(c, out, &r);
    }
    if (loop)
        return sc_unread(c);   /* the run's own answer used before the rule ends */
    const Atom *p = body->expr.elems[k + 1u];
    const char *head = p && p->kind == ATOM_EXPR && p->expr.len ? sym(p->expr.elems[0]) : NULL;
    if (!head)
        return sc_unread(c);
    bool last = k + 1u == premises;
    if (strcmp(head, "gslt:different") == 0 || (strcmp(head, "different") == 0 && p->expr.len == 3u)) {
        const Atom *d = strcmp(head, "different") == 0 ? p : p->expr.len == 2u ? p->expr.elems[1] : NULL;
        if (!d || d->kind != ATOM_EXPR || d->expr.len != 3u)
            return sc_unread(c);
        NSvV1 *x = sv_term(c, d->expr.elems[1], path.b), *y = x ? sv_term(c, d->expr.elems[2], path.b) : NULL;
        NCondV1 yes, no;
        if (!y || !sv_compare(c, x, y, path.cond, &yes, &no))
            return false;
        return cond_empty(no) || sc_body(c, rule, m, k + 1u, (NPathV1){no, path.b}, depth, tail, false, out);
    }
    size_t arity = p->expr.len - 1u;
    uint8_t bits[64];
    NSvV1 *args[64] = {0};
    if (arity > 64u)
        return sc_unread(c);
    for (size_t i = 0; i < arity; i++) {
        bits[i] = sv_bound(p->expr.elems[i + 1u], path.b) ? 1u : 0u;
        if (bits[i] && !(args[i] = sv_term(c, p->expr.elems[i + 1u], path.b)))
            return false;
    }
    const DirectBindingModeV1 *n = mode_find(c->modes, head, arity, bits);
    if (!n || !n->selected || !n->functional)
        return sc_unread(c);
    if (n == c->entry) {
        /* the run's step: its answer on the rest, as holes */
        if (!last || !tail || !args[c->entry_input] || args[c->entry_input]->kind != NSV_CUR ||
            args[c->entry_input]->pos != 1)
            return sc_unread(c);
        const NBindV1 *b = path.b;
        size_t o = 0;
        for (size_t i = 0; i < arity; i++) {
            if (bits[i])
                continue;
            const Atom *x = p->expr.elems[i + 1u];
            NSvV1 *hole = sv_new(c, NSV_HOLE, 0u);
            if (!is_var(x) || !hole)
                return sc_unread(c);
            hole->n = (int64_t)o++;
            if (!(b = bind_add(c, b, cetta_gslt_source_variable_name_v1(x), hole)))
                return false;
        }
        return sc_body(c, rule, m, k + 1u, (NPathV1){path.cond, b}, depth, tail, true, out);
    }
    NResultsV1 sub = {0};
    if (!sc_mode(c, n, args, path.cond, depth + 1u, tail && last, &sub))
        return false;
    for (size_t j = 0; j < sub.len; j++) {
        const NResV1 *r = &sub.items[j];
        NPathsV1 cur = {0};
        if (!paths_push(c, &cur, r->cond, path.b))
            return false;
        size_t o = 0;
        for (size_t i = 0; i < arity; i++) {
            if (bits[i])
                continue;
            NPathsV1 next = {0};
            for (size_t q = 0; q < cur.len; q++)
                if (!sc_match(c, p->expr.elems[i + 1u], r->out[o], cur.items[q], &next))
                    return false;
            o++;
            cur = next;
        }
        for (size_t q = 0; q < cur.len; q++)
            if (!sc_body(c, rule, m, k + 1u, cur.items[q], depth, tail, r->loop, out))
                return false;
    }
    return true;
}

/* The answers of mode m on inputs `in` (by argument position) where `cond`
 * holds: its rules in order, each answering where none before it did. */
static bool sc_mode(NScanCtxV1 *c, const DirectBindingModeV1 *m, NSvV1 *const *in, NCondV1 cond, unsigned depth,
                    bool tail, NResultsV1 *out) {
    if (depth > NSCAN_DEPTH_V1)
        return sc_unread(c);
    NCondV1 left = cond;
    for (size_t k = 0; k < c->rules->len && !cond_empty(left); k++) {
        const DirectRuleV1 *rule = &c->rules->items[k];
        const Atom *h = rule->head;
        if (h->expr.len - 1u != m->arity || strcmp(sym(h->expr.elems[0]), m->relation) != 0)
            continue;
        NPathsV1 heads = {0};
        if (!paths_push(c, &heads, left, NULL))
            return false;
        for (size_t a = 0; a < m->arity; a++) {
            if (!m->input_ground[a])
                continue;
            NPathsV1 next = {0};
            for (size_t q = 0; q < heads.len; q++)
                if (!sc_match(c, h->expr.elems[a + 1u], in[a], heads.items[q], &next))
                    return false;
            heads = next;
        }
        NResultsV1 answers = {0};
        for (size_t q = 0; q < heads.len; q++)
            if (!sc_body(c, rule, m, 0u, heads.items[q], depth, tail, false, &answers))
                return false;
        for (size_t q = 0; q < answers.len; q++) {
            NResV1 r = answers.items[q];
            r.cond = cond_inter(c, r.cond, left);
            if (!res_push(c, out, &r))
                return false;
            left = cond_minus(c, left, r.cond);
        }
    }
    return !c->failed;
}

typedef struct {
    long mode;
    long cons, nil;             /* the run's constructors (operator table ids) */
    size_t run_out, after_out;  /* the outputs' positions among the mode's outputs */
    uint32_t (*ranges)[2];      /* the class, sorted disjoint */
    size_t range_len;
} NScanV1;

typedef struct {
    NScanV1 *items;
    size_t len, cap;
} NScansV1;

/* Whether mode m is a class run, into *scan. */
static bool scan_class_run(const DirectRulesV1 *rules, const DirectBindingModesV1 *modes, const NOpsV1 *ops,
                           long op_cons, long op_nil, long op_cp, long mi, NScanV1 *scan) {
    const DirectBindingModeV1 *m = &modes->items[mi];
    size_t inputs = 0, input = 0;
    for (size_t a = 0; a < m->arity; a++)
        if (m->input_ground[a])
            inputs++, input = a;
    if (inputs != 1u || sc_outputs(m) != 2u)
        return false;
    NScanCtxV1 c = {.rules = rules, .modes = modes, .ops = ops, .op_cons = op_cons, .op_nil = op_nil,
                    .op_cp = op_cp, .entry = m, .entry_input = input};
    NSvV1 *in[64] = {0};
    NResultsV1 rs = {0};
    bool found = false;
    in[input] = sv_new(&c, NSV_CUR, 0u);
    if (!in[input] || m->arity > 64u)
        goto done;
    NCondV1 all = cond_all(&c), stops = {false, {NULL, 0u}}, steps = {false, {NULL, 0u}};
    if (!sc_mode(&c, m, in, all, 0u, true, &rs) || c.failed)
        goto done;
    long cons = -1, nil = -1;
    size_t after = 2u;
    for (size_t j = 0; j < rs.len; j++) {
        const NResV1 *r = &rs.items[j];
        if (!r->loop) {
            /* a stop: NIL and the input */
            size_t k = r->out[0]->kind == NSV_CUR ? 0u : r->out[1]->kind == NSV_CUR ? 1u : 2u;
            const NSvV1 *list = k < 2u ? r->out[1u - k] : NULL;
            if (k == 2u || r->out[k]->pos != 0 || (after < 2u && after != k) || list->kind != NSV_APP ||
                list->nk != 0u || (nil >= 0 && nil != list->op))
                goto done;
            after = k;
            nil = list->op;
            stops = cond_union(&c, stops, r->cond);
            continue;
        }
        /* a step: (CONS C RUN) and AFTER, from the answer on the rest */
        size_t k = r->out[0]->kind == NSV_HOLE && r->out[0]->n == 0 ? 0u :
                   r->out[1]->kind == NSV_HOLE && r->out[1]->n == 1 ? 1u : 2u;
        const NSvV1 *cell = k < 2u ? r->out[1u - k] : NULL;
        if (k == 2u || (after < 2u && after != k) || cell->kind != NSV_APP || cell->nk != 2u ||
            cell->kid[0]->kind != NSV_SCALAR || cell->kid[0]->pos != 0 || cell->kid[1]->kind != NSV_HOLE ||
            cell->kid[1]->n != (int64_t)(1u - k) || (cons >= 0 && cons != cell->op) || r->cond.end)
            goto done;
        after = k;
        cons = cell->op;
        steps = cond_union(&c, steps, r->cond);
    }
    /* every input answered: the end and every scalar outside the class stop */
    NCondV1 answered = cond_union(&c, stops, steps);
    if (cons < 0 || nil < 0 || after > 1u || !stops.end || !answered.end ||
        !set_equal(answered.set, all.set) || c.failed || ops->items[cons].arity != 2u ||
        ops->items[nil].arity != 0u || cons == op_cons || nil == op_nil)
        goto done;
    scan->mode = mi;
    scan->cons = cons;
    scan->nil = nil;
    scan->after_out = after;
    scan->run_out = 1u - after;
    scan->range_len = steps.set.len;
    scan->ranges = malloc((steps.set.len ? steps.set.len : 1u) * sizeof *scan->ranges);
    if (!scan->ranges)
        goto done;
    if (steps.set.len)
        memcpy(scan->ranges, steps.set.r, steps.set.len * sizeof *scan->ranges);
    found = true;
done:
    sc_free(&c);
    return found;
}

/* Every selected mode that is a class run. */
static bool find_scans(const DirectRulesV1 *rules, const DirectBindingModesV1 *modes, const NOpsV1 *ops,
                       long op_cons, long op_nil, long op_cp, NScansV1 *out) {
    for (size_t i = 0; i < modes->len; i++) {
        if (!modes->items[i].selected)
            continue;
        NScanV1 scan;
        if (!scan_class_run(rules, modes, ops, op_cons, op_nil, op_cp, (long)i, &scan))
            continue;
        if (out->len == out->cap) {
            size_t cap = out->cap ? out->cap * 2u : 8u;
            NScanV1 *grown = realloc(out->items, cap * sizeof *grown);
            if (!grown) {
                free(scan.ranges);
                return false;
            }
            out->items = grown;
            out->cap = cap;
        }
        out->items[out->len++] = scan;
    }
    return true;
}

static void scans_free(NScansV1 *s) {
    for (size_t i = 0; i < s->len; i++)
        free(s->items[i].ranges);
    free(s->items);
    *s = (NScansV1){0};
}

static const NScanV1 *scan_of(const NScansV1 *s, long mode) {
    for (size_t i = 0; s && i < s->len; i++)
        if (s->items[i].mode == mode)
            return &s->items[i];
    return NULL;
}

/* The nil of a run built of cons, or -1: values of cons may be spans. */
static long span_nil(const NScansV1 *s, long cons) {
    for (size_t i = 0; s && i < s->len; i++)
        if (s->items[i].cons == cons)
            return s->items[i].nil;
    return -1;
}

/* ── the rule compiler ────────────────────────────────────────────────── */

enum { NATIVE_PATH_MAX_V1 = 96 };

typedef struct {
    const char **names;   /* variable spellings, by C index */
    bool *bound;
    char (*path)[NATIVE_PATH_MAX_V1]; /* the input position it was bound from, or "" */
    size_t len, cap;
} NVarsV1;

static long var_index(NVarsV1 *vs, const char *name) {
    for (size_t i = 0; i < vs->len; i++)
        if (strcmp(vs->names[i], name) == 0)
            return (long)i;
    if (vs->len == vs->cap) {
        size_t cap = vs->cap ? vs->cap * 2u : 32u;
        const char **n = realloc(vs->names, cap * sizeof *n);
        if (n)
            vs->names = n;
        bool *b = n ? realloc(vs->bound, cap * sizeof *b) : NULL;
        if (b)
            vs->bound = b;
        char (*pa)[NATIVE_PATH_MAX_V1] = b ? realloc(vs->path, cap * sizeof *pa) : NULL;
        if (!pa)
            return -1;
        vs->path = pa;
        vs->cap = cap;
    }
    vs->names[vs->len] = name;
    vs->bound[vs->len] = false;
    vs->path[vs->len][0] = 0;
    return (long)vs->len++;
}

typedef struct {
    const DirectBindingModesV1 *modes;
    const NOpsV1 *ops;
    const NScansV1 *scans;      /* class runs: values of their CONS may be spans */
    struct NLitsV1 *lits;       /* the literal texts the rules may test values against */
    long op_cons, op_nil, op_cp;
    NBufV1 *out;
    NVarsV1 vars;
    unsigned temp;
    unsigned label;       /* the current rule's failure label */
    bool label_used;      /* some premise or pattern can fail */
    const char *rule;     /* for messages */
    char *error;
    size_t error_size;
    bool failed;
    /* tail calls */
    const Atom *head;           /* the current rule's head */
    const DirectBindingModeV1 *mode;
    const Atom *last;           /* its last premise */
    bool commit;                /* reaching it decides the mode's answer */
    long tail;                  /* the mode the last premise could jump to, or -1 */
    const long *group_of;       /* tail group by mode, -1 for none */
    long group;                 /* the group being emitted, -1 for none */
    bool jumped;                /* the rule ended with a jump */
    unsigned hole, jumps;
    /* equality tests between input positions, shared by a mode's rules */
    char **memo_keys;
    size_t memo_len, memo_cap;
    unsigned memo_base, memo_next;
    /* the mode's rules a dispatch on its first input jumps to (s<label>),
     * by rule from start_base on */
    const bool *start_target;
    unsigned start_base, start_count;
} NRuleV1;

static bool rule_error(NRuleV1 *r, const char *what, const Atom *t) {
    (void)t;
    if (!r->failed)
        native_error(r->error, r->error_size, "C target: rule %s: %s", r->rule, what);
    r->failed = true;
    return false;
}

static bool ground_now(NRuleV1 *r, const Atom *t) {
    if (is_var(t)) {
        long k = var_index(&r->vars, cetta_gslt_source_variable_name_v1(t));
        return k >= 0 && r->vars.bound[k];
    }
    if (t && t->kind == ATOM_EXPR)
        for (size_t i = 1; i < t->expr.len; i++)
            if (!ground_now(r, t->expr.elems[i]))
                return false;
    return true;
}

/* The equality of the values at input positions pa and pb, as a C
 * expression: a mode's rules test the same positions of the same inputs,
 * so the test runs once per entry into the mode and its answer is kept
 * (gqN: -1 not yet tested, else the answer).  NULL when a position is not
 * one of the inputs'. */
static const char *memo_equal(NRuleV1 *r, const char *pa, const char *pb, const char *a, const char *b,
                              char *out, size_t cap) {
    if (!pa || !pb || !pa[0] || !pb[0])
        return NULL;
    char key[2 * NATIVE_PATH_MAX_V1 + 2];
    int c = strcmp(pa, pb);
    snprintf(key, sizeof key, "%s|%s", c <= 0 ? pa : pb, c <= 0 ? pb : pa);
    size_t i = 0;
    while (i < r->memo_len && strcmp(r->memo_keys[i], key) != 0)
        i++;
    if (i == r->memo_len) {
        if (r->memo_len == r->memo_cap) {
            size_t ncap = r->memo_cap ? r->memo_cap * 2u : 8u;
            char **grown = realloc(r->memo_keys, ncap * sizeof *grown);
            if (!grown)
                return NULL;
            r->memo_keys = grown;
            r->memo_cap = ncap;
        }
        size_t n = strlen(key) + 1u;
        char *copy = malloc(n);
        if (!copy)
            return NULL;
        memcpy(copy, key, n);
        r->memo_keys[r->memo_len++] = copy;
    }
    unsigned g = r->memo_base + (unsigned)i;
    snprintf(out, cap, "(gq%u < 0 ? (gq%u = gn_eq(cx, %s, %s)) : gq%u)", g, g, a, b, g);
    return out;
}

static void memo_reset(NRuleV1 *r) {
    for (size_t i = 0; i < r->memo_len; i++)
        free(r->memo_keys[i]);
    r->memo_len = 0;
}

static void sub_path(char *out, const char *path, const char *step) {
    if (!path || !path[0] || snprintf(out, NATIVE_PATH_MAX_V1, "%s.%s", path, step) >= NATIVE_PATH_MAX_V1)
        out[0] = 0;
}

/* Match a pattern against the C value expression `val`, the value at the
 * input position `path` ("" when it is not one). */
/* Whether t is a list of literal scalars, (CONS (cp N) ... NIL), of a run's
 * constructors: its scalars into s (at most cap) and their number into *n. */
static bool scalar_list(NRuleV1 *r, const Atom *t, long *cons, long *nil, uint32_t *s, size_t cap, size_t *n) {
    *n = 0;
    *cons = -1;
    for (;;) {
        const char *name = sym(t);
        if (name)
            return *cons >= 0 && op_find(r->ops, name, 0u) == *nil;
        if (!t || t->kind != ATOM_EXPR || t->expr.len != 3u || !sym(t->expr.elems[0]) || *n == cap)
            return false;
        long id = op_find(r->ops, sym(t->expr.elems[0]), 2u);
        long run_nil = id < 0 ? -1 : span_nil(r->scans, id);
        if (run_nil < 0 || (*cons >= 0 && id != *cons))
            return false;
        *cons = id;
        *nil = run_nil;
        const Atom *h = t->expr.elems[1];
        int64_t c;
        if (!h || h->kind != ATOM_EXPR || h->expr.len != 2u || !sym(h->expr.elems[0]) ||
            op_find(r->ops, sym(h->expr.elems[0]), 1u) != r->op_cp || !is_int(h->expr.elems[1], &c) || c < 0 ||
            c > NSCAN_MAX_SCALAR_V1)
            return false;
        s[(*n)++] = (uint32_t)c;
        t = t->expr.elems[2];
    }
}

/* The bytes a source holds for scalar c (an escaped byte as itself), as the
 * runtime's gn_utf8_next reads them back. */
static size_t text_bytes(uint32_t c, uint8_t out[4]) {
    if (c >= 0xDC80u && c <= 0xDCFFu) {
        out[0] = (uint8_t)(c - 0xDC00u);
        return 1u;
    }
    if (c < 0x80u)
        return out[0] = (uint8_t)c, 1u;
    if (c < 0x800u)
        return out[0] = (uint8_t)(0xC0u | c >> 6), out[1] = (uint8_t)(0x80u | (c & 0x3Fu)), 2u;
    if (c < 0x10000u)
        return out[0] = (uint8_t)(0xE0u | c >> 12), out[1] = (uint8_t)(0x80u | (c >> 6 & 0x3Fu)),
               out[2] = (uint8_t)(0x80u | (c & 0x3Fu)), 3u;
    return out[0] = (uint8_t)(0xF0u | c >> 18), out[1] = (uint8_t)(0x80u | (c >> 12 & 0x3Fu)),
           out[2] = (uint8_t)(0x80u | (c >> 6 & 0x3Fu)), out[3] = (uint8_t)(0x80u | (c & 0x3Fu)), 4u;
}

/* The literal texts of the rules, each once: gn_lit<k> its scalars, and
 * gn_txt<k> its bytes when they read back as those scalars (an escaped byte
 * may begin a longer scalar, and then the text is compared scalar by
 * scalar). */
typedef struct {
    uint32_t *s;
    size_t n;
    long cons, nil;
    uint8_t *bytes;   /* NULL when they do not read back */
    size_t len;
    bool used;        /* some test compares against it */
} NLitV1;

typedef struct NLitsV1 {
    NLitV1 *items;
    size_t len, cap;
} NLitsV1;

static long lit_find(const NLitsV1 *ls, long cons, long nil, const uint32_t *s, size_t n) {
    for (size_t i = 0; ls && i < ls->len; i++)
        if (ls->items[i].n == n && ls->items[i].cons == cons && ls->items[i].nil == nil &&
            memcmp(ls->items[i].s, s, n * sizeof *s) == 0)
            return (long)i;
    return -1;
}

static bool lit_add(NLitsV1 *ls, long cons, long nil, const uint32_t *s, size_t n) {
    if (lit_find(ls, cons, nil, s, n) >= 0)
        return true;
    if (ls->len == ls->cap) {
        size_t cap = ls->cap ? ls->cap * 2u : 64u;
        NLitV1 *grown = realloc(ls->items, cap * sizeof *grown);
        if (!grown)
            return false;
        ls->items = grown;
        ls->cap = cap;
    }
    NLitV1 l = {malloc(n * sizeof *s), n, cons, nil, malloc(4u * n), 0u, false};
    if (!l.s || !l.bytes) {
        free(l.s);
        free(l.bytes);
        return false;
    }
    memcpy(l.s, s, n * sizeof *s);
    for (size_t i = 0; i < n; i++)
        l.len += text_bytes(s[i], l.bytes + l.len);
    GnSourceV1 src = {l.bytes, l.len};
    size_t pos = 0, i = 0;
    for (; pos < l.len && i < n; i++) {
        uint32_t c;
        pos += gn_utf8_next(&src, pos, &c);
        if (c != s[i])
            break;
    }
    if (pos != l.len || i != n) {
        free(l.bytes);
        l.bytes = NULL;
    }
    ls->items[ls->len++] = l;
    return true;
}

static void lits_free(NLitsV1 *ls) {
    for (size_t i = 0; i < ls->len; i++) {
        free(ls->items[i].s);
        free(ls->items[i].bytes);
    }
    free(ls->items);
    *ls = (NLitsV1){0};
}

/* Every literal text in t, a whole text once. */
static bool collect_lits(NRuleV1 *r, const Atom *t, NLitsV1 *ls) {
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u)
        return true;
    long cons, nil;
    size_t n;
    uint32_t text[64];
    if (t->expr.len == 3u && sym(t->expr.elems[0]) && scalar_list(r, t, &cons, &nil, text, 64u, &n))
        return lit_add(ls, cons, nil, text, n);
    for (size_t i = 0; i < t->expr.len; i++)
        if (!collect_lits(r, t->expr.elems[i], ls))
            return false;
    return true;
}

static void emit_lits(NBufV1 *o, const NLitsV1 *ls) {
    for (size_t k = 0; k < ls->len; k++) {
        const NLitV1 *l = &ls->items[k];
        if (!l->used)
            continue;
        nb_printf(o, "static const uint32_t gn_lit%zu[] = {", k);
        for (size_t i = 0; i < l->n; i++)
            nb_printf(o, "%s%uu", i ? ", " : "", l->s[i]);
        nb_puts(o, "};\n");
        if (l->bytes) {
            nb_printf(o, "static const char gn_txt%zu[] = \"", k);
            for (size_t i = 0; i < l->len; i++)
                nb_printf(o, "\\x%02x", l->bytes[i]);
            nb_puts(o, "\";\n");
        }
    }
    if (ls->len)
        nb_puts(o, "\n");
}

/* A literal text's key, as gn_text_key computes it for a value. */
static uint64_t text_key(const uint32_t *s, size_t n) {
    uint64_t h = GN_TEXT_HASH_BASIS;
    for (size_t i = 0; i < n; i++) {
        uint8_t bytes[4];
        h = gn_text_hash(h, bytes, text_bytes(s[i], bytes));
    }
    return h;
}

/* The C test that the value `val` is the literal text s[0..n) of cons and
 * nil (in the rules' literal table). */
static void emit_text_test(NRuleV1 *r, NBufV1 *e, const char *val, long cons, long nil, const uint32_t *s, size_t n) {
    long k = lit_find(r->lits, cons, nil, s, n);
    if (k < 0) {
        rule_error(r, "a literal text missing from the literal table", NULL);
        return;
    }
    NLitV1 *l = &r->lits->items[k];
    l->used = true;
    if (l->bytes)
        nb_printf(e, "gn_is_text(cx, %s, %ldu, %ldu, gn_lit%ld, %zuu, gn_txt%ld, %zuu)", val,
                  cons + NATIVE_FIRST_OP_V1, nil + NATIVE_FIRST_OP_V1, k, n, k, l->len);
    else
        nb_printf(e, "gn_is_scalars(cx, %s, %ldu, %ldu, gn_lit%ld, %zuu)", val, cons + NATIVE_FIRST_OP_V1,
                  nil + NATIVE_FIRST_OP_V1, k, n);
}

static bool emit_match(NRuleV1 *r, const Atom *p, const char *val, const char *path) {
    NBufV1 *o = r->out;
    if (is_var(p)) {
        long k = var_index(&r->vars, cetta_gslt_source_variable_name_v1(p));
        if (k < 0)
            return rule_error(r, "out of memory", p);
        if (r->vars.bound[k]) {
            char vk[32], memo[512];
            snprintf(vk, sizeof vk, "v%ld", k);
            const char *test = memo_equal(r, r->vars.path[k], path, vk, val, memo, sizeof memo);
            if (test)
                nb_printf(o, "        if (!%s) goto f%u;\n", test, r->label), r->label_used = true;
            else
                nb_printf(o, "        if (!gn_eq(cx, v%ld, %s)) goto f%u;\n", k, val, r->label), r->label_used = true;
        } else {
            nb_printf(o, "        v%ld = %s;\n", k, val);
            r->vars.bound[k] = true;
            snprintf(r->vars.path[k], NATIVE_PATH_MAX_V1, "%s", path ? path : "");
        }
        return true;
    }
    int64_t n;
    if (is_int(p, &n)) {
        nb_printf(o, "        if (!gn_is_int(%s, %lld)) goto f%u;\n", val, (long long)n, r->label), r->label_used = true;
        return true;
    }
    const char *s = sym(p);
    if (s) {
        long id = op_find(r->ops, s, 0u);
        if (id == r->op_nil)
            nb_printf(o, "        if (!gn_is_nil(cx, %s)) goto f%u;\n", val, r->label), r->label_used = true;
        else
            nb_printf(o, "        if (%s->op != %ldu) goto f%u;\n", val, id + NATIVE_FIRST_OP_V1, r->label), r->label_used = true;
        return true;
    }
    if (!p || p->kind != ATOM_EXPR || p->expr.len == 0u || !sym(p->expr.elems[0]))
        return rule_error(r, "a pattern outside the C fragment", p);
    size_t arity = p->expr.len - 1u;
    long id = op_find(r->ops, sym(p->expr.elems[0]), arity);
    long lc, ln;
    size_t scalars;
    uint32_t text[64];
    if (arity == 2u && span_nil(r->scans, id) >= 0 && scalar_list(r, p, &lc, &ln, text, 64u, &scalars)) {
        /* a literal text against a value that may be a span */
        nb_puts(o, "        if (!");
        emit_text_test(r, o, val, lc, ln, text, scalars);
        nb_printf(o, ") goto f%u;\n", r->label), r->label_used = true;
        return true;
    }
    unsigned t = r->temp;
    r->temp += (unsigned)arity;
    if (id == r->op_cons) {
        nb_printf(o, "        if (!gn_uncons(cx, %s, &t%u, &t%u)) goto f%u;\n", val, t, t + 1u, r->label), r->label_used = true;
    } else if (arity == 2u && span_nil(r->scans, id) >= 0) {
        nb_printf(o, "        if (!gn_uncons_op(cx, %s, %ldu, &t%u, &t%u)) goto f%u;\n", val, id + NATIVE_FIRST_OP_V1, t,
                  t + 1u, r->label), r->label_used = true;
    } else {
        nb_printf(o, "        if (%s->op != %ldu) goto f%u;\n", val, id + NATIVE_FIRST_OP_V1, r->label), r->label_used = true;
        for (size_t i = 0; i < arity; i++)
            nb_printf(o, "        t%u = %s->kid[%zu];\n", t + (unsigned)i, val, i);
    }
    char sub[32], step[24], spath[NATIVE_PATH_MAX_V1];
    for (size_t i = 0; i < arity; i++) {
        snprintf(sub, sizeof sub, "t%u", t + (unsigned)i);
        if (id == r->op_cons)
            snprintf(step, sizeof step, "%s", i == 0 ? "h" : "t");
        else
            snprintf(step, sizeof step, "%zu", i);
        sub_path(spath, path, step);
        if (!emit_match(r, p->expr.elems[i + 1u], sub, spath))
            return false;
    }
    return true;
}

/* A C expression building the ground term t (its variables bound). */
static bool emit_build(NRuleV1 *r, const Atom *t, NBufV1 *e) {
    if (is_var(t)) {
        long k = var_index(&r->vars, cetta_gslt_source_variable_name_v1(t));
        if (k < 0 || !r->vars.bound[k])
            return rule_error(r, "an output uses a variable no premise binds", t);
        nb_printf(e, "v%ld", k);
        return true;
    }
    int64_t n;
    if (is_int(t, &n)) {
        nb_printf(e, "gn_int(cx, %lld)", (long long)n);
        return true;
    }
    const char *s = sym(t);
    if (s) {
        long id = op_find(r->ops, s, 0u);
        if (id == r->op_nil)
            return rule_error(r, "builds nil, which the C target reads from the input only", t);
        nb_printf(e, "gn_const(cx, %ldu)", id + NATIVE_FIRST_OP_V1);
        return true;
    }
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u || !sym(t->expr.elems[0]))
        return rule_error(r, "a term outside the C fragment", t);
    size_t arity = t->expr.len - 1u;
    long id = op_find(r->ops, sym(t->expr.elems[0]), arity);
    if (id == r->op_cons)
        return rule_error(r, "builds cons, which the C target reads from the input only", t);
    if (arity == 2u && span_nil(r->scans, id) >= 0)
        nb_printf(e, "gn_cons_cp(cx, %ldu", id + NATIVE_FIRST_OP_V1);   /* extends a span it precedes */
    else
        nb_printf(e, "gn_app%zu(cx, %ldu", arity, id + NATIVE_FIRST_OP_V1);
    for (size_t i = 0; i < arity; i++) {
        nb_puts(e, ", ");
        if (!emit_build(r, t->expr.elems[i + 1u], e))
            return false;
    }
    nb_puts(e, ")");
    return true;
}

static long mode_number(const DirectBindingModesV1 *modes, const DirectBindingModeV1 *m) {
    return (long)(m - modes->items);
}

static bool mentions_list(NRuleV1 *r, const Atom *t) {
    const char *s = sym(t);
    if (s)
        return op_find(r->ops, s, 0u) == r->op_nil;
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u)
        return false;
    if (sym(t->expr.elems[0]) && op_find(r->ops, sym(t->expr.elems[0]), t->expr.len - 1u) == r->op_cons)
        return true;
    for (size_t i = 1; i < t->expr.len; i++)
        if (mentions_list(r, t->expr.elems[i]))
            return true;
    return false;
}

/* A C condition: the value `val` equals the term t (its variables bound),
 * tested in place without building t.  t mentions no list constructor (a
 * list may be a cursor, which gn_eq compares). */
static bool emit_equal_test(NRuleV1 *r, const char *val, const Atom *t, NBufV1 *e) {
    if (is_var(t)) {
        long k = var_index(&r->vars, cetta_gslt_source_variable_name_v1(t));
        if (k < 0 || !r->vars.bound[k])
            return rule_error(r, "different on terms not yet bound", t);
        nb_printf(e, "gn_eq(cx, %s, v%ld)", val, k);
        return true;
    }
    int64_t n;
    if (is_int(t, &n)) {
        nb_printf(e, "gn_is_int(%s, %lld)", val, (long long)n);
        return true;
    }
    const char *s = sym(t);
    if (s) {
        nb_printf(e, "(%s->op == %ldu)", val, op_find(r->ops, s, 0u) + NATIVE_FIRST_OP_V1);
        return true;
    }
    size_t arity = t->expr.len - 1u;
    long id = op_find(r->ops, sym(t->expr.elems[0]), arity);
    if (arity == 2u && span_nil(r->scans, id) >= 0) {
        /* a value that may be a span: a literal text by its scalars,
         * anything else by gn_eq */
        long lc, ln;
        size_t scalars;
        uint32_t text[64];
        if (scalar_list(r, t, &lc, &ln, text, 64u, &scalars)) {
            emit_text_test(r, e, val, lc, ln, text, scalars);
            return true;
        }
        nb_printf(e, "gn_eq(cx, %s, ", val);
        if (!emit_build(r, t, e))
            return false;
        nb_puts(e, ")");
        return true;
    }
    nb_printf(e, "(%s->op == %ldu", val, id + NATIVE_FIRST_OP_V1);
    char sub[256];
    for (size_t i = 0; i < arity; i++) {
        if (snprintf(sub, sizeof sub, "%s->kid[%zu]", val, i) >= (int)sizeof sub)
            return rule_error(r, "a term nested too deep for an equality test", t);
        nb_puts(e, " && ");
        if (!emit_equal_test(r, sub, t->expr.elems[i + 1u], e))
            return false;
    }
    nb_puts(e, ")");
    return true;
}

/* ── tail calls ───────────────────────────────────────────────────────── */

/* Occurrences of the variable `name` in t. */
static size_t var_count(const Atom *t, const char *name) {
    if (is_var(t))
        return strcmp(cetta_gslt_source_variable_name_v1(t), name) == 0 ? 1u : 0u;
    size_t n = 0;
    if (t && t->kind == ATOM_EXPR)
        for (size_t i = 1; i < t->expr.len; i++)
            n += var_count(t->expr.elems[i], name);
    return n;
}

/* The premise p (arguments ground as bits says) can end its rule with a
 * jump: it is the rule's last premise, the rule decides the mode's answer
 * once p is reached, each output of p is a variable of its own not bound
 * yet, and each occurs at most once in the head's outputs, so the place it
 * goes can be handed to the callee. */
static bool tail_convertible(NRuleV1 *r, const Atom *p, const uint8_t *bits, size_t arity) {
    if (p != r->last || !r->commit)
        return false;
    const DirectBindingModeV1 *m = r->mode;
    for (size_t i = 0; i < arity; i++) {
        if (bits[i])
            continue;
        const Atom *x = p->expr.elems[i + 1u];
        if (!is_var(x))
            return false;
        const char *name = cetta_gslt_source_variable_name_v1(x);
        for (size_t j = i + 1u; j < arity; j++)
            if (!bits[j] && var_count(p->expr.elems[j + 1u], name))
                return false;
        size_t uses = 0;
        for (size_t k = 0; k < m->arity; k++)
            if (!m->input_ground[k])
                uses += var_count(r->head->expr.elems[k + 1u], name);
        if (uses > 1u)
            return false;
    }
    return true;
}

/* A C expression building t, whose holes are the variables
 * names[0..count): each node with a hole among its children is built into
 * a temporary of its own (declared in pre), and dest[j] becomes the place
 * hole j is filled through. */
static bool emit_build_holes(NRuleV1 *r, const Atom *t, const char *const *names, char (*dest)[48],
                             size_t count, NBufV1 *pre, NBufV1 *e) {
    size_t uses = 0;
    for (size_t j = 0; j < count; j++)
        uses += var_count(t, names[j]);
    if (uses == 0u)
        return emit_build(r, t, e);
    if (is_var(t) || !t || t->kind != ATOM_EXPR || t->expr.len < 2u || !sym(t->expr.elems[0]))
        return rule_error(r, "an output with a hole outside a constructor", t);
    size_t arity = t->expr.len - 1u;
    if (arity > 16u)
        return rule_error(r, "a constructor with more than 16 arguments", t);
    long id = op_find(r->ops, sym(t->expr.elems[0]), arity);
    if (id == r->op_cons)
        return rule_error(r, "builds cons, which the C target reads from the input only", t);
    NBufV1 args = {0};
    size_t hole_at[16], hole_of[16], nh = 0;
    for (size_t i = 0; i < arity; i++) {
        const Atom *c = t->expr.elems[i + 1u];
        nb_puts(&args, ", ");
        size_t j = count;
        for (size_t q = 0; is_var(c) && q < count; q++)
            if (strcmp(names[q], cetta_gslt_source_variable_name_v1(c)) == 0)
                j = q;
        if (j < count) {
            nb_puts(&args, "(GnVal *)NULL");
            hole_at[nh] = i;
            hole_of[nh++] = j;
        } else if (!emit_build_holes(r, c, names, dest, count, pre, &args)) {
            free(args.bytes);
            return false;
        }
    }
    unsigned h = r->hole++;
    nb_printf(pre, "            GnVal *h%u = gn_app%zu(cx, %ldu%s);\n", h, arity, id + NATIVE_FIRST_OP_V1,
              args.bytes ? (const char *)args.bytes : "");
    for (size_t q = 0; q < nh; q++)
        snprintf(dest[hole_of[q]], sizeof dest[0], "&h%u->kid[%zu]", h, hole_at[q]);
    nb_printf(e, "h%u", h);
    bool nomem = args.nomem;
    free(args.bytes);
    return nomem ? rule_error(r, "out of memory", t) : true;
}

/* The rule's last premise p, a call of n that decides the rule's answer,
 * as a jump: n's inputs are computed, the head's outputs are written
 * through the current output places with a hole wherever an output of p
 * goes, and n runs with those holes as its output places (an output of p
 * the head drops goes to a sink). */
static bool emit_jump(NRuleV1 *r, const Atom *p, const uint8_t *bits, size_t arity,
                      const DirectBindingModeV1 *n) {
    NBufV1 *o = r->out;
    const DirectBindingModeV1 *m = r->mode;
    const char *names[64];
    char dest[64][48];
    size_t holes = 0;
    for (size_t i = 0; i < arity; i++)
        if (!bits[i]) {
            names[holes] = cetta_gslt_source_variable_name_v1(p->expr.elems[i + 1u]);
            snprintf(dest[holes], sizeof dest[holes], "&gn_sink");
            holes++;
        }
    nb_puts(o, "        {\n");
    for (size_t i = 0; i < arity; i++) {
        if (!bits[i])
            continue;
        nb_printf(o, "            GnVal *n_a%zu = ", i);
        if (!emit_build(r, p->expr.elems[i + 1u], o))
            return false;
        nb_puts(o, ";\n");
    }
    for (size_t i = 0, k = 0; i < m->arity; i++) {
        if (m->input_ground[i])
            continue;
        const Atom *t = r->head->expr.elems[i + 1u];
        size_t whole = holes;
        for (size_t j = 0; is_var(t) && j < holes; j++)
            if (strcmp(names[j], cetta_gslt_source_variable_name_v1(t)) == 0)
                whole = j;
        if (whole < holes) {
            snprintf(dest[whole], sizeof dest[whole], "r%zu", k++);
            continue;
        }
        NBufV1 pre = {0}, e = {0};
        bool ok = emit_build_holes(r, t, names, dest, holes, &pre, &e);
        if (ok && (pre.nomem || e.nomem))
            ok = rule_error(r, "out of memory", t);
        if (ok) {
            if (pre.len)
                nb_put(o, (const char *)pre.bytes, pre.len);
            nb_printf(o, "            *r%zu = %s;\n", k, (const char *)e.bytes);
        }
        free(pre.bytes);
        free(e.bytes);
        if (!ok)
            return false;
        k++;
    }
    for (size_t j = 0; j < holes; j++)
        nb_printf(o, "            GnVal **n_r%zu = %s;\n", j, dest[j]);
    for (size_t i = 0; i < arity; i++)
        if (bits[i])
            nb_printf(o, "            a%zu = n_a%zu;\n", i, i);
    for (size_t j = 0; j < holes; j++)
        nb_printf(o, "            r%zu = n_r%zu;\n", j, j);
    nb_trace(o, "            ", "RULE", r->rule, "");
    nb_printf(o, "            goto gm%ld;\n        }\n", mode_number(r->modes, n));
    r->jumped = true;
    r->jumps++;
    return true;
}

static bool emit_premise(NRuleV1 *r, const Atom *p) {
    NBufV1 *o = r->out;
    const char *head = p && p->kind == ATOM_EXPR && p->expr.len > 0u ? sym(p->expr.elems[0]) : NULL;
    if (!head)
        return rule_error(r, "a premise outside the C fragment", p);
    if (strcmp(head, "gslt:different") == 0 || (strcmp(head, "different") == 0 && p->expr.len == 3u)) {
        /* the ground relations' different capability, bare or wrapped */
        const Atom *d = strcmp(head, "different") == 0 ? p : p->expr.len == 2u ? p->expr.elems[1] : NULL;
        if (!d || d->kind != ATOM_EXPR || d->expr.len != 3u || !sym(d->expr.elems[0]) ||
            strcmp(sym(d->expr.elems[0]), "different") != 0)
            return rule_error(r, "a gslt:different premise without (different A B)", p);
        if (!ground_now(r, d->expr.elems[1]) || !ground_now(r, d->expr.elems[2]))
            return rule_error(r, "different on terms not yet bound", p);
        if (is_var(d->expr.elems[1]) && is_var(d->expr.elems[2])) {
            long ka = var_index(&r->vars, cetta_gslt_source_variable_name_v1(d->expr.elems[1]));
            long kb = var_index(&r->vars, cetta_gslt_source_variable_name_v1(d->expr.elems[2]));
            char va[32], vb[32], memo[512];
            snprintf(va, sizeof va, "v%ld", ka);
            snprintf(vb, sizeof vb, "v%ld", kb);
            const char *test = memo_equal(r, r->vars.path[ka], r->vars.path[kb], va, vb, memo, sizeof memo);
            if (test) {
                nb_printf(o, "        if (%s) goto f%u;\n", test, r->label), r->label_used = true;
                return true;
            }
        }
        /* a variable against a term without lists: the term tested in place */
        for (int side = 1; side <= 2; side++) {
            const Atom *x = d->expr.elems[side], *t = d->expr.elems[3 - side];
            if (!is_var(x) || is_var(t) || mentions_list(r, t))
                continue;
            long k = var_index(&r->vars, cetta_gslt_source_variable_name_v1(x));
            char val[32];
            snprintf(val, sizeof val, "v%ld", k);
            NBufV1 test = {0};
            bool ok = emit_equal_test(r, val, t, &test);
            if (ok && test.nomem)
                ok = rule_error(r, "out of memory", p);
            if (ok)
                nb_printf(o, "        if (%s) goto f%u;\n", (const char *)test.bytes, r->label), r->label_used = true;
            free(test.bytes);
            return ok;
        }
        NBufV1 a = {0}, b = {0};
        bool ok = emit_build(r, d->expr.elems[1], &a) && emit_build(r, d->expr.elems[2], &b);
        if (ok && !a.nomem && !b.nomem)
            nb_printf(o, "        if (gn_eq(cx, %s, %s)) goto f%u;\n", (char *)a.bytes, (char *)b.bytes, r->label), r->label_used = true;
        free(a.bytes);
        free(b.bytes);
        return ok;
    }
    size_t arity = p->expr.len - 1u;
    uint8_t bits[64];
    if (arity > sizeof bits)
        return rule_error(r, "a premise with too many arguments", p);
    for (size_t i = 0; i < arity; i++)
        bits[i] = ground_now(r, p->expr.elems[i + 1u]) ? 1u : 0u;
    const DirectBindingModeV1 *m = mode_find(r->modes, head, arity, bits);
    if (!m || !m->selected || !m->functional) {
        char what[512];
        snprintf(what, sizeof what, "calls %s in a mode the analysis did not select as functional", head);
        return rule_error(r, what, p);
    }
    if (tail_convertible(r, p, bits, arity)) {
        r->tail = mode_number(r->modes, m);
        if (r->group >= 0 && r->group_of[r->tail] == r->group)
            return emit_jump(r, p, bits, arity, m);
    }
    unsigned first_out = r->temp;
    size_t outs = 0;
    for (size_t i = 0; i < arity; i++)
        outs += bits[i] ? 0u : 1u;
    r->temp += (unsigned)outs;
    nb_printf(o, "        if (!gn_f%ld(cx", mode_number(r->modes, m));
    for (size_t i = 0; i < arity; i++) {
        if (!bits[i])
            continue;
        nb_puts(o, ", ");
        if (!emit_build(r, p->expr.elems[i + 1u], o))
            return false;
    }
    for (size_t i = 0, k = 0; i < arity; i++)
        if (!bits[i])
            nb_printf(o, ", &t%u", first_out + (unsigned)k++);
    nb_printf(o, ")) goto f%u;\n", r->label), r->label_used = true;
    char val[32];
    for (size_t i = 0, k = 0; i < arity; i++) {
        if (bits[i])
            continue;
        snprintf(val, sizeof val, "t%u", first_out + (unsigned)k++);
        if (!emit_match(r, p->expr.elems[i + 1u], val, ""))
            return false;
    }
    return true;
}

/* The (different A B) of premise p, bare or wrapped in gslt:different, or
 * NULL. */
static const Atom *different_of(const Atom *p) {
    const char *head = p && p->kind == ATOM_EXPR && p->expr.len ? sym(p->expr.elems[0]) : NULL;
    if (!head)
        return NULL;
    if (strcmp(head, "different") == 0 && p->expr.len == 3u)
        return p;
    if (strcmp(head, "gslt:different") == 0 && p->expr.len == 2u) {
        const Atom *d = p->expr.elems[1];
        if (d && d->kind == ATOM_EXPR && d->expr.len == 3u && sym(d->expr.elems[0]) &&
            strcmp(sym(d->expr.elems[0]), "different") == 0)
            return d;
    }
    return NULL;
}

/* The premises from k on that each say a bound variable differs from a
 * literal text, the same variable and constructors throughout: how many,
 * with the variable's index in *var. */
static size_t text_guards(NRuleV1 *r, const Atom *body, size_t k, long *var, long *cons, long *nil) {
    size_t n = 0;
    *var = -1;
    for (; k < body->expr.len; k++, n++) {
        const Atom *d = different_of(body->expr.elems[k]);
        if (!d)
            break;
        const Atom *x = is_var(d->expr.elems[1]) ? d->expr.elems[1] : d->expr.elems[2];
        const Atom *t = x == d->expr.elems[1] ? d->expr.elems[2] : d->expr.elems[1];
        long c, nl;
        size_t len;
        uint32_t text[64];
        if (!is_var(x) || !ground_now(r, x) || !scalar_list(r, t, &c, &nl, text, 64u, &len))
            break;
        long v = var_index(&r->vars, cetta_gslt_source_variable_name_v1(x));
        if ((*var >= 0 && (v != *var || c != *cons || nl != *nil)))
            break;
        *var = v, *cons = c, *nil = nl;
    }
    return n;
}

/* Guards that a variable is none of several literal texts, as one test: a
 * switch on its text key, each literal of that key tested. */
static void emit_text_guards(NRuleV1 *r, const Atom *body, size_t k, size_t n, long var, long cons, long nil) {
    NBufV1 *o = r->out;
    nb_printf(o, "        {\n            uint64_t gn_key;\n            if (gn_text_key(cx, v%ld, %ldu, %ldu, &gn_key)) {\n"
                 "                switch (gn_key) {\n", var, cons + NATIVE_FIRST_OP_V1, nil + NATIVE_FIRST_OP_V1);
    uint64_t keys[256];
    size_t m = n < 256u ? n : 256u;
    for (size_t i = 0; i < m; i++) {
        const Atom *d = different_of(body->expr.elems[k + i]);
        const Atom *t = is_var(d->expr.elems[1]) ? d->expr.elems[2] : d->expr.elems[1];
        long c, nl;
        size_t len;
        uint32_t text[64];
        scalar_list(r, t, &c, &nl, text, 64u, &len);
        keys[i] = text_key(text, len);
        bool earlier = false;
        for (size_t q = 0; q < i && !earlier; q++)
            earlier = keys[q] == keys[i];
        if (earlier)
            continue;
        nb_printf(o, "                case 0x%016llxull:\n", (unsigned long long)keys[i]);
        /* every literal of this key */
        for (size_t j = i; j < m; j++) {
            const Atom *dj = different_of(body->expr.elems[k + j]);
            const Atom *tj = is_var(dj->expr.elems[1]) ? dj->expr.elems[2] : dj->expr.elems[1];
            size_t lj;
            uint32_t tx[64];
            scalar_list(r, tj, &c, &nl, tx, 64u, &lj);
            if (text_key(tx, lj) != keys[i])
                continue;
            char val[32];
            snprintf(val, sizeof val, "v%ld", var);
            nb_puts(o, "                    if (");
            emit_text_test(r, o, val, cons, nil, tx, lj);
            nb_printf(o, ") goto f%u;\n", r->label);
        }
        nb_puts(o, "                    break;\n");
    }
    nb_puts(o, "                }\n            }\n        }\n");
    r->label_used = true;
}

static bool emit_rule(NRuleV1 *r, const DirectRuleV1 *rule, const DirectBindingModeV1 *m,
                      unsigned label, bool commit) {
    NBufV1 body = {0};
    NBufV1 *outer = r->out;
    r->out = &body;
    r->vars.len = 0;
    r->temp = 0;
    r->label = label;
    r->label_used = false;
    r->rule = rule->source && rule->source->name ? rule->source->name : "?";
    const Atom *h = rule->head;
    r->head = h;
    r->mode = m;
    r->commit = commit;
    r->jumped = false;
    r->hole = 0;
    r->tail = -1;
    r->last = rule->body && rule->body->kind == ATOM_EXPR && rule->body->expr.len > 1u
                  ? rule->body->expr.elems[rule->body->expr.len - 1u]
                  : NULL;
    for (size_t i = 0; i < m->arity && !r->failed; i++) {
        if (!m->input_ground[i])
            continue;
        char arg[32];
        snprintf(arg, sizeof arg, "a%zu", i);
        emit_match(r, h->expr.elems[i + 1u], arg, arg);
    }
    const Atom *b = rule->body;
    for (size_t i = 1; b && b->kind == ATOM_EXPR && i < b->expr.len && !r->failed; i++) {
        long var = -1, cons = -1, nil = -1;
        size_t guards = r->scans && r->scans->len ? text_guards(r, b, i, &var, &cons, &nil) : 0u;
        if (guards >= 4u && guards <= 256u) {
            emit_text_guards(r, b, i, guards, var, cons, nil);
            i += guards - 1u;
            continue;
        }
        emit_premise(r, b->expr.elems[i]);
    }
    for (size_t i = 0, k = 0; i < m->arity && !r->failed && !r->jumped; i++) {
        if (m->input_ground[i])
            continue;
        nb_printf(&body, "        *r%zu = ", k++);
        emit_build(r, h->expr.elems[i + 1u], &body);
        nb_puts(&body, ";\n");
    }
    r->out = outer;
    if (r->failed || body.nomem) {
        free(body.bytes);
        return r->failed ? false : rule_error(r, "out of memory", NULL);
    }
    nb_puts(outer, "    /* rule ");
    nb_comment(outer, r->rule);
    nb_printf(outer, " (source rewrite %zu) */\n", rule->source_index);
    if (r->start_target && label >= r->start_base && label - r->start_base < r->start_count &&
        r->start_target[label - r->start_base])
        nb_printf(outer, "s%u:\n", label);
    nb_puts(outer, "    {\n");
    if (r->vars.len) {
        nb_puts(outer, "        GnVal");
        for (size_t i = 0; i < r->vars.len; i++)
            nb_printf(outer, "%s *v%zu = NULL", i ? "," : "", i);
        nb_puts(outer, ";\n");
    }
    if (r->temp) {
        nb_puts(outer, "        GnVal");
        for (unsigned i = 0; i < r->temp; i++)
            nb_printf(outer, "%s *t%u = NULL", i ? "," : "", i);
        nb_puts(outer, ";\n");
    }
    if (r->vars.len || r->temp) {
        nb_puts(outer, "        (void)0");
        for (size_t i = 0; i < r->vars.len; i++)
            nb_printf(outer, ", (void)v%zu", i);
        for (unsigned i = 0; i < r->temp; i++)
            nb_printf(outer, ", (void)t%u", i);
        nb_puts(outer, ";\n");
    }
    nb_put(outer, (const char *)body.bytes, body.len);
    if (!r->jumped) {
        nb_trace(outer, "        ", "RULE", r->rule, "");
        nb_puts(outer, "        return true;\n");
    }
    nb_puts(outer, "    }\n");
    if (r->label_used) {
        nb_printf(outer, "f%u:;\n", label);
        nb_trace(outer, "    ", "MISS", r->rule, "");
    }
    free(body.bytes);
    return true;
}

static bool rule_of_mode(const DirectRuleV1 *rule, const DirectBindingModeV1 *m) {
    const Atom *h = rule->head;
    return h->expr.len - 1u == m->arity && strcmp(sym(h->expr.elems[0]), m->relation) == 0;
}

typedef struct {
    long from, to;
} NEdgeV1;

typedef struct {
    NEdgeV1 *items;
    size_t len, cap;
    bool nomem;
} NEdgesV1;

static void edge_add(NEdgesV1 *es, long from, long to) {
    if (es->len == es->cap) {
        size_t cap = es->cap ? es->cap * 2u : 256u;
        NEdgeV1 *grown = realloc(es->items, cap * sizeof *grown);
        if (!grown) {
            es->nomem = true;
            return;
        }
        es->items = grown;
        es->cap = cap;
    }
    es->items[es->len++] = (NEdgeV1){from, to};
}

/* The rules of mode m (index mi) in their authored order, then the
 * failure; the tail calls found go to edges when given. */
static void mode_bits(const DirectBindingModeV1 *m, char *bits);

/* The mode's name in the trace: RELATION:BITS. */
static void mode_trace_name(const DirectBindingModeV1 *m, char *out, size_t cap) {
    char bits[80];
    mode_bits(m, bits);
    snprintf(out, cap, "%s:%s", m->relation, bits);
}

/* The trace hook of entering mode M, with its inputs. */
static void trace_call(NBufV1 *o, const DirectBindingModeV1 *m) {
    char name[320], args[64 * 8 + 16];
    size_t n = 0, k = 0;
    mode_trace_name(m, name, sizeof name);
    for (size_t a = 0; a < m->arity; a++)
        if (m->input_ground[a])
            k += (size_t)snprintf(args + k, sizeof args - k, ", a%zu", a), n++;
    char tail[64 * 8 + 32];
    snprintf(tail, sizeof tail, ", %zuu%s", n, n ? args : ", NULL");
    nb_trace(o, "    ", "CALL", name, tail);
}

/* The operator a head pattern requires of its value (the runtime's id),
 * or -1 when it takes any value or a list. */
static long pattern_key(const NRuleV1 *r, const Atom *p) {
    int64_t n;
    if (!p || is_var(p) || is_int(p, &n))
        return -1;
    const char *s = sym(p);
    long id;
    if (s) {
        id = op_find(r->ops, s, 0u);
        return id < 0 || id == r->op_nil ? -1 : id + NATIVE_FIRST_OP_V1;
    }
    if (p->kind != ATOM_EXPR || p->expr.len == 0u || !sym(p->expr.elems[0]))
        return -1;
    id = op_find(r->ops, sym(p->expr.elems[0]), p->expr.len - 1u);
    return id < 0 || id == r->op_cons ? -1 : id + NATIVE_FIRST_OP_V1;
}

/* First-argument indexing, as in the WAM's switch_on_term: when a mode's
 * rules require several different operators of one input, a switch on that
 * input's operator jumps to the first rule that could match it.  The rules
 * it passes would each fail on that input's operator, and a rule does
 * nothing but build values before it fails.  The input chosen is the one
 * whose patterns tell the most rules apart; a mode of fewer than four
 * rules, or with no input whose patterns name three operators, keeps its
 * plain order.  Writes the switch and marks the rules it jumps to: 0 when
 * the mode keeps its order, 1 with a switch, 2 with a switch that can go
 * straight to the mode's end (x<mode>). */
/* Indexing on a text, for rules that test one input against literal texts
 * (a keyword table): a switch on the input's text key (gn_text_key) jumps to
 * the first rule that could match it, a rule taking any value or the first
 * with a literal of that key; each still tests its own text, so keys that
 * collide only cost a test.  An input that is no text, or whose key no
 * literal has, goes to the first rule taking any value.  Returns as
 * emit_dispatch does; 0 when no input is tested against four texts. */
static int emit_text_dispatch(NRuleV1 *r, const DirectRulesV1 *rules, const DirectBindingModeV1 *m, long mi,
                              unsigned first, bool *target, size_t count) {
    long best = -1, cons = -1, nil = -1;
    for (size_t i = 0; i < m->arity && best < 0; i++) {
        if (!m->input_ground[i])
            continue;
        size_t texts = 0;
        long c = -1, n = -1;
        bool table = true;
        for (size_t k = 0; k < rules->len && table; k++) {
            const DirectRuleV1 *rule = &rules->items[k];
            if (!rule_of_mode(rule, m) || is_var(rule->head->expr.elems[i + 1u]))
                continue;
            long rc, rn;
            size_t len;
            uint32_t text[64];
            table = scalar_list(r, rule->head->expr.elems[i + 1u], &rc, &rn, text, 64u, &len) &&
                    (c < 0 || (rc == c && rn == n));
            c = rc, n = rn;
            texts++;
        }
        if (table && texts >= 4u)
            best = (long)i, cons = c, nil = n;
    }
    if (best < 0)
        return 0;
    long any = -1;
    for (size_t k = 0, j = 0; k < rules->len && any < 0; k++)
        if (rule_of_mode(&rules->items[k], m)) {
            if (is_var(rules->items[k].head->expr.elems[best + 1]))
                any = (long)j;
            j++;
        }
    if (any == 0)
        return 0;   /* the first rule takes any value: every key would go there */
    uint64_t *keys = calloc(count, sizeof *keys);
    bool *literal = calloc(count, sizeof *literal);
    if (!keys || !literal) {
        free(keys);
        free(literal);
        return 0;
    }
    nb_printf(r->out, "    {\n        uint64_t gn_key;\n        if (gn_text_key(cx, a%ld, %ldu, %ldu, &gn_key)) {\n"
                      "            switch (gn_key) {\n", best, cons + NATIVE_FIRST_OP_V1, nil + NATIVE_FIRST_OP_V1);
    for (size_t k = 0, j = 0; k < rules->len; k++) {
        const DirectRuleV1 *rule = &rules->items[k];
        if (!rule_of_mode(rule, m))
            continue;
        long rc, rn;
        size_t len;
        uint32_t text[64];
        const Atom *p = rule->head->expr.elems[best + 1];
        if (!is_var(p) && scalar_list(r, p, &rc, &rn, text, 64u, &len)) {
            keys[j] = text_key(text, len);
            literal[j] = true;
            bool earlier = false;
            for (size_t q = 0; q < j && !earlier; q++)
                earlier = literal[q] && keys[q] == keys[j];
            if (!earlier) {
                size_t to = any >= 0 && (size_t)any < j ? (size_t)any : j;
                target[to] = true;
                nb_printf(r->out, "            case 0x%016llxull: goto s%u;\n", (unsigned long long)keys[j],
                          first + (unsigned)to);
            }
        }
        j++;
    }
    free(keys);
    free(literal);
    nb_puts(r->out, "            }\n        }\n");
    if (any >= 0) {
        target[any] = true;
        nb_printf(r->out, "        goto s%u;\n    }\n", first + (unsigned)any);
        return 1;
    }
    nb_printf(r->out, "        goto x%ld;\n    }\n", mi);
    return 2;
}

static int emit_dispatch(NRuleV1 *r, const DirectRulesV1 *rules, const DirectBindingModeV1 *m, long mi,
                         unsigned first, bool *target, size_t count) {
    if (count < 4u)
        return 0;
    int text = r->scans && r->scans->len ? emit_text_dispatch(r, rules, m, mi, first, target, count) : 0;
    if (text)
        return text;
    long best = -1;
    size_t best_distinct = 0;
    long *keys = malloc(count * sizeof *keys);
    if (!keys)
        return 0;
    for (size_t i = 0; i < m->arity; i++) {
        if (!m->input_ground[i])
            continue;
        size_t q = 0, distinct = 0;
        for (size_t k = 0; k < rules->len; k++) {
            const DirectRuleV1 *rule = &rules->items[k];
            if (!rule_of_mode(rule, m))
                continue;
            long key = pattern_key(r, rule->head->expr.elems[i + 1u]);
            bool seen = key < 0;
            for (size_t j = 0; !seen && j < q; j++)
                seen = keys[j] == key;
            keys[q++] = key;
            if (!seen)
                distinct++;
        }
        if (distinct > best_distinct) {
            best = (long)i;
            best_distinct = distinct;
        }
    }
    if (best < 0 || best_distinct < 3u) {
        free(keys);
        return 0;
    }
    size_t q = 0;
    for (size_t k = 0; k < rules->len; k++)
        if (rule_of_mode(&rules->items[k], m))
            keys[q++] = pattern_key(r, rules->items[k].head->expr.elems[best + 1]);
    long any = -1;
    bool spans = false;
    for (size_t j = 0; j < count; j++) {
        if (keys[j] < 0 && any < 0)
            any = (long)j;
        spans |= keys[j] >= 0 && span_nil(r->scans, keys[j] - NATIVE_FIRST_OP_V1) >= 0;
    }
    /* a span is dispatched as the cell it begins with */
    nb_printf(r->out, spans ? "    switch (gn_op_of(a%ld)) {\n" : "    switch (a%ld->op) {\n", best);
    for (size_t j = 0; j < count; j++) {
        bool earlier = keys[j] < 0;
        for (size_t i = 0; !earlier && i < j; i++)
            earlier = keys[i] == keys[j];
        if (earlier)
            continue;
        /* the first rule with this operator, or an earlier one taking any */
        size_t to = any >= 0 && (size_t)any < j ? (size_t)any : j;
        target[to] = true;
        nb_printf(r->out, "    case %ldu: goto s%u;\n", keys[j], first + (unsigned)to);
    }
    free(keys);
    if (any >= 0) {
        target[any] = true;
        nb_printf(r->out, "    default: goto s%u;\n    }\n", first + (unsigned)any);
        return 1;
    }
    nb_printf(r->out, "    default: goto x%ld;\n    }\n", mi);
    return 2;
}

static void emit_mode_rules(NRuleV1 *r, const DirectRulesV1 *rules, const DirectBindingModeV1 *m, long mi,
                            unsigned *label, NEdgesV1 *edges) {
    memo_reset(r);
    r->memo_base = r->memo_next;
    size_t last = rules->len, count = 0;
    for (size_t k = 0; k < rules->len; k++)
        if (rule_of_mode(&rules->items[k], m))
            last = k, count++;
    bool *target = count ? calloc(count, sizeof *target) : NULL;
    int indexed = target ? emit_dispatch(r, rules, m, mi, *label, target, count) : 0;
    r->start_target = indexed ? target : NULL;
    r->start_base = *label;
    r->start_count = (unsigned)count;
    for (size_t k = 0; k < rules->len && !r->failed; k++) {
        const DirectRuleV1 *rule = &rules->items[k];
        if (!rule_of_mode(rule, m))
            continue;
        emit_rule(r, rule, m, (*label)++, k == last || m->input_heads_disjoint);
        if (edges && r->tail >= 0)
            edge_add(edges, mi, r->tail);
    }
    char name[320];
    mode_trace_name(m, name, sizeof name);
    if (indexed == 2)
        nb_printf(r->out, "x%ld:;\n", mi);
    nb_trace(r->out, "    ", "FAIL", name, "");
    nb_puts(r->out, "    return false;\n");
    r->start_target = NULL;
    free(target);
    r->memo_next = r->memo_base + (unsigned)r->memo_len;
}

/* The kept equality tests gq<first>..gq<end-1>, declared or reset. */
static void memo_declare(NBufV1 *o, unsigned first, unsigned end) {
    if (first == end)
        return;
    nb_puts(o, "    int");
    for (unsigned g = first; g < end; g++)
        nb_printf(o, "%s gq%u = -1", g == first ? "" : ",", g);
    nb_puts(o, ";\n");
}

static void memo_resets(NBufV1 *o, unsigned first, unsigned end) {
    for (unsigned g = first; g < end; g++)
        nb_printf(o, "    gq%u = -1;\n", g);
}

/* Tail groups: the strongly connected components of the tail-call graph
 * with a cycle in them (Tarjan's algorithm, iteratively).  group_of[i] is
 * mode i's group or -1; the count of groups is returned, -1 when memory ran
 * out. */
static long tail_groups(const NEdgesV1 *es, size_t n, long *group_of) {
    size_t *start = calloc(n + 1u, sizeof *start), *fill = calloc(n + 1u, sizeof *fill);
    long *adj = malloc((es->len ? es->len : 1u) * sizeof *adj);
    long *index = malloc((n ? n : 1u) * sizeof *index), *low = malloc((n ? n : 1u) * sizeof *low);
    long *stack = malloc((n ? n : 1u) * sizeof *stack), *call = malloc((n ? n : 1u) * sizeof *call);
    size_t *pos = malloc((n ? n : 1u) * sizeof *pos);
    bool *on = calloc(n ? n : 1u, sizeof *on);
    long groups = -1;
    if (!start || !fill || !adj || !index || !low || !stack || !call || !pos || !on)
        goto out;
    for (size_t e = 0; e < es->len; e++)
        start[es->items[e].from + 1]++;
    for (size_t i = 0; i < n; i++)
        start[i + 1u] += start[i];
    for (size_t e = 0; e < es->len; e++) {
        size_t f = (size_t)es->items[e].from;
        adj[start[f] + fill[f]++] = es->items[e].to;
    }
    for (size_t i = 0; i < n; i++)
        index[i] = -1, group_of[i] = -1;
    groups = 0;
    long next = 0;
    size_t sp = 0, cs = 0;
    for (size_t s0 = 0; s0 < n; s0++) {
        if (index[s0] >= 0)
            continue;
        index[s0] = low[s0] = next++;
        stack[sp++] = (long)s0;
        on[s0] = true;
        call[cs] = (long)s0;
        pos[cs++] = start[s0];
        while (cs) {
            size_t v = (size_t)call[cs - 1u];
            if (pos[cs - 1u] < start[v + 1u]) {
                size_t w = (size_t)adj[pos[cs - 1u]++];
                if (index[w] < 0) {
                    index[w] = low[w] = next++;
                    stack[sp++] = (long)w;
                    on[w] = true;
                    call[cs] = (long)w;
                    pos[cs++] = start[w];
                } else if (on[w] && index[w] < low[v]) {
                    low[v] = index[w];
                }
                continue;
            }
            if (low[v] == index[v]) {
                size_t begin = sp;
                do
                    begin--;
                while ((size_t)stack[begin] != v);
                bool cyclic = sp - begin > 1u;
                for (size_t e = start[v]; !cyclic && e < start[v + 1u]; e++)
                    cyclic = (size_t)adj[e] == v;
                for (size_t q = begin; q < sp; q++) {
                    on[stack[q]] = false;
                    group_of[stack[q]] = cyclic ? groups : -1;
                }
                groups += cyclic ? 1 : 0;
                sp = begin;
            }
            cs--;
            if (cs) {
                size_t u = (size_t)call[cs - 1u];
                if (low[v] < low[u])
                    low[u] = low[v];
            }
        }
    }
out:
    free(start);
    free(fill);
    free(adj);
    free(index);
    free(low);
    free(stack);
    free(call);
    free(pos);
    free(on);
    return groups;
}

/* A class run's class, gn_k<mode>: scalars below 128 by bit, the others
 * by range. */
static void emit_scan_class(NBufV1 *o, const NScanV1 *scan) {
    uint64_t ascii[2] = {0, 0};
    size_t above = 0;
    for (size_t i = 0; i < scan->range_len; i++) {
        for (uint32_t c = scan->ranges[i][0]; c <= scan->ranges[i][1] && c < 128u; c++)
            ascii[c >> 6] |= (uint64_t)1 << (c & 63u);
        above += scan->ranges[i][1] >= 128u ? 1u : 0u;
    }
    if (above) {
        nb_printf(o, "static const uint32_t gn_k%ld_ranges[][2] = {", scan->mode);
        for (size_t i = 0, k = 0; i < scan->range_len; i++) {
            if (scan->ranges[i][1] < 128u)
                continue;
            uint32_t lo = scan->ranges[i][0] < 128u ? 128u : scan->ranges[i][0];
            nb_printf(o, "%s{%uu, %uu}", k++ ? ", " : "", lo, scan->ranges[i][1]);
        }
        nb_puts(o, "};\n");
    }
    nb_printf(o, "static const GnClassV1 gn_k%ld = {{0x%016llxull, 0x%016llxull}, ", scan->mode,
              (unsigned long long)ascii[0], (unsigned long long)ascii[1]);
    if (above)
        nb_printf(o, "gn_k%ld_ranges, %zuu};\n", scan->mode, above);
    else
        nb_puts(o, "NULL, 0u};\n");
}

/* A class run's answer by the runtime's scan, when its input is a source
 * list; the mode's rules otherwise (the run's text is a span). */
static void emit_scan(NBufV1 *o, const NScanV1 *scan, const DirectBindingModeV1 *m) {
    size_t input = 0;
    for (size_t a = 0; a < m->arity; a++)
        if (m->input_ground[a])
            input = a;
    char name[320];
    mode_trace_name(m, name, sizeof name);
    nb_puts(o, "    {\n        GnVal *gn_run, *gn_after;\n");
    nb_printf(o, "        if (gn_scan_class(cx, a%zu, &gn_k%ld, %ldu, %ldu, &gn_run, &gn_after)) {\n", input,
              scan->mode, scan->cons + NATIVE_FIRST_OP_V1, scan->nil + NATIVE_FIRST_OP_V1);
    char args[48];
    snprintf(args, sizeof args, ", 1u, a%zu", input);
    nb_trace(o, "            ", "CALL", name, args);
    nb_trace(o, "            ", "RULE", "gslt:scan", "");
    nb_printf(o, "            *r%zu = gn_run;\n            *r%zu = gn_after;\n            return true;\n        }\n    }\n",
              scan->run_out, scan->after_out);
}

static size_t mode_outputs(const DirectBindingModeV1 *m) {
    size_t outs = 0;
    for (size_t i = 0; i < m->arity; i++)
        outs += m->input_ground[i] ? 0u : 1u;
    return outs;
}

static void signature(NBufV1 *o, const DirectBindingModesV1 *modes, const DirectBindingModeV1 *m,
                      const char *name) {
    nb_printf(o, "bool %s(GnCtx *cx", name);
    for (size_t i = 0, k = 0; i < m->arity; i++)
        if (m->input_ground[i])
            nb_printf(o, ", GnVal *a%zu", i);
        else
            nb_printf(o, ", GnVal **r%zu", k++);
    (void)modes;
    nb_puts(o, ")");
}

static void mode_bits(const DirectBindingModeV1 *m, char *bits) {
    for (size_t i = 0; i < m->arity; i++)
        bits[i] = m->input_ground[i] ? '1' : '0';
    bits[m->arity] = 0;
}

/* ── token automata ───────────────────────────────────────────────────── */

static const Atom *different_of(const Atom *p);

/*
 * A token automaton: a mode whose input is the source list, evaluated with
 * that input's structure known and its scalars not, into one C function
 * over byte positions.  Its rules and the relations they call are unfolded
 * in place, in their order: a peek, a class table or a lookahead becomes a
 * test on the scalar at a position after the cursor; values become terms of
 * those positions; a tail call of the mode itself on a later cursor loops
 * with the cursor moved, after writing what the rules wrap around its
 * answer (with a hole the next round fills).  A relation whose unfolding
 * would not be finite (it calls itself), or whose inputs include values
 * only a call yields, stays a call of its compiled mode (a residual call).
 * A mode whose unfolding cannot be read stays as compiled.
 *
 * The unfolding builds a decision tree.  A path knows, for each position
 * up to its lookahead, whether the input ends there or which ranges its
 * scalar lies in; a pattern that needs more splits the path.  A rule that
 * fails on a path hands that path to the mode's next rule, and a mode with
 * no rule left fails to its caller's next rule, as the compiled modes do.
 * A relation called on scalars already read (a class table) is unfolded
 * on its own and its answers grouped by value, so its caller goes on once
 * per distinct answer.
 */

enum { NAUTO_LOOK_V1 = 16, NAUTO_DEPTH_V1 = 24, NAUTO_NODES_V1 = 200000, NAUTO_RESULTS_V1 = 1024,
       NAUTO_TEXTS_V1 = 4, NAUTO_LITS_V1 = 256 };

typedef enum { TV_CUR, TV_SCALAR, TV_INT, TV_APP, TV_DYN, TV_HOLE, TV_SPAN } NTvKindV1;

/* A value of the unfolding: the source list from position `at` on (0 the
 * cursor), the scalar at `at` as (cp N), an integer, a constructor
 * application, output n of residual call `at`, output n of the loop's next
 * round (a hole), or the text from position `at` to position n as a span
 * of op and nil (a scan's). */
typedef struct NTvV1 NTvV1;
struct NTvV1 {
    NTvKindV1 kind;
    int at;
    int64_t n;
    long op, nil;
    size_t nk;
    NTvV1 **kid;
};

/* What a path knows of each position.  Position 0 is the cursor; a
 * position gets a successor when its scalar is read, and a scan's end is a
 * position of its own. */
typedef struct {
    signed char end[NAUTO_LOOK_V1];      /* 1 the input ends there, 0 a scalar is there, -1 not known */
    NSetV1 set[NAUTO_LOOK_V1];           /* the scalar's ranges */
    signed char succ[NAUTO_LOOK_V1];     /* the position after it, once its scalar is read; -1 before */
    int used;                            /* positions 0..used-1 are named */
    /* what is known of spans' texts: equal to one literal, or none of some */
    struct {
        signed char at, to;
        long cons;
        short eq;                        /* a literal's id, or -1 */
        uint64_t ne[NAUTO_LITS_V1 / 64]; /* literals it is not */
    } texts[NAUTO_TEXTS_V1];
    int ntexts;
} NApathV1;

typedef enum { TN_FAIL, TN_OUT, TN_END, TN_READ, TN_SET, TN_CALL, TN_LOOP, TN_SCAN, TN_TEXT } NTnKindV1;

typedef struct NTnV1 NTnV1;
struct NTnV1 {
    NTnKindV1 kind;
    int at;            /* END, READ, SET: the position; LOOP: the cursor's next position */
    NTnV1 *yes, *no;   /* END: at the end / a scalar; READ: next; SET: no case; CALL: answered / not */
    size_t ncase;
    NSetV1 *sets;      /* SET: each case's scalars */
    NTnV1 **cases;
    long mode;         /* CALL */
    NTvV1 **vals;      /* CALL: inputs by argument position; OUT, LOOP: the root's outputs */
    size_t nvals;
    int var;           /* CALL: its outputs are x<var>_<k>; READ: the next position; SCAN, TEXT: the end */
};

/* A literal text the unfolding compared a span with: its scalars, and its
 * bytes when they read back as those scalars (a span, aligned with the
 * source's reading, equals it exactly when their bytes agree; a literal
 * whose bytes read back otherwise equals no span). */
typedef struct {
    uint32_t s[64];
    size_t n;
    long cons, nil;
    uint8_t bytes[256];
    size_t len;
    bool round;
} NAlitV1;

/* The rule a call came from: where it goes on once the callee answers,
 * and whose next rule it fails to. */
typedef struct NKontV1 NKontV1;
struct NKontV1 {
    const DirectRuleV1 *rule;   /* NULL: a class table's answers are collected */
    const DirectBindingModeV1 *mode;
    size_t idx;                 /* the rule's index in the rules */
    size_t prem;                /* the calling premise (body element) */
    const NBindV1 *b;
    NTvV1 **in;                 /* the mode's inputs, by argument position */
    bool tail;                  /* the calling premise is its rule's last */
    const NKontV1 *next;
};

typedef struct {
    NApathV1 p;
    NTvV1 **outs;
} NAresultV1;

typedef struct {
    NScanCtxV1 sc;       /* the arena, the program, failure */
    const NScansV1 *scans;
    const DirectBindingModeV1 *root;
    size_t root_outs;
    size_t nodes;
    int calls;           /* residual calls so far: their variables */
    NAresultV1 *results; /* a class table's answers, while collected */
    size_t nresults;
    NAlitV1 *lits;       /* the literal texts compared, by id (NAUTO_LITS_V1) */
    size_t nlits;
} NAutoCtxV1;

static NTvV1 *tv_new(NAutoCtxV1 *x, NTvKindV1 kind, size_t nk) {
    NTvV1 *v = sc_alloc(&x->sc, sizeof *v);
    NTvV1 **kid = nk ? sc_alloc(&x->sc, nk * sizeof *kid) : NULL;
    if (!v || (nk && !kid))
        return NULL;
    *v = (NTvV1){kind, 0, 0, -1, -1, nk, kid};
    return v;
}

static NTvV1 *tv_at(NAutoCtxV1 *x, NTvKindV1 kind, int at) {
    NTvV1 *v = tv_new(x, kind, 0u);
    if (v)
        v->at = at;
    return v;
}

static NTnV1 *tn_new(NAutoCtxV1 *x, NTnKindV1 kind) {
    if (++x->nodes > NAUTO_NODES_V1) {
        sc_unread(&x->sc);
        return NULL;
    }
    NTnV1 *t = sc_alloc(&x->sc, sizeof *t);
    if (t)
        *t = (NTnV1){.kind = kind};
    return t;
}

static NApathV1 apath_all(NAutoCtxV1 *x) {
    NApathV1 p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i < NAUTO_LOOK_V1; i++) {
        p.end[i] = -1;
        p.succ[i] = -1;
        p.set[i] = set_one(&x->sc, 0u, NSCAN_MAX_SCALAR_V1);
    }
    p.used = 1;
    return p;
}

static bool tv_static(const NTvV1 *v) {
    if (v->kind == TV_INT)
        return true;
    if (v->kind != TV_APP)
        return false;
    for (size_t i = 0; i < v->nk; i++)
        if (!tv_static(v->kid[i]))
            return false;
    return true;
}

static bool tv_same(const NTvV1 *a, const NTvV1 *b) {
    if (a->kind != b->kind)
        return false;
    switch (a->kind) {
    case TV_CUR:
    case TV_SCALAR:
        return a->at == b->at;
    case TV_INT:
        return a->n == b->n;
    case TV_DYN:
    case TV_HOLE:
        return a->at == b->at && a->n == b->n;
    case TV_SPAN:
        return a->at == b->at && a->n == b->n && a->op == b->op && a->nil == b->nil;
    case TV_APP:
        if (a->op != b->op || a->nk != b->nk)
            return false;
        for (size_t i = 0; i < a->nk; i++)
            if (!tv_same(a->kid[i], b->kid[i]))
                return false;
        return true;
    }
    return false;
}

static bool tv_has(const NTvV1 *v, NTvKindV1 kind) {
    if (v->kind == kind)
        return true;
    for (size_t i = 0; v->kind == TV_APP && i < v->nk; i++)
        if (tv_has(v->kid[i], kind))
            return true;
    return false;
}

/* The integer N of a static (cp N) in range, or -1. */
static int64_t tv_cp(const NAutoCtxV1 *x, const NTvV1 *v) {
    if (v->kind == TV_APP && v->op == x->sc.op_cp && v->nk == 1u && v->kid[0]->kind == TV_INT &&
        v->kid[0]->n >= 0 && v->kid[0]->n <= NSCAN_MAX_SCALAR_V1)
        return v->kid[0]->n;
    return -1;
}

/* The value of term t, its variables bound, on path p: a cell of a run's
 * constructor holding the scalar just before a span of that run is the
 * longer span. */
static NTvV1 *tv_term(NAutoCtxV1 *x, const Atom *t, const NBindV1 *b, const NApathV1 *p) {
    if (is_var(t)) {
        const NBindV1 *v = bind_find(b, cetta_gslt_source_variable_name_v1(t));
        if (!v)
            sc_unread(&x->sc);
        return v ? (NTvV1 *)v->v : NULL;
    }
    int64_t n;
    if (is_int(t, &n)) {
        NTvV1 *v = tv_new(x, TV_INT, 0u);
        if (v)
            v->n = n;
        return v;
    }
    const char *s = sym(t);
    if (s) {
        long id = op_find(x->sc.ops, s, 0u);
        if (id < 0 || id == x->sc.op_nil) {
            sc_unread(&x->sc);
            return NULL;
        }
        NTvV1 *v = tv_new(x, TV_APP, 0u);
        if (v)
            v->op = id;
        return v;
    }
    if (!t || t->kind != ATOM_EXPR || t->expr.len == 0u || !sym(t->expr.elems[0])) {
        sc_unread(&x->sc);
        return NULL;
    }
    size_t arity = t->expr.len - 1u;
    long id = op_find(x->sc.ops, sym(t->expr.elems[0]), arity);
    if (id < 0 || id == x->sc.op_cons) {
        sc_unread(&x->sc);
        return NULL;
    }
    NTvV1 *v = tv_new(x, TV_APP, arity);
    if (!v)
        return NULL;
    v->op = id;
    for (size_t i = 0; i < arity; i++)
        if (!(v->kid[i] = tv_term(x, t->expr.elems[i + 1u], b, p)))
            return NULL;
    const NTvV1 *h = arity == 2u ? v->kid[0] : NULL, *r = arity == 2u ? v->kid[1] : NULL;
    if (h && h->kind == TV_SCALAR && r->kind == TV_SPAN && r->op == id && p->succ[h->at] == r->at) {
        NTvV1 *span = tv_new(x, TV_SPAN, 0u);
        if (span)
            *span = *r, span->at = h->at;
        return span;
    }
    return v;
}

/* ── the unfolding ── */

/* Where matching goes: on a match, rule `rule` of `mode` runs from body
 * element `prem` on; on a mismatch, the mode's next rule runs. */
typedef struct {
    const DirectRuleV1 *rule;
    const DirectBindingModeV1 *mode;
    size_t idx, prem;
    NTvV1 **in;
    const NKontV1 *k;
} NAcontV1;

static NTnV1 *au_mode(NAutoCtxV1 *x, const DirectBindingModeV1 *m, size_t from, NTvV1 **in, NApathV1 p,
                      const NKontV1 *k);
static NTnV1 *au_body(NAutoCtxV1 *x, const NAcontV1 *c, const NBindV1 *b, NApathV1 p);
static NTnV1 *au_match(NAutoCtxV1 *x, const Atom *const *pats, NTvV1 *const *vals, size_t n, size_t i,
                       const NBindV1 *b, NApathV1 p, const NAcontV1 *c);

static NTnV1 *au_mismatch(NAutoCtxV1 *x, const NAcontV1 *c, NApathV1 p) {
    return au_mode(x, c->mode, c->idx + 1u, c->in, p, c->k);
}

/* The scalar at `at` in `set` or not: the path split, or the one branch
 * the path already takes.  `equal_goes_on` says which branch continues
 * matching from pair `next` (the other mismatches). */
static NTnV1 *au_split(NAutoCtxV1 *x, int at, NSetV1 set, bool equal_goes_on, const Atom *const *pats,
                       NTvV1 *const *vals, size_t n, size_t next, const NBindV1 *b, NApathV1 p, const NAcontV1 *c) {
    NSetV1 in = set_inter(&x->sc, p.set[at], set), out = set_minus(&x->sc, p.set[at], set);
    NApathV1 pin = p, pout = p;
    pin.set[at] = in;
    pout.set[at] = out;
    if (!out.len)
        return equal_goes_on ? au_match(x, pats, vals, n, next, b, p, c) : au_mismatch(x, c, p);
    if (!in.len)
        return equal_goes_on ? au_mismatch(x, c, p) : au_match(x, pats, vals, n, next, b, p, c);
    NTnV1 *t = tn_new(x, TN_SET);
    NSetV1 *sets = sc_alloc(&x->sc, sizeof *sets);
    NTnV1 **cases = sc_alloc(&x->sc, sizeof *cases);
    if (!t || !sets || !cases)
        return NULL;
    t->at = at;
    t->ncase = 1u;
    t->sets = sets;
    t->cases = cases;
    sets[0] = in;
    cases[0] = equal_goes_on ? au_match(x, pats, vals, n, next, b, pin, c) : au_mismatch(x, c, pin);
    t->no = equal_goes_on ? au_mismatch(x, c, pout) : au_match(x, pats, vals, n, next, b, pout, c);
    return x->sc.failed ? NULL : t;
}

/* The pairs with pair i replaced by pairs (subs[j], kids[j]). */
static bool au_expand(NAutoCtxV1 *x, const Atom *const *pats, NTvV1 *const *vals, size_t n, size_t i,
                      const Atom *const *subs, NTvV1 *const *kids, size_t k, const Atom ***out_pats,
                      NTvV1 ***out_vals, size_t *out_n) {
    size_t m = n - 1u + k;
    const Atom **np = sc_alloc(&x->sc, (m ? m : 1u) * sizeof *np);
    NTvV1 **nv = sc_alloc(&x->sc, (m ? m : 1u) * sizeof *nv);
    if (!np || !nv)
        return false;
    size_t q = 0;
    for (size_t j = 0; j < i; j++)
        np[q] = pats[j], nv[q++] = vals[j];
    for (size_t j = 0; j < k; j++)
        np[q] = subs[j], nv[q++] = kids[j];
    for (size_t j = i + 1u; j < n; j++)
        np[q] = pats[j], nv[q++] = vals[j];
    *out_pats = np;
    *out_vals = nv;
    *out_n = m;
    return true;
}

/* Literal text t of a run's constructors (CONS (cp N) ... NIL): its id in
 * the literals compared, or -1 when t is no such literal. */
static int au_literal(NAutoCtxV1 *x, const Atom *t, long cons, long nil) {
    NAlitV1 l;
    memset(&l, 0, sizeof l);
    l.cons = cons;
    l.nil = nil;
    for (;;) {
        const char *name = sym(t);
        if (name) {
            if (op_find(x->sc.ops, name, 0u) != nil)
                return -1;
            break;
        }
        if (!t || t->kind != ATOM_EXPR || t->expr.len != 3u || !sym(t->expr.elems[0]) ||
            op_find(x->sc.ops, sym(t->expr.elems[0]), 2u) != cons || l.n == 64u)
            return -1;
        const Atom *h = t->expr.elems[1];
        int64_t c;
        if (!h || h->kind != ATOM_EXPR || h->expr.len != 2u || !sym(h->expr.elems[0]) ||
            op_find(x->sc.ops, sym(h->expr.elems[0]), 1u) != x->sc.op_cp || !is_int(h->expr.elems[1], &c) ||
            c < 0 || c > NSCAN_MAX_SCALAR_V1)
            return -1;
        l.s[l.n++] = (uint32_t)c;
        t = t->expr.elems[2];
    }
    for (size_t i = 0; i < x->nlits; i++)
        if (x->lits[i].n == l.n && x->lits[i].cons == cons && memcmp(x->lits[i].s, l.s, l.n * sizeof *l.s) == 0)
            return (int)i;
    if (x->nlits == NAUTO_LITS_V1)
        return -1;
    for (size_t i = 0; i < l.n; i++)
        l.len += text_bytes(l.s[i], l.bytes + l.len);
    GnSourceV1 src = {l.bytes, l.len};
    size_t pos = 0, i = 0;
    for (; pos < l.len && i < l.n; i++) {
        uint32_t c;
        pos += gn_utf8_next(&src, pos, &c);
        if (c != l.s[i])
            break;
    }
    l.round = pos == l.len && i == l.n;
    x->lits[x->nlits] = l;
    return (int)x->nlits++;
}

/* The slot of path p's facts about span v, or -1 when full. */
static int au_text_slot(NApathV1 *p, const NTvV1 *v) {
    for (int i = 0; i < p->ntexts; i++)
        if (p->texts[i].at == v->at && p->texts[i].to == v->n && p->texts[i].cons == v->op)
            return i;
    if (p->ntexts == NAUTO_TEXTS_V1)
        return -1;
    int i = p->ntexts++;
    memset(&p->texts[i], 0, sizeof p->texts[i]);
    p->texts[i].at = (signed char)v->at;
    p->texts[i].to = (signed char)v->n;
    p->texts[i].cons = v->op;
    p->texts[i].eq = -1;
    return i;
}

/* Span v against literal `lit`: equal goes to `on_equal` (1 continue with
 * the pairs from `next`, 0 the mismatch), the other way otherwise. */
static NTnV1 *au_text(NAutoCtxV1 *x, NTvV1 *v, int lit, bool equal_goes_on, const Atom *const *pats,
                      NTvV1 *const *vals, size_t n, size_t next, const NBindV1 *b, NApathV1 p, const NAcontV1 *c) {
    const NAlitV1 *l = &x->lits[lit];
    NApathV1 pe = p, pn = p;
    int se = au_text_slot(&pe, v), sn = au_text_slot(&pn, v);
    if (se < 0 || sn < 0)
        return (sc_unread(&x->sc), NULL);
    int known = pe.texts[se].eq;
    bool equal_possible = l->round && v->nil == l->nil && (known < 0 || known == lit) &&
                          !((pe.texts[se].ne[lit / 64] >> (lit % 64)) & 1u);
    bool differ_possible = known != lit;
    pe.texts[se].eq = (short)lit;
    pn.texts[sn].ne[lit / 64] |= (uint64_t)1 << (lit % 64);
    NTnV1 *yes = NULL, *no = NULL;
    if (equal_possible)
        yes = equal_goes_on ? au_match(x, pats, vals, n, next, b, pe, c) : au_mismatch(x, c, pe);
    if (differ_possible)
        no = equal_goes_on ? au_mismatch(x, c, pn) : au_match(x, pats, vals, n, next, b, pn, c);
    if (x->sc.failed)
        return NULL;
    if (!equal_possible)
        return no;
    if (!differ_possible)
        return yes;
    NTnV1 *t = tn_new(x, TN_TEXT);
    if (!t)
        return NULL;
    t->at = v->at;
    t->var = (int)v->n;
    t->mode = lit;
    t->yes = yes;
    t->no = no;
    return t;
}

/* Pairs (pats[j], vals[j]) from i on, then c. */
static NTnV1 *au_match(NAutoCtxV1 *x, const Atom *const *pats, NTvV1 *const *vals, size_t n, size_t i,
                       const NBindV1 *b, NApathV1 p, const NAcontV1 *c) {
    if (x->sc.failed)
        return NULL;
    if (i == n)
        return au_body(x, c, b, p);
    const Atom *pat = pats[i];
    NTvV1 *v = vals[i];
    if (is_var(pat)) {
        const char *name = cetta_gslt_source_variable_name_v1(pat);
        const NBindV1 *had = bind_find(b, name);
        if (!had) {
            const NBindV1 *nb = bind_add(&x->sc, b, name, (NSvV1 *)v);
            return nb ? au_match(x, pats, vals, n, i + 1u, nb, p, c) : NULL;
        }
        const NTvV1 *w = (const NTvV1 *)had->v;
        if (tv_same(w, v))
            return au_match(x, pats, vals, n, i + 1u, b, p, c);
        if (tv_static(w) && tv_static(v))
            return au_mismatch(x, c, p);
        const NTvV1 *s = w->kind == TV_SCALAR ? w : v->kind == TV_SCALAR ? v : NULL;
        const NTvV1 *k = s == w ? v : w;
        if (s && tv_static(k)) {
            int64_t cp = tv_cp(x, k);
            if (cp < 0)
                return au_mismatch(x, c, p);
            return au_split(x, s->at, set_one(&x->sc, (uint32_t)cp, (uint32_t)cp), true, pats, vals, n, i + 1u, b, p, c);
        }
        sc_unread(&x->sc);
        return NULL;
    }
    if (v->kind == TV_SPAN) {
        /* a span against a literal text (the empty one is its nil) */
        int lit = au_literal(x, pat, v->op, v->nil);
        if (lit >= 0)
            return au_text(x, v, lit, true, pats, vals, n, i + 1u, b, p, c);
        sc_unread(&x->sc);   /* taken apart only at run time */
        return NULL;
    }
    if (v->kind == TV_DYN || v->kind == TV_HOLE) {
        sc_unread(&x->sc);
        return NULL;
    }
    int64_t num;
    if (is_int(pat, &num)) {
        if (v->kind == TV_INT)
            return v->n == num ? au_match(x, pats, vals, n, i + 1u, b, p, c) : au_mismatch(x, c, p);
        return au_mismatch(x, c, p);   /* a list, a (cp N) or an application is no integer */
    }
    const char *s = sym(pat);
    if (s) {
        long id = op_find(x->sc.ops, s, 0u);
        if (id >= 0 && id == x->sc.op_nil) {
            if (v->kind != TV_CUR)
                return au_mismatch(x, c, p);
            int at = v->at;
            if (p.end[at] == 1)
                return au_match(x, pats, vals, n, i + 1u, b, p, c);
            if (p.end[at] == 0)
                return au_mismatch(x, c, p);
            NTnV1 *t = tn_new(x, TN_END);
            if (!t)
                return NULL;
            NApathV1 pe = p, pm = p;
            pe.end[at] = 1;
            pm.end[at] = 0;
            t->at = at;
            t->yes = au_match(x, pats, vals, n, i + 1u, b, pe, c);
            t->no = au_mismatch(x, c, pm);
            return x->sc.failed ? NULL : t;
        }
        if (v->kind == TV_APP && v->nk == 0u && v->op == id)
            return au_match(x, pats, vals, n, i + 1u, b, p, c);
        return au_mismatch(x, c, p);
    }
    if (!pat || pat->kind != ATOM_EXPR || pat->expr.len == 0u || !sym(pat->expr.elems[0])) {
        sc_unread(&x->sc);
        return NULL;
    }
    size_t arity = pat->expr.len - 1u;
    long id = op_find(x->sc.ops, sym(pat->expr.elems[0]), arity);
    if (id >= 0 && id == x->sc.op_cons) {
        if (v->kind != TV_CUR)
            return au_mismatch(x, c, p);
        int at = v->at;
        if (p.end[at] == 1)
            return au_mismatch(x, c, p);
        if (p.end[at] == -1) {
            /* the input may end here: the end mismatches, a scalar goes on */
            NTnV1 *t = tn_new(x, TN_END);
            if (!t)
                return NULL;
            NApathV1 pe = p, pm = p;
            pe.end[at] = 1;
            pm.end[at] = 0;
            t->at = at;
            t->yes = au_mismatch(x, c, pe);
            t->no = au_match(x, pats, vals, n, i, b, pm, c);
            return x->sc.failed ? NULL : t;
        }
        if (p.succ[at] < 0) {
            /* read the scalar there, then match against it */
            if (p.used >= NAUTO_LOOK_V1) {
                sc_unread(&x->sc);   /* looking further than the lookahead */
                return NULL;
            }
            NTnV1 *t = tn_new(x, TN_READ);
            if (!t)
                return NULL;
            NApathV1 pr = p;
            pr.succ[at] = (signed char)pr.used++;
            t->at = at;
            t->var = pr.succ[at];
            t->yes = au_match(x, pats, vals, n, i, b, pr, c);
            return x->sc.failed ? NULL : t;
        }
        const Atom *subs[2] = {pat->expr.elems[1], pat->expr.elems[2]};
        NTvV1 *kids[2] = {tv_at(x, TV_SCALAR, at), tv_at(x, TV_CUR, p.succ[at])};
        const Atom **np;
        NTvV1 **nv;
        size_t nn;
        if (!kids[0] || !kids[1] || !au_expand(x, pats, vals, n, i, subs, kids, 2u, &np, &nv, &nn))
            return NULL;
        return au_match(x, np, nv, nn, i, b, p, c);
    }
    if (id >= 0 && id == x->sc.op_cp && v->kind == TV_SCALAR) {
        int64_t k;
        if (!is_int(pat->expr.elems[1], &k)) {
            sc_unread(&x->sc);
            return NULL;
        }
        if (k < 0 || k > NSCAN_MAX_SCALAR_V1)
            return au_mismatch(x, c, p);
        return au_split(x, v->at, set_one(&x->sc, (uint32_t)k, (uint32_t)k), true, pats, vals, n, i + 1u, b, p, c);
    }
    if (v->kind != TV_APP || v->op != id || v->nk != arity)
        return au_mismatch(x, c, p);
    const Atom **np;
    NTvV1 **nv;
    size_t nn;
    if (!au_expand(x, pats, vals, n, i, (const Atom *const *)&pat->expr.elems[1], v->kid, arity, &np, &nv, &nn))
        return NULL;
    return au_match(x, np, nv, nn, i, b, p, c);
}

/* Mode m from rule `from` on, its answer going to k. */
static NTnV1 *au_mode(NAutoCtxV1 *x, const DirectBindingModeV1 *m, size_t from, NTvV1 **in, NApathV1 p,
                      const NKontV1 *k) {
    if (x->sc.failed)
        return NULL;
    for (size_t idx = from; idx < x->sc.rules->len; idx++) {
        const DirectRuleV1 *rule = &x->sc.rules->items[idx];
        const Atom *h = rule->head;
        if (h->expr.len - 1u != m->arity || strcmp(sym(h->expr.elems[0]), m->relation) != 0)
            continue;
        const Atom *pats[64];
        NTvV1 *vals[64];
        size_t n = 0;
        for (size_t a = 0; a < m->arity && a < 64u; a++)
            if (m->input_ground[a])
                pats[n] = h->expr.elems[a + 1u], vals[n++] = in[a];
        NAcontV1 c = {rule, m, idx, 1u, in, k};
        NAcontV1 *cc = sc_alloc(&x->sc, sizeof *cc);
        if (!cc)
            return NULL;
        *cc = c;
        return au_match(x, pats, vals, n, 0u, NULL, p, cc);
    }
    /* no rule left: the caller's rule fails */
    if (!k)
        return tn_new(x, TN_FAIL);
    if (!k->rule) {
        /* a collected table: no answer here */
        return tn_new(x, TN_FAIL);
    }
    return au_mode(x, k->mode, k->idx + 1u, k->in, p, k->next);
}

static bool kont_has(const NKontV1 *k, const DirectBindingModeV1 *m) {
    for (; k; k = k->next)
        if (k->mode == m)
            return true;
    return false;
}

/* The outputs of rule c's head, then back to its caller. */
static NTnV1 *au_return(NAutoCtxV1 *x, const NAcontV1 *c, const NBindV1 *b, NApathV1 p) {
    const DirectBindingModeV1 *m = c->mode;
    size_t nout = mode_outputs(m);
    NTvV1 **outs = sc_alloc(&x->sc, (nout ? nout : 1u) * sizeof *outs);
    if (!outs)
        return NULL;
    for (size_t a = 0, o = 0; a < m->arity; a++)
        if (!m->input_ground[a] && !(outs[o++] = tv_term(x, c->rule->head->expr.elems[a + 1u], b, &p)))
            return NULL;
    const NKontV1 *k = c->k;
    if (!k) {
        NTnV1 *t = tn_new(x, TN_OUT);
        if (!t)
            return NULL;
        t->vals = outs;
        t->nvals = nout;
        return t;
    }
    if (!k->rule) {
        /* a table's answer: kept, its path with it */
        if (x->nresults == NAUTO_RESULTS_V1) {
            sc_unread(&x->sc);
            return NULL;
        }
        x->results[x->nresults++] = (NAresultV1){p, outs};
        return tn_new(x, TN_OUT);
    }
    /* the caller's premise takes the outputs, then its rule goes on */
    const Atom *call = k->rule->body->expr.elems[k->prem];
    const Atom *pats[64];
    size_t n = 0;
    for (size_t a = 0, o = 0; a < m->arity; a++)
        if (!m->input_ground[a]) {
            if (n == 64u)
                return (sc_unread(&x->sc), NULL);
            pats[n++] = call->expr.elems[a + 1u];
            (void)o;
        }
    NAcontV1 *cc = sc_alloc(&x->sc, sizeof *cc);
    if (!cc)
        return NULL;
    *cc = (NAcontV1){k->rule, k->mode, k->idx, k->prem + 1u, k->in, k->next};
    return au_match(x, pats, outs, n, 0u, k->b, p, cc);
}

/* A tail call of the root on the cursor at `at`: the root's outputs as
 * terms of the next round's (holes), written, and the loop. */
static NTnV1 *au_loop(NAutoCtxV1 *x, const NAcontV1 *c, const NBindV1 *b, const Atom *call, int at, NApathV1 p) {
    /* the call's outputs are the next round's */
    const NBindV1 *nb = b;
    size_t o = 0;
    const DirectBindingModeV1 *root = x->root;
    for (size_t a = 0; a < root->arity; a++) {
        if (root->input_ground[a])
            continue;
        const Atom *v = call->expr.elems[a + 1u];
        NTvV1 *hole = tv_new(x, TV_HOLE, 0u);
        if (!is_var(v) || !hole || bind_find(nb, cetta_gslt_source_variable_name_v1(v)))
            return (sc_unread(&x->sc), NULL);
        hole->n = (int64_t)o++;
        if (!(nb = bind_add(&x->sc, nb, cetta_gslt_source_variable_name_v1(v), (NSvV1 *)hole)))
            return NULL;
    }
    /* up through each rule to the root, every call its rule's last premise */
    const DirectRuleV1 *rule = c->rule;
    const DirectBindingModeV1 *m = c->mode;
    const NKontV1 *k = c->k;
    NTvV1 **outs = NULL;
    for (;;) {
        size_t nout = mode_outputs(m);
        outs = sc_alloc(&x->sc, (nout ? nout : 1u) * sizeof *outs);
        if (!outs)
            return NULL;
        for (size_t a = 0, q = 0; a < m->arity; a++)
            if (!m->input_ground[a] && !(outs[q++] = tv_term(x, rule->head->expr.elems[a + 1u], nb, &p)))
                return NULL;
        if (!k)
            break;
        if (!k->rule || !k->tail)
            return (sc_unread(&x->sc), NULL);
        const Atom *up = k->rule->body->expr.elems[k->prem];
        nb = k->b;
        for (size_t a = 0, q = 0; a < m->arity; a++) {
            if (m->input_ground[a])
                continue;
            const Atom *v = up->expr.elems[a + 1u];
            if (!is_var(v) || bind_find(nb, cetta_gslt_source_variable_name_v1(v)))
                return (sc_unread(&x->sc), NULL);
            if (!(nb = bind_add(&x->sc, nb, cetta_gslt_source_variable_name_v1(v), (NSvV1 *)outs[q++])))
                return NULL;
        }
        rule = k->rule;
        m = k->mode;
        k = k->next;
    }
    /* each of the next round's outputs goes to one place */
    size_t uses[64] = {0};
    size_t nr = x->root_outs;
    for (size_t i = 0; i < nr; i++) {
        /* count holes */
        NTvV1 *stack[256];
        size_t sp = 0;
        stack[sp++] = outs[i];
        while (sp) {
            NTvV1 *v = stack[--sp];
            if (v->kind == TV_HOLE && v->n < 64)
                uses[v->n]++;
            for (size_t j = 0; v->kind == TV_APP && j < v->nk && sp < 256u; j++)
                stack[sp++] = v->kid[j];
        }
    }
    for (size_t j = 0; j < nr; j++)
        if (uses[j] > 1u)
            return (sc_unread(&x->sc), NULL);
    NTnV1 *t = tn_new(x, TN_LOOP);
    if (!t)
        return NULL;
    t->at = at;
    t->vals = outs;
    t->nvals = nr;
    return t;
}

/* A class table on scalars already read: its answers, grouped by value,
 * each its caller's continuation once; NULL when the callee does more
 * than test one position (then it is unfolded with its caller). */
static NTnV1 *au_table(NAutoCtxV1 *x, const DirectBindingModeV1 *n, NTvV1 **in, NApathV1 p, const NKontV1 *k,
                       bool *taken) {
    *taken = false;
    NAresultV1 *saved = x->results;
    size_t saved_n = x->nresults;
    NAresultV1 *results = sc_alloc(&x->sc, NAUTO_RESULTS_V1 * sizeof *results);
    if (!results)
        return NULL;
    x->results = results;
    x->nresults = 0;
    /* the callers stay in view for recursion; answers stop here */
    NKontV1 collect = {NULL, n, 0u, 0u, NULL, in, false, k};
    int calls = x->calls;
    NTnV1 *t = au_mode(x, n, 0u, in, p, &collect);
    size_t count = x->nresults;
    x->results = saved;
    x->nresults = saved_n;
    if (x->sc.failed || !t || x->calls != calls) {
        /* not a table: unread, or it calls what it cannot unfold */
        x->sc.failed = false;
        return NULL;
    }
    /* every answer differs from p only in the scalar at one position */
    int at = -1;
    for (size_t r = 0; r < count; r++) {
        const NApathV1 *q = &results[r].p;
        if (q->used != p.used || memcmp(q->end, p.end, sizeof p.end) != 0 ||
            memcmp(q->succ, p.succ, sizeof p.succ) != 0 || q->ntexts != p.ntexts ||
            memcmp(q->texts, p.texts, sizeof p.texts) != 0)
            return NULL;
        for (int j = 0; j < NAUTO_LOOK_V1; j++) {
            if (set_equal(q->set[j], p.set[j]))
                continue;
            if (at >= 0 && at != j)
                return NULL;
            at = j;
        }
    }
    *taken = true;
    if (at < 0) {
        /* one answer everywhere, or none */
        if (count == 0)
            return au_mode(x, k->mode, k->idx + 1u, k->in, p, k->next);
        NAcontV1 c = {k->rule, k->mode, k->idx, k->prem, k->in, k->next};
        NAcontV1 *cc = sc_alloc(&x->sc, sizeof *cc);
        if (!cc)
            return NULL;
        *cc = c;
        /* hand the answer to the caller as au_return would */
        const Atom *call = k->rule->body->expr.elems[k->prem];
        const Atom *pats[64];
        size_t m = 0;
        for (size_t a = 0; a < n->arity && m < 64u; a++)
            if (!n->input_ground[a])
                pats[m++] = call->expr.elems[a + 1u];
        cc->prem = k->prem + 1u;
        return au_match(x, pats, results[0].outs, m, 0u, k->b, p, cc);
    }
    /* group the answers by value */
    NSetV1 *sets = sc_alloc(&x->sc, count * sizeof *sets);
    NTvV1 ***vals = sc_alloc(&x->sc, count * sizeof *vals);
    size_t groups = 0;
    if (!sets || !vals)
        return NULL;
    size_t nout = mode_outputs(n);
    for (size_t r = 0; r < count; r++) {
        size_t g = 0;
        for (; g < groups; g++) {
            bool same = true;
            for (size_t o = 0; o < nout && same; o++)
                same = tv_same(vals[g][o], results[r].outs[o]);
            if (same)
                break;
        }
        if (g == groups) {
            vals[groups] = results[r].outs;
            sets[groups++] = results[r].p.set[at];
        } else {
            sets[g] = set_union(&x->sc, sets[g], results[r].p.set[at]);
        }
    }
    NTnV1 *s = tn_new(x, TN_SET);
    NTnV1 **cases = sc_alloc(&x->sc, (groups ? groups : 1u) * sizeof *cases);
    if (!s || !cases)
        return NULL;
    s->at = at;
    s->ncase = groups;
    s->sets = sets;
    s->cases = cases;
    const Atom *call = k->rule->body->expr.elems[k->prem];
    const Atom *pats[64];
    size_t m = 0;
    for (size_t a = 0; a < n->arity && m < 64u; a++)
        if (!n->input_ground[a])
            pats[m++] = call->expr.elems[a + 1u];
    NSetV1 none = p.set[at];
    for (size_t g = 0; g < groups; g++) {
        NApathV1 pg = p;
        pg.set[at] = sets[g];
        none = set_minus(&x->sc, none, sets[g]);
        NAcontV1 *cc = sc_alloc(&x->sc, sizeof *cc);
        if (!cc)
            return NULL;
        *cc = (NAcontV1){k->rule, k->mode, k->idx, k->prem + 1u, k->in, k->next};
        cases[g] = au_match(x, pats, vals[g], m, 0u, k->b, pg, cc);
        if (x->sc.failed)
            return NULL;
    }
    NApathV1 pn = p;
    pn.set[at] = none;
    s->no = none.len ? au_mode(x, k->mode, k->idx + 1u, k->in, pn, k->next) : tn_new(x, TN_FAIL);
    return x->sc.failed ? NULL : s;
}

/* Rule c's body from c->prem on. */
static NTnV1 *au_body(NAutoCtxV1 *x, const NAcontV1 *c, const NBindV1 *b, NApathV1 p) {
    if (x->sc.failed)
        return NULL;
    const Atom *body = c->rule->body;
    size_t len = body && body->kind == ATOM_EXPR ? body->expr.len : 0u;
    if (c->prem >= len)
        return au_return(x, c, b, p);
    const Atom *prem = body->expr.elems[c->prem];
    bool last = c->prem + 1u == len;
    NAcontV1 next = *c;
    next.prem = c->prem + 1u;
    NAcontV1 *nc = sc_alloc(&x->sc, sizeof *nc);
    if (!nc)
        return NULL;
    *nc = next;
    const Atom *d = different_of(prem);
    if (d) {
        NTvV1 *u = tv_term(x, d->expr.elems[1], b, &p), *v = u ? tv_term(x, d->expr.elems[2], b, &p) : NULL;
        if (!v)
            return NULL;
        if (tv_same(u, v))
            return au_mismatch(x, c, p);
        if (tv_static(u) && tv_static(v))
            return au_body(x, nc, b, p);
        if (u->kind == TV_SPAN || v->kind == TV_SPAN) {
            NTvV1 *span = u->kind == TV_SPAN ? u : v;
            const Atom *other = u->kind == TV_SPAN ? d->expr.elems[2] : d->expr.elems[1];
            int lit = is_var(other) ? -1 : au_literal(x, other, span->op, span->nil);
            if (lit >= 0)
                return au_text(x, span, lit, false, NULL, NULL, 0u, 0u, b, p, nc);
            sc_unread(&x->sc);
            return NULL;
        }
        const NTvV1 *s = u->kind == TV_SCALAR ? u : v->kind == TV_SCALAR ? v : NULL;
        const NTvV1 *k = s == u ? v : u;
        if (s && tv_static(k)) {
            int64_t cp = tv_cp(x, k);
            if (cp < 0)
                return au_body(x, nc, b, p);
            /* equal: the rule fails; different: it goes on */
            return au_split(x, s->at, set_one(&x->sc, (uint32_t)cp, (uint32_t)cp), false, NULL, NULL, 0u, 0u, b,
                            p, nc);
        }
        sc_unread(&x->sc);
        return NULL;
    }
    const char *head = prem && prem->kind == ATOM_EXPR && prem->expr.len ? sym(prem->expr.elems[0]) : NULL;
    if (!head)
        return (sc_unread(&x->sc), NULL);
    size_t arity = prem->expr.len - 1u;
    uint8_t bits[64];
    NTvV1 **in = sc_alloc(&x->sc, (arity ? arity : 1u) * sizeof *in);
    if (arity > 64u || !in)
        return (sc_unread(&x->sc), NULL);
    bool dyn = false;
    for (size_t i = 0; i < arity; i++) {
        in[i] = NULL;
        bits[i] = sv_bound(prem->expr.elems[i + 1u], b) ? 1u : 0u;
        if (bits[i]) {
            if (!(in[i] = tv_term(x, prem->expr.elems[i + 1u], b, &p)))
                return NULL;
            dyn |= tv_has(in[i], TV_DYN) || tv_has(in[i], TV_HOLE);
        }
    }
    const DirectBindingModeV1 *n = mode_find(x->sc.modes, head, arity, bits);
    if (!n || !n->selected || !n->functional)
        return (sc_unread(&x->sc), NULL);
    /* a class run on the source list: scanned, its text a span */
    const NScanV1 *scan = dyn ? NULL : scan_of(x->scans, (long)(n - x->sc.modes->items));
    size_t scan_in = 0;
    for (size_t a = 0; scan && a < arity; a++)
        if (bits[a])
            scan_in = a;
    if (scan && in[scan_in]->kind == TV_CUR) {
        if (p.used >= NAUTO_LOOK_V1)
            return (sc_unread(&x->sc), NULL);
        NTnV1 *t = tn_new(x, TN_SCAN);
        NSetV1 cls = set_new(&x->sc, scan->range_len + 1u);
        if (!t)
            return NULL;
        for (size_t r = 0; r < scan->range_len; r++)
            set_push(&cls, scan->ranges[r][0], scan->ranges[r][1]);
        NApathV1 pe = p;
        int e = pe.used++;
        pe.end[e] = -1;
        pe.succ[e] = -1;
        pe.set[e] = set_minus(&x->sc, set_one(&x->sc, 0u, NSCAN_MAX_SCALAR_V1), cls);
        t->at = in[scan_in]->at;
        t->var = e;
        t->mode = scan->mode;
        NTvV1 *run = tv_new(x, TV_SPAN, 0u), *after = tv_at(x, TV_CUR, e);
        if (!run || !after)
            return NULL;
        run->at = in[scan_in]->at;
        run->n = e;
        run->op = scan->cons;
        run->nil = scan->nil;
        const Atom *pats[2];
        NTvV1 *outs[2];
        for (size_t a = 0, o = 0; a < arity && o < 2u; a++)
            if (!bits[a]) {
                pats[o] = prem->expr.elems[a + 1u];
                outs[o] = o == scan->run_out ? run : after;
                o++;
            }
        const Atom **hp = sc_alloc(&x->sc, 2u * sizeof *hp);
        NTvV1 **hv = sc_alloc(&x->sc, 2u * sizeof *hv);
        if (!hp || !hv)
            return NULL;
        memcpy(hp, pats, sizeof pats);
        memcpy(hv, outs, sizeof outs);
        t->yes = au_match(x, hp, hv, 2u, 0u, b, pe, nc);
        return x->sc.failed ? NULL : t;
    }
    /* the root again, on a later cursor, as the rule's last premise: a loop */
    if (n == x->root && last && !dyn) {
        int at = -1;
        bool rest_static = true;
        for (size_t a = 0; a < arity; a++) {
            if (!bits[a])
                continue;
            if (in[a]->kind == TV_CUR && at < 0)
                at = in[a]->at;
            else
                rest_static = false;
        }
        if (at > 0 && rest_static) {
            NTnV1 *t = au_loop(x, c, b, prem, at, p);
            if (t && !x->sc.failed)
                return t;
            /* not a loop (a rule on the way goes on after it): a call */
            if (x->nodes > NAUTO_NODES_V1)
                return NULL;
            x->sc.failed = false;
        }
    }
    /* unfolded in place: not recursive, not on a call's answer */
    if (!dyn && !kont_has(c->k, n) && n != c->mode && n != x->root) {
        unsigned depth = 0;
        for (const NKontV1 *q = c->k; q; q = q->next)
            depth++;
        if (depth < NAUTO_DEPTH_V1) {
            NKontV1 *k = sc_alloc(&x->sc, sizeof *k);
            if (!k)
                return NULL;
            *k = (NKontV1){c->rule, c->mode, c->idx, c->prem, b, c->in, last, c->k};
            /* a table reads no further: none of its inputs is a list */
            bool taken = false, list = false;
            for (size_t i = 0; i < arity; i++)
                list |= bits[i] && tv_has(in[i], TV_CUR);
            NTnV1 *t = list ? NULL : au_table(x, n, in, p, k, &taken);
            if (taken)
                return t;
            x->sc.failed = false;
            t = au_mode(x, n, 0u, in, p, k);
            if (t && !x->sc.failed)
                return t;
            /* not readable unfolded: called instead (the attempt's
             * nodes stay counted, so attempts are bounded) */
            if (x->nodes > NAUTO_NODES_V1)
                return NULL;
            x->sc.failed = false;
        }
    }
    /* a residual call: its outputs are variables of the code */
    for (size_t i = 0; i < arity; i++)
        if (bits[i] && tv_has(in[i], TV_HOLE))
            return (sc_unread(&x->sc), NULL);
    NTnV1 *t = tn_new(x, TN_CALL);
    if (!t)
        return NULL;
    t->mode = (long)(n - x->sc.modes->items);
    t->vals = in;
    t->nvals = arity;
    t->var = x->calls++;
    const Atom *pats[64];
    NTvV1 *outs[64];
    size_t m = 0;
    for (size_t a = 0; a < arity; a++) {
        if (bits[a])
            continue;
        NTvV1 *o = tv_new(x, TV_DYN, 0u);
        if (!o)
            return NULL;
        o->at = t->var;
        o->n = (int64_t)m;
        pats[m] = prem->expr.elems[a + 1u];
        outs[m++] = o;
    }
    for (size_t j = 0; j < m; j++)
        if (!is_var(pats[j]))
            return (sc_unread(&x->sc), NULL);   /* matched only at run time */
    NTvV1 **heap = sc_alloc(&x->sc, (m ? m : 1u) * sizeof *heap);
    const Atom **hp = sc_alloc(&x->sc, (m ? m : 1u) * sizeof *hp);
    if (!heap || !hp)
        return NULL;
    memcpy(heap, outs, m * sizeof *heap);
    memcpy(hp, pats, m * sizeof *hp);
    t->yes = au_match(x, hp, heap, m, 0u, b, p, nc);
    t->no = au_mismatch(x, c, p);
    return x->sc.failed ? NULL : t;
}

/* ── automata in C ── */

/* What writing an automaton's code needs besides its tree. */
typedef struct {
    const DirectBindingModesV1 *modes;
    const NScansV1 *scans;
    const NAlitV1 *lits;
    const char *name;
} NAemitV1;

static void au_indent(NBufV1 *o, unsigned d) {
    for (unsigned i = 0; i < d; i++)
        nb_puts(o, "    ");
}

/* A C expression building v (holes as NULL). */
static void au_value(NBufV1 *o, const NTvV1 *v, const NScansV1 *scans) {
    switch (v->kind) {
    case TV_CUR:
        nb_printf(o, "gn_cursor(cx, src, p%d)", v->at);
        return;
    case TV_SCALAR:
        nb_printf(o, "gn_scalar(cx, c%d)", v->at);
        return;
    case TV_INT:
        nb_printf(o, "gn_int(cx, %lld)", (long long)v->n);
        return;
    case TV_DYN:
        nb_printf(o, "x%d_%lld", v->at, (long long)v->n);
        return;
    case TV_HOLE:
        nb_puts(o, "NULL");
        return;
    case TV_SPAN:
        nb_printf(o, "gn_span(cx, src, p%d, p%lld, %ldu, %ldu)", v->at, (long long)v->n, v->op + NATIVE_FIRST_OP_V1,
                  v->nil + NATIVE_FIRST_OP_V1);
        return;
    case TV_APP:
        if (v->nk == 0u) {
            nb_printf(o, "gn_const(cx, %ldu)", v->op + NATIVE_FIRST_OP_V1);
            return;
        }
        if (v->nk == 2u && span_nil(scans, v->op) >= 0 && !tv_has(v, TV_HOLE))
            nb_printf(o, "gn_cons_cp(cx, %ldu", v->op + NATIVE_FIRST_OP_V1);
        else
            nb_printf(o, "gn_app%zu(cx, %ldu", v->nk, v->op + NATIVE_FIRST_OP_V1);
        for (size_t i = 0; i < v->nk; i++) {
            nb_puts(o, ", ");
            au_value(o, v->kid[i], scans);
        }
        nb_puts(o, ")");
        return;
    }
}

/* Statements building v into w<*temp>, a hole's place recorded as np<j>. */
static unsigned au_value_holes(NBufV1 *o, unsigned d, const NTvV1 *v, unsigned *temp, const NScansV1 *scans) {
    unsigned w = (*temp)++;
    unsigned kids[64];
    for (size_t i = 0; i < v->nk && i < 64u; i++)
        kids[i] = v->kid[i]->kind == TV_APP && tv_has(v->kid[i], TV_HOLE)
                      ? au_value_holes(o, d, v->kid[i], temp, scans) : UINT32_MAX;
    au_indent(o, d);
    nb_printf(o, "GnVal *w%u = gn_app%zu(cx, %ldu", w, v->nk, v->op + NATIVE_FIRST_OP_V1);
    for (size_t i = 0; i < v->nk; i++) {
        nb_puts(o, ", ");
        if (kids[i] != UINT32_MAX)
            nb_printf(o, "w%u", kids[i]);
        else
            au_value(o, v->kid[i], scans);
    }
    nb_puts(o, ");\n");
    for (size_t i = 0; i < v->nk; i++)
        if (v->kid[i]->kind == TV_HOLE) {
            au_indent(o, d);
            nb_printf(o, "np%lld = &w%u->kid[%zu];\n", (long long)v->kid[i]->n, w, i);
        }
    return w;
}

static void au_emit(NBufV1 *o, const NTnV1 *t, unsigned d, const NAemitV1 *e) {
    const DirectBindingModesV1 *modes = e->modes;
    const NScansV1 *scans = e->scans;
    const char *trace_name = e->name;
    switch (t->kind) {
    case TN_FAIL:
        au_indent(o, d);
        nb_printf(o, "GN_TRACE_FAIL(cx, \"%s\");\n", trace_name);
        au_indent(o, d);
        nb_puts(o, "return false;\n");
        return;
    case TN_OUT:
        for (size_t i = 0; i < t->nvals; i++) {
            au_indent(o, d);
            nb_printf(o, "*r%zu = ", i);
            au_value(o, t->vals[i], scans);
            nb_puts(o, ";\n");
        }
        au_indent(o, d);
        nb_puts(o, "GN_TRACE_RULE(cx, \"gslt:automaton\");\n");
        au_indent(o, d);
        nb_puts(o, "return true;\n");
        return;
    case TN_END:
        au_indent(o, d);
        nb_printf(o, "if (p%d >= len) {\n", t->at);
        au_emit(o, t->yes, d + 1u, e);
        au_indent(o, d);
        nb_puts(o, "} else {\n");
        au_emit(o, t->no, d + 1u, e);
        au_indent(o, d);
        nb_puts(o, "}\n");
        return;
    case TN_READ:
        au_indent(o, d);
        nb_printf(o, "p%d = p%d + gn_utf8_next(src, p%d, &c%d);\n", t->var, t->at, t->at, t->at);
        au_emit(o, t->yes, d, e);
        return;
    case TN_SET: {
        /* standard C: small ranges as case labels, wide ones tested in
         * the default before what no case takes */
        enum { WIDE = 64u };
        bool wide = false;
        au_indent(o, d);
        nb_printf(o, "switch (c%d) {\n", t->at);
        for (size_t g = 0; g < t->ncase; g++) {
            bool any = false;
            for (size_t r = 0; r < t->sets[g].len; r++) {
                uint32_t lo = t->sets[g].r[r][0], hi = t->sets[g].r[r][1];
                if (hi - lo >= WIDE) {
                    wide = true;
                    continue;
                }
                for (uint32_t c = lo;; c++) {
                    au_indent(o, d);
                    nb_printf(o, "case %uu:\n", c);
                    if (c == hi)
                        break;
                }
                any = true;
            }
            if (!any)
                continue;
            au_indent(o, d);
            nb_puts(o, "{\n");
            au_emit(o, t->cases[g], d + 1u, e);
            au_indent(o, d);
            nb_puts(o, "}\n");
        }
        au_indent(o, d);
        nb_puts(o, "default:\n");
        for (size_t g = 0; wide && g < t->ncase; g++) {
            bool first = true;
            for (size_t r = 0; r < t->sets[g].len; r++) {
                uint32_t lo = t->sets[g].r[r][0], hi = t->sets[g].r[r][1];
                if (hi - lo < WIDE)
                    continue;
                au_indent(o, d + 1u);
                nb_printf(o, "%s(c%d >= %uu && c%d <= %uu)\n", first ? "if (" : "    || ", t->at, lo, t->at, hi);
                first = false;
            }
            if (first)
                continue;
            au_indent(o, d + 1u);
            nb_puts(o, ") {\n");
            au_emit(o, t->cases[g], d + 2u, e);
            au_indent(o, d + 1u);
            nb_puts(o, "}\n");
        }
        au_indent(o, d + 1u);
        nb_puts(o, "{\n");
        au_emit(o, t->no, d + 2u, e);
        au_indent(o, d + 1u);
        nb_puts(o, "}\n");
        au_indent(o, d);
        nb_puts(o, "}\n");
        return;
    }
    case TN_CALL: {
        const DirectBindingModeV1 *m = &modes->items[t->mode];
        size_t outs = mode_outputs(m);
        au_indent(o, d);
        nb_puts(o, "{\n");
        if (outs) {
            au_indent(o, d + 1u);
            nb_puts(o, "GnVal");
            for (size_t k = 0; k < outs; k++)
                nb_printf(o, "%s *x%d_%zu = NULL", k ? "," : "", t->var, k);
            nb_puts(o, ";\n");
        }
        au_indent(o, d + 1u);
        nb_printf(o, "if (gn_f%ld(cx", t->mode);
        for (size_t a = 0; a < m->arity; a++)
            if (m->input_ground[a]) {
                nb_puts(o, ", ");
                au_value(o, t->vals[a], scans);
            }
        for (size_t k = 0; k < outs; k++)
            nb_printf(o, ", &x%d_%zu", t->var, k);
        nb_puts(o, ")) {\n");
        au_emit(o, t->yes, d + 2u, e);
        au_indent(o, d + 1u);
        nb_puts(o, "} else {\n");
        au_emit(o, t->no, d + 2u, e);
        au_indent(o, d + 1u);
        nb_puts(o, "}\n");
        au_indent(o, d);
        nb_puts(o, "}\n");
        return;
    }
    case TN_TEXT: {
        /* the chain of tests on this span: at most one literal matches */
        size_t n = 0;
        const NTnV1 *q = t;
        while (q->kind == TN_TEXT && q->at == t->at && q->var == t->var)
            n++, q = q->no;
        au_indent(o, d);
        nb_printf(o, "switch (p%d - p%d) {\n", t->var, t->at);
        bool *done = calloc(n ? n : 1u, sizeof *done);
        if (!done) {
            o->nomem = true;
            return;
        }
        size_t i = 0;
        for (const NTnV1 *a = t; i < n; a = a->no, i++) {
            if (done[i])
                continue;
            size_t len = e->lits[a->mode].len;
            au_indent(o, d);
            nb_printf(o, "case %zuu:\n", len);
            size_t j = i;
            for (const NTnV1 *b2 = a; j < n; b2 = b2->no, j++) {
                if (done[j] || e->lits[b2->mode].len != len)
                    continue;
                done[j] = true;
                au_indent(o, d + 1u);
                nb_printf(o, "if (__builtin_memcmp(src->bytes + p%d, \"", t->at);
                for (size_t k = 0; k < len; k++)
                    nb_printf(o, "\\x%02x", e->lits[b2->mode].bytes[k]);
                nb_printf(o, "\", %zuu) == 0) {\n", len);
                au_emit(o, b2->yes, d + 2u, e);
                au_indent(o, d + 1u);
                nb_puts(o, "}\n");
            }
            au_indent(o, d + 1u);
            nb_puts(o, "break;\n");
        }
        free(done);
        au_indent(o, d);
        nb_puts(o, "}\n");
        au_emit(o, q, d, e);
        return;
    }
    case TN_SCAN:
        au_indent(o, d);
        nb_printf(o, "p%d = p%d;\n", t->var, t->at);
        au_indent(o, d);
        nb_printf(o, "while (p%d < len) {\n", t->var);
        au_indent(o, d + 1u);
        nb_printf(o, "uint32_t gn_c;\n");
        au_indent(o, d + 1u);
        nb_printf(o, "size_t gn_w = gn_utf8_next(src, p%d, &gn_c);\n", t->var);
        au_indent(o, d + 1u);
        nb_printf(o, "if (!gn_class_has(&gn_k%ld, gn_c))\n", t->mode);
        au_indent(o, d + 2u);
        nb_puts(o, "break;\n");
        au_indent(o, d + 1u);
        nb_printf(o, "p%d += gn_w;\n", t->var);
        au_indent(o, d);
        nb_puts(o, "}\n");
        au_emit(o, t->yes, d, e);
        return;
    case TN_LOOP: {
        au_indent(o, d);
        nb_puts(o, "{\n");
        au_indent(o, d + 1u);
        nb_puts(o, "GnVal");
        for (size_t j = 0; j < t->nvals; j++)
            nb_printf(o, "%s **np%zu = &gn_sink", j ? "," : "", j);
        nb_puts(o, ";\n");
        unsigned temp = 0;
        for (size_t i = 0; i < t->nvals; i++) {
            const NTvV1 *v = t->vals[i];
            if (v->kind == TV_HOLE) {
                au_indent(o, d + 1u);
                nb_printf(o, "np%lld = r%zu;\n", (long long)v->n, i);
            } else if (tv_has(v, TV_HOLE)) {
                unsigned w = au_value_holes(o, d + 1u, v, &temp, scans);
                au_indent(o, d + 1u);
                nb_printf(o, "*r%zu = w%u;\n", i, w);
            } else {
                au_indent(o, d + 1u);
                nb_printf(o, "*r%zu = ", i);
                au_value(o, v, scans);
                nb_puts(o, ";\n");
            }
        }
        for (size_t j = 0; j < t->nvals; j++) {
            au_indent(o, d + 1u);
            nb_printf(o, "r%zu = np%zu;\n", j, j);
        }
        au_indent(o, d + 1u);
        nb_printf(o, "p0 = p%d;\n", t->at);
        au_indent(o, d + 1u);
        nb_puts(o, "goto gn_round;\n");
        au_indent(o, d);
        nb_puts(o, "}\n");
        return;
    }
    }
}

/* Whether the tree reads input and loops: worth a function of its own. */
static void au_counts(const NTnV1 *t, size_t *reads, size_t *loops, size_t *calls) {
    if (!t)
        return;
    *reads += t->kind == TN_READ;
    *loops += t->kind == TN_LOOP;
    *calls += t->kind == TN_CALL;
    au_counts(t->yes, reads, loops, calls);
    au_counts(t->no, reads, loops, calls);
    for (size_t g = 0; g < t->ncase; g++)
        au_counts(t->cases[g], reads, loops, calls);
}

/* A mode with an automaton enters it when its input is the source list. */
static void emit_automaton_entry(NBufV1 *o, const DirectBindingModeV1 *m, long mi) {
    size_t input = 0;
    for (size_t a = 0; a < m->arity; a++)
        if (m->input_ground[a])
            input = a;
    nb_printf(o, "    if (a%zu->op == GN_CURSOR)\n        return gn_u%ld(cx", input, mi);
    for (size_t a = 0, k = 0; a < m->arity; a++)
        if (m->input_ground[a])
            nb_printf(o, ", a%zu", a);
        else
            nb_printf(o, ", r%zu", k++);
    nb_puts(o, ");\n");
}

/* Mode mi as a token automaton, into o: gn_u<mi>, true when it is one. */
static bool emit_automaton(NBufV1 *o, const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
                           const NOpsV1 *ops, long op_cons, long op_nil, long op_cp, const NScansV1 *scans,
                           long mi) {
    const DirectBindingModeV1 *m = &modes->items[mi];
    size_t inputs = 0, input = 0;
    for (size_t a = 0; a < m->arity; a++)
        if (m->input_ground[a])
            inputs++, input = a;
    if (inputs != 1u || mode_outputs(m) == 0u || m->arity > 64u || scan_of(scans, mi))
        return false;
    NAutoCtxV1 x = {.sc = {.rules = rules, .modes = modes, .ops = ops, .op_cons = op_cons, .op_nil = op_nil,
                           .op_cp = op_cp},
                    .scans = scans, .root = m, .root_outs = mode_outputs(m)};
    NTvV1 **in = sc_alloc(&x.sc, m->arity * sizeof *in);
    x.lits = sc_alloc(&x.sc, NAUTO_LITS_V1 * sizeof *x.lits);
    bool ok = false;
    if (!in || !x.lits)
        goto done;
    memset(in, 0, m->arity * sizeof *in);
    in[input] = tv_at(&x, TV_CUR, 0);
    NTnV1 *t = in[input] ? au_mode(&x, m, 0u, in, apath_all(&x), NULL) : NULL;
    if (!t || x.sc.failed)
        goto done;
    size_t reads = 0, loops = 0, calls = 0;
    au_counts(t, &reads, &loops, &calls);
    if (!reads || !loops)
        goto done;
    char name[320], bits[80];
    mode_bits(m, bits);
    snprintf(name, sizeof name, "%s:%s", m->relation, bits);
    nb_printf(o, "/* %s as a token automaton: %zu reads, %zu loops, %zu calls of compiled modes */\n", name,
              reads, loops, calls);
    char fn[32];
    snprintf(fn, sizeof fn, "gn_u%ld", mi);
    nb_puts(o, "static ");
    signature(o, modes, m, fn);
    nb_puts(o, " {\n");
    nb_printf(o, "    const GnSourceV1 *src = a%zu->src;\n    size_t len = src->len;\n", input);
    nb_printf(o, "    size_t p0 = (size_t)a%zu->i", input);
    for (int k = 1; k < NAUTO_LOOK_V1; k++)
        nb_printf(o, ", p%d = 0", k);
    nb_puts(o, ";\n    uint32_t c0 = 0");
    for (int k = 1; k < NAUTO_LOOK_V1; k++)
        nb_printf(o, ", c%d = 0", k);
    nb_puts(o, ";\n    GnVal *gn_sink = NULL;\n    (void)len, (void)gn_sink");
    for (int k = 1; k < NAUTO_LOOK_V1; k++)
        nb_printf(o, ", (void)p%d", k);
    for (int k = 0; k < NAUTO_LOOK_V1; k++)
        nb_printf(o, ", (void)c%d", k);
    nb_puts(o, ";\n");
    nb_printf(o, "    GN_TRACE_CALL(cx, \"%s\", 1u, a%zu);\n", name, input);
    nb_puts(o, "gn_round:\n");
    NAemitV1 e = {modes, scans, x.lits, name};
    au_emit(o, t, 1u, &e);
    nb_puts(o, "}\n\n");
    ok = !o->nomem;
done:
    sc_free(&x.sc);
    return ok;
}

bool cetta_gslt_render_native_c_v1(
    const DirectRulesV1 *rules, const DirectBindingModesV1 *modes,
    const DirectOperatorsV1 *operators,
    const char *const *entry_modes, size_t entry_mode_count,
    const char *source_digest,
    uint8_t **out, size_t *out_len,
    char *error, size_t error_size) {
    NBufV1 o = {0};
    NOpsV1 ops = {0};
    NRuleV1 r = {.modes = modes, .ops = &ops, .out = &o, .error = error, .error_size = error_size, .group = -1};
    NEdgesV1 edges = {0};
    NScansV1 scans = {0};
    NLitsV1 lits = {0};
    NBufV1 front = {0};       /* what precedes the functions, the literal texts they use last */
    NBufV1 automata = {0};
    bool *automaton = NULL;
    long *group_of = NULL;
    bool ok = false;

    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (m->selected && !m->functional) {
            native_error(error, error_size, "C target: mode %s is not functional", m->relation);
            goto done;
        }
    }
    /* the input list's constructors first, so the runtime knows their ids */
    if (!op_add(&ops, "cons", 2u) || !op_add(&ops, "nil", 0u) || !op_add(&ops, "cp", 1u))
        goto nomem;
    for (size_t i = 0; operators && i < operators->len; i++)
        if (!is_relation(modes, operators->items[i].name, operators->items[i].arity) &&
            !op_add(&ops, operators->items[i].name, operators->items[i].arity))
            goto nomem;
    for (size_t i = 0; i < rules->len; i++) {
        const DirectRuleV1 *rule = &rules->items[i];
        const Atom *h = rule->head;
        for (size_t k = 1; k < h->expr.len; k++)
            if (!collect_ops(h->expr.elems[k], modes, &ops, false))
                goto nomem;
        const Atom *b = rule->body;
        for (size_t k = 1; b && b->kind == ATOM_EXPR && k < b->expr.len; k++) {
            const Atom *p = b->expr.elems[k];
            const char *ph = p && p->kind == ATOM_EXPR && p->expr.len > 0u ? sym(p->expr.elems[0]) : NULL;
            if (ph && (strcmp(ph, "gslt:different") == 0 ||
                       (strcmp(ph, "different") == 0 && p->expr.len == 3u))) {
                const Atom *d = strcmp(ph, "different") == 0 ? p : p->expr.len == 2u ? p->expr.elems[1] : NULL;
                for (size_t j = 1; d && d->kind == ATOM_EXPR && j < d->expr.len; j++)
                    if (!collect_ops(d->expr.elems[j], modes, &ops, false))
                        goto nomem;
                continue;
            }
            if (!collect_ops(p, modes, &ops, true))
                goto nomem;
        }
    }
    r.op_cons = op_find(&ops, "cons", 2u);
    r.op_nil = op_find(&ops, "nil", 0u);
    r.op_cp = op_find(&ops, "cp", 1u);
    /* class runs, read before any rule is compiled: their values may be
     * spans wherever their constructors are matched */
    if (!find_scans(rules, modes, &ops, r.op_cons, r.op_nil, r.op_cp, &scans))
        goto nomem;
    r.scans = &scans;
    for (size_t i = 0; scans.len && i < rules->len; i++)
        if (!collect_lits(&r, rules->items[i].head, &lits) || !collect_lits(&r, rules->items[i].body, &lits))
            goto nomem;
    r.lits = &lits;
    /* token automata: modes over the source list unfolded into one
     * function each, written before the modes that enter them */
    automaton = calloc(modes->len ? modes->len : 1u, sizeof *automaton);
    if (!automaton)
        goto nomem;
    for (size_t i = 0; i < modes->len; i++)
        if (modes->items[i].selected)
            automaton[i] = emit_automaton(&automata, rules, modes, &ops, r.op_cons, r.op_nil, r.op_cp, &scans,
                                          (long)i);
    if (automata.nomem)
        goto nomem;

    nb_printf(&o, "/* Generated by langdef-compile petta-direct --target c from the GSLT composition\n"
                  " * with source-composition-sha256 %s.  Do not edit. */\n"
                  "#include \"native/gslt_native_runtime_v1.h\"\n\n", source_digest);
    nb_puts(&o, "static const GnOpV1 gn_ops[] = {\n");
    for (size_t i = 0; i < ops.len; i++) {
        nb_puts(&o, "    {\"");
        nb_c_string(&o, ops.items[i].name);
        nb_printf(&o, "\", %zuu},\n", ops.items[i].arity);
    }
    nb_puts(&o, "};\n\n");
    nb_printf(&o, "const GnProgramV1 %s_program = {gn_ops, %zuu, %ldu, %ldu, %ldu};\n\n", g_native_prefix,
              ops.len, (long)NATIVE_FIRST_OP_V1, (long)NATIVE_FIRST_OP_V1 + 1, (long)NATIVE_FIRST_OP_V1 + 2);
    /* the tail calls each rule could jump with, from a first pass whose
     * text is dropped */
    NBufV1 probe = {0};
    r.out = &probe;
    r.group = -1;
    for (size_t i = 0; i < modes->len && !r.failed; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        unsigned label = 0;
        if (m->selected)
            emit_mode_rules(&r, rules, m, (long)i, &label, &edges);
    }
    free(probe.bytes);
    r.out = &o;
    if (r.failed)
        goto done;
    group_of = malloc((modes->len ? modes->len : 1u) * sizeof *group_of);
    long groups = group_of && !edges.nomem ? tail_groups(&edges, modes->len, group_of) : -1;
    if (groups < 0)
        goto nomem;
    r.group_of = group_of;

    char name[320], bits[80];
    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (!m->selected)
            continue;
        mode_bits(m, bits);
        snprintf(name, sizeof name, "gn_f%zu", i);
        nb_puts(&o, "static ");
        signature(&o, modes, m, name);
        nb_puts(&o, "; /* ");
        nb_comment(&o, m->relation);
        nb_printf(&o, ":%s */\n", bits);
    }
    /* a tail group's modes share one function: inputs by argument position,
     * output places by output position, as wide as its widest mode */
    size_t *group_in = calloc((size_t)groups + 1u, sizeof *group_in);
    size_t *group_out = calloc((size_t)groups + 1u, sizeof *group_out);
    if (!group_in || !group_out) {
        free(group_in);
        free(group_out);
        goto nomem;
    }
    for (size_t i = 0; i < modes->len; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (group_of[i] < 0)
            continue;
        size_t g = (size_t)group_of[i];
        for (size_t a = 0; a < m->arity; a++)
            if (m->input_ground[a] && a + 1u > group_in[g])
                group_in[g] = a + 1u;
        if (mode_outputs(m) > group_out[g])
            group_out[g] = mode_outputs(m);
    }
    for (long g = 0; g < groups; g++) {
        nb_printf(&o, "static bool gn_g%ld(GnCtx *cx, unsigned entry", g);
        for (size_t a = 0; a < group_in[g]; a++)
            nb_printf(&o, ", GnVal *a%zu", a);
        for (size_t k = 0; k < group_out[g]; k++)
            nb_printf(&o, ", GnVal **r%zu", k);
        nb_puts(&o, ");\n");
    }
    nb_puts(&o, "\n");
    front = o;
    o = (NBufV1){0};
    for (size_t i = 0; i < scans.len; i++)
        emit_scan_class(&o, &scans.items[i]);
    if (automata.len)
        nb_put(&o, (const char *)automata.bytes, automata.len);
    for (size_t i = 0; i < modes->len && !r.failed; i++) {
        const DirectBindingModeV1 *m = &modes->items[i];
        if (!m->selected)
            continue;
        mode_bits(m, bits);
        snprintf(name, sizeof name, "gn_f%zu", i);
        nb_puts(&o, "/* ");
        nb_comment(&o, m->relation);
        /* a group's modes may be entered only by jumps: their entry points
         * are inline, so one never called is no warning */
        const NScanV1 *scan = scan_of(&scans, (long)i);
        nb_printf(&o, ":%s%s */\n", bits, scan ? " (class run)" : "");
        nb_printf(&o, "static %s", group_of[i] >= 0 ? "inline " : "");
        signature(&o, modes, m, name);
        if (group_of[i] >= 0) {
            size_t g = (size_t)group_of[i];
            unsigned entry = 0;
            for (size_t j = 0; j < i; j++)
                entry += group_of[j] == group_of[i] ? 1u : 0u;
            nb_puts(&o, " {\n");
            if (scan)
                emit_scan(&o, scan, m);
            if (automaton[i])
                emit_automaton_entry(&o, m, (long)i);
            nb_printf(&o, "    return gn_g%zu(cx, %uu", g, entry);
            for (size_t a = 0; a < group_in[g]; a++)
                nb_printf(&o, a < m->arity && m->input_ground[a] ? ", a%zu" : ", NULL", a);
            for (size_t k = 0; k < group_out[g]; k++)
                nb_printf(&o, k < mode_outputs(m) ? ", r%zu" : ", NULL", k);
            nb_puts(&o, ");\n}\n\n");
            continue;
        }
        nb_puts(&o, " {\n    (void)cx;\n");
        for (size_t a = 0; a < m->arity; a++)
            if (m->input_ground[a])
                nb_printf(&o, "    (void)a%zu;\n", a);
        if (scan)
            emit_scan(&o, scan, m);
        if (automaton[i])
            emit_automaton_entry(&o, m, (long)i);
        trace_call(&o, m);
        unsigned label = 0;
        NBufV1 section = {0};
        r.group = -1;
        r.out = &section;
        r.memo_next = 0;
        emit_mode_rules(&r, rules, m, (long)i, &label, NULL);
        r.out = &o;
        memo_declare(&o, 0u, r.memo_next);
        if (section.len)
            nb_put(&o, (const char *)section.bytes, section.len);
        o.nomem |= section.nomem;
        free(section.bytes);
        nb_puts(&o, "}\n\n");
    }
    for (long g = 0; g < groups && !r.failed; g++) {
        NBufV1 sections = {0};
        unsigned label = 0;
        r.group = g;
        r.memo_next = 0;
        for (size_t i = 0; i < modes->len && !r.failed; i++) {
            const DirectBindingModeV1 *m = &modes->items[i];
            if (group_of[i] != g)
                continue;
            mode_bits(m, bits);
            NBufV1 section = {0};
            r.out = &section;
            emit_mode_rules(&r, rules, m, (long)i, &label, NULL);
            nb_printf(&sections, "gm%zu: /* ", i);
            nb_comment(&sections, m->relation);
            nb_printf(&sections, ":%s */\n", bits);
            memo_resets(&sections, r.memo_base, r.memo_next);
            trace_call(&sections, m);
            if (section.len)
                nb_put(&sections, (const char *)section.bytes, section.len);
            sections.nomem |= section.nomem;
            free(section.bytes);
        }
        r.out = &o;
        nb_printf(&o, "/* tail group %ld */\nstatic bool gn_g%ld(GnCtx *cx, unsigned entry", g, g);
        for (size_t a = 0; a < group_in[g]; a++)
            nb_printf(&o, ", GnVal *a%zu", a);
        for (size_t k = 0; k < group_out[g]; k++)
            nb_printf(&o, ", GnVal **r%zu", k);
        nb_puts(&o, ") {\n    GnVal *gn_sink = NULL;\n    (void)cx;\n    (void)gn_sink;\n");
        for (size_t a = 0; a < group_in[g]; a++)
            nb_printf(&o, "    (void)a%zu;\n", a);
        for (size_t k = 0; k < group_out[g]; k++)
            nb_printf(&o, "    (void)r%zu;\n", k);
        memo_declare(&o, 0u, r.memo_next);
        nb_puts(&o, "    switch (entry) {\n");
        unsigned entry = 0;
        for (size_t i = 0; i < modes->len; i++)
            if (group_of[i] == g)
                nb_printf(&o, "    case %uu: goto gm%zu;\n", entry++, i);
        nb_puts(&o, "    default: return false;\n    }\n");
        if (sections.len)
            nb_put(&o, (const char *)sections.bytes, sections.len);
        o.nomem |= sections.nomem;
        free(sections.bytes);
        nb_puts(&o, "}\n\n");
    }
    free(group_in);
    free(group_out);
    if (r.failed)
        goto done;
    nb_printf(&o, "/* %ld tail groups, %u jumps */\n\n", groups, r.jumps);
    NBufV1 table = {0};
    size_t entries = 0;
    for (size_t e = 0; e < entry_mode_count; e++) {
        const char *colon = strrchr(entry_modes[e], ':');
        if (!colon)
            continue;
        size_t rl = (size_t)(colon - entry_modes[e]);
        size_t arity = strlen(colon + 1);
        uint8_t eb[64];
        if (arity > sizeof eb)
            continue;
        for (size_t i = 0; i < arity; i++)
            eb[i] = colon[1 + i] == '1';
        char relation[256];
        if (rl >= sizeof relation)
            continue;
        memcpy(relation, entry_modes[e], rl);
        relation[rl] = 0;
        const DirectBindingModeV1 *m = mode_find(modes, relation, arity, eb);
        if (!m || !m->selected) {
            native_error(error, error_size, "C target: entry mode %s is not selected", entry_modes[e]);
            goto done;
        }
        for (size_t i = 0; i < rl; i++) {
            char ch = entry_modes[e][i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_')) {
                native_error(error, error_size, "C target: entry relation %.*s is not a C identifier", (int)rl,
                             entry_modes[e]);
                goto done;
            }
        }
        snprintf(name, sizeof name, "%s_entry_%.*s_%s", g_native_prefix, (int)rl, entry_modes[e], colon + 1);
        signature(&o, modes, m, name);
        nb_printf(&o, " {\n    return gn_f%ld(cx", mode_number(modes, m));
        for (size_t i = 0, k = 0; i < m->arity; i++)
            if (m->input_ground[i])
                nb_printf(&o, ", a%zu", i);
            else
                nb_printf(&o, ", r%zu", k++);
        nb_puts(&o, ");\n}\n\n");
        /* the uniform calling form: inputs and outputs in arrays */
        size_t ins = 0, outs = 0;
        nb_printf(&o, "static bool gn_t%zu(GnCtx *cx, GnVal **in, GnVal **out) {\n    (void)in;\n    (void)out;\n"
                      "    return %s(cx", entries, name);
        for (size_t i = 0; i < m->arity; i++)
            if (m->input_ground[i])
                nb_printf(&o, ", in[%zu]", ins++);
            else
                nb_printf(&o, ", &out[%zu]", outs++);
        nb_puts(&o, ");\n}\n\n");
        nb_puts(&table, "    {\"");
        nb_c_string(&table, entry_modes[e]);
        nb_printf(&table, "\", %zuu, %zuu, gn_t%zu},\n", ins, outs, entries);
        entries++;
    }
    nb_printf(&o, "const GnEntryV1 %s_entries[] = {\n", g_native_prefix);
    if (table.len)
        nb_put(&o, (const char *)table.bytes, table.len);
    else
        nb_puts(&o, "    {NULL, 0u, 0u, NULL},\n");
    nb_printf(&o, "};\nconst uint32_t %s_entry_count = %zuu;\n", g_native_prefix, entries);
    free(table.bytes);
    emit_lits(&front, &lits);
    nb_put(&front, (const char *)o.bytes, o.len);
    front.nomem |= o.nomem;
    free(o.bytes);
    o = front;
    front = (NBufV1){0};
    if (o.nomem)
        goto nomem;
    *out = o.bytes;
    *out_len = o.len;
    o.bytes = NULL;
    ok = true;
    goto done;
nomem:
    native_error(error, error_size, "C target: out of memory");
done:
    free(o.bytes);
    free(front.bytes);
    free(edges.items);
    scans_free(&scans);
    lits_free(&lits);
    free(automata.bytes);
    free(automaton);
    free(group_of);
    free(ops.items);
    free(r.vars.names);
    free(r.vars.bound);
    free(r.vars.path);
    memo_reset(&r);
    free(r.memo_keys);
    return ok;
}
