#include "petta_arithmetic.h"

#include "petta_semantics.h"
#include "symbol.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

/* SWI's number types, in its promotion order: an operation on two numbers
 * first brings both to the later of their types. */
typedef enum {
    PA_INT = 0,
    PA_MPZ,
    PA_MPQ,
    PA_FLOAT,
} PaType;

typedef struct {
    PaType type;
    int64_t i;
    double f;
#if CETTA_BUILD_WITH_GMP
    mpz_t z;
    mpq_t q;
#endif
} PaNumber;

typedef struct {
    Arena *arena;
    PettaArithmeticFlags flags;
    /* The error the evaluation raised, when it returned false. */
    Atom *error;
} PaEval;

PettaArithmeticFlags petta_arithmetic_flags(void) {
    PettaArithmeticFlags flags = {0};
    if (petta_libpl_arithmetic_flags)
        (void)petta_libpl_arithmetic_flags(&flags);
    return flags;
}

static void pa_clear(PaNumber *n) {
#if CETTA_BUILD_WITH_GMP
    if (n->type == PA_MPZ)
        mpz_clear(n->z);
    else if (n->type == PA_MPQ)
        mpq_clear(n->q);
#endif
    n->type = PA_INT;
    n->i = 0;
}

static void pa_set_int(PaNumber *n, int64_t value) {
    n->type = PA_INT;
    n->i = value;
}

static void pa_set_float(PaNumber *n, double value) {
    n->type = PA_FLOAT;
    n->f = value;
}

/* ── Errors ─────────────────────────────────────────────────────────── */

/* A Prolog compound Name(A) or Name(A, B), as PeTTa reads SWI's terms: an
 * expression in a compound carrier, never a list (car-atom of an error's
 * formal term answers as for any non-list). */
static Atom *pa_compound(Arena *arena, const char *name, Atom *a, Atom *b) {
    Atom *head = atom_symbol(arena, name);
    Atom *body = b ? atom_expr3(arena, head, a, b) : atom_expr2(arena, head, a);
    return body ? atom_petta_prolog_compound(arena, body) : NULL;
}


static Atom *pa_unbound(Arena *arena) {
    return atom_var_with_id(arena, "_", fresh_var_id());
}

/* context(Name/Arity, _), or context(system:Name/Arity, _). */
static Atom *pa_context(Arena *arena, bool system, const char *name,
                        int64_t arity) {
    Atom *indicator = pa_compound(arena, "/",
                                  atom_symbol(arena, name),
                                  atom_int(arena, arity));
    if (system)
        indicator = pa_compound(arena, ":",
                                atom_symbol(arena, "system"), indicator);
    return pa_compound(arena, "context", indicator,
                       pa_unbound(arena));
}

static bool pa_raise(PaEval *ev, Atom *formal, Atom *context) {
    ev->error = atom_error(ev->arena, formal, context);
    return false;
}

/* An error is/2 itself raises: context(system:(is)/2, _). */
static bool pa_raise_in_is(PaEval *ev, Atom *formal) {
    return pa_raise(ev, formal, pa_context(ev->arena, true, "is", 2));
}

static bool pa_evaluation_error(PaEval *ev, const char *what,
                                const char *function) {
    Atom *formal = pa_compound(ev->arena, "evaluation_error",
                               atom_symbol(ev->arena, what), NULL);
    if (!function)
        return pa_raise_in_is(ev, formal);
    return pa_raise(ev, formal, pa_context(ev->arena, false, function, 2));
}

/* ── Conversion between atoms and numbers ───────────────────────────── */

static Atom *pa_number_atom(Arena *arena, const PaNumber *n) {
    switch (n->type) {
    case PA_INT:
        return atom_int(arena, n->i);
    case PA_FLOAT:
        return atom_float(arena, n->f);
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ:
        return atom_bigint_from_mpz(arena, n->z);
    case PA_MPQ:
        return atom_rational_from_mpq(arena, n->q);
#else
    default:
        break;
#endif
    }
    return NULL;
}

#if CETTA_BUILD_WITH_GMP
/* A big integer that fits a machine integer becomes one, and a rational
 * whose denominator is 1 an integer: SWI's canonical forms. */
static void pa_normalize(PaNumber *n) {
    if (n->type == PA_MPQ &&
        mpz_cmp_ui(mpq_denref(n->q), 1u) == 0) {
        mpz_init_set(n->z, mpq_numref(n->q));
        mpq_clear(n->q);
        n->type = PA_MPZ;
    }
    if (n->type == PA_MPZ && mpz_fits_slong_p(n->z)) {
        int64_t value = (int64_t)mpz_get_si(n->z);
        mpz_clear(n->z);
        pa_set_int(n, value);
    }
}

/* The double nearest to the exact rational q, ties to even, as SWI's
 * mpz_to_double and mpq_to_double give it.  mpq_get_d truncates; the
 * neighbour away from zero is taken when q lies past their midpoint. */
