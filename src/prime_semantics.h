#ifndef CETTA_PRIME_SEMANTICS_H
#define CETTA_PRIME_SEMANTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "atom.h"
#include "space.h"

/* MeTTa-Prime is a language package, not an HE profile.  The weak hooks keep
 * standalone evaluator test binaries linkable when this module is omitted. */
Atom *prime_semantics_dispatch(Arena *a, Atom *head, Atom **args,
                               uint32_t nargs) __attribute__((weak));
bool prime_semantics_is_op(const char *name) __attribute__((weak));
bool prime_semantics_is_op_id(SymbolId id) __attribute__((weak));
bool prime_semantics_op_data_arg(const char *name, uint32_t arg_index)
    __attribute__((weak));

/* Internal package boundary used by the Prime gate.  Construction returns
 * NULL if the resulting schema does not validate. */
Atom *prime_semantics_package_atom(Arena *a);
bool prime_semantics_validate_package(Atom *package);
/* Neutral named-telescope elaboration.  A non-NULL result is a closed
 * canonical Pi/Var ABT, not evidence that the source type is well formed. */
Atom *prime_semantics_canonicalize_type(Arena *a, Atom *type);
bool prime_semantics_replay_conversion_certificate(
    Arena *a, Space *space, Atom *certificate, bool *equal_out);

#endif /* CETTA_PRIME_SEMANTICS_H */
