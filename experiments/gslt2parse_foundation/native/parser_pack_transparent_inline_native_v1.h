#ifndef CETTA_GSLT2PARSE_PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_TRANSPARENT_INLINE_NATIVE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t source_production_len;
    uint32_t normalized_production_len;
    uint32_t removed_transparent_production_len;
    uint32_t cyclic_transparent_state_len;
    char source_pack_digest[65];
    char normalized_pack_digest[65];
    char compiler_digest[65];
} PPTransparentInlineNativeV1Summary;

/*
 * Normalize the generic ParserPack ABI by inlining only acyclic pp-sub
 * states.  The emitted ABI carries a content-addressed source fiber for every
 * normalized production and is validated before it atomically replaces out.
 */
bool pp_transparent_inline_native_v1_compile_file(
    const char *source_abi_path,
    const char *out_path,
    uint32_t max_paths_per_state,
    uint32_t max_productions,
    PPTransparentInlineNativeV1Summary *summary,
    char *error_buf,
    size_t error_buf_size);

#endif
