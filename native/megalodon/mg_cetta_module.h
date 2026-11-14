#ifndef CETTA_MEGALODON_CETTA_MODULE_H
#define CETTA_MEGALODON_CETTA_MODULE_H

#include "atom.h"
#include "call_outcome.h"
#include "space.h"

#include <stdbool.h>
#include <stdint.h>

struct CettaLibraryContext;

/* Its import bit: native modules hold the upper half of the session's import
 * mask, each its own bit (native/native_modules.c checks it). */
#define CETTA_NATIVE_IMPORT_MEGALODON (1ull << 44)

/* The `megalodon` library's natives, heads `__cetta_lib_megalodon_*`
 * (lib/megalodon.metta).  False when `head` is not one of them. */
bool cetta_megalodon_module_call(struct CettaLibraryContext *ctx,
                                 Space *space, Arena *arena, Atom *head,
                                 Atom **args, uint32_t nargs,
                                 CettaCallOutcome *out);

#endif /* CETTA_MEGALODON_CETTA_MODULE_H */
