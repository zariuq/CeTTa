#ifndef CETTA_REGEX_CLASS_SOURCE_V1_H
#define CETTA_REGEX_CLASS_SOURCE_V1_H

#include "../src/atom.h"

typedef struct {
    Arena *arena;
    bool insensitive, unicode, bounded;
    uint32_t depth_limit;
    uint64_t *remaining;
} CettaReClassSourceOptionsV1;

/* Project an explicitly selected class CST. No source parsing or ambiguity
 * selection occurs here. A zero depth limit is unbounded. A bounded caller
 * supplies its shared work counter; each visited syntax node consumes one. */
Atom *cetta_re_v1_class_source(const CettaReClassSourceOptionsV1 *options,
                             Atom *tree, Atom **problem);

#endif
