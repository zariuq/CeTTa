#ifndef CESTA_EXPORT_H
#define CESTA_EXPORT_H

#include "cesta.h"

/* Render a cesta-export/1 specification as one ordinary C header. With plan
   set, each exported definition's record is written there. */
bool ce_export_header(const CeGrammar *grammar, CeArena *arena, const char *spec, FILE *plan,
                      CeText *out, CeError *error);
int ce_export_command(int argc, char **argv);

#endif
