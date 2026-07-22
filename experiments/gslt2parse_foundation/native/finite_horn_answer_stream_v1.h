#ifndef CETTA_GSLT2PARSE_FINITE_HORN_ANSWER_STREAM_V1_H
#define CETTA_GSLT2PARSE_FINITE_HORN_ANSWER_STREAM_V1_H

#include <stdbool.h>
#include <stddef.h>

#include "atom.h"

typedef struct {
    Arena arena;
    Atom **terms;
    size_t len;
    size_t cap;
    char digest[65];
} FHAnswerStreamV1;

void fh_answer_stream_v1_init(FHAnswerStreamV1 *stream);
void fh_answer_stream_v1_free(FHAnswerStreamV1 *stream);

bool fh_answer_stream_v1_read(FHAnswerStreamV1 *stream,
                              const char *path,
                              char *error_buf,
                              size_t error_buf_size);

#endif
