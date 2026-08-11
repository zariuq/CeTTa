#include "generated/zero_interact_language_v1.generated.h"
#include "generated/zero_interact_provider_catalog_v1.generated.h"
#include "gslt_abt_provider_v1.h"
#include "gslt_language_runtime.h"
#include "gslt_revisioned_space_provider_v1.h"
#include "parser.h"
#include "symbol.h"

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

typedef struct {
    const char *path;
    const char *expected;
    size_t expected_count;
} InteractFixtureV1;

static const InteractFixtureV1 FIXTURES[] = {
    {"tests/zero_emit/01_emit_then_match.metta", "(seen a)", 1u},
    {"tests/zero_emit/02_add_atom_compatibility.metta", "(seen a)", 1u},
    {"tests/zero_emit/03_duplicate_emissions.metta",
     "(seen a)\n(seen a)", 2u},
    {"tests/zero_emit/04_eval_emitted_equation.metta", "(g a)", 1u},
    {"tests/zero_emit/05_match_before_emit_is_empty.metta", "", 0u},
    {"tests/zero_emit/06_unknown_target_remains_data.metta", "(fact a)", 1u},
    {"tests/zero_emit/07_quote_let_binding.metta", "(swapped b a)", 1u},
    {"tests/zero_emit/08_branching_match.metta",
     "(seen a)\n(seen b)", 2u},
    {"tests/zero_emit/09_emission_is_branch_local.metta",
     "(seen a a)\n(seen b b)", 2u},
    {"tests/zero_emit/10_failed_let_pattern_is_empty.metta", "", 0u},
    {"tests/zero_interact/11_authored_duplicate_occurrences.metta",
     "(seen a)\n(seen a)", 2u},
    {"tests/zero_interact/12_repeated_pattern_variable.metta",
     "(same a)", 1u},
    {"tests/zero_interact/13_stored_variable_is_data.metta", "", 0u},
    {"tests/zero_interact/14_support_indexed_let_under_binders.metta",
     "(Lam A (Lam A (idx 1)))", 1u},
    {"tests/zero_interact/15_support_removal_rejects_capture.metta",
     "", 0u},
};

static const CettaGsltRevisionedSpaceSchemaV1 ZERO_INTERACT_SCHEMA = {
    .open_relation = "zero-space-open",
    .open_semantic_id = "zero.revisioned-space.open.v1",
    .member_relation = "zero-space-member",
    .member_semantic_id = "zero.revisioned-space.member.v1",
    .candidate_relation = "zero-space-candidate",
    .candidate_semantic_id = "zero.revisioned-space.candidate.v1",
    .emit_relation = "zero-space-emit",
    .emit_semantic_id = "zero.revisioned-space.emit.v1",
    .program_nil_constructor = "zero-program-nil",
    .program_cons_constructor = "zero-program-cons",
    .world_token_constructor = "zero-world-token",
    .stored_occurrence_constructor = "zero-space-stored-occurrence",
    .emitted_occurrence_constructor = "zero-space-emitted-occurrence",
    .open_receipt_constructor = "zero-space-open-receipt",
    .emit_receipt_constructor = "zero-space-emit-receipt",
};

static const CettaGsltAbtProviderSchemaV1 ZERO_INTERACT_ABT_SCHEMA = {
    .field_depth_relation = "qabt-field-depth",
    .field_depth_semantic_id = "abt.default-signature.field-depth.v1",
    .transport_relation = "qabt-transport",
    .transport_semantic_id = "abt.default-signature.transport.v1",
};

static CettaGsltHornLimits generous_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 1000000u,
        .max_answers = 10000u,
        .max_depth = 10000u,
    };
}

static bool answer_bags_equal(
    const CettaGsltLanguageResult *actual,
    Atom *const *expected,
    size_t expected_count) {
    if (!actual || actual->answer_count != expected_count)
        return false;
    bool *used = calloc(expected_count ? expected_count : 1u, sizeof(*used));
    if (!used)
        return false;
    bool equal = true;
    for (size_t actual_index = 0u;
         equal && actual_index < actual->answer_count; actual_index++) {
        bool found = false;
        for (size_t expected_index = 0u;
             !found && expected_index < expected_count; expected_index++) {
            if (!used[expected_index] &&
                atom_eq(actual->answers[actual_index], expected[expected_index])) {
                used[expected_index] = true;
                found = true;
            }
        }
        equal = found;
    }
    free(used);
    return equal;
}

