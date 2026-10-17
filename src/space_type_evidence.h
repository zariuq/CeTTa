#ifndef CETTA_SPACE_TYPE_EVIDENCE_H
#define CETTA_SPACE_TYPE_EVIDENCE_H

#include <stdbool.h>

#include "space.h"

/* Every type annotation of `s` has a symbol subject, no subject is declared
 * twice and no equation can extend get-type.  The intrinsic type relation of
 * `s` then has at most one answer in every mode, and exactly one for a fresh
 * requirement.  The answer describes the program projection; callers hold it
 * under the space's program token. */
bool space_type_annotations_functional(Space *s);

/* Visit each arrow declaration whose subject is a symbol, with the number of
 * declarations indexed under that symbol, until `visit` returns false.
 * Returns false, having visited nothing, when this view's index cannot
 * answer; the caller must then assume any declaration may exist. */
bool space_type_arrow_declarations_visit(
    Space *s,
    bool (*visit)(void *context, SymbolId subject, uint32_t declarations,
                  Atom *arrow),
    void *context);

#endif
