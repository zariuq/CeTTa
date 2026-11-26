#ifndef CETTA_FLOAT_NATIVES_H
#define CETTA_FLOAT_NATIVES_H

#include "atom.h"

#include <stdint.h>

/* The operations of lib/float: IEEE 754 binary64 arithmetic as C does it
 * (an overflow is an infinity, an invalid operation a NaN, never an error),
 * exact conversions between floats, integers and ratios of integers, exact
 * comparison of a float with an integer, and decimal text: the shortest
 * digits that read back to the same double, fixed and exponent formats, and
 * reading a float's literal.  Text is always in the C locale.
 *
 * An operation returns its result or refuses; a refusal names what the
 * operation takes. */

typedef struct {
    const char *expected; /* a call outside the signature: what it takes */
} CettaFloatRefusal;

typedef Atom *(*CettaFloatNativeFn)(Arena *a, Atom *const *args,
                                    CettaFloatRefusal *refusal);

typedef struct {
    const char *name;   /* the lib/float name without its float: prefix */
    uint32_t arity;
    CettaFloatNativeFn fn;
} CettaFloatNative;

/* The operation lib/float calls `name`, without the float: prefix, or NULL. */
const CettaFloatNative *cetta_float_native(const char *name);

#endif
