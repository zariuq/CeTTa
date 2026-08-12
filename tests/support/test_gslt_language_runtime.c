#include "atom.h"
#include "gslt_compiled_runtime.h"
#include "gslt_language_runtime.h"
#include "he_compiled_reader.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"
#include "generated/subzero_language_v1.generated.h"
#include "generated/gslt_il_language_v1.generated.h"
#include "generated/zero_language_v1.generated.h"
#include "generated/zero_exp_language_v1.generated.h"
#include "tests/generated/gslt_compiled_canary_v1.generated.h"
#include "tests/generated/gslt_pipeline_canary_v1.generated.h"
#include "tests/generated/metta_zero_ground_library_v1.generated.h"

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

static int parse_document(Arena *arena, const char *text, Atom ***forms) {
    return parse_metta_text(text, arena, forms);
}

static CettaGsltHornLimits limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 1000000u,
        .max_answers = 1000u,
        .max_depth = 10000u,
    };
}

static uint8_t *find_bytes(uint8_t *haystack, size_t haystack_length,
                           const char *needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0u || needle_length > haystack_length)
        return NULL;
    for (size_t offset = 0u;
         offset <= haystack_length - needle_length; offset++)
        if (memcmp(haystack + offset, needle, needle_length) == 0)
            return haystack + offset;
    return NULL;
}

static bool plan_u32_at(const uint8_t *bytes, size_t length,
                        size_t offset, uint32_t *value) {
    if (!bytes || !value || offset > length || length - offset < 4u)
        return false;
    *value = (uint32_t)bytes[offset] |
             ((uint32_t)bytes[offset + 1u] << 8u) |
             ((uint32_t)bytes[offset + 2u] << 16u) |
             ((uint32_t)bytes[offset + 3u] << 24u);
    return true;
}

static void plan_write_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
    bytes[offset + 2u] = (uint8_t)(value >> 16u);
    bytes[offset + 3u] = (uint8_t)(value >> 24u);
}

static bool plan_skip_text(const uint8_t *bytes, size_t length,
                           size_t *offset) {
    uint32_t text_length;
    if (!offset || !plan_u32_at(bytes, length, *offset, &text_length))
        return false;
    *offset += 4u;
    if (*offset > length || text_length > length - *offset)
        return false;
    *offset += text_length;
    return true;
}

/* Locate generic finite-variable certificate fields in a CGP1 plan without
 * depending on any guest rule or vocabulary. */
static bool plan_finite_variable_offsets(
    const uint8_t *bytes, size_t length, size_t *variable_slot_offset,
    size_t *variable_count_offset, uint32_t *variable_count) {
    enum {
        PLAN_HEADER_SIZE = 20u,
        PLAN_NODE_FIXED_SIZE = 21u,
        PLAN_RULE_FIXED_SIZE = 16u,
        PLAN_VARIABLE_KIND = 2u,
    };
    uint32_t node_count, child_count, rule_count;
    if (!bytes || length < PLAN_HEADER_SIZE ||
        memcmp(bytes, "CGP1", 4u) != 0 ||
        !plan_u32_at(bytes, length, 4u, &node_count) ||
        !plan_u32_at(bytes, length, 8u, &child_count) ||
        !plan_u32_at(bytes, length, 12u, &rule_count))
        return false;
    size_t offset = PLAN_HEADER_SIZE;
    bool found_slot = false;
    bool found_count = false;
    for (uint32_t node = 0u; node < node_count; node++) {
        if (offset > length || PLAN_NODE_FIXED_SIZE > length - offset)
            return false;
        if (!found_slot && bytes[offset] == PLAN_VARIABLE_KIND) {
            *variable_slot_offset = offset + 17u;
            found_slot = true;
        }
        offset += PLAN_NODE_FIXED_SIZE;
        if (!plan_skip_text(bytes, length, &offset))
            return false;
    }
    if (offset > length || child_count > (length - offset) / 4u)
        return false;
    offset += (size_t)child_count * 4u;
    for (uint32_t rule = 0u; rule < rule_count; rule++) {
        uint32_t count;
        if (offset > length || PLAN_RULE_FIXED_SIZE > length - offset ||
            !plan_u32_at(bytes, length, offset + 12u, &count))
            return false;
        if (!found_count && count > 0u) {
            *variable_count_offset = offset + 12u;
            *variable_count = count;
            found_count = true;
        }
        offset += PLAN_RULE_FIXED_SIZE;
        if (!plan_skip_text(bytes, length, &offset))
            return false;
    }
    return found_slot && found_count;
}

static void check_finite_variable_plan_admission(
    const CettaGsltEmbeddedLanguageV1 *descriptor) {
    size_t plan_length = descriptor->compiled_plan.length;
    uint8_t *plan = malloc(plan_length);
    char digest[65];
    char error[ERROR_CAP] = {0};
    size_t variable_slot_offset = 0u;
    size_t variable_count_offset = 0u;
    uint32_t variable_count = 0u;
    CettaGsltLanguage *language = NULL;
    CettaGsltEmbeddedLanguageV1 invalid_descriptor;

    CHECK(plan != NULL, "finite-variable mutation buffer is allocated");
    if (!plan)
        return;
    memcpy(plan, descriptor->compiled_plan.bytes, plan_length);
    bool found_finite_fields = plan_finite_variable_offsets(
        plan, plan_length, &variable_slot_offset,
        &variable_count_offset, &variable_count);
    CHECK(found_finite_fields,
          "compiled plan exposes a finite variable inventory");
    if (!found_finite_fields) {
        free(plan);
        return;
    }

    plan_write_u32(plan, variable_slot_offset, UINT32_MAX);
    cetta_native_sha256_hex(plan, plan_length, digest);
    invalid_descriptor = *descriptor;
    invalid_descriptor.compiled_plan.bytes = plan;
    invalid_descriptor.compiled_plan.sha256 = digest;
    CHECK(!cetta_gslt_language_load_embedded_for_realization(
              &invalid_descriptor,
              CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &language, error, sizeof(error)) &&
              strstr(error, "rule forest") != NULL,
          "out-of-range variable slot fails structural admission");
    if (language) {
        cetta_gslt_language_free(language);
        language = NULL;
    }

    memcpy(plan, descriptor->compiled_plan.bytes, plan_length);
    CHECK(variable_count < UINT32_MAX,
          "finite variable count can be mutated without overflow");
    if (variable_count < UINT32_MAX) {
        plan_write_u32(
            plan, variable_count_offset, variable_count + 1u);
        cetta_native_sha256_hex(plan, plan_length, digest);
        invalid_descriptor = *descriptor;
        invalid_descriptor.compiled_plan.bytes = plan;
        invalid_descriptor.compiled_plan.sha256 = digest;
        memset(error, 0, sizeof(error));
        CHECK(!cetta_gslt_language_load_embedded_for_realization(
                  &invalid_descriptor,
                  CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
                  &language, error, sizeof(error)) &&
                  strstr(error, "rule forest") != NULL,
              "unused variable slot fails complete-inventory admission");
        if (language)
            cetta_gslt_language_free(language);
    }
    free(plan);
}

