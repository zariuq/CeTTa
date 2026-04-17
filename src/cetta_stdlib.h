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

#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
/* Test-only helper: load caller-supplied stdlib-format bytes through the same
   runtime deserializer path used by stdlib_load(). */
bool stdlib_load_blob_bytes(Space *s, Arena *a, const unsigned char *data,
                            uint32_t len);
#endif

#endif /* CETTA_STDLIB_H */
