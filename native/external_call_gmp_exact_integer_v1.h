#ifndef CETTA_EXTERNAL_CALL_GMP_EXACT_INTEGER_V1_H
#define CETTA_EXTERNAL_CALL_GMP_EXACT_INTEGER_V1_H

#include "external_call_generated_abi_v1.h"

#include <gmp.h>
#include <stdbool.h>

/*
 * GMP realization of the opaque exact-integer target ABI.  Default GMP
 * allocation does not expose recoverable allocation failure; this provider is
 * therefore qualified separately at the value/decline observation and does
 * not claim a recoverable resource-fault theorem.
 */
struct CettaExternalCallExactIntegerV1 {
    mpz_t value;
};

void cetta_external_call_gmp_exact_integer_init_v1(
    CettaExternalCallExactIntegerV1 *value);
void cetta_external_call_gmp_exact_integer_clear_v1(
    CettaExternalCallExactIntegerV1 *value);
bool cetta_external_call_gmp_exact_integer_set_decimal_v1(
    CettaExternalCallExactIntegerV1 *value,
    const char *decimal);

CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_add_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);
CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_sub_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);
CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_mul_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);
CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_tquot_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);
CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_fquot_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);
CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_trem_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);
CettaExternalCallGeneratedExternalV1 cetta_external_call_exact_integer_frem_v1(
    const CettaExternalCallExactIntegerV1 *first,
    const CettaExternalCallExactIntegerV1 *second,
    CettaExternalCallExactIntegerV1 *output);

#endif /* CETTA_EXTERNAL_CALL_GMP_EXACT_INTEGER_V1_H */
