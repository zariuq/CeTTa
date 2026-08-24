#include "tests/generated/gslt_provider_canary_v1.generated.h"
#include "tests/generated/gslt_provider_canary_catalog_v1.generated.h"
#include "gslt_compiled_runtime.h"
#include "gslt_finite_fact_provider_v1.h"
#include "gslt_horn_runtime.h"
#include "gslt_provider_runtime.h"
#include "gslt_space_fact_provider_v1.h"
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
            fprintf(stderr, "FAIL: %s\n", (label));                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

typedef struct {
    size_t calls;
    bool malformed;
} ProviderContext;

static CettaGsltProviderOutcomeV1 answer_provider(
    void *raw_context,
    Arena *arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    ProviderContext *context = raw_context;
    context->calls++;
    if (answer_limit < 3u)
        return CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
    const char *values[] = {"alpha", "alpha", "beta"};
    for (size_t index = 0u; index < 3u; index++) {
        Atom **elements = arena_alloc(arena, sizeof(*elements) * 3u);
        elements[0] = atom_symbol(
            arena,
            context->malformed && index == 0u
                ? "wrong-relation" : "provider-external");
        elements[1] = atom_deep_copy(arena, (Atom *)goal->expr.elems[1]);
        elements[2] = cetta_gslt_quote_atom_v1(
            arena, atom_symbol(arena, values[index]));
        Atom *answer = elements[1]
            ? atom_expr(arena, elements, 3u) : NULL;
        if (!answer ||
            !cetta_gslt_provider_answers_push_v1(answers, answer)) {
            if (error && error_size > 0u)
                (void)snprintf(
                    error, error_size,
                    "provider canary could not allocate its completed frontier");
            return CETTA_GSLT_PROVIDER_FAULT;
        }
    }
    return CETTA_GSLT_PROVIDER_COMPLETED;
}

static bool answer_has_value(const Atom *answer, const char *value) {
    const Atom *quoted = answer && answer->kind == ATOM_EXPR &&
        answer->expr.len == 3u ? answer->expr.elems[2] : NULL;
    return quoted && quoted->kind == ATOM_EXPR && quoted->expr.len == 2u &&
        atom_is_symbol(quoted->expr.elems[0], "q-sym") &&
        quoted->expr.elems[1]->kind == ATOM_EXPR &&
        quoted->expr.elems[1]->expr.len == 2u &&
        atom_is_symbol(quoted->expr.elems[1]->expr.elems[0], "q-str") &&
        quoted->expr.elems[1]->expr.elems[1]->kind == ATOM_GROUNDED &&
        quoted->expr.elems[1]->expr.elems[1]->ground.gkind == GV_STRING &&
        answer->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(answer->expr.elems[0]), "provider-entry") == 0 &&
        strcmp(quoted->expr.elems[1]->expr.elems[1]->ground.sval, value) == 0;
}

static Atom *parse_query(Arena *arena) {
    Atom **forms = NULL;
    int count = parse_metta_text(
        "(provider-entry program $answer)", arena, &forms);
    Atom *query = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return query;
}

static Atom *make_provider_program(Arena *arena) {
    Atom **forms = NULL;
    int count = parse_metta_text(
        "(provider-program-cons (gslt-source-occurrence 0) "
        "(q-sym (q-str \"seed\")) provider-program-nil)",
        arena, &forms);
    Atom *program = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return program;
}

static Atom *make_space_provider_fact(
    Arena *arena, Atom *program, const char *value) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 3u);
    elements[0] = atom_symbol(arena, "provider-external");
    elements[1] = program;
    elements[2] = cetta_gslt_quote_atom_v1(
        arena, atom_symbol(arena, value));
    return elements[0] && elements[1] && elements[2]
        ? atom_expr(arena, elements, 3u) : NULL;
}

static Atom *make_wrong_space_provider_fact(Arena *arena, Atom *program) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * 3u);
    elements[0] = atom_symbol(arena, "wrong-relation");
    elements[1] = program;
    elements[2] = cetta_gslt_quote_atom_v1(
        arena, atom_symbol(arena, "alpha"));
    return elements[0] && elements[1] && elements[2]
        ? atom_expr(arena, elements, 3u) : NULL;
}

