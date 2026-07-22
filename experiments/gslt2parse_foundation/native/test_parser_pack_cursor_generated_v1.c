#define _POSIX_C_SOURCE 200809L

#include "finite_horn_ground_term_v1.h"
#include "parser_pack_guarded_lexical_exec_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PP_CURSOR_GENERATED_PREFIX
#error "PP_CURSOR_GENERATED_PREFIX must name the generated program"
#endif

#define PP_CURSOR_JOIN_RAW(left, right) left##right
#define PP_CURSOR_JOIN(left, right) PP_CURSOR_JOIN_RAW(left, right)
#define PP_CURSOR_GENERATED_INIT \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, _program_init)
#define PP_CURSOR_GENERATED_DIGEST \
    PP_CURSOR_JOIN(PP_CURSOR_GENERATED_PREFIX, _program_digest)

extern bool PP_CURSOR_GENERATED_INIT(
    PPGuardedLexCursorV1Program *out,
    char *error_buf,
    size_t error_buf_size);
extern const char *PP_CURSOR_GENERATED_DIGEST(void);

static bool read_input(const char *path, uint8_t **out, size_t *out_len) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    if (!path || !out || !out_len)
        return false;
    *out = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input)
        goto done;
    for (;;) {
        size_t amount;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap)
                goto done;
            next = realloc(bytes, next_cap);
            if (!next)
                goto done;
            bytes = next;
            cap = next_cap;
        }
        amount = fread(bytes + len, 1u, cap - len, input);
        len += amount;
        if (amount > 0u)
            continue;
        if (ferror(input))
            goto done;
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

static const char *outcome_name(PPGuardedLexCursorV1Outcome outcome) {
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

static bool write_term(const Atom *term) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    bool ok;

    if (!term)
        return true;
    if (!fh_ground_term_v1_render(
            term, &canonical, &canonical_len, NULL, 0u)) {
        return false;
    }
    ok = printf("semantic-result\t") >= 0 &&
        fwrite(canonical, 1u, canonical_len, stdout) == canonical_len &&
        printf("\n") >= 0;
    free(canonical);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    PPGuardedLexCursorV1Program program;
    PPGuardedLexCursorV1SemanticResult result;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    char error[512] = {0};
    uint32_t saved_transition_begin = 0u;
    uint32_t mutation_killed = 0u;
    bool ok = false;

    if (argc != 2) {
        fprintf(stderr, "usage: generated-cursor-test INPUT\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ppguarded_lex_cursor_v1_program_init(&program);
    ppguarded_lex_cursor_v1_semantic_result_init(&result);
    if (!read_input(argv[1], &input, &input_len) ||
        !PP_CURSOR_GENERATED_INIT(&program, error, sizeof(error)) ||
        strcmp(PP_CURSOR_GENERATED_DIGEST(), program.program_digest) != 0 ||
        !ppguarded_lex_cursor_v1_program_run_semantic_direct_bytes(
            &program, input, input_len, UINT64_C(50000000),
            &result, error, sizeof(error))) {
        goto done;
    }
    if (program.dfa.state_len == 0u || !program.dfa.states)
        goto done;
    saved_transition_begin = program.dfa.states[0].transition_begin;
    program.dfa.states[0].transition_begin ^= UINT32_C(1);
    if (!ppguarded_lex_cursor_v1_program_validate(
            &program, error, sizeof(error))) {
        mutation_killed = 1u;
    }
    program.dfa.states[0].transition_begin = saved_transition_begin;
    if (!mutation_killed ||
        !ppguarded_lex_cursor_v1_program_validate(
            &program, error, sizeof(error))) {
        goto done;
    }

    printf("parser-pack-cursor-generated-v1\n");
    printf("program-digest\t%s\n", program.program_digest);
    printf("outcome\t%s\n", outcome_name(result.receipt.outcome));
    printf("trace-digest\t%s\n", result.receipt.trace_digest);
    printf("source-passes\t%u\n", result.receipt.source_pass_count);
    printf("tokens\t%u\n", result.receipt.token_len);
    printf("shifts\t%u\n", result.receipt.shift_len);
    printf("reductions\t%u\n", result.receipt.reduce_len);
    printf("value-program-runs\t%u\n", result.value_program_run_len);
    printf("mutation-killed\t%u\n", mutation_killed);
    if (!write_term(result.semantic_result))
        goto done;
    printf("end\n");
    ok = true;

done:
    if (!ok)
        fprintf(stderr, "generated cursor test failed: %s\n",
                error[0] ? error : "unknown failure");
    free(input);
    ppguarded_lex_cursor_v1_semantic_result_free(&result);
    ppguarded_lex_cursor_v1_program_free(&program);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
