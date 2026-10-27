#ifndef CETTA_NATIVE_INTERACTION_MODULE_H
#define CETTA_NATIVE_INTERACTION_MODULE_H

#include "atom.h"
#include "call_outcome.h"
#include "space.h"

#include <stdbool.h>
#include <stdint.h>

struct CettaLibraryContext;

#define CETTA_NATIVE_IMPORT_INTERACTION (UINT64_C(1) << 36)

/* The `interaction` library's natives, heads `__cetta_lib_interaction_*`
 * (lib/prime/interaction.metta).  False when `head` is not one of them. */
bool cetta_interaction_module_call(struct CettaLibraryContext *ctx,
                                   Space *space, Arena *arena, Atom *head,
                                   Atom **args, uint32_t nargs,
                                   CettaCallOutcome *out);

#endif /* CETTA_NATIVE_INTERACTION_MODULE_H */
