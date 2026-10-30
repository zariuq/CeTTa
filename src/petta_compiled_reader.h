#ifndef CETTA_PETTA_COMPILED_READER_H
#define CETTA_PETTA_COMPILED_READER_H

#include <stdbool.h>
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
/* The reader of the extended profiles: the PeTTa reader with CeTTa's lists. */
PeTTaCompiledReaderV1 *petta_compiled_reader_v1_new_with_lists(void);
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

/* PeTTa's term reader, for parse and sread: the form LanguageDef of the
 * source reader applied to text that holds exactly one term. */
int petta_compiled_reader_v1_parse_term_bytes_id(
    PeTTaCompiledReaderV1 *reader, const uint8_t *input,
    size_t input_len, TermUniverse *universe, AtomId *out_id,
    char *error_buf, size_t error_buf_size);

int petta_compiled_reader_v1_parse_file_ids(
    PeTTaCompiledReaderV1 *reader, const char *filename,
    TermUniverse *universe, AtomId **out_ids,
    PeTTaCompiledReaderV1Receipt *receipt, char *error_buf,
    size_t error_buf_size);

/* A PeTTa source file read a slice of forms at a time, over a read-only
 * mapping of it, so that a document too large to parse whole keeps only
 * the slice being run.  The pages of the forms read through can be handed
 * back to the system; a rewind reads the file again from its start. */
typedef struct {
    int fd;
    const uint8_t *bytes;
    size_t len;
    size_t pos;
    size_t released;
} PeTTaCompiledDocumentV1;

bool petta_compiled_document_v1_open(PeTTaCompiledDocumentV1 *document,
                                     const char *filename);
/* The next slice, at most MAX_FORMS forms, as ids in *OUT_IDS (the caller
 * frees them): their count, 0 once the document is read through, -1 when
 * the source is outside the reader's language. */
int petta_compiled_document_v1_next(PeTTaCompiledReaderV1 *reader,
                                    PeTTaCompiledDocumentV1 *document,
                                    uint32_t max_forms,
                                    TermUniverse *universe, AtomId **out_ids,
                                    char *error_buf, size_t error_buf_size);
/* The next slice of a document's first pass: every form parsed, and so
 * checked, as petta_compiled_document_v1_next parses it, but only a form
 * whose first token spells `=`, `=%` or `:` built into UNIVERSE; every
 * other form stands as the empty expression, `!` before it when it runs. */
int petta_compiled_document_v1_check_next(PeTTaCompiledReaderV1 *reader,
                                          PeTTaCompiledDocumentV1 *document,
                                          uint32_t max_forms,
                                          TermUniverse *universe,
                                          AtomId **out_ids,
                                          char *error_buf,
                                          size_t error_buf_size);
void petta_compiled_document_v1_release_read(
    PeTTaCompiledDocumentV1 *document);
void petta_compiled_document_v1_rewind(PeTTaCompiledDocumentV1 *document);
void petta_compiled_document_v1_close(PeTTaCompiledDocumentV1 *document);

#endif /* CETTA_PETTA_COMPILED_READER_H */
