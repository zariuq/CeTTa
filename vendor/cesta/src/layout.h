#ifndef CESTA_LAYOUT_H
#define CESTA_LAYOUT_H

#include "cesta.h"

/* A layout presents a value, a list of named fields, in one machine word.
   The word's top bits choose a case through a prefix code; each case places
   every field in a slot of the bits below the prefix, storing the value minus
   an offset, and admits the values within its ranges. A reserved case admits
   none. Fields carry a kind, which says how a derived word treats them; the
   word carries a write discipline. */

typedef struct CeLayouts CeLayouts;

CeLayouts *ce_layouts_new(CeError *error);
void ce_layouts_free(CeLayouts *layouts);
/* Read every layout a theory of a module declares, checking each. */
bool ce_layouts_load(CeLayouts *layouts, const char *path, const char *theory, CeError *error);
size_t ce_layouts_count(const CeLayouts *layouts);
/* The checked layout in the normalized form the Lean admission decodes. */
void ce_layout_write(const CeLayouts *layouts, size_t index, FILE *file);
/* The layout's accessors as recorded expressions, for the Lean admission. */
void ce_layout_functions(const CeLayouts *layouts, size_t index, FILE *file);
/* A C header with every layout's accessors, printed from the same expressions. */
bool ce_layout_header(const CeLayouts *layouts, const char *guard, FILE *file, CeError *error);
int ce_layout_command(int argc, char **argv);

#endif
