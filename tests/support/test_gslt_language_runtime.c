#include "atom.h"
#include "gslt_language_runtime.h"
#include "he_compiled_reader.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"
#include "generated/zero_language_v1.generated.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERROR_CAP = 1024 };

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int parse_document(Arena *arena, const char *text, Atom ***forms) {
    return parse_metta_text(text, arena, forms);
}

static CettaGsltHornLimits limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 1000000u,
        .max_answers = 1000u,
        .max_depth = 10000u,
    };
}

static bool all_answers_equal(const CettaGsltLanguageResult *result,
                              Atom *expected) {
    for (size_t index = 0u; index < result->answer_count; index++)
        if (!atom_eq(result->answers[index], expected))
            return false;
    return true;
}

static void run_document(const CettaGsltLanguage *language,
                         Arena *source, Arena *answers,
                         const char *text, const char *expected_text,
                         size_t expected_count, const char *label) {
    Atom **forms = NULL;
    Atom **expected_forms = NULL;
    int form_count = parse_document(source, text, &forms);
    int expected_form_count = parse_document(
        answers, expected_text, &expected_forms);
    CettaGsltLanguageResult result;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 && expected_form_count == 1 &&
        cetta_gslt_language_execute_atoms(
            language, forms, (size_t)form_count, answers, limits(),
            &result, error, sizeof(error));
    CHECK(executed, label);
    if (executed) {
        CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED,
              "language execution completes semantically");
        CHECK(result.answer_count == expected_count,
              "language execution preserves the expected bag cardinality");
        CHECK(all_answers_equal(&result, expected_forms[0]),
              "language execution returns the expected payload bag");
        cetta_gslt_language_result_free(&result);
    } else if (error[0]) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    }
    free(expected_forms);
    free(forms);
}

static void run_compiled_document(
    const CettaGsltLanguage *language, HECompiledReaderV1 *reader,
    TermUniverse *universe, Arena *answers, const char *text,
    const char *expected_text, size_t expected_count, const char *label) {
    AtomId *ids = NULL;
    Atom **forms = NULL;
    Atom **expected_forms = NULL;
    HECompiledReaderV1Receipt receipt;
    char error[ERROR_CAP] = {0};
    int form_count = he_compiled_reader_v1_parse_text_ids(
        reader, text, universe, &ids, &receipt, error, sizeof(error));
    int expected_form_count = parse_document(
        answers, expected_text, &expected_forms);
    if (form_count > 0) {
        forms = malloc(sizeof(*forms) * (size_t)form_count);
        for (int index = 0; index < form_count; index++)
            forms[index] = term_universe_get_atom(universe, ids[index]);
    }
    CettaGsltLanguageResult result;
    bool executed = form_count >= 0 && expected_form_count == 1 &&
        (form_count == 0 || forms) &&
        cetta_gslt_language_execute_atoms(
            language, forms, (size_t)form_count, answers, limits(),
            &result, error, sizeof(error));
    CHECK(executed, label);
    if (executed) {
        CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED,
              "compiled-reader language execution completes semantically");
        CHECK(result.answer_count == expected_count,
              "compiled-reader boundary preserves bag cardinality");
        CHECK(all_answers_equal(&result, expected_forms[0]),
              "compiled-reader boundary preserves result payloads");
        cetta_gslt_language_result_free(&result);
    } else if (error[0]) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    }
    free(expected_forms);
    free(forms);
    free(ids);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s LANGUAGE_MANIFEST\n", argv[0]);
        return 2;
    }
    SymbolTable symbols;
    VarInternTable variable_names;
    Arena source;
    Arena compiled_source;
    Arena answers;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&source);
    arena_init(&compiled_source);
    arena_set_runtime_kind(
        &compiled_source, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&answers);

    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &compiled_source);
    HECompiledReaderV1 *reader = he_compiled_reader_v1_new();
    char reader_error[ERROR_CAP] = {0};
    CHECK(reader && he_compiled_reader_v1_prepare(
                        reader, reader_error, sizeof(reader_error)),
          "compiled syntax capability prepares for language execution");

    CettaGsltLanguage *language = NULL;
    char error[ERROR_CAP] = {0};
    CHECK(cetta_gslt_language_load_manifest(
              argv[1], &language, error, sizeof(error)),
          "first-class GSLT language manifest loads");
    if (!language) {
        fprintf(stderr, "manifest diagnostic: %s\n", error);
    } else {
        CHECK(strcmp(cetta_gslt_language_name(language), "zero") == 0,
              "language name comes from the manifest");
        CHECK(strcmp(cetta_gslt_language_syntax_backend(language),
                     "he-reader-direct-v1") == 0,
              "syntax capability comes from the manifest");
        CHECK(cetta_gslt_language_semantic_rule_count(language) == 36u,
              "semantic program composes core and public observation");

        run_document(
            language, &source, &answers,
            "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
            "duplicate source rules execute through the result bag");
        run_document(
            language, &source, &answers,
            "(= a b)\n(! unknown)\n", "unused", 0u,
            "unknown source form is inert");
        run_document(
            language, &source, &answers,
            "(= () empty-result)\n(! ())\n", "empty-result", 1u,
            "empty expression executes as ordinary data");
    }

    cetta_gslt_language_free(language);
    language = NULL;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_manifest(
              "tests/fixtures/absent-gslt-language-v1.metta",
              &language, error, sizeof(error)) && error[0] != '\0',
          "missing language manifest fails with a diagnostic");
    CettaGsltEmbeddedLanguageV1 invalid_descriptor =
        cetta_zero_language_v1;
    invalid_descriptor.entry_arity = UINT32_MAX;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "overflowing embedded entry arity is rejected");

    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_zero_language_v1, &language, error, sizeof(error)),
          "digest-checked embedded GSLT language loads");
    if (!language) {
        fprintf(stderr, "embedded descriptor diagnostic: %s\n", error);
    } else {
        run_document(
            language, &source, &answers,
            "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
            "embedded descriptor preserves the authored result bag");
        if (reader) {
            run_compiled_document(
                language, reader, &universe, &answers,
                "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
                "compiled reader composes with generated semantics");
        }
    }
    cetta_gslt_language_free(language);
    he_compiled_reader_v1_free(reader);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&compiled_source);
    arena_free(&source);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    if (failures != 0u) {
        fprintf(stderr, "GsltLanguageRuntimeSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(GsltLanguageRuntimeSummary checks=%u failures=0)\n", checks);
    return 0;
}
