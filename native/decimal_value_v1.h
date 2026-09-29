#ifndef CETTA_DECIMAL_VALUE_V1_H
#define CETTA_DECIMAL_VALUE_V1_H

#include <stdbool.h>

/*
 * Exact values of decimal numerals, independent of their spelling.
 *
 * A rational n/d (optional sign, decimal digits, a slash, a nonzero decimal
 * denominator) denotes the fraction in lowest terms with a positive
 * denominator: 2/4 and +1/2 are 1/2, -0/5 is 0/1.
 *
 * A real (optional sign, digits with a fraction, an exponent or both, such as
 * 1.5, 007E2, 2.50e-3) denotes a decimal fraction, spelled canonically: the
 * digits without leading or trailing zeros and the position of the point
 * decide the spelling, as in the Number to String conversion of ECMA-262.
 * A value whose point lies within 21 digits after or 6 zeros before its
 * digits is written positionally, with .0 when it is integral (100.0, 1.02,
 * 0.0015); any other value is written d.dddEn or dEn (1.23456E80, 1E-7).
 * Zero is 0.0 whatever its sign.
 *
 * Results are malloc'd decimal texts owned by the caller.  Both functions
 * reject text outside their numeral shape.
 */
bool cetta_decimal_rational_value_v1(
    const char *text, char **numerator, char **denominator);

bool cetta_decimal_real_canonical_v1(const char *text, char **out);

/* The nearest double to a real, when it is finite and keeps a nonzero value
 * nonzero; zero of either sign is 0.0.  Otherwise false: the value has no
 * faithful double. */
bool cetta_decimal_real_double_v1(const char *text, double *out);

/* The canonical real spelling of the shortest decimal that reads back as the
 * given finite double: 0.1 is 0.1, 0.30000000000000004 keeps its digits. */
bool cetta_decimal_real_from_double_v1(double value, char **out);

#endif /* CETTA_DECIMAL_VALUE_V1_H */