typedef bool (*QueryEngine)(
    const void *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *output,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error,
    size_t error_size);

static bool query_horn(
    const void *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *output,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error,
    size_t error_size) {
    return cetta_gslt_horn_query_with_providers_v1(
        program, providers, output, query, limits,
        result, error, error_size);
}

static bool query_compiled(
    const void *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *output,
    Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error,
    size_t error_size) {
    return cetta_gslt_compiled_query_with_providers_v1(
        program, providers, output, query, limits,
        result, error, error_size);
}

static void exercise_engine(
    const char *name,
    QueryEngine query_engine,
    const void *program) {
    Arena query_arena;
    Arena output;
    arena_init(&query_arena);
    arena_init(&output);
    Atom *query = parse_query(&query_arena);
    CettaGsltHornLimits generous = {
        .max_rule_attempts = 1000u,
        .max_answers = 100u,
        .max_depth = 100u,
    };
    char label[192];
    char error[ERROR_CAP] = {0};
    CettaGsltHornResult result;

    ProviderContext context = {0};
    CettaGsltProviderV1 provider = {
        .relation = "provider-external",
        .arity = 2u,
        .semantic_id = "canary.completed-bag.v1",
        .context = &context,
        .query = answer_provider,
    };
    CettaGsltProviderRegistryV1 registry = {
        .providers = &provider,
        .provider_count = 1u,
    };
    bool ok = query_engine(
        program, &registry, &output, query, generous,
        &result, error, sizeof(error));
    (void)snprintf(label, sizeof(label), "%s executes a declared provider", name);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 3u && context.calls == 1u,
          label);
    if (ok) {
        size_t alpha = 0u;
        size_t beta = 0u;
        for (size_t index = 0u; index < result.answer_count; index++) {
            alpha += answer_has_value(result.answers[index], "alpha") ? 1u : 0u;
            beta += answer_has_value(result.answers[index], "beta") ? 1u : 0u;
        }
        (void)snprintf(
            label, sizeof(label), "%s preserves provider multiplicity", name);
        CHECK(alpha == 2u && beta == 1u, label);
        cetta_gslt_horn_result_free(&result);
    }

    memset(error, 0, sizeof(error));
    ok = query_engine(
        program, NULL, &output, query, generous,
        &result, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s leaves an unregistered relation empty", name);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 0u,
          label);
    if (ok)
        cetta_gslt_horn_result_free(&result);

    CettaGsltHornLimits bounded = generous;
    bounded.max_answers = 2u;
    context.calls = 0u;
    memset(error, 0, sizeof(error));
    ok = query_engine(
        program, &registry, &output, query, bounded,
        &result, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s fails closed on a partial provider frontier",
        name);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_ANSWER_LIMIT &&
              result.answer_count == 0u && context.calls == 1u,
          label);
    if (ok)
        cetta_gslt_horn_result_free(&result);

    context.calls = 0u;
    context.malformed = true;
    memset(error, 0, sizeof(error));
    ok = query_engine(
        program, &registry, &output, query, generous,
        &result, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s rejects malformed provider answers", name);
    CHECK(!ok && error[0] != '\0' && context.calls == 1u, label);
    context.malformed = false;

    ProviderContext override_context = {0};
    CettaGsltProviderV1 override = {
        .relation = "provider-entry",
        .arity = 2u,
        .semantic_id = "canary.forbidden-override.v1",
        .context = &override_context,
        .query = answer_provider,
    };
    CettaGsltProviderRegistryV1 override_registry = {
        .providers = &override,
        .provider_count = 1u,
    };
    memset(error, 0, sizeof(error));
    ok = query_engine(
        program, &override_registry, &output, query, generous,
        &result, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s providers cannot override authored rules", name);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 0u && override_context.calls == 0u,
          label);
    if (ok)
        cetta_gslt_horn_result_free(&result);

    arena_free(&output);
    arena_free(&query_arena);
}

