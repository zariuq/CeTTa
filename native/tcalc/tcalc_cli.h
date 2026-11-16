/* tcalc_cli.h - running a tcalc program: `cetta --lang tcalc [--profile P] FILE`.
 *
 * The program is written in tcalc's own notation (.tcalc), in f1r3lang
 * notation (.rho) or in CHR notation (.chr); see tcalc_surface.h.  The syntax
 * named by --syntax, or else the file's suffix, chooses the notation.
 *
 * The profile is a tuple of axis settings separated by ';' or ',':
 *   step=one|round|synchronous|joint|every|maximal|unique|all
 *   ask=after-tells|before-tells   store=multiset|set   matches=symmetric|keys
 *   backend=c|x86-flat             observe=outcomes|run  budget=N
 *   region=NAME+NAME...            (every, unique: the rules they range over)
 * A setting overrides the program's own declaration of that axis.  When no
 * step is named, by the profile or the program, the program runs under every
 * step and each interpretation is printed with its outcomes. */
#ifndef CETTA_TCALC_CLI_H
#define CETTA_TCALC_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Exit status: 0 the outcomes were printed (also when a budget ran out, which
 * the output says); 2 the program, the profile or the version was refused;
 * 1 memory ran out.  `syntax` is "tcalc", "rho" or "chr"; NULL is "tcalc". */
int tcalc_run_text(const char *text, size_t length, const char *syntax, const char *profile,
                   const char *version, FILE *out, FILE *err);
/* As tcalc_run_text on a file; a NULL syntax is the one the suffix names. */
int cetta_tcalc_run_file(const char *path, const char *profile, const char *version,
                         const char *syntax);
/* One step of a program (canonical form) from a state, rows alone in the
 * canonical form: for each interpretation, a line (successor ROW ...) per
 * state reached, (ambiguous (matches N) ROW ...) under unique, and
 * (stepped (successors K) (finished B)).  Exit status as above. */
int tcalc_step_text(const char *program, size_t length, const char *state, size_t state_length,
                    const char *profile, FILE *out, FILE *err);
/* The rows a read of `pattern` (a term in the canonical form, its $x the
 * read's own unknowns) can select in each outcome: for each interpretation, a
 * line (answers (outcome ROW ...) ROW ...) per outcome and the explored line. */
int tcalc_query_text(const char *program, size_t length, const char *pattern, size_t pattern_length,
                     const char *profile, FILE *out, FILE *err);
/* Prints the program of a file, written in syntax `from`, in syntax `to`
 * (tcalc, rho or chr), by way of the canonical form.  Exit status as above. */
int cetta_tcalc_translate_file(const char *path, const char *from, const char *to);

#endif
