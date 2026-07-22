#ifndef CETTA_GSLT2PARSE_HE_DOCUMENT_PIPELINE_V1_H
#define CETTA_GSLT2PARSE_HE_DOCUMENT_PIPELINE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../native/parser_atom_projection_v1.h"
#include "../native/parser_pack_guarded_lexical_exec_v1.h"

typedef struct HEDocumentPipelineV1Prepared HEDocumentPipelineV1Prepared;

typedef struct {
    char base_pack_digest[65];
    char lexical_plan_digest[65];
    char guard_plan_digest[65];
    char guarded_plan_digest[65];
    char execution_plan_digest[65];
    char cursor_certificate_digest[65];
    char cursor_program_digest[65];
    char action_compiler_digest[65];
    char action_answer_digest[65];
    char action_program_digest[65];
    char projection_action_program_digest[65];
    char semantic_mask_compiler_digest[65];
    char semantic_mask_answer_digest[65];
    char semantic_mask_binding_digest[65];
    char atom_projection_source_digest[65];
    char atom_projection_reader_digest[65];
    char atom_projection_compiler_digest[65];
    char atom_projection_plan_digest[65];
    uint32_t projection_action_production_len;
    uint32_t semantic_mask_bound_terminal_len;
    uint32_t prepared_build_count;
    uint32_t terminal_value_len;
    uint32_t action_execution_len;
    uint64_t action_instruction_len;
    uint32_t value_program_run_len;
    uint64_t value_parser_work_item_len;
    uint32_t semantic_mask_run_len;
    uint32_t semantic_mask_input_scalar_len;
    uint32_t semantic_mask_retained_scalar_len;
    uint64_t semantic_mask_work_item_len;
    uint32_t prepared_fallback_run_len;
    uint64_t prepared_fallback_work_item_len;
    PPGuardedLexCursorV1Receipt parse;
} HEDocumentPipelineV1Receipt;

typedef struct {
    Arena arena;
    Atom **atoms;
    uint32_t atom_len;
    bool accepted;
    HEDocumentPipelineV1Receipt receipt;
} HEDocumentPipelineV1Result;

void he_document_pipeline_v1_result_init(
    HEDocumentPipelineV1Result *result);
void he_document_pipeline_v1_result_free(
    HEDocumentPipelineV1Result *result);

HEDocumentPipelineV1Prepared *he_document_pipeline_v1_prepared_new(void);
void he_document_pipeline_v1_prepared_free(
    HEDocumentPipelineV1Prepared *prepared);
uint32_t he_document_pipeline_v1_prepared_build_count(
    const HEDocumentPipelineV1Prepared *prepared);

/*
 * Bind an immutable LanguageDef-derived parser and host-Atom projection.
 * All file artifacts are read during preparation; document parsing performs
 * no table construction and no presentation I/O.
 */
bool he_document_pipeline_v1_prepare(
    HEDocumentPipelineV1Prepared *prepared,
    const char *abi_path,
    const char *lexical_nfa_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *guarded_nfa_path,
    const char *action_answer_path,
    const char *semantic_mask_path,
    const char *regular_compiler_digest,
    const char *guarded_compiler_digest,
    const char *action_compiler_digest,
    const char *semantic_mask_compiler_digest,
    const Atom *atom_projection_artifact,
    const PPGuardedLexExecV1Limits *limits,
    uint32_t atom_projection_depth_limit,
    char *error_buf,
    size_t error_buf_size);

/* One UTF-8 decode and one certified deterministic cursor pass. */
bool he_document_pipeline_v1_parse(
    HEDocumentPipelineV1Prepared *prepared,
    const uint8_t *input,
    size_t input_len,
    HEDocumentPipelineV1Result *out,
    char *error_buf,
    size_t error_buf_size);

/* Same semantics and counters, with the exact transition trace enabled. */
bool he_document_pipeline_v1_parse_audited(
    HEDocumentPipelineV1Prepared *prepared,
    const uint8_t *input,
    size_t input_len,
    HEDocumentPipelineV1Result *out,
    char *error_buf,
    size_t error_buf_size);

#endif