static bool value_is_symbol(const Atom *value, const char *name) {
    return value && value->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr((Atom *)value), name) == 0;
}

static void exercise_language(CettaGsltRealization realization) {
    const char *name = cetta_gslt_realization_name(realization);
    char label[192];
    char error[ERROR_CAP] = {0};
    CettaGsltLanguage *language = NULL;
    bool ok = cetta_gslt_language_load_embedded_for_realization(
        &cetta_gslt_provider_canary_v1, realization,
        &language, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s loads the provider language pack", name);
    CHECK(ok && language, label);
    if (!language)
        return;

    Arena source_arena;
    Arena output;
    arena_init(&source_arena);
    arena_init(&output);
    Atom **forms = NULL;
    int form_count = parse_metta_text("seed", &source_arena, &forms);
    ProviderContext context = {0};
    CettaGsltProviderV1 provider = {
        .relation = "provider-external",
        .arity = 2u,
        .semantic_id = "canary.completed-bag.v1",
        .context = &context,
        .query = answer_provider,
    };
    CettaGsltProviderRegistryV1 registry = {
        .providers = &provider,
        .provider_count = 1u,
    };
    CettaGsltHornLimits limits = {
        .max_rule_attempts = 1000u,
        .max_answers = 100u,
        .max_depth = 100u,
    };
    CettaGsltLanguageResult result = {0};
    memset(error, 0, sizeof(error));
    ok = form_count == 1 && cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
        language, realization,
        &cetta_gslt_provider_canary_catalog_v1, &registry,
        forms, 1u, &output, limits, &result,
        error, sizeof(error));
    size_t alpha = 0u;
    size_t beta = 0u;
    if (ok) {
        for (size_t index = 0u; index < result.answer_count; index++) {
            alpha += value_is_symbol(result.answers[index], "alpha") ? 1u : 0u;
            beta += value_is_symbol(result.answers[index], "beta") ? 1u : 0u;
        }
    }
    (void)snprintf(
        label, sizeof(label),
        "%s binds an authored provider catalog to physical answers", name);
    if (!ok)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 3u && alpha == 2u && beta == 1u,
          label);
    if (ok)
        cetta_gslt_language_result_free(&result);

    memset(error, 0, sizeof(error));
    ok = cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
        language, realization,
        &cetta_gslt_provider_canary_catalog_v1, NULL,
        forms, 1u, &output, limits, &result,
        error, sizeof(error));
    (void)snprintf(
        label, sizeof(label),
        "%s treats an absent physical provider as an empty relation", name);
    if (!ok)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 0u,
          label);
    if (ok)
        cetta_gslt_language_result_free(&result);

    memset(error, 0, sizeof(error));
    ok = cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
        language, realization, NULL, &registry,
        forms, 1u, &output, limits, &result,
        error, sizeof(error));
    (void)snprintf(
        label, sizeof(label),
        "%s rejects physical providers without authored authority", name);
    CHECK(!ok && error[0] != '\0', label);

    CettaGsltProviderV1 wrong_provider = provider;
    wrong_provider.semantic_id = "canary.wrong-meaning.v1";
    CettaGsltProviderRegistryV1 wrong_registry = {
        .providers = &wrong_provider,
        .provider_count = 1u,
    };
    memset(error, 0, sizeof(error));
    ok = cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
        language, realization,
        &cetta_gslt_provider_canary_catalog_v1, &wrong_registry,
        forms, 1u, &output, limits, &result,
        error, sizeof(error));
    (void)snprintf(
        label, sizeof(label),
        "%s rejects a provider with the wrong semantic identity", name);
    CHECK(!ok && error[0] != '\0', label);

    CettaGsltProviderCatalogV1 wrong_target =
        cetta_gslt_provider_canary_catalog_v1;
    wrong_target.language_manifest_sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    memset(error, 0, sizeof(error));
    ok = cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
        language, realization, &wrong_target, &registry,
        forms, 1u, &output, limits, &result,
        error, sizeof(error));
    (void)snprintf(
        label, sizeof(label),
        "%s rejects a catalog bound to another manifest revision", name);
    CHECK(!ok && error[0] != '\0', label);

    CettaGsltProviderCatalogV1 truncated =
        cetta_gslt_provider_canary_catalog_v1;
    truncated.source_length--;
    memset(error, 0, sizeof(error));
    ok = cetta_gslt_provider_catalog_validate_v1(
        &truncated, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s rejects a tampered provider catalog", name);
    CHECK(!ok && error[0] != '\0', label);

    free(forms);
    arena_free(&output);
    arena_free(&source_arena);
    cetta_gslt_language_free(language);
}

