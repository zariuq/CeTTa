#ifndef CETTA_MEGALODON_CETTA_MODULE_H
#define CETTA_MEGALODON_CETTA_MODULE_H

#include "atom.h"
#include "call_outcome.h"
#include "space.h"

#include <stdbool.h>
#include <stdint.h>

struct CettaLibraryContext;

/* Bits 24-27 are taken at this base and 30 is lib_seal; 28 and 29 are left
 * to the HOL and MM0 drafts' own registrations. */
#define CETTA_NATIVE_IMPORT_MEGALODON (1u << 31)

/* The `megalodon` library's natives, heads `__cetta_lib_megalodon_*`
 * (lib/megalodon.metta).  False when `head` is not one of them. */
bool cetta_megalodon_module_call(struct CettaLibraryContext *ctx,
                                 Space *space, Arena *arena, Atom *head,
                                 Atom **args, uint32_t nargs,
                                 CettaCallOutcome *out);

#endif /* CETTA_MEGALODON_CETTA_MODULE_H */
