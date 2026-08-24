#include "atom.h"
#include "generated/petta_typecheck_v3_core_provider_catalog_v1.generated.h"
#include "generated/petta_typecheck_v3_core_v1.generated.h"
#include "gslt_compiled_runtime.h"
#include "gslt_finite_fact_provider_v1.h"
#include "gslt_horn_runtime.h"
#include "gslt_language_runtime.h"
#include "parser.h"
#include "petta_type_fact_provider_v1.h"
#include "petta_typecheck_v3.h"
#include "petta_typecheck_v3_decision_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERROR_CAP = 1024 };

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                           \
    do {                                                                  \
        checks++;                                                         \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL: %s\n", (label));                     \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static bool head_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems && atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(atom->expr.elems[0]), name) == 0;
}

static CettaGsltHornLimits qualification_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 100000u,
        .max_answers = 16u,
        .max_depth = 256u,
    };
}

static bool install_live_form(
    PettaProgram *program, Space *space, Arena *source, const char *text) {
    Atom *form = parse_one(source, text);
    if (!form)
        return false;
    CettaCount before = space_length64(space);
    space_add(space, form);
    if (space_length64(space) != before + 1u)
        return false;
    Atom *stored = space_get_at64(space, before);
    return stored && petta_program_note_add(program, space, stored, NULL);
}

static bool result_has_exact_answer(
    const CettaGsltHornResult *result, Atom *expected,
    size_t expected_count) {
    if (!result || result->outcome != CETTA_GSLT_HORN_COMPLETED ||
        result->answer_count != expected_count) {
        return false;
    }
    if (expected_count == 0u)
        return true;
    if (!expected || expected_count != 1u)
        return false;
    return atom_eq(result->answers[0], expected);
}

static void check_query(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers,
    const char *query_text, const char *expected_text,
    size_t expected_count, const char *label) {
    Atom *query = parse_one(queries, query_text);
    Atom *expected = expected_text ? parse_one(queries, expected_text) : NULL;
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
    bool exact = result_has_exact_answer(&result, expected, expected_count);
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr, "%s produced %zu answers with outcome %u\n",
                label, result.answer_count, (unsigned)result.outcome);
    }
    cetta_gslt_horn_result_free(&result);
}

static void check_ground_query(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers,
    const char *query_text, bool expected, const char *label) {
    Atom *query = parse_one(queries, query_text);
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && cetta_gslt_horn_query_with_providers_v1(
        program, providers, answers, query, qualification_limits(),
        &result, error, sizeof error);
    CHECK(ran, label);
    if (!ran) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
        cetta_gslt_horn_result_free(&result);
        return;
    }
    bool exact = result.outcome == CETTA_GSLT_HORN_COMPLETED &&
        ((result.answer_count > 0u) == expected);
    for (size_t index = 0u; exact && index < result.answer_count; index++)
        exact = atom_eq(result.answers[index], query);
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr, "%s produced %zu answers with outcome %u\n",
                label, result.answer_count, (unsigned)result.outcome);
    }
    cetta_gslt_horn_result_free(&result);
}

static void check_evidence_decision(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries,
    const char *evidence_text,
    const char *expected_text,
    const char *demand_text,
    CettaGsltHornLimits limits,
    CettaNikOutcomeV1 expected_outcome,
    CettaPettaV3EvidenceBoundaryV1 expected_boundary,
    CettaPettaV3SeamKindV1 expected_seam,
    bool expected_license,
    const char *expected_relation,
    const char *label) {
    Atom *evidence = parse_one(queries, evidence_text);
    Atom *expected = parse_one(queries, expected_text);
    Atom *demand = parse_one(queries, demand_text);
    CettaPettaV3EvidenceDecisionV1 decision = {0};
    char error[ERROR_CAP] = {0};
    bool decided = evidence && expected && demand &&
        cetta_petta_typecheck_v3_decide_evidence_v1(
            program, providers, evidence, expected, demand, limits,
            &decision, error, sizeof error);
    bool exact = decided && decision.outcome == expected_outcome &&
        decision.boundary == expected_boundary &&
        decision.seam_kind == expected_seam &&
        decision.optimization_license.issued == expected_license &&
        decision.relation &&
        strcmp(decision.relation, expected_relation) == 0;
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr,
                "%s diagnostic: %s; outcome=%u boundary=%u relation=%s "
                "search-outcome=%u\n",
                label, error, (unsigned)decision.outcome,
                (unsigned)decision.boundary,
                decision.relation ? decision.relation : "<none>",
                (unsigned)decision.search_outcome);
    }
}

static void check_named_definition_file(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderCatalogV1 *catalog,
    const char *path,
    const char *definition_name,
    CettaNikOutcomeV1 expected_outcome,
    CettaPettaV3EvidenceBoundaryV1 expected_boundary,
    const char *label) {
    Arena source;
    arena_init(&source);
    Atom **forms = NULL;
    int form_count = parse_metta_file(path, &source, &forms);
    PettaProgram *petta = petta_program_new();
    Space space;
    bool space_initialized = false;
    CettaPettaTypeFactProviderV1 *provider = NULL;
    CettaGsltAuthorizedProviderRegistryV1 authorized = {0};
    Atom *subject = NULL;
    Atom *lhs = NULL;
    Atom *body = NULL;
    char error[ERROR_CAP] = {0};
    bool ready = form_count >= 0 && petta && petta_program_enable_analysis(petta);
    if (ready) {
        space_init(&space);
        space_initialized = true;
        for (int index = 0; index < form_count; index++) {
            Atom *form = forms[index];
            CettaCount before = space_length64(&space);
            space_add(&space, form);
            Atom *stored = space_length64(&space) == before + 1u
                ? space_get_at64(&space, before) : NULL;
            if (!stored || !petta_program_note_add(petta, &space, stored, NULL)) {
                ready = false;
                break;
            }
            if ((!subject || definition_name) &&
                head_is(form, "=") && form->expr.len == 3u &&
                form->expr.elems[1]->kind == ATOM_EXPR &&
                form->expr.elems[1]->expr.len > 0u &&
                form->expr.elems[1]->expr.elems[0]->kind == ATOM_SYMBOL) {
                Atom *candidate = form->expr.elems[1]->expr.elems[0];
                if (!definition_name ||
                    strcmp(atom_name_cstr(candidate), definition_name) == 0) {
                    subject = candidate;
                    lhs = form->expr.elems[1];
                    body = form->expr.elems[2];
                }
            }
        }
    }
    if (ready && subject && lhs && body) {
        provider = cetta_petta_type_fact_provider_create_v1(
            petta, &space, error, sizeof error);
        ready = provider && cetta_gslt_provider_registry_authorize_v1(
            catalog, cetta_petta_type_fact_provider_registry_v1(provider),
            &authorized, error, sizeof error);
    } else {
        ready = false;
    }
    CettaPettaV3EvidenceDecisionV1 decision = {0};
    bool decided = ready &&
        cetta_petta_typecheck_v3_decide_equation_v1(
            program, &authorized.registry, lhs, body,
            qualification_limits(), &decision, error, sizeof error);
    bool exact = decided && decision.outcome == expected_outcome &&
        decision.boundary == expected_boundary && decision.relation;
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr,
                "%s diagnostic: %s; outcome=%u boundary=%u relation=%s "
                "search-outcome=%u\n",
                label, error, (unsigned)decision.outcome,
                (unsigned)decision.boundary,
                decision.relation ? decision.relation : "<none>",
                (unsigned)decision.search_outcome);
    }
    cetta_gslt_authorized_provider_registry_free_v1(&authorized);
    cetta_petta_type_fact_provider_free_v1(provider);
    petta_program_free(petta);
    if (space_initialized)
        space_free(&space);
    free(forms);
    arena_free(&source);
}

static void check_definition_file(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderCatalogV1 *catalog,
    const char *path,
    CettaNikOutcomeV1 expected_outcome,
    CettaPettaV3EvidenceBoundaryV1 expected_boundary,
    const char *label) {
    check_named_definition_file(
        program, catalog, path, NULL, expected_outcome, expected_boundary,
        label);
}

