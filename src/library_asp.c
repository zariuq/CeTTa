#include "library_asp.h"
#include "library.h"
#include "symbol.h"
#include "../native/asp/asp_cdnl_v1.h"
#include "../native/asp/asp_clingo_v1.h"

#include <stdlib.h>
#include <string.h>

/* The program's atoms, numbered in order of first appearance, found by an
 * open-addressed index of their hashes (slot: number + 1, 0 free). */
typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
    uint32_t *slots;
    uint32_t slot_cap;
} AspAtomTable;

static void asp_atom_table_free(AspAtomTable *t) {
    free(t->items);
    free(t->slots);
    t->items = NULL;
    t->slots = NULL;
}

static bool asp_atom_slots_grow(AspAtomTable *t) {
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

static int32_t asp_intern(AspAtomTable *t, Atom *a) {
    if ((t->len + 1u) * 2u > t->slot_cap && !asp_atom_slots_grow(t))
        return -1;
    uint32_t at = atom_hash(a) & (t->slot_cap - 1u);
    for (; t->slots[at]; at = (at + 1u) & (t->slot_cap - 1u))
        if (atom_eq(t->items[t->slots[at] - 1u], a))
            return (int32_t)(t->slots[at] - 1u);
    if (t->len == t->cap) {
        uint32_t cap = t->cap ? t->cap * 2u : 16u;
        Atom **items = realloc(t->items, sizeof(*items) * cap);
        if (!items)
            return -1;
        t->items = items;
        t->cap = cap;
    }
    t->items[t->len] = a;
    t->slots[at] = t->len + 1u;
    return (int32_t)t->len++;
}

typedef struct {
    AspRule *rules;
    uint32_t nrules;
    int32_t *store;
    AspAtomTable atoms;
} AspProgram;

static void asp_program_free(AspProgram *p) {
    free(p->rules);
    free(p->store);
    asp_atom_table_free(&p->atoms);
}

/* One normal rule: Head :- Positive, not Negative, not Extra (Extra may be
 * NULL).  The store has room for every literal the program needs. */
static bool asp_add_rule(AspProgram *p, size_t *at, Atom *head, Atom *pos,
                         Atom *neg, Atom *extra) {
    AspRule *out = &p->rules[p->nrules++];
    out->head = asp_intern(&p->atoms, head);
    out->pos = p->store + *at;
    out->pos_len = (uint32_t)pos->expr.len;
    for (uint32_t i = 0u; i < pos->expr.len; i++)
        p->store[(*at)++] = asp_intern(&p->atoms, pos->expr.elems[i]);
    out->neg = p->store + *at;
    out->neg_len = (uint32_t)neg->expr.len + (extra ? 1u : 0u);
    for (uint32_t i = 0u; i < neg->expr.len; i++)
        p->store[(*at)++] = asp_intern(&p->atoms, neg->expr.elems[i]);
    if (extra)
        p->store[(*at)++] = asp_intern(&p->atoms, extra);
    return out->head >= 0;
}

/* One normal rule over atoms given one by one. */
static bool asp_add_rule_atoms(AspProgram *p, size_t *at, Atom *head,
                               Atom *const *pos, uint32_t npos,
                               Atom *const *neg, uint32_t nneg) {
    AspRule *out = &p->rules[p->nrules++];
    out->head = asp_intern(&p->atoms, head);
    out->pos = p->store + *at;
    out->pos_len = npos;
    for (uint32_t i = 0u; i < npos; i++)
        p->store[(*at)++] = asp_intern(&p->atoms, pos[i]);
    out->neg = p->store + *at;
    out->neg_len = nneg;
    for (uint32_t i = 0u; i < nneg; i++)
        p->store[(*at)++] = asp_intern(&p->atoms, neg[i]);
    return out->head >= 0;
}

/* A weight statement: (weight-rule H L ((P W) ...) ((N W) ...)),
 * (weight-constraint L ((P W) ...) ((N W) ...)) or
 * (weight-choice (A ...) L ((P W) ...) ((N W) ...)); its body is the
 * positive literals, then the negative ones. */
typedef struct {
    Atom *head;        /* the atom of a weight-rule */
    Atom *choices;     /* the atoms of a weight-choice */
    bool constraint;
    uint64_t bound;
    Atom *pos;
    Atom *neg;
    uint64_t n;        /* the body's length */
} AspWeight;

static bool asp_weighted_list(Atom *list) {
    if (!list || list->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < list->expr.len; i++) {
        Atom *t = list->expr.elems[i];
        if (t->kind != ATOM_EXPR || t->expr.len != 2u ||
            t->expr.elems[1]->kind != ATOM_GROUNDED ||
            t->expr.elems[1]->ground.gkind != GV_INT ||
            t->expr.elems[1]->ground.ival < 0)
            return false;
    }
    return true;
}

static bool asp_weight_parse(Atom *st, AspWeight *w) {
    memset(w, 0, sizeof(*w));
    if (st->kind != ATOM_EXPR || st->expr.len < 4u)
        return false;
    Atom **e = st->expr.elems;
    CettaExprIndex b;
    if (st->expr.len == 5u && atom_is_symbol(e[0], "weight-rule")) {
        w->head = e[1];
        b = 2u;
    } else if (st->expr.len == 5u && atom_is_symbol(e[0], "weight-choice") &&
               e[1]->kind == ATOM_EXPR) {
        w->choices = e[1];
        b = 2u;
    } else if (st->expr.len == 4u && atom_is_symbol(e[0], "weight-constraint")) {
        w->constraint = true;
        b = 1u;
    } else {
        return false;
    }
    if (e[b]->kind != ATOM_GROUNDED || e[b]->ground.gkind != GV_INT ||
        e[b]->ground.ival < 0 || !asp_weighted_list(e[b + 1u]) ||
        !asp_weighted_list(e[b + 2u]))
        return false;
    w->bound = (uint64_t)e[b]->ground.ival;
    w->pos = e[b + 1u];
    w->neg = e[b + 2u];
    w->n = (uint64_t)w->pos->expr.len + w->neg->expr.len;
    return true;
}

/* The counter's rules and literals for a weight statement, as
 * WRule.normalize makes them: c(r,0,0); per literal and per k <= L a carry
 * rule and an add rule; and the head's rules. */
static bool asp_weight_size(const AspWeight *w, size_t *rules, size_t *lits) {
    uint64_t width = w->bound + 1u;
    if (w->bound == UINT64_MAX || (w->n && width > UINT64_MAX / 3u / w->n))
        return false;
    uint64_t heads = w->head ? 1u : w->constraint ? 1u : w->choices->expr.len;
    uint64_t r = 1u + 2u * w->n * width + (w->choices ? 2u * heads : heads);
    uint64_t l = 3u * w->n * width + (w->head ? 1u : w->constraint ? 2u : 3u * heads);
    if (r > SIZE_MAX - *rules || l > SIZE_MAX - *lits)
        return false;
    *rules += (size_t)r;
    *lits += (size_t)l;
    return true;
}

/* The counter atom c(r, i, k). */
static Atom *asp_count_atom(Arena *a, uint32_t r, uint64_t i, uint64_t k) {
    Atom *parts[4] = {atom_symbol(a, "asp:count"), atom_int(a, (int64_t)r),
                      atom_int(a, (int64_t)i), atom_int(a, (int64_t)k)};
    return parts[0] && parts[1] && parts[2] && parts[3]
        ? atom_expr(a, parts, 4u) : NULL;
}

static bool asp_weight_emit(Arena *a, AspProgram *p, size_t *at, uint32_t r,
                            const AspWeight *w) {
    Atom *start = asp_count_atom(a, r, 0u, 0u);
    if (!start || !asp_add_rule_atoms(p, at, start, NULL, 0u, NULL, 0u))
        return false;
    for (uint64_t i = 0u; i < w->n; i++) {
        bool positive = i < w->pos->expr.len;
        Atom *term = positive ? w->pos->expr.elems[i]
                              : w->neg->expr.elems[i - w->pos->expr.len];
        Atom *lit = term->expr.elems[0];
        uint64_t weight = (uint64_t)term->expr.elems[1]->ground.ival;
        for (uint64_t k = 0u; k <= w->bound; k++) {
            Atom *before = asp_count_atom(a, r, i, k);
            Atom *carry = asp_count_atom(a, r, i + 1u, k);
            uint64_t reached = weight > w->bound - k ? w->bound : k + weight;
            Atom *add = asp_count_atom(a, r, i + 1u, reached);
            if (!before || !carry || !add ||
                !asp_add_rule_atoms(p, at, carry, &before, 1u, NULL, 0u))
                return false;
            Atom *both[2] = {before, lit};
            if (!(positive
                      ? asp_add_rule_atoms(p, at, add, both, 2u, NULL, 0u)
                      : asp_add_rule_atoms(p, at, add, &before, 1u, &lit, 1u)))
                return false;
        }
    }
    Atom *c = asp_count_atom(a, r, w->n, w->bound);
    if (!c)
        return false;
    if (w->head)
        return asp_add_rule_atoms(p, at, w->head, &c, 1u, NULL, 0u);
    if (w->constraint) {
        Atom *falsum = atom_symbol(a, "asp:falsum");
        return falsum && asp_add_rule_atoms(p, at, falsum, &c, 1u, &falsum, 1u);
    }
    for (CettaExprIndex h = 0u; h < w->choices->expr.len; h++) {
        Atom *atom = w->choices->expr.elems[h];
        Atom *compl = atom_expr2(a, atom_symbol(a, "asp:compl"), atom);
        if (!compl ||
            !asp_add_rule_atoms(p, at, atom, &c, 1u, &compl, 1u) ||
            !asp_add_rule_atoms(p, at, compl, NULL, 0u, &atom, 1u))
            return false;
    }
    return true;
}

/* A program is an expression of statements over ground atoms:
 *   (rule H (P ...) (N ...))       H :- P, not N
 *   (constraint (P ...) (N ...))   no stable model has P true and N false:
 *                                  (asp:violated K) :- P, not N, not (asp:violated K)
 *   (choice A (P ...) (N ...))     A may hold when P, not N:
 *                                  A :- P, not N, not (asp:out A);
 *                                  (asp:out A) :- P, not N, not A
 *   (weight-rule H L ((P W) ...) ((N W) ...))   H holds when the true
 *                                  literals of the body weigh at least L
 *   (weight-constraint L ...)      no stable model's body weighs at least L
 *   (weight-choice (A ...) L ...)  each A may hold when the body does
 * A weight statement is normalized by the sequential weight counter (the
 * verified normalization of the Lean development, rule for rule): atoms
 * (asp:count R I K), (asp:compl A) and asp:falsum.
 * The asp: atoms are the translation's own and never appear in a model. */
static const char *asp_read_program(Arena *arena, Atom *value, AspProgram *p) {
    memset(p, 0, sizeof(*p));
    if (!value || value->kind != ATOM_EXPR)
        return "a program is an expression of rules";
    uint32_t n = (uint32_t)value->expr.len;
    size_t total = 0u;
    uint32_t rules = 0u;
    for (uint32_t r = 0u; r < n; r++) {
        Atom *st = value->expr.elems[r];
        bool rule = st->kind == ATOM_EXPR && st->expr.len == 4u &&
            atom_is_symbol(st->expr.elems[0], "rule");
        bool choice = st->kind == ATOM_EXPR && st->expr.len == 4u &&
            atom_is_symbol(st->expr.elems[0], "choice");
        bool constraint = st->kind == ATOM_EXPR && st->expr.len == 3u &&
            atom_is_symbol(st->expr.elems[0], "constraint");
        AspWeight weight;
        if (!(rule || choice || constraint) && asp_weight_parse(st, &weight)) {
            if (atom_has_vars(st))
                return "a program must be ground";
            size_t r_more = rules, l_more = total;
            if (!asp_weight_size(&weight, &r_more, &l_more) ||
                r_more > UINT32_MAX)
                return "a weight statement too large to normalize";
            rules = (uint32_t)r_more;
            total = l_more;
            continue;
        }
        if (!(rule || choice || constraint) ||
            st->expr.elems[st->expr.len - 2u]->kind != ATOM_EXPR ||
            st->expr.elems[st->expr.len - 1u]->kind != ATOM_EXPR)
            return "a statement is (rule H (P ...) (N ...)), (choice A (P ...) (N ...)), (constraint (P ...) (N ...)) or a weight statement";
        if (atom_has_vars(st))
            return "a program must be ground";
        size_t body = st->expr.elems[st->expr.len - 2u]->expr.len +
                      st->expr.elems[st->expr.len - 1u]->expr.len;
        total += choice ? 2u * (body + 1u) : body + (constraint ? 1u : 0u);
        rules += choice ? 2u : 1u;
    }
    p->rules = malloc(sizeof(*p->rules) * (rules ? rules : 1u));
    p->store = malloc(sizeof(*p->store) * (total ? total : 1u));
    if (!p->rules || !p->store)
        return "out of memory";
    size_t at = 0u;
    uint32_t constraints = 0u;
    uint32_t weights = 0u;
    for (uint32_t r = 0u; r < n; r++) {
        Atom *st = value->expr.elems[r];
        Atom **e = st->expr.elems;
        bool ok;
        AspWeight weight;
        if (asp_weight_parse(st, &weight)) {
            ok = asp_weight_emit(arena, p, &at, weights++, &weight);
        } else if (atom_is_symbol(e[0], "rule")) {
            ok = asp_add_rule(p, &at, e[1], e[2], e[3], NULL);
        } else if (atom_is_symbol(e[0], "constraint")) {
            Atom *bot = atom_expr2(arena, atom_symbol(arena, "asp:violated"),
                                   atom_int(arena, (int64_t)constraints++));
            ok = bot && asp_add_rule(p, &at, bot, e[1], e[2], bot);
        } else {
            Atom *out = atom_expr2(arena, atom_symbol(arena, "asp:out"), e[1]);
            ok = out && asp_add_rule(p, &at, e[1], e[2], e[3], out) &&
                 asp_add_rule(p, &at, out, e[2], e[3], e[1]);
        }
        if (!ok)
            return "out of memory";
    }
    for (size_t i = 0u; i < at; i++)
        if (p->store[i] < 0)
            return "out of memory";
    return NULL;
}

/* whether an atom is the translation's own: (asp:violated K), (asp:out A),
 * (asp:count R I K), (asp:compl A) or asp:falsum */
static bool asp_auxiliary(Atom *a) {
    if (atom_is_symbol(a, "asp:falsum"))
        return true;
    return a->kind == ATOM_EXPR &&
        ((a->expr.len == 2u &&
          (atom_is_symbol(a->expr.elems[0], "asp:violated") ||
           atom_is_symbol(a->expr.elems[0], "asp:out") ||
           atom_is_symbol(a->expr.elems[0], "asp:compl"))) ||
         (a->expr.len == 4u && atom_is_symbol(a->expr.elems[0], "asp:count")));
}

static Atom *asp_int_list(Arena *a, const int32_t *xs, uint32_t n) {
    Atom **items = arena_alloc(a, sizeof(*items) * (n ? n : 1u));
    if (!items)
        return NULL;
    for (uint32_t i = 0u; i < n; i++)
        if (!(items[i] = atom_int(a, xs[i])))
            return NULL;
    return atom_expr(a, items, n);
}

static Atom *asp_uint_list(Arena *a, const uint32_t *xs, uint32_t n) {
    Atom **items = arena_alloc(a, sizeof(*items) * (n ? n : 1u));
    if (!items)
        return NULL;
    for (uint32_t i = 0u; i < n; i++)
        if (!(items[i] = atom_int(a, (int64_t)xs[i])))
            return NULL;
    return atom_expr(a, items, n);
}

/* (certificate STEP ...) */
static Atom *asp_certificate(Arena *a, const AspAnswer *answer) {
    Atom **items = arena_alloc(a, sizeof(*items) * (answer->steps_len + 1u));
    if (!items || !(items[0] = atom_symbol(a, "certificate")))
        return NULL;
    for (uint32_t i = 0u; i < answer->steps_len; i++) {
        const AspStep *st = &answer->steps[i];
        Atom *clause = asp_int_list(a, st->clause, st->clause_len);
        Atom *id = atom_int(a, (int64_t)st->id);
        if (!clause || !id)
            return NULL;
        if (st->kind == ASP_STEP_RUP) {
            Atom *hints = asp_uint_list(a, st->hints, st->hints_len);
            Atom *parts[4] = {atom_symbol(a, "rup"), id, clause, hints};
            items[i + 1u] = hints && parts[0] ? atom_expr(a, parts, 4u) : NULL;
        } else {
            Atom *atoms = asp_uint_list(a, st->atoms, st->atoms_len);
            Atom *parts[5] = {atom_symbol(a, "loop"), id, clause, atoms,
                              atom_int(a, (int64_t)st->atom)};
            items[i + 1u] = atoms && parts[0] && parts[4] ? atom_expr(a, parts, 5u) : NULL;
        }
        if (!items[i + 1u])
            return NULL;
    }
    return atom_expr(a, items, answer->steps_len + 1u);
}

/* the original atoms with the given numbers, the translation's own left out */
static Atom *asp_atom_list(Arena *a, const AspAtomTable *t, const int32_t *ids, uint32_t n) {
    Atom **items = arena_alloc(a, sizeof(*items) * (n ? n : 1u));
    if (!items)
        return NULL;
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < n; i++)
        if (!asp_auxiliary(t->items[ids[i]]))
            items[kept++] = t->items[ids[i]];
    return atom_expr(a, items, kept);
}

