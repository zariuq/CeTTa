#define _GNU_SOURCE

#include "native/language_def_parser_pack_v1.h"
#include "native/parser_pack_identity_wire_v1.h"
#include "native/json_cst_value_v1.h"
#include "native/json_runtime_v1.h"
#include "native/json_value_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "parser_pack_gll_v1.h"
#include "parser_pack_glr_v1.h"
#include "parser_pack_native_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

typedef struct {
    const uint8_t *bytes;
    size_t len;
    bool accepted;
    uint32_t expected_member_occurrences;
    const char *name;
} JsonCase;

static bool expect(TestCounts *counts, bool condition, const char *name) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

static bool write_packet_file(const char *environment_name,
                              const uint8_t *bytes, size_t length) {
    const char *path = getenv(environment_name);
    FILE *file;
    bool written;
    if (!path || path[0] == '\0')
        return true;
    file = fopen(path, "wb");
    if (!file)
        return false;
    written = fwrite(bytes, 1u, length, file) == length;
    if (fclose(file) != 0)
        written = false;
    return written;
}

static void maybe_emit_forest_packet(
    TestCounts *counts, const char *environment_name,
    const CettaLpNativeUtf8Forest *forest, const char *label) {
    const char *path = getenv(environment_name);
    char error[512] = {0};
    uint8_t *wire = NULL;
    size_t wire_size = 0u;
    size_t written = 0u;
    bool ok;
    if (!path || path[0] == '\0')
        return;
    ok = cetta_lp_native_utf8_forest_wire_size(
        forest, &wire_size, error, sizeof(error));
    if (ok)
        wire = (uint8_t *)malloc(wire_size ? wire_size : 1u);
    ok = ok && wire && cetta_lp_native_utf8_forest_wire_write(
        forest, wire, wire_size, &written, error, sizeof(error)) &&
        written == wire_size &&
        write_packet_file(environment_name, wire, wire_size);
    (void)expect(counts, ok, error[0] ? error : label);
    free(wire);
}

static bool text_is(const CettaLdTextV1 *text, const char *value) {
    size_t len = value ? strlen(value) : 0u;
    return text && value && (size_t)text->len == len &&
        (len == 0u ||
         (text->bytes && memcmp(text->bytes, value, len) == 0));
}