static uint64_t check_compiled_index_shape(
    const CettaGsltEmbeddedLanguageV1 *descriptor) {
    CettaGsltCompiledProgram *program = NULL;
    CettaGsltCompiledIndexStatsV1 stats = {0};
    char error[ERROR_CAP] = {0};
    CettaGsltCompiledInputV1 input = {
        .bytes = descriptor->compiled_plan.bytes,
        .length = descriptor->compiled_plan.length,
        .sha256 = descriptor->compiled_plan.sha256,
    };
    bool loaded = cetta_gslt_compiled_program_load_v1(
        &input, &program, error, sizeof(error));
    CHECK(loaded, "compiled plan admits a generic head/arity index");
    if (!loaded)
        return 0u;
    CHECK(cetta_gslt_compiled_program_index_stats_v1(program, &stats),
          "compiled plan exposes its generic index certificate");
    CHECK(stats.bucket_count > 0u,
          "compiled index contains at least one head/arity bucket");
    CHECK(stats.slot_count >= stats.bucket_count * 2u,
          "compiled index maintains its admitted load-factor bound");
    CHECK(stats.maximum_probe <= stats.insertion_collisions,
          "compiled index collision accounting is internally ordered");
    CHECK(stats.dispatch_bucket_count <= stats.bucket_count &&
              stats.dispatch_group_count >= stats.dispatch_bucket_count,
          "compiled coordinate index is derived within head/arity buckets");
    cetta_gslt_compiled_program_free(program);
    return stats.insertion_collisions;
}

static bool all_answers_equal(const CettaGsltLanguageResult *result,
                              Atom *expected) {
    for (size_t index = 0u; index < result->answer_count; index++)
        if (!atom_eq(result->answers[index], expected))
            return false;
    return true;
}