static double pa_mpq_nearest_double(const mpq_t q) {
    int sign = mpq_sgn(q);
    if (sign == 0)
        return 0.0;
    double near = mpq_get_d(q);
    if (isinf(near))
        return near;
    double far = nextafter(near, sign > 0 ? INFINITY : -INFINITY);
    mpq_t magnitude, midpoint, step;
    mpq_inits(magnitude, midpoint, step, NULL);
    mpq_abs(magnitude, q);
    if (isinf(far)) {
        /* Past DBL_MAX: the midpoint is half a unit of DBL_MAX above it. */
        double below = nextafter(fabs(near), 0.0);
        mpq_set_d(midpoint, fabs(near));
        mpq_set_d(step, fabs(near) - below);
        mpz_mul_2exp(mpq_denref(step), mpq_denref(step), 1u);
        mpq_canonicalize(step);
        mpq_add(midpoint, midpoint, step);
    } else {
        mpq_set_d(midpoint, fabs(near));
        mpq_set_d(step, fabs(far));
        mpq_add(midpoint, midpoint, step);
        mpz_mul_2exp(mpq_denref(midpoint), mpq_denref(midpoint), 1u);
        mpq_canonicalize(midpoint);
    }
    int order = mpq_cmp(magnitude, midpoint);
    mpq_clears(magnitude, midpoint, step, NULL);
    if (order > 0)
        return far;
    if (order < 0)
        return near;
    uint64_t bits = 0u;
    memcpy(&bits, &near, sizeof(bits));
    return (bits & 1u) ? far : near;
}

static double pa_mpz_nearest_double(const mpz_t z) {
    mpq_t q;
    mpq_init(q);
    mpq_set_z(q, z);
    double value = pa_mpq_nearest_double(q);
    mpq_clear(q);
    return value;
}
#endif

/* ── Float results ──────────────────────────────────────────────────── */

/* SWI's check_float: an undefined (NaN), infinite or subnormal float
 * result raises, unless the float flags let it stand. */
static bool pa_check_float(PaEval *ev, PaNumber *n) {
    switch (fpclassify(n->f)) {
    case FP_NAN:
        if (ev->flags.undefined_nan)
            return true;
        return pa_evaluation_error(ev, "undefined", NULL);
    case FP_INFINITE:
        if (ev->flags.overflow_infinity)
            return true;
        return pa_evaluation_error(ev, "float_overflow", NULL);
    case FP_SUBNORMAL:
        if (!ev->flags.underflow_error)
            return true;
        return pa_evaluation_error(ev, "float_underflow", NULL);
    default:
        return true;
    }
}

/* A zero divisor: an error, unless the float flags make the quotient NaN
 * (a zero dividend) or a signed infinity. */
static bool pa_zero_divisor(PaEval *ev, int dividend_sign, PaNumber *r,
                            const char *function) {
    if (dividend_sign == 0) {
        if (ev->flags.undefined_nan) {
            pa_set_float(r, NAN);
            return true;
        }
    } else if (ev->flags.zero_div_infinity) {
        pa_set_float(r, dividend_sign > 0 ? INFINITY : -INFINITY);
        return true;
    }
    return pa_evaluation_error(ev, "zero_divisor", function);
}

/* SWI's promoteToFloatNumber: an exact number becomes the nearest double,
 * which must itself be a valid float. */
static bool pa_to_float(PaEval *ev, PaNumber *n) {
    switch (n->type) {
    case PA_FLOAT:
        return true;
    case PA_INT:
        pa_set_float(n, (double)n->i);
        return true;
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ: {
        double value = pa_mpz_nearest_double(n->z);
        pa_clear(n);
        pa_set_float(n, value);
        return pa_check_float(ev, n);
    }
    case PA_MPQ: {
        double value = pa_mpq_nearest_double(n->q);
        pa_clear(n);
        pa_set_float(n, value);
        return pa_check_float(ev, n);
    }
#else
    default:
        (void)ev;
        return false;
#endif
    }
    return false;
}

#if CETTA_BUILD_WITH_GMP
static void pa_to_mpz(PaNumber *n) {
    if (n->type == PA_INT) {
        int64_t value = n->i;
        mpz_init_set_si(n->z, (long)value);
        n->type = PA_MPZ;
    }
}

static void pa_to_mpq(PaNumber *n) {
    if (n->type == PA_INT || n->type == PA_MPZ) {
        pa_to_mpz(n);
        mpq_init(n->q);
        mpq_set_z(n->q, n->z);
        mpz_clear(n->z);
        n->type = PA_MPQ;
    }
}
#endif

/* Bring both numbers to the later of their types. */
static bool pa_same_type(PaEval *ev, PaNumber *a, PaNumber *b) {
    PaType target = a->type > b->type ? a->type : b->type;
    PaNumber *ns[2] = {a, b};
    for (int k = 0; k < 2; k++) {
        PaNumber *n = ns[k];
        if (n->type == target)
            continue;
        switch (target) {
        case PA_FLOAT:
            if (!pa_to_float(ev, n))
                return false;
            break;
#if CETTA_BUILD_WITH_GMP
        case PA_MPQ:
            pa_to_mpq(n);
            break;
        case PA_MPZ:
            pa_to_mpz(n);
            break;
#endif
        default:
            break;
        }
    }
    return true;
}

static int pa_sign(const PaNumber *n) {
    switch (n->type) {
    case PA_INT:
        return (n->i > 0) - (n->i < 0);
    case PA_FLOAT:
        return (n->f > 0.0) - (n->f < 0.0);
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ:
        return mpz_sgn(n->z);
    case PA_MPQ:
        return mpq_sgn(n->q);
#else
    default:
        break;
#endif
    }
    return 0;
}

static bool pa_is_integer(const PaNumber *n) {
    return n->type == PA_INT || n->type == PA_MPZ;
}

