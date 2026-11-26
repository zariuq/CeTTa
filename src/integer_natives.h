#ifndef CETTA_INTEGER_NATIVES_H
#define CETTA_INTEGER_NATIVES_H

#include "atom.h"

#include <stdint.h>

/* The operations of lib/integer: integers of any size (machine or big) as
 * two's complement bit strings extended without end, as GMP and Python see
 * them: and, or, exclusive or and not; shifts (a right shift is a floor
 * division by a power of two); the bit length and the count of one bits of
 * the magnitude; modular powers; the digits in a base from 2 to 36.
 *
 * An operation returns its result or refuses; a refusal names what the
 * operation takes. */

typedef struct {
    const char *expected; /* a call outside the signature: what it takes */
} CettaIntegerRefusal;

typedef Atom *(*CettaIntegerNativeFn)(Arena *a, Atom *const *args,
                                      CettaIntegerRefusal *refusal);

typedef struct {
    const char *name;   /* the lib/integer name without its integer: prefix */
    uint32_t arity;
    CettaIntegerNativeFn fn;
} CettaIntegerNative;

/* The operation lib/integer calls `name`, without the integer: prefix, or NULL. */
const CettaIntegerNative *cetta_integer_native(const char *name);

#endif
