#include "gdl_positive_horn_host.h"
#include "parser.h"
#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t episodes;
    size_t queries;
    size_t proof_occurrences;
    size_t rule_nodes;
    size_t typed_episode_nodes;
    size_t finite_absence_nodes;
    size_t proof_canaries;
} NativeQualificationStatsV1;

static bool expr_named(
    const Atom *atom, const char *name, CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
        atom_is_symbol(atom->expr.elems[0], name);
}

static bool atom_contains_head(
    const Atom *atom, const char *head, size_t depth) {
    if (!atom || !head || depth > 4096u || atom->kind != ATOM_EXPR)
        return false;
    if (atom->expr.len > 0u && atom_is_symbol(atom->expr.elems[0], head))
        return true;
    for (CettaExprLen index = 0u; index < atom->expr.len; index++)
        if (atom_contains_head(atom->expr.elems[index], head, depth + 1u))
            return true;
    return false;
}

static bool audit_proof_tree(
    const Atom *atom, size_t depth, NativeQualificationStatsV1 *stats) {
    if (!atom || !stats || depth > 4096u)
        return false;
    if (atom->kind != ATOM_EXPR)
        return true;
    if (atom->expr.len > 0u &&
        atom_is_symbol(atom->expr.elems[0], "gdl:rule"))
        stats->rule_nodes++;
    if (atom->expr.len > 0u &&
        atom_is_symbol(atom->expr.elems[0], "gdl:episode-fact")) {
        if (!expr_named(atom, "gdl:episode-fact", 3u) ||
            !atom_contains_head(
                atom->expr.elems[1],
                "gdl:space-fact-occurrence-v1", 0u) ||
            !atom_contains_head(
                atom->expr.elems[1],
                "gdl:native-ground-literal-v1", 0u))
            return false;
        stats->typed_episode_nodes++;
    }
    if (atom->expr.len > 0u &&
        atom_is_symbol(atom->expr.elems[0], "gdl:finite-absence-proof")) {
        if (!expr_named(atom, "gdl:finite-absence-proof", 3u) ||
            !atom_contains_head(
                atom->expr.elems[1],
                "gdl:complete-finite-relation-view-v1", 0u) ||
            !atom_contains_head(
                atom->expr.elems[1],
                "gdl:native-ground-literal-v1", 0u))
            return false;
        stats->finite_absence_nodes++;
    }
    for (CettaExprLen index = 0u; index < atom->expr.len; index++)
        if (!audit_proof_tree(atom->expr.elems[index], depth + 1u, stats))
            return false;
    return true;
}

static Atom *proof_goal(Arena *arena, Atom *quoted_proof) {
    if (!arena || !expr_named(quoted_proof, "quote", 2u))
        return NULL;
    Atom *proof = quoted_proof->expr.elems[1];
    if ((expr_named(proof, "gdl:rule", 4u) ||
         expr_named(proof, "gdl:fact", 4u)) &&
        expr_named(
            proof->expr.elems[2], "gdl:structural-judgment", 2u))
        return proof->expr.elems[2]->expr.elems[1];
    if (expr_named(proof, "gdl:episode-fact", 3u))
        return proof->expr.elems[2];
    if (expr_named(proof, "gdl:distinct-proof", 4u)) {
        Atom *items[] = {
            atom_symbol(arena, "distinct"),
            proof->expr.elems[2],
            proof->expr.elems[3],
        };
        return items[0] ? atom_expr(arena, items, 3u) : NULL;
    }
    return NULL;
}

static bool run_selection_is_native(
    const CettaGdlPositiveHornRunV1 *run, bool finite_view) {
    const CettaNikDirectAuthorityV1 *authority = finite_view
        ? cetta_gdl_finite_view_native_authority_v1()
        : cetta_gdl_positive_horn_native_authority_v1();
    return run && authority &&
        run->selection.status ==
            CETTA_NIK_NATIVE_SELECTION_STATUS_OK_V1 &&
        run->selection.kind ==
            CETTA_NIK_NATIVE_SELECTION_UNIQUE_GREATEST_V1 &&
        run->selection.eligible_count == 2u &&
        run->selection.frontier_count == 1u &&
        run->selection.greatest_index == 1u &&
        run->selected_realization_identity ==
            authority->realization_identity;
}

