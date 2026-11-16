/* tcalc_cetta_module.h - tcalc as a module of MeTTa, (import! &self tcalc);
 * see tcalc_cetta_module.c and lib/tcalc.metta. */
#ifndef CETTA_TCALC_CETTA_MODULE_H
#define CETTA_TCALC_CETTA_MODULE_H

#include "atom.h"
#include "call_outcome.h"
#include "space.h"

#include <stdbool.h>
#include <stdint.h>

struct CettaLibraryContext;

/* The module's operations, heads __cetta_lib_tcalc_load, _outcomes, _step and
 * _query.  False for a head it does not know. */
bool cetta_tcalc_module_call(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                             Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out);

#endif
