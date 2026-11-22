/* mgm_instance.h - the Megalodon kernel module compiled by rulec, as a CeTTa native builtin module: the
 * instance mgm_instance.c of the generic host glue (native/rule_module/rcm_cetta.c).  Registration
 * (native/native_modules.c, g_native_modules[]):
 *
 *     {"mgmodule", CETTA_NATIVE_IMPORT_MGM, NULL, "__cetta_lib_mgm_", mgm_module_call, NULL},
 *
 * with lib/mgmodule.metta giving the public names (mgm:...). */
#ifndef CETTA_MGM_INSTANCE_H
#define CETTA_MGM_INSTANCE_H

#include "atom.h"
#include "space.h"

#include <stdint.h>

struct CettaLibraryContext;

Atom *cetta_mgm_dispatch(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head, Atom **args,
                         uint32_t nargs);

#endif
