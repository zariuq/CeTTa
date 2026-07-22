#include "he_document_pipeline_v1.h"

#include "../native/finite_horn_ground_term_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    VarId id;
    uint32_t ordinal;
} HEStreamVariableV1;

typedef struct {
    HEStreamVariableV1 *data;
    uint32_t len;
    uint32_t cap;
} HEStreamVariableVecV1;

typedef struct {
    VarId left;
    VarId right;
} HEStreamVariablePairV1;

typedef struct {
    HEStreamVariablePairV1 *data;
    uint32_t len;
    uint32_t cap;
} HEStreamVariablePairVecV1;

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
        (void)snprintf(error, error_size, "cannot open pipeline input");
        goto done;
    }
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error, error_size,
                               "pipeline input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error, error_size,
                               "cannot allocate pipeline input");
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
                           "cannot read pipeline input");
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

static void write_hex(const uint8_t *bytes, uint32_t len) {
    static const char digits[] = "0123456789abcdef";
    uint32_t index;

    for (index = 0u; index < len; index++) {
        (void)putchar(digits[bytes[index] >> 4u]);
        (void)putchar(digits[bytes[index] & 0x0fu]);
    }
}

static bool variable_ordinal(HEStreamVariableVecV1 *variables,
                             VarId id,
                             uint32_t *out) {
    uint32_t index;

    for (index = 0u; index < variables->len; index++) {
        if (variables->data[index].id == id) {
            *out = variables->data[index].ordinal;
            return true;
        }
    }
    if (variables->len == variables->cap) {
        uint32_t cap = variables->cap ? variables->cap * 2u : 16u;
        HEStreamVariableV1 *next;
        if (cap < variables->cap)
            return false;
        next = realloc(variables->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        variables->data = next;
        variables->cap = cap;
    }
    variables->data[variables->len] = (HEStreamVariableV1){
        .id = id,
        .ordinal = variables->len,
    };
    *out = variables->len;
    variables->len++;
    return true;
}

static bool write_atom_json(Atom *atom,
                            HEStreamVariableVecV1 *variables,
                            uint32_t depth) {
    const uint8_t *name;
    uint32_t name_len;
    uint32_t ordinal;
    CettaExprIndex index;

    if (!atom || depth > 4096u)
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        name = (const uint8_t *)symbol_bytes(g_symbols, atom->sym_id);
        name_len = symbol_len(g_symbols, atom->sym_id);
        (void)printf("{\"kind\":\"sym\",\"hex\":\"");
        write_hex(name, name_len);
        (void)printf("\"}");
        return true;
    case ATOM_VAR:
        if (!variable_ordinal(variables, atom->var_id, &ordinal))
            return false;
        name = (const uint8_t *)symbol_bytes(g_symbols, atom->sym_id);
        name_len = symbol_len(g_symbols, atom->sym_id);
        (void)printf("{\"kind\":\"var\",\"ordinal\":%u,\"hex\":\"",
                     ordinal);
        write_hex(name, name_len);
        (void)printf("\"}");
        return true;
    case ATOM_EXPR:
        (void)printf("{\"kind\":\"expr\",\"children\":[");
        for (index = 0u; index < atom->expr.len; index++) {
            if (index > 0u)
                (void)putchar(',');
            if (!write_atom_json(
                    atom->expr.elems[index], variables, depth + 1u)) {
                return false;
            }
        }
        (void)printf("]}");
        return true;
    case ATOM_GROUNDED:
        return false;
    }
    return false;
}

static bool variable_pair(HEStreamVariablePairVecV1 *pairs,
                          VarId left,
                          VarId right) {
    uint32_t index;

    for (index = 0u; index < pairs->len; index++) {
        if (pairs->data[index].left == left ||
            pairs->data[index].right == right) {
            return pairs->data[index].left == left &&
                pairs->data[index].right == right;
        }
    }
    if (pairs->len == pairs->cap) {
        uint32_t cap = pairs->cap ? pairs->cap * 2u : 16u;
        HEStreamVariablePairV1 *next;
        if (cap < pairs->cap)
            return false;
        next = realloc(pairs->data, sizeof(*next) * (size_t)cap);
        if (!next)
            return false;
        pairs->data = next;
        pairs->cap = cap;
    }
    pairs->data[pairs->len++] = (HEStreamVariablePairV1){
        .left = left,
        .right = right,
    };
    return true;
}

