#define _GNU_SOURCE
#include "float_natives.h"

#include <errno.h>
#include <float.h>
#include <gmp.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Atom *float_expects(CettaFloatRefusal *refusal, const char *expected) {
    refusal->expected = expected;
    return NULL;
}

static bool float_arg(Atom *arg, double *out) {
    if (!arg || arg->kind != ATOM_GROUNDED || arg->ground.gkind != GV_FLOAT)
        return false;
    *out = arg->ground.fval;
    return true;
}

/* An integer argument, machine or big, into `out`, which the caller has
 * initialized and clears. */
static bool float_int_arg(Atom *arg, mpz_t out) {
    if (!arg || arg->kind != ATOM_GROUNDED)
        return false;
    if (arg->ground.gkind == GV_INT) {
        mpz_set_si(out, (long)arg->ground.ival);
        return true;
    }
    return arg->ground.gkind == GV_BIGINT && atom_bigint_get_mpz(arg, out);
}

/* ── the C locale for decimal text ──────────────────────────────────────── */

static locale_t float_c_locale;
static pthread_once_t float_c_locale_once = PTHREAD_ONCE_INIT;

static void float_c_locale_init(void) {
    float_c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
}

static locale_t float_locale(void) {
    pthread_once(&float_c_locale_once, float_c_locale_init);
    return float_c_locale;
}

/* ── arithmetic, as C does it ───────────────────────────────────────────── */

#define FLOAT_BINARY(fname, expr)                                              \
    static Atom *fname(Arena *a, Atom *const *args, CettaFloatRefusal *r) {   \
        double x, y;                                                           \
        if (!float_arg(args[0], &x) || !float_arg(args[1], &y))                \
            return float_expects(r, "expected two floats");                    \
        return atom_float(a, (expr));                                          \
    }

FLOAT_BINARY(float_add, x + y)
FLOAT_BINARY(float_sub, x - y)
FLOAT_BINARY(float_mul, x * y)
FLOAT_BINARY(float_div, x / y)
FLOAT_BINARY(float_fmod, fmod(x, y))
FLOAT_BINARY(float_copysign, copysign(x, y))
FLOAT_BINARY(float_atan2, atan2(x, y))
FLOAT_BINARY(float_hypot, hypot(x, y))
FLOAT_BINARY(float_nextafter, nextafter(x, y))
FLOAT_BINARY(float_remainder, remainder(x, y))

#define FLOAT_UNARY(fname, expr)                                               \
    static Atom *fname(Arena *a, Atom *const *args, CettaFloatRefusal *r) {   \
        double x;                                                              \
        if (!float_arg(args[0], &x))                                           \
            return float_expects(r, "expected a float");                       \
        return atom_float(a, (expr));                                          \
    }

FLOAT_UNARY(float_neg, -x)
FLOAT_UNARY(float_abs, fabs(x))
FLOAT_UNARY(float_floor, floor(x))
FLOAT_UNARY(float_ceil, ceil(x))
FLOAT_UNARY(float_trunc, trunc(x))
FLOAT_UNARY(float_sqrt, sqrt(x))
/* C's libm, as it computes them (a language's math library builds its
 * domain and range errors on the results) */
FLOAT_UNARY(float_exp, exp(x))
FLOAT_UNARY(float_exp2, exp2(x))
FLOAT_UNARY(float_expm1, expm1(x))
FLOAT_UNARY(float_log, log(x))
FLOAT_UNARY(float_log2, log2(x))
FLOAT_UNARY(float_log10, log10(x))
FLOAT_UNARY(float_log1p, log1p(x))
FLOAT_UNARY(float_sin, sin(x))
FLOAT_UNARY(float_cos, cos(x))
FLOAT_UNARY(float_tan, tan(x))
FLOAT_UNARY(float_asin, asin(x))
FLOAT_UNARY(float_acos, acos(x))
FLOAT_UNARY(float_atan, atan(x))
FLOAT_UNARY(float_sinh, sinh(x))
FLOAT_UNARY(float_cosh, cosh(x))
FLOAT_UNARY(float_tanh, tanh(x))
FLOAT_UNARY(float_asinh, asinh(x))
FLOAT_UNARY(float_acosh, acosh(x))
FLOAT_UNARY(float_atanh, atanh(x))
FLOAT_UNARY(float_erf, erf(x))
FLOAT_UNARY(float_erfc, erfc(x))
FLOAT_UNARY(float_cbrt, cbrt(x))

