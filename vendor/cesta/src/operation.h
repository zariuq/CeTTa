#ifndef CESTA_OPERATION_H
#define CESTA_OPERATION_H

#include "cesta.h"

/* An operation contract declares a native operation's facts once: its call
   forms with each argument's evaluation, demand and ownership; the footprint
   of its head (the resources it reads and writes); the context it needs
   (blocking, branching, progress, cost charges); the columns a runtime keeps
   for other reasons (static dispatch, type-level conversion, speculation,
   replay); and its profiles. A vocabulary names the resources, charges and
   profiles a table may use. */

typedef struct CeOperations CeOperations;

CeOperations *ce_operations_new(CeError *error);
void ce_operations_free(CeOperations *operations);
/* Read the vocabulary and every operation a theory of a module declares, checking each. */
bool ce_operations_load(CeOperations *operations, const char *path, const char *theory, CeError *error);
/* The checked table in the normalized form the Lean admission decodes. */
void ce_operations_write(const CeOperations *operations, FILE *file);
int ce_operation_command(int argc, char **argv);

#endif
