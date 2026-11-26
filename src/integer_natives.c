#include "integer_natives.h"

#include <gmp.h>
#include <limits.h>
#include <string.h>

static Atom *integer_expects(CettaIntegerRefusal *refusal, const char *expected) {
    refusal->expected = expected;
    return NULL;
}

/* An integer argument, machine or big, into `out`, which the caller has
 * initialized and clears. */
static bool integer_arg(Atom *arg, mpz_t out) {
    if (!arg || arg->kind != ATOM_GROUNDED)
        return false;
    if (arg->ground.gkind == GV_INT) {
        mpz_set_si(out, (long)arg->ground.ival);
        return true;
    }
    return arg->ground.gkind == GV_BIGINT && atom_bigint_get_mpz(arg, out);
}

#define INTEGER_BINARY(fname, op)                                              \
    static Atom *fname(Arena *a, Atom *const *args, CettaIntegerRefusal *r) { \
        mpz_t x, y, z;                                                         \
        mpz_inits(x, y, z, NULL);                                              \
        if (!integer_arg(args[0], x) || !integer_arg(args[1], y)) {            \
            mpz_clears(x, y, z, NULL);                                         \
            return integer_expects(r, "expected an integer");                  \
        }                                                                      \
        op(z, x, y);                                                           \
        mpz_clears(x, y, NULL);                                                \
        return atom_bigint_take_mpz(a, z);                                     \
    }

INTEGER_BINARY(integer_and, mpz_and)
INTEGER_BINARY(integer_or, mpz_ior)
INTEGER_BINARY(integer_xor, mpz_xor)

/* Not x: minus x minus one. */
static Atom *integer_not(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t x;
    mpz_init(x);
    if (!integer_arg(args[0], x)) {
        mpz_clear(x);
        return integer_expects(r, "expected an integer");
    }
    mpz_com(x, x);
    return atom_bigint_take_mpz(a, x);
}

/* A shift count: a non-negative integer that fits a bit count. */
static bool integer_count(Atom *arg, mp_bitcnt_t *out) {
    mpz_t n;
    mpz_init(n);
    bool ok = integer_arg(arg, n) && mpz_sgn(n) >= 0 && mpz_fits_ulong_p(n);
    if (ok)
        *out = (mp_bitcnt_t)mpz_get_ui(n);
    mpz_clear(n);
    return ok;
}

/* X times two to the n. */
static Atom *integer_shift_left(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t x;
    mp_bitcnt_t n;
    mpz_init(x);
    if (!integer_arg(args[0], x) || !integer_count(args[1], &n)) {
        mpz_clear(x);
        return integer_expects(r, "expected an integer and a non-negative count");
    }
    mpz_mul_2exp(x, x, n);
    return atom_bigint_take_mpz(a, x);
}

/* X divided by two to the n, rounded down. */
static Atom *integer_shift_right(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t x;
    mp_bitcnt_t n;
    mpz_init(x);
    if (!integer_arg(args[0], x) || !integer_count(args[1], &n)) {
        mpz_clear(x);
        return integer_expects(r, "expected an integer and a non-negative count");
    }
    mpz_fdiv_q_2exp(x, x, n);
    return atom_bigint_take_mpz(a, x);
}

/* The number of bits of x's magnitude (0 for 0). */
static Atom *integer_bit_length(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t x;
    mpz_init(x);
    if (!integer_arg(args[0], x)) {
        mpz_clear(x);
        return integer_expects(r, "expected an integer");
    }
    size_t bits = mpz_sgn(x) == 0 ? 0u : mpz_sizeinbase(x, 2);
    mpz_clear(x);
    return atom_int(a, (int64_t)bits);
}

/* The number of one bits of x's magnitude. */
static Atom *integer_bit_count(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t x;
    mpz_init(x);
    if (!integer_arg(args[0], x)) {
        mpz_clear(x);
        return integer_expects(r, "expected an integer");
    }
    mpz_abs(x, x);
    mp_bitcnt_t ones = mpz_popcount(x);
    mpz_clear(x);
    return atom_int(a, (int64_t)ones);
}

/* Base to the exponent modulo m, the result with m's sign (between 0 and m);
 * a negative exponent takes the inverse of base modulo m.  Refused: m zero,
 * or a negative exponent when base has no inverse. */
static Atom *integer_pow_mod(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t b, e, m, absm, z;
    mpz_inits(b, e, m, absm, z, NULL);
    if (!integer_arg(args[0], b) || !integer_arg(args[1], e) || !integer_arg(args[2], m)) {
        mpz_clears(b, e, m, absm, z, NULL);
        return integer_expects(r, "expected integers");
    }
    if (mpz_sgn(m) == 0) {
        mpz_clears(b, e, m, absm, z, NULL);
        return integer_expects(r, "expected a nonzero modulus");
    }
    mpz_abs(absm, m);
    if (mpz_sgn(e) < 0) {
        if (!mpz_invert(b, b, absm)) {
            mpz_clears(b, e, m, absm, z, NULL);
            return integer_expects(r, "expected a base invertible for the modulus");
        }
        mpz_neg(e, e);
    }
    mpz_powm(z, b, e, absm);
    if (mpz_sgn(m) < 0 && mpz_sgn(z) != 0)
        mpz_add(z, z, m);
    mpz_clears(b, e, m, absm, NULL);
    return atom_bigint_take_mpz(a, z);
}

/* The digits of x's magnitude in a base from 2 to 36, lower case. */
static Atom *integer_digits(Arena *a, Atom *const *args, CettaIntegerRefusal *r) {
    mpz_t x, base;
    mpz_inits(x, base, NULL);
    if (!integer_arg(args[0], x) || !integer_arg(args[1], base) ||
        mpz_cmp_si(base, 2) < 0 || mpz_cmp_si(base, 36) > 0) {
        mpz_clears(x, base, NULL);
        return integer_expects(r, "expected an integer and a base from 2 to 36");
    }
    mpz_abs(x, x);
    char *text = mpz_get_str(NULL, (int)mpz_get_si(base), x);
    mpz_clears(x, base, NULL);
    if (!text)
        return integer_expects(r, "expected an integer and a base from 2 to 36");
    Atom *result = atom_string(a, text);
    void (*free_fn)(void *, size_t);
    mp_get_memory_functions(NULL, NULL, &free_fn);
    free_fn(text, strlen(text) + 1u);
    return result;
}

static const CettaIntegerNative integer_natives[] = {
    {"and", 2u, integer_and},
    {"or", 2u, integer_or},
    {"xor", 2u, integer_xor},
    {"not", 1u, integer_not},
    {"shift-left", 2u, integer_shift_left},
    {"shift-right", 2u, integer_shift_right},
    {"bit-length", 1u, integer_bit_length},
    {"bit-count", 1u, integer_bit_count},
    {"pow-mod", 3u, integer_pow_mod},
    {"digits", 2u, integer_digits},
};

const CettaIntegerNative *cetta_integer_native(const char *name) {
    if (!name)
        return NULL;
    for (size_t k = 0; k < sizeof(integer_natives) / sizeof(integer_natives[0]); k++)
        if (strcmp(integer_natives[k].name, name) == 0)
            return &integer_natives[k];
    return NULL;
}
