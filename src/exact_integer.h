#ifndef CETTA_EXACT_INTEGER_H
#define CETTA_EXACT_INTEGER_H

#include "../native/exact_integer_operation_v1.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * One exact-integer arithmetic for every evaluator: the seven operations of
 * the exact-integer theory, on small (int64) operands and on GMP operands.
 * The interpreter, prepared queries, fold kernels, the register machine and
 * the external-call provider all compute through these two functions.
 *
 * A small call answers VALUE when the exact result fits in int64, PROMOTE
 * when it does not (the caller then runs the big call), and never wraps.  A
 * zero divisor is UNDEFINED on both paths.  Which operation a spelling names
 * and how a result or an undefined call is observed belong to the dialect.
 */
typedef enum {
    CETTA_EXACT_INTEGER_VALUE = 0,
    CETTA_EXACT_INTEGER_PROMOTE,
    CETTA_EXACT_INTEGER_UNDEFINED,
} CettaExactIntegerOutcome;

static inline CettaExactIntegerOutcome cetta_exact_integer_small(
    CettaExactIntegerV1Operation operation, int64_t left, int64_t right,
    int64_t *out) {
    switch (operation) {
    case CETTA_EXACT_INTEGER_V1_ADD:
        return __builtin_add_overflow(left, right, out)
            ? CETTA_EXACT_INTEGER_PROMOTE : CETTA_EXACT_INTEGER_VALUE;
    case CETTA_EXACT_INTEGER_V1_SUB:
        return __builtin_sub_overflow(left, right, out)
            ? CETTA_EXACT_INTEGER_PROMOTE : CETTA_EXACT_INTEGER_VALUE;
    case CETTA_EXACT_INTEGER_V1_MUL:
        return __builtin_mul_overflow(left, right, out)
            ? CETTA_EXACT_INTEGER_PROMOTE : CETTA_EXACT_INTEGER_VALUE;
    default:
        break;
    }
    if (right == 0)
        return CETTA_EXACT_INTEGER_UNDEFINED;
    /* INT64_MIN / -1 is the one quotient that does not fit; its remainder
     * is 0 under either convention. */
    bool edge = left == INT64_MIN && right == -1;
    int64_t remainder = edge ? 0 : left % right;
    switch (operation) {
    case CETTA_EXACT_INTEGER_V1_TQUOT:
        if (edge)
            return CETTA_EXACT_INTEGER_PROMOTE;
        *out = left / right;
        return CETTA_EXACT_INTEGER_VALUE;
    case CETTA_EXACT_INTEGER_V1_FQUOT:
        if (edge)
            return CETTA_EXACT_INTEGER_PROMOTE;
        *out = left / right -
               (remainder != 0 && ((remainder < 0) != (right < 0)));
        return CETTA_EXACT_INTEGER_VALUE;
    case CETTA_EXACT_INTEGER_V1_TREM:
        *out = remainder;
        return CETTA_EXACT_INTEGER_VALUE;
    case CETTA_EXACT_INTEGER_V1_FREM:
        *out = remainder != 0 && ((remainder < 0) != (right < 0))
            ? remainder + right : remainder;
        return CETTA_EXACT_INTEGER_VALUE;
    default:
        return CETTA_EXACT_INTEGER_UNDEFINED;
    }
}

/* Builds without GMP (CETTA_BUILD_WITH_GMP 0) have no big path; a file
 * compiled outside the build configuration, such as the standalone
 * provider, has it. */
#if !defined(CETTA_BUILD_WITH_GMP) || CETTA_BUILD_WITH_GMP
#include <gmp.h>

/* The same operations on GMP operands; out may alias either operand.  False
 * exactly for an undefined call. */
static inline bool cetta_exact_integer_big(
    CettaExactIntegerV1Operation operation, mpz_srcptr left,
    mpz_srcptr right, mpz_ptr out) {
    switch (operation) {
    case CETTA_EXACT_INTEGER_V1_ADD:
        mpz_add(out, left, right);
        return true;
    case CETTA_EXACT_INTEGER_V1_SUB:
        mpz_sub(out, left, right);
        return true;
    case CETTA_EXACT_INTEGER_V1_MUL:
        mpz_mul(out, left, right);
        return true;
    default:
        break;
    }
    if (mpz_sgn(right) == 0)
        return false;
    switch (operation) {
    case CETTA_EXACT_INTEGER_V1_TQUOT:
        mpz_tdiv_q(out, left, right);
        return true;
    case CETTA_EXACT_INTEGER_V1_FQUOT:
        mpz_fdiv_q(out, left, right);
        return true;
    case CETTA_EXACT_INTEGER_V1_TREM:
        mpz_tdiv_r(out, left, right);
        return true;
    case CETTA_EXACT_INTEGER_V1_FREM:
        mpz_fdiv_r(out, left, right);
        return true;
    default:
        return false;
    }
}
#endif

#endif /* CETTA_EXACT_INTEGER_H */