/* ── Operands ───────────────────────────────────────────────────────── */

/* A symbol is a named constant of SWI's arithmetic, or else not
 * evaluable.  -inf, the dual of inf, is CeTTa's at PeTTa's numeric
 * boundary as well. */
static bool pa_constant(PaEval *ev, Atom *atom, PaNumber *out) {
    const char *name = symbol_bytes(g_symbols, atom->sym_id);
    if (!name)
        name = "";
    if (strcmp(name, "pi") == 0) {
        pa_set_float(out, acos(-1.0));
    } else if (strcmp(name, "e") == 0) {
        pa_set_float(out, exp(1.0));
    } else if (strcmp(name, "inf") == 0) {
        pa_set_float(out, INFINITY);
    } else if (strcmp(name, "-inf") == 0) {
        pa_set_float(out, -INFINITY);
    } else if (strcmp(name, "nan") == 0) {
        pa_set_float(out, NAN);
    } else if (strcmp(name, "epsilon") == 0) {
        pa_set_float(out, DBL_EPSILON);
    } else if (strcmp(name, "cputime") == 0) {
        pa_set_float(out, (double)clock() / (double)CLOCKS_PER_SEC);
    } else if (strcmp(name, "random_float") == 0) {
        double value = 0.0;
        while (value == 0.0)
            value = (double)rand() / ((double)RAND_MAX + 1.0);
        pa_set_float(out, value);
    } else {
        Atom *indicator = pa_compound(ev->arena, "/",
                                      atom, atom_int(ev->arena, 0));
        Atom *formal = pa_compound(ev->arena, "type_error",
                                   atom_symbol(ev->arena, "evaluable"),
                                   indicator);
        return pa_raise_in_is(ev, formal);
    }
    return true;
}

/* metta.pl's arith_arg/1: a string or an expression (a Prolog list) is
 * not evaluable, whatever SWI would make of it.  Its error is PeTTa's own
 * (petta_semantics_arithmetic_operand_error), raised before is/2 runs. */
static bool pa_arith_arg_ok(PaEval *ev, Atom *atom) {
    bool rejected = atom->kind == ATOM_EXPR ||
        (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING);
    if (!rejected)
        return true;
    ev->error = petta_semantics_arithmetic_operand_error(ev->arena, atom);
    return false;
}

static bool pa_operand(PaEval *ev, Atom *atom, PaNumber *out) {
    pa_set_int(out, 0);
    if (!pa_arith_arg_ok(ev, atom))
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return pa_raise_in_is(ev, atom_symbol(ev->arena,
                                              "instantiation_error"));
    case ATOM_SYMBOL:
        return pa_constant(ev, atom, out);
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
            pa_set_int(out, atom->ground.ival);
            return true;
        case GV_FLOAT:
            pa_set_float(out, atom->ground.fval);
            return true;
#if CETTA_BUILD_WITH_GMP
        case GV_BIGINT:
            mpz_init(out->z);
            out->type = PA_MPZ;
            if (!atom_bigint_get_mpz(atom, out->z))
                break;
            pa_normalize(out);
            return true;
        case GV_RATIONAL:
            mpq_init(out->q);
            out->type = PA_MPQ;
            if (!atom_rational_get_mpq(atom, out->q))
                break;
            pa_normalize(out);
            return true;
#else
        case GV_BIGINT:
            pa_set_float(out, strtod(atom_bigint_cstr(atom), NULL));
            return pa_check_float(ev, out);
#endif
        default:
            break;
        }
        break;
    default:
        break;
    }
    ev->error = petta_semantics_arithmetic_operand_error(ev->arena, atom);
    return false;
}

/* ── Functions ──────────────────────────────────────────────────────── */

typedef enum { PA_ADD, PA_SUB, PA_MUL } PaRingOp;

static bool pa_ring(PaEval *ev, PaRingOp op, PaNumber *a, PaNumber *b,
                    PaNumber *r) {
    if (a->type == PA_INT && b->type == PA_INT) {
        int64_t value = 0;
        bool overflow = op == PA_ADD
            ? __builtin_add_overflow(a->i, b->i, &value)
            : op == PA_SUB
                ? __builtin_sub_overflow(a->i, b->i, &value)
                : __builtin_mul_overflow(a->i, b->i, &value);
        if (!overflow) {
            pa_set_int(r, value);
            return true;
        }
#if CETTA_BUILD_WITH_GMP
        pa_to_mpz(a);
        pa_to_mpz(b);
#else
        if (!pa_to_float(ev, a) || !pa_to_float(ev, b))
            return false;
#endif
    }
    if (!pa_same_type(ev, a, b))
        return false;
    switch (a->type) {
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ:
        mpz_init(r->z);
        r->type = PA_MPZ;
        if (op == PA_ADD) mpz_add(r->z, a->z, b->z);
        else if (op == PA_SUB) mpz_sub(r->z, a->z, b->z);
        else mpz_mul(r->z, a->z, b->z);
        pa_normalize(r);
        return true;
    case PA_MPQ:
        mpq_init(r->q);
        r->type = PA_MPQ;
        if (op == PA_ADD) mpq_add(r->q, a->q, b->q);
        else if (op == PA_SUB) mpq_sub(r->q, a->q, b->q);
        else mpq_mul(r->q, a->q, b->q);
        pa_normalize(r);
        return true;
#endif
    case PA_FLOAT:
        pa_set_float(r, op == PA_ADD ? a->f + b->f
                        : op == PA_SUB ? a->f - b->f : a->f * b->f);
        return pa_check_float(ev, r);
    default:
        return false;
    }
}

