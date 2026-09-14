#ifndef CETTA_GSLT_SUPPORT_PROFILE_V1_H
#define CETTA_GSLT_SUPPORT_PROFILE_V1_H

#include "gslt_support_transform_runtime.h"

/* Decode the selected source policies into the existing descriptor. Every
 * returned string, declaration and packet belongs to arena, not the input
 * bytes or symbol table. On failure the output descriptor is zeroed. */
bool cetta_gslt_support_profile_from_source_v1(
    Arena *arena, const uint8_t *source, size_t source_size,
    const char *compiler_sha256, CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size);

/* CSTP transport for the existing descriptor; no provider execution. */
bool cetta_gslt_support_profile_encode_packet_v1(
    Arena *arena, const CettaGsltSupportTransformProfileV1 *profile,
    const uint8_t **packet, size_t *packet_size, char *error, size_t error_size);

/* Exact packet/descriptor agreement is separate from C-only profile use. */
bool cetta_gslt_support_profile_packet_matches_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size);

#endif
