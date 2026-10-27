#include "native/native_modules.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define check(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"import catalog check failed at line %d: %s\n",__LINE__,#test); \
    return EXIT_FAILURE; } } while (0)

int main(void) {
    const char *names[]={"gparse","langdef","pat","hol","set","holkernel",
        "mm0kernel","seal","megalodon","supervise"};
    CettaLibraryMask native=UINT64_C(1)<<34; /* Reserved optional crypto. */
    check(cetta_native_module_catalog_valid(0));
    for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) {
        const CettaNativeBuiltinModule *module=cetta_native_module_lookup(names[i]);
        if (!module) continue; /* Optional modules need not be compiled. */
        check(module->import_bit && !(module->import_bit & (module->import_bit-1)));
        check(!(native & module->import_bit));
        check(!cetta_native_module_catalog_valid(module->import_bit));
        native |= module->import_bit;
    }
    for (unsigned bit=0;bit<64;++bit) {
        CettaLibraryMask candidate=UINT64_C(1)<<bit;
        check(cetta_native_module_catalog_valid(candidate)==!(native & candidate));
    }
    check(cetta_native_module_catalog_valid(((UINT64_C(1)<<25)-1)|(UINT64_C(1)<<36)));
    check(!cetta_native_module_catalog_valid(((UINT64_C(1)<<25)-1)|(UINT64_C(1)<<25)));
    printf("library import catalog: %u checks passed\n",checks);
    return EXIT_SUCCESS;
}
