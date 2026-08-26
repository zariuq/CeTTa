#ifndef CETTA_C_SUBSET_GMP_EXACT_INTEGER_V1_H
#define CETTA_C_SUBSET_GMP_EXACT_INTEGER_V1_H

#include "c_subset_generated_abi_v1.h"

#include <gmp.h>
#include <stdbool.h>

/*
 * GMP realization of the opaque exact-integer target ABI.  Default GMP
 * allocation does not expose recoverable allocation failure; this provider is
 * therefore qualified separately at the value/decline observation and does
 * not claim a recoverable resource-fault theorem.
 */
struct CettaCSubsetExactIntegerV1 {
    mpz_t value;
};

void cetta_csubset_gmp_exact_integer_init_v1(
    CettaCSubsetExactIntegerV1 *value);
void cetta_csubset_gmp_exact_integer_clear_v1(
    CettaCSubsetExactIntegerV1 *value);
bool cetta_csubset_gmp_exact_integer_set_decimal_v1(
    CettaCSubsetExactIntegerV1 *value,
    const char *decimal);

CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_add_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);
CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_sub_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);
CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_mul_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);
CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_tquot_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);
CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_fquot_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);
CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_trem_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);
CettaCSubsetGeneratedExternalV1 cetta_csubset_exact_integer_frem_v1(
    const CettaCSubsetExactIntegerV1 *first,
    const CettaCSubsetExactIntegerV1 *second,
    CettaCSubsetExactIntegerV1 *output);

#endif /* CETTA_C_SUBSET_GMP_EXACT_INTEGER_V1_H */