static char *render(Atom *term) {
    char *bytes = NULL;
    size_t len = 0u;
    FILE *stream = open_memstream(&bytes, &len);
    if (!stream)
        return NULL;
    atom_print(term, stream);
    if (fclose(stream) != 0) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

static bool semantic_results_equal(const PPNativeV1Result *left,
                                   const PPNativeV1Result *right) {
    uint32_t index;
    if (!left || !right || left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        char *left_text = render(left->semantic_results[index]);
        char *right_text = render(right->semantic_results[index]);
        bool equal = left_text && right_text &&
            strcmp(left_text, right_text) == 0;
        free(left_text);
        free(right_text);
        if (!equal) return false;
    }
    return true;
}

static uint32_t label_occurrences(Atom *term, const char *label) {
    uint32_t count = 0u;
    CettaExprLen index;
    if (!term || !label || term->kind != ATOM_EXPR)
        return 0u;
    if (term->expr.len >= 2u &&
        atom_is_symbol(term->expr.elems[0], "CstRuleV1") &&
        term->expr.elems[1] &&
        term->expr.elems[1]->kind == ATOM_GROUNDED &&
        term->expr.elems[1]->ground.gkind == GV_STRING &&
        term->expr.elems[1]->ground.sval &&
        strcmp(term->expr.elems[1]->ground.sval, label) == 0) {
        count++;
    }
    for (index = 0u; index < term->expr.len; index++)
        count += label_occurrences(term->expr.elems[index], label);
    return count;
}

static uint32_t result_label_occurrences(const PPNativeV1Result *result,
                                         const char *label) {
    uint32_t result_index;
    uint32_t count = 0u;
    if (!result) return 0u;
    for (result_index = 0u;
         result_index < result->semantic_result_len;
         result_index++) {
        count += label_occurrences(result->semantic_results[result_index],
                                   label);
    }
    return count;
}

static Atom *single_result_cst(PPNativeV1Result *result) {
    Atom *answer;
    if (!result || result->semantic_result_len != 1u)
        return NULL;
    answer = result->semantic_results[0];
    if (!answer || answer->kind != ATOM_EXPR || answer->expr.len != 3u ||
        !atom_is_symbol(answer->expr.elems[0], "result") ||
        !atom_is_symbol(answer->expr.elems[2], "nil")) {
        return NULL;
    }
    return answer->expr.elems[1];
}

static uint8_t *read_source(const char *path, size_t *len_out) {
    FILE *file;
    long length;
    uint8_t *bytes;
    if (!path || !len_out || !(file = fopen(path, "rb"))) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length ? (size_t)length : 1u);
    if (!bytes || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len_out = (size_t)length;
    return bytes;
}

typedef struct {
    const CettaJsonRuntimeV1 *runtime;
    const char *source;
    const char *expected;
    bool qualify_with_glr;
    bool passed;
    char error[512];
} JsonConcurrentCase;

static void *json_concurrent_parse(void *opaque) {
    JsonConcurrentCase *test = (JsonConcurrentCase *)opaque;
    CettaJsonRuntimeV1Limits limits;
    Arena arena;
    uint32_t iteration;

    test->passed = false;
    test->error[0] = '\0';
    arena_init(&arena);
    cetta_json_runtime_v1_default_limits(&limits);
    limits.qualify_with_glr = test->qualify_with_glr;
    for (iteration = 0u; iteration < 8u; iteration++) {
        ArenaMark mark = arena_mark(&arena);
        CettaJsonRuntimeV1Status status =
            CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
        Atom *value = NULL;
        char *actual;
        if (!cetta_json_runtime_v1_parse(
                test->runtime, &arena,
                (const uint8_t *)test->source, strlen(test->source),
                &limits, &value, &status,
                test->error, sizeof(test->error))) {
            arena_free(&arena);
            return NULL;
        }
        actual = render(value);
        if (status != CETTA_JSON_RUNTIME_V1_OK || !actual ||
            strcmp(actual, test->expected) != 0) {
            free(actual);
            (void)snprintf(
                test->error, sizeof(test->error),
                "concurrent JSON result disagreement for %s", test->source);
            arena_free(&arena);
            return NULL;
        }
        free(actual);
        arena_reset(&arena, mark);
    }
    test->passed = true;
    arena_free(&arena);
    return NULL;
}

static void concurrent_runtime_gate(TestCounts *counts,
                                    const CettaJsonRuntimeV1 *runtime) {
    JsonConcurrentCase cases[] = {
        {runtime, "null", "JsonNullV1", false, false, {0}},
        {runtime, "[true,false]",
         "(JsonArrayV1 ((JsonBoolV1 True) (JsonBoolV1 False)))",
         false, false, {0}},
        {runtime, "{\"x\":1,\"x\":2}",
         "(JsonObjectV1 ((JsonMemberV1 0 (JsonStringV1 ((cp 120))) "
         "(JsonNumberV1 \"1\")) (JsonMemberV1 1 "
         "(JsonStringV1 ((cp 120))) (JsonNumberV1 \"2\"))))",
         true, false, {0}},
        {runtime, "\"\xce\xbb\"", "(JsonStringV1 ((cp 955)))",
         true, false, {0}},
    };
    pthread_t threads[sizeof(cases) / sizeof(cases[0])];
    uint32_t created = 0u;
    uint32_t index;
    bool passed = true;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        if (pthread_create(
                &threads[index], NULL,
                json_concurrent_parse, &cases[index]) != 0) {
            passed = false;
            break;
        }
        created++;
    }
    for (index = 0u; index < created; index++) {
        if (pthread_join(threads[index], NULL) != 0 || !cases[index].passed) {
            passed = false;
            if (cases[index].error[0])
                fprintf(stderr, "FAIL: %s\n", cases[index].error);
        }
    }
    (void)expect(
        counts, passed,
        "one immutable prepared JSON runtime serves concurrent parses");
    (void)expect(
        counts, cetta_json_runtime_v1_table_build_count(runtime) == 1u,
        "concurrent parsing does not rebuild prepared tables");
}

static bool load_json_language(CettaOperationalLanguageDefV1 *wire,
                               CettaLanguageDefCoreV1 *language,
                               CettaLdParserPackV1Status *pack_status,
                               char *error, size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdCoreV1Status core_status = CETTA_LD_CORE_V1_BAD_ARGUMENT;
    if (!cetta_op_lang_v1_parse_file(
            wire, "langdef/json/rfc8259_syntax_v1.metta",
            12000000u, 24000000u, &wire_status, error, error_size)) {
        return false;
    }
    if (!cetta_language_def_core_v1_decode(
            language, wire, 500000u, &core_status, error, error_size)) {
        return false;
    }
    if (wire_status != CETTA_OP_LANG_V1_OK ||
        core_status != CETTA_LD_CORE_V1_OK) {
        if (pack_status)
            *pack_status = CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
        (void)snprintf(
            error, error_size, "JSON LanguageDef status: wire=%s core=%s",
            cetta_op_lang_v1_status_name(wire_status),
            cetta_ld_core_v1_status_name(core_status));
        return false;
    }
    return true;
}

