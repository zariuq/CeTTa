/*
 * p1_load.h -- t-ISA profile p1 programs from their text.
 *
 * The text of a p1 program is what tisa_print_program prints: HOL Light
 * terms of the datatypes in tcalc/tisa/hol/tisa_p1.ml (TISA_TEXT_HOL), or
 * the S-expressions with the same constructor names (TISA_TEXT_SEXPR). The
 * loader accepts exactly that text, character for character, so that a
 * loaded program prints back to the text it was read from:
 *
 *   tisa_print_program(f, style, &loaded.prog) writes text[0..end).
 *
 * Every other input is refused with the byte offset, line and column of the
 * first character that leaves the grammar, and what was expected there.
 * Numbers must also fit the fields of tisa.h that hold them; a number the
 * printer would spell differently (a leading zero, (SOME 255), the word
 * index of a frame load past 2^28) is refused, since it could not come back.
 *
 * The loader checks the grammar, not the meaning: admission (tisa_admit)
 * decides whether a loaded program is a p1 program. Statements are held in
 * blocks that grow without a fixed limit; their addresses never move, so
 * the program's statement pointers stay valid until p1_free.
 */
#pragma once

#include "tisa.h"
#include "tisa_print.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct P1Block P1Block;

typedef struct {
    TcIsaProgram prog;    /* declarations and root; statements live in the blocks */
    P1Block     *blocks;  /* statement storage, released by p1_free */
    size_t       nstmts;  /* statements in the tree (TSkip is the null statement, not counted) */
    size_t       depth;   /* deepest statement nesting */
} P1Program;

typedef struct {
    size_t   offset;      /* byte offset of the first character outside the grammar */
    unsigned line, column;/* the same position, 1-based */
    char     message[240];
} P1Error;

/* Parses one program in the given style starting at text[0]. On success
 * *end is the offset just after its closing parenthesis. */
bool p1_parse_prefix(const char *text, size_t len, TcIsaTextStyle style,
                     P1Program *out, size_t *end, P1Error *err);

/* text[0..len) must be exactly one program. */
bool p1_parse(const char *text, size_t len, TcIsaTextStyle style, P1Program *out, P1Error *err);

/* The style of the program starting at text[0], from the character after
 * "(TProg ": '[' is HOL, '(' is S-expression. False if neither. */
bool p1_detect_style(const char *text, size_t len, TcIsaTextStyle *style);

/* Reads a file holding exactly one program in either style, followed by at
 * most one newline (files written by line-based tools end with one).
 * The program's name is left NULL. */
bool p1_load_file(const char *path, P1Program *out, TcIsaTextStyle *style, P1Error *err);

/* Prints the error as "PATH:LINE:COLUMN: MESSAGE" (PATH may be NULL). */
void p1_print_error(FILE *f, const char *path, const P1Error *err);

void p1_free(P1Program *p);

#ifdef __cplusplus
}
#endif
