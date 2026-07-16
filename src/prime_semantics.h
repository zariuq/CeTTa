#ifndef CETTA_PRIME_SEMANTICS_H
#define CETTA_PRIME_SEMANTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"

/* MeTTa-Prime is a language package, not an HE profile.  The weak hooks keep
 * standalone evaluator test binaries linkable when this module is omitted. */
Atom *prime_semantics_dispatch(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) __attribute__((weak));
bool prime_semantics_is_op(const char *name) __attribute__((weak));
bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

#endif /* CETTA_PRIME_SEMANTICS_H */