static bool result_occurrences(
    Atom *result, Atom ***occurrences_out, size_t *count_out) {
    if (!expr_named(result, "compile-result", 5u) ||
        !occurrences_out || !count_out)
        return false;
    Atom *bag = result->expr.elems[2];
    if (!bag || bag->kind != ATOM_EXPR || bag->expr.len == 0u ||
        !atom_is_symbol(bag->expr.elems[0], "occurrences"))
        return false;
    *occurrences_out = bag->expr.elems + 1u;
    *count_out = (size_t)bag->expr.len - 1u;
    return true;
}

static bool print_query_observation(
    CettaGdlPositiveHornHostedEpisodeV1 *episode,
    const Space *source_space,
    const Space *episode_space,
    Atom *label,
    Atom *query,
    bool proof_canary,
    bool finite_view,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences,
    bool first,
    NativeQualificationStatsV1 *stats) {
    Arena result_arena;
    arena_init(&result_arena);
    CettaGdlPositiveHornRunV1 run =
        cetta_gdl_positive_horn_hosted_episode_run_v1(
            episode, source_space, episode_space,
            &result_arena, query, depth, max_states, max_occurrences);
    Atom **occurrences = NULL;
    size_t occurrence_count = 0u;
    if (run.kind != CETTA_GDL_POSITIVE_HORN_RUN_COMPLETE_V1 ||
        !run_selection_is_native(&run, finite_view) ||
        !result_occurrences(
            run.result, &occurrences, &occurrence_count)) {
        fputs("FAIL: native query did not return one selected complete bag\n",
              stderr);
        arena_free(&result_arena);
        return false;
    }

    size_t rule_nodes_before = stats->rule_nodes;
    if (!first)
        fputs(", ", stdout);
    fputs("(gdl:answers ", stdout);
    atom_print(label, stdout);
    fputs(" (", stdout);
    for (size_t index = 0u; index < occurrence_count; index++) {
        Atom *occurrence = occurrences[index];
        if (!expr_named(occurrence, "occurrence", 2u) ||
            !audit_proof_tree(
                occurrence->expr.elems[1], 0u, stats)) {
            fputs("FAIL: malformed or untyped native proof occurrence\n",
                  stderr);
            arena_free(&result_arena);
            return false;
        }
        Atom *goal = proof_goal(
            &result_arena, occurrence->expr.elems[1]);
        if (!goal) {
            fputs("FAIL: native proof has no recognized exact conclusion\n",
                  stderr);
            arena_free(&result_arena);
            return false;
        }
        if (index != 0u)
            fputc(' ', stdout);
        atom_print(goal, stdout);
    }
    fputs("))", stdout);
    if (proof_canary) {
        if (stats->rule_nodes == rule_nodes_before) {
            fputs("FAIL: marked proof canary retained no rule node\n", stderr);
            arena_free(&result_arena);
            return false;
        }
        stats->proof_canaries++;
    }
    stats->queries++;
    stats->proof_occurrences += occurrence_count;
    arena_free(&result_arena);
    return true;
}

