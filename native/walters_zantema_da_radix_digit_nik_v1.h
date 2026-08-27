#ifndef CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_H
#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_H

#include "radix_digit_reference_evaluator_v1.h"
#include "walters_zantema_da_to_radix_digit_transform_v1.h"
#include "nik_hosted_calculus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_CANONICAL_DIGITS UINT64_C(100)
#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_ORDERED_PROVENANCE UINT64_C(200)
#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_ADDITION UINT64_C(300)
#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_CAPABILITY_MULTIPLICATION UINT64_C(400)

#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD UINT64_C(1000)
#define CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_MULTIPLY UINT64_C(2000)

typedef struct CettaWaltersZantemaDaRadixDigitNikV1 CettaWaltersZantemaDaRadixDigitNikV1;

typedef struct {
    const uint32_t *first;
    uint32_t first_len;
    const uint32_t *second;
    uint32_t second_len;
    uint32_t output_limit;
    uint32_t fuel;
} CettaWaltersZantemaDaRadixDigitNikV1Request;

typedef struct {
    CettaRadixDigitV1RunResult execution;
} CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt;

void cetta_walters_zantema_da_radix_digit_nik_v1_language_receipt_init(
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *receipt);

void cetta_walters_zantema_da_radix_digit_nik_v1_language_receipt_free(
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *receipt);

typedef struct {
    CettaNikHostAdmissionKindV1 kind;
    CettaWaltersZantemaDaRadixDigitV1Status transform_status;
    CettaWaltersZantemaDaRadixDigitNikV1 *host;
} CettaWaltersZantemaDaRadixDigitNikV1Admission;

/* Admission owns a program freshly derived from both supplied LanguageDefs.
 * The presentations are borrowed and must remain alive and immutable except
 * for replacement by a new revision, which makes this host stale.  Digest
 * snapshots establish identity/currentness only; they never substitute for
 * structural transformation or implementation qualification. */
CettaWaltersZantemaDaRadixDigitNikV1Admission cetta_walters_zantema_da_radix_digit_nik_v1_admit(
    const CettaOperationalLanguageDefV1 *source,
    const CettaOperationalLanguageDefV1 *target,
    char *error_buf,
    size_t error_buf_size);

void cetta_walters_zantema_da_radix_digit_nik_v1_destroy(CettaWaltersZantemaDaRadixDigitNikV1 *host);

bool cetta_walters_zantema_da_radix_digit_nik_v1_is_current(const CettaWaltersZantemaDaRadixDigitNikV1 *host);

CettaNikHostedNativeCallKindV1 cetta_walters_zantema_da_radix_digit_nik_v1_run(
    CettaWaltersZantemaDaRadixDigitNikV1 *host,
    CettaNikNativeOperationIdV1 operation_identity,
    const CettaWaltersZantemaDaRadixDigitNikV1Request *request,
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *language_receipt,
    CettaNikHostedNativeReceiptV1 *hosted_receipt);

const CettaNikNativeCalculusV1 *cetta_walters_zantema_da_radix_digit_nik_v1_calculus(void);

#endif /* CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_H */
