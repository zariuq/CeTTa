#define _POSIX_C_SOURCE 200809L

#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_cursor_c_emitter_v1.h"
#include "parser_pack_guard_evidence_stream_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"

#include "symbol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool read_input(const char *path,
                       uint8_t **out,
                       size_t *out_len,
                       char *error,
                       size_t error_size) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input) {
        (void)snprintf(error, error_size, "cannot open lexical input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size,
                               "lexical input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "failed to allocate lexical input");
                goto done;
            }
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(input)) {
            (void)snprintf(error, error_size,
                           "failed while reading lexical input");
            goto done;
        }
        break;
    }
    *out = bytes;
    *out_len = len;
    bytes = NULL;
    ok = true;

done:
    if (input)
        (void)fclose(input);
    free(bytes);
    return ok;
}

static const char *outcome_name(PPNativeV1Outcome outcome) {
    switch (outcome) {
    case PPNATIVE_V1_COMPLETED:
        return "completed";
    case PPNATIVE_V1_UNSUPPORTED_OPEN_PACK:
        return "unsupported-open-pack";
    case PPNATIVE_V1_RECOGNIZER_LIMIT:
        return "recognizer-limit";
    case PPNATIVE_V1_REPLAY_DEPTH:
        return "replay-depth";
    case PPNATIVE_V1_RESULT_LIMIT:
        return "result-limit";
    }
    return "unknown";
}

static const char *slr_outcome_name(
    CettaLpNativeSlrLatticeOutcome outcome) {
    switch (outcome) {
    case CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED:
        return "accepted";
    case CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL:
        return "needs-general";
    case CETTA_LP_NATIVE_SLR_LATTICE_RESOURCE_LIMIT:
        return "resource-limit";
    }
    return "unknown";
}

static const char *cursor_outcome_name(
    PPGuardedLexCursorV1Outcome outcome) {
    switch (outcome) {
    case PPGUARDED_LEX_CURSOR_V1_ACCEPTED:
        return "accepted";
    case PPGUARDED_LEX_CURSOR_V1_REJECTED:
        return "rejected";
    case PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT:
        return "work-limit";
    }
    return "unknown";
}

static bool cursor_receipts_equal(
    const PPGuardedLexCursorV1Receipt *left,
    const PPGuardedLexCursorV1Receipt *right) {
    return left->outcome == right->outcome &&
        left->input_byte_len == right->input_byte_len &&
        left->input_scalar_len == right->input_scalar_len &&
        left->farthest_scalar == right->farthest_scalar &&
        left->farthest_byte == right->farthest_byte &&
        left->token_len == right->token_len &&
        left->scalar_token_len == right->scalar_token_len &&
        left->ordinary_span_token_len == right->ordinary_span_token_len &&
        left->guarded_span_token_len == right->guarded_span_token_len &&
        left->shift_len == right->shift_len &&
        left->reduce_len == right->reduce_len &&
        left->cursor_scan_len == right->cursor_scan_len &&
        left->max_stack_len == right->max_stack_len &&
        left->source_pass_count == right->source_pass_count &&
        left->dfa_work_item_len == right->dfa_work_item_len &&
        left->parser_work_item_len == right->parser_work_item_len &&
        strcmp(left->trace_digest, right->trace_digest) == 0;
}

static bool write_atom_field(const char *field, const Atom *value) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    int written;

    if (!field || !value ||
        !fh_ground_term_v1_render(
            value, &canonical, &canonical_len, NULL, 0u)) {
        free(canonical);
        return false;
    }
    written = printf(
        "%s\t%.*s\n", field, (int)canonical_len,
        (const char *)canonical);
    free(canonical);
    return written >= 0;
}

static bool write_results(const char *field,
                          const PPNativeV1Result *result) {
    uint32_t index;

    for (index = 0u; index < result->semantic_result_len; index++) {
        if (!write_atom_field(field, result->semantic_results[index]))
            return false;
    }
    return true;
}

static Atom *cursor_action_slot_value(Arena *arena,
                                      uint32_t production_id,
                                      uint32_t slot_id) {
    Atom **items = arena_alloc(arena, sizeof(*items) * 3u);
    items[0] = atom_symbol(arena, "cursor-slot-value");
    items[1] = atom_int(arena, (int64_t)production_id);
    items[2] = atom_int(arena, (int64_t)slot_id);
    return atom_expr(arena, items, 3u);
}

static Atom *cursor_source_action(const PPABIV1Pack *pack,
                                  const PPGuardPlanV1 *guard_plan,
                                  uint32_t production_id) {
    if (production_id < pack->production_len)
        return pack->productions[production_id].action;
    production_id -= pack->production_len;
    if (production_id >= guard_plan->production_len)
        return NULL;
    return guard_plan->productions[production_id].action;
}

typedef struct {
    uint64_t source;
    uint64_t token_projection;
    uint64_t lattices;
    uint64_t gll_witnesses;
    uint64_t glr_witnesses;
    uint64_t gll_relations;
    uint64_t glr_relations;
    uint64_t slr_result;
    uint64_t gll_result;
    uint64_t glr_result;
    uint64_t total;
} RetainedPayload;

static uint64_t retained_array(
    const void *data, uint64_t len, size_t item_size) {
    return data ? len * (uint64_t)item_size : 0u;
}

static uint64_t retained_scan(const RSDFAV1ScanResult *scan) {
    return retained_array(
               scan->tokens, scan->token_len, sizeof(*scan->tokens)) +
        retained_array(
               scan->codepoints, scan->input_scalar_len,
               sizeof(*scan->codepoints)) +
        retained_array(
               scan->byte_offsets,
               (uint64_t)scan->input_scalar_len + 1u,
               sizeof(*scan->byte_offsets));
}

static uint64_t retained_lattice(
    const RSDFAV1ParserLattice *lattice) {
    const CettaLpNativeUtf8Lattice *view = &lattice->lattice;
    return retained_array(
               lattice->terminal_ids, view->terminal_len,
               sizeof(*lattice->terminal_ids)) +
        retained_array(
               lattice->edges, view->edge_len,
               sizeof(*lattice->edges)) +
        retained_array(
               lattice->start_offsets, view->start_offset_len,
               sizeof(*lattice->start_offsets)) +
        retained_array(
               lattice->owned_byte_offsets,
               (uint64_t)view->scalar_len + 1u,
               sizeof(*lattice->owned_byte_offsets));
}

static uint64_t retained_lexical_witness(
    const PPLexV1WitnessTable *witness) {
    uint64_t value_len = witness->len ? witness->len : 1u;
    return (uint64_t)witness->arena.reserved_bytes +
        retained_array(
            witness->values, value_len, sizeof(*witness->values));
}

static uint64_t retained_guarded_witness(
    const PPGuardedLexWitnessV1 *witness) {
    uint64_t value_len = witness->len ? witness->len : 1u;
    return (uint64_t)witness->arena.reserved_bytes +
        retained_array(
            witness->values, value_len, sizeof(*witness->values));
}

static uint64_t retained_relation(
    const PPGuardRelationV1 *relation) {
    const CettaLpNativeUtf8Lattice *lattice = &relation->lattice;
    return (uint64_t)relation->arena.reserved_bytes +
        retained_array(
            relation->terminal_ids, lattice->terminal_len,
            sizeof(*relation->terminal_ids)) +
        retained_array(
            relation->edges, lattice->edge_len,
            sizeof(*relation->edges)) +
        retained_array(
            relation->start_offsets, lattice->start_offset_len,
            sizeof(*relation->start_offsets)) +
        retained_array(
            relation->codepoints, lattice->scalar_len,
            sizeof(*relation->codepoints)) +
        retained_array(
            relation->byte_offsets,
            (uint64_t)lattice->scalar_len + 1u,
            sizeof(*relation->byte_offsets)) +
        retained_array(
            relation->witness_values, relation->witness_len,
            sizeof(*relation->witness_values)) +
        retained_array(
            relation->evidence, relation->evidence_len,
            sizeof(*relation->evidence)) +
        retained_array(
            relation->borrowed_atom_owners,
            relation->borrowed_atom_owner_len,
            sizeof(*relation->borrowed_atom_owners));
}

static uint64_t retained_native_result(const PPNativeV1Result *result) {
    const CettaLpNativeUtf8Forest *forest = &result->forest;
    return (uint64_t)result->arena.reserved_bytes +
        retained_array(
            forest->nodes, forest->node_len, sizeof(*forest->nodes)) +
        retained_array(
            forest->choices, forest->choice_len,
            sizeof(*forest->choices)) +
        retained_array(
            forest->roots, forest->root_len, sizeof(*forest->roots)) +
        retained_array(
            forest->expected_terminal_ids, forest->expected_terminal_len,
            sizeof(*forest->expected_terminal_ids)) +
        retained_array(
            forest->codepoints, forest->scalar_len,
            sizeof(*forest->codepoints)) +
        retained_array(
            forest->byte_offsets,
            (uint64_t)forest->scalar_len + 1u,
            sizeof(*forest->byte_offsets)) +
        retained_array(
            result->semantic_results, result->semantic_result_len,
            sizeof(*result->semantic_results));
}

static RetainedPayload retained_payload(
    const PPGuardedLexExecV1Result *result) {
    RetainedPayload retained = {0};

    retained.source = retained_scan(&result->scan);
    retained.token_projection =
        retained_array(
            result->ordinary_tokens, result->ordinary_token_len,
            sizeof(*result->ordinary_tokens)) +
        retained_array(
            result->guard_tokens, result->guard_token_len,
            sizeof(*result->guard_tokens)) +
        retained_array(
            result->guarded_tokens, result->guarded_candidate_len,
            sizeof(*result->guarded_tokens)) +
        retained_array(
            result->projected_tokens,
            (uint64_t)result->ordinary_token_len +
                result->guarded_candidate_len,
            sizeof(*result->projected_tokens));
    retained.lattices =
        retained_lattice(&result->ordinary_lattice) +
        retained_lattice(&result->projected_lattice);
    retained.gll_witnesses =
        retained_lexical_witness(&result->ordinary_gll_witness) +
        retained_guarded_witness(&result->guarded_gll_witness) +
        retained_array(
            result->gll_witness_values,
            result->witness_len ? result->witness_len : 1u,
            sizeof(*result->gll_witness_values));
    retained.glr_witnesses =
        retained_lexical_witness(&result->ordinary_glr_witness) +
        retained_guarded_witness(&result->guarded_glr_witness) +
        retained_array(
            result->glr_witness_values,
            result->witness_len ? result->witness_len : 1u,
            sizeof(*result->glr_witness_values));
    retained.gll_relations =
        retained_relation(&result->gll_relation) +
        retained_relation(&result->gll_projected_relation);
    retained.glr_relations =
        retained_relation(&result->glr_relation) +
        retained_relation(&result->glr_projected_relation);
    retained.slr_result = retained_native_result(&result->slr);
    retained.gll_result = retained_native_result(&result->gll);
    retained.glr_result = retained_native_result(&result->glr);
    retained.total = retained.source + retained.token_projection +
        retained.lattices + retained.gll_witnesses +
        retained.glr_witnesses + retained.gll_relations +
        retained.glr_relations + retained.slr_result +
        retained.gll_result + retained.glr_result;
    return retained;
}