/* The solver's steps, collected as MeTTa data. */
typedef struct {
    Arena *arena;
    const AspAtomTable *atoms;
    Atom **numbering;
    size_t numbering_len, numbering_cap;
    Atom **events;
    size_t len, cap;
    bool failed;
} AspTraceCollect;

static bool asp_trace_push(Atom ***items, size_t *len, size_t *cap, Atom *item) {
    if (!item)
        return false;
    if (*len == *cap) {
        size_t next = *cap ? *cap * 2u : 64u;
        Atom **grown = realloc(*items, sizeof(*grown) * next);
        if (!grown)
            return false;
        *items = grown;
        *cap = next;
    }
    (*items)[(*len)++] = item;
    return true;
}

static void asp_trace_event(void *context, AspTraceKind kind, int32_t lit,
                            uint32_t level, int32_t nogood,
                            const int32_t *lits, uint32_t count) {
    AspTraceCollect *t = context;
    Arena *a = t->arena;
    if (t->failed)
        return;
    Atom *event = NULL;
    switch (kind) {
    case ASP_TRACE_ATOM:
        if (level >= t->atoms->len ||
            !asp_trace_push(&t->numbering, &t->numbering_len,
                            &t->numbering_cap, t->atoms->items[level]))
            t->failed = true;
        return;
    case ASP_TRACE_DECIDE:
        event = atom_expr3(a, atom_symbol(a, "decide"), atom_int(a, lit),
                           atom_int(a, (int64_t)level));
        break;
    case ASP_TRACE_IMPLY: {
        Atom *parts[4] = {atom_symbol(a, "imply"), atom_int(a, lit),
                          atom_int(a, (int64_t)level), atom_int(a, nogood)};
        event = atom_expr(a, parts, 4u);
        break;
    }
    case ASP_TRACE_CONFLICT:
        event = atom_expr2(a, atom_symbol(a, "conflict"), atom_int(a, nogood));
        break;
    case ASP_TRACE_LEARN: {
        Atom *parts[4] = {atom_symbol(a, "learn"), atom_int(a, nogood),
                          asp_int_list(a, lits, count),
                          atom_int(a, (int64_t)level)};
        event = parts[2] ? atom_expr(a, parts, 4u) : NULL;
        break;
    }
    case ASP_TRACE_UNFOUNDED: {
        Atom *set = asp_int_list(a, lits, count);
        event = set ? atom_expr2(a, atom_symbol(a, "unfounded"), set) : NULL;
        break;
    }
    }
    if (!asp_trace_push(&t->events, &t->len, &t->cap, event))
        t->failed = true;
}

