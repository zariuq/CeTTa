#ifndef CETTA_PETTA_COMPILED_READER_H
#define CETTA_PETTA_COMPILED_READER_H

#include <stddef.h>
#include <stdint.h>

#include "term_universe.h"

typedef struct PeTTaCompiledReaderV1 PeTTaCompiledReaderV1;

typedef struct {
    char program_digest[65];
    char fragment[40];
    char profile[32];
    char splitter_syntax_digest[65];
    char splitter_class_digest[65];
    char form_syntax_digest[65];
    char form_class_digest[65];
    char projection_digest[65];
    char compiler_digest[65];
    uint32_t source_pass_count;
    uint32_t form_replay_count;
    uint32_t input_byte_len;
    uint32_t form_len;
    uint32_t token_len;
    uint32_t shift_len;
    uint32_t reduce_len;
} PeTTaCompiledReaderV1Receipt;

PeTTaCompiledReaderV1 *petta_compiled_reader_v1_new(void);
void petta_compiled_reader_v1_free(PeTTaCompiledReaderV1 *reader);

bool petta_compiled_reader_v1_prepare(
    PeTTaCompiledReaderV1 *reader, char *error_buf,
    size_t error_buf_size);

int petta_compiled_reader_v1_parse_bytes_ids(
    PeTTaCompiledReaderV1 *reader, const uint8_t *input,
    size_t input_len, TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

int petta_compiled_reader_v1_parse_text_ids(
    PeTTaCompiledReaderV1 *reader, const char *text,
    TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

int petta_compiled_reader_v1_parse_file_ids(
    PeTTaCompiledReaderV1 *reader, const char *filename,
    TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

#endif /* CETTA_PETTA_COMPILED_READER_H */
