#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_native_v1.h"

#include "lib_parse_native_grammar.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>

static bool run_stream(const char *path) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    CettaLpNativeGrammar grammar;
    CettaLpNativeSlrSummary summary;
    int32_t start;
    char error[512] = {0};
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    cetta_lp_native_grammar_init(&grammar);
    if (!ppabi_v1_wire_read(&wire, path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, sizeof(error))) {
        fprintf(stderr, "ParserPack SLR input rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    start = ppnative_v1_state_find(&pack, wire.start);
    if (start < 0) {
        fprintf(stderr, "ParserPack SLR start state is absent\n");
        goto done;
    }
    if (!ppnative_v1_grammar_build(&pack, &grammar) ||
        !cetta_lp_native_slr_summary(
            &grammar, (SymbolId)start, &summary, error, sizeof(error))) {
        fprintf(stderr, "ParserPack SLR construction rejected: %s\n",
                error[0] ? error : "unknown rejection");
        goto done;
    }
    printf("(ParserPackSLRSummaryV1 %u %u %u %u %u %u %u %u %u 0)\n",
           pack.state_len, pack.production_len, (uint32_t)start,
           summary.state_len, summary.shift_len, summary.goto_len,
           summary.reduce_len, summary.accept_len, summary.conflict_len);
    ok = true;

done:
    cetta_lp_native_grammar_free(&grammar);
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
