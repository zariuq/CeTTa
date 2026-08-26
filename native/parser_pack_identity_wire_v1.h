#ifndef CETTA_PARSER_PACK_IDENTITY_WIRE_V1_H
#define CETTA_PARSER_PACK_IDENTITY_WIRE_V1_H

#include "language_def_parser_pack_v1.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Canonical opt-in semantic-identity snapshot for a LanguageDef-derived
 * ParserPack.  The PNI1 packet is separate from the neutral forest arrays and
 * is never consulted by prepared parser hot paths.  Production rows carry
 * exact operational descriptors; the target-side checker assigns authored
 * lexical/structural occurrences only after unique resolution against its
 * independently supplied ParserPack plan.
 */
bool cetta_ld_parser_pack_identity_wire_v1_size(
    const CettaLdParserPackV1 *compiled,
    size_t *out_size,
    char *error_buf,
    size_t error_buf_size);

/*
 * Fail-atomic writer.  On failure, out_written is zero and the caller's
 * output buffer is unchanged.
 */
bool cetta_ld_parser_pack_identity_wire_v1_write(
    const CettaLdParserPackV1 *compiled,
    uint8_t *output,
    size_t output_size,
    size_t *out_written,
    char *error_buf,
    size_t error_buf_size);

#endif /* CETTA_PARSER_PACK_IDENTITY_WIRE_V1_H */
