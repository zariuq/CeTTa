#include "gdl_positive_horn_host.h"
#include "gdl_finite_herbrand.h"
#include "gdl_stratified_model.h"
#include "gdl_stratification.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool expr_named(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom_is_symbol(atom->expr.elems[0], name);
}

static Atom *parse_one(Arena *arena, const char *source) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, source, &position);
    return atom && parser_rest_is_delimiters(source, &position) ? atom : NULL;
}

static size_t result_occurrence_count(const Atom *result) {
    if ((!expr_named(result, "compile-result", 5u) &&
         !expr_named(result, "compile-incomplete", 6u)))
        return SIZE_MAX;
    size_t occurrence_index = expr_named(result, "compile-result", 5u)
        ? 2u : 3u;
    Atom *occurrences = result->expr.elems[occurrence_index];
    if (!occurrences || occurrences->kind != ATOM_EXPR ||
        occurrences->expr.len == 0u ||
        !atom_is_symbol(occurrences->expr.elems[0], "occurrences"))
        return SIZE_MAX;
    return (size_t)occurrences->expr.len - 1u;
}

static Atom *first_occurrence_proof(Atom *result) {
    if (!expr_named(result, "compile-result", 5u))
        return NULL;
    Atom *occurrences = result->expr.elems[2];
    return occurrences && occurrences->kind == ATOM_EXPR &&
            occurrences->expr.len > 1u &&
            expr_named(occurrences->expr.elems[1], "occurrence", 2u)
        ? occurrences->expr.elems[1]->expr.elems[1]
        : NULL;
}

static bool atom_contains_head(
    const Atom *atom, const char *head, size_t depth) {
    if (!atom || !head || depth > 4096u)
        return false;
    if (atom->kind != ATOM_EXPR)
        return false;
    if (atom->expr.len > 0u && atom_is_symbol(atom->expr.elems[0], head))
        return true;
    for (size_t index = 0u; index < atom->expr.len; index++)
        if (atom_contains_head(atom->expr.elems[index], head, depth + 1u))
            return true;
    return false;
}

static bool stratification_relation(
    const CettaGdlStratificationV1 *analysis,
    const char *name,
    size_t arity,
    size_t *index_out,
    CettaGdlStratifiedRelationViewV1 *view_out) {
    size_t count = cetta_gdl_stratification_relation_count_v1(analysis);
    for (size_t index = 0u; index < count; index++) {
        CettaGdlStratifiedRelationViewV1 view = {0};
        if (cetta_gdl_stratification_relation_view_v1(
                analysis, index, &view) &&
            view.name && strcmp(view.name, name) == 0 &&
            view.arity == arity) {
            if (index_out)
                *index_out = index;
            if (view_out)
                *view_out = view;
            return true;
        }
    }
    return false;
}

static void check_gdl_finite_herbrand(void) {
    Arena profile_arena;
    GdlSourceProfileV1 profile = {0};
    arena_init(&profile_arena);
    GdlSourceParseV1 parsed = gdl_source_parse_profile_v1(
        &profile_arena,
        "red, blue :: agent.\n"
        "agent :> entity.\n"
        "0, 1 :: nat.\n"
        "move :: agent -> nat -> action.\n"
        "legal :: entity -> action -> bool.\n",
        &profile);
    CHECK(parsed == GDL_SOURCE_PARSE_OK_V1,
          "the finite Herbrand canary profile parses");
    CettaGdlFiniteHerbrandResultV1 result =
        parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_finite_herbrand_construct_v1(
            &profile,
            (CettaGdlFiniteHerbrandLimitsV1){
                .max_terms = 64u,
                .max_constructor_applications = 128u,
                .max_rounds = 8u,
                .max_depth = 8u,
            })
        : (CettaGdlFiniteHerbrandResultV1){0};
    gdl_source_profile_free_v1(&profile);
    arena_free(&profile_arena);
    CHECK(result.kind == CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1 &&
              result.domain &&
              cetta_gdl_finite_herbrand_term_count_v1(result.domain) == 8u &&
              cetta_gdl_finite_herbrand_relation_count_v1(
                  result.domain) == 1u,
          "typed constants and constructor products form one finite carrier");
    CHECK(cetta_gdl_finite_herbrand_type_accepts_v1(
              result.domain, "agent", "entity") &&
              !cetta_gdl_finite_herbrand_type_accepts_v1(
                  result.domain, "entity", "agent"),
          "the carrier retains the authored direction of subtyping");
    CettaGdlFiniteHerbrandRelationViewV1 relation = {0};
    CHECK(cetta_gdl_finite_herbrand_relation_view_v1(
              result.domain, 0u, &relation) &&
              strcmp(relation.name, "legal") == 0 &&
              relation.argument_count == 2u &&
              strcmp(relation.argument_types[0], "entity") == 0 &&
              strcmp(relation.argument_types[1], "action") == 0,
          "Bool signatures remain relation views rather than ground terms");
    Arena term_arena;
    arena_init(&term_arena);
    Atom *move_red_0 = parse_one(&term_arena, "(move red 0)");
    size_t move_index = SIZE_MAX;
    CettaGdlFiniteHerbrandTermViewV1 move_view = {0};
    CettaGdlFiniteHerbrandConstructionViewV1 move_construction = {0};
    CHECK(cetta_gdl_finite_herbrand_find_term_v1(
              result.domain, move_red_0, "action", &move_index) &&
              cetta_gdl_finite_herbrand_term_view_v1(
                  result.domain, move_index, &move_view) &&
              move_view.depth == 1u && move_view.construction_count == 1u &&
              cetta_gdl_finite_herbrand_construction_view_v1(
                  result.domain, move_index, 0u, &move_construction) &&
              move_construction.argument_count == 2u,
          "a compound member retains its exact constructor and arguments");
    arena_free(&term_arena);
    CettaGdlFiniteHerbrandStatsV1 stats = {0};
    CHECK(cetta_gdl_finite_herbrand_stats_v1(result.domain, &stats) &&
              stats.terms == 8u && stats.constructors == 5u &&
              stats.relations == 1u && stats.maximum_depth == 1u,
          "finite carrier statistics expose the completed support geometry");
    cetta_gdl_finite_herbrand_destroy_v1(result.domain);

    arena_init(&profile_arena);
    memset(&profile, 0, sizeof(profile));
    parsed = gdl_source_parse_profile_v1(
        &profile_arena,
        "zero :: nat.\n"
        "zero :: even.\n"
        "even :> nat.\n"
        "box :: nat -> wrapped.\n"
        "holds :: wrapped -> bool.\n",
        &profile);
    result = parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_finite_herbrand_construct_v1(
            &profile,
            (CettaGdlFiniteHerbrandLimitsV1){
                .max_terms = 32u,
                .max_constructor_applications = 64u,
                .max_rounds = 8u,
                .max_depth = 8u,
            })
        : (CettaGdlFiniteHerbrandResultV1){0};
    gdl_source_profile_free_v1(&profile);
    arena_free(&profile_arena);
    arena_init(&term_arena);
    Atom *boxed_zero = parse_one(&term_arena, "(box zero)");
    Atom *zero = parse_one(&term_arena, "zero");
    size_t boxed_index = SIZE_MAX;
    size_t zero_matches[2] = {SIZE_MAX, SIZE_MAX};
    memset(&move_view, 0, sizeof(move_view));
    CHECK(result.kind == CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1 &&
              result.domain &&
              cetta_gdl_finite_herbrand_find_term_v1(
                  result.domain, boxed_zero, "wrapped", &boxed_index) &&
              cetta_gdl_finite_herbrand_term_view_v1(
                  result.domain, boxed_index, &move_view) &&
              move_view.construction_count == 2u,
          "equal typed support retains both overloaded construction witnesses");
    CHECK(cetta_gdl_finite_herbrand_matching_terms_v1(
              result.domain, zero, "nat", zero_matches, 2u) == 2u &&
              zero_matches[0] != zero_matches[1] &&
              cetta_gdl_finite_herbrand_matching_terms_v1(
                  result.domain, zero, "even", NULL, 0u) == 1u,
          "typed structural lookup retains every exact subtype-indexed substitution");
    arena_free(&term_arena);
    cetta_gdl_finite_herbrand_destroy_v1(result.domain);

    arena_init(&profile_arena);
    memset(&profile, 0, sizeof(profile));
    parsed = gdl_source_parse_profile_v1(
        &profile_arena,
        "z :: nat.\n"
        "s :: nat -> nat.\n"
        "p :: nat -> bool.\n",
        &profile);
    result = parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_finite_herbrand_construct_v1(
            &profile,
            (CettaGdlFiniteHerbrandLimitsV1){
                .max_terms = 64u,
                .max_constructor_applications = 64u,
                .max_rounds = 16u,
                .max_depth = 3u,
            })
        : (CettaGdlFiniteHerbrandResultV1){0};
    gdl_source_profile_free_v1(&profile);
    arena_free(&profile_arena);
    CHECK(result.kind == CETTA_GDL_FINITE_HERBRAND_INCOMPLETE_V1 &&
              result.domain &&
              cetta_gdl_finite_herbrand_term_count_v1(result.domain) == 4u,
          "productive recursive data retains a frontier and never claims finite completion");
    cetta_gdl_finite_herbrand_destroy_v1(result.domain);

    arena_init(&profile_arena);
    memset(&profile, 0, sizeof(profile));
    parsed = gdl_source_parse_profile_v1(
        &profile_arena,
        "s :: nat -> nat.\n"
        "p :: nat -> bool.\n",
        &profile);
    result = parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_finite_herbrand_construct_v1(
            &profile,
            (CettaGdlFiniteHerbrandLimitsV1){
                .max_terms = 8u,
                .max_constructor_applications = 8u,
                .max_rounds = 4u,
                .max_depth = 4u,
            })
        : (CettaGdlFiniteHerbrandResultV1){0};
    gdl_source_profile_free_v1(&profile);
    arena_free(&profile_arena);
    CHECK(result.kind == CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1 &&
              result.domain &&
              cetta_gdl_finite_herbrand_term_count_v1(result.domain) == 0u,
          "unseeded recursive constructors have the completed empty ground carrier");
    cetta_gdl_finite_herbrand_destroy_v1(result.domain);
}