static void exercise_space_provider_backend(SpaceEngine engine) {
    const char *backend = space_match_backend_kind_name(engine);
    char label[256];
    char error[ERROR_CAP] = {0};
    CettaGsltSpaceFactProviderV1 *space_provider =
        cetta_gslt_space_fact_provider_create_v1(
            engine, "provider-external", 2u,
            "canary.completed-bag.v1", error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s creates a Space-backed provider", backend);
    if (!space_provider)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(space_provider != NULL, label);
    if (!space_provider)
        return;

    const CettaGsltProviderV1 *physical =
        cetta_gslt_space_fact_provider_physical_v1(space_provider);
    CHECK(physical &&
              cetta_gslt_space_fact_provider_engine_v1(space_provider) == engine,
          "Space-backed provider retains its selected physical engine");

    Arena fact_arena;
    Arena source_arena;
    Arena output;
    arena_init(&fact_arena);
    arena_init(&source_arena);
    arena_init(&output);
    Atom *provider_program = make_provider_program(&fact_arena);
    const char *values[] = {"alpha", "alpha", "beta"};
    bool admitted = provider_program != NULL;
    for (size_t index = 0u; index < 3u; index++) {
        Atom *fact = make_space_provider_fact(
            &fact_arena, provider_program, values[index]);
        memset(error, 0, sizeof(error));
        admitted = admitted && fact &&
            cetta_gslt_space_fact_provider_admit_v1(
                space_provider, &fact_arena, fact,
                error, sizeof(error));
    }
    (void)snprintf(
        label, sizeof(label),
        "%s admits an occurrence bag into its physical space", backend);
    if (!admitted)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(admitted, label);
    (void)snprintf(
        label, sizeof(label),
        "%s keeps the selected primary storage active after publication",
        backend);
    CHECK(cetta_gslt_space_fact_provider_primary_active_v1(space_provider),
          label);

    Atom *wrong = make_wrong_space_provider_fact(
        &fact_arena, provider_program);
    memset(error, 0, sizeof(error));
    bool wrong_admitted = wrong && cetta_gslt_space_fact_provider_admit_v1(
        space_provider, &fact_arena, wrong, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label),
        "%s rejects facts outside its authored relation", backend);
    CHECK(!wrong_admitted && error[0] != '\0', label);

    Atom **forms = NULL;
    int form_count = parse_metta_text("seed", &source_arena, &forms);
    CettaGsltProviderRegistryV1 registry = {
        .providers = physical,
        .provider_count = physical ? 1u : 0u,
    };
    CettaGsltHornLimits limits = {
        .max_rule_attempts = 1000u,
        .max_answers = 100u,
        .max_depth = 100u,
    };
    const CettaGsltRealization realizations[] = {
        CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
    };
    for (size_t realization_index = 0u;
         realization_index < sizeof(realizations) / sizeof(realizations[0]);
         realization_index++) {
        CettaGsltRealization realization = realizations[realization_index];
        const char *realization_name =
            cetta_gslt_realization_name(realization);
        CettaGsltLanguage *language = NULL;
        memset(error, 0, sizeof(error));
        bool ok = cetta_gslt_language_load_embedded_for_realization(
            &cetta_gslt_provider_canary_v1, realization,
            &language, error, sizeof(error));
        (void)snprintf(
            label, sizeof(label), "%s/%s loads for physical differential",
            backend, realization_name);
        CHECK(ok && language, label);
        if (!language)
            continue;

        CettaGsltLanguageResult result = {0};
        memset(error, 0, sizeof(error));
        ok = form_count == 1 &&
            cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
                language, realization,
                &cetta_gslt_provider_canary_catalog_v1, &registry,
                forms, 1u, &output, limits, &result,
                error, sizeof(error));
        size_t alpha = 0u;
        size_t beta = 0u;
        if (ok) {
            for (size_t index = 0u; index < result.answer_count; index++) {
                alpha += value_is_symbol(result.answers[index], "alpha")
                    ? 1u : 0u;
                beta += value_is_symbol(result.answers[index], "beta")
                    ? 1u : 0u;
            }
        }
        (void)snprintf(
            label, sizeof(label),
            "%s/%s preserves the completed occurrence bag",
            backend, realization_name);
        if (!ok || result.outcome != CETTA_GSLT_HORN_COMPLETED ||
            result.answer_count != 3u || alpha != 2u || beta != 1u) {
            fprintf(
                stderr,
                "DETAIL: %s: ok=%u outcome=%u answers=%zu alpha=%zu beta=%zu error=%s\n",
                label, ok ? 1u : 0u, (unsigned)result.outcome,
                result.answer_count, alpha, beta, error);
        }
        CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
                  result.answer_count == 3u && alpha == 2u && beta == 1u,
              label);
        if (ok)
            cetta_gslt_language_result_free(&result);

        (void)snprintf(
            label, sizeof(label),
            "%s/%s does not fall through to another storage engine",
            backend, realization_name);
        CHECK(cetta_gslt_space_fact_provider_primary_active_v1(space_provider),
              label);

        CettaGsltHornLimits bounded = limits;
        bounded.max_answers = 2u;
        memset(error, 0, sizeof(error));
        ok = cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
            language, realization,
            &cetta_gslt_provider_canary_catalog_v1, &registry,
            forms, 1u, &output, bounded, &result,
            error, sizeof(error));
        (void)snprintf(
            label, sizeof(label),
            "%s/%s publishes no partial physical frontier",
            backend, realization_name);
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
        cetta_gslt_language_free(language);
    }

    free(forms);
    arena_free(&output);
    arena_free(&source_arena);
    arena_free(&fact_arena);
    cetta_gslt_space_fact_provider_free_v1(space_provider);
}

