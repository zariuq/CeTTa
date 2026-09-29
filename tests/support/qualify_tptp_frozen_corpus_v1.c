#define _POSIX_C_SOURCE 200809L

/* Frozen-path corpus walk of Problems .p files, Axioms .ax files and
 * optional TSTP files.  Each input of a file goes through the compact
 * morphism under the mixed leaf policy and the printer as the read yields it,
 * and the printed text must read back to the same records.  Every row carries
 * the time of each stage. */
#include "native/tptp_official_snapshot_v1.h"
#include "parser.h"
#include "symbol.h"
#include "tests/support/tptp_compact_stages_v1.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
    uint32_t unprojected;
    uint32_t skipped;
    uint32_t files;
    uint32_t cnf, fof, tff, thf, tcf, tpi, other, tstp, ax;
    uint32_t compacted;
    uint32_t printed;
    uint32_t round_trip;
    uint32_t stage_failures[TPTP_STAGES_ROUND_TRIP_DIFFERS_V1 + 1];
    double read_s;
    double compact_s;
    double print_s;
    double reread_s;
} Totals;

typedef struct {
    char **slots;
    size_t capacity;
    size_t length;
} PathSet;

static uint64_t path_hash(const char *text) {
    uint64_t hash = UINT64_C(1469598103934665603);
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        hash ^= (uint64_t)*p++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int path_set_rehash(PathSet *set, size_t capacity) {
    char **slots = calloc(capacity, sizeof(*slots));
    size_t i;
    if (!slots)
        return 0;
    for (i = 0u; i < set->capacity; i++) {
        char *path = set->slots[i];
        size_t at;
        if (!path)
            continue;
        at = (size_t)(path_hash(path) & (uint64_t)(capacity - 1u));
        while (slots[at])
            at = (at + 1u) & (capacity - 1u);
        slots[at] = path;
    }
    free(set->slots);
    set->slots = slots;
    set->capacity = capacity;
    return 1;
}

static int path_set_insert(PathSet *set, const char *path) {
    size_t at;
    char *copy;
    if (set->capacity == 0u && !path_set_rehash(set, 1024u))
        return 0;
    if ((set->length + 1u) * 3u >= set->capacity * 2u &&
        !path_set_rehash(set, set->capacity * 2u))
        return 0;
    at = (size_t)(path_hash(path) & (uint64_t)(set->capacity - 1u));
    while (set->slots[at]) {
        if (strcmp(set->slots[at], path) == 0)
            return 1;
        at = (at + 1u) & (set->capacity - 1u);
    }
    copy = malloc(strlen(path) + 1u);
    if (!copy)
        return 0;
    strcpy(copy, path);
    set->slots[at] = copy;
    set->length++;
    return 1;
}

static int path_set_contains(const PathSet *set, const char *path) {
    size_t at;
    if (!set || set->capacity == 0u)
        return 0;
    at = (size_t)(path_hash(path) & (uint64_t)(set->capacity - 1u));
    while (set->slots[at]) {
        if (strcmp(set->slots[at], path) == 0)
            return 1;
        at = (at + 1u) & (set->capacity - 1u);
    }
    return 0;
}

static void path_set_free(PathSet *set) {
    size_t i;
    for (i = 0u; i < set->capacity; i++)
        free(set->slots[i]);
    free(set->slots);
    memset(set, 0, sizeof(*set));
}

static int path_set_load_tsv(PathSet *set, const char *path) {
    FILE *file = fopen(path, "rb");
    char *line = NULL;
    size_t capacity = 0u;
    ssize_t length;
    int first = 1;
    if (!file)
        return 0;
    while ((length = getline(&line, &capacity, file)) >= 0) {
        char *tab;
        if (first) {
            first = 0;
            if (strncmp(line, "path\t", 5u) == 0)
                continue;
        }
        tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = '\0';
        if (*line && !path_set_insert(set, line)) {
            free(line);
            fclose(file);
            return 0;
        }
    }
    free(line);
    fclose(file);
    return 1;
}

static bool atom_has_unprojected(Atom *a) {
    if (!a)
        return false;
    if (a->kind == ATOM_SYMBOL && atom_is_symbol(a, "TPTP:Unprojected"))
        return true;
    if (a->kind == ATOM_EXPR) {
        uint32_t i;
        for (i = 0u; i < (uint32_t)a->expr.len; i++) {
            if (atom_has_unprojected(a->expr.elems[i]))
                return true;
        }
    }
    return false;
}

static bool atom_has_unknown(Atom *a) {
    uint32_t i;
    if (!a)
        return false;
    if (a->kind == ATOM_EXPR && a->expr.len == 1u &&
        atom_is_symbol(a->expr.elems[0], "tptp-rec:unknown"))
        return true;
    if (a->kind == ATOM_EXPR) {
        for (i = 0u; i < (uint32_t)a->expr.len; i++) {
            if (atom_has_unknown(a->expr.elems[i]))
                return true;
        }
    }
    return false;
}

static const char *unprojected_name(Atom *a) {
    uint32_t i;
    if (!a)
        return NULL;
    if (a->kind == ATOM_EXPR && a->expr.len == 2u &&
        atom_is_symbol(a->expr.elems[0], "TPTP:Unprojected") &&
        a->expr.elems[1] && a->expr.elems[1]->kind == ATOM_GROUNDED &&
        a->expr.elems[1]->ground.gkind == GV_STRING)
        return a->expr.elems[1]->ground.sval;
    if (a->kind == ATOM_EXPR) {
        for (i = 0u; i < (uint32_t)a->expr.len; i++) {
            const char *n = unprojected_name(a->expr.elems[i]);
            if (n)
                return n;
        }
    }
    return NULL;
}

static const char *family_of(const char *path) {
    const char *base = strrchr(path, '/');
    const char *dot;
    const char *p;
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    if (!dot || dot == base)
        return "other";
    for (p = dot - 1; p >= base; p--) {
        if (*p == '-')
            return "cnf";
        if (*p == '+')
            return "fof";
        if (*p == '^')
            return "thf";
        if (*p == '_')
            return "tff";
        if (*p == '=')
            return "tcf";
        if (*p == '@')
            return "tpi";
    }
    return "other";
}

static void bump_family(Totals *t, const char *fam, int is_ax, int is_tstp) {
    if (is_tstp)
        t->tstp++;
    else if (is_ax)
        t->ax++;
    if (strcmp(fam, "cnf") == 0)
        t->cnf++;
    else if (strcmp(fam, "fof") == 0)
        t->fof++;
    else if (strcmp(fam, "tff") == 0)
        t->tff++;
    else if (strcmp(fam, "thf") == 0)
        t->thf++;
    else if (strcmp(fam, "tcf") == 0)
        t->tcf++;
    else if (strcmp(fam, "tpi") == 0)
        t->tpi++;
    else
        t->other++;
}

/* The stage columns of a row: the read, then for an accepted file the
 * outcome and time of each later stage, its record count and printed size. */
static void print_stages(FILE *tsv, double read_s, const TptpStageCostV1 *stage) {
    if (!stage) {
        fprintf(tsv, "\t%.6f\t-\t0\t0\t0\t0\t0\n", read_s);
        return;
    }
    fprintf(tsv, "\t%.6f\t%s\t%.6f\t%.6f\t%.6f\t%zu\t%zu\n", read_s,
            tptp_stage_result_name_v1(stage->result), stage->compact_s,
            stage->print_s, stage->reread_s, stage->records,
            stage->printed_bytes);
}

static void count_stages(Totals *tot, const TptpStageCostV1 *stage) {
    tot->compact_s += stage->compact_s;
    tot->print_s += stage->print_s;
    tot->reread_s += stage->reread_s;
    tot->stage_failures[stage->result]++;
    if (stage->result != TPTP_STAGES_COMPACT_FAILED_V1)
        tot->compacted++;
    if (stage->result != TPTP_STAGES_COMPACT_FAILED_V1 &&
        stage->result != TPTP_STAGES_PRINT_FAILED_V1)
        tot->printed++;
    if (stage->result == TPTP_STAGES_OK_V1)
        tot->round_trip++;
}

typedef struct {
    int unprojected;
    int unknown;
    char unproj_name[256];
    TptpStageRunV1 run;
} VisitState;

/* An input that is not projected or not known rejects the file; any other
 * goes through the later stages, and a failed stage stops the stages but not
 * the read. */
static bool visit_input(Atom *input, void *user) {
    VisitState *st = (VisitState *)user;
    if (atom_has_unprojected(input)) {
        const char *name = unprojected_name(input);
        st->unprojected = 1;
        snprintf(st->unproj_name, sizeof(st->unproj_name), "%s",
                 name ? name : "");
        return false;
    }
    if (atom_has_unknown(input)) {
        st->unknown = 1;
        return false;
    }
    (void)tptp_stage_run_input_v1(&st->run, input);
    return true;
}

static int qualify_one(const CettaTptpPreparedReaderV1 *reader,
                       const TptpCompactStagesV1 *stages, Arena *arena,
                       const char *path, const char *fam, int is_ax,
                       int is_tstp, uint64_t work_limit,
                       uint64_t gll_descriptor_limit,
                       Totals *tot, FILE *tsv) {
    FILE *file;
    long size;
    char *text;
    size_t got;
    char error[512] = {0};
    ArenaMark mark;
    int ok;
    VisitState visit;
    double t0;
    double read_s;

    file = fopen(path, "rb");
    if (!file) {
        fprintf(tsv, "%s\treject\t0\t%s\tcannot-open", path, fam);
        print_stages(tsv, 0.0, NULL);
        tot->rejected++;
        bump_family(tot, fam, is_ax, is_tstp);
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
    memset(&visit, 0, sizeof(visit));
    tptp_stage_run_init_v1(&visit.run, reader, stages, arena);
    t0 = tptp_stages_clock_v1();
    ok = cetta_tptp_prepared_reader_read_text_each_with_work_limit_v1(
        reader, text, got, work_limit, gll_descriptor_limit,
        arena, visit_input, &visit, NULL, error, sizeof(error));
    read_s = tptp_stages_clock_v1() - t0 -
             tptp_stage_run_seconds_v1(&visit.run);
    tot->read_s += read_s;
    if (!ok && !visit.unprojected && !visit.unknown) {
        fprintf(tsv, "%s\treject\t%ld\t%s\t%s", path, size, fam,
                error[0] ? error : "TPTP:NoParse");
        print_stages(tsv, read_s, NULL);
        tot->rejected++;
    } else if (visit.unprojected) {
        fprintf(tsv, "%s\treject\t%ld\t%s\tTPTP:Unprojected%s%s", path, size,
                fam, visit.unproj_name[0] ? " " : "", visit.unproj_name);
        print_stages(tsv, read_s, NULL);
        tot->rejected++;
        tot->unprojected++;
    } else if (visit.unknown) {
        fprintf(tsv, "%s\treject\t%ld\t%s\tTPTP:unknown", path, size, fam);
        print_stages(tsv, read_s, NULL);
        tot->rejected++;
    } else {
        const TptpStageCostV1 *stage = &visit.run.cost;
        fprintf(tsv, "%s\taccept\t%ld\t%s\t%s", path, size, fam,
                stage->result == TPTP_STAGES_OK_V1 || !stage->error[0]
                    ? "-" : stage->error);
        print_stages(tsv, read_s, stage);
        count_stages(tot, stage);
        tot->accepted++;
    }
    bump_family(tot, fam, is_ax, is_tstp);
    arena_reset(arena, mark);
    tot->files++;
    if (tot->files % 100u == 0u) {
        arena_free(arena);
        arena_init(arena);
    }
    free(text);
    fflush(tsv);
    return ok;
}

static int ends_with(const char *name, const char *suf) {
    size_t n = strlen(name);
    size_t s = strlen(suf);
    return n >= s && strcmp(name + n - s, suf) == 0;
}

static int preflight_regular_file(const char *kind, const char *path) {
    struct stat st;
    FILE *file;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "preflight failed: %s is not a regular file: %s\n",
                kind, path);
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "preflight failed: %s is not readable: %s\n", kind,
                path);
        return 0;
    }
    fclose(file);
    return 1;
}