static void check_gdl_stratification(void) {
    Arena arena;
    GdlSourceRawFormsV1 forms = {0};
    arena_init(&arena);
    GdlSourceParseV1 parsed = gdl_source_parse_forms_v1(
        &arena,
        "(p)\n"
        "(<= q p)\n"
        "(<= r (not q))\n"
        "(<= s (or q r))\n"
        "(<= (next (at ?x)) (next_at ?x))\n"
        "(<= (next_at ?x) (not (blocked ?x)))\n",
        128u, &forms);
    CHECK(parsed == GDL_SOURCE_PARSE_OK_V1,
          "the authored stratification canary parses");
    CettaGdlStratificationResultV1 stratified =
        parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_stratification_construct_v1(
            &forms, (CettaGdlStratificationLimitsV1){0})
        : (CettaGdlStratificationResultV1){0};
    CHECK(stratified.kind == CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 &&
              stratified.analysis &&
              cetta_gdl_stratification_relation_count_v1(
                  stratified.analysis) == 7u &&
              cetta_gdl_stratification_edge_count_v1(
                  stratified.analysis) == 6u &&
              cetta_gdl_stratification_maximum_stratum_v1(
                  stratified.analysis) == 1u,
          "native GDL constructs the least dependency strata");
    CettaGdlStratifiedRelationViewV1 p = {0};
    CettaGdlStratifiedRelationViewV1 q = {0};
    CettaGdlStratifiedRelationViewV1 r = {0};
    CettaGdlStratifiedRelationViewV1 s = {0};
    size_t next_index = SIZE_MAX;
    size_t next_at_index = SIZE_MAX;
    CHECK(stratification_relation(
              stratified.analysis, "p", 0u, NULL, &p) &&
              stratification_relation(
                  stratified.analysis, "q", 0u, NULL, &q) &&
              stratification_relation(
                  stratified.analysis, "r", 0u, NULL, &r) &&
              stratification_relation(
                  stratified.analysis, "s", 0u, NULL, &s) &&
              p.stratum == 0u && q.stratum == 0u &&
              r.stratum == 1u && s.stratum == 1u,
          "negative dependencies raise only their dependent strata");
    CHECK(stratification_relation(
              stratified.analysis, "next", 1u, &next_index, NULL) &&
              stratification_relation(
                  stratified.analysis, "next_at", 1u,
                  &next_at_index, NULL) &&
              next_index != next_at_index,
          "authored predicates never flatten into nested term constructors");
    bool first_branch = false;
    bool second_branch = false;
    for (size_t edge_index = 0u;
         edge_index < cetta_gdl_stratification_edge_count_v1(
             stratified.analysis); edge_index++) {
        CettaGdlDependencyEdgeViewV1 edge = {0};
        if (!cetta_gdl_stratification_edge_view_v1(
                stratified.analysis, edge_index, &edge) ||
            edge.path_length != 2u || edge.path[0] != 2u)
            continue;
        first_branch = first_branch || edge.path[1] == 1u;
        second_branch = second_branch || edge.path[1] == 2u;
    }
    CHECK(first_branch && second_branch,
          "disjunction retains both authored branch occurrences");
    cetta_gdl_stratification_destroy_v1(stratified.analysis);
    gdl_source_raw_forms_free_v1(&forms);
    arena_free(&arena);

    arena_init(&arena);
    memset(&forms, 0, sizeof(forms));
    parsed = gdl_source_parse_forms_v1(
        &arena, "(<= p q)\n(<= q p)\n", 64u, &forms);
    CettaGdlStratificationResultV1 positive_cycle =
        parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_stratification_construct_v1(
            &forms, (CettaGdlStratificationLimitsV1){0})
        : (CettaGdlStratificationResultV1){0};
    CHECK(positive_cycle.kind ==
                  CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 &&
              positive_cycle.analysis &&
              cetta_gdl_stratification_maximum_stratum_v1(
                  positive_cycle.analysis) == 0u,
          "positive recursive components remain in one legal stratum");
    cetta_gdl_stratification_destroy_v1(positive_cycle.analysis);
    gdl_source_raw_forms_free_v1(&forms);
    arena_free(&arena);

    arena_init(&arena);
    memset(&forms, 0, sizeof(forms));
    parsed = gdl_source_parse_forms_v1(
        &arena,
        "(<= p (not q))\n(<= q p)\n",
        64u, &forms);
    CettaGdlStratificationResultV1 negative_cycle =
        parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_stratification_construct_v1(
            &forms, (CettaGdlStratificationLimitsV1){0})
        : (CettaGdlStratificationResultV1){0};
    bool negative_edge = false;
    for (size_t index = 0u;
         negative_cycle.analysis &&
         index < cetta_gdl_stratification_negative_cycle_length_v1(
             negative_cycle.analysis); index++) {
        CettaGdlDependencyEdgeViewV1 edge = {0};
        if (cetta_gdl_stratification_negative_cycle_edge_v1(
                negative_cycle.analysis, index, &edge))
            negative_edge = negative_edge || edge.negative;
    }
    CHECK(negative_cycle.kind ==
                  CETTA_GDL_STRATIFICATION_REFUTED_NEGATIVE_CYCLE_V1 &&
              negative_cycle.analysis && negative_edge,
          "a negative cycle is refuted with its checked edge obstruction");
    cetta_gdl_stratification_destroy_v1(negative_cycle.analysis);

    CettaGdlStratificationResultV1 bounded =
        parsed == GDL_SOURCE_PARSE_OK_V1
        ? cetta_gdl_stratification_construct_v1(
            &forms,
            (CettaGdlStratificationLimitsV1){
                .max_relations = 1u,
                .max_edges = 8u,
                .max_logical_depth = 8u,
            })
        : (CettaGdlStratificationResultV1){0};
    CHECK(bounded.kind == CETTA_GDL_STRATIFICATION_INCOMPLETE_V1 &&
              bounded.analysis == NULL,
          "a finite graph bound yields Incomplete rather than refutation");
    gdl_source_raw_forms_free_v1(&forms);
    arena_free(&arena);
}