static void write_retained_payload(
    const char *prefix, const RetainedPayload *retained) {
    printf("%s-source-bytes\t%llu\n", prefix,
           (unsigned long long)retained->source);
    printf("%s-token-projection-bytes\t%llu\n", prefix,
           (unsigned long long)retained->token_projection);
    printf("%s-lattice-bytes\t%llu\n", prefix,
           (unsigned long long)retained->lattices);
    printf("%s-gll-witness-bytes\t%llu\n", prefix,
           (unsigned long long)retained->gll_witnesses);
    printf("%s-glr-witness-bytes\t%llu\n", prefix,
           (unsigned long long)retained->glr_witnesses);
    printf("%s-gll-relation-bytes\t%llu\n", prefix,
           (unsigned long long)retained->gll_relations);
    printf("%s-glr-relation-bytes\t%llu\n", prefix,
           (unsigned long long)retained->glr_relations);
    printf("%s-slr-result-bytes\t%llu\n", prefix,
           (unsigned long long)retained->slr_result);
    printf("%s-gll-result-bytes\t%llu\n", prefix,
           (unsigned long long)retained->gll_result);
    printf("%s-glr-result-bytes\t%llu\n", prefix,
           (unsigned long long)retained->glr_result);
    printf("%s-total-bytes\t%llu\n", prefix,
           (unsigned long long)retained->total);
}

static bool results_equal(const PPNativeV1Result *left,
                          const PPNativeV1Result *right) {
    uint32_t index;

    if (left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

typedef enum {
    FINAL_FOREST_BENCH_SLR = 0,
    FINAL_FOREST_BENCH_GLL = 1,
    FINAL_FOREST_BENCH_GLR = 2
} FinalForestBenchBackend;

typedef struct {
    uint64_t nanoseconds;
    uint32_t work_item_len;
    uint32_t graph_node_len;
    uint32_t stack_node_len;
    uint32_t forest_node_len;
    uint32_t forest_choice_len;
    uint32_t forest_root_len;
} FinalForestBenchReceipt;

typedef struct {
    uint64_t nanoseconds;
    uint64_t value_parser_work_item_len;
    uint64_t prepared_fallback_work_item_len;
    uint32_t value_program_run_len;
    uint32_t prepared_fallback_run_len;
    uint32_t token_len;
    uint32_t source_pass_count;
} CursorHybridBenchReceipt;

static bool monotonic_nanoseconds(uint64_t *out) {
    struct timespec now;
    uint64_t seconds;

    if (!out || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
        now.tv_sec < 0 || now.tv_nsec < 0 ||
        now.tv_nsec >= 1000000000L) {
        return false;
    }
    seconds = (uint64_t)now.tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)now.tv_nsec) /
                      UINT64_C(1000000000)) {
        return false;
    }
    *out = seconds * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return true;
}

static bool forest_receipt_equal(
    const CettaLpNativeUtf8Forest *left,
    const CettaLpNativeUtf8Forest *right) {
    return left && right &&
        left->outcome == right->outcome &&
        left->node_len == right->node_len &&
        left->choice_len == right->choice_len &&
        left->root_len == right->root_len &&
        left->scalar_len == right->scalar_len &&
        left->input_byte_len == right->input_byte_len &&
        left->farthest_scalar == right->farthest_scalar &&
        left->farthest_byte == right->farthest_byte &&
        left->graph_node_len == right->graph_node_len &&
        left->stack_node_len == right->stack_node_len &&
        left->work_item_len == right->work_item_len &&
        left->source_pass_count == right->source_pass_count;
}

static bool benchmark_final_forest(
    FinalForestBenchBackend backend,
    const PPGuardedLexExecV1Plan *exec_plan,
    const PPGuardedLexExecV1Result *result,
    const PPGuardedLexExecV1Limits *limits,
    uint32_t iterations,
    FinalForestBenchReceipt *out,
    char *error,
    size_t error_size) {
    const CettaLpNativeUtf8Lattice *lattice;
    const CettaLpNativeUtf8Forest *expected;
    FinalForestBenchReceipt receipt = {0};
    uint64_t started;
    uint64_t finished;
    uint32_t iteration;

    if (!exec_plan || !result || !limits || !out || iterations == 0u) {
        (void)snprintf(error, error_size,
                       "bad final-forest benchmark arguments");
        return false;
    }
    lattice = backend == FINAL_FOREST_BENCH_GLR
        ? &result->glr_projected_relation.lattice
        : &result->gll_projected_relation.lattice;
    expected = backend == FINAL_FOREST_BENCH_SLR
        ? &result->slr.forest
        : (backend == FINAL_FOREST_BENCH_GLL
           ? &result->gll.forest : &result->glr.forest);
    if (backend == FINAL_FOREST_BENCH_SLR &&
        result->receipt.slr_outcome !=
            CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED) {
        (void)snprintf(error, error_size,
                       "SLR benchmark requires an accepted shadow input");
        return false;
    }
    if (!monotonic_nanoseconds(&started)) {
        (void)snprintf(error, error_size,
                       "failed to read benchmark start time");
        return false;
    }
    for (iteration = 0u; iteration < iterations; iteration++) {
        CettaLpNativeUtf8Forest forest;
        CettaLpNativeSlrLatticeOutcome slr_outcome =
            CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
        bool parsed;

        cetta_lp_native_utf8_forest_init(&forest);
        if (error && error_size > 0u)
            error[0] = '\0';
        if (backend == FINAL_FOREST_BENCH_SLR) {
            parsed = cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
                &exec_plan->final_slr, lattice, 0u,
                limits->parse_work_limit, &slr_outcome, &forest,
                error, error_size);
            parsed = parsed &&
                slr_outcome == CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED;
        } else if (backend == FINAL_FOREST_BENCH_GLL) {
            parsed =
                cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
                    &exec_plan->final_gll, lattice, 0u,
                    limits->parse_work_limit, &forest, error, error_size);
        } else {
            parsed = cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
                &exec_plan->final_glr, lattice, 0u,
                limits->parse_work_limit, &forest, error, error_size);
        }
        if (!parsed || !forest_receipt_equal(&forest, expected)) {
            if (error && error_size > 0u && error[0] == '\0') {
                (void)snprintf(
                    error, error_size,
                    "final-forest benchmark replay changed its receipt");
            }
            cetta_lp_native_utf8_forest_free(&forest);
            return false;
        }
        cetta_lp_native_utf8_forest_free(&forest);
    }
    if (!monotonic_nanoseconds(&finished) || finished < started) {
        (void)snprintf(error, error_size,
                       "failed to read benchmark finish time");
        return false;
    }
    receipt.nanoseconds = finished - started;
    receipt.work_item_len = expected->work_item_len;
    receipt.graph_node_len = expected->graph_node_len;
    receipt.stack_node_len = expected->stack_node_len;
    receipt.forest_node_len = expected->node_len;
    receipt.forest_choice_len = expected->choice_len;
    receipt.forest_root_len = expected->root_len;
    *out = receipt;
    return true;
}

static void write_final_forest_benchmark(
    const char *backend,
    const FinalForestBenchReceipt *receipt) {
    printf("benchmark-%s-nanoseconds\t%llu\n", backend,
           (unsigned long long)receipt->nanoseconds);
    printf("benchmark-%s-work-items\t%u\n", backend,
           receipt->work_item_len);
    printf("benchmark-%s-graph-nodes\t%u\n", backend,
           receipt->graph_node_len);
    printf("benchmark-%s-stack-nodes\t%u\n", backend,
           receipt->stack_node_len);
    printf("benchmark-%s-forest-nodes\t%u\n", backend,
           receipt->forest_node_len);
    printf("benchmark-%s-forest-choices\t%u\n", backend,
           receipt->forest_choice_len);
    printf("benchmark-%s-forest-roots\t%u\n", backend,
           receipt->forest_root_len);
}