static bool atoms_equal(Atom *left,
                        Atom *right,
                        HEStreamVariablePairVecV1 *pairs,
                        uint32_t depth) {
    CettaExprIndex index;
    uint32_t left_len;
    uint32_t right_len;

    if (!left || !right || left->kind != right->kind || depth > 4096u)
        return false;
    switch (left->kind) {
    case ATOM_SYMBOL:
        left_len = symbol_len(g_symbols, left->sym_id);
        right_len = symbol_len(g_symbols, right->sym_id);
        return left_len == right_len &&
            memcmp(symbol_bytes(g_symbols, left->sym_id),
                   symbol_bytes(g_symbols, right->sym_id), left_len) == 0;
    case ATOM_VAR:
        left_len = symbol_len(g_symbols, left->sym_id);
        right_len = symbol_len(g_symbols, right->sym_id);
        return left_len == right_len &&
            memcmp(symbol_bytes(g_symbols, left->sym_id),
                   symbol_bytes(g_symbols, right->sym_id), left_len) == 0 &&
            variable_pair(pairs, left->var_id, right->var_id);
    case ATOM_EXPR:
        if (left->expr.len != right->expr.len)
            return false;
        for (index = 0u; index < left->expr.len; index++) {
            if (!atoms_equal(left->expr.elems[index],
                             right->expr.elems[index],
                             pairs, depth + 1u)) {
                return false;
            }
        }
        return true;
    case ATOM_GROUNDED:
        return false;
    }
    return false;
}

static bool parse_receipts_equal_except_trace(
    const PPGuardedLexCursorV1Receipt *left,
    const PPGuardedLexCursorV1Receipt *right) {
    return left->outcome == right->outcome &&
        left->source_pass_count == right->source_pass_count &&
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
        left->dfa_work_item_len == right->dfa_work_item_len &&
        left->parser_work_item_len == right->parser_work_item_len;
}

static bool receipts_equal(const HEDocumentPipelineV1Receipt *left,
                           const HEDocumentPipelineV1Receipt *right) {
    return strcmp(left->base_pack_digest,
                  right->base_pack_digest) == 0 &&
        strcmp(left->lexical_plan_digest,
               right->lexical_plan_digest) == 0 &&
        strcmp(left->guard_plan_digest,
               right->guard_plan_digest) == 0 &&
        strcmp(left->guarded_plan_digest,
               right->guarded_plan_digest) == 0 &&
        strcmp(left->execution_plan_digest,
               right->execution_plan_digest) == 0 &&
        strcmp(left->cursor_certificate_digest,
               right->cursor_certificate_digest) == 0 &&
        strcmp(left->cursor_program_digest,
               right->cursor_program_digest) == 0 &&
        strcmp(left->action_compiler_digest,
               right->action_compiler_digest) == 0 &&
        strcmp(left->action_answer_digest,
               right->action_answer_digest) == 0 &&
        strcmp(left->action_program_digest,
               right->action_program_digest) == 0 &&
        strcmp(left->projection_action_program_digest,
               right->projection_action_program_digest) == 0 &&
        strcmp(left->semantic_mask_compiler_digest,
               right->semantic_mask_compiler_digest) == 0 &&
        strcmp(left->semantic_mask_answer_digest,
               right->semantic_mask_answer_digest) == 0 &&
        strcmp(left->semantic_mask_binding_digest,
               right->semantic_mask_binding_digest) == 0 &&
        strcmp(left->atom_projection_source_digest,
               right->atom_projection_source_digest) == 0 &&
        strcmp(left->atom_projection_reader_digest,
               right->atom_projection_reader_digest) == 0 &&
        strcmp(left->atom_projection_compiler_digest,
               right->atom_projection_compiler_digest) == 0 &&
        strcmp(left->atom_projection_plan_digest,
               right->atom_projection_plan_digest) == 0 &&
        left->projection_action_production_len ==
            right->projection_action_production_len &&
        left->semantic_mask_bound_terminal_len ==
            right->semantic_mask_bound_terminal_len &&
        left->prepared_build_count == right->prepared_build_count &&
        left->terminal_value_len == right->terminal_value_len &&
        left->action_execution_len == right->action_execution_len &&
        left->action_instruction_len == right->action_instruction_len &&
        left->value_program_run_len == right->value_program_run_len &&
        left->value_parser_work_item_len ==
            right->value_parser_work_item_len &&
        left->semantic_mask_run_len == right->semantic_mask_run_len &&
        left->semantic_mask_input_scalar_len ==
            right->semantic_mask_input_scalar_len &&
        left->semantic_mask_retained_scalar_len ==
            right->semantic_mask_retained_scalar_len &&
        left->semantic_mask_work_item_len ==
            right->semantic_mask_work_item_len &&
        left->prepared_fallback_run_len ==
            right->prepared_fallback_run_len &&
        left->prepared_fallback_work_item_len ==
            right->prepared_fallback_work_item_len &&
        parse_receipts_equal_except_trace(&left->parse, &right->parse);
}