#define FLOAT_PREDICATE(fname, expr)                                           \
    static Atom *fname(Arena *a, Atom *const *args, CettaFloatRefusal *r) {   \
        double x;                                                              \
        if (!float_arg(args[0], &x))                                           \
            return float_expects(r, "expected a float");                       \
        return (expr) ? atom_true(a) : atom_false(a);                          \
    }

FLOAT_PREDICATE(float_is_nan, isnan(x))
FLOAT_PREDICATE(float_is_inf, isinf(x))
FLOAT_PREDICATE(float_is_finite, isfinite(x))
FLOAT_PREDICATE(float_sign_bit, signbit(x))

/* x to the y as C's pow computes it, with what errno said:
 * (FloatPowed VALUE ok), (FloatPowed VALUE range) or (FloatPowed VALUE domain). */
static Atom *float_pow(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x, y;
    if (!float_arg(args[0], &x) || !float_arg(args[1], &y))
        return float_expects(r, "expected two floats");
    errno = 0;
    double v = pow(x, y);
    const char *what = errno == ERANGE ? "range" : errno == EDOM ? "domain" : "ok";
    if (errno == 0 && isinf(v) && isfinite(x) && isfinite(y))
        what = "range";
    return atom_expr3(a, atom_symbol(a, "FloatPowed"), atom_float(a, v), atom_symbol(a, what));
}

/* x compared with y: -1, 0 or 1, or unordered when either is a NaN. */
static Atom *float_compare(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x, y;
    if (!float_arg(args[0], &x) || !float_arg(args[1], &y))
        return float_expects(r, "expected two floats");
    if (isnan(x) || isnan(y))
        return atom_symbol(a, "unordered");
    return atom_int(a, x < y ? -1 : x > y ? 1 : 0);
}

/* ── exact conversions ──────────────────────────────────────────────────── */

/* The double nearest num/den (den nonzero), ties to even, as a correctly
 * rounded division of the exact integers: 55 bits of quotient and whether a
 * remainder is left, then rounded at the place of a normal number's last bit,
 * or of the least subnormal's.  False when the result overflows. */
static bool float_nearest_ratio(mpz_srcptr num, mpz_srcptr den, double *out) {
    int negative = (mpz_sgn(num) < 0) != (mpz_sgn(den) < 0);
    if (mpz_sgn(num) == 0) {
        *out = negative ? -0.0 : 0.0;
        return true;
    }
    mpz_t n, d, t, rem, q, low, half;
    mpz_inits(n, d, t, rem, q, low, half, NULL);
    mpz_abs(n, num);
    mpz_abs(d, den);
    long e = (long)mpz_sizeinbase(n, 2) - (long)mpz_sizeinbase(d, 2) - 55;
    if (e < 0)
        mpz_mul_2exp(n, n, (mp_bitcnt_t)(-e));
    else
        mpz_mul_2exp(d, d, (mp_bitcnt_t)e);
    mpz_tdiv_qr(t, rem, n, d);
    bool sticky = mpz_sgn(rem) != 0;
    long bits = (long)mpz_sizeinbase(t, 2);
    long ulp = bits - 1 + e - 52;
    if (ulp < -1074)
        ulp = -1074;
    long shift = ulp - e;
    bool ok = true;
    if (shift > 0) {
        mpz_tdiv_q_2exp(q, t, (mp_bitcnt_t)shift);
        mpz_tdiv_r_2exp(low, t, (mp_bitcnt_t)shift);
        mpz_set_ui(half, 1u);
        mpz_mul_2exp(half, half, (mp_bitcnt_t)(shift - 1));
        int c = mpz_cmp(low, half);
        if (c > 0 || (c == 0 && (sticky || mpz_odd_p(q))))
            mpz_add_ui(q, q, 1u);
    } else {
        mpz_mul_2exp(q, t, (mp_bitcnt_t)(-shift));
    }
    double v = ldexp(mpz_get_d(q), (int)ulp);
    if (isinf(v))
        ok = false;
    *out = negative ? -v : v;
    mpz_clears(n, d, t, rem, q, low, half, NULL);
    return ok;
}