static bool language_results_equal(const CettaGsltLanguageResult *left,
                                   const CettaGsltLanguageResult *right) {
    if (left->outcome != right->outcome ||
        left->answer_count != right->answer_count)
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

static bool horn_results_equal(const CettaGsltHornResult *left,
                               const CettaGsltHornResult *right) {
    if (left->outcome != right->outcome ||
        left->answer_count != right->answer_count)
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

static void check_compiled_query_optimizations_cross_guest(
    const CettaGsltEmbeddedLanguageV1 *descriptor, const char *query_text,
    bool expect_ground_dense, bool expect_dispatch, bool expect_prefilter,
    const char *label) {
    Arena query_arena;
    Arena source_answers;
    Arena compiled_answers;
    Atom **forms = NULL;
    CettaGsltHornProgram *source = NULL;
    CettaGsltCompiledProgram *compiled = NULL;
    CettaGsltHornResult reference = {0};
    CettaGsltHornResult optimized = {0};
    char reference_error[ERROR_CAP] = {0};
    char optimized_error[ERROR_CAP] = {0};

    arena_init(&query_arena);
    arena_init(&source_answers);
    arena_init(&compiled_answers);
    int form_count = parse_document(&query_arena, query_text, &forms);
    CHECK(form_count == 1, "compiled optimization witness has one query");
    CettaGsltHornInput *inputs = cetta_malloc(
        descriptor->semantic_source_count * sizeof(*inputs));
    for (size_t index = 0u; index < descriptor->semantic_source_count;
         index++)
        inputs[index] = descriptor->semantic_sources[index].input;
    bool source_loaded = cetta_gslt_horn_program_load_inputs(
        inputs, descriptor->semantic_source_count, &source,
        reference_error, sizeof(reference_error));
    free(inputs);
    bool compiled_loaded = cetta_gslt_compiled_program_load_v1(
        &descriptor->compiled_plan, &compiled,
        optimized_error, sizeof(optimized_error));
    CHECK(source_loaded && compiled_loaded, label);
    if (source_loaded && compiled_loaded && form_count == 1) {
        CHECK(cetta_gslt_compiled_program_matches_source_v1(
              compiled, source, optimized_error,
                  sizeof(optimized_error)),
              "compiled optimization plan matches its authored source");
        bool reference_ok = cetta_gslt_horn_query(
            source, &source_answers, forms[0], limits(), &reference,
            reference_error, sizeof(reference_error));
        bool optimized_ok = cetta_gslt_compiled_query_v1(
            compiled, &compiled_answers, forms[0], limits(), &optimized,
            optimized_error, sizeof(optimized_error));
        CHECK(reference_ok && optimized_ok,
              "direct source and compiled executions succeed");
        if (reference_ok && optimized_ok) {
            CHECK(horn_results_equal(&reference, &optimized),
                  "direct compiled optimizations preserve the answer bag");
            CHECK(reference.rule_attempts == optimized.rule_attempts &&
                      reference.rule_matches == optimized.rule_matches,
                  "direct compiled optimizations preserve rule accounting");
            CHECK(reference.rule_ground_dense_matches == 0u &&
                      (expect_ground_dense
                          ? optimized.rule_ground_dense_matches > 0u
                          : optimized.rule_ground_dense_matches == 0u),
                  expect_ground_dense
                      ? "ground dense matching bypasses generic bindings"
                      : "non-ground head remains on another matcher route");
            CHECK(reference.rule_prefilter_rejects == 0u &&
                      (expect_prefilter
                          ? optimized.rule_prefilter_rejects > 0u
                          : true),
                  expect_prefilter
                      ? "recursive rigid prefilter rejects a nested mismatch"
                      : "reference execution has no compiled prefilter accounting");
            CHECK(reference.rule_dispatch_rejects == 0u &&
                      (expect_dispatch
                          ? optimized.rule_dispatch_rejects > 0u
                          : true),
                  expect_dispatch
                      ? "rigid-coordinate index skips direct candidates"
                      : "reference execution has no compiled dispatch accounting");
            CHECK(reference.rule_outer_head_elisions == 0u &&
                      optimized.rule_outer_head_elisions > 0u,
                  "head/arity buckets elide direct-query outer checks");
            CHECK(reference.worklist_states_created == 0u &&
                      optimized.worklist_states_created > 0u &&
                      optimized.worklist_states_reclaimed ==
                          optimized.worklist_states_created &&
                      optimized.worklist_state_bytes_peak > 0u,
                  "compiled worklist reclaims every value-owned state region");
            if (expect_dispatch && reference.rule_attempts < 64u) {
                for (uint64_t fuel = 1u;
                     fuel <= reference.rule_attempts + 1u; fuel++) {
                    CettaGsltHornLimits bounded = limits();
                    CettaGsltHornResult boundedReference = {0};
                    CettaGsltHornResult boundedOptimized = {0};
                    bounded.max_rule_attempts = fuel;
                    memset(reference_error, 0, sizeof(reference_error));
                    memset(optimized_error, 0, sizeof(optimized_error));
                    bool boundedReferenceOk = cetta_gslt_horn_query(
                        source, &source_answers, forms[0], bounded,
                        &boundedReference, reference_error,
                        sizeof(reference_error));
                    bool boundedOptimizedOk = cetta_gslt_compiled_query_v1(
                        compiled, &compiled_answers, forms[0], bounded,
                        &boundedOptimized, optimized_error,
                        sizeof(optimized_error));
                    CHECK(boundedReferenceOk && boundedOptimizedOk,
                          "bounded dispatch witnesses both execute");
                    if (boundedReferenceOk && boundedOptimizedOk) {
                        CHECK(horn_results_equal(
                                  &boundedReference, &boundedOptimized) &&
                              boundedReference.rule_attempts ==
                                  boundedOptimized.rule_attempts &&
                              boundedReference.rule_matches ==
                                  boundedOptimized.rule_matches,
                              "dispatch preserves every bounded fuel cut");
                    }
                    if (boundedReferenceOk)
                        cetta_gslt_horn_result_free(&boundedReference);
                    if (boundedOptimizedOk)
                        cetta_gslt_horn_result_free(&boundedOptimized);
                }
            }
        }
        if (reference_ok)
            cetta_gslt_horn_result_free(&reference);
        if (optimized_ok)
            cetta_gslt_horn_result_free(&optimized);
    } else {
        if (reference_error[0])
            fprintf(stderr, "%s source diagnostic: %s\n",
                    label, reference_error);
        if (optimized_error[0])
            fprintf(stderr, "%s compiled diagnostic: %s\n",
                    label, optimized_error);
    }
    cetta_gslt_compiled_program_free(compiled);
    cetta_gslt_horn_program_free(source);
    free(forms);
    arena_free(&compiled_answers);
    arena_free(&source_answers);
    arena_free(&query_arena);
}

static void check_compiled_optimizations_cross_guest(
    const CettaGsltLanguage *language, Arena *source, Arena *answers,
    const char *text, bool require_dispatch, bool require_prefilter,
    bool require_flat_head, bool require_ground_dense,
    const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult reference;
    CettaGsltLanguageResult compiled;
    char reference_error[ERROR_CAP] = {0};
    char compiled_error[ERROR_CAP] = {0};
    bool reference_ok = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
            forms, (size_t)form_count, answers, limits(),
            &reference, reference_error, sizeof(reference_error));
    bool compiled_ok = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            forms, (size_t)form_count, answers, limits(),
            &compiled, compiled_error, sizeof(compiled_error));
    CHECK(reference_ok && compiled_ok, label);
    if (reference_ok && compiled_ok) {
        CHECK(language_results_equal(&reference, &compiled),
              "compiled optimizations preserve the reference result bag");
        CHECK(compiled.rule_attempts == reference.rule_attempts &&
                  compiled.rule_matches == reference.rule_matches,
              "compiled optimizations preserve rule accounting");
        CHECK(reference.rule_outer_head_elisions == 0u &&
                  compiled.rule_outer_head_elisions > 0u,
              "head/arity buckets elide redundant outer-head checks");
        CHECK(reference.worklist_states_created == 0u &&
                  compiled.worklist_states_created > 0u &&
                  compiled.worklist_states_reclaimed ==
                      compiled.worklist_states_created &&
                  compiled.worklist_state_bytes_peak > 0u,
              "cross-guest worklists reclaim every value-owned state region");
        CHECK(reference.rule_variable_slot_buffer_uses == 0u &&
                  reference.rule_variable_slot_bytes_elided == 0u &&
                  reference.rule_variable_slot_clear_bytes_elided == 0u &&
                  compiled.rule_variable_slot_buffer_uses > 1u &&
                  compiled.rule_variable_slot_bytes_elided > 0u &&
                  compiled.rule_variable_slot_clear_bytes_elided > 0u,
              "cross-guest matching reuses and epoch-clears finite slots");
        CHECK(reference.rule_ground_subterm_cache_hits == 0u &&
                  reference.rule_ground_subterm_nodes_elided == 0u &&
                  compiled.rule_ground_subterm_cache_hits > 0u &&
                  compiled.rule_ground_subterm_nodes_elided >=
                      compiled.rule_ground_subterm_cache_hits,
              "cross-guest execution reuses immutable ground subterms");
        CHECK(reference.rule_constructor_guided_attempts == 0u &&
                  reference.rule_constructor_guided_matches == 0u &&
                  reference.rule_constructor_nodes_elided == 0u &&
                  compiled.rule_constructor_guided_attempts > 0u &&
                  compiled.rule_constructor_guided_matches > 0u &&
                  compiled.rule_constructor_nodes_elided > 0u,
              "cross-guest matching decomposes rigid constructors directly");
        bool dispatch_exercised = compiled.rule_dispatch_rejects > 0u &&
            reference.rule_dispatch_rejects == 0u;
        if (require_dispatch && !dispatch_exercised)
            fprintf(stderr,
                    "%s: reference dispatch=%llu compiled dispatch=%llu\n",
                    label,
                    (unsigned long long)reference.rule_dispatch_rejects,
                    (unsigned long long)compiled.rule_dispatch_rejects);
        if (require_dispatch)
            CHECK(dispatch_exercised,
                  "rigid-coordinate dispatch rejects candidates in compiled C only");
        bool prefilter_exercised = compiled.rule_prefilter_rejects > 0u &&
            reference.rule_prefilter_rejects == 0u;
        if (require_prefilter && !prefilter_exercised)
            fprintf(stderr,
                    "%s: reference rejects=%llu compiled rejects=%llu\n",
                    label,
                    (unsigned long long)reference.rule_prefilter_rejects,
                    (unsigned long long)compiled.rule_prefilter_rejects);
        if (require_prefilter)
            CHECK(prefilter_exercised,
                  "rigid prefilter rejects candidates in compiled C only");
        bool flat_head_exercised = compiled.rule_flat_head_matches > 0u &&
            reference.rule_flat_head_matches == 0u;
        if (require_flat_head && !flat_head_exercised)
            fprintf(stderr,
                    "%s: reference flat=%llu compiled flat=%llu\n",
                    label,
                    (unsigned long long)reference.rule_flat_head_matches,
                    (unsigned long long)compiled.rule_flat_head_matches);
        if (require_flat_head)
            CHECK(flat_head_exercised,
                  "flat-head matching executes in compiled C only");
        bool ground_dense_exercised =
            compiled.rule_ground_dense_matches > 0u &&
            reference.rule_ground_dense_matches == 0u;
        if (require_ground_dense && !ground_dense_exercised)
            fprintf(stderr,
                    "%s: reference ground-dense=%llu "
                    "compiled ground-dense=%llu\n",
                    label,
                    (unsigned long long)
                        reference.rule_ground_dense_matches,
                    (unsigned long long)
                        compiled.rule_ground_dense_matches);
        if (require_ground_dense)
            CHECK(ground_dense_exercised,
                  "ground dense matching executes in compiled C only");
    } else {
        if (reference_error[0])
            fprintf(stderr, "%s reference diagnostic: %s\n",
                    label, reference_error);
        if (compiled_error[0])
            fprintf(stderr, "%s compiled diagnostic: %s\n",
                    label, compiled_error);
    }
    if (reference_ok)
        cetta_gslt_language_result_free(&reference);
    if (compiled_ok)
        cetta_gslt_language_result_free(&compiled);
    free(forms);
}

