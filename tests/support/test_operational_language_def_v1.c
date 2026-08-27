#include "native/operational_language_def_v1.h"

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

static bool parse_text(CettaOperationalLanguageDefV1 *language,
                       const char *text,
                       uint32_t gll_limit,
                       uint32_t glr_limit,
                       CettaOpLangV1Status *status,
                       char *error,
                       size_t error_size) {
    return cetta_op_lang_v1_parse_bytes(
        language, (const uint8_t *)text, strlen(text),
        gll_limit, glr_limit, status, error, error_size);
}

static bool parse_document_text(CettaOpLangV1Document *document,
                                const char *text,
                                uint32_t gll_limit,
                                uint32_t glr_limit,
                                CettaOpLangV1Status *status,
                                char *error,
                                size_t error_size) {
    return cetta_op_lang_v1_parse_document_bytes(
        document, (const uint8_t *)text, strlen(text),
        gll_limit, glr_limit, status, error, error_size);
}

static bool bytes_equal(const uint8_t *left,
                        size_t left_len,
                        const uint8_t *right,
                        size_t right_len) {
    return left_len == right_len &&
        (left_len == 0u ||
         (left && right && memcmp(left, right, left_len) == 0));
}

static void canonical_file_gate(TestCounts *counts) {
    CettaOperationalLanguageDefV1 language;
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    const CettaOpLangV1SExpr *first_term;
    const CettaOpLangV1SExpr *last_operation;
    const CettaOpLangV1SExpr *last_term;
    char error[512] = {0};

    cetta_op_lang_v1_init(&language);
    if (!cetta_op_lang_v1_parse_file(
            &language, "langdef/arithmetic/exact_arithmetic_v1.metta",
            16000000u, 32000000u, &status, error, sizeof(error))) {
        fprintf(stderr, "ExactArithmetic parse status=%s detail=%s\n",
                cetta_op_lang_v1_status_name(status),
                error[0] ? error : "<none>");
        (void)expect(counts, false,
                     "parse ExactArithmetic with native GLL and GLR");
    } else {
        (void)expect(counts, true,
                     "parse ExactArithmetic with native GLL and GLR");
    }
    (void)expect(counts, status == CETTA_OP_LANG_V1_OK,
                 "ExactArithmetic status is success");
    (void)expect(
        counts,
        strcmp(language.source_sha256,
               "c0958f71834828bdef9e2b825e5755199128b863b09b11ea367ff96086fe45ae") == 0,
        "raw ExactArithmetic source identity is exact");
    (void)expect(counts,
                 bytes_equal(language.name_bytes, language.name_len,
                             (const uint8_t *)"ExactArithmetic", 15u),
                 "ExactArithmetic name retained");
    (void)expect(counts,
                 cetta_op_lang_v1_field_len(language.types_field) == 4u &&
                 cetta_op_lang_v1_field_len(language.terms_field) == 11u &&
                 cetta_op_lang_v1_field_len(language.equations_field) == 0u &&
                 cetta_op_lang_v1_field_len(language.rewrites_field) == 7u,
                 "five fields retain exact entry counts");
    first_term = cetta_op_lang_v1_field_entry(language.terms_field, 0u);
    last_operation = cetta_op_lang_v1_field_entry(language.terms_field, 6u);
    last_term = cetta_op_lang_v1_field_entry(language.terms_field, 10u);
    (void)expect(
        counts,
        cetta_op_lang_v1_application_is(first_term, "GrammarRule", 5u) &&
            cetta_op_lang_v1_string_is(
                first_term->as.application.arguments[0],
                (const uint8_t *)"arith:add", 9u) &&
            last_operation &&
            cetta_op_lang_v1_application_is(
                last_operation, "GrammarRule", 5u) &&
            cetta_op_lang_v1_string_is(
                last_operation->as.application.arguments[0],
                (const uint8_t *)"arith:frem", 10u) &&
            cetta_op_lang_v1_application_is(
                last_term, "GrammarRule", 5u) &&
            cetta_op_lang_v1_string_is(
                last_term->as.application.arguments[0],
                (const uint8_t *)"arith:halted", 12u),
        "field order survives source projection");
    (void)expect(
        counts,
        language.gll.source_pass_count == 0u &&
            language.glr.source_pass_count == 0u &&
            language.gll.decoded_byte_len == 0u &&
            language.glr.decoded_byte_len == 0u &&
            language.gll.source_decode_pass_count == 1u &&
            language.glr.source_decode_pass_count == 1u &&
            language.gll.lexical_projection_pass_count == 1u &&
            language.glr.lexical_projection_pass_count == 1u &&
            language.gll.node_len > 0u && language.glr.node_len > 0u &&
            language.gll.derivation_fingerprint ==
                language.glr.derivation_fingerprint,
        "independent parser receipts retain source and lexical accounting");
    cetta_op_lang_v1_free(&language);
}