/* The float nearest an integer's value, or (FloatOverflow) beyond the
 * finite doubles. */
static Atom *float_from_int(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    mpz_t n, one;
    mpz_inits(n, one, NULL);
    if (!float_int_arg(args[0], n)) {
        mpz_clears(n, one, NULL);
        return float_expects(r, "expected an integer");
    }
    mpz_set_ui(one, 1u);
    double v;
    bool ok = float_nearest_ratio(n, one, &v);
    mpz_clears(n, one, NULL);
    return ok ? atom_float(a, v) : atom_expr(a, (Atom *[]){atom_symbol(a, "FloatOverflow")}, 1);
}

/* The float nearest num/den, or (FloatOverflow); den must not be zero. */
static Atom *float_from_ratio(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    mpz_t n, d;
    mpz_inits(n, d, NULL);
    if (!float_int_arg(args[0], n) || !float_int_arg(args[1], d) || mpz_sgn(d) == 0) {
        mpz_clears(n, d, NULL);
        return float_expects(r, "expected an integer and a nonzero integer");
    }
    double v;
    bool ok = float_nearest_ratio(n, d, &v);
    mpz_clears(n, d, NULL);
    return ok ? atom_float(a, v) : atom_expr(a, (Atom *[]){atom_symbol(a, "FloatOverflow")}, 1);
}

/* The integer a finite float truncates to, exactly. */
static Atom *float_to_int(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x;
    if (!float_arg(args[0], &x) || !isfinite(x))
        return float_expects(r, "expected a finite float");
    mpz_t z;
    mpz_init_set_d(z, x);
    return atom_bigint_take_mpz(a, z);
}

/* A float compared with an integer, exactly: -1, 0 or 1, or unordered. */
static Atom *float_compare_int(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x;
    mpz_t z;
    mpz_init(z);
    if (!float_arg(args[0], &x) || !float_int_arg(args[1], z)) {
        mpz_clear(z);
        return float_expects(r, "expected a float and an integer");
    }
    if (isnan(x)) {
        mpz_clear(z);
        return atom_symbol(a, "unordered");
    }
    int c = mpz_cmp_d(z, x);
    mpz_clear(z);
    return atom_int(a, c < 0 ? 1 : c > 0 ? -1 : 0);
}

/* ── decimal text ───────────────────────────────────────────────────────── */

/* The significant digits of `text`, a %.Ne rendering, and its exponent. */
static void float_split_scientific(const char *text, char *digits, size_t cap, int *exponent) {
    size_t n = 0u;
    const char *p = text;
    for (; *p && *p != 'e'; p++)
        if (*p >= '0' && *p <= '9' && n + 1u < cap)
            digits[n++] = *p;
    digits[n] = '\0';
    *exponent = *p == 'e' ? atoi(p + 1) : 0;
}

static bool float_reads_back(const char *digits, int exponent, double x) {
    char text[64];
    snprintf(text, sizeof(text), "%c.%se%d", digits[0], digits + 1, exponent);
    return strtod_l(text, NULL, float_locale()) == x;
}

/* Add or subtract one in the last place of a decimal digit string of the
 * same length; false when the length would change. */
static bool float_step_digits(const char *digits, int delta, char *out, size_t cap) {
    size_t n = strlen(digits);
    if (n + 1u > cap)
        return false;
    memcpy(out, digits, n + 1u);
    for (size_t k = n; k-- > 0u;) {
        if (delta > 0) {
            if (out[k] < '9') { out[k]++; return true; }
            out[k] = '0';
        } else {
            if (out[k] > '0') { out[k]--; return out[0] != '0'; }
            out[k] = '9';
        }
    }
    return false;
}

/* The shortest decimal digits that read back to x, nearest x among those of
 * that length: (FloatDigits SIGN DIGITS DECPT), the value 0.DIGITS times ten
 * to DECPT; (FloatSpecial inf), (FloatSpecial -inf), (FloatSpecial nan).
 * At each length the correctly rounded digits are tried, and when they do
 * not read back, their neighbours of the same length: around a power of two
 * a double's rounding interval is wider on one side. */
