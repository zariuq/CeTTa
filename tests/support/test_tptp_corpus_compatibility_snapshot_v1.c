#include "native/tptp_official_snapshot_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

/* The productions the compatibility extensions add to the strict grammar. */
static const char *const kExtensionLabels[] = {
    "tff_unitary_term#xxxxx",
    "nhf_parameter#xx",
    "internal_source#x",
    "cnf_disjunction#xx",
    "cnf_disjunction#xxx",
    "cnf_disjunction#xxxx",
    "cnf_disjunction#xxxxx",
    "cnf_disjunction#xxxxxx",
    "cnf_disjunction#xxxxxxx",
    "cnf_disjunction#xxxxxxxx",
    "cnf_disjunction#xxxxxxxxx",
};

static bool is_extension_label(const char *label) {
    size_t i;
    for (i = 0u; i < sizeof(kExtensionLabels) / sizeof(kExtensionLabels[0]);
         i++) {
        if (strcmp(label, kExtensionLabels[i]) == 0)
            return true;
    }
    return false;
}

static CettaTptpReadStatusV1 read_text(const CettaTptpPreparedReaderV1 *reader,
                                       const char *text, Arena *arena,
                                       const char **printed) {
    CettaTptpReadOutcomeV1 outcome;
    Atom *records = NULL;
    char error[512] = {0};
    memset(&outcome, 0, sizeof(outcome));
    *printed = NULL;
    if (!cetta_tptp_prepared_reader_read_text_outcome_v1(
            reader, text, strlen(text), arena, &records, &outcome, error,
            sizeof(error)))
        return outcome.status == CETTA_TPTP_READ_OK_V1
                   ? CETTA_TPTP_READ_ERROR_V1 : outcome.status;
    if (outcome.status == CETTA_TPTP_READ_OK_V1 && records)
        *printed = atom_to_parseable_string(arena, records);
    return outcome.status;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    ArenaMark mark;
    CettaTptpPreparedReaderV1 reader;
    CettaTptpPreparedReaderV1 strict;
    CettaTptpReadOutcomeV1 outcome;
    TestCounts counts = {0};
    Atom *records = NULL;
    char error[512] = {0};
    char out_path[256];
    bool loaded;
    bool strict_loaded;
    bool read;

    if (argc != 7) {
        fprintf(stderr,
                "usage: %s SNAPSHOT STRICT_SNAPSHOT PACK STRICT_PACK "
                "POSITIVE.p MALFORMED.p\n",
                argv[0]);
        return 2;
    }

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    cetta_tptp_prepared_reader_init_v1(&reader);
    cetta_tptp_prepared_reader_init_v1(&strict);

    loaded = cetta_tptp_prepared_reader_load_bound_v1(
        &reader, argv[1], CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
        "corpus-compatible", NULL, error, sizeof(error));
    expect(&counts, loaded,
           error[0] ? error : "load corpus-compatible snapshot");
    error[0] = '\0';
    strict_loaded = cetta_tptp_prepared_reader_load_bound_v1(
        &strict, argv[2], CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
        "strict", NULL, error, sizeof(error));
    expect(&counts, strict_loaded,
           error[0] ? error : "load strict snapshot");

    if (loaded) {
        const CettaLpNativeSlrProgram *slr = &reader.snapshot.slr;
        uint32_t avoided = 0u;
        bool only_extensions = true;
        uint32_t i;
        for (i = 0u; i < slr->production_len; i++) {
            const char *label;
            if (!slr->productions[i].avoided)
                continue;
            avoided++;
            label = slr->productions[i].authored
                        ? symbol_bytes(g_symbols, slr->productions[i].label)
                        : "";
            if (!is_extension_label(label))
                only_extensions = false;
        }
        expect(&counts,
               only_extensions &&
                   avoided == sizeof(kExtensionLabels) /
                                  sizeof(kExtensionLabels[0]),
               "the avoided productions are exactly the extensions");
    }
    if (strict_loaded) {
        const CettaLpNativeSlrProgram *slr = &strict.snapshot.slr;
        bool none = true;
        uint32_t i;
        for (i = 0u; i < slr->production_len; i++)
            none = none && !slr->productions[i].avoided;
        expect(&counts, none, "the strict snapshot avoids nothing");
    }

    if (loaded) {
        mark = arena_mark(&arena);
        memset(&outcome, 0, sizeof(outcome));
        error[0] = '\0';
        read = cetta_tptp_prepared_reader_read_file_outcome_v1(
            &reader, argv[5], &arena, &records, &outcome,
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
            &reader, argv[6], &arena, &records, &outcome,
            error, sizeof(error));
        expect(&counts,
               !read && outcome.status == CETTA_TPTP_READ_NO_PARSE_V1 &&
                   records == NULL,
               error[0] ? error
                        : "malformed parenthesized CNF remains rejected");
        arena_reset(&arena, mark);
    }

    if (loaded && strict_loaded) {
        /* A text the strict grammar reads keeps that reading: the extension
         * reading of '~?[X:r]:f(X)' as a term is the one avoided.  The
         * second text reaches the reader's keyword lattice, whose word
         * 'fof' is a keyword token. */
        static const char *const strict_texts[] = {
            "tff(a,axiom,~?[X:r]:f(X)!=z=>p).\n",
            "tff(a,axiom,~?[X:r]:f(X)!=fof=>p).\n",
            "tff(a,axiom,(~?[X:r]:f(X)!=z=>p)&q).\n",
        };
        /* A text only an extension reads is read through it. */
        static const char *const extension_texts[] = {
            "tff(b,axiom,~f(x)!=z).\n",
            "tff(b,axiom,~f(x)!=fof).\n",
            "thf(c,axiom,{$necessary(agent)} @ p).\n",
        };
        size_t t;
        for (t = 0u; t < sizeof(strict_texts) / sizeof(strict_texts[0]);
             t++) {
            const char *compat_printed = NULL;
            const char *strict_printed = NULL;
            char label[160];
            CettaTptpReadStatusV1 compat_status;
            CettaTptpReadStatusV1 strict_status;
            mark = arena_mark(&arena);
            compat_status =
                read_text(&reader, strict_texts[t], &arena, &compat_printed);
            strict_status =
                read_text(&strict, strict_texts[t], &arena, &strict_printed);
            snprintf(label, sizeof(label),
                     "compatible reads strict text %zu as strict does", t);
            expect(&counts,
                   compat_status == CETTA_TPTP_READ_OK_V1 &&
                       strict_status == CETTA_TPTP_READ_OK_V1 &&
                       compat_printed && strict_printed &&
                       strcmp(compat_printed, strict_printed) == 0,
                   label);
            arena_reset(&arena, mark);
        }
        for (t = 0u;
             t < sizeof(extension_texts) / sizeof(extension_texts[0]); t++) {
            const char *compat_printed = NULL;
            const char *strict_printed = NULL;
            char label[160];
            CettaTptpReadStatusV1 compat_status;
            CettaTptpReadStatusV1 strict_status;
            mark = arena_mark(&arena);
            compat_status = read_text(&reader, extension_texts[t], &arena,
                                      &compat_printed);
            strict_status = read_text(&strict, extension_texts[t], &arena,
                                      &strict_printed);
            snprintf(label, sizeof(label),
                     "extension text %zu reads only through the extension",
                     t);
            expect(&counts,
                   compat_status == CETTA_TPTP_READ_OK_V1 &&
                       strict_status == CETTA_TPTP_READ_NO_PARSE_V1,
                   label);
            arena_reset(&arena, mark);
        }
    }

    /* Construction binds the profile to its baseline. */
    snprintf(out_path, sizeof(out_path),
             "runtime/bootstrap/tables-baseline-probe.%ld.tpp1",
             (long)getpid());
    error[0] = '\0';
    expect(&counts,
           !cetta_tptp_snapshot_construct_from_pack_v1(
               argv[3], NULL, out_path, error, sizeof(error)) &&
               strstr(error, "baseline") != NULL,
           "a corpus-compatible pack needs its strict baseline");
    error[0] = '\0';
    expect(&counts,
           !cetta_tptp_snapshot_construct_from_pack_v1(
               argv[3], argv[1], out_path, error, sizeof(error)) &&
               strstr(error, "DigestMismatch") != NULL,
           "a corpus-compatible snapshot is no baseline");
    error[0] = '\0';
    expect(&counts,
           !cetta_tptp_snapshot_construct_from_pack_v1(
               argv[4], argv[2], out_path, error, sizeof(error)) &&
               strstr(error, "baseline") != NULL,
           "a strict pack has no baseline");
    expect(&counts, access(out_path, F_OK) != 0,
           "a refused construction writes no snapshot");

    printf("(TptpCorpusCompatibilitySnapshotV1Summary %u %u)\n",
           counts.passed, counts.failed);

    cetta_tptp_prepared_reader_free_v1(&strict);
    cetta_tptp_prepared_reader_free_v1(&reader);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
