#define _POSIX_C_SOURCE 200809L

#include "native/operational_language_def_v1.h"
#include "lib_parse_native_grammar.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A normal source read must restore build-time decisions, not prepare the
 * fixed reader grammar. Link wrappers make a regression fail every parse. */
static unsigned preparation_calls;
bool __wrap_cetta_lp_native_gll_prepare(CettaLpNativeGllPrepared *out,
    const CettaLpNativeGrammar *grammar, SymbolId start, char *error, size_t size) {
    (void)out; (void)grammar; (void)start; (void)error; (void)size;
    ++preparation_calls; return false;
}
bool __wrap_cetta_lp_native_glr_prepare(CettaLpNativeGlrPrepared *out,
    const CettaLpNativeGrammar *grammar, SymbolId start, char *error, size_t size) {
    (void)out; (void)grammar; (void)start; (void)error; (void)size;
    ++preparation_calls; return false;
}

/* Counts document parses that actually run: a cache hit runs none. */
static unsigned gll_parse_calls;
bool __real_cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
    const CettaLpNativeGllPrepared *prepared, const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar, uint32_t descriptor_limit, CettaLpNativeUtf8Forest *out,
    char *error_buf, size_t error_buf_size);
bool __wrap_cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
    const CettaLpNativeGllPrepared *prepared, const CettaLpNativeUtf8Lattice *lattice,
    uint32_t start_scalar, uint32_t descriptor_limit, CettaLpNativeUtf8Forest *out,
    char *error_buf, size_t error_buf_size) {
    ++gll_parse_calls;
    return __real_cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
        prepared, lattice, start_scalar, descriptor_limit, out, error_buf, error_buf_size);
}

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
        parse_text(&language, control_source, 500000u, 1000000u,
                   &status, error, sizeof(error)) &&
            bytes_equal(language.name_bytes, language.name_len,
                        (const uint8_t *)"Bad\x01Name", 8u),
        "literal control scalar is retained inside a string");
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

    const struct { const char *text; const char *value; } spellings[] = {
        {bad_escape, "bad\\q"}, {bad_control_alias, "bad\r"},
        {bad_printable_hex, "bad "},
        {"(GSLTLanguageDefWireV1 \"\\u{3bb}\\u03bb\" LNil LNil LNil LNil)", "λλ"},
        {"(GSLTLanguageDefWireV1 \"\\uD83D\\uDE00\" LNil LNil LNil LNil)", "😀"},
        {"(GSLTLanguageDefWireV1 \"\\uD800\\x4\" LNil LNil LNil LNil)", "\\uD800\\x4"}
    };
    for (size_t i = 0; i < sizeof spellings / sizeof spellings[0]; ++i) {
        error[0] = '\0';
        bool ok = parse_text(&language, spellings[i].text, 500000u, 1000000u,
                             &status, error, sizeof error);
        (void)expect(counts, ok && language.name_len == strlen(spellings[i].value) &&
            !memcmp(language.name_bytes, spellings[i].value, language.name_len),
            "module input spelling has its exact literal value");
    }
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

static unsigned cache_entries(const char *directory, char *first, size_t first_size) {
    DIR *dir = opendir(directory);
    struct dirent *entry;
    unsigned count = 0u;

    if (!dir)
        return 0u;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "op-lang-", 8u) != 0)
            continue;
        if (count == 0u && first)
            snprintf(first, first_size, "%s/%s", directory, entry->d_name);
        count++;
    }
    closedir(dir);
    return count;
}

static bool receipts_equal(const CettaOpLangV1ParserReceipt *left,
                           const CettaOpLangV1ParserReceipt *right) {
    return left->node_len == right->node_len &&
        left->choice_len == right->choice_len &&
        left->work_item_len == right->work_item_len &&
        left->graph_node_len == right->graph_node_len &&
        left->stack_node_len == right->stack_node_len &&
        left->source_pass_count == right->source_pass_count &&
        left->decoded_byte_len == right->decoded_byte_len &&
        left->source_decode_pass_count == right->source_decode_pass_count &&
        left->lexical_projection_pass_count == right->lexical_projection_pass_count &&
        left->derivation_fingerprint == right->derivation_fingerprint;
}

static bool documents_equal(const CettaOpLangV1Document *left,
                            const CettaOpLangV1Document *right) {
    return cetta_op_lang_v1_sexpr_equal(left->root, right->root) &&
        receipts_equal(&left->gll, &right->gll) &&
        receipts_equal(&left->glr, &right->glr) &&
        memcmp(left->source_sha256, right->source_sha256, 65u) == 0 &&
        memcmp(left->authority_sha256, right->authority_sha256, 65u) == 0;
}

/* A verified document is stored once and read back equal, spans and
 * receipts included, without a parse. A damaged entry is ignored and
 * replaced; other work limits keep their own entry; without
 * CETTA_CACHE_DIR nothing is read or written. */
