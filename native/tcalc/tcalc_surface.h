/* tcalc_surface.h - a tcalc program written in f1r3lang or CHR notation.
 *
 * tcalc's own notation, S-expressions (tcalc_syntax.h), is the canonical form,
 * and its reader is the one that gives a program meaning.  A program written in
 * another notation is translated into it form by form, so a notation is a way
 * of writing and nothing more.  The same rule in the three notations:
 *
 *   tcalc  (rule pass (take (token $x)) (make (passed $x)))       .tcalc
 *   rho    for ($x <= token) { passed!($x) }                       .rho
 *   chr    pass @ token(X) <=> passed(X).                          .chr
 *
 * rho is the notation of f1r3lang, the language formerly called Rholang: a row
 * is a datum on a channel, a rule a receive.  chr is the notation of Constraint
 * Handling Rules: a row is a constraint, a rule a CHR rule.  docs/tcalc.md
 * (section 8) gives the correspondence and says where each notation's usual
 * reading differs from tcalc's. */
#ifndef CETTA_TCALC_SURFACE_H
#define CETTA_TCALC_SURFACE_H

#include "tcalc_core.h"
#include "tcalc_syntax.h"

#include <stdio.h>

typedef enum { TC_SURFACE_TCALC, TC_SURFACE_RHO, TC_SURFACE_CHR } TcSurface;

const char *tc_surface_name(TcSurface surface);
bool tc_surface_from_name(const char *name, TcSurface *surface);
/* The notation a file's suffix names: .rho or .chr; any other is tcalc's own. */
TcSurface tc_surface_of_path(const char *path);

/* A program in the canonical form, one top-level form per line, with the line
 * of the source each was translated from. */
typedef struct {
    char *text;
    size_t length;
    size_t *source_line;   /* source_line[k]: the source line of line k + 1 */
    size_t lines;
} TcCanonical;

/* Translates a program written in `surface` (rho or chr) into the canonical
 * form.  False on a syntax error, described in *error at its place in the
 * source, or when memory runs out (error->line is then 0). */
bool tc_surface_translate(TcSurface surface, const char *text, size_t length,
                          TcCanonical *out, TcSyntaxError *error);
void tc_canonical_free(TcCanonical *canonical);

/* Writes a program given in the canonical form in the notation `surface` (rho
 * or chr).  When the program holds something the notation cannot write (see
 * tcalc_surface.c, section 8), nothing is written, and false is returned with
 * the reason in *error at its line of the canonical text (line 0: memory ran
 * out). */
bool tc_surface_write(TcSurface surface, const char *canonical, size_t length, FILE *out,
                      TcSyntaxError *error);

/* Prints a row in the notation: rho c!(a, f(b)), chr c(a, f(b)), tcalc's own
 * (c a (f b)).  Unknowns print as ?N in every notation. */
void tc_surface_print_row(TcSurface surface, const TcTerms *terms, TcTerm row, FILE *out);

#endif
