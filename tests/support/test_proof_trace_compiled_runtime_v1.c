#include "langdef/metamath/generated/proof_trace_language_v1.generated.h"
#include "langdef/metamath/generated/proof_trace_provider_catalog_v1.generated.h"

#include "gslt_compiled_runtime.h"
#include "gslt_finite_fact_provider_v1.h"
#include "gslt_horn_runtime.h"
#include "gslt_language_runtime.h"
#include "parser.h"
#include "symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ERROR_CAP = 1024 };

static unsigned checks;
static unsigned failures;
static uint64_t source_rule_attempts;
static uint64_t compiled_rule_attempts;
static uint64_t source_rule_matches;
static uint64_t compiled_rule_matches;
static uint64_t compiled_dispatch_rejects;
static uint64_t compiled_outer_head_elisions;
static uint64_t compiled_prefilter_rejects;
static uint64_t compiled_ground_dense_attempts;
static uint64_t compiled_flat_head_attempts;
static uint64_t compiled_general_head_attempts;
static uint64_t compiled_constructor_guided_attempts;
static uint64_t compiled_constructor_guided_matches;
static uint64_t compiled_constructor_nodes_elided;
static uint64_t compiled_flat_head_matches;
static uint64_t compiled_ground_dense_matches;
static uint64_t compiled_variable_slot_buffer_uses;
static uint64_t compiled_variable_slot_bytes_elided;
static uint64_t compiled_variable_slot_clear_bytes_elided;
static uint64_t compiled_ground_subterm_cache_hits;
static uint64_t compiled_ground_subterm_nodes_elided;
static uint64_t compiled_worklist_states_created;
static uint64_t compiled_worklist_states_reclaimed;
static uint64_t compiled_worklist_pending_peak;
static uint64_t compiled_worklist_state_bytes_peak;
static uint64_t provider_queries;
static uint64_t provider_indexed_queries;
static uint64_t provider_rows_considered;
static uint64_t provider_rows_skipped;
static size_t provider_indexed_relations;

typedef struct {
    CettaGsltHornProgram *program;
    Atom **rows;
    size_t row_count;
} FactSource;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL: %s\n", (label));                         \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static char *read_text(const char *path) {
    FILE *stream = fopen(path, "rb");
    long raw_length;
    size_t length;
    char *text;

    if (!stream || fseek(stream, 0, SEEK_END) != 0 ||
        (raw_length = ftell(stream)) < 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        if (stream)
            fclose(stream);
        return NULL;
    }
    length = (size_t)raw_length;
    if ((long)length != raw_length || length == SIZE_MAX ||
        !(text = malloc(length + 1u))) {
        fclose(stream);
        return NULL;
    }
    if (fread(text, 1u, length, stream) != length) {
        free(text);
        fclose(stream);
        return NULL;
    }
    text[length] = '\0';
    fclose(stream);
    return text;
}

static Atom *parse_query_file(Arena *arena, const char *path) {
    char *text = read_text(path);
    Atom **forms = NULL;
    int count = text ? parse_metta_text(text, arena, &forms) : -1;
    Atom *query = count == 1 && forms ? forms[0] : NULL;

    free(forms);
    free(text);
    return query;
}

static bool provider_row(const Atom *head) {
    if (!head || head->kind != ATOM_EXPR || head->expr.len == 0u ||
        !head->expr.elems[0] ||
        head->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    const char *relation = atom_name_cstr(head->expr.elems[0]);
    for (size_t index = 0u;
         index < cetta_metamath_proof_trace_provider_catalog_v1.requirement_count;
         index++) {
        const CettaGsltProviderRequirementV1 *requirement =
            &cetta_metamath_proof_trace_provider_catalog_v1.requirements[index];
        if ((uint64_t)head->expr.len == (uint64_t)requirement->arity + 1u &&
            strcmp(relation, requirement->relation) == 0)
            return true;
    }
    return false;
}

static bool load_fact_source(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    const char *path, FactSource *source) {
    char error[ERROR_CAP] = {0};
    memset(source, 0, sizeof(*source));
    char *fact_text = read_text(path);
    CettaGsltHornInput *inputs = fact_text && descriptor
        ? calloc(descriptor->semantic_source_count + 1u, sizeof(*inputs))
        : NULL;
    if (!inputs) {
        free(fact_text);
        return false;
    }
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++)
        inputs[index] = descriptor->semantic_sources[index].input;
    inputs[descriptor->semantic_source_count] = (CettaGsltHornInput){
        .bytes = (const uint8_t *)fact_text,
        .length = strlen(fact_text),
        .source = path,
    };
    bool loaded = cetta_gslt_horn_program_load_inputs(
        inputs, descriptor->semantic_source_count + 1u,
        &source->program, error, sizeof(error));
    free(inputs);
    free(fact_text);
    if (!loaded) {
        fprintf(stderr, "fact-source diagnostic: %s\n", error);
        return false;
    }
    size_t rule_count = cetta_gslt_horn_program_rule_count(source->program);
    source->rows = rule_count
        ? calloc(rule_count, sizeof(*source->rows)) : NULL;
    if (rule_count > 0u && !source->rows)
        return false;
    for (size_t index = 0u; index < rule_count; index++) {
        CettaGsltHornRuleViewV1 view;
        if (!cetta_gslt_horn_program_rule_view_v1(
                source->program, index, &view) || !view.head)
            return false;
        if (!provider_row(view.head))
            continue;
        if (view.body_count != 0u)
            return false;
        source->rows[source->row_count++] = (Atom *)view.head;
    }
    return source->row_count > 0u;
}