static bool benchmark_cursor_hybrid_bytes(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input,
    size_t input_len,
    const PPGuardedLexCursorV1SemanticResult *expected,
    const PPGuardedLexExecV1Limits *limits,
    uint32_t iterations,
    CursorHybridBenchReceipt *out,
    char *error,
    size_t error_size) {
    PPGuardedLexCursorV1Receipt expected_receipt;
    CursorHybridBenchReceipt receipt = {0};
    uint64_t started;
    uint64_t finished;
    uint32_t iteration;

    if (!program || !pack || !start_state || !lexical_plan || !guard_plan ||
        !guarded_plan || !exec_plan || !expected || !limits || !out ||
        iterations == 0u) {
        (void)snprintf(
            error, error_size, "bad hybrid cursor benchmark arguments");
        return false;
    }
    expected_receipt = expected->receipt;
    expected_receipt.source_pass_count = 1u;
    if (!monotonic_nanoseconds(&started)) {
        (void)snprintf(
            error, error_size,
            "failed to read hybrid cursor benchmark start time");
        return false;
    }
    for (iteration = 0u; iteration < iterations; iteration++) {
        PPGuardedLexCursorV1SemanticResult result;
        bool semantic_equal;

        ppguarded_lex_cursor_v1_semantic_result_init(&result);
        if (error && error_size > 0u)
            error[0] = '\0';
        if (!ppguarded_lex_cursor_v1_program_run_semantic_hybrid_bytes(
                program, pack, start_state, lexical_plan, guard_plan,
                guarded_plan, exec_plan, input, input_len,
                PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL,
                limits->replay_depth, limits->result_limit,
                limits->parse_work_limit, &result, error, error_size)) {
            ppguarded_lex_cursor_v1_semantic_result_free(&result);
            return false;
        }
        semantic_equal =
            (!expected->semantic_result && !result.semantic_result) ||
            (expected->semantic_result && result.semantic_result &&
             atom_eq(expected->semantic_result, result.semantic_result));
        if (!semantic_equal ||
            !cursor_receipts_equal(&expected_receipt, &result.receipt) ||
            result.terminal_value_len != expected->terminal_value_len ||
            result.action_execution_len != expected->action_execution_len ||
            result.action_instruction_len !=
                expected->action_instruction_len ||
            result.value_program_run_len !=
                expected->value_program_run_len ||
            result.value_parser_work_item_len !=
                expected->value_parser_work_item_len ||
            result.prepared_fallback_run_len !=
                expected->prepared_fallback_run_len ||
            result.prepared_fallback_work_item_len !=
                expected->prepared_fallback_work_item_len) {
            if (error && error_size > 0u && error[0] == '\0') {
                (void)snprintf(
                    error, error_size,
                    "hybrid cursor benchmark replay changed its receipt");
            }
            ppguarded_lex_cursor_v1_semantic_result_free(&result);
            return false;
        }
        ppguarded_lex_cursor_v1_semantic_result_free(&result);
    }
    if (!monotonic_nanoseconds(&finished) || finished < started) {
        (void)snprintf(
            error, error_size,
            "failed to read hybrid cursor benchmark finish time");
        return false;
    }
    receipt.nanoseconds = finished - started;
    receipt.value_parser_work_item_len =
        expected->value_parser_work_item_len;
    receipt.prepared_fallback_work_item_len =
        expected->prepared_fallback_work_item_len;
    receipt.value_program_run_len = expected->value_program_run_len;
    receipt.prepared_fallback_run_len =
        expected->prepared_fallback_run_len;
    receipt.token_len = expected->receipt.token_len;
    receipt.source_pass_count = 1u;
    *out = receipt;
    return true;
}

static void write_cursor_hybrid_benchmark(
    const CursorHybridBenchReceipt *receipt) {
    printf("benchmark-cursor-hybrid-bytes-nanoseconds\t%llu\n",
           (unsigned long long)receipt->nanoseconds);
    printf("benchmark-cursor-hybrid-value-parser-work\t%llu\n",
           (unsigned long long)receipt->value_parser_work_item_len);
    printf("benchmark-cursor-hybrid-prepared-fallback-work\t%llu\n",
           (unsigned long long)receipt->prepared_fallback_work_item_len);
    printf("benchmark-cursor-hybrid-value-program-runs\t%u\n",
           receipt->value_program_run_len);
    printf("benchmark-cursor-hybrid-prepared-fallback-runs\t%u\n",
           receipt->prepared_fallback_run_len);
    printf("benchmark-cursor-hybrid-tokens\t%u\n", receipt->token_len);
    printf("benchmark-cursor-hybrid-source-passes\t%u\n",
           receipt->source_pass_count);
}

static bool run_cursor_hybrid_only(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input,
    size_t input_len,
    const PPGuardedLexExecV1Limits *limits,
    uint32_t benchmark_iterations,
    char *error,
    size_t error_size) {
    PPGuardedLexCursorV1SemanticResult result;
    CursorHybridBenchReceipt benchmark = {0};
    bool ok = false;

    ppguarded_lex_cursor_v1_semantic_result_init(&result);
    if (!program || !pack || !start_state || !lexical_plan || !guard_plan ||
        !guarded_plan || !exec_plan || !limits || !program->actions_bound ||
        benchmark_iterations == 0u ||
        !ppguarded_lex_cursor_v1_program_run_semantic_hybrid_bytes(
            program, pack, start_state, lexical_plan, guard_plan,
            guarded_plan, exec_plan, input, input_len,
            PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL,
            limits->replay_depth, limits->result_limit,
            limits->parse_work_limit, &result, error, error_size) ||
        !benchmark_cursor_hybrid_bytes(
            program, pack, start_state, lexical_plan, guard_plan,
            guarded_plan, exec_plan, input, input_len, &result, limits,
            benchmark_iterations, &benchmark, error, error_size)) {
        if (error && error_size > 0u && error[0] == '\0') {
            (void)snprintf(
                error, error_size,
                "failed to execute cursor-only hybrid benchmark");
        }
        goto done;
    }
    printf("parser-pack-guarded-lexical-cursor-hybrid-v1\n");
    printf("base-pack-digest\t%s\n", pack->pack_digest);
    printf("lexical-plan-digest\t%s\n", lexical_plan->plan_digest);
    printf("execution-plan-digest\t%s\n",
           exec_plan->execution_plan_digest);
    printf("cursor-program-digest\t%s\n", program->program_digest);
    printf("outcome\t%s\n", cursor_outcome_name(result.receipt.outcome));
    printf("input-bytes\t%u\n", result.receipt.input_byte_len);
    printf("input-scalars\t%u\n", result.receipt.input_scalar_len);
    printf("source-passes\t%u\n", result.receipt.source_pass_count);
    printf("tokens\t%u\n", result.receipt.token_len);
    printf("cursor-scans\t%u\n", result.receipt.cursor_scan_len);
    printf("dfa-work\t%llu\n",
           (unsigned long long)result.receipt.dfa_work_item_len);
    printf("parser-work\t%llu\n",
           (unsigned long long)result.receipt.parser_work_item_len);
    printf("value-program-runs\t%u\n", result.value_program_run_len);
    printf("value-parser-work\t%llu\n",
           (unsigned long long)result.value_parser_work_item_len);
    printf("prepared-fallback-runs\t%u\n",
           result.prepared_fallback_run_len);
    printf("prepared-fallback-work\t%llu\n",
           (unsigned long long)result.prepared_fallback_work_item_len);
    printf("trace-digest\t%s\n", result.receipt.trace_digest);
    printf("benchmark-kind\tcursor-hybrid-bytes\n");
    printf("benchmark-iterations\t%u\n", benchmark_iterations);
    write_cursor_hybrid_benchmark(&benchmark);
    if (result.semantic_result &&
        !write_atom_field("semantic-result", result.semantic_result)) {
        (void)snprintf(
            error, error_size,
            "failed to write cursor-only semantic result");
        goto done;
    }
    printf("end\n");
    ok = true;

done:
    ppguarded_lex_cursor_v1_semantic_result_free(&result);
    return ok;
}