static Atom *asp_solve_value(Arena *a, const AspProgram *p, const char **error);

static Atom *asp_trace_value(Arena *a, const AspProgram *p, const char **error) {
    AspTraceCollect t = {.arena = a, .atoms = &p->atoms};
    AspTraceSink sink = {asp_trace_event};
    AspAnswer answer = asp_cdnl_solve_traced(p->rules, p->nrules, &sink, &t);
    Atom *result = NULL;
    if (!t.failed && answer.kind == ASP_ANSWER_SAT) {
        Atom *model = asp_atom_list(a, &p->atoms, answer.model, answer.model_len);
        result = model ? atom_expr2(a, atom_symbol(a, "model"), model) : NULL;
    } else if (!t.failed && answer.kind == ASP_ANSWER_UNSAT) {
        Atom *cert = asp_certificate(a, &answer);
        result = cert ? atom_expr2(a, atom_symbol(a, "unsat"), cert) : NULL;
    } else if (!t.failed) {
        *error = "the solver did not finish";
    }
    Atom *value = NULL;
    if (result) {
        Atom **numbered = arena_alloc(a, sizeof(*numbered) * (t.numbering_len + 1u));
        Atom **events = arena_alloc(a, sizeof(*events) * (t.len + 1u));
        if (numbered && events) {
            numbered[0] = atom_symbol(a, "atoms");
            for (size_t i = 0u; i < t.numbering_len; i++)
                numbered[i + 1u] = t.numbering[i];
            for (size_t i = 0u; i < t.len; i++)
                events[i] = t.events[i];
            Atom *parts[4] = {atom_symbol(a, "trace"),
                              atom_expr(a, numbered, (CettaExprLen)(t.numbering_len + 1u)),
                              atom_expr(a, events, (CettaExprLen)t.len), result};
            value = parts[1] && parts[2] ? atom_expr(a, parts, 4u) : NULL;
        }
    }
    asp_answer_free(&answer);
    free(t.numbering);
    free(t.events);
    return value;
}

