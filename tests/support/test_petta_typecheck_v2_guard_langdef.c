#include "atom.h"
#include "generated/petta_typecheck_v2_source_binding_v1.generated.h"
#include "gslt_finite_fact_provider_v1.h"
#include "gslt_horn_runtime.h"
#include "parser.h"
#include "petta_analysis.h"
#include "petta_typecheck.h"
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

static void check_query(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers,
    const char *query_text, const char *expected_text,
    size_t expected_count, bool exact_count, const char *label) {
    Atom *query = parse_one(queries, query_text);
    Atom *expected = expected_text
        ? parse_one(queries, expected_text) : NULL;
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && (!expected_text || expected) &&
        cetta_gslt_horn_query_with_providers_v1(
            program, providers, answers, query, qualification_limits(),
            &result, error, sizeof error);
    CHECK(ran, label);
    if (!ran) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
        cetta_gslt_horn_result_free(&result);
        return;
    }
    bool exact = result.outcome == CETTA_GSLT_HORN_COMPLETED &&
                 (exact_count
                      ? result.answer_count == expected_count
                      : result.answer_count >= expected_count);
    if (exact && expected) {
        bool found = false;
        for (size_t index = 0u; index < result.answer_count; index++) {
            if (atom_eq(result.answers[index], expected)) {
                found = true;
                break;
            }
        }
        exact = found;
    }
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr, "%s produced %zu answers with outcome %u\n",
                label, result.answer_count, (unsigned)result.outcome);
    }
    cetta_gslt_horn_result_free(&result);
}

