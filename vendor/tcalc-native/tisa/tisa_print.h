/*
 * tisa_print.h -- t-ISA profile p1 programs as text.
 *
 * Two spellings of one syntax: HOL Light terms of the datatypes in
 * hol/tisa_p1.ml, and S-expressions with the same constructor names for
 * MeTTa. Only the fields an instruction uses are printed: the rest are not
 * part of its meaning (SEMANTICS.txt, section 4).
 */
#pragma once
#include "tisa.h"
#include "tisa_x86.h"
#include <stdio.h>

typedef enum { TISA_TEXT_HOL, TISA_TEXT_SEXPR } TcIsaTextStyle;

/* TProg decls init root. False for a statement or instruction that has no
 * p1 meaning (an unknown kind or opcode). */
bool tisa_print_program(FILE *f, TcIsaTextStyle style, const TcIsaProgram *p);
bool tisa_print_stmt(FILE *f, TcIsaTextStyle style, const TcIsaStmt *s);

/* The statement each body span of the p1 image realizes, in the emitter's
 * order, as child indices joined by '.' ("" is the root): SEQ first 0,
 * second 1; IF then 1, else 2; loop body 0. IF, IF-ELSE and the loop spans
 * carry their statement's own path. Writes at most cap paths and returns how
 * many there are. */
size_t tisa_span_paths(const TcIsaStmt *s, char (*paths)[256], size_t cap);

/* One record for the HOL image prover (hol/tisa_x86_image.ml):
 *   tisa_image "name" `PROGRAM` "HEX" [(offset,length,"TEMPLATE","path"); ...];;
 * with path "-" for the wrapper and handler spans. False if the program has
 * no p1 text or its spans do not match its statements. */
bool tisa_print_image_record(FILE *f, const char *name, const TcIsaProgram *p, const TcIsaX86 *code);
