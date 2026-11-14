/* holm_cetta_module.h - the HOL Light kernel module, compiled by rulec, as a CeTTa native builtin
 * module.  Registration (native/native_modules.c, g_native_modules[]):
 *
 *     {"holmodule", CETTA_NATIVE_IMPORT_HOLM, NULL, "__cetta_lib_holm_", holm_module_call, NULL},
 *
 * with lib/holmodule.metta giving the public names (holm:...). */
#ifndef CETTA_HOLM_MODULE_H
#define CETTA_HOLM_MODULE_H

#include "atom.h"
#include "space.h"

#include <stdint.h>

struct CettaLibraryContext;

Atom *cetta_holm_dispatch(struct CettaLibraryContext *ctx, Space *space, Arena *a, Atom *head,
                          Atom **args, uint32_t nargs);

#endif
