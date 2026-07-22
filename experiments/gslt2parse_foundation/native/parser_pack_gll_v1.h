#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GLL_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GLL_V1_H

#include "parser_pack_native_v1.h"

bool ppgll_v1_parse(const PPABIV1Pack *pack,
                    const Atom *start_state,
                    const uint8_t *input_bytes,
                    size_t input_byte_len,
                    uint32_t descriptor_limit,
                    uint32_t replay_depth,
                    uint32_t result_limit,
                    PPNativeV1Result *out,
                    char *error_buf,
                    size_t error_buf_size);

bool ppgll_v1_parse_scalar_view(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size);

bool ppgll_v1_prepared_parse(
    const PPNativeV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size);

bool ppgll_v1_prepared_parse_scalar_view(
    const PPNativeV1Prepared *prepared,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size);

#endif
