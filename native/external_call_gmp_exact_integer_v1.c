#include "external_call_gmp_exact_integer_v1.h"

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

static CettaExternalCallGeneratedExternalV1 gmp_arguments_ok(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    return first && second && output
        ? CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1
        : CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_add_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    CettaExternalCallGeneratedExternalV1 status =
        gmp_arguments_ok(first, second, output);
    if (status != CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1)
        return status;
    mpz_add(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_sub_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    CettaExternalCallGeneratedExternalV1 status =
        gmp_arguments_ok(first, second, output);
    if (status != CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1)
        return status;
    mpz_sub(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_mul_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    CettaExternalCallGeneratedExternalV1 status =
        gmp_arguments_ok(first, second, output);
    if (status != CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1)
        return status;
    mpz_mul(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

static bool gmp_division_arguments_ok(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    return first && second && output && mpz_sgn(second->value) != 0;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_tquot_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    if (!gmp_division_arguments_ok(first, second, output))
        return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1;
    mpz_tdiv_q(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_fquot_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    if (!gmp_division_arguments_ok(first, second, output))
        return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1;
    mpz_fdiv_q(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_trem_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    if (!gmp_division_arguments_ok(first, second, output))
        return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1;
    mpz_tdiv_r(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_frem_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output) {
    if (!gmp_division_arguments_ok(first, second, output))
        return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_ENGINE_FAULT_V1;
    mpz_fdiv_r(output->value, first->value, second->value);
    return CETTA_EXTERNAL_CALL_GENERATED_EXTERNAL_VALUE_V1;
}
