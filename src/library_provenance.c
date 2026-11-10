#include "library_provenance.h"
#include "library.h"
#include "petta_semantics.h"
#include "symbol.h"
#include "../native/provenance/prov_kernel_v1.h"

#include <stdlib.h>
#include <string.h>

/* Ground terms numbered in order of first appearance, found by an
 * open-addressed index of their hashes (slot: number + 1, 0 free). */
typedef struct {
    Atom **items;
    uint32_t len, cap;
    uint32_t *slots;
    uint32_t slot_cap;
} ProvTable;

static void prov_table_free(ProvTable *t) {
    free(t->items);
    free(t->slots);
    memset(t, 0, sizeof(*t));
}

static bool prov_table_grow(ProvTable *t) {
    uint32_t cap = t->slot_cap ? t->slot_cap * 2u : 64u;
    uint32_t *slots = calloc(cap, sizeof(*slots));
    if (!slots)
        return false;
    for (uint32_t i = 0u; i < t->len; i++) {
        uint32_t at = atom_hash(t->items[i]) & (cap - 1u);
        while (slots[at])
            at = (at + 1u) & (cap - 1u);
        slots[at] = i + 1u;
    }
    free(t->slots);
    t->slots = slots;
    t->slot_cap = cap;
    return true;
}

/* the number of a, adding it when create; UINT32_MAX when absent or on OOM */
static uint32_t prov_intern(ProvTable *t, Atom *a, bool create) {
    if ((t->len + 1u) * 2u > t->slot_cap && !prov_table_grow(t))
        return UINT32_MAX;
    uint32_t at = atom_hash(a) & (t->slot_cap - 1u);
    for (; t->slots[at]; at = (at + 1u) & (t->slot_cap - 1u))
        if (atom_eq(t->items[t->slots[at] - 1u], a))
            return t->slots[at] - 1u;
    if (!create)
        return UINT32_MAX;
    if (t->len == t->cap) {
        uint32_t cap = t->cap ? t->cap * 2u : 16u;
        Atom **items = realloc(t->items, sizeof(*items) * cap);
        if (!items)
            return UINT32_MAX;
        t->items = items;
        t->cap = cap;
    }
    t->items[t->len] = a;
    t->slots[at] = t->len + 1u;
    return t->len++;
}

static bool prov_is_expr(const Atom *a, const char *head, uint32_t len) {
    if (!a || a->kind != ATOM_EXPR || a->expr.len != len)
        return false;
    return atom_is_symbol(a->expr.elems[0], head);
}

static bool prov_int(const Atom *a, int64_t lo, int64_t hi, int64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT ||
        a->ground.ival < lo || a->ground.ival > hi)
        return false;
    *out = a->ground.ival;
    return true;
}

static bool prov_ground(const Atom *a) {
    return a && a->kind != ATOM_VAR && !atom_has_vars(a);
}

typedef struct {
    Atom **supports;
    uint32_t len, cap;
} ProvFamily;

typedef struct {
    Arena *arena;
    ProvTable *labels;       /* numbered in the standard order */
    ProvFamily *fams;        /* per goal group */
    bool oom;
} ProvCollect;

static bool prov_collect(void *ctx, uint32_t group, const uint32_t *labels, uint32_t n) {
    ProvCollect *col = ctx;
    ProvFamily *c = &col->fams[group];
    Atom **items = malloc(sizeof(*items) * (n ? n : 1u));
    if (!items) {
        col->oom = true;
        return false;
    }
    for (uint32_t i = 0u; i < n; i++)
        items[i] = col->labels->items[labels[i]];
    Atom *support = atom_expr(col->arena, items, n);
    free(items);
    if (!support) {
        col->oom = true;
        return false;
    }
    if (c->len == c->cap) {
        uint32_t cap = c->cap ? c->cap * 2u : 64u;
        Atom **s = realloc(c->supports, sizeof(*s) * cap);
        if (!s) {
            col->oom = true;
            return false;
        }
        c->supports = s;
        c->cap = cap;
    }
    c->supports[c->len++] = support;
    return true;
}

/* a predicate is its name and its arity, as in Prolog: (NAME ARITY) */
static uint32_t prov_pred(Arena *arena, ProvTable *preds, Atom *name, uint32_t arity, bool create) {
    Atom *key = atom_expr2(arena, name, atom_int(arena, (int64_t)arity));
    return key ? prov_intern(preds, key, create) : UINT32_MAX;
}

