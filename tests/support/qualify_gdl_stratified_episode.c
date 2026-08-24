#include "gdl_stratified_model.h"
#include "match.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static bool expr_named(Atom *atom, const char *name, size_t length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom_is_symbol(atom->expr.elems[0], name);
}

static bool grounded_size(Atom *atom, size_t *value_out) {
    if (!atom || !value_out || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0 ||
        (uint64_t)atom->ground.ival > SIZE_MAX)
        return false;
    *value_out = (size_t)atom->ground.ival;
    return true;
}

static const char *source_outcome_name(
    CettaGdlStratifiedModelKindV1 kind) {
    switch (kind) {
    case CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1:
        return "Established";
    case CETTA_GDL_STRATIFIED_MODEL_REFUTED_NEGATIVE_CYCLE_V1:
        return "RefutedNegativeCycle";
    case CETTA_GDL_STRATIFIED_MODEL_OUTSIDE_FRAGMENT_V1:
        return "OutsideFragment";
    case CETTA_GDL_STRATIFIED_MODEL_INCOMPLETE_V1:
        return "Incomplete";
    case CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1:
    default:
        return "EngineFault";
    }
}

static const char *episode_outcome_name(
    CettaGdlStratifiedEpisodeKindV1 kind) {
    switch (kind) {
    case CETTA_GDL_STRATIFIED_EPISODE_ESTABLISHED_V1:
        return "Established";
    case CETTA_GDL_STRATIFIED_EPISODE_OUTSIDE_FRAGMENT_V1:
        return "OutsideFragment";
    case CETTA_GDL_STRATIFIED_EPISODE_INCOMPLETE_V1:
        return "Incomplete";
    case CETTA_GDL_STRATIFIED_EPISODE_STALE_V1:
        return "Stale";
    case CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1:
    default:
        return "EngineFault";
    }
}

static bool support_selected(
    Atom *literal, Atom *const *queries, size_t query_count) {
    if (query_count == 0u)
        return true;
    for (size_t index = 0u; index < query_count; index++) {
        Bindings bindings;
        bindings_init(&bindings);
        bool matched = queries[index] &&
            match_atoms(queries[index], literal, &bindings);
        bindings_free(&bindings);
        if (matched)
            return true;
    }
    return false;
}