static void check_canonical_arrow_mode_relation(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers) {
    const char *mode_terms[] = {
        "MPlain",
        "MDet",
        "MSemidet",
        "MNondet",
        "(MEffect EffectVar)",
    };
    const PettaAnalysisArrowMode modes[] = {
        PETTA_ANALYSIS_ARROW_MODE_PLAIN,
        PETTA_ANALYSIS_ARROW_MODE_DETERMINISTIC,
        PETTA_ANALYSIS_ARROW_MODE_SEMIDETERMINISTIC,
        PETTA_ANALYSIS_ARROW_MODE_NONDETERMINISTIC,
        PETTA_ANALYSIS_ARROW_MODE_EFFECT,
    };
    for (size_t actual = 0u; actual < 5u; actual++) {
        for (size_t required = 0u; required < 5u; required++) {
            char query[256];
            char label[256];
            int query_length = snprintf(
                query, sizeof(query), "(ModeFits %s %s)",
                mode_terms[actual], mode_terms[required]);
            int label_length = snprintf(
                label, sizeof(label),
                "canonical arrow mode table cell %zu/%zu",
                actual, required);
            bool formatted = query_length >= 0 &&
                (size_t)query_length < sizeof(query) && label_length >= 0 &&
                (size_t)label_length < sizeof(label);
            CHECK(formatted, "canonical arrow mode table formats");
            if (!formatted)
                continue;
            bool fits = petta_analysis_arrow_mode_fits(
                modes[actual], modes[required]);
            check_query(
                program, providers, queries, answers, query,
                fits ? query : NULL, fits ? 1u : 0u, true, label);
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s PETTA_TYPECHECK_V2_GUARD_LANGDEF\n",
                argv[0]);
        return 2;
    }

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

    const CettaNikDirectSourceBindingV1 *binding =
        &petta_typecheck_v2_source_binding_v1;
    CHECK(cetta_nik_direct_source_binding_v1_is_valid(binding) &&
              binding->authority ==
                  &petta_typecheck_v2_direct_authority_v1 &&
              binding->coverage ==
                  CETTA_NIK_DIRECT_SOURCE_AUTHORED_FRAGMENT,
          "PeTTa guard remains an honestly partial direct source binding");

    const char *paths[] = {argv[1]};
    CettaGsltHornProgram *program = NULL;
    char error[ERROR_CAP] = {0};
    bool loaded = cetta_gslt_horn_program_load_paths(
        paths, 1u, &program, error, sizeof error);
    CHECK(loaded, "PeTTa typecheck-v2 guard langdef loads");
    if (!loaded) {
        fprintf(stderr, "PeTTa guard load diagnostic: %s\n", error);
        goto cleanup;
    }
    CHECK(cetta_gslt_horn_program_rule_count(program) == 116u,
          "all 116 authored PeTTa guard rules are executable");

    Atom *environment_facts[] = {
        parse_one(&facts, "(EnvDeclared AliasNum (TAlias TNum))"),
        parse_one(&facts, "(EnvDeclared Count (TNewtype TNum))"),
        parse_one(&facts, "(EnvDeclared OtherCount (TNewtype TNum))"),
        parse_one(&facts, "(EnvDeclared Proof (TNewtype TUndefined))"),
        parse_one(&facts, "(EnvNonNewtype PlainNominal)"),
        parse_one(&facts, "(KnownExpressionEffect Deterministic MDet)"),
        parse_one(&facts,
                  "(KnownExpressionEffect Semideterministic MSemidet)"),
        parse_one(&facts,
                  "(KnownExpressionEffect Nondeterministic MNondet)"),
        parse_one(&facts,
                  "(KnownExpressionResultType KnownNumber TNum)"),
        parse_one(&facts,
                  "(KnownExpressionResultType OpenGenerator TUndefined)"),
        parse_one(&facts,
                  "(KnownExpressionResultType InitialNumber TNum)"),
    };
    bool facts_valid = true;
    for (size_t index = 0u;
         index < sizeof environment_facts / sizeof environment_facts[0];
         index++) {
        if (!environment_facts[index])
            facts_valid = false;
    }
    CettaGsltProviderRequirementV1 requirements[] = {
        {
            .relation = "EnvDeclared",
            .arity = 2u,
            .semantic_id = "petta.typecheck-v2.test-declarations.v1",
        },
        {
            .relation = "EnvNonNewtype",
            .arity = 1u,
            .semantic_id = "petta.typecheck-v2.test-non-newtypes.v1",
        },
        {
            .relation = "KnownExpressionEffect",
            .arity = 2u,
            .semantic_id = "petta.typecheck-v2.test-expression-effects.v1",
        },
        {
            .relation = "KnownExpressionResultType",
            .arity = 2u,
            .semantic_id = "petta.typecheck-v2.test-expression-result-types.v1",
        },
    };
    CettaGsltFiniteFactSpanV1 span = {
        .rows = environment_facts,
        .row_count = facts_valid
            ? sizeof environment_facts / sizeof environment_facts[0] : 0u,
    };
    memset(error, 0, sizeof error);
    CettaGsltFiniteFactProviderSetV1 *environment = facts_valid
        ? cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
              requirements,
              sizeof requirements / sizeof requirements[0],
              &span, 1u, error, sizeof error)
        : NULL;
    CHECK(environment != NULL,
          "PeTTa guard qualification environment is admitted");
    if (!environment) {
        fprintf(stderr, "PeTTa guard environment diagnostic: %s\n", error);
        goto cleanup;
    }
    const CettaGsltProviderRegistryV1 *providers =
        cetta_gslt_finite_fact_provider_set_registry_v1(environment);

    check_canonical_arrow_mode_relation(
        program, providers, &queries, &answers);

    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TUndefined $edge)",
        "(Consistent (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TUndefined EdgeStructural)",
        2u, true, "every actual-union member may fit the gradual type");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TNum $edge)",
        NULL, 0u, true,
        "one matching actual-union member cannot hide a mismatch");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TNum "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) $edge)",
        "(Consistent TNum "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) EdgeStructural)",
        1u, true, "one required-union member may witness compatibility");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TBool "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) $edge)",
        NULL, 0u, true,
        "a required union with no compatible member is rejected");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal AliasNum) TNum $edge)",
        "(Consistent (TNominal AliasNum) TNum EdgeStructural)",
        1u, true, "an actual alias is transparent to compatibility");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal AliasNum) TStr $edge)",
        NULL, 0u, true,
        "an actual alias cannot conceal an incompatible representation");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TNum (TNominal AliasNum) $edge)",
        "(Consistent TNum (TNominal AliasNum) EdgeStructural)",
        1u, true, "a required alias is transparent to compatibility");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TStr (TNominal AliasNum) $edge)",
        NULL, 0u, true,
        "a required alias cannot conceal an incompatible representation");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TUndefined TNum $edge)",
        "(Consistent TUndefined TNum EdgeDynamic)",
        1u, true, "an unknown actual flows to a structural requirement");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TUndefined (TNominal PlainNominal) $edge)",
        "(Consistent TUndefined (TNominal PlainNominal) EdgeDynamic)",
        1u, true,
        "an admitted non-newtype nominal accepts an unknown actual");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TUndefined (TNominal Count) $edge)",
        NULL, 0u, true,
        "an unknown actual cannot introduce a newtype brand");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Count) TUndefined $edge)",
        "(Consistent (TNominal Count) TUndefined EdgeDynamic)",
        1u, false, "a newtype may erase into an unknown requirement");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Count) (TNominal Count) $edge)",
        "(Consistent (TNominal Count) (TNominal Count) EdgeExact)",
        1u, false, "a newtype is compatible with itself");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Count) (TNominal OtherCount) $edge)",
        NULL, 0u, true,
        "distinct newtypes remain incompatible despite equal representations");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Count) TNum $edge)",
        "(Consistent (TNominal Count) TNum EdgeStructural)",
        1u, true, "a concrete newtype eliminates through its representation");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Count) TStr $edge)",
        NULL, 0u, true,
        "a newtype cannot eliminate into an incompatible representation");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Proof) TNum $edge)",
        NULL, 0u, true,
        "a wildcard newtype representation grants no concrete compatibility");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent TNum (TNominal Count) $edge)",
        NULL, 0u, true,
        "a representation does not implicitly introduce its newtype");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TNominal Count) "
        "  (TUnion (TTCons TStr (TTCons (TNominal Count) TTNil))) $edge)",
        "(Consistent (TNominal Count) "
        "  (TUnion (TTCons TStr (TTCons (TNominal Count) TTNil))) "
        "  EdgeStructural)",
        1u, true, "a newtype may satisfy a required union member");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TNum $edge)",
        "(MayOverlap (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TNum OverlapUnionLeft)",
        1u, false,
        "an actual-union ascription has an existential overlap witness");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TNum $edge)",
        "(MayOverlap (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  TNum OverlapReverse)",
        1u, false,
        "reverse compatibility remains an explicit overlap witness");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap TNum "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) $edge)",
        "(MayOverlap TNum "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) OverlapUnionRight)",
        1u, false,
        "a required-union ascription has an existential overlap witness");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap TNum "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) $edge)",
        "(MayOverlap TNum "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) OverlapForward)",
        1u, false,
        "forward compatibility remains an explicit overlap witness");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap TBool "
        "  (TUnion (TTCons TStr (TTCons TNum TTNil))) $edge)",
        NULL, 0u, true,
        "a disjoint ascription has no overlap witness");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap (TNominal AliasNum) TNum $edge)",
        "(MayOverlap (TNominal AliasNum) TNum OverlapAliasLeft)",
        1u, false, "an actual alias is transparent to ascription overlap");
    check_query(
        program, providers, &queries, &answers,
        "(MayOverlap TNum (TNominal AliasNum) $edge)",
        "(MayOverlap TNum (TNominal AliasNum) OverlapAliasRight)",
        1u, false, "a required alias is transparent to ascription overlap");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MDet MDet)", "(ModeFits MDet MDet)",
        1u, true, "a deterministic arrow fits a deterministic slot");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MDet MSemidet)", "(ModeFits MDet MSemidet)",
        1u, true, "a deterministic arrow fits a semideterministic slot");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MSemidet MSemidet)",
        "(ModeFits MSemidet MSemidet)",
        1u, true,
        "a semideterministic arrow fits a semideterministic slot");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MNondet MNondet)", "(ModeFits MNondet MNondet)",
        1u, true, "a nondeterministic arrow fits a nondeterministic slot");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MNondet MPlain)", "(ModeFits MNondet MPlain)",
        1u, true, "a plain arrow slot makes no result-count commitment");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MDet (MEffect EffectVar))",
        "(ModeFits MDet (MEffect EffectVar))",
        1u, true, "an effect-polymorphic slot accepts an actual mode");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MSemidet MDet)", NULL, 0u, true,
        "a semideterministic arrow cannot satisfy a deterministic slot");
    check_query(
        program, providers, &queries, &answers,
        "(ModeFits MNondet MSemidet)", NULL, 0u, true,
        "a nondeterministic arrow cannot satisfy a semideterministic slot");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionEffect (ECollapse Generator) MDet)",
        "(ExpressionEffect (ECollapse Generator) MDet)",
        1u, true,
        "collapse collects any generator into one result");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionResultType (ECollapse KnownNumber) (TList TNum))",
        "(ExpressionResultType (ECollapse KnownNumber) (TList TNum))",
        1u, true,
        "collapse preserves a known element type beneath its list result");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionResultType (ECollapse OpenGenerator) "
        "  (TList TUndefined))",
        "(ExpressionResultType (ECollapse OpenGenerator) "
        "  (TList TUndefined))",
        1u, true,
        "collapse retains its list result when the element remains unknown");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionResultType (EFoldall Add EEmpty InitialNumber) TNum)",
        "(ExpressionResultType (EFoldall Add EEmpty InitialNumber) TNum)",
        1u, true,
        "an empty foldall returns the initializer result type");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionEffect (EFoldall Add Generator Zero) MDet)",
        "(ExpressionEffect (EFoldall Add Generator Zero) MDet)",
        1u, true,
        "v2 consumes foldall generator multiplicity into one result");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionEffect (EOnce Deterministic) MDet)",
        "(ExpressionEffect (EOnce Deterministic) MDet)",
        1u, true,
        "once preserves a deterministic inner expression");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionEffect (EOnce Semideterministic) MSemidet)",
        "(ExpressionEffect (EOnce Semideterministic) MSemidet)",
        1u, true,
        "once preserves a semideterministic inner expression");
    check_query(
        program, providers, &queries, &answers,
        "(ExpressionEffect (EOnce Nondeterministic) MSemidet)",
        "(ExpressionEffect (EOnce Nondeterministic) MSemidet)",
        1u, true,
        "once caps nondeterminism at one optional result");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TArrow MDet (TTCons TNum TTNil) TNum) "
        "  (TArrow MSemidet (TTCons TNum TTNil) TNum) $edge)",
        "(Consistent (TArrow MDet (TTCons TNum TTNil) TNum) "
        "  (TArrow MSemidet (TTCons TNum TTNil) TNum) EdgeStructural)",
        1u, true, "arrow consistency consumes the mode order");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TArrow MSemidet (TTCons TNum TTNil) TNum) "
        "  (TArrow MDet (TTCons TNum TTNil) TNum) $edge)",
        NULL, 0u, true,
        "arrow consistency rejects an incompatible mode direction");
    check_query(
        program, providers, &queries, &answers,
        "(Consistent (TArrow MDet (TTCons TNum TTNil) TStr) "
        "  (TArrow MDet (TTCons TNum TTNil) TNum) $edge)",
        NULL, 0u, true,
        "matching arrow modes cannot conceal an incompatible result");

    cetta_gslt_finite_fact_provider_set_free_v1(environment);

cleanup:
    cetta_gslt_horn_program_free(program);
    g_var_intern = NULL;
    g_symbols = NULL;
    arena_free(&answers);
    arena_free(&queries);
    arena_free(&facts);
    var_intern_free(&variables);
    symbol_table_free(&symbols);

    if (failures != 0u) {
        fprintf(stderr,
                "PettaTypecheckV2GuardLangdefSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(PettaTypecheckV2GuardLangdefSummary checks=%u failures=0)\n",
           checks);
    return 0;
}