static void exercise_finite_fact_provider(
    CettaGsltRealization realization) {
    const char *realization_name = cetta_gslt_realization_name(realization);
    char label[256];
    char error[ERROR_CAP] = {0};
    Arena fact_arena;
    Arena source_arena;
    Arena output;
    arena_init(&fact_arena);
    arena_init(&source_arena);
    arena_init(&output);
    Atom *program = make_provider_program(&fact_arena);
    Atom *rows[3] = {
        make_space_provider_fact(&fact_arena, program, "alpha"),
        make_space_provider_fact(&fact_arena, program, "alpha"),
        make_space_provider_fact(&fact_arena, program, "beta"),
    };
    CettaGsltFiniteFactSpanV1 spans[2] = {
        {.rows = rows, .row_count = 1u},
        {.rows = rows + 1u, .row_count = 2u},
    };
    CettaGsltFiniteFactProviderSetV1 *set =
        cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
            cetta_gslt_provider_canary_catalog_v1.requirements,
            cetta_gslt_provider_canary_catalog_v1.requirement_count,
            spans, 2u, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label),
        "%s builds a catalog-shaped finite provider from two spans",
        realization_name);
    if (!set)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(set &&
              cetta_gslt_finite_fact_provider_set_row_count_v1(set) == 3u,
          label);

    CettaGsltFiniteFactRelationViewV1 relation_view = {0};
    CHECK(set &&
              cetta_gslt_finite_fact_provider_set_relation_count_v1(set) ==
                  cetta_gslt_provider_canary_catalog_v1.requirement_count,
          "finite provider exposes its validated relation count");
    CHECK(set &&
              cetta_gslt_finite_fact_provider_set_relation_view_v1(
                  set, 0u, &relation_view) &&
              relation_view.requirement ==
                  &cetta_gslt_provider_canary_catalog_v1.requirements[0] &&
              strcmp(relation_view.requirement->relation,
                     "provider-external") == 0 &&
              strcmp(relation_view.requirement->semantic_id,
                     "canary.completed-bag.v1") == 0 &&
              relation_view.requirement->arity == 2u &&
              relation_view.row_count == 3u &&
              relation_view.rows[0] == rows[0] &&
              relation_view.rows[1] == rows[1] &&
              relation_view.rows[2] == rows[2],
          "finite provider exposes each validated relation and ordered rows");
    CHECK(set &&
              !cetta_gslt_finite_fact_provider_set_relation_view_v1(
                  set,
                  cetta_gslt_finite_fact_provider_set_relation_count_v1(set),
                  &relation_view) &&
              !relation_view.requirement && !relation_view.rows &&
              relation_view.row_count == 0u,
          "finite provider rejects an out-of-range relation view");
    CHECK(!cetta_gslt_finite_fact_provider_set_relation_view_v1(
              set, 0u, NULL),
          "finite provider rejects an absent relation-view destination");

    CettaGsltLanguage *language = NULL;
    memset(error, 0, sizeof(error));
    bool ok = set && cetta_gslt_language_load_embedded_for_realization(
        &cetta_gslt_provider_canary_v1, realization,
        &language, error, sizeof(error));
    (void)snprintf(
        label, sizeof(label), "%s loads for finite-provider execution",
        realization_name);
    CHECK(ok && language, label);

    Atom **forms = NULL;
    int form_count = parse_metta_text("seed", &source_arena, &forms);
    CettaGsltHornLimits limits = {
        .max_rule_attempts = 1000u,
        .max_answers = 100u,
        .max_depth = 100u,
    };
    CettaGsltLanguageResult result = {0};
    memset(error, 0, sizeof(error));
    ok = language && form_count == 1 &&
        cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
            language, realization,
            &cetta_gslt_provider_canary_catalog_v1,
            cetta_gslt_finite_fact_provider_set_registry_v1(set),
            forms, 1u, &output, limits, &result,
            error, sizeof(error));
    size_t alpha = 0u;
    size_t beta = 0u;
    if (ok) {
        for (size_t index = 0u; index < result.answer_count; index++) {
            alpha += value_is_symbol(result.answers[index], "alpha")
                ? 1u : 0u;
            beta += value_is_symbol(result.answers[index], "beta")
                ? 1u : 0u;
        }
    }
    (void)snprintf(
        label, sizeof(label),
        "%s preserves finite-row occurrence order and multiplicity",
        realization_name);
    if (!ok)
        fprintf(stderr, "DETAIL: %s: %s\n", label, error);
    CHECK(ok && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 3u && alpha == 2u && beta == 1u,
          label);
    if (ok)
        cetta_gslt_language_result_free(&result);

    Atom *wrong = make_wrong_space_provider_fact(&fact_arena, program);
    CettaGsltFiniteFactSpanV1 wrong_span = {
        .rows = &wrong,
        .row_count = 1u,
    };
    memset(error, 0, sizeof(error));
    CettaGsltFiniteFactProviderSetV1 *wrong_set =
        cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
            cetta_gslt_provider_canary_catalog_v1.requirements,
            cetta_gslt_provider_canary_catalog_v1.requirement_count,
            &wrong_span, 1u, error, sizeof(error));
    CHECK(!wrong_set && error[0] != '\0',
          "finite provider rejects a row outside its generated inventory");
    cetta_gslt_finite_fact_provider_set_free_v1(wrong_set);

    Atom **open_forms = NULL;
    int open_count = parse_metta_text(
        "(provider-external $program (q-sym (q-str \"alpha\")))",
        &fact_arena, &open_forms);
    Atom *open = open_count == 1 && open_forms ? open_forms[0] : NULL;
    CettaGsltFiniteFactSpanV1 open_span = {
        .rows = &open,
        .row_count = open ? 1u : 0u,
    };
    memset(error, 0, sizeof(error));
    CettaGsltFiniteFactProviderSetV1 *open_set = open
        ? cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
              cetta_gslt_provider_canary_catalog_v1.requirements,
              cetta_gslt_provider_canary_catalog_v1.requirement_count,
              &open_span, 1u, error, sizeof(error))
        : NULL;
    CHECK(open && !open_set && error[0] != '\0',
          "finite provider rejects an open row");
    cetta_gslt_finite_fact_provider_set_free_v1(open_set);
    free(open_forms);

    free(forms);
    cetta_gslt_language_free(language);
    cetta_gslt_finite_fact_provider_set_free_v1(set);
    arena_free(&output);
    arena_free(&source_arena);
    arena_free(&fact_arena);
}