static Atom *asp_solve_value(Arena *a, const AspProgram *p, const char **error) {
    AspAnswer answer = asp_cdnl_solve(p->rules, p->nrules);
    Atom *result = NULL;
    if (answer.kind == ASP_ANSWER_SAT) {
        Atom *model = asp_atom_list(a, &p->atoms, answer.model, answer.model_len);
        Atom *head = atom_symbol(a, "model");
        result = model && head ? atom_expr2(a, head, model) : NULL;
    } else if (answer.kind == ASP_ANSWER_UNSAT) {
        Atom *cert = asp_certificate(a, &answer);
        Atom *head = atom_symbol(a, "unsat");
        result = cert && head ? atom_expr2(a, head, cert) : NULL;
    } else {
        *error = "the solver did not finish";
    }
    asp_answer_free(&answer);
    return result;
}

static Atom *asp_models_value(Arena *a, const AspProgram *p, const char **error) {
    AspEnumeration e = asp_cdnl_enumerate(p->rules, p->nrules);
    Atom *result = NULL;
    if (e.kind == ASP_ANSWER_UNSAT) {
        Atom **models = arena_alloc(a, sizeof(*models) * (e.models_len ? e.models_len : 1u));
        bool ok = models != NULL;
        for (uint32_t k = 0u; ok && k < e.models_len; k++)
            ok = (models[k] = asp_atom_list(a, &p->atoms, e.models[k].atoms, e.models[k].len)) != NULL;
        Atom *list = ok ? atom_expr(a, models, e.models_len) : NULL;
        Atom *cert = list ? asp_certificate(a, &e.final) : NULL;
        Atom *head = atom_symbol(a, "models");
        result = cert && head ? atom_expr3(a, head, list, cert) : NULL;
    } else {
        *error = "the enumeration did not finish";
    }
    asp_enumeration_free(&e);
    return result;
}

