#include "petta_clpfd.h"

#include <stdlib.h>
#include <string.h>

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

/* ── Integers ───────────────────────────────────────────────────────────
 * Exact integers with GMP; machine integers otherwise, where a result
 * outside their range is SWI's evaluation_error(int_overflow) without
 * big integers. */

typedef struct {
#if CETTA_BUILD_WITH_GMP
    mpz_t v;
#else
    int64_t v;
#endif
} Pic;

typedef struct {
    Arena *arena;
    bool overflow;
    Atom *error;
} ClpContext;

static void pic_init(Pic *x) {
#if CETTA_BUILD_WITH_GMP
    mpz_init(x->v);
#else
    x->v = 0;
#endif
}

static void pic_clear(Pic *x) {
#if CETTA_BUILD_WITH_GMP
    mpz_clear(x->v);
#else
    (void)x;
#endif
}

static void pic_set_si(Pic *x, int64_t value) {
#if CETTA_BUILD_WITH_GMP
    mpz_set_si(x->v, (long)value);
#else
    x->v = value;
#endif
}

static void pic_set(Pic *x, const Pic *y) {
#if CETTA_BUILD_WITH_GMP
    mpz_set(x->v, y->v);
#else
    x->v = y->v;
#endif
}

static int pic_sgn(const Pic *x) {
#if CETTA_BUILD_WITH_GMP
    return mpz_sgn(x->v);
#else
    return (x->v > 0) - (x->v < 0);
#endif
}

static int pic_cmp(const Pic *x, const Pic *y) {
#if CETTA_BUILD_WITH_GMP
    int c = mpz_cmp(x->v, y->v);
    return (c > 0) - (c < 0);
#else
    return (x->v > y->v) - (x->v < y->v);
#endif
}

static void pic_add(ClpContext *cx, Pic *r, const Pic *a, const Pic *b) {
#if CETTA_BUILD_WITH_GMP
    (void)cx;
    mpz_add(r->v, a->v, b->v);
#else
    if (__builtin_add_overflow(a->v, b->v, &r->v))
        cx->overflow = true;
#endif
}

static void pic_mul(ClpContext *cx, Pic *r, const Pic *a, const Pic *b) {
#if CETTA_BUILD_WITH_GMP
    (void)cx;
    mpz_mul(r->v, a->v, b->v);
#else
    if (__builtin_mul_overflow(a->v, b->v, &r->v))
        cx->overflow = true;
#endif
}

static void pic_neg(ClpContext *cx, Pic *r, const Pic *a) {
#if CETTA_BUILD_WITH_GMP
    (void)cx;
    mpz_neg(r->v, a->v);
#else
    if (a->v == INT64_MIN)
        cx->overflow = true;
    else
        r->v = -a->v;
#endif
}

/* b divides a (b nonzero). */
static bool pic_divisible(const Pic *a, const Pic *b) {
#if CETTA_BUILD_WITH_GMP
    return mpz_divisible_p(a->v, b->v) != 0;
#else
    return b->v == -1 || a->v % b->v == 0;
#endif
}

typedef enum { PIC_FLOOR_QUOTIENT, PIC_TRUNC_QUOTIENT, PIC_FLOOR_REMAINDER,
               PIC_EXACT_QUOTIENT } PicDivision;

/* The division `kind` of a by b (b nonzero). */
static void pic_divide(ClpContext *cx, PicDivision kind, Pic *r, const Pic *a,
                       const Pic *b) {
#if CETTA_BUILD_WITH_GMP
    (void)cx;
    switch (kind) {
    case PIC_FLOOR_QUOTIENT: mpz_fdiv_q(r->v, a->v, b->v); break;
    case PIC_TRUNC_QUOTIENT: mpz_tdiv_q(r->v, a->v, b->v); break;
    case PIC_FLOOR_REMAINDER: mpz_fdiv_r(r->v, a->v, b->v); break;
    case PIC_EXACT_QUOTIENT: mpz_divexact(r->v, a->v, b->v); break;
    }
#else
    if (b->v == -1) {
        if (kind == PIC_FLOOR_REMAINDER) {
            r->v = 0;
        } else if (a->v == INT64_MIN) {
            cx->overflow = true;
        } else {
            r->v = -a->v;
        }
        return;
    }
    int64_t q = a->v / b->v, m = a->v % b->v;
    bool adjust = m != 0 && ((m < 0) != (b->v < 0));
    switch (kind) {
    case PIC_FLOOR_QUOTIENT: r->v = adjust ? q - 1 : q; break;
    case PIC_TRUNC_QUOTIENT:
    case PIC_EXACT_QUOTIENT: r->v = q; break;
    case PIC_FLOOR_REMAINDER: r->v = adjust ? m + b->v : m; break;
    }
#endif
}