static bool results_equal(const HEDocumentPipelineV1Result *left,
                          const HEDocumentPipelineV1Result *right) {
    uint32_t index;

    if (left->accepted != right->accepted ||
        left->atom_len != right->atom_len ||
        !receipts_equal(&left->receipt, &right->receipt)) {
        return false;
    }
    for (index = 0u; index < left->atom_len; index++) {
        HEStreamVariablePairVecV1 pairs = {0};
        bool equal = atoms_equal(
            left->atoms[index], right->atoms[index], &pairs, 1u);
        free(pairs.data);
        if (!equal)
            return false;
    }
    return true;
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
                const char *semantic_mask_compiler_digest) {
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
    uint8_t *projection_bytes = NULL;
    size_t projection_len = 0u;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    Arena plan_arena;
    Atom *projection = NULL;
    HEDocumentPipelineV1Prepared *prepared = NULL;
    HEDocumentPipelineV1Result first;
    HEDocumentPipelineV1Result second;
    char error[512] = {0};
    uint32_t index;
    bool ok = false;

    arena_init(&plan_arena);
    he_document_pipeline_v1_result_init(&first);
    he_document_pipeline_v1_result_init(&second);
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
        !he_document_pipeline_v1_parse_audited(
            prepared, input, input_len, &first, error, sizeof(error)) ||
        !he_document_pipeline_v1_parse(
            prepared, input, input_len, &second, error, sizeof(error)) ||
        he_document_pipeline_v1_prepared_build_count(prepared) != 1u ||
        strlen(first.receipt.parse.trace_digest) != 64u ||
        second.receipt.parse.trace_digest[0] != '\0' ||
        !results_equal(&first, &second)) {
        if (!error[0])
            (void)snprintf(error, sizeof(error),
                           "prepared replay changed its observation");
        goto rejected;
    }

    (void)printf("he-document-pipeline-v1\n");
    (void)printf("base-pack-digest\t%s\n",
                 first.receipt.base_pack_digest);
    (void)printf("lexical-plan-digest\t%s\n",
                 first.receipt.lexical_plan_digest);
    (void)printf("guard-plan-digest\t%s\n",
                 first.receipt.guard_plan_digest);
    (void)printf("guarded-plan-digest\t%s\n",
                 first.receipt.guarded_plan_digest);
    (void)printf("execution-plan-digest\t%s\n",
                 first.receipt.execution_plan_digest);
    (void)printf("cursor-certificate-digest\t%s\n",
                 first.receipt.cursor_certificate_digest);
    (void)printf("cursor-program-digest\t%s\n",
                 first.receipt.cursor_program_digest);
    (void)printf("action-compiler-digest\t%s\n",
                 first.receipt.action_compiler_digest);
    (void)printf("action-answer-digest\t%s\n",
                 first.receipt.action_answer_digest);
    (void)printf("action-program-digest\t%s\n",
                 first.receipt.action_program_digest);
    (void)printf("projection-action-program-digest\t%s\n",
                 first.receipt.projection_action_program_digest);
    (void)printf("semantic-mask-compiler-digest\t%s\n",
                 first.receipt.semantic_mask_compiler_digest);
    (void)printf("semantic-mask-answer-digest\t%s\n",
                 first.receipt.semantic_mask_answer_digest);
    (void)printf("semantic-mask-binding-digest\t%s\n",
                 first.receipt.semantic_mask_binding_digest);
    (void)printf("cursor-trace-digest\t%s\n",
                 first.receipt.parse.trace_digest);
    (void)printf("atom-projection-source-digest\t%s\n",
                 first.receipt.atom_projection_source_digest);
    (void)printf("atom-projection-reader-digest\t%s\n",
                 first.receipt.atom_projection_reader_digest);
    (void)printf("atom-projection-compiler-digest\t%s\n",
                 first.receipt.atom_projection_compiler_digest);
    (void)printf("atom-projection-plan-digest\t%s\n",
                 first.receipt.atom_projection_plan_digest);
    (void)printf("projection-action-productions\t%u\n",
                 first.receipt.projection_action_production_len);
    (void)printf("semantic-mask-bound-terminals\t%u\n",
                 first.receipt.semantic_mask_bound_terminal_len);
    (void)printf("prepared-builds\t%u\n",
                 first.receipt.prepared_build_count);
    (void)printf("source-passes\t%u\n",
                 first.receipt.parse.source_pass_count);
    (void)printf("prepared-fallback-runs\t%u\n",
                 first.receipt.prepared_fallback_run_len);
    (void)printf("input-bytes\t%u\n",
                 first.receipt.parse.input_byte_len);
    (void)printf("input-scalars\t%u\n",
                 first.receipt.parse.input_scalar_len);
    (void)printf("projected-tokens\t%u\n",
                 first.receipt.parse.token_len);
    (void)printf("value-program-runs\t%u\n",
                 first.receipt.value_program_run_len);
    (void)printf("semantic-mask-runs\t%u\n",
                 first.receipt.semantic_mask_run_len);
    (void)printf("semantic-mask-input-scalars\t%u\n",
                 first.receipt.semantic_mask_input_scalar_len);
    (void)printf("semantic-mask-retained-scalars\t%u\n",
                 first.receipt.semantic_mask_retained_scalar_len);
    (void)printf("semantic-mask-work\t%llu\n",
                 (unsigned long long)
                     first.receipt.semantic_mask_work_item_len);
    (void)printf("decision\t%s\n",
                 first.accepted ? "accepted" : "rejected");
    (void)printf("atom-count\t%u\n", first.atom_len);
    for (index = 0u; index < first.atom_len; index++) {
        HEStreamVariableVecV1 variables = {0};
        (void)printf("atom\t");
        if (!write_atom_json(first.atoms[index], &variables, 1u)) {
            free(variables.data);
            (void)snprintf(error, sizeof(error),
                           "cannot observe projected host Atom");
            goto rejected;
        }
        free(variables.data);
        (void)putchar('\n');
    }
    (void)printf("replay-agreement\t1\n");
    (void)printf("end\n");
    ok = true;
    goto done;

rejected:
    (void)fprintf(stderr, "HE document pipeline rejected: %s\n",
                  error[0] ? error : "unknown rejection");

done:
    he_document_pipeline_v1_result_free(&second);
    he_document_pipeline_v1_result_free(&first);
    he_document_pipeline_v1_prepared_free(prepared);
    arena_free(&plan_arena);
    free(input);
    free(projection_bytes);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 14) {
        (void)fprintf(
            stderr,
            "usage: %s ABI LEXICAL_NFA GUARD_NFA GUARD_EVIDENCE "
            "GUARDED_NFA ACTION_ANSWERS SEMANTIC_MASK PROJECTION INPUT "
            "REGULAR_SHA256 GUARDED_SHA256 ACTION_SHA256 "
            "SEMANTIC_MASK_SHA256\n",
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
             argv[13]);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return ok ? 0 : 1;
}
