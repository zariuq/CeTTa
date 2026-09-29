/* Read, print and re-read TPTP files through the canonical terms of the
 * pinned grammar.  Each file must read, print, re-read to the same term, and
 * print to the same text again.
 *
 * The first term is compared through the SHA-256 of its preorder
 * serialization, so it is released before the printed text is read again and
 * a file never holds two terms at once.  Each result line reports the arena
 * bytes of the first term and the process peak resident size for that file. */
#include "native/tptp_official_snapshot_v1.h"
#include "parser.h"
#include "native_sha256.h"
#include "symbol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double monotonic_s(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static char *slurp(const char *path, size_t *len) {
    FILE *file = fopen(path, "rb");
    long size;
    char *text;
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    *len = fread(text, 1u, (size_t)size, file);
    fclose(file);
    text[*len] = '\0';
    return text;
}

/* The peak resident size is per file: it is reset before each file where
 * the kernel allows it. */
static void peak_rss_reset(void) {
    FILE *file = fopen("/proc/self/clear_refs", "w");
    if (!file)
        return;
    fputs("5", file);
    fclose(file);
}

static unsigned long peak_rss_kb(void) {
    FILE *file = fopen("/proc/self/status", "r");
    char line[256];
    unsigned long kb = 0u;
    if (!file)
        return 0u;
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "VmHWM: %lu kB", &kb) == 1)
            break;
    fclose(file);
    return kb;
}

static void digest_header(CettaNativeSha256 *sha, char tag, uint64_t len) {
    uint8_t header[9];
    header[0] = (uint8_t)tag;
    for (int i = 0; i < 8; i++)
        header[1 + i] = (uint8_t)(len >> (8 * i));
    cetta_native_sha256_update(sha, header, sizeof(header));
}

static void digest_bytes(CettaNativeSha256 *sha, char tag, const void *bytes, uint64_t len) {
    digest_header(sha, tag, len);
    if (len)
        cetta_native_sha256_update(sha, bytes, (size_t)len);
}

/* SHA-256 of the preorder serialization of a canonical term.  Every node
 * records its kind and length, so equal digests mean equal terms up to hash
 * collision.  Canonical terms hold symbols, strings, expressions and list
 * values; a list records its own kind and its elements, never its tag. */
static bool term_digest(const Atom *root, char out[65], char *error, size_t error_size) {
    CettaNativeSha256 sha;
    const Atom **stack;
    size_t len = 0u, cap = 1024u;
    bool ok = true;

    stack = malloc(cap * sizeof(*stack));
    if (!stack) {
        snprintf(error, error_size, "digest stack allocation failed");
        return false;
    }
    cetta_native_sha256_init(&sha);
    stack[len++] = root;
    while (ok && len > 0u) {
        const Atom *atom = stack[--len];
        if (atom->kind == ATOM_SYMBOL) {
            const char *name = symbol_bytes(g_symbols, atom->sym_id);
            digest_bytes(&sha, 'S', name, symbol_len(g_symbols, atom->sym_id));
        } else if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
            digest_bytes(&sha, 'T', atom->ground.sval, strlen(atom->ground.sval));
        } else if (atom->kind == ATOM_EXPR) {
            bool list = atom_is_list(atom);
            CettaExprLen count = list ? atom_list_len(atom) : atom->expr.len;
            Atom *const *elems = list ? atom_list_elems(atom) : atom->expr.elems;
            digest_header(&sha, list ? 'L' : 'E', (uint64_t)count);
            if (count > cap - len) {
                size_t wanted = cap;
                while (count > wanted - len)
                    wanted *= 2u;
                const Atom **grown = realloc(stack, wanted * sizeof(*stack));
                if (!grown) {
                    snprintf(error, error_size, "digest stack allocation failed");
                    ok = false;
                    break;
                }
                stack = grown;
                cap = wanted;
            }
            for (CettaExprLen i = count; i > 0u; i--)
                stack[len++] = elems[i - 1u];
        } else {
            snprintf(error, error_size, "unexpected atom kind %d in a canonical term",
                     (int)atom->kind);
            ok = false;
        }
    }
    free(stack);
    if (ok)
        cetta_native_sha256_finish_hex(&sha, out);
    return ok;
}

typedef struct {
    double read_s;
    double print_s;
    size_t term_bytes;
} RoundTripMeasure;

/* The text of a file's inputs: each canonical input printed at TPTP_input,
 * one per line. */