static bool load_json_profile(CettaOpLangV1Document *document,
                              CettaLdParserProfileV1 *profile,
                              CettaLdParserPackV1Status *status,
                              char *error, size_t error_size) {
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    if (!cetta_op_lang_v1_parse_document_file(
            document, "langdef/json/rfc8259_parser_profile_v1.metta",
            4000000u, 8000000u, &wire_status, error, error_size)) {
        return false;
    }
    if (wire_status != CETTA_OP_LANG_V1_OK) {
        (void)snprintf(error, error_size, "parser-profile source status: %s",
                       cetta_op_lang_v1_status_name(wire_status));
        return false;
    }
    return cetta_ld_parser_profile_v1_decode(
        profile, document, 100000u, status, error, error_size);
}

static void compile_and_provenance_gate(TestCounts *counts,
                                        CettaOperationalLanguageDefV1 *wire,
                                        CettaLanguageDefCoreV1 *language,
                                        CettaLdParserProfileV1 *profile,
                                        CettaLdParserPackV1 *compiled) {
    CettaLdParserPackV1 duplicate;
    CettaLdParserPackV1Status status = CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    bool compiled_ok;
    char error[512] = {0};

    cetta_ld_parser_pack_v1_init(&duplicate);
    compiled_ok = cetta_language_def_parser_pack_v1_compile(
        compiled, language, wire->source_sha256, profile,
        1000000u, &status, error, sizeof(error));
    if (!compiled_ok && error[0] == '\0') {
        (void)snprintf(error, sizeof(error), "compile status: %s",
                       cetta_ld_parser_pack_v1_status_name(status));
    }
    (void)expect(
        counts,
        compiled_ok,
        error[0] ? error : "compile RFC 8259 LanguageDef to ParserPack");
    (void)expect(
        counts,
        status == CETTA_LD_PARSER_PACK_V1_OK &&
            compiled->start_state &&
            compiled->authored_rule_len == language->term_len &&
            compiled->lexical_rule_len == profile->state_len &&
            compiled->pack.production_len ==
                compiled->authored_rule_len + compiled->lexical_rule_len &&
            compiled->pack.class_clause_len >= profile->class_len &&
            strlen(compiled->pack.pack_digest) == 64u &&
            strlen(compiled->binding_sha256) == 64u,
        "compiled pack retains every authored and lexical rule");

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_language_def_parser_pack_v1_compile(
            &duplicate, language, wire->source_sha256, profile,
            1000000u, &status, error, sizeof(error)) &&
            strcmp(compiled->pack.pack_digest,
                   duplicate.pack.pack_digest) == 0 &&
            strcmp(compiled->binding_sha256,
                   duplicate.binding_sha256) == 0,
        error[0] ? error : "LanguageDef ParserPack compilation is deterministic");
    cetta_ld_parser_pack_v1_free(&duplicate);
}

static void identity_wire_gate(
    TestCounts *counts, CettaLdParserPackV1 *compiled) {
    const char *output_path =
        getenv("CETTA_JSON_PARSER_PACK_IDENTITY_WIRE_OUT");
    char error[512] = {0};
    size_t wire_size = 0u;
    size_t first_written = 0u;
    size_t second_written = 0u;
    uint8_t *first = NULL;
    uint8_t *second = NULL;
    bool sized;

    sized = cetta_ld_parser_pack_identity_wire_v1_size(
        compiled, &wire_size, error, sizeof(error));
    (void)expect(
        counts, sized && wire_size > 4u,
        error[0] ? error : "size ParserPack semantic identity wire");
    if (!sized)
        return;
    first = (uint8_t *)malloc(wire_size);
    second = (uint8_t *)malloc(wire_size);
    if (!expect(counts, first && second,
                "allocate ParserPack identity wire buffers")) {
        free(second);
        free(first);
        return;
    }
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_ld_parser_pack_identity_wire_v1_write(
            compiled, first, wire_size, &first_written,
            error, sizeof(error)) &&
            first_written == wire_size && memcmp(first, "PNI1", 4u) == 0,
        error[0] ? error : "write ParserPack semantic identity wire");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_ld_parser_pack_identity_wire_v1_write(
            compiled, second, wire_size, &second_written,
            error, sizeof(error)) &&
            second_written == wire_size &&
            memcmp(first, second, wire_size) == 0,
        error[0] ? error : "ParserPack identity wire is deterministic");
    memset(second, 0x5au, wire_size);
    second_written = wire_size;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_ld_parser_pack_identity_wire_v1_write(
            compiled, second, wire_size - 1u, &second_written,
            error, sizeof(error)) &&
            second_written == 0u && second[0] == 0x5au,
        "undersized ParserPack identity wire is fail-atomic");
    if (output_path && output_path[0] != '\0') {
        (void)expect(
            counts, write_packet_file(
                "CETTA_JSON_PARSER_PACK_IDENTITY_WIRE_OUT",
                first, wire_size),
            "emit ParserPack semantic identity qualification packet");
    }
    if (compiled->pack.production_len > 0u &&
        compiled->pack.productions[0].item_len > 0u) {
        PPABIV1Item *item = &compiled->pack.productions[0].items[0];
        uint32_t original = item->dense_id;
        item->dense_id = item->kind == PPABI_V1_ITEM_TERMINAL
            ? compiled->pack.terminal_len
            : compiled->pack.state_len;
        wire_size = 123u;
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_ld_parser_pack_identity_wire_v1_size(
                compiled, &wire_size, error, sizeof(error)) &&
                wire_size == 0u,
            "identity wire rejects a production-item identity mutation");
        item->dense_id = original;
    }
    free(second);
    free(first);
}

