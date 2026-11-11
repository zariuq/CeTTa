#ifndef CETTA_MEGALODON_FRONT_H
#define CETTA_MEGALODON_FRONT_H

/*
 * Megalodon's front-end term forms (syntax.ml): the terms its elaborator
 * builds, before any kernel conversion.  They live in a kernel store, but
 * in the front end's own conventions: a type variable TpVar i is the i-th
 * declared type variable of the enclosing sections (counted from the
 * outermost), Set is base type 0, and a polymorphic declaration has no
 * type binders of its own.  The printers give Megalodon's spellings:
 * tp_to_sexpr / tm_to_sexpr / pf_to_sexpr for -sexprinfo rows, and
 * tp_to_str / tm_to_str for messages.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mg_core.h"

typedef struct {
    char *data;
    size_t len, cap;
    bool failed;
} MgBuf;

void mg_buf_putc(MgBuf *b, char c);
void mg_buf_puts(MgBuf *b, const char *s);
void mg_buf_printf(MgBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* The text so far, NUL-terminated ("" when allocation failed). */
const char *mg_buf_text(MgBuf *b);
void mg_buf_reset(MgBuf *b);
void mg_buf_free(MgBuf *b);

void mg_front_tp_sexpr(MgBuf *b, const MgTp *a);
void mg_front_tm_sexpr(MgBuf *b, const MgStore *s, const MgTm *m);
void mg_front_pf_sexpr(MgBuf *b, const MgStore *s, const MgPf *d);
void mg_front_tp_str(MgBuf *b, const MgTp *a);
/* A type in -sexprinfo spelling, e.g. "(AR (SET) (PROP))"; NULL when it
 * does not parse. */
MgTp *mg_front_parse_tp(MgStore *s, const char *text);
void mg_front_tm_str(MgBuf *b, const MgStore *s, const MgTm *m);
void mg_front_pf_str(MgBuf *b, const MgStore *s, const MgPf *d);

#endif