static void order_and_multiplicity_gate(TestCounts *counts) {
    static const char duplicate_source[] =
        "(GSLTLanguageDefWireV1 \"DuplicateTypes\" "
        "(LCons (Type Integer ast) "
        "(LCons (Type Integer ast) LNil)) LNil LNil LNil)";
    static const char improper_source[] =
        "(GSLTLanguageDefWireV1 \"Improper\" "
        "(LCons (Type Integer ast) BrokenTail) LNil LNil LNil)";
    CettaOperationalLanguageDefV1 language;
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    const CettaOpLangV1SExpr *first;
    const CettaOpLangV1SExpr *second;
    char error[512] = {0};

    cetta_op_lang_v1_init(&language);
    (void)expect(
        counts,
        parse_text(&language, duplicate_source, 500000u, 1000000u,
                   &status, error, sizeof(error)),
        error[0] ? error : "parse duplicate field entries");
    first = cetta_op_lang_v1_field_entry(language.types_field, 0u);
    second = cetta_op_lang_v1_field_entry(language.types_field, 1u);
    (void)expect(counts,
                 cetta_op_lang_v1_field_len(language.types_field) == 2u &&
                     first && second &&
                     cetta_op_lang_v1_sexpr_equal(first, second) == false,
                 "duplicates retain distinct source occurrences and spans");

    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, improper_source, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF &&
            bytes_equal(language.name_bytes, language.name_len,
                        (const uint8_t *)"DuplicateTypes", 14u),
        "improper list rejects atomically");
    cetta_op_lang_v1_free(&language);
}

static void malformed_and_resource_gates(TestCounts *counts) {
    static const char missing_source[] =
        "(GSLTLanguageDefWireV1 \"Missing\" LNil LNil LNil)";
    static const char unknown_source[] =
        "(GSLTLanguageDefWireV1 \"Unknown\" LNil LNil LNil LNil Extra)";
    static const char syntax_source[] =
        "(GSLTLanguageDefWireV1 \"Broken\" LNil LNil LNil LNil";
    static const char valid_source[] =
        "(GSLTLanguageDefWireV1 \"Tiny\" LNil LNil LNil LNil)";
    static const char legacy_prime_source[] =
        "(LanguageDefV1 \"NotThisWire\" LNil LNil LNil LNil)";
    static const char control_source[] =
        "(GSLTLanguageDefWireV1 \"Bad\x01Name\" LNil LNil LNil LNil)";
    static const uint8_t invalid_utf8[] = {0xc0u, 0xafu};
    CettaOperationalLanguageDefV1 language;
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_OK;
    char error[512] = {0};

    cetta_op_lang_v1_init(&language);
    (void)expect(
        counts,
        !parse_text(&language, missing_source, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF,
        "missing field rejected");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, unknown_source, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF,
        "unknown field rejected");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, syntax_source, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "malformed source rejected before field decoding");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, valid_source, 1u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_GLL_RESOURCE_LIMIT,
        "GLL resource exhaustion remains distinct");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, valid_source, 500000u, 1u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_GLR_RESOURCE_LIMIT,
        "GLR resource exhaustion remains distinct");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, legacy_prime_source, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_MALFORMED_LANGUAGE_DEF,
        "Prime LanguageDefV1 is not confused with the GSLT wire");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, control_source, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "unclassified lexical scalar rejects through both parsers");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_op_lang_v1_parse_bytes(
            &language, invalid_utf8, sizeof(invalid_utf8),
            500000u, 1000000u, &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_INVALID_UTF8,
        "invalid UTF-8 is distinct from syntax and engine failure");
    cetta_op_lang_v1_free(&language);
}