static bool run(const char *abi_path,
                const char *lexical_nfa_path,
                const char *guard_nfa_path,
                const char *guard_evidence_path,
                const char *guarded_nfa_path,
                const char *input_path,
                const char *regular_compiler_digest,
                const char *guarded_compiler_digest,
                const char *action_answer_path,
                const char *action_compiler_digest,
                uint32_t benchmark_iterations,
                bool cursor_hybrid_only,
                const char *emit_c_path,
                const char *emit_c_prefix) {
    const PPGuardedLexExecV1Limits limits = {
        .dfa_state_limit = UINT32_C(65536),
        .dfa_transition_limit = UINT32_C(2000000),
        .scan_work_limit = UINT64_C(20000000),
        .scan_token_limit = UINT32_C(2000000),
        .witness_work_limit = UINT32_C(10000000),
        .parse_work_limit = UINT32_C(50000000),
        .replay_depth = UINT32_C(4096),
        .result_limit = UINT32_C(1000000),
    };
    PPABIV1Wire pack_wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 lexical_answers;
    FHAnswerStreamV1 guard_answers;
    FHAnswerStreamV1 guarded_answers;
    FHAnswerStreamV1 action_answers;
    RSNFAV1Plan lexical_nfa;
    RSNFAV1Plan guard_nfa;
    PPLexV1Plan lexical_plan;
    PPGuardEvidenceWireV1 evidence;
    PPGuardPlanV1 guard_plan;
    PPGuardPlanV1ProvenanceInput provenance;
    PPGuardedLexV1Plan guarded_plan;
    PPGuardedLexExecV1Plan exec_plan;
    PPGuardedLexCursorV1Program cursor_program;
    PPGuardedLexCursorV1Receipt cursor_receipt;
    PPGuardedLexCursorV1SemanticResult cursor_gll_semantic;
    PPGuardedLexCursorV1SemanticResult cursor_glr_semantic;
    PPGuardedLexCursorV1SemanticResult cursor_direct_semantic;
    PPGuardedLexCursorV1SemanticResult cursor_hybrid_gll_semantic;
    PPGuardedLexCursorV1SemanticResult cursor_hybrid_glr_semantic;
    PPGuardedLexCursorV1SemanticResult cursor_hybrid_bytes_semantic;
    PPGuardedLexCursorV1SemanticResult cursor_hybrid_mutation_semantic;
    PPGuardedLexExecV1Result result;
    PPGuardedLexExecV1Result derived_result;
    CettaLpNativeUtf8ScalarView derived_view;
    Arena action_slot_arena;
    Arena action_tree_arena;
    Arena action_bytecode_arena;
    FinalForestBenchReceipt slr_benchmark = {0};
    FinalForestBenchReceipt gll_benchmark = {0};
    FinalForestBenchReceipt glr_benchmark = {0};
    CursorHybridBenchReceipt cursor_hybrid_benchmark = {0};
    RetainedPayload retained = {0};
    RetainedPayload derived_retained = {0};
    uint32_t cursor_scalar_sources = 0u;
    uint32_t cursor_ordinary_sources = 0u;
    uint32_t cursor_guarded_sources = 0u;
    uint32_t cursor_program_mutations = 0u;
    uint32_t cursor_action_agreements = 0u;
    uint32_t cursor_action_mutations = 0u;
    uint32_t cursor_semantic_mutations = 0u;
    uint32_t cursor_direct_semantic_agreement = 0u;
    uint32_t cursor_hybrid_semantic_agreement = 0u;
    uint32_t cursor_hybrid_bytes_agreement = 0u;
    uint32_t cursor_hybrid_mutations = 0u;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    char error[512] = {0};
    bool cursor_program_built = false;
    bool ok = false;

    ppabi_v1_wire_init(&pack_wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&lexical_answers);
    fh_answer_stream_v1_init(&guard_answers);
    fh_answer_stream_v1_init(&guarded_answers);
    fh_answer_stream_v1_init(&action_answers);
    rsnfa_v1_plan_init(&lexical_nfa);
    rsnfa_v1_plan_init(&guard_nfa);
    pplex_v1_plan_init(&lexical_plan);
    ppguard_evidence_wire_v1_init(&evidence);
    ppguard_plan_v1_init(&guard_plan);
    ppguarded_lex_v1_plan_init(&guarded_plan);
    ppguarded_lex_exec_v1_plan_init(&exec_plan);
    ppguarded_lex_cursor_v1_program_init(&cursor_program);
    ppguarded_lex_cursor_v1_semantic_result_init(&cursor_gll_semantic);
    ppguarded_lex_cursor_v1_semantic_result_init(&cursor_glr_semantic);
    ppguarded_lex_cursor_v1_semantic_result_init(&cursor_direct_semantic);
    ppguarded_lex_cursor_v1_semantic_result_init(
        &cursor_hybrid_gll_semantic);
    ppguarded_lex_cursor_v1_semantic_result_init(
        &cursor_hybrid_glr_semantic);
    ppguarded_lex_cursor_v1_semantic_result_init(
        &cursor_hybrid_bytes_semantic);
    ppguarded_lex_cursor_v1_semantic_result_init(
        &cursor_hybrid_mutation_semantic);
    ppguarded_lex_exec_v1_result_init(&result);
    ppguarded_lex_exec_v1_result_init(&derived_result);
    arena_init(&action_slot_arena);
    arena_init(&action_tree_arena);
    arena_init(&action_bytecode_arena);
    memset(&derived_view, 0, sizeof(derived_view));
    memset(&cursor_receipt, 0, sizeof(cursor_receipt));
    if (!ppabi_v1_wire_read(
            &pack_wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &pack_wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &lexical_answers, lexical_nfa_path,
            error, sizeof(error)) ||
        lexical_answers.len == 0u ||
        lexical_answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &pack, lexical_answers.terms, lexical_answers.len,
            &lexical_nfa, error, sizeof(error)) ||
        !pplex_v1_plan_build(
            &pack, lexical_nfa.tags, lexical_nfa.nfa.tag_len,
            regular_compiler_digest, lexical_answers.digest,
            &lexical_plan, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &guard_answers, guard_nfa_path,
            error, sizeof(error)) ||
        guard_answers.len == 0u ||
        guard_answers.len > UINT32_MAX ||
        !rsnfa_v1_plan_load(
            &pack, guard_answers.terms, guard_answers.len,
            &guard_nfa, error, sizeof(error)) ||
        !ppguard_evidence_wire_v1_read(
            &evidence, guard_evidence_path,
            error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &guarded_answers, guarded_nfa_path,
            error, sizeof(error)) ||
        guarded_answers.len == 0u ||
        !read_input(input_path, &input, &input_len,
                    error, sizeof(error))) {
        goto rejected;
    }
    provenance = (PPGuardPlanV1ProvenanceInput){
        .source_digest = evidence.source_digest,
        .pre_reflection_digest = evidence.pre_reflection_digest,
        .environment_digest = evidence.environment_digest,
        .answer_set_digest = evidence.answer_set_digest,
        .regular_compiler_digest = regular_compiler_digest,
        .guard_nfa_answer_digest = guard_answers.digest,
        .guard_nfa_tags = guard_nfa.tags,
        .guard_nfa_tag_len = guard_nfa.nfa.tag_len,
        .derivations = evidence.derivations,
        .derivation_len = evidence.derivation_len,
    };
    if (!ppguard_plan_v1_build(
            &pack, &lexical_plan, &provenance,
            &guard_plan, error, sizeof(error)) ||
        !ppguarded_lex_v1_plan_build(
            &pack, &lexical_plan, &guard_plan,
            guarded_answers.terms, guarded_answers.len,
            guarded_compiler_digest, guarded_answers.digest,
            &guarded_plan, error, sizeof(error)) ||
        !ppguarded_lex_exec_v1_plan_build(
            &pack, pack_wire.start, &lexical_plan,
            &guard_plan, &guarded_plan,
            &lexical_nfa, &guard_nfa, &limits, &exec_plan,
            error, sizeof(error))) {
        goto rejected;
    }
    if (exec_plan.cursor_certificate.eligible) {
        if (!ppguarded_lex_cursor_v1_program_build(
            &pack, pack_wire.start, &lexical_plan,
            &guard_plan, &guarded_plan, &exec_plan,
            &cursor_program, error, sizeof(error))) {
            goto rejected;
        }
        cursor_program_built = true;
    } else if (action_answer_path) {
        (void)snprintf(
            error, sizeof(error),
            "cursor action binding requires an eligible execution plan");
        goto rejected;
    }
    if (action_answer_path &&
        (!action_compiler_digest ||
         !fh_answer_stream_v1_read(
             &action_answers, action_answer_path,
             error, sizeof(error)) ||
         !ppguarded_lex_cursor_v1_program_bind_actions(
             &cursor_program, &pack, &lexical_plan, &guard_plan,
             &guarded_plan,
             action_answers.terms, action_answers.len,
             action_compiler_digest, action_answers.digest,
            error, sizeof(error)))) {
        goto rejected;
    }
    if (emit_c_path) {
        FILE *generated = NULL;
        bool emitted;
        int close_status;

        if (!emit_c_prefix || !cursor_program.actions_bound) {
            (void)snprintf(
                error, sizeof(error),
                "generated C requires a bound cursor/action program");
            goto rejected;
        }
        generated = fopen(emit_c_path, "wb");
        if (!generated) {
            (void)snprintf(
                error, sizeof(error), "cannot open generated C output");
            goto rejected;
        }
        emitted = ppguarded_lex_cursor_v1_emit_c(
            &cursor_program, generated, emit_c_prefix,
            error, sizeof(error));
        close_status = fclose(generated);
        if (!emitted || close_status != 0) {
            if (error[0] == '\0') {
                (void)snprintf(
                    error, sizeof(error), "cannot finish generated C output");
            }
            goto rejected;
        }
    }
    if (cursor_hybrid_only) {
        if (!run_cursor_hybrid_only(
                &cursor_program, &pack, pack_wire.start,
                &lexical_plan, &guard_plan, &guarded_plan, &exec_plan,
                input, input_len, &limits, benchmark_iterations,
                error, sizeof(error))) {
            goto rejected;
        }
        ok = true;
        goto done;
    }
    if (!ppguarded_lex_exec_v1_run_bytes(
            &pack, pack_wire.start, &lexical_plan, &guard_plan,
            &guarded_plan, &exec_plan, input, input_len, &limits,
            &result, error, sizeof(error))) {
        goto rejected;
    }
    if (cursor_program_built) {
        if (cursor_program.terminal_len == 0u) {
            (void)snprintf(
                error, sizeof(error),
                "cursor program unexpectedly has no terminal sources");
            goto rejected;
        }
    {
        uint32_t saved_terminal_id =
            cursor_program.terminals[0].terminal_id;
        cursor_program.terminals[0].terminal_id ^= UINT32_C(1);
        if (ppguarded_lex_cursor_v1_program_validate(
                &cursor_program, error, sizeof(error))) {
            cursor_program.terminals[0].terminal_id = saved_terminal_id;
            (void)snprintf(
                error, sizeof(error),
                "cursor program accepted a corrupted terminal identity");
            goto rejected;
        }
        cursor_program.terminals[0].terminal_id = saved_terminal_id;
        if (!ppguarded_lex_cursor_v1_program_validate(
                &cursor_program, error, sizeof(error))) {
            goto rejected;
        }
        cursor_program_mutations++;
    }
    if (cursor_program.final_slr.production_len == 0u ||
        !cursor_program.final_production_lhs_indices) {
        (void)snprintf(
            error, sizeof(error),
            "cursor program unexpectedly has no production columns");
        goto rejected;
    }
    {
        uint32_t saved_lhs_index =
            cursor_program.final_production_lhs_indices[0];
        cursor_program.final_production_lhs_indices[0] = UINT32_MAX;
        if (ppguarded_lex_cursor_v1_program_validate(
                &cursor_program, error, sizeof(error))) {
            cursor_program.final_production_lhs_indices[0] =
                saved_lhs_index;
            (void)snprintf(
                error, sizeof(error),
                "corrupted cursor production column survived validation");
            goto rejected;
        }
        cursor_program.final_production_lhs_indices[0] = saved_lhs_index;
        if (!ppguarded_lex_cursor_v1_program_validate(
                &cursor_program, error, sizeof(error))) {
            goto rejected;
        }
        cursor_program_mutations++;
    }
    if (cursor_program.actions_bound) {
        uint32_t production_id;
        if (!ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan,
                error, sizeof(error))) {
            goto rejected;
        }
        for (production_id = 0u;
             production_id < cursor_program.actions.production_len;
             production_id++) {
            const PPActionBytecodeV1Production *production =
                &cursor_program.actions.productions[production_id];
            Atom **slots = NULL;
            Atom *tree_value = NULL;
            Atom *bytecode_value = NULL;
            Atom *source_action = cursor_source_action(
                &pack, &guard_plan, production_id);
            uint32_t slot_id;

            if (production->arity > 0u) {
                slots = malloc(
                    sizeof(*slots) * (size_t)production->arity);
                if (!slots)
                    goto rejected;
            }
            for (slot_id = 0u; slot_id < production->arity; slot_id++) {
                slots[slot_id] = cursor_action_slot_value(
                    &action_slot_arena, production_id, slot_id);
            }
            if (!source_action ||
                !ppnative_v1_apply_action_term(
                    &action_tree_arena, source_action,
                    slots, production->arity, &tree_value,
                    error, sizeof(error)) ||
                !pp_action_bytecode_v1_execute_prevalidated(
                    &cursor_program.actions, production_id,
                    slots, production->arity,
                    &action_bytecode_arena, &bytecode_value,
                    error, sizeof(error)) ||
                !atom_eq(tree_value, bytecode_value)) {
                free(slots);
                (void)snprintf(
                    error, sizeof(error),
                    "bound cursor action disagreement at production %u",
                    production_id);
                goto rejected;
            }
            free(slots);
            cursor_action_agreements++;
        }
        {
            PPActionBytecodeV1Instruction *instruction;
            PPActionBytecodeV1InstructionKind saved_kind;
            uint32_t extension_id = pack.production_len;
            uint32_t instruction_id = cursor_program.actions.productions[
                extension_id].instruction_begin;
            instruction = &cursor_program.actions.instructions[
                instruction_id];
            saved_kind = instruction->kind;
            instruction->kind = (PPActionBytecodeV1InstructionKind)99;
            if (ppguarded_lex_cursor_v1_program_validate_bound(
                    &cursor_program, &pack, &lexical_plan, &guard_plan,
                    &guarded_plan,
                    error, sizeof(error))) {
                instruction->kind = saved_kind;
                (void)snprintf(
                    error, sizeof(error),
                    "corrupted extension action survived validation");
                goto rejected;
            }
            instruction->kind = saved_kind;
            if (!ppguarded_lex_cursor_v1_program_validate_bound(
                    &cursor_program, &pack, &lexical_plan, &guard_plan,
                    &guarded_plan,
                    error, sizeof(error))) {
                goto rejected;
            }
            cursor_action_mutations++;
        }
        {
            uint32_t extension_id = pack.production_len;
            uint32_t slr_index = 0u;
            CettaLpNativeSlrProgramProduction *production;
            while (slr_index <
                       cursor_program.final_slr.authored_production_len &&
                   cursor_program.final_slr.productions[
                       slr_index].label != extension_id) {
                slr_index++;
            }
            if (slr_index >=
                cursor_program.final_slr.authored_production_len) {
                (void)snprintf(
                    error, sizeof(error),
                    "extension action has no SLR production row");
                goto rejected;
            }
            production = &cursor_program.final_slr.productions[slr_index];
            CettaLpNativeSymbol *item = &cursor_program.final_slr.rhs[
                production->rhs_begin];
            SymbolId saved_name = item->name;
            item->name ^= (SymbolId)1u;
            if (ppguarded_lex_cursor_v1_program_validate_bound(
                    &cursor_program, &pack, &lexical_plan, &guard_plan,
                    &guarded_plan,
                    error, sizeof(error))) {
                item->name = saved_name;
                (void)snprintf(
                    error, sizeof(error),
                    "corrupted action/SLR binding survived validation");
                goto rejected;
            }
            item->name = saved_name;
            if (!ppguarded_lex_cursor_v1_program_validate_bound(
                    &cursor_program, &pack, &lexical_plan, &guard_plan,
                    &guarded_plan,
                    error, sizeof(error))) {
                goto rejected;
            }
            cursor_action_mutations++;
        }
        {
            uint32_t saved_len = cursor_program.actions.production_len;
            cursor_program.actions.production_len--;
            if (ppguarded_lex_cursor_v1_program_validate_bound(
                    &cursor_program, &pack, &lexical_plan, &guard_plan,
                    &guarded_plan,
                    error, sizeof(error))) {
                cursor_program.actions.production_len = saved_len;
                (void)snprintf(
                    error, sizeof(error),
                    "truncated cursor action inventory survived validation");
                goto rejected;
            }
            cursor_program.actions.production_len = saved_len;
            if (!ppguarded_lex_cursor_v1_program_validate_bound(
                    &cursor_program, &pack, &lexical_plan, &guard_plan,
                    &guarded_plan,
                    error, sizeof(error))) {
                goto rejected;
            }
            cursor_action_mutations++;
        }
    }
    {
        uint32_t index;
        for (index = 0u; index < cursor_program.terminal_len; index++) {
            switch (cursor_program.terminals[index].kind) {
            case PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR:
                cursor_scalar_sources++;
                break;
            case PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN:
                cursor_ordinary_sources++;
                break;
            case PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN:
                cursor_guarded_sources++;
                break;
            }
        }
    }
    }
    derived_view = (CettaLpNativeUtf8ScalarView){
        .codepoints = result.scan.codepoints,
        .byte_offsets = result.scan.byte_offsets,
        .scalar_len = result.scan.input_scalar_len,
        .input_byte_len = result.scan.input_byte_len,
        .decoded_byte_len = 0u,
        .source_pass_count = 0u,
    };
    if (cursor_program_built &&
        (!ppguarded_lex_cursor_v1_program_run_scalar_view(
             &cursor_program, &derived_view,
             limits.parse_work_limit, &cursor_receipt,
             error, sizeof(error)) ||
         cursor_receipt.outcome ==
             PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT ||
         cursor_receipt.source_pass_count != 0u ||
         (cursor_receipt.outcome ==
              PPGUARDED_LEX_CURSOR_V1_ACCEPTED) != result.gll.accepted)) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "cursor program changed the general-engine decision");
        }
        goto rejected;
    }
    if (cursor_program.actions_bound &&
        (!ppguarded_lex_cursor_v1_program_run_semantic_lattice(
             &cursor_program, &derived_view,
             &result.gll_projected_relation.lattice,
             result.gll_projected_relation.witness_values,
             result.gll_projected_relation.witness_len,
             limits.parse_work_limit, &cursor_gll_semantic,
             error, sizeof(error)) ||
         !ppguarded_lex_cursor_v1_program_run_semantic_lattice(
             &cursor_program, &derived_view,
             &result.glr_projected_relation.lattice,
             result.glr_projected_relation.witness_values,
             result.glr_projected_relation.witness_len,
             limits.parse_work_limit, &cursor_glr_semantic,
             error, sizeof(error)) ||
         !cursor_receipts_equal(
             &cursor_receipt, &cursor_gll_semantic.receipt) ||
         !cursor_receipts_equal(
             &cursor_receipt, &cursor_glr_semantic.receipt) ||
         cursor_gll_semantic.terminal_value_len !=
             cursor_receipt.shift_len ||
         cursor_glr_semantic.terminal_value_len !=
             cursor_receipt.shift_len ||
         cursor_gll_semantic.action_execution_len !=
             cursor_receipt.reduce_len ||
         cursor_glr_semantic.action_execution_len !=
             cursor_receipt.reduce_len ||
         (result.gll.accepted &&
          (result.gll.semantic_result_len != 1u ||
           !cursor_gll_semantic.semantic_result ||
           !cursor_glr_semantic.semantic_result ||
           !atom_eq(cursor_gll_semantic.semantic_result,
                    result.gll.semantic_results[0]) ||
           !atom_eq(cursor_glr_semantic.semantic_result,
                    result.glr.semantic_results[0]) ||
           !atom_eq(cursor_gll_semantic.semantic_result,
                    cursor_glr_semantic.semantic_result))) ||
         (!result.gll.accepted &&
          (cursor_gll_semantic.semantic_result ||
           cursor_glr_semantic.semantic_result)))) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "cursor action trace changed complete-forest semantics");
        }
        goto rejected;
    }
    if (cursor_program.actions_bound &&
        (!ppguarded_lex_cursor_v1_program_run_semantic_hybrid(
             &cursor_program, &pack, pack_wire.start,
             &lexical_plan, &guard_plan, &guarded_plan, &exec_plan,
             &derived_view, PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL,
             limits.replay_depth, limits.result_limit,
             limits.parse_work_limit, &cursor_hybrid_gll_semantic,
             error, sizeof(error)) ||
         !ppguarded_lex_cursor_v1_program_run_semantic_hybrid(
             &cursor_program, &pack, pack_wire.start,
             &lexical_plan, &guard_plan, &guarded_plan, &exec_plan,
             &derived_view, PPGUARDED_LEX_CURSOR_V1_PREPARED_GLR,
             limits.replay_depth, limits.result_limit,
             limits.parse_work_limit, &cursor_hybrid_glr_semantic,
             error, sizeof(error)) ||
         !cursor_receipts_equal(
             &cursor_receipt, &cursor_hybrid_gll_semantic.receipt) ||
         !cursor_receipts_equal(
             &cursor_receipt, &cursor_hybrid_glr_semantic.receipt) ||
         cursor_hybrid_gll_semantic.terminal_value_len !=
             cursor_receipt.shift_len ||
         cursor_hybrid_glr_semantic.terminal_value_len !=
             cursor_receipt.shift_len ||
         cursor_hybrid_gll_semantic.action_execution_len !=
             cursor_receipt.reduce_len ||
         cursor_hybrid_glr_semantic.action_execution_len !=
             cursor_receipt.reduce_len ||
         cursor_hybrid_gll_semantic.receipt.source_pass_count != 0u ||
         cursor_hybrid_glr_semantic.receipt.source_pass_count != 0u ||
         cursor_hybrid_gll_semantic.prepared_fallback_run_len !=
             cursor_hybrid_glr_semantic.prepared_fallback_run_len ||
         (result.gll.accepted &&
          (!cursor_hybrid_gll_semantic.semantic_result ||
           !cursor_hybrid_glr_semantic.semantic_result ||
           !cursor_gll_semantic.semantic_result ||
           !atom_eq(cursor_hybrid_gll_semantic.semantic_result,
                    cursor_gll_semantic.semantic_result) ||
           !atom_eq(cursor_hybrid_glr_semantic.semantic_result,
                    cursor_glr_semantic.semantic_result) ||
           !atom_eq(cursor_hybrid_gll_semantic.semantic_result,
                    cursor_hybrid_glr_semantic.semantic_result))) ||
         (!result.gll.accepted &&
          (cursor_hybrid_gll_semantic.semantic_result ||
           cursor_hybrid_glr_semantic.semantic_result)))) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "hybrid cursor semantic path changed exact semantics");
        }
        goto rejected;
    }
    if (cursor_program.actions_bound)
        cursor_hybrid_semantic_agreement = 1u;
    if (cursor_program.actions_bound) {
        PPGuardedLexCursorV1Receipt expected_bytes_receipt = cursor_receipt;

        expected_bytes_receipt.source_pass_count = 1u;
        if (!ppguarded_lex_cursor_v1_program_run_semantic_hybrid_bytes(
                &cursor_program, &pack, pack_wire.start,
                &lexical_plan, &guard_plan, &guarded_plan, &exec_plan,
                input, input_len, PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL,
                limits.replay_depth, limits.result_limit,
                limits.parse_work_limit, &cursor_hybrid_bytes_semantic,
                error, sizeof(error)) ||
            !cursor_receipts_equal(
                &expected_bytes_receipt,
                &cursor_hybrid_bytes_semantic.receipt) ||
            cursor_hybrid_bytes_semantic.terminal_value_len !=
                cursor_hybrid_gll_semantic.terminal_value_len ||
            cursor_hybrid_bytes_semantic.action_execution_len !=
                cursor_hybrid_gll_semantic.action_execution_len ||
            cursor_hybrid_bytes_semantic.action_instruction_len !=
                cursor_hybrid_gll_semantic.action_instruction_len ||
            cursor_hybrid_bytes_semantic.value_program_run_len !=
                cursor_hybrid_gll_semantic.value_program_run_len ||
            cursor_hybrid_bytes_semantic.value_parser_work_item_len !=
                cursor_hybrid_gll_semantic.value_parser_work_item_len ||
            cursor_hybrid_bytes_semantic.prepared_fallback_run_len !=
                cursor_hybrid_gll_semantic.prepared_fallback_run_len ||
            cursor_hybrid_bytes_semantic.prepared_fallback_work_item_len !=
                cursor_hybrid_gll_semantic.prepared_fallback_work_item_len ||
            ((cursor_hybrid_gll_semantic.semantic_result ||
              cursor_hybrid_bytes_semantic.semantic_result) &&
             (!cursor_hybrid_gll_semantic.semantic_result ||
              !cursor_hybrid_bytes_semantic.semantic_result ||
              !atom_eq(cursor_hybrid_gll_semantic.semantic_result,
                       cursor_hybrid_bytes_semantic.semantic_result)))) {
            if (!error[0]) {
                (void)snprintf(
                    error, sizeof(error),
                    "byte-source hybrid changed scalar-view semantics");
            }
            goto rejected;
        }
        cursor_hybrid_bytes_agreement = 1u;
    }
    if (cursor_program.actions_bound &&
        cursor_hybrid_gll_semantic.prepared_fallback_run_len > 0u) {
        void *saved_ordinary_family =
            exec_plan.ordinary_witness_parsers.implementation;
        bool mutation_survived;

        exec_plan.ordinary_witness_parsers.implementation = NULL;
        error[0] = '\0';
        mutation_survived =
            ppguarded_lex_cursor_v1_program_run_semantic_hybrid(
                &cursor_program, &pack, pack_wire.start,
                &lexical_plan, &guard_plan, &guarded_plan, &exec_plan,
                &derived_view, PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL,
                limits.replay_depth, limits.result_limit,
                limits.parse_work_limit, &cursor_hybrid_mutation_semantic,
                error, sizeof(error));
        exec_plan.ordinary_witness_parsers.implementation =
            saved_ordinary_family;
        ppguarded_lex_cursor_v1_semantic_result_free(
            &cursor_hybrid_mutation_semantic);
        if (mutation_survived) {
            (void)snprintf(
                error, sizeof(error),
                "hybrid cursor accepted a missing prepared fallback family");
            goto rejected;
        }
        error[0] = '\0';
        cursor_hybrid_mutations++;
    }
    if (cursor_program.actions_bound &&
        cursor_program.value_programs_complete &&
        (!ppguarded_lex_cursor_v1_program_run_semantic_direct(
             &cursor_program, &derived_view,
             limits.parse_work_limit, &cursor_direct_semantic,
             error, sizeof(error)) ||
         !cursor_receipts_equal(
             &cursor_receipt, &cursor_direct_semantic.receipt) ||
         cursor_direct_semantic.terminal_value_len !=
             cursor_receipt.shift_len ||
         cursor_direct_semantic.action_execution_len !=
             cursor_receipt.reduce_len ||
         cursor_direct_semantic.receipt.source_pass_count != 0u ||
         (result.gll.accepted &&
          (!cursor_direct_semantic.semantic_result ||
           !cursor_gll_semantic.semantic_result ||
           !atom_eq(cursor_direct_semantic.semantic_result,
                    cursor_gll_semantic.semantic_result))) ||
         (!result.gll.accepted &&
          cursor_direct_semantic.semantic_result))) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "direct cursor value programs changed semantics");
        }
        goto rejected;
    }
    if (cursor_program.actions_bound &&
        cursor_program.value_programs_complete) {
        cursor_direct_semantic_agreement = 1u;
    }
    if (cursor_program.actions_bound &&
        cursor_receipt.ordinary_span_token_len +
                cursor_receipt.guarded_span_token_len > 0u) {
        PPGuardedLexCursorV1SemanticResult missing_witness;

        ppguarded_lex_cursor_v1_semantic_result_init(&missing_witness);
        error[0] = '\0';
        if (ppguarded_lex_cursor_v1_program_run_semantic_lattice(
                &cursor_program, &derived_view,
                &result.gll_projected_relation.lattice,
                NULL, 0u, limits.parse_work_limit,
                &missing_witness, error, sizeof(error))) {
            ppguarded_lex_cursor_v1_semantic_result_free(&missing_witness);
            (void)snprintf(
                error, sizeof(error),
                "cursor semantic execution accepted a missing span witness");
            goto rejected;
        }
        ppguarded_lex_cursor_v1_semantic_result_free(&missing_witness);
        error[0] = '\0';
        cursor_semantic_mutations++;
    }
    if (cursor_program.actions_bound &&
        cursor_program.value_program_len > 0u &&
        cursor_program.value_programs[0].terminal_len > 0u) {
        PPGuardedLexCursorV1ValueTerminal *terminal =
            &cursor_program.value_programs[0].terminals[0];
        uint32_t saved_terminal_id = terminal->terminal_id;

        terminal->terminal_id ^= UINT32_C(1);
        if (ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan, error, sizeof(error))) {
            terminal->terminal_id = saved_terminal_id;
            (void)snprintf(
                error, sizeof(error),
                "corrupted cursor value source survived validation");
            goto rejected;
        }
        terminal->terminal_id = saved_terminal_id;
        if (!ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan, error, sizeof(error))) {
            goto rejected;
        }
        cursor_semantic_mutations++;
    }
    if (cursor_program.actions_bound &&
        cursor_program.value_program_len > 0u &&
        cursor_program.value_programs[0].scalar_dispatch_range_len > 0u &&
        cursor_program.value_programs[0].scalar_dispatch_actions) {
        PPGuardedLexCursorV1ValueProgram *value_program =
            &cursor_program.value_programs[0];
        CettaLpNativeSlrProgramAction saved_action =
            value_program->scalar_dispatch_actions[0];

        value_program->scalar_dispatch_actions[0].kind =
            saved_action.kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR
                ? CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT
                : CETTA_LP_NATIVE_SLR_PROGRAM_ERROR;
        if (ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan, error, sizeof(error))) {
            value_program->scalar_dispatch_actions[0] = saved_action;
            (void)snprintf(
                error, sizeof(error),
                "corrupted cursor scalar dispatch survived validation");
            goto rejected;
        }
        value_program->scalar_dispatch_actions[0] = saved_action;
        if (!ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan, error, sizeof(error))) {
            goto rejected;
        }
        cursor_semantic_mutations++;
    }
    if (cursor_program.actions_bound &&
        cursor_program.value_program_len > 0u &&
        cursor_program.value_programs[0].slr.production_len > 0u &&
        cursor_program.value_programs[0].production_lhs_indices) {
        PPGuardedLexCursorV1ValueProgram *value_program =
            &cursor_program.value_programs[0];
        uint32_t saved_lhs_index = value_program->production_lhs_indices[0];

        value_program->production_lhs_indices[0] = UINT32_MAX;
        if (ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan, error, sizeof(error))) {
            value_program->production_lhs_indices[0] = saved_lhs_index;
            (void)snprintf(
                error, sizeof(error),
                "corrupted cursor value production column survived "
                "validation");
            goto rejected;
        }
        value_program->production_lhs_indices[0] = saved_lhs_index;
        if (!ppguarded_lex_cursor_v1_program_validate_bound(
                &cursor_program, &pack, &lexical_plan, &guard_plan,
                &guarded_plan, error, sizeof(error))) {
            goto rejected;
        }
        cursor_semantic_mutations++;
    }
    if (!ppguarded_lex_exec_v1_run(
            &pack, pack_wire.start, &lexical_plan, &guard_plan,
            &guarded_plan, &exec_plan, &derived_view, &limits,
            &derived_result, error, sizeof(error)) ||
        derived_result.receipt.source_pass_count != 0u ||
        derived_result.receipt.dfa_scan_count != 1u ||
        derived_result.receipt.ordinary_witness_parser_family_build_count !=
            result.receipt.ordinary_witness_parser_family_build_count ||
        derived_result.receipt.ordinary_witness_prepared_start_len !=
            result.receipt.ordinary_witness_prepared_start_len ||
        derived_result.receipt.guard_witness_parser_family_build_count !=
            result.receipt.guard_witness_parser_family_build_count ||
        derived_result.receipt.guard_witness_prepared_start_len !=
            result.receipt.guard_witness_prepared_start_len ||
        derived_result.receipt.guarded_witness_parser_family_build_count !=
            result.receipt.guarded_witness_parser_family_build_count ||
        derived_result.receipt.guarded_witness_prepared_start_len !=
            result.receipt.guarded_witness_prepared_start_len ||
        derived_result.receipt.final_parser_table_build_count !=
            result.receipt.final_parser_table_build_count ||
        derived_result.receipt.slr_outcome !=
            result.receipt.slr_outcome ||
        derived_result.receipt.projected_token_len !=
            result.receipt.projected_token_len ||
        (result.receipt.slr_outcome ==
             CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED &&
         !results_equal(&result.slr, &derived_result.slr)) ||
        !results_equal(&result.gll, &derived_result.gll) ||
        !results_equal(&result.glr, &derived_result.glr)) {
        if (!error[0]) {
            (void)snprintf(
                error, sizeof(error),
                "derived scalar-view execution changed its result");
        }
        goto rejected;
    }
    if (benchmark_iterations > 0u &&
        (!benchmark_final_forest(
             FINAL_FOREST_BENCH_SLR, &exec_plan, &result, &limits,
             benchmark_iterations, &slr_benchmark,
             error, sizeof(error)) ||
         !benchmark_final_forest(
             FINAL_FOREST_BENCH_GLL, &exec_plan, &result, &limits,
             benchmark_iterations, &gll_benchmark,
             error, sizeof(error)) ||
         !benchmark_final_forest(
             FINAL_FOREST_BENCH_GLR, &exec_plan, &result, &limits,
             benchmark_iterations, &glr_benchmark,
             error, sizeof(error)) ||
         (cursor_program.actions_bound &&
          !benchmark_cursor_hybrid_bytes(
              &cursor_program, &pack, pack_wire.start,
              &lexical_plan, &guard_plan, &guarded_plan, &exec_plan,
              input, input_len, &cursor_hybrid_gll_semantic, &limits,
              benchmark_iterations, &cursor_hybrid_benchmark,
              error, sizeof(error))))) {
        goto rejected;
    }
    retained = retained_payload(&result);
    derived_retained = retained_payload(&derived_result);

    printf("parser-pack-guarded-lexical-exec-v1\n");
    printf("base-pack-digest\t%s\n", pack.pack_digest);
    printf("lexical-plan-digest\t%s\n", lexical_plan.plan_digest);
    printf("guard-plan-digest\t%s\n", guard_plan.plan_digest);
    printf("guarded-plan-digest\t%s\n", guarded_plan.plan_digest);
    printf("execution-plan-digest\t%s\n",
           exec_plan.execution_plan_digest);
    printf("cursor-certificate-digest\t%s\n",
           exec_plan.cursor_certificate.digest);
    printf("cursor-certificate-eligible\t%u\n",
           exec_plan.cursor_certificate.eligible ? 1u : 0u);
    printf("cursor-certificate-failure-mask\t%u\n",
           exec_plan.cursor_certificate.failure_mask);
    printf("cursor-certificate-slr-states\t%u\n",
           exec_plan.cursor_certificate.slr.state_len);
    printf("cursor-certificate-slr-conflicts\t%u\n",
           exec_plan.cursor_certificate.slr.conflict_len);
    printf("cursor-certificate-slr-accepts\t%u\n",
           exec_plan.cursor_certificate.slr.accept_len);
    printf("cursor-certificate-reachable-nonterminals\t%u\n",
           exec_plan.cursor_certificate.reachable_nonterminal_len);
    printf("cursor-certificate-reachable-productions\t%u\n",
           exec_plan.cursor_certificate.reachable_production_len);
    printf("cursor-certificate-active-terminals\t%u\n",
           exec_plan.cursor_certificate.active_terminal_len);
    printf("cursor-certificate-scalar-terminals\t%u\n",
           exec_plan.cursor_certificate.scalar_terminal_len);
    printf("cursor-certificate-ordinary-span-tags\t%u\n",
           exec_plan.cursor_certificate.ordinary_span_tag_len);
    printf("cursor-certificate-guarded-span-tags\t%u\n",
           exec_plan.cursor_certificate.guarded_span_tag_len);
    printf("cursor-certificate-guard-lookahead-tags\t%u\n",
           exec_plan.cursor_certificate.guard_lookahead_tag_len);
    printf("cursor-certificate-zero-input-cycles\t%u\n",
           exec_plan.cursor_certificate.zero_input_cycle_len);
    printf("cursor-certificate-unmapped-terminals\t%u\n",
           exec_plan.cursor_certificate.unmapped_terminal_len);
    printf("cursor-certificate-duplicate-terminal-sources\t%u\n",
           exec_plan.cursor_certificate.duplicate_terminal_source_len);
    printf("cursor-certificate-empty-tokens\t%u\n",
           exec_plan.cursor_certificate.empty_token_len);
    printf("cursor-certificate-nullable-tokens\t%u\n",
           exec_plan.cursor_certificate.nullable_token_len);
    printf("cursor-certificate-non-prefix-free-tokens\t%u\n",
           exec_plan.cursor_certificate.non_prefix_free_token_len);
    printf("cursor-certificate-unsupported-guards\t%u\n",
           exec_plan.cursor_certificate.unsupported_guard_len);
    printf("cursor-certificate-boundary-crossings\t%u\n",
           exec_plan.cursor_certificate.boundary_crossing_len);
    printf("cursor-certificate-first-domain-overlaps\t%u\n",
           exec_plan.cursor_certificate.first_domain_overlap_len);
    printf("cursor-program-built\t%u\n",
           cursor_program_built ? 1u : 0u);
    if (cursor_program_built) {
    printf("cursor-program-digest\t%s\n",
           cursor_program.program_digest);
    if (cursor_program.actions_bound) {
        printf("cursor-program-actions-bound\t1\n");
        printf("cursor-program-action-compiler-digest\t%s\n",
               cursor_program.actions.compiler_digest);
        printf("cursor-program-action-answer-digest\t%s\n",
               cursor_program.actions.answer_set_digest);
        printf("cursor-program-action-program-digest\t%s\n",
               cursor_program.actions.program_digest);
        printf("cursor-program-action-productions\t%u\n",
               cursor_program.actions.production_len);
        printf("cursor-program-action-instructions\t%u\n",
               cursor_program.actions.instruction_len);
        printf("cursor-program-action-agreements\t%u\n",
               cursor_action_agreements);
        printf("cursor-program-action-mutations-killed\t%u\n",
               cursor_action_mutations);
        printf("cursor-program-semantic-terminal-values\t%u\n",
               cursor_gll_semantic.terminal_value_len);
        printf("cursor-program-semantic-action-executions\t%u\n",
               cursor_gll_semantic.action_execution_len);
        printf("cursor-program-semantic-action-instructions\t%llu\n",
               (unsigned long long)
                   cursor_gll_semantic.action_instruction_len);
        printf("cursor-program-value-programs\t%u\n",
               cursor_program.value_program_len);
        printf("cursor-program-value-programs-complete\t%u\n",
               cursor_program.value_programs_complete ? 1u : 0u);
        printf("cursor-program-value-program-conflicts\t%u\n",
               cursor_program.value_program_conflict_len);
        printf("cursor-program-direct-value-program-runs\t%u\n",
               cursor_direct_semantic.value_program_run_len);
        printf("cursor-program-direct-value-terminal-values\t%u\n",
               cursor_direct_semantic.value_terminal_value_len);
        printf("cursor-program-direct-value-action-executions\t%u\n",
               cursor_direct_semantic.value_action_execution_len);
        printf("cursor-program-direct-value-action-instructions\t%llu\n",
               (unsigned long long)
                   cursor_direct_semantic.value_action_instruction_len);
        printf("cursor-program-direct-value-parser-work\t%llu\n",
               (unsigned long long)
                   cursor_direct_semantic.value_parser_work_item_len);
        printf("cursor-program-direct-semantic-agreement\t%u\n",
               cursor_direct_semantic_agreement);
        printf("cursor-program-hybrid-value-program-runs\t%u\n",
               cursor_hybrid_gll_semantic.value_program_run_len);
        printf("cursor-program-hybrid-value-terminal-values\t%u\n",
               cursor_hybrid_gll_semantic.value_terminal_value_len);
        printf("cursor-program-hybrid-value-action-executions\t%u\n",
               cursor_hybrid_gll_semantic.value_action_execution_len);
        printf("cursor-program-hybrid-value-action-instructions\t%llu\n",
               (unsigned long long)
                   cursor_hybrid_gll_semantic.value_action_instruction_len);
        printf("cursor-program-hybrid-prepared-fallback-runs\t%u\n",
               cursor_hybrid_gll_semantic.prepared_fallback_run_len);
        printf("cursor-program-hybrid-prepared-fallback-work\t%llu\n",
               (unsigned long long)
                   cursor_hybrid_gll_semantic.prepared_fallback_work_item_len);
        printf("cursor-program-hybrid-value-parser-work\t%llu\n",
               (unsigned long long)
                   cursor_hybrid_gll_semantic.value_parser_work_item_len);
        printf("cursor-program-hybrid-semantic-agreement\t%u\n",
               cursor_hybrid_semantic_agreement);
        printf("cursor-program-hybrid-bytes-agreement\t%u\n",
               cursor_hybrid_bytes_agreement);
        printf("cursor-program-hybrid-bytes-source-passes\t%u\n",
               cursor_hybrid_bytes_semantic.receipt.source_pass_count);
        printf("cursor-program-hybrid-mutations-killed\t%u\n",
               cursor_hybrid_mutations);
        printf("cursor-program-semantic-agreement\t1\n");
        printf("cursor-program-semantic-mutations-killed\t%u\n",
               cursor_semantic_mutations);
    }
    printf("cursor-program-dfa-states\t%u\n",
           cursor_program.dfa.state_len);
    printf("cursor-program-dfa-transitions\t%u\n",
           cursor_program.dfa.transition_len);
    printf("cursor-program-slr-states\t%u\n",
           cursor_program.final_slr.summary.state_len);
    printf("cursor-program-slr-terminals\t%u\n",
           cursor_program.final_slr.terminal_len);
    printf("cursor-program-slr-nonterminals\t%u\n",
           cursor_program.final_slr.nonterminal_len);
    printf("cursor-program-slr-productions\t%u\n",
           cursor_program.final_slr.production_len);
    printf("cursor-program-slr-authored-productions\t%u\n",
           cursor_program.final_slr.authored_production_len);
    printf("cursor-program-slr-actions\t%u\n",
           cursor_program.final_slr.action_len);
    printf("cursor-program-slr-gotos\t%u\n",
           cursor_program.final_slr.goto_len);
    printf("cursor-program-terminal-sources\t%u\n",
           cursor_program.terminal_len);
    printf("cursor-program-scalar-sources\t%u\n",
           cursor_scalar_sources);
    printf("cursor-program-ordinary-span-sources\t%u\n",
           cursor_ordinary_sources);
    printf("cursor-program-guarded-span-sources\t%u\n",
           cursor_guarded_sources);
    printf("cursor-program-ranges\t%u\n",
           cursor_program.range_len);
    printf("cursor-program-mutation-killed\t%u\n",
           cursor_program_mutations);
    printf("cursor-program-outcome\t%s\n",
           cursor_outcome_name(cursor_receipt.outcome));
    printf("cursor-program-trace-digest\t%s\n",
           cursor_receipt.trace_digest);
    printf("cursor-program-source-passes\t%u\n",
           cursor_receipt.source_pass_count);
    printf("cursor-program-cursor-scans\t%u\n",
           cursor_receipt.cursor_scan_len);
    printf("cursor-program-tokens\t%u\n",
           cursor_receipt.token_len);
    printf("cursor-program-scalar-tokens\t%u\n",
           cursor_receipt.scalar_token_len);
    printf("cursor-program-ordinary-span-tokens\t%u\n",
           cursor_receipt.ordinary_span_token_len);
    printf("cursor-program-guarded-span-tokens\t%u\n",
           cursor_receipt.guarded_span_token_len);
    printf("cursor-program-shifts\t%u\n",
           cursor_receipt.shift_len);
    printf("cursor-program-reductions\t%u\n",
           cursor_receipt.reduce_len);
    printf("cursor-program-max-stack\t%u\n",
           cursor_receipt.max_stack_len);
    printf("cursor-program-dfa-work\t%llu\n",
           (unsigned long long)cursor_receipt.dfa_work_item_len);
    printf("cursor-program-parser-work\t%llu\n",
           (unsigned long long)cursor_receipt.parser_work_item_len);
    }
    printf("dfa-states\t%u\n", exec_plan.dfa.state_len);
    printf("dfa-transitions\t%u\n", exec_plan.dfa.transition_len);
    printf("source-passes\t%u\n", result.receipt.source_pass_count);
    printf("dfa-scans\t%u\n", result.receipt.dfa_scan_count);
    printf("view-replay-source-passes\t%u\n",
           derived_result.receipt.source_pass_count);
    printf("view-replay-dfa-scans\t%u\n",
           derived_result.receipt.dfa_scan_count);
    printf("input-bytes\t%u\n", result.receipt.input_byte_len);
    printf("input-scalars\t%u\n", result.receipt.input_scalar_len);
    printf("combined-tokens\t%u\n", result.receipt.combined_token_len);
    printf("ordinary-tokens\t%u\n", result.receipt.ordinary_token_len);
    printf("guard-body-tokens\t%u\n",
           result.receipt.guard_body_token_len);
    printf("guarded-candidates\t%u\n",
           result.receipt.guarded_candidate_len);
    printf("guarded-tokens\t%u\n", result.receipt.guarded_token_len);
    printf("projected-tokens\t%u\n", result.receipt.projected_token_len);
    printf("ordinary-witness-runs\t%u\n",
           result.receipt.ordinary_witness_runs);
    printf("guard-witness-runs\t%u\n",
           result.receipt.guard_witness_runs);
    printf("guarded-witness-runs\t%u\n",
           result.receipt.guarded_witness_runs);
    printf("ordinary-witness-parser-family-builds\t%u\n",
           result.receipt.ordinary_witness_parser_family_build_count);
    printf("ordinary-witness-prepared-starts\t%u\n",
           result.receipt.ordinary_witness_prepared_start_len);
    printf("guard-witness-parser-family-builds\t%u\n",
           result.receipt.guard_witness_parser_family_build_count);
    printf("guard-witness-prepared-starts\t%u\n",
           result.receipt.guard_witness_prepared_start_len);
    printf("guarded-witness-parser-family-builds\t%u\n",
           result.receipt.guarded_witness_parser_family_build_count);
    printf("guarded-witness-prepared-starts\t%u\n",
           result.receipt.guarded_witness_prepared_start_len);
    printf("final-parser-table-builds\t%u\n",
           result.receipt.final_parser_table_build_count);
    printf("slr-shadow-outcome\t%s\n",
           slr_outcome_name(result.receipt.slr_outcome));
    printf("slr-shadow-work-items\t%u\n",
           result.receipt.slr_parse_work_item_len);
    printf("slr-shadow-forest-digest\t%s\n",
           result.receipt.slr_outcome ==
                   CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED
               ? result.slr.forest_digest : "");
    printf("guard-relation-digest\t%s\n",
           result.receipt.guard_relation_digest);
    printf("projected-relation-digest\t%s\n",
           result.receipt.projected_relation_digest);
    printf("gll-outcome\t%s\n", outcome_name(result.gll.outcome));
    printf("glr-outcome\t%s\n", outcome_name(result.glr.outcome));
    printf("gll-decision\t%s\n",
           result.gll.accepted ? "accepted" : "rejected");
    printf("glr-decision\t%s\n",
           result.glr.accepted ? "accepted" : "rejected");
    printf("gll-forest-digest\t%s\n", result.gll.forest_digest);
    printf("glr-forest-digest\t%s\n", result.glr.forest_digest);
    printf("view-replay-forest-digest\t%s\n",
           derived_result.gll.forest_digest);
    printf("gll-forest-nodes\t%u\n", result.gll.forest.node_len);
    printf("glr-forest-nodes\t%u\n", result.glr.forest.node_len);
    printf("final-parse-work-items\t%llu\n",
           (unsigned long long)
               result.receipt.final_parse_work_item_len);
    write_retained_payload("logical-retained", &retained);
    write_retained_payload(
        "view-replay-logical-retained", &derived_retained);
    if (benchmark_iterations > 0u) {
        printf("benchmark-kind\tprepared-final-forest-kernels\n");
        printf("benchmark-iterations\t%u\n", benchmark_iterations);
        write_final_forest_benchmark("slr", &slr_benchmark);
        write_final_forest_benchmark("gll", &gll_benchmark);
        write_final_forest_benchmark("glr", &glr_benchmark);
        if (cursor_program.actions_bound)
            write_cursor_hybrid_benchmark(&cursor_hybrid_benchmark);
    }
    if (!write_results("gll-result", &result.gll) ||
        !write_results("glr-result", &result.glr)) {
        (void)snprintf(error, sizeof(error),
                       "failed to write guarded lexical results");
        goto rejected;
    }
    printf("end\n");
    ok = true;
    goto done;

