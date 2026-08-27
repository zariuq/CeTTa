#include "native/walters_zantema_da_radix_digit_nik_v1.h"

#include <stdio.h>
#include <string.h>

static unsigned passed;
static unsigned failed;

#define CHECK(condition) do { \
    if (condition) { \
        ++passed; \
    } else { \
        ++failed; \
        fprintf(stderr, "FAIL %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); \
    } \
} while (0)

static bool parse_language(
    CettaOperationalLanguageDefV1 *language,
    const char *path) {
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_OK;
    char error[256];
    return cetta_op_lang_v1_parse_file(language, path,
        4000000u, 4000000u, &status, error, sizeof(error));
}

static bool digits_are(
    const CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt *receipt,
    const uint32_t *expected,
    uint32_t expected_len) {
    return receipt->execution.kind == CETTA_RADIX_DIGIT_V1_OUTCOME_VALUE &&
        receipt->execution.digit_len == expected_len &&
        (expected_len == 0u || memcmp(receipt->execution.digits, expected,
            (size_t)expected_len * sizeof(*expected)) == 0);
}

int main(void) {
    CettaOperationalLanguageDefV1 source;
    CettaOperationalLanguageDefV1 target;
    CettaWaltersZantemaDaRadixDigitNikV1Admission admission;
    CettaWaltersZantemaDaRadixDigitNikV1LanguageReceipt language_receipt;
    CettaNikHostedNativeReceiptV1 hosted_receipt;
    CettaWaltersZantemaDaRadixDigitNikV1Request request;
    CettaNikHostedNativeCallKindV1 call;
    char error[256];
    uint32_t seven[] = {1u, 1u, 1u};
    uint32_t one[] = {1u};
    uint32_t eight[] = {0u, 0u, 0u, 1u};
    uint32_t three[] = {1u, 1u};
    uint32_t two[] = {0u, 1u};
    uint32_t six[] = {0u, 1u, 1u};
    uint32_t invalid[] = {2u};

    cetta_op_lang_v1_init(&source);
    cetta_op_lang_v1_init(&target);
    cetta_walters_zantema_da_radix_digit_nik_v1_language_receipt_init(&language_receipt);
    CHECK(parse_language(&source,
        "langdef/arithmetic/walters_zantema_da_radix2_v1.metta"));
    CHECK(parse_language(&target,
        "langdef/machines/radix_digit_machine_v1.metta"));
    CHECK(cetta_nik_native_calculus_v1_is_valid(
        cetta_walters_zantema_da_radix_digit_nik_v1_calculus()));

    CettaOperationalLanguageDefV1 malformed_source = source;
    malformed_source.rewrites_field = source.terms_field;
    CettaWaltersZantemaDaRadixDigitNikV1Admission malformed_admission =
        cetta_walters_zantema_da_radix_digit_nik_v1_admit(
            &malformed_source, &target, error, sizeof(error));
    CHECK(malformed_admission.kind == CETTA_NIK_HOST_ADMISSION_INVALID_V1 &&
        malformed_admission.host == NULL &&
        malformed_admission.transform_status != CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK);

    CettaOperationalLanguageDefV1 malformed_target = target;
    malformed_target.terms_field = target.types_field;
    malformed_admission = cetta_walters_zantema_da_radix_digit_nik_v1_admit(
        &source, &malformed_target, error, sizeof(error));
    CHECK(malformed_admission.kind == CETTA_NIK_HOST_ADMISSION_INVALID_V1 &&
        malformed_admission.host == NULL &&
        malformed_admission.transform_status != CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK);

    admission = cetta_walters_zantema_da_radix_digit_nik_v1_admit(
        &source, &target, error, sizeof(error));
    CHECK(admission.kind == CETTA_NIK_HOST_ADMISSION_ADMITTED_V1);
    CHECK(admission.transform_status == CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_V1_OK);
    CHECK(admission.host != NULL &&
        cetta_walters_zantema_da_radix_digit_nik_v1_is_current(admission.host));
    if (!admission.host)
        goto done;

    request = (CettaWaltersZantemaDaRadixDigitNikV1Request){
        .first = seven,
        .first_len = 3u,
        .second = one,
        .second_len = 1u,
        .output_limit = 16u,
        .fuel = 10000u,
    };
    call = cetta_walters_zantema_da_radix_digit_nik_v1_run(admission.host,
        CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD, &request,
        &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.kind == CETTA_NIK_RESULT_OUTCOME &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED &&
        hosted_receipt.operation_identity ==
            CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD &&
        digits_are(&language_receipt, eight, 4u));
    CHECK(language_receipt.execution.event_len > 0u &&
        language_receipt.execution.steps > 0u);

    request.first = three;
    request.first_len = 2u;
    request.second = two;
    request.second_len = 2u;
    call = cetta_walters_zantema_da_radix_digit_nik_v1_run(admission.host,
        CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_MULTIPLY, &request,
        &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED &&
        digits_are(&language_receipt, six, 3u));

    request.first = invalid;
    request.first_len = 1u;
    request.second = one;
    request.second_len = 1u;
    call = cetta_walters_zantema_da_radix_digit_nik_v1_run(admission.host,
        CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD, &request,
        &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_REFUTED &&
        language_receipt.execution.kind ==
            CETTA_RADIX_DIGIT_V1_OUTCOME_LANGUAGE_FAULT &&
        language_receipt.execution.fault ==
            CETTA_RADIX_DIGIT_V1_FAULT_INVALID_DIGIT);

    request.first = one;
    request.first_len = 1u;
    request.output_limit = 1u;
    call = cetta_walters_zantema_da_radix_digit_nik_v1_run(admission.host,
        CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD, &request,
        &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_INCOMPLETE &&
        language_receipt.execution.kind ==
            CETTA_RADIX_DIGIT_V1_OUTCOME_RESOURCE_FAULT);

    uint32_t receipt_steps = language_receipt.execution.steps;
    call = cetta_walters_zantema_da_radix_digit_nik_v1_run(admission.host, UINT64_C(9999), &request,
        &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_OUTSIDE_CALCULUS_V1 &&
        language_receipt.execution.steps == receipt_steps);

    char source_digest_first = source.source_sha256[0];
    source.source_sha256[0] = source_digest_first == '0' ? '1' : '0';
    CHECK(!cetta_walters_zantema_da_radix_digit_nik_v1_is_current(admission.host));
    call = cetta_walters_zantema_da_radix_digit_nik_v1_run(admission.host,
        CETTA_WALTERS_ZANTEMA_DA_RADIX_DIGIT_NIK_V1_OPERATION_ADD, &request,
        &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1 &&
        language_receipt.execution.steps == receipt_steps);
    source.source_sha256[0] = source_digest_first;
    CHECK(cetta_walters_zantema_da_radix_digit_nik_v1_is_current(admission.host));

    char target_digest_first = target.source_sha256[0];
    target.source_sha256[0] = target_digest_first == '0' ? '1' : '0';
    CHECK(!cetta_walters_zantema_da_radix_digit_nik_v1_is_current(admission.host));
    target.source_sha256[0] = target_digest_first;
    CHECK(cetta_walters_zantema_da_radix_digit_nik_v1_is_current(admission.host));

done:
    cetta_walters_zantema_da_radix_digit_nik_v1_destroy(admission.host);
    cetta_walters_zantema_da_radix_digit_nik_v1_language_receipt_free(&language_receipt);
    cetta_op_lang_v1_free(&target);
    cetta_op_lang_v1_free(&source);
    printf("Walters--Zantema DA/RadixDigitMachine NIK v1: %u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}
