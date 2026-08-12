#ifndef CETTA_NIK_RUNTIME_INTERNAL_H
#define CETTA_NIK_RUNTIME_INTERNAL_H

#include "generated/prime_nik_authorities_v1.generated.h"
#include "gslt_language_runtime.h"
#include "nik_runtime.h"

typedef bool (*CettaNikGsltQueryV1)(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Arena *output_arena,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error,
    size_t error_size);

bool cetta_nik_authority_descriptor_valid_v1(
    const CettaNikAuthorityV1 *authority);

bool cetta_nik_authority_catalog_valid_v1(
    const CettaNikAuthorityV1 *authorities,
    size_t authority_count,
    const char *expected_digest);

CettaNikOutcome cetta_nik_check_with_query_v1(
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size,
    CettaNikGsltQueryV1 query_realization);

CettaNikOutcome cetta_nik_check_differential_v1(
    const char *authority_alias,
    Atom *claim,
    Atom *proof,
    CettaNikLimits limits,
    Arena *arena,
    CettaNikReceiptV1 *receipt,
    char *error_buf,
    size_t error_buf_size);

#endif /* CETTA_NIK_RUNTIME_INTERNAL_H */