static void check_gdl_stratified_model(Atom *source) {
    GdlSourcePackageV1 package = {0};
    Arena presentation_arena;
    Arena query_arena;
    GdlSourceRawFormsV1 forms = {0};
    GdlSourceProfileV1 profile = {0};
    CettaGdlTypeOfNativeV1 *typing = NULL;
    CettaGdlFiniteHerbrandV1 *herbrand = NULL;
    CettaGdlStratificationV1 *stratification = NULL;
    CettaGdlStratifiedModelV1 *model = NULL;
    CettaGdlStratifiedModelV1 *bounded_model = NULL;
    CettaGdlStratifiedEpisodeV1 *episode = NULL;
    CettaGdlStratifiedEpisodeV1 *bounded_episode = NULL;
    arena_init(&presentation_arena);
    arena_init(&query_arena);

    GdlSourceParseV1 package_kind = gdl_source_package_view_v1(
        source, NULL, NULL, NULL, &package);
    CHECK(package_kind == GDL_SOURCE_PARSE_OK_V1,
          "the stratified-model canary has an exact authored package");
    if (package_kind != GDL_SOURCE_PARSE_OK_V1)
        goto done;

    GdlSourceParseV1 forms_kind = gdl_source_parse_forms_v1(
        &presentation_arena, package.source_text, 128u, &forms);
    GdlSourceParseV1 profile_kind = gdl_source_parse_profile_v1(
        &presentation_arena, package.profile_text, &profile);
    CHECK(forms_kind == GDL_SOURCE_PARSE_OK_V1 &&
              profile_kind == GDL_SOURCE_PARSE_OK_V1 &&
              forms.count == 8u && forms.foreign_lines == 0u,
          "the canary source and profile form one authority-free presentation");
    if (forms_kind != GDL_SOURCE_PARSE_OK_V1 ||
        profile_kind != GDL_SOURCE_PARSE_OK_V1)
        goto done;

    CettaGdlTypeOfNativeAdmissionV1 typing_admission =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source, (CettaGdlTypeOfNativeLimitsV1){0});
    typing = typing_admission.native;
    CettaGdlFiniteHerbrandResultV1 herbrand_result =
        cetta_gdl_finite_herbrand_construct_v1(
            &profile, (CettaGdlFiniteHerbrandLimitsV1){0});
    herbrand = herbrand_result.domain;
    CettaGdlStratificationResultV1 stratification_result =
        cetta_gdl_stratification_construct_v1(
            &forms, (CettaGdlStratificationLimitsV1){0});
    stratification = stratification_result.analysis;
    CHECK(typing_admission.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              typing &&
              herbrand_result.kind ==
                  CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1 &&
              herbrand &&
              cetta_gdl_finite_herbrand_term_count_v1(herbrand) == 2u &&
              stratification_result.kind ==
                  CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 &&
              stratification &&
              cetta_gdl_stratification_maximum_stratum_v1(
                  stratification) == 1u,
          "typing, finite support, and stratification independently admit the canary");
    if (!typing || !herbrand || !stratification)
        goto done;

    CettaGdlStratifiedModelResultV1 constructed =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    model = constructed.model;
    CettaGdlStratifiedModelStatsV1 stats = {0};
    CHECK(constructed.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1 &&
              model &&
              cetta_gdl_stratified_model_stats_v1(model, &stats) &&
              stats.source_forms == 8u &&
              stats.source_rules == 5u &&
              stats.source_facts == 3u &&
              stats.assignments == 10u &&
              stats.branch_expansions == 2u &&
              stats.ground_instances == 13u &&
              stats.distinct_checks == 2u &&
              stats.support_nodes == 9u &&
              stats.proof_edges == 11u &&
              stats.positive_premise_references == 8u &&
              stats.absence_receipts == 1u &&
              stats.rounds == 5u &&
              stats.completed_strata == 2u,
          "least-support evaluation retains exact construction and evidence counts");
    if (!model)
        goto done;

    CettaNikDirectAuthorityTokenV1 source_token = {0};
    CettaNikImplementationSelectionV1 source_selection = {0};
    uint64_t source_realization = 0u;
    const char *source_digest = NULL;
    const char *profile_digest = NULL;
    const char *source_revision = NULL;
    CHECK(cetta_nik_direct_authority_v1_is_valid(
              cetta_gdl_stratified_model_authority_v1()) &&
              cetta_gdl_stratified_model_token_v1(
                  model, &source_token) &&
              cetta_gdl_stratified_model_token_is_current_v1(
                  model, &source_token) &&
              cetta_gdl_stratified_model_identity_v1(
                  model, &source_digest, &profile_digest,
                  &source_revision) &&
              source_digest && profile_digest && source_revision &&
              cetta_gdl_stratified_model_selection_v1(
                  model, &source_selection, &source_realization) &&
              source_selection.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
              source_selection.eligible_count == 1u &&
              source_selection.frontier_count == 1u &&
              source_realization ==
                  cetta_gdl_stratified_model_authority_v1()
                      ->realization_identity,
          "the completed source model is one current request-local native calculus");

    Atom *reach_a = parse_one(&query_arena, "(reach a)");
    Atom *loop_a = parse_one(&query_arena, "(loop a)");
    Atom *open_a = parse_one(&query_arena, "(open a)");
    Atom *open_b = parse_one(&query_arena, "(open b)");
    Atom *same_a = parse_one(&query_arena, "(same a)");
    Atom *same_b = parse_one(&query_arena, "(same b)");
    Atom *blocked_a = parse_one(&query_arena, "(blocked a)");
    Atom *a = parse_one(&query_arena, "a");
    Atom *b = parse_one(&query_arena, "b");
    size_t reach_a_index = SIZE_MAX;
    size_t loop_a_index = SIZE_MAX;
    size_t open_a_index = SIZE_MAX;
    size_t same_a_index = SIZE_MAX;
    size_t ignored = SIZE_MAX;
    CHECK(cetta_gdl_stratified_model_find_support_v1(
              model, reach_a, &reach_a_index) &&
              cetta_gdl_stratified_model_find_support_v1(
                  model, loop_a, &loop_a_index) &&
              cetta_gdl_stratified_model_find_support_v1(
                  model, open_a, &open_a_index) &&
              !cetta_gdl_stratified_model_find_support_v1(
                  model, open_b, &ignored) &&
              cetta_gdl_stratified_model_find_support_v1(
                  model, same_a, &same_a_index) &&
              !cetta_gdl_stratified_model_find_support_v1(
                  model, same_b, &ignored) &&
              !cetta_gdl_stratified_model_find_support_v1(
                  model, blocked_a, &ignored),
          "the completed support quotient contains exactly the expected literals");

    CettaGdlStratifiedSupportViewV1 reach_view = {0};
    bool reach_from_seed = false;
    bool reach_from_loop = false;
    CHECK(cetta_gdl_stratified_model_support_view_v1(
              model, reach_a_index, &reach_view) &&
              reach_view.proof_edge_count == 2u,
          "one support member retains both acyclic and cyclic derivation edges");
    for (size_t index = 0u; index < reach_view.proof_edge_count; index++) {
        CettaGdlStratifiedProofEdgeViewV1 edge = {0};
        if (!cetta_gdl_stratified_model_proof_edge_view_v1(
                model, reach_view.proof_edge_indices[index], &edge))
            continue;
        if (edge.source_form_ordinal == 4u)
            reach_from_seed =
                edge.origin ==
                    CETTA_GDL_STRATIFIED_PROOF_AUTHORED_SOURCE_V1 &&
                edge.episode_occurrence == NULL &&
                edge.substitution_count == 1u &&
                edge.branch_choice_count == 1u &&
                edge.branch_choices[0].alternative == 1u &&
                edge.branch_choices[0].source_literal_proof_count != 0u &&
                edge.positive_premise_count == 1u &&
                edge.positive_premises[0].source_literal_proof_count != 0u;
        if (edge.source_form_ordinal == 8u)
            reach_from_loop =
                edge.branch_choice_count == 0u &&
                edge.positive_premise_count == 1u &&
                edge.positive_premises[0].support_index == loop_a_index;
    }
    CettaGdlStratifiedSupportViewV1 loop_view = {0};
    bool loop_closes_cycle = false;
    if (cetta_gdl_stratified_model_support_view_v1(
            model, loop_a_index, &loop_view))
        for (size_t index = 0u; index < loop_view.proof_edge_count; index++) {
            CettaGdlStratifiedProofEdgeViewV1 edge = {0};
            if (cetta_gdl_stratified_model_proof_edge_view_v1(
                    model, loop_view.proof_edge_indices[index], &edge) &&
                edge.source_form_ordinal == 7u &&
                edge.positive_premise_count == 1u &&
                edge.positive_premises[0].support_index == reach_a_index)
                loop_closes_cycle = true;
        }
    CHECK(reach_from_seed && reach_from_loop && loop_closes_cycle,
          "positive recursion is a finite occurrence-bearing proof graph, not an unfolded tree");

    CettaGdlStratifiedSupportViewV1 open_view = {0};
    CettaGdlStratifiedProofEdgeViewV1 open_edge = {0};
    bool open_edge_found =
        cetta_gdl_stratified_model_support_view_v1(
            model, open_a_index, &open_view) &&
        open_view.proof_edge_count == 1u &&
        cetta_gdl_stratified_model_proof_edge_view_v1(
            model, open_view.proof_edge_indices[0], &open_edge);
    CettaGdlStratifiedRelationViewV1 absent_relation = {0};
    bool absence_is_blocked = open_edge_found &&
        open_edge.absence_receipt_count == 1u &&
        cetta_gdl_stratification_relation_view_v1(
            stratification, open_edge.absence_receipts[0].relation_index,
            &absent_relation) &&
        strcmp(absent_relation.name, "blocked") == 0;
    CHECK(open_edge_found && absence_is_blocked &&
              open_edge.positive_premise_count == 1u &&
              open_edge.absence_receipts[0].completed_stratum == 0u &&
              open_edge.absence_receipts[0]
                      .completed_relation_support_count == 1u &&
              atom_eq(open_edge.absence_receipts[0].literal, blocked_a) &&
              open_edge.absence_receipts[0].source_not_proof_count != 0u &&
              open_edge.absence_receipts[0]
                      .source_operand_proof_count != 0u,
          "negation carries checked source evidence and completed lower-stratum absence");

    CettaGdlStratifiedSupportViewV1 same_view = {0};
    CettaGdlStratifiedProofEdgeViewV1 same_edge = {0};
    bool same_edge_found =
        cetta_gdl_stratified_model_support_view_v1(
            model, same_a_index, &same_view) &&
        same_view.proof_edge_count == 1u &&
        cetta_gdl_stratified_model_proof_edge_view_v1(
            model, same_view.proof_edge_indices[0], &same_edge);
    CHECK(same_edge_found && same_edge.distinct_evidence_count == 1u &&
              atom_eq(same_edge.distinct_evidence[0].left, a) &&
              atom_eq(same_edge.distinct_evidence[0].right, b) &&
              same_edge.distinct_evidence[0].source_literal_proof_count != 0u,
          "a successful distinct premise retains its exact inequality evidence");

    Atom *episode_identity = parse_one(
        &query_arena, "(gdl:episode stratified-canary state-1)");
    Atom *episode_facts[] = {
        parse_one(&query_arena, "(alt b)"),
        parse_one(&query_arena, "(alt b)"),
        parse_one(&query_arena, "(blocked a)"),
    };
    CettaGdlStratifiedEpisodeResultV1 episode_result =
        cetta_gdl_stratified_model_admit_episode_v1(
            model, &source_token, episode_identity, episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlStratifiedEpisodeLimitsV1){0});
    episode = episode_result.episode;
    const CettaGdlStratifiedModelV1 *episode_model =
        cetta_gdl_stratified_episode_model_v1(episode);
    CettaGdlStratifiedEpisodeStatsV1 episode_stats = {0};
    CettaNikDirectAuthorityTokenV1 episode_token = {0};
    const char *episode_digest = NULL;
    const char *episode_revision = NULL;
    CHECK(episode_result.kind ==
                  CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1 &&
              episode && episode_model &&
              cetta_gdl_stratified_episode_stats_v1(
                  episode, &episode_stats) &&
              episode_stats.authored_facts == 3u &&
              episode_stats.typing_proof_occurrences == 3u &&
              episode_stats.seeded_support_nodes == 2u &&
              episode_stats.seeded_proof_edges == 3u &&
              cetta_gdl_stratified_episode_token_v1(
                  episode, &episode_token) &&
              cetta_gdl_stratified_episode_token_is_current_v1(
                  episode, &episode_token) &&
              !cetta_nik_direct_authority_token_v1_equal(
                  &source_token, &episode_token) &&
              cetta_gdl_stratified_episode_identity_v1(
                  episode, &episode_digest, &episode_revision) &&
              episode_digest && strlen(episode_digest) == 64u &&
              episode_revision && *episode_revision,
          "typed episode facts construct a distinct current native model revision");

    Atom *alt_b = parse_one(&query_arena, "(alt b)");
    size_t alt_b_index = SIZE_MAX;
    CettaGdlStratifiedSupportViewV1 alt_b_view = {0};
    bool typed_episode_edges = episode_model &&
        cetta_gdl_stratified_model_find_support_v1(
            episode_model, alt_b, &alt_b_index) &&
        cetta_gdl_stratified_model_support_view_v1(
            episode_model, alt_b_index, &alt_b_view) &&
        alt_b_view.proof_edge_count == 2u;
    Atom *first_episode_occurrence = NULL;
    Atom *second_episode_occurrence = NULL;
    if (typed_episode_edges)
        for (size_t index = 0u; index < alt_b_view.proof_edge_count; index++) {
            CettaGdlStratifiedProofEdgeViewV1 edge = {0};
            if (!cetta_gdl_stratified_model_proof_edge_view_v1(
                    episode_model, alt_b_view.proof_edge_indices[index],
                    &edge) ||
                edge.origin !=
                    CETTA_GDL_STRATIFIED_PROOF_TYPED_EPISODE_FACT_V1 ||
                edge.source_form_ordinal != 0u ||
                !edge.episode_occurrence ||
                !atom_contains_head(
                    edge.source_head_proof,
                    "gdl:native-ground-literal-v1", 0u)) {
                typed_episode_edges = false;
                break;
            }
            if (index == 0u)
                first_episode_occurrence = edge.episode_occurrence;
            else
                second_episode_occurrence = edge.episode_occurrence;
        }
    CHECK(typed_episode_edges && first_episode_occurrence &&
              second_episode_occurrence &&
              !atom_eq(first_episode_occurrence,
                       second_episode_occurrence),
          "duplicate episode facts retain distinct native proof occurrences");

    size_t episode_open_index = SIZE_MAX;
    CHECK(episode_model &&
              !cetta_gdl_stratified_model_find_support_v1(
                  episode_model, open_a, &episode_open_index) &&
              !cetta_gdl_stratified_model_find_support_v1(
                  episode_model, open_b, &episode_open_index) &&
              cetta_gdl_stratified_model_find_support_v1(
                  model, open_a, &open_a_index),
          "episode facts participate before lower-stratum completion without mutating the source model");

    CettaNikDirectAuthorityTokenV1 stale_source_token = source_token;
    stale_source_token.words[stale_source_token.length - 1u] ^=
        UINT64_C(1);
    CettaGdlStratifiedEpisodeResultV1 stale_episode =
        cetta_gdl_stratified_model_admit_episode_v1(
            model, &stale_source_token, episode_identity,
            episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlStratifiedEpisodeLimitsV1){0});
    CHECK(stale_episode.kind == CETTA_GDL_STRATIFIED_EPISODE_STALE_V1 &&
              stale_episode.episode == NULL,
          "a stale source model cannot mint an episode realization");
    cetta_gdl_stratified_episode_destroy_v1(stale_episode.episode);

    Atom *open_episode_facts[] = {
        parse_one(&query_arena, "(alt $value)"),
    };
    CettaGdlStratifiedEpisodeResultV1 outside_episode =
        cetta_gdl_stratified_model_admit_episode_v1(
            model, &source_token, episode_identity,
            open_episode_facts, 1u,
            (CettaGdlStratifiedEpisodeLimitsV1){0});
    Atom *ill_typed_episode_facts[] = {
        parse_one(&query_arena, "(alt c)"),
    };
    CettaGdlStratifiedEpisodeResultV1 ill_typed_episode =
        cetta_gdl_stratified_model_admit_episode_v1(
            model, &source_token, episode_identity,
            ill_typed_episode_facts, 1u,
            (CettaGdlStratifiedEpisodeLimitsV1){0});
    CHECK(outside_episode.kind ==
                  CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1 &&
              outside_episode.episode == NULL &&
              ill_typed_episode.kind ==
                  CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1 &&
              ill_typed_episode.episode == NULL,
          "open and ill-typed facts remain outside the native episode boundary");
    cetta_gdl_stratified_episode_destroy_v1(outside_episode.episode);
    cetta_gdl_stratified_episode_destroy_v1(ill_typed_episode.episode);

    CettaGdlStratifiedEpisodeResultV1 bounded_fact_episode =
        cetta_gdl_stratified_model_admit_episode_v1(
            model, &source_token, episode_identity,
            episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlStratifiedEpisodeLimitsV1){.max_facts = 2u});
    CHECK(bounded_fact_episode.kind ==
                  CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1 &&
              bounded_fact_episode.episode == NULL,
          "an episode ingress bound is Incomplete rather than rejection");
    cetta_gdl_stratified_episode_destroy_v1(
        bounded_fact_episode.episode);

    CettaGdlStratifiedEpisodeResultV1 bounded_episode_result =
        cetta_gdl_stratified_model_admit_episode_v1(
            model, &source_token, episode_identity,
            episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlStratifiedEpisodeLimitsV1){
                .evaluation = {.max_rounds = 1u},
            });
    bounded_episode = bounded_episode_result.episode;
    CettaGdlStratifiedModelStatsV1 bounded_episode_model_stats = {0};
    CettaNikDirectAuthorityTokenV1 forbidden_partial_token = {0};
    CHECK(bounded_episode_result.kind ==
                  CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1 &&
              bounded_episode &&
              cetta_gdl_stratified_model_stats_v1(
                  cetta_gdl_stratified_episode_model_v1(bounded_episode),
                  &bounded_episode_model_stats) &&
              bounded_episode_model_stats.support_nodes != 0u &&
              bounded_episode_model_stats.proof_edges != 0u &&
              bounded_episode_model_stats.completed_strata == 0u &&
              bounded_episode_model_stats.absence_receipts == 0u &&
              !cetta_gdl_stratified_episode_token_v1(
                  bounded_episode, &forbidden_partial_token),
          "bounded episode evaluation retains partial proofs but no completion token or absence");

    CettaGdlStratifiedModelResultV1 bounded =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source,
            (CettaGdlStratifiedModelAdmissionLimitsV1){
                .evaluation = {.max_rounds = 1u},
            });
    bounded_model = bounded.model;
    CettaGdlStratifiedModelStatsV1 bounded_stats = {0};
    CHECK(bounded.kind == CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1 &&
              bounded_model &&
              cetta_gdl_stratified_model_stats_v1(
                  bounded_model, &bounded_stats) &&
              bounded_stats.support_nodes != 0u &&
              bounded_stats.proof_edges != 0u &&
              bounded_stats.completed_strata == 0u &&
              bounded_stats.absence_receipts == 0u,
          "a round bound retains partial proofs but cannot mint lower-stratum absence");