static void document_cache_gate(TestCounts *counts) {
    static const char source[] =
        "(CacheProbeV1 \"text \\u{e9}\" 42 symbol "
        "(Nested (LCons (ExactIntegerOperation exact-int:add) LNil)))";
    const char *base = getenv("TMPDIR");
    char directory[512];
    char entry[1024] = {0};
    CettaOpLangV1Document fresh, cached, repaired, other;
    CettaOpLangV1Status status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    char error[512] = {0};
    unsigned before;
    bool parsed;

    snprintf(directory, sizeof(directory), "%s/cetta-op-lang-cache-XXXXXX",
             base && base[0] ? base : "/tmp");
    if (!mkdtemp(directory)) {
        (void)expect(counts, false, "document cache test directory");
        return;
    }
    cetta_op_lang_v1_document_init(&fresh);
    cetta_op_lang_v1_document_init(&cached);
    cetta_op_lang_v1_document_init(&repaired);
    cetta_op_lang_v1_document_init(&other);
    setenv("CETTA_CACHE_DIR", directory, 1);

    before = gll_parse_calls;
    parsed = parse_document_text(&fresh, source, 500000u, 1000000u,
                                 &status, error, sizeof(error));
    (void)expect(counts, parsed && gll_parse_calls == before + 1u &&
                     cache_entries(directory, entry, sizeof(entry)) == 1u,
                 error[0] ? error : "a verified document is parsed once and stored");

    before = gll_parse_calls;
    parsed = parse_document_text(&cached, source, 500000u, 1000000u,
                                 &status, error, sizeof(error));
    (void)expect(counts, parsed && status == CETTA_OP_LANG_V1_OK &&
                     gll_parse_calls == before && documents_equal(&fresh, &cached),
                 "a stored document returns the same tree, spans and receipts without a parse");

    {
        FILE *file = fopen(entry, "r+b");
        int byte;
        if (file && fseek(file, 120L, SEEK_SET) == 0 && (byte = fgetc(file)) != EOF &&
            fseek(file, 120L, SEEK_SET) == 0)
            (void)fputc(byte ^ 0x5a, file);
        if (file)
            fclose(file);
    }
    before = gll_parse_calls;
    parsed = parse_document_text(&repaired, source, 500000u, 1000000u,
                                 &status, error, sizeof(error));
    (void)expect(counts, parsed && gll_parse_calls == before + 1u &&
                     documents_equal(&fresh, &repaired),
                 "a damaged entry is ignored and the document parsed again");
    cetta_op_lang_v1_document_free(&repaired);
    cetta_op_lang_v1_document_init(&repaired);
    before = gll_parse_calls;
    parsed = parse_document_text(&repaired, source, 500000u, 1000000u,
                                 &status, error, sizeof(error));
    (void)expect(counts, parsed && gll_parse_calls == before &&
                     documents_equal(&fresh, &repaired),
                 "the damaged entry is replaced by a good one");

    before = gll_parse_calls;
    parsed = parse_document_text(&other, source, 500001u, 1000000u,
                                 &status, error, sizeof(error));
    (void)expect(counts, parsed && gll_parse_calls == before + 1u &&
                     cache_entries(directory, NULL, 0u) == 2u,
                 "other work limits read and store their own entry");

    unsetenv("CETTA_CACHE_DIR");
    cetta_op_lang_v1_document_free(&other);
    cetta_op_lang_v1_document_init(&other);
    before = gll_parse_calls;
    parsed = parse_document_text(&other, source, 500002u, 1000000u,
                                 &status, error, sizeof(error));
    (void)expect(counts, parsed && gll_parse_calls == before + 1u &&
                     cache_entries(directory, NULL, 0u) == 2u,
                 "without CETTA_CACHE_DIR nothing is read or stored");

    cetta_op_lang_v1_document_free(&fresh);
    cetta_op_lang_v1_document_free(&cached);
    cetta_op_lang_v1_document_free(&repaired);
    cetta_op_lang_v1_document_free(&other);
    {
        DIR *dir = opendir(directory);
        struct dirent *item;
        char path[1024];
        while (dir && (item = readdir(dir)) != NULL) {
            if (strncmp(item->d_name, "op-lang-", 8u) != 0 &&
                strncmp(item->d_name, ".op-lang-", 9u) != 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", directory, item->d_name);
            (void)unlink(path);
        }
        if (dir)
            closedir(dir);
        (void)rmdir(directory);
    }
}

int main(void) {
    TestCounts counts = {0};

    canonical_file_gate(&counts);
    order_and_multiplicity_gate(&counts);
    malformed_and_resource_gates(&counts);
    unicode_and_disagreement_canary(&counts);
    exact_physical_carrier_gate(&counts);
    generic_document_gate(&counts);
    document_cache_gate(&counts);
    (void)expect(&counts, preparation_calls == 0u,
        "fixed reader restores parser programs without grammar preparation");
    printf("operational LanguageDef native ingress: %u passed, %u failed\n",
           counts.passed, counts.failed);
    return counts.failed == 0u ? 0 : 1;
}
