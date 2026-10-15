/* hol_cetta_module.h - the native HOL kernel as a CeTTa native builtin
 * module.  Registration (native/native_modules.c, g_native_modules[]):
 *
 *     {"holkernel", CETTA_NATIVE_IMPORT_HOL, ...},
 *
 * with lib/holkernel.metta giving the public names (hol:...). See
 * docs/hol_light.txt for the operations and ownership contract. */
#ifndef CETTA_HOL_MODULE_H
#define CETTA_HOL_MODULE_H

#include "atom.h"
#include "space.h"

#include <stdint.h>

struct CettaLibraryContext;

Atom *cetta_hol_dispatch(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head,
                         Atom **args, uint32_t nargs);

#endif
