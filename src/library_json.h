#ifndef CETTA_LIBRARY_JSON_H
#define CETTA_LIBRARY_JSON_H

#include "atom.h"

#include <stddef.h>

typedef struct CettaJsonLibraryRuntimeV1 CettaJsonLibraryRuntimeV1;

CettaJsonLibraryRuntimeV1 *cetta_json_library_runtime_v1_new(
    char *error_buf,
    size_t error_buf_size);

/* Reference-parser preparation is lazy and mutex-protected; ordinary JSON
 * calls use immutable generated tables. Concurrent dispatch is safe with a
 * distinct Arena and output ownership per call. The caller must not free
 * the runtime while a dispatch is active. */
void cetta_json_library_runtime_v1_free(CettaJsonLibraryRuntimeV1 *runtime);

Atom *cetta_json_library_dispatch_v1(
    CettaJsonLibraryRuntimeV1 *runtime,
    Arena *arena,
    Atom *head,
    Atom **args,
    uint32_t nargs);

/* A pure prepared caller may borrow an immutable result from its arguments
 * for the duration of that prepared execution.  True means the operation was
 * handled; `*value_out` is either argument-owned or allocated in `arena`.
 * Ordinary callers use cetta_json_library_dispatch_v1 and receive an owned
 * value. */
bool cetta_json_library_borrowed_value_v1(
    CettaJsonLibraryRuntimeV1 *runtime,
    Arena *arena,
    Atom *head,
    Atom **args,
    uint32_t nargs,
    Atom **value_out);

#endif /* CETTA_LIBRARY_JSON_H */
