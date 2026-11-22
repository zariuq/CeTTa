#ifndef CETTA_MEGALODON_GSLT_READER_H
#define CETTA_MEGALODON_GSLT_READER_H

/*
 * Megalodon documents read by the syntax presentations of langdef/megalodon/
 * compiled to C (langdef-compile petta-direct --target c: their relations
 * read as a rule module that rulec compiles), as one instance of the rule
 * runtime with this reader around it (mg_reader_instance.c): a file's tokens
 * (MgNextTokenV1), or its document items after its signatures
 * (MgReaderStartV1, MgReaderNextFileV1, MgReadItemV1), each handed to a
 * callback as the presentations build it.  The items are read by the
 * reader of mg_source.h, which keeps Megalodon's positions; the evaluator
 * gets the same items made into parse trees.
 *
 * A reading keeps its own heap (its values live between steps), so readings
 * run side by side, on any thread.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* A reading: atoms the presentations built, valid while the callback that
 * receives it runs.  mg_gslt_node tells what a value is; an application's
 * children are read with mg_gslt_child. */
typedef struct MgGsltValue MgGsltValue;
typedef enum {
    MG_GSLT_INT,          /* integer */
    MG_GSLT_SYMBOL,       /* name, len */
    MG_GSLT_STRING,       /* name, len: the string's bytes */
    MG_GSLT_APPLY,        /* (name child ...): name, len, arity */
    MG_GSLT_UNREAD,       /* the input not read yet: (cons (cp N) rest), name and arity as an application's */
} MgGsltNodeKind;
typedef struct {
    MgGsltNodeKind kind;
    int64_t integer;
    const char *name;
    size_t len, arity;
} MgGsltNode;
void mg_gslt_node(const MgGsltValue *v, MgGsltNode *out);
const MgGsltValue *mg_gslt_child(const MgGsltValue *v, size_t i);
typedef void (*MgGsltEmit)(void *ctx, const MgGsltValue *reading);

/* The tokens of BYTES; MG_GSLT_READ_ERROR is lexer.mll's failure, where no
 * rule reads the next lexeme (MG_GSLT_LEXING_FAILURE). */
MgGsltReadStatus mg_gslt_read_tokens(const char *bytes, size_t len, MgGsltEmit emit, void *ctx);
#define MG_GSLT_LEXING_FAILURE "failure: lexing: empty token"

/* The items of SOURCE's current input to its end, each handed to EMIT
 * (NULL: read through) as the presentations build it.  On
 * MG_GSLT_READ_ERROR, mg_source_error has Megalodon's message; the other
 * failures have a message there too. */
MgGsltReadStatus mg_source_read_items(MgSource *source, MgGsltEmit emit, void *ctx);

/* The reading work of this thread so far: the rules of the presentations
 * applied, and the calls no rule answered (counted when the instance is
 * built with RC_COUNT; 0 otherwise).  Deterministic, so that a gate can
 * compare the work of inputs of different sizes. */
uint64_t mg_gslt_reader_work(void);

/* With a path prefix (NULL: none), this thread's later steps write their
 * derivations as rulec's trace events (16-bit, little endian;
 * tools/rulec/rulec_tracecheck reads them), the steps of each entry R:BITS
 * one after another in PREFIX.R:BITS.  False when a file cannot be written. */
bool mg_gslt_reader_trace(const char *prefix);

#endif
