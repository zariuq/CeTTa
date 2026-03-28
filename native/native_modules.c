#include "native/native_modules.h"

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name) {
    (void)name;
    return NULL;
}

Atom *cetta_native_module_dispatch_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                          uint32_t active_mask) {
    (void)ctx;
    (void)space;
    (void)a;
    (void)head;
    (void)args;
    (void)nargs;
    (void)active_mask;
    return NULL;
}
