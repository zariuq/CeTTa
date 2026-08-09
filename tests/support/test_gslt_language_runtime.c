#include "atom.h"
#include "gslt_language_runtime.h"
#include "he_compiled_reader.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"
#include "generated/subzero_language_v1.generated.h"
#include "generated/zero_language_v1.generated.h"
#include "generated/zero_exp_language_v1.generated.h"
#include "tests/generated/gslt_compiled_canary_v1.generated.h"
#include "tests/generated/gslt_pipeline_canary_v1.generated.h"
#include "tests/generated/metta_zero_ground_library_v1.generated.h"

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

static uint8_t *find_bytes(uint8_t *haystack, size_t haystack_length,
                           const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0u || needle_length > haystack_length)
        return NULL;
    for (size_t offset = 0u;
         offset <= haystack_length - needle_length; offset++)
        if (memcmp(haystack + offset, needle, needle_length) == 0)
            return haystack + offset;
    return NULL;
}

static bool all_answers_equal(const CettaGsltLanguageResult *result,
                              Atom *expected) {
    for (size_t index = 0u; index < result->answer_count; index++)
        if (!atom_eq(result->answers[index], expected))
            return false;
    return true;
}

static bool evidence_path(const Atom *evidence, const char *outer,
                          const char *inner) {
    if (!evidence || evidence->kind != ATOM_EXPR ||
        evidence->expr.len == 0u ||
        evidence->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(evidence->expr.elems[0]), outer) != 0)
        return false;
    if (!inner)
        return true;
    if (evidence->expr.len < 2u) {
        return false;
    }
    const Atom *nested = evidence->expr.elems[1];
    return nested->kind == ATOM_EXPR && nested->expr.len > 0u &&
        nested->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(nested->expr.elems[0]), inner) == 0;
}

static void run_document_evidence_realization(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers, const char *text,
    const char *outer, const char *inner, const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult result;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, limits(), &result, error, sizeof(error));
    CHECK(executed && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 1u && result.evidence &&
              evidence_path(result.evidence[0], outer, inner),
          label);
    if (executed)
        cetta_gslt_language_result_free(&result);
    else if (error[0])
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    free(forms);
}

static void run_document_realization(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
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
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, limits(), &result, error, sizeof(error));
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

static void run_document(const CettaGsltLanguage *language,
                         Arena *source, Arena *answers,
                         const char *text, const char *expected_text,
                         size_t expected_count, const char *label) {
    run_document_realization(
        language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        source, answers, text, expected_text, expected_count, label);
}

static void run_resource_limit(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers, const char *text,
    CettaGsltHornLimits bounded,
    CettaGsltHornOutcome expected, const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult result;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, bounded, &result, error, sizeof(error));
    CHECK(executed, label);
    if (executed) {
        CHECK(result.outcome == expected,
              "generated realization reports the exact resource boundary");
        CHECK(result.answer_count == 0u,
              "incomplete generated execution publishes no partial answers");
        cetta_gslt_language_result_free(&result);
    }
    free(forms);
}

