#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "gslt_support_profile_v1.h"
#include "native_sha256.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const CettaGsltSupportTransformProfileV1 cetta_mm2_gslt_profile_v1;

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    long size;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        (uintmax_t)size >= SIZE_MAX || fseek(file, 0, SEEK_SET)) {
        fclose(file); return 2;
    }
    uint8_t *bytes = malloc((size_t)size + 1);
    bool ok = bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file)) ok = false;
    if (!ok) { free(bytes); return 2; }
    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols; g_var_intern = &variables;
    CettaGsltSupportTransformProfileV1 decoded = {0};
    const CettaGsltSupportTransformProfileV1 *emitted = &cetta_mm2_gslt_profile_v1;
    char error[512] = {0}, digest[65];
    cetta_native_sha256_hex(bytes, (size_t)size, digest);
    ok = cetta_gslt_support_profile_from_source_v1(&arena, bytes, (size_t)size,
             argv[2], &decoded, error, sizeof(error)) &&
         cetta_gslt_support_profile_packet_matches_v1(emitted, error, sizeof(error)) &&
         !strcmp(emitted->manifest_sha256, digest) &&
         !strcmp(emitted->compiler_sha256, argv[2]) &&
         decoded.physical_profile_packet_size == emitted->physical_profile_packet_size &&
         !memcmp(decoded.physical_profile_packet, emitted->physical_profile_packet,
                 decoded.physical_profile_packet_size);
    if (ok && argc == 4) {
        /* The fixture supplies a declared but unavailable provider. Its work
         * must remain data, not execute using the familiar source spelling. */
        Atom **forms = NULL;
        int count = parse_metta_text(argv[3], &arena, &forms);
        CettaGsltSupportTransformResultV1 result = {0};
        ok = count > 0 && cetta_gslt_support_transform_run_v1(emitted, &arena,
                 forms, (size_t)count, 8, &result, error, sizeof(error)) &&
             result.outcome == CETTA_GSLT_SUPPORT_COMPLETED && !result.steps &&
             result.atom_count == (size_t)count;
        for (int i = 0; ok && i < count; i++) {
            bool found = false;
            for (size_t j = 0; j < result.atom_count; j++)
                if (atom_eq(forms[i], result.atoms[j])) found = true;
            ok = found;
        }
        cetta_gslt_support_transform_result_free_v1(&result);
        free(forms);
        /* The same shell and permuted positions must also execute supported
         * work; otherwise the inert case could pass through non-recognition. */
        forms = NULL;
        count = parse_metta_text(
            "(seed a) (work (all (seed $x)) (emit (seen $x)) (0 positive))",
            &arena, &forms);
        ok = ok && count == 2 && cetta_gslt_support_transform_run_v1(emitted, &arena,
                 forms, (size_t)count, 8, &result, error, sizeof(error)) &&
             result.outcome == CETTA_GSLT_SUPPORT_COMPLETED && result.steps == 1 &&
             result.atom_count == 2;
        Atom *seen_items[] = {atom_symbol(&arena, "seen"), atom_symbol(&arena, "a")};
        Atom *seen = atom_expr(&arena, seen_items, 2);
        bool found_seen = false;
        for (size_t i = 0; i < result.atom_count; i++)
            if (atom_eq(seen, result.atoms[i])) found_seen = true;
        ok = ok && found_seen;
        cetta_gslt_support_transform_result_free_v1(&result);
        free(forms);
    }
    if (!ok) fprintf(stderr, "emitted support profile differs: %s\n", error);
    free(bytes);
    g_symbols = NULL; g_var_intern = NULL;
    var_intern_free(&variables); symbol_table_free(&symbols); arena_free(&arena);
    if (ok) puts("Compiled support-profile fields and packet agree with source; build identities agree.");
    return ok ? 0 : 1;
}