/* ── clingo as a reference ─────────────────────────────────────────── */

/* A clingo symbol as MeTTa data: a number, a string, a constant as a
 * symbol, a function as the expression of its name and arguments, a tuple
 * as the expression of its elements, and a classically negated one as
 * (- S).  #inf and #sup have none. */
static Atom *asp_symbol_atom(Arena *a, AspClingoSymbol s, uint32_t depth) {
    if (depth > 256u)
        return NULL;
    switch (asp_clingo_symbol_kind(s)) {
    case ASP_CLINGO_SYMBOL_NUMBER: {
        int number = 0;
        return asp_clingo_symbol_number(s, &number)
            ? atom_int(a, (int64_t)number) : NULL;
    }
    case ASP_CLINGO_SYMBOL_STRING: {
        const char *text = NULL;
        return asp_clingo_symbol_string(s, &text) ? atom_string(a, text) : NULL;
    }
    case ASP_CLINGO_SYMBOL_FUNCTION: {
        const char *name = NULL;
        const AspClingoSymbol *arguments = NULL;
        size_t count = 0u;
        bool negative = false;
        if (!asp_clingo_symbol_function(s, &name, &arguments, &count, &negative))
            return NULL;
        Atom *term = NULL;
        if (count == 0u && name && *name) {
            term = atom_symbol(a, name);
        } else {
            size_t first = name && *name ? 1u : 0u;
            Atom **items = arena_alloc(a, sizeof(*items) * (count + first + 1u));
            if (!items)
                return NULL;
            if (first && !(items[0] = atom_symbol(a, name)))
                return NULL;
            for (size_t i = 0u; i < count; i++)
                if (!(items[first + i] = asp_symbol_atom(a, arguments[i], depth + 1u)))
                    return NULL;
            term = atom_expr(a, items, (CettaExprLen)(count + first));
        }
        return term && negative
            ? atom_expr2(a, atom_symbol(a, "-"), term) : term;
    }
    default:
        return NULL;
    }
}

/* A ground program as clingo's grounder reports it: statements over its
 * atom numbers, and the terms its shown atoms name.  Facts that always
 * hold are shown under atom 0. */
typedef struct {
    bool choice;
    bool weight;
    int32_t lower;
    size_t head_at, head_len;
    size_t body_at, body_len;
} AspGroundStatement;

typedef struct {
    Arena *arena;
    AspGroundStatement *statements;
    size_t len, cap;
    uint32_t *heads;
    size_t heads_len, heads_cap;
    int32_t *literals;
    int32_t *weights;
    size_t literals_len, literals_cap, weights_cap;
    Atom **names;
    size_t names_cap;
    Atom **extra;
    size_t extra_len, extra_cap;
    const char *refused;
} AspGround;

static bool asp_grow(void **items, size_t *cap, size_t need, size_t size) {
    if (need <= *cap)
        return true;
    size_t next = *cap ? *cap : 16u;
    while (next < need)
        next *= 2u;
    void *grown = realloc(*items, next * size);
    if (!grown)
        return false;
    *items = grown;
    *cap = next;
    return true;
}

/* Each literal is kept with its weight, 1 in a rule that has none. */
static bool asp_ground_statement(AspGround *g, bool choice, bool weight,
                                 int32_t lower, const uint32_t *head,
                                 size_t head_len, const int32_t *body,
                                 const int32_t *weights, size_t body_len) {
    if (!asp_grow((void **)&g->statements, &g->cap, g->len + 1u,
                  sizeof(*g->statements)) ||
        !asp_grow((void **)&g->heads, &g->heads_cap, g->heads_len + head_len,
                  sizeof(*g->heads)) ||
        !asp_grow((void **)&g->literals, &g->literals_cap,
                  g->literals_len + body_len, sizeof(*g->literals)) ||
        !asp_grow((void **)&g->weights, &g->weights_cap,
                  g->literals_len + body_len, sizeof(*g->weights))) {
        g->refused = "out of memory";
        return false;
    }
    g->statements[g->len++] = (AspGroundStatement){
        .choice = choice, .weight = weight, .lower = lower,
        .head_at = g->heads_len, .head_len = head_len,
        .body_at = g->literals_len, .body_len = body_len};
    if (head_len)
        memcpy(g->heads + g->heads_len, head, sizeof(*head) * head_len);
    g->heads_len += head_len;
    for (size_t i = 0u; i < body_len; i++) {
        g->literals[g->literals_len + i] = body[i];
        g->weights[g->literals_len + i] = weights ? weights[i] : 1;
    }
    g->literals_len += body_len;
    return true;
}