static bool run_episode(
    CettaGdlPositiveHornHostV1 *host,
    const Space *source_space,
    Arena *workload_arena,
    Atom *form,
    uint32_t depth,
    uint64_t max_states,
    uint32_t max_occurrences,
    NativeQualificationStatsV1 *stats) {
    if (!host || !source_space || !workload_arena || !stats ||
        (!expr_named(
             form, "gdl-positive-horn-native-episode-v1", 4u) &&
         !expr_named(
             form, "gdl-finite-view-native-episode-v1", 4u)))
        return false;
    bool finite_view = expr_named(
        form, "gdl-finite-view-native-episode-v1", 4u);
    Atom *identity = form->expr.elems[1];
    Atom *facts = form->expr.elems[2];
    Atom *queries = form->expr.elems[3];
    if (!facts || facts->kind != ATOM_EXPR || facts->expr.len == 0u ||
        !atom_is_symbol(facts->expr.elems[0], "facts") ||
        !queries || queries->kind != ATOM_EXPR ||
        queries->expr.len == 0u ||
        !atom_is_symbol(queries->expr.elems[0], "queries"))
        return false;

    size_t fact_count = (size_t)facts->expr.len - 1u;
    CettaIndex *fact_occurrences = fact_count
        ? malloc(fact_count * sizeof(*fact_occurrences)) : NULL;
    if (fact_count && !fact_occurrences)
        return false;

    Space episode_space;
    space_init(&episode_space);
    bool ok = true;
    for (size_t index = 0u; index < fact_count; index++) {
        fact_occurrences[index] = space_length64(&episode_space);
        if (!space_admit_atom_from_source_arena(
                &episode_space, NULL, workload_arena,
                facts->expr.elems[index + 1u])) {
            ok = false;
            break;
        }
    }

    CettaGdlPositiveHornHostedEpisodeV1 *episode = NULL;
    if (ok) {
        CettaGdlPositiveHornHostedEpisodeAdmissionV1 admitted = finite_view
            ? cetta_gdl_finite_view_host_admit_complete_episode_v1(
                host, source_space, &episode_space, identity,
                (CettaGdlPositiveHornEpisodeLimitsV1){0})
            : cetta_gdl_positive_horn_host_admit_episode_v1(
                host, source_space, &episode_space,
                identity, fact_occurrences, fact_count,
                (CettaGdlPositiveHornEpisodeLimitsV1){0});
        episode = admitted.episode;
        ok = admitted.kind ==
                CETTA_GDL_POSITIVE_HORN_EPISODE_ADMITTED_V1 &&
            episode &&
            cetta_gdl_positive_horn_hosted_episode_is_current_v1(
                episode, source_space, &episode_space);
    }
    CettaGdlPositiveHornHostedEpisodeReceiptV1 receipt = {0};
    CettaGdlPositiveHornEpisodeStatsV1 episode_stats = {0};
    if (ok)
        ok = cetta_gdl_positive_horn_hosted_episode_receipt_v1(
                 episode, source_space, &episode_space, &receipt) &&
            receipt.fact_count == fact_count &&
            cetta_gdl_positive_horn_hosted_episode_stats_v1(
                episode, &episode_stats) &&
            episode_stats.authored_facts == fact_count &&
            episode_stats.typing_proof_occurrences >= fact_count &&
            (!finite_view ||
             episode_stats.finite_state_absence_proof_occurrences != 0u);
    if (!ok)
        fputs("FAIL: typed Space-hosted episode admission failed\n", stderr);

    if (ok)
        fputc('[', stdout);
    for (CettaExprLen index = 1u;
         ok && index < queries->expr.len; index++) {
        Atom *query = queries->expr.elems[index];
        if (!expr_named(query, "query", 4u) ||
            (!atom_is_symbol(query->expr.elems[3], "answers-only") &&
             !atom_is_symbol(query->expr.elems[3], "proof-canary"))) {
            ok = false;
            break;
        }
        ok = print_query_observation(
            episode, source_space, &episode_space,
            query->expr.elems[1], query->expr.elems[2],
            atom_is_symbol(query->expr.elems[3], "proof-canary"),
            finite_view,
            depth, max_states, max_occurrences,
            index == 1u, stats);
    }
    if (ok) {
        fputs("]\n", stdout);
        stats->episodes++;
    }

    cetta_gdl_positive_horn_hosted_episode_destroy_v1(episode);
    space_free(&episode_space);
    free(fact_occurrences);
    return ok;
}

static bool parse_u64(const char *text, uint64_t maximum, uint64_t *out) {
    if (!text || !*text || !out)
        return false;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0u ||
        (uint64_t)value > maximum)
        return false;
    *out = (uint64_t)value;
    return true;
}