static void print_answers(const CettaGsltLanguageResult *result) {
    Arena rendered;
    arena_init(&rendered);
    fprintf(stderr, "  actual=[");
    if (result) {
        for (size_t index = 0u; index < result->answer_count; index++) {
            char *text = atom_to_string(&rendered, result->answers[index]);
            fprintf(stderr, "%s%s", index ? ", " : "", text ? text : "<oom>");
        }
    }
    fprintf(stderr, "]\n");
    arena_free(&rendered);
}

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void run_raw_candidate_frontier(
    CettaGsltRevisionedSpaceProviderV1 *provider,
    const char *engine_name) {
    Arena query_arena;
    Arena open_answer_arena;
    Arena candidate_answer_arena;
    arena_init(&query_arena);
    arena_init(&open_answer_arena);
    arena_init(&candidate_answer_arena);
    Atom *open_goal = parse_one(
        &query_arena,
        "(zero-space-open "
        " (zero-program-cons (gslt-source-occurrence 0) "
        "  (q-app (q-sym (q-str \"fact\")) "
        "   (q-cons (q-sym (q-str \"a\")) q-nil)) "
        "  (zero-program-cons (gslt-source-occurrence 1) "
        "   (q-app (q-sym (q-str \"fact\")) "
        "    (q-cons (q-sym (q-str \"a\")) q-nil)) "
        "   zero-program-nil)) "
        " $world $receipt)");
    const CettaGsltProviderRegistryV1 *registry =
        cetta_gslt_revisioned_space_provider_registry_v1(provider);
    const CettaGsltProviderV1 *open_provider =
        cetta_gslt_provider_find_v1(registry, open_goal);
    CettaGsltProviderAnswersV1 open_answers = {0};
    char error[ERROR_CAP] = {0};
    CettaGsltProviderOutcomeV1 open_outcome = open_provider
        ? open_provider->query(
              open_provider->context, &open_answer_arena, open_goal, 10u,
              &open_answers, error, sizeof(error))
        : CETTA_GSLT_PROVIDER_FAULT;
    char label[384];
    (void)snprintf(
        label, sizeof(label),
        "%s opens an occurrence-labelled physical world", engine_name);
    CHECK(open_goal && open_provider &&
              open_outcome == CETTA_GSLT_PROVIDER_COMPLETED &&
              open_answers.answer_count == 1u,
          label);
    if (!open_goal || !open_provider ||
        open_outcome != CETTA_GSLT_PROVIDER_COMPLETED ||
        open_answers.answer_count != 1u)
        goto done;

    Atom *candidate_goal = parse_one(
        &query_arena,
        "(zero-space-candidate placeholder "
        " (q-app (q-sym (q-str \"fact\")) "
        "  (q-cons (q-var q-zero) q-nil)) "
        " $occurrence $candidate)");
    Atom *world_token = open_answers.answers[0]->expr.elems[2];
    if (candidate_goal)
        candidate_goal->expr.elems[1] =
            atom_deep_copy(&query_arena, world_token);
    const CettaGsltProviderV1 *candidate_provider =
        cetta_gslt_provider_find_v1(registry, candidate_goal);
    CettaGsltProviderAnswersV1 candidate_answers = {0};
    memset(error, 0, sizeof(error));
    CettaGsltProviderOutcomeV1 candidate_outcome = candidate_provider
        ? candidate_provider->query(
              candidate_provider->context, &candidate_answer_arena,
              candidate_goal, 10u, &candidate_answers,
              error, sizeof(error))
        : CETTA_GSLT_PROVIDER_FAULT;
    Atom *occurrence_zero = parse_one(
        &query_arena, "(gslt-source-occurrence 0)");
    Atom *occurrence_one = parse_one(
        &query_arena, "(gslt-source-occurrence 1)");
    Atom *candidate_value = parse_one(
        &query_arena,
        "(q-app (q-sym (q-str \"fact\")) "
        " (q-cons (q-sym (q-str \"a\")) q-nil))");
    bool saw_zero = false;
    bool saw_one = false;
    bool rows_valid =
        candidate_outcome == CETTA_GSLT_PROVIDER_COMPLETED &&
        candidate_answers.answer_count == 2u;
    for (size_t index = 0u;
         rows_valid && index < candidate_answers.answer_count; index++) {
        Atom *answer = candidate_answers.answers[index];
        rows_valid = answer && answer->kind == ATOM_EXPR &&
            answer->expr.len == 5u &&
            atom_eq(answer->expr.elems[1], world_token) &&
            atom_eq(answer->expr.elems[4], candidate_value);
        if (rows_valid) {
            saw_zero = saw_zero ||
                atom_eq(answer->expr.elems[3], occurrence_zero);
            saw_one = saw_one ||
                atom_eq(answer->expr.elems[3], occurrence_one);
        }
    }
    (void)snprintf(
        label, sizeof(label),
        "%s returns two stable IDs for two equal candidate occurrences",
        engine_name);
    if (!rows_valid)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(rows_valid && saw_zero && saw_one, label);
    cetta_gslt_provider_answers_free_v1(&candidate_answers);

done:
    cetta_gslt_provider_answers_free_v1(&open_answers);
    arena_free(&candidate_answer_arena);
    arena_free(&open_answer_arena);
    arena_free(&query_arena);
}

