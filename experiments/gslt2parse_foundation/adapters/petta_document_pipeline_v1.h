#ifndef CETTA_GSLT2PARSE_PETTA_DOCUMENT_PIPELINE_V1_H
#define CETTA_GSLT2PARSE_PETTA_DOCUMENT_PIPELINE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../native/parser_pack_guard_scalar_exec_v1.h"
#include "../native/parser_pack_guarded_lexical_exec_v1.h"

typedef struct {
    uint8_t *bytes;
    size_t len;
} PeTTaDocumentPipelineV1Response;

typedef struct {
    char lexical_plan_digest[65];
    char guard_plan_digest[65];
    char guarded_plan_digest[65];
    char execution_plan_digest[65];
    uint32_t form_execution_count;
    uint32_t semantic_agreement_count;
    uint32_t source_pass_count;
    uint32_t dfa_scan_count;
    uint32_t splitter_parser_table_build_count;
    uint32_t scalar_form_parser_table_build_count;
    uint32_t scalar_form_witness_parser_family_build_count;
    uint32_t scalar_form_witness_prepared_start_len;
    uint32_t lexical_final_parser_table_build_count;
    uint32_t lexical_witness_parser_family_build_count;
    uint32_t lexical_witness_prepared_start_len;
} PeTTaDocumentPipelineV1LexicalShadowReceipt;

typedef struct PeTTaDocumentPipelineV1Prepared
    PeTTaDocumentPipelineV1Prepared;

void petta_document_pipeline_v1_response_init(
    PeTTaDocumentPipelineV1Response *response);
void petta_document_pipeline_v1_response_free(
    PeTTaDocumentPipelineV1Response *response);
void petta_document_pipeline_v1_lexical_shadow_receipt_init(
    PeTTaDocumentPipelineV1LexicalShadowReceipt *receipt);

/*
 * A prepared pipeline owns one immutable, digest-bound parser configuration.
 * It may parse any number of documents, but it cannot be rebound to a second
 * configuration.  Create a new object when the LanguageDef artifacts change.
 */
PeTTaDocumentPipelineV1Prepared *
petta_document_pipeline_v1_prepared_new(void);
void petta_document_pipeline_v1_prepared_free(
    PeTTaDocumentPipelineV1Prepared *prepared);
uint32_t petta_document_pipeline_v1_prepared_build_count(
    const PeTTaDocumentPipelineV1Prepared *prepared);

bool petta_document_pipeline_v1_prepare(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *regular_compiler_digest,
    const PPGuardScalarExecV1Limits *limits,
    char *error_buf,
    size_t error_buf_size);

bool petta_document_pipeline_v1_prepare_lexical_shadow(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *lexical_nfa_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *guarded_nfa_path,
    const char *regular_compiler_digest,
    const char *guarded_compiler_digest,
    const PPGuardScalarExecV1Limits *scalar_limits,
    const PPGuardedLexExecV1Limits *lexical_limits,
    char *error_buf,
    size_t error_buf_size);

bool petta_document_pipeline_v1_prepared_parse(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size);

bool petta_document_pipeline_v1_prepared_parse_lexical_shadow(
    PeTTaDocumentPipelineV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PeTTaDocumentPipelineV1LexicalShadowReceipt *receipt,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute the closed document splitter and every guarded per-form parse over
 * one decoded scalar buffer.  The response is one canonical finite-Horn
 * ground term; host-language term projection remains outside this C adapter.
 */
bool petta_document_pipeline_v1_parse(
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const char *regular_compiler_digest,
    const PPGuardScalarExecV1Limits *limits,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Preserve the canonical scalar-path response while independently executing
 * every filtered form through the one-scan guarded lexical plan.  A mismatch
 * rejects the call; the separate receipt records only shadow evidence.
 */
bool petta_document_pipeline_v1_parse_lexical_shadow(
    const char *splitter_abi_path,
    const char *form_abi_path,
    const char *lexical_nfa_path,
    const char *guard_nfa_path,
    const char *guard_evidence_path,
    const char *guarded_nfa_path,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const char *regular_compiler_digest,
    const char *guarded_compiler_digest,
    const PPGuardScalarExecV1Limits *scalar_limits,
    const PPGuardedLexExecV1Limits *lexical_limits,
    PeTTaDocumentPipelineV1LexicalShadowReceipt *receipt,
    PeTTaDocumentPipelineV1Response *out,
    char *error_buf,
    size_t error_buf_size);

#endif