static bool parse_pair(const PPNativeV1Prepared *prepared,
                       const uint8_t *bytes, size_t len,
                       uint32_t recognizer_limit,
                       PPNativeV1Result *gll,
                       PPNativeV1Result *glr,
                       char *error, size_t error_size) {
    if (!ppgll_v1_prepared_parse(
            prepared, bytes, len, recognizer_limit,
            512u, 4096u, gll, error, error_size)) {
        return false;
    }
    error[0] = '\0';
    return ppglr_v1_prepared_parse(
        prepared, bytes, len, recognizer_limit,
        512u, 4096u, glr, error, error_size);
}

static void json_corpus_gate(TestCounts *counts,
                             const CettaLdParserPackV1 *compiled) {
    static const uint8_t invalid_utf8[] = {'"', 0xc0u, 0xafu, '"'};
    static const JsonCase cases[] = {
        {(const uint8_t *)"null", 4u, true, 0u, "top-level null"},
        {(const uint8_t *)" \t\r\ntrue ", 9u, true, 0u, "RFC whitespace"},
        {(const uint8_t *)"[true,false,null,-12.50e+2]", 27u,
         true, 0u, "nested array and number forms"},
        {(const uint8_t *)"{\"x\":1,\"x\":2}", 13u,
         true, 2u, "duplicate member occurrences"},
        {(const uint8_t *)"\"\\uD800\"", 8u,
         true, 0u, "syntactic escaped surrogate"},
        {(const uint8_t *)"\"\xce\xbb\"", 4u,
         true, 0u, "raw Unicode scalar"},
        {(const uint8_t *)"42", 2u, true, 0u, "top-level scalar number"},
        {(const uint8_t *)"", 0u, false, 0u, "empty input"},
        {(const uint8_t *)"01", 2u, false, 0u, "leading zero"},
        {(const uint8_t *)"[1,]", 4u, false, 0u, "trailing array comma"},
        {(const uint8_t *)"{\"x\" 1}", 7u, false, 0u, "missing member colon"},
        {(const uint8_t *)"\"\\x\"", 4u, false, 0u, "invalid escape"},
        {(const uint8_t *)"null true", 9u, false, 0u, "trailing JSON value"},
    };
    PPNativeV1Prepared prepared;
    uint32_t index;
    char error[512] = {0};

    ppnative_v1_prepared_init(&prepared);
    (void)expect(
        counts,
        ppnative_v1_prepare(&prepared, &compiled->pack,
                            compiled->start_state, error, sizeof(error)),
        error[0] ? error : "prepare JSON ParserPack once");
    (void)expect(
        counts, ppnative_v1_prepared_table_build_count(&prepared) == 1u,
        "one preparation builds shared GLL and GLR tables once");

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PPNativeV1Result gll;
        PPNativeV1Result glr;
        char label[256];
        bool ran;
        ppnative_v1_result_init(&gll);
        ppnative_v1_result_init(&glr);
        error[0] = '\0';
        ran = parse_pair(&prepared, cases[index].bytes, cases[index].len,
                         2000000u, &gll, &glr, error, sizeof(error));
        (void)snprintf(label, sizeof(label),
                       "dual native parsers execute %s", cases[index].name);
        (void)expect(counts, ran, error[0] ? error : label);
        (void)snprintf(label, sizeof(label),
                       "GLL/GLR agree for %s", cases[index].name);
        (void)expect(
            counts,
            ran && semantic_results_equal(&gll, &glr) &&
                gll.outcome == PPNATIVE_V1_COMPLETED &&
                gll.accepted == cases[index].accepted,
            label);
        if (ran && cases[index].accepted) {
            (void)snprintf(label, sizeof(label),
                           "one semantic JSON CST for %s", cases[index].name);
            (void)expect(counts, gll.semantic_result_len == 1u, label);
        }
        if (ran && cases[index].expected_member_occurrences > 0u) {
            (void)expect(
                counts,
                result_label_occurrences(&gll, "json:member") ==
                    cases[index].expected_member_occurrences,
                "duplicate JSON members remain distinct CST occurrences");
        }
        if (ran && index == 3u) {
            maybe_emit_forest_packet(
                counts, "CETTA_JSON_GLL_FOREST_WIRE_OUT", &gll.forest,
                "emit actual JSON GLL forest qualification packet");
            maybe_emit_forest_packet(
                counts, "CETTA_JSON_GLR_FOREST_WIRE_OUT", &glr.forest,
                "emit actual JSON GLR forest qualification packet");
        }
        ppnative_v1_result_free(&gll);
        ppnative_v1_result_free(&glr);
    }

    {
        PPNativeV1Result gll;
        PPNativeV1Result glr;
        ppnative_v1_result_init(&gll);
        ppnative_v1_result_init(&glr);
        error[0] = '\0';
        (void)expect(
            counts,
            !ppgll_v1_prepared_parse(
                &prepared, invalid_utf8, sizeof(invalid_utf8),
                1000u, 64u, 64u, &gll, error, sizeof(error)) &&
                strstr(error, "UTF-8") != NULL,
            "GLL rejects invalid UTF-8 before syntax recognition");
        error[0] = '\0';
        (void)expect(
            counts,
            !ppglr_v1_prepared_parse(
                &prepared, invalid_utf8, sizeof(invalid_utf8),
                1000u, 64u, 64u, &glr, error, sizeof(error)) &&
                strstr(error, "UTF-8") != NULL,
            "GLR rejects invalid UTF-8 before syntax recognition");
        ppnative_v1_result_free(&gll);
        ppnative_v1_result_free(&glr);
    }

    {
        static const uint8_t input[] = "[1,2,3,4]";
        PPNativeV1Result gll;
        PPNativeV1Result glr;
        bool ran;
        ppnative_v1_result_init(&gll);
        ppnative_v1_result_init(&glr);
        error[0] = '\0';
        ran = parse_pair(&prepared, input, sizeof(input) - 1u,
                         1u, &gll, &glr, error, sizeof(error));
        (void)expect(
            counts,
            ran && gll.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT &&
                glr.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT &&
                !gll.accepted && !glr.accepted,
            "GLL and GLR expose recognizer resource exhaustion");
        ppnative_v1_result_free(&gll);
        ppnative_v1_result_free(&glr);
    }

    (void)expect(
        counts, ppnative_v1_prepared_table_build_count(&prepared) == 1u,
        "prepared JSON parsing does not rebuild native tables per source");
    ppnative_v1_prepared_free(&prepared);
}

