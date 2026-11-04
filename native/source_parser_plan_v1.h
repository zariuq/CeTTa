#ifndef CETTA_SOURCE_PARSER_PLAN_V1_H
#define CETTA_SOURCE_PARSER_PLAN_V1_H
#include "src/atom.h"
struct CettaLibraryContext;
Atom *cetta_source_parser_plan_dispatch_v1(struct CettaLibraryContext *, Arena *, Atom *, Atom **, uint32_t);
bool cetta_source_parser_plan_matches_v1(struct CettaLibraryContext *,Atom *,Atom *);
#endif
