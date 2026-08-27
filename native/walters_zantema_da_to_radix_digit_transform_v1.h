#ifndef CETTA_WALTERS_ZANTEMA_DA_TO_RADIX_DIGIT_TRANSFORM_V1_H
#define CETTA_WALTERS_ZANTEMA_DA_TO_RADIX_DIGIT_TRANSFORM_V1_H

#include "radix_digit_target_program_v1.h"
#include "operational_language_def_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * "DA" denotes the digit-as-operators rewrite system of Walters and Zantema,
 * Rewrite Systems for Integer Arithmetic (RTA 1995).  This is algorithmic
 * Expand legalization: the transformer reads those authored rule bodies and
 * constructs target-owned RadixDigitMachine programs; rule names carry
 * provenance but do not determine behavior.
 */

typedef enum {
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK = 0,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_BAD_ARGUMENT,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_SOURCE,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INCONSISTENT_SOURCE,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_UNSUPPORTED_TARGET,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_ALLOCATION_FAILURE,
    CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_INTERNAL_FAILURE
} CettaWaltersZantemaDaRadixDigitV1Status;

typedef CettaRadixDigitV1TableRow CettaWaltersZantemaDaRadixDigitV1TableRow;
typedef CettaRadixDigitV1Table CettaWaltersZantemaDaRadixDigitV1Table;

typedef struct {
    uint32_t radix;
    CettaRadixDigitV1Program addition_program;
    CettaRadixDigitV1Program multiplication_program;
    CettaRadixDigitV1TargetProfile target_profile;
} CettaWaltersZantemaDaRadixDigitV1Program;

void cetta_walters_zantema_da_radix_digit_v1_program_init(CettaWaltersZantemaDaRadixDigitV1Program *program);
void cetta_walters_zantema_da_radix_digit_v1_program_free(CettaWaltersZantemaDaRadixDigitV1Program *program);

/*
 * Structurally inspect both supplied LanguageDefs and derive an owned
 * RadixDigitMachine program.  Source rule names are provenance only.  Replacement is atomic:
 * failure leaves an existing value in out unchanged.
 */
bool cetta_walters_zantema_da_radix_digit_v1_transform(
    CettaWaltersZantemaDaRadixDigitV1Program *out,
    const CettaOperationalLanguageDefV1 *source,
    const CettaOperationalLanguageDefV1 *target,
    CettaWaltersZantemaDaRadixDigitV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_walters_zantema_da_radix_digit_v1_status_name(CettaWaltersZantemaDaRadixDigitV1Status status);

#endif /* CETTA_WALTERS_ZANTEMA_DA_TO_RADIX_DIGIT_TRANSFORM_V1_H */
