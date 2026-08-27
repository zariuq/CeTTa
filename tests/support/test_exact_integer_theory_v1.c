#include "native/exact_integer_theory_v1.h"

#include <stdio.h>
#include <string.h>

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

static bool parse_document(CettaOpLangV1Document *document,
                           const char *source,
                           CettaOpLangV1Status *status,
                           char *error,
                           size_t error_size) {
    return cetta_op_lang_v1_parse_document_bytes(
        document, (const uint8_t *)source, strlen(source),
        2000000u, 4000000u, status, error, error_size);
}

static bool decode_source(CettaOpLangV1Document *document,
                          CettaExactIntegerTheoryV1 *theory,
                          const char *source,
                          uint32_t work_limit,
                          CettaOpLangV1Status *source_status,
                          CettaExactIntegerTheoryV1Status *theory_status,
                          char *error,
                          size_t error_size) {
    if (!parse_document(
            document, source, source_status, error, error_size)) {
        return false;
    }
    return cetta_exact_integer_theory_v1_decode(
        theory, document->root, work_limit, theory_status,
        error, error_size);
}

static void standard_theory_gate(TestCounts *counts) {
    CettaOpLangV1Document document;
    CettaExactIntegerTheoryV1 theory;
    CettaOpLangV1Status source_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaExactIntegerTheoryV1Status theory_status =
        CETTA_EXACT_INTEGER_THEORY_V1_BAD_ARGUMENT;
    char error[512] = {0};
    uint32_t index;
    bool exact_order = true;

    cetta_op_lang_v1_document_init(&document);
    cetta_exact_integer_theory_v1_init(&theory);
    (void)expect(
        counts,
        cetta_op_lang_v1_parse_document_file(
            &document, "langdef/arithmetic/exact_integer_theory_v1.metta",
            4000000u, 8000000u, &source_status,
            error, sizeof(error)),
        error[0] ? error : "parse standard exact-integer theory");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_exact_integer_theory_v1_decode(
            &theory, document.root, 1000u, &theory_status,
            error, sizeof(error)),
        error[0] ? error : "decode standard exact-integer interface");
    for (index = 0u; index < theory.declaration_len; index++) {
        if ((uint32_t)theory.declarations[index].operation != index ||
            theory.declarations[index].source_byte_left >=
                theory.declarations[index].source_byte_right) {
            exact_order = false;
        }
    }
    (void)expect(
        counts,
        source_status == CETTA_OP_LANG_V1_OK &&
            theory_status == CETTA_EXACT_INTEGER_THEORY_V1_OK &&
            theory.declaration_len ==
                (uint32_t)CETTA_EXACT_INTEGER_V1_OPERATION_COUNT &&
            exact_order,
        "standard theory retains all seven operations in authored order");
    (void)expect(
        counts,
        strcmp(cetta_exact_integer_v1_operation_name(
                   CETTA_EXACT_INTEGER_V1_ADD),
               "exact-int:add") == 0 &&
            !cetta_exact_integer_v1_operation_is_partial(
                CETTA_EXACT_INTEGER_V1_MUL) &&
            cetta_exact_integer_v1_operation_is_partial(
                CETTA_EXACT_INTEGER_V1_FREM),
        "operation metadata matches the shared Lean arithmetic family");
    cetta_exact_integer_theory_v1_free(&theory);
    cetta_op_lang_v1_document_free(&document);
}

static void subtheory_and_rejection_gate(TestCounts *counts) {
    static const char subset[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolFalse CoreAdd) LNil))";
    static const char wrong_name[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolFalse CoreAdd) LNil))";
    static const char wrong_inputs[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger LNil) NumInteger BoolFalse CoreAdd) LNil))";
    static const char wrong_partial[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolTrue CoreAdd) LNil))";
    static const char unknown_operation[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolFalse CoreInvented) LNil))";
    static const char duplicate[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolFalse CoreAdd) "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolFalse CoreAdd) LNil)))";
    static const char improper[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation \"exact-int:add\" "
        "(LCons NumInteger (LCons NumInteger LNil)) "
        "NumInteger BoolFalse CoreAdd) BrokenTail))";
    CettaOpLangV1Document document;
    CettaExactIntegerTheoryV1 theory;
    CettaOpLangV1Status source_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaExactIntegerTheoryV1Status theory_status =
        CETTA_EXACT_INTEGER_THEORY_V1_BAD_ARGUMENT;
    char error[512] = {0};
    const char *malformed_sources[] = {
        wrong_name, wrong_inputs, wrong_partial, unknown_operation,
    };
    size_t index;

    cetta_op_lang_v1_document_init(&document);
    cetta_exact_integer_theory_v1_init(&theory);
    (void)expect(
        counts,
        decode_source(
            &document, &theory, subset, 1000u,
            &source_status, &theory_status, error, sizeof(error)) &&
            theory.declaration_len == 1u &&
            theory.declarations[0].operation ==
                CETTA_EXACT_INTEGER_V1_ADD,
        error[0] ? error : "proper decoded subinterface succeeds");

    for (index = 0u;
         index < sizeof(malformed_sources) / sizeof(malformed_sources[0]);
         index++) {
        error[0] = '\0';
        (void)expect(
            counts,
            !decode_source(
                &document, &theory, malformed_sources[index], 1000u,
                &source_status, &theory_status, error, sizeof(error)) &&
                source_status == CETTA_OP_LANG_V1_OK &&
                theory_status ==
                    CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DECLARATION &&
                theory.declaration_len == 1u &&
                theory.declarations[0].operation ==
                    CETTA_EXACT_INTEGER_V1_ADD,
            "redundant declaration mismatch rejects atomically");
    }

    error[0] = '\0';
    (void)expect(
        counts,
        !decode_source(
            &document, &theory, duplicate, 1000u,
            &source_status, &theory_status, error, sizeof(error)) &&
            theory_status ==
                CETTA_EXACT_INTEGER_THEORY_V1_DUPLICATE_OPERATION &&
            theory.declaration_len == 1u,
        "duplicate operation identity rejects atomically");
    error[0] = '\0';
    (void)expect(
        counts,
        !decode_source(
            &document, &theory, improper, 1000u,
            &source_status, &theory_status, error, sizeof(error)) &&
            theory_status ==
                CETTA_EXACT_INTEGER_THEORY_V1_MALFORMED_DOCUMENT &&
            theory.declaration_len == 1u,
        "improper declaration list rejects atomically");
    error[0] = '\0';
    (void)expect(
        counts,
        !decode_source(
            &document, &theory, subset, 1u,
            &source_status, &theory_status, error, sizeof(error)) &&
            theory_status ==
                CETTA_EXACT_INTEGER_THEORY_V1_RESOURCE_LIMIT &&
            theory.declaration_len == 1u,
        "decode resource exhaustion remains distinct and atomic");
    cetta_exact_integer_theory_v1_free(&theory);
    cetta_op_lang_v1_document_free(&document);
}

int main(void) {
    TestCounts counts = {0};

    standard_theory_gate(&counts);
    subtheory_and_rejection_gate(&counts);
    printf("exact-integer operation-interface decode: %u passed, %u failed\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