/* SWI's ar_divide outside ISO mode: an exact integer quotient is an
 * integer; an inexact one a rational under prefer_rationals and a float
 * otherwise; a rational divides exactly; floats divide as floats. */
static bool pa_divide(PaEval *ev, PaNumber *a, PaNumber *b, PaNumber *r) {
    if (b->type == PA_FLOAT && isinf(b->f)) {
        if (a->type == PA_FLOAT) {
            pa_set_float(r, isfinite(a->f)
                ? 0.0 * (double)pa_sign(a) * (double)pa_sign(b) : NAN);
            return pa_check_float(ev, r);
        }
        pa_set_int(r, 0);
        return true;
    }
    if (!pa_same_type(ev, a, b))
        return false;
    switch (a->type) {
    case PA_INT:
        if (b->i == 0)
            return pa_zero_divisor(ev, pa_sign(a), r, "/");
        if (b->i == -1) {
            if (a->i != INT64_MIN) {
                pa_set_int(r, -a->i);
                return true;
            }
            break;
        }
        if (a->i % b->i == 0) {
            pa_set_int(r, a->i / b->i);
            return true;
        }
        break;
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ:
        if (mpz_sgn(b->z) == 0)
            return pa_zero_divisor(ev, pa_sign(a), r, "/");
        break;
    case PA_MPQ:
        if (mpq_sgn(b->q) == 0)
            return pa_zero_divisor(ev, pa_sign(a), r, "/");
        mpq_init(r->q);
        r->type = PA_MPQ;
        mpq_div(r->q, a->q, b->q);
        pa_normalize(r);
        return true;
#endif
    default:
        break;
    }
#if CETTA_BUILD_WITH_GMP
    if (pa_is_integer(a)) {
        pa_to_mpz(a);
        pa_to_mpz(b);
        if (mpz_divisible_p(a->z, b->z)) {
            mpz_init(r->z);
            r->type = PA_MPZ;
            mpz_divexact(r->z, a->z, b->z);
            pa_normalize(r);
            return true;
        }
        if (ev->flags.prefer_rationals) {
            mpq_init(r->q);
            r->type = PA_MPQ;
            mpz_set(mpq_numref(r->q), a->z);
            mpz_set(mpq_denref(r->q), b->z);
            mpq_canonicalize(r->q);
            pa_normalize(r);
            return true;
        }
    }
#endif
    if (!pa_to_float(ev, a) || !pa_to_float(ev, b))
        return false;
    if (b->f == 0.0 && !isnan(a->f) && a->f != 0.0)
        return pa_zero_divisor(
            ev, signbit(a->f) == signbit(b->f) ? 1 : -1, r, "/");
    pa_set_float(r, a->f / b->f);
    return pa_check_float(ev, r);
}

static bool pa_integer_operand(PaEval *ev, PaNumber *n,
                               const char *function) {
    if (pa_is_integer(n))
        return true;
    Atom *value = pa_number_atom(ev->arena, n);
    Atom *formal = pa_compound(ev->arena, "type_error",
                               atom_symbol(ev->arena, "integer"), value);
    return pa_raise(ev, formal,
                    pa_context(ev->arena, false, function, 2));
}

typedef enum {
    PA_MOD,      /* floor remainder: the divisor's sign */
    PA_TDIV,     /* // : quotient truncated toward zero */
    PA_FDIV,     /* div: quotient rounded down */
} PaIntegerDivision;

static bool pa_integer_division(PaEval *ev, PaIntegerDivision kind,
                                PaNumber *a, PaNumber *b, PaNumber *r,
                                const char *function) {
    if (!pa_integer_operand(ev, a, function) ||
        !pa_integer_operand(ev, b, function))
        return false;
    if (pa_sign(b) == 0)
        return pa_evaluation_error(ev, "zero_divisor", function);
    if (a->type == PA_INT && b->type == PA_INT) {
        int64_t x = a->i, y = b->i;
        if (y == -1) {
            if (kind == PA_MOD) {
                pa_set_int(r, 0);
                return true;
            }
            if (x != INT64_MIN) {
                pa_set_int(r, -x);
                return true;
            }
        } else {
            int64_t q = x / y, m = x % y;
            if (kind == PA_MOD) {
                if (m != 0 && ((m < 0) != (y < 0)))
                    m += y;
                pa_set_int(r, m);
            } else if (kind == PA_TDIV) {
                pa_set_int(r, q);
            } else {
                if (m != 0 && ((m < 0) != (y < 0)))
                    q -= 1;
                pa_set_int(r, q);
            }
            return true;
        }
    }
#if CETTA_BUILD_WITH_GMP
    pa_to_mpz(a);
    pa_to_mpz(b);
    mpz_init(r->z);
    r->type = PA_MPZ;
    if (kind == PA_MOD)
        mpz_fdiv_r(r->z, a->z, b->z);
    else if (kind == PA_TDIV)
        mpz_tdiv_q(r->z, a->z, b->z);
    else
        mpz_fdiv_q(r->z, a->z, b->z);
    pa_normalize(r);
    return true;
#else
    return pa_evaluation_error(ev, "int_overflow", NULL);
#endif
}