static void json_semantic_elaboration_gate(
    TestCounts *counts, const CettaLdParserPackV1 *compiled) {
    typedef struct {
        const char *source;
        const char *expected;
        const char *name;
    } ValueCase;
    static const ValueCase cases[] = {
        {"null", "JsonNullV1", "canonical null"},
        {"true", "(JsonBoolV1 True)", "canonical Boolean"},
        {"-12.50e+2", "(JsonNumberV1 \"-12.50e+2\")",
         "exact number lexeme"},
        {"\"\\u0000\"", "(JsonStringV1 ((cp 0)))",
         "embedded NUL scalar"},
        {"\"\\uD834\\uDD1E\"", "(JsonStringV1 ((cp 119070)))",
         "decoded surrogate pair"},
        {"[false,2]",
         "(JsonArrayV1 ((JsonBoolV1 False) (JsonNumberV1 \"2\")))",
         "ordered array values"},
    };
    PPNativeV1Prepared prepared;
    Arena values;
    uint32_t index;
    char error[512] = {0};

    ppnative_v1_prepared_init(&prepared);
    arena_init(&values);
    (void)expect(
        counts,
        ppnative_v1_prepare(&prepared, &compiled->pack,
                            compiled->start_state, error, sizeof(error)),
        error[0] ? error : "prepare JSON semantic elaboration parser");
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        PPNativeV1Result result;
        CettaJsonCstValueV1Status status =
            CETTA_JSON_CST_VALUE_V1_BAD_ARGUMENT;
        Atom *value = NULL;
        char *actual = NULL;
        char label[256];
        ppnative_v1_result_init(&result);
        error[0] = '\0';
        (void)expect(
            counts,
            ppgll_v1_prepared_parse(
                &prepared, (const uint8_t *)cases[index].source,
                strlen(cases[index].source), 2000000u, 512u, 4096u,
                &result, error, sizeof(error)) && result.accepted,
            error[0] ? error : cases[index].name);
        error[0] = '\0';
        (void)expect(
            counts,
            cetta_json_cst_value_v1_elaborate(
                &values, single_result_cst(&result),
                100000u, 1024u, &value, &status,
                error, sizeof(error)),
            error[0] ? error : cases[index].name);
        actual = value ? render(value) : NULL;
        (void)snprintf(label, sizeof(label),
                       "semantic elaboration preserves %s", cases[index].name);
        if (!actual || strcmp(actual, cases[index].expected) != 0) {
            fprintf(stderr, "semantic expected: %s\nsemantic actual:   %s\n",
                    cases[index].expected, actual ? actual : "<null>");
        }
        (void)expect(counts,
                     status == CETTA_JSON_CST_VALUE_V1_OK && actual &&
                         strcmp(actual, cases[index].expected) == 0,
                     label);
        free(actual);
        ppnative_v1_result_free(&result);
    }

    {
        static const char duplicate[] = "{\"x\":1,\"x\":2}";
        PPNativeV1Result result;
        CettaJsonCstValueV1Status status;
        Atom *value = NULL;
        Atom *members = NULL;
        ppnative_v1_result_init(&result);
        error[0] = '\0';
        (void)expect(
            counts,
            ppgll_v1_prepared_parse(
                &prepared, (const uint8_t *)duplicate,
                sizeof(duplicate) - 1u, 2000000u, 512u, 4096u,
                &result, error, sizeof(error)) && result.accepted &&
            cetta_json_cst_value_v1_elaborate(
                &values, single_result_cst(&result),
                100000u, 1024u, &value, &status,
                error, sizeof(error)),
            error[0] ? error : "elaborate duplicate JSON object");
        if (value && value->kind == ATOM_EXPR && value->expr.len == 2u &&
            atom_is_symbol(value->expr.elems[0], "JsonObjectV1")) {
            members = value->expr.elems[1];
        }
        (void)expect(
            counts,
            members && members->kind == ATOM_EXPR &&
                members->expr.len == 2u &&
                members->expr.elems[0]->kind == ATOM_EXPR &&
                members->expr.elems[0]->expr.len == 4u &&
                atom_is_symbol(members->expr.elems[0]->expr.elems[0],
                               "JsonMemberV1") &&
                members->expr.elems[0]->expr.elems[1]->kind ==
                    ATOM_GROUNDED &&
                members->expr.elems[0]->expr.elems[1]->ground.gkind ==
                    GV_INT &&
                members->expr.elems[0]->expr.elems[1]->ground.ival == 0 &&
                members->expr.elems[1]->expr.elems[1]->ground.ival == 1,
            "semantic JSON object retains ordered duplicate occurrence IDs");
        ppnative_v1_result_free(&result);
    }

    {
        static const char lone_surrogate[] = "\"\\uD800\"";
        PPNativeV1Result result;
        CettaJsonCstValueV1Status status = CETTA_JSON_CST_VALUE_V1_OK;
        Atom *sentinel = atom_symbol(&values, "stable-value");
        Atom *value = sentinel;
        ArenaMark mark;
        ppnative_v1_result_init(&result);
        error[0] = '\0';
        (void)expect(
            counts,
            ppgll_v1_prepared_parse(
                &prepared, (const uint8_t *)lone_surrogate,
                sizeof(lone_surrogate) - 1u,
                2000000u, 512u, 4096u, &result,
                error, sizeof(error)) && result.accepted,
            error[0] ? error : "syntax admits lone escaped surrogate");
        mark = arena_mark(&values);
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_json_cst_value_v1_elaborate(
                &values, single_result_cst(&result),
                100000u, 1024u, &value, &status,
                error, sizeof(error)) &&
                status ==
                    CETTA_JSON_CST_VALUE_V1_INVALID_UNICODE_ESCAPE &&
                value == sentinel && values.live_bytes == mark.live_bytes,
            "Unicode semantic profile rejects lone surrogate atomically");
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_json_cst_value_v1_elaborate(
                &values, single_result_cst(&result),
                1u, 1024u, &value, &status,
                error, sizeof(error)) &&
                status == CETTA_JSON_CST_VALUE_V1_RESOURCE_LIMIT &&
                value == sentinel,
            "JSON semantic resource exhaustion is distinct and atomic");
        ppnative_v1_result_free(&result);
    }

    arena_free(&values);
    ppnative_v1_prepared_free(&prepared);
}