static void free_fact_source(FactSource *source) {
    if (!source)
        return;
    free(source->rows);
    cetta_gslt_horn_program_free(source->program);
    memset(source, 0, sizeof(*source));
}

static bool answer_bags_equal(const CettaGsltHornResult *left,
                              const CettaGsltHornResult *right) {
    if (!left || !right || left->answer_count != right->answer_count)
        return false;
    bool *used = calloc(right->answer_count, sizeof(*used));
    if (!used && right->answer_count != 0u)
        return false;
    for (size_t left_index = 0u; left_index < left->answer_count;
         left_index++) {
        bool found = false;
        for (size_t right_index = 0u; right_index < right->answer_count;
             right_index++) {
            if (!used[right_index] &&
                atom_eq(left->answers[left_index],
                        right->answers[right_index])) {
                used[right_index] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            free(used);
            return false;
        }
    }
    free(used);
    return true;
}

static void check_query(const CettaGsltLanguage *source,
                        const CettaGsltLanguage *compiled,
                        const CettaGsltHornProgram *source_program,
                        const CettaGsltCompiledProgram *compiled_program,
                        const CettaGsltProviderRegistryV1 *providers,
                        const char *path, bool admitted_entry,
                        bool expected_answer) {
    Arena query_arena;
    Arena source_output;
    Arena compiled_output;
    CettaGsltHornResult source_result = {0};
    CettaGsltHornResult compiled_result = {0};
    char source_error[ERROR_CAP] = {0};
    char compiled_error[ERROR_CAP] = {0};
    CettaGsltHornLimits limits = {
        .max_rule_attempts = UINT64_C(10000000),
        .max_answers = UINT64_C(64),
        .max_depth = 4096u,
    };

    arena_init(&query_arena);
    arena_init(&source_output);
    arena_init(&compiled_output);
    Atom *query = parse_query_file(&query_arena, path);
    CHECK(query != NULL, "proof-trace fixture contains one query");
    bool source_ok = false;
    bool compiled_ok = false;
    if (query && admitted_entry) {
        source_ok = cetta_gslt_language_query_with_providers_v1(
            source, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
            &cetta_metamath_proof_trace_provider_catalog_v1, providers,
            &source_output, query, limits, &source_result,
            source_error, sizeof(source_error));
        compiled_ok = cetta_gslt_language_query_with_providers_v1(
            compiled, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &cetta_metamath_proof_trace_provider_catalog_v1, providers,
            &compiled_output, query, limits, &compiled_result,
            compiled_error, sizeof(compiled_error));
    } else if (query) {
        source_ok = cetta_gslt_horn_query_with_providers_v1(
            source_program, providers, &source_output, query, limits,
            &source_result, source_error, sizeof(source_error));
        compiled_ok = cetta_gslt_compiled_query_with_providers_v1(
            compiled_program, providers, &compiled_output, query, limits,
            &compiled_result, compiled_error, sizeof(compiled_error));
    }
    CHECK(source_ok && compiled_ok,
          "source and compiled proof-trace machines both execute");
    if (!source_ok && source_error[0] != '\0')
        fprintf(stderr, "source diagnostic: %s\n", source_error);
    if (!compiled_ok && compiled_error[0] != '\0')
        fprintf(stderr, "compiled diagnostic: %s\n", compiled_error);
    if (source_ok && compiled_ok) {
        source_rule_attempts += source_result.rule_attempts;
        compiled_rule_attempts += compiled_result.rule_attempts;
        source_rule_matches += source_result.rule_matches;
        compiled_rule_matches += compiled_result.rule_matches;
        compiled_dispatch_rejects +=
            compiled_result.rule_dispatch_rejects;
        compiled_outer_head_elisions +=
            compiled_result.rule_outer_head_elisions;
        compiled_prefilter_rejects +=
            compiled_result.rule_prefilter_rejects;
        compiled_ground_dense_attempts +=
            compiled_result.rule_ground_dense_attempts;
        compiled_flat_head_attempts +=
            compiled_result.rule_flat_head_attempts;
        compiled_general_head_attempts +=
            compiled_result.rule_general_head_attempts;
        compiled_constructor_guided_attempts +=
            compiled_result.rule_constructor_guided_attempts;
        compiled_constructor_guided_matches +=
            compiled_result.rule_constructor_guided_matches;
        compiled_constructor_nodes_elided +=
            compiled_result.rule_constructor_nodes_elided;
        compiled_flat_head_matches +=
            compiled_result.rule_flat_head_matches;
        compiled_ground_dense_matches +=
            compiled_result.rule_ground_dense_matches;
        compiled_variable_slot_buffer_uses +=
            compiled_result.rule_variable_slot_buffer_uses;
        compiled_variable_slot_bytes_elided +=
            compiled_result.rule_variable_slot_bytes_elided;
        compiled_variable_slot_clear_bytes_elided +=
            compiled_result.rule_variable_slot_clear_bytes_elided;
        compiled_ground_subterm_cache_hits +=
            compiled_result.rule_ground_subterm_cache_hits;
        compiled_ground_subterm_nodes_elided +=
            compiled_result.rule_ground_subterm_nodes_elided;
        compiled_worklist_states_created +=
            compiled_result.worklist_states_created;
        compiled_worklist_states_reclaimed +=
            compiled_result.worklist_states_reclaimed;
        if (compiled_worklist_pending_peak <
            compiled_result.worklist_pending_peak)
            compiled_worklist_pending_peak =
                compiled_result.worklist_pending_peak;
        if (compiled_worklist_state_bytes_peak <
            compiled_result.worklist_state_bytes_peak)
            compiled_worklist_state_bytes_peak =
                compiled_result.worklist_state_bytes_peak;
        CHECK(source_result.outcome == CETTA_GSLT_HORN_COMPLETED &&
                  compiled_result.outcome == CETTA_GSLT_HORN_COMPLETED,
              "source and compiled proof-trace machines terminate normally");
        CHECK(answer_bags_equal(&source_result, &compiled_result),
              "compiled proof-trace answer bag equals its source semantics");
        CHECK((compiled_result.answer_count != 0u) == expected_answer,
              expected_answer
                  ? "compiled proof-trace accepts the positive witness"
                  : "compiled proof-trace rejects the negative witness");
    }
    cetta_gslt_horn_result_free(&source_result);
    cetta_gslt_horn_result_free(&compiled_result);
    arena_free(&compiled_output);
    arena_free(&source_output);
    arena_free(&query_arena);
}

int main(int argc, char **argv) {
    if (argc < 5 || ((argc - 3) % 2) != 0) {
        fprintf(stderr,
                "usage: %s NORMAL_FACTS COMPRESSED_FACTS "
                "(accept|reject|raw-accept|raw-reject QUERY)+\n",
                argv[0]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variable_names;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;

    const CettaGsltEmbeddedLanguageV1 *descriptor =
        &cetta_metamath_proof_trace_v1;
    CettaGsltHornInput *inputs = cetta_malloc(
        descriptor->semantic_source_count * sizeof(*inputs));
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++)
        inputs[index] = descriptor->semantic_sources[index].input;
    CettaGsltHornProgram *source_program = NULL;
    CettaGsltCompiledProgram *compiled_program = NULL;
    CettaGsltLanguage *source_language = NULL;
    CettaGsltLanguage *compiled_language = NULL;
    FactSource normal_facts = {0};
    FactSource compressed_facts = {0};
    CettaGsltFiniteFactProviderSetV1 *provider_set = NULL;
    char error[ERROR_CAP] = {0};
    CHECK(cetta_gslt_horn_program_load_inputs(
              inputs, descriptor->semantic_source_count,
              &source_program, error, sizeof(error)),
          "composed proof-trace source admits as finite Horn");
    free(inputs);
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_compiled_program_load_v1(
              &descriptor->compiled_plan, &compiled_program,
              error, sizeof(error)),
          "generated proof-trace CGP1 plan admits in generic C");
    if (source_program && compiled_program) {
        memset(error, 0, sizeof(error));
        CHECK(cetta_gslt_compiled_program_matches_source_v1(
                  compiled_program, source_program, error, sizeof(error)),
              "proof-trace CGP1 plan exactly matches composed source");
    }
    CHECK(load_fact_source(descriptor, argv[1], &normal_facts),
          "normal chronological facts admit independently of the program");
    CHECK(load_fact_source(descriptor, argv[2], &compressed_facts),
          "compressed chronological facts admit independently of the program");
    if (normal_facts.program && compressed_facts.program) {
        CettaGsltFiniteFactSpanV1 spans[2] = {
            {
                .rows = normal_facts.rows,
                .row_count = normal_facts.row_count,
            },
            {
                .rows = compressed_facts.rows,
                .row_count = compressed_facts.row_count,
            },
        };
        memset(error, 0, sizeof(error));
        provider_set =
            cetta_gslt_finite_fact_provider_set_create_borrowed_v1(
                cetta_metamath_proof_trace_provider_catalog_v1.requirements,
                cetta_metamath_proof_trace_provider_catalog_v1.requirement_count,
                spans, 2u, error, sizeof(error));
        if (!provider_set)
            fprintf(stderr, "provider-set diagnostic: %s\n", error);
        CHECK(provider_set &&
                  cetta_gslt_finite_fact_provider_set_row_count_v1(
                      provider_set) ==
                      normal_facts.row_count + compressed_facts.row_count,
              "generated catalog admits the variable-length chronological rows");
    }
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded_for_realization(
              descriptor, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
              &source_language, error, sizeof(error)),
          "proof-trace service loads in the source realization");
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded_for_realization(
              descriptor, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &compiled_language, error, sizeof(error)),
          "proof-trace service loads in the compiled realization");
    if (source_language && compiled_language && provider_set) {
        const CettaGsltProviderRegistryV1 *providers =
            cetta_gslt_finite_fact_provider_set_registry_v1(provider_set);
        for (int index = 3; index < argc; index += 2) {
            bool admitted_entry = strncmp(argv[index], "raw-", 4u) != 0;
            const char *expectation = admitted_entry
                ? argv[index] : argv[index] + 4;
            bool expected_answer = strcmp(expectation, "accept") == 0;
            if (!expected_answer && strcmp(expectation, "reject") != 0) {
                fprintf(stderr, "unknown proof-trace expectation: %s\n",
                        argv[index]);
                failures++;
                break;
            }
            check_query(source_language, compiled_language,
                        source_program, compiled_program, providers,
                        argv[index + 1],
                        admitted_entry, expected_answer);
        }
        CHECK(compiled_rule_attempts <= source_rule_attempts,
              "compiled proof-trace execution does no more rule work");
        CHECK(source_rule_matches == compiled_rule_matches,
              "compiled proof-trace execution preserves matching-rule work");
        CHECK(compiled_rule_matches < compiled_rule_attempts,
              "guarded body materialization skips rejected candidates");
        CHECK(compiled_dispatch_rejects != 0u,
              "rigid-coordinate dispatch skips impossible candidates");
        CHECK(compiled_outer_head_elisions != 0u,
              "head/arity buckets elide redundant outer-head checks");
        CHECK(compiled_prefilter_rejects != 0u,
              "rigid head prefilter rejects impossible candidates");
        CHECK(compiled_flat_head_matches != 0u,
              "flat variable heads bypass whole-head materialization");
        CHECK(compiled_ground_dense_matches != 0u,
              "ground dense heads bypass fresh variables and bindings");
        CHECK(compiled_constructor_guided_attempts != 0u &&
                  compiled_constructor_guided_matches != 0u &&
                  compiled_constructor_nodes_elided != 0u,
              "constructor-guided matching decomposes open structured heads");
        CHECK(compiled_variable_slot_buffer_uses > 1u &&
                  compiled_variable_slot_bytes_elided != 0u &&
                  compiled_variable_slot_clear_bytes_elided != 0u,
              "finite variable slots reuse and epoch-clear query storage");
        CHECK(compiled_ground_subterm_cache_hits != 0u &&
                  compiled_ground_subterm_nodes_elided >=
                      compiled_ground_subterm_cache_hits,
              "immutable ground subterms bypass dynamic construction");
        CHECK(compiled_ground_dense_attempts +
                  compiled_flat_head_attempts +
                  compiled_general_head_attempts +
                  compiled_constructor_guided_attempts +
                  compiled_prefilter_rejects ==
                compiled_outer_head_elisions,
              "compiled matcher routes partition every inspected candidate");
        CHECK(compiled_worklist_states_created != 0u,
              "compiled proof trace reports worklist state ownership");
        CettaGsltFiniteFactProviderStatsV1 provider_stats;
        cetta_gslt_finite_fact_provider_set_stats_v1(
            provider_set, &provider_stats);
        provider_queries = provider_stats.queries;
        provider_indexed_queries = provider_stats.indexed_queries;
        provider_rows_considered = provider_stats.rows_considered;
        provider_rows_skipped = provider_stats.rows_skipped;
        provider_indexed_relations = provider_stats.indexed_relations;
        CHECK(provider_indexed_relations != 0u,
              "ground provider inventory admits rigid relation indexes");
        CHECK(provider_indexed_queries != 0u,
              "proof-trace execution exercises rigid provider indexes");
        CHECK(provider_rows_skipped != 0u,
              "rigid provider indexes skip physical row matches");
    }

    cetta_gslt_language_free(compiled_language);
    cetta_gslt_language_free(source_language);
    cetta_gslt_finite_fact_provider_set_free_v1(provider_set);
    free_fact_source(&compressed_facts);
    free_fact_source(&normal_facts);
    cetta_gslt_compiled_program_free(compiled_program);
    cetta_gslt_horn_program_free(source_program);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);

    if (failures != 0u) {
        fprintf(stderr, "%u/%u compiled proof-trace checks failed\n",
                failures, checks);
        return 1;
    }
    printf("(ProofTraceCompiledRuntimeV1Summary checks=%u failures=0 "
           "source-rule-attempts=%" PRIu64 " compiled-rule-attempts=%" PRIu64
           " source-rule-matches=%" PRIu64 " compiled-rule-matches=%" PRIu64
           " compiled-dispatch-rejects=%" PRIu64
           " compiled-outer-head-elisions=%" PRIu64
           " compiled-prefilter-rejects=%" PRIu64
           " compiled-ground-dense-attempts=%" PRIu64
           " compiled-flat-head-attempts=%" PRIu64
           " compiled-general-head-attempts=%" PRIu64
           " compiled-constructor-guided-attempts=%" PRIu64
           " compiled-constructor-guided-matches=%" PRIu64
           " compiled-constructor-nodes-elided=%" PRIu64
           " compiled-flat-head-matches=%" PRIu64
           " compiled-ground-dense-matches=%" PRIu64
           " compiled-variable-slot-buffer-uses=%" PRIu64
           " compiled-variable-slot-bytes-elided=%" PRIu64
           " compiled-variable-slot-clear-bytes-elided=%" PRIu64
           " compiled-ground-subterm-cache-hits=%" PRIu64
           " compiled-ground-subterm-nodes-elided=%" PRIu64
           " compiled-worklist-states-created=%" PRIu64
           " compiled-worklist-states-reclaimed=%" PRIu64
           " compiled-worklist-pending-peak=%" PRIu64
           " compiled-worklist-state-bytes-peak=%" PRIu64
           " provider-queries=%" PRIu64
           " provider-indexed-queries=%" PRIu64
           " provider-rows-considered=%" PRIu64
           " provider-rows-skipped=%" PRIu64
           " provider-indexed-relations=%zu"
           ")\n",
           checks, source_rule_attempts, compiled_rule_attempts,
           source_rule_matches, compiled_rule_matches,
           compiled_dispatch_rejects, compiled_outer_head_elisions,
           compiled_prefilter_rejects,
           compiled_ground_dense_attempts,
           compiled_flat_head_attempts,
           compiled_general_head_attempts,
           compiled_constructor_guided_attempts,
           compiled_constructor_guided_matches,
           compiled_constructor_nodes_elided,
           compiled_flat_head_matches,
           compiled_ground_dense_matches,
           compiled_variable_slot_buffer_uses,
           compiled_variable_slot_bytes_elided,
           compiled_variable_slot_clear_bytes_elided,
           compiled_ground_subterm_cache_hits,
           compiled_ground_subterm_nodes_elided,
           compiled_worklist_states_created,
           compiled_worklist_states_reclaimed,
           compiled_worklist_pending_peak,
           compiled_worklist_state_bytes_peak,
           provider_queries,
           provider_indexed_queries,
           provider_rows_considered,
           provider_rows_skipped,
           provider_indexed_relations);
    return 0;
}