/* SWI's cmpNumbers: against a float the other number becomes the nearest
 * double; NaN is unordered (2). */
static int pa_compare_numbers(PaEval *ev, PaNumber *a, PaNumber *b) {
    if (a->type != b->type) {
        if (a->type == PA_FLOAT || b->type == PA_FLOAT) {
            if ((a->type == PA_FLOAT && isnan(a->f)) ||
                (b->type == PA_FLOAT && isnan(b->f)))
                return 2;
            PaEval quiet = {.arena = ev->arena,
                            .flags = {.overflow_infinity = true,
                                      .undefined_nan = true}};
            (void)pa_to_float(&quiet, a);
            (void)pa_to_float(&quiet, b);
        } else {
            (void)pa_same_type(ev, a, b);
        }
    }
    switch (a->type) {
    case PA_INT:
        return (a->i > b->i) - (a->i < b->i);
    case PA_FLOAT:
        if (a->f == b->f)
            return 0;
        if (a->f < b->f)
            return -1;
        if (a->f > b->f)
            return 1;
        return 2;
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ: {
        int c = mpz_cmp(a->z, b->z);
        return (c > 0) - (c < 0);
    }
    case PA_MPQ: {
        int c = mpq_cmp(a->q, b->q);
        return (c > 0) - (c < 0);
    }
#else
    default:
        break;
#endif
    }
    return 2;
}

static void pa_move(PaNumber *to, PaNumber *from) {
    *to = *from;
    from->type = PA_INT;
    from->i = 0;
}

static void pa_copy(PaNumber *to, const PaNumber *from) {
    *to = *from;
#if CETTA_BUILD_WITH_GMP
    if (from->type == PA_MPZ) {
        mpz_init_set(to->z, from->z);
    } else if (from->type == PA_MPQ) {
        mpq_init(to->q);
        mpq_set(to->q, from->q);
    }
#endif
}

/* SWI's ar_min and ar_max: NaN if either is NaN; of equal values the
 * negative zero for min (the other for max), else both in their common
 * type. */
static bool pa_min_max(PaEval *ev, bool minimum, PaNumber *a, PaNumber *b,
                       PaNumber *r) {
    PaNumber x, y;
    pa_copy(&x, a);
    pa_copy(&y, b);
    int order = pa_compare_numbers(ev, &x, &y);
    pa_clear(&x);
    pa_clear(&y);
    if (order == 2) {
        pa_set_float(r, NAN);
        return true;
    }
    if (order == 0) {
        bool a_neg_zero = a->type == PA_FLOAT && a->f == 0.0 && signbit(a->f);
        bool b_neg_zero = b->type == PA_FLOAT && b->f == 0.0 && signbit(b->f);
        if (a_neg_zero || b_neg_zero) {
            bool take_a = minimum ? a_neg_zero : !a_neg_zero;
            pa_move(r, take_a ? a : b);
            return true;
        }
        if (!pa_same_type(ev, a, b))
            return false;
        pa_move(r, a);
        return true;
    }
    bool take_a = minimum ? order < 0 : order > 0;
    pa_move(r, take_a ? a : b);
    return true;
}

#if CETTA_BUILD_WITH_GMP
/* An integer power too large for memory is SWI's resource error rather
 * than an allocation the process cannot survive. */
static bool pa_power_fits(PaEval *ev, const mpz_t base, uint64_t power) {
    uint64_t bits = (uint64_t)mpz_sizeinbase(base, 2);
    uint64_t total = 0u;
    if (mpz_cmpabs_ui(base, 1u) <= 0 ||
        (!__builtin_mul_overflow(bits, power, &total) &&
         total <= (UINT64_C(1) << 33)))
        return true;
    Atom *formal = pa_compound(ev->arena, "resource_error",
                               atom_symbol(ev->arena, "memory"), NULL);
    return pa_raise(ev, formal, pa_unbound(ev->arena));
}
#endif

/* Exact non-negative integer/rational powers stay exact; any float operand
 * stays in floating point (avoiding SWI's non-ISO quirk where e.g.
 * 2.718281828459045 ** 0.0 or 1 ** 2.5 returns an integer). */
