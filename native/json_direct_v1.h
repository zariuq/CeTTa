#ifndef CETTA_JSON_DIRECT_V1_H
#define CETTA_JSON_DIRECT_V1_H

#include "json_runtime_v1.h"
#include <stdio.h>

/* One pass, immediate value construction, and no forest or per-input digest.
 * On failure the output and the caller's arena remain unchanged. The heap
 * continuation has no built-in depth cap: zero means no caller depth budget,
 * and a nonzero depth_limit is an explicit optional resource policy. */
bool cetta_json_direct_v1_parse(Arena *arena, const uint8_t *bytes, size_t len,
    bool ordinary, bool petta_booleans, uint32_t depth_limit, Atom **out,
    CettaJsonRuntimeV1Status *status, char *error, size_t error_size);

/* Validate the complete document while materializing only the subtree at a
 * path of native strings (object members, first occurrence) and nonnegative
 * integers (array indices).  A successful missing path sets found=false and
 * allocates no JSON value.  Failure leaves the caller's arena unchanged. */
bool cetta_json_direct_v1_project(Arena *arena,
    const uint8_t *bytes, size_t len, Atom *path,
    bool ordinary, bool petta_booleans, uint32_t depth_limit,
    Atom **out, bool *found, CettaJsonRuntimeV1Status *status,
    char *error, size_t error_size);

bool cetta_json_direct_v1_number_valid(const uint8_t *bytes, size_t len);

/* The value is borrowed until consume returns. Each row has a separate arena;
 * a consumer retaining a value must copy it. Rows already delivered survive a
 * later parse failure. Empty lines are ignored. No whole-file allocation. */
typedef bool (*CettaJsonLineConsumerV1)(void *context, Atom *value, size_t line);
bool cetta_json_direct_v1_lines(FILE *input, bool ordinary, bool petta_booleans,
    uint32_t depth_limit, CettaJsonLineConsumerV1 consume, void *context,
    size_t *failed_line, char *error, size_t error_size);

#endif