static void unicode_and_disagreement_canary(TestCounts *counts) {
    static const char unicode_source[] =
        "(GSLTLanguageDefWireV1 \"\xce\x9b" "Arithmetic\" "
        "LNil LNil LNil LNil)";
    static const char other_source[] =
        "(GSLTLanguageDefWireV1 \"Other\" LNil LNil LNil LNil)";
    CettaOperationalLanguageDefV1 unicode_language;
    CettaOperationalLanguageDefV1 other_language;
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    cetta_op_lang_v1_init(&unicode_language);
    cetta_op_lang_v1_init(&other_language);
    (void)expect(
        counts,
        parse_text(&unicode_language, unicode_source,
                   500000u, 1000000u, &status, error, sizeof(error)) &&
            bytes_equal(unicode_language.name_bytes, unicode_language.name_len,
                        (const uint8_t *)"\xce\x9b" "Arithmetic", 12u),
        error[0] ? error : "Unicode language identity preserved");
    error[0] = '\0';
    (void)expect(
        counts,
        parse_text(&other_language, other_source,
                   500000u, 1000000u, &status, error, sizeof(error)),
        error[0] ? error : "parse independent disagreement canary");
    (void)expect(
        counts,
        !cetta_op_lang_v1_sexpr_equal(
            unicode_language.root, other_language.root) &&
            unicode_language.gll.derivation_fingerprint !=
                other_language.gll.derivation_fingerprint,
        "semantic disagreement cannot pass the dual-parser equality gate");
    cetta_op_lang_v1_free(&other_language);
    cetta_op_lang_v1_free(&unicode_language);
}

