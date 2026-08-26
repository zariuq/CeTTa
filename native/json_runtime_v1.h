#ifndef CETTA_JSON_RUNTIME_V1_H
#define CETTA_JSON_RUNTIME_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaJsonRuntimeV1 CettaJsonRuntimeV1;

typedef enum {
    CETTA_JSON_RUNTIME_V1_OK = 0,
    CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT,
    CETTA_JSON_RUNTIME_V1_INVALID_LANGUAGE_SOURCE,
    CETTA_JSON_RUNTIME_V1_OUTSIDE_LANGUAGE_FRAGMENT,
    CETTA_JSON_RUNTIME_V1_PREPARATION_FAILURE,
    CETTA_JSON_RUNTIME_V1_INVALID_UTF8,
    CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED,
    CETTA_JSON_RUNTIME_V1_AMBIGUOUS,
    CETTA_JSON_RUNTIME_V1_BACKEND_DISAGREEMENT,
    CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT,
    CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE,
    CETTA_JSON_RUNTIME_V1_MALFORMED_VALUE,
    CETTA_JSON_RUNTIME_V1_ALLOCATION_FAILURE,
    CETTA_JSON_RUNTIME_V1_INTERNAL_FAILURE
} CettaJsonRuntimeV1Status;

typedef struct {
    uint32_t recognizer_work_limit;
    uint32_t replay_depth_limit;
    uint32_t result_limit;
    uint32_t elaboration_work_limit;
    uint32_t value_depth_limit;
    bool qualify_with_glr;
} CettaJsonRuntimeV1Limits;

void cetta_json_runtime_v1_default_limits(CettaJsonRuntimeV1Limits *limits);

/*
 * Prepare one JSON realization from the authored LanguageDef and its lexical
 * profile.  Both source documents pass through the independent native GLL and
 * GLR ingress before the LanguageDef-to-ParserPack compiler is invoked.  The
 * resulting tables are built once and remain immutable for the runtime's
 * lifetime.
 */
CettaJsonRuntimeV1 *cetta_json_runtime_v1_new(
    const uint8_t *language_source,
    size_t language_source_len,
    const uint8_t *profile_source,
    size_t profile_source_len,
    char *error_buf,
    size_t error_buf_size);

void cetta_json_runtime_v1_free(CettaJsonRuntimeV1 *runtime);

/*
 * Parse and elaborate to the canonical occurrence-preserving JSON value.
 * Failure is atomic with respect to the caller's arena and output pointer.
 */
bool cetta_json_runtime_v1_parse(
    const CettaJsonRuntimeV1 *runtime,
    Arena *arena,
    const uint8_t *json_bytes,
    size_t json_byte_len,
    const CettaJsonRuntimeV1Limits *limits,
    Atom **out,
    CettaJsonRuntimeV1Status *status,
    char *error_buf,
    size_t error_buf_size);

uint32_t cetta_json_runtime_v1_table_build_count(
    const CettaJsonRuntimeV1 *runtime);

const char *cetta_json_runtime_v1_language_digest(
    const CettaJsonRuntimeV1 *runtime);
const char *cetta_json_runtime_v1_profile_digest(
    const CettaJsonRuntimeV1 *runtime);
const char *cetta_json_runtime_v1_binding_digest(
    const CettaJsonRuntimeV1 *runtime);
const char *cetta_json_runtime_v1_compiler_contract_digest(
    const CettaJsonRuntimeV1 *runtime);
const char *cetta_json_runtime_v1_environment_contract_digest(
    const CettaJsonRuntimeV1 *runtime);
const char *cetta_json_runtime_v1_parser_pack_digest(
    const CettaJsonRuntimeV1 *runtime);

const char *cetta_json_runtime_v1_status_name(
    CettaJsonRuntimeV1Status status);

#endif /* CETTA_JSON_RUNTIME_V1_H */
