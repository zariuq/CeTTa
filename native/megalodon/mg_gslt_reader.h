#ifndef CETTA_MEGALODON_GSLT_READER_H
#define CETTA_MEGALODON_GSLT_READER_H

/*
 * Megalodon documents read by the syntax presentations of langdef/megalodon/
 * compiled to C (langdef-compile petta-direct --target c --c-prefix
 * megalodon_reader_v1): a file's tokens (MgNextTokenV1), or its document
 * items after its signatures (MgReaderStartV1, MgReaderNextFileV1,
 * MgReadItemV1), each handed to a callback as the presentations build it.
 * The items are read by the reader of mg_source.h, which keeps Megalodon's
 * positions; the evaluator gets the same items made into parse trees.
 */

#include <stdbool.h>
#include <stddef.h>

#include "../gslt_native_runtime_v1.h"
#include "mg_source.h"

typedef enum {
    MG_GSLT_READ_END = 0,        /* the whole input was read */
    MG_GSLT_READ_ERROR = 1,      /* Megalodon's error: the input is not a document */
    MG_GSLT_READ_REFUSED = 2,    /* the presentations have no reading */
    MG_GSLT_READ_MALFORMED = 3,  /* a step answered outside its contract */
    MG_GSLT_READ_NOMEM = 4,      /* out of memory */
} MgGsltReadStatus;

/* The file's bytes, NUL-terminated, or NULL. */
char *mg_gslt_file_bytes(const char *path, size_t *len);

/* The file's Unicode scalars, or NULL.  UTF-8 is decoded; a byte that is
 * not UTF-8 becomes U+DC80..U+DCFF (surrogate escape), so every text reads
 * back to its bytes. */
GnSourceV1 *mg_gslt_source_load(const char *path);
void mg_gslt_source_free(GnSourceV1 *src);

GnCtx *mg_gslt_reader_context(void);

/* A reading lives until the step that read it ends: keep it with
 * gn_keep_result to use it later. */
typedef void (*MgGsltEmit)(void *ctx, GnVal *reading);

/* SRC's tokens; MG_GSLT_READ_ERROR is lexer.mll's failure, where no rule
 * reads the next lexeme (MG_GSLT_LEXING_FAILURE). */
MgGsltReadStatus mg_gslt_read_tokens(GnCtx *cx, const GnSourceV1 *src, MgGsltEmit emit, void *ctx);
#define MG_GSLT_LEXING_FAILURE "failure: lexing: empty token"

/* The items of SOURCE's current input to its end, each handed to EMIT
 * (NULL: read through) as the presentations build it, in
 * mg_source_context's values.  On MG_GSLT_READ_ERROR, mg_source_error has
 * Megalodon's message; the other failures have a message there too. */
MgGsltReadStatus mg_source_read_items(MgSource *source, MgGsltEmit emit, void *ctx);
GnCtx *mg_source_context(const MgSource *source);

#endif
