/* rcm_cetta.h - a rule module compiled by rulec, as a CeTTa native builtin module (rcm_cetta.c).
 * Each compiled module is an instance with its own dispatch function; registration
 * (native/native_modules.c, g_native_modules[]), for the Megalodon kernel module:
 *
 *     {"mgmodule", CETTA_NATIVE_IMPORT_MGM, NULL, "__cetta_lib_mgm_", mgm_module_call, NULL},
 *
 * with lib/mgmodule.metta giving the public names (mgm:...). */
#ifndef CETTA_RCM_CETTA_H
#define CETTA_RCM_CETTA_H

#include "atom.h"
#include "space.h"

#include <stdint.h>

struct CettaLibraryContext;

/* the Megalodon kernel module (native/megalodon_module) */
Atom *cetta_mgm_dispatch(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head,
                         Atom **args, uint32_t nargs);

#endif