done:
    cetta_gdl_stratified_episode_destroy_v1(bounded_episode);
    cetta_gdl_stratified_episode_destroy_v1(episode);
    cetta_gdl_stratified_model_destroy_v1(bounded_model);
    cetta_gdl_stratified_model_destroy_v1(model);
    cetta_gdl_stratification_destroy_v1(stratification);
    cetta_gdl_finite_herbrand_destroy_v1(herbrand);
    cetta_gdl_type_of_native_destroy_v1(typing);
    gdl_source_profile_free_v1(&profile);
    gdl_source_raw_forms_free_v1(&forms);
    arena_free(&query_arena);
    arena_free(&presentation_arena);
}

static void check_gdl_stratified_support_growth(Atom *source) {
    CettaGdlStratifiedModelResultV1 result =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CettaGdlStratifiedModelV1 *model = result.model;
    CettaGdlStratifiedModelStatsV1 stats = {0};
    CHECK(result.kind == CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1 &&
              model &&
              cetta_gdl_stratified_model_stats_v1(model, &stats) &&
              stats.source_forms == 5u &&
              stats.source_rules == 2u &&
              stats.source_facts == 3u &&
              stats.branch_expansions == 0u &&
              stats.ground_instances == 165u &&
              stats.distinct_checks == 162u &&
              stats.support_nodes == 75u &&
              stats.proof_edges == 111u &&
              stats.positive_premise_references == 432u &&
              stats.absence_receipts == 0u &&
              stats.rounds == 3u &&
              stats.completed_strata == 1u,
          "support growth preserves every typed grounding and proof edge");
    if (!model)
        goto done;

    Arena query_arena;
    arena_init(&query_arena);
    const char *required_literals[] = {
        "(distinctcell 1 1 1 2)",
        "(distinctcell 1 2 1 1)",
        "(distinctcell 2 1 2 2)",
        "(distinctcell 2 2 2 1)",
        "(distinctcell 3 1 3 2)",
        "(distinctcell 3 2 3 1)",
    };
    bool all_required = true;
    for (size_t index = 0u;
         index < sizeof(required_literals) / sizeof(required_literals[0]);
         index++) {
        Atom *literal = parse_one(&query_arena, required_literals[index]);
        size_t support_index = SIZE_MAX;
        all_required = all_required && literal &&
            cetta_gdl_stratified_model_find_support_v1(
                model, literal, &support_index);
    }
    CHECK(all_required,
          "later frontier joins retain supports across model growth");

    Atom *overlap = parse_one(
        &query_arena, "(distinctcell 1 3 2 1)");
    size_t overlap_index = SIZE_MAX;
    CettaGdlStratifiedSupportViewV1 overlap_view = {0};
    CHECK(overlap &&
              cetta_gdl_stratified_model_find_support_v1(
                  model, overlap, &overlap_index) &&
              cetta_gdl_stratified_model_support_view_v1(
                  model, overlap_index, &overlap_view) &&
              overlap_view.proof_edge_count == 2u,
          "one support retains distinct derivations from both source rules");
    arena_free(&query_arena);

done:
    cetta_gdl_stratified_model_destroy_v1(model);
}

static void check_gdl_stratified_negative_cycle(Atom *source) {
    CettaGdlStratifiedModelResultV1 result =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    bool has_negative_edge = false;
    for (size_t index = 0u;
         result.negative_cycle_obstruction &&
         index < cetta_gdl_stratification_negative_cycle_length_v1(
             result.negative_cycle_obstruction); index++) {
        CettaGdlDependencyEdgeViewV1 edge = {0};
        if (cetta_gdl_stratification_negative_cycle_edge_v1(
                result.negative_cycle_obstruction, index, &edge))
            has_negative_edge = has_negative_edge || edge.negative;
    }
    CHECK(result.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_REFUTED_NEGATIVE_CYCLE_V1 &&
              !result.model && result.negative_cycle_obstruction &&
              has_negative_edge,
          "non-stratification is Refuted only with its checked negative-cycle obstruction");
    cetta_gdl_stratification_destroy_v1(
        result.negative_cycle_obstruction);
}

static void check_gdl_stratified_ambiguous_overload(Atom *source) {
    CettaGdlStratifiedModelResultV1 result =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CHECK(result.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1 &&
              !result.model && !result.negative_cycle_obstruction,
          "an unresolved overloaded source type abstains without becoming a refutation");
    cetta_gdl_stratified_model_destroy_v1(result.model);
}

static void check_gdl_stratified_false_source_distinct(Atom *source) {
    CettaGdlStratifiedModelResultV1 result =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CHECK(result.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1 &&
              !result.model && !result.negative_cycle_obstruction,
          "a reflexive top-level distinct cannot mint structural evidence");
    cetta_gdl_stratified_model_destroy_v1(result.model);
}

static void check_gdl_rule_variable_greatest(Atom *source) {
    CettaGdlTypeOfNativeAdmissionV1 typing =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source, (CettaGdlTypeOfNativeLimitsV1){0});
    CettaGdlRuleVariableSelectionV1 selection = {0};
    CHECK(typing.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              typing.native &&
              cetta_gdl_type_of_native_rule_variable_selection_v1(
                  typing.native, &selection) &&
              selection.kind ==
                  CETTA_GDL_RULE_VARIABLE_UNIQUE_GREATEST_V1 &&
              selection.component_count == 1u &&
              selection.candidate_count == 2u &&
              selection.ambiguous_component_count == 1u &&
              selection.greatest_component_count == 1u,
          "a subtype-ordered rule-variable fibre selects its checked greatest type");

    CettaGdlStratifiedModelResultV1 result =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    Arena query_arena;
    arena_init(&query_arena);
    Atom *copy_blank = parse_one(&query_arena, "(copy blank)");
    Atom *copy_red = parse_one(&query_arena, "(copy red)");
    size_t copy_blank_index = SIZE_MAX;
    size_t copy_red_index = SIZE_MAX;
    CHECK(result.kind == CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1 &&
              result.model &&
              cetta_gdl_stratified_model_find_support_v1(
                  result.model, copy_blank, &copy_blank_index) &&
              cetta_gdl_stratified_model_find_support_v1(
                  result.model, copy_red, &copy_red_index),
          "the greatest checked type retains both supertype and subtype substitutions");
    arena_free(&query_arena);
    cetta_gdl_stratified_model_destroy_v1(result.model);
    cetta_gdl_type_of_native_destroy_v1(typing.native);
}

static void check_gdl_rule_variable_incomparable(Atom *source) {
    CettaGdlTypeOfNativeAdmissionV1 typing =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source, (CettaGdlTypeOfNativeLimitsV1){0});
    CettaGdlRuleVariableSelectionV1 selection = {0};
    CHECK(typing.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              typing.native &&
              cetta_gdl_type_of_native_rule_variable_selection_v1(
                  typing.native, &selection) &&
              selection.kind ==
                  CETTA_GDL_RULE_VARIABLE_NO_COMMON_GREATEST_V1 &&
              selection.component_count == 1u &&
              selection.candidate_count > 1u &&
              selection.ambiguous_component_count == 1u &&
              selection.greatest_component_count == 0u,
          "an incomparable rule-variable fibre remains a visible frontier");
    CettaGdlStratifiedModelResultV1 result =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CHECK(result.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1 &&
              !result.model && !result.negative_cycle_obstruction,
          "the direct model abstains instead of breaking an incomparable frontier by order");
    cetta_gdl_stratified_model_destroy_v1(result.model);
    cetta_gdl_type_of_native_destroy_v1(typing.native);
}

