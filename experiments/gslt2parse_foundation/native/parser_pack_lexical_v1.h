#ifndef CETTA_GSLT2PARSE_PARSER_PACK_LEXICAL_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_LEXICAL_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_native_v1.h"
#include "regular_span_dfa_v1.h"

typedef struct {
    uint32_t tag_index;
    uint32_t state_id;
    uint32_t terminal_index;
    Atom *tag;
    char *state_canonical;
} PPLexV1Entry;

typedef struct {
    Arena arena;
    PPLexV1Entry *entries;
    PPNativeV1TerminalExtension *terminals;
    uint32_t *tag_terminal_ids;
    uint32_t entry_len;
    char base_pack_digest[65];
    char regular_compiler_digest[65];
    char nfa_answer_digest[65];
    char plan_digest[65];
} PPLexV1Plan;

typedef enum {
    PPLEX_V1_WITNESS_COMPLETED = 0,
    PPLEX_V1_WITNESS_RECOGNIZER_LIMIT = 1,
    PPLEX_V1_WITNESS_RECOGNITION_MISMATCH = 2,
    PPLEX_V1_WITNESS_NONFUNCTIONAL = 3,
    PPLEX_V1_WITNESS_REPLAY_DEPTH = 4,
    PPLEX_V1_WITNESS_RESULT_LIMIT = 5
} PPLexV1WitnessOutcome;

typedef struct {
    Arena arena;
    Atom **values;
    uint32_t len;
    PPLexV1WitnessOutcome outcome;
    uint32_t parser_runs;
    uint64_t work_item_len;
    uint32_t source_pass_count;
    char plan_digest[65];
    char detail[512];
} PPLexV1WitnessTable;

void pplex_v1_plan_init(PPLexV1Plan *plan);
void pplex_v1_plan_free(PPLexV1Plan *plan);

bool pplex_v1_plan_build(
    const PPABIV1Pack *pack,
    Atom *const *tags,
    uint32_t tag_len,
    const char *regular_compiler_digest,
    const char *nfa_answer_digest,
    PPLexV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_grammar_build(const PPABIV1Pack *pack,
                            const PPLexV1Plan *plan,
                            CettaLpNativeGrammar *grammar,
                            char *error_buf,
                            size_t error_buf_size);

void pplex_v1_witness_table_init(PPLexV1WitnessTable *table);
void pplex_v1_witness_table_free(PPLexV1WitnessTable *table);

bool pplex_v1_witness_family_prepare(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    PPNativeV1PreparedStartFamily *family,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_witness_table_build_gll(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_witness_table_build_glr(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_witness_table_build_gll_prepared(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    const PPNativeV1PreparedStartFamily *family,
    const PPNativeV1TerminalPlan *terminals,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_witness_table_build_glr_prepared(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *plan,
    const RSDFAV1ScanResult *scan,
    const PPNativeV1PreparedStartFamily *family,
    const PPNativeV1TerminalPlan *terminals,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPLexV1WitnessTable *out,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_gll_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size);

bool pplex_v1_glr_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size);

#endif
