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
    CETTA_JSON_VALUE_V1_RESOURCE_LIMIT,
    CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE,
    CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT
} CettaJsonValueV1Status;

/*
 * The canonical representation retains Unicode scalar lists, exact number
 * lexemes, member order, duplicate occurrences, occurrence identities, and
 * explicit source-span provenance.  Members converted from the ordinary
 * representation carry JsonNoSourceSpanV1 instead of an invented location.
 * Ordinary JsonObject/JsonPair/JsonArray/... values realize native strings;
 * the explicit scalar/occurrence view remains the inspection authority.
 * Ordinary strings and keys use their full stored byte lengths in both
 * directions, including U+0000.
 * Conversion and emission use explicit heap continuations. Zero work and
 * depth limits mean no caller budget; zero byte_limit uses the engine's
 * representable string size. Nonzero limits remain explicit resource policy.
 */
bool cetta_json_value_v1_to_ordinary(
    Arena *arena,
    Atom *canonical,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_json_value_v1_from_ordinary(
    Arena *arena,
    Atom *ordinary,
    uint32_t work_limit,
    uint32_t depth_limit,
    Atom **out,
    CettaJsonValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* The from-ordinary validation walk, without materializing the inspection
 * scalar AST. */
bool cetta_json_value_v1_validate_ordinary(Atom *value,
    uint32_t work_limit, uint32_t depth_limit, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size);

/* Copy an accepted ordinary value through the same validation rules used by
 * emission, while retaining native scalar leaves. Output is failure-atomic. */
bool cetta_json_value_v1_normalize_ordinary(Arena *arena, Atom *value, Atom **out,
    CettaJsonValueV1Status *status, char *error_buf, size_t error_buf_size);

/*
 * Emit strict, minified JSON.  The emitted bytes are reparsed by the authored
 * JSON runtime and must recover the exact canonical value before success.
 * On success the caller owns *bytes_out and must free it.
 */
bool cetta_json_value_v1_stringify(
    const CettaJsonRuntimeV1 *runtime,
    Atom *value,
    bool ordinary_input,
    uint32_t work_limit,
    uint32_t depth_limit,
    size_t byte_limit,
    uint8_t **bytes_out,
    size_t *byte_len_out,
    CettaJsonValueV1Status *status,
    char *error_buf,
    size_t error_buf_size);

/* Strict emission without per-value reference parsing or certification. */
bool cetta_json_value_v1_emit(Atom *value, bool ordinary_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size);

/* The same iterative traversal, with two-space indentation. No scalar AST
 * or intermediate minified text is constructed to change whitespace. */
bool cetta_json_value_v1_emit_pretty(Atom *value, bool ordinary_input,
    uint32_t work_limit, uint32_t depth_limit, size_t byte_limit,
    uint8_t **bytes_out, size_t *byte_len_out, CettaJsonValueV1Status *status,
    char *error_buf, size_t error_buf_size);

const char *cetta_json_value_v1_status_name(CettaJsonValueV1Status status);

#endif /* CETTA_JSON_VALUE_V1_H */
