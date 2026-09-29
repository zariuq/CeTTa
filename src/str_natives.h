#ifndef CETTA_STR_NATIVES_H
#define CETTA_STR_NATIVES_H

#include "atom.h"

#include <stddef.h>
#include <stdint.h>

/* The operations of lib/str over atoms.  Ordinary programs reach them through
 * lib/str and prepared equation runners call them as operators, under the same
 * str: names, so a str: call means one thing wherever it runs.
 *
 * A text argument is a string or a symbol's spelling.  An operation returns its
 * result or refuses.  A refusal names its reason and the byte offset or item
 * index it concerns; a call outside an operation's signature names what the
 * operation takes.  Each host reports refusals in its own way.  Predicates
 * answer with atom_true and atom_false. */

typedef struct {
    const char *reason;   /* a refusal, such as StrMalformedUtf8V1 */
    size_t offset;        /* the byte offset or item index it concerns */
    const char *expected; /* a call outside the signature: what it takes */
} CettaStrRefusal;

typedef Atom *(*CettaStrNativeFn)(Arena *a, Atom *const *args,
                                  CettaStrRefusal *refusal);

/* What an operation's value is: always an atom, or possibly a structure
 * (a list, a set, an expression). */
typedef enum {
    CETTA_STR_RESULT_ATOM = 0,
    CETTA_STR_RESULT_STRUCTURE
} CettaStrResultKind;

typedef struct {
    const char *name;   /* the lib/str name without its str: prefix */
    uint32_t arity;     /* at most CETTA_STR_NATIVE_MAX_ARITY */
    uint32_t sequences; /* bit i set: argument i is a list or expression */
    CettaStrNativeFn fn;
    CettaStrResultKind result;
} CettaStrNative;

#define CETTA_STR_NATIVE_MAX_ARITY 3u

/* The operation lib/str calls `name`, without the str: prefix, or NULL. */
const CettaStrNative *cetta_str_native(const char *name);

#endif