static void run_completion_boundary(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers, const char *text, const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult complete;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, limits(), &complete, error, sizeof(error));
    CHECK(executed && complete.outcome == CETTA_GSLT_HORN_COMPLETED &&
              complete.answer_count == 1u && complete.rule_attempts > 1u,
          label);
    if (executed && complete.outcome == CETTA_GSLT_HORN_COMPLETED &&
        complete.rule_attempts > 1u) {
        uint64_t exact_attempts = complete.rule_attempts;
        cetta_gslt_language_result_free(&complete);
        CettaGsltLanguageResult incomplete;
        memset(error, 0, sizeof(error));
        executed = cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers,
            (CettaGsltHornLimits){
                .max_rule_attempts = exact_attempts - 1u,
                .max_answers = 1000u,
                .max_depth = 10000u,
            },
            &incomplete, error, sizeof(error));
        CHECK(executed && incomplete.outcome == CETTA_GSLT_HORN_RULE_LIMIT,
              "pipeline reports an open frontier immediately before completion");
        if (executed) {
            CHECK(incomplete.answer_count == 0u,
                  "open pipeline frontier cannot trigger inert observation");
            cetta_gslt_language_result_free(&incomplete);
        }
    } else if (executed) {
        cetta_gslt_language_result_free(&complete);
    }
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
        CHECK(strcmp(cetta_gslt_language_name(language), "subzero") == 0,
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
        cetta_subzero_language_v1;
    invalid_descriptor.entry_arity = UINT32_MAX;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "overflowing embedded entry arity is rejected");

    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.name = "not-the-authored-name";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "differs from its authored manifest") != NULL,
          "descriptor fields cannot drift from the authored manifest");

    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.manifest.sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "manifest digest") != NULL,
          "tampered embedded manifest authority is rejected");

    CettaGsltRequestPipelineV1 drifted_pipeline =
        *cetta_zero_language_v1.request_pipeline;
    drifted_pipeline.classify_relation = "zero-produce";
    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.request_pipeline = &drifted_pipeline;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "differs from its authored manifest") != NULL,
          "pipeline selection cannot drift from the authored manifest");

    CettaGsltEmbeddedSourceV1 reordered_sources[3];
    memcpy(reordered_sources, cetta_zero_language_v1.semantic_sources,
           sizeof(reordered_sources));
    CettaGsltEmbeddedSourceV1 first_source = reordered_sources[0];
    reordered_sources[0] = reordered_sources[1];
    reordered_sources[1] = first_source;
    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.semantic_sources = reordered_sources;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "source order") != NULL,
          "semantic-source composition order remains manifest-authored");

    invalid_descriptor = cetta_zero_exp_language_v1;
    invalid_descriptor.profile_name = NULL;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "differs from its authored manifest") != NULL,
          "generated profile identity cannot be erased from its descriptor");

    invalid_descriptor = cetta_zero_exp_language_v1;
    invalid_descriptor.profile_name = "absent";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "selected profile") != NULL,
          "descriptor cannot select a profile absent from the manifest");

    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.compiled_plan.sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "digest") != NULL,
          "tampered compiled-plan authority is rejected");

    size_t malformed_length = cetta_subzero_language_v1.compiled_plan.length;
    uint8_t *malformed_plan = malloc(malformed_length);
    char malformed_digest[65];
    memcpy(malformed_plan, cetta_subzero_language_v1.compiled_plan.bytes,
           malformed_length);
    malformed_plan[20] = 0xffu;
    cetta_native_sha256_hex(
        malformed_plan, malformed_length, malformed_digest);
    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.compiled_plan.bytes = malformed_plan;
    invalid_descriptor.compiled_plan.sha256 = malformed_digest;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded_for_realization(
              &invalid_descriptor,
              CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &language, error, sizeof(error)) &&
              strstr(error, "node table") != NULL,
          "digest-valid malformed compiled plan fails structural admission");
    free(malformed_plan);

    size_t divergent_length = cetta_subzero_language_v1.compiled_plan.length;
    uint8_t *divergent_plan = malloc(divergent_length);
    char divergent_digest[65];
    memcpy(divergent_plan, cetta_subzero_language_v1.compiled_plan.bytes,
           divergent_length);
    uint8_t *changed_head = find_bytes(
        divergent_plan, divergent_length, "subzero-step");
    CHECK(changed_head != NULL,
          "semantic mutation locates a compiled relational head");
    if (changed_head) {
        *changed_head = (uint8_t)'x';
        cetta_native_sha256_hex(
            divergent_plan, divergent_length, divergent_digest);
        invalid_descriptor = cetta_subzero_language_v1;
        invalid_descriptor.compiled_plan.bytes = divergent_plan;
        invalid_descriptor.compiled_plan.sha256 = divergent_digest;
        memset(error, 0, sizeof(error));
        CHECK(!cetta_gslt_language_load_embedded_for_realization(
                  &invalid_descriptor,
                  CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
                  &language, error, sizeof(error)) &&
                  strstr(error, "differs from admitted source") != NULL,
              "digest-valid same-count semantic plan drift is rejected");
    }
    free(divergent_plan);

    invalid_descriptor = cetta_subzero_language_v1;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded_for_realization(
              &invalid_descriptor, (CettaGsltRealization)99,
              &language, error, sizeof(error)) && error[0] != '\0',
          "unknown generated realization fails at admission");

    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.entry_relation = "conflicting-entry";
    invalid_descriptor.entry_arity = 1u;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "descriptor cannot mix a single entry with a request pipeline");

    CettaGsltRequestPipelineV1 incomplete_pipeline =
        *cetta_zero_language_v1.request_pipeline;
    incomplete_pipeline.observe_relation = NULL;
    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.request_pipeline = &incomplete_pipeline;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "incomplete request-pipeline ABI fails at admission");

    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_subzero_language_v1, &language, error, sizeof(error)),
          "digest-checked embedded GSLT language loads");
    if (!language) {
        fprintf(stderr, "embedded descriptor diagnostic: %s\n", error);
    } else {
        for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
             raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
            CettaGsltRealization realization = (CettaGsltRealization)raw;
            CHECK(cetta_gslt_realization_name(realization) != NULL,
                  "generated realization has a stable public name");
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
                "generated realization preserves duplicate rule occurrences");
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(! (wrap a))\n", "(wrap b)", 1u,
                "generated realization preserves contextual rewriting");
            run_document_realization(
                language, realization, &source, &answers,
                "(= ($x $x) same)\n(! (a a))\n", "same", 1u,
                "generated realization preserves repeated-variable matching");
            run_document_realization(
                language, realization, &source, &answers,
                "(= ($x $x) same)\n(! (a b))\n", "unused", 0u,
                "generated realization rejects inconsistent repeated variables");
            run_resource_limit(
                language, realization, &source, &answers,
                "(= a b)\n(= a b)\n(! a)\n",
                (CettaGsltHornLimits){
                    .max_rule_attempts = 1000000u,
                    .max_answers = 1u,
                    .max_depth = 10000u,
                },
                CETTA_GSLT_HORN_ANSWER_LIMIT,
                "generated realization fails closed at the answer limit");
            run_resource_limit(
                language, realization, &source, &answers,
                "(= a b)\n(! a)\n",
                (CettaGsltHornLimits){
                    .max_rule_attempts = 1u,
                    .max_answers = 1000u,
                    .max_depth = 10000u,
                },
                CETTA_GSLT_HORN_RULE_LIMIT,
                "generated realization fails closed at the rule limit");
        }
        if (reader) {
            run_compiled_document(
                language, reader, &universe, &answers,
                "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
                "compiled reader composes with generated semantics");
        }
    }
    cetta_gslt_language_free(language);
    language = NULL;
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_zero_language_v1, &language, error, sizeof(error)),
          "query-first generated MeTTa Zero language loads");
    if (!language) {
        fprintf(stderr, "MeTTa Zero descriptor diagnostic: %s\n", error);
    } else {
        CHECK(strcmp(cetta_gslt_language_name(language), "zero") == 0,
              "request pipeline language identity comes from its descriptor");
        CHECK(cetta_gslt_language_semantic_rule_count(language) == 51u,
              "request pipeline composes matching, query, and observation");
        for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
             raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
            CettaGsltRealization realization = (CettaGsltRealization)raw;
            run_document_realization(
                language, realization, &source, &answers,
                "fact\n(zero-query fact hit)\n", "hit", 1u,
                "request pipeline publishes arbitrary reflective query");
            run_document_evidence_realization(
                language, realization, &source, &answers,
                "fact\n(zero-query fact hit)\n",
                "zero-observed-evidence", "zero-query-evidence",
                "public query retains its authored occurrence evidence");
            run_document_realization(
                language, realization, &source, &answers,
                "(= (f $x) (g $x))\n(! (f a))\n", "(g a)", 1u,
                "request pipeline derives evaluation through public query");
            run_document_realization(
                language, realization, &source, &answers,
                "(! unknown)\n", "unknown", 1u,
                "closed empty producer retains an inert subject");
            run_document_realization(
                language, realization, &source, &answers,
                "(! native)\n", "native", 1u,
                "grounding declines when no library is composed");
            run_document_evidence_realization(
                language, realization, &source, &answers,
                "(! native)\n", "zero-inert-evidence", NULL,
                "declined grounding retains explicit inert evidence");
            run_document_realization(
                language, realization, &source, &answers,
                "fact\n(zero-query absent hit)\n", "unused", 0u,
                "closed empty query remains an empty answer bag");
            run_resource_limit(
                language, realization, &source, &answers,
                "fact\nfact\n(zero-query fact hit)\n",
                (CettaGsltHornLimits){
                    .max_rule_attempts = 1000000u,
                    .max_answers = 1u,
                    .max_depth = 10000u,
                },
                CETTA_GSLT_HORN_ANSWER_LIMIT,
                "incomplete producer cannot publish its first occurrence");
            run_completion_boundary(
                language, realization, &source, &answers,
                "(! unknown)\n",
                "completed empty producer reaches inert observation");
        }
    }
    cetta_gslt_language_free(language);
    language = NULL;
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_zero_exp_language_v1, &language,
              error, sizeof(error)),
          "authored experimental Zero runner profile loads");
    if (!language) {
        fprintf(stderr, "MeTTa Zero exp descriptor diagnostic: %s\n", error);
    } else {
        CHECK(cetta_gslt_language_semantic_rule_count(language) == 57u,
              "experimental profile adds exactly its authored runner rules");
        for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
             raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
            CettaGsltRealization realization = (CettaGsltRealization)raw;
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(zero-step (zero-pending a zero-halt))\n",
                "(zero-pending b zero-halt)", 1u,
                "runner delegates a successful step to query-derived evaluation");
            run_document_realization(
                language, realization, &source, &answers,
                "(zero-step (zero-pending b zero-halt))\n",
                "(zero-completed b)", 1u,
                "runner distinguishes completed quiescence from pending work");
            run_document_realization(
                language, realization, &source, &answers,
                "(zero-step (zero-pending b "
                "(zero-then (wrap $x) zero-halt)))\n",
                "(zero-pending (wrap b) zero-halt)", 1u,
                "runner applies an authored semantic continuation");
        }
    }
    cetta_gslt_language_free(language);
    language = NULL;
    for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
         raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
        CettaGsltRealization realization = (CettaGsltRealization)raw;
        memset(error, 0, sizeof(error));
        CHECK(cetta_gslt_language_load_embedded_for_realization(
                  &cetta_zero_ground_library_v1, realization,
                  &language, error, sizeof(error)),
              "authored grounding library composes into a generated pack");
        if (language) {
            CHECK(cetta_gslt_language_semantic_rule_count(language) == 52u,
                  "library composition adds exactly its authored rule");
            run_document_realization(
                language, realization, &source, &answers,
                "(! native)\n", "grounded", 1u,
                "grounding is supplied by the composed library relation");
            run_document_evidence_realization(
                language, realization, &source, &answers,
                "(! native)\n",
                "zero-observed-evidence", "zero-ground-evidence",
                "library grounding retains its authored capability evidence");
        }
        cetta_gslt_language_free(language);
        language = NULL;
    }
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded_for_realization(
              &cetta_gslt_compiled_canary_v1,
              CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &language, error, sizeof(error)),
          "renamed non-Zero compiled canary loads generically");
    if (language) {
        run_document_realization(
            language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &source, &answers, "", "renamed-answer", 1u,
            "compiled executor runs a renamed independent language");
        Atom **no_forms = NULL;
        CettaGsltLanguageResult unavailable;
        memset(error, 0, sizeof(error));
        CHECK(!cetta_gslt_language_execute_atoms_with_realization(
              language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
                  no_forms, 0u, &answers, limits(), &unavailable,
                  error, sizeof(error)) &&
                  strstr(error, "not loaded") != NULL,
              "compiled-only load does not retain the reference program");
    }
    cetta_gslt_language_free(language);
    language = NULL;
    for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
         raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
        CettaGsltRealization realization = (CettaGsltRealization)raw;
        memset(error, 0, sizeof(error));
        CHECK(cetta_gslt_language_load_embedded_for_realization(
                  &cetta_gslt_pipeline_canary_v1, realization,
                  &language, error, sizeof(error)),
              "renamed non-Zero request pipeline loads generically");
        if (language) {
            run_document_realization(
                language, realization, &source, &answers,
                "(canary-run)\n", "renamed-pipeline-answer", 1u,
                "request pipeline executes without language vocabulary dispatch");
        }
        cetta_gslt_language_free(language);
        language = NULL;
    }
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