static int preflight_directory(const char *kind, const char *path) {
    struct stat st;
    DIR *dir;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "preflight failed: %s is not a directory: %s\n", kind,
                path);
        return 0;
    }
    dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "preflight failed: %s is not readable: %s\n", kind,
                path);
        return 0;
    }
    closedir(dir);
    return 1;
}

static void walk_dir(const CettaTptpPreparedReaderV1 *reader,
                     const TptpCompactStagesV1 *stages, Arena *arena,
                     const char *dir, const char *suffix, int is_ax,
                     const PathSet *resume, uint64_t work_limit,
                     uint64_t gll_descriptor_limit,
                     Totals *tot, FILE *tsv) {
    DIR *d = opendir(dir);
    struct dirent *ent;
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL) {
        char path[4096];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            walk_dir(reader, stages, arena, path, suffix, is_ax, resume,
                     work_limit, gll_descriptor_limit, tot, tsv);
        else if (S_ISREG(st.st_mode) && ends_with(ent->d_name, suffix)) {
            if (path_set_contains(resume, path)) {
                tot->skipped++;
                continue;
            }
            qualify_one(reader, stages, arena, path, family_of(path), is_ax,
                        0, work_limit, gll_descriptor_limit, tot, tsv);
        }
    }
    closedir(d);
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    CettaTptpPreparedReaderV1 reader;
    TptpCompactStagesV1 stages;
    Totals tot;
    PathSet resume;
    char error[512] = {0};
    const char *snap_path;
    FILE *tsv;
    int extras_at;
    int i;
    uint64_t work_limit = 0u;
    uint64_t gll_descriptor_limit = 0u;

    if (argc < 4) {
        fprintf(stderr,
                "usage: %s SNAPSHOT.tpp1 PROBLEMS_DIR AXIOMS_DIR "
                "[--resume PRIOR.tsv]... [--work-limit N] "
                "[--gll-descriptor-limit N] [TSTP...]\n",
                argv[0]);
        return 2;
    }
    snap_path = argv[1];
    memset(&tot, 0, sizeof(tot));
    memset(&resume, 0, sizeof(resume));
    extras_at = 4;
    while (extras_at < argc) {
        if (strcmp(argv[extras_at], "--resume") == 0) {
            if (extras_at + 1 >= argc) {
                fprintf(stderr, "--resume requires a TSV path\n");
                path_set_free(&resume);
                return 2;
            }
            if (!path_set_load_tsv(&resume, argv[extras_at + 1])) {
                fprintf(stderr, "cannot load resume TSV: %s\n",
                        argv[extras_at + 1]);
                path_set_free(&resume);
                return 2;
            }
            extras_at += 2;
        } else if (strcmp(argv[extras_at], "--work-limit") == 0) {
            char *end = NULL;
            unsigned long long parsed;
            if (extras_at + 1 >= argc) {
                fprintf(stderr, "--work-limit requires a positive integer\n");
                path_set_free(&resume);
                return 2;
            }
            parsed = strtoull(argv[extras_at + 1], &end, 10);
            if (!end || *end != '\0' || parsed == 0u) {
                fprintf(stderr, "invalid --work-limit: %s\n",
                        argv[extras_at + 1]);
                path_set_free(&resume);
                return 2;
            }
            work_limit = (uint64_t)parsed;
            extras_at += 2;
        } else if (strcmp(argv[extras_at], "--gll-descriptor-limit") == 0) {
            char *end = NULL;
            unsigned long long parsed;
            if (extras_at + 1 >= argc) {
                fprintf(stderr,
                        "--gll-descriptor-limit requires a positive integer\n");
                path_set_free(&resume);
                return 2;
            }
            parsed = strtoull(argv[extras_at + 1], &end, 10);
            if (!end || *end != '\0' || parsed == 0u) {
                fprintf(stderr, "invalid --gll-descriptor-limit: %s\n",
                        argv[extras_at + 1]);
                path_set_free(&resume);
                return 2;
            }
            gll_descriptor_limit = (uint64_t)parsed;
            extras_at += 2;
        } else {
            break;
        }
    }
    if (!preflight_regular_file("snapshot", snap_path) ||
        !preflight_directory("Problems root", argv[2]) ||
        !preflight_directory("Axioms root", argv[3])) {
        path_set_free(&resume);
        return 2;
    }
    for (i = extras_at; i < argc; i++) {
        if (!preflight_regular_file("TSTP input", argv[i])) {
            path_set_free(&resume);
            return 2;
        }
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    cetta_tptp_prepared_reader_init_v1(&reader);
    if (!cetta_tptp_prepared_reader_load_v1(
            &reader, snap_path, CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
            error, sizeof(error)) ||
        !tptp_compact_stages_load_v1(&stages, snap_path, error,
                                     sizeof(error))) {
        fprintf(stderr, "load failed: %s\n", error);
        cetta_tptp_prepared_reader_free_v1(&reader);
        arena_free(&arena);
        symbol_table_free(&symbols);
        path_set_free(&resume);
        g_symbols = NULL;
        return 1;
    }
    tsv = stdout;
    setvbuf(tsv, NULL, _IOLBF, 0);
    fprintf(tsv, "path\tstatus\tbytes\tfamily\twitness\tread_s\tstages\t"
                 "compact_s\tprint_s\treread_s\trecords\tprinted_bytes\n");
    walk_dir(&reader, &stages, &arena, argv[2], ".p", 0, &resume, work_limit,
             gll_descriptor_limit, &tot, tsv);
    walk_dir(&reader, &stages, &arena, argv[3], ".ax", 1, &resume, work_limit,
             gll_descriptor_limit, &tot, tsv);
    for (i = extras_at; i < argc; i++) {
        if (path_set_contains(&resume, argv[i])) {
            tot.skipped++;
            continue;
        }
        qualify_one(&reader, &stages, &arena, argv[i], "tstp", 0, 1,
                    work_limit, gll_descriptor_limit, &tot, tsv);
    }
    fprintf(stderr,
            "(TptpCorpusQualificationV1 accepted=%u rejected=%u unprojected=%u "
            "skipped=%u cnf=%u fof=%u tff=%u thf=%u tcf=%u tpi=%u ax=%u tstp=%u "
            "other=%u)\n",
            tot.accepted, tot.rejected, tot.unprojected, tot.skipped, tot.cnf,
            tot.fof, tot.tff, tot.thf, tot.tcf, tot.tpi, tot.ax, tot.tstp,
            tot.other);
    fprintf(stderr,
            "(TptpCorpusStagesV1 compacted=%u printed=%u round-trip=%u "
            "compact-failed=%u print-failed=%u reread-failed=%u "
            "round-trip-differs=%u read_s=%.3f compact_s=%.3f print_s=%.3f "
            "reread_s=%.3f)\n",
            tot.compacted, tot.printed, tot.round_trip,
            tot.stage_failures[TPTP_STAGES_COMPACT_FAILED_V1],
            tot.stage_failures[TPTP_STAGES_PRINT_FAILED_V1],
            tot.stage_failures[TPTP_STAGES_REREAD_FAILED_V1],
            tot.stage_failures[TPTP_STAGES_ROUND_TRIP_DIFFERS_V1],
            tot.read_s, tot.compact_s, tot.print_s, tot.reread_s);
    tptp_compact_stages_free_v1(&stages);
    cetta_tptp_prepared_reader_free_v1(&reader);
    arena_free(&arena);
    symbol_table_free(&symbols);
    path_set_free(&resume);
    g_symbols = NULL;
    return 0;
}
