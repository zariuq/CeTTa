#ifndef CETTA_JSON_VALUE_V1_H
#define CETTA_JSON_VALUE_V1_H

#include "json_runtime_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CETTA_JSON_VALUE_V1_OK = 0,
    CETTA_JSON_VALUE_V1_BAD_ARGUMENT,
    CETTA_JSON_VALUE_V1_MALFORMED_VALUE,
    CETTA_JSON_VALUE_V1_INVALID_UTF8,
    CETTA_JSON_VALUE_V1_UNREPRESENTABLE_LEGACY_STRING,
    CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
    CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
    CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT
} CettaJsonValueV1Status;

/*
 * The canonical representation retains Unicode scalar lists, exact number
 * lexemes, member order, duplicate occurrences, occurrence identities, and
 * explicit source-span provenance.  Members converted from the historical
 * representation carry JsonNoSourceSpanV1 instead of an invented location.
 * The historical JsonObject/JsonPair/JsonArray/... representation is a
 * compatibility codec over that value, not its definition.
 */
bool cetta_json_value_v1_to_legacy(
    Arena *arena,
    Atom *canonical,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_json_value_v1_from_legacy(
    Arena *arena,
    Atom *legacy,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/*
 * Emit strict, minified JSON.  The emitted bytes are reparsed by the authored
 * JSON runtime and must recover the exact canonical value before success.
 * On success the caller owns *bytes_out and must free it.
 */
bool cetta_json_value_v1_stringify(
    const CettaJsonRuntimeV1 *runtime,
    Atom *value,
    bool legacy_input,
    uint32_t work_limit,
    uint32_t depth_limit,
    size_t byte_limit,
    uint8_t **bytes_out,
    size_t *byte_len_out,
    CettaJsonValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_json_value_v1_status_name(CettaJsonValueV1Status status);

#endif /* CETTA_JSON_VALUE_V1_H */
