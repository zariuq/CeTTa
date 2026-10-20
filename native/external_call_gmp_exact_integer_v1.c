#include "external_call_gmp_exact_integer_v1.h"
#include "../src/exact_integer.h"

void cetta_external_call_gmp_exact_integer_init_v1(
    CettaExternalCallExactIntegerV1 *value) {
    if (value)
        mpz_init(value->value);
}

void cetta_external_call_gmp_exact_integer_clear_v1(
    CettaExternalCallExactIntegerV1 *value) {
    if (value)
        mpz_clear(value->value);
}

bool cetta_external_call_gmp_exact_integer_set_decimal_v1(
    CettaExternalCallExactIntegerV1 *value,
    const char *decimal) {
    return value && decimal && mpz_set_str(value->value, decimal, 10) == 0;
}

bool cetta_external_call_exact_integer_is_zero_v1(
    const CettaExternalCallExactIntegerV1 *value) {
    return value && mpz_sgn(value->value) == 0;
}

/* Every operation is the exact-integer core's; a missing argument or a
 * zero divisor is an invalid call, which the generated guard never makes. */
static CettaExternalCallGeneratedExternalV1 gmp_operation(
    CettaExactIntegerV1Operation operation,
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    if (!first || !second || !output ||
        !cetta_exact_integer_big(operation, first->value, second->value,
                                 output->value))
        return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1;
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

#define GMP_OPERATION_V1(name, operation)                                   \
    CettaExternalCallGeneratedExternalV1                                    \
    cetta_external_call_exact_integer_##name##_v1(                          \
        const CettaExternalCallExactIntegerV1 *first,                       \
        const CettaExternalCallExactIntegerV1 *second,                      \
        CettaExternalCallExactIntegerV1 *output) {                          \
        return gmp_operation(operation, first, second, output);            \
    }

GMP_OPERATION_V1(add, CETTA_EXACT_INTEGER_V1_ADD)
GMP_OPERATION_V1(sub, CETTA_EXACT_INTEGER_V1_SUB)
GMP_OPERATION_V1(mul, CETTA_EXACT_INTEGER_V1_MUL)
GMP_OPERATION_V1(tquot, CETTA_EXACT_INTEGER_V1_TQUOT)
GMP_OPERATION_V1(fquot, CETTA_EXACT_INTEGER_V1_FQUOT)
GMP_OPERATION_V1(trem, CETTA_EXACT_INTEGER_V1_TREM)
GMP_OPERATION_V1(frem, CETTA_EXACT_INTEGER_V1_FREM)
