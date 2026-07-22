#include "he_document_pipeline_v1.h"

#include "../native/finite_horn_ground_term_v1.h"

#include "symbol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HE_FNV_OFFSET UINT64_C(14695981039346656037)
#define HE_FNV_PRIME UINT64_C(1099511628211)

typedef struct {
    Atom **data;
    size_t len;
    size_t cap;
} HEBenchAtomStackV1;

typedef struct {
    uint64_t value;
    uint64_t nodes;
} HEBenchFingerprintV1;

static volatile uint64_t he_bench_sink_v1;

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
        (void)snprintf(error, error_size, "cannot open benchmark input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size,
                               "benchmark input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "cannot allocate benchmark input");
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
                           "cannot read benchmark input");
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

static bool parse_count(const char *text, bool allow_zero, uint32_t *out) {
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value > UINT32_MAX ||
        (!allow_zero && value == 0u)) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool atom_stack_push(HEBenchAtomStackV1 *stack, Atom *atom) {
    if (stack->len == stack->cap) {
        size_t cap = stack->cap ? stack->cap * 2u : 128u;
        Atom **next;
        if (cap < stack->cap || cap > SIZE_MAX / sizeof(*next))
            return false;
        next = realloc(stack->data, sizeof(*next) * cap);
        if (!next)
            return false;
        stack->data = next;
        stack->cap = cap;
    }
    stack->data[stack->len++] = atom;
    return true;
}

static void fingerprint_byte(HEBenchFingerprintV1 *fingerprint,
                             uint8_t value) {
    fingerprint->value ^= value;
    fingerprint->value *= HE_FNV_PRIME;
}

static void fingerprint_u64(HEBenchFingerprintV1 *fingerprint,
                            uint64_t value) {
    int shift;

    for (shift = 56; shift >= 0; shift -= 8)
        fingerprint_byte(fingerprint, (uint8_t)(value >> shift));
}

static void fingerprint_bytes(HEBenchFingerprintV1 *fingerprint,
                              const uint8_t *bytes,
                              uint32_t len) {
    uint32_t index;

    fingerprint_u64(fingerprint, len);
    for (index = 0u; index < len; index++)
        fingerprint_byte(fingerprint, bytes[index]);
}

static bool fingerprint_atoms(Atom *const *atoms,
                              uint32_t atom_len,
                              HEBenchFingerprintV1 *out) {
    HEBenchAtomStackV1 stack = {0};
    uint32_t index;
    bool ok = false;

    *out = (HEBenchFingerprintV1){
        .value = HE_FNV_OFFSET,
        .nodes = 0u,
    };
    for (index = atom_len; index > 0u; index--) {
        if (!atom_stack_push(&stack, atoms[index - 1u]))
            goto done;
    }
    while (stack.len > 0u) {
        Atom *atom = stack.data[--stack.len];
        uint32_t name_len;

        if (!atom || out->nodes == UINT64_MAX)
            goto done;
        out->nodes++;
        switch (atom->kind) {
        case ATOM_SYMBOL:
            fingerprint_byte(out, (uint8_t)'S');
            name_len = symbol_len(g_symbols, atom->sym_id);
            fingerprint_bytes(
                out,
                (const uint8_t *)symbol_bytes(g_symbols, atom->sym_id),
                name_len);
            break;
        case ATOM_VAR:
            fingerprint_byte(out, (uint8_t)'V');
            name_len = symbol_len(g_symbols, atom->sym_id);
            fingerprint_bytes(
                out,
                (const uint8_t *)symbol_bytes(g_symbols, atom->sym_id),
                name_len);
            break;
        case ATOM_EXPR:
            fingerprint_byte(out, (uint8_t)'E');
            fingerprint_u64(out, atom->expr.len);
            for (index = atom->expr.len; index > 0u; index--) {
                if (!atom_stack_push(
                        &stack, atom->expr.elems[index - 1u])) {
                    goto done;
                }
            }
            break;
        case ATOM_GROUNDED:
            goto done;
        }
    }
    ok = true;

done:
    free(stack.data);
    return ok;
}

static bool monotonic_nanoseconds(uint64_t *out) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 ||
        now.tv_nsec < 0) {
        return false;
    }
    *out = (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec;
    return true;
}

static bool parse_once(HEDocumentPipelineV1Prepared *prepared,
                       const uint8_t *input,
                       size_t input_len,
                       uint32_t expected_atoms,
                       bool require_count,
                       HEDocumentPipelineV1Result *result,
                       char *error,
                       size_t error_size) {
    return he_document_pipeline_v1_parse(
            prepared, input, input_len, result, error, error_size) &&
        result->accepted &&
        (!require_count || result->atom_len == expected_atoms) &&
        result->receipt.parse.source_pass_count == 1u &&
        result->receipt.prepared_fallback_run_len == 0u &&
        result->receipt.prepared_fallback_work_item_len == 0u;
}

