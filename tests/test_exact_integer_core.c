#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "exact_integer.h"

/* The reference: each operation computed from its definition in 128 bits,
 * where every result of two int64 operands is exact. */
static bool reference(CettaExactIntegerV1Operation operation, int64_t left,
                      int64_t right, __int128 *out) {
    __int128 l = left, r = right;
    switch (operation) {
    case CETTA_EXACT_INTEGER_V1_ADD: *out = l + r; return true;
    case CETTA_EXACT_INTEGER_V1_SUB: *out = l - r; return true;
    case CETTA_EXACT_INTEGER_V1_MUL: *out = l * r; return true;
    default: break;
    }
    if (r == 0)
        return false;
    __int128 truncated = l / r;
    __int128 floored = truncated - ((l % r != 0) && ((l < 0) != (r < 0)));
    switch (operation) {
    case CETTA_EXACT_INTEGER_V1_TQUOT: *out = truncated; return true;
    case CETTA_EXACT_INTEGER_V1_FQUOT: *out = floored; return true;
    case CETTA_EXACT_INTEGER_V1_TREM: *out = l - r * truncated; return true;
    case CETTA_EXACT_INTEGER_V1_FREM: *out = l - r * floored; return true;
    default: return false;
    }
}

static bool fits(__int128 value) {
    return value >= INT64_MIN && value <= INT64_MAX;
}

static unsigned long checked;

static void check(CettaExactIntegerV1Operation operation, int64_t left,
                  int64_t right) {
    __int128 expected = 0;
    bool defined = reference(operation, left, right, &expected);
    int64_t small = 0;
    CettaExactIntegerOutcome outcome =
        cetta_exact_integer_small(operation, left, right, &small);
    if (!defined)
        assert(outcome == CETTA_EXACT_INTEGER_UNDEFINED);
    else if (fits(expected))
        assert(outcome == CETTA_EXACT_INTEGER_VALUE && small == expected);
    else
        assert(outcome == CETTA_EXACT_INTEGER_PROMOTE);
#if CETTA_BUILD_WITH_GMP
    mpz_t l, r, out;
    mpz_inits(l, r, out, NULL);
    mpz_set_si(l, left);
    mpz_set_si(r, right);
    bool big = cetta_exact_integer_big(operation, l, r, out);
    assert(big == defined);
    if (defined) {
        /* The GMP result, read back through its two 64-bit halves. */
        mpz_t high, low;
        mpz_inits(high, low, NULL);
        mpz_fdiv_q_2exp(high, out, 64);
        mpz_fdiv_r_2exp(low, out, 64);
        __int128 value = (__int128)mpz_get_si(high) * ((__int128)1 << 64) +
                         (__int128)(unsigned __int128)mpz_get_ui(low);
        assert(mpz_fits_slong_p(high) && value == expected);
        mpz_clears(high, low, NULL);
        /* Aliasing the output with an operand computes the same value. */
        mpz_set(out, l);
        assert(cetta_exact_integer_big(operation, out, r, out));
        mpz_t again;
        mpz_init(again);
        assert(cetta_exact_integer_big(operation, l, r, again));
        assert(mpz_cmp(out, again) == 0);
        mpz_clear(again);
    }
    mpz_clears(l, r, out, NULL);
#endif
    checked++;
}

static uint64_t next_random(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

/* Operands near the edges: zero, ±1, powers of two and their neighbours,
 * the int64 limits, and arbitrary words. */
static int64_t operand(uint64_t *state) {
    uint64_t word = next_random(state);
    switch (word % 6u) {
    case 0: return (int64_t)(word >> 3) % 9 - 4;
    case 1: return INT64_MIN + (int64_t)((word >> 3) % 4u);
    case 2: return INT64_MAX - (int64_t)((word >> 3) % 4u);
    case 3: {
        int64_t power = (int64_t)(UINT64_C(1) << ((word >> 3) % 63u));
        return ((word >> 9) & 1u ? -power : power) +
               (int64_t)((word >> 10) % 3u) - 1;
    }
    case 4: return (int64_t)(int32_t)(word >> 3);
    default: return (int64_t)word;
    }
}

int main(void) {
    /* Each convention on a hand-checked case: -7 by 2 truncates to -3
     * remainder -1 and floors to -4 remainder 1. */
    int64_t q;
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_TQUOT, -7, 2, &q) == CETTA_EXACT_INTEGER_VALUE && q == -3);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_FQUOT, -7, 2, &q) == CETTA_EXACT_INTEGER_VALUE && q == -4);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_TREM, -7, 2, &q) == CETTA_EXACT_INTEGER_VALUE && q == -1);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_FREM, -7, 2, &q) == CETTA_EXACT_INTEGER_VALUE && q == 1);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_FREM, 7, -2, &q) == CETTA_EXACT_INTEGER_VALUE && q == -1);
    /* The one quotient that does not fit promotes; its remainders are 0. */
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_TQUOT, INT64_MIN, -1, &q) == CETTA_EXACT_INTEGER_PROMOTE);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_TREM, INT64_MIN, -1, &q) == CETTA_EXACT_INTEGER_VALUE && q == 0);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_ADD, INT64_MAX, 1, &q) == CETTA_EXACT_INTEGER_PROMOTE);
    assert(cetta_exact_integer_small(CETTA_EXACT_INTEGER_V1_FREM, 5, 0, &q) == CETTA_EXACT_INTEGER_UNDEFINED);

    static const int64_t edges[] = {
        0, 1, -1, 2, -2, 3, -3, 7, -7, INT32_MAX, INT32_MIN,
        INT64_MAX, INT64_MAX - 1, INT64_MIN, INT64_MIN + 1,
        INT64_C(3037000499), INT64_C(-3037000500), INT64_C(4611686018427387904),
    };
    enum { EDGES = sizeof(edges) / sizeof(edges[0]) };
    for (int operation = 0; operation < CETTA_EXACT_INTEGER_V1_OPERATION_COUNT; operation++) {
        for (unsigned i = 0u; i < EDGES; i++)
            for (unsigned j = 0u; j < EDGES; j++)
                check((CettaExactIntegerV1Operation)operation, edges[i], edges[j]);
        uint64_t state = UINT64_C(0x9e3779b97f4a7c15) + (uint64_t)operation;
        for (unsigned n = 0u; n < 40000u; n++)
            check((CettaExactIntegerV1Operation)operation, operand(&state),
                  operand(&state));
    }
    printf("PASS: exact-integer core, %lu calls agree with the 128-bit reference%s\n",
           checked, CETTA_BUILD_WITH_GMP ? " and the GMP path" : "");
    return 0;
}
