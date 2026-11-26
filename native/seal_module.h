#ifndef CETTA_NATIVE_SEAL_MODULE_H
#define CETTA_NATIVE_SEAL_MODULE_H

#include "atom.h"
#include "import_mask.h"
#include "call_outcome.h"
#include "space.h"

#include <stdbool.h>
#include <stdint.h>

struct CettaLibraryContext;

#define CETTA_NATIVE_IMPORT_SEAL CETTA_NATIVE_IMPORT_BIT(11)

/* The `seal` library's natives, heads `__cetta_lib_seal_*` (lib/seal.metta).
 * False when `head` is not one of them. */
bool cetta_seal_module_call(struct CettaLibraryContext *ctx, Space *space,
                            Arena *arena, Atom *head, Atom **args,
                            uint32_t nargs, CettaCallOutcome *out);

#endif /* CETTA_NATIVE_SEAL_MODULE_H */
