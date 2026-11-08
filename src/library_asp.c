#include "library_asp.h"
#include "library.h"
#include "symbol.h"
#include "native_handle.h"
#include "../native/asp/asp_cdnl_v1.h"
#include "../native/asp/asp_clingo_v1.h"
#include "../native/asp/asp_hall_v1.h"
#include "../native/asp/sat_ipasir_v1.h"
#include "../native/smt/lra_simplex_v1.h"
#include "parser.h"

#include <errno.h>
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
    /* weight statements the solver propagates itself (asp_cdnl_v1.h): their
     * definitions, over dstore's atoms and wstore's weights */
    AspWeightDef *defs;
    uint32_t ndefs;
    int32_t *dstore;
    uint64_t *wstore;
} AspProgram;

/* The library's search: the fast one with Hall's condition over the
 * program's at-least-one constraints and at-most-one cliques.  Its check is
 * cheap where the program has no such constraints, and it refutes pigeonhole
 * and clique structure the plain search takes exponential time on. */
#define ASP_LIBRARY_SEARCH ASP_SEARCH_FAST_HALL

static void asp_program_free(AspProgram *p) {
    free(p->rules);
    free(p->store);
    free(p->defs);
    free(p->dstore);
    free(p->wstore);
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

/* Rules and literals a weight statement takes when the solver propagates
 * it: the head's rule, or the constraint's, or each choice's two. */
static void asp_weight_native_size(const AspWeight *w, size_t *rules,
                                   size_t *lits, size_t *def_lits) {
    if (w->head) {
        *rules += 1u;
        *lits += 1u;
    } else if (w->constraint) {
        *rules += 1u;
        *lits += 2u;
    } else {
        *rules += 2u * w->choices->expr.len;
        *lits += 3u * w->choices->expr.len;
    }
    *def_lits += (size_t)w->n;
}

/* A weight statement the solver propagates: the definition of its atom
 * (asp:weight R), true exactly when the body weighs at least the bound, and
 * the head's rule H :- (asp:weight R), the constraint's, or each choice's,
 * as the counter's rules would use the counter's last atom. */
static bool asp_weight_native(Arena *a, AspProgram *p, size_t *at,
                              size_t *dat, uint32_t r, const AspWeight *w) {
    Atom *wa = atom_expr2(a, atom_symbol(a, "asp:weight"), atom_int(a, (int64_t)r));
    if (!wa)
        return false;
    AspWeightDef *def = &p->defs[p->ndefs++];
    def->atom = asp_intern(&p->atoms, wa);
    def->bound = w->bound;
    int32_t *pos = p->dstore + *dat;
    uint64_t *pos_w = p->wstore + *dat;
    uint32_t np = 0u;
    for (CettaExprIndex i = 0u; i < w->pos->expr.len; i++) {
        Atom *t = w->pos->expr.elems[i];
        uint64_t weight = (uint64_t)t->expr.elems[1]->ground.ival;
        if (!weight)
            continue;
        pos[np] = asp_intern(&p->atoms, t->expr.elems[0]);
        pos_w[np++] = weight;
    }
    int32_t *neg = pos + np;
    uint64_t *neg_w = pos_w + np;
    uint32_t nn = 0u;
    for (CettaExprIndex i = 0u; i < w->neg->expr.len; i++) {
        Atom *t = w->neg->expr.elems[i];
        uint64_t weight = (uint64_t)t->expr.elems[1]->ground.ival;
        if (!weight)
            continue;
        neg[nn] = asp_intern(&p->atoms, t->expr.elems[0]);
        neg_w[nn++] = weight;
    }
    *dat += np + nn;
    def->pos = pos;
    def->pos_w = pos_w;
    def->pos_len = np;
    def->neg = neg;
    def->neg_w = neg_w;
    def->neg_len = nn;
    for (uint32_t i = 0u; i < np; i++)
        if (pos[i] < 0)
            return false;
    for (uint32_t i = 0u; i < nn; i++)
        if (neg[i] < 0)
            return false;
    if (def->atom < 0)
        return false;
    if (w->head)
        return asp_add_rule_atoms(p, at, w->head, &wa, 1u, NULL, 0u);
    if (w->constraint) {
        Atom *falsum = atom_symbol(a, "asp:falsum");
        return falsum && asp_add_rule_atoms(p, at, falsum, &wa, 1u, &falsum, 1u);
    }
    for (CettaExprIndex h = 0u; h < w->choices->expr.len; h++) {
        Atom *atom = w->choices->expr.elems[h];
        Atom *compl = atom_expr2(a, atom_symbol(a, "asp:compl"), atom);
        if (!compl ||
            !asp_add_rule_atoms(p, at, atom, &wa, 1u, &compl, 1u) ||
            !asp_add_rule_atoms(p, at, compl, NULL, 0u, &atom, 1u))
            return false;
    }
    return true;
}

/* (A W) pairs with integer weights of either sign. */
static bool asp_signed_weighted_list(Atom *list) {
    if (!list || list->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < list->expr.len; i++) {
        Atom *t = list->expr.elems[i];
        if (t->kind != ATOM_EXPR || t->expr.len != 2u ||
            t->expr.elems[1]->kind != ATOM_GROUNDED ||
            t->expr.elems[1]->ground.gkind != GV_INT)
            return false;
    }
    return true;
}

/* (minimize PRIORITY ((A W) ...) ((N W) ...)): the weights of the true
 * literals, A true and N false, count toward the cost at PRIORITY. */
static bool asp_is_minimize(Atom *st) {
    return st && st->kind == ATOM_EXPR && st->expr.len == 4u &&
        atom_is_symbol(st->expr.elems[0], "minimize") &&
        st->expr.elems[1]->kind == ATOM_GROUNDED &&
        st->expr.elems[1]->ground.gkind == GV_INT &&
        asp_signed_weighted_list(st->expr.elems[2]) &&
        asp_signed_weighted_list(st->expr.elems[3]);
}

/* (disjunction (H ...) (P ...) (N ...)): some H holds when P, not N. */
static bool asp_is_disjunction(Atom *st) {
    return st->kind == ATOM_EXPR && st->expr.len == 4u &&
        atom_is_symbol(st->expr.elems[0], "disjunction") &&
        st->expr.elems[1]->kind == ATOM_EXPR && st->expr.elems[1]->expr.len > 0u &&
        st->expr.elems[2]->kind == ATOM_EXPR && st->expr.elems[3]->kind == ATOM_EXPR;
}

/* The components of the positive dependency graph, a head depending on the
 * positive atoms of its rules' bodies: comp[a] for each atom (Tarjan's
 * algorithm, iterative). */
static uint32_t *asp_positive_components(const AspProgram *p) {
    uint32_t n = p->atoms.len;
    uint32_t *start = calloc((size_t)n + 1u, sizeof(*start));
    size_t edges = 0u;
    for (uint32_t r = 0u; start && r < p->nrules; r++)
        if (p->rules[r].head >= 0) {
            start[p->rules[r].head + 1] += p->rules[r].pos_len;
            edges += p->rules[r].pos_len;
        }
    uint32_t *adj = malloc(sizeof(*adj) * (edges ? edges : 1u));
    uint32_t *fill = malloc(sizeof(*fill) * ((size_t)n + 1u));
    uint32_t *index = malloc(sizeof(*index) * ((size_t)n + 1u));
    uint32_t *low = malloc(sizeof(*low) * ((size_t)n + 1u));
    uint32_t *comp = malloc(sizeof(*comp) * ((size_t)n + 1u));
    uint32_t *stack = malloc(sizeof(*stack) * ((size_t)n + 1u));
    uint32_t *calls = malloc(sizeof(*calls) * ((size_t)n + 1u));
    uint32_t *next = malloc(sizeof(*next) * ((size_t)n + 1u));
    uint8_t *on = calloc((size_t)n + 1u, 1u);
    bool ok = start && adj && fill && index && low && comp && stack && calls &&
              next && on;
    if (ok) {
        for (uint32_t a = 0u; a < n; a++)
            start[a + 1u] += start[a];
        memcpy(fill, start, sizeof(*fill) * n);
        for (uint32_t r = 0u; r < p->nrules; r++) {
            const AspRule *rule = &p->rules[r];
            if (rule->head >= 0)
                for (uint32_t k = 0u; k < rule->pos_len; k++)
                    adj[fill[rule->head]++] = (uint32_t)rule->pos[k];
        }
        uint32_t counter = 0u, top = 0u, ncomp = 0u;
        for (uint32_t a = 0u; a < n; a++)
            index[a] = UINT32_MAX;
        for (uint32_t root = 0u; root < n; root++) {
            if (index[root] != UINT32_MAX)
                continue;
            uint32_t depth = 0u;
            calls[depth++] = root;
            index[root] = low[root] = counter++;
            next[root] = start[root];
            stack[top++] = root;
            on[root] = 1u;
            while (depth) {
                uint32_t v = calls[depth - 1u];
                if (next[v] < start[v + 1u]) {
                    uint32_t w = adj[next[v]++];
                    if (index[w] == UINT32_MAX) {
                        index[w] = low[w] = counter++;
                        next[w] = start[w];
                        stack[top++] = w;
                        on[w] = 1u;
                        calls[depth++] = w;
                    } else if (on[w] && index[w] < low[v]) {
                        low[v] = index[w];
                    }
                    continue;
                }
                depth--;
                if (depth && low[v] < low[calls[depth - 1u]])
                    low[calls[depth - 1u]] = low[v];
                if (low[v] == index[v]) {
                    uint32_t w;
                    do {
                        w = stack[--top];
                        on[w] = 0u;
                        comp[w] = ncomp;
                    } while (w != v);
                    ncomp++;
                }
            }
        }
    }
    free(start);
    free(adj);
    free(fill);
    free(index);
    free(low);
    free(stack);
    free(calls);
    free(next);
    free(on);
    if (!ok) {
        free(comp);
        return NULL;
    }
    return comp;
}

/* A program is an expression of statements over ground atoms:
 *   (rule H (P ...) (N ...))       H :- P, not N
 *   (constraint (P ...) (N ...))   no stable model has P true and N false:
 *                                  asp:falsum :- P, not N, not asp:falsum,
 *                                  one head for every constraint; the solver
 *                                  keeps each such rule as the one nogood of
 *                                  its body
 *   (choice A (P ...) (N ...))     A may hold when P, not N:
 *                                  A :- P, not N, not (asp:out A);
 *                                  (asp:out A) :- P, not N, not A
 *   (weight-rule H L ((P W) ...) ((N W) ...))   H holds when the true
 *                                  literals of the body weigh at least L
 *   (weight-constraint L ...)      no stable model's body weighs at least L
 *   (weight-choice (A ...) L ...)  each A may hold when the body does
 *   (disjunction (H ...) (P ...) (N ...))   some H holds when P, not N
 * A disjunction is shifted, each H holding when P, not N and no other head
 * does, which keeps the stable models exactly when the program is
 * head-cycle-free: no two atoms of one head depend positively on each other.
 * A disjunctive program that is not is refused.
 * A weight statement is normalized by the sequential weight counter (the
 * verified normalization of the Lean development, rule for rule): atoms
 * (asp:count R I K), (asp:compl A) and asp:falsum.
 * The asp: atoms are the translation's own and never appear in a model. */
static const char *asp_read_program_with(Arena *arena, Atom *value,
                                         AspProgram *p,
                                         const uint8_t *normalize,
                                         uint32_t **def_stmt_out) {
    memset(p, 0, sizeof(*p));
    *def_stmt_out = NULL;
    if (!value || value->kind != ATOM_EXPR)
        return "a program is an expression of rules";
    uint32_t n = (uint32_t)value->expr.len;
    size_t total = 0u;
    uint32_t rules = 0u;
    size_t disjunctions = 0u, disjunct_atoms = 0u;
    size_t def_lits = 0u;
    uint32_t nweights = 0u, nnative = 0u;
    for (uint32_t r = 0u; r < n; r++) {
        Atom *st = value->expr.elems[r];
        bool rule = st->kind == ATOM_EXPR && st->expr.len == 4u &&
            atom_is_symbol(st->expr.elems[0], "rule");
        bool choice = st->kind == ATOM_EXPR && st->expr.len == 4u &&
            atom_is_symbol(st->expr.elems[0], "choice");
        bool constraint = st->kind == ATOM_EXPR && st->expr.len == 3u &&
            atom_is_symbol(st->expr.elems[0], "constraint");
        if (asp_is_minimize(st))
            continue;   /* the optimization's, not the solver's */
        if (asp_is_disjunction(st)) {
            if (atom_has_vars(st))
                return "a program must be ground";
            size_t heads = st->expr.elems[1]->expr.len;
            size_t body = st->expr.elems[2]->expr.len + st->expr.elems[3]->expr.len;
            total += heads * (body + heads - 1u);
            rules += (uint32_t)heads;
            disjunctions++;
            disjunct_atoms += heads;
            continue;
        }
        AspWeight weight;
        if (!(rule || choice || constraint) && asp_weight_parse(st, &weight)) {
            if (atom_has_vars(st))
                return "a program must be ground";
            size_t r_more = rules, l_more = total;
            if (normalize && normalize[nweights]) {
                if (!asp_weight_size(&weight, &r_more, &l_more))
                    return "a weight statement too large to normalize";
            } else {
                asp_weight_native_size(&weight, &r_more, &l_more, &def_lits);
                nnative++;
            }
            if (r_more > UINT32_MAX)
                return "a weight statement too large to normalize";
            nweights++;
            rules = (uint32_t)r_more;
            total = l_more;
            continue;
        }
        if (!(rule || choice || constraint) ||
            st->expr.elems[st->expr.len - 2u]->kind != ATOM_EXPR ||
            st->expr.elems[st->expr.len - 1u]->kind != ATOM_EXPR)
            return "a statement is (rule H (P ...) (N ...)), (choice A (P ...) (N ...)), (constraint (P ...) (N ...)), (disjunction (H ...) (P ...) (N ...)) or a weight statement";
        if (atom_has_vars(st))
            return "a program must be ground";
        size_t body = st->expr.elems[st->expr.len - 2u]->expr.len +
                      st->expr.elems[st->expr.len - 1u]->expr.len;
        total += choice ? 2u * (body + 1u) : body + (constraint ? 1u : 0u);
        rules += choice ? 2u : 1u;
    }
    p->rules = malloc(sizeof(*p->rules) * (rules ? rules : 1u));
    p->store = malloc(sizeof(*p->store) * (total ? total : 1u));
    p->defs = malloc(sizeof(*p->defs) * (nnative ? nnative : 1u));
    p->dstore = malloc(sizeof(*p->dstore) * (def_lits ? def_lits : 1u));
    p->wstore = malloc(sizeof(*p->wstore) * (def_lits ? def_lits : 1u));
    uint32_t *def_stmt = malloc(sizeof(*def_stmt) * (nnative ? nnative : 1u));
    if (!p->rules || !p->store || !p->defs || !p->dstore || !p->wstore ||
        !def_stmt) {
        free(def_stmt);
        return "out of memory";
    }
    *def_stmt_out = def_stmt;
    size_t at = 0u, dat = 0u;
    uint32_t weights = 0u;
    /* Each disjunction's distinct heads: their atoms, and where each
     * disjunction's run of them ends. */
    int32_t *disjunct = disjunctions ? malloc(sizeof(*disjunct) * disjunct_atoms) : NULL;
    size_t *disjunct_end = disjunctions ? malloc(sizeof(*disjunct_end) * disjunctions) : NULL;
    size_t ndisjunct = 0u, ndisjunctions = 0u;
    if (disjunctions && (!disjunct || !disjunct_end)) {
        free(disjunct);
        free(disjunct_end);
        return "out of memory";
    }
    for (uint32_t r = 0u; r < n; r++) {
        Atom *st = value->expr.elems[r];
        Atom **e = st->expr.elems;
        bool ok;
        AspWeight weight;
        if (asp_is_minimize(st))
            continue;
        if (asp_is_disjunction(st)) {
            Atom **h = e[1]->expr.elems;
            uint32_t nh = 0u, nn = (uint32_t)e[3]->expr.len;
            Atom **heads = malloc(sizeof(*heads) * e[1]->expr.len);
            Atom **neg = malloc(sizeof(*neg) * (nn + e[1]->expr.len));
            ok = heads && neg;
            for (CettaExprIndex i = 0u; ok && i < e[1]->expr.len; i++) {
                bool seen = false;
                for (uint32_t j = 0u; !seen && j < nh; j++)
                    seen = atom_eq(heads[j], h[i]);
                if (!seen)
                    heads[nh++] = h[i];
            }
            for (uint32_t i = 0u; ok && i < nh; i++) {
                uint32_t k = 0u;
                for (uint32_t j = 0u; j < nn; j++)
                    neg[k++] = e[3]->expr.elems[j];
                for (uint32_t j = 0u; j < nh; j++)
                    if (j != i)
                        neg[k++] = heads[j];
                ok = asp_add_rule_atoms(p, &at, heads[i], e[2]->expr.elems,
                                        (uint32_t)e[2]->expr.len, neg, k);
                if (ok)
                    disjunct[ndisjunct++] = p->rules[p->nrules - 1u].head;
            }
            if (ok)
                disjunct_end[ndisjunctions++] = ndisjunct;
            free(heads);
            free(neg);
        } else if (asp_weight_parse(st, &weight)) {
            uint32_t r_index = weights++;
            if (normalize && normalize[r_index]) {
                ok = asp_weight_emit(arena, p, &at, r_index, &weight);
            } else {
                def_stmt[p->ndefs] = r_index;
                ok = asp_weight_native(arena, p, &at, &dat, r_index, &weight);
            }
        } else if (atom_is_symbol(e[0], "rule")) {
            ok = asp_add_rule(p, &at, e[1], e[2], e[3], NULL);
        } else if (atom_is_symbol(e[0], "constraint")) {
            Atom *falsum = atom_symbol(arena, "asp:falsum");
            ok = falsum && asp_add_rule(p, &at, falsum, e[1], e[2], falsum);
        } else {
            Atom *out = atom_expr2(arena, atom_symbol(arena, "asp:out"), e[1]);
            ok = out && asp_add_rule(p, &at, e[1], e[2], e[3], out) &&
                 asp_add_rule(p, &at, out, e[2], e[3], e[1]);
        }
        if (!ok) {
            free(disjunct);
            free(disjunct_end);
            return "out of memory";
        }
    }
    for (size_t i = 0u; i < at; i++)
        if (p->store[i] < 0) {
            free(disjunct);
            free(disjunct_end);
            return "out of memory";
        }
    const char *error = NULL;
    if (ndisjunctions) {
        uint32_t *comp = asp_positive_components(p);
        if (!comp)
            error = "out of memory";
        for (size_t d = 0u, from = 0u; !error && d < ndisjunctions;
             from = disjunct_end[d++])
            for (size_t i = from; !error && i < disjunct_end[d]; i++)
                for (size_t j = i + 1u; !error && j < disjunct_end[d]; j++)
                    if (comp[disjunct[i]] == comp[disjunct[j]])
                        error = "a disjunction two of whose head atoms depend "
                                "positively on each other: the program is not "
                                "head-cycle-free";
        free(comp);
    }
    free(disjunct);
    free(disjunct_end);
    return error;
}

/* Whether each definition's atom stands alone in the positive dependency
 * graph with the definitions' edges, its atom on its positive literals:
 * those that do not are marked for the counter's normalization in
 * `normalize` (by weight statement); false when none needs it. */
static bool asp_defs_on_cycles(const AspProgram *p, const uint32_t *def_stmt,
                               uint8_t *normalize, bool *oom) {
    *oom = false;
    if (!p->ndefs)
        return false;
    /* the rules, with each definition read as rules W :- P for its
     * positive literals, are the graph */
    size_t extra = 0u;
    for (uint32_t d = 0u; d < p->ndefs; d++)
        extra += p->defs[d].pos_len ? 1u : 0u;
    AspProgram g = *p;
    g.rules = malloc(sizeof(*g.rules) * ((size_t)p->nrules + extra + 1u));
    if (!g.rules) {
        *oom = true;
        return false;
    }
    memcpy(g.rules, p->rules, sizeof(*p->rules) * p->nrules);
    g.nrules = p->nrules;
    for (uint32_t d = 0u; d < p->ndefs; d++)
        if (p->defs[d].pos_len)
            g.rules[g.nrules++] = (AspRule){.head = p->defs[d].atom,
                                            .pos = p->defs[d].pos,
                                            .pos_len = p->defs[d].pos_len};
    uint32_t *comp = asp_positive_components(&g);
    bool any = false;
    if (!comp) {
        *oom = true;
    } else {
        uint32_t *size = calloc((size_t)p->atoms.len + 1u, sizeof(*size));
        if (!size) {
            *oom = true;
        } else {
            for (uint32_t a = 0u; a < p->atoms.len; a++)
                size[comp[a]]++;
            for (uint32_t d = 0u; d < p->ndefs; d++) {
                int32_t w = p->defs[d].atom;
                bool cyclic = size[comp[w]] > 1u;
                for (uint32_t i = 0u; !cyclic && i < p->defs[d].pos_len; i++)
                    cyclic = p->defs[d].pos[i] == w;
                if (cyclic) {
                    normalize[def_stmt[d]] = 1u;
                    any = true;
                }
            }
            free(size);
        }
    }
    free(comp);
    free(g.rules);
    return any;
}

/* The program as the solver reads it: weight statements propagated by the
 * solver where no positive cycle runs through them, normalized by the
 * sequential counter where one does. */
/* CETTA_ASP_WEIGHTS_NORMALIZED=1 normalizes every weight statement, so the
 * solver's own propagation can be compared with the verified counter. */
static bool asp_weights_normalized(void) {
    static int normalized = -1;
    if (normalized < 0) {
        const char *setting = getenv("CETTA_ASP_WEIGHTS_NORMALIZED");
        normalized = setting && strcmp(setting, "1") == 0;
    }
    return normalized != 0;
}

static const char *asp_read_program(Arena *arena, Atom *value, AspProgram *p) {
    uint32_t *def_stmt = NULL;
    if (asp_weights_normalized()) {
        uint32_t count = 0u;
        if (value && value->kind == ATOM_EXPR)
            for (CettaExprIndex i = 0u; i < value->expr.len; i++) {
                AspWeight w;
                count += asp_weight_parse(value->expr.elems[i], &w) ? 1u : 0u;
            }
        uint8_t *normalize = malloc(count ? count : 1u);
        if (!normalize)
            return "out of memory";
        memset(normalize, 1, count ? count : 1u);
        const char *error = asp_read_program_with(arena, value, p, normalize,
                                                  &def_stmt);
        free(normalize);
        free(def_stmt);
        return error;
    }
    const char *error = asp_read_program_with(arena, value, p, NULL, &def_stmt);
    if (error || !p->ndefs) {
        free(def_stmt);
        return error;
    }
    uint32_t nweights = 0u;
    for (uint32_t d = 0u; d < p->ndefs; d++)
        if (def_stmt[d] + 1u > nweights)
            nweights = def_stmt[d] + 1u;
    uint8_t *normalize = calloc(nweights ? nweights : 1u, 1u);
    bool oom = normalize == NULL;
    bool again = !oom && asp_defs_on_cycles(p, def_stmt, normalize, &oom);
    free(def_stmt);
    if (oom) {
        free(normalize);
        return "out of memory";
    }
    if (again) {
        asp_program_free(p);
        error = asp_read_program_with(arena, value, p, normalize, &def_stmt);
        free(def_stmt);
    }
    free(normalize);
    return error;
}

/* whether an atom is the translation's own: (asp:out A), (asp:count R I K),
 * (asp:compl A), (asp:weight R) or asp:falsum */
/* The atoms a program's translations add: the solver's own, and the
 * grounder's hidden atoms (asp:aux N), which no model shows. */
static bool asp_auxiliary(Atom *a) {
    if (atom_is_symbol(a, "asp:falsum"))
        return true;
    return a->kind == ATOM_EXPR &&
        ((a->expr.len == 2u &&
          (atom_is_symbol(a->expr.elems[0], "asp:out") ||
           atom_is_symbol(a->expr.elems[0], "asp:compl") ||
           atom_is_symbol(a->expr.elems[0], "asp:weight") ||
           atom_is_symbol(a->expr.elems[0], "asp:aux"))) ||
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

static Atom *smt_number_atom(Arena *a, const char *text);

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
        } else if (st->kind == ASP_STEP_EUF) {
            /* (euf ID (LIT ...)): a clause congruence closure entails */
            Atom *parts[3] = {atom_symbol(a, "euf"), id, clause};
            items[i + 1u] = parts[0] ? atom_expr(a, parts, 3u) : NULL;
        } else if (st->kind == ASP_STEP_INPUT) {
            /* (input ID (LIT ...)): an added constraint's clause */
            Atom *parts[3] = {atom_symbol(a, "input"), id, clause};
            items[i + 1u] = parts[0] ? atom_expr(a, parts, 3u) : NULL;
        } else if (st->kind == ASP_STEP_LRA) {
            /* (lra ID (LIT ...) (W ...)): the bounds the clause's
             * falsification asserts, weighed by W, sum to a contradiction */
            Atom **ws = arena_alloc(a, sizeof(*ws) * (st->weights_len ? st->weights_len : 1u));
            bool ok = ws != NULL;
            for (uint32_t k = 0u; ok && k < st->weights_len; k++)
                ok = (ws[k] = smt_number_atom(a, st->weights[k])) != NULL;
            Atom *parts[4] = {atom_symbol(a, "lra"), id, clause,
                              ok ? atom_expr(a, ws, st->weights_len) : NULL};
            items[i + 1u] = parts[0] && parts[3] ? atom_expr(a, parts, 4u) : NULL;
        } else if (st->kind == ASP_STEP_WEIGHT) {
            /* (weight ID (LIT ...) DEF): a clause weight statement DEF
             * implies, counting the statements the solver propagates */
            Atom *parts[4] = {atom_symbol(a, "weight"), id, clause,
                              atom_int(a, (int64_t)st->def)};
            items[i + 1u] = parts[0] && parts[3] ? atom_expr(a, parts, 4u) : NULL;
        } else if (st->kind == ASP_STEP_LOOP) {
            Atom *atoms = asp_uint_list(a, st->atoms, st->atoms_len);
            Atom *parts[5] = {atom_symbol(a, "loop"), id, clause, atoms,
                              atom_int(a, (int64_t)st->atom)};
            items[i + 1u] = atoms && parts[0] && parts[4] ? atom_expr(a, parts, 5u) : NULL;
        } else {
            /* (hall ID (LIT ...) (GROUP ...) ((ATOM ...) ...)): the groups as
             * rule numbers, each clique a list of atom numbers */
            Atom *groups = asp_uint_list(a, st->groups, st->groups_len);
            uint32_t ncliques = 0u;
            for (uint32_t k = 0u; k < st->cliques_len; k++)
                ncliques += st->cliques[k] == UINT32_MAX;
            Atom **cliques = arena_alloc(a, sizeof(*cliques) * (ncliques ? ncliques : 1u));
            uint32_t at = 0u, start = 0u;
            for (uint32_t k = 0u; cliques && k < st->cliques_len; k++)
                if (st->cliques[k] == UINT32_MAX) {
                    cliques[at] = asp_uint_list(a, st->cliques + start, k - start);
                    if (!cliques[at++])
                        cliques = NULL;
                    start = k + 1u;
                }
            Atom *clique_list = cliques ? atom_expr(a, cliques, ncliques) : NULL;
            Atom *parts[5] = {atom_symbol(a, "hall"), id, clause, groups, clique_list};
            items[i + 1u] = groups && clique_list && parts[0] ? atom_expr(a, parts, 5u) : NULL;
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
    AspAnswer answer = asp_cdnl_solve_weighted(p->rules, p->nrules, p->defs, p->ndefs, ASP_LIBRARY_SEARCH, &sink, &t);
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

/* What asp:solve answers for a finished search: (model (A ...)) or
 * (unsat (certificate STEP ...)). */
static Atom *asp_answer_value(Arena *a, const AspProgram *p, const AspAnswer *answer,
                              const char **error) {
    Atom *result = NULL;
    if (answer->kind == ASP_ANSWER_SAT) {
        Atom *model = asp_atom_list(a, &p->atoms, answer->model, answer->model_len);
        Atom *head = atom_symbol(a, "model");
        result = model && head ? atom_expr2(a, head, model) : NULL;
    } else if (answer->kind == ASP_ANSWER_UNSAT) {
        Atom *cert = asp_certificate(a, answer);
        Atom *head = atom_symbol(a, "unsat");
        result = cert && head ? atom_expr2(a, head, cert) : NULL;
    } else {
        *error = answer->kind == ASP_ANSWER_REFUSED
            ? "the solver's answer failed its check" : "the solver did not finish";
    }
    return result;
}

static Atom *asp_solve_value(Arena *a, const AspProgram *p, const char **error) {
    AspAnswer answer = asp_cdnl_solve_weighted(p->rules, p->nrules, p->defs, p->ndefs, ASP_LIBRARY_SEARCH, NULL, NULL);
    Atom *result = asp_answer_value(a, p, &answer, error);
    asp_answer_free(&answer);
    return result;
}

/* ---- sessions: a solve one step at a time -----------------------------------
 * (asp:start P) opens a session on a program, searching as asp:solve does.
 * (asp:step S) advances it one iteration and answers (running (EVENT ...)), the
 * events asp:trace shows for that iteration, or (done RESULT), RESULT what
 * asp:solve answers; once done it keeps answering the same.  (asp:state S) is the
 * search's state as data: (state LEVEL CONFLICTS (trail ((LIT LEVEL REASON) ...))
 * (true (A ...)) (false (A ...))), the trail over the certificate's numbering and
 * REASON the implying nogood or -1 for a decision; a finished session's state is
 * (done RESULT).  (asp:close S) ends it.  A session holds its own copy of the
 * program, and every answer is copied out of it. */

static const char ASP_SESSION_KIND[] = "asp-session";

typedef struct {
    Arena arena;              /* the program's copy */
    AspProgram program;
    bool program_read;
    AspSession *session;
    AspTraceCollect collect;  /* the current iteration's events */
    AspAnswer answer;
    bool answered;
} AspSessionHandle;

static const AspTraceSink ASP_SESSION_SINK = {asp_trace_event};

static void asp_session_handle_free(void *resource) {
    AspSessionHandle *h = resource;
    if (!h)
        return;
    asp_session_free(h->session);
    asp_answer_free(&h->answer);
    if (h->program_read)
        asp_program_free(&h->program);
    free(h->collect.numbering);
    free(h->collect.events);
    arena_free(&h->arena);
    free(h);
}

static Atom *asp_start_value(struct CettaLibraryContext *context, Arena *a,
                             Atom *program, const char **error) {
    AspSessionHandle *h = calloc(1u, sizeof(*h));
    if (!h)
        return NULL;
    arena_init(&h->arena);
    Atom *copy = atom_deep_copy(&h->arena, program);
    *error = copy ? asp_read_program(&h->arena, copy, &h->program) : NULL;
    if (!copy || *error) {
        asp_session_handle_free(h);
        return NULL;
    }
    h->program_read = true;
    h->collect = (AspTraceCollect){.arena = a, .atoms = &h->program.atoms};
    h->session = asp_session_new_weighted(h->program.rules, h->program.nrules,
                                          h->program.defs, h->program.ndefs,
                                          ASP_LIBRARY_SEARCH, &ASP_SESSION_SINK,
                                          &h->collect);
    /* the numbering the setup reports is the program's own; nothing keeps it */
    free(h->collect.numbering);
    h->collect.numbering = NULL;
    h->collect.numbering_len = h->collect.numbering_cap = 0u;
    h->collect.len = 0u;
    uint64_t id = 0u;
    if (!h->session || h->collect.failed ||
        !cetta_native_handle_alloc(context, ASP_SESSION_KIND, h,
                                   asp_session_handle_free, &id)) {
        asp_session_handle_free(h);
        return NULL;
    }
    Atom *value = cetta_native_handle_owned_atom(context, a, ASP_SESSION_KIND, id);
    if (!value)
        (void)cetta_native_handle_close(context, ASP_SESSION_KIND, id);
    return value;
}

static Atom *asp_done_value(Arena *a, AspSessionHandle *h, const char **error) {
    Atom *result = asp_answer_value(a, &h->program, &h->answer, error);
    Atom *done = result ? atom_expr2(a, atom_symbol(a, "done"), result) : NULL;
    return done ? atom_deep_copy(a, done) : NULL;
}

static Atom *asp_step_value(Arena *a, AspSessionHandle *h, const char **error) {
    if (!h->answered) {
        h->collect.arena = a;
        h->collect.len = 0u;
        AspSessionStatus status = asp_session_step(h->session);
        if (h->collect.failed)
            return NULL;
        if (status == ASP_SESSION_RUNNING) {
            Atom **events = arena_alloc(a, sizeof(*events) *
                                           (h->collect.len ? h->collect.len : 1u));
            if (!events)
                return NULL;
            for (size_t i = 0u; i < h->collect.len; i++)
                events[i] = h->collect.events[i];
            Atom *list = atom_expr(a, events, (CettaExprLen)h->collect.len);
            return list ? atom_expr2(a, atom_symbol(a, "running"), list) : NULL;
        }
        h->answer = asp_session_answer(h->session);
        h->answered = true;
    }
    return asp_done_value(a, h, error);
}

static Atom *asp_state_value(Arena *a, AspSessionHandle *h, const char **error) {
    if (h->answered)
        return asp_done_value(a, h, error);
    AspSessionCounts counts = asp_session_counts(h->session);
    AspTrailEntry *trail = malloc(sizeof(*trail) * (counts.trail ? counts.trail : 1u));
    if (!trail)
        return NULL;
    uint32_t len = asp_session_trail(h->session, trail, counts.trail);
    Atom **entries = arena_alloc(a, sizeof(*entries) * (len ? len : 1u));
    Atom **truths = arena_alloc(a, sizeof(*truths) * (len ? len : 1u));
    Atom **falses = arena_alloc(a, sizeof(*falses) * (len ? len : 1u));
    uint32_t ntrue = 0u, nfalse = 0u;
    bool ok = entries && truths && falses;
    for (uint32_t i = 0u; ok && i < len; i++) {
        Atom *parts[3] = {atom_int(a, trail[i].lit), atom_int(a, (int64_t)trail[i].level),
                          atom_int(a, trail[i].reason)};
        entries[i] = atom_expr(a, parts, 3u);
        ok = entries[i] != NULL;
        uint32_t variable = (uint32_t)(trail[i].lit > 0 ? trail[i].lit : -trail[i].lit) - 1u;
        int32_t id = asp_session_atom(h->session, variable);
        if (ok && id >= 0 && (uint32_t)id < h->program.atoms.len &&
            !asp_auxiliary(h->program.atoms.items[id])) {
            if (trail[i].lit > 0)
                truths[ntrue++] = h->program.atoms.items[id];
            else
                falses[nfalse++] = h->program.atoms.items[id];
        }
    }
    free(trail);
    if (!ok)
        return NULL;
    Atom *trail_list = atom_expr(a, entries, len);
    Atom *true_list = atom_expr(a, truths, ntrue);
    Atom *false_list = atom_expr(a, falses, nfalse);
    if (!trail_list || !true_list || !false_list)
        return NULL;
    Atom *parts[6] = {atom_symbol(a, "state"), atom_int(a, (int64_t)counts.level),
                      atom_int(a, (int64_t)counts.conflicts),
                      atom_expr2(a, atom_symbol(a, "trail"), trail_list),
                      atom_expr2(a, atom_symbol(a, "true"), true_list),
                      atom_expr2(a, atom_symbol(a, "false"), false_list)};
    Atom *state = atom_expr(a, parts, 6u);
    (void)error;
    return state ? atom_deep_copy(a, state) : NULL;
}

static int32_t asp_atom_find(const AspAtomTable *t, Atom *a);

/* (asp:next S): the search to its next answer.  (model (A ...)) keeps the
 * session open, constraints may then be added and the search goes on; a
 * refutation ends it. */
static Atom *asp_next_value(Arena *a, AspSessionHandle *h, const char **error) {
    if (h->answered)
        return asp_done_value(a, h, error);
    h->collect.arena = a;
    AspSessionStatus status;
    do {
        h->collect.len = 0u;
        status = asp_session_step(h->session);
    } while (status == ASP_SESSION_RUNNING && !h->collect.failed);
    if (h->collect.failed)
        return NULL;
    int32_t *model = NULL;
    uint32_t len = 0u;
    if (asp_session_model(h->session, &model, &len)) {
        Atom *atoms = asp_atom_list(a, &h->program.atoms, model, len);
        free(model);
        Atom *value = atoms ? atom_expr2(a, atom_symbol(a, "model"), atoms) : NULL;
        return value ? atom_deep_copy(a, value) : NULL;
    }
    h->answer = asp_session_answer(h->session);
    h->answered = true;
    Atom *result = asp_answer_value(a, &h->program, &h->answer, error);
    return result ? atom_deep_copy(a, result) : NULL;
}

/* (asp:constrain S ((constraint (P ...) (N ...)) ...)): constraints added to
 * the session's program; constrained. */
static Atom *asp_constrain_value(Arena *a, AspSessionHandle *h, Atom *list,
                                 const char **error) {
    if (h->answered) {
        *error = "the session has finished";
        return NULL;
    }
    if (!list || list->kind != ATOM_EXPR) {
        *error = "expected a list of (constraint (P ...) (N ...))";
        return NULL;
    }
    bool ok = true;
    for (CettaExprIndex i = 0u; ok && i < list->expr.len; i++) {
        Atom *c = list->expr.elems[i];
        if (c->kind != ATOM_EXPR || c->expr.len != 3u ||
            !atom_is_symbol(c->expr.elems[0], "constraint") ||
            c->expr.elems[1]->kind != ATOM_EXPR || c->expr.elems[2]->kind != ATOM_EXPR) {
            *error = "expected a list of (constraint (P ...) (N ...))";
            return NULL;
        }
        Atom *ps = c->expr.elems[1], *ns = c->expr.elems[2];
        int32_t *ids = malloc(sizeof(*ids) * (ps->expr.len + ns->expr.len + 1u));
        if (!ids)
            return NULL;
        /* an atom the program never mentions: no id, never true */
        uint32_t np = 0u, nn = 0u;
        bool never = false;
        for (CettaExprIndex k = 0u; k < ps->expr.len && !never; k++) {
            int32_t id = asp_atom_find(&h->program.atoms, ps->expr.elems[k]);
            if (id < 0)
                never = true;
            else
                ids[np++] = id;
        }
        for (CettaExprIndex k = 0u; k < ns->expr.len && !never; k++) {
            int32_t id = asp_atom_find(&h->program.atoms, ns->expr.elems[k]);
            if (id >= 0)
                ids[np + nn++] = id;
        }
        if (!never)
            ok = asp_session_constrain(h->session, ids, np, ids + np, nn);
        free(ids);
    }
    if (!ok)
        *error = "the constraints could not be added";
    return ok ? atom_symbol(a, "constrained") : NULL;
}

/* TERM with each variable of VARS (k of them) replaced by its value. */
static Atom *asp_subst(Arena *a, Atom *t, Atom *const *vars, Atom *const *vals, uint32_t k) {
    if (t->kind == ATOM_VAR) {
        for (uint32_t j = 0u; j < k; j++)
            if (var_id_eq(vars[j]->var_id, t->var_id))
                return vals[j];
        return t;
    }
    if (t->kind != ATOM_EXPR || t->expr.len == 0u)
        return t;
    Atom **items = NULL;
    for (CettaExprIndex i = 0u; i < t->expr.len; i++) {
        Atom *child = t->expr.elems[i];
        Atom *c = asp_subst(a, child, vars, vals, k);
        if (!c)
            return NULL;
        if (c != child && !items) {
            items = arena_alloc(a, sizeof(*items) * t->expr.len);
            if (!items)
                return NULL;
            for (CettaExprIndex j = 0u; j < i; j++)
                items[j] = t->expr.elems[j];
        }
        if (items)
            items[i] = c;
    }
    return items ? atom_expr(a, items, t->expr.len) : t;
}

/* (asp:injections TERM VARS VALUES): every instance of TERM under an
 * injective map of the distinct variables VARS into VALUES, in
 * lexicographic order of the values' places.  The variants of a rule body
 * under renamings of its variables, as constraints over a generator's
 * literals use them. */
static Atom *asp_injections_value(Arena *a, Atom *term, Atom *vars_atom, Atom *values_atom,
                                  const char **error) {
    if (!term || !vars_atom || vars_atom->kind != ATOM_EXPR ||
        !values_atom || values_atom->kind != ATOM_EXPR) {
        *error = "expected a term, a list of variables and a list of values";
        return NULL;
    }
    uint32_t k = (uint32_t)vars_atom->expr.len, n = (uint32_t)values_atom->expr.len;
    Atom *const *vars = vars_atom->expr.elems;
    for (uint32_t i = 0u; i < k; i++) {
        if (vars[i]->kind != ATOM_VAR) {
            *error = "expected a list of variables";
            return NULL;
        }
        for (uint32_t j = 0u; j < i; j++)
            if (var_id_eq(vars[j]->var_id, vars[i]->var_id)) {
                *error = "the variables are not distinct";
                return NULL;
            }
    }
    if (k > 64u) {
        *error = "more than 64 variables";
        return NULL;
    }
    if (n < k)
        return atom_expr(a, NULL, 0u);
    uint64_t count = 1u;
    for (uint32_t j = 0u; j < k; j++) {
        count *= (uint64_t)(n - j);
        if (count > 10000000u) {
            *error = "too many injections";
            return NULL;
        }
    }
    Atom **instances = malloc(sizeof(*instances) * (count ? count : 1u));
    uint8_t *used = calloc(n + 1u, 1u);
    Atom *vals[64];
    uint32_t pick[64];
    uint32_t next[65];
    uint32_t depth = 0u;
    uint64_t made = 0u;
    bool ok = instances && used;
    next[0] = 0u;
    while (ok) {
        if (depth == k) {
            Atom *inst = asp_subst(a, term, vars, vals, k);
            ok = inst != NULL;
            if (ok)
                instances[made++] = inst;
            if (k == 0u)
                break;
            depth--;
            used[pick[depth]] = 0u;
            next[depth] = pick[depth] + 1u;
            continue;
        }
        uint32_t c = next[depth];
        while (c < n && used[c])
            c++;
        if (c >= n) {
            if (depth == 0u)
                break;
            depth--;
            used[pick[depth]] = 0u;
            next[depth] = pick[depth] + 1u;
            continue;
        }
        pick[depth] = c;
        vals[depth] = values_atom->expr.elems[c];
        used[c] = 1u;
        depth++;
        next[depth] = 0u;
    }
    Atom *result = ok ? atom_expr(a, instances, (CettaExprLen)made) : NULL;
    free(instances);
    free(used);
    return result;
}

/* (asp:decide S ATOM VALUE): decided, (assigned VALUE) when the atom has a
 * value now. */
static Atom *asp_decide_value(Arena *a, AspSessionHandle *h, Atom *atom, Atom *value,
                              const char **error) {
    if (h->answered) {
        *error = "the session has finished";
        return NULL;
    }
    /* True and False, as HE and PeTTa (true, false) spell them */
    bool boolean = value->kind == ATOM_GROUNDED && value->ground.gkind == GV_BOOL;
    bool truth = boolean ? value->ground.bval
                         : atom_is_symbol(value, "True") || atom_is_symbol(value, "true");
    if (!boolean && !truth && !atom_is_symbol(value, "False") && !atom_is_symbol(value, "false")) {
        *error = "expected an atom and True or False";
        return NULL;
    }
    int32_t id = asp_atom_find(&h->program.atoms, atom);
    int r = id >= 0 ? asp_session_decide(h->session, id, truth) : -1;
    if (r < 0) {
        *error = "not an atom of the program";
        return NULL;
    }
    return r == 1 ? atom_symbol(a, "decided")
                  : atom_expr2(a, atom_symbol(a, "assigned"), r == 2 ? atom_true(a) : atom_false(a));
}

/* (asp:nogoods S): (nogoods ((LIT ...) ...)), the nogoods made so far over
 * the trail's numbering. */
static Atom *asp_nogoods_value(Arena *a, AspSessionHandle *h, const char **error) {
    if (h->answered)
        return asp_done_value(a, h, error);
    int32_t *lits = NULL;
    uint32_t *starts = NULL, count = 0u;
    if (!asp_session_nogoods(h->session, &lits, &starts, &count))
        return NULL;
    Atom **items = arena_alloc(a, sizeof(*items) * (count ? count : 1u));
    bool ok = items != NULL;
    for (uint32_t k = 0u; ok && k < count; k++) {
        uint32_t n = starts[k + 1u] - starts[k];
        Atom **ls = arena_alloc(a, sizeof(*ls) * (n ? n : 1u));
        ok = ls != NULL;
        for (uint32_t i = 0u; ok && i < n; i++)
            ok = (ls[i] = atom_int(a, lits[starts[k] + i])) != NULL;
        items[k] = ok ? atom_expr(a, ls, n) : NULL;
        ok = ok && items[k];
    }
    free(lits);
    free(starts);
    Atom *list = ok ? atom_expr(a, items, count) : NULL;
    return list ? atom_expr2(a, atom_symbol(a, "nogoods"), list) : NULL;
}

static bool asp_session_call(struct CettaLibraryContext *context, Arena *arena,
                             Atom *head, Atom **args, uint32_t nargs,
                             CettaCallOutcome *out) {
    SymbolId h = head->sym_id;
    bool start = h == g_builtin_syms.lib_asp_start;
    bool step = h == g_builtin_syms.lib_asp_step;
    bool state = h == g_builtin_syms.lib_asp_state;
    bool close = h == g_builtin_syms.lib_asp_close;
    bool next = h == g_builtin_syms.lib_asp_next;
    bool constrain = h == g_builtin_syms.lib_asp_constrain;
    bool decide = h == g_builtin_syms.lib_asp_decide;
    bool nogoods = h == g_builtin_syms.lib_asp_nogoods;
    if (h == g_builtin_syms.lib_asp_injections) {
        const char *rerror = NULL;
        Atom *rvalue = nargs == 3u ? asp_injections_value(arena, args[0], args[1], args[2], &rerror)
                                   : NULL;
        if (!rvalue) {
            Atom *parts[4] = {head, NULL, NULL, NULL};
            for (uint32_t i = 0u; i < nargs && i < 3u; i++)
                parts[i + 1u] = args[i];
            Atom *call = atom_expr(arena, parts, nargs < 3u ? nargs + 1u : 4u);
            rvalue = atom_error(arena, call, atom_string(arena, rerror ? rerror
                                       : nargs != 3u ? "expected a term, a list of variables and a list of values"
                                                     : "out of memory"));
        }
        *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = rvalue,
                                  .result_form = CETTA_CALL_RESULT_EXPRESSION};
        return rvalue != NULL;
    }
    if (!start && !step && !state && !close && !next && !constrain && !decide && !nogoods)
        return false;
    const char *error = NULL;
    Atom *value = NULL;
    if (nargs != (constrain ? 2u : decide ? 3u : 1u)) {
        error = start ? "expected one program"
              : constrain ? "expected a session and a list of constraints"
              : decide ? "expected a session, an atom and True or False"
              : "expected one session";
    } else if (start) {
        value = asp_start_value(context, arena, args[0], &error);
    } else {
        AspSessionHandle *handle =
            cetta_native_handle_get_owned(context, args[0], ASP_SESSION_KIND);
        uint64_t id = 0u;
        if (!handle) {
            error = "not an open session";
        } else if (close) {
            value = cetta_native_handle_arg(args[0], ASP_SESSION_KIND, &id) &&
                    cetta_native_handle_close(context, ASP_SESSION_KIND, id)
                ? atom_symbol(arena, "closed") : NULL;
        } else {
            value = step ? asp_step_value(arena, handle, &error)
                  : next ? asp_next_value(arena, handle, &error)
                  : constrain ? asp_constrain_value(arena, handle, args[1], &error)
                  : decide ? asp_decide_value(arena, handle, args[1], args[2], &error)
                  : nogoods ? asp_nogoods_value(arena, handle, &error)
                  : asp_state_value(arena, handle, &error);
        }
    }
    if (!value) {
        Atom *call = atom_expr(arena, (Atom *[]){head, nargs ? args[0] : head}, nargs ? 2u : 1u);
        value = atom_error(arena, call, atom_string(arena, error ? error : "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}

static Atom *asp_models_value(Arena *a, const AspProgram *p, const char **error) {
    AspEnumeration e = asp_cdnl_enumerate_weighted(p->rules, p->nrules, p->defs, p->ndefs, ASP_LIBRARY_SEARCH);
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

/* ── optimization ──────────────────────────────────────────────────── */

/* A cost term: weight w counts when `atom` is true (positive) or false. */
typedef struct {
    Atom *atom;
    bool positive;
    uint64_t w;
} AspCostTerm;

/* One priority's cost: the offset plus the weights of its terms that hold. */
typedef struct {
    int64_t priority;
    AspCostTerm *terms;
    uint32_t n, cap;
    int64_t offset;
    uint64_t total;
} AspLevel;

static int32_t asp_atom_find(const AspAtomTable *t, Atom *a) {
    if (!t->slot_cap)
        return -1;
    uint32_t at = atom_hash(a) & (t->slot_cap - 1u);
    for (; t->slots[at]; at = (at + 1u) & (t->slot_cap - 1u))
        if (atom_eq(t->items[t->slots[at] - 1u], a))
            return (int32_t)(t->slots[at] - 1u);
    return -1;
}

static bool asp_level_term(AspLevel *l, Atom *atom, bool positive, int64_t w) {
    if (w == 0)
        return true;
    if (w < 0) {
        /* -w when it holds is -w, plus w when it does not */
        l->offset += w;
        positive = !positive;
        w = -w;
    }
    if (l->n == l->cap) {
        uint32_t cap = l->cap ? 2u * l->cap : 16u;
        AspCostTerm *grown = realloc(l->terms, sizeof(*grown) * cap);
        if (!grown)
            return false;
        l->terms = grown;
        l->cap = cap;
    }
    l->terms[l->n++] = (AspCostTerm){atom, positive, (uint64_t)w};
    l->total += (uint64_t)w;
    return true;
}

/* The weight of a level's terms that hold in the model of program p. */
static uint64_t asp_level_cost(const AspLevel *l, const AspProgram *p,
                               const uint8_t *in_model) {
    uint64_t cost = 0u;
    for (uint32_t i = 0u; i < l->n; i++) {
        int32_t id = asp_atom_find(&p->atoms, l->terms[i].atom);
        bool holds = id >= 0 && in_model[id];
        if (holds == l->terms[i].positive)
            cost += l->terms[i].w;
    }
    return cost;
}

/* (weight-constraint BOUND ((A W) ...) ((N W) ...)): no model in which the
 * level's terms that hold weigh BOUND or more. */
static Atom *asp_level_bound(Arena *a, const AspLevel *l, uint64_t bound) {
    Atom **pos = arena_alloc(a, sizeof(*pos) * (l->n + 1u));
    Atom **neg = arena_alloc(a, sizeof(*neg) * (l->n + 1u));
    if (!pos || !neg)
        return NULL;
    uint32_t np = 0u, nn = 0u;
    for (uint32_t i = 0u; i < l->n; i++) {
        Atom *t = atom_expr2(a, l->terms[i].atom, atom_int(a, (int64_t)l->terms[i].w));
        if (!t)
            return NULL;
        if (l->terms[i].positive)
            pos[np++] = t;
        else
            neg[nn++] = t;
    }
    Atom *parts[4] = {atom_symbol(a, "weight-constraint"),
                      atom_int(a, (int64_t)bound),
                      atom_expr(a, pos, np), atom_expr(a, neg, nn)};
    return parts[0] && parts[1] && parts[2] && parts[3]
        ? atom_expr(a, parts, 4u) : NULL;
}

static int asp_level_order(const void *x, const void *y) {
    int64_t a = ((const AspLevel *)x)->priority, b = ((const AspLevel *)y)->priority;
    return (a < b) - (a > b);   /* the highest priority first */
}

/* A program with minimize statements: its optimal stable models, the cost
 * at each priority lexicographically least, the highest priority first.
 * Branch and bound: each model found bounds the next search below its cost
 * at the current priority, by a weight constraint, until the search refutes
 * the bound, the refutation's certificate showing that no model costs less
 * there; that priority is then held at its optimum.  (asp:solve P) answers
 * (optimum (A ...) ((PRIORITY COST) ...) ((PRIORITY (certificate ...)) ...))
 * or, without a stable model, (unsat (certificate ...)); asp:models answers
 * the optimal models. */
static Atom *asp_optimize_value(Arena *a, Atom *value, bool enumerate,
                                const char **error) {
    uint32_t n = (uint32_t)value->expr.len;
    Atom **base = arena_alloc(a, sizeof(*base) * (n + 1u));
    AspLevel *levels = calloc(n ? n : 1u, sizeof(*levels));
    uint32_t nbase = 0u, nlevels = 0u;
    Atom *result = NULL;
    bool ok = base && levels;
    for (uint32_t i = 0u; ok && i < n; i++) {
        Atom *st = value->expr.elems[i];
        if (!asp_is_minimize(st)) {
            base[nbase++] = st;
            continue;
        }
        if (atom_has_vars(st)) {
            *error = "a program must be ground";
            ok = false;
            break;
        }
        int64_t priority = st->expr.elems[1]->ground.ival;
        uint32_t li = 0u;
        while (li < nlevels && levels[li].priority != priority)
            li++;
        if (li == nlevels)
            levels[nlevels++].priority = priority;
        for (int side = 0; ok && side < 2; side++) {
            Atom *list = st->expr.elems[2 + side];
            for (CettaExprIndex k = 0u; ok && k < list->expr.len; k++)
                ok = asp_level_term(&levels[li], list->expr.elems[k]->expr.elems[0],
                                    side == 0,
                                    list->expr.elems[k]->expr.elems[1]->ground.ival);
        }
    }
    if (ok)
        qsort(levels, nlevels, sizeof(*levels), asp_level_order);
    /* the fixed bounds of finished priorities, then the current one */
    Atom **program = ok ? arena_alloc(a, sizeof(*program) * (nbase + nlevels + 2u)) : NULL;
    Atom **certificates = ok ? arena_alloc(a, sizeof(*certificates) * (nlevels + 1u)) : NULL;
    uint64_t *optimum = ok ? calloc(nlevels + 1u, sizeof(*optimum)) : NULL;
    Atom *incumbent = NULL;
    ok = ok && program && certificates && optimum;
    if (ok)
        memcpy(program, base, sizeof(*base) * nbase);
    uint32_t nfixed = 0u;
    for (uint32_t li = 0u; ok && li < nlevels; li++) {
        Atom *current = NULL;
        certificates[li] = NULL;
        for (;;) {
            uint32_t len = nbase + nfixed;
            if (current)
                program[len++] = current;
            Atom *prog = atom_expr(a, program, len);
            AspProgram p = {0};
            const char *read_error = prog ? asp_read_program(a, prog, &p) : "out of memory";
            if (read_error) {
                *error = read_error;
                asp_program_free(&p);
                ok = false;
                break;
            }
            AspAnswer answer = asp_cdnl_solve_weighted(p.rules, p.nrules, p.defs, p.ndefs,
                                                       ASP_LIBRARY_SEARCH, NULL, NULL);
            if (answer.kind == ASP_ANSWER_SAT) {
                uint8_t *in_model = calloc(p.atoms.len ? p.atoms.len : 1u, 1u);
                if (!in_model) {
                    ok = false;
                } else {
                    for (uint32_t k = 0u; k < answer.model_len; k++)
                        in_model[answer.model[k]] = 1u;
                    incumbent = asp_atom_list(a, &p.atoms, answer.model, answer.model_len);
                    for (uint32_t lj = 0u; lj < nlevels; lj++)
                        optimum[lj] = asp_level_cost(&levels[lj], &p, in_model);
                    free(in_model);
                    ok = incumbent != NULL;
                }
                uint64_t cost = optimum[li];
                asp_answer_free(&answer);
                asp_program_free(&p);
                if (!ok)
                    break;
                if (cost == 0u)
                    break;          /* nothing weighs less */
                current = asp_level_bound(a, &levels[li], cost);
                if (!current) {
                    ok = false;
                    break;
                }
                continue;
            }
            if (answer.kind == ASP_ANSWER_UNSAT) {
                Atom *cert = asp_certificate(a, &answer);
                asp_answer_free(&answer);
                asp_program_free(&p);
                if (!cert) {
                    ok = false;
                    break;
                }
                if (!incumbent) {
                    /* no stable model at all: asp:solve's and asp:models'
                     * shapes for that */
                    result = enumerate
                        ? atom_expr3(a, atom_symbol(a, "models"), atom_expr(a, NULL, 0u), cert)
                        : atom_expr2(a, atom_symbol(a, "unsat"), cert);
                    ok = false;
                    break;
                }
                certificates[li] = cert;
                break;
            }
            asp_answer_free(&answer);
            asp_program_free(&p);
            *error = answer.kind == ASP_ANSWER_REFUSED
                ? "the solver's answer was refused by its own check"
                : "the solve did not finish";
            ok = false;
            break;
        }
        if (!ok)
            break;
        /* this priority held at its optimum from now on */
        if (optimum[li] + 1u <= levels[li].total) {
            Atom *fixed = asp_level_bound(a, &levels[li], optimum[li] + 1u);
            if (!fixed) {
                ok = false;
                break;
            }
            program[nbase + nfixed++] = fixed;
        }
    }
    if (ok && enumerate) {
        Atom *prog = atom_expr(a, program, nbase + nfixed);
        AspProgram p = {0};
        const char *read_error = prog ? asp_read_program(a, prog, &p) : "out of memory";
        if (read_error)
            *error = read_error;
        else
            result = asp_models_value(a, &p, error);
        asp_program_free(&p);
    } else if (ok) {
        Atom **costs = arena_alloc(a, sizeof(*costs) * (nlevels + 1u));
        Atom **proofs = arena_alloc(a, sizeof(*proofs) * (nlevels + 1u));
        uint32_t nproofs = 0u;
        for (uint32_t li = 0u; costs && proofs && li < nlevels; li++) {
            costs[li] = atom_expr2(a, atom_int(a, levels[li].priority),
                                   atom_int(a, levels[li].offset + (int64_t)optimum[li]));
            if (certificates[li])
                proofs[nproofs++] = atom_expr2(a, atom_int(a, levels[li].priority),
                                               certificates[li]);
        }
        Atom *parts[4] = {atom_symbol(a, "optimum"), incumbent,
                          costs ? atom_expr(a, costs, nlevels) : NULL,
                          proofs ? atom_expr(a, proofs, nproofs) : NULL};
        result = parts[0] && parts[1] && parts[2] && parts[3]
            ? atom_expr(a, parts, 4u) : NULL;
    }
    for (uint32_t li = 0u; levels && li < nlevels; li++)
        free(levels[li].terms);
    free(levels);
    free(optimum);
    if (!result && !*error)
        *error = "out of memory";
    return result;
}

static bool asp_has_minimize(Atom *value) {
    if (!value || value->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < value->expr.len; i++)
        if (asp_is_minimize(value->expr.elems[i]))
            return true;
    return false;
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
    /* minimize statements: priority and their literals' run in literals and
     * weights */
    struct { int32_t priority; size_t at, len; } *minimize;
    size_t minimize_len, minimize_cap;
    /* element buffers, reused: atom_expr copies its elements; and the one
     * empty body shared by every statement */
    Atom **scratch;
    size_t scratch_cap;
    Atom *empty;
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

static bool asp_ground_minimize(void *context, int32_t priority,
                                const int32_t *literals, const int32_t *weights,
                                size_t len) {
    AspGround *g = context;
    if (!asp_grow((void **)&g->minimize, &g->minimize_cap, g->minimize_len + 1u,
                  sizeof(*g->minimize)) ||
        !asp_grow((void **)&g->literals, &g->literals_cap, g->literals_len + len,
                  sizeof(*g->literals)) ||
        !asp_grow((void **)&g->weights, &g->weights_cap, g->literals_len + len,
                  sizeof(*g->weights))) {
        g->refused = "out of memory";
        return false;
    }
    g->minimize[g->minimize_len].priority = priority;
    g->minimize[g->minimize_len].at = g->literals_len;
    g->minimize[g->minimize_len].len = len;
    g->minimize_len++;
    for (size_t i = 0u; i < len; i++) {
        g->literals[g->literals_len + i] = literals[i];
        g->weights[g->literals_len + i] = weights[i];
    }
    g->literals_len += len;
    return true;
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
static Atom *asp_ground_list(AspGround *g, Atom **elems, size_t len) {
    if (len == 0u) {
        if (!g->empty)
            g->empty = atom_expr(g->arena, NULL, 0u);
        return g->empty;
    }
    return atom_expr(g->arena, elems, (CettaExprLen)len);
}

static bool asp_ground_bodies(AspGround *g, const AspGroundStatement *s,
                              Atom **pos, Atom **neg) {
    if (!asp_grow((void **)&g->scratch, &g->scratch_cap, 2u * s->body_len + 2u,
                  sizeof(*g->scratch)))
        return false;
    Atom **p = g->scratch;
    Atom **n = g->scratch + s->body_len + 1u;
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
    *pos = asp_ground_list(g, p, np);
    *neg = asp_ground_list(g, n, nn);
    return *pos && *neg;
}

static Atom *asp_ground_program_items(AspGround *g, Atom **items);

/* The program: its statements' items gathered in a heap buffer, which the
 * program expression copies. */
static Atom *asp_ground_program(AspGround *g) {
    size_t cap = g->extra_len + g->heads_len + g->len + g->minimize_len + 1u;
    Atom **items = malloc(sizeof(*items) * cap);
    if (!items)
        return NULL;
    Atom *program = asp_ground_program_items(g, items);
    free(items);
    return program;
}

static Atom *asp_ground_program_items(AspGround *g, Atom **items) {
    size_t len = 0u;
    for (size_t i = 0u; i < g->extra_len; i++)
        items[len++] = g->extra[i];
    for (size_t r = 0u; r < g->len; r++) {
        const AspGroundStatement *s = &g->statements[r];
        Atom *pos = NULL, *neg = NULL;
        if (!asp_ground_bodies(g, s, &pos, &neg))
            return NULL;
        if (!s->choice && s->head_len > 1u) {
            if (s->weight) {
                g->refused = "a disjunction with a weight body";
                return NULL;
            }
            Atom **heads = arena_alloc(g->arena, sizeof(*heads) * s->head_len);
            if (!heads)
                return NULL;
            for (size_t h = 0u; h < s->head_len; h++)
                if (!(heads[h] = asp_ground_atom(g, g->heads[s->head_at + h])))
                    return NULL;
            Atom *parts[4] = {atom_symbol(g->arena, "disjunction"),
                              atom_expr(g->arena, heads, (CettaExprLen)s->head_len),
                              pos, neg};
            items[len++] = parts[1] ? atom_expr(g->arena, parts, 4u) : NULL;
            if (!items[len - 1u])
                return NULL;
            continue;
        }
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
    for (size_t m = 0u; m < g->minimize_len; m++) {
        /* (minimize PRIORITY ((A W) ...) ((N W) ...)): a negative literal's
         * atom among the second list */
        size_t n = g->minimize[m].len, at = g->minimize[m].at;
        Atom **pos = arena_alloc(g->arena, sizeof(*pos) * (n + 1u));
        Atom **neg = arena_alloc(g->arena, sizeof(*neg) * (n + 1u));
        if (!pos || !neg)
            return NULL;
        size_t np = 0u, nn = 0u;
        for (size_t i = 0u; i < n; i++) {
            int32_t literal = g->literals[at + i];
            Atom *atom = asp_ground_atom(g, (uint32_t)(literal < 0 ? -literal : literal));
            Atom *term = atom ? atom_expr2(g->arena, atom,
                                           atom_int(g->arena, (int64_t)g->weights[at + i]))
                              : NULL;
            if (!term)
                return NULL;
            if (literal < 0)
                neg[nn++] = term;
            else
                pos[np++] = term;
        }
        Atom *parts[4] = {atom_symbol(g->arena, "minimize"),
                          atom_int(g->arena, (int64_t)g->minimize[m].priority),
                          atom_expr(g->arena, pos, (CettaExprLen)np),
                          atom_expr(g->arena, neg, (CettaExprLen)nn)};
        items[len++] = parts[1] && parts[2] && parts[3] ? atom_expr(g->arena, parts, 4u) : NULL;
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
                                asp_ground_output, asp_ground_unsupported,
                                asp_ground_minimize};
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
    free(g.minimize);
    free(g.scratch);
    return program;
}

/* clingo's models as they arrive: all of them, or the first `limit`. */
typedef struct {
    Arena *arena;
    const AspAtomTable *atoms;
    size_t limit;
    Atom **models;
    size_t len, cap;
} AspClingoCollect;

static AspClingoNext asp_clingo_kept(AspClingoCollect *c, Atom *model) {
    if (!model)
        return ASP_CLINGO_FAIL;
    c->models[c->len++] = model;
    return c->limit && c->len >= c->limit ? ASP_CLINGO_STOP : ASP_CLINGO_MORE;
}

/* One clingo model: the program's atoms it holds, in table order. */
static AspClingoNext asp_clingo_collect_model(void *context, const uint32_t *atoms,
                                              size_t len) {
    AspClingoCollect *c = context;
    Atom **items = arena_alloc(c->arena, sizeof(*items) * (len + 1u));
    if (!items || !asp_grow((void **)&c->models, &c->cap, c->len + 1u,
                            sizeof(*c->models)))
        return ASP_CLINGO_FAIL;
    size_t kept = 0u;
    for (size_t i = 0u; i < len; i++)
        if (!asp_auxiliary(c->atoms->items[atoms[i] - 1u]))
            items[kept++] = c->atoms->items[atoms[i] - 1u];
    return asp_clingo_kept(c, atom_expr(c->arena, items, (CettaExprLen)kept));
}

/* One clingo model of a program text: its shown symbols as terms. */
static AspClingoNext asp_clingo_collect_symbols(void *context,
                                                const AspClingoSymbol *symbols,
                                                size_t len) {
    AspClingoCollect *c = context;
    Atom **items = arena_alloc(c->arena, sizeof(*items) * (len + 1u));
    if (!items || !asp_grow((void **)&c->models, &c->cap, c->len + 1u,
                            sizeof(*c->models)))
        return ASP_CLINGO_FAIL;
    for (size_t i = 0u; i < len; i++)
        if (!(items[i] = asp_symbol_atom(c->arena, symbols[i], 0u)))
            return ASP_CLINGO_FAIL;
    return asp_clingo_kept(c, atom_expr(c->arena, items, (CettaExprLen)len));
}

/* The clingo library answers in the asp library's shapes, with clingo as
 * the evidence where the asp library gives its certificate:
 * (models ((A ...) ...) (reference clingo)) for every model, and
 * (model (A ...)) or (unsat (reference clingo)) for one. */
static Atom *asp_clingo_answer(Arena *a, const AspClingoCollect *c, bool solve) {
    Atom *evidence = atom_expr2(a, atom_symbol(a, "reference"),
                                atom_symbol(a, "clingo"));
    if (!evidence)
        return NULL;
    if (solve)
        return c->len ? atom_expr2(a, atom_symbol(a, "model"), c->models[0])
                      : atom_expr2(a, atom_symbol(a, "unsat"), evidence);
    Atom *list = atom_expr(a, c->models, (CettaExprLen)c->len);
    return list ? atom_expr3(a, atom_symbol(a, "models"), list, evidence) : NULL;
}

/* clingo's stable models of a program text, by its shown atoms. */
static Atom *asp_clingo_text_value(Arena *a, Atom *text, bool solve,
                                   const char **error) {
    AspClingoCollect collect = {.arena = a, .limit = solve ? 1u : 0u};
    Atom *result = NULL;
    if (asp_clingo_text_models(text->ground.sval, asp_clingo_collect_symbols,
                               &collect, error)) {
        result = asp_clingo_answer(a, &collect, solve);
    } else if (!*error) {
        *error = "out of memory";
    }
    free(collect.models);
    return result;
}

/* clingo's stable models of program data: rules, choices and constraints
 * given to its solver as such, over the program's atoms in order of first
 * appearance. */
static Atom *asp_clingo_models_value(Arena *a, Atom *value, bool solve,
                                     const char **error) {
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
    AspClingoCollect collect = {.arena = a, .atoms = &atoms,
                                .limit = solve ? 1u : 0u};
    if (!asp_clingo_models(atoms.len, rules, n, asp_clingo_collect_model,
                           &collect, error)) {
        if (!*error)
            *error = "out of memory";
        free(collect.models);
        goto done;
    }
    result = asp_clingo_answer(a, &collect, solve);
    free(collect.models);
done:
    free(rules);
    free(store);
    asp_atom_table_free(&atoms);
    return result;
}

/* ---- distinct values: Hall's condition (native/asp/asp_hall_v1.h) --------
 * (asp:distinct ((KEY V ...) ...)) gives every key one of its open values,
 * no value twice, or names keys that between them have fewer open values
 * than keys.  The solver keeps its at-least-one constraints apart from its
 * at-most-one cliques with the same matching. */

/* The groups' values, numbered in order of first appearance, found by an
 * open-addressed index of their hashes (slot: number + 1, 0 free). */
typedef struct {
    Atom **items;
    uint32_t len, cap;
    uint32_t *slots;
    uint32_t slot_cap;
} AspValues;

static void asp_values_free(AspValues *t) {
    free(t->items);
    free(t->slots);
}

static bool asp_values_grow(AspValues *t) {
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

static int64_t asp_value_id(AspValues *t, Atom *a) {
    if ((t->len + 1u) * 2u > t->slot_cap && !asp_values_grow(t))
        return -1;
    uint32_t at = atom_hash(a) & (t->slot_cap - 1u);
    for (; t->slots[at]; at = (at + 1u) & (t->slot_cap - 1u))
        if (atom_eq(t->items[t->slots[at] - 1u], a))
            return (int64_t)(t->slots[at] - 1u);
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
    return (int64_t)t->len++;
}

/* Groups are (KEY V ...) with ground keys and values; anything else is not
 * this library's to answer. */
static bool asp_groups_shape(Atom *groups, uint32_t *nitems) {
    if (!groups || groups->kind != ATOM_EXPR)
        return false;
    uint64_t n = 0u;
    for (CettaExprIndex g = 0u; g < groups->expr.len; g++) {
        Atom *group = groups->expr.elems[g];
        if (group->kind != ATOM_EXPR || group->expr.len < 1u ||
            atom_has_vars(group))
            return false;
        n += group->expr.len - 1u;
    }
    if (n >= UINT32_MAX || groups->expr.len >= UINT32_MAX)
        return false;
    *nitems = (uint32_t)n;
    return true;
}

static Atom *asp_distinct_value(Arena *a, Atom *groups, uint32_t nitems) {
    uint32_t ngroups = (uint32_t)groups->expr.len;
    uint32_t *group = malloc(sizeof(*group) * (nitems ? nitems : 1u));
    uint32_t *clique = malloc(sizeof(*clique) * (nitems ? nitems : 1u));
    AspValues values = {0};
    AspHall *h = NULL;
    Atom *answer = NULL;
    bool ok = group && clique;
    uint32_t i = 0u;
    for (uint32_t g = 0u; ok && g < ngroups; g++) {
        Atom *row = groups->expr.elems[g];
        for (CettaExprIndex k = 1u; ok && k < row->expr.len; k++) {
            int64_t v = asp_value_id(&values, row->expr.elems[k]);
            ok = v >= 0;
            group[i] = g;
            clique[i++] = (uint32_t)v;
        }
    }
    if (ok) {
        AspHallShape shape = {nitems, group, ngroups, clique, values.len};
        h = asp_hall_new(&shape);
        ok = h != NULL;
    }
    AspHallWhy why;
    if (ok && asp_hall_feasible(h, &why)) {
        Atom **pairs = arena_alloc(a, sizeof(*pairs) * (ngroups ? ngroups : 1u));
        for (uint32_t g = 0u; pairs && g < ngroups; g++) {
            uint32_t c = asp_hall_match(h, g);
            Atom *key = groups->expr.elems[g]->expr.elems[0];
            pairs[g] = atom_expr2(a, atom_deep_copy(a, key),
                                  atom_deep_copy(a, values.items[c]));
        }
        Atom *sdr = pairs ? atom_expr(a, pairs, ngroups) : NULL;
        answer = sdr ? atom_expr2(a, atom_symbol(a, "sdr"), sdr) : NULL;
    } else if (ok) {
        Atom **keys = arena_alloc(a, sizeof(*keys) * (why.ngroups ? why.ngroups : 1u));
        Atom **vals = arena_alloc(a, sizeof(*vals) * (why.ncliques ? why.ncliques : 1u));
        for (uint32_t k = 0u; keys && k < why.ngroups; k++)
            keys[k] = atom_deep_copy(a, groups->expr.elems[why.groups[k]]->expr.elems[0]);
        for (uint32_t k = 0u; vals && k < why.ncliques; k++)
            vals[k] = atom_deep_copy(a, values.items[why.cliques[k]]);
        if (keys && vals) {
            Atom *parts[3] = {atom_symbol(a, "hall"),
                              atom_expr(a, keys, why.ngroups),
                              atom_expr(a, vals, why.ncliques)};
            answer = atom_expr(a, parts, 3u);
        }
    }
    asp_hall_free(h);
    asp_values_free(&values);
    free(group);
    free(clique);
    return answer;
}

static bool asp_distinct_call(Arena *arena, Atom *head, Atom **args,
                              uint32_t nargs, CettaCallOutcome *out) {
    uint32_t nitems = 0u;
    if (nargs != 1u || !asp_groups_shape(args[0], &nitems))
        return false;
    Atom *value = asp_distinct_value(arena, args[0], nitems);
    if (!value) {
        Atom *call = atom_expr2(arena, head, args[0]);
        value = atom_error(arena, call, atom_string(arena, "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}

bool cetta_asp_call(struct CettaLibraryContext *context, Arena *arena,
                    Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    if (head->sym_id == g_builtin_syms.lib_asp_distinct)
        return asp_distinct_call(arena, head, args, nargs, out);
    if (asp_session_call(context, arena, head, args, nargs, out))
        return true;
    bool solve = head->sym_id == g_builtin_syms.lib_asp_solve;
    bool models = head->sym_id == g_builtin_syms.lib_asp_models;
    bool trace = head->sym_id == g_builtin_syms.lib_asp_trace;
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
    if (!error && !trace && asp_has_minimize(args[0]))
        value = asp_optimize_value(arena, args[0], !solve, &error);
    else if (!error)
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

/* The clingo library: clingo's grounder, and its solver behind the asp
 * library's answer shapes.  (clingo:ground TEXT) grounds a program text into
 * the asp library's statements; (clingo:models P) and (clingo:solve P) answer
 * as asp:models and asp:solve do, for program data or a program text, with
 * (reference clingo) where the asp library gives its certificate.  Without
 * clingo loadable its heads are not this library's to answer. */
bool cetta_clingo_result_long_lived(SymbolId head) {
    return head == g_builtin_syms.lib_clingo_ground;
}

bool cetta_clingo_call(struct CettaLibraryContext *context, Arena *arena,
                       Atom *head, Atom **args, uint32_t nargs,
                       CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    bool ground = head->sym_id == g_builtin_syms.lib_clingo_ground;
    bool models = head->sym_id == g_builtin_syms.lib_clingo_models;
    bool solve = head->sym_id == g_builtin_syms.lib_clingo_solve;
    if (!(ground || models || solve) || !asp_clingo_available())
        return false;
    const char *error = NULL;
    Atom *value = NULL;
    if (nargs != 1u) {
        error = ground ? "expected one program text" : "expected one program";
    } else if (ground) {
        value = asp_ground_value(arena, args[0], &error);
    } else if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_STRING) {
        value = asp_clingo_text_value(arena, args[0], solve, &error);
    } else {
        value = asp_clingo_models_value(arena, args[0], solve, &error);
    }
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

/* ---- the sat library: clauses over atoms, on the same kernel ------------------
 * (sat:solve CLAUSES): CLAUSES a list of (clause (P ...) (N ...)), each holding
 * when some atom P is true or some atom N false, over ground atoms.
 * (model (A ...)), the atoms true in a model in order of first occurrence
 * (every other atom false), or (unsat (certificate STEP ...)): RUP steps over
 * the atoms numbered from 1 in order of first occurrence (a literal i or -i)
 * and the clauses named from 1, which is an LRAT proof.
 * (sat:models CLAUSES): (models ((A ...) ...) (certificate STEP ...)), every
 * model once and the refutation of the clauses with each found model's
 * assignment blocked by one more clause, named after them in the order found. */

typedef struct {
    AspAtomTable atoms;
    int32_t *lits;
    AspClause *clauses;
    uint32_t nclauses;
} SatCnf;

static void sat_cnf_free(SatCnf *f) {
    asp_atom_table_free(&f->atoms);
    free(f->lits);
    free(f->clauses);
}

/* a clause (clause (P ...) (N ...)): some P holds or some N does not */
static bool sat_clause_shape(Atom *c) {
    return c->kind == ATOM_EXPR && c->expr.len == 3u &&
        atom_is_symbol(c->expr.elems[0], "clause") &&
        c->expr.elems[1]->kind == ATOM_EXPR && c->expr.elems[2]->kind == ATOM_EXPR;
}

static const char *sat_read(Atom *value, SatCnf *f) {
    memset(f, 0, sizeof(*f));
    if (!value || value->kind != ATOM_EXPR)
        return "the clauses are a list of clauses";
    size_t total = 0u;
    for (uint32_t i = 0u; i < value->expr.len; i++) {
        Atom *c = value->expr.elems[i];
        if (!sat_clause_shape(c))
            return "a clause is (clause (P ...) (N ...))";
        if (atom_has_vars(c))
            return "a clause must be ground";
        total += c->expr.elems[1]->expr.len + c->expr.elems[2]->expr.len;
    }
    f->nclauses = (uint32_t)value->expr.len;
    f->lits = malloc(sizeof(*f->lits) * (total ? total : 1u));
    f->clauses = malloc(sizeof(*f->clauses) * (f->nclauses ? f->nclauses : 1u));
    if (!f->lits || !f->clauses)
        return "out of memory";
    size_t at = 0u;
    for (uint32_t i = 0u; i < f->nclauses; i++) {
        Atom *c = value->expr.elems[i];
        size_t first = at;
        for (uint32_t side = 1u; side <= 2u; side++) {
            Atom *list = c->expr.elems[side];
            for (uint32_t k = 0u; k < list->expr.len; k++) {
                int32_t id = asp_intern(&f->atoms, list->expr.elems[k]);
                if (id < 0)
                    return "out of memory";
                f->lits[at++] = side == 1u ? id + 1 : -(id + 1);
            }
        }
        f->clauses[i] = (AspClause){f->lits + first, (uint32_t)(at - first)};
    }
    return NULL;
}

static Atom *sat_model_atoms(Arena *a, const SatCnf *f, const int32_t *vars, uint32_t n) {
    Atom **items = arena_alloc(a, sizeof(*items) * (n ? n : 1u));
    if (!items)
        return NULL;
    for (uint32_t i = 0u; i < n; i++)
        items[i] = f->atoms.items[vars[i] - 1];
    return atom_expr(a, items, n);
}

static Atom *sat_solve_value(Arena *a, const SatCnf *f, const char **error) {
    AspAnswer answer = asp_sat_solve(f->atoms.len, f->clauses, f->nclauses);
    Atom *result = NULL;
    if (answer.kind == ASP_ANSWER_SAT) {
        Atom *model = sat_model_atoms(a, f, answer.model, answer.model_len);
        Atom *head = atom_symbol(a, "model");
        result = model && head ? atom_expr2(a, head, model) : NULL;
    } else if (answer.kind == ASP_ANSWER_UNSAT) {
        Atom *cert = asp_certificate(a, &answer);
        Atom *head = atom_symbol(a, "unsat");
        result = cert && head ? atom_expr2(a, head, cert) : NULL;
    } else {
        *error = answer.kind == ASP_ANSWER_REFUSED
            ? "the solver's answer failed its check" : "the solver did not finish";
    }
    asp_answer_free(&answer);
    return result;
}

static Atom *sat_models_value(Arena *a, SatCnf *f, const char **error) {
    uint32_t n = f->atoms.len;
    uint32_t cap = f->nclauses + 16u;
    AspClause *clauses = malloc(sizeof(*clauses) * cap);
    Atom **models = NULL;
    uint32_t nmodels = 0u, capmodels = 0u;
    int32_t **blocks = NULL;
    Atom *result = NULL;
    if (!clauses)
        return NULL;
    memcpy(clauses, f->clauses, sizeof(*clauses) * f->nclauses);
    uint32_t m = f->nclauses;
    for (;;) {
        AspAnswer answer = asp_sat_solve(n, clauses, m);
        if (answer.kind == ASP_ANSWER_UNSAT) {
            Atom *cert = asp_certificate(a, &answer);
            Atom *list = atom_expr(a, models, nmodels);
            Atom *parts[3] = {atom_symbol(a, "models"), list, cert};
            result = cert && list && parts[0] ? atom_expr(a, parts, 3u) : NULL;
            asp_answer_free(&answer);
            break;
        }
        if (answer.kind != ASP_ANSWER_SAT) {
            *error = answer.kind == ASP_ANSWER_REFUSED
                ? "the solver's answer failed its check" : "the solver did not finish";
            asp_answer_free(&answer);
            break;
        }
        /* the model, and the clause that every later model differs from it */
        Atom *model = sat_model_atoms(a, f, answer.model, answer.model_len);
        int32_t *block = malloc(sizeof(*block) * (n ? n : 1u));
        uint8_t *val = calloc(n + 1u, 1u);
        bool grown = true;
        if (nmodels == capmodels) {
            capmodels = capmodels ? 2u * capmodels : 8u;
            Atom **gm = realloc(models, sizeof(*gm) * capmodels);
            int32_t **gb = realloc(blocks, sizeof(*gb) * capmodels);
            if (gm)
                models = gm;
            if (gb)
                blocks = gb;
            grown = gm && gb;
        }
        if (m == cap) {
            cap *= 2u;
            AspClause *gc = realloc(clauses, sizeof(*gc) * cap);
            if (gc)
                clauses = gc;
            grown = grown && gc;
        }
        if (!model || !block || !val || !grown) {
            free(block);
            free(val);
            asp_answer_free(&answer);
            break;
        }
        for (uint32_t k = 0u; k < answer.model_len; k++)
            val[answer.model[k]] = 1u;
        for (uint32_t v = 1u; v <= n; v++)
            block[v - 1u] = val[v] ? -(int32_t)v : (int32_t)v;
        free(val);
        models[nmodels] = model;
        blocks[nmodels++] = block;
        clauses[m++] = (AspClause){block, n};
        asp_answer_free(&answer);
    }
    for (uint32_t k = 0u; k < nmodels; k++)
        free(blocks[k]);
    free(blocks);
    free(models);
    free(clauses);
    return result;
}

bool cetta_sat_call(struct CettaLibraryContext *context, Arena *arena,
                    Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    bool solve = head->sym_id == g_builtin_syms.lib_sat_solve;
    bool models = head->sym_id == g_builtin_syms.lib_sat_models;
    if (!solve && !models)
        return false;
    const char *error = NULL;
    Atom *value = NULL;
    SatCnf cnf;
    if (nargs != 1u) {
        error = "expected a list of clauses";
        memset(&cnf, 0, sizeof(cnf));
    } else {
        error = sat_read(args[0], &cnf);
    }
    if (!error)
        value = solve ? sat_solve_value(arena, &cnf, &error)
                      : sat_models_value(arena, &cnf, &error);
    sat_cnf_free(&cnf);
    if (!value) {
        Atom *call = atom_expr(arena, (Atom *[]){head, nargs ? args[0] : head}, nargs ? 2u : 1u);
        value = atom_error(arena, call, atom_string(arena, error ? error : "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}

/* ---- the smt library: equality with uninterpreted functions ------------------
 * (smt:solve CLAUSES), the clauses as the sat library reads them, over atoms
 * of three kinds: (eq S T), the equality of the ground terms S and T; an
 * application (p T ...) of a predicate, which holds exactly when that term
 * equals the reserved truth term (so congruence reaches predicates); and any
 * other atom, a proposition.  A term is a symbol, a value, or (f T ...).
 * (model (A ...)) for a model, its true atoms in order of first occurrence,
 * or (unsat (certificate STEP ...)): RUP steps over the clauses and
 * (euf ID (LIT ...)) steps, clauses congruence closure entails. */

typedef struct {
    SatCnf cnf;
    AspAtomTable symbols;     /* symbols and values, by index */
    AspAtomTable term_atoms;  /* terms by their atom */
    AspSmtTerm *terms;
    uint32_t nterms, capterms;
    uint32_t *arg_store;
    size_t nargs, capargs;
    AspSmtEquality *eqs;
    uint32_t neqs;
    uint32_t truth;           /* the truth term */
    /* linear real arithmetic: the reals by their atom, the bound atoms (each
     * owning its arrays), an error's reason */
    AspAtomTable reals;
    AspLraAtom *lra;
    uint32_t nlra, caplra;
    const char *lra_error;
} SmtProblem;

static void smt_problem_free(SmtProblem *p) {
    sat_cnf_free(&p->cnf);
    asp_atom_table_free(&p->symbols);
    asp_atom_table_free(&p->term_atoms);
    free(p->terms);
    free(p->arg_store);
    free(p->eqs);
    asp_atom_table_free(&p->reals);
    for (uint32_t i = 0u; i < p->nlra; i++)
        lra_parts_free((uint32_t *)p->lra[i].reals, (char **)p->lra[i].coefs,
                       p->lra[i].n, (char *)p->lra[i].bound);
    free(p->lra);
}

/* ---- linear real arithmetic (shared by the clause form and SMT-LIB) ---- */

/* Exact text of a number atom: an integer, a big integer or a rational. */
static char *smt_number_text(Atom *a) {
    if (!a || a->kind != ATOM_GROUNDED)
        return NULL;
    if (a->ground.gkind == GV_INT) {
        char buf[32];
        snprintf(buf, sizeof buf, "%lld", (long long)a->ground.ival);
        return lra_decimal(buf);
    }
    if (a->ground.gkind == GV_BIGINT)
        return lra_decimal(atom_bigint_cstr(a));
    if (a->ground.gkind == GV_RATIONAL)
        return lra_decimal(atom_rational_cstr(a));
    return NULL;
}

/* A number as a value: an integer, or (div N D). */
static Atom *smt_integer_atom(Arena *a, const char *text) {
    char *end = NULL;
    errno = 0;
    long long v = strtoll(text, &end, 10);
    if (!errno && end && *end == '\0')
        return atom_int(a, (int64_t)v);
    return atom_bigint(a, text);
}

static Atom *smt_number_atom(Arena *a, const char *text) {
    const char *slash = strchr(text, '/');
    if (!slash)
        return smt_integer_atom(a, text);
    size_t nlen = (size_t)(slash - text);
    char *num = malloc(nlen + 1u);
    if (!num)
        return NULL;
    memcpy(num, text, nlen);
    num[nlen] = '\0';
    Atom *parts[3] = {atom_symbol(a, "div"), smt_integer_atom(a, num),
                      smt_integer_atom(a, slash + 1)};
    free(num);
    return parts[0] && parts[1] && parts[2] ? atom_expr(a, parts, 3u) : NULL;
}

/* The bound atom `var`: diff REL 0, diff = sum c_i x_i + k, kept as
 * sum c_i x_i REL -k. */
static bool smt_lra_add(SmtProblem *p, uint32_t var, const LraLinear *diff,
                        AspLraRelation rel) {
    uint32_t *vars = NULL, n = 0u;
    char **coefs = NULL, *constant = NULL;
    if (!lra_linear_parts(diff, &vars, &coefs, &n, &constant))
        return false;
    char *bound = lra_negation(constant);
    free(constant);
    if (bound && p->nlra == p->caplra) {
        uint32_t cap = p->caplra ? 2u * p->caplra : 64u;
        AspLraAtom *grown = realloc(p->lra, sizeof(*grown) * cap);
        if (grown) {
            p->lra = grown;
            p->caplra = cap;
        }
    }
    if (!bound || p->nlra == p->caplra) {
        lra_parts_free(vars, coefs, n, bound);
        return false;
    }
    p->lra[p->nlra++] = (AspLraAtom){var, vars, (const char *const *)coefs, n, rel, bound};
    return true;
}

/* The term of `atom`, its arguments made first; UINT32_MAX when it is no
 * ground term or memory runs out. */
static uint32_t smt_term(SmtProblem *p, Atom *atom, uint32_t depth) {
    if (depth > 4096u || atom_has_vars(atom))
        return UINT32_MAX;
    int32_t known = asp_atom_find(&p->term_atoms, atom);
    if (known >= 0)
        return (uint32_t)known < p->nterms ? (uint32_t)known : UINT32_MAX;
    uint32_t args[64];
    uint32_t nargs = 0u;
    Atom *head = atom;
    if (atom->kind == ATOM_EXPR) {
        if (atom->expr.len == 0u || atom->expr.len - 1u > 64u)
            return UINT32_MAX;
        head = atom->expr.elems[0];
        for (CettaExprIndex i = 1u; i < atom->expr.len; i++) {
            args[nargs] = smt_term(p, atom->expr.elems[i], depth + 1u);
            if (args[nargs++] == UINT32_MAX)
                return UINT32_MAX;
        }
    }
    if (head->kind == ATOM_EXPR)
        return UINT32_MAX;
    int32_t symbol = asp_intern(&p->symbols, head);
    if (symbol < 0)
        return UINT32_MAX;
    if (p->nargs + nargs > p->capargs) {
        size_t cap = p->capargs ? 2u * p->capargs : 256u;
        while (cap < p->nargs + nargs)
            cap *= 2u;
        uint32_t *grown = realloc(p->arg_store, sizeof(*grown) * cap);
        if (!grown)
            return UINT32_MAX;
        /* earlier terms' argument arrays move with the store */
        for (uint32_t t = 0u; t < p->nterms; t++)
            if (p->terms[t].nargs)
                p->terms[t].args = grown + (p->terms[t].args - p->arg_store);
        p->arg_store = grown;
        p->capargs = cap;
    }
    if (p->nterms == p->capterms) {
        uint32_t cap = p->capterms ? 2u * p->capterms : 64u;
        AspSmtTerm *grown = realloc(p->terms, sizeof(*grown) * cap);
        if (!grown)
            return UINT32_MAX;
        p->terms = grown;
        p->capterms = cap;
    }
    uint32_t *store = p->arg_store + p->nargs;
    if (nargs)
        memcpy(store, args, sizeof(*args) * nargs);
    p->nargs += nargs;
    uint32_t t = p->nterms++;
    p->terms[t] = (AspSmtTerm){(uint32_t)symbol, nargs ? store : NULL, nargs};
    if (asp_intern(&p->term_atoms, atom) != (int32_t)t)
        return UINT32_MAX;
    return t;
}

static bool smtt_head_is(Atom *a, const char *head) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1u &&
           atom_is_symbol(a->expr.elems[0], head);
}

static bool smt_is_eq(Atom *a) {
    return a->kind == ATOM_EXPR && a->expr.len == 3u &&
           atom_is_symbol(a->expr.elems[0], "eq");
}

/* (le A B), (lt A B), (ge A B) or (gt A B): its relation */
static bool smt_bound_relation(Atom *a, AspLraRelation *rel) {
    if (a->kind != ATOM_EXPR || a->expr.len != 3u)
        return false;
    static const char *const names[] = {"le", "lt", "ge", "gt"};
    for (unsigned i = 0u; i < 4u; i++)
        if (atom_is_symbol(a->expr.elems[0], names[i])) {
            *rel = (AspLraRelation)i;
            return true;
        }
    return false;
}

/* The product of two forms, one of them constant; NULL for a nonlinear
 * product.  Consumes neither. */
static LraLinear *smt_linear_product(const LraLinear *x, const LraLinear *y) {
    char *c = NULL;
    const LraLinear *other = y;
    if (!lra_linear_is_constant(x, &c)) {
        if (!lra_linear_is_constant(y, &c))
            return NULL;
        other = x;
    }
    LraLinear *r = lra_linear_new();
    bool ok = r && lra_linear_add_scaled(r, other, c);
    free(c);
    if (!ok) {
        lra_linear_free(r);
        return NULL;
    }
    return r;
}

/* An arithmetic term of the clause form as an exact linear form: a number,
 * a real (a symbol), or (add T ...), (sub T ...), (mul T ...) with at most
 * one factor not constant, (div T C) with C a nonzero constant. */
static LraLinear *smt_linear(SmtProblem *p, Atom *e, uint32_t depth) {
    if (depth > 4096u) {
        p->lra_error = "an arithmetic term nests too deeply";
        return NULL;
    }
    LraLinear *r = lra_linear_new();
    if (!r)
        return NULL;
    if (e->kind == ATOM_GROUNDED) {
        char *text = smt_number_text(e);
        bool ok = text && lra_linear_add_constant(r, text);
        free(text);
        if (!ok) {
            p->lra_error = e->ground.gkind == GV_FLOAT
                ? "a float is no exact real: write integers and (div N D)"
                : "an arithmetic term's value is not a number";
            lra_linear_free(r);
            return NULL;
        }
        return r;
    }
    if (e->kind == ATOM_SYMBOL) {
        int32_t x = asp_intern(&p->reals, e);
        if (x < 0 || !lra_linear_add_term(r, (uint32_t)x, "1")) {
            lra_linear_free(r);
            return NULL;
        }
        return r;
    }
    bool add = smtt_head_is(e, "add"), sub = smtt_head_is(e, "sub");
    bool mul = smtt_head_is(e, "mul"), div = smtt_head_is(e, "div");
    uint32_t n = e->kind == ATOM_EXPR ? (uint32_t)e->expr.len - 1u : 0u;
    if (!(add || sub || mul || div) || n == 0u || (div && n != 2u)) {
        p->lra_error = "an arithmetic term is a number, a real, or add, sub, mul or div of terms";
        lra_linear_free(r);
        return NULL;
    }
    LraLinear *acc = smt_linear(p, e->expr.elems[1], depth + 1u);
    bool ok = acc != NULL;
    if (ok && sub && n == 1u)
        ok = lra_linear_add_scaled(r, acc, "-1");
    else if (ok)
        ok = lra_linear_add_scaled(r, acc, "1");
    lra_linear_free(acc);
    for (uint32_t i = 2u; ok && i <= n; i++) {
        LraLinear *x = smt_linear(p, e->expr.elems[i], depth + 1u);
        ok = x != NULL;
        if (ok && (add || sub)) {
            ok = lra_linear_add_scaled(r, x, add ? "1" : "-1");
        } else if (ok && mul) {
            LraLinear *prod = smt_linear_product(r, x);
            ok = prod != NULL;
            if (!ok)
                p->lra_error = "a nonlinear product";
            lra_linear_free(r);
            r = prod;
        } else if (ok) {
            char *c = NULL, *inv = NULL;
            ok = lra_linear_is_constant(x, &c) && (inv = lra_reciprocal(c)) != NULL;
            if (!ok)
                p->lra_error = "a division by a term that is not a nonzero constant";
            LraLinear *q = ok ? lra_linear_new() : NULL;
            ok = ok && q && lra_linear_add_scaled(q, r, inv);
            free(c);
            free(inv);
            lra_linear_free(r);
            r = q;
        }
        lra_linear_free(x);
    }
    if (!ok) {
        lra_linear_free(r);
        return NULL;
    }
    return r;
}

/* No real is also an uninterpreted term's symbol: the theories share none. */
static bool smt_reals_apart(SmtProblem *p) {
    for (uint32_t i = 0u; i < p->reals.len; i++)
        if (p->reals.items[i]->kind == ATOM_SYMBOL &&
            asp_atom_find(&p->symbols, p->reals.items[i]) >= 0) {
            p->lra_error = "a real is also an uninterpreted term: the theories share no symbol";
            return false;
        }
    return true;
}

static const char *smt_read(Arena *arena, Atom *value, SmtProblem *p) {
    memset(p, 0, sizeof(*p));
    const char *error = sat_read(value, &p->cnf);
    if (error)
        return error;
    /* the truth term first, a symbol no program can spell as a term of its
     * own: the reserved constant smt:true */
    p->truth = smt_term(p, atom_symbol(arena, "smt:true"), 0u);
    if (p->truth == UINT32_MAX)
        return "out of memory";
    p->eqs = malloc(sizeof(*p->eqs) * (p->cnf.atoms.len ? p->cnf.atoms.len : 1u));
    if (!p->eqs)
        return "out of memory";
    for (uint32_t i = 0u; i < p->cnf.atoms.len; i++) {
        Atom *a = p->cnf.atoms.items[i];
        uint32_t lhs, rhs;
        AspLraRelation rel;
        if (smt_bound_relation(a, &rel)) {
            LraLinear *x = smt_linear(p, a->expr.elems[1], 0u);
            LraLinear *y = x ? smt_linear(p, a->expr.elems[2], 0u) : NULL;
            bool ok = y && lra_linear_add_scaled(x, y, "-1") &&
                      smt_lra_add(p, i + 1u, x, rel);
            lra_linear_free(x);
            lra_linear_free(y);
            if (!ok)
                return p->lra_error ? p->lra_error : "out of memory";
            continue;
        }
        if (smt_is_eq(a)) {
            lhs = smt_term(p, a->expr.elems[1], 0u);
            rhs = smt_term(p, a->expr.elems[2], 0u);
        } else if (a->kind == ATOM_EXPR && a->expr.len >= 2u &&
                   a->expr.elems[0]->kind != ATOM_EXPR) {
            lhs = smt_term(p, a, 0u);
            rhs = p->truth;
        } else {
            continue;   /* a proposition */
        }
        if (lhs == UINT32_MAX || rhs == UINT32_MAX)
            return "an equality's sides and a predicate's arguments are ground terms";
        p->eqs[p->neqs++] = (AspSmtEquality){i + 1u, lhs, rhs};
    }
    return smt_reals_apart(p) ? NULL : p->lra_error;
}

static Atom *smt_solve_value(Arena *a, const SmtProblem *p, const char **error) {
    AspSmtTheories theories = {p->terms, p->nterms, p->eqs, p->neqs,
                               p->reals.len, p->lra, p->nlra};
    AspAnswer answer = asp_smt_solve(p->cnf.atoms.len, p->cnf.clauses,
                                     p->cnf.nclauses, &theories);
    Atom *result = NULL;
    if (answer.kind == ASP_ANSWER_SAT) {
        Atom *model = sat_model_atoms(a, &p->cnf, answer.model, answer.model_len);
        Atom *head = atom_symbol(a, "model");
        result = model && head ? atom_expr2(a, head, model) : NULL;
        /* with reals: (model (A ...) ((X V) ...)) */
        if (result && p->reals.len) {
            Atom **values = arena_alloc(a, sizeof(*values) * p->reals.len);
            bool ok = values && answer.reals_len == p->reals.len;
            for (uint32_t i = 0u; ok && i < p->reals.len; i++) {
                Atom *v = smt_number_atom(a, answer.reals[i]);
                values[i] = v ? atom_expr2(a, p->reals.items[i], v) : NULL;
                ok = values[i] != NULL;
            }
            Atom *list = ok ? atom_expr(a, values, p->reals.len) : NULL;
            result = list ? atom_expr3(a, head, model, list) : NULL;
        }
    } else if (answer.kind == ASP_ANSWER_UNSAT) {
        Atom *cert = asp_certificate(a, &answer);
        Atom *head = atom_symbol(a, "unsat");
        result = cert && head ? atom_expr2(a, head, cert) : NULL;
    } else {
        *error = answer.kind == ASP_ANSWER_REFUSED
            ? "the solver's answer was refused by its own check"
            : "the solve did not finish";
    }
    asp_answer_free(&answer);
    return result;
}

/* ---- SMT-LIB text (QF_UF) -----------------------------------------------------
 * (smt:solve-text TEXT): an SMT-LIB script in the theory of equality with
 * uninterpreted functions, read by the lane's own reader (SMT-LIB is
 * s-expressions) and translated to clauses: declare-sort, declare-fun,
 * declare-const, define-fun (expanded where used), assert and check-sat; the
 * Boolean connectives not, and, or, =>, xor, ite, = and distinct, Boolean and
 * term ite, let and (! F ...) annotations.  A let binds a formula to its
 * literal and a term to its term, so nothing is expanded twice; each compound
 * formula is a fresh variable defined by its clauses (Tseitin).  sat or
 * (unsat (certificate STEP ...)). */

typedef struct {
    Atom *name;
    bool boolean;          /* returns Bool */
    uint32_t arity;
    Atom *params;          /* define-fun: ((x S) ...) */
    Atom *body;            /* define-fun: the body */
    bool real;             /* returns Real or Int */
    bool integer;          /* returns Int */
} SmtFun;

typedef struct {
    Atom *name;
    bool is_term;
    int32_t lit;           /* a formula's literal */
    uint32_t term;         /* a term's term */
    LraLinear *linear;     /* a real's linear form, else NULL */
} SmtBinding;

typedef struct {
    SmtProblem p;          /* terms, equalities; its cnf unused */
    SmtFun *funs;
    uint32_t nfuns, capfuns;
    SmtBinding *env;
    uint32_t nenv, capenv;
    uint32_t nvars;
    int32_t *lits;
    size_t nlits, caplits;
    uint32_t *clause_start;
    uint32_t nclauses, capclauses;
    int32_t truth_lit;     /* a variable held true */
    /* equality atoms by their sides */
    uint32_t *eq_lhs, *eq_rhs, *eq_var;
    uint32_t neq, capeq;
    /* the linear forms made, freed with the text; bound atoms by their
     * relation, form and bound (an open-addressed index of atom numbers) */
    LraLinear **linears;
    uint32_t nlinears, caplinears;
    uint32_t *lra_slots;
    uint32_t lra_slot_cap;
    uint32_t fresh_reals;
    /* QF_UFLRA: the terms both theories read, each an uninterpreted term and
     * a linear form (a real of its own, or a numeral's constant) */
    uint32_t *sh_term;
    LraLinear **sh_lin;
    uint32_t nsh, capsh;
    const char *error;
} SmtText;

static int32_t smtt_var(SmtText *t) {
    return (int32_t)++t->nvars;
}

static bool smtt_clause(SmtText *t, const int32_t *ls, uint32_t n) {
    if (t->nlits + n > t->caplits) {
        size_t cap = t->caplits ? 2u * t->caplits : 1024u;
        while (cap < t->nlits + n)
            cap *= 2u;
        int32_t *g = realloc(t->lits, sizeof(*g) * cap);
        if (!g)
            return false;
        t->lits = g;
        t->caplits = cap;
    }
    if (t->nclauses + 2u > t->capclauses) {
        uint32_t cap = t->capclauses ? 2u * t->capclauses : 256u;
        uint32_t *g = realloc(t->clause_start, sizeof(*g) * cap);
        if (!g)
            return false;
        t->clause_start = g;
        t->capclauses = cap;
    }
    t->clause_start[t->nclauses] = (uint32_t)t->nlits;
    memcpy(t->lits + t->nlits, ls, sizeof(*ls) * n);
    t->nlits += n;
    t->nclauses++;
    t->clause_start[t->nclauses] = (uint32_t)t->nlits;
    return true;
}

static const SmtFun *smtt_fun(const SmtText *t, Atom *name) {
    for (uint32_t i = t->nfuns; i-- > 0u;)
        if (atom_eq(t->funs[i].name, name))
            return &t->funs[i];
    return NULL;
}

static const SmtBinding *smtt_bound(const SmtText *t, Atom *name) {
    for (uint32_t i = t->nenv; i-- > 0u;)
        if (atom_eq(t->env[i].name, name))
            return &t->env[i];
    return NULL;
}

static bool smtt_bind(SmtText *t, SmtBinding b) {
    if (t->nenv == t->capenv) {
        uint32_t cap = t->capenv ? 2u * t->capenv : 64u;
        SmtBinding *g = realloc(t->env, sizeof(*g) * cap);
        if (!g)
            return false;
        t->env = g;
        t->capenv = cap;
    }
    t->env[t->nenv++] = b;
    return true;
}

/* A term both theories read: the term K with the linear form LIN. */
static bool smtt_share(SmtText *t, uint32_t k, LraLinear *lin) {
    for (uint32_t i = 0u; i < t->nsh; i++)
        if (t->sh_term[i] == k)
            return true;
    if (t->nsh == t->capsh) {
        uint32_t cap = t->capsh ? 2u * t->capsh : 16u;
        uint32_t *g = realloc(t->sh_term, sizeof(*g) * cap);
        if (!g)
            return false;
        t->sh_term = g;
        LraLinear **h = realloc(t->sh_lin, sizeof(*h) * cap);
        if (!h)
            return false;
        t->sh_lin = h;
        t->capsh = cap;
    }
    t->sh_term[t->nsh] = k;
    t->sh_lin[t->nsh++] = lin;
    return true;
}

static bool smtt_is(Atom *a, const char *head) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1u &&
           atom_is_symbol(a->expr.elems[0], head);
}

static int32_t smtt_formula(SmtText *t, Arena *arena, Atom *f, uint32_t depth);
static uint32_t smtt_termof(SmtText *t, Arena *arena, Atom *x, uint32_t depth);
static LraLinear *smtt_linear(SmtText *t, Arena *arena, Atom *x, uint32_t depth);
static bool smtt_is_real(SmtText *t, Atom *x);
static bool smtt_boolean(SmtText *t, Atom *x);

/* A form the text keeps until it is freed. */
static LraLinear *smtt_own(SmtText *t, LraLinear *lin) {
    if (!lin)
        return NULL;
    if (t->nlinears == t->caplinears) {
        uint32_t cap = t->caplinears ? 2u * t->caplinears : 64u;
        LraLinear **g = realloc(t->linears, sizeof(*g) * cap);
        if (!g) {
            lra_linear_free(lin);
            return NULL;
        }
        t->linears = g;
        t->caplinears = cap;
    }
    t->linears[t->nlinears++] = lin;
    return lin;
}

static uint64_t smtt_lra_hash(const AspLraAtom *at) {
    uint64_t h = 1469598103934665603ull ^ (uint64_t)at->rel;
    for (const char *c = at->bound; *c; c++)
        h = (h ^ (unsigned char)*c) * 1099511628211ull;
    for (uint32_t i = 0u; i < at->n; i++) {
        h = (h ^ at->reals[i]) * 1099511628211ull;
        for (const char *c = at->coefs[i]; *c; c++)
            h = (h ^ (unsigned char)*c) * 1099511628211ull;
    }
    return h;
}

static bool smtt_lra_same(const AspLraAtom *a, const AspLraAtom *b) {
    if (a->rel != b->rel || a->n != b->n || strcmp(a->bound, b->bound) != 0)
        return false;
    for (uint32_t i = 0u; i < a->n; i++)
        if (a->reals[i] != b->reals[i] || strcmp(a->coefs[i], b->coefs[i]) != 0)
            return false;
    return true;
}

/* The literal of diff REL 0: decided when diff is constant, else the bound
 * atom, made once. */
static int32_t smtt_bound_lit(SmtText *t, const LraLinear *diff, AspLraRelation rel) {
    char *c = NULL;
    if (lra_linear_is_constant(diff, &c)) {
        int sign = 0;
        bool ok = lra_compare(NULL, NULL, 0u, NULL, 0u, c, &sign);
        free(c);
        if (!ok)
            return 0;
        sign = -sign;   /* the sign of the constant itself */
        bool holds = rel == ASP_LRA_LE ? sign <= 0 : rel == ASP_LRA_LT ? sign < 0
                   : rel == ASP_LRA_GE ? sign >= 0 : sign > 0;
        return holds ? t->truth_lit : -t->truth_lit;
    }
    uint32_t before = t->p.nlra;
    if (!smt_lra_add(&t->p, 0u, diff, rel))
        return 0;
    AspLraAtom *fresh = &t->p.lra[before];
    if (t->p.nlra * 2u >= t->lra_slot_cap) {
        uint32_t cap = t->lra_slot_cap ? 2u * t->lra_slot_cap : 256u;
        uint32_t *slots = calloc(cap, sizeof(*slots));
        if (!slots)
            return 0;
        for (uint32_t i = 0u; i < before; i++) {
            uint32_t k = (uint32_t)smtt_lra_hash(&t->p.lra[i]) & (cap - 1u);
            while (slots[k])
                k = (k + 1u) & (cap - 1u);
            slots[k] = i + 1u;
        }
        free(t->lra_slots);
        t->lra_slots = slots;
        t->lra_slot_cap = cap;
    }
    uint32_t k = (uint32_t)smtt_lra_hash(fresh) & (t->lra_slot_cap - 1u);
    while (t->lra_slots[k]) {
        AspLraAtom *old = &t->p.lra[t->lra_slots[k] - 1u];
        if (smtt_lra_same(old, fresh)) {
            lra_parts_free((uint32_t *)fresh->reals, (char **)fresh->coefs, fresh->n,
                           (char *)fresh->bound);
            t->p.nlra = before;
            return (int32_t)old->var;
        }
        k = (k + 1u) & (t->lra_slot_cap - 1u);
    }
    t->lra_slots[k] = before + 1u;
    fresh->var = (uint32_t)smtt_var(t);
    return (int32_t)fresh->var;
}

/* a - b as a new form */
static LraLinear *smtt_difference(SmtText *t, const LraLinear *a, const LraLinear *b) {
    LraLinear *d = smtt_own(t, lra_linear_copy(a));
    return d && lra_linear_add_scaled(d, b, "-1") ? d : NULL;
}

/* The literal of a = b over reals: a - b <= 0 and a - b >= 0. */
static int32_t smtt_real_equality(SmtText *t, const LraLinear *a, const LraLinear *b);

/* The equality atom of terms a and b (either order), made once. */
static int32_t smtt_equality(SmtText *t, uint32_t a, uint32_t b) {
    if (a == b)
        return t->truth_lit;
    if (a > b) {
        uint32_t c = a;
        a = b;
        b = c;
    }
    for (uint32_t i = 0u; i < t->neq; i++)
        if (t->eq_lhs[i] == a && t->eq_rhs[i] == b)
            return (int32_t)t->eq_var[i];
    if (t->neq == t->capeq) {
        uint32_t cap = t->capeq ? 2u * t->capeq : 64u;
        uint32_t *l = realloc(t->eq_lhs, sizeof(*l) * cap);
        if (l)
            t->eq_lhs = l;
        uint32_t *r = realloc(t->eq_rhs, sizeof(*r) * cap);
        if (r)
            t->eq_rhs = r;
        uint32_t *v = realloc(t->eq_var, sizeof(*v) * cap);
        if (v)
            t->eq_var = v;
        if (!l || !r || !v)
            return 0;
        t->capeq = cap;
    }
    int32_t v = smtt_var(t);
    t->eq_lhs[t->neq] = a;
    t->eq_rhs[t->neq] = b;
    t->eq_var[t->neq++] = (uint32_t)v;
    return v;
}

/* A term with symbol `head` over the terms in args. */
static uint32_t smtt_app(SmtText *t, Arena *arena, Atom *head,
                         const uint32_t *args, uint32_t n) {
    /* a term as an atom of the problem's term table: the head over the
     * arguments' own atoms */
    Atom **parts = arena_alloc(arena, sizeof(*parts) * (n + 1u));
    if (!parts)
        return UINT32_MAX;
    parts[0] = head;
    for (uint32_t i = 0u; i < n; i++)
        parts[i + 1u] = t->p.term_atoms.items[args[i]];
    Atom *atom = n ? atom_expr(arena, parts, n + 1u) : head;
    return atom ? smt_term(&t->p, atom, 0u) : UINT32_MAX;
}

/* A define-fun's body with its parameters bound to the arguments. */
static bool smtt_enter_macro(SmtText *t, Arena *arena, const SmtFun *fn,
                             Atom *call, uint32_t depth, uint32_t *saved) {
    *saved = t->nenv;
    SmtBinding *values = malloc(sizeof(*values) * (fn->arity ? fn->arity : 1u));
    if (!values)
        return false;
    bool ok = true;
    for (uint32_t i = 0u; ok && i < fn->arity; i++) {
        Atom *param = fn->params->expr.elems[i];
        Atom *sort = param->expr.elems[1];
        Atom *arg = call->expr.elems[i + 1u];
        if (atom_is_symbol(sort, "Bool")) {
            int32_t l = smtt_formula(t, arena, arg, depth + 1u);
            ok = l != 0;
            values[i] = (SmtBinding){param->expr.elems[0], false, l, 0u, NULL};
        } else if (atom_is_symbol(sort, "Real") || atom_is_symbol(sort, "Int")) {
            LraLinear *lin = smtt_linear(t, arena, arg, depth + 1u);
            ok = lin != NULL;
            values[i] = (SmtBinding){param->expr.elems[0], false, 0, 0u, lin};
        } else {
            uint32_t x = smtt_termof(t, arena, arg, depth + 1u);
            ok = x != UINT32_MAX;
            values[i] = (SmtBinding){param->expr.elems[0], true, 0, x};
        }
    }
    for (uint32_t i = 0u; ok && i < fn->arity; i++)
        ok = smtt_bind(t, values[i]);
    free(values);
    return ok;
}

/* A fresh constant of both theories equal to the linear form LIN. */
static uint32_t smtt_purify(SmtText *t, Arena *arena, LraLinear *lin) {
    Atom *fresh = atom_expr2(arena, atom_symbol(arena, "smt:real"),
                             atom_int(arena, (int64_t)t->nvars + 1));
    uint32_t k = fresh ? smt_term(&t->p, fresh, 0u) : UINT32_MAX;
    int32_t v = k != UINT32_MAX ? asp_intern(&t->p.reals, fresh) : -1;
    smtt_var(t);   /* names the fresh constant apart */
    LraLinear *own = v >= 0 ? smtt_own(t, lra_linear_new()) : NULL;
    if (!own || !lra_linear_add_term(own, (uint32_t)v, "1"))
        return UINT32_MAX;
    int32_t eq = smtt_real_equality(t, own, lin);
    return eq && smtt_clause(t, &eq, 1u) && smtt_share(t, k, own) ? k : UINT32_MAX;
}

static uint32_t smtt_termof(SmtText *t, Arena *arena, Atom *x, uint32_t depth) {
    if (depth > 100000u)
        return UINT32_MAX;
    if (x->kind != ATOM_EXPR) {
        const SmtBinding *b = smtt_bound(t, x);
        if (b && b->is_term)
            return b->term;
        const SmtFun *fn = smtt_fun(t, x);
        if (fn && fn->body && fn->arity == 0u) {
            uint32_t saved = t->nenv;
            uint32_t r = smtt_termof(t, arena, fn->body, depth + 1u);
            t->nenv = saved;
            return r;
        }
        if ((b && b->linear) ||
            (x->kind == ATOM_GROUNDED &&
             (x->ground.gkind == GV_INT || x->ground.gkind == GV_BIGINT)) ||
            (fn && fn->real && fn->arity == 0u)) {
            /* QF_UFLRA: a real argument of an uninterpreted function, the
             * term of both theories: a real constant, or a numeral, or a
             * bound real through a fresh constant equal to it */
            LraLinear *lin = smtt_linear(t, arena, x, depth + 1u);
            if (!lin)
                return UINT32_MAX;
            if (b && b->linear)
                return smtt_purify(t, arena, lin);
            uint32_t k = smt_term(&t->p, x, 0u);
            return k != UINT32_MAX && smtt_share(t, k, lin) ? k : UINT32_MAX;
        }
        return smt_term(&t->p, x, 0u);
    }
    if (x->expr.len == 0u)
        return UINT32_MAX;
    if (smtt_is(x, "!") && x->expr.len >= 2u)
        return smtt_termof(t, arena, x->expr.elems[1], depth + 1u);
    if (smtt_is(x, "let") && x->expr.len == 3u) {
        uint32_t saved = t->nenv;
        Atom *bindings = x->expr.elems[1];
        for (CettaExprIndex i = 0u; i < bindings->expr.len; i++) {
            Atom *b = bindings->expr.elems[i];
            if (b->kind != ATOM_EXPR || b->expr.len != 2u)
                return UINT32_MAX;
            uint32_t v = smtt_termof(t, arena, b->expr.elems[1], depth + 1u);
            if (v == UINT32_MAX || !smtt_bind(t, (SmtBinding){b->expr.elems[0], true, 0, v}))
                return UINT32_MAX;
        }
        uint32_t r = smtt_termof(t, arena, x->expr.elems[2], depth + 1u);
        t->nenv = saved;
        return r;
    }
    if (smtt_is(x, "ite") && x->expr.len == 4u) {
        /* a fresh constant k with c -> k = a and not c -> k = b */
        int32_t c = smtt_formula(t, arena, x->expr.elems[1], depth + 1u);
        uint32_t a = smtt_termof(t, arena, x->expr.elems[2], depth + 1u);
        uint32_t b = smtt_termof(t, arena, x->expr.elems[3], depth + 1u);
        if (c == 0 || a == UINT32_MAX || b == UINT32_MAX)
            return UINT32_MAX;
        Atom *fresh = atom_expr2(arena, atom_symbol(arena, "smt:ite"),
                                 atom_int(arena, (int64_t)t->nvars + 1));
        uint32_t k = fresh ? smt_term(&t->p, fresh, 0u) : UINT32_MAX;
        if (k == UINT32_MAX)
            return UINT32_MAX;
        smtt_var(t);   /* names the fresh constant apart */
        int32_t ka = smtt_equality(t, k, a), kb = smtt_equality(t, k, b);
        int32_t c1[2] = {-c, ka}, c2[2] = {c, kb};
        if (!ka || !kb || !smtt_clause(t, c1, 2u) || !smtt_clause(t, c2, 2u))
            return UINT32_MAX;
        return k;
    }
    Atom *head = x->expr.elems[0];
    if (smtt_is(x, "+") || smtt_is(x, "-") || smtt_is(x, "*") || smtt_is(x, "/")) {
        /* QF_UFLRA: an arithmetic argument, through a fresh constant */
        LraLinear *lin = smtt_linear(t, arena, x, depth + 1u);
        return lin ? smtt_purify(t, arena, lin) : UINT32_MAX;
    }
    const SmtFun *fn = smtt_fun(t, head);
    if (fn && fn->body) {
        uint32_t saved;
        if (fn->arity != x->expr.len - 1u ||
            !smtt_enter_macro(t, arena, fn, x, depth, &saved))
            return UINT32_MAX;
        uint32_t r = smtt_termof(t, arena, fn->body, depth + 1u);
        t->nenv = saved;
        return r;
    }
    uint32_t n = (uint32_t)x->expr.len - 1u;
    uint32_t *args = malloc(sizeof(*args) * (n ? n : 1u));
    if (!args)
        return UINT32_MAX;
    for (uint32_t i = 0u; i < n; i++) {
        args[i] = smtt_termof(t, arena, x->expr.elems[i + 1u], depth + 1u);
        if (args[i] == UINT32_MAX) {
            free(args);
            return UINT32_MAX;
        }
    }
    uint32_t r = smtt_app(t, arena, head, args, n);
    free(args);
    return r;
}

/* Whether x is a real term: a number, a real constant or binding, an
 * arithmetic application, a real macro, or a let, annotation or ite whose
 * value is real. */
static bool smtt_is_real(SmtText *t, Atom *x) {
    if (x->kind == ATOM_GROUNDED)
        return x->ground.gkind == GV_INT || x->ground.gkind == GV_BIGINT;
    if (x->kind != ATOM_EXPR) {
        const SmtBinding *b = smtt_bound(t, x);
        if (b)
            return b->linear != NULL;
        const SmtFun *fn = smtt_fun(t, x);
        return fn && fn->real;
    }
    if (x->expr.len == 0u)
        return false;
    if (smtt_is(x, "+") || smtt_is(x, "-") || smtt_is(x, "*") || smtt_is(x, "/"))
        return true;
    if (smtt_is(x, "ite") && x->expr.len == 4u)
        return smtt_is_real(t, x->expr.elems[2]);
    if ((smtt_is(x, "!") || smtt_is(x, "let")) && x->expr.len >= 2u)
        return smtt_is_real(t, x->expr.elems[smtt_is(x, "!") ? 1u : x->expr.len - 1u]);
    const SmtFun *fn = smtt_fun(t, x->expr.elems[0]);
    return fn && fn->real;
}

static int32_t smtt_real_equality(SmtText *t, const LraLinear *a, const LraLinear *b) {
    LraLinear *d = smtt_difference(t, a, b);
    if (!d)
        return 0;
    int32_t le = smtt_bound_lit(t, d, ASP_LRA_LE), ge = le ? smtt_bound_lit(t, d, ASP_LRA_GE) : 0;
    if (!le || !ge)
        return 0;
    if (le == ge)
        return le;
    int32_t v = smtt_var(t);
    int32_t c1[2] = {-v, le}, c2[2] = {-v, ge}, c3[3] = {v, -le, -ge};
    return smtt_clause(t, c1, 2u) && smtt_clause(t, c2, 2u) && smtt_clause(t, c3, 3u) ? v : 0;
}

/* A real term as its exact linear form: numbers, real constants and
 * bindings, + - * (by constants) and / (by a nonzero constant), ite through
 * a fresh real, let, annotations and real macros. */
static LraLinear *smtt_linear(SmtText *t, Arena *arena, Atom *x, uint32_t depth) {
    if (depth > 100000u)
        return NULL;
    if (x->kind == ATOM_GROUNDED) {
        char *text = smt_number_text(x);
        LraLinear *r = text ? smtt_own(t, lra_linear_new()) : NULL;
        bool ok = r && lra_linear_add_constant(r, text);
        free(text);
        return ok ? r : NULL;
    }
    if (x->kind != ATOM_EXPR) {
        const SmtBinding *b = smtt_bound(t, x);
        if (b)
            return b->linear;
        const SmtFun *fn = smtt_fun(t, x);
        if (fn && fn->real && fn->body && fn->arity == 0u) {
            uint32_t saved = t->nenv;
            LraLinear *r = smtt_linear(t, arena, fn->body, depth + 1u);
            t->nenv = saved;
            return r;
        }
        if (!fn || !fn->real || fn->arity != 0u) {
            t->error = "a term outside QF_LRA's reals";
            return NULL;
        }
        int32_t v = asp_intern(&t->p.reals, x);
        LraLinear *r = v >= 0 ? smtt_own(t, lra_linear_new()) : NULL;
        return r && lra_linear_add_term(r, (uint32_t)v, "1") ? r : NULL;
    }
    uint32_t n = (uint32_t)x->expr.len - 1u;
    if (smtt_is(x, "!") && n >= 1u)
        return smtt_linear(t, arena, x->expr.elems[1], depth + 1u);
    if (smtt_is(x, "let") && n == 2u) {
        uint32_t saved = t->nenv;
        Atom *bindings = x->expr.elems[1];
        for (CettaExprIndex i = 0u; i < bindings->expr.len; i++) {
            Atom *b = bindings->expr.elems[i];
            if (b->kind != ATOM_EXPR || b->expr.len != 2u)
                return NULL;
            SmtBinding nb = {b->expr.elems[0], false, 0, 0u, NULL};
            if (smtt_boolean(t, b->expr.elems[1])) {
                if (!(nb.lit = smtt_formula(t, arena, b->expr.elems[1], depth + 1u)))
                    return NULL;
            } else if (smtt_is_real(t, b->expr.elems[1])) {
                if (!(nb.linear = smtt_linear(t, arena, b->expr.elems[1], depth + 1u)))
                    return NULL;
            } else {
                nb.is_term = true;
                if ((nb.term = smtt_termof(t, arena, b->expr.elems[1], depth + 1u)) == UINT32_MAX)
                    return NULL;
            }
            if (!smtt_bind(t, nb))
                return NULL;
        }
        LraLinear *r = smtt_linear(t, arena, x->expr.elems[2], depth + 1u);
        t->nenv = saved;
        return r;
    }
    if (smtt_is(x, "ite") && n == 3u) {
        /* a fresh real k with c -> k = a and not c -> k = b */
        int32_t c = smtt_formula(t, arena, x->expr.elems[1], depth + 1u);
        LraLinear *a = c ? smtt_linear(t, arena, x->expr.elems[2], depth + 1u) : NULL;
        LraLinear *b = a ? smtt_linear(t, arena, x->expr.elems[3], depth + 1u) : NULL;
        Atom *fresh = b ? atom_expr2(arena, atom_symbol(arena, "smt:real-ite"),
                                     atom_int(arena, (int64_t)t->fresh_reals++)) : NULL;
        int32_t k = fresh ? asp_intern(&t->p.reals, fresh) : -1;
        LraLinear *kl = k >= 0 ? smtt_own(t, lra_linear_new()) : NULL;
        if (!kl || !lra_linear_add_term(kl, (uint32_t)k, "1"))
            return NULL;
        int32_t ka = smtt_real_equality(t, kl, a), kb = ka ? smtt_real_equality(t, kl, b) : 0;
        int32_t c1[2] = {-c, ka}, c2[2] = {c, kb};
        return ka && kb && smtt_clause(t, c1, 2u) && smtt_clause(t, c2, 2u) ? kl : NULL;
    }
    bool add = smtt_is(x, "+"), sub = smtt_is(x, "-"), mul = smtt_is(x, "*");
    bool div = smtt_is(x, "/");
    /* SMT-LIB: + * and / take two or more arguments; - one (negation) or more */
    if ((add || sub || mul || div) && n >= (sub ? 1u : 2u)) {
        LraLinear *first = smtt_linear(t, arena, x->expr.elems[1], depth + 1u);
        LraLinear *r = first ? smtt_own(t, lra_linear_new()) : NULL;
        bool ok = r && lra_linear_add_scaled(r, first, sub && n == 1u ? "-1" : "1");
        for (uint32_t i = 2u; ok && i <= n; i++) {
            LraLinear *y = smtt_linear(t, arena, x->expr.elems[i], depth + 1u);
            ok = y != NULL;
            if (ok && (add || sub)) {
                ok = lra_linear_add_scaled(r, y, add ? "1" : "-1");
            } else if (ok && mul) {
                r = smtt_own(t, smt_linear_product(r, y));
                ok = r != NULL;
                if (!ok && !t->error)
                    t->error = "a nonlinear product: QF_LRA multiplies by constants";
            } else if (ok) {
                char *c = NULL, *inv = NULL;
                ok = lra_linear_is_constant(y, &c) && (inv = lra_reciprocal(c)) != NULL;
                if (!ok && !t->error)
                    t->error = "a division by a term that is not a nonzero constant";
                LraLinear *q = ok ? smtt_own(t, lra_linear_new()) : NULL;
                ok = ok && q && lra_linear_add_scaled(q, r, inv);
                free(c);
                free(inv);
                r = q;
            }
        }
        return ok ? r : NULL;
    }
    const SmtFun *fn = smtt_fun(t, x->expr.elems[0]);
    if (fn && fn->real && fn->body) {
        uint32_t saved;
        if (fn->arity != n || !smtt_enter_macro(t, arena, fn, x, depth, &saved))
            return NULL;
        LraLinear *r = smtt_linear(t, arena, fn->body, depth + 1u);
        t->nenv = saved;
        return r;
    }
    if (fn && fn->real && !fn->body && fn->arity == n && n > 0u) {
        /* QF_UFLRA: (f T ...) a term of both theories, a real of its own */
        uint32_t k = smtt_termof(t, arena, x, depth + 1u);
        if (k == UINT32_MAX)
            return NULL;
        int32_t v = asp_intern(&t->p.reals, t->p.term_atoms.items[k]);
        LraLinear *r = v >= 0 ? smtt_own(t, lra_linear_new()) : NULL;
        return r && lra_linear_add_term(r, (uint32_t)v, "1") && smtt_share(t, k, r) ? r : NULL;
    }
    t->error = add || mul || div ? "SMT-LIB's + * and / take two or more arguments"
             : "a term outside QF_LRA's reals";
    return NULL;
}

/* Whether x is Boolean: a Boolean literal, connective, binding, constant or
 * predicate application. */
static bool smtt_boolean(SmtText *t, Atom *x) {
    if (x->kind != ATOM_EXPR) {
        if (atom_is_symbol(x, "true") || atom_is_symbol(x, "false"))
            return true;
        const SmtBinding *b = smtt_bound(t, x);
        if (b)
            return !b->is_term;
        const SmtFun *fn = smtt_fun(t, x);
        return fn && fn->boolean;
    }
    if (x->expr.len == 0u)
        return false;
    static const char *const connectives[] = {"not", "and", "or", "=>", "xor",
                                              "=", "distinct", "<=", "<", ">=", ">"};
    for (size_t i = 0u; i < sizeof(connectives) / sizeof(connectives[0]); i++)
        if (smtt_is(x, connectives[i]))
            return true;
    if (smtt_is(x, "!") || smtt_is(x, "let") || smtt_is(x, "ite"))
        return x->expr.len >= 2u && smtt_boolean(t, x->expr.elems[x->expr.len - 1u]);
    const SmtFun *fn = smtt_fun(t, x->expr.elems[0]);
    return fn && fn->boolean;
}

/* A fresh variable v with v <-> (l1 op l2 ...) for and / or. */
static int32_t smtt_gate(SmtText *t, bool conj, const int32_t *ls, uint32_t n) {
    int32_t v = smtt_var(t);
    int32_t *big = malloc(sizeof(*big) * (n + 1u));
    if (!big)
        return 0;
    bool ok = true;
    for (uint32_t i = 0u; ok && i < n; i++) {
        int32_t two[2] = {conj ? -v : v, conj ? ls[i] : -ls[i]};
        ok = smtt_clause(t, two, 2u);
        big[i] = conj ? -ls[i] : ls[i];
    }
    big[n] = conj ? v : -v;
    ok = ok && smtt_clause(t, big, n + 1u);
    free(big);
    return ok ? v : 0;
}

static int32_t smtt_iff(SmtText *t, int32_t a, int32_t b) {
    int32_t v = smtt_var(t);
    int32_t c1[3] = {-v, -a, b}, c2[3] = {-v, a, -b}, c3[3] = {v, a, b}, c4[3] = {v, -a, -b};
    return smtt_clause(t, c1, 3u) && smtt_clause(t, c2, 3u) &&
           smtt_clause(t, c3, 3u) && smtt_clause(t, c4, 3u) ? v : 0;
}

static int32_t smtt_formula(SmtText *t, Arena *arena, Atom *f, uint32_t depth) {
    if (depth > 100000u)
        return 0;
    if (f->kind != ATOM_EXPR) {
        if (atom_is_symbol(f, "true"))
            return t->truth_lit;
        if (atom_is_symbol(f, "false"))
            return -t->truth_lit;
        const SmtBinding *b = smtt_bound(t, f);
        if (b && !b->is_term)
            return b->lit;
        const SmtFun *fn = smtt_fun(t, f);
        if (fn && fn->body && fn->arity == 0u) {
            uint32_t saved = t->nenv;
            int32_t r = smtt_formula(t, arena, fn->body, depth + 1u);
            t->nenv = saved;
            return r;
        }
        /* a Boolean constant: the term itself equal to the truth term */
        uint32_t x = smt_term(&t->p, f, 0u);
        return x == UINT32_MAX ? 0 : smtt_equality(t, x, t->p.truth);
    }
    uint32_t n = (uint32_t)f->expr.len - 1u;
    if (smtt_is(f, "!") && n >= 1u)
        return smtt_formula(t, arena, f->expr.elems[1], depth + 1u);
    if (smtt_is(f, "not") && n == 1u) {
        int32_t l = smtt_formula(t, arena, f->expr.elems[1], depth + 1u);
        return l ? -l : 0;
    }
    if (smtt_is(f, "let") && n == 2u) {
        uint32_t saved = t->nenv;
        Atom *bindings = f->expr.elems[1];
        for (CettaExprIndex i = 0u; i < bindings->expr.len; i++) {
            Atom *b = bindings->expr.elems[i];
            if (b->kind != ATOM_EXPR || b->expr.len != 2u)
                return 0;
            SmtBinding nb = {b->expr.elems[0], false, 0, 0u, NULL};
            if (smtt_boolean(t, b->expr.elems[1])) {
                nb.lit = smtt_formula(t, arena, b->expr.elems[1], depth + 1u);
                if (!nb.lit)
                    return 0;
            } else if (smtt_is_real(t, b->expr.elems[1])) {
                nb.linear = smtt_linear(t, arena, b->expr.elems[1], depth + 1u);
                if (!nb.linear)
                    return 0;
            } else {
                nb.is_term = true;
                nb.term = smtt_termof(t, arena, b->expr.elems[1], depth + 1u);
                if (nb.term == UINT32_MAX)
                    return 0;
            }
            if (!smtt_bind(t, nb))
                return 0;
        }
        int32_t r = smtt_formula(t, arena, f->expr.elems[2], depth + 1u);
        t->nenv = saved;
        return r;
    }
    bool conj = smtt_is(f, "and"), disj = smtt_is(f, "or");
    bool imp = smtt_is(f, "=>"), xr = smtt_is(f, "xor");
    if (conj || disj || imp || xr) {
        if (n == 0u)
            return conj ? t->truth_lit : -t->truth_lit;
        int32_t *ls = malloc(sizeof(*ls) * n);
        if (!ls)
            return 0;
        for (uint32_t i = 0u; i < n; i++) {
            ls[i] = smtt_formula(t, arena, f->expr.elems[i + 1u], depth + 1u);
            if (!ls[i]) {
                free(ls);
                return 0;
            }
        }
        int32_t r;
        if (imp) {
            /* a1 => a2 => ... => an: not a1 or ... or an, right-associated */
            for (uint32_t i = 0u; i + 1u < n; i++)
                ls[i] = -ls[i];
            r = n == 1u ? ls[0] : smtt_gate(t, false, ls, n);
        } else if (xr) {
            r = ls[0];
            for (uint32_t i = 1u; r && i < n; i++)
                r = -smtt_iff(t, r, ls[i]);
        } else {
            r = n == 1u ? ls[0] : smtt_gate(t, conj, ls, n);
        }
        free(ls);
        return r;
    }
    if (smtt_is(f, "ite") && n == 3u) {
        int32_t c = smtt_formula(t, arena, f->expr.elems[1], depth + 1u);
        int32_t a = smtt_formula(t, arena, f->expr.elems[2], depth + 1u);
        int32_t b = smtt_formula(t, arena, f->expr.elems[3], depth + 1u);
        if (!c || !a || !b)
            return 0;
        int32_t v = smtt_var(t);
        int32_t c1[3] = {-v, -c, a}, c2[3] = {-v, c, b}, c3[3] = {v, -c, -a}, c4[3] = {v, c, -b};
        return smtt_clause(t, c1, 3u) && smtt_clause(t, c2, 3u) &&
               smtt_clause(t, c3, 3u) && smtt_clause(t, c4, 3u) ? v : 0;
    }
    bool le = smtt_is(f, "<="), lt = smtt_is(f, "<"), ge = smtt_is(f, ">="), gt = smtt_is(f, ">");
    bool real_eq = (smtt_is(f, "=") || smtt_is(f, "distinct")) && n >= 2u &&
                   !smtt_boolean(t, f->expr.elems[1]) && smtt_is_real(t, f->expr.elems[1]);
    if (((le || lt || ge || gt) && n >= 2u) || real_eq) {
        /* a chain over neighbours (distinct: every pair), conjoined */
        bool distinct = smtt_is(f, "distinct");
        LraLinear **xs = malloc(sizeof(*xs) * n);
        int32_t *ls = malloc(sizeof(*ls) * (n * n + 1u));
        bool ok = xs && ls;
        for (uint32_t i = 0u; ok && i < n; i++)
            ok = (xs[i] = smtt_linear(t, arena, f->expr.elems[i + 1u], depth + 1u)) != NULL;
        uint32_t nl = 0u;
        AspLraRelation rel = le ? ASP_LRA_LE : lt ? ASP_LRA_LT : ge ? ASP_LRA_GE : ASP_LRA_GT;
        for (uint32_t i = 0u; ok && i + 1u < n; i++)
            for (uint32_t j = i + 1u; ok && j < (distinct ? n : i + 2u); j++) {
                int32_t l;
                if (real_eq) {
                    l = smtt_real_equality(t, xs[i], xs[j]);
                    l = distinct ? -l : l;
                } else {
                    LraLinear *d = smtt_difference(t, xs[i], xs[j]);
                    l = d ? smtt_bound_lit(t, d, rel) : 0;
                }
                ok = l != 0;
                ls[nl++] = l;
            }
        int32_t r = !ok ? 0 : nl == 1u ? ls[0] : smtt_gate(t, true, ls, nl);
        free(xs);
        free(ls);
        return r;
    }
    if ((smtt_is(f, "=") || smtt_is(f, "distinct")) && n >= 2u) {
        bool distinct = smtt_is(f, "distinct");
        bool boolean = smtt_boolean(t, f->expr.elems[1]);
        uint32_t m = n;
        int32_t *pairs = malloc(sizeof(*pairs) * (m * m + 1u));
        uint32_t *terms = boolean ? NULL : malloc(sizeof(*terms) * m);
        int32_t *bools = boolean ? malloc(sizeof(*bools) * m) : NULL;
        if (!pairs || (!boolean && !terms) || (boolean && !bools)) {
            free(pairs);
            free(terms);
            free(bools);
            return 0;
        }
        bool ok = true;
        for (uint32_t i = 0u; ok && i < m; i++) {
            if (boolean) {
                bools[i] = smtt_formula(t, arena, f->expr.elems[i + 1u], depth + 1u);
                ok = bools[i] != 0;
            } else {
                terms[i] = smtt_termof(t, arena, f->expr.elems[i + 1u], depth + 1u);
                ok = terms[i] != UINT32_MAX;
            }
        }
        uint32_t np = 0u;
        /* = chains its neighbours; distinct takes every pair apart */
        for (uint32_t i = 0u; ok && i + 1u < m; i++)
            for (uint32_t j = i + 1u; ok && j < (distinct ? m : i + 2u); j++) {
                int32_t e = boolean ? smtt_iff(t, bools[i], bools[j])
                                    : smtt_equality(t, terms[i], terms[j]);
                ok = e != 0;
                pairs[np++] = distinct ? -e : e;
            }
        int32_t r = !ok ? 0 : np == 1u ? pairs[0] : smtt_gate(t, true, pairs, np);
        free(pairs);
        free(terms);
        free(bools);
        return r;
    }
    /* a predicate's application or a Boolean macro */
    const SmtFun *fn = smtt_fun(t, f->expr.elems[0]);
    if (fn && fn->body) {
        uint32_t saved;
        if (fn->arity != n || !smtt_enter_macro(t, arena, fn, f, depth, &saved))
            return 0;
        int32_t r = smtt_formula(t, arena, fn->body, depth + 1u);
        t->nenv = saved;
        return r;
    }
    if (!fn || !fn->boolean) {
        t->error = "an assertion holds a construct outside QF_UF and QF_LRA";
        return 0;
    }
    uint32_t x = smtt_termof(t, arena, f, depth + 1u);
    return x == UINT32_MAX ? 0 : smtt_equality(t, x, t->p.truth);
}

static void smt_text_free(SmtText *t) {
    for (uint32_t i = 0u; i < t->nlinears; i++)
        lra_linear_free(t->linears[i]);
    free(t->linears);
    free(t->lra_slots);
    smt_problem_free(&t->p);
    free(t->funs);
    free(t->env);
    free(t->lits);
    free(t->clause_start);
    free(t->eq_lhs);
    free(t->eq_rhs);
    free(t->eq_var);
    free(t->sh_term);
    free(t->sh_lin);
}

/* The script with each decimal numeral (-?D+.D+) written as the exact
 * quotient (/ N 10^k), outside strings, quoted symbols and comments: the
 * reader would make a decimal a binary float. */
static char *smt_text_exact_decimals(const char *src) {
    size_t len = strlen(src), cap = 2u * len + 64u, k = 0u;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    bool in_string = false, in_quoted = false, in_comment = false;
    for (size_t i = 0u; i < len;) {
        char c = src[i];
        if (k + 64u >= cap) {
            cap *= 2u;
            char *g = realloc(out, cap);
            if (!g) {
                free(out);
                return NULL;
            }
            out = g;
        }
        if (in_comment) {
            in_comment = c != '\n';
        } else if (in_string) {
            in_string = c != '"';
        } else if (in_quoted) {
            in_quoted = c != '|';
        } else if (c == ';') {
            in_comment = true;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '|') {
            in_quoted = true;
        } else if ((c >= '0' && c <= '9') &&
                   (i == 0u || src[i - 1u] == '(' || src[i - 1u] == ')' ||
                    src[i - 1u] == ' ' || src[i - 1u] == '\t' || src[i - 1u] == '\n' ||
                    src[i - 1u] == '\r')) {
            size_t j = i;
            while (j < len && src[j] >= '0' && src[j] <= '9')
                j++;
            size_t point = j;
            if (j < len && src[j] == '.') {
                j++;
                size_t frac = j;
                while (j < len && src[j] >= '0' && src[j] <= '9')
                    j++;
                bool ends = j == len || src[j] == ')' || src[j] == '(' || src[j] == ' ' ||
                            src[j] == '\t' || src[j] == '\n' || src[j] == '\r';
                if (j > frac && ends) {
                    size_t digits = j - frac;
                    size_t need = k + (point - i) + 2u * digits + 16u;
                    while (need >= cap) {
                        cap *= 2u;
                        char *g = realloc(out, cap);
                        if (!g) {
                            free(out);
                            return NULL;
                        }
                        out = g;
                    }
                    k += (size_t)sprintf(out + k, "(/ ");
                    memcpy(out + k, src + i, point - i);
                    k += point - i;
                    memcpy(out + k, src + frac, digits);
                    k += digits;
                    out[k++] = ' ';
                    out[k++] = '1';
                    for (size_t z = 0u; z < digits; z++)
                        out[k++] = '0';
                    out[k++] = ')';
                    i = j;
                    continue;
                }
            }
            memcpy(out + k, src + i, j - i);
            k += j - i;
            i = j;
            continue;
        }
        out[k++] = c;
        i++;
    }
    out[k] = '\0';
    return out;
}

/* The optimization loop both fronts share.  Over the clauses BASE (natoms
 * variables) and the theories BASE_TH, the least value of the linear form
 * OVARS/OCOEFS (no constant; a greatest is the least of its negation): each
 * model's reals are moved to where the form is least under its bounds, then
 * the form is bounded below that value by a fresh atom, until the bound
 * refutes.  SMT_OPT_UNSAT with no model, SMT_OPT_UNBOUNDED, or SMT_OPT_BEST:
 * the value BEST, ATTAINED or only approached, BEST_ANSWER its model, LAST
 * the refutation of a better value. */
typedef enum { SMT_OPT_UNSAT, SMT_OPT_UNBOUNDED, SMT_OPT_BEST } SmtOptKind;
typedef struct {
    SmtOptKind kind;
    char *best;
    bool attained;
    AspAnswer best_answer;
    AspAnswer last;
} SmtOptimum;

static void smt_optimum_free(SmtOptimum *o) {
    free(o->best);
    asp_answer_free(&o->best_answer);
    asp_answer_free(&o->last);
}

static bool smt_optimize(uint32_t natoms, const AspClause *base, uint32_t nbase,
                         const AspSmtTheories *base_th, uint32_t *ovars, char **ocoefs,
                         uint32_t on, SmtOptimum *o, const char **error) {
    memset(o, 0, sizeof(*o));
    AspClause *cls = malloc(sizeof(*cls) * (nbase + 1u));
    AspLraAtom *atoms = malloc(sizeof(*atoms) * (base_th->nlra + 1u));
    int32_t bound_lit = (int32_t)natoms + 1;
    bool ok = cls && atoms, done = false;
    if (ok) {
        if (nbase)
            memcpy(cls, base, sizeof(*cls) * nbase);
        if (base_th->nlra)
            memcpy(atoms, base_th->lra, sizeof(*atoms) * base_th->nlra);
        cls[nbase] = (AspClause){&bound_lit, 1u};
    }
    while (ok && !done) {
        bool bounded = o->best != NULL;
        if (bounded)
            atoms[base_th->nlra] = (AspLraAtom){(uint32_t)bound_lit, ovars, (const char *const *)ocoefs,
                                                on, o->attained ? ASP_LRA_LT : ASP_LRA_LE, o->best};
        AspSmtTheories th = *base_th;
        th.lra = atoms;
        th.nlra = base_th->nlra + (bounded ? 1u : 0u);
        th.objective_reals = ovars;
        th.objective_coefs = (const char *const *)ocoefs;
        th.objective_n = on;
        AspAnswer answer = asp_smt_solve(natoms + (bounded ? 1u : 0u), cls,
                                         nbase + (bounded ? 1u : 0u), &th);
        if (answer.kind == ASP_ANSWER_SAT && answer.objective_unbounded) {
            o->kind = SMT_OPT_UNBOUNDED;
            o->last = answer;
            done = true;
        } else if (answer.kind == ASP_ANSWER_SAT && (answer.objective || on == 0u)) {
            /* a form without variables is its constant, 0 here: attained */
            char *value = answer.objective;
            if (!value && (value = malloc(2u)) != NULL)
                memcpy(value, "0", 2u);
            if (!value) {
                *error = "out of memory";
                asp_answer_free(&answer);
                ok = false;
                break;
            }
            free(o->best);
            o->best = value;
            answer.objective = NULL;
            o->attained = on == 0u || answer.objective_attained;
            asp_answer_free(&o->best_answer);
            o->best_answer = answer;
        } else if (answer.kind == ASP_ANSWER_UNSAT) {
            o->kind = o->best ? SMT_OPT_BEST : SMT_OPT_UNSAT;
            o->last = answer;
            done = true;
        } else {
            *error = answer.kind == ASP_ANSWER_REFUSED
                ? "the solver's answer was refused by its own check"
                : "the solve did not finish";
            asp_answer_free(&answer);
            ok = false;
        }
    }
    free(cls);
    free(atoms);
    if (!ok)
        smt_optimum_free(o);
    return ok && done;
}

/* The optimum's value: the form's least value, the term's constant added,
 * negated back for a greatest. */
static Atom *smt_optimum_atom(Arena *a, const SmtOptimum *o, const char *oconst, bool maximize) {
    LraLinear *v = lra_linear_new();
    char *vt = NULL;
    bool vok = v && lra_linear_add_constant(v, o->best) &&
               (!oconst || lra_linear_add_constant(v, oconst)) &&
               (!maximize || lra_linear_add_scaled(v, v, "-2")) &&
               lra_linear_is_constant(v, &vt);
    lra_linear_free(v);
    Atom *value = vok && vt ? smt_number_atom(a, vt) : NULL;
    free(vt);
    return value;
}

static const char *smt_optimum_name(const SmtOptimum *o, bool maximize) {
    return o->attained ? "optimum" : maximize ? "supremum" : "infimum";
}

/* Whether the text declares an integer constant. */
static bool smtt_has_integers(SmtText *t) {
    for (uint32_t i = 0u; i < t->nfuns; i++)
        if (t->funs[i].integer && t->funs[i].arity == 0u && !t->funs[i].body)
            return true;
    return false;
}

/* The text's theories share only the shared terms, through their interface
 * equalities. */
static bool smtt_reals_apart(SmtText *t) {
    for (uint32_t i = 0u; i < t->p.reals.len; i++) {
        Atom *r = t->p.reals.items[i];
        if (r->kind != ATOM_SYMBOL || asp_atom_find(&t->p.symbols, r) < 0)
            continue;
        bool shared = false;
        for (uint32_t s = 0u; s < t->nsh && !shared; s++)
            shared = atom_eq(t->p.term_atoms.items[t->sh_term[s]], r);
        if (!shared) {
            t->p.lra_error = "a real is also an uninterpreted term: the theories share no symbol";
            return false;
        }
    }
    return true;
}

/* ---- integers (QF_LIA, QF_LIRA): branch and bound -------------------------
 * The integer constants are reals of the relaxation.  A model giving one a
 * value V that is not an integer splits the problem: X <= floor(V), and
 * X >= floor(V) + 1, each solved again with that bound added (an atom with a
 * unit clause).  A refutation is the tree (split X K CERT CERT), its leaves
 * the solver's own checked certificates of the problem with the bounds on
 * their path, each split valid over the integers.  Past a depth of 64 the
 * answer is unknown, never guessed. */
typedef struct {
    uint32_t real;
    char *bound;
    bool ge;
} SmtSplit;

typedef enum { SMT_BB_SAT, SMT_BB_UNSAT, SMT_BB_UNKNOWN, SMT_BB_ERROR } SmtBbKind;

/* floor of the exact value TEXT ("n" or "p/q"), or false when it does not
 * fit in 64 bits; *whole when it is an integer. */
static bool smtt_floor(const char *text, int64_t *floor_out, bool *whole) {
    errno = 0;
    char *end = NULL;
    long long p = strtoll(text, &end, 10);
    if (errno || end == text)
        return false;
    if (*end == '\0') {
        *floor_out = (int64_t)p;
        *whole = true;
        return true;
    }
    if (*end != '/')
        return false;
    long long q = strtoll(end + 1, NULL, 10);
    if (errno || q <= 0)
        return false;
    long long f = p / q;
    if ((p % q != 0) && (p < 0))
        f -= 1;
    *floor_out = (int64_t)f;
    *whole = p % q == 0;
    return true;
}

static SmtBbKind smtt_bb(Arena *arena, SmtText *t, const AspClause *clauses, uint32_t nclauses,
                         const AspSmtEquality *eqs, const uint32_t *ints, uint32_t nints,
                         SmtSplit *path, uint32_t depth, Atom **cert, const char **error) {
    if (depth > 64u)
        return SMT_BB_UNKNOWN;
    AspClause *cls = malloc(sizeof(*cls) * (nclauses + depth + 1u));
    AspLraAtom *atoms = malloc(sizeof(*atoms) * (t->p.nlra + depth + 1u));
    int32_t *units = malloc(sizeof(*units) * (depth + 1u));
    static const char *one = "1";
    if (!cls || !atoms || !units) {
        free(cls);
        free(atoms);
        free(units);
        return SMT_BB_ERROR;
    }
    memcpy(cls, clauses, sizeof(*cls) * nclauses);
    if (t->p.nlra)
        memcpy(atoms, t->p.lra, sizeof(*atoms) * t->p.nlra);
    for (uint32_t k = 0u; k < depth; k++) {
        units[k] = (int32_t)(t->nvars + k + 1u);
        atoms[t->p.nlra + k] = (AspLraAtom){(uint32_t)units[k], &path[k].real,
            (const char *const *)&one, 1u, path[k].ge ? ASP_LRA_GE : ASP_LRA_LE, path[k].bound};
        cls[nclauses + k] = (AspClause){&units[k], 1u};
    }
    AspSmtTheories th = {t->p.terms, t->p.nterms, eqs, t->neq, t->p.reals.len,
                         atoms, t->p.nlra + depth};
    AspAnswer answer = asp_smt_solve(t->nvars + depth, cls, nclauses + depth, &th);
    SmtBbKind kind = SMT_BB_ERROR;
    if (answer.kind == ASP_ANSWER_UNSAT) {
        *cert = asp_certificate(arena, &answer);
        kind = *cert ? SMT_BB_UNSAT : SMT_BB_ERROR;
    } else if (answer.kind == ASP_ANSWER_SAT) {
        /* the first integer constant without an integer value */
        uint32_t split = UINT32_MAX;
        int64_t fl = 0;
        bool fits = true;
        for (uint32_t i = 0u; i < nints && split == UINT32_MAX && fits; i++) {
            bool whole = false;
            fits = ints[i] < answer.reals_len && smtt_floor(answer.reals[ints[i]], &fl, &whole);
            if (fits && !whole)
                split = ints[i];
        }
        if (!fits) {
            kind = SMT_BB_UNKNOWN;
        } else if (split == UINT32_MAX) {
            kind = SMT_BB_SAT;
        } else {
            char low[32], high[32];
            snprintf(low, sizeof low, "%lld", (long long)fl);
            snprintf(high, sizeof high, "%lld", (long long)fl + 1);
            path[depth] = (SmtSplit){split, low, false};
            Atom *left = NULL, *right = NULL;
            SmtBbKind lk = smtt_bb(arena, t, clauses, nclauses, eqs, ints, nints, path,
                                   depth + 1u, &left, error);
            SmtBbKind rk = SMT_BB_UNSAT;
            if (lk == SMT_BB_UNSAT || lk == SMT_BB_UNKNOWN) {
                path[depth] = (SmtSplit){split, high, true};
                rk = smtt_bb(arena, t, clauses, nclauses, eqs, ints, nints, path,
                             depth + 1u, &right, error);
            }
            if (lk == SMT_BB_SAT || rk == SMT_BB_SAT)
                kind = SMT_BB_SAT;
            else if (lk == SMT_BB_ERROR || rk == SMT_BB_ERROR)
                kind = SMT_BB_ERROR;
            else if (lk == SMT_BB_UNKNOWN || rk == SMT_BB_UNKNOWN)
                kind = SMT_BB_UNKNOWN;
            else {
                Atom *parts[5] = {atom_symbol(arena, "split"), t->p.reals.items[split],
                                  smt_integer_atom(arena, low), left, right};
                *cert = parts[2] ? atom_expr(arena, parts, 5u) : NULL;
                kind = *cert ? SMT_BB_UNSAT : SMT_BB_ERROR;
            }
        }
    } else {
        *error = answer.kind == ASP_ANSWER_REFUSED
            ? "the solver's answer was refused by its own check"
            : "the solve did not finish";
    }
    asp_answer_free(&answer);
    free(cls);
    free(atoms);
    free(units);
    return kind;
}

static Atom *smt_text_value(Arena *arena, Atom *text, const char **error) {
    if (!text || text->kind != ATOM_GROUNDED || text->ground.gkind != GV_STRING) {
        *error = "expected an SMT-LIB script as a string";
        return NULL;
    }
    Atom **items = NULL;
    char *exact = smt_text_exact_decimals(text->ground.sval);
    if (!exact) {
        *error = "out of memory";
        return NULL;
    }
    int count = parse_metta_text(exact, arena, &items);
    free(exact);
    if (count < 0) {
        *error = "the script does not read as s-expressions";
        return NULL;
    }
    SmtText t;
    memset(&t, 0, sizeof(t));
    Atom *result = NULL;
    t.p.truth = smt_term(&t.p, atom_symbol(arena, "smt:true"), 0u);
    t.truth_lit = smtt_var(&t);
    Atom *objectives[16];
    bool maximizes[16];
    uint32_t nobj = 0u;
    bool ok = t.p.truth != UINT32_MAX && smtt_clause(&t, &t.truth_lit, 1u);
    for (int i = 0; ok && i < count; i++) {
        Atom *c = items[i];
        if (smtt_is(c, "declare-fun") && c->expr.len == 4u) {
            if (t.nfuns == t.capfuns) {
                uint32_t cap = t.capfuns ? 2u * t.capfuns : 64u;
                SmtFun *g = realloc(t.funs, sizeof(*g) * cap);
                if (!(ok = g != NULL))
                    break;
                t.funs = g;
                t.capfuns = cap;
            }
            t.funs[t.nfuns++] = (SmtFun){c->expr.elems[1],
                atom_is_symbol(c->expr.elems[3], "Bool"),
                c->expr.elems[2]->kind == ATOM_EXPR ? (uint32_t)c->expr.elems[2]->expr.len : 0u,
                NULL, NULL,
                atom_is_symbol(c->expr.elems[3], "Real") || atom_is_symbol(c->expr.elems[3], "Int"),
                atom_is_symbol(c->expr.elems[3], "Int")};
        } else if (smtt_is(c, "declare-const") && c->expr.len == 3u) {
            if (t.nfuns == t.capfuns) {
                uint32_t cap = t.capfuns ? 2u * t.capfuns : 64u;
                SmtFun *g = realloc(t.funs, sizeof(*g) * cap);
                if (!(ok = g != NULL))
                    break;
                t.funs = g;
                t.capfuns = cap;
            }
            t.funs[t.nfuns++] = (SmtFun){c->expr.elems[1],
                atom_is_symbol(c->expr.elems[2], "Bool"), 0u, NULL, NULL,
                atom_is_symbol(c->expr.elems[2], "Real") || atom_is_symbol(c->expr.elems[2], "Int"),
                atom_is_symbol(c->expr.elems[2], "Int")};
        } else if (smtt_is(c, "define-fun") && c->expr.len == 5u &&
                   c->expr.elems[2]->kind == ATOM_EXPR) {
            if (t.nfuns == t.capfuns) {
                uint32_t cap = t.capfuns ? 2u * t.capfuns : 64u;
                SmtFun *g = realloc(t.funs, sizeof(*g) * cap);
                if (!(ok = g != NULL))
                    break;
                t.funs = g;
                t.capfuns = cap;
            }
            t.funs[t.nfuns++] = (SmtFun){c->expr.elems[1],
                atom_is_symbol(c->expr.elems[3], "Bool"),
                (uint32_t)c->expr.elems[2]->expr.len, c->expr.elems[2],
                c->expr.elems[4],
                atom_is_symbol(c->expr.elems[3], "Real") || atom_is_symbol(c->expr.elems[3], "Int"),
                atom_is_symbol(c->expr.elems[3], "Int")};
        } else if (smtt_is(c, "assert") && c->expr.len == 2u) {
            int32_t l = smtt_formula(&t, arena, c->expr.elems[1], 0u);
            ok = l != 0 && smtt_clause(&t, &l, 1u);
            if (!ok && !t.error)
                t.error = "an assertion does not read as a QF_UF or QF_LRA formula";
        } else if ((smtt_is(c, "minimize") || smtt_is(c, "maximize")) && c->expr.len == 2u) {
            if (nobj == 16u) {
                t.error = "more than 16 objectives";
                ok = false;
            } else {
                objectives[nobj] = c->expr.elems[1];
                maximizes[nobj++] = smtt_is(c, "maximize");
            }
        } else if (smtt_is(c, "get-objectives")) {
            continue;
        } else if (smtt_is(c, "set-logic") || smtt_is(c, "set-info") ||
                   smtt_is(c, "set-option") || smtt_is(c, "declare-sort") ||
                   smtt_is(c, "check-sat") || smtt_is(c, "exit") ||
                   smtt_is(c, "get-model") || smtt_is(c, "get-info")) {
            continue;
        } else {
            t.error = "a command outside QF_UF's and QF_LRA's assertions";
            ok = false;
        }
    }
    /* QF_UFLRA by delayed theory combination: for each pair of shared terms
     * an equality both theories read, the uninterpreted equality of the
     * terms exactly when their linear forms are equal (a valid clause of the
     * combined theory); the search decides how the shared terms fall into
     * classes, and each theory's certificate steps stay its own. */
    uint32_t nshared = t.nsh;
    for (uint32_t i = 0u; ok && i < nshared; i++)
        for (uint32_t j = i + 1u; ok && j < nshared; j++) {
            int32_t e = smtt_equality(&t, t.sh_term[i], t.sh_term[j]);
            int32_t r = e ? smtt_real_equality(&t, t.sh_lin[i], t.sh_lin[j]) : 0;
            int32_t c1[2] = {-e, r}, c2[2] = {e, -r};
            ok = e && r && smtt_clause(&t, c1, 2u) && smtt_clause(&t, c2, 2u);
        }
    if (ok) {
        AspClause *clauses = malloc(sizeof(*clauses) * (t.nclauses ? t.nclauses : 1u));
        AspSmtEquality *eqs = malloc(sizeof(*eqs) * (t.neq ? t.neq : 1u));
        ok = clauses && eqs;
        for (uint32_t i = 0u; ok && i < t.nclauses; i++)
            clauses[i] = (AspClause){t.lits + t.clause_start[i],
                                     t.clause_start[i + 1u] - t.clause_start[i]};
        for (uint32_t i = 0u; ok && i < t.neq; i++)
            eqs[i] = (AspSmtEquality){t.eq_var[i], t.eq_lhs[i], t.eq_rhs[i]};
        if (ok) {
            ok = smtt_reals_apart(&t);
            if (!ok)
                t.error = t.p.lra_error;
        }
        if (ok && nobj) {
            /* (minimize T) or (maximize T): (optimum V CERT), (infimum V CERT)
             * or (supremum V CERT) when V is only approached, unbounded, or
             * (unsat CERT); CERT refutes a better value.  Several objectives
             * are lexicographic: each stage optimized with the earlier ones
             * held at their optima, (lexicographic (STAGE ...)), ending at
             * the first stage that is unbounded or only approached (no model
             * attains it, so later stages have no meaning); each stage's CERT
             * refutes a better value with the earlier optima held. */
            Atom **stages = arena_alloc(arena, sizeof(*stages) * nobj);
            uint32_t nstages = 0u;
            /* the stages held: their forms (minimized) and least values */
            uint32_t *hv[16];
            char **hc[16];
            uint32_t hn[16];
            char *hbest[16];
            uint32_t nheld = 0u;
            bool unsat = false;
            Atom *unsat_value = NULL;
            for (uint32_t i = 0u; ok && i < nobj && stages; i++) {
                LraLinear *obj = smtt_linear(&t, arena, objectives[i], 0u);
                LraLinear *goal = obj ? lra_linear_new() : NULL;
                uint32_t *ovars = NULL, on = 0u;
                char **ocoefs = NULL, *oconst = NULL;
                bool gok = goal && lra_linear_add_scaled(goal, obj, maximizes[i] ? "-1" : "1") &&
                           lra_linear_parts(goal, &ovars, &ocoefs, &on, &oconst);
                /* the reader owns obj */
                lra_linear_free(goal);
                if (!gok) {
                    t.error = t.error ? t.error : t.p.lra_error ? t.p.lra_error
                                                 : "the objective is not a linear real term";
                    ok = false;
                    lra_parts_free(ovars, ocoefs, on, oconst);
                    break;
                }
                /* the held stages: form <= best and form >= best, each a
                 * fresh atom with a unit clause */
                uint32_t natoms = t.nvars + 2u * nheld;
                AspClause *cls = malloc(sizeof(*cls) * (t.nclauses + 2u * nheld + 1u));
                AspLraAtom *atoms = malloc(sizeof(*atoms) * (t.p.nlra + 2u * nheld + 1u));
                int32_t units[32];
                bool sok = cls && atoms;
                if (sok) {
                    memcpy(cls, clauses, sizeof(*cls) * t.nclauses);
                    if (t.p.nlra)
                        memcpy(atoms, t.p.lra, sizeof(*atoms) * t.p.nlra);
                    for (uint32_t j = 0u; j < nheld; j++)
                        for (uint32_t side = 0u; side < 2u; side++) {
                            uint32_t k = 2u * j + side;
                            units[k] = (int32_t)(t.nvars + k + 1u);
                            atoms[t.p.nlra + k] = (AspLraAtom){(uint32_t)units[k], hv[j],
                                (const char *const *)hc[j], hn[j],
                                side ? ASP_LRA_GE : ASP_LRA_LE, hbest[j]};
                            cls[t.nclauses + k] = (AspClause){&units[k], 1u};
                        }
                }
                AspSmtTheories base = {t.p.terms, t.p.nterms, eqs, t.neq, t.p.reals.len,
                                       atoms, t.p.nlra + 2u * nheld};
                SmtOptimum o;
                bool held = false;
                if (!sok) {
                    ok = false;
                } else if (smt_optimize(natoms, cls, t.nclauses + 2u * nheld, &base,
                                        ovars, ocoefs, on, &o, error)) {
                    Atom *cert = o.kind == SMT_OPT_UNBOUNDED ? NULL : asp_certificate(arena, &o.last);
                    Atom *stage = NULL;
                    if (o.kind == SMT_OPT_UNBOUNDED) {
                        stage = atom_symbol(arena, "unbounded");
                    } else if (o.kind == SMT_OPT_UNSAT) {
                        unsat = true;
                        unsat_value = cert ? atom_expr2(arena, atom_symbol(arena, "unsat"), cert) : NULL;
                    } else {
                        Atom *value = smt_optimum_atom(arena, &o, oconst, maximizes[i]);
                        stage = value && cert
                            ? atom_expr3(arena, atom_symbol(arena, smt_optimum_name(&o, maximizes[i])), value, cert)
                            : NULL;
                        if (stage && o.attained && i + 1u < nobj) {
                            /* hold it: keep the form and its least value */
                            hv[nheld] = ovars;
                            hc[nheld] = ocoefs;
                            hn[nheld] = on;
                            hbest[nheld++] = o.best;
                            o.best = NULL;
                            ovars = NULL;
                            ocoefs = NULL;
                            on = 0u;
                            held = true;
                        }
                    }
                    if (stage)
                        stages[nstages++] = stage;
                    else if (!unsat)
                        ok = false;
                    smt_optimum_free(&o);
                } else {
                    ok = false;
                }
                free(cls);
                free(atoms);
                lra_parts_free(ovars, ocoefs, on, oconst);
                if (unsat || !held)
                    break;
            }
            for (uint32_t j = 0u; j < nheld; j++) {
                lra_parts_free(hv[j], hc[j], hn[j], NULL);
                free(hbest[j]);
            }
            if (ok)
                result = unsat ? unsat_value
                       : nobj == 1u ? (nstages ? stages[0] : NULL)
                       : atom_expr2(arena, atom_symbol(arena, "lexicographic"),
                                    atom_expr(arena, stages, nstages));
        } else if (ok && smtt_has_integers(&t)) {
            /* integer constants: branch and bound */
            uint32_t *ints = malloc(sizeof(*ints) * (t.p.reals.len ? t.p.reals.len : 1u));
            uint32_t nints = 0u;
            for (uint32_t i = 0u; ints && i < t.p.reals.len; i++) {
                const SmtFun *fn = smtt_fun(&t, t.p.reals.items[i]);
                if (fn && fn->integer && fn->arity == 0u)
                    ints[nints++] = i;
            }
            SmtSplit path[65];
            Atom *cert = NULL;
            SmtBbKind kind = ints ? smtt_bb(arena, &t, clauses, t.nclauses, eqs, ints, nints,
                                            path, 0u, &cert, error)
                                  : SMT_BB_ERROR;
            free(ints);
            if (kind == SMT_BB_SAT)
                result = atom_symbol(arena, "sat");
            else if (kind == SMT_BB_UNKNOWN)
                result = atom_symbol(arena, "unknown");
            else if (kind == SMT_BB_UNSAT && cert)
                result = atom_expr2(arena, atom_symbol(arena, "unsat"), cert);
        } else if (ok) {
            AspSmtTheories theories = {t.p.terms, t.p.nterms, eqs, t.neq,
                                       t.p.reals.len, t.p.lra, t.p.nlra};
            AspAnswer answer = asp_smt_solve(t.nvars, clauses, t.nclauses, &theories);
            if (answer.kind == ASP_ANSWER_SAT) {
                result = atom_symbol(arena, "sat");
            } else if (answer.kind == ASP_ANSWER_UNSAT) {
                Atom *cert = asp_certificate(arena, &answer);
                result = cert ? atom_expr2(arena, atom_symbol(arena, "unsat"), cert) : NULL;
            } else {
                *error = answer.kind == ASP_ANSWER_REFUSED
                    ? "the solver's answer was refused by its own check"
                    : "the solve did not finish";
            }
            asp_answer_free(&answer);
        }
        free(clauses);
        free(eqs);
    }
    if (!result && !*error)
        *error = t.error ? t.error : "out of memory";
    smt_text_free(&t);
    free(items);
    return result;
}

/* A model as the smt library gives it: its true atoms among the first
 * `natoms` variables, and with reals ((X V) ...). */
static Atom *smt_model_value(Arena *a, const SmtProblem *p, const AspAnswer *answer,
                             uint32_t natoms) {
    int32_t *vars = malloc(sizeof(*vars) * (answer->model_len ? answer->model_len : 1u));
    if (!vars)
        return NULL;
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < answer->model_len; i++)
        if (answer->model[i] >= 1 && (uint32_t)answer->model[i] <= natoms)
            vars[n++] = answer->model[i];
    Atom *atoms = sat_model_atoms(a, &p->cnf, vars, n);
    free(vars);
    if (!atoms || !p->reals.len)
        return atoms;
    Atom **values = arena_alloc(a, sizeof(*values) * p->reals.len);
    bool ok = values && answer->reals_len == p->reals.len;
    for (uint32_t i = 0u; ok && i < p->reals.len; i++) {
        Atom *v = smt_number_atom(a, answer->reals[i]);
        values[i] = v ? atom_expr2(a, p->reals.items[i], v) : NULL;
        ok = values[i] != NULL;
    }
    Atom *list = ok ? atom_expr(a, values, p->reals.len) : NULL;
    return list ? atom_expr2(a, atoms, list) : NULL;
}

/* (smt:minimize CLAUSES TERM) and (smt:maximize CLAUSES TERM): the least
 * (greatest) value of an arithmetic term over the clauses' models, by
 * linear search: each model's reals are moved to where the term is least
 * under its bounds, then the term is bounded below that value and the
 * search goes on, until the bound refutes.  (optimum V (A ...) VALUES
 * CERT) when V is attained, (infimum V ...) or (supremum V ...) when it is
 * only approached, (unbounded (A ...) VALUES) when there is no bound, and
 * (unsat CERT) when there is no model.  CERT refutes the clauses with one
 * more, (clause ((lt TERM V)) ()) for a least value (le when not attained,
 * gt and ge for a greatest), its atom numbered after the others. */
static Atom *smt_minimize_value(Arena *a, Atom *clauses, Atom *term, bool maximize,
                                const char **error) {
    SmtProblem p;
    *error = smt_read(a, clauses, &p);
    if (*error) {
        smt_problem_free(&p);
        return NULL;
    }
    LraLinear *obj = smt_linear(&p, term, 0u);
    LraLinear *goal = obj ? lra_linear_new() : NULL;
    if (!goal || !lra_linear_add_scaled(goal, obj, maximize ? "-1" : "1")) {
        *error = p.lra_error ? p.lra_error : "out of memory";
        lra_linear_free(obj);
        lra_linear_free(goal);
        smt_problem_free(&p);
        return NULL;
    }
    lra_linear_free(obj);
    uint32_t *ovars = NULL, on = 0u;
    char **ocoefs = NULL, *oconst = NULL;
    bool ok = smt_reals_apart(&p) && lra_linear_parts(goal, &ovars, &ocoefs, &on, &oconst);
    lra_linear_free(goal);
    if (!ok) {
        *error = p.lra_error ? p.lra_error : "out of memory";
        smt_problem_free(&p);
        return NULL;
    }
    uint32_t natoms = p.cnf.atoms.len;
    AspSmtTheories base = {p.terms, p.nterms, p.eqs, p.neqs, p.reals.len, p.lra, p.nlra};
    SmtOptimum o;
    Atom *result = NULL;
    if (smt_optimize(natoms, p.cnf.clauses, p.cnf.nclauses, &base, ovars, ocoefs, on, &o, error)) {
        Atom *cert = o.kind == SMT_OPT_UNBOUNDED ? NULL : asp_certificate(a, &o.last);
        if (o.kind == SMT_OPT_UNBOUNDED) {
            Atom *model = smt_model_value(a, &p, &o.last, natoms);
            result = model ? atom_expr2(a, atom_symbol(a, "unbounded"), model) : NULL;
        } else if (o.kind == SMT_OPT_UNSAT) {
            result = cert ? atom_expr2(a, atom_symbol(a, "unsat"), cert) : NULL;
        } else {
            Atom *value = smt_optimum_atom(a, &o, oconst, maximize);
            Atom *model = smt_model_value(a, &p, &o.best_answer, natoms);
            Atom *parts[4] = {atom_symbol(a, smt_optimum_name(&o, maximize)), value, model, cert};
            result = value && model && cert ? atom_expr(a, parts, 4u) : NULL;
        }
        smt_optimum_free(&o);
    }
    if (!result && !*error)
        *error = "out of memory";
    lra_parts_free(ovars, ocoefs, on, oconst);
    smt_problem_free(&p);
    return result;
}

bool cetta_smt_call(struct CettaLibraryContext *context, Arena *arena,
                    Atom *head, Atom **args, uint32_t nargs,
                    CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    bool text = head->sym_id == atom_symbol_literal_id("__cetta_lib_smt_solve_text");
    bool minimize = head->sym_id == atom_symbol_literal_id("__cetta_lib_smt_minimize");
    bool maximize = head->sym_id == atom_symbol_literal_id("__cetta_lib_smt_maximize");
    if (!text && !minimize && !maximize &&
        head->sym_id != atom_symbol_literal_id("__cetta_lib_smt_solve"))
        return false;
    const char *error = NULL;
    Atom *value = NULL;
    if (minimize || maximize) {
        if (nargs != 2u)
            error = "expected a list of clauses and an arithmetic term";
        else
            value = smt_minimize_value(arena, args[0], args[1], maximize, &error);
    } else if (text) {
        if (nargs != 1u)
            error = "expected an SMT-LIB script";
        else
            value = smt_text_value(arena, args[0], &error);
    } else {
        SmtProblem problem;
        if (nargs != 1u) {
            error = "expected a list of clauses";
            memset(&problem, 0, sizeof(problem));
        } else {
            error = smt_read(arena, args[0], &problem);
        }
        if (!error)
            value = smt_solve_value(arena, &problem, &error);
        smt_problem_free(&problem);
    }
    if (!value) {
        Atom *call = atom_expr(arena, (Atom *[]){head, nargs ? args[0] : head}, nargs ? 2u : 1u);
        value = atom_error(arena, call, atom_string(arena, error ? error : "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}

/* ---- the ipasir library: an IPASIR solver behind the sat library's shapes ----
 * (ipasir:solve CLAUSES) and (ipasir:models CLAUSES) answer as sat:solve and
 * sat:models do, the clauses read the same way, with (reference SIGNATURE),
 * the loaded solver's signature, where the sat library gives its certificate.
 * Without an IPASIR solver loadable its heads are not this library's to
 * answer. */

static Atom *ipasir_reference(Arena *a) {
    const char *sig = sat_ipasir_signature();
    Atom *head = atom_symbol(a, "reference");
    Atom *name = atom_string(a, sig ? sig : "ipasir");
    return head && name ? atom_expr2(a, head, name) : NULL;
}

/* the clause starts, then each found model blocked by one more clause */
typedef struct {
    int32_t *lits;
    uint32_t *start;
    uint32_t nclauses, nlits, caplits, capclauses;
} IpasirCnf;

static bool ipasir_push_clause(IpasirCnf *c, const int32_t *lits, uint32_t n) {
    if (c->nclauses + 2u > c->capclauses) {
        uint32_t cap = c->capclauses ? 2u * c->capclauses : 64u;
        uint32_t *start = realloc(c->start, sizeof(*start) * cap);
        if (!start)
            return false;
        c->start = start;
        c->capclauses = cap;
    }
    if (c->nlits + n > c->caplits) {
        uint32_t cap = c->caplits ? c->caplits : 256u;
        while (cap < c->nlits + n)
            cap *= 2u;
        int32_t *grown = realloc(c->lits, sizeof(*grown) * cap);
        if (!grown)
            return false;
        c->lits = grown;
        c->caplits = cap;
    }
    if (c->nclauses == 0u)
        c->start[0] = 0u;
    for (uint32_t k = 0u; k < n; k++)
        c->lits[c->nlits++] = lits[k];
    c->start[++c->nclauses] = c->nlits;
    return true;
}

static Atom *ipasir_value(Arena *a, const SatCnf *f, bool all, const char **error) {
    uint32_t n = f->atoms.len;
    IpasirCnf c = {0};
    uint8_t *val = calloc(n + 1u, 1u);
    int32_t *vars = malloc(sizeof(*vars) * (n ? n : 1u));
    int32_t *block = malloc(sizeof(*block) * (n ? n : 1u));
    Atom **models = NULL;
    uint32_t nmodels = 0u, capmodels = 0u;
    Atom *result = NULL;
    bool ok = val && vars && block;
    for (uint32_t i = 0u; ok && i < f->nclauses; i++)
        ok = ipasir_push_clause(&c, f->clauses[i].lits, f->clauses[i].len);
    while (ok) {
        SatIpasirResult r = sat_ipasir_solve(n, c.lits, c.start, c.nclauses, val);
        if (r == SAT_IPASIR_FAILED) {
            *error = "the IPASIR solver gave no verdict";
            break;
        }
        if (r == SAT_IPASIR_UNSAT) {
            Atom *ref = ipasir_reference(a);
            if (!all) {
                Atom *head = atom_symbol(a, "unsat");
                result = ref && head ? atom_expr2(a, head, ref) : NULL;
            } else {
                Atom *list = atom_expr(a, models, nmodels);
                Atom *parts[3] = {atom_symbol(a, "models"), list, ref};
                result = list && ref && parts[0] ? atom_expr(a, parts, 3u) : NULL;
            }
            break;
        }
        uint32_t len = 0u;
        for (uint32_t v = 1u; v <= n; v++)
            if (val[v])
                vars[len++] = (int32_t)v;
        Atom *model = sat_model_atoms(a, f, vars, len);
        if (!model)
            break;
        if (!all) {
            Atom *head = atom_symbol(a, "model");
            result = head ? atom_expr2(a, head, model) : NULL;
            break;
        }
        if (nmodels == capmodels) {
            capmodels = capmodels ? 2u * capmodels : 8u;
            Atom **grown = realloc(models, sizeof(*grown) * capmodels);
            if (!grown)
                break;
            models = grown;
        }
        models[nmodels++] = model;
        for (uint32_t v = 1u; v <= n; v++)
            block[v - 1u] = val[v] ? -(int32_t)v : (int32_t)v;
        ok = ipasir_push_clause(&c, block, n);
    }
    free(models);
    free(c.lits);
    free(c.start);
    free(val);
    free(vars);
    free(block);
    return result;
}

bool cetta_ipasir_call(struct CettaLibraryContext *context, Arena *arena,
                       Atom *head, Atom **args, uint32_t nargs,
                       CettaCallOutcome *out) {
    (void)context;
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    bool solve = head->sym_id == g_builtin_syms.lib_ipasir_solve;
    bool models = head->sym_id == g_builtin_syms.lib_ipasir_models;
    if ((!solve && !models) || !sat_ipasir_available())
        return false;
    const char *error = NULL;
    Atom *value = NULL;
    SatCnf cnf;
    if (nargs != 1u) {
        error = "expected a list of clauses";
        memset(&cnf, 0, sizeof(cnf));
    } else {
        error = sat_read(args[0], &cnf);
    }
    if (!error)
        value = ipasir_value(arena, &cnf, models, &error);
    sat_cnf_free(&cnf);
    if (!value) {
        Atom *call = atom_expr(arena, (Atom *[]){head, nargs ? args[0] : head}, nargs ? 2u : 1u);
        value = atom_error(arena, call, atom_string(arena, error ? error : "out of memory"));
    }
    *out = (CettaCallOutcome){.kind = CETTA_CALL_VALUE, .term = value,
                              .result_form = CETTA_CALL_RESULT_EXPRESSION};
    return value != NULL;
}