static bool run(const char *abi_path,
                const char *lexical_nfa_path,
                const char *guard_nfa_path,
                const char *guard_evidence_path,
                const char *guarded_nfa_path,
                const char *action_answer_path,
                const char *semantic_mask_path,
                const char *projection_path,
                const char *input_path,
                const char *regular_compiler_digest,
                const char *guarded_compiler_digest,
                const char *action_compiler_digest,
                const char *semantic_mask_compiler_digest,
                uint32_t warmup_iterations,
                uint32_t measured_iterations) {
    const PPGuardedLexExecV1Limits limits = {
        .dfa_state_limit = UINT32_C(65536),
        .dfa_transition_limit = UINT32_C(2000000),
        .scan_work_limit = UINT64_C(500000000),
        .scan_token_limit = UINT32_C(20000000),
        .witness_work_limit = UINT32_C(500000000),
        .parse_work_limit = UINT32_C(500000000),
        .replay_depth = UINT32_C(4096),
        .result_limit = UINT32_C(20000000),
    };
    uint8_t *projection_bytes = NULL;
    size_t projection_len = 0u;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    Arena plan_arena;
    Atom *projection = NULL;
    HEDocumentPipelineV1Prepared *prepared = NULL;
    HEDocumentPipelineV1Result result;
    HEBenchFingerprintV1 fingerprint;
    HEDocumentPipelineV1Receipt receipt;
    uint32_t expected_atoms = 0u;
    uint32_t iteration;
    uint64_t started = 0u;
    uint64_t finished = 0u;
    uint64_t total_atoms = 0u;
    char error[512] = {0};
    bool ok = false;

    arena_init(&plan_arena);
    he_document_pipeline_v1_result_init(&result);
    memset(&receipt, 0, sizeof(receipt));
    if (!read_input(projection_path, &projection_bytes, &projection_len,
                    error, sizeof(error)) ||
        !read_input(input_path, &input, &input_len,
                    error, sizeof(error))) {
        goto rejected;
    }
    while (projection_len > 0u &&
           (projection_bytes[projection_len - 1u] == (uint8_t)'\n' ||
            projection_bytes[projection_len - 1u] == (uint8_t)'\r')) {
        projection_len--;
    }
    if (!fh_ground_term_v1_parse(
            &plan_arena, projection_bytes, projection_len,
            &projection, error, sizeof(error)) ||
        !(prepared = he_document_pipeline_v1_prepared_new()) ||
        !he_document_pipeline_v1_prepare(
            prepared, abi_path, lexical_nfa_path, guard_nfa_path,
            guard_evidence_path, guarded_nfa_path, action_answer_path,
            semantic_mask_path,
            regular_compiler_digest, guarded_compiler_digest,
            action_compiler_digest, semantic_mask_compiler_digest,
            projection, &limits, 4096u, error, sizeof(error)) ||
        !parse_once(prepared, input, input_len, 0u, false,
                    &result, error, sizeof(error)) ||
        !fingerprint_atoms(result.atoms, result.atom_len, &fingerprint)) {
        if (!error[0])
            (void)snprintf(error, sizeof(error),
                           "cannot fingerprint prepared HE parse");
        goto rejected;
    }
    expected_atoms = result.atom_len;
    receipt = result.receipt;
    he_document_pipeline_v1_result_free(&result);
    he_document_pipeline_v1_result_init(&result);

    for (iteration = 0u; iteration < warmup_iterations; iteration++) {
        if (!parse_once(prepared, input, input_len, expected_atoms, true,
                        &result, error, sizeof(error))) {
            goto rejected;
        }
        he_bench_sink_v1 ^= result.atom_len;
        he_document_pipeline_v1_result_free(&result);
        he_document_pipeline_v1_result_init(&result);
    }
    if (!monotonic_nanoseconds(&started)) {
        (void)snprintf(error, sizeof(error),
                       "cannot start monotonic benchmark timer");
        goto rejected;
    }
    for (iteration = 0u; iteration < measured_iterations; iteration++) {
        if (!parse_once(prepared, input, input_len, expected_atoms, true,
                        &result, error, sizeof(error))) {
            goto rejected;
        }
        total_atoms += result.atom_len;
        he_document_pipeline_v1_result_free(&result);
        he_document_pipeline_v1_result_init(&result);
    }
    if (!monotonic_nanoseconds(&finished) || finished < started) {
        (void)snprintf(error, sizeof(error),
                       "cannot finish monotonic benchmark timer");
        goto rejected;
    }
    he_bench_sink_v1 ^= total_atoms;
    (void)printf("he-gslt-document-parse-only-v1\n");
    (void)printf("input-bytes\t%zu\n", input_len);
    (void)printf("top-level-atoms\t%u\n", expected_atoms);
    (void)printf("atom-nodes\t%llu\n",
                 (unsigned long long)fingerprint.nodes);
    (void)printf("semantic-fnv64\t%016llx\n",
                 (unsigned long long)fingerprint.value);
    (void)printf("warmup-iterations\t%u\n", warmup_iterations);
    (void)printf("measured-iterations\t%u\n", measured_iterations);
    (void)printf("total-nanoseconds\t%llu\n",
                 (unsigned long long)(finished - started));
    (void)printf("prepared-builds\t%u\n",
                 he_document_pipeline_v1_prepared_build_count(prepared));
    (void)printf("source-passes-per-parse\t%u\n",
                 receipt.parse.source_pass_count);
    (void)printf("prepared-fallback-runs-per-parse\t%u\n",
                 receipt.prepared_fallback_run_len);
    (void)printf("input-scalars\t%u\n", receipt.parse.input_scalar_len);
    (void)printf("cursor-tokens\t%u\n", receipt.parse.token_len);
    (void)printf("cursor-dfa-work\t%llu\n",
                 (unsigned long long)receipt.parse.dfa_work_item_len);
    (void)printf("cursor-parser-work\t%llu\n",
                 (unsigned long long)receipt.parse.parser_work_item_len);
    (void)printf("terminal-values\t%u\n", receipt.terminal_value_len);
    (void)printf("action-executions\t%u\n", receipt.action_execution_len);
    (void)printf("action-instructions\t%llu\n",
                 (unsigned long long)receipt.action_instruction_len);
    (void)printf("value-program-runs\t%u\n",
                 receipt.value_program_run_len);
    (void)printf("value-parser-work\t%llu\n",
                 (unsigned long long)receipt.value_parser_work_item_len);
    (void)printf("semantic-mask-bound-terminals\t%u\n",
                 receipt.semantic_mask_bound_terminal_len);
    (void)printf("semantic-mask-runs\t%u\n",
                 receipt.semantic_mask_run_len);
    (void)printf("semantic-mask-input-scalars\t%u\n",
                 receipt.semantic_mask_input_scalar_len);
    (void)printf("semantic-mask-retained-scalars\t%u\n",
                 receipt.semantic_mask_retained_scalar_len);
    (void)printf("semantic-mask-work\t%llu\n",
                 (unsigned long long)receipt.semantic_mask_work_item_len);
    (void)printf("execution-plan-digest\t%s\n",
                 receipt.execution_plan_digest);
    (void)printf("cursor-certificate-digest\t%s\n",
                 receipt.cursor_certificate_digest);
    (void)printf("cursor-program-digest\t%s\n",
                 receipt.cursor_program_digest);
    (void)printf("action-program-digest\t%s\n",
                 receipt.action_program_digest);
    (void)printf("semantic-mask-compiler-digest\t%s\n",
                 receipt.semantic_mask_compiler_digest);
    (void)printf("semantic-mask-answer-digest\t%s\n",
                 receipt.semantic_mask_answer_digest);
    (void)printf("semantic-mask-binding-digest\t%s\n",
                 receipt.semantic_mask_binding_digest);
    (void)printf("atom-projection-plan-digest\t%s\n",
                 receipt.atom_projection_plan_digest);
    (void)printf("end\n");
    ok = true;
    goto done;

rejected:
    (void)fprintf(stderr, "HE prepared parse-only benchmark rejected: %s\n",
                  error[0] ? error : "unknown rejection");

done:
    he_document_pipeline_v1_result_free(&result);
    he_document_pipeline_v1_prepared_free(prepared);
    arena_free(&plan_arena);
    free(input);
    free(projection_bytes);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    uint32_t warmup_iterations;
    uint32_t measured_iterations;
    bool ok;

    if (argc != 16 || !parse_count(argv[14], true, &warmup_iterations) ||
        !parse_count(argv[15], false, &measured_iterations)) {
        (void)fprintf(
            stderr,
            "usage: %s ABI LEXICAL_NFA GUARD_NFA GUARD_EVIDENCE "
            "GUARDED_NFA ACTION_ANSWERS SEMANTIC_MASK PROJECTION INPUT "
            "REGULAR_SHA256 GUARDED_SHA256 ACTION_SHA256 "
            "SEMANTIC_MASK_SHA256 "
            "WARMUP_ITERATIONS MEASURED_ITERATIONS\n",
            argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
             argv[7], argv[8], argv[9], argv[10], argv[11], argv[12],
             argv[13],
             warmup_iterations, measured_iterations);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return ok ? 0 : 1;
}
