#ifndef CETTA_PARSER_TOKEN_RUNTIME_V1_H
#define CETTA_PARSER_TOKEN_RUNTIME_V1_H

#include "language_def_parser_pack_v1.h"
#include "parser_token_profile_v1.h"
#include "parser_pack_table_snapshot_v1.h"

/* The compiled pack and token profile are borrowed, immutable authorities.
 * The runtime owns its automaton and extended GLL/GLR tables. */
typedef struct { void *implementation; } CettaParserTokenRuntimeV1;

typedef enum {
    CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED = 0,
    CETTA_PARSER_TOKEN_SCAN_V1_REJECTED,
    CETTA_PARSER_TOKEN_SCAN_V1_BYTE_SPLIT,
    CETTA_PARSER_TOKEN_SCAN_V1_WORK_LIMIT
} CettaParserTokenScanOutcomeV1;

typedef struct {
    Arena arena;
    CettaLpNativeUtf8ScalarBuffer source;
    CettaLpNativeUtf8Lattice lattice;
    CettaLpNativeUtf8LatticeEdge *edges;
    uint32_t *start_offsets;
    uint32_t *terminal_ids;
    Atom **witness_values;
    uint32_t witness_len;
    CettaParserTokenScanOutcomeV1 outcome;
    uint32_t failure_scalar;
    uint32_t failure_byte;
    uint64_t work_used;
    char binding_sha256[65];
    char detail[256];
    /* A grammar with layout is scanned from its text with the inserted
       braces and separators written out, three bytes each (" { "); these
       are the source bytes before which they were inserted, in order, so a
       position in the scanned text maps back to the source. */
    uint32_t *layout_inserted;
    uint32_t layout_inserted_len;
} CettaParserTokenScanV1;

void cetta_parser_token_runtime_v1_init(CettaParserTokenRuntimeV1 *runtime);
void cetta_parser_token_runtime_v1_free(CettaParserTokenRuntimeV1 *runtime);
bool cetta_parser_token_runtime_v1_prepare(
    CettaParserTokenRuntimeV1 *out, const CettaLdParserPackV1 *compiled,
    const CettaParserTokenProfileV1 *profile, uint32_t work_limit,
    char *error, size_t error_size);
/* Store and restore the same immutable lexer and recognizer decisions. The
 * enclosing parser owner retains the compiled pack, bindings and profile. */
bool cetta_parser_token_runtime_v1_export_program(
    const CettaParserTokenRuntimeV1 *runtime, PPTableSnapshotV1 *out,
    Arena *arena, Atom **metadata, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_import_program(
    CettaParserTokenRuntimeV1 *out, const CettaLdParserPackV1 *compiled,
    const CettaParserTokenProfileV1 *profile, const PPTableSnapshotV1 *snapshot,
    Atom *metadata, char *error, size_t error_size);
/* Explicit metadata audit against the stored immutable profile. This does
 * not reconstruct automata or establish regex/DFA language equivalence. */
bool cetta_parser_token_runtime_v1_check_metadata(
    const CettaParserTokenRuntimeV1 *, char *, size_t);

void cetta_parser_token_scan_v1_init(CettaParserTokenScanV1 *scan);
void cetta_parser_token_scan_v1_free(CettaParserTokenScanV1 *scan);
/* The source position of a position in the scanned text: the same for a
 * grammar without layout; for one with layout, the inserted bytes before it
 * are taken away (a position inside an insertion is the source byte the
 * insertion stands before).  Every inserted byte is ASCII, so the same shift
 * maps a scalar position. */
void cetta_parser_token_scan_v1_source_position(const CettaParserTokenScanV1 *scan,
                                                uint32_t *scalar, uint32_t *byte);
/* Invalid authorities fail; an input refusal is returned as an explicit
 * outcome with its original source position. Replacement is atomic. */
bool cetta_parser_token_runtime_v1_scan(
    const CettaParserTokenRuntimeV1 *runtime,
    const uint8_t *bytes, size_t length, uint64_t work_limit,
    CettaParserTokenScanV1 *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_parse(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    PPNativeV1Result *out, char *error, size_t error_size);
/* The owner keeps the runtime immutable. Parse only a scan freshly produced
 * by this runtime; external lattice data uses the checked operations above. */
bool cetta_parser_token_runtime_v1_scan_values(
    const CettaParserTokenRuntimeV1 *runtime,
    const uint8_t *bytes, size_t length, uint64_t work_limit,
    CettaParserTokenScanV1 *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_parse_values(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    PPNativeV1Result *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_parse_values_selected(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    const PPNativeV1FamilySelection *selection,
    PPNativeV1Result *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_build_predictive(
    const CettaParserTokenRuntimeV1 *runtime,
    CettaLpNativePredictiveProgram *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_check_predictive(
    const CettaParserTokenRuntimeV1 *, const CettaLpNativePredictiveProgram *, char *, size_t);
bool cetta_parser_token_runtime_v1_parse_predictive_values(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaLpNativePredictiveProgram *program, const CettaParserTokenScanV1 *scan,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    PPNativeV1Result *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_parse_predictive_values_selected(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaLpNativePredictiveProgram *program, const CettaParserTokenScanV1 *scan,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    const PPNativeV1FamilySelection *selection,
    PPNativeV1Result *out, char *error, size_t error_size);
bool cetta_parser_token_runtime_v1_materialize(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaParserTokenScanV1 *scan, PPNativeV1Result *result,
    uint32_t item_limit, char *error, size_t error_size);

#endif