static void exercise_finite_fact_rigid_index(void) {
    Arena fact_arena;
    Arena answer_arena;
    arena_init(&fact_arena);
    arena_init(&answer_arena);
    Atom **rows = NULL;
    int row_count = parse_metta_text(
        "(indexed-fact common alpha)\n"
        "(indexed-fact common alpha)\n"
        "(indexed-fact common beta)\n",
        &fact_arena, &rows);
    CettaGsltProviderRequirementV1 requirement = {
        .relation = "indexed-fact",
        .arity = 2u,
        .semantic_id = "canary.ordered-rigid-index.v1",
    };
    CettaGsltFiniteFactSpanV1 span = {
        .rows = rows,
        .row_count = row_count == 3 ? 3u : 0u,
    };
    char error[ERROR_CAP] = {0};
    CettaGsltFiniteFactProviderSetV1 *set = row_count == 3
        ? cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
              &requirement, 1u, &span, 1u, error, sizeof(error))
        : NULL;
    CHECK(set != NULL,
          "finite provider admits a useful generated rigid coordinate");
    const CettaGsltProviderRegistryV1 *registry =
        cetta_gslt_finite_fact_provider_set_registry_v1(set);
    const CettaGsltProviderV1 *provider = set
        ? cetta_gslt_provider_find_v1(registry, rows[0]) : NULL;
    CettaGsltProviderAnswersV1 answers = {0};
    CettaGsltProviderOutcomeV1 outcome = provider
        ? provider->query(provider->context, &answer_arena, rows[0], 10u,
              &answers, error, sizeof(error))
        : CETTA_GSLT_PROVIDER_FAULT;
    CHECK(outcome == CETTA_GSLT_PROVIDER_COMPLETED &&
              answers.answer_count == 2u &&
              atom_eq(answers.answers[0], rows[0]) &&
              atom_eq(answers.answers[1], rows[1]),
          "rigid finite-provider index preserves duplicate occurrence order");
    cetta_gslt_provider_answers_free_v1(&answers);

    Atom **open_forms = NULL;
    int open_count = parse_metta_text(
        "(indexed-fact common $value)", &fact_arena, &open_forms);
    Atom *open = open_count == 1 && open_forms ? open_forms[0] : NULL;
    provider = set && open
        ? cetta_gslt_provider_find_v1(registry, open) : NULL;
    memset(&answers, 0, sizeof(answers));
    memset(error, 0, sizeof(error));
    outcome = provider
        ? provider->query(provider->context, &answer_arena, open, 10u,
              &answers, error, sizeof(error))
        : CETTA_GSLT_PROVIDER_FAULT;
    CHECK(outcome == CETTA_GSLT_PROVIDER_COMPLETED &&
              answers.answer_count == 3u,
          "open indexed coordinate falls back to the complete finite bag");
    cetta_gslt_provider_answers_free_v1(&answers);

    CettaGsltFiniteFactProviderStatsV1 stats;
    cetta_gslt_finite_fact_provider_set_stats_v1(set, &stats);
    CHECK(stats.queries == 2u && stats.indexed_queries == 1u &&
              stats.rows_considered == 5u && stats.rows_skipped == 1u &&
              stats.indexed_relations == 1u,
          "finite-provider counters witness physical row discrimination");

    free(open_forms);
    cetta_gslt_finite_fact_provider_set_free_v1(set);
    free(rows);
    arena_free(&answer_arena);
    arena_free(&fact_arena);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variable_names;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;

    CettaGsltHornInput *inputs = cetta_malloc(
        sizeof(*inputs) * cetta_gslt_provider_canary_v1.semantic_source_count);
    for (size_t index = 0u;
         index < cetta_gslt_provider_canary_v1.semantic_source_count; index++)
        inputs[index] =
            cetta_gslt_provider_canary_v1.semantic_sources[index].input;
    CettaGsltHornProgram *horn = NULL;
    CettaGsltCompiledProgram *compiled = NULL;
    char error[ERROR_CAP] = {0};
    CHECK(cetta_gslt_horn_program_load_inputs(
              inputs,
              cetta_gslt_provider_canary_v1.semantic_source_count,
              &horn, error, sizeof(error)),
          "provider canary source admits to Horn");
    free(inputs);
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_compiled_program_load_v1(
              &cetta_gslt_provider_canary_v1.compiled_plan,
              &compiled, error, sizeof(error)),
          "provider canary residual plan loads");
    if (horn && compiled) {
        memset(error, 0, sizeof(error));
        CHECK(cetta_gslt_compiled_program_matches_source_v1(
                  compiled, horn, error, sizeof(error)),
              "provider canary residual plan matches its authored source");
        exercise_engine("Horn reference", query_horn, horn);
        exercise_engine("compiled worklist", query_compiled, compiled);
    }
    exercise_language(CETTA_GSLT_REALIZATION_HORN_REFERENCE);
    exercise_language(CETTA_GSLT_REALIZATION_COMPILED_WORKLIST);
    exercise_space_provider_backend(SPACE_ENGINE_NATIVE);
    exercise_finite_fact_provider(CETTA_GSLT_REALIZATION_HORN_REFERENCE);
    exercise_finite_fact_provider(CETTA_GSLT_REALIZATION_COMPILED_WORKLIST);
    exercise_finite_fact_rigid_index();