static bool evidence_path(const Atom *evidence, const char *outer,
                          const char *inner) {
    if (!evidence || evidence->kind != ATOM_EXPR ||
        evidence->expr.len == 0u ||
        evidence->expr.elems[0]->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(evidence->expr.elems[0]), outer) != 0)
        return false;
    if (!inner)
        return true;
    if (evidence->expr.len < 2u) {
        return false;
    }
    const Atom *nested = evidence->expr.elems[1];
    return nested->kind == ATOM_EXPR && nested->expr.len > 0u &&
        nested->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(nested->expr.elems[0]), inner) == 0;
}

static void run_document_evidence_realization(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers, const char *text,
    const char *outer, const char *inner, const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult result;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, limits(), &result, error, sizeof(error));
    CHECK(executed && result.outcome == CETTA_GSLT_HORN_COMPLETED &&
              result.answer_count == 1u && result.evidence &&
              evidence_path(result.evidence[0], outer, inner),
          label);
    if (executed)
        cetta_gslt_language_result_free(&result);
    else if (error[0])
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    free(forms);
}

static void run_document_realization(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers,
    const char *text, const char *expected_text,
    size_t expected_count, const char *label) {
    Atom **forms = NULL;
    Atom **expected_forms = NULL;
    int form_count = parse_document(source, text, &forms);
    int expected_form_count = parse_document(
        answers, expected_text, &expected_forms);
    CettaGsltLanguageResult result;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 && expected_form_count == 1 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, limits(), &result, error, sizeof(error));
    CHECK(executed, label);
    if (executed) {
        CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED,
              "language execution completes semantically");
        CHECK(result.answer_count == expected_count,
              "language execution preserves the expected bag cardinality");
        CHECK(all_answers_equal(&result, expected_forms[0]),
              "language execution returns the expected payload bag");
        cetta_gslt_language_result_free(&result);
    } else if (error[0]) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    }
    free(expected_forms);
    free(forms);
}

