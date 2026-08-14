#include "generated/petta_typecheck_v2_fragment_provider_catalog_v1.generated.h"
#include "generated/petta_typecheck_v2_fragment_v1.generated.h"
#include "gslt_compiled_runtime.h"
#include "gslt_finite_fact_provider_v1.h"
#include "gslt_horn_runtime.h"
#include "gslt_language_runtime.h"
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
            fprintf(stderr, "FAIL: %s\\n", (label));                     \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static CettaGsltHornLimits qualification_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 100000u,
        .max_answers = 16u,
        .max_depth = 256u,
    };
}

static bool result_has_exact_answer(
    const CettaGsltHornResult *result, Atom *expected,
    size_t expected_count) {
    if (!result || result->outcome != CETTA_GSLT_HORN_COMPLETED ||
        result->answer_count != expected_count)
        return false;
    if (expected_count == 0u)
        return true;
    if (!expected || expected_count != 1u)
        return false;
    return atom_eq(result->answers[0], expected);
}

static void check_equivalent_query(
    const CettaGsltHornProgram *reference,
    const CettaGsltCompiledProgram *compiled,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers,
    const char *query_text, const char *expected_text,
    size_t expected_count, const char *label) {
    Atom *query = parse_one(queries, query_text);
    Atom *expected = expected_text ? parse_one(queries, expected_text) : NULL;
    CettaGsltHornResult reference_result = {0};
    CettaGsltHornResult compiled_result = {0};
    char reference_error[ERROR_CAP] = {0};
    char compiled_error[ERROR_CAP] = {0};
    bool reference_ran = query && (!expected_text || expected) &&
        cetta_gslt_horn_query_with_providers_v1(
            reference, providers, answers, query, qualification_limits(),
            &reference_result, reference_error, sizeof(reference_error));
    bool compiled_ran = query && (!expected_text || expected) &&
        cetta_gslt_compiled_query_with_providers_v1(
            compiled, providers, answers, query, qualification_limits(),
            &compiled_result, compiled_error, sizeof(compiled_error));
    bool expected_reference = reference_ran && result_has_exact_answer(
        &reference_result, expected, expected_count);
    bool expected_compiled = compiled_ran && result_has_exact_answer(
        &compiled_result, expected, expected_count);
    CHECK(expected_reference && expected_compiled, label);
    if (!expected_reference || !expected_compiled) {
        fprintf(stderr, "%s diagnostics: reference=%s compiled=%s\\n",
                label, reference_error, compiled_error);
    }
    cetta_gslt_horn_result_free(&reference_result);
    cetta_gslt_horn_result_free(&compiled_result);
}