static void run_fixture(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    CettaGsltRevisionedSpaceProviderV1 *provider,
    const CettaGsltProviderRegistryV1 *physical,
    const InteractFixtureV1 *fixture,
    const char *engine_name) {
    Arena source;
    Arena expected_arena;
    Arena output;
    arena_init(&source);
    arena_init(&expected_arena);
    arena_init(&output);
    Atom **forms = NULL;
    Atom **expected = NULL;
    int form_count = parse_metta_file(fixture->path, &source, &forms);
    int expected_count = parse_metta_text(
        fixture->expected, &expected_arena, &expected);
    char label[384];
    (void)snprintf(
        label, sizeof(label), "%s/%s executes %s",
        engine_name, cetta_gslt_realization_name(realization), fixture->path);
    CHECK(form_count >= 0 && expected_count >= 0 &&
              (size_t)expected_count == fixture->expected_count,
          label);

    CettaGsltLanguageResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ok = form_count >= 0 && expected_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
            language, realization,
            &cetta_zero_interact_provider_catalog_v1,
            physical,
            forms, (size_t)form_count, &output, generous_limits(),
            &result, error, sizeof(error));
    if (!ok || result.outcome != CETTA_GSLT_HORN_COMPLETED ||
        !answer_bags_equal(&result, expected, fixture->expected_count)) {
        fprintf(
            stderr,
            "DETAIL: %s: ok=%u outcome=%u expected=%zu error=%s\n",
            label, ok ? 1u : 0u, (unsigned)result.outcome,
            fixture->expected_count, error);
        print_answers(&result);
    }
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              answer_bags_equal(&result, expected, fixture->expected_count),
          label);
    (void)snprintf(
        label, sizeof(label), "%s/%s keeps its backend active after %s",
        engine_name, cetta_gslt_realization_name(realization), fixture->path);
    CHECK(cetta_gslt_revisioned_space_provider_primary_active_v1(provider),
          label);
    if (ok)
        cetta_gslt_language_result_free(&result);
    free(expected);
    free(forms);
    arena_free(&output);
    arena_free(&expected_arena);
    arena_free(&source);
}

static void run_bounded_frontier(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    CettaGsltRevisionedSpaceProviderV1 *provider,
    const CettaGsltProviderRegistryV1 *physical,
    const char *engine_name) {
    Arena source;
    Arena output;
    arena_init(&source);
    arena_init(&output);
    Atom **forms = NULL;
    int form_count = parse_metta_file(
        "tests/zero_interact/11_authored_duplicate_occurrences.metta",
        &source, &forms);
    CettaGsltHornLimits limits = generous_limits();
    limits.max_answers = 1u;
    CettaGsltLanguageResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ok = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
            language, realization,
            &cetta_zero_interact_provider_catalog_v1,
            physical,
            forms, (size_t)form_count, &output, limits,
            &result, error, sizeof(error));
    char label[384];
    (void)snprintf(
        label, sizeof(label),
        "%s/%s publishes no partial occurrence frontier",
        engine_name, cetta_gslt_realization_name(realization));
    if (!ok || result.outcome != CETTA_GSLT_HORN_ANSWER_LIMIT ||
        result.answer_count != 0u) {
        fprintf(
            stderr,
            "DETAIL: %s: ok=%u outcome=%u answers=%zu error=%s\n",
            label, ok ? 1u : 0u, (unsigned)result.outcome,
            result.answer_count, error);
    }
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_ANSWER_LIMIT &&
              result.answer_count == 0u,
          label);
    if (ok)
        cetta_gslt_language_result_free(&result);
    free(forms);
    arena_free(&output);
    arena_free(&source);
}

