#include "native/external_call_gmp_exact_integer_v1.h"

#include <stdbool.h>
#include <stdio.h>

typedef CettaExternalCallGeneratedOutcomeV1 (*GeneratedOperation)(
    const CettaExternalCallExactIntegerV1 *,
    const CettaExternalCallExactIntegerV1 *,
    CettaExternalCallExactIntegerV1 *,
    CettaExternalCallGeneratedReceiptV1 *);

#define DECLARE_GENERATED(name)                                               \
    CettaExternalCallGeneratedOutcomeV1 name(                                     \
        const CettaExternalCallExactIntegerV1 *,                                  \
        const CettaExternalCallExactIntegerV1 *,                                  \
        CettaExternalCallExactIntegerV1 *,                                        \
        CettaExternalCallGeneratedReceiptV1 *)

DECLARE_GENERATED(cetta_generated_exact_integer_add_v1);
DECLARE_GENERATED(cetta_generated_exact_integer_sub_v1);
DECLARE_GENERATED(cetta_generated_exact_integer_mul_v1);
DECLARE_GENERATED(cetta_generated_exact_integer_tquot_v1);
DECLARE_GENERATED(cetta_generated_exact_integer_fquot_v1);
DECLARE_GENERATED(cetta_generated_exact_integer_trem_v1);
DECLARE_GENERATED(cetta_generated_exact_integer_frem_v1);

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

static bool expect(TestCounts *counts, bool condition, const char *name) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

static void run_value_case(TestCounts *counts,
                           GeneratedOperation operation,
                           const char *first_text,
                           const char *second_text,
                           long expected,
                           uint32_t expected_events,
                           const char *name) {
    CettaExternalCallExactIntegerV1 first;
    CettaExternalCallExactIntegerV1 second;
    CettaExternalCallExactIntegerV1 output;
    CettaExternalCallGeneratedEventV1 events[4];
    CettaExternalCallGeneratedReceiptV1 receipt;
    CettaExternalCallGeneratedOutcomeV1 outcome;

    cetta_external_call_gmp_exact_integer_init_v1(&first);
    cetta_external_call_gmp_exact_integer_init_v1(&second);
    cetta_external_call_gmp_exact_integer_init_v1(&output);
    cetta_external_call_generated_receipt_init_v1(&receipt, events, 4u);
    outcome =
        cetta_external_call_gmp_exact_integer_set_decimal_v1(&first, first_text) &&
        cetta_external_call_gmp_exact_integer_set_decimal_v1(&second, second_text)
        ? operation(&first, &second, &output, &receipt)
        : CETTA_EXTERNAL_CALL_GENERATED_ENGINE_FAULT_V1;
    (void)expect(
        counts,
        outcome == CETTA_EXTERNAL_CALL_GENERATED_VALUE_V1 &&
            receipt.outcome == CETTA_EXTERNAL_CALL_GENERATED_VALUE_V1 &&
            receipt.complete && receipt.event_count == expected_events &&
            mpz_cmp_si(output.value, expected) == 0,
        name);
    cetta_external_call_gmp_exact_integer_clear_v1(&output);
    cetta_external_call_gmp_exact_integer_clear_v1(&second);
    cetta_external_call_gmp_exact_integer_clear_v1(&first);
}

static void run_zero_divisor_case(TestCounts *counts,
                                  GeneratedOperation operation,
                                  const char *name) {
    CettaExternalCallExactIntegerV1 first;
    CettaExternalCallExactIntegerV1 second;
    CettaExternalCallExactIntegerV1 output;
    CettaExternalCallGeneratedEventV1 events[2];
    CettaExternalCallGeneratedReceiptV1 receipt;
    CettaExternalCallGeneratedOutcomeV1 outcome;

    cetta_external_call_gmp_exact_integer_init_v1(&first);
    cetta_external_call_gmp_exact_integer_init_v1(&second);
    cetta_external_call_gmp_exact_integer_init_v1(&output);
    (void)cetta_external_call_gmp_exact_integer_set_decimal_v1(&first, "17");
    (void)cetta_external_call_gmp_exact_integer_set_decimal_v1(&second, "0");
    mpz_set_si(output.value, 12345);
    cetta_external_call_generated_receipt_init_v1(&receipt, events, 2u);
    outcome = operation(&first, &second, &output, &receipt);
    (void)expect(
        counts,
        outcome == CETTA_EXTERNAL_CALL_GENERATED_DECLINED_V1 &&
            receipt.outcome == CETTA_EXTERNAL_CALL_GENERATED_DECLINED_V1 &&
            receipt.complete && receipt.event_count == 2u &&
            receipt.events[0].kind ==
                CETTA_EXTERNAL_CALL_GENERATED_EVENT_STEP_V1 &&
            receipt.events[0].instruction == 0u &&
            receipt.events[1].kind ==
                CETTA_EXTERNAL_CALL_GENERATED_EVENT_STEP_V1 &&
            receipt.events[1].instruction == 1u &&
            mpz_cmp_si(output.value, 12345) == 0,
        name);
    cetta_external_call_gmp_exact_integer_clear_v1(&output);
    cetta_external_call_gmp_exact_integer_clear_v1(&second);
    cetta_external_call_gmp_exact_integer_clear_v1(&first);
}

int main(void) {
    TestCounts counts = {0};

    run_value_case(&counts, cetta_generated_exact_integer_add_v1,
                   "-7", "3", -4, 3u, "generated add");
    run_value_case(&counts, cetta_generated_exact_integer_sub_v1,
                   "-7", "3", -10, 3u, "generated sub");
    run_value_case(&counts, cetta_generated_exact_integer_mul_v1,
                   "-7", "3", -21, 3u, "generated mul");
    run_value_case(&counts, cetta_generated_exact_integer_tquot_v1,
                   "-7", "3", -2, 4u, "generated truncating quotient");
    run_value_case(&counts, cetta_generated_exact_integer_fquot_v1,
                   "-7", "3", -3, 4u, "generated floor quotient");
    run_value_case(&counts, cetta_generated_exact_integer_trem_v1,
                   "-7", "3", -1, 4u, "generated truncating remainder");
    run_value_case(&counts, cetta_generated_exact_integer_frem_v1,
                   "-7", "3", 2, 4u, "generated floor remainder");
    run_zero_divisor_case(&counts, cetta_generated_exact_integer_tquot_v1,
                          "truncating quotient declines zero divisor");
    run_zero_divisor_case(&counts, cetta_generated_exact_integer_fquot_v1,
                          "floor quotient declines zero divisor");
    run_zero_divisor_case(&counts, cetta_generated_exact_integer_trem_v1,
                          "truncating remainder declines zero divisor");
    run_zero_divisor_case(&counts, cetta_generated_exact_integer_frem_v1,
                          "floor remainder declines zero divisor");
    printf("(ExactArithmeticExternalCallGeneratedSummary passed=%u failed=%u)\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