static bool print_inputs(const PPTableSnapshotV1 *snapshot, const Atom *inputs, char **out,
                         size_t *out_len, char *error, size_t error_size) {
    char *text = NULL;
    size_t len = 0u, cap = 0u;
    CettaGrammarCanonicalTableV1 *table = NULL;
    *out = NULL;
    *out_len = 0u;
    if (!inputs || inputs->kind != ATOM_EXPR) {
        snprintf(error, error_size, "canonical inputs are not a sequence");
        return false;
    }
    if (!cetta_tptp_snapshot_canonical_table_v1(snapshot, &table, error, error_size))
        return false;
    for (CettaExprIndex i = 0u; i < inputs->expr.len; i++) {
        char *one = NULL;
        size_t one_len = 0u;
        if (!cetta_tptp_snapshot_canonical_print_v1(snapshot, table, inputs->expr.elems[i],
                                                    "TPTP_input", &one, &one_len, error,
                                                    error_size)) {
            cetta_grammar_canonical_table_free_v1(table);
            free(text);
            return false;
        }
        if (len + one_len + 2u > cap) {
            size_t grown_cap = cap ? cap : 4096u;
            char *grown;
            while (len + one_len + 2u > grown_cap)
                grown_cap *= 2u;
            grown = realloc(text, grown_cap);
            if (!grown) {
                free(one);
                free(text);
                cetta_grammar_canonical_table_free_v1(table);
                snprintf(error, error_size, "out of memory");
                return false;
            }
            text = grown;
            cap = grown_cap;
        }
        memcpy(text + len, one, one_len);
        len += one_len;
        text[len++] = '\n';
        text[len] = '\0';
        free(one);
    }
    cetta_grammar_canonical_table_free_v1(table);
    *out = text ? text : calloc(1u, 1u);
    *out_len = len;
    return *out != NULL;
}

static const char *round_trip(const CettaTptpPreparedReaderV1 *reader, Arena *arena,
                              const char *text, size_t len, RoundTripMeasure *measure,
                              char *error, size_t error_size) {
    CettaTptpReadOutcomeV1 outcome;
    ArenaMark mark = arena_mark(arena);
    Atom *first = NULL, *second = NULL;
    char *printed = NULL, *reprinted = NULL;
    size_t printed_len = 0u, reprinted_len = 0u;
    char first_digest[65], second_digest[65];
    const char *status = "ok";
    double t0 = monotonic_s();

    if (!cetta_tptp_prepared_reader_read_text_outcome_v1(reader, text, len, arena, &first,
                                                          &outcome, error, error_size) ||
        !first)
        return "read-fail";
    measure->read_s = monotonic_s() - t0;
    measure->term_bytes =
        arena_accounted_live_bytes(arena) - arena_mark_accounted_live_bytes(mark);
    t0 = monotonic_s();
    if (!print_inputs(&reader->snapshot, first, &printed, &printed_len, error, error_size))
        return "print-fail";
    measure->print_s = monotonic_s() - t0;
    if (!term_digest(first, first_digest, error, error_size)) {
        status = "digest-fail";
        goto done;
    }
    first = NULL;
    arena_reset(arena, mark);
    if (!cetta_tptp_prepared_reader_read_text_outcome_v1(reader, printed, printed_len, arena,
                                                          &second, &outcome, error,
                                                          error_size) ||
        !second) {
        status = "reread-fail";
        goto done;
    }
    if (!term_digest(second, second_digest, error, error_size)) {
        status = "digest-fail";
        goto done;
    }
    if (strcmp(first_digest, second_digest) != 0) {
        snprintf(error, error_size, "canonical terms differ");
        status = "term-mismatch";
        goto done;
    }
    if (!print_inputs(&reader->snapshot, second, &reprinted, &reprinted_len, error,
                      error_size)) {
        status = "reprint-fail";
        goto done;
    }
    if (reprinted_len != printed_len || memcmp(printed, reprinted, printed_len) != 0) {
        snprintf(error, error_size, "printed texts differ");
        status = "text-mismatch";
    }
done:
    free(printed);
    free(reprinted);
    return status;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena arena;
    CettaTptpPreparedReaderV1 reader;
    char error[512] = {0};
    unsigned long passed = 0u, failed = 0u;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s SNAPSHOT.tpp1 FILE...\n", argv[0]);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    cetta_tptp_prepared_reader_init_v1(&reader);
    if (!cetta_tptp_prepared_reader_load_v1(&reader, argv[1],
                                            CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, error,
                                            sizeof(error))) {
        fprintf(stderr, "load failed: %s\n", error);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("path\tbytes\tstatus\tread_s\tprint_s\tterm_bytes\tpeak_kb\tdetail\n");
    for (i = 2; i < argc; i++) {
        size_t len = 0u;
        char *text;
        RoundTripMeasure measure = {0};
        const char *status;
        ArenaMark mark = arena_mark(&arena);
        peak_rss_reset();
        text = slurp(argv[i], &len);
        error[0] = '\0';
        status = text ? round_trip(&reader, &arena, text, len, &measure, error, sizeof(error))
                      : "cannot-open";
        arena_reset(&arena, mark);
        free(text);
        printf("%s\t%zu\t%s\t%.6f\t%.6f\t%zu\t%lu\t%s\n", argv[i], len, status,
               measure.read_s, measure.print_s, measure.term_bytes, peak_rss_kb(),
               error[0] ? error : "-");
        if (strcmp(status, "ok") == 0)
            passed++;
        else
            failed++;
    }
    fprintf(stderr, "(TptpCanonicalRoundTripV1Summary %lu %lu)\n", passed, failed);
    cetta_tptp_prepared_reader_free_v1(&reader);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failed == 0u ? 0 : 1;
}
