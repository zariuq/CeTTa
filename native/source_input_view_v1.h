#ifndef CETTA_SOURCE_INPUT_VIEW_V1_H
#define CETTA_SOURCE_INPUT_VIEW_V1_H
#include "src/atom.h"
struct CettaLibraryContext;
Atom *cetta_source_input_view_dispatch_v1(struct CettaLibraryContext *,Arena *,Atom *,Atom **,uint32_t);
#endif