static bool pic_from_atom(Pic *x, Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED)
        return false;
    if (atom->ground.gkind == GV_INT) {
        pic_set_si(x, atom->ground.ival);
        return true;
    }
#if CETTA_BUILD_WITH_GMP
    if (atom->ground.gkind == GV_BIGINT)
        return atom_bigint_get_mpz(atom, x->v);
#endif
    return false;
}

static Atom *pic_to_atom(Arena *arena, const Pic *x) {
#if CETTA_BUILD_WITH_GMP
    return atom_bigint_from_mpz(arena, x->v);
#else
    return atom_int(arena, x->v);
#endif
}

/* ── Names ──────────────────────────────────────────────────────────── */

/* A Prolog compound Name(A) or Name(A, B), as PeTTa reads SWI's terms: an
 * expression in a compound carrier, never a list (car-atom of an error's
 * formal term answers as for any non-list). */
static Atom *clp_term(Arena *arena, const char *name, Atom *a, Atom *b) {
    Atom *head = atom_symbol(arena, name);
    Atom *body = b ? atom_expr3(arena, head, a, b) : atom_expr2(arena, head, a);
    return body ? atom_petta_prolog_compound(arena, body) : NULL;
}


static const char *const k_relation_names[] = {
    "#=", "#\\=", "#<", "#>", "#=<", "#>=",
};

static bool clp_relation_named(const char *name, PettaClpfdRelation *out) {
    for (size_t k = 0; k < sizeof(k_relation_names) / sizeof(k_relation_names[0]);
         k++) {
        if (strcmp(name, k_relation_names[k]) == 0) {
            if (out)
                *out = (PettaClpfdRelation)k;
            return true;
        }
    }
    return false;
}

bool petta_clpfd_function(SymbolId head, const char **op) {
    static const struct {
        const char *name;
        const char *op;
    } functions[] = {
        {"#+", "+"}, {"#-", "-"}, {"#*", "*"}, {"#div", "div"},
        {"#//", "//"}, {"#mod", "mod"}, {"#min", "min"}, {"#max", "max"},
    };
    const char *name = head == SYMBOL_ID_NONE ? NULL
                                              : symbol_bytes(g_symbols, head);
    if (!name || name[0] != '#')
        return false;
    for (size_t k = 0; k < sizeof(functions) / sizeof(functions[0]); k++) {
        if (strcmp(name, functions[k].name) == 0) {
            if (op)
                *op = functions[k].op;
            return true;
        }
    }
    return false;
}

bool petta_clpfd_reified(SymbolId head, PettaClpfdRelation *relation) {
    const char *name = head == SYMBOL_ID_NONE ? NULL
                                              : symbol_bytes(g_symbols, head);
    PettaClpfdRelation found;
    if (!name || name[0] != '#' || !clp_relation_named(name, &found) ||
        found == PETTA_CLPFD_LE || found == PETTA_CLPFD_GE)
        return false;
    if (relation)
        *relation = found;
    return true;
}

/* ── Goal terms ─────────────────────────────────────────────────────── */

static Atom *clp_compound(Arena *arena, const char *name, Atom *a, Atom *b) {
    Atom *body = atom_expr3(arena, atom_symbol(arena, name), a, b);
    return body ? atom_petta_prolog_compound(arena, body) : NULL;
}

Atom *petta_clpfd_function_goal(Arena *arena, const char *op, Atom *a,
                                Atom *b, Atom *result) {
    Atom *expression = clp_compound(arena, op, a, b);
    return expression ? clp_compound(arena, "#=", result, expression) : NULL;
}

Atom *petta_clpfd_relation_goal(Arena *arena, PettaClpfdRelation relation,
                                Atom *a, Atom *b) {
    return clp_compound(arena, k_relation_names[relation], a, b);
}

/* The body (name arg...) of a compound side: a Prolog compound carrier,
 * or, at a goal's top, the goal as written. */
