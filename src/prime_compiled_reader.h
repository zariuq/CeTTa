#ifndef CETTA_PRIME_COMPILED_READER_H
#define CETTA_PRIME_COMPILED_READER_H

#include <stddef.h>
#include <stdint.h>

#include "term_universe.h"

typedef struct PrimeCompiledReaderV1 PrimeCompiledReaderV1;

typedef struct {
    char program_digest[65];
    char fragment[48];
    char profile[32];
    char syntax_digest[65];
    char scalar_class_digest[65];
    char projection_digest[65];
    char compiler_digest[65];
    uint32_t source_pass_count;
    uint32_t input_byte_len;
    uint32_t token_len;
    uint32_t prefix_len;
    uint32_t shift_len;
    uint32_t reduce_len;
} PrimeCompiledReaderV1Receipt;

PrimeCompiledReaderV1 *prime_compiled_reader_v1_new(void);
void prime_compiled_reader_v1_free(PrimeCompiledReaderV1 *reader);

bool prime_compiled_reader_v1_prepare(
    PrimeCompiledReaderV1 *reader, char *error_buf,
    size_t error_buf_size);

int prime_compiled_reader_v1_parse_bytes_ids(
    PrimeCompiledReaderV1 *reader, const uint8_t *input,
    size_t input_len, TermUniverse *universe, AtomId **out_ids,
    PrimeCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

int prime_compiled_reader_v1_parse_text_ids(
    PrimeCompiledReaderV1 *reader, const char *text,
    TermUniverse *universe, AtomId **out_ids,
    PrimeCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

int prime_compiled_reader_v1_parse_file_ids(
    PrimeCompiledReaderV1 *reader, const char *filename,
    TermUniverse *universe, AtomId **out_ids,
    PrimeCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

#endif /* CETTA_PRIME_COMPILED_READER_H */
