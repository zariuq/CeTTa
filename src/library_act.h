#ifndef CETTA_LIBRARY_ACT_H
#define CETTA_LIBRARY_ACT_H
#include "library.h"
struct SourceValidation;
Atom *cetta_act_validation_owned_space(Arena *,struct SourceValidation *);
Atom *cetta_act_dispatch(CettaLibraryContext *, Arena *, Atom *, Atom **,
                         uint32_t);
#endif