static void check_gdl_target_fibre(Atom *source) {
    CettaGdlTypeOfNativeAdmissionV1 whole_typing =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source, (CettaGdlTypeOfNativeLimitsV1){0});
    CettaGdlStratifiedModelResultV1 whole_model =
        cetta_gdl_stratified_model_admit_authored_source_v1(
            source, (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CHECK(whole_typing.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              !whole_typing.native &&
              whole_model.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1 &&
              !whole_model.model,
          "an unrelated authored type obstruction prevents only whole-source admission");

    CettaGdlTypeOfNativeAdmissionV1 goal_typing =
        cetta_gdl_type_of_native_admit_authored_target_v1(
            source, "goal", 1u,
            (CettaGdlTypeOfNativeLimitsV1){0});
    const char *typing_target = NULL;
    size_t typing_arity = 0u;
    size_t typing_source_forms = 0u;
    size_t typing_selected_forms = 0u;
    size_t typing_reachable = 0u;
    size_t typing_external = 0u;
    CettaNikDirectAuthorityTokenV1 goal_typing_token = {0};
    CHECK(goal_typing.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              goal_typing.native &&
              cetta_gdl_type_of_native_target_slice_v1(
                  goal_typing.native, &typing_target, &typing_arity,
                  &typing_source_forms, &typing_selected_forms,
                  &typing_reachable, &typing_external) &&
              strcmp(typing_target, "goal") == 0 &&
              typing_arity == 1u && typing_source_forms == 4u &&
              typing_selected_forms == 2u && typing_reachable == 2u &&
              typing_external == 0u &&
              cetta_gdl_type_of_native_token_v1(
                  goal_typing.native, &goal_typing_token),
          "target typing derives a dependency-closed fibre over original source occurrences");

    CettaGdlTypeOfNativeAdmissionV1 seed_typing =
        cetta_gdl_type_of_native_admit_authored_target_v1(
            source, "seed", 1u,
            (CettaGdlTypeOfNativeLimitsV1){0});
    CettaNikDirectAuthorityTokenV1 seed_typing_token = {0};
    CHECK(seed_typing.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              seed_typing.native &&
              cetta_gdl_type_of_native_token_v1(
                  seed_typing.native, &seed_typing_token) &&
              !cetta_nik_direct_authority_token_v1_equal(
                  &goal_typing_token, &seed_typing_token) &&
              !cetta_gdl_type_of_native_token_is_current_v1(
                  goal_typing.native, &seed_typing_token) &&
              !cetta_gdl_type_of_native_token_is_current_v1(
                  seed_typing.native, &goal_typing_token),
          "target and exact retained occurrence ordinals refine admission identity");

    CettaGdlTypeOfNativeAdmissionV1 reachable_typing =
        cetta_gdl_type_of_native_admit_authored_target_v1(
            source, "reachable", 1u,
            (CettaGdlTypeOfNativeLimitsV1){0});
    CHECK(reachable_typing.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_OUTSIDE_FRAGMENT_V1 &&
              !reachable_typing.native,
          "dependency closure cannot hide a reachable authored type obstruction");

    CettaGdlStratifiedModelResultV1 goal_result =
        cetta_gdl_stratified_model_admit_authored_target_v1(
            source, "goal", 1u,
            (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CettaGdlStratifiedModelV1 *goal_model = goal_result.model;
    const char *model_target = NULL;
    size_t model_arity = 0u;
    size_t model_source_forms = 0u;
    size_t model_selected_forms = 0u;
    size_t model_reachable = 0u;
    size_t model_external = 0u;
    CettaGdlStratifiedModelStatsV1 target_stats = {0};
    CHECK(goal_result.kind == CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1 &&
              goal_model &&
              cetta_gdl_stratified_model_target_slice_v1(
                  goal_model, &model_target, &model_arity,
                  &model_source_forms, &model_selected_forms,
                  &model_reachable, &model_external) &&
              strcmp(model_target, "goal") == 0 && model_arity == 1u &&
              model_source_forms == 4u && model_selected_forms == 2u &&
              model_reachable == 2u && model_external == 0u &&
              cetta_gdl_stratified_model_stats_v1(
                  goal_model, &target_stats) &&
              target_stats.source_forms == 2u &&
              target_stats.source_facts == 1u &&
              target_stats.source_rules == 1u,
          "the target model evaluates exactly the selected native calculus");

    Arena query_arena;
    arena_init(&query_arena);
    Atom *goal_a = parse_one(&query_arena, "(goal a)");
    size_t support_index = SIZE_MAX;
    CettaGdlStratifiedSupportViewV1 support = {0};
    bool original_rule_ordinal = false;
    if (goal_model && goal_a &&
        cetta_gdl_stratified_model_find_support_v1(
            goal_model, goal_a, &support_index) &&
        cetta_gdl_stratified_model_support_view_v1(
            goal_model, support_index, &support)) {
        for (size_t index = 0u; index < support.proof_edge_count; index++) {
            CettaGdlStratifiedProofEdgeViewV1 edge = {0};
            if (cetta_gdl_stratified_model_proof_edge_view_v1(
                    goal_model, support.proof_edge_indices[index], &edge) &&
                edge.source_form_ordinal == 3u)
                original_rule_ordinal = true;
        }
    }
    CHECK(original_rule_ordinal,
          "target evaluation retains the original authored rule ordinal rather than renumbering the slice");
    arena_free(&query_arena);

    CettaGdlStratifiedModelResultV1 reachable_result =
        cetta_gdl_stratified_model_admit_authored_target_v1(
            source, "reachable", 1u,
            (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CHECK(reachable_result.kind ==
                  CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1 &&
              !reachable_result.model,
          "target model admission preserves a reachable typing abstention");

    cetta_gdl_stratified_model_destroy_v1(reachable_result.model);
    cetta_gdl_stratified_model_destroy_v1(goal_model);
    cetta_gdl_type_of_native_destroy_v1(reachable_typing.native);
    cetta_gdl_type_of_native_destroy_v1(seed_typing.native);
    cetta_gdl_type_of_native_destroy_v1(goal_typing.native);
    cetta_gdl_stratified_model_destroy_v1(whole_model.model);
    cetta_gdl_type_of_native_destroy_v1(whole_typing.native);
}

int main(int argc, char **argv) {
    if (argc != 12) {
        fprintf(
            stderr,
            "usage: %s POSITIVE-SOURCE NEGATED-SOURCE FINITE-SOURCE "
            "STRATIFIED-SOURCE NEGATIVE-CYCLE-SOURCE "
            "OVERLOADED-SUBSTITUTION-SOURCE SUPPORT-GROWTH-SOURCE "
            "FALSE-SOURCE-DISTINCT GREATEST-VARIABLE-SOURCE "
            "INCOMPARABLE-VARIABLE-SOURCE TARGET-FIBRE-SOURCE\n",
            argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena source_arena;
    Arena negative_source_arena;
    Arena finite_source_arena;
    Arena stratified_source_arena;
    Arena negative_cycle_source_arena;
    Arena overloaded_source_arena;
    Arena support_growth_source_arena;
    Arena false_source_distinct_arena;
    Arena greatest_variable_source_arena;
    Arena incomparable_variable_source_arena;
    Arena target_fibre_source_arena;
    Atom **source_forms = NULL;
    Atom **negative_source_forms = NULL;
    Atom **finite_source_forms = NULL;
    Atom **stratified_source_forms = NULL;
    Atom **negative_cycle_source_forms = NULL;
    Atom **overloaded_source_forms = NULL;
    Atom **support_growth_source_forms = NULL;
    Atom **false_source_distinct_forms = NULL;
    Atom **greatest_variable_source_forms = NULL;
    Atom **incomparable_variable_source_forms = NULL;
    Atom **target_fibre_source_forms = NULL;
    CettaGdlPositiveHornNativeV1 *native = NULL;
    CettaGdlTypeOfNativeV1 *typing_native = NULL;
    CettaGdlPositiveHornEpisodeV1 *episode = NULL;
    CettaGdlPositiveHornHostV1 *host = NULL;
    CettaGdlPositiveHornHostedEpisodeV1 *hosted_episode = NULL;
    CettaGdlPositiveHornNativeV1 *finite_native = NULL;
    CettaGdlPositiveHornHostV1 *finite_host = NULL;
    CettaGdlPositiveHornHostedEpisodeV1 *finite_episode = NULL;
    Space source_space;
    Space episode_space;
    Space finite_source_space;
    Space finite_episode_space;
    bool source_space_initialized = false;
    bool episode_space_initialized = false;
    bool finite_source_space_initialized = false;
    bool finite_episode_space_initialized = false;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&source_arena);
    arena_init(&negative_source_arena);
    arena_init(&finite_source_arena);
    arena_init(&stratified_source_arena);
    arena_init(&negative_cycle_source_arena);
    arena_init(&overloaded_source_arena);
    arena_init(&support_growth_source_arena);
    arena_init(&false_source_distinct_arena);
    arena_init(&greatest_variable_source_arena);
    arena_init(&incomparable_variable_source_arena);
    arena_init(&target_fibre_source_arena);
    check_gdl_finite_herbrand();
    check_gdl_stratification();
    space_init(&source_space);
    source_space_initialized = true;
    space_init(&episode_space);
    episode_space_initialized = true;
    space_init(&finite_source_space);
    finite_source_space_initialized = true;
    space_init(&finite_episode_space);
    finite_episode_space_initialized = true;

    int source_count = parse_metta_file(
        argv[1], &source_arena, &source_forms);
    int negative_source_count = parse_metta_file(
        argv[2], &negative_source_arena, &negative_source_forms);
    int finite_source_count = parse_metta_file(
        argv[3], &finite_source_arena, &finite_source_forms);
    int stratified_source_count = parse_metta_file(
        argv[4], &stratified_source_arena, &stratified_source_forms);
    int negative_cycle_source_count = parse_metta_file(
        argv[5], &negative_cycle_source_arena,
        &negative_cycle_source_forms);
    int overloaded_source_count = parse_metta_file(
        argv[6], &overloaded_source_arena, &overloaded_source_forms);
    int support_growth_source_count = parse_metta_file(
        argv[7], &support_growth_source_arena,
        &support_growth_source_forms);
    int false_source_distinct_count = parse_metta_file(
        argv[8], &false_source_distinct_arena,
        &false_source_distinct_forms);
    int greatest_variable_source_count = parse_metta_file(
        argv[9], &greatest_variable_source_arena,
        &greatest_variable_source_forms);
    int incomparable_variable_source_count = parse_metta_file(
        argv[10], &incomparable_variable_source_arena,
        &incomparable_variable_source_forms);
    int target_fibre_source_count = parse_metta_file(
        argv[11], &target_fibre_source_arena,
        &target_fibre_source_forms);
    Atom *source = source_count == 1 && source_forms
        ? source_forms[0] : NULL;
    Atom *negative_source =
        negative_source_count == 1 && negative_source_forms
            ? negative_source_forms[0] : NULL;
    Atom *finite_source =
        finite_source_count == 1 && finite_source_forms
            ? finite_source_forms[0] : NULL;
    Atom *stratified_source =
        stratified_source_count == 1 && stratified_source_forms
            ? stratified_source_forms[0] : NULL;
    Atom *negative_cycle_source =
        negative_cycle_source_count == 1 && negative_cycle_source_forms
            ? negative_cycle_source_forms[0] : NULL;
    Atom *overloaded_source =
        overloaded_source_count == 1 && overloaded_source_forms
            ? overloaded_source_forms[0] : NULL;
    Atom *support_growth_source =
        support_growth_source_count == 1 && support_growth_source_forms
            ? support_growth_source_forms[0] : NULL;
    Atom *false_source_distinct =
        false_source_distinct_count == 1 && false_source_distinct_forms
            ? false_source_distinct_forms[0] : NULL;
    Atom *greatest_variable_source =
        greatest_variable_source_count == 1 &&
                greatest_variable_source_forms
            ? greatest_variable_source_forms[0] : NULL;
    Atom *incomparable_variable_source =
        incomparable_variable_source_count == 1 &&
                incomparable_variable_source_forms
            ? incomparable_variable_source_forms[0] : NULL;
    Atom *target_fibre_source =
        target_fibre_source_count == 1 && target_fibre_source_forms
            ? target_fibre_source_forms[0] : NULL;
    CHECK(expr_named(source, "gdl-type-source-v1", 6u),
          "one authored positive-Horn GDL package parses");
    CHECK(expr_named(negative_source, "gdl-type-source-v1", 6u),
          "one authored GDL negative-control package parses");
    CHECK(expr_named(finite_source, "gdl-type-source-v1", 6u),
          "one authored finite-view GDL package parses");
    CHECK(expr_named(stratified_source, "gdl-type-source-v1", 6u),
          "one authored stratified-model GDL package parses");
    CHECK(expr_named(negative_cycle_source, "gdl-type-source-v1", 6u),
          "one authored negative-cycle GDL package parses");
    CHECK(expr_named(overloaded_source, "gdl-type-source-v1", 6u),
          "one authored overloaded-substitution GDL package parses");
    CHECK(expr_named(support_growth_source, "gdl-type-source-v1", 6u),
          "one authored support-growth GDL package parses");
    CHECK(expr_named(false_source_distinct, "gdl-type-source-v1", 6u),
          "one authored false-distinct GDL package parses");
    CHECK(expr_named(greatest_variable_source, "gdl-type-source-v1", 6u),
          "one authored greatest-variable GDL package parses");
    CHECK(expr_named(
              incomparable_variable_source, "gdl-type-source-v1", 6u),
          "one authored incomparable-variable GDL package parses");
    CHECK(expr_named(
              target_fibre_source, "gdl-type-source-v1", 6u),
          "one authored target-fibre GDL package parses");
    if (!source || !negative_source || !finite_source ||
        !stratified_source || !negative_cycle_source ||
        !overloaded_source || !support_growth_source ||
        !false_source_distinct || !greatest_variable_source ||
        !incomparable_variable_source || !target_fibre_source)
        goto done;
    check_gdl_stratified_model(stratified_source);
    check_gdl_stratified_support_growth(support_growth_source);
    check_gdl_stratified_negative_cycle(negative_cycle_source);
    check_gdl_stratified_ambiguous_overload(overloaded_source);
    check_gdl_stratified_false_source_distinct(false_source_distinct);
    check_gdl_rule_variable_greatest(greatest_variable_source);
    check_gdl_rule_variable_incomparable(incomparable_variable_source);
    check_gdl_target_fibre(target_fibre_source);

    CettaGdlPositiveHornAdmissionV1 admitted =
        cetta_gdl_positive_horn_native_admit_v1(
            source, (CettaGdlPositiveHornLimitsV1){0});
    if (admitted.kind != CETTA_GDL_POSITIVE_HORN_ADMITTED_V1)
        fprintf(stderr, "positive-Horn admission kind: %d\n",
                (int)admitted.kind);
    native = admitted.native;
    CHECK(admitted.kind == CETTA_GDL_POSITIVE_HORN_ADMITTED_V1 && native,
          "complete native typing admits the exact positive-Horn source");
    if (!native)
        goto done;

    CettaGdlPositiveHornStatsV1 stats = {0};
    CHECK(cetta_gdl_positive_horn_native_stats_v1(native, &stats) &&
              cetta_gdl_positive_horn_native_stratification_v1(native) &&
              stats.source_forms == 23u &&
              stats.source_rules == 12u &&
              stats.source_facts == 11u &&
              stats.distinct_premises == 3u &&
              stats.distinct_evidence_blocks == 2u &&
              stats.dependency_relations == 13u &&
              stats.dependency_edges == 22u &&
              stats.dependency_negative_edges == 0u &&
              stats.dependency_strata == 1u &&
              stats.compiled_blocks == 25u,
          "native construction retains source, dependency, and evidence counts");

    CettaNikDirectAuthorityTokenV1 token = {0};
    CHECK(cetta_nik_direct_authority_v1_is_valid(
              cetta_gdl_positive_horn_native_authority_v1()) &&
              cetta_gdl_positive_horn_native_token_v1(native, &token) &&
              cetta_gdl_positive_horn_native_token_is_current_v1(
                  native, &token),
          "the constructed realization carries one current NIK identity");
    const char *native_source_digest = NULL;
    const char *native_profile_digest = NULL;
    const char *native_revision = NULL;
    CHECK(cetta_gdl_positive_horn_native_identity_v1(
              native, &native_source_digest,
              &native_profile_digest, &native_revision) &&
              native_source_digest && native_profile_digest &&
              native_revision,
          "the native calculus retains its exact authored content identity");

    CettaGdlTypeOfNativeAdmissionV1 typing_admission =
        cetta_gdl_type_of_native_admit_authored_source_v1(
            source, (CettaGdlTypeOfNativeLimitsV1){0});
    typing_native = typing_admission.native;
    CettaNikDirectAuthorityTokenV1 typing_token = {0};
    CHECK(typing_admission.kind == CETTA_GDL_TYPE_OF_NATIVE_ADMITTED_V1 &&
              typing_native &&
              cetta_gdl_type_of_native_token_v1(
                  typing_native, &typing_token),
          "the same authored presentation admits its native typing calculus");
    if (!typing_native)
        goto done;

    Arena query_arena;
    Arena result_arena;
    arena_init(&query_arena);
    arena_init(&result_arena);
    Atom *episode_occurrence = parse_one(
        &query_arena, "(gdl:episode-occurrence sps state-1 1)");
    Atom *ground_literal = parse_one(
        &query_arena, "(true (step 0))");
    CettaGdlTypeOfNativeGroundV1 ground =
        cetta_gdl_type_of_native_construct_ground_literal_v1(
            typing_native, &typing_token, &result_arena,
            episode_occurrence, ground_literal, 0u);
    CHECK(ground.kind == CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1 &&
              ground.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED &&
              ground.selection.kind ==
                  CETTA_NIK_IMPLEMENTATION_SELECTION_UNIQUE_GREATEST_V1 &&
              ground.proof_count == 1u && ground.type &&
              expr_named(
                  ground.proofs[0], "gdl:native-ground-literal-v1", 6u),
          "native typing constructs the exact ground-literal proof fibre");

    Arena second_ground_arena;
    arena_init(&second_ground_arena);
    Atom *second_occurrence = parse_one(
        &query_arena, "(gdl:episode-occurrence sps state-2 1)");
    CettaGdlTypeOfNativeGroundV1 second_ground =
        cetta_gdl_type_of_native_construct_ground_literal_v1(
            typing_native, &typing_token, &second_ground_arena,
            second_occurrence, ground_literal, 0u);
    CHECK(second_ground.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1 &&
              second_ground.value.outcome ==
                  CETTA_NIK_OUTCOME_ESTABLISHED &&
              second_ground.proof_count == 1u &&
              !atom_eq(ground.proofs[0], second_ground.proofs[0]),
          "equal literals at distinct episode occurrences retain distinct proofs");
    arena_free(&second_ground_arena);

    Arena outside_ground_arena;
    arena_init(&outside_ground_arena);
    Atom *ill_typed_literal = parse_one(&query_arena, "(true paper)");
    Atom *open_literal = parse_one(&query_arena, "(true $value)");
    CettaGdlTypeOfNativeGroundV1 ill_typed_ground =
        cetta_gdl_type_of_native_construct_ground_literal_v1(
            typing_native, &typing_token, &outside_ground_arena,
            episode_occurrence, ill_typed_literal, 0u);
    CettaGdlTypeOfNativeGroundV1 open_ground =
        cetta_gdl_type_of_native_construct_ground_literal_v1(
            typing_native, &typing_token, &outside_ground_arena,
            episode_occurrence, open_literal, 0u);
    CHECK(ill_typed_ground.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1 &&
              ill_typed_ground.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT &&
              ill_typed_ground.proof_count == 0u &&
              open_ground.kind ==
                  CETTA_GDL_TYPE_OF_NATIVE_GROUND_OUTCOME_V1 &&
              open_ground.value.outcome ==
                  CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT,
          "ill-typed and open literals abstain rather than fault or refute");

    CettaNikDirectAuthorityTokenV1 stale_typing_token = typing_token;
    stale_typing_token.words[stale_typing_token.length - 1u] ^=
        UINT64_C(1);
    CettaGdlTypeOfNativeGroundV1 stale_ground =
        cetta_gdl_type_of_native_construct_ground_literal_v1(
            typing_native, &stale_typing_token, &outside_ground_arena,
            episode_occurrence, ground_literal, 0u);
    CHECK(stale_ground.kind == CETTA_GDL_TYPE_OF_NATIVE_GROUND_STALE_V1 &&
              stale_ground.proof_count == 0u,
          "a stale typing realization cannot construct episode evidence");
    arena_free(&outside_ground_arena);

    Atom *episode_identity = parse_one(
        &query_arena,
        "(gdl:episode-equivalence sps state-1 source-occurrences)");
    Atom *episode_facts[] = {
        parse_one(&query_arena, "(true (step 0))"),
        parse_one(&query_arena, "(true (score p1 0))"),
        parse_one(&query_arena, "(true (score p1 0))"),
        parse_one(&query_arena, "(true (score p2 0))"),
        parse_one(&query_arena, "(does p1 scissors)"),
        parse_one(&query_arena, "(does p2 paper)"),
    };
    CettaGdlPositiveHornEpisodeAdmissionV1 episode_admission =
        cetta_gdl_positive_horn_native_admit_episode_v1(
            native, &token, episode_identity,
            episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlPositiveHornEpisodeLimitsV1){0});
    episode = episode_admission.episode;
    if (episode_admission.kind !=
        CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1)
        fprintf(stderr, "typed episode admission kind: %d\n",
                (int)episode_admission.kind);
    CettaGdlPositiveHornEpisodeStatsV1 episode_stats = {0};
    CettaNikDirectAuthorityTokenV1 episode_token = {0};
    const char *episode_digest = NULL;
    const char *episode_revision = NULL;
    CHECK(episode_admission.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
              episode &&
              cetta_gdl_positive_horn_episode_stats_v1(
                  episode, &episode_stats) &&
              episode_stats.authored_facts == 6u &&
              episode_stats.typing_proof_occurrences == 6u &&
              episode_stats.compiled_delta_blocks == 6u &&
              cetta_gdl_positive_horn_episode_token_v1(
                  episode, &episode_token) &&
              cetta_gdl_positive_horn_episode_token_is_current_v1(
                  episode, &episode_token) &&
              !cetta_nik_direct_authority_token_v1_equal(
                  &token, &episode_token) &&
              cetta_gdl_positive_horn_episode_identity_v1(
                  episode, &episode_digest, &episode_revision) &&
              episode_digest && strlen(episode_digest) == 64u &&
              episode_revision && *episode_revision,
          "typed episode admission retains every fact proof and a distinct current revision");

    Arena episode_result_arena;
    arena_init(&episode_result_arena);
    Atom *goal_query = parse_one(&query_arena, "(goal p1 $score)");
    CettaGdlPositiveHornRunV1 episode_goal = episode
        ? cetta_gdl_positive_horn_episode_run_v1(
            episode, &episode_token, &episode_result_arena,
            goal_query, 8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    Atom *episode_goal_proof = first_occurrence_proof(episode_goal.result);
    CHECK(episode_goal.kind ==
                  CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              episode_goal.implementation_identity ==
                  cetta_gdl_positive_horn_native_authority_v1()
                      ->realization_identity &&
              result_occurrence_count(episode_goal.result) == 2u &&
              atom_contains_head(
                  episode_goal_proof,
                  "gdl:typed-episode-occurrence", 0u) &&
              atom_contains_head(
                  episode_goal_proof,
                  "gdl:native-ground-literal-v1", 0u),
          "relational chaining retains duplicate typed episode occurrences and native proofs");

    Atom *structural_next_query = parse_one(
        &query_arena, "(next (step $next-step))");
    Atom *flattened_next_query = parse_one(
        &query_arena, "(next_step $next-step)");
    CettaGdlPositiveHornRunV1 structural_next = episode
        ? cetta_gdl_positive_horn_episode_run_v1(
            episode, &episode_token, &episode_result_arena,
            structural_next_query, 8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    CettaGdlPositiveHornRunV1 flattened_next = episode
        ? cetta_gdl_positive_horn_episode_run_v1(
            episode, &episode_token, &episode_result_arena,
            flattened_next_query, 8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    CHECK(structural_next.kind ==
                  CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              result_occurrence_count(structural_next.result) == 1u &&
              flattened_next.kind ==
                  CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              result_occurrence_count(flattened_next.result) == 0u,
          "the native kernel preserves nested constructors instead of flattening predicate identity");

    arena_free(&episode_result_arena);
    arena_init(&episode_result_arena);
    CettaGdlPositiveHornRunV1 base_goal =
        cetta_gdl_positive_horn_native_run_v1(
            native, &token, &episode_result_arena,
            goal_query, 8u, 100000u, 100u);
    CHECK(base_goal.kind == CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              base_goal.implementation_identity ==
                  cetta_gdl_positive_horn_native_authority_v1()
                      ->realization_identity &&
              result_occurrence_count(base_goal.result) == 0u,
          "publishing an episode leaves the persistent source artifact unchanged");

    CettaNikDirectAuthorityTokenV1 stale_episode_token = episode_token;
    if (stale_episode_token.length != 0u)
        stale_episode_token.words[stale_episode_token.length - 1u] ^=
            UINT64_C(1);
    CettaGdlPositiveHornRunV1 stale_episode_run = episode
        ? cetta_gdl_positive_horn_episode_run_v1(
            episode, &stale_episode_token, &episode_result_arena,
            goal_query, 8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    CHECK(stale_episode_run.kind == CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1 &&
              stale_episode_run.implementation_identity == 0u &&
              stale_episode_run.result == NULL,
          "a stale episode revision deoptimizes without semantic rejection");

    CettaGdlPositiveHornEpisodeAdmissionV1 bounded_episode =
        cetta_gdl_positive_horn_native_admit_episode_v1(
            native, &token, episode_identity,
            episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlPositiveHornEpisodeLimitsV1){.max_facts = 5u});
    CHECK(bounded_episode.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_INCOMPLETE_V1 &&
              bounded_episode.episode == NULL,
          "an episode construction bound is Incomplete rather than Refuted");
    cetta_gdl_positive_horn_episode_destroy_v1(bounded_episode.episode);

    Atom *ill_typed_facts[] = {
        parse_one(&query_arena, "(true paper)"),
    };
    CettaGdlPositiveHornEpisodeAdmissionV1 outside_episode =
        cetta_gdl_positive_horn_native_admit_episode_v1(
            native, &token, episode_identity,
            ill_typed_facts, 1u,
            (CettaGdlPositiveHornEpisodeLimitsV1){0});
    CHECK(outside_episode.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1 &&
              outside_episode.episode == NULL,
          "a fact without a native Bool proof cannot enter the episode delta");
    cetta_gdl_positive_horn_episode_destroy_v1(outside_episode.episode);

    CettaNikDirectAuthorityTokenV1 stale_source_token = token;
    stale_source_token.words[stale_source_token.length - 1u] ^=
        UINT64_C(1);
    CettaGdlPositiveHornEpisodeAdmissionV1 stale_episode_admission =
        cetta_gdl_positive_horn_native_admit_episode_v1(
            native, &stale_source_token, episode_identity,
            episode_facts,
            sizeof(episode_facts) / sizeof(episode_facts[0]),
            (CettaGdlPositiveHornEpisodeLimitsV1){0});
    CHECK(stale_episode_admission.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_STALE_V1 &&
              stale_episode_admission.episode == NULL,
          "a stale source realization cannot mint a typed episode");
    cetta_gdl_positive_horn_episode_destroy_v1(
        stale_episode_admission.episode);
    arena_free(&episode_result_arena);

    CettaIndex source_package_occurrence = space_length64(&source_space);
    CHECK(space_admit_atom_from_source_arena(
              &source_space, NULL, &source_arena, source) &&
              space_length64(&source_space) ==
                  source_package_occurrence + 1u,
          "the authored calculus enters Space as ordinary data");
    CettaGdlPositiveHornHostAdmissionV1 host_admission =
        cetta_gdl_positive_horn_host_admit_v1(
            &source_space, source_package_occurrence,
            native_revision, (CettaGdlPositiveHornLimitsV1){0});
    host = host_admission.host;
    CettaGdlPositiveHornHostReceiptV1 host_receipt = {0};
    CHECK(host_admission.kind ==
                  CETTA_GDL_POSITIVE_HORN_HOST_ADMITTED_V1 &&
              host && cetta_gdl_positive_horn_host_is_current_v1(
                  host, &source_space) &&
              cetta_gdl_positive_horn_host_receipt_v1(
                  host, &source_space, &host_receipt) &&
              host_receipt.package_occurrence ==
                  source_package_occurrence,
          "the Space-hosted calculus retains exact source currentness without parser authority");

    CettaIndex hosted_fact_occurrences[
        sizeof(episode_facts) / sizeof(episode_facts[0])];
    bool hosted_facts_added = true;
    for (size_t index = 0u;
         index < sizeof(episode_facts) / sizeof(episode_facts[0]);
         index++) {
        hosted_fact_occurrences[index] = space_length64(&episode_space);
        if (!space_admit_atom_from_source_arena(
                &episode_space, NULL, &query_arena,
                episode_facts[index]))
            hosted_facts_added = false;
    }
    CHECK(hosted_facts_added &&
              space_length64(&episode_space) ==
                  sizeof(episode_facts) / sizeof(episode_facts[0]),
          "the episode fact bag enters a separate Space in authored order");
    CettaGdlPositiveHornHostedEpisodeAdmissionV1
        hosted_episode_admission =
            cetta_gdl_positive_horn_host_admit_episode_v1(
                host, &source_space, &episode_space,
                episode_identity, hosted_fact_occurrences,
                sizeof(hosted_fact_occurrences) /
                    sizeof(hosted_fact_occurrences[0]),
                (CettaGdlPositiveHornEpisodeLimitsV1){0});
    hosted_episode = hosted_episode_admission.episode;
    CettaGdlPositiveHornHostedEpisodeReceiptV1
        hosted_episode_receipt = {0};
    CHECK(hosted_episode_admission.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
              hosted_episode &&
              cetta_gdl_positive_horn_hosted_episode_is_current_v1(
                  hosted_episode, &source_space, &episode_space) &&
              cetta_gdl_positive_horn_hosted_episode_receipt_v1(
                  hosted_episode, &source_space, &episode_space,
                  &hosted_episode_receipt) &&
              hosted_episode_receipt.fact_count == 6u &&
              hosted_episode_receipt.episode_read.instance_id ==
                  space_instance_id(&episode_space),
          "the hosted typed episode retains exact source and fact-Space receipts");

    arena_init(&episode_result_arena);
    CettaGdlPositiveHornRunV1 hosted_goal = hosted_episode
        ? cetta_gdl_positive_horn_hosted_episode_run_v1(
            hosted_episode, &source_space, &episode_space,
            &episode_result_arena, goal_query,
            8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    CHECK(hosted_goal.kind ==
                  CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              hosted_goal.implementation_identity ==
                  cetta_gdl_positive_horn_native_authority_v1()
                      ->realization_identity &&
              result_occurrence_count(hosted_goal.result) == 2u &&
              atom_contains_head(
                  first_occurrence_proof(hosted_goal.result),
                  "gdl:space-fact-occurrence-v1", 0u),
          "Space-hosted execution retains duplicate fact provenance in its proof bag");
    arena_free(&episode_result_arena);

    Atom *episode_mutation = atom_symbol(
        &query_arena, "episode-revision-mutation");
    CHECK(space_admit_atom_from_source_arena(
              &episode_space, NULL, &query_arena, episode_mutation) &&
              !cetta_gdl_positive_horn_hosted_episode_is_current_v1(
                  hosted_episode, &source_space, &episode_space),
          "a changed episode Space invalidates the hosted realization");
    arena_init(&episode_result_arena);
    CettaGdlPositiveHornRunV1 stale_hosted_goal = hosted_episode
        ? cetta_gdl_positive_horn_hosted_episode_run_v1(
            hosted_episode, &source_space, &episode_space,
            &episode_result_arena, goal_query,
            8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    CHECK(stale_hosted_goal.kind ==
                  CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1 &&
              stale_hosted_goal.implementation_identity == 0u &&
              stale_hosted_goal.result == NULL,
          "hosted episode staleness deoptimizes rather than refuting");
    arena_free(&episode_result_arena);

    Atom *source_mutation = atom_symbol(
        &source_arena, "source-revision-mutation");
    CHECK(space_admit_atom_from_source_arena(
              &source_space, NULL, &source_arena, source_mutation) &&
              !cetta_gdl_positive_horn_host_is_current_v1(
                  host, &source_space),
          "a changed source Space invalidates its hosted native calculus");

    Atom *player_query = parse_one(&query_arena, "(player $who)");
    CettaGdlPositiveHornRunV1 player_run =
        cetta_gdl_positive_horn_native_run_v1(
            native, &token, &result_arena, player_query,
            8u, 100000u, 100u);
    Atom *first_proof = first_occurrence_proof(player_run.result);
    if (player_run.kind != CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 ||
        result_occurrence_count(player_run.result) != 2u) {
        fputs("player query result: ", stderr);
        atom_print(player_run.result, stderr);
        fputc('\n', stderr);
    }
    CHECK(player_run.kind == CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              player_run.implementation_identity ==
                  cetta_gdl_positive_horn_native_authority_v1()
                      ->realization_identity &&
              result_occurrence_count(player_run.result) == 2u &&
              expr_named(first_proof, "quote", 2u) &&
              expr_named(first_proof->expr.elems[1], "gdl:fact", 4u),
          "native proof construction preserves both authored player fact occurrences");

    arena_free(&result_arena);
    arena_init(&result_arena);
    CettaGdlPositiveHornRunV1 bounded =
        cetta_gdl_positive_horn_native_run_v1(
            native, &token, &result_arena, player_query,
            8u, 100000u, 1u);
    if (bounded.kind != CETTA_GDL_POSITIVE_HORN_RUN_INCOMPLETE_V1) {
        fputs("bounded player query result: ", stderr);
        atom_print(bounded.result, stderr);
        fputc('\n', stderr);
    }
    CHECK(bounded.kind == CETTA_GDL_POSITIVE_HORN_RUN_INCOMPLETE_V1 &&
              expr_named(bounded.result, "compile-incomplete", 6u) &&
              atom_is_symbol(bounded.result->expr.elems[1],
                             "occurrence-limit") &&
              result_occurrence_count(bounded.result) == 1u,
          "a bounded answer bag is Incomplete and retains its partial proof occurrence");

    arena_free(&result_arena);
    arena_init(&result_arena);
    Atom *unknown_query = parse_one(&query_arena, "(unknown $value)");
    CettaGdlPositiveHornRunV1 unknown =
        cetta_gdl_positive_horn_native_run_v1(
            native, &token, &result_arena, unknown_query,
            8u, 100000u, 100u);
    CHECK(unknown.kind == CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              result_occurrence_count(unknown.result) == 0u,
          "complete empty search stays an empty proof bag rather than Refuted");

    CettaNikDirectAuthorityTokenV1 stale = token;
    stale.words[stale.length - 1u] ^= UINT64_C(1);
    CettaGdlPositiveHornRunV1 stale_run =
        cetta_gdl_positive_horn_native_run_v1(
            native, &stale, &result_arena, player_query,
            8u, 100000u, 100u);
    CHECK(stale_run.kind == CETTA_GDL_POSITIVE_HORN_RUN_STALE_V1 &&
              stale_run.implementation_identity == 0u &&
              stale_run.result == NULL,
          "a changed realization identity deauthorizes execution without refutation");

    CettaGdlPositiveHornRunV1 oversized_budget =
        cetta_gdl_positive_horn_native_run_v1(
            native, &token, &result_arena, player_query,
            8u, UINT64_MAX, 100u);
    CHECK(oversized_budget.kind ==
              CETTA_GDL_POSITIVE_HORN_RUN_ENGINE_FAULT_V1 &&
              oversized_budget.result == NULL,
          "an unrepresentable engine bound is not a semantic outcome");

    CettaGdlPositiveHornAdmissionV1 bounded_admission =
        cetta_gdl_positive_horn_native_admit_v1(
            source,
            (CettaGdlPositiveHornLimitsV1){.max_source_blocks = 24u});
    CHECK(bounded_admission.kind ==
              CETTA_GDL_POSITIVE_HORN_INCOMPLETE_V1 &&
              bounded_admission.native == NULL,
          "a finite construction bound reports Incomplete rather than rejecting the calculus");
    cetta_gdl_positive_horn_native_destroy_v1(bounded_admission.native);

    CettaGdlPositiveHornAdmissionV1 negated_admission =
        cetta_gdl_positive_horn_native_admit_v1(
            negative_source, (CettaGdlPositiveHornLimitsV1){0});
    CHECK(negated_admission.kind ==
              CETTA_GDL_POSITIVE_HORN_OUTSIDE_FRAGMENT_V1 &&
              negated_admission.native == NULL,
          "negation remains outside this exact native positive-Horn image");
    cetta_gdl_positive_horn_native_destroy_v1(negated_admission.native);

    CettaGdlPositiveHornAdmissionV1 finite_admission =
        cetta_gdl_finite_view_native_admit_v1(
            finite_source, (CettaGdlPositiveHornLimitsV1){0});
    finite_native = finite_admission.native;
    CettaGdlPositiveHornStatsV1 finite_stats = {0};
    CettaNikDirectAuthorityTokenV1 finite_token = {0};
    const char *finite_source_digest = NULL;
    const char *finite_profile_digest = NULL;
    const char *finite_revision = NULL;
    CHECK(finite_admission.kind == CETTA_GDL_POSITIVE_HORN_ADMITTED_V1 &&
              finite_native &&
              cetta_gdl_positive_horn_native_stats_v1(
                  finite_native, &finite_stats) &&
              finite_stats.source_forms == 5u &&
              finite_stats.source_rules == 1u &&
              finite_stats.source_facts == 4u &&
              finite_stats.finite_state_domain_members == 2u &&
              finite_stats.finite_state_negative_premises == 1u &&
              cetta_gdl_positive_horn_native_stratification_v1(
                  finite_native) &&
              finite_stats.dependency_relations == 4u &&
              finite_stats.dependency_edges == 2u &&
              finite_stats.dependency_negative_edges == 1u &&
              finite_stats.dependency_strata == 2u &&
              finite_stats.compiled_blocks == 5u &&
              cetta_gdl_positive_horn_native_token_v1(
                  finite_native, &finite_token) &&
              cetta_gdl_positive_horn_native_identity_v1(
                  finite_native, &finite_source_digest,
                  &finite_profile_digest, &finite_revision) &&
              finite_source_digest && finite_profile_digest &&
              finite_revision &&
              cetta_nik_direct_authority_v1_is_valid(
                  cetta_gdl_finite_view_native_authority_v1()),
          "finite-view admission retains its domain, negative demand, and distinct native authority");

    Atom *finite_identity = parse_one(
        &query_arena, "(gdl:finite-episode synthetic state-1)");
    Atom *finite_fact_q = parse_one(&query_arena, "(true q)");
    Atom *selective_facts[] = {finite_fact_q};
    CettaGdlPositiveHornEpisodeAdmissionV1 selective_finite =
        cetta_gdl_positive_horn_native_admit_episode_v1(
            finite_native, &finite_token, finite_identity,
            selective_facts, 1u,
            (CettaGdlPositiveHornEpisodeLimitsV1){0});
    CHECK(selective_finite.kind ==
              CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1 &&
              selective_finite.episode == NULL,
          "a selective fact list cannot manufacture finite-state absence");
    cetta_gdl_positive_horn_episode_destroy_v1(
        selective_finite.episode);

    CettaIndex finite_source_occurrence =
        space_length64(&finite_source_space);
    CHECK(space_admit_atom_from_source_arena(
              &finite_source_space, NULL,
              &finite_source_arena, finite_source),
          "the finite-view source enters Space as ordinary data");
    CettaGdlPositiveHornHostAdmissionV1 finite_host_admission =
        cetta_gdl_finite_view_host_admit_v1(
            &finite_source_space, finite_source_occurrence,
            finite_revision, (CettaGdlPositiveHornLimitsV1){0});
    finite_host = finite_host_admission.host;
    CHECK(finite_host_admission.kind ==
              CETTA_GDL_POSITIVE_HORN_HOST_ADMITTED_V1 &&
              finite_host &&
              cetta_gdl_positive_horn_host_is_current_v1(
                  finite_host, &finite_source_space),
          "the finite-view calculus is hosted without parser authority");

    CHECK(space_admit_atom_from_source_arena(
              &finite_episode_space, NULL,
              &query_arena, finite_fact_q) &&
              space_admit_atom_from_source_arena(
                  &finite_episode_space, NULL,
                  &query_arena, finite_fact_q) &&
              space_length64(&finite_episode_space) == 2u,
          "the complete episode Space retains duplicate positive occurrences");
    CettaGdlPositiveHornHostedEpisodeAdmissionV1
        finite_episode_admission =
            cetta_gdl_finite_view_host_admit_complete_episode_v1(
                finite_host, &finite_source_space,
                &finite_episode_space, finite_identity,
                (CettaGdlPositiveHornEpisodeLimitsV1){0});
    finite_episode = finite_episode_admission.episode;
    CettaGdlPositiveHornEpisodeStatsV1 finite_episode_stats = {0};
    CHECK(finite_episode_admission.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
              finite_episode &&
              cetta_gdl_positive_horn_hosted_episode_stats_v1(
                  finite_episode, &finite_episode_stats) &&
              finite_episode_stats.authored_facts == 2u &&
              finite_episode_stats.finite_state_absence_proof_occurrences ==
                  1u &&
              finite_episode_stats.typing_proof_occurrences == 3u &&
              finite_episode_stats.compiled_delta_blocks == 3u,
          "the complete view constructs typed absence while retaining positive multiplicity");

    arena_free(&result_arena);
    arena_init(&result_arena);
    Atom *missing_query = parse_one(
        &query_arena, "(missing $which)");
    CettaGdlPositiveHornRunV1 missing_run = finite_episode
        ? cetta_gdl_positive_horn_hosted_episode_run_v1(
            finite_episode, &finite_source_space,
            &finite_episode_space, &result_arena,
            missing_query, 8u, 100000u, 100u)
        : (CettaGdlPositiveHornRunV1){0};
    CHECK(missing_run.kind ==
                  CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 &&
              missing_run.implementation_identity ==
                  cetta_gdl_finite_view_native_authority_v1()
                      ->realization_identity &&
              result_occurrence_count(missing_run.result) == 1u &&
              atom_contains_head(
                  first_occurrence_proof(missing_run.result),
                  "gdl:finite-absence-proof", 0u) &&
              atom_contains_head(
                  first_occurrence_proof(missing_run.result),
                  "gdl:native-ground-literal-v1", 0u),
          "the strongest licensed finite kernel derives only explicitly absent p with its typing proof");

    Atom *finite_fact_r = parse_one(&query_arena, "(true r)");
    CHECK(space_admit_atom_from_source_arena(
              &finite_episode_space, NULL,
              &query_arena, finite_fact_r) &&
              !cetta_gdl_positive_horn_hosted_episode_is_current_v1(
                  finite_episode, &finite_source_space,
                  &finite_episode_space),
          "changing the complete state view makes its absence receipt stale");
    CettaGdlPositiveHornHostedEpisodeAdmissionV1 outside_finite =
        cetta_gdl_finite_view_host_admit_complete_episode_v1(
            finite_host, &finite_source_space,
            &finite_episode_space, finite_identity,
            (CettaGdlPositiveHornEpisodeLimitsV1){0});
    CHECK(outside_finite.kind ==
                  CETTA_GDL_POSITIVE_HORN_EPISODE_OUTSIDE_FRAGMENT_V1 &&
              outside_finite.episode == NULL,
          "a well-typed true fact outside base coverage abstains rather than extending the closed view");
    cetta_gdl_positive_horn_hosted_episode_destroy_v1(
        outside_finite.episode);

    arena_free(&result_arena);
    arena_free(&query_arena);

done:
    cetta_gdl_positive_horn_hosted_episode_destroy_v1(finite_episode);
    cetta_gdl_positive_horn_host_destroy_v1(finite_host);
    cetta_gdl_positive_horn_native_destroy_v1(finite_native);
    cetta_gdl_positive_horn_hosted_episode_destroy_v1(hosted_episode);
    cetta_gdl_positive_horn_host_destroy_v1(host);
    cetta_gdl_positive_horn_episode_destroy_v1(episode);
    cetta_gdl_type_of_native_destroy_v1(typing_native);
    cetta_gdl_positive_horn_native_destroy_v1(native);
    free(negative_source_forms);
    free(finite_source_forms);
    free(stratified_source_forms);
    free(negative_cycle_source_forms);
    free(overloaded_source_forms);
    free(support_growth_source_forms);
    free(false_source_distinct_forms);
    free(greatest_variable_source_forms);
    free(incomparable_variable_source_forms);
    free(target_fibre_source_forms);
    free(source_forms);
    if (finite_episode_space_initialized)
        space_free(&finite_episode_space);
    if (finite_source_space_initialized)
        space_free(&finite_source_space);
    if (episode_space_initialized)
        space_free(&episode_space);
    if (source_space_initialized)
        space_free(&source_space);
    arena_free(&negative_source_arena);
    arena_free(&finite_source_arena);
    arena_free(&stratified_source_arena);
    arena_free(&negative_cycle_source_arena);
    arena_free(&overloaded_source_arena);
    arena_free(&support_growth_source_arena);
    arena_free(&false_source_distinct_arena);
    arena_free(&greatest_variable_source_arena);
    arena_free(&incomparable_variable_source_arena);
    arena_free(&target_fibre_source_arena);
    arena_free(&source_arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;

    if (failures) {
        fprintf(stderr, "%u/%u native positive-Horn GDL checks failed\n",
                failures, checks);
        return 1;
    }
    printf(
        "PASS: native positive-Horn GDL construction: "
        "%zu blocks / %u checks\n",
        (size_t)25u, checks);
    return 0;
}
