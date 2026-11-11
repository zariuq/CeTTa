#ifndef CETTA_LIBRARY_PRESENTATION_H
#define CETTA_LIBRARY_PRESENTATION_H

#include "space.h"
#include <stdbool.h>
#include <stddef.h>

/* Whether the live definitions in `program` are exactly a library's, so that
 * a native realization of the library's operations may answer for them: for
 * each head the file at `path` defines, its equations and its declarations,
 * in file order, are the live ones, and none of the library's data
 * constructors has an equation.  The file is read with the lane's own
 * reader, as an import of it reads it into the program's universe.  With
 * `trace_tag`, a difference is reported on stderr under that tag. */
bool cetta_library_presentation_exact(Space *program, const char *path,
                                      const char *const *constructors,
                                      size_t nconstructors,
                                      const char *trace_tag);

#endif
