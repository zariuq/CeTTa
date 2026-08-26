#ifndef CETTA_LIBRARY_JSON_H
#define CETTA_LIBRARY_JSON_H

#include "atom.h"

#include <stddef.h>

typedef struct CettaJsonLibraryRuntimeV1 CettaJsonLibraryRuntimeV1;

CettaJsonLibraryRuntimeV1 *cetta_json_library_runtime_v1_new(
    char *error_buf,
    size_t error_buf_size);
void cetta_json_library_runtime_v1_free(CettaJsonLibraryRuntimeV1 *runtime);

Atom *cetta_json_library_dispatch_v1(
    CettaJsonLibraryRuntimeV1 *runtime,
    Arena *arena,
    Atom *head,
    Atom **args,
    uint32_t nargs);

#endif /* CETTA_LIBRARY_JSON_H */
