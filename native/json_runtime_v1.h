#ifndef CETTA_JSON_RUNTIME_V1_H
#define CETTA_JSON_RUNTIME_V1_H

#include "atom.h"
#include "json_elaboration_plan_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaJsonRuntimeV1 CettaJsonRuntimeV1;

typedef enum {
    CETTA_JSON_RUNTIME_V1_OK = 0,
    CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT,
    CETTA_JSON_RUNTIME_V1_INVALID_LANGUAGE_SOURCE,
    CETTA_JSON_RUNTIME_V1_INVALID_TARGET_SOURCE,
    CETTA_JSON_RUNTIME_V1_OUTSIDE_LANGUAGE_FRAGMENT,
    CETTA_JSON_RUNTIME_V1_OUTSIDE_ELABORATION_PROFILE,
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

/*
 * Parser kernels consume the same prepared ParserPack and return the same
 * occurrence-preserving forest observation.  The dual package admits a
 * result only when the independently scheduled GLL and GLR engines agree.
 */
typedef enum {
    CETTA_JSON_KERNEL_V1_PACKED_GLL = 0,
    CETTA_JSON_KERNEL_V1_PACKED_GLR = 1,
    CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL = 2
} CettaJsonKernelV1;

typedef struct {
    uint32_t recognizer_work_limit;
    uint32_t replay_depth_limit;
    uint32_t result_limit;
    uint32_t elaboration_work_limit;
    uint32_t value_depth_limit;
    CettaJsonKernelV1 kernel;
} CettaJsonRuntimeV1Limits;

void cetta_json_runtime_v1_default_limits(CettaJsonRuntimeV1Limits *limits);

/*
 * Prepare one JSON realization from the authored syntax LanguageDef, lexical
 * profile, and occurrence-preserving value LanguageDef.  The elaboration plan
 * is compiled from both language presentations; its source operations and
 * target constructors remain load-bearing throughout value construction.
 */
CettaJsonRuntimeV1 *cetta_json_runtime_v1_new(
    const uint8_t *language_source,
    size_t language_source_len,
    const uint8_t *profile_source,
    size_t profile_source_len,
    const uint8_t *target_source,
    size_t target_source_len,
    char *error_buf,
    size_t error_buf_size);

void cetta_json_runtime_v1_free(CettaJsonRuntimeV1 *runtime);

/*
 * Parse and elaborate to the canonical occurrence-preserving JSON value.
 * Failure is atomic with respect to the caller's arena and output pointer.
 *
 * A prepared runtime is immutable after construction.  Concurrent calls are
 * safe when each call supplies a distinct Arena and output storage.  The
 * caller must externally synchronize destruction with all parses and borrowed
 * observations of the runtime.
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
const char *cetta_json_runtime_v1_target_digest(
    const CettaJsonRuntimeV1 *runtime);
const CettaJsonElaborationPlanV1 *
cetta_json_runtime_v1_elaboration_plan(
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
const char *cetta_json_kernel_v1_name(CettaJsonKernelV1 kernel);

#endif /* CETTA_JSON_RUNTIME_V1_H */
