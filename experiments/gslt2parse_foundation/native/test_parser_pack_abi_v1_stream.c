#include "parser_pack_abi_stream_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>

static bool run_stream(const char *path) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    if (!ppabi_v1_wire_read(
            &wire, path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(
            &wire, &pack, error, sizeof(error))) {
        fprintf(stderr, "ParserPack ABI stream rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    printf("(ParserPackABIV1StreamSummary %u %u %u %u 0)\n",
           pack.production_len, pack.class_clause_len,
           pack.derivation_len, wire.expected_closed ? 1u : 0u);
    ok = true;

done:
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 2) {
        fprintf(stderr, "expected one ParserPack ABI stream\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run_stream(argv[1]);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
