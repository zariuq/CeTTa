#ifndef CETTA_JSON_DIRECT_V1_H
#define CETTA_JSON_DIRECT_V1_H

#include "json_runtime_v1.h"
#include <stdio.h>

/* One pass, immediate value construction, and no forest or per-input digest.
 * On failure the output and the caller's arena remain unchanged. The heap
 * continuation has no built-in depth cap: zero means no caller depth budget,
 * and a nonzero depth_limit is an explicit optional resource policy. */
bool cetta_json_direct_v1_parse(Arena *arena, const uint8_t *bytes, size_t len,
    bool legacy, bool petta_booleans, uint32_t depth_limit, Atom **out,
    CettaJsonRuntimeV1Status *status, char *error, size_t error_size);

bool cetta_json_direct_v1_number_valid(const uint8_t *bytes, size_t len);

/* The value is borrowed until consume returns. Each row has a separate arena;
 * a consumer retaining a value must copy it. Rows already delivered survive a
 * later parse failure. Empty lines are ignored. No whole-file allocation. */
typedef bool (*CettaJsonLineConsumerV1)(void *context, Atom *value, size_t line);
bool cetta_json_direct_v1_lines(FILE *input, bool legacy, bool petta_booleans,
    uint32_t depth_limit, CettaJsonLineConsumerV1 consume, void *context,
    size_t *failed_line, char *error, size_t error_size);

#endif
