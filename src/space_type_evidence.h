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

/* Whether every declared type has no variable on its head path, and so has
 * every arrow's range but those of the subjects EXEMPT names (under sealed
 * sorts, functions, whose ranges type no value).  With functional
 * annotations every ground value's one typing then has one head path. */
bool space_type_annotations_rigid(
    Space *s, bool (*exempt)(void *context, SymbolId subject), void *context);

/* Whether the sort SORT (with a parameter, as List, or without, as Nat) is
 * sealed-closed: NIL and CONS keep the sort's usual rows and are no
 * functions; every other symbol that is no function offers no instance of
 * the sort through any row (an arrow's range, or the row itself); every
 * function's rows other than arrows offer none either; every declaration
 * has a symbol subject.  Then every ground value the sealed relation types
 * once at the sort is a spine of NIL and CONS cells
 * (SealedSorts.sealed_spine_of_count, sealed_nat_spine_of_count). */
bool space_type_sort_sealed_closed(
    Space *s, SymbolId sort, bool parametric, SymbolId nil, SymbolId cons,
    bool (*function)(void *context, SymbolId subject), void *context);

/* Visit each arrow declaration whose subject is a symbol, with the number of
 * distinct types declared for that symbol (rows repeating one type count
 * once, as a call answers once for them), until `visit` returns false.
 * Returns false, having visited nothing, when this view's index cannot
 * answer; the caller must then assume any declaration may exist. */
bool space_type_arrow_declarations_visit(
    Space *s,
    bool (*visit)(void *context, SymbolId subject, uint32_t declarations,
                  Atom *arrow),
    void *context);

#endif