static void check_admitted_entry(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    const CettaGsltProviderRegistryV1 *physical,
    Arena *queries, Arena *answers,
    const char *label) {
    Atom *query = parse_one(queries, "(GuardPasses VNum TNum)");
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && cetta_gslt_language_query_with_providers_v1(
        language, realization,
        &cetta_petta_typecheck_v2_fragment_provider_catalog_v1, physical,
        answers, query, qualification_limits(), &result, error, sizeof(error));
    CHECK(ran && result_has_exact_answer(&result, query, 1u), label);
    if (!ran)
        fprintf(stderr, "%s diagnostic: %s\\n", label, error);
    cetta_gslt_horn_result_free(&result);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    Arena facts;
    Arena queries;
    Arena answers;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    arena_init(&facts);
    arena_init(&queries);
    arena_init(&answers);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = &variables;

    CettaGsltFiniteFactProviderSetV1 *provider_set = NULL;
    CettaGsltAuthorizedProviderRegistryV1 authorized = {0};

    const CettaGsltProviderCatalogV1 *catalog =
        &cetta_petta_typecheck_v2_fragment_provider_catalog_v1;
    char error[ERROR_CAP] = {0};
    CHECK(cetta_gslt_provider_catalog_validate_v1(
              catalog, error, sizeof(error)) &&
              catalog->requirement_count == 5u,
          "the five PeTTa fragment provider obligations are authored");

    Atom *environment_facts[] = {
        parse_one(&facts, "(EnvDeclared AliasNum (TAlias TNum))"),
        parse_one(&facts, "(EnvDeclared Count (TNewtype TNum))"),
        parse_one(&facts,
                  "(EnvDeclaredList DeclaredCount "
                  " (DCons (Decl DeclaredCount TNum) DNil))"),
        parse_one(&facts, "(EnvNonNewtype PlainNominal)"),
        parse_one(&facts, "(KnownExpressionEffect Deterministic MDet)"),
        parse_one(&facts, "(KnownExpressionResultType KnownNumber TNum)"),
    };
    bool facts_valid = true;
    for (size_t index = 0u;
         index < sizeof(environment_facts) / sizeof(environment_facts[0]);
         index++) {
        if (!environment_facts[index])
            facts_valid = false;
    }
    CettaGsltFiniteFactSpanV1 span = {
        .rows = environment_facts,
        .row_count = facts_valid
            ? sizeof(environment_facts) / sizeof(environment_facts[0]) : 0u,
    };
    provider_set = facts_valid
        ? cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
              catalog->requirements, catalog->requirement_count,
              &span, 1u, error, sizeof(error))
        : NULL;
    CHECK(provider_set != NULL,
          "the authored provider contract admits a finite environment");
    if (!provider_set) {
        fprintf(stderr, "provider diagnostic: %s\\n", error);
        goto cleanup;
    }
    const CettaGsltProviderRegistryV1 *physical =
        cetta_gslt_finite_fact_provider_set_registry_v1(provider_set);

    memset(error, 0, sizeof(error));
    bool authorized_ok = cetta_gslt_provider_registry_authorize_v1(
        catalog, physical, &authorized, error, sizeof(error));
    CHECK(authorized_ok &&
              authorized.registry.provider_count == catalog->requirement_count,
          "the generated catalog authorizes every finite fact provider");
    if (!authorized_ok) {
        fprintf(stderr, "authorization diagnostic: %s\\n", error);
        goto cleanup;
    }

    CettaGsltProviderV1 *wrong_providers = calloc(
        physical->provider_count, sizeof(*wrong_providers));
    bool copied = wrong_providers != NULL;
    if (copied)
        memcpy(wrong_providers, physical->providers,
               physical->provider_count * sizeof(*wrong_providers));
    if (copied)
        wrong_providers[0].semantic_id = "wrong.petta.provider.v1";
    CettaGsltProviderRegistryV1 wrong_registry = {
        .providers = wrong_providers,
        .provider_count = physical->provider_count,
    };
    CettaGsltAuthorizedProviderRegistryV1 rejected = {0};
    memset(error, 0, sizeof(error));
    CHECK(copied && !cetta_gslt_provider_registry_authorize_v1(
                       catalog, &wrong_registry, &rejected,
                       error, sizeof(error)),
          "a provider with the wrong declared meaning is rejected");
    cetta_gslt_authorized_provider_registry_free_v1(&rejected);
    free(wrong_providers);

    const char *paths[] = {
        "langdef/petta/generated/typecheck_v2_guard_v1.metta",
        "langdef/petta/generated/typecheck_v2_boundary_core_v1.metta",
    };
    CettaGsltHornProgram *reference = NULL;
    CettaGsltCompiledProgram *compiled = NULL;
    memset(error, 0, sizeof(error));
    bool source_loaded = cetta_gslt_horn_program_load_paths(
        paths, sizeof(paths) / sizeof(paths[0]),
        &reference, error, sizeof(error));
    bool compiled_loaded = source_loaded && cetta_gslt_compiled_program_load_v1(
        &cetta_petta_typecheck_v2_fragment_v1.compiled_plan,
        &compiled, error, sizeof(error));
    bool exact_plan = compiled_loaded &&
        cetta_gslt_compiled_program_matches_source_v1(
            compiled, reference, error, sizeof(error));
    CHECK(source_loaded && compiled_loaded && exact_plan,
          "generated PeTTa fragment plan exactly matches its authored rules");
    if (!source_loaded || !compiled_loaded || !exact_plan) {
        fprintf(stderr, "compiled-plan diagnostic: %s\\n", error);
        cetta_gslt_compiled_program_free(compiled);
        cetta_gslt_horn_program_free(reference);
        goto cleanup;
    }

    check_equivalent_query(
        reference, compiled, &authorized.registry, &queries, &answers,
        "(Consistent (TNominal AliasNum) TNum $edge)",
        "(Consistent (TNominal AliasNum) TNum EdgeStructural)", 1u,
        "alias compatibility agrees across generated and reference engines");
    check_equivalent_query(
        reference, compiled, &authorized.registry, &queries, &answers,
        "(DynamicMayFlowTo (TNominal PlainNominal))",
        "(DynamicMayFlowTo (TNominal PlainNominal))", 1u,
        "open nominal flow agrees across generated and reference engines");
    check_equivalent_query(
        reference, compiled, &authorized.registry, &queries, &answers,
        "(ExpressionEffect Deterministic $mode)",
        "(ExpressionEffect Deterministic MDet)", 1u,
        "known expression cardinality agrees across generated and reference engines");
    check_equivalent_query(
        reference, compiled, &authorized.registry, &queries, &answers,
        "(ExpressionResultType KnownNumber $type)",
        "(ExpressionResultType KnownNumber TNum)", 1u,
        "known expression result typing agrees across generated and reference engines");
    check_equivalent_query(
        reference, compiled, &authorized.registry, &queries, &answers,
        "(DefiniteMismatch (FS TNum) (VSym DeclaredCount) TStr)",
        "(DefiniteMismatch (FS TNum) (VSym DeclaredCount) TStr)", 1u,
        "declared-list mismatch agrees across generated and reference engines");

    CettaGsltLanguage *reference_language = NULL;
    CettaGsltLanguage *compiled_language = NULL;
    memset(error, 0, sizeof(error));
    bool reference_language_loaded =
        cetta_gslt_language_load_embedded_for_realization(
            &cetta_petta_typecheck_v2_fragment_v1,
            CETTA_GSLT_REALIZATION_HORN_REFERENCE,
            &reference_language, error, sizeof(error));
    bool compiled_language_loaded = reference_language_loaded &&
        cetta_gslt_language_load_embedded_for_realization(
            &cetta_petta_typecheck_v2_fragment_v1,
            CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &compiled_language, error, sizeof(error));
    CHECK(reference_language_loaded && compiled_language_loaded &&
              cetta_gslt_language_semantic_rule_count(reference_language) == 176u &&
              cetta_gslt_language_semantic_rule_count(compiled_language) == 176u,
          "the isolated profile loads all 176 generated fragment rules");
    if (reference_language_loaded && compiled_language_loaded) {
        check_admitted_entry(
            reference_language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
            physical, &queries, &answers,
            "the isolated fragment admits its declared entry in the reference engine");
        check_admitted_entry(
            compiled_language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            physical, &queries, &answers,
            "the isolated fragment admits its declared entry in the compiled engine");
    } else {
        fprintf(stderr, "embedded-language diagnostic: %s\\n", error);
    }
    cetta_gslt_language_free(compiled_language);
    cetta_gslt_language_free(reference_language);
    cetta_gslt_compiled_program_free(compiled);
    cetta_gslt_horn_program_free(reference);

cleanup:
    cetta_gslt_authorized_provider_registry_free_v1(&authorized);
    cetta_gslt_finite_fact_provider_set_free_v1(provider_set);
    g_var_intern = NULL;
    g_symbols = NULL;
    arena_free(&answers);
    arena_free(&queries);
    arena_free(&facts);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    if (failures != 0u) {
        fprintf(stderr,
                "PettaTypecheckV2FragmentRuntimeSummary checks=%u failures=%u\\n",
                checks, failures);
        return 1;
    }
    printf("(PettaTypecheckV2FragmentRuntimeSummary checks=%u failures=0)\\n",
           checks);
    return 0;
}
