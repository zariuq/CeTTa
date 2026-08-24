#include "gdl_stratified_model.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static const char *outcome_name(CettaGdlStratifiedModelKindV1 kind) {
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

static void print_stats(
    CettaGdlStratifiedModelKindV1 kind,
    const CettaGdlStratifiedModelStatsV1 *stats) {
    CettaGdlStratifiedModelStatsV1 zero = {0};
    if (!stats)
        stats = &zero;
    printf(
        "GdlStratifiedModelV1\t%s\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu"
        "\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\n",
        outcome_name(kind), stats->source_forms, stats->source_rules,
        stats->source_facts, stats->assignments,
        stats->branch_expansions, stats->ground_instances,
        stats->distinct_checks, stats->support_nodes,
        stats->proof_edges, stats->positive_premise_references,
        stats->absence_receipts, stats->rounds,
        stats->completed_strata);
}

static int print_negative_cycle(
    const CettaGdlStratificationV1 *obstruction) {
    if (!obstruction)
        return fail("negative-cycle result has no checked obstruction");
    size_t edge_count = cetta_gdl_stratification_edge_count_v1(obstruction);
    printf("N\t%zu\n", edge_count);
    for (size_t index = 0u; index < edge_count; index++) {
        CettaGdlDependencyEdgeViewV1 edge = {0};
        if (!cetta_gdl_stratification_edge_view_v1(
                obstruction, index, &edge))
            return fail("negative-cycle edge cannot be observed");
        printf(
            "NE\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%d\n",
            index, edge.source_form_ordinal, edge.source_start_line,
            edge.source_end_line, edge.head_relation,
            edge.body_relation, edge.negative ? 1 : 0);
    }
    return 0;
}

int main(int argc, char **argv) {
    bool print_proof_graph = argc == 3 &&
        strcmp(argv[2], "--proof-graph") == 0;
    if (argc != 2 && !print_proof_graph) {
        fprintf(
            stderr,
            "usage: %s GDL-TYPE-SOURCE [--proof-graph]\n",
            argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena package_arena;
    Atom **package_forms = NULL;
    CettaGdlStratifiedModelResultV1 constructed = {0};
    int result = 1;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&package_arena);

    const char *package_path = strcmp(argv[1], "-") == 0
        ? "/dev/stdin" : argv[1];
    int package_count = parse_metta_file(
        package_path, &package_arena, &package_forms);
    if (package_count != 1 || !package_forms || !package_forms[0]) {
        result = fail("one authored GDL source package did not parse");
        goto done;
    }

    constructed = cetta_gdl_stratified_model_admit_authored_source_v1(
        package_forms[0], (CettaGdlStratifiedModelAdmissionLimitsV1){0});
    if (constructed.kind ==
            CETTA_GDL_STRATIFIED_MODEL_ENGINE_FAULT_V1) {
        result = fail("stratified model construction faulted");
        goto done;
    }

    CettaGdlStratifiedModelStatsV1 stats = {0};
    if (constructed.model &&
        !cetta_gdl_stratified_model_stats_v1(
            constructed.model, &stats)) {
        result = fail("stratified model statistics cannot be observed");
        goto done;
    }
    print_stats(constructed.kind, constructed.model ? &stats : NULL);

    if (constructed.kind ==
            CETTA_GDL_STRATIFIED_MODEL_REFUTED_NEGATIVE_CYCLE_V1) {
        result = print_negative_cycle(
            constructed.negative_cycle_obstruction);
        goto done;
    }
    if (!constructed.model) {
        result = 0;
        goto done;
    }

    Arena render_arena;
    arena_init(&render_arena);
    size_t support_count =
        cetta_gdl_stratified_model_support_count_v1(constructed.model);
    for (size_t index = 0u; index < support_count; index++) {
        CettaGdlStratifiedSupportViewV1 support = {0};
        if (!cetta_gdl_stratified_model_support_view_v1(
                constructed.model, index, &support)) {
            arena_free(&render_arena);
            result = fail("stratified support cannot be observed");
            goto done;
        }
        ArenaMark mark = arena_mark(&render_arena);
        char *rendered = parser_render_syntax(
            &render_arena, support.literal,
            PARSER_SYNTAX_PRINT_COMPACT);
        if (!rendered) {
            arena_free(&render_arena);
            result = fail("stratified support cannot be rendered");
            goto done;
        }
        printf(
            "S\t%zu\t%zu\t%zu\t%zu\t%s\n",
            index, support.relation_index, support.stratum,
            support.proof_edge_count, rendered);
        arena_reset(&render_arena, mark);
    }
    if (print_proof_graph) {
        size_t edge_count =
            cetta_gdl_stratified_model_proof_edge_count_v1(
                constructed.model);
        for (size_t index = 0u; index < edge_count; index++) {
            CettaGdlStratifiedProofEdgeViewV1 edge = {0};
            if (!cetta_gdl_stratified_model_proof_edge_view_v1(
                    constructed.model, index, &edge)) {
                arena_free(&render_arena);
                result = fail("stratified proof edge cannot be observed");
                goto done;
            }
            printf(
                "E\t%zu\t%zu\t%zu\t%zu\t",
                index, edge.source_form_ordinal,
                edge.head_support_index, edge.substitution_count);
            if (edge.substitution_count == 0u) {
                fputc('-', stdout);
            } else {
                for (size_t variable = 0u;
                     variable < edge.substitution_count; variable++) {
                    if (variable != 0u)
                        fputc(',', stdout);
                    printf(
                        "%s=%zu:%s",
                        edge.substitution[variable].name,
                        edge.substitution[variable].term_index,
                        edge.substitution[variable].exact_type);
                }
            }
            printf(
                "\t%zu\t%zu\t%zu\t%zu\n",
                edge.branch_choice_count,
                edge.positive_premise_count,
                edge.absence_receipt_count,
                edge.distinct_evidence_count);
        }
    }
    arena_free(&render_arena);
    result = 0;

done:
    cetta_gdl_stratified_model_destroy_v1(constructed.model);
    cetta_gdl_stratification_destroy_v1(
        constructed.negative_cycle_obstruction);
    free(package_forms);
    arena_free(&package_arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    return result;
}