static bool pa_power(PaEval *ev, PaNumber *a, PaNumber *b, PaNumber *r) {
    bool exact_operands = a->type != PA_FLOAT && b->type != PA_FLOAT;
    int exp_sign = pa_sign(b);
    if (exact_operands && exp_sign == 0) {
        pa_set_int(r, 1);
        return true;
    }
    if (exact_operands && a->type == PA_INT && a->i >= -1 && a->i <= 1) {
        if (a->i == 1) {
            pa_set_int(r, 1);
            return true;
        }
        if (a->i == 0) {
            if (exp_sign > 0) {
                pa_set_int(r, 0);
                return true;
            }
            return pa_zero_divisor(ev, 1, r, "**");
        }
        if (a->i == -1 && pa_is_integer(b)) {
            bool even = b->type == PA_INT ? (b->i % 2 == 0)
#if CETTA_BUILD_WITH_GMP
                                          : mpz_even_p(b->z)
#else
                                          : false
#endif
                ;
            pa_set_int(r, even ? 1 : -1);
            return true;
        }
    }
#if CETTA_BUILD_WITH_GMP
    if (pa_is_integer(a) && pa_is_integer(b)) {
        if (b->type == PA_MPZ)
            return pa_evaluation_error(ev, "int_overflow", NULL);
        if (exp_sign < 0) {
            if (!ev->flags.prefer_rationals)
                goto real;
            pa_to_mpq(a);
            goto rational_power;
        }
        pa_to_mpz(a);
        if (!pa_power_fits(ev, a->z, (uint64_t)b->i))
            return false;
        mpz_init(r->z);
        r->type = PA_MPZ;
        mpz_pow_ui(r->z, a->z, (unsigned long)b->i);
        pa_normalize(r);
        return true;
    }
    if (a->type == PA_MPQ && pa_is_integer(b)) {
        if (b->type == PA_MPZ)
            return pa_evaluation_error(ev, "int_overflow", NULL);
    rational_power:;
        uint64_t power = b->i < 0 ? (uint64_t)(-(b->i + 1)) + 1u
                                  : (uint64_t)b->i;
        if (!pa_power_fits(ev, mpq_numref(a->q), power) ||
            !pa_power_fits(ev, mpq_denref(a->q), power))
            return false;
        mpz_t num, den;
        mpz_inits(num, den, NULL);
        mpz_pow_ui(num, mpq_numref(a->q), (unsigned long)power);
        mpz_pow_ui(den, mpq_denref(a->q), (unsigned long)power);
        mpq_init(r->q);
        r->type = PA_MPQ;
        if (exp_sign > 0) {
            mpz_set(mpq_numref(r->q), num);
            mpz_set(mpq_denref(r->q), den);
        } else {
            if (mpz_sgn(num) == 0) {
                mpz_clears(num, den, NULL);
                mpq_clear(r->q);
                pa_set_int(r, 0);
                return pa_zero_divisor(ev, 1, r, "**");
            }
            mpz_set(mpq_numref(r->q), den);
            mpz_set(mpq_denref(r->q), num);
        }
        mpz_clears(num, den, NULL);
        mpq_canonicalize(r->q);
        pa_normalize(r);
        return true;
    }
    if (b->type == PA_MPQ) {
        /* X ** P/Q: an exact root when there is one, else a float. */
        unsigned long den = mpz_fits_ulong_p(mpq_denref(b->q))
            ? mpz_get_ui(mpq_denref(b->q)) : 0u;
        unsigned long num = mpz_fits_slong_p(mpq_numref(b->q))
            ? (unsigned long)labs(mpz_get_si(mpq_numref(b->q))) : 0u;
        if (den != 0u && num != 0u && pa_is_integer(a)) {
            pa_to_mpz(a);
            if (mpz_sgn(a->z) < 0 && !(den & 1u)) {
                pa_set_float(r, NAN);
                return pa_check_float(ev, r);
            }
            mpz_t root;
            mpz_init(root);
            if (mpz_root(root, a->z, den)) {
                mpz_pow_ui(root, root, num);
                if (exp_sign > 0) {
                    mpz_init_set(r->z, root);
                    r->type = PA_MPZ;
                } else {
                    mpq_init(r->q);
                    r->type = PA_MPQ;
                    mpq_set_z(r->q, root);
                    mpq_inv(r->q, r->q);
                }
                mpz_clear(root);
                pa_normalize(r);
                return true;
            }
            mpz_clear(root);
        } else if (den != 0u && num != 0u && a->type == PA_MPQ) {
            if (mpq_sgn(a->q) < 0 && !(den & 1u)) {
                pa_set_float(r, NAN);
                return pa_check_float(ev, r);
            }
            mpz_t rn, rd;
            mpz_inits(rn, rd, NULL);
            if (mpz_root(rn, mpq_numref(a->q), den) &&
                mpz_root(rd, mpq_denref(a->q), den)) {
                mpz_pow_ui(rn, rn, num);
                mpz_pow_ui(rd, rd, num);
                mpq_init(r->q);
                r->type = PA_MPQ;
                mpz_set(mpq_numref(r->q), exp_sign > 0 ? rn : rd);
                mpz_set(mpq_denref(r->q), exp_sign > 0 ? rd : rn);
                mpq_canonicalize(r->q);
                mpz_clears(rn, rd, NULL);
                pa_normalize(r);
                return true;
            }
            mpz_clears(rn, rd, NULL);
        } else if (a->type == PA_FLOAT && a->f < 0.0 && den != 0u &&
                   !(den & 1u)) {
            pa_set_float(r, NAN);
            return pa_check_float(ev, r);
        }
        if (a->type == PA_FLOAT && a->f < 0.0 && den != 0u && (den & 1u)) {
            bool odd = (num & 1u) != 0u;
            double base = -a->f;
            if (!pa_to_float(ev, b))
                return false;
            double value = pow(base, b->f);
            pa_set_float(r, odd ? -value : value);
            return pa_check_float(ev, r);
        }
    }
real:
#endif
    {
        int zero_div_sign = 1;
        if (b->type == PA_INT && (b->i % 2 != 0) &&
            a->type == PA_FLOAT && signbit(a->f))
            zero_div_sign = -1;
        if (!pa_to_float(ev, a) || !pa_to_float(ev, b))
            return false;
        exp_sign = (b->f > 0.0) - (b->f < 0.0);
        if (a->f == 0.0 && exp_sign == -1)
            return pa_zero_divisor(ev, zero_div_sign, r, "**");
        pa_set_float(r, a->f == 1.0 ? 1.0 : pow(a->f, b->f));
        return pa_check_float(ev, r);
    }
}