/* a literal (lit PRED V ...) of variable indices below 32 */
static bool prov_read_lit(Arena *arena, ProvTable *preds, Atom *l, ProvLit *out, uint32_t *nvars) {
    if (!l || l->kind != ATOM_EXPR || l->expr.len < 3u ||
        l->expr.len - 2u > PROV_MAX_ARITY || !prov_is_expr(l, "lit", l->expr.len) ||
        !prov_ground(l->expr.elems[1]))
        return false;
    out->arity = (uint32_t)l->expr.len - 2u;
    out->pred = prov_pred(arena, preds, l->expr.elems[1], out->arity, true);
    for (uint32_t k = 0u; k < out->arity; k++) {
        int64_t v;
        if (!prov_int(l->expr.elems[2u + k], 0, 31, &v))
            return false;
        out->args[k] = (uint32_t)v;
        if ((uint32_t)v + 1u > *nvars)
            *nvars = (uint32_t)v + 1u;
    }
    return out->pred != UINT32_MAX;
}

/* each: GOALS is a list of goal groups, and the answer one family per group */
static Atom *prov_supports_value(Arena *arena, Atom **args, bool each, const char **error) {
    Atom *facts = args[0], *rules = args[1], *goals = args[2], *bound = args[3];
    int64_t K = 0;
    bool reuse;
    if (prov_is_expr(bound, "steps", 2u) && prov_int(bound->expr.elems[1], 0, 1000000, &K))
        reuse = false;
    else if (prov_is_expr(bound, "labels", 2u) && prov_int(bound->expr.elems[1], 0, 1000000, &K))
        reuse = true;
    else {
        *error = "expected the bound (steps K) or (labels K), K a natural number";
        return NULL;
    }
    if (facts->kind != ATOM_EXPR || rules->kind != ATOM_EXPR || goals->kind != ATOM_EXPR) {
        *error = "expected the facts, the rules and the goals as expressions";
        return NULL;
    }
    uint32_t ngroups = each ? (uint32_t)goals->expr.len : 1u, total = 0u;
    if (each) {
        for (uint32_t i = 0u; i < goals->expr.len; i++) {
            if (goals->expr.elems[i]->kind != ATOM_EXPR) {
                *error = "expected each goal group as an expression of goals";
                return NULL;
            }
            total += (uint32_t)goals->expr.elems[i]->expr.len;
        }
    } else {
        total = (uint32_t)goals->expr.len;
    }
    ProvTable seen = {0}, labels = {0}, nodes = {0}, preds = {0};
    ProvProgram *P = prov_new();
    ProvLit *gs = malloc(sizeof(*gs) * (total ? total : 1u));
    uint32_t *gg = malloc(sizeof(*gg) * (total ? total : 1u));
    ProvCollect col = {.arena = arena, .labels = &labels,
                       .fams = calloc(ngroups ? ngroups : 1u, sizeof(ProvFamily))};
    Atom *value = NULL;
    if (!P || !gs || !gg || !col.fams) {
        *error = "out of memory";
        goto done;
    }
    /* the labels, numbered in the standard order, so that supports come out
     * sorted */
    for (uint32_t i = 0u; i < rules->expr.len; i++) {
        Atom *r = rules->expr.elems[i];
        bool from = prov_is_expr(r, "rule-from", 4u);
        if ((!from && !prov_is_expr(r, "rule", 4u)) || r->expr.elems[3]->kind != ATOM_EXPR) {
            *error = "expected each rule as (rule LABEL (lit HEAD V ...) ((lit PRED V ...) ...)) "
                     "or (rule-from K (lit HEAD V ...) (...))";
            goto done;
        }
        if (from)
            continue;
        if (!prov_ground(r->expr.elems[1])) {
            *error = "a rule's label must be ground";
            goto done;
        }
        if (prov_intern(&seen, r->expr.elems[1], true) == UINT32_MAX) {
            *error = "out of memory";
            goto done;
        }
    }
    for (uint32_t i = 0u; i < facts->expr.len; i++) {
        Atom *f = facts->expr.elems[i];
        if (f->kind == ATOM_EXPR && f->expr.len >= 4u && prov_is_expr(f, "labelled", f->expr.len)) {
            if (!prov_ground(f->expr.elems[1])) {
                *error = "a fact's label must be ground";
                goto done;
            }
            if (prov_intern(&seen, f->expr.elems[1], true) == UINT32_MAX) {
                *error = "out of memory";
                goto done;
            }
        }
    }
    if (seen.len) {
        Atom *all = atom_expr(arena, seen.items, seen.len);
        bool type_error = false;
        Atom *sorted = all ? petta_semantics_sort_unique_value(arena, all, &type_error) : NULL;
        if (!sorted || sorted->kind != ATOM_EXPR || sorted->expr.len != seen.len) {
            *error = type_error ? "the labels could not be ordered" : "out of memory";
            goto done;
        }
        for (uint32_t i = 0u; i < sorted->expr.len; i++)
            if (prov_intern(&labels, sorted->expr.elems[i], true) != i) {
                *error = "out of memory";
                goto done;
            }
    }
    for (uint32_t i = 0u; i < facts->expr.len; i++) {
        Atom *f = facts->expr.elems[i];
        /* (fact PRED A ...) or (labelled LABEL PRED A ...) */
        uint32_t off = f->kind == ATOM_EXPR && f->expr.len >= 4u &&
                       prov_is_expr(f, "labelled", f->expr.len) ? 1u : 0u;
        if (f->kind != ATOM_EXPR || f->expr.len < 3u + off ||
            f->expr.len - 2u - off > PROV_MAX_ARITY ||
            (!off && !prov_is_expr(f, "fact", f->expr.len)) || !prov_ground(f)) {
            *error = "expected each fact as a ground (fact PRED A ...) or (labelled LABEL PRED A ...), "
                     "at most 8 arguments";
            goto done;
        }
        uint32_t arity = (uint32_t)f->expr.len - 2u - off;
        uint32_t p = prov_pred(arena, &preds, f->expr.elems[1u + off], arity, true);
        uint32_t a[PROV_MAX_ARITY];
        bool ok = p != UINT32_MAX;
        for (uint32_t k = 0u; ok && k < arity; k++)
            ok = (a[k] = prov_intern(&nodes, f->expr.elems[k + 2u + off], true)) != UINT32_MAX;
        if (ok && off) {
            uint32_t label = prov_intern(&labels, f->expr.elems[1], false);
            ok = label != UINT32_MAX && prov_add_labelled_fact(P, label, p, arity, a);
        } else if (ok) {
            ok = prov_add_fact(P, p, arity, a);
        }
        if (!ok) {
            *error = "out of memory";
            goto done;
        }
    }
    for (uint32_t i = 0u; i < rules->expr.len; i++) {
        Atom *r = rules->expr.elems[i], *lits = r->expr.elems[3];
        ProvLit head, ls[32];
        uint32_t nvars = 0u;
        if (lits->expr.len > 32u || !prov_read_lit(arena, &preds, r->expr.elems[2], &head, &nvars)) {
            *error = "expected each rule as (rule LABEL (lit HEAD V ...) ((lit PRED V ...) ...)), "
                     "at most 32 literals of at most 8 variable indices below 32";
            goto done;
        }
        for (uint32_t k = 0u; k < lits->expr.len; k++)
            if (!prov_read_lit(arena, &preds, lits->expr.elems[k], &ls[k], &nvars)) {
                *error = "expected each body literal as (lit PRED V ...), variable indices below 32";
                goto done;
            }
        if (prov_is_expr(r, "rule-from", 4u)) {
            int64_t k;
            if (!prov_int(r->expr.elems[1], 0, (int64_t)lits->expr.len - 1, &k)) {
                *error = "expected (rule-from K ...), K the index of one of its body literals";
                goto done;
            }
            if (!prov_add_rule_labelled_by(P, (uint32_t)k, &head, nvars, (uint32_t)lits->expr.len, ls)) {
                *error = "out of memory";
                goto done;
            }
            continue;
        }
        uint32_t label = prov_intern(&labels, r->expr.elems[1], false);
        if (label == UINT32_MAX ||
            !prov_add_rule(P, label, &head, nvars, (uint32_t)lits->expr.len, ls)) {
            *error = "out of memory";
            goto done;
        }
    }
    uint32_t ngoals = 0u;
    for (uint32_t gi = 0u; gi < total; gi++) {
        uint32_t group = 0u, at = gi;
        if (each)
            while (at >= goals->expr.elems[group]->expr.len)
                at -= (uint32_t)goals->expr.elems[group++]->expr.len;
        Atom *g = each ? goals->expr.elems[group]->expr.elems[at] : goals->expr.elems[gi];
        if (g->kind != ATOM_EXPR || g->expr.len < 3u || g->expr.len - 2u > PROV_MAX_ARITY ||
            !prov_is_expr(g, "goal", g->expr.len) || !prov_ground(g->expr.elems[1])) {
            *error = "expected each goal as (goal PRED A ...), each A ground or a variable";
            goto done;
        }
        ProvLit *gl = &gs[ngoals];
        gl->arity = (uint32_t)g->expr.len - 2u;
        /* a predicate or node that appears nowhere else has no derivations */
        gl->pred = prov_pred(arena, &preds, g->expr.elems[1], gl->arity, false);
        bool known = gl->pred != UINT32_MAX;
        for (uint32_t k = 0u; k < gl->arity; k++) {
            Atom *a = g->expr.elems[k + 2u];
            if (a->kind == ATOM_VAR) {
                gl->args[k] = PROV_ANY;
            } else if (!prov_ground(a)) {
                *error = "expected each goal as (goal PRED A ...), each A ground or a variable";
                goto done;
            } else {
                gl->args[k] = prov_intern(&nodes, a, false);
                known &= gl->args[k] != UINT32_MAX;
            }
        }
        if (known)
            gg[ngoals++] = group;
    }
    ProvStatus st = reuse
        ? prov_supports_reuse_groups(P, gs, gg, ngoals, ngroups, (uint32_t)K, prov_collect, &col)
        : prov_supports_graded_groups(P, gs, gg, ngoals, ngroups, (uint32_t)K, prov_collect, &col);
    if (st == PROV_UNSAFE_RULE) {
        *error = "a rule leaves a head variable unbound by its body";
        goto done;
    }
    if (st == PROV_MIXED_PRED) {
        *error = "a predicate both heads a rule and has facts";
        goto done;
    }
    if (st == PROV_ARITY) {
        *error = "a predicate is used with two arities";
        goto done;
    }
    if (st == PROV_BAD_LABEL) {
        *error = "a rule-from literal is derived or meets a fact without a label, "
                 "or a fact has two labels";
        goto done;
    }
    if (st != PROV_OK || col.oom) {
        *error = st == PROV_LIMIT ? "an arity or the bound is beyond the kernel's limits" : "out of memory";
        goto done;
    }
    Atom **families = malloc(sizeof(*families) * (ngroups ? ngroups : 1u));
    bool ok = families != NULL;
    for (uint32_t g = 0u; ok && g < ngroups; g++) {
        Atom *family = atom_expr(arena, col.fams[g].supports, col.fams[g].len);
        bool type_error = false;
        families[g] = family ? petta_semantics_sort_unique_value(arena, family, &type_error) : NULL;
        ok = families[g] != NULL;
    }
    value = !ok ? NULL : each ? atom_expr(arena, families, ngroups) : families[0];
    free(families);
    if (!value)
        *error = "out of memory";
done:
    for (uint32_t g = 0u; col.fams && g < ngroups; g++)
        free(col.fams[g].supports);
    free(col.fams);
    free(gg);
    free(gs);
    prov_free(P);
    prov_table_free(&seen);
    prov_table_free(&labels);
    prov_table_free(&nodes);
    prov_table_free(&preds);
    return value;
}

bool cetta_provenance_call(struct CettaLibraryContext *context, Arena *arena,
                           Atom *head, Atom **args, uint32_t nargs,
                           CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    bool each = atom_symbol_current_id(head) == g_builtin_syms.lib_provenance_supports_each;
    if (!each && atom_symbol_current_id(head) != g_builtin_syms.lib_provenance_supports)
        return false;
    const char *error = NULL;
    Atom *value = nargs == 4u ? prov_supports_value(arena, args, each, &error) : NULL;
    if (nargs != 4u)
        error = "expected the facts, the rules, the goals and a bound";
    if (!value) {
        Atom *parts[5] = {head};
        for (uint32_t i = 0u; i < nargs && i < 4u; i++)
            parts[i + 1u] = args[i];
        Atom *call = atom_expr(arena, parts, 1u + (nargs < 4u ? nargs : 4u));
        value = atom_error(arena, call, atom_string(arena, error ? error : "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}
