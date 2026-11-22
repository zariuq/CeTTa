#ifndef CETTA_PETTA_ARITHMETIC_H
#define CETTA_PETTA_ARITHMETIC_H

#include "atom.h"
#include "call_outcome.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * PeTTa's arithmetic is SWI-Prolog's is/2: metta.pl defines every numeric
 * operation as `R is A op B` behind its arith_arg/1 check.  This module is
 * that evaluator, native: integers widen to big integers instead of
 * overflowing, an exact integer quotient stays an integer, any float
 * operand makes the result a float, a rational operand keeps it exact, and
 * the named constants (pi, e, inf, nan, epsilon, cputime, random_float)
 * evaluate.  Errors are SWI's error(Formal, Context) terms, read as
 * (Error Formal Context) with Formal and Context Prolog compounds (not
 * lists), except arith_arg/1's, which is PeTTa's own
 * (petta_semantics_arithmetic_operand_error):
 *
 *   a string or expression operand   type_error(evaluable, X), context _
 *   any other symbol                 type_error(evaluable, Name/0) in is/2
 *   an unbound operand               instantiation_error in is/2
 *   a zero divisor                   evaluation_error(zero_divisor) in F/2
 *   a non-integer operand of mod     type_error(integer, X) in mod/2
 *   a NaN, infinite or subnormal     evaluation_error(undefined |
 *     float result                     float_overflow | float_underflow) in is/2
 *
 * SWI's float flags decide the last two rows; PettaArithmeticFlags carries
 * them, with SWI's defaults unless a program changed them in the embedded
 * engine.
 */

typedef struct {
    /* float_overflow=infinity: an infinite result stands. */
    bool overflow_infinity;
    /* float_zero_div=infinity: X/0.0 is a signed infinity. */
    bool zero_div_infinity;
    /* float_undefined=nan: an undefined result is NaN. */
    bool undefined_nan;
    /* float_underflow=error: a subnormal result raises. */
    bool underflow_error;
    /* prefer_rationals=true: an inexact integer quotient is a rational. */
    bool prefer_rationals;
} PettaArithmeticFlags;

/* The flags in force: the embedded engine's when one runs (a program sets
 * them there with set_prolog_flag/2), else SWI's defaults. */
PettaArithmeticFlags petta_arithmetic_flags(void);

/* Read by petta_arithmetic_flags when the embedded engine is linked. */
bool petta_libpl_arithmetic_flags(PettaArithmeticFlags *flags)
    __attribute__((weak));

/* Evaluate Function(Args...) as is/2 does, or, with no function, the single
 * operand args[0].  The functions are those PeTTa's operations use:
 * + - * / mod // ** min max abs sqrt exp log sin cos tan asin acos atan
 * truncate ceil floor round.  *out is the value or the raised error; false
 * only when the arena or the function is unusable. */
bool petta_arithmetic_evaluate(Arena *arena, const char *function,
                               Atom **args, uint32_t nargs,
                               CettaCallOutcome *out);

/* Compare args[0] with args[1] by Relation (< > =< >= =:= =\=), as SWI's
 * arithmetic comparison does: true or false, or the error evaluating an
 * operand raised. */
bool petta_arithmetic_compare(Arena *arena, const char *relation,
                              Atom **args, CettaCallOutcome *out);

#endif