static void run_document(const CettaGsltLanguage *language,
                         Arena *source, Arena *answers,
                         const char *text, const char *expected_text,
                         size_t expected_count, const char *label) {
    run_document_realization(
        language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
        source, answers, text, expected_text, expected_count, label);
}

static void run_resource_limit(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers, const char *text,
    CettaGsltHornLimits bounded,
    CettaGsltHornOutcome expected, const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult result;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, bounded, &result, error, sizeof(error));
    CHECK(executed, label);
    if (executed) {
        CHECK(result.outcome == expected,
              "generated realization reports the exact resource boundary");
        CHECK(result.answer_count == 0u,
              "incomplete generated execution publishes no partial answers");
        cetta_gslt_language_result_free(&result);
    }
    free(forms);
}

static void run_completion_boundary(
    const CettaGsltLanguage *language, CettaGsltRealization realization,
    Arena *source, Arena *answers, const char *text, const char *label) {
    Atom **forms = NULL;
    int form_count = parse_document(source, text, &forms);
    CettaGsltLanguageResult complete;
    char error[ERROR_CAP] = {0};
    bool executed = form_count >= 0 &&
        cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers, limits(), &complete, error, sizeof(error));
    CHECK(executed && complete.outcome == CETTA_GSLT_HORN_COMPLETED &&
              complete.answer_count == 1u && complete.rule_attempts > 1u,
          label);
    if (executed && complete.outcome == CETTA_GSLT_HORN_COMPLETED &&
        complete.rule_attempts > 1u) {
        uint64_t exact_attempts = complete.rule_attempts;
        cetta_gslt_language_result_free(&complete);
        CettaGsltLanguageResult incomplete;
        memset(error, 0, sizeof(error));
        executed = cetta_gslt_language_execute_atoms_with_realization(
            language, realization, forms, (size_t)form_count,
            answers,
            (CettaGsltHornLimits){
                .max_rule_attempts = exact_attempts - 1u,
                .max_answers = 1000u,
                .max_depth = 10000u,
            },
            &incomplete, error, sizeof(error));
        CHECK(executed && incomplete.outcome == CETTA_GSLT_HORN_RULE_LIMIT,
              "pipeline reports an open frontier immediately before completion");
        if (executed) {
            CHECK(incomplete.answer_count == 0u,
                  "open pipeline frontier cannot trigger inert observation");
            cetta_gslt_language_result_free(&incomplete);
        }
    } else if (executed) {
        cetta_gslt_language_result_free(&complete);
    }
    free(forms);
}