static void check_compiled_query(
    const CettaGsltCompiledProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers,
    const char *query_text, const char *expected_text,
    size_t expected_count, const char *label) {
    Atom *query = parse_one(queries, query_text);
    Atom *expected = expected_text ? parse_one(queries, expected_text) : NULL;
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && (!expected_text || expected) &&
        cetta_gslt_compiled_query_with_providers_v1(
            program, providers, answers, query, qualification_limits(),
            &result, error, sizeof error);
    bool exact = ran &&
        result_has_exact_answer(&result, expected, expected_count);
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr, "%s diagnostic: %s; answers=%zu outcome=%u\n",
                label, error, result.answer_count, (unsigned)result.outcome);
    }
    cetta_gslt_horn_result_free(&result);
}

static void check_language_query(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers, const char *label) {
    const char *query_text =
        "(V3CallAccept "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) V3ArgsNil) "
        "    (V3Grade V3Det) (V3Prim V3Str)) "
        "  (V3ArgsCons (V3Prim V3Num) V3ArgsNil) (V3Grade V3Det))";
    Atom *query = parse_one(queries, query_text);
    CettaGsltHornResult result = {0};
    char error[ERROR_CAP] = {0};
    bool ran = query && cetta_gslt_language_query_with_providers_v1(
        language, realization,
        &cetta_petta_typecheck_v3_core_provider_catalog_v1, providers,
        answers, query, qualification_limits(), &result, error, sizeof error);
    bool exact = ran && result_has_exact_answer(&result, query, 1u);
    CHECK(exact, label);
    if (!exact) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    }
    cetta_gslt_horn_result_free(&result);
}

static void check_mode_table(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers) {
    const char *modes[] = {
        "V3Plain",
        "(V3Grade V3Det)",
        "(V3Grade V3Semidet)",
        "(V3Grade V3Nondet)",
        "(V3Effect EffectVar)",
    };
    const bool expected[5][5] = {
        {true, false, false, true, true},
        {true, true, true, true, true},
        {true, false, true, true, true},
        {true, false, false, true, true},
        {true, false, false, true, true},
    };
    for (size_t actual = 0u; actual < 5u; actual++) {
        for (size_t required = 0u; required < 5u; required++) {
            char query[256];
            char label[128];
            int query_length = snprintf(
                query, sizeof query, "(V3ModeFits %s %s)",
                modes[actual], modes[required]);
            int label_length = snprintf(
                label, sizeof label, "v3 arrow mode table cell %zu/%zu",
                actual, required);
            bool formatted = query_length >= 0 &&
                (size_t)query_length < sizeof query && label_length >= 0 &&
                (size_t)label_length < sizeof label;
            CHECK(formatted, "v3 arrow mode query formats");
            if (formatted) {
                check_query(
                    program, providers, queries, answers, query,
                    expected[actual][required] ? query : NULL,
                    expected[actual][required] ? 1u : 0u, label);
            }
        }
    }
    check_query(
        program, providers, queries, answers,
        "(V3ConcretizeMode (V3Grade V3Semidet) V3Semidet)",
        "(V3ConcretizeMode (V3Grade V3Semidet) V3Semidet)", 1u,
        "a concrete effect retains its exact execution grade");
    check_query(
        program, providers, queries, answers,
        "(V3ConcretizeMode (V3Effect EffectVar) V3Nondet)",
        "(V3ConcretizeMode (V3Effect EffectVar) V3Nondet)", 1u,
        "a named effect materializes only its safe execution upper bound");
    check_query(
        program, providers, queries, answers,
        "(V3ConcretizeMode (V3Effect EffectVar) V3Det)", NULL, 0u,
        "a named effect does not manufacture deterministic evidence");
    check_query(
        program, providers, queries, answers,
        "(V3DirectEffectCard (V3Effect EffectVar) (V3Effect EffectVar) "
        "  (V3Grade V3Semidet) V3Semidet)",
        "(V3DirectEffectCard (V3Effect EffectVar) (V3Effect EffectVar) "
        "  (V3Grade V3Semidet) V3Semidet)", 1u,
        "a matching direct effect slot contributes its concrete grade");
    check_query(
        program, providers, queries, answers,
        "(V3DirectEffectCard (V3Effect EffectVar) (V3Effect OtherEffect) "
        "  (V3Grade V3Semidet) V3Semidet)", NULL, 0u,
        "an unrelated effect slot contributes no grade evidence");
    check_query(
        program, providers, queries, answers,
        "(V3DirectEffectCard (V3Effect EffectVar) (V3Effect EffectVar) "
        "  (V3Effect EffectVar) V3Semidet)", NULL, 0u,
        "an unresolved higher-order actual contributes no concrete grade");
}

