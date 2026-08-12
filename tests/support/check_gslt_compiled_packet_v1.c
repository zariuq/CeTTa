#include "atom.h"
#include "gslt_compiled_runtime.h"
#include "native_sha256.h"
#include "symbol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { ERROR_CAP = 1024 };

static uint8_t *read_packet(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    long raw_length;
    uint8_t *bytes;

    if (!stream || fseek(stream, 0, SEEK_END) != 0 ||
        (raw_length = ftell(stream)) < 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        if (stream)
            fclose(stream);
        return NULL;
    }
    *length = (size_t)raw_length;
    if ((long)*length != raw_length || *length == 0u ||
        !(bytes = malloc(*length))) {
        fclose(stream);
        return NULL;
    }
    if (fread(bytes, 1u, *length, stream) != *length) {
        free(bytes);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    return bytes;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s COMPILED_PLAN\n", argv[0]);
        return 2;
    }

    size_t length = 0u;
    uint8_t *bytes = read_packet(argv[1], &length);
    if (!bytes) {
        fprintf(stderr, "cannot read compiled GSLT packet\n");
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    var_intern_init(&variables);
    g_var_intern = &variables;

    char digest[65];
    char error[ERROR_CAP] = {0};
    CettaGsltCompiledProgram *program = NULL;
    CettaGsltCompiledIndexStatsV1 stats = {0};
    cetta_native_sha256_hex(bytes, length, digest);
    CettaGsltCompiledInputV1 input = {
        .bytes = bytes,
        .length = length,
        .sha256 = digest,
    };
    bool accepted = cetta_gslt_compiled_program_load_v1(
        &input, &program, error, sizeof(error));
    bool indexed = accepted &&
        cetta_gslt_compiled_program_index_stats_v1(program, &stats);

    int result = 0;
    if (!accepted || !indexed) {
        fprintf(stderr, "compiled GSLT packet rejected: %s\n", error);
        result = 1;
    } else {
        printf("(GsltCompiledPacketV1Accepted bytes=%zu rules=%zu "
               "buckets=%zu)\n",
               length, cetta_gslt_compiled_program_rule_count(program),
               stats.bucket_count);
    }

    cetta_gslt_compiled_program_free(program);
    g_symbols = NULL;
    g_var_intern = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    free(bytes);
    return result;
}