static void run_compiled_document(
    const CettaGsltLanguage *language, HECompiledReaderV1 *reader,
    TermUniverse *universe, Arena *answers, const char *text,
    const char *expected_text, size_t expected_count, const char *label) {
    AtomId *ids = NULL;
    Atom **forms = NULL;
    Atom **expected_forms = NULL;
    HECompiledReaderV1Receipt receipt;
    char error[ERROR_CAP] = {0};
    int form_count = he_compiled_reader_v1_parse_text_ids(
        reader, text, universe, &ids, &receipt, error, sizeof(error));
    int expected_form_count = parse_document(
        answers, expected_text, &expected_forms);
    if (form_count > 0) {
        forms = malloc(sizeof(*forms) * (size_t)form_count);
        for (int index = 0; index < form_count; index++)
            forms[index] = term_universe_get_atom(universe, ids[index]);
    }
    CettaGsltLanguageResult result;
    bool executed = form_count >= 0 && expected_form_count == 1 &&
        (form_count == 0 || forms) &&
        cetta_gslt_language_execute_atoms(
            language, forms, (size_t)form_count, answers, limits(),
            &result, error, sizeof(error));
    CHECK(executed, label);
    if (executed) {
        CHECK(result.outcome == CETTA_GSLT_HORN_COMPLETED,
              "compiled-reader language execution completes semantically");
        CHECK(result.answer_count == expected_count,
              "compiled-reader boundary preserves bag cardinality");
        CHECK(all_answers_equal(&result, expected_forms[0]),
              "compiled-reader boundary preserves result payloads");
        cetta_gslt_language_result_free(&result);
    } else if (error[0]) {
        fprintf(stderr, "%s diagnostic: %s\n", label, error);
    }
    free(expected_forms);
    free(forms);
    free(ids);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s LANGUAGE_MANIFEST\n", argv[0]);
        return 2;
    }
    SymbolTable symbols;
    VarInternTable variable_names;
    Arena source;
    Arena compiled_source;
    Arena answers;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variable_names);
    g_var_intern = &variable_names;
    arena_init(&source);
    arena_init(&compiled_source);
    arena_set_runtime_kind(
        &compiled_source, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&answers);

    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &compiled_source);
    HECompiledReaderV1 *reader = he_compiled_reader_v1_new();
    char reader_error[ERROR_CAP] = {0};
    CHECK(reader && he_compiled_reader_v1_prepare(
                        reader, reader_error, sizeof(reader_error)),
          "compiled syntax capability prepares for language execution");

    CettaGsltLanguage *language = NULL;
    char error[ERROR_CAP] = {0};
    CHECK(cetta_gslt_language_load_manifest(
              argv[1], &language, error, sizeof(error)),
          "first-class GSLT language manifest loads");
    if (!language) {
        fprintf(stderr, "manifest diagnostic: %s\n", error);
    } else {
        CHECK(strcmp(cetta_gslt_language_name(language), "subzero") == 0,
              "language name comes from the manifest");
        CHECK(strcmp(cetta_gslt_language_syntax_backend(language),
                     "he-reader-direct-v1") == 0,
              "syntax capability comes from the manifest");
        CHECK(cetta_gslt_language_semantic_rule_count(language) == 36u,
              "semantic program composes core and public observation");

        run_document(
            language, &source, &answers,
            "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
            "duplicate source rules execute through the result bag");
        run_document(
            language, &source, &answers,
            "(= a b)\n(! unknown)\n", "unused", 0u,
            "unknown source form is inert");
        run_document(
            language, &source, &answers,
            "(= () empty-result)\n(! ())\n", "empty-result", 1u,
            "empty expression executes as ordinary data");
    }

    cetta_gslt_language_free(language);
    language = NULL;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_manifest(
              "tests/fixtures/absent-gslt-language-v1.metta",
              &language, error, sizeof(error)) && error[0] != '\0',
          "missing language manifest fails with a diagnostic");
    CettaGsltEmbeddedLanguageV1 invalid_descriptor =
        cetta_subzero_language_v1;
    invalid_descriptor.entry_arity = UINT32_MAX;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "overflowing embedded entry arity is rejected");

    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.name = "not-the-authored-name";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "differs from its authored manifest") != NULL,
          "descriptor fields cannot drift from the authored manifest");

    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.manifest.sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "manifest digest") != NULL,
          "tampered embedded manifest authority is rejected");

    CettaGsltRequestPipelineV1 drifted_pipeline =
        *cetta_zero_language_v1.request_pipeline;
    drifted_pipeline.classify_relation = "zero-produce";
    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.request_pipeline = &drifted_pipeline;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "differs from its authored manifest") != NULL,
          "pipeline selection cannot drift from the authored manifest");

    CettaGsltEmbeddedSourceV1 reordered_sources[3];
    memcpy(reordered_sources, cetta_zero_language_v1.semantic_sources,
           sizeof(reordered_sources));
    CettaGsltEmbeddedSourceV1 first_source = reordered_sources[0];
    reordered_sources[0] = reordered_sources[1];
    reordered_sources[1] = first_source;
    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.semantic_sources = reordered_sources;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "source order") != NULL,
          "semantic-source composition order remains manifest-authored");

    invalid_descriptor = cetta_zero_exp_language_v1;
    invalid_descriptor.profile_name = NULL;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "differs from its authored manifest") != NULL,
          "generated profile identity cannot be erased from its descriptor");

    invalid_descriptor = cetta_zero_exp_language_v1;
    invalid_descriptor.profile_name = "absent";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "selected profile") != NULL,
          "descriptor cannot select a profile absent from the manifest");

    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.compiled_plan.sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)) &&
              strstr(error, "digest") != NULL,
          "tampered compiled-plan authority is rejected");

    size_t malformed_length = cetta_subzero_language_v1.compiled_plan.length;
    uint8_t *malformed_plan = malloc(malformed_length);
    char malformed_digest[65];
    memcpy(malformed_plan, cetta_subzero_language_v1.compiled_plan.bytes,
           malformed_length);
    malformed_plan[20] = 0xffu;
    cetta_native_sha256_hex(
        malformed_plan, malformed_length, malformed_digest);
    invalid_descriptor = cetta_subzero_language_v1;
    invalid_descriptor.compiled_plan.bytes = malformed_plan;
    invalid_descriptor.compiled_plan.sha256 = malformed_digest;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded_for_realization(
              &invalid_descriptor,
              CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &language, error, sizeof(error)) &&
              strstr(error, "node table") != NULL,
          "digest-valid malformed compiled plan fails structural admission");
    free(malformed_plan);

    check_finite_variable_plan_admission(&cetta_subzero_language_v1);
    check_finite_variable_plan_admission(&cetta_zero_language_v1);
    check_finite_variable_plan_admission(&cetta_zero_exp_language_v1);
    uint64_t index_collisions =
        check_compiled_index_shape(&cetta_subzero_language_v1) +
        check_compiled_index_shape(&cetta_zero_language_v1) +
        check_compiled_index_shape(&cetta_zero_exp_language_v1);
    CHECK(index_collisions > 0u,
          "cross-guest indexes exercise collision resolution");

    size_t divergent_length = cetta_subzero_language_v1.compiled_plan.length;
    uint8_t *divergent_plan = malloc(divergent_length);
    char divergent_digest[65];
    memcpy(divergent_plan, cetta_subzero_language_v1.compiled_plan.bytes,
           divergent_length);
    uint8_t *changed_head = find_bytes(
        divergent_plan, divergent_length, "subzero-step");
    CHECK(changed_head != NULL,
          "semantic mutation locates a compiled relational head");
    if (changed_head) {
        *changed_head = (uint8_t)'x';
        cetta_native_sha256_hex(
            divergent_plan, divergent_length, divergent_digest);
        invalid_descriptor = cetta_subzero_language_v1;
        invalid_descriptor.compiled_plan.bytes = divergent_plan;
        invalid_descriptor.compiled_plan.sha256 = divergent_digest;
        memset(error, 0, sizeof(error));
        CHECK(!cetta_gslt_language_load_embedded_for_realization(
                  &invalid_descriptor,
                  CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
                  &language, error, sizeof(error)) &&
                  strstr(error, "differs from admitted source") != NULL,
              "digest-valid same-count semantic plan drift is rejected");
    }
    free(divergent_plan);

    invalid_descriptor = cetta_subzero_language_v1;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded_for_realization(
              &invalid_descriptor, (CettaGsltRealization)99,
              &language, error, sizeof(error)) && error[0] != '\0',
          "unknown generated realization fails at admission");

    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.entry_relation = "conflicting-entry";
    invalid_descriptor.entry_arity = 1u;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "descriptor cannot mix a single entry with a request pipeline");

    CettaGsltRequestPipelineV1 incomplete_pipeline =
        *cetta_zero_language_v1.request_pipeline;
    incomplete_pipeline.observe_relation = NULL;
    invalid_descriptor = cetta_zero_language_v1;
    invalid_descriptor.request_pipeline = &incomplete_pipeline;
    memset(error, 0, sizeof(error));
    CHECK(!cetta_gslt_language_load_embedded(
              &invalid_descriptor, &language, error, sizeof(error)),
          "incomplete request-pipeline ABI fails at admission");

    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_subzero_language_v1, &language, error, sizeof(error)),
          "digest-checked embedded GSLT language loads");
    if (!language) {
        fprintf(stderr, "embedded descriptor diagnostic: %s\n", error);
    } else {
        for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
             raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
            CettaGsltRealization realization = (CettaGsltRealization)raw;
            CHECK(cetta_gslt_realization_name(realization) != NULL,
                  "generated realization has a stable public name");
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
                "generated realization preserves duplicate rule occurrences");
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(! (wrap a))\n", "(wrap b)", 1u,
                "generated realization preserves contextual rewriting");
            run_document_realization(
                language, realization, &source, &answers,
                "(= ($x $x) same)\n(! (a a))\n", "same", 1u,
                "generated realization preserves repeated-variable matching");
            run_document_realization(
                language, realization, &source, &answers,
                "(= ($x $x) same)\n(! (a b))\n", "unused", 0u,
                "generated realization rejects inconsistent repeated variables");
            run_resource_limit(
                language, realization, &source, &answers,
                "(= a b)\n(= a b)\n(! a)\n",
                (CettaGsltHornLimits){
                    .max_rule_attempts = 1000000u,
                    .max_answers = 1u,
                    .max_depth = 10000u,
                },
                CETTA_GSLT_HORN_ANSWER_LIMIT,
                "generated realization fails closed at the answer limit");
            run_resource_limit(
                language, realization, &source, &answers,
                "(= a b)\n(! a)\n",
                (CettaGsltHornLimits){
                    .max_rule_attempts = 1u,
                    .max_answers = 1000u,
                    .max_depth = 10000u,
                },
                CETTA_GSLT_HORN_RULE_LIMIT,
                "generated realization fails closed at the rule limit");
        }
        if (reader) {
            run_compiled_document(
                language, reader, &universe, &answers,
                "(= a b)\n(= a b)\n(! a)\n", "b", 2u,
                "compiled reader composes with generated semantics");
        }
        check_compiled_optimizations_cross_guest(
            language, &source, &answers,
            "(= a b)\n(! a)\n",
            true, false, true, false,
            "Subzero exercises generic dispatch and flat-head matching");
    }
    cetta_gslt_language_free(language);
    language = NULL;
    check_compiled_query_optimizations_cross_guest(
        &cetta_subzero_language_v1,
        "(subzero-index-lt q-zero (q-succ q-zero))\n",
        true, false, false,
        "Subzero exercises generic ground dense matching");
    check_compiled_query_optimizations_cross_guest(
        &cetta_subzero_language_v1,
        "(subzero-match (q-sym name) (q-sym name) env env)\n",
        true, false, false,
        "Subzero exercises nonlinear repeated-slot ground matching");
    check_compiled_query_optimizations_cross_guest(
        &cetta_subzero_language_v1,
        "(subzero-match (q-sym name) (q-int 1) env env)\n",
        false, true, true,
        "Subzero exercises nested rigid incompatibility after dispatch");
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_zero_language_v1, &language, error, sizeof(error)),
          "query-first generated MeTTa Zero language loads");
    if (!language) {
        fprintf(stderr, "MeTTa Zero descriptor diagnostic: %s\n", error);
    } else {
        CHECK(strcmp(cetta_gslt_language_name(language), "zero") == 0,
              "request pipeline language identity comes from its descriptor");
        CHECK(cetta_gslt_language_semantic_rule_count(language) == 51u,
              "request pipeline composes matching, query, and observation");
        for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
             raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
            CettaGsltRealization realization = (CettaGsltRealization)raw;
            run_document_realization(
                language, realization, &source, &answers,
                "fact\n(zero-query fact hit)\n", "hit", 1u,
                "request pipeline publishes arbitrary reflective query");
            run_document_evidence_realization(
                language, realization, &source, &answers,
                "fact\n(zero-query fact hit)\n",
                "zero-observed-evidence", "zero-query-evidence",
                "public query retains its authored occurrence evidence");
            run_document_realization(
                language, realization, &source, &answers,
                "(= (f $x) (g $x))\n(eval (f a))\n", "(g a)", 1u,
                "request pipeline derives evaluation through public query");
            run_document_realization(
                language, realization, &source, &answers,
                "(eval unknown)\n", "unknown", 1u,
                "closed empty producer retains an inert subject");
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(! a)\n", "unused", 0u,
                "bang remains outside the admitted Zero request language");
            run_document_realization(
                language, realization, &source, &answers,
                "(eval native)\n", "native", 1u,
                "grounding declines when no library is composed");
            run_document_evidence_realization(
                language, realization, &source, &answers,
                "(eval native)\n", "zero-inert-evidence", NULL,
                "declined grounding retains explicit inert evidence");
            run_document_realization(
                language, realization, &source, &answers,
                "fact\n(zero-query absent hit)\n", "unused", 0u,
                "closed empty query remains an empty answer bag");
            run_resource_limit(
                language, realization, &source, &answers,
                "fact\nfact\n(zero-query fact hit)\n",
                (CettaGsltHornLimits){
                    .max_rule_attempts = 1000000u,
                    .max_answers = 1u,
                    .max_depth = 10000u,
                },
                CETTA_GSLT_HORN_ANSWER_LIMIT,
                "incomplete producer cannot publish its first occurrence");
            run_completion_boundary(
                language, realization, &source, &answers,
                "(eval unknown)\n",
                "completed empty producer reaches inert observation");
        }
        check_compiled_optimizations_cross_guest(
            language, &source, &answers,
            "(= (f $x) (g $x))\n(eval (f a))\n",
            true, true, false, false,
            "Zero exercises generic dispatch and recursive prefiltering");
    }
    cetta_gslt_language_free(language);
    language = NULL;
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_gslt_il_language_v1, &language,
              error, sizeof(error)),
          "generated GSLT-IL language loads for optimization checking");
    if (!language) {
        fprintf(stderr, "GSLT-IL descriptor diagnostic: %s\n", error);
    } else {
        check_compiled_optimizations_cross_guest(
            language, &source, &answers,
            "(in &stage-0 (= ready done))\n"
            "(in &stage-1 (= mapped-ready mapped-done))\n"
            "(route grow &stage-0 &stage-1)\n"
            "(= (grow ready) mapped-ready)\n"
            "(= (grow done) mapped-done)\n"
            "(! (grow ready))\n",
            true, false, true, false,
            "GSLT-IL exercises generic flat-head matching");
    }
    cetta_gslt_language_free(language);
    language = NULL;
    check_compiled_query_optimizations_cross_guest(
        &cetta_gslt_il_language_v1,
        "(gslt-il-program-atom "
        "(gslt-il-program-cons occurrence-0 (q-sym (q-str a)) "
        "(gslt-il-program-cons occurrence-1 (q-sym (q-str b)) "
        "gslt-il-program-nil)) occurrence-1 (q-sym (q-str b)))\n",
        true, false, false,
        "GSLT-IL exercises generic ground dense matching");
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded(
              &cetta_zero_exp_language_v1, &language,
              error, sizeof(error)),
          "authored experimental Zero runner profile loads");
    if (!language) {
        fprintf(stderr, "MeTTa Zero exp descriptor diagnostic: %s\n", error);
    } else {
        CHECK(cetta_gslt_language_semantic_rule_count(language) == 57u,
              "experimental profile adds exactly its authored runner rules");
        for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
             raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
            CettaGsltRealization realization = (CettaGsltRealization)raw;
            run_document_realization(
                language, realization, &source, &answers,
                "(= a b)\n(zero-step (zero-pending a zero-halt))\n",
                "(zero-pending b zero-halt)", 1u,
                "runner delegates a successful step to query-derived evaluation");
            run_document_realization(
                language, realization, &source, &answers,
                "(zero-step (zero-pending b zero-halt))\n",
                "(zero-completed b)", 1u,
                "runner distinguishes completed quiescence from pending work");
            run_document_realization(
                language, realization, &source, &answers,
                "(zero-step (zero-pending b "
                "(zero-then (wrap $x) zero-halt)))\n",
                "(zero-pending (wrap b) zero-halt)", 1u,
                "runner applies an authored semantic continuation");
        }
    }
    cetta_gslt_language_free(language);
    language = NULL;
    for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
         raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
        CettaGsltRealization realization = (CettaGsltRealization)raw;
        memset(error, 0, sizeof(error));
        CHECK(cetta_gslt_language_load_embedded_for_realization(
                  &cetta_zero_ground_library_v1, realization,
                  &language, error, sizeof(error)),
              "authored grounding library composes into a generated pack");
        if (language) {
            CHECK(cetta_gslt_language_semantic_rule_count(language) == 52u,
                  "library composition adds exactly its authored rule");
            run_document_realization(
                language, realization, &source, &answers,
                "(eval native)\n", "grounded", 1u,
                "grounding is supplied by the composed library relation");
            run_document_evidence_realization(
                language, realization, &source, &answers,
                "(eval native)\n",
                "zero-observed-evidence", "zero-ground-evidence",
                "library grounding retains its authored capability evidence");
        }
        cetta_gslt_language_free(language);
        language = NULL;
    }
    memset(error, 0, sizeof(error));
    CHECK(cetta_gslt_language_load_embedded_for_realization(
              &cetta_gslt_compiled_canary_v1,
              CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
              &language, error, sizeof(error)),
          "renamed non-Zero compiled canary loads generically");
    if (language) {
        run_document_realization(
            language, CETTA_GSLT_REALIZATION_COMPILED_WORKLIST,
            &source, &answers, "", "renamed-answer", 1u,
            "compiled executor runs a renamed independent language");
        Atom **no_forms = NULL;
        CettaGsltLanguageResult unavailable;
        memset(error, 0, sizeof(error));
        CHECK(!cetta_gslt_language_execute_atoms_with_realization(
              language, CETTA_GSLT_REALIZATION_HORN_REFERENCE,
                  no_forms, 0u, &answers, limits(), &unavailable,
                  error, sizeof(error)) &&
                  strstr(error, "not loaded") != NULL,
              "compiled-only load does not retain the reference program");
    }
    cetta_gslt_language_free(language);
    language = NULL;
    for (uint32_t raw = CETTA_GSLT_REALIZATION_HORN_REFERENCE;
         raw <= CETTA_GSLT_REALIZATION_COMPILED_WORKLIST; raw++) {
        CettaGsltRealization realization = (CettaGsltRealization)raw;
        memset(error, 0, sizeof(error));
        CHECK(cetta_gslt_language_load_embedded_for_realization(
                  &cetta_gslt_pipeline_canary_v1, realization,
                  &language, error, sizeof(error)),
              "renamed non-Zero request pipeline loads generically");
        if (language) {
            run_document_realization(
                language, realization, &source, &answers,
                "(canary-run)\n", "renamed-pipeline-answer", 1u,
                "request pipeline executes without language vocabulary dispatch");
        }
        cetta_gslt_language_free(language);
        language = NULL;
    }
    he_compiled_reader_v1_free(reader);
    term_universe_free(&universe);
    arena_free(&answers);
    arena_free(&compiled_source);
    arena_free(&source);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&variable_names);
    symbol_table_free(&symbols);
    if (failures != 0u) {
        fprintf(stderr, "GsltLanguageRuntimeSummary checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("(GsltLanguageRuntimeSummary checks=%u failures=0)\n", checks);
    return 0;
}
