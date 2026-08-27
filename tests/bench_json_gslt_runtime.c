#define _POSIX_C_SOURCE 200809L

#include "native/json_runtime_v1.h"
#include "symbol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t *read_source(const char *path, size_t *len_out) {
    FILE *file;
    long length;
    uint8_t *bytes;
    if (!path || !len_out || !(file = fopen(path, "rb"))) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length ? (size_t)length : 1u);
    if (!bytes || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len_out = (size_t)length;
    return bytes;
}

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec;
}

int main(int argc, char **argv) {
    static const char input[] =
        "{\"id\":42,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"shell\",\"arguments\":{\"cmd\":\"pwd\"}}}";
    static const CettaJsonKernelV1 kernels[] = {
        CETTA_JSON_KERNEL_V1_PACKED_GLL,
        CETTA_JSON_KERNEL_V1_PACKED_GLR,
        CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL,
    };
    uint32_t iterations = 100u;
    uint8_t *language_source = NULL;
    uint8_t *profile_source = NULL;
    uint8_t *target_source = NULL;
    size_t language_len = 0u;
    size_t profile_len = 0u;
    size_t target_len = 0u;
    CettaJsonRuntimeV1 *runtime = NULL;
    CettaJsonRuntimeV1Limits limits;
    SymbolTable symbols;
    Arena arena;
    uint64_t preparation_start;
    uint64_t preparation_end;
    uint64_t parse_start;
    uint64_t parse_end;
    uint32_t kernel_index;
    uint32_t index;
    char error[512] = {0};
    int exit_code = 1;

    if (argc == 2) {
        char *end = NULL;
        unsigned long parsed;
        errno = 0;
        parsed = strtoul(argv[1], &end, 10);
        if (errno != 0 || !end || *end != '\0' ||
            parsed == 0ul || parsed > UINT32_MAX) {
            fprintf(stderr, "invalid iteration count: %s\n", argv[1]);
            return 2;
        }
        iterations = (uint32_t)parsed;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [iterations]\n", argv[0]);
        return 2;
    }

    language_source = read_source(
        "langdef/json/rfc8259_syntax_v1.metta", &language_len);
    profile_source = read_source(
        "langdef/json/rfc8259_parser_profile_v1.metta", &profile_len);
    target_source = read_source(
        "langdef/json/occurrence_preserving_value_v1.metta", &target_len);
    if (!language_source || !profile_source || !target_source) {
        fprintf(stderr, "failed to read authored JSON sources\n");
        goto done_sources;
    }

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    preparation_start = monotonic_ns();
    runtime = cetta_json_runtime_v1_new(
        language_source, language_len, profile_source, profile_len,
        target_source, target_len,
        error, sizeof(error));
    preparation_end = monotonic_ns();
    if (!runtime) {
        fprintf(stderr, "JSON preparation failed: %s\n", error);
        goto done_symbols;
    }

    for (kernel_index = 0u;
         kernel_index < sizeof(kernels) / sizeof(kernels[0]);
         kernel_index++) {
        arena_init(&arena);
        cetta_json_runtime_v1_default_limits(&limits);
        limits.kernel = kernels[kernel_index];
        parse_start = monotonic_ns();
        for (index = 0u; index < iterations; index++) {
            ArenaMark mark = arena_mark(&arena);
            CettaJsonRuntimeV1Status status =
                CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
            Atom *value = NULL;
            error[0] = '\0';
            if (!cetta_json_runtime_v1_parse(
                    runtime, &arena, (const uint8_t *)input,
                    sizeof(input) - 1u, &limits, &value,
                    &status, error, sizeof(error)) ||
                status != CETTA_JSON_RUNTIME_V1_OK || !value ||
                value->kind != ATOM_EXPR || value->expr.len != 2u ||
                !atom_is_symbol(value->expr.elems[0], "JsonObjectV1")) {
                fprintf(
                    stderr,
                    "JSON %s parse failed at iteration %u: %s\n",
                    cetta_json_kernel_v1_name(limits.kernel), index, error);
                arena_free(&arena);
                goto done_runtime;
            }
            arena_reset(&arena, mark);
        }
        parse_end = monotonic_ns();
        arena_free(&arena);

        printf("(JsonGsltRuntimeBench %s %u %" PRIu64 " %" PRIu64
               " %" PRIu64 " %u)\n",
               cetta_json_kernel_v1_name(limits.kernel),
               iterations,
               preparation_end - preparation_start,
               parse_end - parse_start,
               (parse_end - parse_start) / iterations,
               cetta_json_runtime_v1_table_build_count(runtime));
    }
    exit_code = 0;

done_runtime:
    cetta_json_runtime_v1_free(runtime);
done_symbols:
    symbol_table_free(&symbols);
    g_symbols = NULL;
done_sources:
    free(target_source);
    free(profile_source);
    free(language_source);
    return exit_code;
}