static void fail_closed_gate(TestCounts *counts,
                             CettaOperationalLanguageDefV1 *wire,
                             CettaLanguageDefCoreV1 *language,
                             CettaLdParserProfileV1 *profile,
                             CettaLdParserPackV1 *compiled) {
    static const char malformed_profile[] =
        "(GSLTParserProfileLayerV1 \"Mutant\" \"JsonText\" "
        "(LCons (LexicalClassPoints \"Only\" (LCons 65 LNil)) LNil) "
        "(LCons (LexicalState \"JsonDigit\" \"Missing\" \"bad\") LNil))";
    CettaOpLangV1Document document;
    CettaOpLangV1Status wire_status = CETTA_OP_LANG_V1_INTERNAL_FAILURE;
    CettaLdParserPackV1Status status = CETTA_LD_PARSER_PACK_V1_OK;
    char original_binding[65];
    uint32_t original_rewrite_len = language->rewrite_len;
    char error[512] = {0};

    (void)memcpy(original_binding, compiled->binding_sha256,
                 sizeof(original_binding));
    cetta_op_lang_v1_document_init(&document);
    (void)expect(
        counts,
        cetta_op_lang_v1_parse_document_bytes(
            &document, (const uint8_t *)malformed_profile,
            sizeof(malformed_profile) - 1u, 1000000u, 2000000u,
            &wire_status, error, sizeof(error)),
        error[0] ? error : "parse malformed profile negative control");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_ld_parser_profile_v1_decode(
            profile, &document, 10000u, &status,
            error, sizeof(error)) &&
            status == CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE &&
            text_is(&profile->name, "RFC8259JsonParserV1"),
        "unknown lexical-class reference rejects atomically");
    cetta_op_lang_v1_document_free(&document);

    language->rewrite_len = 1u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_parser_pack_v1_compile(
            compiled, language, wire->source_sha256, profile,
            1000000u, &status, error, sizeof(error)) &&
            status == CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT &&
            strcmp(compiled->binding_sha256, original_binding) == 0,
        "semantic LanguageDef relations never fall through to parser lowering");
    language->rewrite_len = original_rewrite_len;

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_language_def_parser_pack_v1_compile(
            compiled, language, wire->source_sha256, profile,
            1u, &status, error, sizeof(error)) &&
            status == CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT &&
            strcmp(compiled->binding_sha256, original_binding) == 0,
        "compiler resource exhaustion is distinct and atomic");
}