static void check_card_seq_table(
    const CettaGsltHornProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *queries, Arena *answers) {
    const char *cards[] = {"V3Det", "V3Semidet", "V3Nondet"};
    const size_t result[3][3] = {
        {0u, 1u, 2u},
        {1u, 1u, 2u},
        {2u, 2u, 2u},
    };
    for (size_t left = 0u; left < 3u; left++) {
        for (size_t right = 0u; right < 3u; right++) {
            char query[128];
            char label[128];
            int query_length = snprintf(
                query, sizeof query, "(V3CardSeq %s %s %s)",
                cards[left], cards[right], cards[result[left][right]]);
            int label_length = snprintf(
                label, sizeof label, "v3 cardinality sequence cell %zu/%zu",
                left, right);
            bool formatted = query_length >= 0 &&
                (size_t)query_length < sizeof query && label_length >= 0 &&
                (size_t)label_length < sizeof label;
            CHECK(formatted, "v3 cardinality sequence query formats");
            if (formatted) {
                check_query(
                    program, providers, queries, answers,
                    query, query, 1u, label);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s PETTA_TYPECHECK_V3_CORE_LANGDEF\n",
                argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variables;
    Arena facts;
    Arena queries;
    Arena answers;
    Arena live_source;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    arena_init(&facts);
    arena_init(&queries);
    arena_init(&answers);
    arena_init(&live_source);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = &variables;

    const char *paths[] = {argv[1]};
    CettaGsltHornProgram *program = NULL;
    CettaGsltCompiledProgram *compiled = NULL;
    CettaGsltFiniteFactProviderSetV1 *environment = NULL;
    CettaGsltAuthorizedProviderRegistryV1 authorized = {0};
    CettaGsltAuthorizedProviderRegistryV1 live_authorized = {0};
    CettaGsltLanguage *reference_language = NULL;
    CettaGsltLanguage *compiled_language = NULL;
    CettaPettaTypeFactProviderV1 *live_provider = NULL;
    CettaPettaTypecheckV3 *product_checker = NULL;
    PettaProgram *product_program = NULL;
    Registry product_registry;
    bool product_registry_initialized = false;
    Space product_space;
    bool product_space_initialized = false;
    PettaProgram *live_program = NULL;
    Space live_space;
    bool live_space_initialized = false;
    char error[ERROR_CAP] = {0};
    bool loaded = cetta_gslt_horn_program_load_paths(
        paths, 1u, &program, error, sizeof error);
    CHECK(loaded, "PeTTa typecheck-v3 core langdef loads");
    if (!loaded) {
        fprintf(stderr, "PeTTa typecheck-v3 load diagnostic: %s\n", error);
        goto cleanup;
    }
    CHECK(cetta_gslt_horn_program_rule_count(program) == 149u,
          "all 149 typecheck-v3 core and seam rules are executable");

    const CettaGsltProviderCatalogV1 *catalog =
        &cetta_petta_typecheck_v3_core_provider_catalog_v1;
    memset(error, 0, sizeof error);
    CHECK(cetta_gslt_provider_catalog_validate_v1(
              catalog, error, sizeof error) &&
              catalog->requirement_count == 5u,
          "the five inherited PeTTa provider identities are authored");

    Atom *environment_facts[] = {
        parse_one(&facts, "(EnvDeclared AliasNum (TAlias TNum))"),
        parse_one(&facts, "(EnvDeclared Count (TNewtype TNum))"),
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
    environment = facts_valid
        ? cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
              catalog->requirements, catalog->requirement_count,
              &span, 1u, error, sizeof error)
        : NULL;
    CHECK(environment != NULL,
          "PeTTa typecheck-v3 alias environment is admitted");
    if (!environment) {
        fprintf(stderr, "PeTTa typecheck-v3 environment diagnostic: %s\n",
                error);
        goto cleanup;
    }
    const CettaGsltProviderRegistryV1 *providers =
        cetta_gslt_finite_fact_provider_set_registry_v1(environment);

    memset(error, 0, sizeof error);
    bool authorized_ok = cetta_gslt_provider_registry_authorize_v1(
        catalog, providers, &authorized, error, sizeof error);
    CHECK(authorized_ok &&
              authorized.registry.provider_count == catalog->requirement_count,
          "the v3 runtime authorizes exactly the five inherited providers");
    if (!authorized_ok) {
        fprintf(stderr, "PeTTa typecheck-v3 authorization diagnostic: %s\n",
                error);
        goto cleanup;
    }

    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ExactTy "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) V3ArgsNil) "
        "    (V3Grade V3Det) (V3Union (V3Prim V3Str) (V3Prim V3Bool))) "
        "  $exact)",
        "(V3ExactTy "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) V3ArgsNil) "
        "    (V3Grade V3Det) (V3Union (V3Prim V3Str) (V3Prim V3Bool))) "
        "  V3Yes)",
        1u, "nested unknown-free types are exact");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ExactTy (V3List (V3Newtype Brand V3Unknown)) $exact)",
        "(V3ExactTy (V3List (V3Newtype Brand V3Unknown)) V3No)",
        1u, "a buried unknown makes a type inexact");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ExactTy V3Unknown V3Yes)", NULL, 0u,
        "unknown cannot manufacture positive exactness");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SeamKind V3Established V3TypedEvidence V3Yes $kind)",
        "(V3SeamKind V3Established V3TypedEvidence V3Yes V3Exact)",
        1u, "established exact evidence reaches the licensed seam kind");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SeamKind V3Established V3TypedEvidence V3No $kind)",
        "(V3SeamKind V3Established V3TypedEvidence V3No V3Gradual)",
        1u, "established inexact evidence remains gradual");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SeamKind V3Established V3UntypedEvidence V3No $kind)",
        "(V3SeamKind V3Established V3UntypedEvidence V3No V3Gradual)",
        1u, "established empty evidence remains unlicensed");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SeamKind V3Refuted V3TypedEvidence V3Yes $kind)",
        "(V3SeamKind V3Refuted V3TypedEvidence V3Yes V3Conflict)",
        1u, "refutation reaches conflict independently of exactness");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SeamKind V3Established V3TypedEvidence V3No V3Exact)",
        NULL, 0u, "inexact evidence cannot manufacture an exact seam kind");

    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated "
        "  (V3TypedResult (V3Prim V3Num)) V3Det "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Num)", "(V3Grade V3Det)", qualification_limits(),
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        CETTA_PETTA_V3_SEAM_EXACT, true,
        "v3-evidence-outcome-established",
        "native decision establishes matching positive evidence");
    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated "
        "  (V3TypedResult (V3Prim V3Num)) V3Det "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Str)", "(V3Grade V3Det)", qualification_limits(),
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        CETTA_PETTA_V3_SEAM_CONFLICT, false, "V3Consistent",
        "native shape refutation names its evidence boundary");
    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated "
        "  (V3TypedResult (V3Prim V3Num)) V3Nondet "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Num)", "(V3Grade V3Det)", qualification_limits(),
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        CETTA_PETTA_V3_SEAM_CONFLICT, false, "V3ModeFits",
        "native grade refutation names its evidence boundary");
    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated V3UnknownResult V3Semidet "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Num)", "(V3Grade V3Semidet)", qualification_limits(),
        CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        CETTA_PETTA_V3_SEAM_GRADUAL, false,
        "v3-evidence-outcome-undetermined",
        "native decision does not refute from missing evidence");
    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Num)", "(V3Grade V3Semidet)", qualification_limits(),
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        CETTA_PETTA_V3_SEAM_GRADUAL, false,
        "v3-evidence-outcome-empty-established",
        "proven empty evidence vacuously satisfies a result shape");
    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Nondet "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Num)", "(V3Grade V3Semidet)", qualification_limits(),
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        CETTA_PETTA_V3_SEAM_CONFLICT, false, "V3ModeFits",
        "empty evidence still respects its nondeterministic grade");
    check_evidence_decision(
        program, &authorized.registry, &queries,
        "(V3RuntimeEvidence V3StageEvaluated "
        "  (V3TypedResult (V3Prim V3Num)) V3Det "
        "  (V3Facts V3No V3No V3No))",
        "(V3Prim V3Num)", "(V3Grade V3Det)",
        (CettaGsltHornLimits){
            .max_rule_attempts = 1u, .max_answers = 16u, .max_depth = 256u,
        },
        CETTA_NIK_OUTCOME_INCOMPLETE,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        CETTA_PETTA_V3_SEAM_GRADUAL, false, "V3Consistent",
        "native decision maps search exhaustion to incomplete");

    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/111_quoted_brand_declared_output_candidate.metta",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_STAGE,
        "H5 111 rejects held nominal code at the stage boundary");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/112_quoted_the_declared_output_candidate.metta",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_STAGE,
        "H5 112 rejects held ascription code at the stage boundary");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/113_quoted_data_marker_declared_product_candidate.metta",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 113 rejects a quoted data product that omits the held head");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/114_quoted_data_marker_full_code_product_candidate.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 114 accepts the complete quoted data code product");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/115_eval_quoted_brand_reactivates_nominal_assertion_candidate.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 115 accepts eval-reactivated nominal evidence");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/116_eval_quoted_the_reactivates_ascription_candidate.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 116 accepts eval-reactivated ascription evidence");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/117_eval_quoted_brand_nondet_admission_candidate.metta",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 117 rejects nominal evidence promised as its representation");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/118_eval_quoted_literal_nondet_candidate.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 118 accepts evaluated literal evidence under nondet demand");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/119_eval_quoted_the_nondet_candidate.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 119 accepts evaluated ascription under nondet demand");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/120_direct_brand_literal_newtype_candidate.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 120 accepts direct branded newtype construction");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/109_declared_superpose_all_empty_semidet_accept.metta",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "H5 109 retains nondeterministic grade for empty superposition");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/110_declared_superpose_all_empty_nondet_accept.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 110 accepts empty superposition under nondet demand");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/104_inferred_if_all_empty_result.metta",
        CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 104 withholds a signature for an inferred empty conditional");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/105_declared_if_all_empty_semidet_accept.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 105 establishes a declared semidet empty conditional");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/106_inferred_case_all_empty_result.metta",
        CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 106 withholds a signature for an inferred empty case");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/107_declared_case_all_empty_semidet_accept.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 107 establishes a declared semidet empty case");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/108_inferred_superpose_all_empty_result.metta",
        CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 108 withholds a signature for an inferred empty superposition");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/84_foldall_empty_returns_initializer.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 84 preserves the initializer type for an empty fold");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/85_foldall_disguised_empty_reject.metta",
        "bad-fold",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 85 rejects an empty fold whose initializer violates the result type");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/81_inferred_untyped_atom_has_no_positive_type.metta",
        CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 81 does not manufacture positive evidence for an untyped atom");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/39_ascription_actual_union_reject.metta",
        "use-bool", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "v3 rejects an ascription disjoint from every actual union member");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/40_ascription_actual_alias_union.metta",
        "use-number", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "v3 narrows a transparent actual alias union existentially");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/41_ascription_required_alias_union.metta",
        "use-target", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "v3 narrows into a transparent required alias union");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/42_ascription_required_alias_union_reject.metta",
        "use-target", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "v3 rejects a required alias union with no overlap witness");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/43_ascription_reverse_newtype_overlap.metta",
        "use-count", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "v3 admits a runtime check through one nominal representation");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/44_ascription_disjoint_reject.metta",
        "use-string", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "v3 rejects an ascription between disjoint primitive types");
    check_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/60_repeated_head_variable_disjoint_slots.metta",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "v3 treats a contradictory repeated head variable as an empty clause");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/82_inferred_function_is_not_explicit_value_evidence.metta",
        "choose-increment",
        CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 82 does not publish an inferred function as an explicit value");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/89_if_nonempty_list_selection.metta",
        "if-list-caller", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 89 retains proper-list evidence through a conditional");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/90_case_proper_list_selection.metta",
        "case-list-caller", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 90 retains proper-list evidence through case branches");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/91_let_bound_proper_list_selection.metta",
        "let-list-caller", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 91 transports proper-list evidence through let");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/92_let_nonlist_selection_reject.metta",
        "let-nonlist-caller", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 92 rejects scalar evidence at a list selector");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/93_let_star_bound_proper_list_selection.metta",
        "let-star-list-caller", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 93 transports proper-list evidence through let-star");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/94_chain_identity_proper_list_selection.metta",
        "chain-list-caller", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "H5 94 retains the semideterministic chain boundary");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/95_let_bound_bool_selection.metta",
        "let-bool-caller", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 95 transports Boolean evidence through let");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/96_let_nonbool_bool_selection_reject.metta",
        "let-nonbool-caller", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 96 rejects numeric evidence at a Boolean selector");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/97_let_star_bound_bool_selection.metta",
        "let-star-bool-caller", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 97 transports Boolean evidence through let-star");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/98_let_star_alias_chain_bool_selection.metta",
        "let-star-alias-chain-caller",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 98 preserves Boolean evidence through an alias chain");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/99_let_star_alias_chain_nonbool_reject.metta",
        "let-star-alias-chain-nonbool-caller",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 99 preserves non-Boolean evidence through an alias chain");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/100_let_star_alias_chain_proper_list_selection.metta",
        "let-star-list-alias-chain-caller",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 100 preserves proper-list evidence through an alias chain");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/101_let_star_alias_chain_nondet_bool_reject.metta",
        "let-star-alias-chain-nondet-caller",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "H5 101 preserves nondeterminism through an alias chain");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/102_let_star_three_hop_bool_selection.metta",
        "let-star-three-hop-caller",
        CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 102 preserves Boolean evidence through three aliases");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/103_let_star_shadowed_alias_nonbool_reject.metta",
        "let-star-shadowed-alias-caller",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 103 respects lexical shadowing in alias evidence");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/71_concrete_data_selection_is_deterministic.metta",
        "concrete-data-selector", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 71 proves concrete inert-data selection total");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/72_direct_concrete_selection_is_deterministic.metta",
        "direct-concrete-selector", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 72 proves direct concrete selection total");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/73_expression_empty_cons_selection_is_not_total.metta",
        "expression-shape-caller", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "H5 73 rejects an open expression selector without total coverage");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/74_inferred_superpose_result_widens_unknown.metta",
        "use-mixed", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 74 publishes a precise heterogeneous inferred result");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/75_inferred_superpose_unknown_does_not_reject.metta",
        "use-mixed-as-number", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 75 rejects a precise heterogeneous result at Number");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/76_inferred_canonical_list_preserved.metta",
        "use-collected-numbers", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 76 publishes canonical inferred list evidence");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/77_inferred_canonical_list_rejects_mismatch.metta",
        "misuse-collected-numbers", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 77 retains inferred list element shape");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/78_inferred_canonical_arrow_preserved.metta",
        "use-increment", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 78 publishes an explicitly typed first-class arrow");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/79_inferred_canonical_arrow_rejects_mismatch.metta",
        "misuse-increment", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 79 retains inferred arrow component shape");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/80_inferred_ambiguous_symbol_widens_unknown.metta",
        "use-dual", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "H5 80 retains ambiguity as a precise union");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/87_inferred_if_empty_result.metta",
        "use-maybe", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 87 publishes a semideterministic surviving branch");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/121_nested_literal_selector_totality_candidate.metta",
        "fixed-cell-caller", CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 121 withholds totality across a caller-bound nested literal");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/123_variable_headed_selection_literal_body_candidate.metta",
        "dynamic-head-caller", CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 123 withholds totality across a variable-headed selection");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_once_cardinality.metta",
        "choose-once", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "H5 determinism accepts once-nondet at semidet");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_once_cardinality.metta",
        "choose-once-too-strong", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "once never manufactures deterministic totality");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_once_cardinality.metta",
        "bump-once", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "once preserves an already deterministic grade");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/31_contextual_cons_list.metta",
        "last-number", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "an open typed list tail preserves constructor evidence");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v2_repros/32_contextual_cons_tail_mismatch.metta",
        "prepend-number", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "a non-list tail is a replayable constructor conflict");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_open_list_construction.metta",
        "empty-numbers", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "the empty list is a deterministic gradual list value");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_open_list_construction.metta",
        "preserve-open-tail", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "a typed open tail preserves deterministic list construction");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_open_list_construction.metta",
        "reject-nonlist-tail", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "a scalar tail cannot masquerade as a list");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_guarded_expression_selection.metta",
        "guarded-expression-head", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "is-expr supplies branch-local total selection evidence");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_guarded_expression_selection.metta",
        "unguarded-expression-head", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "an unguarded head selection remains semideterministic");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_effect_instantiation.metta",
        "use-deterministic-effect", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "a direct effect grade alone cannot prove combinator coverage");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_effect_instantiation.metta",
        "use-semideterministic-effect",
        CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "a direct semideterministic grade still lacks coverage evidence");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_effect_instantiation.metta",
        "use-conservative-effect", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "an unresolved named effect remains executable as nondeterministic");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_effect_instantiation.metta",
        "reject-effect-overclaim", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "a semideterministic closure cannot satisfy a deterministic promise");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_case_pattern_bindings.metta",
        "case-number", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "a constructor pattern types its branch-local payload");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_case_pattern_bindings.metta",
        "reject-case-string", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "a typed constructor payload cannot masquerade as a string");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_case_pattern_bindings.metta",
        "union-list-tail", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "a unique list alternative types its case-pattern tail");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_case_pattern_bindings.metta",
        "reject-union-list-head-string", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_SHAPE,
        "union narrowing exposes an incompatible list element");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_quoted_argument.metta",
        "build-runtime-rule", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "constructing quoted Atom data does not execute its latent effect");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_quoted_argument.metta",
        "build-inert-rule", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "constructing inert expression data is deterministic");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_quoted_argument.metta",
        "reject-nondet-brand", CETTA_NIK_OUTCOME_REFUTED,
        CETTA_PETTA_V3_BOUNDARY_CARDINALITY,
        "nominal construction preserves payload multiplicity");
    check_named_definition_file(
        program, catalog,
        "tests/petta/typecheck_v3_quoted_argument.metta",
        "build-fresh-pair", CETTA_NIK_OUTCOME_ESTABLISHED,
        CETTA_PETTA_V3_BOUNDARY_NONE,
        "a fresh logic variable is a gradual executable value");

    memset(error, 0, sizeof error);
    bool compiled_loaded = cetta_gslt_compiled_program_load_v1(
        &cetta_petta_typecheck_v3_core_v1.compiled_plan,
        &compiled, error, sizeof error);
    bool exact_plan = compiled_loaded &&
        cetta_gslt_compiled_program_matches_source_v1(
            compiled, program, error, sizeof error);
    CHECK(compiled_loaded && exact_plan,
          "generated v3 plan exactly matches the authored core");
    if (!compiled_loaded || !exact_plan) {
        fprintf(stderr, "PeTTa typecheck-v3 compiled-plan diagnostic: %s\n",
                error);
        goto cleanup;
    }

    check_mode_table(program, &authorized.registry, &queries, &answers);
    check_card_seq_table(program, &authorized.registry, &queries, &answers);

    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ElaborateType TAtom $core)",
        "(V3ElaborateType TAtom V3Unknown)",
        1u, "a declared Atom is ignorance, not exact symbol evidence");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ElaborateType "
        "  (TArrow MDet (TTCons TNum (TTCons TStr TTNil)) TBool) $core)",
        "(V3ElaborateType "
        "  (TArrow MDet (TTCons TNum (TTCons TStr TTNil)) TBool) "
        "  (V3Arrow "
        "    (V3ArgsCons (V3Prim V3Num) "
        "      (V3ArgsCons (V3Prim V3Str) V3ArgsNil)) "
        "    (V3Grade V3Det) (V3Prim V3Bool)))",
        1u, "provider arrow wire elaborates into the native v3 core");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ElaborateType (TUnion (TTCons TNum (TTCons TStr TTNil))) $core)",
        "(V3ElaborateType (TUnion (TTCons TNum (TTCons TStr TTNil))) "
        "  (V3Union (V3Prim V3Num) (V3Prim V3Str)))",
        1u, "provider union wire elaborates without publication widening");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3BranchEvidence "
        "  (V3SomeEvidence "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3List (V3Prim V3Num))) V3Det "
        "      (V3Facts V3Yes V3No V3No))) "
        "  V3NoEvidence $evidence)",
        "(V3BranchEvidence "
        "  (V3SomeEvidence "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3List (V3Prim V3Num))) V3Det "
        "      (V3Facts V3Yes V3No V3No))) "
        "  V3NoEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3List (V3Prim V3Num))) V3Semidet "
        "    (V3Facts V3Yes V3No V3No)))",
        1u, "an empty branch changes grade without erasing list evidence");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3BranchEvidence V3NoEvidence V3NoEvidence $evidence)",
        "(V3BranchEvidence V3NoEvidence V3NoEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet "
        "    (V3Facts V3No V3No V3No)))",
        1u, "two empty branches do not invent a result type");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SelectionEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3List (V3Prim V3Num))) V3Det "
        "    (V3Facts V3Yes V3No V3No)) "
        "  V3RequireProperList V3Det $card)",
        "(V3SelectionEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3List (V3Prim V3Num))) V3Det "
        "    (V3Facts V3Yes V3No V3No)) "
        "  V3RequireProperList V3Det V3Det)",
        1u, "positive proper-list evidence licenses total selection");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3SelectionEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Det "
        "    (V3Facts V3No V3No V3No)) "
        "  V3RequireProperList V3Det $card)",
        NULL, 0u,
        "a scalar cannot manufacture proper-list selector evidence");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3AllEmptySuperposeEvidence $evidence)",
        "(V3AllEmptySuperposeEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Nondet "
        "    (V3Facts V3No V3No V3No)))",
        1u, "all-empty superposition retains grade but no type");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ExpressionEvidence V3SourceSuperposeTwoEmpty $evidence)",
        "(V3ExpressionEvidence V3SourceSuperposeTwoEmpty "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Nondet "
        "    (V3Facts V3No V3No V3No)))",
        1u, "source all-empty superposition elaborates to proven emptiness");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ExpressionEvidence V3SourceIfAllEmpty $evidence)",
        "(V3ExpressionEvidence V3SourceIfAllEmpty "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet "
        "    (V3Facts V3No V3No V3No)))",
        1u, "source all-empty conditional elaborates to proven emptiness");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ExpressionEvidence V3SourceCaseAllEmpty $evidence)",
        "(V3ExpressionEvidence V3SourceCaseAllEmpty "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet "
        "    (V3Facts V3No V3No V3No)))",
        1u, "source all-empty case elaborates to proven emptiness");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated V3UnknownResult V3Semidet "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Semidet) $outcome)",
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated V3UnknownResult V3Semidet "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Semidet) V3Undetermined)",
        1u, "missing result evidence stays undetermined");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Semidet) $outcome)",
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated V3EmptyResult V3Semidet "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Semidet) V3Established)",
        1u, "proven empty evidence is distinct from ignorance");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Det "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Det) $outcome)",
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Det "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Det) V3Established)",
        1u, "positive shape and grade evidence is established");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3EvidenceOutcome "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Nondet "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3Prim V3Num) (V3Grade V3Det) V3Established)",
        NULL, 0u,
        "a nondeterministic result cannot establish a deterministic promise");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ApplyBindings V3EnvNil "
        "  (V3BindingsCons "
        "    (V3ValueSource "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Bool)) V3Det "
        "        (V3Facts V3No V3Yes V3No))) "
        "    (V3BindingsCons (V3AliasSource V3IndexZero) "
        "      (V3BindingsCons (V3AliasSource V3IndexZero) "
        "        V3BindingsNil))) $environment)",
        "(V3ApplyBindings V3EnvNil "
        "  (V3BindingsCons "
        "    (V3ValueSource "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Bool)) V3Det "
        "        (V3Facts V3No V3Yes V3No))) "
        "    (V3BindingsCons (V3AliasSource V3IndexZero) "
        "      (V3BindingsCons (V3AliasSource V3IndexZero) "
        "        V3BindingsNil))) "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Det "
        "      (V3Facts V3No V3Yes V3No)) "
        "    (V3EnvCons "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Bool)) V3Det "
        "        (V3Facts V3No V3Yes V3No)) "
        "      (V3EnvCons "
        "        (V3RuntimeEvidence V3StageEvaluated "
        "          (V3TypedResult (V3Prim V3Bool)) V3Det "
        "          (V3Facts V3No V3Yes V3No)) V3EnvNil))))",
        1u, "three-hop lexical aliases preserve Boolean evidence");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ApplyBindings "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "      (V3Facts V3No V3Yes V3No)) V3EnvNil) "
        "  (V3BindingsCons (V3AliasSource V3IndexZero) V3BindingsNil) "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "      (V3Facts V3No V3Yes V3No)) "
        "    (V3EnvCons "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "        (V3Facts V3No V3Yes V3No)) V3EnvNil)))",
        "(V3ApplyBindings "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "      (V3Facts V3No V3Yes V3No)) V3EnvNil) "
        "  (V3BindingsCons (V3AliasSource V3IndexZero) V3BindingsNil) "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "      (V3Facts V3No V3Yes V3No)) "
        "    (V3EnvCons "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "        (V3Facts V3No V3Yes V3No)) V3EnvNil)))",
        1u, "alias forwarding preserves nondeterministic grade");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3EnvLookup "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Det "
        "      (V3Facts V3No V3Yes V3No)) "
        "    (V3EnvCons "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Num)) V3Det "
        "        (V3Facts V3No V3No V3No)) V3EnvNil)) "
        "  (V3IndexSucc V3IndexZero) $evidence)",
        "(V3EnvLookup "
        "  (V3EnvCons "
        "    (V3RuntimeEvidence V3StageEvaluated "
        "      (V3TypedResult (V3Prim V3Bool)) V3Det "
        "      (V3Facts V3No V3Yes V3No)) "
        "    (V3EnvCons "
        "      (V3RuntimeEvidence V3StageEvaluated "
        "        (V3TypedResult (V3Prim V3Num)) V3Det "
        "        (V3Facts V3No V3No V3No)) V3EnvNil)) "
        "  (V3IndexSucc V3IndexZero) "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Det "
        "    (V3Facts V3No V3No V3No)))",
        1u, "de Bruijn lookup preserves the pre-shadow binding");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ApplyBindings V3EnvNil "
        "  (V3BindingsCons (V3AliasSource V3IndexZero) V3BindingsNil) "
        "  $environment)",
        NULL, 0u, "dangling lexical aliases do not invent evidence");

    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ConsistentInput (V3Alias AliasNum) (V3Prim V3Num))",
        "(V3ConsistentInput (V3Alias AliasNum) (V3Prim V3Num))",
        1u, "admitted aliases resolve before consistency");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ResolveInput (V3Alias Count) $core)",
        "(V3ResolveInput (V3Alias Count) "
        "  (V3Newtype Count (V3Prim V3Num)))",
        1u, "provider newtypes retain their nominal brand");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3ConsistentInput (V3Alias Missing) (V3Prim V3Num))",
        NULL, 0u, "missing aliases yield no rejection judgment");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3Consistent (V3Union (V3Prim V3Num) (V3Prim V3Str)) "
        "  (V3Prim V3Num))",
        NULL, 0u, "actual union requires every branch");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3Consistent (V3Prim V3Num) "
        "  (V3Union (V3Prim V3Num) (V3Prim V3Str)))",
        "(V3Consistent (V3Prim V3Num) "
        "  (V3Union (V3Prim V3Num) (V3Prim V3Str)))",
        1u, "required union accepts one branch");
    check_ground_query(
        program, &authorized.registry, &queries, &answers,
        "(V3MayOverlap "
        "  (V3Union (V3Prim V3Num) (V3Prim V3Str)) "
        "  (V3Prim V3Str))",
        true, "explicit narrowing accepts one possible union member");
    check_ground_query(
        program, &authorized.registry, &queries, &answers,
        "(V3MayOverlap "
        "  (V3Newtype Count (V3Prim V3Num)) (V3Prim V3Num))",
        true, "explicit narrowing can inspect one nominal representation");
    check_ground_query(
        program, &authorized.registry, &queries, &answers,
        "(V3MayOverlap "
        "  (V3Newtype Count (V3Prim V3Num)) "
        "  (V3Newtype OtherCount (V3Prim V3Num)))",
        false,
        "explicit narrowing preserves distinct nominal identities");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3CallAccept "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) V3ArgsNil) "
        "    (V3Grade V3Det) (V3Prim V3Str)) "
        "  (V3ArgsCons (V3Prim V3Num) V3ArgsNil) (V3Grade V3Det))",
        "(V3CallAccept "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) V3ArgsNil) "
        "    (V3Grade V3Det) (V3Prim V3Str)) "
        "  (V3ArgsCons (V3Prim V3Num) V3ArgsNil) (V3Grade V3Det))",
        1u, "multi-argument call accepts an exact spine");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3CallAccept "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) "
        "    (V3ArgsCons (V3Prim V3Str) V3ArgsNil)) "
        "    (V3Grade V3Det) (V3Prim V3Str)) "
        "  (V3ArgsCons (V3Prim V3Num) V3ArgsNil) (V3Grade V3Det))",
        NULL, 0u, "multi-argument call rejects an arity mismatch");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3LetEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Bool)) V3Det "
        "    (V3Facts V3No V3Yes V3No)) "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Semidet "
        "    (V3Facts V3No V3No V3No)) $evidence)",
        "(V3LetEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Bool)) V3Det "
        "    (V3Facts V3No V3Yes V3No)) "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Semidet "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Semidet "
        "    (V3Facts V3No V3No V3No)))",
        1u, "let evidence composes once and preserves the body result");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3LetEvidence "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Bool)) V3Nondet "
        "    (V3Facts V3No V3Yes V3No)) "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Det "
        "    (V3Facts V3No V3No V3No)) "
        "  (V3RuntimeEvidence V3StageEvaluated "
        "    (V3TypedResult (V3Prim V3Num)) V3Det "
        "    (V3Facts V3No V3No V3No)))",
        NULL, 0u, "a nondeterministic binding cannot masquerade as deterministic");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3GroundingLicensed Fact V3StageSource)",
        NULL, 0u, "source-stage evidence cannot mint a grounding license");
    check_query(
        program, &authorized.registry, &queries, &answers,
        "(V3GroundingLicensed Fact V3StageCompiled)",
        "(V3GroundingLicensed Fact V3StageCompiled)",
        1u, "compiled-stage evidence licenses grounding");

    const char *accepted_call =
        "(V3CallAccept "
        "  (V3Arrow (V3ArgsCons (V3Prim V3Num) V3ArgsNil) "
        "    (V3Grade V3Det) (V3Prim V3Str)) "
        "  (V3ArgsCons (V3Prim V3Num) V3ArgsNil) (V3Grade V3Det))";
    check_compiled_query(
        compiled, &authorized.registry, &queries, &answers,
        accepted_call, accepted_call, 1u,
        "compiled v3 worklist accepts the same multi-argument call");

    live_program = petta_program_new();
    bool live_program_ready = live_program != NULL;
    if (live_program_ready) {
        space_init(&live_space);
        live_space_initialized = true;
        live_program_ready = petta_program_enable_analysis(live_program) &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: AliasNum (Alias Number))") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: Count (Newtype Number))") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: Abstract Type)") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: plain-a PlainA)") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: plain-b PlainB)") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: pick (-[det]-> Number))") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(: recursive-grade (-[semidet]-> Number Number))") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(= (recursive-grade $x) (recursive-grade $x))") &&
            install_live_form(
                live_program, &live_space, &live_source,
                "(= (untyped-recursive $x) (untyped-recursive $x))");
    }
    CHECK(live_program_ready,
          "a live PeTTa declaration state is available to v3");
    if (live_program_ready) {
        memset(error, 0, sizeof error);
        live_provider = cetta_petta_type_fact_provider_create_v1(
            live_program, &live_space, error, sizeof error);
        bool live_bound = live_provider &&
            cetta_petta_type_fact_provider_is_current_v1(live_provider) &&
            cetta_gslt_provider_registry_authorize_v1(
                catalog,
                cetta_petta_type_fact_provider_registry_v1(live_provider),
                &live_authorized, error, sizeof error);
        CHECK(live_bound,
              "the five providers bind to one live Space revision");
        if (live_bound) {
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ConsistentInput (V3Alias AliasNum) (V3Prim V3Num))",
                "(V3ConsistentInput (V3Alias AliasNum) (V3Prim V3Num))",
                1u, "live provider aliases elaborate into v3");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ResolveInput (V3Alias Count) $core)",
                "(V3ResolveInput (V3Alias Count) "
                "  (V3Newtype Count (V3Prim V3Num)))",
                1u, "live provider newtypes remain nominal");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ResolveInput (V3Alias Abstract) $core)",
                "(V3ResolveInput (V3Alias Abstract) "
                "  (V3Newtype Abstract V3Unknown))",
                1u, "abstract declared types gain nominal hidden shape");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3Consistent "
                "  (V3Newtype Abstract V3Unknown) "
                "  (V3Newtype Count V3Unknown))",
                NULL, 0u,
                "distinct abstract and explicit nominal types remain distinct");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ResolveInput (V3Alias PlainA) $core)",
                "(V3ResolveInput (V3Alias PlainA) "
                "  (V3Newtype PlainA V3Unknown))",
                1u,
                "a symbol used as a type is positive abstract-type evidence");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3Consistent "
                "  (V3Newtype PlainA V3Unknown) "
                "  (V3Newtype PlainB V3Unknown))",
                NULL, 0u,
                "distinct plain source type symbols remain incompatible");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(EnvDeclaredList pick $declarations)",
                "(EnvDeclaredList pick "
                "  (DCons (Decl pick (TArrow MDet TTNil TNum)) DNil))",
                1u, "live declarations retain source order and arrow grade");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(EnvDeclaredList and $declarations)",
                "(EnvDeclaredList and "
                "  (DCons (Decl and "
                "    (TArrow MDet "
                "      (TTCons TBool (TTCons TBool TTNil)) TBool)) DNil))",
                1u,
                "the admitted PeTTa builtin catalog supplies one typed signature");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(KnownExpressionEffect (and True False) $mode)",
                "(KnownExpressionEffect (and True False) MDet)", 1u,
                "builtin execution grade is derived from its admitted signature");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(KnownExpressionEffect (recursive-grade 1) $mode)",
                "(KnownExpressionEffect (recursive-grade 1) MSemidet)", 1u,
                "recursive calls consume their validated declared grade");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(KnownExpressionEffect (untyped-recursive 1) $mode)",
                NULL, 0u,
                "untyped recursion cannot manufacture an execution grade");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(EnvNonNewtype AliasNum)",
                "(EnvNonNewtype AliasNum)", 1u,
                "a live alias is positively known not to be a newtype");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(EnvNonNewtype Count)", NULL, 0u,
                "a live newtype is excluded from non-newtype evidence");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(KnownExpressionEffect "
                "  (eval (quote (superpose (1 2)))) $mode)",
                NULL, 0u,
                "provider facts do not flatten eval across a stage boundary");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(KnownExpressionResultType "
                "  (eval (quote (brand Count 1))) $type)",
                NULL, 0u,
                "provider facts do not publish held result types");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ExpressionEvidence (quote (brand Count 1)) $evidence)",
                "(V3ExpressionEvidence (quote (brand Count 1)) "
                "  (V3HeldEvidence "
                "    (V3TypedResult (V3Newtype Count (V3Prim V3Num))) "
                "    V3Det (V3Facts V3No V3No V3No)))",
                1u, "quote retains latent evidence without publishing it");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ExpressionEvidence (quote (brand Count 1)) "
                "  (V3RuntimeEvidence $stage $type $card $facts))",
                NULL, 0u,
                "held code is not usable as runtime result evidence");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(V3ExpressionEvidence "
                "  (eval (quote (brand Count 1))) $evidence)",
                "(V3ExpressionEvidence "
                "  (eval (quote (brand Count 1))) "
                "  (V3RuntimeEvidence V3StageEvaluated "
                "    (V3TypedResult (V3Newtype Count (V3Prim V3Num))) "
                "    V3Det (V3Facts V3No V3No V3No)))",
                1u, "eval alone reactivates held nominal evidence");
            check_query(
                program, &live_authorized.registry, &queries, &answers,
                "(KnownExpressionResultType (pick) $type)",
                "(KnownExpressionResultType (pick) TNum)", 1u,
                "zero-argument calls obtain their declared result type");

            bool installed_later = install_live_form(
                live_program, &live_space, &live_source,
                "(: Later (Alias String))");
            CHECK(installed_later &&
                      !cetta_petta_type_fact_provider_is_current_v1(
                          live_provider),
                  "a Space mutation invalidates the pinned provider");
            Atom *stale_query = parse_one(
                &queries,
                "(V3ConsistentInput (V3Alias Later) (V3Prim V3Str))");
            CettaGsltHornResult stale_result = {0};
            memset(error, 0, sizeof error);
            bool stale_ran = stale_query &&
                cetta_gslt_horn_query_with_providers_v1(
                    program, &live_authorized.registry, &answers,
                    stale_query, qualification_limits(), &stale_result,
                    error, sizeof error);
            CHECK(!stale_ran && strstr(error, "stale") != NULL,
                  "stale provider evidence faults instead of being reused");
            cetta_gslt_horn_result_free(&stale_result);

            cetta_gslt_authorized_provider_registry_free_v1(
                &live_authorized);
            cetta_petta_type_fact_provider_free_v1(live_provider);
            live_provider = NULL;
            memset(error, 0, sizeof error);
            live_provider = cetta_petta_type_fact_provider_create_v1(
                live_program, &live_space, error, sizeof error);
            bool rebound = live_provider &&
                cetta_gslt_provider_registry_authorize_v1(
                    catalog,
                    cetta_petta_type_fact_provider_registry_v1(live_provider),
                    &live_authorized, error, sizeof error);
            CHECK(rebound,
                  "one fresh provider admission binds the new revision");
            if (rebound) {
                check_query(
                    program, &live_authorized.registry, &queries, &answers,
                    "(V3ConsistentInput "
                    "  (V3Alias Later) (V3Prim V3Str))",
                    "(V3ConsistentInput "
                    "  (V3Alias Later) (V3Prim V3Str))",
                    1u, "fresh revision evidence replaces the stale snapshot");
            }

            bool installed_conflict = install_live_form(
                live_program, &live_space, &live_source,
                "(: Conflict (Alias Number))") &&
                install_live_form(
                    live_program, &live_space, &live_source,
                    "(: Conflict (Newtype Number))");
            cetta_gslt_authorized_provider_registry_free_v1(
                &live_authorized);
            cetta_petta_type_fact_provider_free_v1(live_provider);
            live_provider = NULL;
            memset(error, 0, sizeof error);
            live_provider = cetta_petta_type_fact_provider_create_v1(
                live_program, &live_space, error, sizeof error);
            bool source_ordered = live_provider &&
                cetta_gslt_provider_registry_authorize_v1(
                    catalog,
                    cetta_petta_type_fact_provider_registry_v1(live_provider),
                    &live_authorized, error, sizeof error);
            CHECK(installed_conflict && source_ordered,
                  "later nominal declarations preserve first-source authority");
            if (source_ordered) {
                check_query(
                    program, &live_authorized.registry, &queries, &answers,
                    "(V3ResolveInput (V3Alias Conflict) $core)",
                    "(V3ResolveInput (V3Alias Conflict) (V3Prim V3Num))",
                    1u,
                    "the first alias meaning excludes a later newtype meaning");
            }
        } else {
            fprintf(stderr, "live provider diagnostic: %s\n", error);
        }
    }

    memset(error, 0, sizeof error);
    product_checker = cetta_petta_typecheck_v3_create(error, sizeof error);
    CHECK(product_checker &&
              cetta_petta_typecheck_v3_rule_count(product_checker) == 149u,
          "the product checker loads the generated 149-rule calculus");
    product_program = petta_program_new();
    registry_init(&product_registry);
    product_registry_initialized = true;
    bool product_ready = product_checker && product_program &&
        petta_program_enable_analysis(product_program);
    if (product_ready) {
        space_init(&product_space);
        product_space_initialized = true;
        Atom *positive_overlay[] = {
            parse_one(&queries, "(: overlay-ok (-[det]-> Number))"),
            parse_one(&queries, "(= (overlay-ok) 7)"),
        };
        CettaCount before = space_length64(&product_space);
        CettaPettaTypecheckV3BlockResult positive = {0};
        bool positive_checked = positive_overlay[0] && positive_overlay[1] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                positive_overlay, 2u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT, &positive);
        CHECK(positive_checked &&
                  positive.verdict == PETTA_ANALYSIS_ESTABLISHED &&
                  positive.declarations_seen == 1u &&
                  positive.equations_checked == 1u &&
                  positive.established_equations == 1u &&
                  positive.exact_equations == 1u &&
                  positive.gradual_equations == 0u &&
                  positive.optimization_license.issued &&
                  cetta_petta_typecheck_v3_opt_license_is_current(
                      &positive.optimization_license, &product_space,
                      positive_overlay, 2u,
                      CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT),
              "a source overlay is checked as one mutually visible block");
        CHECK(space_length64(&product_space) == before,
              "checking a v3 source overlay does not publish it");

        Atom *negative_overlay[] = {
            parse_one(&queries, "(: overlay-bad (-[det]-> Number))"),
            parse_one(&queries, "(= (overlay-bad) \"not-a-number\")"),
        };
        CettaPettaTypecheckV3BlockResult negative = {0};
        bool negative_checked = negative_overlay[0] && negative_overlay[1] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                negative_overlay, 2u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT, &negative);
        CHECK(negative_checked &&
                  negative.verdict == PETTA_ANALYSIS_REFUTED &&
                  negative.boundary == CETTA_PETTA_V3_BOUNDARY_SHAPE &&
                  strcmp(negative.relation, "V3Consistent") == 0 &&
                  strstr(negative.diagnostic, "shape evidence boundary") &&
                  negative.conflict_equations == 1u &&
                  !negative.optimization_license.issued,
              "a product rejection names its native rule and boundary");

        Atom *let_bool_overlay[] = {
            parse_one(
                &queries,
                "(: let-bool (-[det]-> Bool Bool))"),
            parse_one(
                &queries,
                "(= (let-bool $input) "
                "  (let $bound $input (and $bound True)))"),
        };
        CettaPettaTypecheckV3BlockResult let_bool = {0};
        bool let_bool_checked = let_bool_overlay[0] && let_bool_overlay[1] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                let_bool_overlay, 2u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT, &let_bool);
        CHECK(let_bool_checked &&
                  let_bool.verdict == PETTA_ANALYSIS_ESTABLISHED &&
                  let_bool.established_equations == 1u,
              "lexical Boolean evidence reaches a typed builtin call");

        Atom *let_number_overlay[] = {
            parse_one(
                &queries,
                "(: let-number-bad (-[det]-> Number Bool))"),
            parse_one(
                &queries,
                "(= (let-number-bad $input) "
                "  (let $bound $input (and $bound True)))"),
        };
        CettaPettaTypecheckV3BlockResult let_number = {0};
        bool let_number_checked =
            let_number_overlay[0] && let_number_overlay[1] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                let_number_overlay, 2u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT, &let_number);
        CHECK(let_number_checked &&
                  let_number.verdict == PETTA_ANALYSIS_REFUTED &&
                  let_number.boundary == CETTA_PETTA_V3_BOUNDARY_SHAPE &&
                  strcmp(let_number.relation, "V3Consistent") == 0,
              "lexical evidence rejects a non-Boolean builtin argument");

        Atom *open_overlay[] = {
            parse_one(&queries, "(= (overlay-open) (unknown-call 1))"),
        };
        CettaPettaTypecheckV3BlockResult open = {0};
        bool open_checked = open_overlay[0] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                open_overlay, 1u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT, &open);
        CHECK(open_checked &&
                  open.verdict == PETTA_ANALYSIS_UNDETERMINED &&
                  open.undetermined_equations == 1u &&
                  open.gradual_equations == 1u &&
                  !open.optimization_license.issued,
              "open evidence stays undetermined even under strict linting");

        CettaPettaTypecheckV3CompatibilityResult exact_product = {0};
        bool exact_product_checked =
            cetta_petta_typecheck_v3_compatibility_block(
                product_checker, product_program, &product_space,
                &product_registry, positive_overlay, 2u,
                PETTA_TYPECHECK_POLICY_DEFAULT, false, &exact_product);
        CHECK(exact_product_checked &&
                  exact_product.route ==
                      CETTA_PETTA_TYPECHECK_V3_ROUTE_NATIVE_AGREEMENT &&
                  exact_product.verdict == PETTA_TYPECHECK_ESTABLISHED &&
                  exact_product.native_optimization_authorized,
              "exact native agreement is the sole product optimization authority");

        CettaPettaTypecheckV3CompatibilityResult conflict_product = {0};
        bool conflict_product_checked =
            cetta_petta_typecheck_v3_compatibility_block(
                product_checker, product_program, &product_space,
                &product_registry, negative_overlay, 2u,
                PETTA_TYPECHECK_POLICY_DEFAULT, false, &conflict_product);
        CHECK(conflict_product_checked &&
                  !conflict_product.native_optimization_authorized,
              "a product rejection cannot authorize typed optimization");

        CettaPettaTypecheckV3CompatibilityResult gradual_product = {0};
        bool gradual_product_checked =
            cetta_petta_typecheck_v3_compatibility_block(
                product_checker, product_program, &product_space,
                &product_registry, open_overlay, 1u,
                PETTA_TYPECHECK_POLICY_DEFAULT, false, &gradual_product);
        CHECK(gradual_product_checked &&
                  gradual_product.route ==
                      CETTA_PETTA_TYPECHECK_V3_ROUTE_LEGACY_V2 &&
                  !gradual_product.native_optimization_authorized,
              "a gradual legacy route cannot authorize typed optimization");

        Atom *coverage_positive_overlay[] = {
            parse_one(&queries, "(: bool-not (-[det]-> Bool Bool))"),
            parse_one(&queries, "(= (bool-not True) False)"),
            parse_one(&queries, "(= (bool-not False) True)"),
        };
        CettaPettaTypecheckV3BlockResult coverage_positive = {0};
        bool coverage_positive_checked =
            coverage_positive_overlay[0] && coverage_positive_overlay[1] &&
            coverage_positive_overlay[2] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                coverage_positive_overlay, 3u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT,
                &coverage_positive);
        CHECK(coverage_positive_checked &&
                  coverage_positive.verdict == PETTA_ANALYSIS_ESTABLISHED &&
                  coverage_positive.established_equations == 2u,
              "relation coverage accepts a complete Boolean pattern column");

        Atom *coverage_number_overlay[] = {
            parse_one(&queries, "(: partial-number (-[det]-> Number Number))"),
            parse_one(&queries, "(= (partial-number 1) 10)"),
            parse_one(&queries, "(= (partial-number 2) 20)"),
        };
        CettaPettaTypecheckV3BlockResult coverage_number = {0};
        bool coverage_number_checked = coverage_number_overlay[0] &&
            coverage_number_overlay[1] && coverage_number_overlay[2] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                coverage_number_overlay, 3u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT, &coverage_number);
        CHECK(coverage_number_checked &&
                  coverage_number.verdict == PETTA_ANALYSIS_REFUTED &&
                  coverage_number.boundary ==
                      CETTA_PETTA_V3_BOUNDARY_CARDINALITY &&
                  strcmp(
                      coverage_number.relation,
                      "V3RelationCoverage") == 0,
              "literal-only infinite-domain coverage carries a negative witness");

        Atom *coverage_nominal_overlay[] = {
            parse_one(&queries, "(: Shape Type)"),
            parse_one(&queries, "(: Circle (-> Number Shape))"),
            parse_one(&queries, "(: Square (-> Number Shape))"),
            parse_one(&queries, "(: Point Shape)"),
            parse_one(&queries, "(: area (-[det]-> Shape Number))"),
            parse_one(&queries, "(= (area (Circle $r)) $r)"),
            parse_one(&queries, "(= (area (Square $s)) $s)"),
        };
        CettaPettaTypecheckV3BlockResult coverage_nominal = {0};
        bool coverage_nominal_checked = true;
        for (size_t index = 0u; index < 7u; index++)
            coverage_nominal_checked = coverage_nominal_checked &&
                coverage_nominal_overlay[index];
        coverage_nominal_checked = coverage_nominal_checked &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                coverage_nominal_overlay, 7u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT, &coverage_nominal);
        CHECK(coverage_nominal_checked &&
                  coverage_nominal.verdict == PETTA_ANALYSIS_REFUTED &&
                  strcmp(
                      coverage_nominal.relation,
                      "V3RelationCoverage") == 0 &&
                  strstr(coverage_nominal.diagnostic, "Point"),
              "nominal coverage names an unmatched visible constructor");

        Atom *coverage_effect_overlay[] = {
            parse_one(
                &queries,
                "(: incomplete-effect "
                "(-[$effect]-> (-[$effect]-> Number Number) Bool Number))"),
            parse_one(
                &queries,
                "(= (incomplete-effect $continuation True) "
                "($continuation 1))"),
        };
        CettaPettaTypecheckV3BlockResult coverage_effect = {0};
        bool coverage_effect_checked = coverage_effect_overlay[0] &&
            coverage_effect_overlay[1] &&
            cetta_petta_typecheck_v3_declaration_block(
                product_checker, product_program, &product_space,
                coverage_effect_overlay, 2u,
                CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT_DET,
                &coverage_effect);
        CHECK(coverage_effect_checked &&
                  coverage_effect.verdict == PETTA_ANALYSIS_REFUTED &&
                  strcmp(
                      coverage_effect.relation,
                      "V3RelationCoverage") == 0 &&
                  strstr(coverage_effect.diagnostic, "False"),
              "a deterministic effect instance owes whole-relation coverage");
    } else {
        fprintf(stderr, "PeTTa typecheck-v3 product diagnostic: %s\n",
                error);
    }

    memset(error, 0, sizeof error);
    bool reference_language_loaded =
        cetta_gslt_language_load_embedded_for_realization(
            &cetta_petta_typecheck_v3_core_v1,
            CETTA_GSLT_REALIZATION_HORN_REFERENCE,
            &reference_language, error, sizeof error);
    bool compiled_language_loaded = reference_language_loaded &&
        cetta_gslt_language_load_embedded_for_realization(
            &cetta_petta_typecheck_v3_core_v1,
            CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &compiled_language, error, sizeof error);
    CHECK(reference_language_loaded && compiled_language_loaded &&
              cetta_gslt_language_semantic_rule_count(reference_language) ==
                  149u &&
              cetta_gslt_language_semantic_rule_count(compiled_language) ==
                  149u,
          "the isolated v3 language loads all 149 rules in both engines");
    if (reference_language_loaded && compiled_language_loaded) {
        check_language_query(
            reference_language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
            providers, &queries, &answers,
            "isolated v3 reference entry is admitted");
        check_language_query(
            compiled_language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            providers, &queries, &answers,
            "isolated v3 compiled entry is admitted");
    } else {
        fprintf(stderr, "PeTTa typecheck-v3 language diagnostic: %s\n",
                error);
    }