#if CETTA_BUILD_WITH_PATHMAP_SPACE
    exercise_space_provider_backend(SPACE_ENGINE_PATHMAP);
#else
    memset(error, 0, sizeof(error));
    CettaGsltSpaceFactProviderV1 *unavailable_pathmap =
        cetta_gslt_space_fact_provider_create_v1(
            SPACE_ENGINE_PATHMAP, "provider-external", 2u,
            "canary.completed-bag.v1", error, sizeof(error));
    CHECK(!unavailable_pathmap && error[0] != '\0',
          "non-bridge build fails closed instead of imitating PathMap");
    cetta_gslt_space_fact_provider_free_v1(unavailable_pathmap);
#endif

    ProviderContext duplicate_context = {0};
    CettaGsltProviderV1 duplicates[2] = {
        {
            .relation = "provider-external",
            .arity = 2u,
            .semantic_id = "canary.first.v1",
            .context = &duplicate_context,
            .query = answer_provider,
        },
        {
            .relation = "provider-external",
            .arity = 2u,
            .semantic_id = "canary.second.v1",
            .context = &duplicate_context,
            .query = answer_provider,
        },
    };
    CettaGsltProviderRegistryV1 invalid = {
        .providers = duplicates,
        .provider_count = 2u,
    };
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_provider_registry_validate_v1(
              &invalid, error, sizeof(error)) && error[0] != '\0',
          "provider registry rejects duplicate physical dispatch keys");

    cetta_gslt_compiled_program_free(compiled);
    cetta_gslt_horn_program_free(horn);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);

    if (failures != 0u) {
        fprintf(stderr, "%u/%u provider checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS: %u GSLT semantic-provider checks\n", checks);
    return 0;
}
