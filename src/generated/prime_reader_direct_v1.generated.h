/* Generated from the composed syntax, scalar-class, and projection GSLTs. */
#ifndef CETTA_GENERATED_PRIME_READER_DIRECT_V1_H
#define CETTA_GENERATED_PRIME_READER_DIRECT_V1_H

#include "gslt_direct_reader_v1.h"

extern const GSLTDirectPrefixReaderV1Plan prime_reader_direct_v1_plan;
const char *prime_reader_direct_v1_program_digest(void);

int prime_reader_direct_v1_parse_bytes_ids(
    const uint8_t *input, size_t input_len,
    const GSLTDirectPrefixProjectionV1 *projection,
    AtomId **out_ids, GSLTDirectPrefixReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size);

#endif /* CETTA_GENERATED_PRIME_READER_DIRECT_V1_H */
