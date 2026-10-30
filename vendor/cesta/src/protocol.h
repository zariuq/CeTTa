#ifndef CESTA_PROTOCOL_H
#define CESTA_PROTOCOL_H

#include "cesta.h"

/* Structured native protocols: checked control flow and explicit ownership. */
int ce_protocol_command(int argc, char **argv);
bool ce_protocol_expand(CeArena *arena, const char *path, const char *theory,
                        CeText *text, CeText **dependencies, size_t *ndependencies,
                        FILE *plan, CeError *error);

#endif