static const char *source_revision(Atom *source) {
    if (!expr_named(source, "gdl-type-source-v1", 6u))
        return NULL;
    Atom *field = source->expr.elems[3];
    return expr_named(field, "revision", 2u) &&
            field->expr.elems[1]->kind == ATOM_SYMBOL
        ? atom_name_cstr(field->expr.elems[1])
        : NULL;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
                "usage: %s WORKLOAD DEPTH MAX_STATES MAX_OCCURRENCES\n",
                argv[0]);
        return 2;
    }
    uint64_t parsed_depth;
    uint64_t max_states;
    uint64_t parsed_occurrences;
    if (!parse_u64(argv[2], UINT32_MAX, &parsed_depth) ||
        !parse_u64(argv[3], INT64_MAX, &max_states) ||
        !parse_u64(argv[4], UINT32_MAX, &parsed_occurrences)) {
        fputs("FAIL: invalid native qualification bounds\n", stderr);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    Arena workload_arena;
    Atom **forms = NULL;
    Space source_space;
    bool source_space_initialized = false;
    CettaGdlPositiveHornHostV1 *host = NULL;
    NativeQualificationStatsV1 stats = {0};
    int result = 1;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&workload_arena);

    int form_count = parse_metta_file(argv[1], &workload_arena, &forms);
    if (form_count < 2 || !forms) {
        fputs("FAIL: native qualification workload is empty\n", stderr);
        goto done;
    }
    bool finite_view = expr_named(
        forms[1], "gdl-finite-view-native-episode-v1", 4u);
    if (!finite_view && !expr_named(
            forms[1], "gdl-positive-horn-native-episode-v1", 4u)) {
        fputs("FAIL: workload has no recognized native GDL mode\n", stderr);
        goto done;
    }
    Atom *source = forms[0];
    const char *revision = source_revision(source);
    if (!revision) {
        fputs("FAIL: workload does not begin with one source package\n",
              stderr);
        goto done;
    }

    space_init(&source_space);
    source_space_initialized = true;
    CettaIndex source_occurrence = space_length64(&source_space);
    if (!space_admit_atom_from_source_arena(
            &source_space, NULL, &workload_arena, source)) {
        fputs("FAIL: authored source package did not enter Space\n", stderr);
        goto done;
    }
    CettaGdlPositiveHornHostAdmissionV1 admitted = finite_view
        ? cetta_gdl_finite_view_host_admit_v1(
            &source_space, source_occurrence, revision,
            (CettaGdlPositiveHornLimitsV1){0})
        : cetta_gdl_positive_horn_host_admit_v1(
            &source_space, source_occurrence, revision,
            (CettaGdlPositiveHornLimitsV1){0});
    host = admitted.host;
    if (admitted.kind != CETTA_GDL_POSITIVE_HORN_HOST_ADMITTED_V1 ||
        !host ||
        !cetta_gdl_positive_horn_host_is_current_v1(host, &source_space)) {
        fputs("FAIL: authored source did not admit a current native kernel\n",
              stderr);
        goto done;
    }

    for (int index = 1; index < form_count; index++) {
        if (finite_view != expr_named(
                forms[index],
                "gdl-finite-view-native-episode-v1", 4u)) {
            fputs("FAIL: workload mixes native GDL modes\n", stderr);
            goto done;
        }
        if (!run_episode(
                host, &source_space, &workload_arena, forms[index],
                (uint32_t)parsed_depth, max_states,
                (uint32_t)parsed_occurrences, &stats))
            goto done;
    }
    if (stats.episodes != (size_t)form_count - 1u ||
        stats.queries == 0u || stats.proof_occurrences == 0u ||
        stats.rule_nodes == 0u || stats.typed_episode_nodes == 0u ||
        (finite_view && stats.finite_absence_nodes == 0u) ||
        stats.proof_canaries == 0u) {
        fputs("FAIL: native qualification lacks a required proof witness\n",
              stderr);
        goto done;
    }
    fprintf(stderr,
            "NativeGdlQualification mode=%s episodes=%zu queries=%zu "
            "proof_occurrences=%zu rule_nodes=%zu "
            "typed_episode_nodes=%zu finite_absence_nodes=%zu "
            "proof_canaries=%zu\n",
            finite_view ? "finite-view" : "positive-horn",
            stats.episodes, stats.queries, stats.proof_occurrences,
            stats.rule_nodes, stats.typed_episode_nodes,
            stats.finite_absence_nodes, stats.proof_canaries);
    result = 0;

done:
    cetta_gdl_positive_horn_host_destroy_v1(host);
    if (source_space_initialized)
        space_free(&source_space);
    free(forms);
    arena_free(&workload_arena);
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    return result;
}
