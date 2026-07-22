#define _POSIX_C_SOURCE 200809L

#include "he_compiled_reader.h"

#include "match.h"
#include "parser.h"
#include "symbol.h"
#include "term_universe.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static char *read_text_file(const char *path, size_t *len_out) {
    FILE *file = NULL;
    char *text = NULL;
    long size;

    if (!path || !len_out)
        return NULL;
    *len_out = 0u;
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0)
        goto fail;
    size = ftell(file);
    if (size < 0 || (uintmax_t)size > SIZE_MAX - 1u ||
        fseek(file, 0, SEEK_SET) != 0) {
        goto fail;
    }
    text = malloc((size_t)size + 1u);
    if (!text || fread(text, 1u, (size_t)size, file) != (size_t)size)
        goto fail;
    text[size] = '\0';
    *len_out = (size_t)size;
    (void)fclose(file);
    return text;

fail:
    if (file)
        (void)fclose(file);
    free(text);
    return NULL;
}

static bool parse_iterations(
    HECompiledReaderV1 *reader, const char *text, TermUniverse *universe,
    unsigned iterations, bool compiled, int expected_atoms,
    uint64_t *elapsed_ns, uint64_t *source_passes) {
    uint64_t total = 0u;
    uint64_t passes = 0u;

    for (unsigned index = 0u; index < iterations; index++) {
        HECompiledReaderV1Receipt receipt;
        AtomId *ids = NULL;
        char error[512] = {0};
        uint64_t start = monotonic_ns();
        int atoms = compiled
                        ? he_compiled_reader_v1_parse_text_ids(
                              reader, text, universe, &ids, &receipt,
                              error, sizeof(error))
                        : parse_metta_text_ids(text, universe, &ids);
        uint64_t stop = monotonic_ns();

        if (start == 0u || stop < start || atoms != expected_atoms ||
            (atoms > 0 && !ids)) {
            fprintf(stderr, "benchmark parse failed (%s): %s\n",
                    compiled ? "compiled" : "legacy",
                    compiled ? error : "legacy reader rejected input");
            free(ids);
            return false;
        }
        if (compiled) {
            if (receipt.source_pass_count != 1u) {
                fprintf(stderr,
                        "compiled benchmark parse was not one source pass\n");
                free(ids);
                return false;
            }
            passes += receipt.source_pass_count;
        }
        total += stop - start;
        free(ids);
    }
    *elapsed_ns += total;
    *source_passes += passes;
    return true;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    Arena persistent;
    TermUniverse universe;
    HECompiledReaderV1 *reader = NULL;
    HECompiledReaderV1Receipt receipt;
    AtomId *legacy_ids = NULL;
    AtomId *compiled_ids = NULL;
    char error[512] = {0};
    char *text = NULL;
    size_t input_len = 0u;
    unsigned iterations;
    char *end = NULL;
    int expected_atoms;
    int compiled_atoms;
    uint64_t legacy_ns = 0u;
    uint64_t compiled_ns = 0u;
    uint64_t source_passes = 0u;
    double ratio;
    const char *mode = "both";
    int result = 1;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s FILE ITERATIONS [both|legacy|compiled]\n",
                argv[0]);
        return 2;
    }
    if (argc == 4) {
        mode = argv[3];
        if (strcmp(mode, "both") != 0 && strcmp(mode, "legacy") != 0 &&
            strcmp(mode, "compiled") != 0) {
            fprintf(stderr, "invalid benchmark mode\n");
            return 2;
        }
    }
    errno = 0;
    unsigned long parsed_iterations = strtoul(argv[2], &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed_iterations == 0u ||
        parsed_iterations > UINT32_MAX) {
        fprintf(stderr, "invalid iteration count\n");
        return 2;
    }
    iterations = (unsigned)parsed_iterations;
    text = read_text_file(argv[1], &input_len);
    if (!text) {
        fprintf(stderr, "cannot read benchmark input\n");
        return 2;
    }

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    arena_init(&persistent);
    arena_set_runtime_kind(&persistent,
                           CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);

    reader = he_compiled_reader_v1_new();
    if (!reader || !he_compiled_reader_v1_prepare(
                       reader, error, sizeof(error))) {
        fprintf(stderr, "compiled reader preparation failed: %s\n", error);
        goto done;
    }

    expected_atoms = parse_metta_text_ids(text, &universe, &legacy_ids);
    compiled_atoms = he_compiled_reader_v1_parse_text_ids(
        reader, text, &universe, &compiled_ids, &receipt,
        error, sizeof(error));
    if (expected_atoms < 0 || compiled_atoms != expected_atoms ||
        receipt.source_pass_count != 1u) {
        fprintf(stderr,
                "benchmark readers disagree: legacy=%d compiled=%d (%s)\n",
                expected_atoms, compiled_atoms, error);
        goto done;
    }
    {
        Arena comparison;
        bool equivalent = true;
        arena_init(&comparison);
        arena_set_hashcons(&comparison, NULL);
        for (int index = 0; index < expected_atoms; index++) {
            ArenaMark mark = arena_mark(&comparison);
            Atom *legacy = term_universe_copy_atom(
                &universe, &comparison, legacy_ids[index]);
            Atom *compiled = term_universe_copy_atom(
                &universe, &comparison, compiled_ids[index]);
            if (!legacy || !compiled || !atom_alpha_eq(legacy, compiled)) {
                fprintf(stderr,
                        "benchmark readers differ at top-level atom %d\n",
                        index);
                if (legacy) {
                    fputs("legacy:   ", stderr);
                    atom_print(legacy, stderr);
                    fputc('\n', stderr);
                }
                if (compiled) {
                    fputs("compiled: ", stderr);
                    atom_print(compiled, stderr);
                    fputc('\n', stderr);
                }
                equivalent = false;
                break;
            }
            arena_reset(&comparison, mark);
        }
        arena_free(&comparison);
        if (!equivalent)
            goto done;
    }
    free(legacy_ids);
    legacy_ids = NULL;
    free(compiled_ids);
    compiled_ids = NULL;

    if (strcmp(mode, "both") == 0) {
        for (unsigned index = 0u; index < iterations; index++) {
            bool compiled_first = (index & 1u) != 0u;
            if (!parse_iterations(reader, text, &universe, 1u,
                                  compiled_first, expected_atoms,
                                  compiled_first ? &compiled_ns : &legacy_ns,
                                  &source_passes) ||
                !parse_iterations(reader, text, &universe, 1u,
                                  !compiled_first, expected_atoms,
                                  compiled_first ? &legacy_ns : &compiled_ns,
                                  &source_passes)) {
                goto done;
            }
        }
        if (legacy_ns == 0u || source_passes != iterations)
            goto done;
        ratio = (double)compiled_ns / (double)legacy_ns;
        printf("(HECompiledReaderBenchmarkV1 %zu %u %d "
               "legacy-ns %" PRIu64 " compiled-ns %" PRIu64
               " ratio %.6f source-passes %" PRIu64 ")\n",
               input_len, iterations, expected_atoms, legacy_ns, compiled_ns,
               ratio, source_passes);
    } else {
        bool compiled = strcmp(mode, "compiled") == 0;
        uint64_t *elapsed = compiled ? &compiled_ns : &legacy_ns;
        if (!parse_iterations(reader, text, &universe, iterations, compiled,
                              expected_atoms, elapsed, &source_passes) ||
            (compiled && source_passes != iterations)) {
            goto done;
        }
        printf("(HECompiledReaderProfileV1 %s %zu %u %d "
               "nanoseconds %" PRIu64 " source-passes %" PRIu64 ")\n",
               mode, input_len, iterations, expected_atoms, *elapsed,
               source_passes);
    }
    result = 0;

done:
    he_compiled_reader_v1_free(reader);
    term_universe_free(&universe);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    free(legacy_ids);
    free(compiled_ids);
    free(text);
    return result;
}
