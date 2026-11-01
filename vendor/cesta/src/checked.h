#ifndef CESTA_CHECKED_H
#define CESTA_CHECKED_H

#include "native.h"

bool ce_checked_document(const CeAtom *atom);
bool ce_checked_text(CeArena *arena, CeText source, const char *identity,
                     bool lines, FILE *plan, CeNativeOutput *out, CeError *error);
int ce_checked_command(int argc, char **argv);

#endif