static bool asp_ground_rule(void *context, bool choice, const uint32_t *head,
                            size_t head_len, const int32_t *body,
                            size_t body_len) {
    return asp_ground_statement(context, choice, false, 0, head, head_len,
                                body, NULL, body_len);
}

static bool asp_ground_weight_rule(void *context, bool choice,
                                   const uint32_t *head, size_t head_len,
                                   int32_t lower, const int32_t *literals,
                                   const int32_t *weights, size_t body_len) {
    return asp_ground_statement(context, choice, true, lower, head, head_len,
                                literals, weights, body_len);
}

static bool asp_ground_output(void *context, AspClingoSymbol symbol,
                              uint32_t atom) {
    AspGround *g = context;
    Atom *term = asp_symbol_atom(g->arena, symbol, 0u);
    if (!term) {
        g->refused = "a shown symbol with no reading as a term";
        return false;
    }
    if (atom == 0u) {
        /* A fact that always holds. */
        Atom *empty = atom_expr(g->arena, NULL, 0u);
        Atom *parts[4] = {atom_symbol(g->arena, "rule"), term, empty, empty};
        Atom *fact = parts[0] && empty ? atom_expr(g->arena, parts, 4u) : NULL;
        if (!fact || !asp_grow((void **)&g->extra, &g->extra_cap,
                               g->extra_len + 1u, sizeof(*g->extra)))
            return false;
        g->extra[g->extra_len++] = fact;
        return true;
    }
    size_t old_cap = g->names_cap;
    if (!asp_grow((void **)&g->names, &g->names_cap, (size_t)atom + 1u,
                  sizeof(*g->names)))
        return false;
    memset(g->names + old_cap, 0, sizeof(*g->names) * (g->names_cap - old_cap));
    if (!g->names[atom]) {
        g->names[atom] = term;
        return true;
    }
    /* A second name of the same atom holds exactly when the first does. */
    Atom *empty = atom_expr(g->arena, NULL, 0u);
    Atom *rules[2];
    for (int k = 0; k < 2; k++) {
        Atom *from = k ? term : g->names[atom];
        Atom *to = k ? g->names[atom] : term;
        Atom *body = atom_expr(g->arena, &from, 1u);
        Atom *parts[4] = {atom_symbol(g->arena, "rule"), to, body, empty};
        rules[k] = body && empty && parts[0] ? atom_expr(g->arena, parts, 4u) : NULL;
        if (!rules[k])
            return false;
    }
    if (!asp_grow((void **)&g->extra, &g->extra_cap, g->extra_len + 2u,
                  sizeof(*g->extra)))
        return false;
    g->extra[g->extra_len++] = rules[0];
    g->extra[g->extra_len++] = rules[1];
    return true;
}

static bool asp_ground_unsupported(void *context, const char *what) {
    AspGround *g = context;
    g->refused = what;
    return false;
}

static Atom *asp_ground_atom(AspGround *g, uint32_t atom) {
    if (atom < g->names_cap && g->names[atom])
        return g->names[atom];
    return atom_expr2(g->arena, atom_symbol(g->arena, "asp:aux"),
                      atom_int(g->arena, (int64_t)atom));
}

/* The positive and the negative body of statement S: atoms, or (A W)
 * pairs for a weight rule. */
static bool asp_ground_bodies(AspGround *g, const AspGroundStatement *s,
                              Atom **pos, Atom **neg) {
    Atom **p = arena_alloc(g->arena, sizeof(*p) * (s->body_len + 1u));
    Atom **n = arena_alloc(g->arena, sizeof(*n) * (s->body_len + 1u));
    if (!p || !n)
        return false;
    size_t np = 0u, nn = 0u;
    for (size_t i = 0u; i < s->body_len; i++) {
        int32_t literal = g->literals[s->body_at + i];
        Atom *atom = asp_ground_atom(g, (uint32_t)(literal < 0 ? -literal : literal));
        if (!atom)
            return false;
        if (s->weight) {
            atom = atom_expr2(g->arena, atom,
                              atom_int(g->arena, (int64_t)g->weights[s->body_at + i]));
            if (!atom)
                return false;
        }
        if (literal < 0)
            n[nn++] = atom;
        else
            p[np++] = atom;
    }
    *pos = atom_expr(g->arena, p, (CettaExprLen)np);
    *neg = atom_expr(g->arena, n, (CettaExprLen)nn);
    return *pos && *neg;
}