static void reusable_runtime_gate(TestCounts *counts) {
    uint8_t *language_source = NULL;
    uint8_t *profile_source = NULL;
    size_t language_len = 0u;
    size_t profile_len = 0u;
    CettaJsonRuntimeV1 *runtime = NULL;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    Arena arena;
    Atom *value = NULL;
    Atom *sentinel;
    char *actual = NULL;
    Atom *legacy = NULL;
    CettaJsonValueV1Status value_status = CETTA_JSON_VALUE_V1_BAD_ARGUMENT;
    uint8_t *json_bytes = NULL;
    size_t json_len = 0u;
    char error[512] = {0};

    language_source = read_source(
        "langdef/json/rfc8259_syntax_v1.metta", &language_len);
    profile_source = read_source(
        "langdef/json/rfc8259_parser_profile_v1.metta", &profile_len);
    (void)expect(counts, language_source && profile_source,
                 "read authored JSON sources for reusable runtime");
    if (!language_source || !profile_source) goto done;
    runtime = cetta_json_runtime_v1_new(
        language_source, language_len, profile_source, profile_len,
        error, sizeof(error));
    (void)expect(counts, runtime != NULL,
                 error[0] ? error : "prepare reusable JSON runtime");
    if (!runtime) goto done;
    (void)expect(
        counts,
        cetta_json_runtime_v1_table_build_count(runtime) == 1u &&
            strlen(cetta_json_runtime_v1_language_digest(runtime)) == 64u &&
            strlen(cetta_json_runtime_v1_profile_digest(runtime)) == 64u &&
            strlen(cetta_json_runtime_v1_binding_digest(runtime)) == 64u &&
            strlen(cetta_json_runtime_v1_compiler_contract_digest(runtime)) ==
                64u &&
            strlen(cetta_json_runtime_v1_environment_contract_digest(runtime)) ==
                64u &&
            strlen(cetta_json_runtime_v1_parser_pack_digest(runtime)) == 64u,
        "runtime retains prepared tables and semantic build identities");

    arena_init(&arena);
    cetta_json_runtime_v1_default_limits(&limits);
    limits.qualify_with_glr = true;
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_json_runtime_v1_parse(
            runtime, &arena,
            (const uint8_t *)"{\"x\":1,\"x\":2}", 13u,
            &limits, &value, &status, error, sizeof(error)),
        error[0] ? error : "runtime parses through qualified prepared backends");
    actual = value ? render(value) : NULL;
    (void)expect(
        counts,
        status == CETTA_JSON_RUNTIME_V1_OK && actual &&
            strstr(actual, "JsonObjectV1") &&
            strstr(actual, "JsonMemberV1 0") &&
            strstr(actual, "JsonMemberV1 1"),
        "runtime exposes ordered duplicate-member occurrences");
    free(actual);
    actual = NULL;

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_json_value_v1_to_legacy(
            &arena, value, 100000u, 1024u, &legacy,
            &value_status, error, sizeof(error)),
        error[0] ? error : "project canonical JSON through legacy codec");
    actual = legacy ? render(legacy) : NULL;
    (void)expect(
        counts,
        actual && strcmp(
            actual,
            "(JsonObject ((JsonPair \"x\" (JsonNumber \"1\")) "
            "(JsonPair \"x\" (JsonNumber \"2\"))))") == 0,
        "legacy projection retains member order and duplicates");
    free(actual);
    actual = NULL;
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_json_value_v1_stringify(
            runtime, legacy, true, 2000000u, 1024u, 1024u,
            &json_bytes, &json_len, &value_status,
            error, sizeof(error)) && json_len == 13u &&
            memcmp(json_bytes, "{\"x\":1,\"x\":2}", 13u) == 0,
        error[0] ? error :
            "legacy JSON stringification round-trips through grammar");
    free(json_bytes);
    json_bytes = NULL;

    {
        Atom *bad_items[2] = {
            atom_symbol(&arena, "JsonNumber"),
            atom_string(&arena, "01"),
        };
        Atom *bad_number = atom_expr(&arena, bad_items, 2u);
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_json_value_v1_stringify(
                runtime, bad_number, true, 100000u, 128u, 64u,
                &json_bytes, &json_len, &value_status,
                error, sizeof(error)) &&
                value_status == CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT,
            "legacy stringifier rejects number text outside authored grammar");
    }

    value = NULL;
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_json_runtime_v1_parse(
            runtime, &arena, (const uint8_t *)"\"\\u0000\"", 8u,
            &limits, &value, &status, error, sizeof(error)),
        error[0] ? error : "parse canonical embedded-NUL string");
    legacy = NULL;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_json_value_v1_to_legacy(
            &arena, value, 100000u, 128u, &legacy,
            &value_status, error, sizeof(error)) &&
            value_status ==
                CETTA_JSON_VALUE_V1_UNREPRESENTABLE_LEGACY_STRING,
        "legacy projection rejects unrepresentable U+0000 without loss");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_json_value_v1_stringify(
            runtime, value, false, 100000u, 128u, 64u,
            &json_bytes, &json_len, &value_status,
            error, sizeof(error)) && json_len == 8u &&
            memcmp(json_bytes, "\"\\u0000\"", 8u) == 0,
        error[0] ? error : "canonical stringification retains embedded NUL");
    free(json_bytes);
    json_bytes = NULL;

    sentinel = atom_symbol(&arena, "stable-runtime-output");
    value = sentinel;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_json_runtime_v1_parse(
            runtime, &arena, (const uint8_t *)"01", 2u,
            &limits, &value, &status, error, sizeof(error)) &&
            status == CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED &&
            value == sentinel,
        "runtime rejects invalid JSON without replacing output");
    (void)expect(
        counts, cetta_json_runtime_v1_table_build_count(runtime) == 1u,
        "runtime never rebuilds parser tables per JSON value");
    concurrent_runtime_gate(counts, runtime);
    arena_free(&arena);