rejected:
    fprintf(stderr, "guarded lexical execution rejected: %s\n",
            error[0] ? error : "unknown rejection");

done:
    free(input);
    arena_free(&action_bytecode_arena);
    arena_free(&action_tree_arena);
    arena_free(&action_slot_arena);
    ppguarded_lex_cursor_v1_semantic_result_free(
        &cursor_hybrid_mutation_semantic);
    ppguarded_lex_cursor_v1_semantic_result_free(
        &cursor_hybrid_bytes_semantic);
    ppguarded_lex_cursor_v1_semantic_result_free(
        &cursor_hybrid_glr_semantic);
    ppguarded_lex_cursor_v1_semantic_result_free(
        &cursor_hybrid_gll_semantic);
    ppguarded_lex_cursor_v1_semantic_result_free(&cursor_direct_semantic);
    ppguarded_lex_cursor_v1_semantic_result_free(&cursor_glr_semantic);
    ppguarded_lex_cursor_v1_semantic_result_free(&cursor_gll_semantic);
    ppguarded_lex_exec_v1_result_free(&derived_result);
    ppguarded_lex_exec_v1_result_free(&result);
    ppguarded_lex_cursor_v1_program_free(&cursor_program);
    ppguarded_lex_exec_v1_plan_free(&exec_plan);
    ppguarded_lex_v1_plan_free(&guarded_plan);
    ppguard_plan_v1_free(&guard_plan);
    ppguard_evidence_wire_v1_free(&evidence);
    pplex_v1_plan_free(&lexical_plan);
    rsnfa_v1_plan_free(&guard_nfa);
    rsnfa_v1_plan_free(&lexical_nfa);
    fh_answer_stream_v1_free(&guarded_answers);
    fh_answer_stream_v1_free(&action_answers);
    fh_answer_stream_v1_free(&guard_answers);
    fh_answer_stream_v1_free(&lexical_answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&pack_wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    const char *action_answer_path = NULL;
    const char *action_compiler_digest = NULL;
    const char *emit_c_path = NULL;
    const char *emit_c_prefix = NULL;
    int benchmark_index = 0;
    uint32_t benchmark_iterations = 0u;
    bool cursor_hybrid_only = false;
    bool ok;

    if (argc < 9 || argc > 14) {
        fprintf(stderr,
                "usage: parser_pack_guarded_lexical_exec_v1_stream "
                "ABI LEXICAL_NFA GUARD_NFA GUARD_EVIDENCE GUARDED_NFA "
                "INPUT REGULAR_COMPILER_SHA256 GUARDED_COMPILER_SHA256 "
                "[BENCHMARK_ITERATIONS | "
                "ACTION_ANSWERS ACTION_COMPILER_SHA256 "
                "[BENCHMARK_ITERATIONS | "
                "--cursor-hybrid-only BENCHMARK_ITERATIONS | "
                "--emit-c OUTPUT_C IDENTIFIER_PREFIX]]\n");
        return 1;
    }
    if (argc >= 11) {
        action_answer_path = argv[9];
        action_compiler_digest = argv[10];
        if (argc == 12)
            benchmark_index = 11;
        else if (argc == 13) {
            if (strcmp(argv[11], "--cursor-hybrid-only") != 0) {
                fprintf(stderr, "invalid cursor-only benchmark mode\n");
                return 1;
            }
            cursor_hybrid_only = true;
            benchmark_index = 12;
        } else if (argc == 14) {
            if (strcmp(argv[11], "--emit-c") != 0) {
                fprintf(stderr, "invalid cursor C-emission mode\n");
                return 1;
            }
            emit_c_path = argv[12];
            emit_c_prefix = argv[13];
        }
    } else if (argc == 10) {
        benchmark_index = 9;
    }
    if (benchmark_index != 0) {
        char *end = NULL;
        unsigned long parsed;

        errno = 0;
        parsed = strtoul(argv[benchmark_index], &end, 10);
        if (errno != 0 || !end || *end != '\0' ||
            parsed == 0ul || parsed > UINT32_MAX) {
            fprintf(stderr, "invalid benchmark iteration count\n");
            return 1;
        }
        benchmark_iterations = (uint32_t)parsed;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
             argv[7], argv[8], action_answer_path,
             action_compiler_digest, benchmark_iterations,
             cursor_hybrid_only, emit_c_path, emit_c_prefix);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