static Atom *float_digits(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x;
    if (!float_arg(args[0], &x))
        return float_expects(r, "expected a float");
    if (isnan(x))
        return atom_expr2(a, atom_symbol(a, "FloatSpecial"), atom_string(a, "nan"));
    if (isinf(x))
        return atom_expr2(a, atom_symbol(a, "FloatSpecial"), atom_string(a, x < 0 ? "-inf" : "inf"));
    const char *sign = signbit(x) ? "-" : "";
    double m = fabs(x);
    char digits[40] = "0", step[40];
    int exponent = 0;
    if (m != 0.0) {
        locale_t previous = uselocale(float_locale());
        bool found = false;
        for (int p = 1; p <= 17 && !found; p++) {
            char text[64];
            snprintf(text, sizeof(text), "%.*e", p - 1, m);
            float_split_scientific(text, digits, sizeof(digits), &exponent);
            if (float_reads_back(digits, exponent, m)) {
                found = true;
                break;
            }
            for (int delta = -1; delta <= 1 && !found; delta += 2)
                if (float_step_digits(digits, delta, step, sizeof(step)) &&
                    float_reads_back(step, exponent, m)) {
                    memcpy(digits, step, strlen(step) + 1u);
                    found = true;
                }
        }
        (void)uselocale(previous);
        size_t n = strlen(digits);
        while (n > 1u && digits[n - 1u] == '0')
            digits[--n] = '\0';
        exponent += 1;
    } else {
        exponent = 1;
    }
    Atom *items[4] = {atom_symbol(a, "FloatDigits"), atom_string(a, sign),
                      atom_string(a, digits), atom_int(a, exponent)};
    return atom_expr(a, items, 4);
}

/* x written by C's %.Nf, %.Ne or %.Ng: CONVERSION is f, e or g, PRECISION
 * the digits; correctly rounded, ties to even. */
static Atom *float_format(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x;
    if (!float_arg(args[0], &x) || !args[1] || args[1]->kind != ATOM_SYMBOL ||
        !args[2] || args[2]->kind != ATOM_GROUNDED || args[2]->ground.gkind != GV_INT ||
        args[2]->ground.ival < 0 || args[2]->ground.ival > 1100)
        return float_expects(r, "expected a float, f, e or g, and a precision from 0 to 1100");
    const char *conversion = atom_name_cstr(args[1]);
    char spec[8];
    if (!conversion || strlen(conversion) != 1u || !strchr("feg", conversion[0]))
        return float_expects(r, "expected a float, f, e or g, and a precision from 0 to 1100");
    snprintf(spec, sizeof(spec), "%%.*%c", conversion[0]);
    locale_t previous = uselocale(float_locale());
    int length = snprintf(NULL, 0, spec, (int)args[2]->ground.ival, x);
    char *text = arena_alloc(a, (size_t)length + 1u);
    snprintf(text, (size_t)length + 1u, spec, (int)args[2]->ground.ival, x);
    (void)uselocale(previous);
    return atom_string(a, text);
}

/* A float's literal read whole, nearest, in the C locale: digits with a point
 * or an exponent, inf, infinity or nan in any case, with a sign; any other
 * text refuses. */
static Atom *float_parse(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    if (!args[0] || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_STRING)
        return float_expects(r, "expected text");
    const char *text = args[0]->ground.sval;
    size_t len = args[0]->ground.slen;
    if (len == 0u || memchr(text, '\0', len) || strchr(" \t\n\r\f\v", text[0]))
        return float_expects(r, "expected a float's literal");
    char *copy = arena_alloc(a, len + 1u);
    memcpy(copy, text, len);
    copy[len] = '\0';
    for (size_t k = 0; k < len; k++)
        if (strchr("xXpP(", copy[k]))
            return float_expects(r, "expected a float's literal");
    char *end = NULL;
    double v = strtod_l(copy, &end, float_locale());
    if (!end || *end != '\0' || end == copy)
        return float_expects(r, "expected a float's literal");
    return atom_float(a, v);
}

/* x times y plus z, rounded once (C's fma). */
static Atom *float_fma(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x, y, z;
    if (!float_arg(args[0], &x) || !float_arg(args[1], &y) || !float_arg(args[2], &z))
        return float_expects(r, "expected a float");
    return atom_float(a, fma(x, y, z));
}