cleanup:
    cetta_petta_typecheck_v3_free(product_checker);
    petta_program_free(product_program);
    if (product_registry_initialized)
        registry_free(&product_registry);
    if (product_space_initialized)
        space_free(&product_space);
    cetta_gslt_authorized_provider_registry_free_v1(&live_authorized);
    cetta_petta_type_fact_provider_free_v1(live_provider);
    petta_program_free(live_program);
    if (live_space_initialized)
        space_free(&live_space);
    cetta_gslt_language_free(compiled_language);
    cetta_gslt_language_free(reference_language);
    cetta_gslt_compiled_program_free(compiled);
    cetta_gslt_authorized_provider_registry_free_v1(&authorized);
    cetta_gslt_finite_fact_provider_set_free_v1(environment);
    cetta_gslt_horn_program_free(program);
    g_var_intern = NULL;
    g_symbols = NULL;
    arena_free(&answers);
    arena_free(&queries);
    arena_free(&facts);
    arena_free(&live_source);
    var_intern_free(&variables);
    symbol_table_free(&symbols);

    if (failures != 0u) {
        fprintf(stderr,
                "PettaTypecheckV3CoreLangdefSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(PettaTypecheckV3CoreLangdefSummary checks=%u failures=0)\n",
           checks);
    return 0;
}