static int print_episode(
    size_t ordinal,
    CettaGdlStratifiedEpisodeKindV1 kind,
    CettaGdlStratifiedEpisodeV1 *episode,
    Atom *const *queries,
    size_t query_count,
    Arena *render_arena) {
    CettaGdlStratifiedEpisodeStatsV1 episode_stats = {0};
    CettaGdlStratifiedModelStatsV1 model_stats = {0};
    const CettaGdlStratifiedModelV1 *model = episode
        ? cetta_gdl_stratified_episode_model_v1(episode) : NULL;
    const char *digest = NULL;
    const char *revision = NULL;
    if (episode &&
        (!cetta_gdl_stratified_episode_stats_v1(
             episode, &episode_stats) ||
         !model ||
         !cetta_gdl_stratified_model_stats_v1(model, &model_stats) ||
         !cetta_gdl_stratified_episode_identity_v1(
             episode, &digest, &revision) ||
         !digest || !revision))
        return fail("episode evidence cannot be observed");

    size_t emitted_supports = 0u;
    size_t support_count = model
        ? cetta_gdl_stratified_model_support_count_v1(model) : 0u;
    for (size_t index = 0u; index < support_count; index++) {
        CettaGdlStratifiedSupportViewV1 support = {0};
        if (!cetta_gdl_stratified_model_support_view_v1(
                model, index, &support))
            return fail("episode support cannot be observed");
        if (support_selected(support.literal, queries, query_count))
            emitted_supports++;
    }

    printf(
        "E\t%zu\t%s\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu"
        "\t%zu\t%zu\t%zu\t%zu\t%s\n",
        ordinal, episode_outcome_name(kind),
        episode_stats.authored_facts,
        episode_stats.typing_proof_occurrences,
        episode_stats.seeded_support_nodes,
        episode_stats.seeded_proof_edges,
        model_stats.support_nodes, model_stats.proof_edges,
        model_stats.ground_instances, model_stats.distinct_checks,
        model_stats.absence_receipts, model_stats.completed_strata,
        query_count, emitted_supports,
        digest ? digest : "-");
    if (!model)
        return 0;

    for (size_t index = 0u; index < support_count; index++) {
        CettaGdlStratifiedSupportViewV1 support = {0};
        if (!cetta_gdl_stratified_model_support_view_v1(
                model, index, &support))
            return fail("episode support cannot be observed");
        if (!support_selected(support.literal, queries, query_count))
            continue;
        ArenaMark mark = arena_mark(render_arena);
        char *rendered = parser_render_syntax(
            render_arena, support.literal, PARSER_SYNTAX_PRINT_COMPACT);
        if (!rendered)
            return fail("episode support cannot be rendered");
        printf(
            "S\t%zu\t%zu\t%zu\t%zu\t%zu\t%s\n",
            ordinal, index, support.relation_index, support.stratum,
            support.proof_edge_count, rendered);
        arena_reset(render_arena, mark);
    }

    size_t authored_edges = 0u;
    size_t episode_edges = 0u;
    size_t edge_count =
        cetta_gdl_stratified_model_proof_edge_count_v1(model);
    for (size_t index = 0u; index < edge_count; index++) {
        CettaGdlStratifiedProofEdgeViewV1 edge = {0};
        if (!cetta_gdl_stratified_model_proof_edge_view_v1(
                model, index, &edge))
            return fail("episode proof edge cannot be observed");
        if (edge.origin == CETTA_GDL_STRATIFIED_PROOF_AUTHORED_SOURCE_V1) {
            if (edge.episode_occurrence)
                return fail("authored proof carries an episode occurrence");
            authored_edges++;
            continue;
        }
        if (edge.origin !=
                CETTA_GDL_STRATIFIED_PROOF_TYPED_EPISODE_FACT_V1 ||
            !edge.episode_occurrence)
            return fail("episode proof lacks its typed occurrence identity");
        episode_edges++;
        ArenaMark mark = arena_mark(render_arena);
        char *rendered = parser_render_syntax(
            render_arena, edge.episode_occurrence,
            PARSER_SYNTAX_PRINT_COMPACT);
        if (!rendered)
            return fail("episode occurrence cannot be rendered");
        printf(
            "P\t%zu\t%zu\t%zu\t%s\n",
            ordinal, index, edge.head_support_index, rendered);
        arena_reset(render_arena, mark);
    }
    if (episode_edges != episode_stats.seeded_proof_edges)
        return fail("typed episode edge count disagrees with its receipt");
    printf("O\t%zu\t%zu\t%zu\n", ordinal, authored_edges, episode_edges);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s GDL-EPISODE-WORKLOAD\n", argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena input_arena;
    Arena render_arena;
    Atom **forms = NULL;
    CettaGdlStratifiedModelResultV1 source = {0};
    int result = 1;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&input_arena);
    arena_init(&render_arena);

    const char *path = strcmp(argv[1], "-") == 0 ? "/dev/stdin" : argv[1];
    int form_count = parse_metta_file(path, &input_arena, &forms);
    if (form_count < 1 || !forms || !forms[0]) {
        result = fail("episode workload did not parse");
        goto done;
    }

    int episode_start = 1;
    const char *target_name = NULL;
    size_t target_arity = 0u;
    if (form_count > 1 &&
        expr_named(forms[1], "gdl-stratified-target-v1", 3u)) {
        if (!forms[1]->expr.elems[1] ||
            forms[1]->expr.elems[1]->kind != ATOM_SYMBOL ||
            !(target_name = atom_name_cstr(forms[1]->expr.elems[1])) ||
            target_name[0] == '\0' || target_name[0] == '$' ||
            !grounded_size(forms[1]->expr.elems[2], &target_arity)) {
            result = fail("target request is malformed");
            goto done;
        }
        episode_start = 2;
    }

    source = target_name
        ? cetta_gdl_stratified_model_admit_authored_target_v1(
            forms[0], target_name, target_arity,
            (CettaGdlStratifiedModelAdmissionLimitsV1){0})
        : cetta_gdl_stratified_model_admit_authored_source_v1(
            forms[0], (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    CettaNikDirectAuthorityTokenV1 source_token = {0};
    CettaNikNativeSelectionV1 selection = {0};
    uint64_t realization_identity = 0u;
    bool source_ready =
        source.kind == CETTA_GDL_STRATIFIED_MODEL_ESTABLISHED_V1 &&
        source.model &&
        cetta_gdl_stratified_model_token_v1(source.model, &source_token) &&
        cetta_gdl_stratified_model_selection_v1(
            source.model, &selection, &realization_identity);
    const char *selected_target = NULL;
    size_t selected_target_arity = 0u;
    size_t target_source_forms = 0u;
    size_t target_selected_forms = 0u;
    size_t target_reachable_relations = 0u;
    size_t target_external_relations = 0u;
    bool target_ready = target_name && source.model &&
        cetta_gdl_stratified_model_target_slice_v1(
            source.model, &selected_target, &selected_target_arity,
            &target_source_forms, &target_selected_forms,
            &target_reachable_relations, &target_external_relations);
    printf(
        "GdlStratifiedEpisodesV2\t%s\t%d\t%d\t%zu\t%zu\t%zu"
        "\t%s\t%zu\t%zu\t%zu\t%zu\t%zu\n",
        source_outcome_name(source.kind), form_count - episode_start,
        source_ready ? (int)selection.kind : 0,
        source_ready ? selection.eligible_count : 0u,
        source_ready ? selection.frontier_count : 0u,
        source_ready ? selection.greatest_index : 0u,
        target_name ? target_name : "-",
        target_ready ? selected_target_arity : target_arity,
        target_ready ? target_source_forms : 0u,
        target_ready ? target_selected_forms : 0u,
        target_ready ? target_reachable_relations : 0u,
        target_ready ? target_external_relations : 0u);
    if (!source_ready) {
        result = source.kind == CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1
            ? fail("source model construction faulted") : 0;
        goto done;
    }

    for (int form_index = episode_start;
         form_index < form_count; form_index++) {
        Atom *form = forms[form_index];
        if (!expr_named(form, "gdl-stratified-episode-v1", 4u) ||
            !form->expr.elems[1] ||
            !form->expr.elems[2] ||
            form->expr.elems[2]->kind != ATOM_EXPR ||
            form->expr.elems[2]->expr.len == 0u ||
            !atom_is_symbol(form->expr.elems[2]->expr.elems[0], "facts") ||
            !form->expr.elems[3] ||
            form->expr.elems[3]->kind != ATOM_EXPR ||
            form->expr.elems[3]->expr.len == 0u ||
            !atom_is_symbol(
                form->expr.elems[3]->expr.elems[0], "queries")) {
            result = fail("episode form is malformed");
            goto done;
        }
        Atom *facts = form->expr.elems[2];
        Atom *queries = form->expr.elems[3];
        CettaGdlStratifiedEpisodeResultV1 admitted =
            cetta_gdl_stratified_model_admit_episode_v1(
                source.model, &source_token, form->expr.elems[1],
                facts->expr.elems + 1u,
                (size_t)facts->expr.len - 1u,
                (CettaGdlStratifiedEpisodeLimitsV1){0});
        if (admitted.kind ==
                CETTA_GDL_STRATIFIED_EPISODE_ENGINE_FAULT_V1) {
            cetta_gdl_stratified_episode_destroy_v1(admitted.episode);
            result = fail("episode construction faulted");
            goto done;
        }
        result = print_episode(
            (size_t)(form_index - episode_start + 1), admitted.kind,
            admitted.episode,
            queries->expr.elems + 1u,
            (size_t)queries->expr.len - 1u,
            &render_arena);
        cetta_gdl_stratified_episode_destroy_v1(admitted.episode);
        if (result != 0)
            goto done;
    }
    result = 0;

done:
    cetta_gdl_stratified_model_destroy_v1(source.model);
    cetta_gdl_stratification_destroy_v1(
        source.negative_cycle_obstruction);
    free(forms);
    arena_free(&render_arena);
    arena_free(&input_arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    return result;
}
