#define _POSIX_C_SOURCE 200809L
#include "parser_pack_native_v1.h"
#include "parser_pack_table_snapshot_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native/language_def_parser_pack_v1.h"
#include "native/parser_baked_v1.h"
#include "native_sha256.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t clock_ns(clockid_t kind) {
    struct timespec value;
    if (clock_gettime(kind, &value)) abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

int main(int argc, char **argv) {
    const CettaBakedParserV1 *baked = cetta_parser_baked_v1_find("regex-pattern-reader");
    PPTableSnapshotV1 snapshot = {0};
    CettaLdParserPackV1 compiled = {0};
    PPNativeV1Prepared prepared = {0};
    PPNativeV1PreparedView view = {0};
    PPNativeV1Result result = {0};
    SymbolTable symbols;
    Arena arena;
    Atom *metadata = NULL;
    char error[512] = {0};
    uint8_t *bytes = NULL;
    FILE *file = NULL;
    int status = 1;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    arena_init(&arena);
    if ((argc != 2 && argc != 3) ||
        (argc == 3 && strcmp(argv[2], "glr") && strcmp(argv[2], "lr") && strcmp(argv[2], "trace")) ||
        !(file = fopen(argv[1], "rb")) || fseek(file, 0, SEEK_END)) goto done;
    long extent = ftell(file);
    if (extent < 0 || fseek(file, 0, SEEK_SET)) goto done;
    size_t len = (size_t)extent;
    bytes = malloc(len ? len : 1u);
    if (!bytes || fread(bytes, 1u, len, file) != len) goto done;
    fclose(file); file = NULL;
    if (!baked || !pp_table_snapshot_v1_read(&snapshot, baked->bytes, baked->length, error, sizeof(error)) ||
        !fh_ground_term_v1_read_source(&arena, snapshot.general_metadata, snapshot.general_metadata_len,
            &metadata, error, sizeof(error)) || !metadata || metadata->kind != ATOM_EXPR ||
        metadata->expr.len < 5u ||
        !cetta_ld_parser_pack_v1_import_storage(&compiled, metadata->expr.elems[4], error, sizeof(error)) ||
        compiled.tokenized ||
        !ppnative_v1_prepared_import_programs(&prepared, &compiled.pack, snapshot.gll.start_nonterminal,
            &snapshot.scalar_terminals, &snapshot.gll, &snapshot.slr, error, sizeof(error)) ||
        !ppnative_v1_prepared_immutable_view(&prepared, &view)) goto done;
    uint64_t real_start = clock_ns(CLOCK_MONOTONIC), cpu_start = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    CettaLpNativeSlrLatticeOutcome corridor = CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
    if (argc == 3 && !strcmp(argv[2], "trace")) {
        /* The value-only corridor: its derivation trace replayed directly. */
        CettaLpNativeLrTrace trace = {0};
        bool traced = cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_trace(view.glr,
            view.terminal_plan->terminals, view.terminal_plan->len, bytes, len, UINT32_MAX,
            &corridor, &result.forest, &trace, error, sizeof(error));
        uint64_t real_traced = clock_ns(CLOCK_MONOTONIC), cpu_traced = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
        if (traced && corridor == CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED) {
            result.accepted = true;
            traced = ppnative_v1_finish_values_trace(&result, &compiled.pack, &trace, UINT32_MAX,
                UINT32_MAX, NULL, error, sizeof(error));
        }
        uint32_t steps = trace.len;
        cetta_lp_native_lr_trace_free(&trace);
        if (!traced) goto done;
        uint64_t real_replayed = clock_ns(CLOCK_MONOTONIC), cpu_replayed = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
        if (corridor != CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED) {
            printf("{\"bytes\":%zu,\"corridor\":\"needs-general\",\"recognize_ns\":%llu}\n",
                len, (unsigned long long)(real_traced - real_start));
            status = 0; goto done;
        }
        char trace_digest[65] = {0};
        CettaNativeSha256 trace_sha;
        cetta_native_sha256_init(&trace_sha);
        for (uint32_t i = 0u; i < result.semantic_result_len; ++i) {
            uint8_t *wire = NULL; size_t wire_len = 0u;
            if (!fh_ground_term_v1_render(result.semantic_results[i], &wire, &wire_len, error, sizeof(error))) goto done;
            uint8_t length_bytes[8];
            for (uint32_t j = 0u; j < 8u; ++j) length_bytes[j] = (uint8_t)((uint64_t)wire_len >> (j * 8u));
            cetta_native_sha256_update(&trace_sha, length_bytes, sizeof(length_bytes));
            cetta_native_sha256_update(&trace_sha, wire, wire_len);
            free(wire);
        }
        cetta_native_sha256_finish_hex(&trace_sha, trace_digest);
        printf("{\"bytes\":%zu,\"accepted\":%s,\"values\":%u,\"recognize_ns\":%llu,\"recognize_cpu_ns\":%llu,"
               "\"replay_ns\":%llu,\"replay_cpu_ns\":%llu,\"steps\":%u,\"value_sha256\":\"%s\"}\n",
            len, result.accepted ? "true" : "false", result.semantic_result_len,
            (unsigned long long)(real_traced - real_start), (unsigned long long)(cpu_traced - cpu_start),
            (unsigned long long)(real_replayed - real_traced), (unsigned long long)(cpu_replayed - cpu_traced),
            steps, trace_digest);
        status = 0; goto done;
    }
    bool parsed = argc == 3 && !strcmp(argv[2], "lr")
        ? cetta_lp_native_glr_prepared_parse_owned_utf8_deterministic_forest(view.glr,
            view.terminal_plan->terminals, view.terminal_plan->len, bytes, len, UINT32_MAX,
            &corridor, &result.forest, error, sizeof(error))
        : argc == 3 ? cetta_lp_native_glr_prepared_parse_owned_utf8_forest(view.glr,
            view.terminal_plan->terminals, view.terminal_plan->len, bytes, len, UINT32_MAX,
            &result.forest, error, sizeof(error))
        : cetta_lp_native_gll_prepared_parse_owned_utf8_forest_complete(view.gll,
            view.terminal_plan->terminals, view.terminal_plan->len, bytes, len, UINT32_MAX,
            &result.forest, error, sizeof(error));
    if (!parsed) goto done;
    uint64_t real_parsed = clock_ns(CLOCK_MONOTONIC), cpu_parsed = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    if (argc == 3 && !strcmp(argv[2], "lr") && corridor == CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL) {
        printf("{\"bytes\":%zu,\"corridor\":\"needs-general\",\"recognize_ns\":%llu,\"recognize_cpu_ns\":%llu}\n",
            len, (unsigned long long)(real_parsed - real_start), (unsigned long long)(cpu_parsed - cpu_start));
        status = 0; goto done;
    }
    result.accepted = result.forest.root_len != 0u;
    if (!ppnative_v1_finish_values_selected(&result, &compiled.pack, NULL, UINT32_MAX,
            UINT32_MAX, NULL, error, sizeof(error))) goto done;
    uint64_t real_replayed = clock_ns(CLOCK_MONOTONIC), cpu_replayed = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
    char forest_digest[65] = {0}, value_digest[65] = {0};
    size_t wire_len = 0u, written = 0u;
    uint8_t *wire = NULL;
    if (!cetta_lp_native_utf8_forest_wire_size(&result.forest, &wire_len, error, sizeof(error))) goto done;
    wire = malloc(wire_len ? wire_len : 1u);
    if (!wire || !cetta_lp_native_utf8_forest_wire_write(&result.forest, wire, wire_len,
            &written, error, sizeof(error))) { free(wire); goto done; }
    cetta_native_sha256_hex(wire, written, forest_digest);
    free(wire); wire = NULL;
    CettaNativeSha256 values_sha;
    cetta_native_sha256_init(&values_sha);
    for (uint32_t i = 0u; i < result.semantic_result_len; ++i) {
        if (!fh_ground_term_v1_render(result.semantic_results[i], &wire, &wire_len, error, sizeof(error))) goto done;
        uint8_t length_bytes[8];
        for (uint32_t j = 0u; j < 8u; ++j) length_bytes[j] = (uint8_t)((uint64_t)wire_len >> (j * 8u));
        cetta_native_sha256_update(&values_sha, length_bytes, sizeof(length_bytes));
        cetta_native_sha256_update(&values_sha, wire, wire_len);
        free(wire); wire = NULL;
    }
    cetta_native_sha256_finish_hex(&values_sha, value_digest);
    printf("{\"bytes\":%zu,\"accepted\":%s,\"values\":%u,\"recognize_ns\":%llu,\"recognize_cpu_ns\":%llu,"
           "\"replay_ns\":%llu,\"replay_cpu_ns\":%llu,\"descriptors\":%u,\"gss\":%u,\"nodes\":%u,\"choices\":%u,"
           "\"forest_sha256\":\"%s\",\"value_sha256\":\"%s\"}\n",
        len, result.accepted ? "true" : "false", result.semantic_result_len,
        (unsigned long long)(real_parsed - real_start), (unsigned long long)(cpu_parsed - cpu_start),
        (unsigned long long)(real_replayed - real_parsed), (unsigned long long)(cpu_replayed - cpu_parsed),
        result.forest.work_item_len, result.forest.graph_node_len,
        result.forest.node_len, result.forest.choice_len, forest_digest, value_digest);
    status = 0;
done:
    if (status) fprintf(stderr, "%s\n", error[0] ? error : "expected a readable pattern input file");
    if (file) fclose(file);
    free(bytes);
    ppnative_v1_result_free(&result);
    ppnative_v1_prepared_free(&prepared);
    cetta_ld_parser_pack_v1_free(&compiled);
    pp_table_snapshot_v1_free(&snapshot);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return status;
}
