#include "gslt_abt_provider_v1.h"
#include "gslt_provider_runtime.h"
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

static const CettaGsltAbtProviderSchemaV1 SCHEMA = {
    .field_depth_relation = "qabt-field-depth",
    .field_depth_semantic_id = "abt.default-signature.field-depth.v1",
    .transport_relation = "qabt-transport",
    .transport_semantic_id = "abt.default-signature.transport.v1",
};

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static void expect_query(
    const CettaGsltProviderRegistryV1 *registry,
    const char *query_text,
    const char *expected_text,
    uint64_t answer_limit,
    CettaGsltProviderOutcomeV1 expected_outcome,
    const char *label) {
    Arena query_arena;
    Arena answer_arena;
    Arena expected_arena;
    arena_init(&query_arena);
    arena_init(&answer_arena);
    arena_init(&expected_arena);
    Atom *query = parse_one(&query_arena, query_text);
    Atom *expected = expected_text
        ? parse_one(&expected_arena, expected_text) : NULL;
    const CettaGsltProviderV1 *provider =
        cetta_gslt_provider_find_v1(registry, query);
    CettaGsltProviderAnswersV1 answers = {0};
    char error[ERROR_CAP] = {0};
    CettaGsltProviderOutcomeV1 outcome = provider
        ? provider->query(
              provider->context, &answer_arena, query, answer_limit,
              &answers, error, sizeof(error))
        : CETTA_GSLT_PROVIDER_FAULT;
    bool answer_ok = expected
        ? answers.answer_count == 1u &&
              atom_eq(answers.answers[0], expected)
        : answers.answer_count == 0u;
    if (!query || !provider || outcome != expected_outcome || !answer_ok)
        fprintf(
            stderr,
            "DETAIL: %s: query=%u provider=%u outcome=%u answers=%zu error=%s\n",
            label, query ? 1u : 0u, provider ? 1u : 0u,
            (unsigned)outcome, answers.answer_count, error);
    CHECK(query && provider && outcome == expected_outcome && answer_ok, label);
    cetta_gslt_provider_answers_free_v1(&answers);
    arena_free(&expected_arena);
    arena_free(&answer_arena);
    arena_free(&query_arena);
}