done:
    free(json_bytes);
    cetta_json_runtime_v1_free(runtime);
    free(profile_source);
    free(language_source);
}

int main(void) {
    SymbolTable symbols;
    CettaOperationalLanguageDefV1 wire;
    CettaLanguageDefCoreV1 language;
    CettaOpLangV1Document profile_document;
    CettaLdParserProfileV1 profile;
    CettaLdParserPackV1 compiled;
    CettaLdParserPackV1Status status = CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    TestCounts counts = {0};
    char error[512] = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    cetta_op_lang_v1_init(&wire);
    cetta_language_def_core_v1_init(&language);
    cetta_op_lang_v1_document_init(&profile_document);
    cetta_ld_parser_profile_v1_init(&profile);
    cetta_ld_parser_pack_v1_init(&compiled);

    (void)expect(
        &counts,
        load_json_language(&wire, &language, &status,
                           error, sizeof(error)),
        error[0] ? error : "load authored RFC 8259 LanguageDef");
    error[0] = '\0';
    (void)expect(
        &counts,
        load_json_profile(&profile_document, &profile, &status,
                          error, sizeof(error)),
        error[0] ? error : "load authored RFC 8259 parser profile");
    (void)expect(
        &counts,
        text_is(&language.name, "RFC8259JsonSyntaxV1") &&
            text_is(&profile.name, "RFC8259JsonParserV1") &&
            text_is(&profile.start_sort, "JsonText"),
        "JSON syntax and lexical profile remain separate authored layers");

    compile_and_provenance_gate(
        &counts, &wire, &language, &profile, &compiled);
    if (compiled.start_state)
        identity_wire_gate(&counts, &compiled);
    if (compiled.start_state)
        json_corpus_gate(&counts, &compiled);
    else
        (void)expect(&counts, false,
                     "compiled JSON ParserPack has a start state");
    if (compiled.start_state)
        json_semantic_elaboration_gate(&counts, &compiled);
    fail_closed_gate(
        &counts, &wire, &language, &profile, &compiled);
    reusable_runtime_gate(&counts);

    printf("(LanguageDefParserPackV1Summary %u %u)\n",
           counts.passed, counts.failed);
    cetta_ld_parser_pack_v1_free(&compiled);
    cetta_ld_parser_profile_v1_free(&profile);
    cetta_op_lang_v1_document_free(&profile_document);
    cetta_language_def_core_v1_free(&language);
    cetta_op_lang_v1_free(&wire);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