static Atom *asp_ground_program(AspGround *g) {
    size_t cap = g->extra_len + g->heads_len + g->len + 1u;
    Atom **items = arena_alloc(g->arena, sizeof(*items) * cap);
    if (!items)
        return NULL;
    size_t len = 0u;
    for (size_t i = 0u; i < g->extra_len; i++)
        items[len++] = g->extra[i];
    for (size_t r = 0u; r < g->len; r++) {
        const AspGroundStatement *s = &g->statements[r];
        if (!s->choice && s->head_len > 1u) {
            g->refused = "disjunctive rule";
            return NULL;
        }
        Atom *pos = NULL, *neg = NULL;
        if (!asp_ground_bodies(g, s, &pos, &neg))
            return NULL;
        Atom *bound = s->weight ? atom_int(g->arena, (int64_t)s->lower) : NULL;
        if (s->choice && s->weight) {
            Atom **heads = arena_alloc(g->arena, sizeof(*heads) * (s->head_len + 1u));
            if (!heads)
                return NULL;
            for (size_t h = 0u; h < s->head_len; h++)
                if (!(heads[h] = asp_ground_atom(g, g->heads[s->head_at + h])))
                    return NULL;
            Atom *parts[5] = {atom_symbol(g->arena, "weight-choice"),
                              atom_expr(g->arena, heads, (CettaExprLen)s->head_len),
                              bound, pos, neg};
            items[len++] = atom_expr(g->arena, parts, 5u);
        } else if (s->choice) {
            for (size_t h = 0u; h < s->head_len; h++) {
                Atom *parts[4] = {atom_symbol(g->arena, "choice"),
                                  asp_ground_atom(g, g->heads[s->head_at + h]),
                                  pos, neg};
                items[len++] = atom_expr(g->arena, parts, 4u);
            }
        } else if (s->head_len == 0u) {
            Atom *parts[4] = {atom_symbol(g->arena,
                                  s->weight ? "weight-constraint" : "constraint"),
                              s->weight ? bound : pos, s->weight ? pos : neg, neg};
            items[len++] = atom_expr(g->arena, parts, s->weight ? 4u : 3u);
        } else {
            Atom *head = asp_ground_atom(g, g->heads[s->head_at]);
            Atom *parts[5] = {atom_symbol(g->arena, s->weight ? "weight-rule" : "rule"),
                              head, s->weight ? bound : pos, s->weight ? pos : neg,
                              neg};
            items[len++] = atom_expr(g->arena, parts, s->weight ? 5u : 4u);
        }
        if (!items[len - 1u])
            return NULL;
    }
    return atom_expr(g->arena, items, (CettaExprLen)len);
}

static Atom *asp_ground_value(Arena *a, Atom *text, const char **error) {
    if (!text || text->kind != ATOM_GROUNDED || text->ground.gkind != GV_STRING) {
        *error = "expected a program text";
        return NULL;
    }
    AspGround g = {.arena = a};
    AspClingoGroundSink sink = {asp_ground_rule, asp_ground_weight_rule,
                                asp_ground_output, asp_ground_unsupported};
    const char *clingo_error = NULL;
    Atom *program = NULL;
    if (asp_clingo_ground(text->ground.sval, &sink, &g, &clingo_error))
        program = asp_ground_program(&g);
    if (!program)
        *error = g.refused ? g.refused : clingo_error ? clingo_error
                                                      : "out of memory";
    free(g.statements);
    free(g.heads);
    free(g.literals);
    free(g.weights);
    free(g.names);
    free(g.extra);
    return program;
}

typedef struct {
    Arena *arena;
    const AspAtomTable *atoms;
    Atom **models;
    size_t len, cap;
} AspClingoCollect;

/* One clingo model: the program's atoms it holds, in table order. */
static bool asp_clingo_collect_model(void *context, const uint32_t *atoms,
                                     size_t len) {
    AspClingoCollect *c = context;
    Atom **items = arena_alloc(c->arena, sizeof(*items) * (len + 1u));
    if (!items || !asp_grow((void **)&c->models, &c->cap, c->len + 1u,
                            sizeof(*c->models)))
        return false;
    for (size_t i = 0u; i < len; i++)
        items[i] = c->atoms->items[atoms[i] - 1u];
    Atom *model = atom_expr(c->arena, items, (CettaExprLen)len);
    if (!model)
        return false;
    c->models[c->len++] = model;
    return true;
}

/* One clingo model of a program text: its shown symbols as terms. */
static bool asp_clingo_collect_symbols(void *context,
                                       const AspClingoSymbol *symbols,
                                       size_t len) {
    AspClingoCollect *c = context;
    Atom **items = arena_alloc(c->arena, sizeof(*items) * (len + 1u));
    if (!items || !asp_grow((void **)&c->models, &c->cap, c->len + 1u,
                            sizeof(*c->models)))
        return false;
    for (size_t i = 0u; i < len; i++)
        if (!(items[i] = asp_symbol_atom(c->arena, symbols[i], 0u)))
            return false;
    Atom *model = atom_expr(c->arena, items, (CettaExprLen)len);
    if (!model)
        return false;
    c->models[c->len++] = model;
    return true;
}

/* clingo's stable models of a program text, by its shown atoms. */
static Atom *asp_clingo_text_value(Arena *a, Atom *text, const char **error) {
    AspClingoCollect collect = {.arena = a};
    Atom *result = NULL;
    if (asp_clingo_text_models(text->ground.sval, asp_clingo_collect_symbols,
                               &collect, error)) {
        Atom *list = atom_expr(a, collect.models, (CettaExprLen)collect.len);
        result = list ? atom_expr2(a, atom_symbol(a, "clingo-models"), list) : NULL;
    } else if (!*error) {
        *error = "out of memory";
    }
    free(collect.models);
    return result;
}

/* clingo's stable models of program data: rules, choices and constraints
 * given to its solver as such, over the program's atoms in order of first
 * appearance. */