static Atom *clp_compound_body(Atom *atom) {
    Atom *body = NULL;
    if (atom_petta_prolog_compound_body(atom, &body) ||
        atom_prolog_compound_body(atom, &body))
        return body;
    return NULL;
}

static bool clp_relation_parts(Atom *goal, PettaClpfdRelation *relation,
                               Atom **left, Atom **right) {
    Atom *body = clp_compound_body(goal);
    if (!body)
        body = goal;
    if (!body || body->kind != ATOM_EXPR || body->expr.len != 3u ||
        body->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    const char *name = symbol_bytes(g_symbols, body->expr.elems[0]->sym_id);
    if (!name || !clp_relation_named(name, relation))
        return false;
    *left = body->expr.elems[1];
    *right = body->expr.elems[2];
    return true;
}

bool petta_clpfd_is_relation_goal(Atom *goal) {
    PettaClpfdRelation relation;
    Atom *left = NULL, *right = NULL;
    return clp_relation_parts(goal, &relation, &left, &right);
}

/* ── Linear forms ───────────────────────────────────────────────────── */

typedef struct {
    Atom *variable;
    Pic coefficient;
} LinTerm;

/* sum of coefficient * variable, plus constant. */
typedef struct {
    LinTerm *terms;
    uint32_t len;
    uint32_t cap;
    Pic constant;
    /* A product of unknowns, or a div, //, mod, min or max with an unknown
     * operand: the side is not linear. */
    bool nonlinear;
    /* A div, // or mod by zero: the side has no value. */
    bool undefined;
} Lin;

static void lin_init(Lin *lin) {
    memset(lin, 0, sizeof(*lin));
    pic_init(&lin->constant);
}

static void lin_clear(Lin *lin) {
    for (uint32_t k = 0u; k < lin->len; k++)
        pic_clear(&lin->terms[k].coefficient);
    free(lin->terms);
    pic_clear(&lin->constant);
    memset(lin, 0, sizeof(*lin));
}

static bool lin_is_constant(const Lin *lin) {
    return !lin->nonlinear && !lin->undefined && lin->len == 0u;
}

/* lin += scale * variable. */
static bool lin_add_variable(ClpContext *cx, Lin *lin, Atom *variable,
                             const Pic *scale) {
    for (uint32_t k = 0u; k < lin->len; k++) {
        if (var_id_eq(lin->terms[k].variable->var_id, variable->var_id)) {
            pic_add(cx, &lin->terms[k].coefficient,
                    &lin->terms[k].coefficient, scale);
            return true;
        }
    }
    if (lin->len == lin->cap) {
        uint32_t next = lin->cap ? lin->cap * 2u : 4u;
        LinTerm *grown = cetta_realloc(lin->terms, sizeof(*grown) * next);
        if (!grown)
            return false;
        lin->terms = grown;
        lin->cap = next;
    }
    LinTerm *term = &lin->terms[lin->len++];
    term->variable = variable;
    pic_init(&term->coefficient);
    pic_set(&term->coefficient, scale);
    return true;
}

/* lin += scale * other. */
static bool lin_add_scaled_lin(ClpContext *cx, Lin *lin, const Lin *other,
                               const Pic *scale) {
    Pic product;
    pic_init(&product);
    bool ok = true;
    for (uint32_t k = 0u; ok && k < other->len; k++) {
        pic_mul(cx, &product, &other->terms[k].coefficient, scale);
        ok = lin_add_variable(cx, lin, other->terms[k].variable, &product);
    }
    pic_mul(cx, &product, &other->constant, scale);
    pic_add(cx, &lin->constant, &lin->constant, &product);
    pic_clear(&product);
    lin->nonlinear = lin->nonlinear || other->nonlinear;
    lin->undefined = lin->undefined || other->undefined;
    return ok;
}

static bool clp_domain_error(ClpContext *cx, Atom *side) {
    Atom *formal = clp_term(cx->arena, "domain_error",
                            atom_symbol(cx->arena, "clpfd_expression"),
                            side);
    cx->error = atom_error(cx->arena, formal,
                           atom_var_with_id(cx->arena, "_", fresh_var_id()));
    return false;
}

static bool lin_of(ClpContext *cx, Atom *side, Lin *out);

/* lin += scale * side. */
static bool lin_add_scaled(ClpContext *cx, Lin *lin, Atom *side,
                           const Pic *scale) {
    Lin part;
    lin_init(&part);
    bool ok = lin_of(cx, side, &part) &&
              lin_add_scaled_lin(cx, lin, &part, scale);
    lin_clear(&part);
    return ok;
}

/* The linear form of one side. */
static bool lin_of(ClpContext *cx, Atom *side, Lin *out) {
    if (!side)
        return false;
    if (side->kind == ATOM_VAR) {
        Pic one;
        pic_init(&one);
        pic_set_si(&one, 1);
        bool ok = lin_add_variable(cx, out, side, &one);
        pic_clear(&one);
        return ok;
    }
    if (pic_from_atom(&out->constant, side))
        return true;
    Atom *body = clp_compound_body(side);
    if (!body || body->expr.len < 2u || body->expr.len > 3u)
        return clp_domain_error(cx, side);
    const char *op = symbol_bytes(g_symbols, body->expr.elems[0]->sym_id);
    if (!op)
        return clp_domain_error(cx, side);
    Pic one, minus_one;
    pic_init(&one);
    pic_init(&minus_one);
    pic_set_si(&one, 1);
    pic_set_si(&minus_one, -1);
    bool ok = true;
    if (body->expr.len == 2u) {
        if (strcmp(op, "-") == 0)
            ok = lin_add_scaled(cx, out, body->expr.elems[1], &minus_one);
        else if (strcmp(op, "+") == 0)
            ok = lin_add_scaled(cx, out, body->expr.elems[1], &one);
        else
            ok = clp_domain_error(cx, side);
    } else if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0) {
        ok = lin_add_scaled(cx, out, body->expr.elems[1], &one) &&
             lin_add_scaled(cx, out, body->expr.elems[2],
                            op[0] == '+' ? &one : &minus_one);
    } else {
        Lin a, b;
        lin_init(&a);
        lin_init(&b);
        ok = lin_of(cx, body->expr.elems[1], &a) &&
             lin_of(cx, body->expr.elems[2], &b);
        if (ok && strcmp(op, "*") == 0) {
            if (lin_is_constant(&a))
                ok = lin_add_scaled_lin(cx, out, &b, &a.constant);
            else if (lin_is_constant(&b))
                ok = lin_add_scaled_lin(cx, out, &a, &b.constant);
            else
                out->nonlinear = true;
        } else if (ok && (strcmp(op, "div") == 0 || strcmp(op, "//") == 0 ||
                          strcmp(op, "mod") == 0 || strcmp(op, "min") == 0 ||
                          strcmp(op, "max") == 0)) {
            if (a.undefined || b.undefined) {
                out->undefined = true;
            } else if (!lin_is_constant(&a) || !lin_is_constant(&b)) {
                out->nonlinear = true;
            } else if (op[0] == 'm' && op[1] == 'i') {
                pic_add(cx, &out->constant, &out->constant,
                        pic_cmp(&a.constant, &b.constant) <= 0
                            ? &a.constant : &b.constant);
            } else if (op[0] == 'm' && op[1] == 'a') {
                pic_add(cx, &out->constant, &out->constant,
                        pic_cmp(&a.constant, &b.constant) >= 0
                            ? &a.constant : &b.constant);
            } else if (pic_sgn(&b.constant) == 0) {
                out->undefined = true;
            } else {
                Pic value;
                pic_init(&value);
                pic_divide(cx,
                           strcmp(op, "div") == 0 ? PIC_FLOOR_QUOTIENT
                           : strcmp(op, "//") == 0 ? PIC_TRUNC_QUOTIENT
                                                    : PIC_FLOOR_REMAINDER,
                           &value, &a.constant, &b.constant);
                pic_add(cx, &out->constant, &out->constant, &value);
                pic_clear(&value);
            }
        } else if (ok) {
            ok = clp_domain_error(cx, side);
        }
        lin_clear(&a);
        lin_clear(&b);
    }
    pic_clear(&one);
    pic_clear(&minus_one);
    return ok;
}