typedef double (*PaFloatFunction)(double);

static bool pa_float_function(PaEval *ev, PaFloatFunction function,
                              PaNumber *a, PaNumber *r) {
    if (!pa_to_float(ev, a))
        return false;
    pa_set_float(r, function(a->f));
    return pa_check_float(ev, r);
}

static bool pa_abs(PaEval *ev, PaNumber *a, PaNumber *r) {
    (void)ev;
    switch (a->type) {
    case PA_INT:
        if (a->i != INT64_MIN) {
            pa_set_int(r, a->i < 0 ? -a->i : a->i);
            return true;
        }
#if CETTA_BUILD_WITH_GMP
        pa_to_mpz(a);
        mpz_init(r->z);
        r->type = PA_MPZ;
        mpz_abs(r->z, a->z);
        return true;
    case PA_MPZ:
        mpz_init(r->z);
        r->type = PA_MPZ;
        mpz_abs(r->z, a->z);
        pa_normalize(r);
        return true;
    case PA_MPQ:
        mpq_init(r->q);
        r->type = PA_MPQ;
        mpq_abs(r->q, a->q);
        return true;
#else
        pa_set_float(r, -(double)INT64_MIN);
        return true;
#endif
    case PA_FLOAT:
        pa_set_float(r, signbit(a->f) ? -a->f : a->f);
        return true;
    default:
        return false;
    }
}

typedef enum { PA_TRUNCATE, PA_CEIL, PA_FLOOR, PA_ROUND } PaRounding;

/* truncate, ceil, floor and round: an integer stays itself, NaN and the
 * infinities stay floats, any other float or rational becomes the
 * integer its rounding gives. */
static bool pa_round(PaEval *ev, PaRounding kind, PaNumber *a, PaNumber *r) {
    switch (a->type) {
    case PA_INT:
#if CETTA_BUILD_WITH_GMP
    case PA_MPZ:
#endif
        pa_move(r, a);
        return true;
#if CETTA_BUILD_WITH_GMP
    case PA_MPQ:
        mpz_init(r->z);
        r->type = PA_MPZ;
        if (kind == PA_TRUNCATE) {
            mpz_tdiv_q(r->z, mpq_numref(a->q), mpq_denref(a->q));
        } else if (kind == PA_CEIL) {
            mpz_cdiv_q(r->z, mpq_numref(a->q), mpq_denref(a->q));
        } else if (kind == PA_FLOOR) {
            mpz_fdiv_q(r->z, mpq_numref(a->q), mpq_denref(a->q));
        } else {
            mpq_t half, q;
            mpq_inits(half, q, NULL);
            mpq_set_ui(half, 1u, 2u);
            if (mpq_sgn(a->q) > 0)
                mpq_add(q, a->q, half);
            else
                mpq_sub(q, a->q, half);
            mpz_set_q(r->z, q);
            mpq_clears(half, q, NULL);
        }
        pa_normalize(r);
        return true;
#endif
    case PA_FLOAT: {
        if (isnan(a->f) || isinf(a->f)) {
            pa_move(r, a);
            return true;
        }
        double value = kind == PA_TRUNCATE ? trunc(a->f)
            : kind == PA_CEIL ? ceil(a->f)
            : kind == PA_FLOOR ? floor(a->f) : round(a->f);
        if (value >= -9223372036854775808.0 &&
            value < 9223372036854775808.0) {
            pa_set_int(r, (int64_t)value);
            return true;
        }
#if CETTA_BUILD_WITH_GMP
        mpz_init_set_d(r->z, value);
        r->type = PA_MPZ;
        return true;
#else
        return pa_evaluation_error(ev, "int_overflow", NULL);
#endif
    }
    default:
        (void)ev;
        return false;
    }
}

/* The function `name` applied to the evaluated operands. */
static bool pa_apply(PaEval *ev, const char *name, PaNumber *args,
                     uint32_t nargs, PaNumber *r) {
    if (nargs == 2u) {
        if (strcmp(name, "+") == 0)
            return pa_ring(ev, PA_ADD, &args[0], &args[1], r);
        if (strcmp(name, "-") == 0)
            return pa_ring(ev, PA_SUB, &args[0], &args[1], r);
        if (strcmp(name, "*") == 0)
            return pa_ring(ev, PA_MUL, &args[0], &args[1], r);
        if (strcmp(name, "/") == 0)
            return pa_divide(ev, &args[0], &args[1], r);
        if (strcmp(name, "mod") == 0)
            return pa_integer_division(ev, PA_MOD, &args[0], &args[1], r,
                                       "mod");
        if (strcmp(name, "//") == 0)
            return pa_integer_division(ev, PA_TDIV, &args[0], &args[1], r,
                                       "//");
        if (strcmp(name, "div") == 0)
            return pa_integer_division(ev, PA_FDIV, &args[0], &args[1], r,
                                       "div");
        if (strcmp(name, "**") == 0)
            return pa_power(ev, &args[0], &args[1], r);
        if (strcmp(name, "min") == 0)
            return pa_min_max(ev, true, &args[0], &args[1], r);
        if (strcmp(name, "max") == 0)
            return pa_min_max(ev, false, &args[0], &args[1], r);
        return false;
    }
    if (nargs != 1u)
        return false;
    static const struct {
        const char *name;
        PaFloatFunction function;
    } float_functions[] = {
        {"sqrt", sqrt}, {"exp", exp}, {"log", log},
        {"sin", sin}, {"cos", cos}, {"tan", tan},
        {"asin", asin}, {"acos", acos}, {"atan", atan},
    };
    for (size_t k = 0; k < sizeof(float_functions) / sizeof(float_functions[0]);
         k++) {
        if (strcmp(name, float_functions[k].name) == 0)
            return pa_float_function(ev, float_functions[k].function,
                                     &args[0], r);
    }
    if (strcmp(name, "abs") == 0)
        return pa_abs(ev, &args[0], r);
    if (strcmp(name, "truncate") == 0)
        return pa_round(ev, PA_TRUNCATE, &args[0], r);
    if (strcmp(name, "ceil") == 0 || strcmp(name, "ceiling") == 0)
        return pa_round(ev, PA_CEIL, &args[0], r);
    if (strcmp(name, "floor") == 0)
        return pa_round(ev, PA_FLOOR, &args[0], r);
    if (strcmp(name, "round") == 0 || strcmp(name, "integer") == 0)
        return pa_round(ev, PA_ROUND, &args[0], r);
    return false;
}

