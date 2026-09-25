/* Stage timings for the frozen TPTP read path: lexing, parsing, projection
 * and combination of each input, then the compact morphism under the mixed
 * leaf policy and the printer of its records, with a check that the printed
 * text reads back to the same records.  The later stages run on each input as
 * the read yields it; wall_s is the read alone. */
#include "native/tptp_official_snapshot_v1.h"
#include "parser.h"
#include "symbol.h"
#include "tests/support/tptp_compact_stages_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

static long rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    return ru.ru_maxrss;
}

/* A failed stage stops the stages, not the read. */
static bool visit_stages(Atom *input, void *user) {
    (void)tptp_stage_run_input_v1((TptpStageRunV1 *)user, input);
    return true;
}

static int run_one(const CettaTptpPreparedReaderV1 *reader,
                   const TptpCompactStagesV1 *stages,
                   uint64_t gll_descriptor_limit, Arena *arena,
                   const char *path, FILE *tsv) {
    FILE *file;
    long size;
    char *text;
    size_t got;
    char error[512] = {0};
    CettaTptpReadCostV1 cost;
    TptpStageRunV1 run;
    const TptpStageCostV1 *stage = &run.cost;
    ArenaMark mark;
    double t0;
    double wall;
    int ok;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(tsv, "%s\t0\tfail\tcannot-open\t0\t0\t0\t0\t0\t0\t0\t0\t0\t%ld"
                "\t-\t0\t0\t0\t0\t0\n", path, rss_kb());
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
    memset(&cost, 0, sizeof(cost));
    tptp_stage_run_init_v1(&run, reader, stages, arena);
    t0 = tptp_stages_clock_v1();
    ok = cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
        reader, text, got, 0u, gll_descriptor_limit, arena, visit_stages,
        &run, &cost, error, sizeof(error));
    wall = tptp_stages_clock_v1() - t0 - tptp_stage_run_seconds_v1(&run);
    fprintf(tsv,
            "%s\t%ld\t%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%llu\t%u\t%u\t%.6f\t%ld"
            "\t%s\t%.6f\t%.6f\t%.6f\t%zu\t%zu\n",
            path, size, ok ? "ok" : "fail",
            ok ? (stage->result == TPTP_STAGES_OK_V1 || !stage->error[0]
                      ? "-" : stage->error)
               : (error[0] ? error : "TPTP:NoParse"),
            cost.lex_s, cost.parse_s, cost.project_s, cost.combine_s, wall,
            (unsigned long long)cost.gll_descriptor_count, cost.token_count,
            cost.input_count, size > 0 && wall > 0.0
                                  ? ((double)size / 1048576.0) / wall
                                  : 0.0,
            rss_kb(), ok ? tptp_stage_result_name_v1(stage->result) : "-",
            stage->compact_s, stage->print_s, stage->reread_s, stage->records,
            stage->printed_bytes);
    arena_reset(arena, mark);
    free(text);
    return ok && stage->result == TPTP_STAGES_OK_V1;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    CettaTptpPreparedReaderV1 reader;
    TptpCompactStagesV1 stages;
    char error[512] = {0};
    double t0;
    uint64_t gll_descriptor_limit = 0u;
    int files_at = 2;
    int failures = 0;
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
    t0 = tptp_stages_clock_v1();
    if (!cetta_tptp_prepared_reader_load_v1(
            &reader, argv[1], CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
            error, sizeof(error)) ||
        !tptp_compact_stages_load_v1(&stages, argv[1], error, sizeof(error))) {
        fprintf(stderr, "load failed: %s\n", error);
        cetta_tptp_prepared_reader_free_v1(&reader);
        arena_free(&arena);
        symbol_table_free(&symbols);
        g_symbols = NULL;
        return 1;
    }
    fprintf(stderr, "load_s=%.6f rss_kb=%ld\n", tptp_stages_clock_v1() - t0,
            rss_kb());
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("path\tbytes\tstatus\twitness\tlex_s\tparse_s\tproject_s\tcombine_s\t"
           "wall_s\tgll_descriptors\ttokens\tinputs\tMBps\trss_kb\t"
           "stages\tcompact_s\tprint_s\treread_s\trecords\tprinted_bytes\n");
    for (i = files_at; i < argc; i++) {
        if (!run_one(&reader, &stages, gll_descriptor_limit, &arena, argv[i],
                     stdout))
            failures++;
    }
    tptp_compact_stages_free_v1(&stages);
    cetta_tptp_prepared_reader_free_v1(&reader);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures ? 1 : 0;
}
