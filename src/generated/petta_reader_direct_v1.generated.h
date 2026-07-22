/* Generated from the composed PeTTa document, form, and projection GSLTs. */
#ifndef CETTA_GENERATED_PETTA_READER_DIRECT_V1_H
#define CETTA_GENERATED_PETTA_READER_DIRECT_V1_H

#include "gslt_direct_reader_v1.h"

extern const GSLTDirectPeTTaReaderV1Plan petta_reader_direct_v1_plan;
const char *petta_reader_direct_v1_program_digest(void);

int petta_reader_direct_v1_parse_bytes_ids(
    const uint8_t *input, size_t input_len,
    const GSLTDirectPeTTaProjectionV1 *projection,
    AtomId **out_ids, GSLTDirectPeTTaReaderV1Receipt *receipt,
    char *error_buf, size_t error_buf_size);

#endif /* CETTA_GENERATED_PETTA_READER_DIRECT_V1_H */
