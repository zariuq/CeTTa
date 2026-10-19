/* mm0_cetta_module.h - the native MM0 verifier as a CeTTa native builtin
 * module.  Registration (native/native_modules.c, g_native_modules[]):
 *
 *     {"mm0kernel", CETTA_NATIVE_IMPORT_MM0, NULL, "__cetta_lib_mm0_", ...},
 *
 * with lib/mm0kernel.metta giving the public names (mm0:...).  See
 * docs/mm0_native.txt for the operations and the ownership contract. */
#ifndef CETTA_MM0_CETTA_MODULE_H
#define CETTA_MM0_CETTA_MODULE_H

#include "atom.h"
#include "space.h"

#include <stdint.h>

struct CettaLibraryContext;

Atom *cetta_mm0_dispatch(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head,
                         Atom **args, uint32_t nargs);

#endif
