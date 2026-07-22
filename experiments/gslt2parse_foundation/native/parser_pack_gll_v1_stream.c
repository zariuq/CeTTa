#include "finite_horn_ground_term_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_gll_v1.h"

#include "symbol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_limit(const char *text, uint32_t *out) {
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value == 0u ||
        value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool read_input(const char *path,
                       uint8_t **out_bytes,
                       size_t *out_len,
                       char *error_buf,
                       size_t error_buf_size) {
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool ok = false;

    *out_bytes = NULL;
    *out_len = 0u;
    input = fopen(path, "rb");
    if (!input) {
        (void)snprintf(error_buf, error_buf_size,
                       "cannot open parser input");
        goto done;
    }
    for (;;) {
        size_t read;
        if (len == cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            uint8_t *next;
            if (next_cap < cap) {
                (void)snprintf(error_buf, error_buf_size,
                               "parser input is too large");
                goto done;
            }
            next = realloc(bytes, next_cap);
            if (!next) {
                (void)snprintf(error_buf, error_buf_size,
                               "cannot allocate parser input");
                goto done;
            }
            bytes = next;
            cap = next_cap;
        }
        read = fread(bytes + len, 1u, cap - len, input);
        len += read;
        if (read > 0u)
            continue;
        if (ferror(input)) {
            (void)snprintf(error_buf, error_buf_size,
                           "failed while reading parser input");
            goto done;
        }
        break;
    }
    *out_bytes = bytes;
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
        return "descriptor-limit";
    case PPNATIVE_V1_REPLAY_DEPTH:
        return "replay-depth";
    case PPNATIVE_V1_RESULT_LIMIT:
        return "result-limit";
    }
    return "unknown";
}

static bool write_result_term(Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return false;
    if (fputs("result\t", stdout) == EOF ||
        (len > 0u && fwrite(bytes, 1u, len, stdout) != len) ||
        fputc('\n', stdout) == EOF) {
        free(bytes);
        return false;
    }
    free(bytes);
    return true;
}

static bool run(const char *abi_path,
                const char *start_text,
                const char *input_path,
                uint32_t descriptor_limit,
                uint32_t replay_depth,
                uint32_t result_limit) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    PPNativeV1Result result;
    Arena start_arena;
    Atom *start = NULL;
    uint8_t *input = NULL;
    size_t input_len = 0u;
    uint32_t result_index;
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    ppnative_v1_result_init(&result);
    arena_init(&start_arena);
    if (!ppabi_v1_wire_read(
            &wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &wire, &pack, error, sizeof(error)) ||
        !fh_ground_term_v1_parse(
            &start_arena, (const uint8_t *)start_text,
            strlen(start_text), &start, error, sizeof(error)) ||
        !read_input(input_path, &input, &input_len,
                    error, sizeof(error)) ||
        !ppgll_v1_parse(
            &pack, start, input, input_len, descriptor_limit,
            replay_depth, result_limit,
            &result, error, sizeof(error))) {
        fprintf(stderr, "ParserPack GLL stream rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    printf("parser-pack-gll-stream-v1\n");
    printf("outcome\t%s\n", outcome_name(result.outcome));
    if (result.outcome == PPNATIVE_V1_COMPLETED) {
        printf("decision\t%s\n", result.accepted ? "accepted" : "rejected");
        printf("forest-digest\t%s\n", result.forest_digest);
        printf("forest-materialized\t%u\n",
               result.canonical_forest_materialized ? 1u : 0u);
        printf("forest-nodes\t%u\n", result.forest.node_len);
        printf("forest-choices\t%u\n", result.forest.choice_len);
        printf("forest-roots\t%u\n", result.forest.root_len);
        printf("input-bytes\t%u\n", result.forest.input_byte_len);
        printf("input-scalars\t%u\n", result.forest.scalar_len);
        printf("decoded-bytes\t%u\n", result.forest.decoded_byte_len);
        printf("source-passes\t%u\n", result.forest.source_pass_count);
        printf("farthest-scalar\t%u\n", result.forest.farthest_scalar);
        printf("farthest-byte\t%u\n", result.forest.farthest_byte);
        printf("descriptors\t%u\n", result.forest.work_item_len);
        printf("gss-nodes\t%u\n", result.forest.graph_node_len);
        for (result_index = 0u;
             result_index < result.semantic_result_len; result_index++) {
            if (!write_result_term(result.semantic_results[result_index])) {
                fprintf(stderr, "failed to write semantic result\n");
                goto done;
            }
        }
    } else if (result.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT ||
               result.outcome == PPNATIVE_V1_REPLAY_DEPTH ||
               result.outcome == PPNATIVE_V1_RESULT_LIMIT) {
        if (result.forest_digest[0] != '\0') {
            printf("forest-digest\t%s\n", result.forest_digest);
            printf("forest-materialized\t%u\n",
                   result.canonical_forest_materialized ? 1u : 0u);
            printf("forest-nodes\t%u\n", result.forest.node_len);
            printf("forest-choices\t%u\n", result.forest.choice_len);
            printf("forest-roots\t%u\n", result.forest.root_len);
        }
        printf("input-bytes\t%u\n", result.forest.input_byte_len);
        printf("input-scalars\t%u\n", result.forest.scalar_len);
        printf("decoded-bytes\t%u\n", result.forest.decoded_byte_len);
        printf("source-passes\t%u\n", result.forest.source_pass_count);
        printf("farthest-scalar\t%u\n", result.forest.farthest_scalar);
        printf("farthest-byte\t%u\n", result.forest.farthest_byte);
        printf("descriptors\t%u\n", result.forest.work_item_len);
        printf("gss-nodes\t%u\n", result.forest.graph_node_len);
    }
    printf("end\n");
    ok = true;

done:
    free(input);
    arena_free(&start_arena);
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    uint32_t descriptor_limit = 2000000u;
    uint32_t replay_depth = 4096u;
    uint32_t result_limit = 65536u;
    bool ok;

    if ((argc != 4 && argc != 7) ||
        (argc == 7 &&
         (!parse_limit(argv[4], &descriptor_limit) ||
          !parse_limit(argv[5], &replay_depth) ||
          !parse_limit(argv[6], &result_limit)))) {
        fprintf(stderr,
                "expected ABI stream, canonical start term, input file, "
                "and optional positive work/depth/result limits\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], descriptor_limit,
             replay_depth, result_limit);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