static Atom *asp_clingo_models_value(Arena *a, Atom *value, const char **error) {
    if (!value || value->kind != ATOM_EXPR) {
        *error = "a program is an expression of rules";
        return NULL;
    }
    AspAtomTable atoms = {0};
    size_t n = value->expr.len;
    AspClingoRule *rules = calloc(n ? n : 1u, sizeof(*rules));
    size_t total = 0u;
    for (size_t r = 0u; r < n; r++) {
        Atom *st = value->expr.elems[r];
        if (st->kind == ATOM_EXPR && st->expr.len >= 3u)
            total += st->expr.elems[st->expr.len - 2u]->kind == ATOM_EXPR
                ? st->expr.elems[st->expr.len - 2u]->expr.len : 0u;
        if (st->kind == ATOM_EXPR && st->expr.len >= 3u)
            total += st->expr.elems[st->expr.len - 1u]->kind == ATOM_EXPR
                ? st->expr.elems[st->expr.len - 1u]->expr.len : 0u;
    }
    uint32_t *store = malloc(sizeof(*store) * (total ? total : 1u));
    Atom *result = NULL;
    if (!rules || !store) {
        *error = "out of memory";
        goto done;
    }
    size_t at = 0u;
    for (size_t r = 0u; r < n; r++) {
        Atom *st = value->expr.elems[r];
        bool rule = st->kind == ATOM_EXPR && st->expr.len == 4u &&
            atom_is_symbol(st->expr.elems[0], "rule");
        bool choice = st->kind == ATOM_EXPR && st->expr.len == 4u &&
            atom_is_symbol(st->expr.elems[0], "choice");
        bool constraint = st->kind == ATOM_EXPR && st->expr.len == 3u &&
            atom_is_symbol(st->expr.elems[0], "constraint");
        if (!(rule || choice || constraint) ||
            st->expr.elems[st->expr.len - 2u]->kind != ATOM_EXPR ||
            st->expr.elems[st->expr.len - 1u]->kind != ATOM_EXPR) {
            *error = "a statement is (rule H (P ...) (N ...)), (choice A (P ...) (N ...)) or (constraint (P ...) (N ...))";
            goto done;
        }
        if (atom_has_vars(st)) {
            *error = "a program must be ground";
            goto done;
        }
        Atom *pos = st->expr.elems[st->expr.len - 2u];
        Atom *neg = st->expr.elems[st->expr.len - 1u];
        AspClingoRule *out = &rules[r];
        out->choice = choice;
        if (!constraint) {
            int32_t h = asp_intern(&atoms, st->expr.elems[1]);
            if (h < 0) { *error = "out of memory"; goto done; }
            out->head = (uint32_t)h + 1u;
        }
        out->pos = store + at;
        out->pos_len = pos->expr.len;
        for (size_t i = 0u; i < pos->expr.len; i++) {
            int32_t id = asp_intern(&atoms, pos->expr.elems[i]);
            if (id < 0) { *error = "out of memory"; goto done; }
            store[at++] = (uint32_t)id + 1u;
        }
        out->neg = store + at;
        out->neg_len = neg->expr.len;
        for (size_t i = 0u; i < neg->expr.len; i++) {
            int32_t id = asp_intern(&atoms, neg->expr.elems[i]);
            if (id < 0) { *error = "out of memory"; goto done; }
            store[at++] = (uint32_t)id + 1u;
        }
    }
    AspClingoCollect collect = {.arena = a, .atoms = &atoms};
    if (!asp_clingo_models(atoms.len, rules, n, asp_clingo_collect_model,
                           &collect, error)) {
        if (!*error)
            *error = "out of memory";
        goto done;
    }
    Atom *list = atom_expr(a, collect.models, (CettaExprLen)collect.len);
    result = list ? atom_expr2(a, atom_symbol(a, "clingo-models"), list) : NULL;
    free(collect.models);
done:
    free(rules);
    free(store);
    asp_atom_table_free(&atoms);
    return result;
}

bool cetta_asp_call(struct CettaLibraryContext *context, Arena *arena,
                    Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    bool solve = head->sym_id == g_builtin_syms.lib_asp_solve;
    bool models = head->sym_id == g_builtin_syms.lib_asp_models;
    bool ground = head->sym_id == g_builtin_syms.lib_asp_ground;
    bool clingo_models = head->sym_id == g_builtin_syms.lib_asp_clingo_models;
    bool trace = head->sym_id == g_builtin_syms.lib_asp_trace;
    /* Without clingo its two heads are not this library's to answer. */
    if ((ground || clingo_models) && !asp_clingo_available())
        return false;
    if (ground || clingo_models) {
        const char *error = NULL;
        Atom *value = nargs != 1u ? NULL
            : ground ? asp_ground_value(arena, args[0], &error)
            : args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_STRING
                ? asp_clingo_text_value(arena, args[0], &error)
                : asp_clingo_models_value(arena, args[0], &error);
        if (nargs != 1u)
            error = ground ? "expected one program text" : "expected one program";
        if (!value) {
            Atom *call = atom_expr(arena, (Atom *[]){head, nargs ? args[0] : head},
                                   nargs ? 2u : 1u);
            value = atom_error(arena, call,
                               atom_string(arena, error ? error : "out of memory"));
        }
        *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                                  .result_form = CETTA_CALL_RESULT_EXPRESSION};
        return value != NULL;
    }
    if (!solve && !models && !trace)
        return false;
    const char *error = NULL;
    Atom *value = NULL;
    AspProgram program;
    if (nargs != 1u) {
        error = "expected one program";
        memset(&program, 0, sizeof(program));
    } else {
        error = asp_read_program(arena, args[0], &program);
    }
    if (!error)
        value = solve ? asp_solve_value(arena, &program, &error)
              : trace ? asp_trace_value(arena, &program, &error)
                      : asp_models_value(arena, &program, &error);
    asp_program_free(&program);
    if (!value) {
        Atom *call = atom_expr(arena, (Atom *[]){head, nargs ? args[0] : head}, nargs ? 2u : 1u);
        value = atom_error(arena, call, atom_string(arena, error ? error : "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}
