#ifndef CETTA_STDLIB_H
#define CETTA_STDLIB_H

#include "atom.h"
#include "space.h"

/* Deserialize precompiled stdlib blob into the space.
   Atoms are allocated in the provided arena. */
void stdlib_load(Space *s, Arena *a);

/* Serialize parsed atoms from a .metta file to a C header on stdout.
   Used by --compile-stdlib flag. Returns 0 on success, -1 on error. */
int stdlib_compile(const char *metta_path, Arena *a, FILE *out);

#endif /* CETTA_STDLIB_H */
