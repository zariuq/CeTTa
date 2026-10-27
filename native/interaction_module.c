#include "native/interaction_module.h"
#include "native/interaction_core.h"

#include <string.h>

/* The `interaction` library's natives (lib/prime/interaction.metta): each
 * head __cetta_lib_interaction_OP is the engine's operation OP
 * (src/interaction_core.c). */

#define IA_HEAD_PREFIX "__cetta_lib_interaction_"

bool cetta_interaction_module_call(struct CettaLibraryContext *ctx,
                                   Space *space, Arena *a, Atom *head,
                                   Atom **args, uint32_t nargs,
                                   CettaCallOutcome *out) {
    (void)ctx;
    if (!head || head->kind != ATOM_SYMBOL || !a || !out)
        return false;
    const char *name = atom_name_cstr(head);
    size_t prefix = sizeof(IA_HEAD_PREFIX) - 1u;
    if (!name || strncmp(name, IA_HEAD_PREFIX, prefix) != 0)
        return false;
    return cetta_interaction_core_call(space, a, name + prefix, args, nargs, out);
}