static CettaGsltProviderOutcomeV1 empty_provider(
    void *context,
    Arena *arena,
    const Atom *goal,
    uint64_t answer_limit,
    CettaGsltProviderAnswersV1 *answers,
    char *error,
    size_t error_size) {
    (void)context;
    (void)arena;
    (void)goal;
    (void)answer_limit;
    (void)answers;
    (void)error;
    (void)error_size;
    return CETTA_GSLT_PROVIDER_COMPLETED;
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

    char error[ERROR_CAP] = {0};
    Arena extension_arena;
    arena_init(&extension_arena);
    Atom *extension = parse_one(
        &extension_arena,
        "(AbtSignatures "
        "  (AbtSignature Forall (Fields domain body) (Bind1 body)))");
    CettaGsltAbtProviderV1 *provider =
        cetta_gslt_abt_provider_create_v1(
            &SCHEMA, extension, error, sizeof(error));
    CHECK(provider != NULL, "creates declaration-driven ABT provider");
    const CettaGsltProviderRegistryV1 *registry =
        cetta_gslt_abt_provider_registry_v1(provider);
    CHECK(registry && registry->provider_count == 2u,
          "registers exactly field-depth and transport leaves");

    expect_query(
        registry,
        "(qabt-field-depth "
        "  (q-sym (q-str \"Lam\")) "
        "  (q-cons (q-sym (q-str \"domain\")) "
        "    (q-cons (q-sym (q-str \"body\")) q-nil)) "
        "  (q-succ q-zero) $depth)",
        "(qabt-field-depth "
        "  (q-sym (q-str \"Lam\")) "
        "  (q-cons (q-sym (q-str \"domain\")) "
        "    (q-cons (q-sym (q-str \"body\")) q-nil)) "
        "  (q-succ q-zero) (q-succ q-zero))",
        1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "default Lam body crosses one binder");
    expect_query(
        registry,
        "(qabt-field-depth "
        "  (q-sym (q-str \"Forall\")) "
        "  (q-cons (q-sym (q-str \"domain\")) "
        "    (q-cons (q-sym (q-str \"body\")) q-nil)) "
        "  (q-succ q-zero) $depth)",
        "(qabt-field-depth "
        "  (q-sym (q-str \"Forall\")) "
        "  (q-cons (q-sym (q-str \"domain\")) "
        "    (q-cons (q-sym (q-str \"body\")) q-nil)) "
        "  (q-succ q-zero) (q-succ q-zero))",
        1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "package signature extends field-depth semantics");
    expect_query(
        registry,
        "(qabt-field-depth "
        "  (q-sym (q-str \"Dynamic\")) "
        "  (q-cons (q-sym (q-str \"x\")) q-nil) q-zero $depth)",
        "(qabt-field-depth "
        "  (q-sym (q-str \"Dynamic\")) "
        "  (q-cons (q-sym (q-str \"x\")) q-nil) q-zero q-zero)",
        1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "undeclared constructors are structurally nonbinding");
    expect_query(
        registry,
        "(qabt-field-depth "
        "  (q-sym (q-str \"Lam\")) q-nil q-zero $depth)",
        NULL, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "out-of-range field requests fail closed");

    const char *index0 =
        "(q-app (q-sym (q-str \"idx\")) (q-cons (q-int 0) q-nil))";
    const char *index1 =
        "(q-app (q-sym (q-str \"idx\")) (q-cons (q-int 1) q-nil))";
    const char *index2 =
        "(q-app (q-sym (q-str \"idx\")) (q-cons (q-int 2) q-nil))";
    char query[8192];
    char expected[8192];

    (void)snprintf(
        query, sizeof(query),
        "(qabt-transport q-zero (q-succ q-zero) %s $result)", index0);
    (void)snprintf(
        expected, sizeof(expected),
        "(qabt-transport q-zero (q-succ q-zero) %s %s)", index0, index1);
    expect_query(
        registry, query, expected, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "transport weakens a loose index");

    (void)snprintf(
        query, sizeof(query),
        "(qabt-transport (q-succ q-zero) q-zero %s $result)", index1);
    (void)snprintf(
        expected, sizeof(expected),
        "(qabt-transport (q-succ q-zero) q-zero %s %s)", index1, index0);
    expect_query(
        registry, query, expected, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "transport lowers an index that survives support removal");

    (void)snprintf(
        query, sizeof(query),
        "(qabt-transport (q-succ q-zero) q-zero %s $result)", index0);
    expect_query(
        registry, query, NULL, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "transport rejects a reference to the removed support");

    char lam_index1[2048];
    char lam_index2[2048];
    (void)snprintf(
        lam_index1, sizeof(lam_index1),
        "(q-app (q-sym (q-str \"Lam\")) "
        " (q-cons (q-sym (q-str \"A\")) (q-cons %s q-nil)))",
        index1);
    (void)snprintf(
        lam_index2, sizeof(lam_index2),
        "(q-app (q-sym (q-str \"Lam\")) "
        " (q-cons (q-sym (q-str \"A\")) (q-cons %s q-nil)))",
        index2);
    (void)snprintf(
        query, sizeof(query),
        "(qabt-transport (q-succ q-zero) q-zero %s $result)", lam_index1);
    expect_query(
        registry, query, NULL, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "transport rejects capture beneath a constructor binder");
    (void)snprintf(
        query, sizeof(query),
        "(qabt-transport (q-succ q-zero) q-zero %s $result)", lam_index2);
    (void)snprintf(
        expected, sizeof(expected),
        "(qabt-transport (q-succ q-zero) q-zero %s %s)",
        lam_index2, lam_index1);
    expect_query(
        registry, query, expected, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "transport lowers a surviving loose index beneath a binder");

    expect_query(
        registry,
        "(qabt-transport q-zero q-zero (q-sym raw) $result)",
        NULL, 1u, CETTA_GSLT_PROVIDER_COMPLETED,
        "transport rejects noncanonical quoted syntax");
    expect_query(
        registry,
        "(qabt-transport q-zero q-zero "
        " (q-app (q-sym (q-str \"idx\")) (q-cons (q-int 0) q-nil)) "
        " $result)",
        NULL, 0u, CETTA_GSLT_PROVIDER_ANSWER_LIMIT,
        "transport publishes no partial answer beyond its limit");

    CettaGsltProviderV1 extra = {
        .relation = "extra-provider",
        .arity = 1u,
        .semantic_id = "extra-provider.v1",
        .context = NULL,
        .query = empty_provider,
    };
    CettaGsltProviderRegistryV1 extra_registry = {
        .providers = &extra,
        .provider_count = 1u,
    };
    const CettaGsltProviderRegistryV1 *inputs[] = {registry, &extra_registry};
    CettaGsltOwnedProviderRegistryV1 combined = {0};
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_provider_registry_union_v1(
              inputs, 2u, &combined, error, sizeof(error)) &&
              combined.registry.provider_count == 3u,
          "unions independently owned provider libraries");
    Arena extra_query_arena;
    arena_init(&extra_query_arena);
    Atom *extra_query = parse_one(
        &extra_query_arena, "(extra-provider $answer)");
    CHECK(cetta_gslt_provider_find_v1(
              &combined.registry, extra_query) == &combined.storage[2],
          "combined registry dispatches every component");
    cetta_gslt_owned_provider_registry_free_v1(&combined);
    arena_free(&extra_query_arena);

    const CettaGsltProviderRegistryV1 *duplicates[] = {registry, registry};
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_provider_registry_union_v1(
              duplicates, 2u, &combined, error, sizeof(error)) &&
              combined.storage == NULL && error[0] != '\0',
          "provider union rejects duplicate semantic authority");

    CettaGsltAbtProviderSchemaV1 invalid = SCHEMA;
    invalid.transport_relation = invalid.field_depth_relation;
    memset(error, 0, sizeof(error));
    CettaGsltAbtProviderV1 *bad =
        cetta_gslt_abt_provider_create_v1(
            &invalid, NULL, error, sizeof(error));
    CHECK(!bad && error[0] != '\0',
          "ABT provider rejects ambiguous dispatch schemas");

    cetta_gslt_abt_provider_free_v1(provider);
    arena_free(&extension_arena);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    if (failures != 0u) {
        fprintf(stderr, "%u/%u ABT provider checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS: %u support-indexed ABT provider checks\n", checks);
    return 0;
}