static bool pa_known_function(const char *name, uint32_t nargs) {
    static const char *const binary[] = {
        "+", "-", "*", "/", "mod", "//", "div", "**", "min", "max",
    };
    static const char *const unary[] = {
        "sqrt", "exp", "log", "sin", "cos", "tan", "asin", "acos", "atan",
        "abs", "truncate", "ceil", "ceiling", "floor", "round", "integer",
    };
    const char *const *names = nargs == 2u ? binary : unary;
    size_t count = nargs == 2u ? sizeof(binary) / sizeof(binary[0])
                               : sizeof(unary) / sizeof(unary[0]);
    if (nargs != 1u && nargs != 2u)
        return false;
    for (size_t k = 0; k < count; k++)
        if (strcmp(name, names[k]) == 0)
            return true;
    return false;
}

bool petta_arithmetic_evaluate(Arena *arena, const char *function,
                               Atom **args, uint32_t nargs,
                               CettaCallOutcome *out) {
    if (!arena || !out || !args || nargs == 0u || nargs > 2u ||
        (!function && nargs != 1u) ||
        (function && !pa_known_function(function, nargs)))
        return false;
    for (uint32_t k = 0u; k < nargs; k++)
        if (!args[k])
            return false;
    PaEval ev = {.arena = arena, .flags = petta_arithmetic_flags()};
    PaNumber operands[2];
    PaNumber result;
    pa_set_int(&operands[0], 0);
    pa_set_int(&operands[1], 0);
    pa_set_int(&result, 0);
    /* arith_arg/1 reads every operand before is/2 evaluates any. */
    bool ok = true;
    for (uint32_t k = 0u; ok && k < nargs; k++)
        ok = pa_arith_arg_ok(&ev, args[k]);
    for (uint32_t k = 0u; ok && k < nargs; k++)
        ok = pa_operand(&ev, args[k], &operands[k]);
    if (ok) {
        if (function) {
            ok = pa_apply(&ev, function, operands, nargs, &result);
        } else {
            pa_move(&result, &operands[0]);
        }
    }
    Atom *value = ok ? pa_number_atom(arena, &result) : NULL;
    for (uint32_t k = 0u; k < nargs; k++)
        pa_clear(&operands[k]);
    pa_clear(&result);
    if (ok) {
        if (!value)
            return false;
        *out = cetta_call_value(value);
        return true;
    }
    if (!ev.error)
        return false;
    *out = cetta_call_raised(ev.error);
    return true;
}

bool petta_arithmetic_compare(Arena *arena, const char *relation,
                              Atom **args, CettaCallOutcome *out) {
    if (!arena || !relation || !args || !args[0] || !args[1] || !out)
        return false;
    int want = 0;
    if (strcmp(relation, "<") == 0) want = 1;
    else if (strcmp(relation, ">") == 0) want = 2;
    else if (strcmp(relation, "=<") == 0) want = 3;
    else if (strcmp(relation, ">=") == 0) want = 4;
    else if (strcmp(relation, "=:=") == 0) want = 5;
    else if (strcmp(relation, "=\\=") == 0) want = 6;
    else return false;
    PaEval ev = {.arena = arena, .flags = petta_arithmetic_flags()};
    PaNumber a, b;
    pa_set_int(&a, 0);
    pa_set_int(&b, 0);
    bool ok = pa_arith_arg_ok(&ev, args[0]) &&
              pa_arith_arg_ok(&ev, args[1]) &&
              pa_operand(&ev, args[0], &a) &&
              pa_operand(&ev, args[1], &b);
    int order = ok ? pa_compare_numbers(&ev, &a, &b) : 2;
    pa_clear(&a);
    pa_clear(&b);
    if (!ok) {
        if (!ev.error)
            return false;
        *out = cetta_call_raised(ev.error);
        return true;
    }
    bool truth = false;
    switch (want) {
    case 1: truth = order == -1; break;
    case 2: truth = order == 1; break;
    case 3: truth = order == -1 || order == 0; break;
    case 4: truth = order == 1 || order == 0; break;
    case 5: truth = order == 0; break;
    default: truth = order != 0; break;
    }
    *out = cetta_call_value(atom_symbol(arena, truth ? "true" : "false"));
    return true;
}