/* ── Decision ───────────────────────────────────────────────────────── */

static bool clp_compare_holds(PettaClpfdRelation relation, int sign) {
    switch (relation) {
    case PETTA_CLPFD_EQ: return sign == 0;
    case PETTA_CLPFD_NE: return sign != 0;
    case PETTA_CLPFD_LT: return sign < 0;
    case PETTA_CLPFD_GT: return sign > 0;
    case PETTA_CLPFD_LE: return sign <= 0;
    case PETTA_CLPFD_GE: return sign >= 0;
    }
    return false;
}

bool petta_clpfd_decide(Arena *arena, Atom *goal, PettaClpfdDecision *out) {
    if (!arena || !goal || !out)
        return false;
    *out = (PettaClpfdDecision){.kind = PETTA_CLPFD_FAILS};
    PettaClpfdRelation relation;
    Atom *left = NULL, *right = NULL;
    if (!clp_relation_parts(goal, &relation, &left, &right))
        return false;
    ClpContext cx = {.arena = arena};
    Lin difference;
    lin_init(&difference);
    Pic one, minus_one;
    pic_init(&one);
    pic_init(&minus_one);
    pic_set_si(&one, 1);
    pic_set_si(&minus_one, -1);
    /* left - right, compared with 0. */
    bool ok = lin_add_scaled(&cx, &difference, left, &one) &&
              lin_add_scaled(&cx, &difference, right, &minus_one);
    pic_clear(&one);
    pic_clear(&minus_one);
    if (!ok) {
        lin_clear(&difference);
        if (!cx.error)
            return false;
        out->kind = PETTA_CLPFD_RAISES;
        out->error = cx.error;
        return true;
    }
    if (cx.overflow) {
        lin_clear(&difference);
        Atom *formal = clp_term(arena, "evaluation_error",
                                atom_symbol(arena, "int_overflow"), NULL);
        out->kind = PETTA_CLPFD_RAISES;
        out->error = atom_error(arena, formal,
                                atom_var_with_id(arena, "_", fresh_var_id()));
        return true;
    }
    /* Unknowns whose coefficients cancelled are no longer unknowns. */
    uint32_t unknowns = 0u;
    for (uint32_t k = 0u; k < difference.len; k++) {
        if (pic_sgn(&difference.terms[k].coefficient) != 0)
            difference.terms[unknowns++] = difference.terms[k];
        else
            pic_clear(&difference.terms[k].coefficient);
    }
    difference.len = unknowns;
    if (difference.undefined) {
        out->kind = PETTA_CLPFD_FAILS;
    } else if (difference.nonlinear) {
        out->kind = PETTA_CLPFD_RESIDUAL;
    } else if (unknowns == 0u) {
        out->kind = clp_compare_holds(relation, pic_sgn(&difference.constant))
            ? PETTA_CLPFD_HOLDS : PETTA_CLPFD_FAILS;
    } else if (unknowns == 1u &&
               (relation == PETTA_CLPFD_EQ || relation == PETTA_CLPFD_NE)) {
        LinTerm *term = &difference.terms[0];
        Pic minus_k;
        pic_init(&minus_k);
        pic_neg(&cx, &minus_k, &difference.constant);
        bool divides = pic_divisible(&minus_k, &term->coefficient);
        if (relation == PETTA_CLPFD_NE) {
            out->kind = divides ? PETTA_CLPFD_RESIDUAL : PETTA_CLPFD_HOLDS;
        } else if (!divides) {
            out->kind = PETTA_CLPFD_FAILS;
        } else {
            Pic value;
            pic_init(&value);
            pic_divide(&cx, PIC_EXACT_QUOTIENT, &value, &minus_k,
                       &term->coefficient);
            out->kind = PETTA_CLPFD_BINDS;
            out->variable = term->variable;
            out->value = cx.overflow ? NULL : pic_to_atom(arena, &value);
            pic_clear(&value);
            ok = cx.overflow || out->value != NULL;
        }
        pic_clear(&minus_k);
    } else {
        out->kind = PETTA_CLPFD_RESIDUAL;
    }
    lin_clear(&difference);
    if (!ok)
        return false;
    if (cx.overflow) {
        Atom *formal = clp_term(arena, "evaluation_error",
                                atom_symbol(arena, "int_overflow"), NULL);
        *out = (PettaClpfdDecision){
            .kind = PETTA_CLPFD_RAISES,
            .error = atom_error(arena, formal,
                                atom_var_with_id(arena, "_", fresh_var_id())),
        };
        return true;
    }
    if (out->kind == PETTA_CLPFD_RESIDUAL) {
        out->goal = petta_clpfd_relation_goal(arena, relation, left, right);
        if (!out->goal)
            return false;
    }
    return true;
}
