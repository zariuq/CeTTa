#include "gdl_finite_herbrand.h"
#include "gdl_stratification.h"
#include "parser.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(int argc, char **argv) {
    bool herbrand_only = argc == 3 &&
        strcmp(argv[2], "--herbrand") == 0;
    if (argc != 2 && !herbrand_only) {
        fprintf(
            stderr,
            "usage: %s GDL-TYPE-SOURCE [--herbrand]\n",
            argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena package_arena;
    Arena source_arena;
    Atom **package_forms = NULL;
    GdlSourceRawFormsV1 source_forms = {0};
    GdlSourceProfileV1 profile = {0};
    CettaGdlStratificationV1 *analysis = NULL;
    CettaGdlFiniteHerbrandV1 *herbrand = NULL;
    int result = 1;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&package_arena);
    arena_init(&source_arena);
    arena_set_runtime_kind(
        &source_arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);

    const char *package_path = strcmp(argv[1], "-") == 0
        ? "/dev/stdin" : argv[1];
    int package_count = parse_metta_file(
        package_path, &package_arena, &package_forms);
    if (package_count != 1 || !package_forms || !package_forms[0]) {
        result = fail("one authored GDL source package did not parse");
        goto done;
    }
    GdlSourcePackageV1 package = {0};
    if (gdl_source_package_view_v1(
            package_forms[0], NULL, NULL, NULL, &package) !=
            GDL_SOURCE_PARSE_OK_V1) {
        result = fail("authored GDL source package is malformed");
        goto done;
    }
    if (herbrand_only) {
        if (gdl_source_parse_profile_v1(
                &source_arena, package.profile_text, &profile) !=
            GDL_SOURCE_PARSE_OK_V1) {
            result = fail("authored GDL type profile did not parse");
            goto done;
        }
        CettaGdlFiniteHerbrandResultV1 constructed =
            cetta_gdl_finite_herbrand_construct_v1(
                &profile, (CettaGdlFiniteHerbrandLimitsV1){0});
        herbrand = constructed.domain;
        if (constructed.kind !=
                CETTA_GDL_FINITE_HERBRAND_ESTABLISHED_V1 ||
            !herbrand) {
            fprintf(
                stderr,
                "FAIL: finite Herbrand outcome %d is not Established\n",
                (int)constructed.kind);
            goto done;
        }
        CettaGdlFiniteHerbrandStatsV1 stats = {0};
        if (!cetta_gdl_finite_herbrand_stats_v1(herbrand, &stats)) {
            result = fail("finite Herbrand statistics cannot be observed");
            goto done;
        }
        printf(
            "GdlFiniteHerbrandV1\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\t%zu\n",
            stats.types, stats.signatures, stats.constructors,
            stats.relations, stats.subtype_edges, stats.terms,
            stats.construction_witnesses,
            stats.constructor_applications, stats.rounds,
            stats.maximum_depth);
        Arena render_arena;
        arena_init(&render_arena);
        for (size_t term_index = 0u;
             term_index < stats.terms; term_index++) {
            CettaGdlFiniteHerbrandTermViewV1 term = {0};
            if (!cetta_gdl_finite_herbrand_term_view_v1(
                    herbrand, term_index, &term)) {
                arena_free(&render_arena);
                result = fail("finite Herbrand term cannot be observed");
                goto done;
            }
            ArenaMark mark = arena_mark(&render_arena);
            char *rendered = parser_render_syntax(
                &render_arena, term.term, PARSER_SYNTAX_PRINT_COMPACT);
            if (!rendered) {
                arena_free(&render_arena);
                result = fail("finite Herbrand term cannot be rendered");
                goto done;
            }
            printf(
                "T\t%zu\t%s\t%zu\t%zu\t%s\n",
                term_index, term.exact_type, term.depth,
                term.construction_count, rendered);
            arena_reset(&render_arena, mark);
            for (size_t construction_index = 0u;
                 construction_index < term.construction_count;
                 construction_index++) {
                CettaGdlFiniteHerbrandConstructionViewV1 construction = {0};
                if (!cetta_gdl_finite_herbrand_construction_view_v1(
                        herbrand, term_index, construction_index,
                        &construction)) {
                    arena_free(&render_arena);
                    result = fail(
                        "finite Herbrand construction cannot be observed");
                    goto done;
                }
                printf(
                    "C\t%zu\t%zu\t%zu\t%zu\t%zu\t",
                    term_index, construction_index,
                    construction.signature_index,
                    construction.statement_ordinal,
                    construction.name_ordinal);
                if (construction.argument_count == 0u) {
                    fputc('-', stdout);
                } else {
                    for (size_t argument = 0u;
                         argument < construction.argument_count; argument++) {
                        if (argument != 0u)
                            fputc(',', stdout);
                        printf(
                            "%zu",
                            construction.argument_term_indices[argument]);
                    }
                }
                fputc('\n', stdout);
            }
        }
        for (size_t relation_index = 0u;
             relation_index < stats.relations; relation_index++) {
            CettaGdlFiniteHerbrandRelationViewV1 relation = {0};
            if (!cetta_gdl_finite_herbrand_relation_view_v1(
                    herbrand, relation_index, &relation)) {
                arena_free(&render_arena);
                result = fail("finite Herbrand relation cannot be observed");
                goto done;
            }
            printf(
                "Q\t%zu\t%zu\t%zu\t%zu\t%s\t%zu\t",
                relation_index, relation.signature_index,
                relation.statement_ordinal, relation.name_ordinal,
                relation.name, relation.argument_count);
            if (relation.argument_count == 0u) {
                fputc('-', stdout);
            } else {
                for (size_t argument = 0u;
                     argument < relation.argument_count; argument++) {
                    if (argument != 0u)
                        fputc(',', stdout);
                    fputs(relation.argument_types[argument], stdout);
                }
            }
            fputc('\n', stdout);
        }
        arena_free(&render_arena);
        result = 0;
        goto done;
    }
    if (gdl_source_parse_forms_v1(
            &source_arena, package.source_text, 4096u,
            &source_forms) != GDL_SOURCE_PARSE_OK_V1) {
        result = fail("authored GDL source text did not parse");
        goto done;
    }
    CettaGdlStratificationResultV1 constructed =
        cetta_gdl_stratification_construct_v1(
            &source_forms, (CettaGdlStratificationLimitsV1){0});
    analysis = constructed.analysis;
    if (constructed.kind != CETTA_GDL_STRATIFICATION_ESTABLISHED_V1 ||
        !analysis) {
        fprintf(
            stderr, "FAIL: stratification outcome %d is not Established\n",
            (int)constructed.kind);
        goto done;
    }

    size_t relation_count =
        cetta_gdl_stratification_relation_count_v1(analysis);
    size_t edge_count = cetta_gdl_stratification_edge_count_v1(analysis);
    printf(
        "GdlStratificationV1\t%zu\t%zu\t%zu\n",
        relation_count, edge_count,
        cetta_gdl_stratification_maximum_stratum_v1(analysis));
    for (size_t index = 0u; index < relation_count; index++) {
        CettaGdlStratifiedRelationViewV1 relation = {0};
        if (!cetta_gdl_stratification_relation_view_v1(
                analysis, index, &relation)) {
            result = fail("relation witness cannot be observed");
            goto done;
        }
        printf(
            "R\t%zu\t%s\t%zu\t%zu\t%d\n",
            index, relation.name, relation.arity, relation.stratum,
            relation.defined ? 1 : 0);
    }
    for (size_t index = 0u; index < edge_count; index++) {
        CettaGdlDependencyEdgeViewV1 edge = {0};
        if (!cetta_gdl_stratification_edge_view_v1(
                analysis, index, &edge)) {
            result = fail("dependency-edge witness cannot be observed");
            goto done;
        }
        printf(
            "E\t%zu\t%zu\t%zu\t%zu\t",
            index, edge.source_form_ordinal,
            edge.source_start_line, edge.source_end_line);
        if (edge.path_length == 0u) {
            fputc('-', stdout);
        } else {
            for (size_t step = 0u; step < edge.path_length; step++) {
                if (step != 0u)
                    fputc('.', stdout);
                printf("%zu", edge.path[step]);
            }
        }
        printf(
            "\t%zu\t%zu\t%d\n",
            edge.head_relation, edge.body_relation,
            edge.negative ? 1 : 0);
    }
    result = 0;

done:
    cetta_gdl_finite_herbrand_destroy_v1(herbrand);
    cetta_gdl_stratification_destroy_v1(analysis);
    gdl_source_profile_free_v1(&profile);
    gdl_source_raw_forms_free_v1(&source_forms);
    free(package_forms);
    arena_free(&source_arena);
    arena_free(&package_arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    return result;
}