static void exercise_backend(SpaceEngine engine) {
    const char *engine_name = space_match_backend_kind_name(engine);
    char error[ERROR_CAP] = {0};
    CettaGsltRevisionedSpaceProviderV1 *provider =
        cetta_gslt_revisioned_space_provider_create_v1(
            engine, &ZERO_INTERACT_SCHEMA, error, sizeof(error));
    char label[256];
    (void)snprintf(
        label, sizeof(label), "%s revisioned-space provider is available",
        engine_name);
    if (!provider)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(provider != NULL, label);
    if (!provider)
        return;

    memset(error, 0, sizeof(error));
    CettaGsltAbtProviderV1 *abt_provider =
        cetta_gslt_abt_provider_create_v1(
            &ZERO_INTERACT_ABT_SCHEMA, NULL, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s support-indexed ABT provider is available",
        engine_name);
    if (!abt_provider)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(abt_provider != NULL, label);
    if (!abt_provider) {
        cetta_gslt_revisioned_space_provider_free_v1(provider);
        return;
    }
    const CettaGsltProviderRegistryV1 *registries[] = {
        cetta_gslt_revisioned_space_provider_registry_v1(provider),
        cetta_gslt_abt_provider_registry_v1(abt_provider),
    };
    CettaGsltOwnedProviderRegistryV1 physical = {0};
    memset(error, 0, sizeof(error));
    bool combined = cetta_gslt_provider_registry_union_v1(
        registries, 2u, &physical, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s composes space and ABT provider libraries",
        engine_name);
    if (!combined)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(combined && physical.registry.provider_count == 6u, label);
    if (!combined) {
        cetta_gslt_abt_provider_free_v1(abt_provider);
        cetta_gslt_revisioned_space_provider_free_v1(provider);
        return;
    }

    run_raw_candidate_frontier(provider, engine_name);

    size_t worlds_after_reference = 0u;
    for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
         raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
        CettaGsltRealization realization = (CettaGsltRealization)raw;
        CettaGsltLanguage *language = NULL;
        memset(error, 0, sizeof(error));
        bool loaded = cetta_gslt_language_load_embedded_for_realization(
            &cetta_zero_interact_language_v1, realization,
            &language, error, sizeof(error));
        (void)snprintf(
            label, sizeof(label), "%s/%s loads the generated interact profile",
            engine_name, cetta_gslt_realization_name(realization));
        if (!loaded)
            fprintf(stderr, "DETAIL: %s: %s\n", label, error);
        CHECK(loaded, label);
        if (language) {
            for (size_t index = 0u;
                 index < sizeof(FIXTURES) / sizeof(FIXTURES[0]); index++)
                run_fixture(
                    language, realization, provider,
                    &physical.registry,
                    &FIXTURES[index], engine_name);
            run_bounded_frontier(
                language, realization, provider,
                &physical.registry, engine_name);
        }
        cetta_gslt_language_free(language);
        if (realization == CETTA_GSLT_REALIZATION_HORN_REFERENCE)
            worlds_after_reference =
                cetta_gslt_revisioned_space_provider_world_count_v1(provider);
    }

    size_t worlds_after_both =
        cetta_gslt_revisioned_space_provider_world_count_v1(provider);
    (void)snprintf(
        label, sizeof(label),
        "%s content-addresses identical worlds across realizations",
        engine_name);
    CHECK(worlds_after_reference > 0u &&
              worlds_after_both == worlds_after_reference,
          label);
    (void)snprintf(
        label, sizeof(label), "%s keeps its selected physical backend active",
        engine_name);
    CHECK(cetta_gslt_revisioned_space_provider_primary_active_v1(provider),
          label);
    cetta_gslt_owned_provider_registry_free_v1(&physical);
    cetta_gslt_abt_provider_free_v1(abt_provider);
    cetta_gslt_revisioned_space_provider_free_v1(provider);
}

int main(int argc, char **argv) {
    bool run_native = true;
    bool run_pathmap = true;
    if (argc > 2 ||
        (argc == 2 && strcmp(argv[1], "all") != 0 &&
         strcmp(argv[1], "native") != 0 &&
         strcmp(argv[1], "pathmap") != 0)) {
        fprintf(stderr, "usage: %s [all|native|pathmap]\n", argv[0]);
        return 2;
    }
    if (argc == 2 && strcmp(argv[1], "native") == 0)
        run_pathmap = false;
    else if (argc == 2 && strcmp(argv[1], "pathmap") == 0)
        run_native = false;

    SymbolTable symbols;
    VarInternTable variable_names;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;

    if (run_native)
        exercise_backend(SPACE_ENGINE_NATIVE);
#if CETTA_BUILD_WITH_PATHMAP_SPACE
    if (run_pathmap)
        exercise_backend(SPACE_ENGINE_PATHMAP);
#else
    if (run_pathmap) {
        char error[ERROR_CAP] = {0};
        CettaGsltRevisionedSpaceProviderV1 *unavailable =
            cetta_gslt_revisioned_space_provider_create_v1(
                SPACE_ENGINE_PATHMAP, &ZERO_INTERACT_SCHEMA,
                error, sizeof(error));
        CHECK(!unavailable && error[0] != '\0',
              "non-bridge build cannot imitate Rust/PathMap via C ABI");
        cetta_gslt_revisioned_space_provider_free_v1(unavailable);
    }
#endif

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    if (failures != 0u) {
        fprintf(
            stderr, "%u/%u Zero interact provider checks failed\n",
            failures, checks);
        return 1;
    }
    printf("PASS: %u Zero interact provider checks\n", checks);
    return 0;
}
