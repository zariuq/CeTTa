#ifndef CETTA_GSLT2PARSE_PARSER_PACK_NATIVE_API_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_NATIVE_API_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PPNATIVE_API_V1_GLL = 0,
    PPNATIVE_API_V1_GLR = 1
} PPNativeApiV1Backend;

typedef struct {
    uint8_t *bytes;
    size_t len;
} PPNativeApiV1Response;

void ppnative_api_v1_response_init(PPNativeApiV1Response *response);
void ppnative_api_v1_response_free(PPNativeApiV1Response *response);

typedef bool (*PPNativeApiV1RuntimeCall)(void *context);

/*
 * Run one native-parser operation while owning the serialized CeTTa atom
 * runtime.  Adapter libraries use this boundary instead of creating a second
 * lock or symbol table around the same process-global runtime.  The callback
 * must not recursively enter this boundary.
 */
bool ppnative_api_v1_with_runtime(
    PPNativeApiV1RuntimeCall call,
    void *context,
    char *error_buf,
    size_t error_buf_size);

/*
 * Parse one UTF-8 source with a provenance-bearing ParserPack ABI artifact.
 * The response is one canonical finite-Horn ground term. Calls are serialized
 * because the surrounding CeTTa atom runtime currently owns a process-global
 * symbol table.
 */
bool ppnative_api_v1_parse(
    PPNativeApiV1Backend backend,
    const char *abi_path,
    const uint8_t *start_text,
    size_t start_text_len,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t work_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeApiV1Response *out,
    char *error_buf,
    size_t error_buf_size);

#endif
