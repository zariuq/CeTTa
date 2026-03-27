#include "native/native_modules.h"

#include "native/metamath/module.h"
#include <string.h>

static bool native_ascii_ieq(const char *lhs, const char *rhs) {
    while (*lhs && *rhs) {
        unsigned char a = (unsigned char)*lhs;
        unsigned char b = (unsigned char)*rhs;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) return false;
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static const CettaNativeBuiltinModule CETTA_NATIVE_MODULES[] = {
    {
        "metamath",
        CETTA_METAMATH_NATIVE_IMPORT_BIT,
        cetta_metamath_native_dispatch,
    },
};

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name) {
    size_t count = sizeof(CETTA_NATIVE_MODULES) / sizeof(CETTA_NATIVE_MODULES[0]);
    for (size_t i = 0; i < count; i++) {
        if (native_ascii_ieq(CETTA_NATIVE_MODULES[i].name, name)) {
            return &CETTA_NATIVE_MODULES[i];
        }
    }
    return NULL;
}

Atom *cetta_native_module_dispatch_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                          uint32_t active_mask) {
    size_t count = sizeof(CETTA_NATIVE_MODULES) / sizeof(CETTA_NATIVE_MODULES[0]);
    for (size_t i = 0; i < count; i++) {
        if ((active_mask & CETTA_NATIVE_MODULES[i].import_bit) == 0) continue;
        Atom *result = CETTA_NATIVE_MODULES[i].dispatch(ctx, space, a, head, args, nargs);
        if (result) return result;
    }
    return NULL;
}