static void exact_physical_carrier_gate(TestCounts *counts) {
    static const char source[] =
        "(GSLTLanguageDefWireV1 \"Wire\\\"\\\\\\n\\t\\x00\xce\xbb\" "
        "LNil (LCons (Carrier word \"text ()\" 0 "
        "1234567890123456789012345678901234567890) LNil) "
        "LNil LNil)";
    static const uint8_t expected_name[] = {
        'W', 'i', 'r', 'e', '"', '\\', '\n', '\t', 0u, 0xceu, 0xbbu,
    };
    static const char bad_escape[] =
        "(GSLTLanguageDefWireV1 \"bad\\q\" LNil LNil LNil LNil)";
    static const char bad_control_alias[] =
        "(GSLTLanguageDefWireV1 \"bad\\r\" LNil LNil LNil LNil)";
    static const char bad_printable_hex[] =
        "(GSLTLanguageDefWireV1 \"bad\\x20\" LNil LNil LNil LNil)";
    static const char leading_zero[] =
        "(GSLTLanguageDefWireV1 \"bad\" LNil "
        "(LCons (N 00) LNil) LNil LNil)";
    static const char empty_application[] =
        "(GSLTLanguageDefWireV1 \"bad\" LNil "
        "(LCons () LNil) LNil LNil)";
    static const char string_head_application[] =
        "(GSLTLanguageDefWireV1 \"bad\" LNil "
        "(LCons (\"NotAHead\" x) LNil) LNil LNil)";
    static const char natural_head_application[] =
        "(GSLTLanguageDefWireV1 \"bad\" LNil "
        "(LCons (7 x) LNil) LNil LNil)";
    CettaOperationalLanguageDefV1 language;
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    const CettaOpLangV1SExpr *carrier;
    char error[512] = {0};

    cetta_op_lang_v1_init(&language);
    (void)expect(
        counts,
        parse_text(&language, source, 500000u, 1000000u,
                   &status, error, sizeof(error)),
        error[0] ? error : "parse exact physical carrier canary");
    carrier = cetta_op_lang_v1_field_entry(language.terms_field, 0u);
    (void)expect(
        counts,
        bytes_equal(language.name_bytes, language.name_len,
                    expected_name, sizeof(expected_name)) &&
            cetta_op_lang_v1_application_is(carrier, "Carrier", 4u) &&
            cetta_op_lang_v1_symbol_is(
                carrier->as.application.arguments[0], "word") &&
            cetta_op_lang_v1_string_is(
                carrier->as.application.arguments[1],
                (const uint8_t *)"text ()", 7u) &&
            carrier->as.application.arguments[2]->kind ==
                CETTA_OP_LANG_V1_SEXPR_NATURAL &&
            strcmp(carrier->as.application.arguments[2]->as.natural,
                   "0") == 0 &&
            carrier->as.application.arguments[3]->kind ==
                CETTA_OP_LANG_V1_SEXPR_NATURAL &&
            strcmp(carrier->as.application.arguments[3]->as.natural,
                   "1234567890123456789012345678901234567890") == 0,
        "symbols, byte strings, and unbounded naturals remain distinct");

    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, bad_escape, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "unknown string escape rejects");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, bad_control_alias, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "noncanonical control escape rejects");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, bad_printable_hex, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "noncanonical printable hex escape rejects");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, leading_zero, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "noncanonical natural rejects");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, empty_application, 500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "headless application rejects");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, string_head_application,
                    500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "string application head rejects");
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_text(&language, natural_head_application,
                    500000u, 1000000u,
                    &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED,
        "natural application head rejects");
    cetta_op_lang_v1_free(&language);
}

static void generic_document_gate(TestCounts *counts) {
    static const char extension_source[] =
        "(ExactIntegerTheoryV1 "
        "(LCons (ExactIntegerOperation exact-int:add) LNil))";
    static const char broken_source[] =
        "(ExactIntegerTheoryV1 (LCons (Broken LNil)";
    CettaOpLangV1Document document;
    CettaOpLangV1SExpr *saved_root;
    char saved_sha256[65];
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    char error[512] = {0};

    cetta_op_lang_v1_document_init(&document);
    (void)expect(
        counts,
        parse_document_text(&document, extension_source,
                            500000u, 1000000u,
                            &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_OK &&
            cetta_op_lang_v1_application_is(
                document.root, "ExactIntegerTheoryV1", 1u) &&
            document.gll.derivation_fingerprint ==
                document.glr.derivation_fingerprint,
        error[0] ? error :
            "generic document ingress accepts non-LanguageDef roots");
    saved_root = document.root;
    memcpy(saved_sha256, document.source_sha256, sizeof(saved_sha256));
    error[0] = '\0';
    (void)expect(
        counts,
        !parse_document_text(&document, broken_source,
                             500000u, 1000000u,
                             &status, error, sizeof(error)) &&
            status == CETTA_OP_LANG_V1_SYNTAX_REJECTED &&
            document.root == saved_root &&
            memcmp(document.source_sha256, saved_sha256,
                   sizeof(saved_sha256)) == 0,
        "generic document replacement is atomic on syntax failure");
    cetta_op_lang_v1_document_free(&document);
}

int main(void) {
    TestCounts counts = {0};

    canonical_file_gate(&counts);
    order_and_multiplicity_gate(&counts);
    malformed_and_resource_gates(&counts);
    unicode_and_disagreement_canary(&counts);
    exact_physical_carrier_gate(&counts);
    generic_document_gate(&counts);
    printf("operational LanguageDef native ingress: %u passed, %u failed\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
