#include "native/tptp_official_snapshot_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned passed;
    unsigned failed;
} TestCounts;

static void expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    ArenaMark mark;
    CettaTptpPreparedReaderV1 reader;
    CettaTptpReadOutcomeV1 outcome;
    TestCounts counts = {0};
    Atom *records = NULL;
    char error[512] = {0};
    bool loaded;
    bool read;

    if (argc != 4) {
        fprintf(stderr, "usage: %s SNAPSHOT POSITIVE.p MALFORMED.p\n", argv[0]);
        return 2;
    }

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    cetta_tptp_prepared_reader_init_v1(&reader);

    loaded = cetta_tptp_prepared_reader_load_bound_v1(
        &reader, argv[1], CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
        "corpus-compatible", NULL, error, sizeof(error));
    expect(&counts, loaded,
           error[0] ? error : "load corpus-compatible snapshot");

    if (loaded) {
        mark = arena_mark(&arena);
        memset(&outcome, 0, sizeof(outcome));
        error[0] = '\0';
        read = cetta_tptp_prepared_reader_read_file_outcome_v1(
            &reader, argv[2], &arena, &records, &outcome,
            error, sizeof(error));
        expect(&counts,
               read && outcome.status == CETTA_TPTP_READ_OK_V1 &&
                   records != NULL,
               error[0] ? error
                        : "parenthesized CNF compatibility is uniquely readable");
        arena_reset(&arena, mark);

        records = NULL;
        memset(&outcome, 0, sizeof(outcome));
        error[0] = '\0';
        read = cetta_tptp_prepared_reader_read_file_outcome_v1(
            &reader, argv[3], &arena, &records, &outcome,
            error, sizeof(error));
        expect(&counts,
               !read && outcome.status == CETTA_TPTP_READ_NO_PARSE_V1 &&
                   records == NULL,
               error[0] ? error
                        : "malformed parenthesized CNF remains rejected");
    }

    printf("(TptpCorpusCompatibilitySnapshotV1Summary %u %u)\n",
           counts.passed, counts.failed);

    cetta_tptp_prepared_reader_free_v1(&reader);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
