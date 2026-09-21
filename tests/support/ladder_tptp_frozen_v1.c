/* Stage timings for the frozen per-TPTP_input read path. */
#include "native/tptp_official_snapshot_v1.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double monotonic_s(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static bool visit_keep(Atom *input, void *user) {
    (void)input;
    (void)user;
    return true;
}

static long rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    return ru.ru_maxrss;
}

static int run_one(const CettaTptpPreparedReaderV1 *reader,
                   uint64_t gll_descriptor_limit, Arena *arena,
                   const char *path, FILE *tsv) {
    FILE *file;
    long size;
    char *text;
    size_t got;
    char error[512] = {0};
    CettaTptpReadCostV1 cost;
    ArenaMark mark;
    double t0;
    double wall;
    int ok;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(tsv, "%s\t0\tfail\tcannot-open\t0\t0\t0\t0\t0\t0\t0\t0\t0\t%ld\n",
                path, rss_kb());
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fclose(file);
        return 0;
    }
    rewind(file);
    text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return 0;
    }
    got = fread(text, 1u, (size_t)size, file);
    fclose(file);
    text[got] = '\0';
    mark = arena_mark(arena);
    t0 = monotonic_s();
    ok = cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
        reader, text, got, 0u, gll_descriptor_limit, arena,
        visit_keep, NULL, &cost, error, sizeof(error));
    wall = monotonic_s() - t0;
    fprintf(tsv,
            "%s\t%ld\t%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%llu\t%u\t%u\t%.6f\t%ld\n",
            path, size, ok ? "ok" : "fail",
            ok ? "-" : (error[0] ? error : "TPTP:NoParse"), cost.lex_s,
            cost.parse_s, cost.project_s, cost.combine_s, wall,
            (unsigned long long)cost.gll_descriptor_count, cost.token_count,
            cost.input_count, size > 0 && wall > 0.0
                                  ? ((double)size / 1048576.0) / wall
                                  : 0.0,
            rss_kb());
    arena_reset(arena, mark);
    free(text);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    CettaTptpPreparedReaderV1 reader;
    char error[512] = {0};
    double t0;
    uint64_t gll_descriptor_limit = 0u;
    int files_at = 2;
    int i;
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s SNAPSHOT.tpp1 [--gll-descriptor-limit N] FILE...\n",
                argv[0]);
        return 2;
    }
    if (argc >= 5 && strcmp(argv[2], "--gll-descriptor-limit") == 0) {
        char *end = NULL;
        unsigned long long parsed = strtoull(argv[3], &end, 10);
        if (!end || *end != '\0' || parsed == 0u) {
            fprintf(stderr, "invalid --gll-descriptor-limit: %s\n", argv[3]);
            return 2;
        }
        gll_descriptor_limit = (uint64_t)parsed;
        files_at = 4;
    }
    if (files_at >= argc) {
        fprintf(stderr, "no input files\n");
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    cetta_tptp_prepared_reader_init_v1(&reader);
    t0 = monotonic_s();
    if (!cetta_tptp_prepared_reader_load_v1(
            &reader, argv[1], CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
            error, sizeof(error))) {
        fprintf(stderr, "load failed: %s\n", error);
        cetta_tptp_prepared_reader_free_v1(&reader);
        arena_free(&arena);
        symbol_table_free(&symbols);
        g_symbols = NULL;
        return 1;
    }
    fprintf(stderr, "load_s=%.6f rss_kb=%ld\n", monotonic_s() - t0, rss_kb());
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("path\tbytes\tstatus\twitness\tlex_s\tparse_s\tproject_s\tcombine_s\t"
           "wall_s\tgll_descriptors\ttokens\tinputs\tMBps\trss_kb\n");
    for (i = files_at; i < argc; i++)
        run_one(&reader, gll_descriptor_limit, &arena, argv[i], stdout);
    cetta_tptp_prepared_reader_free_v1(&reader);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