/* x as m times two to the e, m in [0.5, 1) or zero (C's frexp): (FloatFrexp M E). */
static Atom *float_frexp(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x;
    int e = 0;
    if (!float_arg(args[0], &x))
        return float_expects(r, "expected a float");
    double m = frexp(x, &e);
    Atom *parts[3] = {atom_symbol(a, "FloatFrexp"), atom_float(a, m), atom_int(a, e)};
    return atom_expr(a, parts, 3);
}

/* x times two to an integer n (C's ldexp); n beyond the int range is clamped,
 * which gives the same overflow or underflow. */
static Atom *float_ldexp(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x;
    mpz_t n;
    if (!float_arg(args[0], &x))
        return float_expects(r, "expected a float");
    mpz_init(n);
    if (!float_int_arg(args[1], n)) {
        mpz_clear(n);
        return float_expects(r, "expected an integer");
    }
    long e;
    if (mpz_cmp_si(n, 100000) > 0)
        e = 100000;
    else if (mpz_cmp_si(n, -100000) < 0)
        e = -100000;
    else
        e = mpz_get_si(n);
    mpz_clear(n);
    return atom_float(a, ldexp(x, (int)e));
}

/* x's fractional and whole parts, each with x's sign (C's modf): (FloatModf FRAC WHOLE). */
static Atom *float_modf(Arena *a, Atom *const *args, CettaFloatRefusal *r) {
    double x, whole;
    if (!float_arg(args[0], &x))
        return float_expects(r, "expected a float");
    double frac = modf(x, &whole);
    Atom *parts[3] = {atom_symbol(a, "FloatModf"), atom_float(a, frac), atom_float(a, whole)};
    return atom_expr(a, parts, 3);
}

static const CettaFloatNative float_natives[] = {
    {"add", 2u, float_add},
    {"sub", 2u, float_sub},
    {"mul", 2u, float_mul},
    {"div", 2u, float_div},
    {"fmod", 2u, float_fmod},
    {"copysign", 2u, float_copysign},
    {"pow", 2u, float_pow},
    {"compare", 2u, float_compare},
    {"neg", 1u, float_neg},
    {"abs", 1u, float_abs},
    {"floor", 1u, float_floor},
    {"ceil", 1u, float_ceil},
    {"trunc", 1u, float_trunc},
    {"sqrt", 1u, float_sqrt},
    {"exp", 1u, float_exp},
    {"exp2", 1u, float_exp2},
    {"expm1", 1u, float_expm1},
    {"log", 1u, float_log},
    {"log2", 1u, float_log2},
    {"log10", 1u, float_log10},
    {"log1p", 1u, float_log1p},
    {"sin", 1u, float_sin},
    {"cos", 1u, float_cos},
    {"tan", 1u, float_tan},
    {"asin", 1u, float_asin},
    {"acos", 1u, float_acos},
    {"atan", 1u, float_atan},
    {"sinh", 1u, float_sinh},
    {"cosh", 1u, float_cosh},
    {"tanh", 1u, float_tanh},
    {"asinh", 1u, float_asinh},
    {"acosh", 1u, float_acosh},
    {"atanh", 1u, float_atanh},
    {"erf", 1u, float_erf},
    {"erfc", 1u, float_erfc},
    {"cbrt", 1u, float_cbrt},
    {"atan2", 2u, float_atan2},
    {"hypot", 2u, float_hypot},
    {"nextafter", 2u, float_nextafter},
    {"remainder", 2u, float_remainder},
    {"fma", 3u, float_fma},
    {"frexp", 1u, float_frexp},
    {"ldexp", 2u, float_ldexp},
    {"modf", 1u, float_modf},
    {"is-nan", 1u, float_is_nan},
    {"is-inf", 1u, float_is_inf},
    {"is-finite", 1u, float_is_finite},
    {"sign-bit", 1u, float_sign_bit},
    {"from-int", 1u, float_from_int},
    {"from-ratio", 2u, float_from_ratio},
    {"to-int", 1u, float_to_int},
    {"compare-int", 2u, float_compare_int},
    {"digits", 1u, float_digits},
    {"format", 3u, float_format},
    {"parse", 1u, float_parse},
};

const CettaFloatNative *cetta_float_native(const char *name) {
    if (!name)
        return NULL;
    for (size_t k = 0; k < sizeof(float_natives) / sizeof(float_natives[0]); k++)
        if (strcmp(float_natives[k].name, name) == 0)
            return &float_natives[k];
    return NULL;
}
